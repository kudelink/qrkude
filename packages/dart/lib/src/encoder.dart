// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// QR Code encoder (ISO/IEC 18004, model 2, versions 1-40).
///
/// A line-by-line port of `core/src/qrk_encode.c`. Every intermediate value
/// stays well below 2^31, so the results are identical on the Dart VM and
/// when compiled to JavaScript.
library;

import 'dart:convert' show utf8;
import 'dart:typed_data';

/// Error correction level, from lowest to highest.
///
/// Error correction adds redundant data so that a scanner can still read
/// the code when part of it is damaged, dirty or covered (for example by a
/// logo). More correction means more modules (a bigger, denser symbol) for
/// the same text. See `docs/GUIDE.md#error-correction`.
enum Ecc {
  /// Level L: about 7% of the symbol can be restored.
  low('L'),

  /// Level M: about 15% can be restored (the default without a logo).
  medium('M'),

  /// Level Q: about 25% can be restored.
  quartile('Q'),

  /// Level H: about 30% can be restored (the default with a logo).
  high('H');

  const Ecc(this.letter);

  /// `"L"`, `"M"`, `"Q"` or `"H"`.
  final String letter;

  /// Parses `"L"`, `"M"`, `"Q"` or `"H"`; returns null otherwise.
  static Ecc? tryParse(String letter) {
    for (final e in Ecc.values) {
      if (e.letter == letter) return e;
    }
    return null;
  }
}

/// How the payload is stored in the symbol (chosen automatically, SPEC 1.1).
///
/// Digits-only text packs best, then upper-case text, then anything else.
enum EncodeMode {
  /// Only the digits `0`-`9` (about 3.3 bits per character).
  numeric(1),

  /// Only `0-9 A-Z`, space and `$ % * + - . / :` (5.5 bits per character).
  alphanumeric(2),

  /// Any bytes; text is stored as UTF-8 (8 bits per byte).
  byte(4);

  const EncodeMode(this.bits);

  /// Mode indicator written into the symbol.
  final int bits;
}

/// Base class of the errors thrown by QRKude itself.
///
/// Invalid JSON styles throw a [FormatException] and invalid options an
/// [ArgumentError] instead (see the README section "Untrusted styles").
class QRKudeException implements Exception {
  /// Creates an exception with a short, human-readable [message].
  const QRKudeException(this.message);

  /// What went wrong, e.g. `data too long`.
  final String message;

  @override
  String toString() => 'QRKudeException: $message';
}

/// The payload does not fit in the allowed versions at the requested ECC.
///
/// Shorten the text, lower the ECC level or raise
/// [EncodeOptions.maxVersion]. The largest symbol (version 40) holds 2953
/// bytes at level L and 1273 bytes at level H.
class DataTooLongException extends QRKudeException {
  /// Creates the exception (its message is `data too long`).
  const DataTooLongException() : super('data too long');
}

/// Encoder options (the `encode` object of the JSON format).
///
/// The defaults suit almost every use: the smallest version that fits, an
/// automatic mask and ECC raised for free when there is room.
class EncodeOptions {
  /// Creates encoder options; every parameter is optional.
  const EncodeOptions({
    this.ecc,
    this.minVersion = 1,
    this.maxVersion = 40,
    this.mask = -1,
    this.boostEcc = true,
    this.eci = false,
  });

  /// Minimum error correction. `null` means "not specified": [encode] uses
  /// [Ecc.medium], the style-aware helpers use `H` when the style has a logo.
  final Ecc? ecc;

  /// Smallest version (symbol size) to try, 1..40. Version `v` has
  /// `4v + 17` modules per side. Raise it to get the same size for
  /// different payloads.
  final int minVersion;

  /// Largest version to try, 1..40.
  final int maxVersion;

  /// Mask pattern: -1 picks the best one automatically, 0..7 forces one.
  /// Masks flip modules in a fixed pattern to avoid large uniform areas.
  final int mask;

  /// Raise the ECC level while the chosen version still fits (free extra
  /// robustness, the symbol does not grow).
  final bool boostEcc;

  /// Prepend an ECI header that declares UTF-8. Most scanners assume UTF-8
  /// anyway; enable it only if a reader needs it.
  final bool eci;

  /// A copy with the given fields replaced; pass `ecc: null` to clear it.
  EncodeOptions copyWith({
    Object? ecc = _unset,
    int? minVersion,
    int? maxVersion,
    int? mask,
    bool? boostEcc,
    bool? eci,
  }) =>
      EncodeOptions(
        ecc: identical(ecc, _unset) ? this.ecc : ecc as Ecc?,
        minVersion: minVersion ?? this.minVersion,
        maxVersion: maxVersion ?? this.maxVersion,
        mask: mask ?? this.mask,
        boostEcc: boostEcc ?? this.boostEcc,
        eci: eci ?? this.eci,
      );

  /// The JSON form (`ecc` omitted when null).
  Map<String, Object?> toJson() => {
        if (ecc != null) 'ecc': ecc!.letter,
        'minVersion': minVersion,
        'maxVersion': maxVersion,
        'mask': mask,
        'boostEcc': boostEcc,
        'eci': eci,
      };

  @override
  bool operator ==(Object other) =>
      other is EncodeOptions &&
      other.ecc == ecc &&
      other.minVersion == minVersion &&
      other.maxVersion == maxVersion &&
      other.mask == mask &&
      other.boostEcc == boostEcc &&
      other.eci == eci;

  @override
  int get hashCode => Object.hash(ecc, minVersion, maxVersion, mask, boostEcc, eci);

  @override
  String toString() => 'EncodeOptions(${toJson()})';
}

const Object _unset = Object();

/// An encoded QR Code symbol: an immutable square grid of modules.
///
/// A *module* is one cell of the grid, dark or light. Create codes with
/// [encode], [encodeBytes] or `encodeForStyle`; draw them with `toSvg` or a
/// renderer of your own (see `emitLayer`). Two codes are equal when their
/// version, ECC, mask, mode and modules are equal.
class QRCode {
  QRCode._(this.version, this.ecc, this.mask, this.mode, this._modules) : size = version * 4 + 17;

  /// Symbol version, 1..40 (bigger versions hold more data).
  final int version;

  /// Modules per side (`4 * version + 17`).
  final int size;

  /// The error correction level actually used (after boosting).
  final Ecc ecc;

  /// The mask pattern actually used (0..7).
  final int mask;

  /// The segment mode of the payload.
  final EncodeMode mode;

  final Uint8List _modules;
  int? _hash;

  /// Whether module ([x], [y]) is dark; (0, 0) is the top-left module.
  /// Out-of-range coordinates are light.
  bool isDark(int x, int y) {
    if (x < 0 || y < 0 || x >= size || y >= size) return false;
    return _modules[y * size + x] != 0;
  }

  /// The grid as rows of `'0'`/`'1'` characters, `'1'` = dark (the format
  /// of `spec/vectors/encode.json`).
  List<String> toRows() => [
        for (var y = 0; y < size; y++)
          String.fromCharCodes([
            for (var x = 0; x < size; x++) _modules[y * size + x] != 0 ? 0x31 : 0x30,
          ]),
      ];

  @override
  bool operator ==(Object other) {
    if (identical(this, other)) return true;
    if (other is! QRCode ||
        other.version != version ||
        other.ecc != ecc ||
        other.mask != mask ||
        other.mode != mode ||
        other.hashCode != hashCode) {
      return false;
    }
    for (var i = 0; i < _modules.length; i++) {
      if (_modules[i] != other._modules[i]) return false;
    }
    return true;
  }

  @override
  int get hashCode => _hash ??= Object.hash(version, ecc, mask, mode, Object.hashAll(_modules));

  @override
  String toString() => 'QRCode(version: $version, ecc: ${ecc.letter}, mask: $mask, mode: ${mode.name})';
}

// ---- tables -----------------------------------------------------------------

// Index: [ecc][version], ecc in L, M, Q, H order.
const List<List<int>> _eccCodewordsPerBlock = [
  [
    -1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, //
    28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30
  ],
  [
    -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, //
    26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28
  ],
  [
    -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, //
    28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30
  ],
  [
    -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, //
    30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30
  ],
];

const List<List<int>> _numEccBlocks = [
  [
    -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, //
    8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25
  ],
  [
    -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, //
    17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49
  ],
  [
    -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, //
    23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68
  ],
  [
    -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, //
    25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81
  ],
];

// Format information ECC bits for L, M, Q, H.
const List<int> _eccFormatBits = [1, 0, 3, 2];

const String _alphanumericCharset = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ \$%*+-./:';

/// Advanced (ISO/IEC 18004 tables): error correction codewords per
/// Reed-Solomon block for ([ecc], [version]).
int eccCodewordsPerBlock(Ecc ecc, int version) => _eccCodewordsPerBlock[ecc.index][version];

/// Advanced: number of Reed-Solomon blocks for ([ecc], [version]).
int numEccBlocks(Ecc ecc, int version) => _numEccBlocks[ecc.index][version];

int _rawDataModules(int ver) {
  var result = (16 * ver + 128) * ver + 64;
  if (ver >= 2) {
    final numAlign = ver ~/ 7 + 2;
    result -= (25 * numAlign - 10) * numAlign - 55;
    if (ver >= 7) result -= 36;
  }
  return result;
}

/// Advanced: total codewords (8-bit units, data + error correction) of a
/// [version].
int rawCodewords(int version) => _rawDataModules(version) ~/ 8;

int _dataCodewords(int ver, int ecc) => rawCodewords(ver) - _eccCodewordsPerBlock[ecc][ver] * _numEccBlocks[ecc][ver];

/// Advanced: alignment pattern center coordinates (used on both axes) of a
/// version; empty for version 1. Alignment patterns are the small 5x5
/// squares that help scanners correct perspective.
List<int> alignmentPositions(int ver) {
  if (ver == 1) return const [];
  final numAlign = ver ~/ 7 + 2;
  final step = (ver * 8 + numAlign * 3 + 5) ~/ (numAlign * 4 - 4) * 2;
  final out = List<int>.filled(numAlign, 0);
  var pos = ver * 4 + 10;
  for (var i = numAlign - 1; i >= 1; i--, pos -= step) {
    out[i] = pos;
  }
  out[0] = 6;
  return out;
}

bool _isFunctionModuleSlow(int ver, int x, int y) {
  final size = ver * 4 + 17;
  // finders + separators + format information + dark module
  if (x <= 8 && y <= 8) return true;
  if (x >= size - 8 && y <= 8) return true;
  if (x <= 8 && y >= size - 8) return true;
  // timing patterns
  if (x == 6 || y == 6) return true;
  // version information
  if (ver >= 7) {
    if (x >= size - 11 && x < size - 8 && y < 6) return true;
    if (y >= size - 11 && y < size - 8 && x < 6) return true;
  }
  // alignment patterns
  final pos = alignmentPositions(ver);
  final n = pos.length;
  for (var i = 0; i < n; i++) {
    for (var j = 0; j < n; j++) {
      if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
      final dx = x - pos[j], dy = y - pos[i];
      if (dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2) return true;
    }
  }
  return false;
}

final List<Uint8List?> _functionMaps = List<Uint8List?>.filled(41, null);

/// One byte per module (row-major), 1 for function patterns.
Uint8List functionModuleMap(int ver) {
  final cached = _functionMaps[ver];
  if (cached != null) return cached;
  final size = ver * 4 + 17;
  final map = Uint8List(size * size);
  for (var y = 0; y < size; y++) {
    for (var x = 0; x < size; x++) {
      if (_isFunctionModuleSlow(ver, x, y)) map[y * size + x] = 1;
    }
  }
  return _functionMaps[ver] = map;
}

/// Advanced: whether ([x], [y]) belongs to a function pattern (finder,
/// timing, alignment, format or version information) of a symbol of
/// [version], as opposed to a data module.
bool isFunctionModule(int version, int x, int y) {
  final size = version * 4 + 17;
  return functionModuleMap(version)[y * size + x] != 0;
}

// ---- Reed-Solomon over GF(2^8 / 0x11D) --------------------------------------

int _gfMul(int x, int y) {
  var z = 0;
  for (var i = 7; i >= 0; i--) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return z & 0xFF;
}

Uint8List _rsDivisor(int degree) {
  final result = Uint8List(degree);
  result[degree - 1] = 1;
  var root = 1;
  for (var i = 0; i < degree; i++) {
    for (var j = 0; j < degree; j++) {
      result[j] = _gfMul(result[j], root);
      if (j + 1 < degree) result[j] ^= result[j + 1];
    }
    root = _gfMul(root, 0x02);
  }
  return result;
}

Uint8List _rsRemainder(Uint8List data, int start, int len, Uint8List divisor) {
  final degree = divisor.length;
  final result = Uint8List(degree);
  for (var i = 0; i < len; i++) {
    final factor = data[start + i] ^ result[0];
    for (var j = 0; j + 1 < degree; j++) {
      result[j] = result[j + 1];
    }
    result[degree - 1] = 0;
    for (var j = 0; j < degree; j++) {
      result[j] ^= _gfMul(divisor[j], factor);
    }
  }
  return result;
}

// ---- data segment -----------------------------------------------------------

int _alnumIndex(int c) => c == 0 ? -1 : _alphanumericCharset.indexOf(String.fromCharCode(c));

EncodeMode _detectMode(List<int> data) {
  var numeric = true, alnum = true;
  for (final b in data) {
    if (b < 0x30 || b > 0x39) numeric = false;
    if (_alnumIndex(b) < 0) alnum = false;
  }
  if (numeric) return EncodeMode.numeric;
  if (alnum) return EncodeMode.alphanumeric;
  return EncodeMode.byte;
}

int _charCountBits(EncodeMode mode, int ver) {
  final i = (ver + 7) ~/ 17;
  switch (mode) {
    case EncodeMode.numeric:
      return const [10, 12, 14][i];
    case EncodeMode.alphanumeric:
      return const [9, 11, 13][i];
    case EncodeMode.byte:
      return const [8, 16, 16][i];
  }
}

int _payloadBits(EncodeMode mode, int len) {
  switch (mode) {
    case EncodeMode.numeric:
      return (len ~/ 3) * 10 + const [0, 4, 7][len % 3];
    case EncodeMode.alphanumeric:
      return (len ~/ 2) * 11 + (len % 2) * 6;
    case EncodeMode.byte:
      return len * 8;
  }
}

/// Total bit length for this version, or -1 if the count field overflows.
int _totalBits(EncodeMode mode, int len, bool eci, int ver) {
  var bits = eci ? 12 : 0;
  if (len > 0) {
    final ccb = _charCountBits(mode, ver);
    if (len >= (1 << ccb)) return -1;
    bits += 4 + ccb + _payloadBits(mode, len);
  }
  return bits;
}

class _BitBuffer {
  _BitBuffer(this.buf);

  final Uint8List buf;
  int bits = 0;

  void append(int value, int n) {
    for (var i = n - 1; i >= 0; i--, bits++) {
      if (((value >> i) & 1) != 0) buf[bits >> 3] |= 0x80 >> (bits & 7);
    }
  }
}

// ---- matrix -----------------------------------------------------------------

class _Matrix {
  _Matrix(this.version)
      : size = version * 4 + 17,
        modules = Uint8List((version * 4 + 17) * (version * 4 + 17));

  final int version;
  final int size;
  final Uint8List modules;
  int ecc = 0;

  bool get(int x, int y) {
    if (x < 0 || y < 0 || x >= size || y >= size) return false;
    return modules[y * size + x] != 0;
  }

  void set(int x, int y, bool dark) => modules[y * size + x] = dark ? 1 : 0;

  void setChecked(int x, int y, bool dark) {
    if (x >= 0 && y >= 0 && x < size && y < size) set(x, y, dark);
  }

  void drawFormatBits(int mask) {
    final data = (_eccFormatBits[ecc] << 3) | mask;
    var rem = data;
    for (var i = 0; i < 10; i++) {
      rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    }
    final bits = ((data << 10) | rem) ^ 0x5412;

    for (var i = 0; i <= 5; i++) {
      set(8, i, ((bits >> i) & 1) != 0);
    }
    set(8, 7, ((bits >> 6) & 1) != 0);
    set(8, 8, ((bits >> 7) & 1) != 0);
    set(7, 8, ((bits >> 8) & 1) != 0);
    for (var i = 9; i < 15; i++) {
      set(14 - i, 8, ((bits >> i) & 1) != 0);
    }

    for (var i = 0; i < 8; i++) {
      set(size - 1 - i, 8, ((bits >> i) & 1) != 0);
    }
    for (var i = 8; i < 15; i++) {
      set(8, size - 15 + i, ((bits >> i) & 1) != 0);
    }
    set(8, size - 8, true);
  }

  void drawFinder(int cx, int cy) {
    for (var dy = -4; dy <= 4; dy++) {
      for (var dx = -4; dx <= 4; dx++) {
        final adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        final dist = adx > ady ? adx : ady;
        setChecked(cx + dx, cy + dy, dist != 2 && dist != 4);
      }
    }
  }

  void drawFunctionPatterns() {
    for (var i = 0; i < size; i++) {
      set(6, i, i % 2 == 0);
      set(i, 6, i % 2 == 0);
    }
    drawFinder(3, 3);
    drawFinder(size - 4, 3);
    drawFinder(3, size - 4);

    final pos = alignmentPositions(version);
    final n = pos.length;
    for (var i = 0; i < n; i++) {
      for (var j = 0; j < n; j++) {
        if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
        for (var dy = -2; dy <= 2; dy++) {
          for (var dx = -2; dx <= 2; dx++) {
            final adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            set(pos[j] + dx, pos[i] + dy, (adx > ady ? adx : ady) != 1);
          }
        }
      }
    }

    drawFormatBits(0); // placeholder, redrawn after masking

    if (version >= 7) {
      var rem = version;
      for (var i = 0; i < 12; i++) {
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
      }
      final bits = (version << 12) | rem;
      for (var i = 0; i < 18; i++) {
        final bit = ((bits >> i) & 1) != 0;
        final a = size - 11 + i % 3, b = i ~/ 3;
        set(a, b, bit);
        set(b, a, bit);
      }
    }
  }

  void placeCodewords(Uint8List data, Uint8List functionMap) {
    var i = 0;
    final len = data.length;
    for (var right = size - 1; right >= 1; right -= 2) {
      if (right == 6) right = 5;
      for (var vert = 0; vert < size; vert++) {
        for (var j = 0; j < 2; j++) {
          final x = right - j;
          final upward = ((right + 1) & 2) == 0;
          final y = upward ? size - 1 - vert : vert;
          if (functionMap[y * size + x] == 0 && i < len * 8) {
            set(x, y, ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0);
            i++;
          }
        }
      }
    }
  }

  void applyMask(int mask, Uint8List functionMap) {
    for (var y = 0; y < size; y++) {
      for (var x = 0; x < size; x++) {
        final index = y * size + x;
        if (functionMap[index] == 0 && _maskBit(mask, x, y)) {
          modules[index] ^= 1;
        }
      }
    }
  }

  int penaltyScore() {
    var result = 0;
    final history = List<int>.filled(7, 0);

    for (var pass = 0; pass < 2; pass++) {
      for (var a = 0; a < size; a++) {
        var color = false;
        var run = 0;
        history.fillRange(0, 7, 0);
        for (var b = 0; b < size; b++) {
          final m = pass == 0 ? get(b, a) : get(a, b);
          if (m == color) {
            run++;
            if (run == 5) {
              result += 3;
            } else if (run > 5) {
              result++;
            }
          } else {
            _runHistoryAdd(run, history, size);
            if (!color) result += _finderLikeCount(history) * 40;
            color = m;
            run = 1;
          }
        }
        result += _finderTerminate(color, run, history, size) * 40;
      }
    }

    for (var y = 0; y < size - 1; y++) {
      for (var x = 0; x < size - 1; x++) {
        final c = get(x, y);
        if (c == get(x + 1, y) && c == get(x, y + 1) && c == get(x + 1, y + 1)) result += 3;
      }
    }

    var dark = 0;
    for (var i = 0; i < modules.length; i++) {
      dark += modules[i];
    }
    final total = size * size;
    var diff = dark * 20 - total * 10;
    if (diff < 0) diff = -diff;
    final k = (diff + total - 1) ~/ total - 1;
    result += k * 10;
    return result;
  }
}

bool _maskBit(int mask, int x, int y) {
  switch (mask) {
    case 0:
      return (x + y) % 2 == 0;
    case 1:
      return y % 2 == 0;
    case 2:
      return x % 3 == 0;
    case 3:
      return (x + y) % 3 == 0;
    case 4:
      return (x ~/ 3 + y ~/ 2) % 2 == 0;
    case 5:
      return (x * y) % 2 + (x * y) % 3 == 0;
    case 6:
      return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    default:
      return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
  }
}

void _runHistoryAdd(int run, List<int> history, int size) {
  if (history[0] == 0) run += size; // light border before the first run
  for (var i = 6; i >= 1; i--) {
    history[i] = history[i - 1];
  }
  history[0] = run;
}

int _finderLikeCount(List<int> h) {
  final n = h[1];
  final core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
  return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) + (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
}

int _finderTerminate(bool color, int run, List<int> history, int size) {
  if (color) {
    _runHistoryAdd(run, history, size);
    run = 0;
  }
  run += size;
  _runHistoryAdd(run, history, size);
  return _finderLikeCount(history);
}

// ---- public API -------------------------------------------------------------

/// Encodes the UTF-8 bytes of [text]. The mode (numeric / alphanumeric /
/// byte) is chosen automatically. A null [EncodeOptions.ecc] means `M`; use
/// `encodeForStyle` to get `H` automatically when the style has a logo.
///
/// Throws [DataTooLongException] when the payload does not fit and
/// [ArgumentError] for invalid options.
QRCode encode(String text, [EncodeOptions options = const EncodeOptions()]) => encodeBytes(utf8.encode(text), options);

/// Encodes raw bytes (each element must be 0..255), for binary payloads.
/// See [encode].
QRCode encodeBytes(List<int> data, [EncodeOptions options = const EncodeOptions()]) {
  final o = options;
  if (o.minVersion < 1 || o.maxVersion > 40 || o.minVersion > o.maxVersion || o.mask < -1 || o.mask > 7) {
    throw ArgumentError('invalid encode options: $o');
  }
  for (final b in data) {
    if (b < 0 || b > 255) throw ArgumentError.value(b, 'data', 'not a byte');
  }
  final len = data.length;
  final mode = _detectMode(data);
  final minEcc = (o.ecc ?? Ecc.medium).index;

  var ver = 0;
  var used = 0;
  for (var v = o.minVersion; v <= o.maxVersion; v++) {
    final bits = _totalBits(mode, len, o.eci, v);
    if (bits >= 0 && bits <= _dataCodewords(v, minEcc) * 8) {
      ver = v;
      used = bits;
      break;
    }
  }
  if (ver == 0) throw const DataTooLongException();

  var ecc = minEcc;
  if (o.boostEcc) {
    for (var e = ecc + 1; e <= 3; e++) {
      if (used <= _dataCodewords(ver, e) * 8) ecc = e;
    }
  }

  // data codewords
  final dataLen = _dataCodewords(ver, ecc);
  final raw = rawCodewords(ver);
  final bb = _BitBuffer(Uint8List(raw));
  if (o.eci) {
    bb.append(0x7, 4);
    bb.append(26, 8);
  }
  if (len > 0) {
    bb.append(mode.bits, 4);
    bb.append(len, _charCountBits(mode, ver));
    if (mode == EncodeMode.numeric) {
      var i = 0;
      for (; i + 3 <= len; i += 3) {
        bb.append((data[i] - 0x30) * 100 + (data[i + 1] - 0x30) * 10 + (data[i + 2] - 0x30), 10);
      }
      if (len - i == 2) {
        bb.append((data[i] - 0x30) * 10 + (data[i + 1] - 0x30), 7);
      } else if (len - i == 1) {
        bb.append(data[i] - 0x30, 4);
      }
    } else if (mode == EncodeMode.alphanumeric) {
      var i = 0;
      for (; i + 2 <= len; i += 2) {
        bb.append(_alnumIndex(data[i]) * 45 + _alnumIndex(data[i + 1]), 11);
      }
      if (i < len) bb.append(_alnumIndex(data[i]), 6);
    } else {
      for (var i = 0; i < len; i++) {
        bb.append(data[i], 8);
      }
    }
  }
  final capacity = dataLen * 8;
  final term = capacity - bb.bits;
  bb.append(0, term < 4 ? term : 4);
  bb.append(0, (8 - bb.bits % 8) % 8);
  for (var pad = 0xEC; bb.bits < capacity; pad ^= 0xEC ^ 0x11) {
    bb.append(pad, 8);
  }

  // error correction + interleaving
  final numBlocks = _numEccBlocks[ecc][ver];
  final blockEccLen = _eccCodewordsPerBlock[ecc][ver];
  final numShort = numBlocks - raw % numBlocks;
  final shortLen = raw ~/ numBlocks - blockEccLen;
  final divisor = _rsDivisor(blockEccLen);
  final out = Uint8List(raw);
  final dat = bb.buf;
  var offset = 0;
  for (var i = 0; i < numBlocks; i++) {
    final datLen = shortLen + (i < numShort ? 0 : 1);
    final eccBuf = _rsRemainder(dat, offset, datLen, divisor);
    for (var j = 0, k = i; j < datLen; j++, k += numBlocks) {
      if (j == shortLen) k -= numShort;
      out[k] = dat[offset + j];
    }
    for (var j = 0, k = dataLen + i; j < blockEccLen; j++, k += numBlocks) {
      out[k] = eccBuf[j];
    }
    offset += datLen;
  }

  // matrix
  final qr = _Matrix(ver)..ecc = ecc;
  qr.drawFunctionPatterns();
  final functionMap = functionModuleMap(ver);
  qr.placeCodewords(out, functionMap);

  var mask = o.mask;
  if (mask < 0) {
    var best = -1;
    for (var m = 0; m < 8; m++) {
      qr.applyMask(m, functionMap);
      qr.drawFormatBits(m);
      final p = qr.penaltyScore();
      if (best < 0 || p < best) {
        best = p;
        mask = m;
      }
      qr.applyMask(m, functionMap);
    }
  }
  qr.applyMask(mask, functionMap);
  qr.drawFormatBits(mask);
  return QRCode._(ver, Ecc.values[ecc], mask, mode, qr.modules);
}
