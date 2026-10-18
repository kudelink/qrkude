// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// The cross-language JSON document `{"encode": .., "style": .., "svg": ..}`.
///
/// The reader follows SPEC 2.0 (and `core/src/qrk_json.c`), because styles
/// often come from untrusted sources:
/// - members apply in document order; a repeated key whose value is an
///   object merges field by field, scalars, arrays and paints replace;
///   `logo`/`label`/`alignment: null` reset that feature to its defaults;
/// - unknown keys of any length are ignored;
/// - numbers parse to the nearest double; a number token longer than 400
///   characters or a non-finite number is a parse error anywhere;
/// - nesting deeper than 64 objects/arrays is a parse error;
/// - a string ends at its first U+0000 and unpaired surrogates become U+FFFD;
/// - parse errors throw [FormatException]; every step is linear in the input.
library;

import 'color.dart';
import 'encoder.dart';
import 'style.dart';
import 'svg.dart';
import 'text.dart';

/// Everything needed to turn text into an SVG: encoder options, style and
/// SVG options.
///
/// It mirrors the cross-language JSON document
/// `{"encode": {...}, "style": {...}, "svg": {...}}`, so a style saved by
/// another QRKude port (web, Python, Rust, C) gives the same SVG here.
class QRKudeDocument {
  /// Creates a document; every part defaults to the defaults of its class.
  const QRKudeDocument({
    this.encodeOptions = const EncodeOptions(),
    this.style = const QRKudeStyle(),
    this.svgOptions = const SvgOptions(),
  });

  /// Parses a JSON `String` or a decoded `Map`: the wrapper document or a
  /// bare style object (top-level style keys are accepted too). Throws a
  /// [FormatException] on invalid input (see the README section "Untrusted
  /// styles").
  factory QRKudeDocument.fromJson(Object json) {
    final root = json is String ? _JsonParser(json).parseRoot() : _fromDart(json);
    if (root is! _Obj) throw const FormatException('the document must be a JSON object');
    final reader = _Reader(const QRKudeStyle());
    reader.document(root);
    return QRKudeDocument(encodeOptions: reader.encode, style: reader.build(), svgOptions: reader.svg);
  }

  /// Converts a document-like value: a [QRKudeDocument], a [QRKudeStyle], a
  /// JSON `String`/`Map`, or null (defaults).
  factory QRKudeDocument.from(Object? value) {
    if (value == null) return const QRKudeDocument();
    if (value is QRKudeDocument) return value;
    if (value is QRKudeStyle) return QRKudeDocument(style: value);
    return QRKudeDocument.fromJson(value);
  }

  /// `encode` options. A null [EncodeOptions.ecc] selects the default ECC
  /// for the style (`H` with a logo, else `M`).
  final EncodeOptions encodeOptions;

  /// How the code looks.
  final QRKudeStyle style;

  /// SVG output options (size attributes, gradient id prefix).
  final SvgOptions svgOptions;

  /// Encoder options with the default-ECC rule (SPEC 1.3) applied.
  EncodeOptions get effectiveEncodeOptions =>
      encodeOptions.ecc != null ? encodeOptions : encodeOptions.copyWith(ecc: style.recommendedEcc);

  /// Encodes [text] with [effectiveEncodeOptions]. Throws
  /// [DataTooLongException] or [ArgumentError] like [encode].
  QRCode encodeText(String text) => encode(text, effectiveEncodeOptions);

  /// Encodes [text] and returns the SVG document (a complete `<svg>` string).
  String toSvgString(String text) => toSvg(encodeText(text), style, svgOptions);

  /// A copy with the given parts replaced.
  QRKudeDocument copyWith({EncodeOptions? encodeOptions, QRKudeStyle? style, SvgOptions? svgOptions}) => QRKudeDocument(
        encodeOptions: encodeOptions ?? this.encodeOptions,
        style: style ?? this.style,
        svgOptions: svgOptions ?? this.svgOptions,
      );

  /// The JSON form `{"encode": .., "style": .., "svg": ..}`; pass it to
  /// `jsonEncode` to store or share the document.
  Map<String, Object?> toJson() => {
        'encode': encodeOptions.toJson(),
        'style': style.toJson(),
        'svg': svgOptions.toJson(),
      };

  @override
  bool operator ==(Object other) =>
      other is QRKudeDocument &&
      other.encodeOptions == encodeOptions &&
      other.style == style &&
      other.svgOptions == svgOptions;

  @override
  int get hashCode => Object.hash(encodeOptions, style, svgOptions);
}

/// Encodes [text] for [style]: a null [EncodeOptions.ecc] ("auto") becomes
/// `H` when the style has a logo and `M` otherwise (SPEC 1.3).
QRCode encodeForStyle(String text, QRKudeStyle style, [EncodeOptions options = const EncodeOptions()]) =>
    QRKudeDocument(encodeOptions: options, style: style).encodeText(text);

/// Encodes [text] and returns the SVG, applying the default-ECC rule.
///
/// [document] may be a [QRKudeDocument], a [QRKudeStyle], a JSON `String`
/// or `Map` (`{"encode": .., "style": .., "svg": ..}` or a bare style), or
/// null for the default style. Throws [FormatException] (invalid JSON),
/// [ArgumentError] (invalid encode options) or [DataTooLongException].
String svg(String text, [Object? document]) => QRKudeDocument.from(document).toSvgString(text);

// ---- serialization ----------------------------------------------------------

/// JSON form of a paint (null, color string or gradient object).
Object? paintToJson(QRKudePaint? paint) => switch (paint) {
      null => null,
      QRKudeSolid(:final color) => QRKudeColor.toHex(color),
      QRKudeLinearGradient(:final angle, :final stops) => {
          'type': 'linear',
          'angle': angle,
          'stops': [for (final s in stops) _stopToJson(s)],
        },
      QRKudeRadialGradient(:final cx, :final cy, :final r, :final stops) => {
          'type': 'radial',
          'cx': cx,
          'cy': cy,
          'r': r,
          'stops': [for (final s in stops) _stopToJson(s)],
        },
    };

Map<String, Object?> _stopToJson(QRKudeGradientStop s) => {'offset': s.offset, 'color': QRKudeColor.toHex(s.color)};

/// JSON form of a style (every key written).
Map<String, Object?> styleToJson(QRKudeStyle s) {
  final logo = s.logo, label = s.label;
  return {
    'margin': s.margin,
    'modules': {
      'shape': s.modules.shape.name,
      'size': s.modules.size,
      'roundness': s.modules.roundness,
      'color': paintToJson(s.modules.color),
    },
    'eyes': {
      'frame': s.eyes.frame.name,
      'ball': s.eyes.ball.name,
      'frameColor': paintToJson(s.eyes.frameColor),
      'ballColor': paintToJson(s.eyes.ballColor),
    },
    'alignment': {'frame': s.alignment.frame?.name, 'ball': s.alignment.ball?.name},
    'background': {'color': paintToJson(s.background.color), 'radius': s.background.radius},
    'border': {'width': s.border.width, 'color': QRKudeColor.toHex(s.border.color)},
    'logo': logo == null
        ? null
        : {
            'href': logo.href,
            'size': logo.size,
            'margin': logo.margin,
            'shape': logo.shape.name,
            'excavate': logo.excavate,
            'background': logo.background == 0 ? null : QRKudeColor.toHex(logo.background),
          },
    'label': label == null
        ? null
        : {
            'text': label.text,
            'font': label.font,
            'size': label.size,
            'color': QRKudeColor.toHex(label.color),
            'position': label.position.name,
            'bold': label.bold,
          },
  };
}

// ---- generic JSON tree ------------------------------------------------------

/// Maximum nesting of objects/arrays; the top-level object is level 1 (SPEC 2.0).
const int _maxDepth = 64;

/// Longest accepted number token, in characters (SPEC 2.0).
const int _maxNumberLength = 400;

/// A JSON object that keeps every member in document order, duplicates
/// included (the C reader is a streaming parser).
class _Obj {
  _Obj(this.entries);

  final List<MapEntry<String, Object?>> entries;
}

/// Converts decoded Dart JSON (`Map`/`List`/scalars) into the reader's tree,
/// with the same limits as the text parser (depth, finite numbers). The
/// depth limit also stops cyclic structures.
Object? _fromDart(Object? v, [int depth = 0]) {
  if (v == null || v is bool) return v;
  if (v is String) return normalizeString(v);
  if (v is num) {
    final d = v.toDouble();
    if (!d.isFinite) throw FormatException('non-finite number', v);
    return d;
  }
  if (v is Map || v is List) {
    if (depth + 1 > _maxDepth) throw const FormatException('nesting deeper than 64 levels');
  }
  if (v is Map) {
    return _Obj([
      for (final e in v.entries)
        if (e.key is String)
          MapEntry(normalizeString(e.key as String), _fromDart(e.value, depth + 1))
        else
          throw FormatException('object keys must be strings', e.key),
    ]);
  }
  if (v is List) return [for (final e in v) _fromDart(e, depth + 1)];
  throw FormatException('unsupported JSON value', v);
}

/// A small JSON parser that keeps members in order. Like the C reader it
/// ignores anything after the root object.
class _JsonParser {
  _JsonParser(this.s);

  final String s;
  int pos = 0;
  int _depth = 0;

  Never _fail(String message) => throw FormatException(message, s, pos);

  void _enter() {
    if (++_depth > _maxDepth) _fail('nesting deeper than 64 levels');
  }

  int _peek() {
    while (pos < s.length) {
      final c = s.codeUnitAt(pos);
      if (c != 0x20 && c != 0x09 && c != 0x0A && c != 0x0D) return c;
      pos++;
    }
    return -1;
  }

  bool _eat(int c) {
    if (_peek() == c) {
      pos++;
      return true;
    }
    return false;
  }

  Object? parseRoot() {
    if (_peek() != 0x7B) _fail('expected an object');
    return _value();
  }

  bool _literal(String lit) {
    if (s.startsWith(lit, pos)) {
      pos += lit.length;
      return true;
    }
    return false;
  }

  Object? _value() {
    final c = _peek();
    if (c == 0x7B) return _object();
    if (c == 0x5B) return _array();
    if (c == 0x22) return _string();
    if (_literal('true')) return true;
    if (_literal('false')) return false;
    if (_literal('null')) return null;
    return _number();
  }

  _Obj _object() {
    pos++; // {
    _enter();
    final entries = <MapEntry<String, Object?>>[];
    if (!_eat(0x7D)) {
      do {
        if (_peek() != 0x22) _fail('expected a key');
        final key = _string();
        if (!_eat(0x3A)) _fail('expected ":"');
        entries.add(MapEntry(key, _value()));
      } while (_eat(0x2C));
      if (!_eat(0x7D)) _fail('expected "}"');
    }
    _depth--;
    return _Obj(entries);
  }

  List<Object?> _array() {
    pos++; // [
    _enter();
    final items = <Object?>[];
    if (!_eat(0x5D)) {
      do {
        items.add(_value());
      } while (_eat(0x2C));
      if (!_eat(0x5D)) _fail('expected "]"');
    }
    _depth--;
    return items;
  }

  /// Reads 4 hex digits; -1 when they are missing or invalid.
  int _tryHex4() {
    if (pos + 4 > s.length) return -1;
    var v = 0;
    for (var i = 0; i < 4; i++) {
      final c = s.codeUnitAt(pos++);
      int h;
      if (c >= 0x30 && c <= 0x39) {
        h = c - 0x30;
      } else if (c >= 0x61 && c <= 0x66) {
        h = c - 0x61 + 10;
      } else if (c >= 0x41 && c <= 0x46) {
        h = c - 0x41 + 10;
      } else {
        return -1;
      }
      v = v * 16 + h;
    }
    return v;
  }

  /// The code point of a `\\uXXXX` escape (after the `u`). A high surrogate
  /// pairs only with a directly following `\\u` low surrogate; otherwise the
  /// following escape is processed on its own. Unpaired surrogates become
  /// U+FFFD (SPEC 2.0).
  int _unicodeEscape() {
    var cp = _tryHex4();
    if (cp < 0) _fail('bad \\u escape');
    if (cp >= 0xD800 && cp < 0xDC00) {
      var lo = 0;
      final save = pos;
      if (pos + 6 <= s.length && s.codeUnitAt(pos) == 0x5C && s.codeUnitAt(pos + 1) == 0x75) {
        pos += 2;
        lo = _tryHex4();
        if (lo < 0xDC00 || lo > 0xDFFF) {
          pos = save;
          lo = 0;
        }
      }
      cp = lo != 0 ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
      cp = 0xFFFD; // lone low surrogate
    }
    return cp;
  }

  String _string() {
    pos++; // opening quote
    final out = StringBuffer();
    while (pos < s.length) {
      final c = s.codeUnitAt(pos++);
      // a string ends at its first U+0000; raw unpaired surrogates -> U+FFFD
      if (c == 0x22) return normalizeString(out.toString());
      if (c != 0x5C) {
        out.writeCharCode(c);
        continue;
      }
      if (pos >= s.length) break;
      final e = s.codeUnitAt(pos++);
      switch (e) {
        case 0x6E: // n
          out.writeCharCode(0x0A);
        case 0x74: // t
          out.writeCharCode(0x09);
        case 0x72: // r
          out.writeCharCode(0x0D);
        case 0x62: // b
          out.writeCharCode(0x08);
        case 0x66: // f
          out.writeCharCode(0x0C);
        case 0x75: // u
          out.writeCharCode(_unicodeEscape());
        default: // \" \\ \/ and anything else: the character itself
          out.writeCharCode(e);
      }
    }
    _fail('unterminated string');
  }

  /// Same grammar as the C reader: `-? digits* (. digits*)? ([eE] [+-]? digits*)?`
  /// with at least one digit. The value is the nearest double; tokens longer
  /// than 400 characters and non-finite values are parse errors. The work is
  /// linear in the token length (the exponent is capped while scanning).
  double _number() {
    _peek();
    final start = pos;
    var neg = false;
    if (pos < s.length && s.codeUnitAt(pos) == 0x2D) {
      neg = true;
      pos++;
    }
    final intStart = pos;
    while (pos < s.length && _isDigit(s.codeUnitAt(pos))) {
      pos++;
    }
    final intDigits = s.substring(intStart, pos);
    var fracDigits = '';
    if (pos < s.length && s.codeUnitAt(pos) == 0x2E) {
      pos++;
      final fracStart = pos;
      while (pos < s.length && _isDigit(s.codeUnitAt(pos))) {
        pos++;
      }
      fracDigits = s.substring(fracStart, pos);
    }
    if (intDigits.isEmpty && fracDigits.isEmpty) {
      pos = start;
      _fail('invalid value');
    }
    var exp = 0;
    if (pos < s.length && (s.codeUnitAt(pos) == 0x65 || s.codeUnitAt(pos) == 0x45)) {
      pos++;
      var eneg = false;
      if (pos < s.length && (s.codeUnitAt(pos) == 0x2B || s.codeUnitAt(pos) == 0x2D)) {
        eneg = s.codeUnitAt(pos++) == 0x2D;
      }
      while (pos < s.length && _isDigit(s.codeUnitAt(pos))) {
        if (exp < 100000) exp = exp * 10 + (s.codeUnitAt(pos) - 0x30);
        pos++;
      }
      if (eneg) exp = -exp;
    }
    if (pos - start > _maxNumberLength) _fail('number token longer than 400 characters');
    final literal = '${intDigits.isEmpty ? '0' : intDigits}.${fracDigits.isEmpty ? '0' : fracDigits}e$exp';
    final v = double.parse(literal);
    // JSON has no infinities (1e999), and they would poison every size
    if (!v.isFinite) _fail('non-finite number');
    return neg ? -v : v;
  }

  static bool _isDigit(int c) => c >= 0x30 && c <= 0x39;
}

// ---- style reader -----------------------------------------------------------

class _Reader {
  _Reader(QRKudeStyle base) {
    margin = base.margin;
    shape = base.modules.shape;
    size = base.modules.size;
    roundness = base.modules.roundness;
    modulesPaint = base.modules.color;
    eyeFrame = base.eyes.frame;
    eyeBall = base.eyes.ball;
    framePaint = base.eyes.frameColor;
    ballPaint = base.eyes.ballColor;
    alignFrame = base.alignment.frame;
    alignBall = base.alignment.ball;
    bgPaint = base.background.color;
    bgRadius = base.background.radius;
    borderWidth = base.border.width;
    borderColor = base.border.color;
    final logo = base.logo ?? const QRKudeLogo(size: 0);
    logoSize = base.logo == null ? 0 : logo.size;
    logoMargin = logo.margin;
    logoShape = logo.shape;
    excavate = logo.excavate;
    logoBackground = logo.background;
    href = logo.href;
    final label = base.label;
    labelText = label?.text;
    labelFont = label?.font ?? 'sans-serif';
    labelSize = label?.size ?? 4;
    labelColor = label?.color ?? QRKudeColor.black;
    labelPosition = label?.position ?? LabelPosition.bottom;
    bold = label?.bold ?? true;
  }

  EncodeOptions encode = const EncodeOptions();
  SvgOptions svg = const SvgOptions();

  late double margin;
  late ModuleShape shape;
  late double size, roundness;
  late QRKudePaint? modulesPaint;
  late EyeShape eyeFrame, eyeBall;
  late QRKudePaint? framePaint, ballPaint;
  late EyeShape? alignFrame, alignBall;
  late QRKudePaint? bgPaint;
  late double bgRadius;
  late double borderWidth;
  late int borderColor;
  late double logoSize, logoMargin;
  late LogoShape logoShape;
  late bool excavate;
  late int logoBackground;
  late String? href;
  late String? labelText;
  late String labelFont;
  late double labelSize;
  late int labelColor;
  late LabelPosition labelPosition;
  late bool bold;

  QRKudeStyle build() => QRKudeStyle(
        margin: margin,
        modules: QRKudeModules(shape: shape, size: size, roundness: roundness, color: modulesPaint),
        eyes: QRKudeEyes(frame: eyeFrame, ball: eyeBall, frameColor: framePaint, ballColor: ballPaint),
        alignment: QRKudeAlignment(frame: alignFrame, ball: alignBall),
        background: QRKudeBackground(color: bgPaint, radius: bgRadius),
        border: QRKudeBorder(width: borderWidth, color: borderColor),
        logo: logoSize > 0
            ? QRKudeLogo(
                href: href,
                size: logoSize,
                margin: logoMargin,
                shape: logoShape,
                excavate: excavate,
                background: logoBackground,
              )
            : null,
        label: labelText != null && labelText!.isNotEmpty
            ? QRKudeLabel(
                text: labelText!,
                font: labelFont,
                size: labelSize,
                color: labelColor,
                position: labelPosition,
                bold: bold,
              )
            : null,
      );

  void document(_Obj root) {
    for (final e in root.entries) {
      switch (e.key) {
        case 'encode':
          _encode(_object(e.value, 'encode'));
        case 'svg':
          _svg(_object(e.value, 'svg'));
        case 'style':
          for (final s in _object(e.value, 'style').entries) {
            styleKey(s.key, s.value);
          }
        default:
          styleKey(e.key, e.value);
      }
    }
  }

  void styleKey(String k, Object? v) {
    switch (k) {
      case 'margin':
        margin = _number(v, k);
      case 'modules':
        for (final e in _object(v, k).entries) {
          final value = e.value;
          switch (e.key) {
            case 'shape':
              shape = ModuleShape.tryParse(_string(value, 'modules.shape')) ?? _bad('modules.shape', value);
            case 'size':
              size = _number(value, 'modules.size');
            case 'roundness':
              roundness = _number(value, 'modules.roundness');
            case 'color':
              modulesPaint = _paint(value, 'modules.color');
          }
        }
      case 'eyes':
        for (final e in _object(v, k).entries) {
          final value = e.value;
          switch (e.key) {
            case 'frame':
              eyeFrame = _eyeShape(value, 'eyes.frame', allowNull: false)!;
            case 'ball':
              eyeBall = _eyeShape(value, 'eyes.ball', allowNull: false)!;
            case 'frameColor':
              framePaint = _paint(value, 'eyes.frameColor');
            case 'ballColor':
              ballPaint = _paint(value, 'eyes.ballColor');
          }
        }
      case 'alignment':
        if (v == null) {
          // alignment: null turns styled alignment patterns off (SPEC 2.0)
          alignFrame = null;
          alignBall = null;
          return;
        }
        for (final e in _object(v, k).entries) {
          switch (e.key) {
            case 'frame':
              alignFrame = _eyeShape(e.value, 'alignment.frame', allowNull: true);
            case 'ball':
              alignBall = _eyeShape(e.value, 'alignment.ball', allowNull: true);
          }
        }
      case 'background':
        for (final e in _object(v, k).entries) {
          switch (e.key) {
            case 'color':
              bgPaint = _paint(e.value, 'background.color');
            case 'radius':
              bgRadius = _number(e.value, 'background.radius');
          }
        }
      case 'border':
        for (final e in _object(v, k).entries) {
          switch (e.key) {
            case 'width':
              borderWidth = _number(e.value, 'border.width');
            case 'color':
              borderColor = _color(e.value, 'border.color');
          }
        }
      case 'logo':
        if (v == null) {
          // null resets the feature to its defaults (absent), forgetting
          // every member set before (SPEC 2.0)
          const d = QRKudeLogo();
          logoSize = 0;
          logoMargin = d.margin;
          logoShape = d.shape;
          excavate = d.excavate;
          logoBackground = d.background;
          href = d.href;
          return;
        }
        final obj = _object(v, k);
        // the default size applies only when no logo is present yet, so a
        // repeated logo object merges into the previous one (SPEC 2.0)
        if (!(logoSize > 0)) logoSize = 0.2;
        for (final e in obj.entries) {
          final value = e.value;
          switch (e.key) {
            case 'href':
              href = _stringOrNull(value, 'logo.href');
            case 'size':
              logoSize = _number(value, 'logo.size');
            case 'margin':
              logoMargin = _number(value, 'logo.margin');
            case 'excavate':
              excavate = _bool(value, 'logo.excavate');
            case 'background':
              logoBackground = _color(value, 'logo.background');
            case 'shape':
              logoShape = LogoShape.tryParse(_string(value, 'logo.shape')) ?? _bad('logo.shape', value);
          }
        }
      case 'label':
        if (v == null) {
          const d = QRKudeLabel(text: '');
          labelText = null;
          labelFont = d.font;
          labelSize = d.size;
          labelColor = d.color;
          labelPosition = d.position;
          bold = d.bold;
          return;
        }
        for (final e in _object(v, k).entries) {
          final value = e.value;
          switch (e.key) {
            case 'text':
              labelText = _stringOrNull(value, 'label.text');
            case 'font':
              labelFont = _stringOrNull(value, 'label.font') ?? '';
            case 'size':
              labelSize = _number(value, 'label.size');
            case 'color':
              labelColor = _color(value, 'label.color');
            case 'bold':
              bold = _bool(value, 'label.bold');
            case 'position':
              labelPosition = LabelPosition.tryParse(_string(value, 'label.position')) ?? _bad('label.position', value);
          }
        }
    }
  }

  void _encode(_Obj obj) {
    var o = encode;
    for (final e in obj.entries) {
      final value = e.value;
      switch (e.key) {
        case 'ecc':
          o = o.copyWith(ecc: Ecc.tryParse(_string(value, 'encode.ecc')) ?? _bad('encode.ecc', value));
        case 'minVersion':
          o = o.copyWith(minVersion: _int(value, 'encode.minVersion'));
        case 'maxVersion':
          o = o.copyWith(maxVersion: _int(value, 'encode.maxVersion'));
        case 'mask':
          o = o.copyWith(mask: _int(value, 'encode.mask'));
        case 'boostEcc':
          o = o.copyWith(boostEcc: _bool(value, 'encode.boostEcc'));
        case 'eci':
          o = o.copyWith(eci: _bool(value, 'encode.eci'));
      }
    }
    encode = o;
  }

  void _svg(_Obj obj) {
    var o = svg;
    for (final e in obj.entries) {
      switch (e.key) {
        case 'width':
          o = SvgOptions(width: _number(e.value, 'svg.width'), idPrefix: o.idPrefix);
        case 'idPrefix':
          o = SvgOptions(width: o.width, idPrefix: _stringOrNull(e.value, 'svg.idPrefix') ?? 'qrk');
      }
    }
    svg = o;
  }

  // ---- value helpers --------------------------------------------------------

  static Never _bad(String key, Object? value) => throw FormatException('invalid value for "$key"', value);

  static _Obj _object(Object? v, String key) => v is _Obj ? v : _bad(key, v);

  static double _number(Object? v, String key) => v is double ? v : _bad(key, v);

  /// Integer fields clamp to [-1000, 1000] and then truncate toward zero
  /// (SPEC 2.0); out-of-range values stay invalid for the encoder.
  static int _int(Object? v, String key) {
    final d = _number(v, key);
    return (d < -1000 ? -1000.0 : (d > 1000 ? 1000.0 : d)).truncate();
  }

  static bool _bool(Object? v, String key) => v is bool ? v : _bad(key, v);

  static String _string(Object? v, String key) => v is String ? v : _bad(key, v);

  static String? _stringOrNull(Object? v, String key) => v == null ? null : _string(v, key);

  /// A color string, or null (= transparent).
  static int _color(Object? v, String key) {
    if (v == null) return QRKudeColor.transparent;
    return QRKudeColor.tryParse(_string(v, key)) ?? _bad(key, v);
  }

  static EyeShape? _eyeShape(Object? v, String key, {required bool allowNull}) {
    if (v == null) return allowNull ? null : _bad(key, v);
    return EyeShape.tryParse(_string(v, key)) ?? _bad(key, v);
  }

  static QRKudePaint? _paint(Object? v, String key) {
    if (v == null) return null;
    if (v is String) return QRKudeSolid(_color(v, key));
    final obj = _object(v, key);
    var radial = false;
    var angle = 0.0, cx = 0.5, cy = 0.5, r = 0.75;
    var stops = <QRKudeGradientStop>[];
    for (final e in obj.entries) {
      final value = e.value;
      switch (e.key) {
        case 'type':
          final t = _string(value, '$key.type');
          if (t == 'radial') {
            radial = true;
          } else if (t == 'linear') {
            radial = false;
          } else {
            _bad('$key.type', value);
          }
        case 'angle':
          angle = _number(value, '$key.angle');
        case 'cx':
          cx = _number(value, '$key.cx');
        case 'cy':
          cy = _number(value, '$key.cy');
        case 'r':
          r = _number(value, '$key.r');
        case 'stops':
          if (value is! List) _bad('$key.stops', value);
          stops = [];
          for (final item in value) {
            var offset = 0.0;
            var color = QRKudeColor.transparent;
            for (final s in _object(item, '$key.stops').entries) {
              switch (s.key) {
                case 'offset':
                  offset = _number(s.value, '$key.stops.offset');
                case 'color':
                  color = _color(s.value, '$key.stops.color');
              }
            }
            // like the C reader: at most 8 stops are kept
            if (stops.length < 8) stops.add(QRKudeGradientStop(offset, color));
          }
      }
    }
    return radial
        ? QRKudeRadialGradient(cx: cx, cy: cy, r: r, stops: List.unmodifiable(stops))
        : QRKudeLinearGradient(angle: angle, stops: List.unmodifiable(stops));
  }
}
