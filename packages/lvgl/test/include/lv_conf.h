/*
 * Minimal lv_conf.h for the lv_qrkude host tests (LVGL 8.3 and 9.x).
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* LVGL >= 9.6 prefers LV_COLOR_FORMAT_DEFAULT; LV_COLOR_DEPTH still works
 * (with a deprecation warning) and is what 8.3 and older 9.x read. */
#if defined(LV_TEST_COLOR_FORMAT)
#define LV_COLOR_FORMAT_DEFAULT LV_TEST_COLOR_FORMAT
#elif !defined(LV_COLOR_DEPTH)
#define LV_COLOR_DEPTH 16
#endif
#ifndef LV_COLOR_16_SWAP
#define LV_COLOR_16_SWAP 0 /* LVGL 8 only */
#endif

/* Built-in allocator (both versions) so that lv_mem_monitor() can check
 * that a deleted widget frees everything. */
#define LV_MEM_CUSTOM 0
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (2U * 1024U * 1024U)

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_CANVAS 1
#define LV_FONT_MONTSERRAT_14 1

#endif
