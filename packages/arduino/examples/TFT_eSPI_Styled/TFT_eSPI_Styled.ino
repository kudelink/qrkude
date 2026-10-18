/*
 * QRKude - TFT_eSPI_Styled
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cycles through a few QR code styles on a color TFT driven by TFT_eSPI
 * (configure TFT_eSPI as usual: User_Setup.h, or build flags with
 * USER_SETUP_LOADED on PlatformIO). The same draw() call works with
 * Adafruit_GFX, LovyanGFX and Arduino_GFX displays.
 */
#include <QRKude.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <stdarg.h>

TFT_eSPI tft;
QRKude qr;

/* Styles in the cross-language JSON format (docs/SPEC.md). */
static const char *const STYLES[] = {
    /* classic */
    "{}",
    /* fluid modules, linear gradient, colored eyes */
    "{\"modules\":{\"shape\":\"fluid\",\"color\":{\"type\":\"linear\",\"angle\":45,"
    "\"stops\":[{\"offset\":0,\"color\":\"#6a11cb\"},{\"offset\":1,\"color\":\"#2575fc\"}]}},"
    "\"eyes\":{\"frame\":\"rounded\",\"ball\":\"circle\",\"frameColor\":\"#d6336c\"},"
    "\"background\":{\"color\":\"#fff8e7\",\"radius\":3}}",
    /* liquid, radial gradient */
    "{\"modules\":{\"shape\":\"liquid\",\"color\":{\"type\":\"radial\",\"stops\":["
    "{\"offset\":0,\"color\":\"#0b7285\"},{\"offset\":1,\"color\":\"#212529\"}]}},"
    "\"eyes\":{\"frame\":\"circle\",\"ball\":\"circle\"}}",
    /* dots with leaf eyes */
    "{\"modules\":{\"shape\":\"dot\",\"size\":0.9,\"color\":\"#2b8a3e\"},"
    "\"eyes\":{\"frame\":\"leaf\",\"ball\":\"leaf\",\"ballColor\":\"#e8590c\"}}",
    /* vertical bars */
    "{\"modules\":{\"shape\":\"vbars\",\"size\":0.8,\"color\":\"#343a40\"},"
    "\"eyes\":{\"frame\":\"drop\",\"ball\":\"drop\"}}",
};
static const int STYLE_COUNT = sizeof STYLES / sizeof STYLES[0];

/* printf-style logging that also works on cores without Serial.printf. */
static void logLine(const char *fmt, ...)
{
    char line[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    Serial.println(line);
}

static void show(int index)
{
    if (!qr.setStyleJson(STYLES[index])) {
        logLine("style %d: %s", index, qr.errorString());
        return;
    }
    /* Anti-aliased edges blend into the screen background. */
    qr.setClearColor(QRKude::fromRGB565(TFT_BLACK));
    if (!qr.encode("https://github.com/kudelink/qrkude")) {
        logLine("encode: %s", qr.errorString());
        return;
    }
    for (uint32_t flag = 1; flag <= QRK_LINT_EYE_SHAPES; flag <<= 1)
        if (qr.lint() & flag)
            logLine("lint: %s", QRKude::lintName(flag));

    int16_t size = min(tft.width(), tft.height()) - 16;
    tft.fillScreen(TFT_BLACK);
    uint32_t t0 = millis();
    qr.draw(tft, (tft.width() - size) / 2, (tft.height() - size) / 2, size);
    logLine("style %d: version %d, %dx%d px in %lu ms", index, qr.version(), size, size,
            (unsigned long)(millis() - t0));
}

void setup()
{
    Serial.begin(115200);
    tft.init();
    tft.setRotation(0);
    show(0);
}

void loop()
{
    static int index = 0;
    delay(4000);
    index = (index + 1) % STYLE_COUNT;
    show(index);
}
