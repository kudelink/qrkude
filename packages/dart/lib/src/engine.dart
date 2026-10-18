// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Internal geometry engine: clamped layout values, module classification,
/// the shape model, the path emitter and the logo damage estimate (port of
/// `core/src/qrk_layout.c`). Not exported: the public API is in
/// `layout.dart`. Every floating point expression keeps the operand order of
/// the C code so that the geometry is bit-identical.
library;

// ignore_for_file: public_member_api_docs

import 'dart:math' as math;
import 'dart:typed_data';

import 'encoder.dart';
import 'layout.dart';
import 'style.dart';
import 'text.dart';

const int _none = 0, _round = 1, _chamfer = 2;
const int _tl = 0, _tr = 1, _br = 2, _bl = 3;

/// Clamps like the C core: NaN clamps to [lo].
double _clamp(double v, double lo, double hi) {
  if (!(v >= lo)) return lo;
  return v > hi ? hi : v;
}

/// Upper bound for lengths given in modules (margin, border, logo margin,
/// label size), SPEC 2.2.
const double _maxLength = 1000;

/// Axis-aligned rectangle with per-corner rounding / chamfering
/// (corners in TL, TR, BR, BL order).
class _CRect {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  final Int32List kind = Int32List(4);
  final Float64List a = Float64List(4);

  void set(double x0, double y0, double x1, double y1, int kind, double a) {
    this.x0 = x0;
    this.y0 = y0;
    this.x1 = x1;
    this.y1 = y1;
    setCorners(kind, a);
  }

  void setCorners(int k, double amount) {
    for (var i = 0; i < 4; i++) {
      kind[i] = k;
      a[i] = amount;
    }
  }

  void setCorner(int i, int k, double amount) {
    kind[i] = k;
    a[i] = amount;
  }

  double amt(int i) => kind[i] == _none ? 0 : a[i];
}

/// Clamped style values and module classification for one (code, style).
class LayoutContext {
  LayoutContext(this.code, this.style) : n = code.size {
    margin = _clamp(style.margin, 0, _maxLength);
    border = _clamp(style.border.width, 0, _maxLength);
    shape = style.modules.shape;
    size = _clamp(style.modules.size, 0.1, 1);
    roundness = _clamp(style.modules.roundness, 0, 1);

    final frame = margin + border;
    W = n + 2 * frame;
    H = W;
    ox = frame;
    oy = frame;

    final label = style.label;
    hasLabel = label != null && !isEmptyString(label.text);
    if (hasLabel) {
      final f = _clamp(label!.size, 0.1, _maxLength);
      labelSize = f;
      bandH = f * 1.5;
      H += bandH;
      if (label.position == LabelPosition.top) {
        bandY = border;
        oy += bandH;
      } else {
        bandY = W - border;
      }
      labelX = W / 2;
      labelBaseline = bandY + f * 1.1;
    }
    final minSide = W < H ? W : H;
    bgRadius = _clamp(style.background.radius, 0, minSide / 2);

    final logo = style.logo;
    hasLogo = logo != null && logo.size > 0;
    if (hasLogo) {
      excavate = logo!.excavate;
      logoShape = logo.shape;
      logoCx = ox + n / 2.0;
      logoCy = oy + n / 2.0;
      logoHalf = n * _clamp(logo.size, 0, 1) / 2;
      logoAreaHalf = logoHalf + _clamp(logo.margin, 0, _maxLength);
    }

    alignStyled = style.alignment.frame != null;
    if (alignStyled) alignPos = alignmentPositions(code.version);

    _dataCell = Uint8List(n * n);
    _dataDark = Uint8List(n * n);
    for (var y = 0; y < n; y++) {
      for (var x = 0; x < n; x++) {
        if (!isFinder(x, y) && !inStyledAlignment(x, y) && !isExcavated(x, y)) {
          _dataCell[y * n + x] = 1;
          if (code.isDark(x, y)) _dataDark[y * n + x] = 1;
        }
      }
    }
  }

  final QRCode code;
  final QRKudeStyle style;
  final int n;

  late final double margin, border, bgRadius;
  late final ModuleShape shape;
  late final double size, roundness;

  // ignore: non_constant_identifier_names
  late final double W;
  // ignore: non_constant_identifier_names
  double H = 0;
  late final double ox;
  double oy = 0;

  late final bool hasLabel;
  double bandY = 0, bandH = 0, labelX = 0, labelBaseline = 0, labelSize = 0;

  late final bool hasLogo;
  bool excavate = false;
  LogoShape logoShape = LogoShape.square;
  double logoCx = 0, logoCy = 0, logoHalf = 0, logoAreaHalf = 0;

  late final bool alignStyled;
  List<int> alignPos = const [];

  late final Uint8List _dataCell, _dataDark;

  // ---- classification -------------------------------------------------------

  bool isFinder(int x, int y) => (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7);

  static bool _isAlignmentCenter(int k, int i, int j) =>
      !((i == 0 && j == 0) || (i == 0 && j == k - 1) || (i == k - 1 && j == 0));

  bool inStyledAlignment(int x, int y) {
    if (!alignStyled) return false;
    final k = alignPos.length;
    for (var i = 0; i < k; i++) {
      final dy = y - alignPos[i];
      if (dy < -2 || dy > 2) continue;
      for (var j = 0; j < k; j++) {
        final dx = x - alignPos[j];
        if (dx >= -2 && dx <= 2 && _isAlignmentCenter(k, i, j)) return true;
      }
    }
    return false;
  }

  /// Module overlaps the logo area (box + margin).
  bool inLogoArea(int x, int y) {
    final c = n / 2.0, h = logoAreaHalf;
    final dx = (x + 0.5 - c).abs(), dy = (y + 0.5 - c).abs();
    if (logoShape == LogoShape.circle) {
      final nx = dx - 0.5 > 0 ? dx - 0.5 : 0.0;
      final ny = dy - 0.5 > 0 ? dy - 0.5 : 0.0;
      return nx * nx + ny * ny < h * h;
    }
    return dx < h + 0.5 && dy < h + 0.5;
  }

  bool isExcavated(int x, int y) => hasLogo && excavate && inLogoArea(x, y);

  bool isDataCell(int x, int y) {
    if (x < 0 || y < 0 || x >= n || y >= n) return false;
    return _dataCell[y * n + x] != 0;
  }

  bool isDataDark(int x, int y) {
    if (x < 0 || y < 0 || x >= n || y >= n) return false;
    return _dataDark[y * n + x] != 0;
  }

  // ---- shape model ----------------------------------------------------------

  /// Cell-local shape of a dark data module; false for the star shape.
  bool _moduleCRect(int x, int y, _CRect r) {
    final s = size, d = (1 - s) / 2, rho = roundness;
    r.set(d, d, 1 - d, 1 - d, _none, 0);

    switch (shape) {
      case ModuleShape.square:
        return true;
      case ModuleShape.dot:
        r.setCorners(_round, s / 2);
        return true;
      case ModuleShape.rounded:
        r.setCorners(_round, rho * s / 2);
        return true;
      case ModuleShape.diamond:
        r.setCorners(_chamfer, s / 2);
        return true;
      case ModuleShape.star:
        return false;
      case ModuleShape.hbars:
        final l = isDataDark(x - 1, y), rt = isDataDark(x + 1, y);
        if (l) r.x0 = 0;
        if (rt) r.x1 = 1;
        final a = rho * s / 2;
        if (!l) {
          r.setCorner(_tl, _round, a);
          r.setCorner(_bl, _round, a);
        }
        if (!rt) {
          r.setCorner(_tr, _round, a);
          r.setCorner(_br, _round, a);
        }
        return true;
      case ModuleShape.vbars:
        final u = isDataDark(x, y - 1), dn = isDataDark(x, y + 1);
        if (u) r.y0 = 0;
        if (dn) r.y1 = 1;
        final a = rho * s / 2;
        if (!u) {
          r.setCorner(_tl, _round, a);
          r.setCorner(_tr, _round, a);
        }
        if (!dn) {
          r.setCorner(_bl, _round, a);
          r.setCorner(_br, _round, a);
        }
        return true;
      case ModuleShape.fluid:
      case ModuleShape.liquid:
      case ModuleShape.bevel:
      case ModuleShape.classy:
        break;
    }

    // connected shapes always fill the whole cell
    r.x0 = r.y0 = 0;
    r.x1 = r.y1 = 1;
    final up = isDataDark(x, y - 1), down = isDataDark(x, y + 1);
    final left = isDataDark(x - 1, y), right = isDataDark(x + 1, y);
    final open = [!up && !left, !up && !right, !down && !right, !down && !left];
    final kind = shape == ModuleShape.bevel ? _chamfer : _round;
    for (var i = 0; i < 4; i++) {
      var use = open[i];
      if (shape == ModuleShape.classy && (i == _tr || i == _bl)) use = false;
      if (use) r.setCorner(i, kind, rho / 2);
    }
    return true;
  }

  /// Which corners of a light data cell get a liquid fillet (bit i = corner i).
  int filletMask(int x, int y) {
    if (shape != ModuleShape.liquid || roundness <= 0) return 0;
    final up = isDataDark(x, y - 1), down = isDataDark(x, y + 1);
    final left = isDataDark(x - 1, y), right = isDataDark(x + 1, y);
    var m = 0;
    if (up && left && isDataDark(x - 1, y - 1)) m |= 1 << _tl;
    if (up && right && isDataDark(x + 1, y - 1)) m |= 1 << _tr;
    if (down && right && isDataDark(x + 1, y + 1)) m |= 1 << _br;
    if (down && left && isDataDark(x - 1, y + 1)) m |= 1 << _bl;
    return m;
  }

  // ---- emission -------------------------------------------------------------

  /// Emits one layer; returns the number of sub-paths.
  int emit(QRKudeLayer layer, QRKudePathSink? sink) {
    final e = _Emitter(sink);
    final r = _CRect();
    switch (layer) {
      case QRKudeLayer.background:
        _canvasCRect(r, 0);
        e.crect(r);
      case QRKudeLayer.border:
        if (border > 0) {
          _canvasCRect(r, 0);
          e.crect(r);
          _canvasCRect(r, border);
          e.crectCcw(r);
        }
      case QRKudeLayer.modules:
        _emitModules(e);
      case QRKudeLayer.eyeFrames:
        _emitEyes(e, true);
      case QRKudeLayer.eyeBalls:
        _emitEyes(e, false);
      case QRKudeLayer.logoBackground:
        final bg = style.logo?.background ?? 0;
        if (hasLogo && bg % 256 != 0) {
          _logoBgCRect(r);
          e.crect(r);
        }
    }
    return e.count;
  }

  void _canvasCRect(_CRect r, double inset) {
    final a = bgRadius - inset;
    r.set(inset, inset, W - inset, H - inset, a > 0 ? _round : _none, a > 0 ? a : 0);
  }

  void _logoBgCRect(_CRect r) {
    final h = logoAreaHalf;
    r.x0 = logoCx - h;
    r.y0 = logoCy - h;
    r.x1 = logoCx + h;
    r.y1 = logoCy + h;
    switch (logoShape) {
      case LogoShape.rounded:
        r.setCorners(_round, h * 0.3);
      case LogoShape.circle:
        r.setCorners(_round, h);
      case LogoShape.square:
        r.setCorners(_none, 0);
    }
  }

  void _emitModules(_Emitter e) {
    final s = size, d = (1 - s) / 2;
    final r = _CRect();

    if ((shape == ModuleShape.square && s == 1) || shape == ModuleShape.hbars) {
      final bars = shape == ModuleShape.hbars;
      final inset = bars ? d : 0.0, a = bars ? roundness * s / 2 : 0.0;
      for (var y = 0; y < n; y++) {
        for (var x = 0; x < n;) {
          if (!isDataDark(x, y)) {
            x++;
            continue;
          }
          final x0 = x;
          while (x < n && isDataDark(x, y)) {
            x++;
          }
          r.set(ox + x0 + inset, oy + y + inset, ox + x - inset, oy + y + 1 - inset, bars ? _round : _none, a);
          e.crect(r);
        }
      }
      return;
    }
    if (shape == ModuleShape.vbars) {
      final a = roundness * s / 2;
      for (var x = 0; x < n; x++) {
        for (var y = 0; y < n;) {
          if (!isDataDark(x, y)) {
            y++;
            continue;
          }
          final y0 = y;
          while (y < n && isDataDark(x, y)) {
            y++;
          }
          r.set(ox + x + d, oy + y0 + d, ox + x + 1 - d, oy + y - d, _round, a);
          e.crect(r);
        }
      }
      return;
    }

    for (var y = 0; y < n; y++) {
      for (var x = 0; x < n; x++) {
        final X = ox + x, Y = oy + y;
        if (isDataDark(x, y)) {
          if (!_moduleCRect(x, y, r)) {
            e.star(X + 0.5, Y + 0.5, s / 2);
            continue;
          }
          r.x0 += X;
          r.x1 += X;
          r.y0 += Y;
          r.y1 += Y;
          e.crect(r);
        } else if (isDataCell(x, y)) {
          final m = filletMask(x, y);
          for (var c = 0; c < 4; c++) {
            if ((m & (1 << c)) != 0) e.fillet(c, X, Y, roundness / 2);
          }
        }
      }
    }
  }

  void _emitEyes(_Emitter e, bool frames) {
    final ex = [0, n - 7, 0], ey = [0, 0, n - 7];
    const cc = [_br, _bl, _tr];
    final fs = style.eyes.frame;
    final bs = ballShape(style.eyes.ball);
    for (var i = 0; i < 3; i++) {
      final X = ox + ex[i], Y = oy + ey[i];
      if (frames) {
        _emitEyePart(e, fs, X, Y, 7, true, cc[i]);
      } else {
        _emitEyePart(e, bs, X + 2, Y + 2, 3, false, cc[i]);
      }
    }
    if (!alignStyled) return;
    final afs = style.alignment.frame!;
    final abs = ballShape(style.alignment.ball ?? EyeShape.square);
    final k = alignPos.length;
    for (var i = 0; i < k; i++) {
      for (var j = 0; j < k; j++) {
        if (!_isAlignmentCenter(k, i, j)) continue;
        final X = ox + alignPos[j], Y = oy + alignPos[i];
        if (frames) {
          _emitEyePart(e, afs, X - 2, Y - 2, 5, true, _br);
        } else {
          _emitEyePart(e, abs, X, Y, 1, false, _br);
        }
      }
    }
  }

  /// One eye-like pattern: frame (ring) or ball of size [s] at ([X], [Y]).
  // ignore: non_constant_identifier_names
  void _emitEyePart(_Emitter e, EyeShape shape, double X, double Y, double s, bool ring, int cc) {
    if (shape == EyeShape.dots) {
      final k = s.toInt();
      final r = _CRect();
      for (var j = 0; j < k; j++) {
        for (var i = 0; i < k; i++) {
          if (ring && !(i == 0 || j == 0 || i == k - 1 || j == k - 1)) continue;
          r.set(X + i, Y + j, X + i + 1, Y + j + 1, _round, 0.5);
          e.crect(r);
        }
      }
      return;
    }
    final outer = _CRect()
      ..x0 = X
      ..y0 = Y
      ..x1 = X + s
      ..y1 = Y + s;
    _eyeCorners(shape, s, cc, outer);
    e.crect(outer);
    if (ring) {
      final inner = _CRect()
        ..x0 = X + 1
        ..y0 = Y + 1
        ..x1 = X + s - 1
        ..y1 = Y + s - 1;
      _innerCorners(outer, s - 2, inner);
      e.crectCcw(inner);
    }
  }

  // ---- logo damage ----------------------------------------------------------

  double logoDamage() {
    if (!hasLogo) return 0;
    final ver = code.version, size = code.size;
    final ecc = code.ecc;
    final numBlocks = numEccBlocks(ecc, ver);
    final blockEcc = eccCodewordsPerBlock(ecc, ver);
    final raw = rawCodewords(ver);
    final dataLen = raw - numBlocks * blockEcc;
    final numShort = numBlocks - raw % numBlocks;
    final shortLen = raw ~/ numBlocks - blockEcc;
    final functionMap = functionModuleMap(ver);

    final damaged = List<int>.filled(numBlocks, 0);
    var lastCw = -1;
    var lastHit = false;

    var bit = 0;
    for (var right = size - 1; right >= 1; right -= 2) {
      if (right == 6) right = 5;
      for (var vert = 0; vert < size; vert++) {
        for (var j = 0; j < 2; j++) {
          final x = right - j;
          final upward = ((right + 1) & 2) == 0;
          final y = upward ? size - 1 - vert : vert;
          if (functionMap[y * size + x] != 0 || bit >= raw * 8) continue;
          final cw = bit ~/ 8;
          bit++;
          if (cw != lastCw) {
            lastCw = cw;
            lastHit = false;
          }
          if (lastHit || !inLogoArea(x, y)) continue;
          lastHit = true;
          int block;
          if (cw < dataLen) {
            block = cw < shortLen * numBlocks ? cw % numBlocks : numShort + (cw - shortLen * numBlocks);
          } else {
            block = (cw - dataLen) % numBlocks;
          }
          damaged[block]++;
        }
      }
    }

    var worst = 0.0;
    final correctable = blockEcc ~/ 2;
    for (var b = 0; b < numBlocks; b++) {
      final ratio = damaged[b] / correctable;
      if (ratio > worst) worst = ratio;
    }
    return worst;
  }
}

void _eyeCorners(EyeShape shape, double s, int cc, _CRect r) {
  r.setCorners(_none, 0);
  switch (shape) {
    case EyeShape.rounded:
      r.setCorners(_round, s * 0.25);
    case EyeShape.circle:
      r.setCorners(_round, s / 2);
    // leaf/drop use 0.36 * s: fully rounded versions break finder detection
    case EyeShape.leaf:
      r.setCorner(cc, _round, s * 0.36);
      r.setCorner((cc + 2) % 4, _round, s * 0.36);
    case EyeShape.drop:
      r.setCorners(_round, s * 0.36);
      r.setCorner(cc, _none, 0);
    case EyeShape.bevel:
      r.setCorners(_chamfer, s * 0.25);
    case EyeShape.diamond:
      r.setCorners(_chamfer, s / 2);
    case EyeShape.square:
    case EyeShape.dots:
      break;
  }
}

void _innerCorners(_CRect outer, double sInner, _CRect out) {
  for (var i = 0; i < 4; i++) {
    var a = outer.a[i];
    if (outer.kind[i] == _round) {
      a -= 1;
    } else if (outer.kind[i] == _chamfer) {
      a -= 2 - math.sqrt(2.0);
    }
    a = _clamp(a, 0, sInner / 2);
    out.kind[i] = a > 0 ? outer.kind[i] : _none;
    out.a[i] = a > 0 ? a : 0;
  }
}

/// Gradient geometry of [p] for [layer] (SPEC 8.4).
QRKudeGradientGeometry gradientGeometryFor(LayoutContext L, QRKudeLayer layer, QRKudePaint? p) {
  double ax, ay, aw, ah;
  if (layer == QRKudeLayer.background) {
    ax = 0;
    ay = 0;
    aw = L.W;
    ah = L.H;
  } else {
    ax = L.ox;
    ay = L.oy;
    aw = L.n.toDouble();
    ah = L.n.toDouble();
  }
  final cx = ax + aw / 2, cy = ay + ah / 2;

  // a null paint has zeroed parameters in the C core; a solid paint has the
  // gradient defaults
  final double angle, pcx, pcy, pr;
  switch (p) {
    case null:
      angle = 0;
      pcx = pcy = pr = 0;
    case QRKudeSolid():
      angle = 0;
      pcx = pcy = 0.5;
      pr = 0.75;
    case QRKudeLinearGradient(angle: final a):
      angle = a;
      pcx = pcy = 0.5;
      pr = 0.75;
    case QRKudeRadialGradient(cx: final x, cy: final y, r: final rr):
      angle = 0;
      pcx = x;
      pcy = y;
      pr = rr;
  }

  var a = angle.remainder(360.0);
  if (a < 0) a += 360.0;
  double dx, dy;
  if (a == 0) {
    dx = 1;
    dy = 0;
  } else if (a == 90) {
    dx = 0;
    dy = 1;
  } else if (a == 180) {
    dx = -1;
    dy = 0;
  } else if (a == 270) {
    dx = 0;
    dy = -1;
  } else {
    final rad = a * math.pi / 180.0;
    dx = math.cos(rad);
    dy = math.sin(rad);
  }
  final half = (dx.abs() * aw + dy.abs() * ah) / 2;
  return QRKudeGradientGeometry(
    cx - dx * half,
    cy - dy * half,
    cx + dx * half,
    cy + dy * half,
    ax + aw * pcx,
    ay + ah * pcy,
    pr * (aw > ah ? aw : ah),
  );
}

/// Normalizes the command stream so that every sink (and every port) sees
/// the same thing: zero-length lines are dropped and a line that returns to
/// the sub-path start right before close is omitted (SPEC 7).
class _Emitter {
  _Emitter(this.sink);

  final QRKudePathSink? sink;
  double sx = 0, sy = 0, cx = 0, cy = 0;
  bool pending = false;
  double px = 0, py = 0;
  int count = 0;

  void _flush() {
    if (pending) {
      sink?.lineTo(px, py);
      pending = false;
    }
  }

  void move(double x, double y) {
    _flush();
    sx = cx = x;
    sy = cy = y;
    count++;
    sink?.moveTo(x, y);
  }

  void line(double x, double y) {
    if (x == cx && y == cy) return;
    _flush();
    pending = true;
    px = cx = x;
    py = cy = y;
  }

  void arc(double r, bool cw, double x, double y, double ccx, double ccy) {
    _flush();
    cx = x;
    cy = y;
    sink?.arcTo(r, cw, x, y, ccx, ccy);
  }

  void close() {
    if (pending && !(px == sx && py == sy)) _flush();
    pending = false;
    cx = sx;
    cy = sy;
    sink?.close();
  }

  void _corner(_CRect r, int i, bool cw, double x, double y, double ccx, double ccy) {
    final k = r.kind[i], a = r.a[i];
    if (k == _round && a > 0) {
      arc(a, cw, x, y, ccx, ccy);
    } else if (k == _chamfer && a > 0) {
      line(x, y);
    }
  }

  /// Clockwise (filled) rectangle with corner treatments.
  void crect(_CRect r) {
    final x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
    final tl = r.amt(_tl), tr = r.amt(_tr), br = r.amt(_br), bl = r.amt(_bl);
    move(x0 + tl, y0);
    line(x1 - tr, y0);
    _corner(r, _tr, true, x1, y0 + tr, x1 - tr, y0 + tr);
    line(x1, y1 - br);
    _corner(r, _br, true, x1 - br, y1, x1 - br, y1 - br);
    line(x0 + bl, y1);
    _corner(r, _bl, true, x0, y1 - bl, x0 + bl, y1 - bl);
    line(x0, y0 + tl);
    _corner(r, _tl, true, x0 + tl, y0, x0 + tl, y0 + tl);
    close();
  }

  /// Counter-clockwise version, used for holes.
  void crectCcw(_CRect r) {
    final x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
    final tl = r.amt(_tl), tr = r.amt(_tr), br = r.amt(_br), bl = r.amt(_bl);
    move(x0, y0 + tl);
    line(x0, y1 - bl);
    _corner(r, _bl, false, x0 + bl, y1, x0 + bl, y1 - bl);
    line(x1 - br, y1);
    _corner(r, _br, false, x1, y1 - br, x1 - br, y1 - br);
    line(x1, y0 + tr);
    _corner(r, _tr, false, x1 - tr, y0, x1 - tr, y0 + tr);
    line(x0 + tl, y0);
    _corner(r, _tl, false, x0, y0 + tl, x0 + tl, y0 + tl);
    close();
  }

  /// Four-point star: tips at distance h, concave arcs of radius
  /// h * sqrt(2.5) centered at (+-1.5h, +-1.5h).
  void star(double cx, double cy, double h) {
    final r = math.sqrt(2.5) * h, c = 1.5 * h;
    move(cx, cy - h);
    arc(r, false, cx + h, cy, cx + c, cy - c);
    arc(r, false, cx, cy + h, cx + c, cy + c);
    arc(r, false, cx - h, cy, cx - c, cy + c);
    arc(r, false, cx, cy - h, cx - c, cy - c);
    close();
  }

  // ignore: non_constant_identifier_names
  void fillet(int corner, double X, double Y, double r) {
    switch (corner) {
      case _tl:
        move(X, Y);
        line(X + r, Y);
        arc(r, false, X, Y + r, X + r, Y + r);
      case _tr:
        move(X + 1, Y);
        line(X + 1, Y + r);
        arc(r, false, X + 1 - r, Y, X + 1 - r, Y + r);
      case _br:
        move(X + 1, Y + 1);
        line(X + 1 - r, Y + 1);
        arc(r, false, X + 1, Y + 1 - r, X + 1 - r, Y + 1 - r);
      default:
        move(X, Y + 1);
        line(X, Y + 1 - r);
        arc(r, false, X + r, Y + 1, X + r, Y + 1 - r);
    }
    close();
  }
}
