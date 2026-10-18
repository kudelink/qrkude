// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// QRKude: styled QR codes (dot, fluid, liquid and bar modules, custom eyes,
/// gradients, logos, frames and labels) with SVG output that is
/// byte-identical across all QRKude ports.
///
/// ```dart
/// import 'package:qrkude/qrkude.dart';
///
/// final svgText = svg('https://kudelink.com', {
///   'style': {
///     'modules': {'shape': 'fluid'},
///     'eyes': {'frame': 'rounded', 'ball': 'circle'},
///   },
/// });
/// ```
///
/// Pure Dart, no `dart:io`: works on the VM, in Flutter and on the web.
library;

export 'src/color.dart' show QRKudeColor;
export 'src/document.dart' show QRKudeDocument, encodeForStyle, svg, paintToJson, styleToJson;
export 'src/encoder.dart'
    show
        DataTooLongException,
        Ecc,
        EncodeMode,
        EncodeOptions,
        QRCode,
        QRKudeException,
        alignmentPositions,
        eccCodewordsPerBlock,
        encode,
        encodeBytes,
        isFunctionModule,
        numEccBlocks,
        rawCodewords;
export 'src/layout.dart'
    show
        QRKudeGradientGeometry,
        QRKudeLayer,
        QRKudeLayout,
        QRKudePathSink,
        ballShape,
        computeLayout,
        emitLayer,
        gradientGeometry,
        layerPaint,
        layerVisible,
        logoDamage;
export 'src/lint.dart' show QRKudeLint, lint;
export 'src/payload.dart' show QRKudePayload;
export 'src/style.dart';
export 'src/svg.dart' show SvgOptions, SvgPathSink, formatNumber, layerPathData, sanitizeIdPrefix, toSvg;
export 'src/text.dart' show normalizeString, sanitizeText;

/// Version of this library (matches the C core's `QRK_LIB_VERSION`).
const String qrkudeVersion = '0.1.0';
