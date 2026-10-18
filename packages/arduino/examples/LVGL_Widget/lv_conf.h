/*
 * Minimal lv_conf.h for the QRKude LVGL_Widget example (LVGL 9 and 8.3).
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Arduino IDE: copy it next to the "lvgl" folder of your libraries.
 * PlatformIO: build_flags = -DLV_CONF_INCLUDE_SIMPLE -I<this folder>.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0 /* LVGL 8: the flush callback swaps the bytes */

/* Big canvases (the QR code) come from the C library heap. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB /* LVGL 9 */
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_MEM_CUSTOM 1 /* LVGL 8 */

#define LV_USE_CANVAS 1 /* required by lv_qrkude */
#define LV_USE_LABEL 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#endif
