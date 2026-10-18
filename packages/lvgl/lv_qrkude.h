/*
 * lv_qrkude - styled QR code widget for LVGL (v9, with v8.3 compatibility).
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The widget is an lv_canvas whose pixels are drawn by the QRKude core in the
 * display's color format:
 *
 *   lv_obj_t * qr = lv_qrkude_create(lv_screen_active());   // lv_scr_act() on v8
 *   lv_qrkude_set_size(qr, 180);
 *   lv_qrkude_set_style_json(qr, "{\"modules\":{\"shape\":\"liquid\"}}");
 *   lv_qrkude_update(qr, "https://lvgl.io", 15);
 *   lv_obj_center(qr);
 *
 * Words used below: a "module" is one square of the QR grid; the "version"
 * (1..40) sets the grid size (21x21 up to 177x177 modules); "ECC" (error
 * correction: L, M, Q, H) is how much of the code may be hidden, e.g. by a
 * logo, while it still scans. See docs/GUIDE.md.
 *
 * Memory, from LV_QRKUDE_MALLOC (lv_malloc by default): the payload copy,
 * the module grid (QRK_BUFFER_LEN_FOR_VERSION(LV_QRKUDE_MAX_VERSION) + 5
 * bytes, 3922 for version 40) and the pixel buffer (size x size x 2 bytes in
 * RGB565, so 80 KB for 200 px). The encoder's work area (same size as the
 * grid) is only allocated during an update. Everything is freed when the
 * object is deleted.
 *
 * Compile-time options (define them in lv_conf.h, or as compiler flags for
 * lv_qrkude.c):
 *   LV_QRKUDE_MAX_VERSION    largest version (default QRK_MAX_VERSION, 40)
 *   LV_QRKUDE_MALLOC(size) / LV_QRKUDE_FREE(ptr)
 *                            allocator (default lv_malloc/lv_free, v8:
 *                            lv_mem_alloc/lv_mem_free), e.g. PSRAM on ESP32
 *   LV_QRKUDE_COLOR_FORMAT   LVGL 9 only: force LV_COLOR_FORMAT_RGB565,
 *                            _ARGB8888 or _L8 instead of the display's format
 * Requires LV_USE_CANVAS 1 (the default).
 */
#ifndef LV_QRKUDE_H
#define LV_QRKUDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "lvgl.h" is never probed with __has_include before being included: GCC < 10
 * then silently skips the #include (GCC bug 80753), and Arduino needs the
 * "missing header" error to discover the LVGL library. */
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(__has_include)
#if __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#else
#include "lvgl.h"
#endif
#else
#include "lvgl.h"
#endif

/* QRKude core. "core/qrkude.h" is the layout of the assembled Arduino
 * library, where the core header cannot sit next to QRKude.h. */
#if defined(__has_include)
#if __has_include("core/qrkude.h")
#include "core/qrkude.h"
#else
#include "qrkude.h"
#endif
#else
#include "qrkude.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Largest QR version a widget can show (memory: see above). Longer data
 * makes lv_qrkude_update() return QRK_ERR_DATA_TOO_LONG. */
#ifndef LV_QRKUDE_MAX_VERSION
#define LV_QRKUDE_MAX_VERSION QRK_MAX_VERSION
#endif

/* Creates an empty QR code widget (an lv_canvas), 100 x 100 px. It shows
 * nothing until lv_qrkude_update(). Returns NULL when out of memory. Delete
 * it with lv_obj_delete() (lv_obj_del() on v8) like any object. */
lv_obj_t * lv_qrkude_create(lv_obj_t * parent);

/* Side length in pixels (the widget is square). The code is scaled to fit
 * and centered; with the default pixel snapping some border may remain.
 * Re-renders when a code is set. */
void lv_qrkude_set_size(lv_obj_t * obj, int32_t size);

/* Copies a style (qrk_style in qrkude.h, docs/STYLES.md). Strings it points
 * to (label.text, logo.href) are not copied and must stay valid. Re-encodes
 * and redraws: a logo raises the automatic error correction to HIGH. */
void lv_qrkude_set_style(lv_obj_t * obj, const qrk_style * style);

/* Replaces the style and the encode options with the defaults overridden by
 * a JSON document ({"encode":..,"style":..} or a bare style object, see
 * docs/STYLES.md). Strings are copied. Returns QRK_OK, QRK_ERR_PARSE (bad
 * JSON, unknown names, nesting deeper than 64, numbers longer than 400
 * characters) or QRK_ERR_OUT_OF_MEMORY; on error nothing changes. */
qrk_status lv_qrkude_set_style_json(lv_obj_t * obj, const char * json);

/* The widget's own style, for changing single fields in place; call
 * lv_qrkude_refresh() afterwards. NULL if obj is not a QRKude widget. */
qrk_style * lv_qrkude_get_style(lv_obj_t * obj);

/* Encodes `len` bytes of text or binary data (copied) and draws them.
 * Returns QRK_OK, QRK_ERR_DATA_TOO_LONG, QRK_ERR_OUT_OF_MEMORY or
 * QRK_ERR_INVALID_ARGUMENT; on error the previous code stays visible. */
qrk_status lv_qrkude_update(lv_obj_t * obj, const void * data, size_t len);

/* Bitmap drawn in the middle of the code (qrk_image, e.g. made with
 * img2qrk.py). It needs a logo area: style.logo.size is set to 0.2 (20% of
 * the width) if it is 0. Not copied: it must stay valid. NULL removes the
 * bitmap (the area stays until logo.size is set back to 0). Re-encodes. */
void lv_qrkude_set_logo(lv_obj_t * obj, const qrk_image * logo);

/* Color behind transparent parts of the style and around the code when it
 * does not fill the widget (default opaque white, 0xRRGGBBAA). Use the
 * parent's color, or QRK_TRANSPARENT with an ARGB8888 canvas. */
void lv_qrkude_set_clear_color(lv_obj_t * obj, qrk_color color);

/* Smoothing: samples per pixel and axis, 1 (off) to 4, default 2; and
 * integer_scale: draw every module with the same whole number of pixels
 * (default true, sharpest and easiest to scan). */
void lv_qrkude_set_antialias(lv_obj_t * obj, int supersample, bool integer_scale);

/* Re-encodes and redraws with the current style, e.g. after changing
 * fields through lv_qrkude_get_style(). */
qrk_status lv_qrkude_refresh(lv_obj_t * obj);

/* The current code for the C API (version, size, qrk_lint...), or NULL
 * before the first successful update. */
const qrk_code * lv_qrkude_get_code(const lv_obj_t * obj);

#ifdef __cplusplus
}
#endif

#endif /* LV_QRKUDE_H */
