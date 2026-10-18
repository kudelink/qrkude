/*
 * QRKude - BasicSerial
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Encodes a URL and prints the QR code on the serial port twice: as text
 * (Unicode half blocks, scannable straight from the serial monitor) and as
 * an SVG document you can paste into a file or a browser.
 * No display needed; works on every board.
 *
 * Boards with 2 KB of RAM (Uno, Nano) print the text version only: on AVR
 * the SVG writer takes ~12 KB of flash and its string literals live in RAM.
 * They also use a static buffer for the code instead of the heap, so the
 * compiler's RAM figure is the real one.
 */
#include <QRKude.h>

#if defined(__AVR__) && RAMEND < 0x1000 /* less than 4 KB of RAM */
#define TEXT_ONLY
static uint8_t qrBuffer[QRKUDE_BUFFER_SIZE(3)]; /* versions 1..3: 218 bytes */
QRKude qr(qrBuffer, sizeof qrBuffer);
#else
QRKude qr;
#endif

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }

    /* The style only affects the SVG (and the displays); the text dump
     * shows the module matrix. */
    qr.setModules(QRK_MODULE_DOT, 0.9);
    qr.setEyes(QRK_EYE_ROUNDED, QRK_EYE_CIRCLE);
    qr.setColors(QRK_HEX(0x1B3A5C), QRK_HEX(0xFFFFFF));

    if (!qr.encode("https://kudelink.com")) {
        Serial.print(F("encode failed: "));
        Serial.println(qr.errorString());
        return;
    }
    Serial.print(F("QR version "));
    Serial.print(qr.version());
    Serial.print(F(", "));
    Serial.print(qr.size());
    Serial.print(F(" modules, ECC "));
    Serial.println("LMQH"[qr.ecc()]);

    /* Light text on a dark terminal: pass invert = true. QRKude::ASCII_HASH
     * prints plain ASCII when the monitor does not support UTF-8. */
    qr.printAscii(Serial, QRKude::ASCII_HALF_BLOCKS, true);

#ifndef TEXT_ONLY
    Serial.println();
    Serial.print(qr.svgLength());
    Serial.println(F(" bytes of SVG:"));
    qr.printSvg(Serial);
#endif
}

void loop()
{
}
