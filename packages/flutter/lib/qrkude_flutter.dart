// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Styled QR codes for Flutter.
///
/// - [QRKudeView]: a widget that encodes and draws a styled QR code.
/// - [QRKudePainter]: the `CustomPainter` behind it, usable standalone, with
///   `toImage` / `toPngBytes` helpers.
/// - [QRKudeGeometry]: the cached Flutter paths and paints of a code.
///
/// Re-exports the pure Dart core (`package:qrkude/qrkude.dart`).
library;

export 'package:qrkude/qrkude.dart';

export 'src/geometry.dart' show FlutterPathSink, QRKudeGeometry, qrkudeColorToColor, qrkudePaint;
export 'src/painter.dart' show QRKudePainter, qrkudeLabelTextStyle, qrkudeToPngBytes;
export 'src/view.dart' show QRKudeErrorBuilder, QRKudeView;
