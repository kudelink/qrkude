// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

// Renders the style gallery (spec/cases/gallery.json) to PNG with
// QRKudePainter and writes the files plus a manifest.json (text, PNG file and
// the matching SVG) so that they can be decode-checked externally.
//
//   QRKUDE_PNG_DIR=/tmp/qrkude-png flutter test test/png_test.dart
//
// Without QRKUDE_PNG_DIR the files go to a temporary directory.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';
import 'package:qrkude_flutter/qrkude_flutter.dart';

const _texts = [
  'https://kudelink.com',
  'HELLO QRKUDE 2026',
  'Wi-Fi: WIFI:T:WPA;S:Kude;P:secret;; ñ ü €',
  'https://example.com/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
      'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
];

/// Extra styles on top of the gallery: eye paints, top label, alignment,
/// borders with transparency.
const _extra = <Map<String, Object?>>[
  {
    'name': 'kitchen-sink',
    'text': 'https://kudelink.com/kitchen-sink?utm_source=qr',
    'style': {
      'margin': 3,
      'modules': {
        'shape': 'liquid',
        'roundness': 0.9,
        'color': {
          'type': 'linear',
          'angle': 135,
          'stops': [
            {'offset': 0, 'color': '#0b3d91'},
            {'offset': 0.5, 'color': '#00a896'},
            {'offset': 1, 'color': '#1d3557'},
          ],
        },
      },
      'eyes': {
        'frame': 'leaf',
        'ball': 'drop',
        'frameColor': '#0b3d91',
        'ballColor': {
          'type': 'radial',
          'stops': [
            {'offset': 0, 'color': '#e63946'},
            {'offset': 1, 'color': '#9d0208'},
          ],
        },
      },
      'alignment': {'frame': 'circle', 'ball': 'circle'},
      'background': {'color': '#fdfcdc', 'radius': 4},
      'border': {'width': 1.5, 'color': '#0b3d91'},
      'logo': {'href': r'$LOGO', 'size': 0.2, 'shape': 'rounded', 'background': '#fdfcdc'},
      'label': {'text': 'kudelink.com', 'color': '#0b3d91'},
    },
  },
  {
    'name': 'label-top',
    'text': 'https://kudelink.com/top',
    'style': {
      'modules': {'shape': 'classy', 'roundness': 0.7},
      'eyes': {'frame': 'drop', 'ball': 'circle'},
      'label': {'text': 'Top label', 'position': 'top', 'bold': false, 'size': 2.5, 'color': '#e63946'},
    },
  },
  {
    'name': 'star-gradient-bg',
    'text': 'https://kudelink.com/star',
    'style': {
      'modules': {'shape': 'star', 'size': 0.95},
      'eyes': {'frame': 'circle', 'ball': 'circle'},
      'background': {
        'color': {
          'type': 'linear',
          'angle': 90,
          'stops': [
            {'offset': 0, 'color': '#ffffff'},
            {'offset': 1, 'color': '#e0f7fa'},
          ],
        },
      },
    },
  },
  {
    'name': 'alignment-dots',
    'text': 'https://kudelink.com/alignment-patterns-alignment-patterns-alignment-patterns',
    'style': {
      'modules': {'shape': 'rounded', 'size': 0.85, 'roundness': 0.5},
      'eyes': {'frame': 'bevel', 'ball': 'bevel'},
      'alignment': {'frame': 'dots', 'ball': 'circle'},
    },
  },
];

/// A 64x64 logo: red disc with a white square (like spec/tools/check_decode.py).
Future<ui.Image> _logo() {
  final recorder = ui.PictureRecorder();
  final canvas = ui.Canvas(recorder);
  canvas.drawOval(const ui.Rect.fromLTRB(2, 2, 61, 61), ui.Paint()..color = const ui.Color(0xFFE63946));
  canvas.drawRect(const ui.Rect.fromLTRB(19.2, 19.2, 44.8, 44.8), ui.Paint()..color = const ui.Color(0xFFFFFFFF));
  return recorder.endRecording().toImage(64, 64);
}

void main() {
  testWidgets('renders the style gallery to PNG', (tester) async {
    final outDir = Platform.environment['QRKUDE_PNG_DIR'];
    final dir = outDir != null ? Directory(outDir) : Directory.systemTemp.createTempSync('qrkude_png_');
    dir.createSync(recursive: true);
    final galleryPath = Platform.environment['QRKUDE_GALLERY'] ?? '../../spec/cases/gallery.json';
    final gallery = (jsonDecode(File(galleryPath).readAsStringSync()) as List<Object?>).cast<Map<String, Object?>>();
    final cases = [...gallery, ..._extra];
    const width = 360.0;

    await tester.runAsync(() async {
      final logo = await _logo();
      final logoPng = (await logo.toByteData(format: ui.ImageByteFormat.png))!.buffer.asUint8List();
      final logoUri = 'data:image/png;base64,${base64Encode(logoPng)}';
      final manifest = <Map<String, Object?>>[];

      for (final c in cases) {
        final name = c['name'] as String;
        var json = jsonEncode({'style': c['style']});
        json = json.replaceAll(r'$LOGO', logoUri);
        final doc = QRKudeDocument.fromJson(json);
        final texts = c.containsKey('text') ? [c['text'] as String] : _texts;
        for (final (i, text) in texts.indexed) {
          final code = doc.encodeText(text);
          final painter = QRKudePainter(code: code, style: doc.style, logo: logo);
          final Uint8List png = await painter.toPngBytes(width);
          expect(png.sublist(0, 8), [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
          final file = '$name-$i.png';
          File('${dir.path}/$file').writeAsBytesSync(png);
          final layout = painter.geometry.layout;
          manifest.add({
            'name': name,
            'text': text,
            'png': file,
            'width': width.round(),
            'height': (width / (layout.width / layout.height)).round(),
            'svg': toSvg(code, doc.style),
            'label': layout.hasLabel,
            'lint': lint(code, doc.style),
          });
        }
      }
      File('${dir.path}/manifest.json').writeAsStringSync(const JsonEncoder.withIndent(' ').convert(manifest));
      // ignore: avoid_print
      print('wrote ${manifest.length} PNG files to ${dir.path}');
      expect(manifest.length, greaterThan(60));
    });
  });

  testWidgets('qrkudeToPngBytes encodes with the default-ECC rule', (tester) async {
    await tester.runAsync(() async {
      final png = await qrkudeToPngBytes('https://kudelink.com',
          style: const {
            'modules': {'shape': 'dot'},
          },
          width: 100);
      final codec = await ui.instantiateImageCodec(png);
      final frame = await codec.getNextFrame();
      expect(frame.image.width, 100);
      expect(frame.image.height, 100);
      frame.image.dispose();
    });
  });
}
