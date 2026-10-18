/*
 * QRKude - scannability checks.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>

#include "qrk_internal.h"

static double channel(double v)
{
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

/* WCAG relative luminance of `c` composited over the opaque color `under`. */
static double luminance_over(qrk_color c, qrk_color under)
{
    double a = (c & 0xFF) / 255.0, l = 0;
    static const double weight[3] = {0.2126, 0.7152, 0.0722};
    for (int i = 0; i < 3; i++) {
        int shift = 24 - 8 * i;
        double v = ((c >> shift) & 0xFF) / 255.0, u = ((under >> shift) & 0xFF) / 255.0;
        l += weight[i] * channel(v * a + u * (1 - a));
    }
    return l;
}

/* The colors a paint can produce: its color or its stops. A missing paint
 * is fully transparent. */
static int paint_colors(const qrk_paint *p, qrk_color out[QRK_MAX_STOPS])
{
    if (p->type == QRK_PAINT_SOLID) {
        out[0] = p->color;
        return 1;
    }
    if ((p->type == QRK_PAINT_LINEAR || p->type == QRK_PAINT_RADIAL) && p->stop_count > 0) {
        int n = p->stop_count < QRK_MAX_STOPS ? p->stop_count : QRK_MAX_STOPS;
        for (int i = 0; i < n; i++)
            out[i] = p->stops[i].color;
        return n;
    }
    out[0] = QRK_TRANSPARENT;
    return 1;
}

static qrk_color opaque_over_white(qrk_color c)
{
    double a = (c & 0xFF) / 255.0;
    qrk_color out = 0xFF;
    for (int shift = 8; shift <= 24; shift += 8) {
        double v = ((c >> shift) & 0xFF) * a + 255 * (1 - a);
        out |= (qrk_color)(int)(v + 0.5) << shift;
    }
    return out;
}

uint32_t qrk_lint(const qrk_code *code, const qrk_style *style)
{
    uint32_t w = 0;
    qrk__layout L;
    qrk__layout_init(&L, code, style);

    if (L.has_logo) {
        double d = qrk_logo_damage(code, style);
        if (d > 1)
            w |= QRK_LINT_LOGO_UNREADABLE;
        else if (d > 0.7)
            w |= QRK_LINT_LOGO_RISKY;
    }

    /* background colors, made opaque over white (what a page usually shows) */
    qrk_color bg[QRK_MAX_STOPS];
    int nbg = paint_colors(&style->background.paint, bg);
    bool transparent = true;
    for (int i = 0; i < nbg; i++) {
        if (bg[i] & 0xFF)
            transparent = false;
        bg[i] = opaque_over_white(bg[i]);
    }
    if (transparent)
        w |= QRK_LINT_TRANSPARENT_BACKGROUND;
    double bg_lo = 1, bg_hi = 0;
    for (int i = 0; i < nbg; i++) {
        double l = luminance_over(bg[i], bg[i]);
        if (l < bg_lo) bg_lo = l;
        if (l > bg_hi) bg_hi = l;
    }

    /* foreground: modules and eyes composited over every background color */
    double fg_lo = 1, fg_hi = 0;
    for (int layer = QRK_LAYER_MODULES; layer <= QRK_LAYER_EYE_BALLS; layer++) {
        qrk_paint p = qrk__resolve_paint(style, layer);
        qrk_color fg[QRK_MAX_STOPS];
        int nfg = paint_colors(&p, fg);
        bool visible = false;
        for (int i = 0; i < nfg; i++) {
            if (fg[i] & 0xFF)
                visible = true;
            for (int j = 0; j < nbg; j++) {
                double l = luminance_over(fg[i], bg[j]);
                if (l < fg_lo) fg_lo = l;
                if (l > fg_hi) fg_hi = l;
            }
        }
        if (!visible)
            w |= QRK_LINT_INVISIBLE;
    }
    double contrast;
    if ((fg_lo + fg_hi) / 2 > (bg_lo + bg_hi) / 2) {
        w |= QRK_LINT_INVERTED;
        contrast = (fg_lo + 0.05) / (bg_hi + 0.05);
    } else {
        contrast = (bg_lo + 0.05) / (fg_hi + 0.05);
    }
    if (contrast < 3)
        w |= QRK_LINT_LOW_CONTRAST;

    if (L.margin < 2)
        w |= QRK_LINT_QUIET_ZONE;

    switch (L.shape) {
    case QRK_MODULE_SQUARE: case QRK_MODULE_DOT: case QRK_MODULE_ROUNDED:
    case QRK_MODULE_DIAMOND: case QRK_MODULE_STAR: case QRK_MODULE_HBARS: case QRK_MODULE_VBARS:
        if (L.size < 0.6)
            w |= QRK_LINT_SMALL_MODULES;
        break;
    default:
        break;
    }

    /* Measured with zxing: diamond frames break finder detection with any
     * ball; diamond balls only work inside round or beveled frames. */
    int frame = style->eyes.frame, ball = qrk__ball_shape(style->eyes.ball);
    if (frame == QRK_EYE_DIAMOND ||
        (ball == QRK_EYE_DIAMOND && (frame == QRK_EYE_SQUARE || frame == QRK_EYE_LEAF)))
        w |= QRK_LINT_EYE_SHAPES;
    return w;
}

const char *qrk_lint_name(uint32_t flag)
{
    switch (flag) {
    case QRK_LINT_LOGO_RISKY: return "logo-risky";
    case QRK_LINT_LOGO_UNREADABLE: return "logo-unreadable";
    case QRK_LINT_LOW_CONTRAST: return "low-contrast";
    case QRK_LINT_INVERTED: return "inverted";
    case QRK_LINT_TRANSPARENT_BACKGROUND: return "transparent-background";
    case QRK_LINT_QUIET_ZONE: return "quiet-zone";
    case QRK_LINT_SMALL_MODULES: return "small-modules";
    case QRK_LINT_EYE_SHAPES: return "eye-shapes";
    case QRK_LINT_INVISIBLE: return "invisible";
    default: return NULL;
    }
}
