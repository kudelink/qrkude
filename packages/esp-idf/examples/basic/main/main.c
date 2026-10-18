/*
 * QRKude ESP-IDF example: encodes a styled Wi-Fi QR code, prints it as text
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 * and as SVG on the console, measures the rasterizer and, when
 * CONFIG_EXAMPLE_LCD is enabled, draws it on an ST7789 SPI LCD.
 */
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "qrkude.h"
#include "example_lcd.h"

static const char *TAG = "qrkude";

/* Static buffers: the core never allocates when given a scratch buffer. */
static qrk_code s_code;
static uint8_t s_scratch[QRK_BUFFER_LEN_MAX];
static char s_arena[128]; /* strings of the JSON style (label, href) */

static const char *STYLE_JSON =
    "{\"style\": {"
    "  \"modules\": {\"shape\": \"fluid\", \"color\": {\"type\": \"linear\", \"angle\": 45,"
    "    \"stops\": [{\"offset\": 0, \"color\": \"#0b7285\"}, {\"offset\": 1, \"color\": \"#5f3dc4\"}]}},"
    "  \"eyes\": {\"frame\": \"rounded\", \"ball\": \"circle\", \"ballColor\": \"#e8590c\"},"
    "  \"background\": {\"color\": \"#ffffff\", \"radius\": 2},"
    "  \"label\": {\"text\": \"Join the Wi-Fi\", \"size\": 3, \"color\": \"#343a40\"}"
    "}, \"svg\": {\"width\": 320}}";

static void stdout_write(void *user, const char *data, size_t len)
{
    (void)user;
    fwrite(data, 1, len, stdout);
}

/* Two module rows per text line with Unicode half blocks. */
static void print_half_blocks(const qrk_code *code, int margin)
{
    static const char *const cells[4] = {" ", "\xE2\x96\x80", "\xE2\x96\x84", "\xE2\x96\x88"};
    for (int y = -margin; y < code->size + margin; y += 2) {
        for (int x = -margin; x < code->size + margin; x++) {
            int top = qrk_get_module(code, x, y);
            int bottom = qrk_get_module(code, x, y + 1);
            fputs(cells[top | bottom << 1], stdout);
        }
        fputc('\n', stdout);
    }
}

static void count_span(void *user, int x, int y, int len, qrk_color color)
{
    (void)x;
    (void)y;
    (void)len;
    (void)color;
    (*(int *)user)++;
}

void app_main(void)
{
    qrk_style style;
    qrk_encode_opts enc;
    qrk_svg_opts svg;
    qrk_style_default(&style);
    qrk_encode_opts_default(&enc);
    qrk_svg_opts_default(&svg);

    qrk_status st = qrk_style_from_json(STYLE_JSON, strlen(STYLE_JSON), &style, &enc, &svg,
                                        s_arena, sizeof s_arena);
    if (st != QRK_OK) {
        ESP_LOGE(TAG, "style: %s", qrk_status_str(st));
        return;
    }
    enc.ecc = qrk_recommended_ecc(&style); /* the JSON sets no "ecc" */

    char payload[96];
    qrk_payload_wifi(payload, sizeof payload, "QRKude Guest", "esp32-rocks", "WPA", false);

    int64_t t0 = esp_timer_get_time();
    st = qrk_encode_text(&s_code, payload, &enc, s_scratch);
    int64_t t1 = esp_timer_get_time();
    if (st != QRK_OK) {
        ESP_LOGE(TAG, "encode: %s", qrk_status_str(st));
        return;
    }
    ESP_LOGI(TAG, "QRKude %s, QRK_MAX_VERSION %d, sizeof(qrk_code) %u", QRK_LIB_VERSION,
             QRK_MAX_VERSION, (unsigned)sizeof(qrk_code));
    ESP_LOGI(TAG, "payload \"%s\" -> version %d (%dx%d), ecc %c, mask %d in %lld us", payload,
             s_code.version, s_code.size, s_code.size, "LMQH"[s_code.ecc], s_code.mask,
             (long long)(t1 - t0));

    uint32_t lint = qrk_lint(&s_code, &style);
    for (uint32_t flag = 1; flag <= QRK_LINT_EYE_SHAPES; flag <<= 1)
        if (lint & flag)
            ESP_LOGW(TAG, "lint: %s", qrk_lint_name(flag));

    print_half_blocks(&s_code, 2);

    printf("----- SVG -----\n");
    qrk_svg_write(&s_code, &style, &svg, stdout_write, NULL);
    printf("----- end SVG -----\n");

    /* Rasterizer speed: 240x240 px, 2x2 supersampling, spans only. */
    qrk_raster_opts ro;
    qrk_raster_opts_default(&ro, 240, 240);
    int spans = 0;
    t0 = esp_timer_get_time();
    qrk_render_spans(&s_code, &style, &ro, count_span, &spans);
    t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "rendered 240x240 (supersample %d): %d spans in %lld ms", ro.supersample, spans,
             (long long)((t1 - t0) / 1000));

    /* The same into an RGB565 buffer (e.g. for esp_lcd or LVGL). */
    size_t bytes = qrk_pixel_buffer_size(QRK_PIXEL_RGB565, 240, 240);
    uint16_t *pixels = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    if (pixels) {
        t0 = esp_timer_get_time();
        qrk_render_buffer(&s_code, &style, &ro, QRK_PIXEL_RGB565, pixels, 0);
        t1 = esp_timer_get_time();
        ESP_LOGI(TAG, "RGB565 buffer (%u bytes) in %lld ms", (unsigned)bytes,
                 (long long)((t1 - t0) / 1000));
        heap_caps_free(pixels);
    }

#if CONFIG_EXAMPLE_LCD
    example_lcd_draw(&s_code, &style);
#endif
    ESP_LOGI(TAG, "free heap: %u bytes", (unsigned)esp_get_free_heap_size());
}
