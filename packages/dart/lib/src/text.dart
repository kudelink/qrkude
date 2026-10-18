// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// String rules shared by the JSON reader and the SVG writer (SPEC 2.0,
/// 8.3).
library;

/// SPEC 2.0 strings: a string ends at its first U+0000 (the C core uses C
/// strings) and a surrogate that is not part of a valid high/low pair
/// becomes U+FFFD.
String normalizeString(String s) {
  final nul = s.indexOf('\u0000');
  if (nul >= 0) s = s.substring(0, nul);
  var clean = true;
  for (var i = 0; i < s.length; i++) {
    final c = s.codeUnitAt(i);
    if (c >= 0xD800 && c <= 0xDFFF) {
      clean = false;
      break;
    }
  }
  if (clean) return s;
  final sb = StringBuffer();
  for (var i = 0; i < s.length; i++) {
    final c = s.codeUnitAt(i);
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.length) {
      final lo = s.codeUnitAt(i + 1);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        sb
          ..writeCharCode(c)
          ..writeCharCode(lo);
        i++;
        continue;
      }
    }
    sb.writeCharCode(c >= 0xD800 && c <= 0xDFFF ? 0xFFFD : c);
  }
  return sb.toString();
}

/// Text as the SVG writer emits it (before XML escaping): [normalizeString]
/// and without the characters U+0001-U+001F other than tab, LF and CR,
/// which XML 1.0 does not allow (SPEC 8.3). Renderers that draw the label
/// themselves should show this text.
String sanitizeText(String s) {
  s = normalizeString(s);
  for (var i = 0; i < s.length; i++) {
    final c = s.codeUnitAt(i);
    if (c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D) {
      final sb = StringBuffer();
      for (var j = 0; j < s.length; j++) {
        final d = s.codeUnitAt(j);
        if (d >= 0x20 || d == 0x09 || d == 0x0A || d == 0x0D) sb.writeCharCode(d);
      }
      return sb.toString();
    }
  }
  return s;
}

/// Whether a string is empty under the U+0000 rule (C: `!s || !s[0]`).
bool isEmptyString(String? s) => s == null || s.isEmpty || s.codeUnitAt(0) == 0;
