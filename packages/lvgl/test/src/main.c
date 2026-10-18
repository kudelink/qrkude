/*
 * Host test of the lv_qrkude widget: renders a few scenes with LVGL into a
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 * frame buffer, writes them as PPM files plus a manifest for
 * packages/arduino/test/native/check_decode.py (zxing-cpp), and checks that
 * deleting a widget frees all its memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lv_qrkude.h"

#define W 240
#define H 240

static int failures;
static const char * out_dir = ".";
static char manifest[8192];

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                             \
        }                                                                           \
    } while(0)

/* RGB888 copy of the screen, filled by the flush callback. */
static uint8_t screen_rgb[W * H * 3];

#if LVGL_VERSION_MAJOR >= 9
static uint8_t draw_buf[W * H * 4 + 64];

static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    lv_color_format_t cf = lv_display_get_color_format(disp);
    uint32_t bpp = lv_color_format_get_size(cf);
    for(int32_t y = area->y1; y <= area->y2; y++) {
        for(int32_t x = area->x1; x <= area->x2; x++) {
            const uint8_t * p = px_map + ((size_t)(y - area->y1) * (area->x2 - area->x1 + 1) + (x - area->x1)) * bpp;
            uint8_t * o = screen_rgb + ((size_t)y * W + x) * 3;
            if(bpp == 1) {
                o[0] = o[1] = o[2] = p[0];
            }
            else if(bpp == 2) {
                uint16_t v = (uint16_t)(p[0] | p[1] << 8);
                o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
                o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
                o[2] = (uint8_t)((v & 31) * 255 / 31);
            }
            else {
                o[0] = p[2];
                o[1] = p[1];
                o[2] = p[0];
            }
        }
    }
    lv_display_flush_ready(disp);
}

static void display_init(void)
{
    lv_display_t * disp = lv_display_create(W, H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof draw_buf, LV_DISPLAY_RENDER_MODE_FULL);
}
#define SCREEN() lv_screen_active()
#define OBJ_DELETE(o) lv_obj_delete(o)
#if LVGL_VERSION_MAJOR > 9 || LVGL_VERSION_MINOR >= 6
#define OBJ_SHOW(o) lv_obj_set_hidden(o, false)
#define OBJ_HIDE(o) lv_obj_set_hidden(o, true)
#else
#define OBJ_SHOW(o) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN)
#define OBJ_HIDE(o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN)
#endif
#else
static lv_color_t draw_buf[W * H];
static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;

static void flush_cb(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * px)
{
    for(int32_t y = area->y1; y <= area->y2; y++) {
        for(int32_t x = area->x1; x <= area->x2; x++) {
            lv_color32_t c;
            c.full = lv_color_to32(*px++);
            uint8_t * o = screen_rgb + ((size_t)y * W + x) * 3;
            o[0] = c.ch.red;
            o[1] = c.ch.green;
            o[2] = c.ch.blue;
        }
    }
    lv_disp_flush_ready(drv);
}

static void display_init(void)
{
    lv_disp_draw_buf_init(&disp_buf, draw_buf, NULL, W * H);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = W;
    disp_drv.ver_res = H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);
}
#define SCREEN() lv_scr_act()
#define OBJ_DELETE(o) lv_obj_del(o)
#define OBJ_SHOW(o) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN)
#define OBJ_HIDE(o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN)
#endif

static size_t mem_used(void)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return mon.total_size - mon.free_size;
}

static void snapshot(const char * name, const char * expect)
{
    lv_obj_invalidate(SCREEN());
    lv_refr_now(NULL);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", out_dir, name);
    FILE * f = fopen(path, "wb");
    if(f == NULL) {
        CHECK(!"cannot write snapshot");
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(screen_rgb, 1, sizeof screen_rgb, f);
    fclose(f);
    size_t n = strlen(manifest);
    snprintf(manifest + n, sizeof manifest - n, "%s  {\"file\": \"%s.ppm\", \"expect\": \"%s\"}",
             n ? ",\n" : "", name, expect);
}

static const uint8_t * pixel_at(int x, int y)
{
    return screen_rgb + ((size_t)y * W + x) * 3;
}

int main(int argc, char ** argv)
{
    if(argc > 1) out_dir = argv[1];
    lv_init();
    display_init();
    lv_obj_set_style_bg_color(SCREEN(), lv_color_make(0x30, 0x30, 0x30), 0);
    printf("LVGL %d.%d.%d, LV_COLOR_DEPTH %d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
           LVGL_VERSION_PATCH, LV_COLOR_DEPTH);

    /* Warm-up: LVGL allocates some caches lazily on the first renders, so
     * the leak check compares against a create/render/delete cycle. */
    {
        lv_obj_t * w = lv_qrkude_create(SCREEN());
        lv_qrkude_update(w, "warm-up", 7);
        lv_refr_now(NULL);
        OBJ_DELETE(w);
        lv_refr_now(NULL);
    }
    size_t base = mem_used();

    /* 1. classic */
    lv_obj_t * qr = lv_qrkude_create(SCREEN());
    CHECK(qr != NULL);
    lv_obj_set_user_data(qr, (void *)0x1234); /* stays usable by the application */
    lv_qrkude_set_size(qr, 200);
    CHECK(lv_qrkude_get_code(qr) == NULL);
    CHECK(lv_qrkude_update(qr, "https://lvgl.io", 15) == QRK_OK);
    CHECK(lv_qrkude_get_code(qr) != NULL);
    lv_obj_center(qr);
    snapshot("lv_classic", "https://lvgl.io");
    CHECK(lv_obj_get_user_data(qr) == (void *)0x1234);

    /* 2. styled with JSON, resized */
    CHECK(lv_qrkude_set_style_json(
              qr, "{\"style\":{\"modules\":{\"shape\":\"liquid\",\"color\":{\"type\":\"linear\",\"angle\":90,"
                  "\"stops\":[{\"offset\":0,\"color\":\"#0b7285\"},{\"offset\":1,\"color\":\"#5f3dc4\"}]}},"
                  "\"eyes\":{\"frame\":\"circle\",\"ball\":\"circle\",\"ballColor\":\"#e8590c\"},"
                  "\"background\":{\"color\":\"#f8f9fa\",\"radius\":2}}}") == QRK_OK);
    lv_qrkude_set_size(qr, 220);
    lv_obj_center(qr);
    snapshot("lv_liquid_gradient", "https://lvgl.io");
    CHECK(lv_qrkude_get_style(qr)->modules.shape == QRK_MODULE_LIQUID);

    /* 3. invalid JSON: error, style unchanged */
    CHECK(lv_qrkude_set_style_json(qr, "{\"modules\":") != QRK_OK);
    CHECK(lv_qrkude_get_style(qr)->modules.shape == QRK_MODULE_LIQUID);

    /* 4. logo: ECC raised to H automatically */
    static uint16_t logo_px[20 * 20];
    for(int y = 0; y < 20; y++)
        for(int x = 0; x < 20; x++) {
            int dx = 2 * x - 19, dy = 2 * y - 19;
            logo_px[y * 20 + x] = dx * dx + dy * dy < 16 * 16 ? 0xF800 : 0xFFFF;
        }
    static const qrk_image logo = {20, 20, QRK_PIXEL_RGB565, logo_px, 0};
    qrk_style * st = lv_qrkude_get_style(qr);
    st->logo.size = 0.22;
    st->logo.shape = QRK_LOGO_CIRCLE;
    st->logo.background = QRK_HEX(0xFFFFFF);
    lv_qrkude_set_logo(qr, &logo);
    CHECK(lv_qrkude_get_code(qr)->ecc == QRK_ECC_HIGH);
    snapshot("lv_logo", "https://lvgl.io");
    {
        const uint8_t * c = pixel_at(W / 2, H / 2);
        if(c[0] == c[1] && c[1] == c[2])
            CHECK(c[0] > 50 && c[0] < 110); /* grayscale display: red -> luminance ~76 */
        else
            CHECK(c[0] > 200 && c[1] < 60 && c[2] < 60); /* red logo center */
    }

    /* 5. too long for LV_QRKUDE_MAX_VERSION: error, previous code kept */
    {
        static char big[600];
        memset(big, 'x', sizeof big - 1);
        CHECK(lv_qrkude_update(qr, big, strlen(big)) == QRK_ERR_DATA_TOO_LONG);
        snapshot("lv_after_error", "https://lvgl.io");
    }

    /* 6. second widget with a C style, then a new payload */
    lv_obj_t * qr2 = lv_qrkude_create(SCREEN());
    qrk_style s2;
    qrk_style_default(&s2);
    s2.modules.shape = QRK_MODULE_DOT;
    s2.modules.size = 0.9;
    s2.eyes.frame = QRK_EYE_ROUNDED;
    s2.eyes.ball = QRK_EYE_ROUNDED;
    s2.margin = 2;
    lv_qrkude_set_style(qr2, &s2);
    lv_qrkude_set_size(qr2, 120);
    CHECK(lv_qrkude_update(qr2, "WIFI:T:WPA;S:lvgl;P:secret;;", 28) == QRK_OK);
    OBJ_HIDE(qr);
    lv_obj_align(qr2, LV_ALIGN_TOP_LEFT, 10, 10);
    snapshot("lv_dots_small", "WIFI:T:WPA;S:lvgl;P:secret;;");
    OBJ_SHOW(qr);

    /* 7. delete frees everything */
    OBJ_DELETE(qr2);
    OBJ_DELETE(qr);
    lv_refr_now(NULL);
    size_t after = mem_used();
    printf("memory: before %zu, after delete %zu\n", base, after);
    CHECK(after <= base);

    char path[512];
    snprintf(path, sizeof path, "%s/manifest.json", out_dir);
    FILE * f = fopen(path, "w");
    if(f) {
        fprintf(f, "[\n%s\n]\n", manifest);
        fclose(f);
    }
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
