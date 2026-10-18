// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// SVG writer. The output is byte-identical across all QRKude ports
/// (port of `core/src/qrk_svg.c`).
library;

import 'dart:convert' show utf8;

import 'color.dart';
import 'encoder.dart';
import 'engine.dart';
import 'layout.dart';
import 'style.dart';
import 'text.dart';

/// Options of the SVG writer (the `svg` object of the JSON format).
class SvgOptions {
  /// Creates SVG options; the defaults write a size-less SVG that scales to
  /// its container.
  const SvgOptions({this.width = 0, this.idPrefix = 'qrk'});

  /// Value of the `width` attribute in px (`height` follows the aspect
  /// ratio); 0 omits both so the SVG scales to its container.
  final double width;

  /// Prefix of the gradient ids (empty = `qrk`). It is sanitized before it
  /// is written (see [sanitizeIdPrefix]). Gradient ids are global in an HTML
  /// page: give each inlined SVG a distinct prefix.
  final String idPrefix;

  /// The JSON form (`{"width": .., "idPrefix": ..}`).
  Map<String, Object?> toJson() => {'width': width, 'idPrefix': idPrefix};

  @override
  bool operator ==(Object other) => other is SvgOptions && other.width == width && other.idPrefix == idPrefix;

  @override
  int get hashCode => Object.hash(width, idPrefix);
}

/// Formats a number like every QRKude port (SPEC 8.1): rounded half up to
/// 3 decimals, no trailing zeros, never `-0`. NaN and `|v| >= 1e15` give `0`.
String formatNumber(double value) => _formatFixed(value, 1000, 3);

/// `fmt` with [digits] decimals; [scale] is `10^digits`.
String _formatFixed(double value, int scale, int digits) {
  if (!(value > -1e15 && value < 1e15)) value = 0; // NaN, infinities, absurd sizes
  final f = (value * scale + 0.5).floorToDouble();
  final neg = f < 0;
  final abs = f.abs();
  String ip;
  int fp;
  if (abs < 9007199254740992.0) {
    // exact on the VM and in JavaScript (below 2^53)
    final n = abs.toInt();
    fp = n % scale;
    ip = ((n - fp) ~/ scale).toString();
  } else {
    // like C's 64-bit integer arithmetic, also exact in JavaScript
    final n = BigInt.from(abs), b = BigInt.from(scale);
    fp = (n % b).toInt();
    ip = (n ~/ b).toString();
  }
  final sb = StringBuffer();
  if (neg) sb.write('-');
  sb.write(ip);
  if (fp != 0) {
    var frac = fp.toString().padLeft(digits, '0');
    var end = frac.length;
    while (frac.codeUnitAt(end - 1) == 0x30) {
      end--;
    }
    frac = frac.substring(0, end);
    sb
      ..write('.')
      ..write(frac);
  }
  return sb.toString();
}

/// `f6` of SPEC 12 (geo payloads): like [formatNumber] with 6 decimals.
String formatNumber6(double value) => _formatFixed(value, 1000000, 6);

/// The gradient id prefix actually written (SPEC 8.3): every byte of the
/// UTF-8 encoding outside `[A-Za-z0-9_-]` becomes `_`, a leading digit or
/// `-` gets a `q` in front, and an empty prefix is `qrk`. The result is safe
/// inside `id="..."` and `url(#...)`, whatever the (untrusted) input.
String sanitizeIdPrefix(String? prefix) {
  prefix = prefix == null ? '' : normalizeString(prefix);
  if (prefix.isEmpty) return 'qrk';
  final bytes = utf8.encode(prefix);
  final sb = StringBuffer();
  final first = bytes.first;
  if ((first >= 0x30 && first <= 0x39) || first == 0x2D) sb.write('q');
  for (final b in bytes) {
    final ok =
        (b >= 0x61 && b <= 0x7A) || (b >= 0x41 && b <= 0x5A) || (b >= 0x30 && b <= 0x39) || b == 0x5F || b == 0x2D;
    sb.writeCharCode(ok ? b : 0x5F);
  }
  return sb.toString();
}

/// XML escaping of text and attributes (SPEC 8.3), applied to [sanitizeText].
String _escape(String s) {
  s = sanitizeText(s);
  if (!s.contains(RegExp('[&<>"]'))) return s;
  final sb = StringBuffer();
  for (var i = 0; i < s.length; i++) {
    final c = s.codeUnitAt(i);
    switch (c) {
      case 0x26:
        sb.write('&amp;');
      case 0x3C:
        sb.write('&lt;');
      case 0x3E:
        sb.write('&gt;');
      case 0x22:
        sb.write('&quot;');
      default:
        sb.writeCharCode(c);
    }
  }
  return sb.toString();
}

/// A [QRKudePathSink] that writes SVG path data (the `d` attribute of a
/// `<path>`, SPEC 8.2). [toString] returns what was written.
class SvgPathSink implements QRKudePathSink {
  /// Creates a sink that appends to [out] (a new buffer by default).
  SvgPathSink([StringBuffer? out]) : out = out ?? StringBuffer();

  /// The path data written so far.
  final StringBuffer out;
  double _lx = 0, _ly = 0;

  @override
  void moveTo(double x, double y) {
    out
      ..write('M')
      ..write(formatNumber(x))
      ..write(' ')
      ..write(formatNumber(y));
    _lx = x;
    _ly = y;
  }

  @override
  void lineTo(double x, double y) {
    if (y == _ly) {
      out
        ..write('H')
        ..write(formatNumber(x));
    } else if (x == _lx) {
      out
        ..write('V')
        ..write(formatNumber(y));
    } else {
      out
        ..write('L')
        ..write(formatNumber(x))
        ..write(' ')
        ..write(formatNumber(y));
    }
    _lx = x;
    _ly = y;
  }

  @override
  void arcTo(double r, bool clockwise, double x, double y, double cx, double cy) {
    final rs = formatNumber(r);
    out
      ..write('A')
      ..write(rs)
      ..write(' ')
      ..write(rs)
      ..write(clockwise ? ' 0 0 1 ' : ' 0 0 0 ')
      ..write(formatNumber(x))
      ..write(' ')
      ..write(formatNumber(y));
    _lx = x;
    _ly = y;
  }

  @override
  void close() => out.write('Z');

  @override
  String toString() => out.toString();
}

/// SVG path data (`d` attribute) of one layer; '' when the layer is empty.
String layerPathData(QRCode code, QRKudeStyle style, QRKudeLayer layer) {
  final sink = SvgPathSink();
  emitLayer(code, style, layer, sink);
  return sink.toString();
}

void _opacity(StringBuffer c, String attr, int color) {
  final a = QRKudeColor.alpha(color);
  if (a != 255) {
    c
      ..write(' ')
      ..write(attr)
      ..write('="')
      ..write(formatNumber(a / 255.0))
      ..write('"');
  }
}

void _fill(StringBuffer c, QRKudePaint? p, String prefix, String id) {
  switch (p) {
    case QRKudeGradient():
      c.write(' fill="url(#$prefix-$id)"');
    case QRKudeSolid(:final color):
      c
        ..write(' fill="')
        ..write(QRKudeColor.toRgbHex(color))
        ..write('"');
      _opacity(c, 'fill-opacity', color);
    case null:
      c.write(' fill="none"');
  }
}

void _gradient(StringBuffer c, LayoutContext L, QRKudeLayer layer, QRKudeGradient p, String prefix, String id) {
  final g = gradientGeometryFor(L, layer, p);
  final linear = p is QRKudeLinearGradient;
  c
    ..write(linear ? '<linearGradient id="' : '<radialGradient id="')
    ..write(prefix)
    ..write('-')
    ..write(id)
    ..write('" gradientUnits="userSpaceOnUse"');
  if (linear) {
    c.write(
        ' x1="${formatNumber(g.x1)}" y1="${formatNumber(g.y1)}" x2="${formatNumber(g.x2)}" y2="${formatNumber(g.y2)}');
  } else {
    c.write(' cx="${formatNumber(g.cx)}" cy="${formatNumber(g.cy)}" r="${formatNumber(g.r)}');
  }
  c.write('">');
  for (final s in p.effectiveStops) {
    c
      ..write('<stop offset="')
      ..write(formatNumber(s.offset))
      ..write('" stop-color="')
      ..write(QRKudeColor.toRgbHex(s.color))
      ..write('"');
    _opacity(c, 'stop-opacity', s.color);
    c.write('/>');
  }
  c.write(linear ? '</linearGradient>' : '</radialGradient>');
}

void _layerPath(StringBuffer c, LayoutContext L, QRKudeLayer layer, QRKudePaint? paint, String prefix, String id) {
  final sink = SvgPathSink();
  if (L.emit(layer, sink) == 0) return;
  c
    ..write('<path d="')
    ..write(sink.out)
    ..write('"');
  _fill(c, paint, prefix, id);
  c.write('/>\n');
}

/// Writes the complete SVG document (`<svg>...</svg>\n`) of [code] drawn
/// with [style] (SPEC 8.3). The output is byte-identical in every QRKude
/// port. The SVG uses module units in its `viewBox`, so it scales to any
/// size without losing sharpness.
String toSvg(QRCode code, QRKudeStyle style, [SvgOptions options = const SvgOptions()]) {
  final prefix = sanitizeIdPrefix(options.idPrefix);
  final L = LayoutContext(code, style);
  final c = StringBuffer();

  c
    ..write('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 ')
    ..write(formatNumber(L.W))
    ..write(' ')
    ..write(formatNumber(L.H))
    ..write('"');
  if (options.width > 0) {
    c
      ..write(' width="')
      ..write(formatNumber(options.width))
      ..write('" height="')
      ..write(formatNumber(options.width * L.H / L.W))
      ..write('"');
  }
  c.write('>\n');

  final bg = style.background.color;
  final fg = style.modules.color;
  final ef = style.eyes.frameColor;
  final eb = style.eyes.ballColor;
  if (bg is QRKudeGradient || fg is QRKudeGradient || ef is QRKudeGradient || eb is QRKudeGradient) {
    c.write('<defs>');
    if (bg is QRKudeGradient) _gradient(c, L, QRKudeLayer.background, bg, prefix, 'bg');
    if (fg is QRKudeGradient) _gradient(c, L, QRKudeLayer.modules, fg, prefix, 'fg');
    if (ef is QRKudeGradient) _gradient(c, L, QRKudeLayer.eyeFrames, ef, prefix, 'ef');
    if (eb is QRKudeGradient) _gradient(c, L, QRKudeLayer.eyeBalls, eb, prefix, 'eb');
    c.write('</defs>\n');
  }

  final bgVisible = bg is QRKudeGradient || (bg is QRKudeSolid && QRKudeColor.alpha(bg.color) != 0);
  if (bgVisible) {
    c
      ..write('<rect width="')
      ..write(formatNumber(L.W))
      ..write('" height="')
      ..write(formatNumber(L.H))
      ..write('"');
    if (L.bgRadius > 0) {
      c
        ..write(' rx="')
        ..write(formatNumber(L.bgRadius))
        ..write('"');
    }
    _fill(c, bg, prefix, 'bg');
    c.write('/>\n');
  }

  _layerPath(c, L, QRKudeLayer.border, QRKudeSolid(style.border.color), prefix, '');
  _layerPath(c, L, QRKudeLayer.modules, fg, prefix, 'fg');
  _layerPath(c, L, QRKudeLayer.eyeFrames, ef ?? fg, prefix, ef != null ? 'ef' : 'fg');
  _layerPath(c, L, QRKudeLayer.eyeBalls, eb ?? fg, prefix, eb != null ? 'eb' : 'fg');
  _layerPath(c, L, QRKudeLayer.logoBackground, QRKudeSolid(style.logo?.background ?? 0), prefix, '');

  final logo = style.logo;
  final href = logo?.href;
  if (L.hasLogo && !isEmptyString(href)) {
    c
      ..write('<image x="')
      ..write(formatNumber(L.logoCx - L.logoHalf))
      ..write('" y="')
      ..write(formatNumber(L.logoCy - L.logoHalf))
      ..write('" width="')
      ..write(formatNumber(2 * L.logoHalf))
      ..write('" height="')
      ..write(formatNumber(2 * L.logoHalf))
      ..write('" preserveAspectRatio="xMidYMid meet" xlink:href="')
      ..write(_escape(href!))
      ..write('"/>\n');
  }

  final label = style.label;
  if (L.hasLabel && label != null) {
    c
      ..write('<text x="')
      ..write(formatNumber(L.labelX))
      ..write('" y="')
      ..write(formatNumber(L.labelBaseline))
      ..write('" text-anchor="middle" font-family="')
      ..write(_escape(isEmptyString(label.font) ? 'sans-serif' : label.font))
      ..write('" font-size="')
      ..write(formatNumber(L.labelSize))
      ..write('"');
    if (label.bold) c.write(' font-weight="bold"');
    c
      ..write(' fill="')
      ..write(QRKudeColor.toRgbHex(label.color))
      ..write('"');
    _opacity(c, 'fill-opacity', label.color);
    c
      ..write('>')
      ..write(_escape(label.text))
      ..write('</text>\n');
  }

  c.write('</svg>\n');
  return c.toString();
}
