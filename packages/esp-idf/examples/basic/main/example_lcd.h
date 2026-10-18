/*
 * QRKude ESP-IDF example: drawing on an SPI LCD with esp_lcd.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "qrkude.h"

/* Initializes the ST7789 panel configured in menuconfig ("QRKude example")
 * and draws the code centered, streaming 16-row strips over SPI DMA. */
void example_lcd_draw(const qrk_code *code, const qrk_style *style);
