// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'dart:collection';
import 'dart:ui' as ui;

import 'package:flutter/painting.dart';
import 'package:qrkude/qrkude.dart';

/// Converts a QRKude `0xRRGGBBAA` color into a Flutter [Color].
Color qrkudeColorToColor(int rgba) => Color.fromARGB(
      QRKudeColor.alpha(rgba),
      QRKudeColor.red(rgba),
      QRKudeColor.green(rgba),
      QRKudeColor.blue(rgba),
    );

/// A [QRKudePathSink] that builds a Flutter [Path] (in module units).
///
/// Pass it to `emitLayer` to get the exact SVG geometry of a layer as a
/// [Path]; scale the canvas to draw it at any size.
class FlutterPathSink implements QRKudePathSink {
  /// Creates a sink that appends to [path] (a new [Path] by default).
  FlutterPathSink([Path? path]) : path = path ?? Path();

  /// The path being built; fill it with [PathFillType.nonZero].
  final Path path;

  @override
  void moveTo(double x, double y) => path.moveTo(x, y);

  @override
  void lineTo(double x, double y) => path.lineTo(x, y);

  @override
  void arcTo(double r, bool clockwise, double x, double y, double cx, double cy) =>
      path.arcToPoint(Offset(x, y), radius: Radius.circular(r), clockwise: clockwise);

  @override
  void close() => path.close();
}

/// Builds the [Paint] of a QRKude paint, with gradients placed exactly like
/// in the SVG output (`gradientUnits="userSpaceOnUse"`, module units).
/// Returns null when nothing must be drawn (no paint, or a gradient
/// without stops).
Paint? qrkudePaint(QRKudePaint? paint, QRKudeGradientGeometry g) {
  switch (paint) {
    case null:
      return null;
    case QRKudeSolid(:final color):
      return Paint()..color = qrkudeColorToColor(color);
    case QRKudeGradient():
      final stops = paint.effectiveStops;
      if (stops.isEmpty) return null; // SVG: as if "none" were specified
      // SVG stop semantics: offsets are clamped to [0, 1] and never decrease
      final colors = <Color>[];
      final offsets = <double>[];
      var last = 0.0;
      for (final s in stops) {
        var o = s.offset.isNaN ? 0.0 : s.offset.clamp(0.0, 1.0);
        if (o < last) o = last;
        last = o;
        colors.add(qrkudeColorToColor(s.color));
        offsets.add(o);
      }
      if (colors.length == 1) return Paint()..color = colors.first;
      if (paint is QRKudeLinearGradient) {
        if (g.x1 == g.x2 && g.y1 == g.y2) return Paint()..color = colors.last;
        return Paint()
          ..shader = ui.Gradient.linear(Offset(g.x1, g.y1), Offset(g.x2, g.y2), colors, offsets, TileMode.clamp);
      }
      if (!(g.r > 0)) return Paint()..color = colors.last;
      return Paint()..shader = ui.Gradient.radial(Offset(g.cx, g.cy), g.r, colors, offsets, TileMode.clamp);
  }
}

/// The drawable geometry of a (code, style) pair: one [Path] and one
/// [Paint] per layer, in module units. Built once and cached per code.
class QRKudeGeometry {
  QRKudeGeometry._(this.code, this.style)
      : layout = computeLayout(code, style),
        paths = {},
        paints = {} {
    final bg = style.background.color;
    final bgVisible = bg is QRKudeGradient || (bg is QRKudeSolid && QRKudeColor.alpha(bg.color) != 0);
    for (final layer in QRKudeLayer.values) {
      if (layer == QRKudeLayer.background && !bgVisible) continue;
      final paint = qrkudePaint(layerPaint(style, layer), gradientGeometry(code, style, layer));
      if (paint == null) continue;
      final sink = FlutterPathSink();
      if (emitLayer(code, style, layer, sink) == 0) continue;
      paths[layer] = sink.path;
      paints[layer] = paint..isAntiAlias = true;
    }
  }

  /// Returns the cached geometry for [code] and [style].
  factory QRKudeGeometry.of(QRCode code, QRKudeStyle style) {
    final cache = _cache[code] ??= LinkedHashMap<QRKudeStyle, QRKudeGeometry>();
    final hit = cache.remove(style);
    if (hit != null) {
      cache[style] = hit; // most recently used last
      return hit;
    }
    final g = QRKudeGeometry._(code, style);
    cache[style] = g;
    while (cache.length > 4) {
      cache.remove(cache.keys.first);
    }
    return g;
  }

  static final Expando<LinkedHashMap<QRKudeStyle, QRKudeGeometry>> _cache = Expando('qrkude geometry');

  /// The code this geometry draws.
  final QRCode code;

  /// The style this geometry draws.
  final QRKudeStyle style;

  /// Canvas size and positions, in modules.
  final QRKudeLayout layout;

  /// Layers that draw something, in paint order.
  final Map<QRKudeLayer, Path> paths;

  /// The paint of each layer in [paths] (colors or gradient shaders, in
  /// module units).
  final Map<QRKudeLayer, Paint> paints;

  /// Paints every layer (not the logo image or the label) in module units.
  void paintLayers(Canvas canvas) {
    for (final layer in QRKudeLayer.values) {
      final path = paths[layer];
      if (path != null) canvas.drawPath(path, paints[layer]!);
    }
  }
}
