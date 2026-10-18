// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Public geometry API: canvas layout, drawing layers, the path sink that
/// renderers implement, paints and gradient geometry per layer, and the logo
/// damage estimate. The computations live in the internal `engine.dart`.
library;

import 'encoder.dart';
import 'engine.dart';
import 'style.dart';

/// The drawing layers, in paint order (back to front). The logo image and
/// the label text are drawn after them.
enum QRKudeLayer {
  /// The canvas background ([QRKudeStyle.background]).
  background,

  /// The border ring ([QRKudeStyle.border]).
  border,

  /// The data modules (and alignment patterns drawn as modules).
  modules,

  /// The eye frames and styled alignment frames.
  eyeFrames,

  /// The eye balls and styled alignment centers.
  eyeBalls,

  /// The area cleared behind the logo, when it has a background color.
  logoBackground,
}

/// Receives the geometry of a layer from [emitLayer]; implement it to draw
/// QRKude codes with any graphics API (canvas, PDF, plotter...).
///
/// Coordinates are in modules ("canvas units"): x to the right, y down, the
/// canvas from (0, 0) to ([QRKudeLayout.width], [QRKudeLayout.height]).
/// Multiply by your scale. Every sub-path starts with [moveTo] and ends with
/// [close]; fill the result with the non-zero winding rule (holes run
/// counter-clockwise).
abstract class QRKudePathSink {
  /// Starts a new sub-path at ([x], [y]).
  void moveTo(double x, double y);

  /// A straight line from the current point to ([x], [y]).
  void lineTo(double x, double y);

  /// A circular arc of radius [r] from the current point to ([x], [y])
  /// around the center ([cx], [cy]); [clockwise] refers to the screen (y
  /// down), like the SVG sweep flag. Arcs never exceed 90 degrees.
  void arcTo(double r, bool clockwise, double x, double y, double cx, double cy);

  /// Closes the current sub-path (back to its start point).
  void close();
}

/// Where things go on the canvas, in modules (see [computeLayout]).
///
/// The canvas is the symbol plus the quiet zone, the border and the label
/// band. Multiply by your pixels-per-module scale to draw it.
class QRKudeLayout {
  /// Creates a layout; normally obtained from [computeLayout].
  const QRKudeLayout({
    required this.width,
    required this.height,
    required this.symbolX,
    required this.symbolY,
    required this.modules,
    required this.hasLogo,
    required this.logoX,
    required this.logoY,
    required this.logoSize,
    required this.hasLabel,
    required this.labelX,
    required this.labelBaseline,
    required this.labelFontSize,
    required this.backgroundRadius,
  });

  /// Canvas width.
  final double width;

  /// Canvas height (larger than [width] when there is a label band).
  final double height;

  /// X of the top-left corner of module (0, 0).
  final double symbolX;

  /// Y of the top-left corner of module (0, 0).
  final double symbolY;

  /// Modules per side of the symbol.
  final int modules;

  /// Whether the style has a logo.
  final bool hasLogo;

  /// Left edge of the square logo box (the image is fitted inside it).
  final double logoX;

  /// Top edge of the square logo box.
  final double logoY;

  /// Side of the square logo box.
  final double logoSize;

  /// Whether a label is drawn.
  final bool hasLabel;

  /// X of the label's center (the text is centered on it).
  final double labelX;

  /// Y of the label's baseline.
  final double labelBaseline;

  /// Label font size after clamping.
  final double labelFontSize;

  /// Background corner radius after clamping.
  final double backgroundRadius;
}

/// Where a layer's gradient lies on the canvas, in modules, exactly as
/// written to the SVG (SPEC 8.4). Use it to build native gradients.
class QRKudeGradientGeometry {
  /// Creates the geometry; normally obtained from [gradientGeometry].
  const QRKudeGradientGeometry(this.x1, this.y1, this.x2, this.y2, this.cx, this.cy, this.r);

  /// Linear gradient: start point x.
  final double x1;

  /// Linear gradient: start point y.
  final double y1;

  /// Linear gradient: end point x.
  final double x2;

  /// Linear gradient: end point y.
  final double y2;

  /// Radial gradient: center x.
  final double cx;

  /// Radial gradient: center y.
  final double cy;

  /// Radial gradient: radius.
  final double r;
}

/// Computes where everything goes when [code] is drawn with [style]
/// (canvas size, symbol position, logo box, label anchor).
QRKudeLayout computeLayout(QRCode code, QRKudeStyle style) {
  final L = LayoutContext(code, style);
  return QRKudeLayout(
    width: L.W,
    height: L.H,
    symbolX: L.ox,
    symbolY: L.oy,
    modules: L.n,
    hasLogo: L.hasLogo,
    logoX: L.logoCx - L.logoHalf,
    logoY: L.logoCy - L.logoHalf,
    logoSize: 2 * L.logoHalf,
    hasLabel: L.hasLabel,
    labelX: L.labelX,
    labelBaseline: L.labelBaseline,
    labelFontSize: L.labelSize,
    backgroundRadius: L.bgRadius,
  );
}

/// Sends the geometry of one [layer] to [sink] and returns the number of
/// sub-paths (0 = nothing to draw). Without a sink it only counts. This is
/// the shape model every QRKude renderer shares, so custom renderers draw
/// exactly what the SVG shows.
int emitLayer(QRCode code, QRKudeStyle style, QRKudeLayer layer, [QRKudePathSink? sink]) =>
    LayoutContext(code, style).emit(layer, sink);

/// The paint a [layer] is filled with: eye paints are resolved (a null eye
/// color means the module paint), border and logo background come back as
/// solid colors. null = no fill.
QRKudePaint? layerPaint(QRKudeStyle style, QRKudeLayer layer) {
  switch (layer) {
    case QRKudeLayer.background:
      return style.background.color;
    case QRKudeLayer.border:
      return QRKudeSolid(style.border.color);
    case QRKudeLayer.eyeFrames:
      return style.eyes.frameColor ?? style.modules.color;
    case QRKudeLayer.eyeBalls:
      return style.eyes.ballColor ?? style.modules.color;
    case QRKudeLayer.logoBackground:
      return QRKudeSolid(style.logo?.background ?? 0);
    case QRKudeLayer.modules:
      return style.modules.color;
  }
}

/// Whether a layer draws anything visible (SPEC 8.5): its resolved paint has
/// at least one color with alpha > 0 (a solid color, or any of the first 8
/// gradient stops; a gradient without stops is not visible), and for the
/// border a width > 0, for the logo background a logo, for the modules at
/// least one sub-path. A hint for native renderers; the SVG writer keeps its
/// own rules.
bool layerVisible(QRCode code, QRKudeStyle style, QRKudeLayer layer) {
  final p = layerPaint(style, layer);
  final painted = switch (p) {
    QRKudeSolid(:final color) => color % 256 != 0,
    QRKudeGradient() => p.effectiveStops.any((s) => s.color % 256 != 0),
    null => false,
  };
  switch (layer) {
    case QRKudeLayer.border:
      return painted && style.border.width > 0;
    case QRKudeLayer.logoBackground:
      final logo = style.logo;
      return painted && logo != null && logo.size > 0;
    case QRKudeLayer.modules:
      return painted && emitLayer(code, style, layer) > 0;
    case QRKudeLayer.background:
    case QRKudeLayer.eyeFrames:
    case QRKudeLayer.eyeBalls:
      return painted;
  }
}

/// Where the gradient of [layer]'s paint lies on the canvas, in modules
/// (meaningful when [layerPaint] returns a gradient).
QRKudeGradientGeometry gradientGeometry(QRCode code, QRKudeStyle style, QRKudeLayer layer) =>
    gradientGeometryFor(LayoutContext(code, style), layer, layerPaint(style, layer));

/// How much of the error correction budget the logo uses up, worst case
/// (SPEC 10). 0 = no logo; 1 = the limit; above 1 the code cannot be
/// decoded at all. Keep it at 0.7 or below (`lint` reports `logo-risky`
/// above that) by making the logo smaller or raising the ECC level.
///
/// Technically: the maximum over Reed-Solomon blocks of the codewords hidden
/// by the logo area divided by the codewords the block can correct.
double logoDamage(QRCode code, QRKudeStyle style) => LayoutContext(code, style).logoDamage();

/// The ball shape actually drawn for [shape]: `dots` balls are drawn as
/// circles (a grid of dots would break the pattern scanners look for).
EyeShape ballShape(EyeShape shape) => shape == EyeShape.dots ? EyeShape.circle : shape;
