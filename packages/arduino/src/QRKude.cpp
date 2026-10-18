/*
 * QRKude for Arduino - C++ wrapper implementation.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "QRKude.h"

#include <stdlib.h>
#include <string.h>

namespace {

struct Counter {
    size_t len;
};

void countWrite(void *user, const char *, size_t len)
{
    static_cast<Counter *>(user)->len += len;
}

struct Forward {
    qrk_write_fn fn;
    void *user;
    size_t len;
};

void forwardWrite(void *user, const char *data, size_t len)
{
    Forward *f = static_cast<Forward *>(user);
    f->fn(f->user, data, len);
    f->len += len;
}

#if defined(QRKUDE_HAS_PRINT)
/* Coalesces the many small writes of the SVG/ASCII writers into chunks:
 * one Print::write() per chunk keeps TCP and USB CDC traffic efficient. */
struct PrintSink {
    Print *out;
    size_t used, total;
    char buf[QRKUDE_PRINT_CHUNK];

    void flush()
    {
        if (used) {
            total += out->write(reinterpret_cast<const uint8_t *>(buf), used);
            used = 0;
        }
    }
    static void write(void *user, const char *data, size_t len)
    {
        PrintSink *s = static_cast<PrintSink *>(user);
        while (len > 0) {
            size_t n = sizeof s->buf - s->used;
            if (n > len)
                n = len;
            memcpy(s->buf + s->used, data, n);
            s->used += n;
            data += n;
            len -= n;
            if (s->used == sizeof s->buf)
                s->flush();
        }
    }
};
#endif

/* Re-points a string that lives in [from, from + len) to the same offset in
 * `to` (used when the JSON arena is moved). */
void rebase(const char *&s, const char *from, size_t len, const char *to)
{
    if (s && from && s >= from && s < from + len)
        s = to + (s - from);
}

} /* namespace */

QRKude::QRKude(uint8_t maxVersion)
    : code_(NULL), scratch_(NULL), userBuffer_(NULL), maxVersion_(maxVersion)
{
    if (maxVersion_ < 1 || maxVersion_ > QRK_MAX_VERSION)
        maxVersion_ = QRK_MAX_VERSION;
    init();
}

QRKude::QRKude(void *buffer, size_t size)
    : code_(NULL), scratch_(NULL), userBuffer_(buffer), maxVersion_(0)
{
    for (int v = QRK_MAX_VERSION; v >= 1 && buffer; v--)
        if (QRKUDE_BUFFER_SIZE(v) <= size) {
            maxVersion_ = (uint8_t)v;
            break;
        }
    init();
}

void QRKude::init()
{
    arena_ = NULL;
    arenaLen_ = 0;
    logo_ = NULL;
    clearColor_ = QRK_HEX(0xFFFFFF);
    supersample_ = 2;
    integerScale_ = true;
    valid_ = false;
    status_ = QRK_OK;
    qrk_style_default(&style_);
    qrk_encode_opts_default(&opts_);
    opts_.max_version = maxVersion_ ? maxVersion_ : 1;
    qrk_svg_opts_default(&svgOpts_);
}

QRKude::~QRKude()
{
    if (!userBuffer_)
        free(code_); /* scratch_ belongs to the same block */
    free(arena_);
}

bool QRKude::fail(qrk_status st)
{
    status_ = st;
    return st == QRK_OK;
}

bool QRKude::allocate()
{
    if (code_)
        return true;
    if (maxVersion_ == 0)
        return false;
    void *block = userBuffer_ ? userBuffer_ : malloc(QRKUDE_BUFFER_SIZE(maxVersion_));
    if (!block)
        return false;
    /* The core never touches modules beyond the version it encodes, which
     * is bounded by opts.max_version <= maxVersion_. */
    size_t head = QRKUDE_BUFFER_SIZE(maxVersion_) - (size_t)QRK_BUFFER_LEN_FOR_VERSION(maxVersion_);
    code_ = static_cast<qrk_code *>(block);
    scratch_ = static_cast<uint8_t *>(block) + head;
    return true;
}

/* ---- style ---------------------------------------------------------------- */

void QRKude::resetStyle()
{
    qrk_style_default(&style_);
}

void QRKude::setColors(qrk_color foreground, qrk_color background)
{
    style_.modules.paint = qrk_paint_solid(foreground);
    style_.background.paint = qrk_paint_solid(background);
}

void QRKude::setModules(qrk_module_shape shape, double size, double roundness)
{
    style_.modules.shape = (uint8_t)shape;
    style_.modules.size = size;
    style_.modules.roundness = roundness;
}

void QRKude::setEyes(qrk_eye_shape frame, qrk_eye_shape ball)
{
    style_.eyes.frame = (uint8_t)frame;
    style_.eyes.ball = (uint8_t)ball;
}

bool QRKude::setStyleJson(const char *json)
{
    return parseJson(json, json ? strlen(json) : 0, false);
}

bool QRKude::setStyleJson(const char *json, size_t len)
{
    return parseJson(json, len, false);
}

bool QRKude::applyStyleJson(const char *json)
{
    return parseJson(json, json ? strlen(json) : 0, true);
}

bool QRKude::applyStyleJson(const char *json, size_t len)
{
    return parseJson(json, len, true);
}

bool QRKude::parseJson(const char *json, size_t len, bool merge)
{
    if (!json)
        return fail(QRK_ERR_INVALID_ARGUMENT);

    /* Decoded JSON strings are never longer than their source, so `len`
     * bytes of arena are always enough. When merging, the strings of the
     * current arena are kept in front of the new ones. */
    size_t keep = merge ? arenaLen_ : 0;
    char *arena = static_cast<char *>(malloc(keep + len + 1));
    qrk_style *st = static_cast<qrk_style *>(malloc(sizeof(qrk_style)));
    if (!arena || !st) {
        free(arena);
        free(st);
        return fail(QRK_ERR_OUT_OF_MEMORY);
    }

    qrk_encode_opts enc;
    qrk_svg_opts svg;
    if (merge) {
        *st = style_;
        enc = opts_;
        svg = svgOpts_;
        if (keep) {
            memcpy(arena, arena_, keep);
            rebase(st->label.text, arena_, keep, arena);
            rebase(st->label.font, arena_, keep, arena);
            rebase(st->logo.href, arena_, keep, arena);
            rebase(svg.id_prefix, arena_, keep, arena);
        }
    } else {
        qrk_style_default(st);
        qrk_encode_opts_default(&enc);
        enc.max_version = maxVersion_;
        qrk_svg_opts_default(&svg);
    }
    /* The parser only assigns the keys it reads: without "encode.ecc" the
     * ECC stays QRK_ECC_AUTO (setStyleJson) or keeps its current value
     * (applyStyleJson). */
    qrk_status s = qrk_style_from_json(json, len, st, &enc, &svg, arena + keep, len + 1);
    if (s != QRK_OK) {
        free(arena);
        free(st);
        return fail(s);
    }
    if (enc.max_version > maxVersion_)
        enc.max_version = maxVersion_;

    style_ = *st;
    opts_ = enc;
    svgOpts_ = svg;
    free(st);
    free(arena_);
    arena_ = arena;
    arenaLen_ = keep + len + 1;
    return fail(QRK_OK);
}

/* ---- encoding ------------------------------------------------------------- */

bool QRKude::encode(const char *text)
{
    return encode(reinterpret_cast<const uint8_t *>(text), text ? strlen(text) : 0);
}

bool QRKude::encode(const uint8_t *data, size_t len)
{
    valid_ = false;
    if (!allocate())
        return fail(maxVersion_ ? QRK_ERR_OUT_OF_MEMORY : QRK_ERR_BUFFER_TOO_SMALL);
    qrk_encode_opts o = opts_;
    if (o.max_version > maxVersion_)
        o.max_version = maxVersion_;
    /* QRK_ECC_AUTO: HIGH when the style has a logo, MEDIUM otherwise. */
    qrk_status s = qrk_encode_for_style(code_, data, len, &o, &style_, scratch_);
    valid_ = s == QRK_OK;
    return fail(s);
}

/* Builds a payload with one of the core qrk_payload_* functions into a
 * temporary heap buffer and encodes it. */
#define QRKUDE_ENCODE_PAYLOAD(call_with_out_cap)                                  \
    do {                                                                          \
        char *out = NULL;                                                         \
        size_t cap = 0;                                                           \
        size_t n = call_with_out_cap;                                             \
        out = static_cast<char *>(malloc(n + 1));                                 \
        if (!out)                                                                 \
            return fail(QRK_ERR_OUT_OF_MEMORY);                                   \
        cap = n + 1;                                                              \
        call_with_out_cap;                                                        \
        bool ok = encode(reinterpret_cast<const uint8_t *>(out), n);              \
        free(out);                                                                \
        return ok;                                                                \
    } while (0)

bool QRKude::encodeWifi(const char *ssid, const char *password, const char *auth, bool hidden)
{
    QRKUDE_ENCODE_PAYLOAD(qrk_payload_wifi(out, cap, ssid, password, auth, hidden));
}

bool QRKude::encodeEmail(const char *to, const char *subject, const char *body)
{
    QRKUDE_ENCODE_PAYLOAD(qrk_payload_email(out, cap, to, subject, body));
}

bool QRKude::encodeSms(const char *number, const char *message)
{
    QRKUDE_ENCODE_PAYLOAD(qrk_payload_sms(out, cap, number, message));
}

bool QRKude::encodeTel(const char *number)
{
    QRKUDE_ENCODE_PAYLOAD(qrk_payload_tel(out, cap, number));
}

bool QRKude::encodeGeo(double latitude, double longitude)
{
    QRKUDE_ENCODE_PAYLOAD(qrk_payload_geo(out, cap, latitude, longitude));
}

bool QRKude::encodeVCard(const qrk_vcard &card)
{
    QRKUDE_ENCODE_PAYLOAD(qrk_payload_vcard(out, cap, &card));
}

bool QRKude::encodeEvent(const qrk_event &event)
{
    QRKUDE_ENCODE_PAYLOAD(qrk_payload_event(out, cap, &event));
}

#undef QRKUDE_ENCODE_PAYLOAD

uint32_t QRKude::lint() const
{
    return valid_ ? qrk_lint(code_, &style_) : 0;
}

double QRKude::logoDamage() const
{
    return valid_ ? qrk_logo_damage(code_, &style_) : 0;
}

bool QRKude::layout(qrk_layout &out) const
{
    if (!valid_)
        return false;
    qrk_get_layout(code_, &style_, &out);
    return true;
}

/* ---- raster --------------------------------------------------------------- */

void QRKude::prepareRaster(qrk_raster_opts &ro, int width, int height) const
{
    qrk_raster_opts_default(&ro, width, height);
    ro.supersample = supersample_;
    ro.integer_scale = integerScale_;
    ro.clear_color = clearColor_;
    ro.logo = logo_;
}

bool QRKude::renderSpans(int width, int height, qrk_span_fn fn, void *user) const
{
    if (!valid_ || !fn)
        return false;
    qrk_raster_opts ro;
    prepareRaster(ro, width, height);
    return qrk_render_spans(code_, &style_, &ro, fn, user) == QRK_OK;
}

bool QRKude::renderBuffer(void *pixels, int width, int height, qrk_pixel_format format,
                          size_t stride) const
{
    if (!valid_ || !pixels)
        return false;
    qrk_raster_opts ro;
    prepareRaster(ro, width, height);
    return qrk_render_buffer(code_, &style_, &ro, format, pixels, stride) == QRK_OK;
}

/* ---- SVG / text ----------------------------------------------------------- */

size_t QRKude::writeSvg(qrk_write_fn fn, void *user) const
{
    if (!valid_ || !fn)
        return 0;
    Forward f = {fn, user, 0};
    if (qrk_svg_write(code_, &style_, &svgOpts_, forwardWrite, &f) != QRK_OK)
        return 0;
    return f.len;
}

size_t QRKude::svgLength() const
{
    Counter c = {0};
    if (!valid_ || qrk_svg_write(code_, &style_, &svgOpts_, countWrite, &c) != QRK_OK)
        return 0;
    return c.len;
}

size_t QRKude::svgToBuffer(char *buf, size_t cap) const
{
    if (!valid_) {
        if (buf && cap)
            buf[0] = 0;
        return 0;
    }
    return qrk_svg_to_buffer(code_, &style_, &svgOpts_, buf, cap);
}

size_t QRKude::writeAscii(qrk_write_fn fn, void *user, AsciiStyle mode, bool invert,
                          uint8_t margin) const
{
    if (!valid_ || !fn)
        return 0;
    static const char *const kHalf[4] = {" ", "\xE2\x96\x80", "\xE2\x96\x84", "\xE2\x96\x88"};
    static const char *const kFull[2] = {"  ", "\xE2\x96\x88\xE2\x96\x88"};
    static const char *const kHash[2] = {"  ", "##"};
    const int n = code_->size, m = margin;
    size_t total = 0;
    if (mode == ASCII_HALF_BLOCKS) {
        for (int y = -m; y < n + m; y += 2) {
            for (int x = -m; x < n + m; x++) {
                int top = qrk_get_module(code_, x, y) != invert;
                int bottom = y + 1 < n + m ? (qrk_get_module(code_, x, y + 1) != invert) : invert;
                const char *s = kHalf[top | bottom << 1];
                size_t len = strlen(s);
                fn(user, s, len);
                total += len;
            }
            fn(user, "\n", 1);
            total++;
        }
    } else {
        const char *const *cells = mode == ASCII_FULL_BLOCKS ? kFull : kHash;
        for (int y = -m; y < n + m; y++) {
            for (int x = -m; x < n + m; x++) {
                const char *s = cells[qrk_get_module(code_, x, y) != invert];
                size_t len = strlen(s);
                fn(user, s, len);
                total += len;
            }
            fn(user, "\n", 1);
            total++;
        }
    }
    return total;
}

#if defined(QRKUDE_HAS_PRINT)
size_t QRKude::printSvg(Print &out) const
{
    PrintSink sink;
    sink.out = &out;
    sink.used = sink.total = 0;
    if (!valid_ || qrk_svg_write(code_, &style_, &svgOpts_, PrintSink::write, &sink) != QRK_OK)
        return 0;
    sink.flush();
    return sink.total;
}

size_t QRKude::printAscii(Print &out, AsciiStyle mode, bool invert, uint8_t margin) const
{
    PrintSink sink;
    sink.out = &out;
    sink.used = sink.total = 0;
    writeAscii(PrintSink::write, &sink, mode, invert, margin);
    sink.flush();
    return sink.total;
}
#endif
