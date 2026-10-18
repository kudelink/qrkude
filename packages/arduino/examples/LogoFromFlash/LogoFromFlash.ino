/*
 * QRKude - LogoFromFlash
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * A QR code with a logo stored in flash as an RGB565 array (logo.h was
 * generated from logo.png with extras/img2qrk.py):
 *
 *   python3 img2qrk.py logo.png -n qrkude_logo -f RGB565 -s 32 -o logo.h
 *
 * With a logo the error correction automatically goes up to H; lint() and
 * logoDamage() tell whether the logo still leaves the code readable.
 */
#include <QRKude.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "logo.h"

TFT_eSPI tft;
QRKude qr;

void setup()
{
    Serial.begin(115200);
    tft.init();
    tft.fillScreen(TFT_WHITE);

    qr.setStyleJson(
        "{\"modules\":{\"shape\":\"rounded\",\"size\":0.92,\"roundness\":0.6,\"color\":\"#1b1f3b\"},"
        "\"eyes\":{\"frame\":\"rounded\",\"ball\":\"rounded\",\"ballColor\":\"#6a11cb\"},"
        "\"logo\":{\"size\":0.24,\"margin\":0.5,\"shape\":\"rounded\",\"background\":\"#ffffff\"}}");
    qr.setLogo(qrkude_logo);

    if (!qr.encode("https://github.com/kudelink/qrkude")) {
        Serial.println(qr.errorString());
        return;
    }
    Serial.print(F("version "));
    Serial.print(qr.version());
    Serial.print(F(", ECC "));
    Serial.print("LMQH"[qr.ecc()]);
    Serial.print(F(", logo damage "));
    Serial.println(qr.logoDamage(), 3); /* <= ~0.6 is comfortable, > 1 unreadable */
    uint32_t lint = qr.lint();
    for (uint32_t flag = 1; flag <= QRK_LINT_EYE_SHAPES; flag <<= 1)
        if (lint & flag) {
            Serial.print(F("lint: "));
            Serial.println(QRKude::lintName(flag));
        }

    int16_t size = min(tft.width(), tft.height()) - 10;
    qr.draw(tft, (tft.width() - size) / 2, (tft.height() - size) / 2, size);
}

void loop()
{
}
