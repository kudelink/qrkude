// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Payload builders (port of `core/src/qrk_payload.c`, SPEC 12).
library;

import 'dart:convert' show utf8;

import 'svg.dart' show formatNumber6;

bool _empty(String? s) => s == null || s.isEmpty;

/// Backslash-escapes every character of [special]; newlines become `\n`.
String _escaped(String? s, String special) {
  if (s == null) return '';
  final sb = StringBuffer();
  for (var i = 0; i < s.length; i++) {
    final c = s[i];
    if (c == '\n') {
      sb.write(r'\n');
      continue;
    }
    if (special.contains(c)) sb.write(r'\');
    sb.write(c);
  }
  return sb.toString();
}

String _percent(String s) {
  const hex = '0123456789ABCDEF';
  final sb = StringBuffer();
  for (final c in utf8.encode(s)) {
    final keep = (c >= 0x41 && c <= 0x5A) ||
        (c >= 0x61 && c <= 0x7A) ||
        (c >= 0x30 && c <= 0x39) ||
        c == 0x2D ||
        c == 0x5F ||
        c == 0x2E ||
        c == 0x7E;
    if (keep) {
      sb.writeCharCode(c);
    } else {
      sb
        ..write('%')
        ..write(hex[c >> 4])
        ..write(hex[c & 15]);
    }
  }
  return sb.toString();
}

/// Builders for common QR payloads. Optional values are omitted when null or
/// empty. The outputs match every QRKude port byte for byte.
abstract final class QRKudePayload {
  /// Wi-Fi network. [auth]: `WPA`, `WEP`, `nopass` or null.
  static String wifi({required String ssid, String? password, String? auth, bool hidden = false}) {
    const special = '\\;,:"';
    final sb = StringBuffer('WIFI:');
    if (!_empty(auth)) sb.write('T:$auth;');
    sb.write('S:${_escaped(ssid, special)};');
    if (!_empty(password) && auth != 'nopass') sb.write('P:${_escaped(password, special)};');
    if (hidden) sb.write('H:true;');
    sb.write(';');
    return sb.toString();
  }

  /// `mailto:` link with optional percent-encoded subject and body.
  static String email({required String to, String? subject, String? body}) {
    final sb = StringBuffer('mailto:$to');
    var sep = '?';
    if (!_empty(subject)) {
      sb.write('${sep}subject=${_percent(subject!)}');
      sep = '&';
    }
    if (!_empty(body)) sb.write('${sep}body=${_percent(body!)}');
    return sb.toString();
  }

  /// `SMSTO:` payload.
  static String sms({required String number, String? message}) =>
      _empty(message) ? 'SMSTO:$number' : 'SMSTO:$number:$message';

  /// `tel:` link.
  static String tel(String number) => 'tel:$number';

  /// `geo:` URI with up to 6 decimals; latitude is clamped to -90..90 and
  /// longitude to -180..180 (NaN clamps to the lower bound).
  static String geo(double latitude, double longitude) =>
      'geo:${formatNumber6(_clamp(latitude, -90, 90))},${formatNumber6(_clamp(longitude, -180, 180))}';

  static double _clamp(double v, double lo, double hi) => !(v >= lo) ? lo : (v > hi ? hi : v);

  /// vCard 3.0 contact.
  static String vcard({
    String? firstName,
    String? lastName,
    String? organization,
    String? title,
    String? phone,
    String? email,
    String? url,
    String? address,
    String? note,
  }) {
    const special = '\\;,';
    final sb = StringBuffer('BEGIN:VCARD\nVERSION:3.0\nN:');
    sb
      ..write(_escaped(lastName, special))
      ..write(';')
      ..write(_escaped(firstName, special))
      ..write(';;;\nFN:')
      ..write(_escaped(firstName, special));
    if (!_empty(firstName) && !_empty(lastName)) sb.write(' ');
    sb
      ..write(_escaped(lastName, special))
      ..write('\n');
    _line(sb, 'ORG', organization);
    _line(sb, 'TITLE', title);
    _line(sb, 'TEL', phone);
    _line(sb, 'EMAIL', email);
    _line(sb, 'URL', url);
    if (!_empty(address)) sb.write('ADR:;;${_escaped(address, special)};;;;\n');
    _line(sb, 'NOTE', note);
    sb.write('END:VCARD');
    return sb.toString();
  }

  /// iCalendar VEVENT. [start] and [end] are timestamps such as
  /// `20261017T120000Z`.
  static String event({
    required String summary,
    String? start,
    String? end,
    String? location,
    String? description,
  }) {
    final sb = StringBuffer('BEGIN:VEVENT\n');
    _line(sb, 'SUMMARY', summary, always: true);
    _line(sb, 'DTSTART', start);
    _line(sb, 'DTEND', end);
    _line(sb, 'LOCATION', location);
    _line(sb, 'DESCRIPTION', description);
    sb.write('END:VEVENT');
    return sb.toString();
  }

  static void _line(StringBuffer sb, String key, String? value, {bool always = false}) {
    if (!always && _empty(value)) return;
    sb.write('$key:${_escaped(value, '\\;,')}\n');
  }
}
