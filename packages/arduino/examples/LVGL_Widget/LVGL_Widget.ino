/*
 * QRKude - LVGL_Widget
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The lv_qrkude widget (shipped with this library, compiled only when LVGL
 * is installed) on a TFT_eSPI display. Works with LVGL 9 and 8.3.
 *
 * LVGL setup for the Arduino IDE: copy lv_conf.h from this folder next to
 * the "lvgl" folder in your libraries directory. The QR code canvas
 * needs size x size x 2 bytes (80 KB for 200 px) from the LVGL allocator,
 * so lv_conf.h uses the C library malloc instead of LVGL's 64 KB pool.
 */
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <lv_qrkude.h> /* after lvgl.h */

static lv_obj_t *qr;
static lv_obj_t *caption;

/* Display driver glue for LVGL 9 and 8 (kept out of the .ino because the
 * Arduino prototype generator does not honor #if). */
#include "lvgl_port.h"

/* Re-encodes a new payload every few seconds. */
static void update_cb(lv_timer_t *timer)
{
    static uint32_t count = 0;
    char text[64];
    snprintf(text, sizeof text, "https://kudelink.com/?n=%lu", (unsigned long)++count);
    qrk_status st = lv_qrkude_update(qr, text, strlen(text));
    lv_label_set_text(caption, st == QRK_OK ? text : qrk_status_str(st));
}

void setup()
{
    Serial.begin(115200);
    tft.begin();
    tft.setRotation(0);
    lv_init();
    lvgl_port_init();

    qr = lv_qrkude_create(SCREEN());
    lv_qrkude_set_size(qr, 200);
    lv_qrkude_set_style_json(qr,
        "{\"modules\":{\"shape\":\"liquid\",\"color\":{\"type\":\"linear\",\"angle\":90,"
        "\"stops\":[{\"offset\":0,\"color\":\"#0b7285\"},{\"offset\":1,\"color\":\"#5f3dc4\"}]}},"
        "\"eyes\":{\"frame\":\"circle\",\"ball\":\"circle\",\"ballColor\":\"#e8590c\"}}");
    lv_qrkude_update(qr, "https://lvgl.io", 15);
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 20);

    caption = lv_label_create(SCREEN());
    lv_label_set_text(caption, "https://lvgl.io");
    lv_obj_align_to(caption, qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_timer_create(update_cb, 3000, NULL);
}

void loop()
{
    lvgl_port_tick();
    lv_timer_handler();
    delay(5);
}
