/*
 * QRKude Qt - QPainter renderer.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef QRKUDERENDERER_H
#define QRKUDERENDERER_H

#include <QtCore/QRectF>
#include <QtCore/QStringList>
#include <QtGui/QBrush>
#include <QtGui/QFont>
#include <QtGui/QImage>
#include <QtGui/QPainterPath>
#include <QtGui/QTransform>

#include <array>
#include <optional>

#include "qrkudecode.h"
#include "qrkudestyle.h"

class QPainter;

namespace QRKude {

/* Where things are on the canvas. All values are in modules ("canvas
 * units"): multiply by the scale you draw at. */
struct Layout
{
    QSizeF canvas;        /* whole canvas: code, quiet zone, border and label band */
    QPointF symbolOrigin; /* top-left corner of module (0, 0) */
    int modules = 0;      /* modules per side */
    bool hasLogo = false;
    QRectF logoBox;       /* square box the logo image is fitted into */
    bool hasLabel = false;
    QPointF labelAnchor;  /* x = center of the text, y = its baseline */
    double labelSize = 0; /* font size, after clamping (as in the SVG) */
};

/*
 * Draws a Code with a Style: on any QPainter, into a QImage, or as SVG.
 * The shapes come from the C core, so they match the SVG output and every
 * other QRKude port.
 *
 * Paths and brushes are in modules; paint() scales them into the target
 * rectangle and keeps the aspect ratio. A Renderer is a value type, cheap to
 * copy. It caches its paths on first use, so do not use one instance from
 * several threads at once; give each thread its own copy.
 */
class Renderer
{
public:
    Renderer(); /* invalid until a valid code is set */
    Renderer(const Code &code, const Style &style);
    /* Encodes `text` (see Code::encode); check isValid() afterwards. */
    explicit Renderer(const QString &text, const Style &style = Style(),
                      const EncodeOptions &options = EncodeOptions());

    const Code &code() const { return m_code; }
    void setCode(const Code &code);
    const Style &style() const { return m_style; }
    void setStyle(const Style &style);

    /* false when the code is null (nothing is drawn, toImage() and toSvg()
     * return empty results). */
    bool isValid() const { return m_code.isValid(); }

    Layout layout() const;
    QSizeF canvasSize() const; /* layout().canvas */

    /* The shapes of one layer, in modules, with fill rule Qt::WindingFill.
     * Every shape is a closed sub-path; holes run the other way round.
     * Empty when the layer has nothing to draw (but Background always has
     * its rectangle: check layerBrush()). */
    QPainterPath layerPath(Layer layer) const;
    /* How to fill one layer: a color or a QLinearGradient/QRadialGradient in
     * modules. Qt::NoBrush when the layer is invisible (no paint or fully
     * transparent). */
    QBrush layerBrush(Layer layer) const;
    /* The label font from the style, at `pixelSize` pixels. */
    QFont labelFont(int pixelSize) const;

    /* The largest rectangle with the canvas aspect ratio, centered in
     * `target`: where paint() draws. */
    QRectF fittedRect(const QRectF &target) const;
    /* Maps modules to `target` coordinates, as paint() does. Use it to draw
     * layerPath() yourself. */
    QTransform canvasTransform(const QRectF &target) const;

    /* Draws the whole code into `target`, centered, keeping the aspect
     * ratio, with antialiasing; respects the painter's current transform.
     * Order: the layers, the logo image (scaled to fit its box), the label.
     * Without `logo`, the logo is loaded from Style::logoHref() if possible
     * (data: URI, local file, Qt resource). The painter state is restored. */
    void paint(QPainter &painter, const QRectF &target, const QImage &logo = QImage()) const;
    /* The code as an image `width` pixels wide; the height follows the
     * aspect ratio (taller with a label). Format ARGB32_Premultiplied; areas
     * without background stay transparent. Null when invalid or width <= 0. */
    QImage toImage(int width, const QImage &logo = QImage()) const;
    /* The code as an SVG document (UTF-8), written by the C core: the same
     * bytes in every QRKude port. `widthPx` sets the width attribute
     * (0 = none). The logo is referenced by Style::logoHref(); a logo given
     * only as a QImage is not included. */
    QByteArray toSvg(double widthPx = 0) const;
    QByteArray toSvg(const SvgOptions &options) const;

    /* Scannability warnings, e.g. ("logo-risky", "low-contrast"); empty when
     * nothing was found. See Lint::Flag and docs/GUIDE.md#testing-your-codes. */
    QStringList lint() const;
    quint32 lintFlags() const; /* the same as Lint::Flag bits */
    /* How much of the error correction the logo uses up, worst case: 0 = no
     * logo, 1 = all of it (the code cannot be read above 1). Keep it below
     * about 0.6. See docs/GUIDE.md#logos. */
    double logoDamage() const;

    /* The image loaded from Style::logoHref() (cached), or a null image. */
    QImage styleLogo() const;
    /* Loads an image from a data: URI, a file path or file: URL, or a Qt
     * resource (":/..." or "qrc:/..."). Returns a null image for anything
     * else, including http(s) URLs. */
    static QImage loadImage(const QString &href);

private:
    void invalidate();

    Code m_code;
    Style m_style;
    mutable std::array<std::optional<QPainterPath>, LayerCount> m_paths;
    mutable std::optional<QImage> m_logo;
};

} // namespace QRKude

#endif // QRKUDERENDERER_H
