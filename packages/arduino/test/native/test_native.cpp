/*
 * Host test of the QRKude Arduino wrapper.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Draws QR codes on fake GFX-like displays (Adafruit_GFX, TFT_eSPI,
 * LovyanGFX, SSD1306 and U8g2 flavours) that record every horizontal line in
 * a pixel buffer, checks the wrapper against the C core and writes the
 * images plus a manifest for check_decode.py (zxing-cpp).
 */
#include <QRKude.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

static int failures = 0;
static std::string out_dir = ".";
static std::string manifest;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                               \
        }                                                                             \
    } while (0)

/* ---- fake displays --------------------------------------------------------- */

struct Canvas565 {
    int w, h;
    std::vector<uint16_t> px;
    long lines;
    Canvas565(int w_, int h_, uint16_t fill = 0x1234) : w(w_), h(h_), px((size_t)w_ * h_, fill), lines(0) {}
    void hline(long x, long y, long len, uint16_t c)
    {
        lines++;
        if (y < 0 || y >= h)
            return;
        for (long i = x; i < x + len; i++)
            if (i >= 0 && i < w)
                px[(size_t)y * w + i] = c;
    }
};

/* Adafruit_GFX / Arduino_GFX style: writeFastHLine inside startWrite/endWrite. */
struct FakeAdafruitGFX : Canvas565 {
    int starts, ends, writes, draws;
    bool in_write;
    FakeAdafruitGFX(int w_, int h_) : Canvas565(w_, h_), starts(0), ends(0), writes(0), draws(0), in_write(false) {}
    void startWrite() { starts++; in_write = true; }
    void endWrite() { ends++; in_write = false; }
    void writeFastHLine(int16_t x, int16_t y, int16_t w_, uint16_t c)
    {
        CHECK(in_write);
        writes++;
        hline(x, y, w_, c);
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w_, uint16_t c)
    {
        draws++;
        hline(x, y, w_, c);
    }
};

/* TFT_eSPI style: int32 coordinates, uint32 color, no writeFastHLine. */
struct FakeTFTeSPI : Canvas565 {
    int starts, ends;
    FakeTFTeSPI(int w_, int h_) : Canvas565(w_, h_), starts(0), ends(0) {}
    void startWrite() { starts++; }
    void endWrite() { ends++; }
    void drawFastHLine(int32_t x, int32_t y, int32_t w_, uint32_t color) { hline(x, y, w_, (uint16_t)color); }
};

/* LovyanGFX style: the color type selects the format (uint16_t = RGB565). */
struct FakeLovyanGFX : Canvas565 {
    int bad_color_type;
    FakeLovyanGFX(int w_, int h_) : Canvas565(w_, h_), bad_color_type(0) {}
    void startWrite() {}
    void endWrite() {}
    template <typename T>
    void writeFastHLine(int32_t x, int32_t y, int32_t w_, const T &color)
    {
        if (sizeof(T) != 2)
            bad_color_type++;
        hline(x, y, w_, (uint16_t)color);
    }
};

struct CanvasMono {
    int w, h;
    std::vector<uint8_t> px; /* 0/1 per pixel, 2 = untouched */
    CanvasMono(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 2) {}
    void set(long x, long y, long len, uint8_t c)
    {
        if (y < 0 || y >= h)
            return;
        for (long i = x; i < x + len; i++)
            if (i >= 0 && i < w)
                px[(size_t)y * w + i] = c;
    }
};

/* Adafruit_SSD1306 style (drawFastHLine with BLACK = 0 / WHITE = 1). */
struct FakeSSD1306 : CanvasMono {
    FakeSSD1306(int w_, int h_) : CanvasMono(w_, h_) {}
    void drawFastHLine(int16_t x, int16_t y, int16_t w_, uint16_t color) { set(x, y, w_, (uint8_t)color); }
};

/* U8g2 style: setDrawColor() + drawHLine() with unsigned coordinates. */
struct FakeU8g2 : CanvasMono {
    uint8_t color;
    FakeU8g2(int w_, int h_) : CanvasMono(w_, h_), color(1) {}
    void setDrawColor(uint8_t c) { color = c; }
    void drawHLine(uint16_t x, uint16_t y, uint16_t w_) { set(x, y, w_, color); }
};

/* Print that collects bytes and counts write() calls. */
struct StringPrint : Print {
    std::string data;
    int calls;
    StringPrint() : calls(0) {}
    size_t write(uint8_t c) override
    {
        calls++;
        data.push_back((char)c);
        return 1;
    }
    size_t write(const uint8_t *buf, size_t n) override
    {
        calls++;
        data.append(reinterpret_cast<const char *>(buf), n);
        return n;
    }
};

/* ---- output helpers -------------------------------------------------------- */

static void add_manifest(const std::string &file, const std::string &expect)
{
    std::string esc;
    for (size_t i = 0; i < expect.size(); i++) {
        char c = expect[i];
        if (c == '"' || c == '\\') {
            esc += '\\';
            esc += c;
        } else if (c == '\n') {
            esc += "\\n";
        } else {
            esc += c;
        }
    }
    if (!manifest.empty())
        manifest += ",\n";
    manifest += "  {\"file\": \"" + file + "\", \"expect\": \"" + esc + "\"}";
}

static void save_565(const Canvas565 &c, const char *name, const std::string &expect)
{
    std::string path = out_dir + "/" + name + ".ppm";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        CHECK(!"cannot write ppm");
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", c.w, c.h);
    for (size_t i = 0; i < c.px.size(); i++) {
        uint16_t v = c.px[i];
        unsigned char rgb[3] = {(unsigned char)(((v >> 11) & 31) * 255 / 31),
                                (unsigned char)(((v >> 5) & 63) * 255 / 63),
                                (unsigned char)((v & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    add_manifest(std::string(name) + ".ppm", expect);
}

static void save_mono(const CanvasMono &c, const char *name, const std::string &expect)
{
    std::string path = out_dir + "/" + name + ".pgm";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        CHECK(!"cannot write pgm");
        return;
    }
    fprintf(f, "P5\n%d %d\n255\n", c.w, c.h);
    for (size_t i = 0; i < c.px.size(); i++) {
        unsigned char g = c.px[i] == 1 ? 255 : c.px[i] == 0 ? 0 : 128;
        fwrite(&g, 1, 1, f);
    }
    fclose(f);
    add_manifest(std::string(name) + ".pgm", expect);
}

static void save_text(const std::string &data, const char *name, const std::string &expect)
{
    std::string path = out_dir + "/" + name;
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        CHECK(!"cannot write file");
        return;
    }
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    if (!expect.empty())
        add_manifest(name, expect);
}

/* ---- tests ----------------------------------------------------------------- */

static const char *URL = "https://github.com/kudelink/qrkude";

static void test_adafruit_classic()
{
    QRKude qr;
    CHECK(qr.encode(URL));
    CHECK(qr.valid() && qr.version() >= 1);
    CHECK(qr.ecc() >= QRK_ECC_MEDIUM); /* auto ECC: MEDIUM (maybe boosted) */
    FakeAdafruitGFX gfx(220, 230);
    CHECK(qr.draw(gfx, 10, 15, 200));
    CHECK(gfx.starts == 1 && gfx.ends == 1);
    CHECK(gfx.writes > 0 && gfx.draws == 0);

    /* Same pixels as qrk_render_buffer(RGB565) through renderBuffer(). */
    std::vector<uint16_t> buf(200 * 200);
    CHECK(qr.renderBuffer(buf.data(), 200, 200, QRK_PIXEL_RGB565));
    bool same = true;
    for (int y = 0; y < 200 && same; y++)
        for (int x = 0; x < 200; x++)
            if (gfx.px[(size_t)(y + 15) * 220 + x + 10] != buf[(size_t)y * 200 + x]) {
                same = false;
                break;
            }
    CHECK(same);
    CHECK(gfx.px[0] == 0x1234); /* outside the QR code: untouched */
    save_565(gfx, "adafruit_classic", URL);
}

static void test_tft_espi_styled()
{
    QRKude qr;
    CHECK(qr.setStyleJson(
        "{\"style\":{\"modules\":{\"shape\":\"fluid\",\"color\":{\"type\":\"linear\",\"angle\":45,"
        "\"stops\":[{\"offset\":0,\"color\":\"#6a11cb\"},{\"offset\":1,\"color\":\"#2575fc\"}]}},"
        "\"eyes\":{\"frame\":\"rounded\",\"ball\":\"circle\",\"frameColor\":\"#d6336c\"},"
        "\"background\":{\"color\":\"#fff8e7\",\"radius\":3}}}"));
    CHECK(qr.style().modules.shape == QRK_MODULE_FLUID);
    CHECK(qr.encode("Styled QR on TFT_eSPI"));
    qr.setClearColor(QRK_HEX(0x202020));
    FakeTFTeSPI tft(240, 240);
    CHECK(qr.draw(tft, 0, 0, 240));
    CHECK(tft.starts == 1 && tft.ends == 1);
    save_565(tft, "tft_espi_fluid_gradient", "Styled QR on TFT_eSPI");
}

static void test_lovyan_dots()
{
    QRKude qr;
    qr.setModules(QRK_MODULE_DOT, 0.9);
    qr.setEyes(QRK_EYE_CIRCLE, QRK_EYE_CIRCLE);
    qr.setColors(QRK_HEX(0x003366), QRK_HEX(0xFFFFFF));
    CHECK(qr.encode("LovyanGFX dots"));
    FakeLovyanGFX lcd(160, 160);
    CHECK(qr.draw(lcd, 0, 0, 160));
    CHECK(lcd.bad_color_type == 0);
    save_565(lcd, "lovyan_dots", "LovyanGFX dots");
}

static void test_logo()
{
    /* 24x24 RGB565 logo: red disc with a white border, transparent-free. */
    static uint16_t pixels[24 * 24];
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 24; x++) {
            int dx = 2 * x - 23, dy = 2 * y - 23, d2 = dx * dx + dy * dy;
            pixels[y * 24 + x] = d2 < 18 * 18 ? 0xF800 : d2 < 22 * 22 ? 0xFFFF : 0x001F;
        }
    const qrk_image logo = {24, 24, QRK_PIXEL_RGB565, pixels, 0};

    QRKude qr;
    CHECK(qr.setStyleJson("{\"modules\":{\"shape\":\"liquid\"},\"eyes\":{\"frame\":\"circle\",\"ball\":\"circle\"},"
                          "\"logo\":{\"size\":0.2,\"margin\":0.5,\"shape\":\"circle\",\"background\":\"#ffffff\"}}"));
    qr.setLogo(logo);
    CHECK(qr.encode(URL));
    CHECK(qr.ecc() == QRK_ECC_HIGH); /* auto ECC with a logo */
    double damage = qr.logoDamage();
    printf("logo: version %d ecc %d damage %.3f lint 0x%02x\n", qr.version(), (int)qr.ecc(), damage,
           (unsigned)qr.lint());
    CHECK(damage > 0 && damage < 0.7);
    FakeAdafruitGFX gfx(256, 256);
    CHECK(qr.draw(gfx, 0, 0, 256));
    /* the logo center is red */
    CHECK(gfx.px[128 * 256 + 128] == 0xF800);
    save_565(gfx, "logo_liquid", URL);
}

static void test_mono()
{
    QRKude qr(6); /* small instance */
    CHECK(qr.encode("OLED 128x64"));
    CHECK(qr.version() <= 6);
    FakeSSD1306 oled(128, 64);
    CHECK(qr.drawMono(oled, 32, 0, 64));
    size_t touched = 0;
    for (size_t i = 0; i < oled.px.size(); i++)
        touched += oled.px[i] != 2;
    CHECK(touched == 64 * 64);
    save_mono(oled, "ssd1306_mono", "OLED 128x64");

    FakeU8g2 u8(128, 64);
    qr.setModules(QRK_MODULE_ROUNDED, 0.9, 0.5);
    CHECK(qr.drawMono(u8, -4, 0, 64)); /* partly off-screen on the left */
    save_mono(u8, "u8g2_mono_clipped", "OLED 128x64");
    FakeU8g2 u8b(128, 64);
    CHECK(qr.drawMono(u8b, 40, 0, 64));
    save_mono(u8b, "u8g2_mono_rounded", "OLED 128x64");
}

/* Automatic ECC must come from the parsed document, not from a text search:
 * an unknown "future":{"ecc":..} key keeps H with a logo, and an escaped key
 * spelling ("\u0065cc") still sets the level. */
static void test_ecc_detection()
{
    QRKude a;
    CHECK(a.setStyleJson("{\"encode\":{\"\\u0065cc\":\"L\",\"boostEcc\":false}}"));
    CHECK(a.encode("test"));
    CHECK(a.ecc() == QRK_ECC_LOW);

    QRKude b;
    CHECK(b.setStyleJson("{\"encode\":{\"boostEcc\":false},\"logo\":{\"size\":0.2},"
                         "\"future\":{\"ecc\":\"unused\"}}"));
    CHECK(b.encode("test"));
    CHECK(b.ecc() == QRK_ECC_HIGH);

    QRKude c;
    CHECK(c.setStyleJson("{\"encode\":{\"boostEcc\":false}}"));
    CHECK(c.encode("test"));
    CHECK(c.ecc() == QRK_ECC_MEDIUM);

    /* encodeOptions().ecc, setEcc() and setAutoEcc() drive the same field */
    QRKude d;
    d.encodeOptions().boost_ecc = false;
    d.encodeOptions().ecc = QRK_ECC_LOW;
    CHECK(d.encode("test") && d.ecc() == QRK_ECC_LOW);
    d.setEcc(QRK_ECC_QUARTILE);
    CHECK(d.encode("test") && d.ecc() == QRK_ECC_QUARTILE);
    d.setAutoEcc();
    d.style().logo.size = 0.2;
    CHECK(d.encode("test") && d.ecc() == QRK_ECC_HIGH);
    /* applyStyleJson without "ecc" keeps the current setting */
    d.setEcc(QRK_ECC_LOW);
    CHECK(d.applyStyleJson("{\"modules\":{\"shape\":\"dot\"}}"));
    CHECK(d.encode("test") && d.ecc() == QRK_ECC_LOW);
}

static void test_svg_and_ascii()
{
    QRKude qr;
    CHECK(qr.setStyleJson("{\"encode\":{\"ecc\":\"Q\"},\"style\":{\"modules\":{\"shape\":\"classy\"},"
                          "\"eyes\":{\"frame\":\"leaf\",\"ball\":\"leaf\"},"
                          "\"label\":{\"text\":\"SCAN <ME> & \\\"go\\\"\",\"size\":3}},"
                          "\"svg\":{\"width\":256,\"idPrefix\":\"ard\"}}"));
    CHECK(qr.encode(URL));
    CHECK(qr.ecc() >= QRK_ECC_QUARTILE);

    /* printSvg() == the core's qrk_svg_to_buffer() */
    size_t n = qrk_svg_to_buffer(qr.code(), &qr.style(), &qr.svgOptions(), NULL, 0);
    std::vector<char> ref(n + 1);
    qrk_svg_to_buffer(qr.code(), &qr.style(), &qr.svgOptions(), ref.data(), ref.size());
    StringPrint p;
    size_t written = qr.printSvg(p);
    CHECK(written == n);
    CHECK(p.data == std::string(ref.data(), n));
    CHECK(qr.svgLength() == n);
    CHECK(p.calls == (int)((n + QRKUDE_PRINT_CHUNK - 1) / QRKUDE_PRINT_CHUNK));
    std::vector<char> buf(n + 1);
    CHECK(qr.svgToBuffer(buf.data(), buf.size()) == n && strcmp(buf.data(), ref.data()) == 0);
    CHECK(p.data.find("SCAN &lt;ME&gt; &amp; &quot;go&quot;") != std::string::npos);
    CHECK(p.data.find("width=\"256\"") != std::string::npos);
    printf("svg: %zu bytes in %d Print::write() calls\n", n, p.calls);
    save_text(p.data, "classy_label.svg", URL);

    /* merge: the label string survives a second document (arena rebase) */
    CHECK(qr.applyStyleJson("{\"modules\":{\"shape\":\"dot\"}}"));
    CHECK(qr.style().modules.shape == QRK_MODULE_DOT);
    CHECK(qr.style().label.text && strcmp(qr.style().label.text, "SCAN <ME> & \"go\"") == 0);
    CHECK(qr.svgOptions().id_prefix && strcmp(qr.svgOptions().id_prefix, "ard") == 0);
    CHECK(qr.encode(URL) && qr.ecc() >= QRK_ECC_QUARTILE); /* explicit ECC kept */

    /* invalid JSON: false, nothing changes */
    CHECK(!qr.setStyleJson("{\"modules\":{\"shape\":\"nope\""));
    CHECK(qr.status() != QRK_OK);
    CHECK(qr.style().modules.shape == QRK_MODULE_DOT);

    /* setStyleJson replaces: back to auto ECC and default style */
    CHECK(qr.setStyleJson("{}"));
    CHECK(qr.style().modules.shape == QRK_MODULE_SQUARE && qr.style().label.text == NULL);

    /* ASCII: "##" rendering parses back to the module matrix */
    CHECK(qr.encode("ASCII"));
    StringPrint a;
    qr.printAscii(a, QRKude::ASCII_HASH, false, 1);
    int size = qr.size();
    bool match = true;
    size_t pos = 0;
    for (int y = -1; y < size + 1; y++) {
        for (int x = -1; x < size + 1; x++, pos += 2)
            if ((a.data.compare(pos, 2, "##") == 0) != qr.module(x, y))
                match = false;
        if (a.data[pos++] != '\n')
            match = false;
    }
    CHECK(match && pos == a.data.size());
    StringPrint h;
    qr.printAscii(h, QRKude::ASCII_HALF_BLOCKS, true, 2);
    size_t lines = 0;
    for (size_t i = 0; i < h.data.size(); i++)
        lines += h.data[i] == '\n';
    CHECK(lines == (size_t)(size + 4 + 1) / 2);
    fputs(h.data.c_str(), stdout);
}

static void test_payloads_and_limits()
{
    QRKude qr;
    CHECK(qr.encodeWifi("My Net;1", "p@ss:word", "WPA"));
    FakeAdafruitGFX gfx(200, 200);
    CHECK(qr.draw(gfx, 0, 0, 200));
    save_565(gfx, "wifi", "WIFI:T:WPA;S:My Net\\;1;P:p@ss\\:word;;");

    qrk_vcard card;
    memset(&card, 0, sizeof card);
    card.first_name = "Ada";
    card.last_name = "Lovelace";
    card.email = "ada@example.com";
    CHECK(qr.encodeVCard(card));
    FakeAdafruitGFX gfx2(240, 240);
    CHECK(qr.draw(gfx2, 0, 0, 240));
    save_565(gfx2, "vcard",
             "BEGIN:VCARD\nVERSION:3.0\nN:Lovelace;Ada;;;\nFN:Ada Lovelace\nEMAIL:ada@example.com\nEND:VCARD");

    QRKude tiny(2);
    CHECK(!tiny.encode("This text does not fit into a version 2 QR code at all, sorry."));
    CHECK(tiny.status() == QRK_ERR_DATA_TOO_LONG && !tiny.valid());
    CHECK(!tiny.draw(gfx, 0, 0, 100));
    CHECK(tiny.encode("short"));
    CHECK(tiny.version() <= 2);

    /* caller-provided buffer: no heap for the code */
    static uint8_t buffer[QRKUDE_BUFFER_SIZE(3)];
    QRKude fixed(buffer, sizeof buffer);
    CHECK(fixed.maxVersion() == 3);
    CHECK(fixed.encode("static buffer") && fixed.code() == (const qrk_code *)(void *)buffer);
    FakeAdafruitGFX gfx3(120, 120);
    CHECK(fixed.draw(gfx3, 0, 0, 120));
    save_565(gfx3, "static_buffer", "static buffer");
    QRKude too_small(buffer, 10);
    CHECK(!too_small.encode("x") && too_small.status() == QRK_ERR_BUFFER_TOO_SMALL);
    printf("QRKUDE_BUFFER_SIZE(3) = %zu, (4) = %zu, (10) = %zu, (40) = %zu\n", QRKUDE_BUFFER_SIZE(3),
           QRKUDE_BUFFER_SIZE(4), QRKUDE_BUFFER_SIZE(10), QRKUDE_BUFFER_SIZE(40));

    /* lambda span callback */
    long pixels = 0;
    CHECK(tiny.renderSpans(50, 50, [&](int, int, int len, qrk_color) { pixels += len; }));
    CHECK(pixels == 50 * 50);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        out_dir = argv[1];
    test_adafruit_classic();
    test_tft_espi_styled();
    test_lovyan_dots();
    test_logo();
    test_mono();
    test_ecc_detection();
    test_svg_and_ascii();
    test_payloads_and_limits();

    std::string path = out_dir + "/manifest.json";
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
        fprintf(f, "[\n%s\n]\n", manifest.c_str());
        fclose(f);
    }
    printf("sizeof(QRKude) = %zu, QRK_MAX_VERSION = %d\n", sizeof(QRKude), QRK_MAX_VERSION);
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
