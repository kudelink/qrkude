// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Helpers for QRKude colors.
///
/// A QRKude color is a plain `int` in `0xRRGGBBAA` order: red, green, blue,
/// then alpha (opacity, `0xFF` = opaque). For example `0x1D3557FF` is an
/// opaque dark blue and `0x00000080` is black at 50% opacity. Note that
/// Flutter's `Color` uses `0xAARRGGBB` instead; see [toArgb].
///
/// All helpers use arithmetic instead of bit operations above 2^31 so they
/// behave identically on the Dart VM and in JavaScript.
abstract final class QRKudeColor {
  /// Opaque black, `0x000000FF`.
  static const int black = 0x000000FF;

  /// Opaque white, `0xFFFFFFFF`.
  static const int white = 0xFFFFFFFF;

  /// Fully transparent, `0x00000000`.
  static const int transparent = 0x00000000;

  /// Builds a color from 0..255 channels; [a] defaults to opaque.
  static int rgba(int r, int g, int b, [int a = 255]) =>
      (r & 0xFF) * 0x1000000 + (g & 0xFF) * 0x10000 + (b & 0xFF) * 0x100 + (a & 0xFF);

  /// Opaque color from a `0xRRGGBB` literal, e.g. `QRKudeColor.hex(0x1D3557)`.
  static int hex(int rgb) => (rgb % 0x1000000) * 0x100 + 0xFF;

  /// Red channel (0..255) of a `0xRRGGBBAA` color.
  static int red(int color) => (color ~/ 0x1000000) % 256;

  /// Green channel (0..255) of a `0xRRGGBBAA` color.
  static int green(int color) => (color ~/ 0x10000) % 256;

  /// Blue channel (0..255) of a `0xRRGGBBAA` color.
  static int blue(int color) => (color ~/ 0x100) % 256;

  /// Alpha channel (0 = invisible, 255 = opaque) of a `0xRRGGBBAA` color.
  static int alpha(int color) => color % 256;

  /// `0xAARRGGBB`, the order used by Flutter's `Color(int)`.
  static int toArgb(int color) => alpha(color) * 0x1000000 + color ~/ 0x100;

  /// Parses `#rgb`, `#rrggbb`, `#rrggbbaa` (case-insensitive) or
  /// `transparent`. Returns null for anything else.
  static int? tryParse(String s) {
    if (s == 'transparent') return transparent;
    final len = s.length;
    if (len < 1 || s.codeUnitAt(0) != 0x23 /* # */) return null;
    if (len != 4 && len != 7 && len != 9) return null;
    final d = <int>[];
    for (var i = 1; i < len; i++) {
      final v = _hexDigit(s.codeUnitAt(i));
      if (v < 0) return null;
      d.add(v);
    }
    switch (len) {
      case 4:
        return rgba(d[0] * 17, d[1] * 17, d[2] * 17);
      case 7:
        return rgba(d[0] * 16 + d[1], d[2] * 16 + d[3], d[4] * 16 + d[5]);
      default:
        return rgba(d[0] * 16 + d[1], d[2] * 16 + d[3], d[4] * 16 + d[5], d[6] * 16 + d[7]);
    }
  }

  /// Like [tryParse] but throws a [FormatException].
  static int parse(String s) {
    final c = tryParse(s);
    if (c == null) throw FormatException('invalid color', s);
    return c;
  }

  /// `#rrggbb` (lower-case), ignoring alpha, as written to SVG.
  static String toRgbHex(int color) => '#${_hex2(red(color))}${_hex2(green(color))}${_hex2(blue(color))}';

  /// `#rrggbb`, or `#rrggbbaa` when the color is not opaque.
  static String toHex(int color) {
    final a = alpha(color);
    return a == 255 ? toRgbHex(color) : '${toRgbHex(color)}${_hex2(a)}';
  }

  static String _hex2(int v) => v.toRadixString(16).padLeft(2, '0');

  static int _hexDigit(int c) {
    if (c >= 0x30 && c <= 0x39) return c - 0x30;
    if (c >= 0x61 && c <= 0x66) return c - 0x61 + 10;
    if (c >= 0x41 && c <= 0x46) return c - 0x41 + 10;
    return -1;
  }
}
