// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

// Conformance: spec/vectors/{encode,render,payloads}.json, byte for byte.
// Runs on the VM (`dart test`) and compiled to JavaScript (`dart test -p node`).
// Outside the QRKude repository the vector tests are skipped unless
// QRKUDE_VECTORS points to a copy of spec/vectors.

import 'dart:convert';

import 'package:qrkude/qrkude.dart';
import 'package:test/test.dart';

import 'support/vectors.dart';

Map<String, Object?> _doc(Map<String, Object?> v) => {
      if (v.containsKey('encode')) 'encode': v['encode'],
      'style': v['style'],
      if (v.containsKey('svg')) 'svg': v['svg'],
    };

const _missing = 'spec/vectors not found: run inside the QRKude repository or set QRKUDE_VECTORS';

void main() {
  group('encode vectors', () {
    final vectors = loadVectors('encode');
    if (vectors == null) return test('encode.json', () {}, skip: _missing);
    test('count', () => expect(vectors, isNotEmpty));
    for (final (i, v) in vectors.indexed) {
      final text = v['text'] as String;
      final label = '#$i ${text.length > 24 ? '${text.substring(0, 24)}...' : text} ${jsonEncode(v['encode'])}';
      test(label, () {
        final doc = QRKudeDocument.fromJson({'encode': v['encode']});
        if (v.containsKey('error')) {
          expect(v['error'], 'data too long');
          expect(() => doc.encodeText(text), throwsA(isA<DataTooLongException>()));
          return;
        }
        final code = doc.encodeText(text);
        expect(code.version, v['version']);
        expect(code.ecc.letter, v['ecc']);
        expect(code.mask, v['mask']);
        expect(code.mode.name, v['mode']);
        expect(code.toRows(), v['matrix']);
      });
    }
  });

  group('render vectors', () {
    final vectors = loadVectors('render');
    if (vectors == null) return test('render.json', () {}, skip: _missing);
    test('count', () => expect(vectors, isNotEmpty));
    for (final v in vectors) {
      test(v['name'] as String, () {
        final text = v['text'] as String;
        // raw-text cases (duplicate keys, limits, errors) are read from `json`
        final Object docJson = v.containsKey('json') ? v['json'] as String : _doc(v);

        if (v.containsKey('error')) {
          switch (v['error']) {
            case 'parse error':
              expect(() => QRKudeDocument.fromJson(docJson), throwsFormatException);
            case 'invalid argument':
              final doc = QRKudeDocument.fromJson(docJson);
              expect(() => doc.encodeText(text), throwsArgumentError);
            case 'data too long':
              final doc = QRKudeDocument.fromJson(docJson);
              expect(() => doc.encodeText(text), throwsA(isA<DataTooLongException>()));
            default:
              fail('unknown error kind ${v['error']}');
          }
          return;
        }

        final doc = QRKudeDocument.fromJson(docJson);
        if (docJson is Map) {
          // the JSON string reader and the Map reader agree
          expect(QRKudeDocument.fromJson(jsonEncode(docJson)), doc);
        }
        // and the style survives a toJson round trip
        expect(QRKudeStyle.fromJson(doc.style.toJson()), doc.style);

        final code = doc.encodeText(text);
        expect(code.version, v['version'], reason: 'version');
        expect(code.ecc.letter, v['ecc'], reason: 'ecc');
        expect(code.mask, v['mask'], reason: 'mask');
        expect(toSvg(code, doc.style, doc.svgOptions), v['output']);
        expect(svg(text, docJson), v['output']);
        expect(lint(code, doc.style), v['lint']);
        if (v.containsKey('logoDamage')) {
          final expected = (v['logoDamage'] as num).toDouble();
          expect(logoDamage(code, doc.style), closeTo(expected, 1e-6));
        }
      });
    }
  });

  group('payload vectors', () {
    final vectors = loadVectors('payloads');
    if (vectors == null) return test('payloads.json', () {}, skip: _missing);
    test('count', () => expect(vectors, isNotEmpty));
    for (final (i, v) in vectors.indexed) {
      final type = v['type'] as String;
      test('#$i $type', () {
        final a = v['args'] as Map<String, Object?>;
        String? s(String k) => a[k] as String?;
        final out = switch (type) {
          'wifi' => QRKudePayload.wifi(
              ssid: s('ssid')!, password: s('password'), auth: s('auth'), hidden: a['hidden'] as bool? ?? false),
          'email' => QRKudePayload.email(to: s('to')!, subject: s('subject'), body: s('body')),
          'sms' => QRKudePayload.sms(number: s('number')!, message: s('message')),
          'tel' => QRKudePayload.tel(s('number')!),
          'geo' => QRKudePayload.geo((a['latitude'] as num).toDouble(), (a['longitude'] as num).toDouble()),
          'vcard' => QRKudePayload.vcard(
              firstName: s('firstName'),
              lastName: s('lastName'),
              organization: s('organization'),
              title: s('title'),
              phone: s('phone'),
              email: s('email'),
              url: s('url'),
              address: s('address'),
              note: s('note'),
            ),
          'event' => QRKudePayload.event(
              summary: s('summary')!,
              start: s('start'),
              end: s('end'),
              location: s('location'),
              description: s('description'),
            ),
          _ => throw StateError('unknown payload type $type'),
        };
        expect(out, v['output']);
      });
    }
  });
}
