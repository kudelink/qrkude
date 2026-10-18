// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// The style model: how a code looks (shapes, colors, logo, label).
///
/// Values are stored as given; out-of-range values are clamped when the code
/// is drawn, exactly like in the C core (see SPEC 2.2). The visual reference
/// with pictures is `docs/STYLES.md`.
library;

import 'color.dart';
import 'document.dart';
import 'encoder.dart';
import 'text.dart';

const Object _unset = Object();

/// Shape of the data modules, the small cells that carry the data
/// (SPEC 5.2, pictures in `docs/STYLES.md#modules`).
///
/// [QRKudeModules.size] shrinks the separate shapes (square, dot, rounded,
/// diamond, star, bars); the connected shapes (fluid, liquid, bevel, classy)
/// always fill their cell and use [QRKudeModules.roundness].
enum ModuleShape {
  /// Classic squares; neighbours touch when the size is 1.
  square,

  /// Circles.
  dot,

  /// Separate squares with rounded corners (radius set by roundness).
  rounded,

  /// Squares rotated by 45 degrees.
  diamond,

  /// Four-point sparkles with concave sides.
  star,

  /// Neighbours merge; only the outer corners are rounded.
  fluid,

  /// Like [fluid], plus smooth fillets in the inner corners.
  liquid,

  /// Neighbours merge; the outer corners are cut at 45 degrees.
  bevel,

  /// Neighbours merge; only the top-left and bottom-right corners round.
  classy,

  /// Horizontal runs of dark modules merge into pills.
  hbars,

  /// Vertical runs of dark modules merge into pills.
  vbars;

  /// Parses a JSON name; null when unknown.
  static ModuleShape? tryParse(String name) => _byName(values, name);
}

/// Shape of the eyes and of styled alignment patterns (SPEC 6, pictures in
/// `docs/STYLES.md#eyes`).
///
/// The three *eyes* (finder patterns) are the big squares in three corners
/// that scanners look for first. Each has a 7x7 *frame* (ring) and a 3x3
/// *ball* (center). Corner shapes are oriented so that the special corner
/// faces the center of the symbol.
enum EyeShape {
  /// Plain squares.
  square,

  /// Rounded corners (radius 25% of the side).
  rounded,

  /// Fully round.
  circle,

  /// Two opposite corners rounded, like a leaf.
  leaf,

  /// All corners rounded except the one facing the center.
  drop,

  /// Corners cut at 45 degrees.
  bevel,

  /// Fully cut corners (a rotated square). Diamond frames hurt scanning;
  /// `lint` reports `eye-shapes`.
  diamond,

  /// Frames: one dot per module. Balls: drawn as [circle].
  dots;

  /// Parses a JSON name; null when unknown.
  static EyeShape? tryParse(String name) => _byName(values, name);
}

/// Shape of the area cleared around the logo (and of its background).
enum LogoShape {
  /// A square area.
  square,

  /// A square area with rounded corners.
  rounded,

  /// A round area.
  circle;

  /// Parses a JSON name; null when unknown.
  static LogoShape? tryParse(String name) => _byName(values, name);
}

/// Where the label band goes, relative to the symbol.
enum LabelPosition {
  /// Below the symbol (the default).
  bottom,

  /// Above the symbol.
  top;

  /// Parses a JSON name; null when unknown.
  static LabelPosition? tryParse(String name) => _byName(values, name);
}

T? _byName<T extends Enum>(List<T> values, String name) {
  for (final v in values) {
    if (v.name == name) return v;
  }
  return null;
}

bool _listEquals<T>(List<T> a, List<T> b) {
  if (identical(a, b)) return true;
  if (a.length != b.length) return false;
  for (var i = 0; i < a.length; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

// ---- paints -----------------------------------------------------------------

/// A gradient stop: the [color] reached at [offset] along the gradient.
class QRKudeGradientStop {
  /// Creates a stop at [offset] (0 = start, 1 = end) with a `0xRRGGBBAA`
  /// [color].
  const QRKudeGradientStop(this.offset, this.color);

  /// Position along the gradient, 0..1 (values outside are clamped when
  /// drawn, like in SVG).
  final double offset;

  /// `0xRRGGBBAA` color.
  final int color;

  @override
  bool operator ==(Object other) => other is QRKudeGradientStop && other.offset == offset && other.color == color;

  @override
  int get hashCode => Object.hash(offset, color);

  @override
  String toString() => 'QRKudeGradientStop($offset, ${QRKudeColor.toHex(color)})';
}

/// A fill: a solid color ([QRKudeSolid]) or a gradient
/// ([QRKudeLinearGradient], [QRKudeRadialGradient]), SPEC 2.1.
///
/// Where the JSON format accepts `null` the Dart API uses a nullable
/// `QRKudePaint?` (no fill, or "same as the modules" for eye colors).
sealed class QRKudePaint {
  /// Base constructor for the subclasses.
  const QRKudePaint();

  /// A solid `0xRRGGBBAA` color.
  const factory QRKudePaint.solid(int color) = QRKudeSolid;

  /// A linear gradient; [angle] in degrees, 0 = left to right, 90 = top to bottom.
  const factory QRKudePaint.linear({double angle, required List<QRKudeGradientStop> stops}) = QRKudeLinearGradient;

  /// A radial gradient; center and radius relative to the painted area.
  const factory QRKudePaint.radial({
    double cx,
    double cy,
    double r,
    required List<QRKudeGradientStop> stops,
  }) = QRKudeRadialGradient;

  /// A solid color from a CSS-like string (`#rgb`, `#rrggbb`, `#rrggbbaa`, `transparent`).
  factory QRKudePaint.parse(String color) => QRKudeSolid(QRKudeColor.parse(color));

  /// Whether this paint is a gradient.
  bool get isGradient;
}

/// A solid color fill.
final class QRKudeSolid extends QRKudePaint {
  /// Creates a solid fill with a `0xRRGGBBAA` [color].
  const QRKudeSolid(this.color);

  /// `0xRRGGBBAA`.
  final int color;

  @override
  bool get isGradient => false;

  @override
  bool operator ==(Object other) => other is QRKudeSolid && other.color == color;

  @override
  int get hashCode => color.hashCode;

  @override
  String toString() => 'QRKudeSolid(${QRKudeColor.toHex(color)})';
}

/// A gradient fill: colors blend between [stops].
///
/// The gradient spans the painted area: the symbol (modules and eyes) or,
/// for the background, the whole canvas. A gradient without stops paints
/// nothing.
sealed class QRKudeGradient extends QRKudePaint {
  /// Base constructor for the two gradient kinds.
  const QRKudeGradient(this.stops);

  /// The stops, in order; renderers use at most the first 8.
  final List<QRKudeGradientStop> stops;

  /// The stops actually used (at most 8).
  List<QRKudeGradientStop> get effectiveStops => stops.length <= 8 ? stops : stops.sublist(0, 8);

  @override
  bool get isGradient => true;
}

/// A linear gradient: colors change along a straight line.
final class QRKudeLinearGradient extends QRKudeGradient {
  /// Creates a linear gradient at [angle] degrees.
  const QRKudeLinearGradient({this.angle = 0, required List<QRKudeGradientStop> stops}) : super(stops);

  /// Direction in degrees: 0 = left to right, 90 = top to bottom, 45 =
  /// top-left to bottom-right.
  final double angle;

  @override
  bool operator ==(Object other) =>
      other is QRKudeLinearGradient && other.angle == angle && _listEquals(other.stops, stops);

  @override
  int get hashCode => Object.hash(angle, Object.hashAll(stops));

  @override
  String toString() => 'QRKudeLinearGradient(angle: $angle, stops: $stops)';
}

/// A radial gradient: colors change from a center outwards.
final class QRKudeRadialGradient extends QRKudeGradient {
  /// Creates a radial gradient; the defaults center it with a radius of 75%
  /// of the painted area.
  const QRKudeRadialGradient({this.cx = 0.5, this.cy = 0.5, this.r = 0.75, required List<QRKudeGradientStop> stops})
      : super(stops);

  /// Center, as a fraction of the painted area's width (0 = left, 1 = right).
  final double cx;

  /// Center, as a fraction of the painted area's height (0 = top, 1 = bottom).
  final double cy;

  /// Radius, as a fraction of the painted area's larger side.
  final double r;

  @override
  bool operator ==(Object other) =>
      other is QRKudeRadialGradient &&
      other.cx == cx &&
      other.cy == cy &&
      other.r == r &&
      _listEquals(other.stops, stops);

  @override
  int get hashCode => Object.hash(cx, cy, r, Object.hashAll(stops));

  @override
  String toString() => 'QRKudeRadialGradient(cx: $cx, cy: $cy, r: $r, stops: $stops)';
}

// ---- sections ---------------------------------------------------------------

/// How the data modules look (the `modules` section of a style).
class QRKudeModules {
  /// Creates the section; the defaults are black squares.
  const QRKudeModules({
    this.shape = ModuleShape.square,
    this.size = 1,
    this.roundness = 1,
    this.color = const QRKudeSolid(QRKudeColor.black),
  });

  /// Shape of each dark module.
  final ModuleShape shape;

  /// Size of each module relative to its cell, 0.1..1 (clamped). Values
  /// below 1 leave gaps; below 0.6 scanning suffers (`small-modules` lint).
  /// Used by square, dot, rounded, diamond, star and bars.
  final double size;

  /// How round the corners are, 0..1 (clamped). Used by rounded, fluid,
  /// liquid, bevel, classy and bars.
  final double roundness;

  /// Fill of the modules: a color or gradient. null = no fill (the code is
  /// invisible; `lint` reports it).
  final QRKudePaint? color;

  /// A copy with the given fields replaced; pass `color: null` to clear it.
  QRKudeModules copyWith({ModuleShape? shape, double? size, double? roundness, Object? color = _unset}) =>
      QRKudeModules(
        shape: shape ?? this.shape,
        size: size ?? this.size,
        roundness: roundness ?? this.roundness,
        color: identical(color, _unset) ? this.color : color as QRKudePaint?,
      );

  @override
  bool operator ==(Object other) =>
      other is QRKudeModules &&
      other.shape == shape &&
      other.size == size &&
      other.roundness == roundness &&
      other.color == color;

  @override
  int get hashCode => Object.hash(shape, size, roundness, color);
}

/// How the three eyes look (the `eyes` section of a style).
///
/// The eyes (finder patterns) are the big squares in three corners. Scanners
/// find them first, so keep them clearly darker than the background.
class QRKudeEyes {
  /// Creates the section; the defaults are square eyes in the module color.
  const QRKudeEyes({
    this.frame = EyeShape.square,
    this.ball = EyeShape.square,
    this.frameColor,
    this.ballColor,
  });

  /// Shape of the 7x7 ring.
  final EyeShape frame;

  /// Shape of the 3x3 center ([EyeShape.dots] is drawn as a circle).
  final EyeShape ball;

  /// Fill of the frames; null = same paint as the modules.
  final QRKudePaint? frameColor;

  /// Fill of the balls; null = same paint as the modules.
  final QRKudePaint? ballColor;

  /// A copy with the given fields replaced; pass `frameColor: null` or
  /// `ballColor: null` to go back to the module paint.
  QRKudeEyes copyWith({
    EyeShape? frame,
    EyeShape? ball,
    Object? frameColor = _unset,
    Object? ballColor = _unset,
  }) =>
      QRKudeEyes(
        frame: frame ?? this.frame,
        ball: ball ?? this.ball,
        frameColor: identical(frameColor, _unset) ? this.frameColor : frameColor as QRKudePaint?,
        ballColor: identical(ballColor, _unset) ? this.ballColor : ballColor as QRKudePaint?,
      );

  @override
  bool operator ==(Object other) =>
      other is QRKudeEyes &&
      other.frame == frame &&
      other.ball == ball &&
      other.frameColor == frameColor &&
      other.ballColor == ballColor;

  @override
  int get hashCode => Object.hash(frame, ball, frameColor, ballColor);
}

/// How the alignment patterns look (the `alignment` section of a style).
///
/// Alignment patterns are the small 5x5 squares that symbols of version 2
/// and above have (one or more), which help scanners correct perspective.
/// By default they are drawn like the other modules; set [frame] to draw
/// them like small eyes.
class QRKudeAlignment {
  /// Creates the section; the default (no frame) draws them as modules.
  const QRKudeAlignment({this.frame, this.ball});

  /// Shape of the 5x5 ring; null = draw the pattern as ordinary modules.
  final EyeShape? frame;

  /// Shape of the center module; null = square (when [frame] is set).
  final EyeShape? ball;

  /// A copy with the given fields replaced (null values are allowed).
  QRKudeAlignment copyWith({Object? frame = _unset, Object? ball = _unset}) => QRKudeAlignment(
        frame: identical(frame, _unset) ? this.frame : frame as EyeShape?,
        ball: identical(ball, _unset) ? this.ball : ball as EyeShape?,
      );

  @override
  bool operator ==(Object other) => other is QRKudeAlignment && other.frame == frame && other.ball == ball;

  @override
  int get hashCode => Object.hash(frame, ball);
}

/// The canvas background (the `background` section of a style).
///
/// The background covers the whole canvas, including the *quiet zone*: the
/// empty margin around the symbol that scanners need ([QRKudeStyle.margin]).
class QRKudeBackground {
  /// Creates the section; the default is plain white with square corners.
  const QRKudeBackground({this.color = const QRKudeSolid(QRKudeColor.white), this.radius = 0});

  /// Fill of the canvas; null = transparent (the page shows through).
  final QRKudePaint? color;

  /// Corner radius in modules (clamped to half the shorter canvas side).
  final double radius;

  /// A copy with the given fields replaced; pass `color: null` for none.
  QRKudeBackground copyWith({Object? color = _unset, double? radius}) => QRKudeBackground(
        color: identical(color, _unset) ? this.color : color as QRKudePaint?,
        radius: radius ?? this.radius,
      );

  @override
  bool operator ==(Object other) => other is QRKudeBackground && other.color == color && other.radius == radius;

  @override
  int get hashCode => Object.hash(color, radius);
}

/// A frame around the canvas (the `border` section of a style).
///
/// The border is drawn outside the quiet zone and follows the background's
/// corner radius.
class QRKudeBorder {
  /// Creates the section; the default width 0 means no border.
  const QRKudeBorder({this.width = 0, this.color = QRKudeColor.black});

  /// Width in modules, 0..1000 (clamped); 0 = no border.
  final double width;

  /// Solid `0xRRGGBBAA` color.
  final int color;

  /// A copy with the given fields replaced.
  QRKudeBorder copyWith({double? width, int? color}) =>
      QRKudeBorder(width: width ?? this.width, color: color ?? this.color);

  @override
  bool operator ==(Object other) => other is QRKudeBorder && other.width == width && other.color == color;

  @override
  int get hashCode => Object.hash(width, color);
}

/// A logo in the middle of the code (the `logo` section of a style).
///
/// The logo covers modules, which only works because error correction can
/// restore them; with a logo the default ECC is `H`. Check `logoDamage` or
/// `lint` before printing (`docs/GUIDE.md#logos`). The logo is present when
/// [size] > 0.
class QRKudeLogo {
  /// Creates the section; the default is 20% of the symbol width with one
  /// module of clear space and no image.
  const QRKudeLogo({
    this.href,
    this.size = 0.2,
    this.margin = 1,
    this.shape = LogoShape.square,
    this.excavate = true,
    this.background = QRKudeColor.transparent,
  });

  /// Image for the SVG output: a URL or a `data:` URI, written to an
  /// `<image>` element. Native renderers take the image separately (e.g.
  /// the `logo` of `QRKudeView` in qrkude_flutter).
  final String? href;

  /// Width of the logo box as a fraction of the symbol width, 0..1
  /// (clamped). Keep it at about 0.25 or less.
  final double size;

  /// Clear space around the logo box, in modules, 0..1000 (clamped).
  final double margin;

  /// Shape of the cleared area.
  final LogoShape shape;

  /// Remove the modules behind the logo area (recommended), instead of
  /// drawing the logo on top of them.
  final bool excavate;

  /// Solid `0xRRGGBBAA` fill of the cleared area; transparent = none.
  final int background;

  /// A copy with the given fields replaced; pass `href: null` to clear it.
  QRKudeLogo copyWith({
    Object? href = _unset,
    double? size,
    double? margin,
    LogoShape? shape,
    bool? excavate,
    int? background,
  }) =>
      QRKudeLogo(
        href: identical(href, _unset) ? this.href : href as String?,
        size: size ?? this.size,
        margin: margin ?? this.margin,
        shape: shape ?? this.shape,
        excavate: excavate ?? this.excavate,
        background: background ?? this.background,
      );

  @override
  bool operator ==(Object other) =>
      other is QRKudeLogo &&
      other.href == href &&
      other.size == size &&
      other.margin == margin &&
      other.shape == shape &&
      other.excavate == excavate &&
      other.background == background;

  @override
  int get hashCode => Object.hash(href, size, margin, shape, excavate, background);
}

/// A line of text above or below the symbol (the `label` section of a
/// style), e.g. "SCAN ME".
///
/// The canvas grows by a band of 1.5 x [size] modules. Drawn when [text] is
/// not empty.
class QRKudeLabel {
  /// Creates a label; only [text] is required.
  const QRKudeLabel({
    required this.text,
    this.font = 'sans-serif',
    this.size = 4,
    this.color = QRKudeColor.black,
    this.position = LabelPosition.bottom,
    this.bold = true,
  });

  /// The text. Control characters are dropped when it is drawn.
  final String text;

  /// CSS font-family list such as `Georgia, serif`; empty = `sans-serif`.
  final String font;

  /// Font size in modules, 0.1..1000 (clamped).
  final double size;

  /// `0xRRGGBBAA` text color.
  final int color;

  /// Whether the band goes below or above the symbol.
  final LabelPosition position;

  /// Bold text (the default).
  final bool bold;

  /// A copy with the given fields replaced.
  QRKudeLabel copyWith({
    String? text,
    String? font,
    double? size,
    int? color,
    LabelPosition? position,
    bool? bold,
  }) =>
      QRKudeLabel(
        text: text ?? this.text,
        font: font ?? this.font,
        size: size ?? this.size,
        color: color ?? this.color,
        position: position ?? this.position,
        bold: bold ?? this.bold,
      );

  @override
  bool operator ==(Object other) =>
      other is QRKudeLabel &&
      other.text == text &&
      other.font == font &&
      other.size == size &&
      other.color == color &&
      other.position == position &&
      other.bold == bold;

  @override
  int get hashCode => Object.hash(text, font, size, color, position, bold);
}

/// A complete QRKude style: everything about how a code looks (SPEC 2,
/// pictures in `docs/STYLES.md`).
///
/// Immutable; use [copyWith] to derive variants. The default style is the
/// classic look: black squares on white with a 4-module quiet zone.
class QRKudeStyle {
  /// Creates a style; every section has a default.
  const QRKudeStyle({
    this.margin = 4,
    this.modules = const QRKudeModules(),
    this.eyes = const QRKudeEyes(),
    this.alignment = const QRKudeAlignment(),
    this.background = const QRKudeBackground(),
    this.border = const QRKudeBorder(),
    this.logo,
    this.label,
  });

  /// Parses a style from a JSON `String` or a decoded `Map`. Accepts the
  /// wrapper document `{"encode": .., "style": .., "svg": ..}` (top-level
  /// style keys are accepted too) with the same keys, defaults and errors
  /// as the C reader. Throws [FormatException] on invalid input.
  factory QRKudeStyle.fromJson(Object json) => QRKudeDocument.fromJson(json).style;

  /// The default style.
  static const QRKudeStyle defaults = QRKudeStyle();

  /// Quiet zone: the empty margin around the symbol, in modules, 0..1000
  /// (clamped). Scanners need it; keep it at 4 (2 is the practical minimum,
  /// `lint` reports `quiet-zone` below that).
  final double margin;

  /// Data modules: shape, size, roundness and color.
  final QRKudeModules modules;

  /// The three eyes (finder patterns).
  final QRKudeEyes eyes;

  /// The alignment patterns (versions 2 and above).
  final QRKudeAlignment alignment;

  /// The canvas background.
  final QRKudeBackground background;

  /// A frame around the canvas.
  final QRKudeBorder border;

  /// The logo; null = no logo.
  final QRKudeLogo? logo;

  /// The label; null = no label.
  final QRKudeLabel? label;

  /// Whether a logo is drawn (logo present with size > 0).
  bool get hasLogo => logo != null && logo!.size > 0;

  /// Whether a label is drawn (non-empty text; a string ends at U+0000).
  bool get hasLabel => label != null && !isEmptyString(label!.text);

  /// Recommended minimum ECC: `H` with a logo, else `M` (SPEC 1.3).
  Ecc get recommendedEcc => hasLogo ? Ecc.high : Ecc.medium;

  /// A copy with the given sections replaced; pass `logo: null` or
  /// `label: null` to remove them.
  QRKudeStyle copyWith({
    double? margin,
    QRKudeModules? modules,
    QRKudeEyes? eyes,
    QRKudeAlignment? alignment,
    QRKudeBackground? background,
    QRKudeBorder? border,
    Object? logo = _unset,
    Object? label = _unset,
  }) =>
      QRKudeStyle(
        margin: margin ?? this.margin,
        modules: modules ?? this.modules,
        eyes: eyes ?? this.eyes,
        alignment: alignment ?? this.alignment,
        background: background ?? this.background,
        border: border ?? this.border,
        logo: identical(logo, _unset) ? this.logo : logo as QRKudeLogo?,
        label: identical(label, _unset) ? this.label : label as QRKudeLabel?,
      );

  /// The JSON form (a bare style object, every key written).
  Map<String, Object?> toJson() => styleToJson(this);

  @override
  bool operator ==(Object other) =>
      other is QRKudeStyle &&
      other.margin == margin &&
      other.modules == modules &&
      other.eyes == eyes &&
      other.alignment == alignment &&
      other.background == background &&
      other.border == border &&
      other.logo == logo &&
      other.label == label;

  @override
  int get hashCode => Object.hash(margin, modules, eyes, alignment, background, border, logo, label);

  @override
  String toString() => 'QRKudeStyle(${toJson()})';
}
