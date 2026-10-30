/*
 * QRKude core unit tests. Decoding tests live in spec/tools/check_decode.py.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "qrkude.h"

static int failures = 0, checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define CHECK_STR(got, want)                                                 \
    do {                                                                     \
        checks++;                                                            \
        if (strcmp((got), (want)) != 0) {                                    \
            failures++;                                                      \
            fprintf(stderr, "%s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (got), (want)); \
        }                                                                    \
    } while (0)

static qrk_code code;

static int version_for(const char *text, size_t len, qrk_ecc ecc)
{
    qrk_encode_opts o;
    qrk_encode_opts_default(&o);
    o.ecc = ecc;
    o.boost_ecc = false;
    if (qrk_encode(&code, text, len, &o, NULL) != QRK_OK)
        return -1;
    return code.version;
}

static void test_capacity(void)
{
    /* Version 1 capacities from ISO/IEC 18004 table 7. */
    static const int cap[4][3] = {{41, 25, 17}, {34, 20, 14}, {27, 16, 11}, {17, 10, 7}};
    char buf[3000];
    for (int e = 0; e < 4; e++) {
        for (int m = 0; m < 3; m++) {
            char fill = m == 0 ? '7' : m == 1 ? 'K' : 'k';
            memset(buf, fill, sizeof buf);
            CHECK(version_for(buf, (size_t)cap[e][m], (qrk_ecc)e) == 1);
            CHECK(version_for(buf, (size_t)cap[e][m] + 1, (qrk_ecc)e) == 2);
        }
    }
    memset(buf, 'k', sizeof buf);
    CHECK(version_for(buf, 2953, QRK_ECC_LOW) == 40);
    CHECK(version_for(buf, 2954, QRK_ECC_LOW) == -1);
    CHECK(version_for("", 0, QRK_ECC_MEDIUM) == 1);
    static char numeric[7090];
    memset(numeric, '7', sizeof numeric);
    CHECK(version_for(numeric, 7089, QRK_ECC_LOW) == 40);
    CHECK(version_for(numeric, sizeof numeric, QRK_ECC_LOW) == -1);
}

static void test_modes(void)
{
    qrk_encode_text(&code, "0123456789", NULL, NULL);
    CHECK(code.mode == QRK_MODE_NUMERIC);
    qrk_encode_text(&code, "HELLO WORLD $%*+-./:", NULL, NULL);
    CHECK(code.mode == QRK_MODE_ALPHANUMERIC);
    qrk_encode_text(&code, "hello", NULL, NULL);
    CHECK(code.mode == QRK_MODE_BYTE);

    qrk_encode_opts o;
    qrk_encode_opts_default(&o);
    o.mask = 5;
    qrk_encode_text(&code, "mask", &o, NULL);
    CHECK(code.mask == 5);
    o.mask = -1;
    o.min_version = 7;
    qrk_encode_text(&code, "v7", &o, NULL);
    CHECK(code.version == 7 && code.size == 45);
    /* boost: short text at v1 ends up with ECC H */
    qrk_encode_text(&code, "1", NULL, NULL);
    CHECK(code.ecc == QRK_ECC_HIGH);
    /* finder pattern corners */
    CHECK(qrk_get_module(&code, 0, 0) && qrk_get_module(&code, 6, 6) && !qrk_get_module(&code, 7, 7));
    CHECK(qrk_get_module(&code, 8, code.size - 8)); /* dark module */
}

static void test_numbers(void)
{
    char buf[32];
    struct { double v; const char *s; } cases[] = {
        {0, "0"}, {1, "1"}, {-1, "-1"}, {0.5, "0.5"}, {0.25, "0.25"}, {0.125, "0.125"},
        {0.0625, "0.063"}, {-0.0625, "-0.062"}, {1.0005, "1.001"}, {12.3456, "12.346"},
        {-0.0001, "0"}, {100, "100"}, {0.1 + 0.2, "0.3"}, {2.9999, "3"}, {-2.5, "-2.5"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        qrk_format_number(cases[i].v, buf);
        CHECK_STR(buf, cases[i].s);
    }
}

static void test_colors(void)
{
    qrk_color c;
    CHECK(qrk_parse_color("#fff", 4, &c) && c == 0xFFFFFFFF);
    CHECK(qrk_parse_color("#1a2B3c", 7, &c) && c == 0x1A2B3CFF);
    CHECK(qrk_parse_color("#11223344", 9, &c) && c == 0x11223344);
    CHECK(qrk_parse_color("transparent", 11, &c) && c == 0);
    CHECK(!qrk_parse_color("red", 3, &c));
    CHECK(!qrk_parse_color("#12345", 6, &c));
}

static void test_payloads(void)
{
    char buf[512];
    qrk_payload_wifi(buf, sizeof buf, "My;Net", "p@ss:\"word\"", "WPA", false);
    CHECK_STR(buf, "WIFI:T:WPA;S:My\\;Net;P:p@ss\\:\\\"word\\\";;");
    qrk_payload_wifi(buf, sizeof buf, "Open", "ignored", "nopass", true);
    CHECK_STR(buf, "WIFI:T:nopass;S:Open;H:true;;");
    qrk_payload_email(buf, sizeof buf, "a@b.c", "Hi there", "x&y=z");
    CHECK_STR(buf, "mailto:a@b.c?subject=Hi%20there&body=x%26y%3Dz");
    qrk_payload_email(buf, sizeof buf, "a@b.c", NULL, "only body");
    CHECK_STR(buf, "mailto:a@b.c?body=only%20body");
    qrk_payload_sms(buf, sizeof buf, "+34600000000", "Hola");
    CHECK_STR(buf, "SMSTO:+34600000000:Hola");
    qrk_payload_tel(buf, sizeof buf, "+1 555");
    CHECK_STR(buf, "tel:+1 555");
    qrk_payload_geo(buf, sizeof buf, 40.4168, -3.7038);
    CHECK_STR(buf, "geo:40.4168,-3.7038");
    qrk_payload_geo(buf, sizeof buf, 0, 1.5);
    CHECK_STR(buf, "geo:0,1.5");

    qrk_vcard card = {0};
    card.first_name = "Ana";
    card.last_name = "García";
    card.organization = "Kude; Labs";
    card.phone = "+34 600";
    card.email = "ana@example.com";
    card.address = "Calle 1, Madrid";
    qrk_payload_vcard(buf, sizeof buf, &card);
    CHECK_STR(buf, "BEGIN:VCARD\nVERSION:3.0\nN:García;Ana;;;\nFN:Ana García\nORG:Kude\\; Labs\n"
                   "TEL:+34 600\nEMAIL:ana@example.com\nADR:;;Calle 1\\, Madrid;;;;\nEND:VCARD");

    qrk_event ev = {0};
    ev.summary = "Launch";
    ev.start = "20261017T100000Z";
    ev.location = "Online";
    qrk_payload_event(buf, sizeof buf, &ev);
    CHECK_STR(buf, "BEGIN:VEVENT\nSUMMARY:Launch\nDTSTART:20261017T100000Z\nLOCATION:Online\nEND:VEVENT");

    /* snprintf-like truncation */
    char small[8];
    size_t n = qrk_payload_tel(small, sizeof small, "123456789");
    CHECK(n == 13 && strlen(small) == 7);
}

static void test_json(void)
{
    const char *json =
        "{\"encode\":{\"ecc\":\"Q\",\"minVersion\":3,\"mask\":2,\"boostEcc\":false},"
        " \"style\":{\"margin\":2.5,\"modules\":{\"shape\":\"liquid\",\"roundness\":0.8,"
        "  \"color\":{\"type\":\"radial\",\"r\":0.6,\"stops\":[{\"offset\":0,\"color\":\"#f00\"},"
        "  {\"offset\":1,\"color\":\"#0000ff80\"}]}},"
        "  \"eyes\":{\"frame\":\"leaf\",\"ball\":\"circle\",\"frameColor\":\"#123456\",\"ballColor\":null},"
        "  \"alignment\":{\"frame\":\"circle\",\"ball\":\"dots\"},"
        "  \"background\":{\"color\":null,\"radius\":3},\"border\":{\"width\":1,\"color\":\"#00ff00\"},"
        "  \"logo\":{\"href\":\"data:image/png;base64,AAA\\u00e9\",\"shape\":\"circle\",\"background\":\"#fff\"},"
        "  \"label\":{\"text\":\"SCAN \\\"ME\\\"\",\"position\":\"top\",\"bold\":false},"
        "  \"unknown\":[1,{\"a\":[true,null]}]},"
        " \"svg\":{\"width\":256,\"idPrefix\":\"x1\"}}";
    qrk_style s;
    qrk_encode_opts e;
    qrk_svg_opts o;
    qrk_style_default(&s);
    qrk_encode_opts_default(&e);
    qrk_svg_opts_default(&o);
    char arena[256];
    CHECK(qrk_style_from_json(json, strlen(json), &s, &e, &o, arena, sizeof arena) == QRK_OK);
    CHECK(e.ecc == QRK_ECC_QUARTILE && e.min_version == 3 && e.mask == 2 && !e.boost_ecc);
    CHECK(s.margin == 2.5);
    /* Cast decimal expectations to double: x87 otherwise keeps literals at
     * extended precision while the parsed fields are stored as doubles. */
    CHECK(s.modules.shape == QRK_MODULE_LIQUID && s.modules.roundness == (double)0.8);
    CHECK(s.modules.paint.type == QRK_PAINT_RADIAL && s.modules.paint.stop_count == 2);
    CHECK(s.modules.paint.r == (double)0.6 && s.modules.paint.stops[1].color == 0x0000FF80);
    CHECK(s.eyes.frame == QRK_EYE_LEAF && s.eyes.ball == QRK_EYE_CIRCLE);
    CHECK(s.eyes.frame_paint.type == QRK_PAINT_SOLID && s.eyes.frame_paint.color == 0x123456FF);
    CHECK(s.eyes.ball_paint.type == QRK_PAINT_NONE);
    CHECK(s.alignment.frame == QRK_EYE_CIRCLE && s.alignment.ball == QRK_EYE_DOTS);
    CHECK(s.background.paint.type == QRK_PAINT_NONE && s.background.radius == 3);
    CHECK(s.border.width == 1 && s.border.color == 0x00FF00FF);
    CHECK(s.logo.size == (double)0.2 && s.logo.shape == QRK_LOGO_CIRCLE && s.logo.background == 0xFFFFFFFF);
    CHECK_STR(s.logo.href, "data:image/png;base64,AAA\xc3\xa9");
    CHECK_STR(s.label.text, "SCAN \"ME\"");
    CHECK(s.label.position == QRK_LABEL_TOP && !s.label.bold);
    CHECK(o.width == 256);
    CHECK_STR(o.id_prefix, "x1");

    CHECK(qrk_style_from_json("{\"modules\":{\"shape\":\"nope\"}}", 29, &s, NULL, NULL, NULL, 0) == QRK_ERR_PARSE);
    const char *long_keys = "{\"style\":{\"aVeryLongUnknownKeyOfThirtyTwoChars\":{\"anotherVeryLongNestedUnknownKey__\":1},"
                            "\"margin\":7}}";
    CHECK(qrk_style_from_json(long_keys, strlen(long_keys), &s, NULL, NULL, NULL, 0) == QRK_OK && s.margin == 7);
    CHECK(qrk_style_from_json("{\"margin\":", 10, &s, NULL, NULL, NULL, 0) == QRK_ERR_PARSE);
    /* nearest double for long inputs */
    const char *lm = "{\"margin\":3.6672146059500514,\"border\":{\"width\":1e-30}}";
    CHECK(qrk_style_from_json(lm, strlen(lm), &s, NULL, NULL, NULL, 0) == QRK_OK);
    CHECK(s.margin == (double)3.6672146059500514 && s.border.width == (double)1e-30);
    /* arena exhaustion is reported as such */
    const char *lbl = "{\"label\":{\"text\":\"a label that does not fit\"}}";
    char tiny[8];
    CHECK(qrk_style_from_json(lbl, strlen(lbl), &s, NULL, NULL, tiny, sizeof tiny) == QRK_ERR_BUFFER_TOO_SMALL);
}

static size_t count_char(const char *s, char c)
{
    size_t n = 0;
    for (; *s; s++)
        n += *s == c;
    return n;
}

static void test_svg(void)
{
    qrk_style s;
    qrk_style_default(&s);
    qrk_encode_opts o;
    qrk_encode_opts_default(&o);
    o.ecc = QRK_ECC_LOW;
    o.mask = 0;
    o.boost_ecc = false;
    qrk_encode_text(&code, "QRKude", &o, NULL);
    static char svg[1 << 16];
    size_t n = qrk_svg_to_buffer(&code, &s, NULL, svg, sizeof svg);
    CHECK(n > 0 && n == strlen(svg));
    CHECK(strncmp(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\"", 39) == 0);
    CHECK(strstr(svg, "viewBox=\"0 0 29 29\"") != NULL);
    CHECK(strstr(svg, "<rect width=\"29\" height=\"29\" fill=\"#ffffff\"/>") != NULL);
    /* 3 eye frames = 3 outer + 3 inner sub-paths, 3 balls */
    CHECK(count_char(svg, 'Z') > 9);

    /* truncation keeps the full length */
    char small[16];
    CHECK(qrk_svg_to_buffer(&code, &s, NULL, small, sizeof small) == n && strlen(small) == 15);

    /* logo damage grows with the logo */
    qrk_encode_opts_default(&o);
    o.ecc = QRK_ECC_HIGH;
    qrk_encode_text(&code, "https://example.com/some/longer/path?with=query", &o, NULL);
    s.logo.size = 0.15;
    double small_damage = qrk_logo_damage(&code, &s);
    s.logo.size = 0.35;
    double big_damage = qrk_logo_damage(&code, &s);
    CHECK(small_damage > 0 && small_damage < big_damage);
    CHECK(big_damage > 0.8);
}

static void count_span(void *user, int x, int y, int len, qrk_color color)
{
    (void)x; (void)y; (void)len; (void)color;
    (*(int *)user)++;
}

static void test_raster_bounds(void)
{
    qrk_style s;
    qrk_style_default(&s);
    CHECK(qrk_encode_text(&code, "bounds", NULL, NULL) == QRK_OK);
    qrk_raster_opts o;
    qrk_raster_opts_default(&o, 9, 2);
    o.supersample = 1;
    for (int f = QRK_PIXEL_RGBA8888; f <= QRK_PIXEL_MONO; f++) {
        qrk_pixel_format format = (qrk_pixel_format)f;
        size_t row = qrk_pixel_buffer_size(format, o.width, 1);
        size_t stride = row + 3;
        uint8_t packed[72], padded[80], untouched[80];
        memset(packed, 0xA5, sizeof packed);
        memset(padded, 0xA5, sizeof padded);
        memcpy(untouched, padded, sizeof padded);
        /* Invalid stride must be rejected before even the first row is written. */
        CHECK(qrk_render_buffer(&code, &s, &o, format, padded, row - 1) == QRK_ERR_INVALID_ARGUMENT);
        CHECK(memcmp(padded, untouched, sizeof padded) == 0);
        CHECK(qrk_render_buffer(&code, &s, &o, format, padded, SIZE_MAX) == QRK_ERR_INVALID_ARGUMENT);
        CHECK(memcmp(padded, untouched, sizeof padded) == 0);
        CHECK(qrk_render_buffer(&code, &s, &o, format, packed, 0) == QRK_OK);
        CHECK(qrk_render_buffer(&code, &s, &o, format, padded, stride) == QRK_OK);
        CHECK(memcmp(packed, padded, row) == 0);
        CHECK(memcmp(packed + row, padded + stride, row) == 0);
        CHECK(memcmp(padded + row, untouched + row, stride - row) == 0);
        CHECK(memcmp(padded + stride + row, untouched + stride + row,
                     sizeof padded - stride - row) == 0);
        CHECK(qrk_pixel_buffer_size(format, -1, 2) == 0);
        CHECK(qrk_pixel_buffer_size(format, 9, 0) == 0);
        CHECK(qrk_pixel_buffer_size(format, 9, -1) == 0);
    }
    CHECK(qrk_pixel_buffer_size(QRK_PIXEL_MONO, INT_MAX, 1) == (size_t)INT_MAX / 8 + 1);
    if ((size_t)INT_MAX > SIZE_MAX / 4)
        CHECK(qrk_pixel_buffer_size(QRK_PIXEL_RGBA8888, INT_MAX, 1) == 0);
    if ((size_t)INT_MAX > SIZE_MAX / (size_t)INT_MAX)
        CHECK(qrk_pixel_buffer_size(QRK_PIXEL_GRAY8, INT_MAX, INT_MAX) == 0);
    uint8_t pixels[80], untouched[80];
    memset(pixels, 0xA5, sizeof pixels);
    memcpy(untouched, pixels, sizeof pixels);
    CHECK(qrk_pixel_buffer_size((qrk_pixel_format)-1, 9, 2) == 0);
    CHECK(qrk_render_buffer(&code, &s, &o, (qrk_pixel_format)-1, pixels, 0) == QRK_ERR_INVALID_ARGUMENT);
    CHECK(qrk_render_buffer(&code, &s, &o, (qrk_pixel_format)(QRK_PIXEL_MONO + 1), pixels, 0) == QRK_ERR_INVALID_ARGUMENT);
    CHECK(memcmp(pixels, untouched, sizeof pixels) == 0);

    /* Logo metadata is checked even for the streaming API, before callbacks. */
    qrk_image logo = {9, 2, QRK_PIXEL_RGBA8888, pixels, 1};
    o.logo = &logo;
    s.logo.size = 0.2;
    int spans = 0;
    CHECK(qrk_render_spans(&code, &s, &o, count_span, &spans) == QRK_ERR_INVALID_ARGUMENT);
    CHECK(spans == 0);
    logo.stride = SIZE_MAX;
    CHECK(qrk_render_buffer(&code, &s, &o, QRK_PIXEL_RGBA8888, pixels, 0) == QRK_ERR_INVALID_ARGUMENT);
    CHECK(memcmp(pixels, untouched, sizeof pixels) == 0);
    logo.stride = 0;
    logo.pixels = NULL;
    CHECK(qrk_render_spans(&code, &s, &o, count_span, &spans) == QRK_ERR_INVALID_ARGUMENT);
    logo.width = -1;
    CHECK(qrk_render_spans(&code, &s, &o, count_span, &spans) == QRK_ERR_INVALID_ARGUMENT);
    logo.width = 0; /* explicitly empty bitmaps are ignored */
    CHECK(qrk_render_spans(&code, &s, &o, count_span, &spans) == QRK_OK);
    CHECK(spans > 0);
}

static void test_hardening(void)
{
    /* id prefix cannot carry markup */
    qrk_style s;
    qrk_style_default(&s);
    s.modules.paint = qrk_paint_linear(0, QRK_HEX(0x000000), QRK_HEX(0x112233));
    qrk_encode_text(&code, "x", NULL, NULL);
    qrk_svg_opts o;
    qrk_svg_opts_default(&o);
    o.id_prefix = "x\"/><svg onload=\"alert(1)\">";
    static char svg[1 << 16];
    qrk_svg_to_buffer(&code, &s, &o, svg, sizeof svg);
    CHECK(strstr(svg, "onload=") == NULL && strstr(svg, "id=\"x____svg_onload__alert_1___-fg\"") != NULL);
    o.id_prefix = "9";
    qrk_svg_to_buffer(&code, &s, &o, svg, sizeof svg);
    CHECK(strstr(svg, "id=\"q9-fg\"") != NULL);

    /* invisible modules are reported */
    s.modules.paint.type = QRK_PAINT_NONE;
    CHECK((qrk_lint(&code, &s) & (QRK_LINT_INVISIBLE | QRK_LINT_LOW_CONTRAST)) ==
          (QRK_LINT_INVISIBLE | QRK_LINT_LOW_CONTRAST));
    /* half-transparent black on black is low contrast */
    s.modules.paint = qrk_paint_solid(QRK_RGBA(0, 0, 0, 128));
    s.background.paint = qrk_paint_solid(QRK_HEX(0x000000));
    CHECK(qrk_lint(&code, &s) & QRK_LINT_LOW_CONTRAST);

    qrk_style_default(&s);
    s.modules.shape = QRK_MODULE_DOT;
    s.modules.size = 0.6;
    CHECK(!(qrk_lint(&code, &s) & QRK_LINT_SMALL_MODULES));
    s.modules.size = 0.59;
    CHECK(qrk_lint(&code, &s) & QRK_LINT_SMALL_MODULES);

    /* NaN / huge lengths are clamped, numbers stay finite */
    qrk_style_default(&s);
    s.margin = 1e300;
    s.border.width = 0.0 / 0.0;
    qrk_layout lay;
    qrk_get_layout(&code, &s, &lay);
    CHECK(lay.symbol_x == 1000 && lay.width == code.size + 2000);

    /* JSON limits */
    char deep[256];
    int n = 0;
    n += sprintf(deep + n, "{\"x\":");
    for (int i = 0; i < 63; i++) deep[n++] = '[';
    for (int i = 0; i < 63; i++) deep[n++] = ']';
    deep[n++] = '}';
    deep[n] = 0;
    CHECK(qrk_style_from_json(deep, strlen(deep), &s, NULL, NULL, NULL, 0) == QRK_OK);
    memmove(deep + 6, deep + 5, strlen(deep + 5) + 1);
    deep[5] = '[';
    char *end = strrchr(deep, '}');
    memmove(end + 1, end, 2);
    *end = ']';
    CHECK(qrk_style_from_json(deep, strlen(deep), &s, NULL, NULL, NULL, 0) == QRK_ERR_PARSE);
    CHECK(qrk_style_from_json("{\"x\":1e999}", 11, &s, NULL, NULL, NULL, 0) == QRK_ERR_PARSE);
    qrk_encode_opts e;
    qrk_encode_opts_default(&e);
    const char *enc_json = "{\"encode\":{\"minVersion\":1e100,\"mask\":-1.7}}";
    CHECK(qrk_style_from_json(enc_json, strlen(enc_json), NULL, &e, NULL, NULL, 0) == QRK_OK);
    CHECK(e.min_version == 1000 && e.mask == -1);
    CHECK(qrk_encode_text(&code, "x", &e, NULL) == QRK_ERR_INVALID_ARGUMENT);

    /* strings: lone surrogates -> U+FFFD, control characters dropped in SVG */
    char arena[128];
    const char *sj = "{\"label\":{\"text\":\"a\\u0001b\\t\\ud800c\\ud83d\\ude00\"}}";
    qrk_style_default(&s);
    CHECK(qrk_style_from_json(sj, strlen(sj), &s, NULL, NULL, arena, sizeof arena) == QRK_OK);
    CHECK_STR(s.label.text, "a\x01" "b\t\xef\xbf\xbd" "c\xf0\x9f\x98\x80");
    qrk_svg_to_buffer(&code, &s, NULL, svg, sizeof svg);
    CHECK(strstr(svg, ">ab\t\xef\xbf\xbd" "c\xf0\x9f\x98\x80</text>") != NULL);

    char buf[64];
    qrk_payload_geo(buf, sizeof buf, 95.5, -200);
    CHECK_STR(buf, "geo:90,-180");
}

int main(void)
{
    test_raster_bounds();
    test_hardening();
    test_capacity();
    test_modes();
    test_numbers();
    test_colors();
    test_payloads();
    test_json();
    test_svg();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
