/*
 * LVGL 9 / 8.3 display driver glue for the QRKude LVGL_Widget example:
 * TFT_eSPI flush callback, draw buffer and tick.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <TFT_eSPI.h>
#include <lvgl.h>

static TFT_eSPI tft;

#define DRAW_ROWS 20

#if LVGL_VERSION_MAJOR >= 9
static uint8_t draw_buf[TFT_WIDTH * DRAW_ROWS * 2];

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

static uint32_t tick(void)
{
    return millis();
}

static void lvgl_port_init(void)
{
    lv_tick_set_cb(tick);
    lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_flush_cb(disp, flush);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof draw_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
}
#define SCREEN() lv_screen_active()
#else
static lv_color_t draw_buf[TFT_WIDTH * DRAW_ROWS];
static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;

static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void lvgl_port_init(void)
{
    lv_disp_draw_buf_init(&disp_buf, draw_buf, NULL, TFT_WIDTH * DRAW_ROWS);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = TFT_WIDTH;
    disp_drv.ver_res = TFT_HEIGHT;
    disp_drv.flush_cb = flush;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);
}
#define SCREEN() lv_scr_act()
#endif

/* LVGL 9 reads the time through lv_tick_set_cb(); LVGL 8 needs ticks. */
static void lvgl_port_tick(void)
{
#if LVGL_VERSION_MAJOR < 9
    static uint32_t last = millis();
    uint32_t now = millis();
    lv_tick_inc(now - last);
    last = now;
#endif
}
