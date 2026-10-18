/*
 * QRKude command line tool.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qrkude.h"

static void usage(void)
{
    fputs(
        "usage: qrkude [options] [TEXT]\n"
        "  -o FILE        output file (.svg or .png); default: SVG on stdout\n"
        "  -s FILE        style JSON file ({\"encode\":..,\"style\":..,\"svg\":..})\n"
        "  -j JSON        style JSON string\n"
        "  -p PX          PNG size in pixels (default 512)\n"
        "  -a N           PNG supersampling 1..4 (default 3)\n"
        "  -l FILE        logo for PNG output: raw RGBA file, needs -L\n"
        "  -L WxH         logo dimensions\n"
        "  --stdin        read the payload from stdin (raw bytes)\n"
        "  --matrix       print the module matrix ('1' = dark)\n"
        "  --info         print version, ecc, mask and logo damage\n",
        stderr);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = path ? fopen(path, "rb") : stdin;
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap + 1);
    size_t r;
    while (buf && (r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) {
            cap *= 2;
            buf = realloc(buf, cap + 1);
        }
    }
    if (path)
        fclose(f);
    if (buf)
        buf[n] = 0;
    *len = n;
    return buf;
}

static void file_write(void *user, const char *data, size_t len)
{
    fwrite(data, 1, len, (FILE *)user);
}

/* ---- minimal PNG writer (stored deflate blocks) -------------------------- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t c[4];
    put32(c, crc);
    fwrite(c, 1, 4, f);
}

static int write_png(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    crc_init();
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);

    size_t raw_len = (size_t)h * ((size_t)w * 4 + 1);
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (w * 4 + 1)] = 0;
        memcpy(raw + (size_t)y * (w * 4 + 1) + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len), *p = z;
    *p++ = 0x78;
    *p++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *p++ = off + n == raw_len ? 1 : 0;
        *p++ = (uint8_t)n;
        *p++ = (uint8_t)(n >> 8);
        *p++ = (uint8_t)~n;
        *p++ = (uint8_t)(~n >> 8);
        memcpy(p, raw + off, n);
        p += n;
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
    }
    put32(p, b << 16 | a);
    p += 4;
    png_chunk(f, "IDAT", z, (size_t)(p - z));
    png_chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------------ */

/* heap buffers released on every exit path */
static char *g_data, *g_json, *g_logo;
static uint8_t *g_pixels;

static void cleanup(void)
{
    free(g_data);
    free(g_json);
    free(g_logo);
    free(g_pixels);
}

int main(int argc, char **argv)
{
    atexit(cleanup);
    const char *out = NULL, *style_file = NULL, *style_json = NULL, *text = NULL;
    const char *logo_file = NULL, *logo_dims = NULL;
    int png_size = 512, aa = 3;
    bool from_stdin = false, matrix = false, info = false;
    /* -p and -a are validated after parsing */

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "-s") && i + 1 < argc) style_file = argv[++i];
        else if (!strcmp(a, "-j") && i + 1 < argc) style_json = argv[++i];
        else if (!strcmp(a, "-p") && i + 1 < argc) png_size = atoi(argv[++i]);
        else if (!strcmp(a, "-a") && i + 1 < argc) aa = atoi(argv[++i]);
        else if (!strcmp(a, "-l") && i + 1 < argc) logo_file = argv[++i];
        else if (!strcmp(a, "-L") && i + 1 < argc) logo_dims = argv[++i];
        else if (!strcmp(a, "--stdin")) from_stdin = true;
        else if (!strcmp(a, "--matrix")) matrix = true;
        else if (!strcmp(a, "--info")) info = true;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else if (a[0] == '-' && a[1]) { usage(); return 2; }
        else text = a;
    }

    if (png_size < 1 || png_size > 16384 || aa < 1 || aa > 4) {
        fprintf(stderr, "-p must be 1..16384 and -a 1..4\n");
        return 2;
    }

    size_t data_len = 0;
    char *data = NULL;
    if (from_stdin) {
        data = g_data = read_file(NULL, &data_len);
    } else if (text) {
        data_len = strlen(text);
        data = g_data = malloc(data_len + 1);
        memcpy(data, text, data_len + 1);
    } else {
        usage();
        return 2;
    }

    qrk_style style;
    qrk_encode_opts enc;
    qrk_svg_opts svg;
    qrk_style_default(&style);
    qrk_encode_opts_default(&enc);
    qrk_svg_opts_default(&svg);

    static char arena[1 << 20];
    char *json = NULL;
    size_t json_len = 0;
    if (style_file) {
        json = g_json = read_file(style_file, &json_len);
        if (!json) { fprintf(stderr, "cannot read %s\n", style_file); return 1; }
    } else if (style_json) {
        json_len = strlen(style_json);
        json = g_json = malloc(json_len + 1);
        memcpy(json, style_json, json_len + 1);
    }
    if (json) {
        qrk_status st = qrk_style_from_json(json, json_len, &style, &enc, &svg, arena, sizeof arena);
        if (st != QRK_OK) {
            fprintf(stderr, "style: %s\n", qrk_status_str(st));
            return 1;
        }
    }

    static qrk_code code;
    qrk_status st = qrk_encode_for_style(&code, data, data_len, &enc, &style, NULL);
    if (st != QRK_OK) {
        fprintf(stderr, "encode: %s\n", qrk_status_str(st));
        return 1;
    }

    if (info) {
        qrk_layout lay;
        qrk_get_layout(&code, &style, &lay);
        printf("logo %d\n", lay.has_logo ? 1 : 0);
        printf("version %d\nsize %d\necc %c\nmask %d\nmode %d\nlogo_damage %.6f\n", code.version,
               code.size, "LMQH"[code.ecc], code.mask, code.mode, qrk_logo_damage(&code, &style));
        uint32_t lint = qrk_lint(&code, &style);
        printf("lint");
        for (uint32_t f = 1; f <= 0x100; f <<= 1)
            if (lint & f)
                printf(" %s", qrk_lint_name(f));
        printf("\n");
    }
    if (matrix) {
        for (int y = 0; y < code.size; y++) {
            for (int x = 0; x < code.size; x++)
                putchar(qrk_get_module(&code, x, y) ? '1' : '0');
            putchar('\n');
        }
    }
    if (info || matrix) {
        if (!out)
            return 0;
    }

    size_t olen = out ? strlen(out) : 0;
    if (out && olen > 4 && !strcmp(out + olen - 4, ".png")) {
        qrk_raster_opts ro;
        qrk_raster_opts_default(&ro, png_size, png_size);
        ro.supersample = aa;
        ro.integer_scale = false;
        ro.clear_color = QRK_TRANSPARENT;
        qrk_layout lay;
        qrk_get_layout(&code, &style, &lay);
        /* clamped style lengths keep this finite; still check before casting */
        double h = png_size * lay.height / lay.width + 0.5;
        if (!(h >= 1 && h <= 16384)) {
            fprintf(stderr, "png: image height out of range\n");
            return 1;
        }
        ro.height = (int)h;
        qrk_image logo;
        char *logo_px = NULL;
        if (logo_file || logo_dims) {
            size_t ll = 0;
            int lw = 0, lh = 0;
            if (!logo_file || !logo_dims || sscanf(logo_dims, "%dx%d", &lw, &lh) != 2 ||
                lw < 1 || lh < 1 || lw > 4096 || lh > 4096) {
                fprintf(stderr, "logo: -l FILE and -L WxH (1..4096) are both required\n");
                return 1;
            }
            logo_px = g_logo = read_file(logo_file, &ll);
            if (!logo_px || ll != (size_t)lw * (size_t)lh * 4) {
                fprintf(stderr, "logo: %s must be exactly %dx%dx4 bytes of RGBA\n", logo_file, lw, lh);
                return 1;
            }
            logo.width = lw;
            logo.height = lh;
            logo.format = QRK_PIXEL_RGBA8888;
            logo.pixels = logo_px;
            logo.stride = 0;
            ro.logo = &logo;
        }
        uint8_t *px = g_pixels = calloc(qrk_pixel_buffer_size(QRK_PIXEL_RGBA8888, ro.width, ro.height), 1);
        if (!px) {
            fprintf(stderr, "png: out of memory\n");
            return 1;
        }
        qrk_render_buffer(&code, &style, &ro, QRK_PIXEL_RGBA8888, px, 0);
        return write_png(out, px, ro.width, ro.height) ? 1 : 0;
    }

    FILE *f = out ? fopen(out, "wb") : stdout;
    if (!f) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    qrk_svg_write(&code, &style, &svg, file_write, f);
    if (out)
        fclose(f);
    return 0;
}
