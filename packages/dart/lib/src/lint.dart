// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Scannability checks (port of `core/src/qrk_lint.c`, SPEC 11).
library;

import 'dart:math' as math;

import 'color.dart';
import 'encoder.dart';
import 'engine.dart';
import 'layout.dart';
import 'style.dart';

/// The warnings [lint] can report, in the order it reports them. Each is
/// a plain string so it can be shown, logged or compared across ports.
abstract final class QRKudeLint {
  /// The logo hides 70% to 100% of what error correction can repair: it
  /// may scan, but any extra damage breaks it. Use a smaller logo.
  static const logoRisky = 'logo-risky';

  /// The logo hides more than error correction can repair: the code cannot
  /// be decoded. Use a smaller logo or ECC `H`.
  static const logoUnreadable = 'logo-unreadable';

  /// Foreground and background are too similar (WCAG contrast ratio below
  /// 3:1). Darken the modules or lighten the background.
  static const lowContrast = 'low-contrast';

  /// Light modules on a dark background. Many scanners handle it, some do
  /// not.
  static const inverted = 'inverted';

  /// The background is missing or fully transparent, so the code depends on
  /// whatever it is placed on (lint assumes white).
  static const transparentBackground = 'transparent-background';

  /// The quiet zone (margin) is below 2 modules; use 4.
  static const quietZone = 'quiet-zone';

  /// Modules are drawn smaller than 60% of their cell (size below 0.6);
  /// small dots are hard to read from a distance.
  static const smallModules = 'small-modules';

  /// A diamond eye frame, or a diamond ball in a square or leaf frame:
  /// combinations that measurably break finder detection.
  static const eyeShapes = 'eye-shapes';

  /// The modules, eye frames or eye balls have no visible color (none, or
  /// alpha 0 everywhere): nothing to scan.
  static const invisible = 'invisible';

  /// Every flag, in report order.
  static const all = [
    logoRisky,
    logoUnreadable,
    lowContrast,
    inverted,
    transparentBackground,
    quietZone,
    smallModules,
    eyeShapes,
    invisible,
  ];
}

double _channel(double v) => v <= 0.04045 ? v / 12.92 : math.pow((v + 0.055) / 1.055, 2.4).toDouble();

const List<double> _weights = [0.2126, 0.7152, 0.0722];

/// WCAG relative luminance of `c` composited over the opaque color [under]
/// (per channel `c·a + u·(1 − a)` in floating point, not rounded).
double _luminanceOver(int c, int under) {
  final a = QRKudeColor.alpha(c) / 255.0;
  final cs = [QRKudeColor.red(c), QRKudeColor.green(c), QRKudeColor.blue(c)];
  final us = [QRKudeColor.red(under), QRKudeColor.green(under), QRKudeColor.blue(under)];
  var l = 0.0;
  for (var i = 0; i < 3; i++) {
    final v = cs[i] / 255.0, u = us[i] / 255.0;
    l += _weights[i] * _channel(v * a + u * (1 - a));
  }
  return l;
}

/// The colors a paint can produce: its color or its (first 8) stops. A
/// missing paint, or a gradient without stops, is one transparent color.
List<int> _paintColors(QRKudePaint? p) {
  switch (p) {
    case QRKudeSolid(:final color):
      return [color];
    case QRKudeGradient():
      final stops = p.effectiveStops;
      if (stops.isNotEmpty) return [for (final s in stops) s.color];
      return const [QRKudeColor.transparent];
    case null:
      return const [QRKudeColor.transparent];
  }
}

/// `c` made opaque over white with 8-bit rounding:
/// `floor(c·a + 255·(1 − a) + 0.5)` per channel.
int _opaqueOverWhite(int c) {
  final a = QRKudeColor.alpha(c) / 255.0;
  int ch(int v) => (v * a + 255 * (1 - a) + 0.5).floor();
  return QRKudeColor.rgba(ch(QRKudeColor.red(c)), ch(QRKudeColor.green(c)), ch(QRKudeColor.blue(c)));
}

/// Checks a styled code for problems that make it hard or impossible to
/// scan, and returns the flags found (see [QRKudeLint] for what each means
/// and how to fix it), in table order. An empty list means no warning.
///
/// It is a static check, not a scan: test real codes with real phones
/// (`docs/GUIDE.md#testing-your-codes`).
List<String> lint(QRCode code, QRKudeStyle style) {
  final flags = <String>{};
  final L = LayoutContext(code, style);

  if (L.hasLogo) {
    final d = L.logoDamage();
    if (d > 1) {
      flags.add(QRKudeLint.logoUnreadable);
    } else if (d > 0.7) {
      flags.add(QRKudeLint.logoRisky);
    }
  }

  // background colors, made opaque over white (what a page usually shows)
  final bgOriginal = _paintColors(style.background.color);
  if (bgOriginal.every((c) => QRKudeColor.alpha(c) == 0)) flags.add(QRKudeLint.transparentBackground);
  final bg = [for (final c in bgOriginal) _opaqueOverWhite(c)];
  var bgLo = 1.0, bgHi = 0.0;
  for (final c in bg) {
    final l = _luminanceOver(c, c);
    if (l < bgLo) bgLo = l;
    if (l > bgHi) bgHi = l;
  }

  // foreground: modules and eyes composited over every background color
  var fgLo = 1.0, fgHi = 0.0;
  for (final layer in const [QRKudeLayer.modules, QRKudeLayer.eyeFrames, QRKudeLayer.eyeBalls]) {
    final fg = _paintColors(layerPaint(style, layer));
    var visible = false;
    for (final f in fg) {
      if (QRKudeColor.alpha(f) != 0) visible = true;
      for (final b in bg) {
        final l = _luminanceOver(f, b);
        if (l < fgLo) fgLo = l;
        if (l > fgHi) fgHi = l;
      }
    }
    if (!visible) flags.add(QRKudeLint.invisible);
  }

  double contrast;
  if ((fgLo + fgHi) / 2 > (bgLo + bgHi) / 2) {
    flags.add(QRKudeLint.inverted);
    contrast = (fgLo + 0.05) / (bgHi + 0.05);
  } else {
    contrast = (bgLo + 0.05) / (fgHi + 0.05);
  }
  if (contrast < 3) flags.add(QRKudeLint.lowContrast);

  if (L.margin < 2) flags.add(QRKudeLint.quietZone);

  switch (L.shape) {
    case ModuleShape.square:
    case ModuleShape.dot:
    case ModuleShape.rounded:
    case ModuleShape.diamond:
    case ModuleShape.star:
    case ModuleShape.hbars:
    case ModuleShape.vbars:
      if (L.size < 0.6) flags.add(QRKudeLint.smallModules);
    case ModuleShape.fluid:
    case ModuleShape.liquid:
    case ModuleShape.bevel:
    case ModuleShape.classy:
      break;
  }

  // Measured with zxing: diamond frames break finder detection with any
  // ball; diamond balls only work inside round or beveled frames.
  final frame = style.eyes.frame, ball = ballShape(style.eyes.ball);
  if (frame == EyeShape.diamond || (ball == EyeShape.diamond && (frame == EyeShape.square || frame == EyeShape.leaf))) {
    flags.add(QRKudeLint.eyeShapes);
  }

  return [
    for (final f in QRKudeLint.all)
      if (flags.contains(f)) f
  ];
}
