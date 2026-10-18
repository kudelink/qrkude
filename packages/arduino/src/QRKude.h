/*
 * QRKude for Arduino - styled QR codes on microcontrollers.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * A C++11 wrapper around the QRKude C core (qrkude.h, whose qrk_* types and
 * functions are all available too):
 *
 *   QRKude qr;                                // nothing is allocated yet
 *   qr.setStyleJson("{\"modules\":{\"shape\":\"fluid\"}}");
 *   qr.encode("https://example.com");         // allocates its buffer once
 *   qr.draw(tft, 10, 10, 200);                // any GFX-like color display
 *   qr.drawMono(oled, 0, 0, 64);              // 1-bit displays (SSD1306, U8g2)
 *   qr.printSvg(Serial);                      // SVG streamed to any Print
 *   qr.printAscii(Serial);                    // quick look in a serial monitor
 *
 * Words used below: a "module" is one square of the QR grid; the "version"
 * (1..40) sets the grid size (21x21 up to 177x177 modules) and so how much
 * data fits; "ECC" (error correction: L, M, Q, H) is the share of the code
 * that may be damaged or hidden while it still scans. See docs/GUIDE.md.
 *
 * Memory: the module grid and the encoder's work area take
 * QRKUDE_BUFFER_SIZE(maxVersion) bytes of heap, allocated by the first
 * encode(): 7841 bytes for version 40, 819 for version 10. Pass a smaller
 * maxVersion to the constructor, or your own buffer, on small boards.
 */
#ifndef QRKUDE_ARDUINO_H
#define QRKUDE_ARDUINO_H

#include <stddef.h>
#include <stdint.h>

/* The C core header lives in src/core/ in the assembled library. It is not
 * placed next to this file because "qrkude.h" and "QRKude.h" collide on
 * case-insensitive file systems. */
#if defined(__has_include)
#if __has_include("core/qrkude.h")
#include "core/qrkude.h" /* assembled library (tools/package.py) */
#elif __has_include("../../../core/include/qrkude.h")
#include "../../../core/include/qrkude.h" /* QRKude monorepo checkout */
#else
#include <qrkude.h> /* core installed elsewhere on the include path */
#endif
#else
#include "core/qrkude.h"
#endif
#ifndef QRKUDE_H
#error "QRKude: the C core header (qrkude.h) was not found"
#endif

#if defined(ARDUINO)
#include <Arduino.h>
#define QRKUDE_HAS_PRINT 1
#endif

/* Largest version a QRKude object accepts when the constructor gets no
 * argument: QRK_MAX_VERSION (40) in general, 4 on AVR (33x33 modules, up to
 * 62 bytes of text at ECC M, 281 bytes of RAM). Define it before including
 * QRKude.h, or as a build flag, to change it. */
#ifndef QRKUDE_DEFAULT_MAX_VERSION
#if defined(__AVR__)
#define QRKUDE_DEFAULT_MAX_VERSION 4
#else
#define QRKUDE_DEFAULT_MAX_VERSION QRK_MAX_VERSION
#endif
#endif

/* Bytes a QRKude object needs to handle codes up to `version`: the module
 * grid plus the encoder's work area. A constant expression, so it can size a
 * static buffer for QRKude(void *buffer, size_t size):
 *   static uint8_t qrbuf[QRKUDE_BUFFER_SIZE(4)];   // 281 bytes */
#define QRKUDE_BUFFER_SIZE(version)                                                 \
    ((((size_t)offsetof(qrk_code, modules) + QRK_BUFFER_LEN_FOR_VERSION(version) + 3) & \
      ~(size_t)3) +                                                                 \
     (size_t)QRK_BUFFER_LEN_FOR_VERSION(version))

/* printSvg() and printAscii() collect output in a stack buffer of this many
 * bytes and pass it to Print::write() in chunks (fewer, larger writes are
 * much faster on Wi-Fi and USB). */
#ifndef QRKUDE_PRINT_CHUNK
#if defined(__AVR__)
#define QRKUDE_PRINT_CHUNK 32
#else
#define QRKUDE_PRINT_CHUNK 128
#endif
#endif

namespace qrkude_detail {

/* Overload priority tags: p0 is tried first, then p1, then p2. */
struct p2 {};
struct p1 : p2 {};
struct p0 : p1 {};

/* Horizontal line on a GFX-like display. writeFastHLine is preferred (no
 * per-call transaction on Adafruit_GFX, LovyanGFX and Arduino_GFX), then
 * drawFastHLine (TFT_eSPI, Adafruit_SSD1306...), then the U8g2 API. The
 * color is passed as uint16_t, which LovyanGFX interprets as RGB565. */
template <typename D>
inline auto hline(D &d, int16_t x, int16_t y, int16_t w, uint16_t c, p0)
    -> decltype(d.writeFastHLine(x, y, w, c), void())
{
    d.writeFastHLine(x, y, w, c);
}
template <typename D>
inline auto hline(D &d, int16_t x, int16_t y, int16_t w, uint16_t c, p1)
    -> decltype(d.drawFastHLine(x, y, w, c), void())
{
    d.drawFastHLine(x, y, w, c);
}
template <typename D>
inline auto hline(D &d, int16_t x, int16_t y, int16_t w, uint16_t c, p2)
    -> decltype(d.setDrawColor((uint8_t)c), d.drawHLine(x, y, w), void())
{
    if (x < 0) { /* U8g2 coordinates are unsigned */
        w = (int16_t)(w + x);
        x = 0;
    }
    if (y < 0 || w <= 0)
        return;
    d.setDrawColor((uint8_t)c);
    d.drawHLine(x, y, w);
}

template <typename D>
inline auto start_write(D &d, p0) -> decltype(d.startWrite(), void())
{
    d.startWrite();
}
template <typename D>
inline void start_write(D &, p1)
{
}
template <typename D>
inline auto end_write(D &d, p0) -> decltype(d.endWrite(), void())
{
    d.endWrite();
}
template <typename D>
inline void end_write(D &, p1)
{
}

} /* namespace qrkude_detail */

/* QRKude holds one QR code and its style. Typical use: set the style, call
 * encode(), then draw it (draw / drawMono), stream it (printSvg) or render
 * it (renderSpans / renderBuffer). Functions that can fail return false and
 * leave the reason in status() / errorString(). Not copyable. */
class QRKude {
public:
    /* Text renderings for printAscii() / writeAscii(). */
    enum AsciiStyle {
        ASCII_HALF_BLOCKS = 0, /* UTF-8 half blocks, 1 char per module, 2 rows per line */
        ASCII_FULL_BLOCKS = 1, /* UTF-8 full blocks, 2 chars per module                 */
        ASCII_HASH = 2         /* plain "##" / "  ", 2 chars per module                 */
    };

    /* Codes up to version `maxVersion` (1..QRK_MAX_VERSION). The first
     * encode() allocates QRKUDE_BUFFER_SIZE(maxVersion) bytes of heap, kept
     * until the object is destroyed. */
    explicit QRKude(uint8_t maxVersion = QRKUDE_DEFAULT_MAX_VERSION);
    /* Uses `buffer` (any alignment) instead of the heap for the code; it
     * must outlive the object. The largest version whose
     * QRKUDE_BUFFER_SIZE() fits in `size` is used; if none fits, encode()
     * fails with QRK_ERR_BUFFER_TOO_SMALL. setStyleJson() and the encodeXxx()
     * helpers still allocate temporary heap memory. */
    QRKude(void *buffer, size_t size);
    /* Frees the heap memory (never a caller-provided buffer). */
    ~QRKude();

    /* ---- style ----------------------------------------------------------- */

    /* The style (shapes, colors, logo area...) used by the next encode() and
     * by every output. Change its fields directly; see qrk_style in
     * qrkude.h and docs/STYLES.md. Strings it points to (label.text,
     * logo.href) must stay valid. */
    qrk_style &style() { return style_; }
    const qrk_style &style() const { return style_; }
    /* Back to the default style: black squares on white, 4-module margin. */
    void resetStyle();

    /* Replaces the style, the encode options and the SVG options with the
     * defaults overridden by a JSON document: {"encode":..,"style":..,
     * "svg":..} or a bare style object (docs/STYLES.md). Strings (label,
     * logo href) are copied. On error (bad JSON, unknown names, limits
     * exceeded) nothing changes and status() says why. */
    bool setStyleJson(const char *json);
    bool setStyleJson(const char *json, size_t len);
    /* Same, but applied on top of the current style and options: only the
     * keys present in the document change. */
    bool applyStyleJson(const char *json);
    bool applyStyleJson(const char *json, size_t len);

    /* Shortcuts. Colors are 0xRRGGBBAA, e.g. QRK_HEX(0x1B3A5C). */
    /* Solid module color and solid background. */
    void setColors(qrk_color foreground, qrk_color background);
    /* Module shape; size 0.1..1 shrinks separate shapes (dots, squares...),
     * roundness 0..1 rounds corners of rounded/fluid/liquid/bars shapes. */
    void setModules(qrk_module_shape shape, double size = 1.0, double roundness = 1.0);
    /* Shapes of the three corner "eyes": the 7x7 frame and the 3x3 ball. */
    void setEyes(qrk_eye_shape frame, qrk_eye_shape ball);
    /* Quiet zone (empty border) in modules; 4 is the standard, 2 usually
     * still scans. */
    void setMargin(double modules) { style_.margin = modules; }

    /* ---- encoding -------------------------------------------------------- */

    /* Options for the next encode(): ecc, min_version, max_version, mask,
     * boost_ecc, eci (qrk_encode_opts in qrkude.h). ecc defaults to
     * QRK_ECC_AUTO: HIGH when the style has a logo, MEDIUM otherwise. */
    qrk_encode_opts &encodeOptions() { return opts_; }
    /* Minimum error correction (QRK_ECC_LOW..QRK_ECC_HIGH); it is raised
     * further for free when the data still fits the same version. */
    void setEcc(qrk_ecc ecc) { opts_.ecc = ecc; }
    /* Back to QRK_ECC_AUTO. */
    void setAutoEcc() { opts_.ecc = QRK_ECC_AUTO; }

    /* Encodes text (UTF-8) or bytes with the current style and options; the
     * smallest version that fits is chosen. Returns false when the data does
     * not fit maxVersion() (QRK_ERR_DATA_TOO_LONG) or memory is missing. */
    bool encode(const char *text);
    bool encode(const uint8_t *data, size_t len);
#if defined(ARDUINO)
    bool encode(const String &text) { return encode(text.c_str()); }
#endif

    /* Content helpers: build a standard payload that phones understand
     * (join a Wi-Fi network, write an e-mail...) and encode it. The text is
     * built in a temporary heap buffer. */
    /* Join a Wi-Fi network. auth: "WPA" (also WPA2/WPA3), "WEP", "nopass"
     * (open network, password ignored) or NULL. hidden: the SSID is not
     * broadcast. */
    bool encodeWifi(const char *ssid, const char *password, const char *auth = "WPA",
                    bool hidden = false);
    /* Write an e-mail (mailto: link; subject and body are optional). */
    bool encodeEmail(const char *to, const char *subject = NULL, const char *body = NULL);
    /* Write a text message (SMSTO:). */
    bool encodeSms(const char *number, const char *message = NULL);
    /* Call a phone number (tel:). */
    bool encodeTel(const char *number);
    /* Open a map location (geo:, degrees, 6 decimals). */
    bool encodeGeo(double latitude, double longitude);
    /* Save a contact (vCard 3.0); unset (NULL) fields are left out. */
    bool encodeVCard(const qrk_vcard &card);
    /* Add a calendar event (iCalendar VEVENT); times like "20261017T120000Z". */
    bool encodeEvent(const qrk_event &event);

    /* The last encode() succeeded (outputs return false/0 otherwise). */
    bool valid() const { return valid_; }
    /* The encoded symbol for the C API, or NULL. */
    const qrk_code *code() const { return valid_ ? code_ : NULL; }
    /* Version 1..40 of the code (0 if none). */
    int version() const { return valid_ ? code_->version : 0; }
    /* Modules per side, without the quiet zone (0 if none). */
    int size() const { return valid_ ? code_->size : 0; }
    /* Error correction level actually used. */
    qrk_ecc ecc() const { return valid_ ? (qrk_ecc)code_->ecc : QRK_ECC_LOW; }
    /* Mask pattern 0..7 chosen by the encoder (-1 if none). */
    int mask() const { return valid_ ? code_->mask : -1; }
    /* true = dark module at column x, row y; false outside the grid. */
    bool module(int x, int y) const { return valid_ && qrk_get_module(code_, x, y); }

    /* Scannability warnings for the current code and style: a bit set of
     * QRK_LINT_* flags, 0 = nothing found. Call after encode(). */
    uint32_t lint() const;
    /* Share of the error correction used up by the logo area: 0 = no logo,
     * keep it below ~0.6; above 1 the code cannot be read. */
    double logoDamage() const;
    /* Name of one QRK_LINT_* flag, e.g. "low-contrast" (NULL if unknown). */
    static const char *lintName(uint32_t flag) { return qrk_lint_name(flag); }

    /* Result of the last call that can fail (QRK_OK on success). */
    qrk_status status() const { return status_; }
    /* status() as text, e.g. "data too long". */
    const char *errorString() const { return qrk_status_str(status_); }
    /* Largest version this object can encode (0: buffer too small). */
    uint8_t maxVersion() const { return maxVersion_; }

    /* Geometry in modules: canvas size, position of the grid, logo box and
     * label anchor (qrk_layout in qrkude.h). false before encode(). */
    bool layout(qrk_layout &out) const;

    /* ---- raster output --------------------------------------------------- */

    /* Anti-aliasing: samples per pixel and axis, 1 (off, sharp edges) to 4;
     * default 2. drawMono() always uses 1. */
    void setSupersample(int samples) { supersample_ = samples; }
    /* Snap modules to whole pixels (default true): crisper and scanner friendly. */
    void setIntegerScale(bool on) { integerScale_ = on; }
    /* Color behind transparent areas and outside the canvas (default white).
     * Use the screen background color so anti-aliased edges blend in. With a
     * fully transparent clear color draw() leaves those pixels untouched. */
    void setClearColor(qrk_color color) { clearColor_ = color; }
    /* Bitmap drawn in the logo area (style().logo.size must be > 0). The
     * image must stay valid while rendering; see tools/img2qrk.py. */
    void setLogo(const qrk_image *logo) { logo_ = logo; }
    void setLogo(const qrk_image &logo) { logo_ = &logo; }

    /* Renders width x height pixels (the code scaled to fit and centered)
     * as horizontal runs of one color, row by row: fn(user, x, y, len,
     * color). Nothing is buffered. false if no code is encoded. */
    bool renderSpans(int width, int height, qrk_span_fn fn, void *user) const;

    /* Same with any callable fn(int x, int y, int len, qrk_color color),
     * e.g. a lambda. */
    template <typename F>
    bool renderSpans(int width, int height, F fn) const
    {
        return renderSpans(width, height, &spanThunk<F>, &fn);
    }

    /* Renders into a pixel buffer of qrk_pixel_buffer_size(format, width,
     * height) bytes: QRK_PIXEL_RGB565, _RGB565_SWAPPED, _RGB888, _RGBA8888,
     * _BGRA8888, _GRAY8 or _MONO (1 bit per pixel). stride = bytes per row,
     * 0 = tightly packed. */
    bool renderBuffer(void *pixels, int width, int height, qrk_pixel_format format,
                      size_t stride = 0) const;

    /* Draws on any GFX-like color display (Adafruit_GFX, TFT_eSPI,
     * LovyanGFX, Arduino_GFX...) with RGB565 horizontal lines, in the
     * square at (x, y) of `size` pixels; the code is scaled to fit and
     * centered. Uses writeFastHLine() or drawFastHLine(), whichever the
     * display has. false if no code is encoded. */
    template <class Display>
    bool draw(Display &display, int16_t x, int16_t y, int16_t size)
    {
        return draw(display, x, y, size, size);
    }

    /* Same in a width x height rectangle. */
    template <class Display>
    bool draw(Display &display, int16_t x, int16_t y, int16_t width, int16_t height)
    {
        ColorTarget<Display> t;
        t.display = &display;
        t.x = x;
        t.y = y;
        qrkude_detail::start_write(display, qrkude_detail::p0());
        bool ok = renderSpans(width, height, &ColorTarget<Display>::span, &t);
        qrkude_detail::end_write(display, qrkude_detail::p0());
        return ok;
    }

    /* 1-bit displays: pixels darker than `threshold` (0..255 luminance) get
     * `dark`, the others `light`. The defaults suit OLEDs (Adafruit_SSD1306
     * BLACK = 0 / WHITE = 1, U8g2 draw colors): unlit modules on a lit
     * background, which every scanner reads. Rendered without
     * anti-aliasing. */
    template <class Display>
    bool drawMono(Display &display, int16_t x, int16_t y, int16_t size, uint16_t dark = 0,
                  uint16_t light = 1, uint8_t threshold = 128)
    {
        MonoTarget<Display> t;
        t.display = &display;
        t.x = x;
        t.y = y;
        t.dark = dark;
        t.light = light;
        t.threshold = threshold;
        int ss = supersample_;
        supersample_ = 1;
        qrkude_detail::start_write(display, qrkude_detail::p0());
        bool ok = renderSpans(size, size, &MonoTarget<Display>::span, &t);
        qrkude_detail::end_write(display, qrkude_detail::p0());
        supersample_ = ss;
        return ok;
    }

    /* ---- SVG and text output --------------------------------------------- */

    /* SVG options: width (the width attribute in px, 0 = none) and
     * id_prefix (for gradient ids; set it when several codes share one HTML
     * page). */
    qrk_svg_opts &svgOptions() { return svgOpts_; }

    /* SVG (scalable vector image) output. The document is the same,
     * byte for byte, as every other QRKude port produces. */
    /* Streams the SVG through a callback; returns the number of bytes. */
    size_t writeSvg(qrk_write_fn fn, void *user) const;
    /* Length of the SVG in bytes, e.g. for an HTTP Content-Length (0 if
     * nothing is encoded). */
    size_t svgLength() const;
    /* Writes a NUL-terminated SVG into buf; returns the full length like
     * snprintf (call with buf = NULL, cap = 0 to measure). */
    size_t svgToBuffer(char *buf, size_t cap) const;

#if defined(QRKUDE_HAS_PRINT)
    /* Streams the SVG to Serial, a WiFiClient, a File... in
     * QRKUDE_PRINT_CHUNK-byte writes. Returns the number of bytes written. */
    size_t printSvg(Print &out) const;

    /* Prints the module grid as text for a serial monitor, with `margin`
     * light modules around it. `invert` swaps dark and light: use it when
     * the terminal shows light text on a dark background. */
    size_t printAscii(Print &out, AsciiStyle style = ASCII_HALF_BLOCKS, bool invert = false,
                      uint8_t margin = 2) const;
#endif

    /* printAscii() for any byte sink (works without Arduino's Print). */
    size_t writeAscii(qrk_write_fn fn, void *user, AsciiStyle style = ASCII_HALF_BLOCKS,
                      bool invert = false, uint8_t margin = 2) const;

    /* ---- color helpers ---------------------------------------------------- */

    /* 0xRRGGBBAA -> the 16-bit RGB565 color that TFT libraries use. */
    static uint16_t toRGB565(qrk_color c)
    {
        return (uint16_t)(((c >> 16) & 0xF800u) | ((c >> 13) & 0x07E0u) | ((c >> 11) & 0x001Fu));
    }
    /* RGB565 (e.g. TFT_BLACK) -> opaque 0xRRGGBBAA. */
    static qrk_color fromRGB565(uint16_t c)
    {
        uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        return QRK_RGB(r * 255 / 31, g * 255 / 63, b * 255 / 31);
    }
    /* Perceived brightness 0 (black) .. 255 (white), as drawMono() uses. */
    static uint8_t luminance(qrk_color c)
    {
        uint32_t r = c >> 24, g = (c >> 16) & 0xFF, b = (c >> 8) & 0xFF;
        return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
    }

private:
    QRKude(const QRKude &) = delete;
    QRKude &operator=(const QRKude &) = delete;

    void init();
    bool allocate();
    bool fail(qrk_status st);
    bool parseJson(const char *json, size_t len, bool merge);
    void prepareRaster(qrk_raster_opts &ro, int width, int height) const;

    template <typename F>
    static void spanThunk(void *user, int x, int y, int len, qrk_color color)
    {
        (*static_cast<F *>(user))(x, y, len, color);
    }

    template <class Display>
    struct ColorTarget {
        Display *display;
        int16_t x, y;
        static void span(void *user, int x, int y, int len, qrk_color color)
        {
            if ((color & 0xFF) == 0)
                return; /* fully transparent: leave the screen as is */
            ColorTarget *t = static_cast<ColorTarget *>(user);
            qrkude_detail::hline(*t->display, (int16_t)(t->x + x), (int16_t)(t->y + y),
                                 (int16_t)len, toRGB565(color), qrkude_detail::p0());
        }
    };

    template <class Display>
    struct MonoTarget {
        Display *display;
        int16_t x, y;
        uint16_t dark, light;
        uint8_t threshold;
        static void span(void *user, int x, int y, int len, qrk_color color)
        {
            if ((color & 0xFF) == 0)
                return;
            MonoTarget *t = static_cast<MonoTarget *>(user);
            uint16_t c = luminance(color) < t->threshold ? t->dark : t->light;
            qrkude_detail::hline(*t->display, (int16_t)(t->x + x), (int16_t)(t->y + y),
                                 (int16_t)len, c, qrkude_detail::p0());
        }
    };

    qrk_code *code_;  /* header + modules for maxVersion_ (heap or user buffer) */
    uint8_t *scratch_; /* encoder scratch, same block as code_ */
    void *userBuffer_; /* non-NULL: caller-owned block, never freed */
    char *arena_;      /* heap: strings referenced by style_ / svgOpts_ */
    size_t arenaLen_;
    qrk_style style_;
    qrk_encode_opts opts_;
    qrk_svg_opts svgOpts_;
    const qrk_image *logo_;
    qrk_color clearColor_;
    int supersample_;
    bool integerScale_;
    bool valid_;
    uint8_t maxVersion_;
    qrk_status status_;
};

#endif /* QRKUDE_ARDUINO_H */
