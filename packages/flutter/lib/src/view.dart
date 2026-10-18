// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'package:flutter/widgets.dart';
import 'package:qrkude/qrkude.dart';

import 'painter.dart';

/// Builds the widget shown when the data cannot be encoded or the style
/// cannot be parsed.
typedef QRKudeErrorBuilder = Widget Function(BuildContext context, Object error);

/// A widget that encodes [data] and draws it as a styled QR code.
///
/// The code is re-encoded only when [data] or the encoding options change,
/// and the drawing geometry is cached, so rebuilding the widget is cheap.
/// If the data does not fit or a JSON style is invalid, [errorBuilder] is
/// shown instead. The widget is exposed to screen readers as an image.
///
/// ```dart
/// QRKudeView(
///   data: 'https://kudelink.com',
///   style: const QRKudeStyle(modules: QRKudeModules(shape: ModuleShape.fluid)),
///   size: 240,
/// )
/// ```
class QRKudeView extends StatefulWidget {
  /// Creates a QR code widget for [data].
  const QRKudeView({
    super.key,
    required this.data,
    this.style,
    this.encodeOptions,
    this.logo,
    this.size,
    this.semanticsLabel,
    this.errorBuilder,
  });

  /// The text to encode (UTF-8).
  final String data;

  /// A [QRKudeStyle], a [QRKudeDocument] or the JSON form (`Map` or
  /// `String`, bare style or `{"encode": .., "style": .., "svg": ..}`).
  /// null = default style.
  final Object? style;

  /// Overrides the document's encode options. A null ECC selects `H` when
  /// the style has a logo and `M` otherwise.
  final EncodeOptions? encodeOptions;

  /// Image drawn in the logo box. When the style has no logo, a default
  /// [QRKudeLogo] (20% of the symbol, 1 module of clear space) is added.
  final ImageProvider? logo;

  /// Width in logical pixels (the height follows the canvas aspect ratio).
  /// null = as wide as the parent allows; the parent must then give a
  /// bounded width (inside a `Row`, wrap the view in `Expanded` or give it a
  /// size).
  final double? size;

  /// Accessibility label; defaults to "QR code".
  final String? semanticsLabel;

  /// Shown instead of the code when encoding fails. Defaults to an empty box.
  final QRKudeErrorBuilder? errorBuilder;

  @override
  State<QRKudeView> createState() => _QRKudeViewState();
}

class _QRKudeViewState extends State<QRKudeView> {
  QRKudeDocument? _doc;
  String? _data;
  QRCode? _code;
  Object? _error;

  ImageStream? _stream;
  ImageInfo? _logoInfo;
  late final ImageStreamListener _listener = ImageStreamListener(_onLogo, onError: _onLogoError);

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _resolveLogo();
  }

  @override
  void didUpdateWidget(QRKudeView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.logo != oldWidget.logo) _resolveLogo();
  }

  @override
  void dispose() {
    _stopLogo();
    _logoInfo?.dispose();
    _logoInfo = null;
    super.dispose();
  }

  /// Parses the style and encodes the data, reusing the previous [QRCode]
  /// (and therefore the cached geometry) when nothing relevant changed.
  void _update() {
    try {
      var doc = QRKudeDocument.from(widget.style);
      if (widget.encodeOptions != null) doc = doc.copyWith(encodeOptions: widget.encodeOptions);
      if (widget.logo != null && doc.style.logo == null) {
        doc = doc.copyWith(style: doc.style.copyWith(logo: const QRKudeLogo()));
      }
      final sameEncoding = _code != null &&
          _data == widget.data &&
          _doc != null &&
          _doc!.effectiveEncodeOptions == doc.effectiveEncodeOptions;
      if (!sameEncoding) _code = doc.encodeText(widget.data);
      _doc = doc;
      _data = widget.data;
      _error = null;
    } on Object catch (e) {
      if (e is! QRKudeException && e is! FormatException && e is! ArgumentError) rethrow;
      _error = e;
      _code = null;
      _doc = null;
      _data = null;
    }
  }

  void _resolveLogo() {
    final provider = widget.logo;
    if (provider == null) {
      _stopLogo();
      if (_logoInfo != null) {
        _logoInfo!.dispose();
        _logoInfo = null;
      }
      return;
    }
    final stream = provider.resolve(createLocalImageConfiguration(context));
    if (_stream?.key == stream.key) return;
    _stopLogo();
    _stream = stream..addListener(_listener);
  }

  void _stopLogo() {
    _stream?.removeListener(_listener);
    _stream = null;
  }

  void _onLogo(ImageInfo info, bool synchronousCall) {
    void apply() {
      _logoInfo?.dispose();
      _logoInfo = info;
    }

    if (synchronousCall || !mounted) {
      apply();
    } else {
      setState(apply);
    }
  }

  void _onLogoError(Object error, StackTrace? stackTrace) {
    FlutterError.reportError(FlutterErrorDetails(
      exception: error,
      stack: stackTrace,
      library: 'qrkude_flutter',
      context: ErrorDescription('while loading the QRKudeView logo'),
    ));
  }

  @override
  Widget build(BuildContext context) {
    _update();
    final code = _code, doc = _doc;
    if (code == null || doc == null) {
      final builder = widget.errorBuilder;
      if (builder != null) return builder(context, _error!);
      return SizedBox(width: widget.size, height: widget.size);
    }
    final painter = QRKudePainter(
      code: code,
      style: doc.style,
      logo: _logoInfo?.image,
      textDirection: Directionality.maybeOf(context) ?? TextDirection.ltr,
    );
    final aspect = painter.aspectRatio;
    Widget child = RepaintBoundary(
      child: CustomPaint(painter: painter, isComplex: true),
    );
    final size = widget.size;
    child = size != null
        ? SizedBox(width: size, height: size / aspect, child: child)
        : AspectRatio(aspectRatio: aspect, child: child);
    return Semantics(
      container: true,
      image: true,
      label: widget.semanticsLabel ?? 'QR code',
      child: child,
    );
  }
}
