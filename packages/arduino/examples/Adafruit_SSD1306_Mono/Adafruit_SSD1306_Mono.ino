/*
 * QRKude - Adafruit_SSD1306_Mono
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * A QR code on a 128x64 I2C OLED. drawMono() thresholds the rendering to
 * the display's two colors (unlit modules on a lit background, the way
 * scanners expect it). The same call works with U8g2 objects.
 *
 * 64 px fit a version 2 code (25 modules + 2 x 2 quiet zone) at 2 px per
 * module: up to 26 bytes at ECC M. Keep the text short or use a larger
 * display.
 */
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <QRKude.h>
#include <Wire.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
QRKude qr(4); /* versions 1..4 are plenty for a 64 px display */

void setup()
{
    Serial.begin(115200);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 not found"));
        return;
    }
    display.clearDisplay();

    qr.setMargin(2); /* 2 modules of quiet zone: scanners cope, pixels count */
    qr.setModules(QRK_MODULE_ROUNDED, 1.0, 0.5);
    if (!qr.encode("https://kudelink.com")) {
        Serial.println(qr.errorString());
        return;
    }
    qr.drawMono(display, 0, 0, 64, SSD1306_BLACK, SSD1306_WHITE);

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(70, 20);
    display.print(F("Scan me"));
    display.setCursor(70, 36);
    display.print(F("v"));
    display.print(qr.version());
    display.print(' ');
    display.print(qr.size());
    display.print(F("x"));
    display.print(qr.size());
    display.display();
}

void loop()
{
}
