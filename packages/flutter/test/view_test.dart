// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:qrkude_flutter/qrkude_flutter.dart';

QRKudePainter _painter(WidgetTester tester) {
  final paints = tester.widgetList<CustomPaint>(find.byType(CustomPaint)).where((p) => p.painter is QRKudePainter);
  expect(paints, hasLength(1));
  return paints.single.painter! as QRKudePainter;
}

Widget _app(Widget child) => MaterialApp(home: Scaffold(body: Center(child: child)));

// A 1x1 transparent PNG.
final Uint8List _png1x1 = Uint8List.fromList(const [
  0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, //
  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
  0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x60, 0x00, 0x02, 0x00,
  0x00, 0x05, 0x00, 0x01, 0x7A, 0x5E, 0xAB, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
  0xAE, 0x42, 0x60, 0x82,
]);

void main() {
  testWidgets('QRKudeView paints a QR code at the requested size', (tester) async {
    await tester.pumpWidget(_app(const QRKudeView(data: 'https://kudelink.com', size: 200)));
    expect(find.byType(QRKudeView), findsOneWidget);
    expect(tester.getSize(find.byType(QRKudeView)), const Size(200, 200));
    final painter = _painter(tester);
    expect(painter.code.version, 2);
    expect(painter.code.ecc, Ecc.quartile); // M boosted to Q, as in the vectors
    expect(painter.style, const QRKudeStyle());
    expect(find.bySemanticsLabel('QR code'), findsOneWidget);
  });

  testWidgets('accepts JSON styles and documents', (tester) async {
    await tester.pumpWidget(_app(const QRKudeView(
      data: 'HELLO',
      size: 150,
      semanticsLabel: 'Hello code',
      style: {
        'encode': {'ecc': 'L', 'boostEcc': false},
        'style': {
          'modules': {'shape': 'liquid', 'color': '#1d3557'},
          'eyes': {'frame': 'leaf', 'ball': 'leaf'},
        },
      },
    )));
    final painter = _painter(tester);
    expect(painter.style.modules.shape, ModuleShape.liquid);
    expect(painter.style.eyes.frame, EyeShape.leaf);
    expect(painter.code.ecc, Ecc.low);
    expect(find.bySemanticsLabel('Hello code'), findsOneWidget);
  });

  testWidgets('a label band changes the aspect ratio', (tester) async {
    await tester.pumpWidget(_app(const QRKudeView(
      data: 'x',
      size: 290,
      style: QRKudeStyle(label: QRKudeLabel(text: 'SCAN ME', size: 2)),
    )));
    // canvas 29 x 32 modules
    final size = tester.getSize(find.byType(QRKudeView));
    expect(size.width, 290);
    expect(size.height, closeTo(320, 1e-9));
  });

  testWidgets('without a size it fills the available width', (tester) async {
    await tester.pumpWidget(_app(const SizedBox(width: 123, child: QRKudeView(data: 'x'))));
    expect(tester.getSize(find.byType(QRKudeView)), const Size(123, 123));
  });

  testWidgets('equal styles reuse the encoded code (no repaint)', (tester) async {
    // a fresh (non-identical but equal) style map on every build
    Map<String, Object?> styleJson() => {
          'modules': {'shape': 'dot'},
        };
    Widget build(Color background) => _app(ColoredBox(
          color: background,
          child: QRKudeView(data: 'same data', size: 100, style: styleJson()),
        ));
    await tester.pumpWidget(build(Colors.white));
    final first = _painter(tester);
    await tester.pumpWidget(build(Colors.grey));
    final second = _painter(tester);
    expect(identical(first.code, second.code), isTrue);
    expect(second.shouldRepaint(first), isFalse);
    expect(identical(first.geometry, second.geometry), isTrue);

    await tester.pumpWidget(_app(const QRKudeView(data: 'other data', size: 100)));
    expect(_painter(tester).shouldRepaint(second), isTrue);
  });

  testWidgets('a logo image adds a default logo and selects ECC H', (tester) async {
    await tester.pumpWidget(_app(QRKudeView(data: 'https://kudelink.com/logo', size: 200, logo: MemoryImage(_png1x1))));
    final painter = _painter(tester);
    expect(painter.style.logo, const QRKudeLogo());
    expect(painter.code.ecc, Ecc.high);
    expect(lint(painter.code, painter.style), isNot(contains(QRKudeLint.logoUnreadable)));
  });

  testWidgets('errorBuilder is used when the data does not fit', (tester) async {
    await tester.pumpWidget(_app(QRKudeView(
      data: 'x' * 3000,
      size: 100,
      errorBuilder: (context, error) => Text('error: $error', textDirection: TextDirection.ltr),
    )));
    expect(find.textContaining('data too long'), findsOneWidget);
    expect(
        find.byType(CustomPaint).evaluate().where((e) => (e.widget as CustomPaint).painter is QRKudePainter), isEmpty);

    await tester.pumpWidget(_app(QRKudeView(
      data: 'ok',
      style: '{"modules": {"shape": "hexagon"}}',
      errorBuilder: (context, error) => const Text('bad style', textDirection: TextDirection.ltr),
    )));
    expect(find.text('bad style'), findsOneWidget);
  });

  test('paths are cached per code and style', () {
    final code = encode('cache');
    const style = QRKudeStyle(modules: QRKudeModules(shape: ModuleShape.fluid));
    final a = QRKudeGeometry.of(code, style);
    final b = QRKudeGeometry.of(code, const QRKudeStyle(modules: QRKudeModules(shape: ModuleShape.fluid)));
    expect(identical(a, b), isTrue);
    expect(a.paths.keys, [QRKudeLayer.background, QRKudeLayer.modules, QRKudeLayer.eyeFrames, QRKudeLayer.eyeBalls]);
    expect(a.layout.width, 29);
  });

  test('gradient paints follow the SVG stop rules', () {
    final g = gradientGeometry(
      encode('x'),
      const QRKudeStyle(
        modules: QRKudeModules(color: QRKudeLinearGradient(stops: [QRKudeGradientStop(0, 0)])),
      ),
      QRKudeLayer.modules,
    );
    expect(qrkudePaint(const QRKudeLinearGradient(stops: []), g), isNull);
    expect(qrkudePaint(const QRKudeRadialGradient(stops: [QRKudeGradientStop(0.5, 0xFF000080)]), g)!.color.toARGB32(),
        0x80FF0000);
    expect(qrkudePaint(null, g), isNull);
    expect(qrkudePaint(const QRKudeSolid(0x11223344), g)!.color.toARGB32(), 0x44112233);
  });

  test('label text style maps the CSS font list', () {
    final s = qrkudeLabelTextStyle(const QRKudeLabel(text: 'x', font: 'Georgia, "Times New Roman", serif'), 12);
    expect(s.fontFamily, 'Georgia');
    expect(s.fontFamilyFallback, ['Times New Roman', 'serif']);
    expect(s.fontWeight, FontWeight.bold);
    expect(qrkudeLabelTextStyle(const QRKudeLabel(text: 'x'), 12).fontFamily, isNull);
  });

  test('image export sizes are bounded', () {
    final painter = QRKudePainter(code: encode('x'));
    for (final w in [0.0, -5.0, double.nan, double.infinity, 20000.0]) {
      expect(() => painter.toPicture(w), throwsArgumentError, reason: '$w');
    }
    // a huge label band (label size clamps to 1000 modules) makes the image too tall
    final tall = QRKudePainter(code: encode('x'), style: const QRKudeStyle(label: QRKudeLabel(text: 'x', size: 1e9)));
    expect(() => tall.toPicture(1000), throwsArgumentError);
    expect(painter.toPicture(100), isA<ui.Picture>());
  });

  test('hostile JSON styles are rejected, not rendered', () {
    final deep = '{"style": {"x": ${'[' * 1000}${']' * 1000}}}';
    expect(() => QRKudeDocument.from(deep), throwsFormatException);
    expect(() => QRKudeDocument.from('{"margin": 1e999}'), throwsFormatException);
  });
}
