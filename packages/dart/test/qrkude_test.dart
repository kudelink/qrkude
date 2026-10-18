// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'package:qrkude/qrkude.dart';
import 'package:test/test.dart';

class _Recorder implements QRKudePathSink {
  final List<String> ops = [];

  @override
  void moveTo(double x, double y) => ops.add('M ${formatNumber(x)} ${formatNumber(y)}');

  @override
  void lineTo(double x, double y) => ops.add('L ${formatNumber(x)} ${formatNumber(y)}');

  @override
  void arcTo(double r, bool clockwise, double x, double y, double cx, double cy) =>
      ops.add('A ${formatNumber(r)} ${clockwise ? 'cw' : 'ccw'} ${formatNumber(x)} ${formatNumber(y)}');

  @override
  void close() => ops.add('Z');
}

void main() {
  group('formatNumber', () {
    test('SPEC 8.1 examples', () {
      expect(formatNumber(0.0625), '0.063');
      expect(formatNumber(-0.0625), '-0.062');
      expect(formatNumber(-0.0001), '0');
      expect(formatNumber(-0.0), '0');
      expect(formatNumber(29), '29');
      expect(formatNumber(1.5), '1.5');
      expect(formatNumber(1.25), '1.25');
      expect(formatNumber(2 - 1e-12), '2');
      expect(formatNumber(1234567.25), '1234567.25');
      expect(formatNumber(-3.1), '-3.1');
    });
  });

  group('colors', () {
    test('parse', () {
      expect(QRKudeColor.parse('#000'), QRKudeColor.black);
      expect(QRKudeColor.parse('#FFFFFF'), QRKudeColor.white);
      expect(QRKudeColor.parse('#11223380'), 0x11223380);
      expect(QRKudeColor.parse('#a1c'), 0xAA11CCFF);
      expect(QRKudeColor.parse('transparent'), 0);
      expect(QRKudeColor.tryParse('red'), isNull);
      expect(QRKudeColor.tryParse('#12345'), isNull);
      expect(QRKudeColor.tryParse('#gggggg'), isNull);
      expect(QRKudeColor.tryParse('TRANSPARENT'), isNull);
    });

    test('format', () {
      expect(QRKudeColor.toHex(0xAA11CCFF), '#aa11cc');
      expect(QRKudeColor.toHex(0x11223380), '#11223380');
      expect(QRKudeColor.toArgb(0x11223380), 0x80112233);
    });
  });

  group('encoder', () {
    test('modes', () {
      expect(encode('0123').mode, EncodeMode.numeric);
      expect(encode('HELLO WORLD').mode, EncodeMode.alphanumeric);
      expect(encode('hello').mode, EncodeMode.byte);
    });

    test('default ECC is M (boosted)', () {
      final code = encode('x' * 100, const EncodeOptions(boostEcc: false));
      expect(code.ecc, Ecc.medium);
    });

    test('invalid options', () {
      expect(() => encode('x', const EncodeOptions(minVersion: 0)), throwsArgumentError);
      expect(() => encode('x', const EncodeOptions(mask: 8)), throwsArgumentError);
      expect(() => encode('x', const EncodeOptions(minVersion: 5, maxVersion: 4)), throwsArgumentError);
    });

    test('data too long', () {
      expect(() => encode('x' * 3000), throwsA(isA<DataTooLongException>()));
    });

    test('isDark out of range is light', () {
      final code = encode('x');
      expect(code.isDark(-1, 0), isFalse);
      expect(code.isDark(code.size, 0), isFalse);
      expect(code.isDark(0, 0), isTrue);
    });

    test('equality', () {
      expect(encode('same'), encode('same'));
      expect(encode('same').hashCode, encode('same').hashCode);
      expect(encode('same') == encode('other'), isFalse);
    });
  });

  group('document', () {
    test('default ECC rule', () {
      expect(QRKudeDocument.fromJson({}).effectiveEncodeOptions.ecc, Ecc.medium);
      expect(
        QRKudeDocument.fromJson({
          'style': {'logo': {}},
        }).effectiveEncodeOptions.ecc,
        Ecc.high,
      );
      expect(
        QRKudeDocument.fromJson({
          'encode': {'ecc': 'L'},
          'style': {'logo': {}},
        }).effectiveEncodeOptions.ecc,
        Ecc.low,
      );
    });

    test('wrapper and top-level keys', () {
      final doc = QRKudeDocument.fromJson('{"margin": 2, "style": {"modules": {"shape": "dot"}}, '
          '"svg": {"width": 300, "idPrefix": "x"}, "encode": {"minVersion": 3.9, "mask": 2}}');
      expect(doc.style.margin, 2);
      expect(doc.style.modules.shape, ModuleShape.dot);
      expect(doc.svgOptions, const SvgOptions(width: 300, idPrefix: 'x'));
      expect(doc.encodeOptions.minVersion, 3);
      expect(doc.encodeOptions.mask, 2);
      expect(doc.encodeOptions.ecc, isNull);
    });

    test('duplicate keys are applied in document order (objects merge)', () {
      final s = QRKudeStyle.fromJson('{"modules": {"shape": "dot"}, "modules": {"size": 0.5}, '
          '"logo": {"href": "a.png", "size": 0.3}, "logo": null, "logo": {"margin": 2}}');
      expect(s.modules.shape, ModuleShape.dot);
      expect(s.modules.size, 0.5);
      // logo: null forgets the earlier members; the next object starts from defaults
      expect(s.logo, const QRKudeLogo(margin: 2));
      final l = QRKudeStyle.fromJson('{"label": {"text": "a", "size": 9, "bold": false}, "label": null, '
          '"label": {"text": "b"}}');
      expect(l.label, const QRKudeLabel(text: 'b'));
    });

    test('logo and label presence', () {
      expect(QRKudeStyle.fromJson({'logo': null}).logo, isNull);
      expect(QRKudeStyle.fromJson({'logo': {}}).logo, const QRKudeLogo());
      expect(
        QRKudeStyle.fromJson({
          'logo': {'size': 0},
        }).hasLogo,
        isFalse,
      );
      expect(
        QRKudeStyle.fromJson({
          'label': {'text': ''},
        }).label,
        isNull,
      );
      expect(
        QRKudeStyle.fromJson({
          'label': {'text': 'Hi', 'font': null},
        }).label!.font,
        '',
      );
    });

    test('paints', () {
      final s = QRKudeStyle.fromJson({
        'modules': {
          'color': {
            'type': 'radial',
            'r': 1,
            'stops': [
              for (var i = 0; i < 10; i++) {'offset': i / 10, 'color': null},
            ],
          },
        },
        'eyes': {'frameColor': '#f00', 'ballColor': null},
        'background': {'color': null},
      });
      final m = s.modules.color as QRKudeRadialGradient;
      expect(m.r, 1);
      expect(m.cx, 0.5);
      expect(m.stops.length, 8);
      expect(m.stops.first, const QRKudeGradientStop(0, 0));
      expect(s.eyes.frameColor, const QRKudeSolid(0xFF0000FF));
      expect(s.eyes.ballColor, isNull);
      expect(s.background.color, isNull);
    });

    test('alignment', () {
      expect(QRKudeStyle.fromJson({'alignment': null}).alignment, const QRKudeAlignment());
      expect(
        QRKudeStyle.fromJson({
          'alignment': {'frame': 'leaf', 'ball': null},
        }).alignment,
        const QRKudeAlignment(frame: EyeShape.leaf),
      );
    });

    test('invalid values are rejected like the C reader', () {
      final bad = <Object>[
        '[]',
        '{"modules": {"shape": "hexagon"}}',
        '{"modules": {"shape": null}}',
        '{"modules": null}',
        '{"margin": "4"}',
        '{"margin": null}',
        '{"eyes": {"frame": null}}',
        '{"modules": {"color": "#12"}}',
        '{"modules": {"color": 5}}',
        '{"modules": {"color": {"type": "conic"}}}',
        '{"logo": {"shape": "star"}}',
        '{"logo": {"excavate": 1}}',
        '{"label": {"position": "left"}}',
        '{"encode": {"ecc": "X"}}',
        '{"encode": {"ecc": "LM"}}',
        '{"border": {"color": {"type": "linear"}}}',
        '{"style": null}',
        '{"modules": {"size": 1',
        '{"label": {"text": 3}}',
      ];
      for (final json in bad) {
        expect(() => QRKudeDocument.fromJson(json), throwsFormatException, reason: '$json');
      }
      expect(
        () => QRKudeDocument.fromJson({
          'margin': [1],
        }),
        throwsFormatException,
      );
    });

    test('unknown keys and trailing content are ignored', () {
      final s = QRKudeStyle.fromJson('{"future": {"a": [1, {"b": null}]}, "margin": 1e0} trailing');
      expect(s, const QRKudeStyle(margin: 1));
    });

    test('string escapes', () {
      final s = QRKudeStyle.fromJson(r'{"label": {"text": "a\"b\\c\/dñ😀\n"}}');
      expect(s.label!.text, 'a"b\\c/dñ😀\n');
    });

    test('toJson round trip', () {
      const style = QRKudeStyle(
        margin: 2.5,
        modules: QRKudeModules(
          shape: ModuleShape.liquid,
          roundness: 0.7,
          color: QRKudeLinearGradient(angle: 45, stops: [
            QRKudeGradientStop(0, 0x6A11CBFF),
            QRKudeGradientStop(1, 0x2575FC80),
          ]),
        ),
        eyes: QRKudeEyes(frame: EyeShape.leaf, ball: EyeShape.dots, ballColor: QRKudeSolid(0xE63946FF)),
        alignment: QRKudeAlignment(frame: EyeShape.circle),
        background: QRKudeBackground(color: null, radius: 3),
        border: QRKudeBorder(width: 1, color: 0x1D3557FF),
        logo: QRKudeLogo(href: 'data:,x', shape: LogoShape.circle, background: 0xFFFFFFFF),
        label: QRKudeLabel(text: 'SCAN ME', position: LabelPosition.top, bold: false),
      );
      expect(QRKudeStyle.fromJson(style.toJson()), style);
      final doc = QRKudeDocument(
        encodeOptions: const EncodeOptions(ecc: Ecc.quartile, mask: 3),
        style: style,
        svgOptions: const SvgOptions(width: 128),
      );
      expect(QRKudeDocument.fromJson(doc.toJson()), doc);
    });
  });

  group('geometry', () {
    test('emitter normalization (SPEC 7)', () {
      final code = encode('x');
      final rec = _Recorder();
      final count = emitLayer(code, const QRKudeStyle(), QRKudeLayer.eyeBalls, rec);
      expect(count, 3);
      // a square: move, 3 lines, close (the closing line is omitted)
      expect(rec.ops.sublist(0, 5), ['M 6 6', 'L 9 6', 'L 9 9', 'L 6 9', 'Z']);
    });

    test('dot frames and star arcs', () {
      final code = encode('x');
      final rec = _Recorder();
      const style = QRKudeStyle(modules: QRKudeModules(shape: ModuleShape.star));
      emitLayer(code, style, QRKudeLayer.modules, rec);
      expect(rec.ops.where((o) => o.startsWith('A')).every((o) => o.contains('ccw')), isTrue);
      expect(emitLayer(code, const QRKudeStyle(eyes: QRKudeEyes(frame: EyeShape.dots)), QRKudeLayer.eyeFrames), 3 * 24);
    });

    test('layout', () {
      final code = encode('x');
      final layout = computeLayout(
        code,
        const QRKudeStyle(
          border: QRKudeBorder(width: 1),
          label: QRKudeLabel(text: 'L', size: 2, position: LabelPosition.top),
          logo: QRKudeLogo(size: 0.5),
        ),
      );
      expect(layout.width, 21 + 10);
      expect(layout.height, 31 + 3);
      expect(layout.symbolX, 5);
      expect(layout.symbolY, 8);
      expect(layout.labelBaseline, 1 + 2.2);
      expect(layout.logoSize, 10.5);
      expect(layout.logoX, 5 + 10.5 - 5.25);
    });

    test('layer paints', () {
      const style = QRKudeStyle(modules: QRKudeModules(color: QRKudeSolid(0x112233FF)));
      expect(layerPaint(style, QRKudeLayer.eyeFrames), const QRKudeSolid(0x112233FF));
      expect(layerPaint(style, QRKudeLayer.border), const QRKudeSolid(QRKudeColor.black));
    });

    test('gradient geometry special angles', () {
      final code = encode('x');
      for (final (angle, x1, y1, x2, y2) in [
        (0.0, 4.0, 14.5, 25.0, 14.5),
        (90.0, 14.5, 4.0, 14.5, 25.0),
        (180.0, 25.0, 14.5, 4.0, 14.5),
        (-90.0, 14.5, 25.0, 14.5, 4.0),
        (720.0, 4.0, 14.5, 25.0, 14.5),
      ]) {
        final style = QRKudeStyle(
          modules: QRKudeModules(
            color: QRKudeLinearGradient(angle: angle, stops: const [QRKudeGradientStop(0, 0)]),
          ),
        );
        final g = gradientGeometry(code, style, QRKudeLayer.modules);
        expect([g.x1, g.y1, g.x2, g.y2], [x1, y1, x2, y2], reason: '$angle');
      }
    });
  });

  group('lint', () {
    test('eye shapes', () {
      final code = encode('x');
      List<String> eyes(EyeShape f, EyeShape b) => lint(code, QRKudeStyle(eyes: QRKudeEyes(frame: f, ball: b)));
      expect(eyes(EyeShape.diamond, EyeShape.diamond), [QRKudeLint.eyeShapes]);
      expect(eyes(EyeShape.diamond, EyeShape.circle), [QRKudeLint.eyeShapes]);
      expect(eyes(EyeShape.square, EyeShape.diamond), [QRKudeLint.eyeShapes]);
      expect(eyes(EyeShape.leaf, EyeShape.diamond), [QRKudeLint.eyeShapes]);
      expect(eyes(EyeShape.rounded, EyeShape.diamond), isEmpty);
      expect(eyes(EyeShape.bevel, EyeShape.diamond), isEmpty);
    });

    test('contrast and background', () {
      final code = encode('x');
      expect(
        lint(
          code,
          const QRKudeStyle(
            modules: QRKudeModules(color: QRKudeSolid(0xFFFFFFFF)),
            background: QRKudeBackground(color: QRKudeSolid(0x000000FF)),
            margin: 1,
          ),
        ),
        [QRKudeLint.inverted, QRKudeLint.quietZone],
      );
      expect(
        lint(code, const QRKudeStyle(background: QRKudeBackground(color: null))),
        [QRKudeLint.transparentBackground],
      );
      expect(
        lint(code, const QRKudeStyle(modules: QRKudeModules(color: QRKudeSolid(0xBBBBBBFF)))),
        [QRKudeLint.lowContrast],
      );
    });
  });

  group('hardening (SPEC 2.0, 2.2, 8.1, 8.3, 11, 12)', () {
    test('idPrefix is sanitized', () {
      expect(sanitizeIdPrefix(null), 'qrk');
      expect(sanitizeIdPrefix(''), 'qrk');
      expect(sanitizeIdPrefix('ok_-1'), 'ok_-1');
      expect(sanitizeIdPrefix('1abc'), 'q1abc');
      expect(sanitizeIdPrefix('-x'), 'q-x');
      expect(sanitizeIdPrefix('ñ'), '__'); // one '_' per UTF-8 byte
      expect(sanitizeIdPrefix('x"/><svg onload="a()">'), 'x____svg_onload__a____');
      const style = QRKudeStyle(
        modules: QRKudeModules(color: QRKudeLinearGradient(stops: [QRKudeGradientStop(0, 0)])),
      );
      final out = toSvg(encode('x'), style, const SvgOptions(idPrefix: '"><script>alert(1)</script>'));
      expect(out, isNot(contains('<script')));
      expect(out, contains('id="___script_alert_1___script_-fg"'));
    });

    test('fmt guards', () {
      expect(formatNumber(double.nan), '0');
      expect(formatNumber(double.infinity), '0');
      expect(formatNumber(-1e15), '0');
      expect(formatNumber(1e15), '0');
      // large values: exact 64-bit integer semantics, also in JavaScript
      // (same strings as the C reference)
      expect(formatNumber(999999999999999.9), '999999999999999.872');
      expect(formatNumber(123456789012.3456), '123456789012.346');
      expect(formatNumber(98765432109876.5), '98765432109876.496');
      expect(formatNumber(-98765432109876.5), '-98765432109876.496');
      expect(formatNumber(72057594037927.937), '72057594037927.936');
      expect(formatNumber(9007199254740.993), '9007199254740.992');
      expect(formatNumber(5e13), '50000000000000');
    });

    test('length clamps and NaN', () {
      final code = encode('x');
      final l = computeLayout(
        code,
        const QRKudeStyle(
          margin: 1e6,
          border: QRKudeBorder(width: 5000),
          label: QRKudeLabel(text: 'x', size: 1e9),
        ),
      );
      expect(l.symbolX, 2000);
      expect(l.labelFontSize, 1000);
      final n = computeLayout(code, const QRKudeStyle(margin: double.nan, modules: QRKudeModules(size: double.nan)));
      expect(n.symbolX, 0);
      expect(
          lint(code, const QRKudeStyle(modules: QRKudeModules(size: double.nan))), contains(QRKudeLint.smallModules));
    });

    test('numbers', () {
      expect(QRKudeStyle.fromJson('{"margin": 2.5e0}').margin, 2.5);
      expect(QRKudeStyle.fromJson('{"x": 0.${'1' * 398}}'), const QRKudeStyle());
      for (final bad in [
        '{"x": 0.${'1' * 399}}',
        '{"margin": 1e999}',
        '{"future": -1e999}',
        '{"future": 1e999999999999999999999999}',
        '{"modules": {"size": 1e400}}',
      ]) {
        expect(() => QRKudeStyle.fromJson(bad), throwsFormatException, reason: bad);
      }
      expect(() => QRKudeStyle.fromJson({'margin': double.infinity}), throwsFormatException);
      expect(() => QRKudeStyle.fromJson({'x': double.nan}), throwsFormatException);
      // tiny numbers underflow to (signed) zero, they are finite
      expect(QRKudeStyle.fromJson('{"margin": -1e-999}').margin, 0);
    });

    test('integer fields clamp to [-1000, 1000] and truncate', () {
      EncodeOptions e(String json) => QRKudeDocument.fromJson(json).encodeOptions;
      expect(e('{"encode": {"mask": 6.9}}').mask, 6);
      expect(e('{"encode": {"mask": -1.7}}').mask, -1);
      expect(e('{"encode": {"minVersion": 1e100}}').minVersion, 1000);
      expect(e('{"encode": {"maxVersion": -1e100}}').maxVersion, -1000);
      expect(() => QRKudeDocument.fromJson('{"encode": {"minVersion": 1e100}}').encodeText('x'), throwsArgumentError);
    });

    test('nesting limit', () {
      String nested(int arrays) => '{"style": {"x": ${'[' * arrays}${']' * arrays}}}';
      expect(QRKudeStyle.fromJson(nested(62)), const QRKudeStyle()); // 64 levels
      expect(() => QRKudeStyle.fromJson(nested(63)), throwsFormatException);
      expect(() => QRKudeStyle.fromJson(nested(100000)), throwsFormatException); // no stack overflow
      Object deep = <Object?>[];
      for (var i = 0; i < 63; i++) {
        deep = [deep];
      }
      expect(() => QRKudeStyle.fromJson({'x': deep}), throwsFormatException);
      final cyclic = <String, Object?>{};
      cyclic['self'] = cyclic;
      expect(() => QRKudeStyle.fromJson(cyclic), throwsFormatException);
    });

    test('large hostile inputs fail fast', () {
      final sw = Stopwatch()..start();
      expect(() => QRKudeStyle.fromJson('{"x": 1e${'9' * 2000000}}'), throwsFormatException);
      expect(() => QRKudeStyle.fromJson('{"x": ${'1' * 2000000}}'), throwsFormatException);
      expect(() => QRKudeStyle.fromJson('{"x": ${'[' * 2000000}'), throwsFormatException);
      expect(QRKudeStyle.fromJson('{"x": "${'a' * 2000000}"}'), const QRKudeStyle());
      expect(sw.elapsed, lessThan(const Duration(seconds: 20)));
    });

    test('strings: U+0000, surrogates and control characters', () {
      QRKudeLabel label(String json) => QRKudeStyle.fromJson('{"label": {"text": "$json"}}').label!;
      expect(label(r'ab\u0000cd').text, 'ab');
      expect(QRKudeStyle.fromJson(r'{"label": {"text": "\u0000x"}}').label, isNull);
      expect(label(r'pair 😀').text, 'pair 😀');
      expect(label(r'lone \ud800 high').text, 'lone � high');
      expect(label(r'lone \udc00 low').text, 'lone � low');
      expect(label(r'swapped \udc00\ud800').text, 'swapped ��');
      // a high surrogate followed by a non-low escape: U+FFFD, then the escape on its own
      expect(label(r'\ud800A').text, '�A');
      expect(() => QRKudeStyle.fromJson(r'{"label": {"text": "\ud800\uzzzz"}}'), throwsFormatException);
      // keys and names end at U+0000 too
      expect(QRKudeStyle.fromJson(r'{"modules\u0000x": {"shape": "dot\u0000y"}}').modules.shape, ModuleShape.dot);
      // Map input follows the same rules
      expect(
          QRKudeStyle.fromJson({
            'label': {'text': 'a\u0000b'}
          }).label!.text,
          'a');
      expect(
          QRKudeStyle.fromJson({
            'label': {'text': 'x\uD800'}
          }).label!.text,
          'x�');

      // SVG text drops U+0001-U+001F except tab, LF and CR
      expect(sanitizeText('a\u0001b\u001fc\td\ne\rf\u0000g'), 'abc\td\ne\rf');
      final out = toSvg(
        encode('x'),
        const QRKudeStyle(label: QRKudeLabel(text: 'a\u0007<b>\u0000hidden', font: 'x\u0002y')),
        const SvgOptions(idPrefix: 'p\u0000q'),
      );
      expect(out, contains('font-family="xy"'));
      expect(out, contains('>a&lt;b&gt;</text>'));
      expect(out, isNot(contains('hidden')));
      // programmatic strings follow the C-string rule as well
      expect(const QRKudeStyle(label: QRKudeLabel(text: '\u0000x')).hasLabel, isFalse);
      expect(sanitizeIdPrefix('\u0000x'), 'qrk');
      expect(sanitizeIdPrefix('a\uD800'), 'a___'); // U+FFFD is 3 UTF-8 bytes
    });

    test('merging rules', () {
      final s = QRKudeStyle.fromJson('{"logo": {"size": 0.3}, "logo": {"href": "x.png"}, '
          '"alignment": {"frame": "circle"}, "alignment": null, "label": {"text": "a"}, "label": null}');
      expect(s.logo, const QRKudeLogo(href: 'x.png', size: 0.3));
      expect(s.alignment, const QRKudeAlignment());
      expect(s.label, isNull);
      final p = QRKudeStyle.fromJson('{"modules": {"color": {"type": "radial", "r": 2}}, '
          '"modules": {"color": {"stops": []}}}');
      expect(p.modules.color, const QRKudeLinearGradient(stops: []));
    });

    test('long and escaped keys', () {
      final s = QRKudeDocument.fromJson('{"encode": {"\\u0065cc": "L"}, "${'k' * 5000}": {"${'n' * 100}": 1}, '
          '"style": {"m\\u006fdules": {"shape": "dot"}}}');
      expect(s.encodeOptions.ecc, Ecc.low);
      expect(s.style.modules.shape, ModuleShape.dot);
    });

    test('lint invisible and contrast over the real background', () {
      final code = encode('x');
      expect(lint(code, const QRKudeStyle(modules: QRKudeModules(color: null))),
          [QRKudeLint.lowContrast, QRKudeLint.invisible]);
      expect(
        lint(
          code,
          const QRKudeStyle(
            modules: QRKudeModules(color: QRKudeSolid(0x00000080)),
            background: QRKudeBackground(color: QRKudeSolid(0x000000FF)),
          ),
        ),
        [QRKudeLint.lowContrast],
      );
      expect(
        lint(
          code,
          const QRKudeStyle(
            background: QRKudeBackground(
              color: QRKudeLinearGradient(stops: [
                QRKudeGradientStop(0, 0xFFFFFF00),
                QRKudeGradientStop(1, 0x00000000),
              ]),
            ),
          ),
        ),
        [QRKudeLint.transparentBackground],
      );
    });

    test('layerVisible', () {
      final code = encode('x');
      expect(layerVisible(code, const QRKudeStyle(), QRKudeLayer.modules), isTrue);
      expect(layerVisible(code, const QRKudeStyle(), QRKudeLayer.border), isFalse);
      expect(layerVisible(code, const QRKudeStyle(border: QRKudeBorder(width: 1)), QRKudeLayer.border), isTrue);
      expect(layerVisible(code, const QRKudeStyle(background: QRKudeBackground(color: null)), QRKudeLayer.background),
          isFalse);
      expect(
          layerVisible(code, const QRKudeStyle(logo: QRKudeLogo(background: 0xFFFFFFFF)), QRKudeLayer.logoBackground),
          isTrue);
    });

    test('layerVisible follows SPEC 8.5 for gradients', () {
      final code = encode('x');
      QRKudeStyle bg(List<QRKudeGradientStop> stops) =>
          QRKudeStyle(background: QRKudeBackground(color: QRKudeLinearGradient(stops: stops)));
      expect(layerVisible(code, bg(const []), QRKudeLayer.background), isFalse);
      expect(layerVisible(code, bg(const [QRKudeGradientStop(0, 0xFFFFFF00)]), QRKudeLayer.background), isFalse);
      expect(
          layerVisible(code, bg(const [QRKudeGradientStop(0, 0xFFFFFF00), QRKudeGradientStop(1, 0x00000001)]),
              QRKudeLayer.background),
          isTrue);
      expect(layerVisible(code, const QRKudeStyle(modules: QRKudeModules(color: QRKudeSolid(0))), QRKudeLayer.eyeBalls),
          isFalse);
    });

    test('geo clamps', () {
      expect(QRKudePayload.geo(95, -200), 'geo:90,-180');
      expect(QRKudePayload.geo(double.nan, double.infinity), 'geo:-90,180');
    });

    test('encodeForStyle', () {
      expect(encodeForStyle('x' * 30, const QRKudeStyle(logo: QRKudeLogo())).ecc, Ecc.high);
      expect(encodeForStyle('x' * 30, const QRKudeStyle()).ecc, Ecc.quartile);
      expect(
          encodeForStyle('x', const QRKudeStyle(logo: QRKudeLogo()), const EncodeOptions(ecc: Ecc.low, boostEcc: false))
              .ecc,
          Ecc.low);
    });
  });
}
