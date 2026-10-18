/*
 * lv_qrkude - styled QR code widget for LVGL (v9, with v8.3 compatibility).
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implementation notes
 *  - The widget is a plain lv_canvas: only public LVGL API is used, so it
 *    builds against every v9 minor release (whose private headers move) and
 *    against v8.3. lv_canvas_set_buffer() also takes care of the image cache.
 *  - The widget state is the user data of its LV_EVENT_DELETE callback, so
 *    lv_obj_set_user_data() stays available to the application.
 */

/* Compile to nothing when LVGL is not available, e.g. in the Arduino
 * library when the sketch does not use LVGL. */
#if defined(__has_include)
#if !__has_include("lvgl.h") && !__has_include("lvgl/lvgl.h")
#define LV_QRKUDE_NO_LVGL 1
#endif
#endif

#ifndef LV_QRKUDE_NO_LVGL

#include "lv_qrkude.h"

#include <string.h>

#if LVGL_VERSION_MAJOR < 8 || (LVGL_VERSION_MAJOR == 8 && LVGL_VERSION_MINOR < 3)
#error "lv_qrkude needs LVGL 8.3 or newer"
#endif
#if !LV_USE_CANVAS
#error "lv_qrkude needs LV_USE_CANVAS 1 in lv_conf.h"
#endif

#ifndef LV_QRKUDE_MALLOC
#if LVGL_VERSION_MAJOR >= 9
#define LV_QRKUDE_MALLOC(size) lv_malloc(size)
#define LV_QRKUDE_FREE(ptr) lv_free(ptr)
#else
#define LV_QRKUDE_MALLOC(size) lv_mem_alloc(size)
#define LV_QRKUDE_FREE(ptr) lv_mem_free(ptr)
#endif
#endif

/* LVGL 9 draws any image format on any display: the canvas uses the format
 * of the object's display when the core can produce it (RGB565, L8),
 * ARGB8888 otherwise. Define LV_QRKUDE_COLOR_FORMAT to force one of
 * LV_COLOR_FORMAT_RGB565, LV_COLOR_FORMAT_ARGB8888 or LV_COLOR_FORMAT_L8.
 * LVGL 8 canvases use lv_color_t (any LV_COLOR_DEPTH, LV_COLOR_16_SWAP). */
#if LVGL_VERSION_MAJOR >= 9
static lv_color_format_t qk_color_format(lv_obj_t * obj, qrk_pixel_format * pf)
{
#ifdef LV_QRKUDE_COLOR_FORMAT
    lv_color_format_t cf = LV_QRKUDE_COLOR_FORMAT;
    LV_UNUSED(obj);
#else
    lv_display_t * disp = lv_obj_get_display(obj);
    lv_color_format_t cf = disp ? lv_display_get_color_format(disp) : LV_COLOR_FORMAT_ARGB8888;
#endif
    switch(cf) {
        case LV_COLOR_FORMAT_RGB565:
            *pf = QRK_PIXEL_RGB565;
            return LV_COLOR_FORMAT_RGB565;
        case LV_COLOR_FORMAT_L8:
        case LV_COLOR_FORMAT_I1:
            *pf = QRK_PIXEL_GRAY8;
            return LV_COLOR_FORMAT_L8;
        default:
            *pf = QRK_PIXEL_BGRA8888; /* bytes B,G,R,A */
            return LV_COLOR_FORMAT_ARGB8888;
    }
}
#endif

#define QK_MAGIC 0x514B5244u
#define QK_DEFAULT_SIZE 100

typedef struct {
    uint32_t magic;
    qrk_style style;
    qrk_encode_opts enc;
    bool auto_ecc;      /* ECC from qrk_recommended_ecc() (docs/SPEC.md 1.3) */
    char * arena;       /* strings of the last JSON style */
    uint8_t * data;     /* payload copy, for re-encoding after style changes */
    size_t data_len;
    qrk_code * code;    /* sized for LV_QRKUDE_MAX_VERSION */
    bool valid;
    const qrk_image * logo;
    qrk_color clear;
    int32_t size;
    int supersample;
    bool integer_scale;
    void * buf;         /* canvas buffer */
    size_t buf_size;
} qk_state_t;

static void qk_event_cb(lv_event_t * e);

static qk_state_t * qk_get(const lv_obj_t * cobj)
{
    lv_obj_t * obj = (lv_obj_t *)cobj;
    qk_state_t * s = NULL;
    if(obj == NULL) return NULL;
#if LVGL_VERSION_MAJOR >= 9
    uint32_t n = lv_obj_get_event_count(obj);
    for(uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t * d = lv_obj_get_event_dsc(obj, i);
        if(d && lv_event_dsc_get_cb(d) == qk_event_cb) {
            s = (qk_state_t *)lv_event_dsc_get_user_data(d);
            break;
        }
    }
#else
    s = (qk_state_t *)lv_obj_get_event_user_data(obj, qk_event_cb);
#endif
    return s && s->magic == QK_MAGIC ? s : NULL;
}

static void qk_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_DELETE) return;
    qk_state_t * s = (qk_state_t *)lv_event_get_user_data(e);
    if(s == NULL || s->magic != QK_MAGIC) return;
    s->magic = 0;
    LV_QRKUDE_FREE(s->buf);
    LV_QRKUDE_FREE(s->code);
    LV_QRKUDE_FREE(s->data);
    LV_QRKUDE_FREE(s->arena);
    LV_QRKUDE_FREE(s);
}

/* ---- rendering ------------------------------------------------------------ */

#if LVGL_VERSION_MAJOR < 9
typedef struct {
    lv_color_t * px;
    int32_t w;
} qk_v8_target_t;

static void qk_v8_span(void * user, int x, int y, int len, qrk_color c)
{
    qk_v8_target_t * t = (qk_v8_target_t *)user;
    lv_color_t col = lv_color_make((uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8));
    lv_color_t * row = t->px + (size_t)y * (size_t)t->w;
    for(int i = x; i < x + len; i++) row[i] = col;
}
#endif

static qrk_status qk_render(lv_obj_t * obj, qk_state_t * s)
{
    int32_t size = s->size;
    lv_obj_set_size(obj, size, size);
    if(!s->valid) return QRK_OK;

#if LVGL_VERSION_MAJOR >= 9
    qrk_pixel_format pf;
    lv_color_format_t cf = qk_color_format(obj, &pf);
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)size, cf);
    size_t need = (size_t)stride * (size_t)size + LV_DRAW_BUF_ALIGN;
#else
    size_t need = (size_t)size * (size_t)size * sizeof(lv_color_t);
#endif
    if(s->buf_size < need) {
        /* The canvas still points at the old buffer: nothing draws before
         * lv_canvas_set_buffer() below, LVGL being single threaded. */
        LV_QRKUDE_FREE(s->buf);
        s->buf_size = 0;
        s->buf = LV_QRKUDE_MALLOC(need);
        if(s->buf == NULL) {
            LV_LOG_WARN("lv_qrkude: out of memory for a %d px buffer", (int)size);
            return QRK_ERR_OUT_OF_MEMORY;
        }
        s->buf_size = need;
    }

    qrk_raster_opts ro;
    qrk_raster_opts_default(&ro, (int)size, (int)size);
    ro.supersample = s->supersample;
    ro.integer_scale = s->integer_scale;
    ro.clear_color = s->clear;
    ro.logo = s->logo;

    qrk_status st;
#if LVGL_VERSION_MAJOR >= 9
    /* lv_canvas_set_buffer() aligns the pointer the same way. */
    void * px = lv_draw_buf_align(s->buf, cf);
    st = qrk_render_buffer(s->code, &s->style, &ro, pf, px, stride);
    if(st == QRK_OK) lv_canvas_set_buffer(obj, s->buf, size, size, cf);
#else
    qk_v8_target_t t;
    t.px = (lv_color_t *)s->buf;
    t.w = size;
    st = qrk_render_spans(s->code, &s->style, &ro, qk_v8_span, &t);
    if(st == QRK_OK) lv_canvas_set_buffer(obj, s->buf, (lv_coord_t)size, (lv_coord_t)size, LV_IMG_CF_TRUE_COLOR);
#endif
    lv_obj_invalidate(obj);
    return st;
}

/* Encodes `data` into the widget's code. On error the code is untouched. */
static qrk_status qk_encode(qk_state_t * s, const uint8_t * data, size_t len)
{
    if(s->code == NULL) {
        s->code = (qrk_code *)LV_QRKUDE_MALLOC(offsetof(qrk_code, modules) +
                                               QRK_BUFFER_LEN_FOR_VERSION(LV_QRKUDE_MAX_VERSION));
        if(s->code == NULL) return QRK_ERR_OUT_OF_MEMORY;
    }
    uint8_t * scratch = (uint8_t *)LV_QRKUDE_MALLOC(QRK_BUFFER_LEN_FOR_VERSION(LV_QRKUDE_MAX_VERSION));
    if(scratch == NULL) return QRK_ERR_OUT_OF_MEMORY;
    qrk_encode_opts o = s->enc;
    if(s->auto_ecc) o.ecc = qrk_recommended_ecc(&s->style);
    if(o.max_version > LV_QRKUDE_MAX_VERSION) o.max_version = LV_QRKUDE_MAX_VERSION;
    if(o.min_version > o.max_version) o.min_version = o.max_version;
    /* The core returns before touching `code` when the data does not fit. */
    qrk_status st = qrk_encode(s->code, data, len, &o, scratch);
    LV_QRKUDE_FREE(scratch);
    if(st == QRK_OK) s->valid = true;
    return st;
}

static qrk_status qk_refresh(lv_obj_t * obj, qk_state_t * s)
{
    qrk_status st = QRK_OK;
    if(s->data) {
        st = qk_encode(s, s->data, s->data_len);
        if(st != QRK_OK) LV_LOG_WARN("lv_qrkude: %s", qrk_status_str(st));
    }
    qrk_status r = qk_render(obj, s);
    return st != QRK_OK ? st : r;
}

/* ---- public API ----------------------------------------------------------- */

lv_obj_t * lv_qrkude_create(lv_obj_t * parent)
{
    qk_state_t * s = (qk_state_t *)LV_QRKUDE_MALLOC(sizeof(qk_state_t));
    if(s == NULL) return NULL;
    memset(s, 0, sizeof *s);
    s->magic = QK_MAGIC;
    qrk_style_default(&s->style);
    qrk_encode_opts_default(&s->enc);
    s->enc.max_version = LV_QRKUDE_MAX_VERSION;
    s->auto_ecc = true;
    s->clear = QRK_HEX(0xFFFFFF);
    s->size = QK_DEFAULT_SIZE;
    s->supersample = 2;
    s->integer_scale = true;

    lv_obj_t * obj = lv_canvas_create(parent);
    if(obj == NULL) {
        LV_QRKUDE_FREE(s);
        return NULL;
    }
    lv_obj_add_event_cb(obj, qk_event_cb, LV_EVENT_DELETE, s);
    lv_obj_set_size(obj, s->size, s->size);
    return obj;
}

void lv_qrkude_set_size(lv_obj_t * obj, int32_t size)
{
    qk_state_t * s = qk_get(obj);
    if(s == NULL || size <= 0) return;
    s->size = size;
    qk_render(obj, s);
}

void lv_qrkude_set_style(lv_obj_t * obj, const qrk_style * style)
{
    qk_state_t * s = qk_get(obj);
    if(s == NULL || style == NULL) return;
    s->style = *style;
    qk_refresh(obj, s);
}

qrk_style * lv_qrkude_get_style(lv_obj_t * obj)
{
    qk_state_t * s = qk_get(obj);
    return s ? &s->style : NULL;
}

qrk_status lv_qrkude_set_style_json(lv_obj_t * obj, const char * json)
{
    qk_state_t * s = qk_get(obj);
    if(s == NULL || json == NULL) return QRK_ERR_INVALID_ARGUMENT;
    size_t len = strlen(json);
    /* Decoded strings are never longer than the document. */
    char * arena = (char *)LV_QRKUDE_MALLOC(len + 1);
    qrk_style * style = (qrk_style *)LV_QRKUDE_MALLOC(sizeof(qrk_style));
    if(arena == NULL || style == NULL) {
        LV_QRKUDE_FREE(arena);
        LV_QRKUDE_FREE(style);
        return QRK_ERR_OUT_OF_MEMORY;
    }
    qrk_encode_opts enc;
    qrk_style_default(style);
    qrk_encode_opts_default(&enc);
    enc.max_version = LV_QRKUDE_MAX_VERSION;
    enc.ecc = QRK_ECC_AUTO; /* stays AUTO unless the document sets encode.ecc */
    qrk_status st = qrk_style_from_json(json, len, style, &enc, NULL, arena, len + 1);
    if(st != QRK_OK) {
        LV_QRKUDE_FREE(arena);
        LV_QRKUDE_FREE(style);
        return st;
    }
    s->auto_ecc = enc.ecc == QRK_ECC_AUTO;
    s->style = *style;
    s->enc = enc;
    LV_QRKUDE_FREE(style);
    LV_QRKUDE_FREE(s->arena);
    s->arena = arena;
    return qk_refresh(obj, s);
}

qrk_status lv_qrkude_update(lv_obj_t * obj, const void * data, size_t len)
{
    qk_state_t * s = qk_get(obj);
    if(s == NULL || (data == NULL && len > 0)) return QRK_ERR_INVALID_ARGUMENT;
    uint8_t * copy = (uint8_t *)LV_QRKUDE_MALLOC(len ? len : 1);
    if(copy == NULL) return QRK_ERR_OUT_OF_MEMORY;
    if(len) memcpy(copy, data, len);
    qrk_status st = qk_encode(s, copy, len);
    if(st != QRK_OK) {
        LV_QRKUDE_FREE(copy);
        return st;
    }
    LV_QRKUDE_FREE(s->data);
    s->data = copy;
    s->data_len = len;
    return qk_render(obj, s);
}

void lv_qrkude_set_logo(lv_obj_t * obj, const qrk_image * logo)
{
    qk_state_t * s = qk_get(obj);
    if(s == NULL) return;
    s->logo = logo;
    if(logo && s->style.logo.size <= 0) s->style.logo.size = 0.2;
    qk_refresh(obj, s);
}

void lv_qrkude_set_clear_color(lv_obj_t * obj, qrk_color color)
{
    qk_state_t * s = qk_get(obj);
    if(s == NULL) return;
    s->clear = color;
    qk_render(obj, s);
}

void lv_qrkude_set_antialias(lv_obj_t * obj, int supersample, bool integer_scale)
{
    qk_state_t * s = qk_get(obj);
    if(s == NULL) return;
    s->supersample = supersample;
    s->integer_scale = integer_scale;
    qk_render(obj, s);
}

qrk_status lv_qrkude_refresh(lv_obj_t * obj)
{
    qk_state_t * s = qk_get(obj);
    return s ? qk_refresh(obj, s) : QRK_ERR_INVALID_ARGUMENT;
}

const qrk_code * lv_qrkude_get_code(const lv_obj_t * obj)
{
    qk_state_t * s = qk_get(obj);
    return s && s->valid ? s->code : NULL;
}

#endif /* LV_QRKUDE_NO_LVGL */
