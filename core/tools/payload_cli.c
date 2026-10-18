/*
 * Prints a payload built by the reference implementation (vector generation).
 *   qrkude-payload wifi SSID PASSWORD AUTH HIDDEN(0|1)
 *   qrkude-payload email TO SUBJECT BODY
 *   qrkude-payload sms NUMBER MESSAGE
 *   qrkude-payload tel NUMBER
 *   qrkude-payload geo LAT LON
 *   qrkude-payload vcard FIRST LAST ORG TITLE PHONE EMAIL URL ADDRESS NOTE
 *   qrkude-payload event SUMMARY START END LOCATION DESCRIPTION
 * An argument of "-" means NULL/absent.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qrkude.h"

static const char *arg(int argc, char **argv, int i)
{
    return i < argc && strcmp(argv[i], "-") != 0 ? argv[i] : NULL;
}

int main(int argc, char **argv)
{
    static char out[1 << 16];
    if (argc < 2)
        return 2;
    const char *t = argv[1];
#define A(i) arg(argc, argv, (i) + 2)
    if (!strcmp(t, "wifi"))
        qrk_payload_wifi(out, sizeof out, A(0), A(1), A(2), A(3) && !strcmp(A(3), "1"));
    else if (!strcmp(t, "email"))
        qrk_payload_email(out, sizeof out, A(0), A(1), A(2));
    else if (!strcmp(t, "sms"))
        qrk_payload_sms(out, sizeof out, A(0), A(1));
    else if (!strcmp(t, "tel"))
        qrk_payload_tel(out, sizeof out, A(0));
    else if (!strcmp(t, "geo"))
        qrk_payload_geo(out, sizeof out, strtod(A(0), NULL), strtod(A(1), NULL));
    else if (!strcmp(t, "vcard")) {
        qrk_vcard c = {A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8)};
        qrk_payload_vcard(out, sizeof out, &c);
    } else if (!strcmp(t, "event")) {
        qrk_event e = {A(0), A(1), A(2), A(3), A(4)};
        qrk_payload_event(out, sizeof out, &e);
    } else
        return 2;
    fputs(out, stdout);
    return 0;
}
