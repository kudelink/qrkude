/*
 * QRKude - payload builders (Wi-Fi, vCard, event, mailto, SMS, tel, geo).
 * Port of core/src/qrk_payload.c.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { utf8 } from './encode.js';

type Opt = string | null | undefined;

function empty(s: Opt): boolean {
  return !s;
}

/* Backslash-escapes every character of `special`; newlines become "\n". */
function escaped(s: Opt, special: string): string {
  if (!s) return '';
  let out = '';
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '\n') {
      out += '\\n';
      continue;
    }
    if (special.indexOf(c) >= 0) out += '\\';
    out += c;
  }
  return out;
}

const HEX = '0123456789ABCDEF';

function percent(s: string): string {
  let out = '';
  const bytes = utf8(s);
  for (let i = 0; i < bytes.length; i++) {
    const c = bytes[i];
    if (
      (c >= 0x41 && c <= 0x5a) || (c >= 0x61 && c <= 0x7a) || (c >= 0x30 && c <= 0x39) ||
      c === 0x2d || c === 0x5f || c === 0x2e || c === 0x7e
    )
      out += String.fromCharCode(c);
    else out += '%' + HEX[c >> 4] + HEX[c & 15];
  }
  return out;
}

/** Wi-Fi network. `auth`: `"WPA"`, `"WEP"`, `"nopass"` or null. */
export function wifi(ssid: string, password?: Opt, auth?: Opt, hidden = false): string {
  const special = '\\;,:"';
  let out = 'WIFI:';
  if (!empty(auth)) out += 'T:' + auth + ';';
  out += 'S:' + escaped(ssid, special) + ';';
  if (!empty(password) && auth !== 'nopass') out += 'P:' + escaped(password, special) + ';';
  if (hidden) out += 'H:true;';
  return out + ';';
}

/** `mailto:` link with optional subject and body (percent-encoded). */
export function email(to: string, subject?: Opt, body?: Opt): string {
  let out = 'mailto:' + (to || '');
  let sep = '?';
  if (!empty(subject)) {
    out += sep + 'subject=' + percent(subject as string);
    sep = '&';
  }
  if (!empty(body)) out += sep + 'body=' + percent(body as string);
  return out;
}

/** `SMSTO:` payload. */
export function sms(number: string, message?: Opt): string {
  return 'SMSTO:' + (number || '') + (empty(message) ? '' : ':' + message);
}

/** `tel:` link. */
export function tel(number: string): string {
  return 'tel:' + (number || '');
}

/* Same rounding rule as formatNumber, with 6 decimals. */
function fixed6(v: number): string {
  let n = Math.floor(v * 1000000.0 + 0.5);
  let out = '';
  if (n < 0) {
    out = '-';
    n = -n;
  }
  const fp = n % 1000000;
  const ip = (n - fp) / 1000000;
  out += String(ip);
  if (fp) {
    let frac = String(fp);
    while (frac.length < 6) frac = '0' + frac;
    out += '.' + frac.replace(/0+$/, '');
  }
  return out;
}

/* NaN clamps to lo, like the reference. */
function clamp(v: number, lo: number, hi: number): number {
  if (!(v >= lo)) return lo;
  return v > hi ? hi : v;
}

/** `geo:` URI with up to 6 decimals; latitude is clamped to [-90, 90] and
 * longitude to [-180, 180]. */
export function geo(latitude: number, longitude: number): string {
  return 'geo:' + fixed6(clamp(latitude, -90, 90)) + ',' + fixed6(clamp(longitude, -180, 180));
}

export interface VCard {
  firstName?: Opt;
  lastName?: Opt;
  organization?: Opt;
  title?: Opt;
  phone?: Opt;
  email?: Opt;
  url?: Opt;
  address?: Opt;
  note?: Opt;
}

function vline(key: string, value: Opt, always = false): string {
  if (!always && empty(value)) return '';
  return key + ':' + escaped(value, '\\;,') + '\n';
}

/** vCard 3.0 contact. */
export function vcard(card: VCard): string {
  const special = '\\;,';
  let out = 'BEGIN:VCARD\nVERSION:3.0\nN:';
  out += escaped(card.lastName, special) + ';' + escaped(card.firstName, special) + ';;;\nFN:';
  out += escaped(card.firstName, special);
  if (!empty(card.firstName) && !empty(card.lastName)) out += ' ';
  out += escaped(card.lastName, special) + '\n';
  out += vline('ORG', card.organization);
  out += vline('TITLE', card.title);
  out += vline('TEL', card.phone);
  out += vline('EMAIL', card.email);
  out += vline('URL', card.url);
  if (!empty(card.address)) out += 'ADR:;;' + escaped(card.address, special) + ';;;;\n';
  out += vline('NOTE', card.note);
  return out + 'END:VCARD';
}

export interface CalendarEvent {
  summary?: Opt;
  /** e.g. "20261017T120000Z" */
  start?: Opt;
  end?: Opt;
  location?: Opt;
  description?: Opt;
}

/** iCalendar VEVENT. */
export function event(e: CalendarEvent): string {
  let out = 'BEGIN:VEVENT\n';
  out += vline('SUMMARY', e.summary, true);
  out += vline('DTSTART', e.start);
  out += vline('DTEND', e.end);
  out += vline('LOCATION', e.location);
  out += vline('DESCRIPTION', e.description);
  return out + 'END:VEVENT';
}
