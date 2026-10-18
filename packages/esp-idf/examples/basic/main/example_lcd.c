/*
 * QRKude ESP-IDF example: draws a QR code on an ST7789 SPI LCD with esp_lcd.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * qrk_render_spans() produces the image row by row, so it is streamed in
 * strips of STRIP_ROWS lines through two small DMA buffers instead of a full
 * frame buffer (240 x 16 x 2 bytes x 2 = 15 KB for a 240 px code).
 */
#include "sdkconfig.h"

#if CONFIG_EXAMPLE_LCD

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "example_lcd.h"

#define LCD_HOST SPI2_HOST
#define STRIP_ROWS 16

static const char *TAG = "qrkude_lcd";

typedef struct {
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t done; /* given when the previous strip was sent */
    uint16_t *buf[2];
    int cur;
    int x0, y0, width;
    int strip_y; /* first row of the strip being filled */
} strip_writer_t;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                          void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &woken);
    return woken == pdTRUE;
}

static void strip_flush(strip_writer_t *w, int rows)
{
    xSemaphoreTake(w->done, portMAX_DELAY); /* previous strip sent */
    esp_lcd_panel_draw_bitmap(w->panel, w->x0, w->y0 + w->strip_y, w->x0 + w->width,
                              w->y0 + w->strip_y + rows, w->buf[w->cur]);
    w->cur ^= 1; /* fill the other buffer while this one is sent */
    w->strip_y += rows;
}

static void strip_span(void *user, int x, int y, int len, qrk_color c)
{
    strip_writer_t *w = (strip_writer_t *)user;
    /* RGB565, big endian on the wire */
    uint16_t v = (uint16_t)(((c >> 16) & 0xF800) | ((c >> 13) & 0x07E0) | ((c >> 11) & 0x001F));
    v = (uint16_t)(v >> 8 | v << 8);
    int row = y - w->strip_y;
    uint16_t *p = w->buf[w->cur] + row * w->width + x;
    for (int i = 0; i < len; i++)
        p[i] = v;
    if (x + len == w->width && row == STRIP_ROWS - 1)
        strip_flush(w, STRIP_ROWS);
}

static esp_lcd_panel_handle_t panel_init(SemaphoreHandle_t done)
{
    spi_bus_config_t bus = {
        .sclk_io_num = CONFIG_EXAMPLE_LCD_PIN_SCLK,
        .mosi_io_num = CONFIG_EXAMPLE_LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_EXAMPLE_LCD_H_RES * STRIP_ROWS * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_EXAMPLE_LCD_PIN_DC,
        .cs_gpio_num = CONFIG_EXAMPLE_LCD_PIN_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .user_ctx = done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_EXAMPLE_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, CONFIG_EXAMPLE_LCD_INVERT));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    if (CONFIG_EXAMPLE_LCD_PIN_BL >= 0) {
        gpio_config_t bl = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << CONFIG_EXAMPLE_LCD_PIN_BL};
        ESP_ERROR_CHECK(gpio_config(&bl));
        gpio_set_level(CONFIG_EXAMPLE_LCD_PIN_BL, 1);
    }
    return panel;
}

void example_lcd_draw(const qrk_code *code, const qrk_style *style)
{
    const int width = CONFIG_EXAMPLE_LCD_H_RES, height = CONFIG_EXAMPLE_LCD_V_RES;
    const int size = width < height ? width : height;

    strip_writer_t w = {0};
    w.done = xSemaphoreCreateBinary();
    xSemaphoreGive(w.done);
    for (int i = 0; i < 2; i++) {
        w.buf[i] = heap_caps_malloc((size_t)size * STRIP_ROWS * 2, MALLOC_CAP_DMA);
        if (!w.buf[i]) {
            ESP_LOGE(TAG, "no DMA memory for the strip buffers");
            return;
        }
    }
    w.panel = panel_init(w.done);
    w.width = size;
    w.x0 = (width - size) / 2;
    w.y0 = (height - size) / 2;

    qrk_raster_opts ro;
    qrk_raster_opts_default(&ro, size, size);
    ro.clear_color = QRK_HEX(0xFFFFFF);
    int64_t t0 = esp_timer_get_time();
    qrk_render_spans(code, style, &ro, strip_span, &w);
    if (w.strip_y < size)
        strip_flush(&w, size - w.strip_y); /* last partial strip */
    xSemaphoreTake(w.done, portMAX_DELAY); /* wait for the last transfer */
    ESP_LOGI(TAG, "drawn %dx%d on the LCD in %lld ms", size, size,
             (long long)((esp_timer_get_time() - t0) / 1000));

    heap_caps_free(w.buf[0]);
    heap_caps_free(w.buf[1]);
    vSemaphoreDelete(w.done);
}

#endif /* CONFIG_EXAMPLE_LCD */
