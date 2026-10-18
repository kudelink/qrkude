// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:qrkude/qrkude.dart';

import 'geometry.dart';

/// Paints a styled QR code with the shared QRKude shape model.
///
/// The canvas (quiet zone, border and label band included) is scaled
/// uniformly to fit the paint size and centered. Paths and paints are
/// computed once per (code, style) and cached.
class QRKudePainter extends CustomPainter {
  /// Creates a painter for an already encoded [code].
  QRKudePainter({
    required this.code,
    this.style = const QRKudeStyle(),
    this.logo,
    this.textDirection = TextDirection.ltr,
    this.drawLabel = true,
  });

  /// Convenience constructor: encodes [data] with the default-ECC rule
  /// (`H` when the style has a logo, else `M`, unless [encodeOptions] sets
  /// an ECC). Throws [DataTooLongException] when the data does not fit and
  /// [ArgumentError] for invalid options.
  factory QRKudePainter.fromData(
    String data, {
    QRKudeStyle style = const QRKudeStyle(),
    EncodeOptions encodeOptions = const EncodeOptions(),
    ui.Image? logo,
    TextDirection textDirection = TextDirection.ltr,
  }) {
    final doc = QRKudeDocument(encodeOptions: encodeOptions, style: style);
    return QRKudePainter(code: doc.encodeText(data), style: style, logo: logo, textDirection: textDirection);
  }

  /// The encoded symbol.
  final QRCode code;

  /// How it looks.
  final QRKudeStyle style;

  /// Bitmap drawn (aspect-fit, centered) in the logo box when the style has
  /// a logo.
  final ui.Image? logo;

  /// Direction of the label text.
  final TextDirection textDirection;

  /// Whether to draw the label text (the band is always reserved).
  final bool drawLabel;

  /// The cached geometry of [code] and [style].
  QRKudeGeometry get geometry => QRKudeGeometry.of(code, style);

  /// Canvas width / height (1 unless there is a label band).
  double get aspectRatio {
    final l = geometry.layout;
    return l.width / l.height;
  }

  @override
  void paint(Canvas canvas, Size size) {
    final g = geometry;
    final layout = g.layout;
    if (size.isEmpty) return;
    final scale = math.min(size.width / layout.width, size.height / layout.height);
    final dx = (size.width - layout.width * scale) / 2;
    final dy = (size.height - layout.height * scale) / 2;

    canvas.save();
    canvas.translate(dx, dy);
    canvas.scale(scale);
    g.paintLayers(canvas);
    final image = logo;
    if (layout.hasLogo && image != null && image.width > 0 && image.height > 0) {
      // aspect fit into the logo box, like preserveAspectRatio="xMidYMid meet"
      final box = layout.logoSize;
      final k = box / math.max(image.width, image.height);
      final w = image.width * k, h = image.height * k;
      final dst = Rect.fromLTWH(layout.logoX + (box - w) / 2, layout.logoY + (box - h) / 2, w, h);
      final src = Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble());
      canvas.drawImageRect(
        image,
        src,
        dst,
        Paint()
          ..filterQuality = FilterQuality.medium
          ..isAntiAlias = true,
      );
    }
    canvas.restore();

    final label = style.label;
    if (drawLabel && layout.hasLabel && label != null) {
      // laid out in device space so that glyphs are hinted at their real size
      final painter = TextPainter(
        text:
            TextSpan(text: sanitizeText(label.text), style: qrkudeLabelTextStyle(label, layout.labelFontSize * scale)),
        textDirection: textDirection,
        textAlign: TextAlign.center,
        maxLines: 1,
      )..layout();
      final baseline = painter.computeDistanceToActualBaseline(TextBaseline.alphabetic);
      final x = dx + layout.labelX * scale - painter.width / 2;
      final y = dy + layout.labelBaseline * scale - baseline;
      painter.paint(canvas, Offset(x, y));
      painter.dispose();
    }
  }

  @override
  bool shouldRepaint(QRKudePainter oldDelegate) =>
      oldDelegate.code != code ||
      oldDelegate.style != style ||
      oldDelegate.logo != logo ||
      oldDelegate.textDirection != textDirection ||
      oldDelegate.drawLabel != drawLabel;

  /// Records the code into a [ui.Picture] of [width] logical pixels (the
  /// height follows the canvas aspect ratio). Both sides must be
  /// 1..[maxImageSide], otherwise an [ArgumentError] is thrown.
  ui.Picture toPicture(double width) {
    final size = _sizeFor(width);
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder, Offset.zero & size);
    paint(canvas, size);
    return recorder.endRecording();
  }

  /// Renders the code into an image [width] pixels wide (rounded to whole
  /// pixels; the height follows the canvas aspect ratio).
  Future<ui.Image> toImage(double width) async {
    final size = _sizeFor(width);
    final picture = toPicture(size.width);
    try {
      return await picture.toImage(size.width.round(), size.height.round());
    } finally {
      picture.dispose();
    }
  }

  /// Renders the code as PNG bytes, [width] pixels wide.
  Future<Uint8List> toPngBytes(double width) async {
    final image = await toImage(width);
    try {
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      if (data == null) throw StateError('PNG encoding failed');
      return data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes);
    } finally {
      image.dispose();
    }
  }

  /// Largest image side accepted by [toImage] / [toPngBytes] (like the
  /// reference CLI), so that untrusted styles (e.g. a huge label band) or
  /// widths cannot request gigantic bitmaps.
  static const int maxImageSide = 16384;

  Size _sizeFor(double width) {
    final w = width.roundToDouble();
    final h = (w / aspectRatio).roundToDouble();
    if (!(w >= 1 && w <= maxImageSide)) {
      throw ArgumentError.value(width, 'width', 'must be 1..$maxImageSide pixels');
    }
    if (!(h >= 1 && h <= maxImageSide)) {
      throw ArgumentError.value(width, 'width', 'the image height ($h px) must be 1..$maxImageSide pixels');
    }
    return Size(w, h);
  }
}

/// Text style of a QRKude label at [fontSize] logical pixels. The CSS
/// font-family list is mapped to `fontFamily` + `fontFamilyFallback`;
/// `sans-serif` (and an empty list) use the platform default font.
TextStyle qrkudeLabelTextStyle(QRKudeLabel label, double fontSize) {
  final families = <String>[
    for (final part in sanitizeText(label.font).split(','))
      if (part.trim().replaceAll('"', '').replaceAll("'", '') case final name
          when name.isNotEmpty && name != 'sans-serif')
        name,
  ];
  return TextStyle(
    color: qrkudeColorToColor(label.color),
    fontSize: fontSize,
    fontWeight: label.bold ? FontWeight.bold : FontWeight.normal,
    fontFamily: families.isEmpty ? null : families.first,
    fontFamilyFallback: families.length > 1 ? families.sublist(1) : null,
    height: 1,
  );
}

/// Encodes [data] and renders it as PNG bytes, [width] pixels wide.
///
/// [style] may be a [QRKudeStyle], a [QRKudeDocument], a JSON `String` or a
/// decoded `Map`; the default-ECC rule applies unless the document sets
/// `encode.ecc`.
Future<Uint8List> qrkudeToPngBytes(String data, {Object? style, double width = 512, ui.Image? logo}) {
  final doc = QRKudeDocument.from(style);
  final painter = QRKudePainter(code: doc.encodeText(data), style: doc.style, logo: logo);
  return painter.toPngBytes(width);
}
