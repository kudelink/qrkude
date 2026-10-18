// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

// Prints a styled QR code as SVG, plus its lint flags.
//
//   dart run example/qrkude_example.dart > qr.svg

import 'package:qrkude/qrkude.dart';

void main() {
  // 1. A payload builder (Wi-Fi, vCard, event, mailto, SMS, tel, geo).
  final payload = QRKudePayload.wifi(ssid: 'Kude', password: 'secret', auth: 'WPA');

  // 2. A style, either as Dart objects...
  const style = QRKudeStyle(
    modules: QRKudeModules(
      shape: ModuleShape.liquid,
      color: QRKudeLinearGradient(angle: 45, stops: [
        QRKudeGradientStop(0, 0x6A11CBFF),
        QRKudeGradientStop(1, 0x2575FCFF),
      ]),
    ),
    eyes: QRKudeEyes(frame: EyeShape.rounded, ball: EyeShape.circle, frameColor: QRKudeSolid(0xE63946FF)),
    background: QRKudeBackground(radius: 3),
    label: QRKudeLabel(text: 'Wi-Fi'),
  );

  // ... or from the cross-language JSON document (same result).
  final fromJson = QRKudeDocument.fromJson('''{
    "style": {
      "modules": {"shape": "liquid", "color": {"type": "linear", "angle": 45,
        "stops": [{"offset": 0, "color": "#6a11cb"}, {"offset": 1, "color": "#2575fc"}]}},
      "eyes": {"frame": "rounded", "ball": "circle", "frameColor": "#e63946"},
      "background": {"radius": 3},
      "label": {"text": "Wi-Fi"}
    }
  }''');
  assert(fromJson.style == style);

  // 3. Encode (the default ECC is H with a logo, M otherwise) and render.
  final doc = QRKudeDocument(style: style);
  final code = doc.encodeText(payload);
  print(toSvg(code, style, const SvgOptions(width: 320)));

  // 4. Scannability checks, printed as an SVG comment.
  print('<!-- version ${code.version}, ECC ${code.ecc.letter}, lint: ${lint(code, style)} -->');
}
