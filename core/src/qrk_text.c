/*
 * QRKude - text output for terminals and serial monitors (SPEC 14).
 * The output is byte-identical across all ports.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "qrk_internal.h"

#define TEXT_MARGIN_MAX 16

static const char *const HALF[4] = {" ", "\xE2\x96\x80", "\xE2\x96\x84", "\xE2\x96\x88"};
static const char *const FULL[2] = {"  ", "\xE2\x96\x88\xE2\x96\x88"};
static const char *const HASH[2] = {"  ", "##"};

void qrk_text_opts_default(qrk_text_opts *opts)
{
    opts->style = QRK_TEXT_HALF_BLOCKS;
    opts->margin = 2;
    opts->invert = false;
    opts->colors = QRK_TEXT_COLORS_NONE;
}

/* Copies the options (or the defaults) with the margin clamped. */
static bool resolve(const qrk_text_opts *opts, qrk_text_opts *o)
{
    if (opts)
        *o = *opts;
    else
        qrk_text_opts_default(o);
    if (o->style > QRK_TEXT_HASH || o->colors > QRK_TEXT_COLORS_ANSI)
        return false;
    if (o->margin < 0)
        o->margin = 0;
    else if (o->margin > TEXT_MARGIN_MAX)
        o->margin = TEXT_MARGIN_MAX;
    return true;
}

static int ink(const qrk_code *code, const qrk_text_opts *o, int x, int y)
{
    return qrk_get_module(code, x, y) != o->invert;
}

qrk_status qrk_text_write(const qrk_code *code, const qrk_text_opts *opts,
                          qrk_write_fn write, void *user)
{
    qrk_text_opts o;
    if (!code || !write || !resolve(opts, &o))
        return QRK_ERR_INVALID_ARGUMENT;
    const int n = code->size, m = o.margin;
    const int step = o.style == QRK_TEXT_HALF_BLOCKS ? 2 : 1;
    const char *const *cells = o.style == QRK_TEXT_HALF_BLOCKS ? HALF
                             : o.style == QRK_TEXT_FULL_BLOCKS ? FULL
                                                               : HASH;
    for (int y = -m; y < n + m; y += step) {
        if (o.colors == QRK_TEXT_COLORS_ANSI)
            write(user, "\x1b[30;107m", 9);
        for (int x = -m; x < n + m; x++) {
            int index = ink(code, &o, x, y);
            if (step == 2)
                index |= ink(code, &o, x, y + 1) << 1;
            write(user, cells[index], strlen(cells[index]));
        }
        if (o.colors == QRK_TEXT_COLORS_ANSI)
            write(user, "\x1b[0m", 4);
        write(user, "\n", 1);
    }
    return QRK_OK;
}

typedef struct {
    char *buf;
    size_t cap, len;
} text_buf;

static void buf_write(void *u, const char *data, size_t len)
{
    text_buf *b = (text_buf *)u;
    for (size_t i = 0; i < len; i++, b->len++)
        if (b->len + 1 < b->cap)
            b->buf[b->len] = data[i];
}

size_t qrk_text_to_buffer(const qrk_code *code, const qrk_text_opts *opts, char *buf,
                          size_t cap)
{
    text_buf b = {buf, buf ? cap : 0, 0};
    if (qrk_text_write(code, opts, buf_write, &b) != QRK_OK) {
        if (b.cap > 0)
            buf[0] = 0;
        return 0;
    }
    if (b.cap > 0)
        buf[b.len < b.cap ? b.len : b.cap - 1] = 0;
    return b.len;
}

void qrk_text_size(const qrk_code *code, const qrk_text_opts *opts, int *columns, int *lines)
{
    qrk_text_opts o;
    int cols = 0, rows = 0;
    if (code && resolve(opts, &o)) {
        int side = code->size + 2 * o.margin;
        cols = o.style == QRK_TEXT_HALF_BLOCKS ? side : 2 * side;
        rows = o.style == QRK_TEXT_HALF_BLOCKS ? (side + 1) / 2 : side;
    }
    if (columns)
        *columns = cols;
    if (lines)
        *lines = rows;
}
