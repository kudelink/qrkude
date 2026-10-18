/*
 * Checks the headers written by tools/img2qrk.py: every pixel format
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 * compiles, renders as a logo with the same colors, and the QR code still
 * decodes (check_decode.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qrkude.h"

#include "logo_RGB565.h"
#include "logo_RGB565_SWAPPED.h"
#include "logo_RGBA8888.h"
#include "logo_BGRA8888.h"
#include "logo_RGB888.h"
#include "logo_GRAY8.h"
#include "logo_MONO.h"

#define SIZE 240

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    const struct {
        const char *name;
        const qrk_image *img;
    } logos[] = {
        {"RGB565", &logo_RGB565},     {"RGB565_SWAPPED", &logo_RGB565_SWAPPED},
        {"RGBA8888", &logo_RGBA8888}, {"BGRA8888", &logo_BGRA8888},
        {"RGB888", &logo_RGB888},     {"GRAY8", &logo_GRAY8},
        {"MONO", &logo_MONO},
    };
    static uint8_t rgb[SIZE * SIZE * 3];
    static qrk_code code;
    static uint8_t scratch[QRK_BUFFER_LEN_MAX];
    const char *text = "https://github.com/kudelink/qrkude";
    int failures = 0;
    char path[512];
    snprintf(path, sizeof path, "%s/manifest.json", out);
    FILE *manifest = fopen(path, "w");
    if (!manifest)
        return 1;
    fputs("[\n", manifest);

    qrk_style style;
    qrk_style_default(&style);
    style.logo.size = 0.22;
    style.logo.margin = 0.5;
    style.logo.background = QRK_HEX(0xFFFFFF);
    qrk_encode_opts enc;
    qrk_encode_opts_default(&enc);
    enc.ecc = qrk_recommended_ecc(&style);
    if (qrk_encode_text(&code, text, &enc, scratch) != QRK_OK)
        return 1;

    uint8_t ref[3] = {0, 0, 0};
    for (size_t i = 0; i < sizeof logos / sizeof logos[0]; i++) {
        qrk_raster_opts ro;
        qrk_raster_opts_default(&ro, SIZE, SIZE);
        ro.supersample = 1;
        ro.logo = logos[i].img;
        if (qrk_render_buffer(&code, &style, &ro, QRK_PIXEL_RGB888, rgb, 0) != QRK_OK)
            return 1;
        /* Logo center pixel: same color for every color format. */
        const uint8_t *c = rgb + ((SIZE / 2) * SIZE + SIZE / 2 + 3) * 3;
        int color = strcmp(logos[i].name, "GRAY8") != 0 && strcmp(logos[i].name, "MONO") != 0;
        if (i == 0)
            memcpy(ref, c, 3);
        else if (color && (abs(c[0] - ref[0]) > 8 || abs(c[1] - ref[1]) > 8 || abs(c[2] - ref[2]) > 8)) {
            fprintf(stderr, "FAIL %s: center %02x%02x%02x, RGB565 gives %02x%02x%02x\n",
                    logos[i].name, c[0], c[1], c[2], ref[0], ref[1], ref[2]);
            failures++;
        }
        printf("%-15s %dx%d center #%02x%02x%02x\n", logos[i].name, logos[i].img->width,
               logos[i].img->height, c[0], c[1], c[2]);
        snprintf(path, sizeof path, "%s/logo_%s.ppm", out, logos[i].name);
        FILE *f = fopen(path, "wb");
        if (!f)
            return 1;
        fprintf(f, "P6\n%d %d\n255\n", SIZE, SIZE);
        fwrite(rgb, 1, sizeof rgb, f);
        fclose(f);
        fprintf(manifest, "%s  {\"file\": \"logo_%s.ppm\", \"expect\": \"%s\"}", i ? ",\n" : "",
                logos[i].name, text);
    }
    fputs("\n]\n", manifest);
    fclose(manifest);
    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
