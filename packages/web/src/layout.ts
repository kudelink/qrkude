/*
 * QRKude - layout, module classification, shape model and vector geometry.
 * Port of core/src/qrk_layout.c. Every arithmetic expression keeps the
 * operation order of the reference so that the emitted doubles are identical.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import {
  QRCode,
  alignmentPositions,
  eccCodewordsPerBlock,
  eccIndex,
  functionMap,
  numEccBlocks,
  rawCodewords,
} from './encode.js';
import { EYE_SHAPES, EyeShape, MODULE_SHAPES, ModuleShape, Paint, Style, solidPaint } from './style.js';

/** Drawing layers, in painting order. */
export type Layer = 'background' | 'border' | 'modules' | 'eyeFrames' | 'eyeBalls' | 'logoBackground';

export const LAYERS: readonly Layer[] = ['background', 'border', 'modules', 'eyeFrames', 'eyeBalls', 'logoBackground'];

/**
 * Receives the geometry of a layer. All coordinates are in modules (canvas
 * units, y down). All sub-paths are closed; fill with the non-zero rule.
 * `arcTo` draws a circular arc of radius `r` from the current point to
 * `(x, y)` around the center `(cx, cy)`; `clockwise` refers to the screen.
 * Arcs never exceed 90 degrees.
 */
export interface PathSink {
  moveTo(x: number, y: number): void;
  lineTo(x: number, y: number): void;
  arcTo(r: number, clockwise: boolean, x: number, y: number, cx: number, cy: number): void;
  close(): void;
}

/** Canvas layout in modules. */
export interface Layout {
  /** Whole canvas. */
  width: number;
  height: number;
  /** Top-left of module (0, 0). */
  symbolX: number;
  symbolY: number;
  /** Modules per side. */
  modules: number;
  hasLogo: boolean;
  /** Logo image box (square). */
  logoX: number;
  logoY: number;
  logoSize: number;
  hasLabel: boolean;
  /** Text anchor (middle) and baseline. */
  labelX: number;
  labelBaseline: number;
  /** Clamped label font size. */
  labelSize: number;
}

/** Gradient geometry in canvas units, as written to the SVG. */
export interface GradientGeometry {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  cx: number;
  cy: number;
  r: number;
}

/* ---- corners and corner rectangles ---------------------------------------- */

const NONE = 0;
const ROUND = 1;
const CHAMFER = 2;
const TL = 0;
const TR = 1;
const BR = 2;
const BL = 3;

interface Corner {
  kind: number;
  a: number;
}

interface CRect {
  x0: number;
  y0: number;
  x1: number;
  y1: number;
  c: Corner[]; /* TL, TR, BR, BL */
}

function corners(kind: number, a: number): Corner[] {
  return [{ kind, a }, { kind, a }, { kind, a }, { kind, a }];
}

function crect(x0: number, y0: number, x1: number, y1: number, kind: number, a: number): CRect {
  return { x0, y0, x1, y1, c: corners(kind, a) };
}

/** NaN clamps to lo. */
function clamp(v: number, lo: number, hi: number): number {
  if (!(v >= lo)) return lo;
  return v > hi ? hi : v;
}

/** Upper bound for lengths given in modules (margin, border, label size...). */
export const MAX_LENGTH = 1000;

/* ---- layout -------------------------------------------------------------- */

const MODULE_INDEX: Record<string, number> = {};
MODULE_SHAPES.forEach((s, i) => (MODULE_INDEX[s] = i));
const EYE_INDEX: Record<string, number> = {};
EYE_SHAPES.forEach((s, i) => (EYE_INDEX[s] = i));

function validModuleShape(s: unknown): ModuleShape {
  return typeof s === 'string' && s in MODULE_INDEX ? (s as ModuleShape) : 'square';
}

function validEyeShape(s: unknown): EyeShape | null {
  return typeof s === 'string' && s in EYE_INDEX ? (s as EyeShape) : null;
}

/** Ball shape actually drawn: dots become a circle, invalid becomes square. */
export function ballShape(shape: EyeShape | null | undefined): EyeShape {
  if (shape === 'dots') return 'circle';
  return validEyeShape(shape) || 'square';
}

/** @internal Resolved layout with clamped values (qrk__layout). */
export class LayoutContext {
  readonly n: number;
  readonly margin: number;
  readonly border: number;
  readonly bgRadius: number;
  readonly shape: ModuleShape;
  readonly size: number;
  readonly roundness: number;
  readonly W: number;
  readonly H: number;
  readonly ox: number;
  readonly oy: number;
  readonly hasLabel: boolean;
  readonly labelSize: number = 0;
  readonly bandY: number = 0;
  readonly bandH: number = 0;
  readonly labelX: number = 0;
  readonly labelBaseline: number = 0;
  readonly hasLogo: boolean;
  readonly excavate: boolean = false;
  readonly logoCx: number = 0;
  readonly logoCy: number = 0;
  readonly logoHalf: number = 0;
  readonly logoAreaHalf: number = 0;
  readonly alignStyled: boolean;
  readonly alignPos: number[];
  private cells: Uint8Array | null = null;

  constructor(readonly code: QRCode, readonly style: Style) {
    const s = style;
    const n = (this.n = code.size);
    this.margin = clamp(s.margin, 0, MAX_LENGTH);
    this.border = clamp(s.border.width, 0, MAX_LENGTH);
    this.shape = validModuleShape(s.modules.shape);
    this.size = clamp(s.modules.size, 0.1, 1);
    this.roundness = clamp(s.modules.roundness, 0, 1);

    const frame = this.margin + this.border;
    this.W = n + 2 * frame;
    let H = this.W;
    this.ox = frame;
    let oy = frame;

    this.hasLabel = !!s.label.text;
    if (this.hasLabel) {
      const f = clamp(s.label.size, 0.1, MAX_LENGTH);
      this.labelSize = f;
      this.bandH = f * 1.5;
      H += this.bandH;
      if (s.label.position === 'top') {
        this.bandY = this.border;
        oy += this.bandH;
      } else {
        this.bandY = this.W - this.border;
      }
      this.labelX = this.W / 2;
      this.labelBaseline = this.bandY + f * 1.1;
    }
    this.H = H;
    this.oy = oy;
    const minSide = this.W < this.H ? this.W : this.H;
    this.bgRadius = clamp(s.background.radius, 0, minSide / 2);

    this.hasLogo = s.logo.size > 0;
    if (this.hasLogo) {
      this.excavate = !!s.logo.excavate;
      this.logoCx = this.ox + n / 2.0;
      this.logoCy = this.oy + n / 2.0;
      this.logoHalf = (n * clamp(s.logo.size, 0, 1)) / 2;
      this.logoAreaHalf = this.logoHalf + clamp(s.logo.margin, 0, MAX_LENGTH);
    }

    this.alignStyled = validEyeShape(s.alignment.frame) !== null;
    this.alignPos = this.alignStyled ? alignmentPositions(code.version) : [];
  }

  static isFinder(n: number, x: number, y: number): boolean {
    return (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7);
  }

  inStyledAlignment(x: number, y: number): boolean {
    if (!this.alignStyled) return false;
    const pos = this.alignPos;
    const k = pos.length;
    for (let i = 0; i < k; i++) {
      const dy = y - pos[i];
      if (dy < -2 || dy > 2) continue;
      for (let j = 0; j < k; j++) {
        const dx = x - pos[j];
        if (dx >= -2 && dx <= 2 && isAlignmentCenter(k, i, j)) return true;
      }
    }
    return false;
  }

  /** Module overlaps the logo area (box + margin). */
  inLogoArea(x: number, y: number): boolean {
    const c = this.n / 2.0;
    const h = this.logoAreaHalf;
    const dx = Math.abs(x + 0.5 - c);
    const dy = Math.abs(y + 0.5 - c);
    if (this.style.logo.shape === 'circle') {
      const nx = dx - 0.5 > 0 ? dx - 0.5 : 0;
      const ny = dy - 0.5 > 0 ? dy - 0.5 : 0;
      return nx * nx + ny * ny < h * h;
    }
    return dx < h + 0.5 && dy < h + 0.5;
  }

  isExcavated(x: number, y: number): boolean {
    return this.excavate && this.inLogoArea(x, y);
  }

  /* 0 = not a data cell, 1 = light data cell, 2 = dark data cell */
  private classify(): Uint8Array {
    if (this.cells) return this.cells;
    const n = this.n;
    const cells = new Uint8Array(n * n);
    for (let y = 0; y < n; y++)
      for (let x = 0; x < n; x++) {
        if (LayoutContext.isFinder(n, x, y) || this.inStyledAlignment(x, y) || this.isExcavated(x, y)) continue;
        cells[y * n + x] = this.code.modules[y * n + x] === 1 ? 2 : 1;
      }
    this.cells = cells;
    return cells;
  }

  isDataCell(x: number, y: number): boolean {
    const n = this.n;
    if (x < 0 || y < 0 || x >= n || y >= n) return false;
    return this.classify()[y * n + x] !== 0;
  }

  isDataDark(x: number, y: number): boolean {
    const n = this.n;
    if (x < 0 || y < 0 || x >= n || y >= n) return false;
    return this.classify()[y * n + x] === 2;
  }

  /** Cell-local shape of a dark data module; null for the star shape. */
  moduleCRect(x: number, y: number): CRect | null {
    const s = this.size;
    const d = (1 - s) / 2;
    const rho = this.roundness;
    const r = crect(d, d, 1 - d, 1 - d, NONE, 0);
    switch (this.shape) {
      case 'square':
        return r;
      case 'dot':
        r.c = corners(ROUND, s / 2);
        return r;
      case 'rounded':
        r.c = corners(ROUND, (rho * s) / 2);
        return r;
      case 'diamond':
        r.c = corners(CHAMFER, s / 2);
        return r;
      case 'star':
        return null;
      case 'hbars': {
        const l = this.isDataDark(x - 1, y);
        const rt = this.isDataDark(x + 1, y);
        if (l) r.x0 = 0;
        if (rt) r.x1 = 1;
        const a = (rho * s) / 2;
        if (!l) r.c[TL] = r.c[BL] = { kind: ROUND, a };
        if (!rt) r.c[TR] = r.c[BR] = { kind: ROUND, a };
        return r;
      }
      case 'vbars': {
        const u = this.isDataDark(x, y - 1);
        const dn = this.isDataDark(x, y + 1);
        if (u) r.y0 = 0;
        if (dn) r.y1 = 1;
        const a = (rho * s) / 2;
        if (!u) r.c[TL] = r.c[TR] = { kind: ROUND, a };
        if (!dn) r.c[BL] = r.c[BR] = { kind: ROUND, a };
        return r;
      }
      default:
        break;
    }
    /* connected shapes always fill the whole cell */
    r.x0 = r.y0 = 0;
    r.x1 = r.y1 = 1;
    const up = this.isDataDark(x, y - 1);
    const down = this.isDataDark(x, y + 1);
    const left = this.isDataDark(x - 1, y);
    const right = this.isDataDark(x + 1, y);
    const open = [!up && !left, !up && !right, !down && !right, !down && !left];
    const kind = this.shape === 'bevel' ? CHAMFER : ROUND;
    for (let i = 0; i < 4; i++) {
      let use = open[i];
      if (this.shape === 'classy' && (i === TR || i === BL)) use = false;
      if (use) r.c[i] = { kind, a: rho / 2 };
    }
    return r;
  }

  /** Which corners of a light data cell get a liquid fillet (bit i = corner i). */
  filletMask(x: number, y: number): number {
    if (this.shape !== 'liquid' || this.roundness <= 0) return 0;
    const up = this.isDataDark(x, y - 1);
    const down = this.isDataDark(x, y + 1);
    const left = this.isDataDark(x - 1, y);
    const right = this.isDataDark(x + 1, y);
    let m = 0;
    if (up && left && this.isDataDark(x - 1, y - 1)) m |= 1 << TL;
    if (up && right && this.isDataDark(x + 1, y - 1)) m |= 1 << TR;
    if (down && right && this.isDataDark(x + 1, y + 1)) m |= 1 << BR;
    if (down && left && this.isDataDark(x - 1, y + 1)) m |= 1 << BL;
    return m;
  }

  logoBgCRect(): CRect {
    const h = this.logoAreaHalf;
    const r = crect(this.logoCx - h, this.logoCy - h, this.logoCx + h, this.logoCy + h, NONE, 0);
    if (this.style.logo.shape === 'rounded') r.c = corners(ROUND, h * 0.3);
    else if (this.style.logo.shape === 'circle') r.c = corners(ROUND, h);
    return r;
  }
}

function isAlignmentCenter(k: number, i: number, j: number): boolean {
  return !((i === 0 && j === 0) || (i === 0 && j === k - 1) || (i === k - 1 && j === 0));
}

function eyeCorners(shape: EyeShape, s: number, cc: number): Corner[] {
  switch (shape) {
    case 'rounded':
      return corners(ROUND, s * 0.25);
    case 'circle':
      return corners(ROUND, s / 2);
    /* leaf/drop use 0.36 * s: fully rounded versions break finder detection */
    case 'leaf': {
      const c = corners(NONE, 0);
      c[cc] = { kind: ROUND, a: s * 0.36 };
      c[(cc + 2) % 4] = { kind: ROUND, a: s * 0.36 };
      return c;
    }
    case 'drop': {
      const c = corners(ROUND, s * 0.36);
      c[cc] = { kind: NONE, a: 0 };
      return c;
    }
    case 'bevel':
      return corners(CHAMFER, s * 0.25);
    case 'diamond':
      return corners(CHAMFER, s / 2);
    default:
      return corners(NONE, 0);
  }
}

function innerCorners(outer: Corner[], sInner: number): Corner[] {
  return outer.map((o) => {
    let a = o.a;
    if (o.kind === ROUND) a -= 1;
    else if (o.kind === CHAMFER) a -= 2 - Math.sqrt(2.0);
    a = clamp(a, 0, sInner / 2);
    return { kind: a > 0 ? o.kind : NONE, a: a > 0 ? a : 0 };
  });
}

/* ---- paints -------------------------------------------------------------- */

/** Paint used by a layer (eye paints resolved, border / logo background as solid). */
export function layerPaint(style: Style, layer: Layer): Paint {
  switch (layer) {
    case 'background':
      return style.background.color;
    case 'border':
      return solidPaint(style.border.color);
    case 'eyeFrames':
      return style.eyes.frameColor.type !== 'none' ? style.eyes.frameColor : style.modules.color;
    case 'eyeBalls':
      return style.eyes.ballColor.type !== 'none' ? style.eyes.ballColor : style.modules.color;
    case 'logoBackground':
      return solidPaint(style.logo.background);
    default:
      return style.modules.color;
  }
}

/**
 * Whether a layer paints anything (SPEC 8.5): its resolved paint has at least
 * one color with alpha > 0 (a solid color, or any of the first 8 gradient
 * stops), and the border has a width > 0, the logo background has a logo, the
 * modules have at least one sub-path. A hint for renderers that skip
 * invisible layers; the SVG writer keeps its own rules.
 */
export function layerVisible(code: QRCode, style: Style, layer: Layer): boolean {
  const p = layerPaint(style, layer);
  let painted = p.type === 'solid' && (p.color & 0xff) !== 0;
  if (p.type === 'linear' || p.type === 'radial')
    painted = p.stops.slice(0, 8).some((st) => (st.color & 0xff) !== 0);
  switch (layer) {
    case 'border':
      return painted && style.border.width > 0;
    case 'logoBackground':
      return painted && style.logo.size > 0;
    case 'modules':
      return painted && emitLayer(code, style, layer) > 0;
    default:
      return painted;
  }
}

/** @internal */
export function gradientGeometryFor(L: LayoutContext, layer: Layer, p: Paint): GradientGeometry {
  let ax: number, ay: number, aw: number, ah: number;
  if (layer === 'background') {
    ax = 0;
    ay = 0;
    aw = L.W;
    ah = L.H;
  } else {
    ax = L.ox;
    ay = L.oy;
    aw = L.n;
    ah = L.n;
  }
  const cx = ax + aw / 2;
  const cy = ay + ah / 2;
  const angle = 'angle' in p ? p.angle : 0;
  let a = angle % 360.0;
  if (a < 0) a += 360.0;
  let dx: number, dy: number;
  if (a === 0) {
    dx = 1;
    dy = 0;
  } else if (a === 90) {
    dx = 0;
    dy = 1;
  } else if (a === 180) {
    dx = -1;
    dy = 0;
  } else if (a === 270) {
    dx = 0;
    dy = -1;
  } else {
    const rad = (a * 3.14159265358979323846) / 180.0;
    dx = Math.cos(rad);
    dy = Math.sin(rad);
  }
  const half = (Math.abs(dx) * aw + Math.abs(dy) * ah) / 2;
  const pcx = 'cx' in p ? p.cx : 0.5;
  const pcy = 'cy' in p ? p.cy : 0.5;
  const pr = 'r' in p ? p.r : 0.75;
  return {
    x1: cx - dx * half,
    y1: cy - dy * half,
    x2: cx + dx * half,
    y2: cy + dy * half,
    cx: ax + aw * pcx,
    cy: ay + ah * pcy,
    r: pr * (aw > ah ? aw : ah),
  };
}

/** Gradient geometry of a layer's paint, in canvas units. */
export function gradientGeometry(code: QRCode, style: Style, layer: Layer): GradientGeometry {
  return gradientGeometryFor(new LayoutContext(code, style), layer, layerPaint(style, layer));
}

/** Canvas layout of a code with a style. */
export function getLayout(code: QRCode, style: Style): Layout {
  const L = new LayoutContext(code, style);
  return {
    width: L.W,
    height: L.H,
    symbolX: L.ox,
    symbolY: L.oy,
    modules: L.n,
    hasLogo: L.hasLogo,
    logoX: L.logoCx - L.logoHalf,
    logoY: L.logoCy - L.logoHalf,
    logoSize: 2 * L.logoHalf,
    hasLabel: L.hasLabel,
    labelX: L.labelX,
    labelBaseline: L.labelBaseline,
    labelSize: L.labelSize,
  };
}

/* ---- path emitter -------------------------------------------------------- */

/* Normalizes the command stream so that every sink (and every port) sees the
 * same thing: zero-length lines are dropped and a line that returns to the
 * sub-path start right before close is omitted. */
class Emitter {
  sx = 0;
  sy = 0;
  cx = 0;
  cy = 0;
  pending = false;
  px = 0;
  py = 0;
  count = 0;

  constructor(readonly sink: PathSink | null) {}

  flush(): void {
    if (this.pending) {
      if (this.sink) this.sink.lineTo(this.px, this.py);
      this.pending = false;
    }
  }

  move(x: number, y: number): void {
    this.flush();
    this.sx = this.cx = x;
    this.sy = this.cy = y;
    this.count++;
    if (this.sink) this.sink.moveTo(x, y);
  }

  line(x: number, y: number): void {
    if (x === this.cx && y === this.cy) return;
    this.flush();
    this.pending = true;
    this.px = this.cx = x;
    this.py = this.cy = y;
  }

  arc(r: number, cw: boolean, x: number, y: number, ccx: number, ccy: number): void {
    this.flush();
    this.cx = x;
    this.cy = y;
    if (this.sink) this.sink.arcTo(r, cw, x, y, ccx, ccy);
  }

  close(): void {
    if (this.pending && !(this.px === this.sx && this.py === this.sy)) this.flush();
    this.pending = false;
    this.cx = this.sx;
    this.cy = this.sy;
    if (this.sink) this.sink.close();
  }

  corner(c: Corner, cw: boolean, x: number, y: number, ccx: number, ccy: number): void {
    if (c.kind === ROUND && c.a > 0) this.arc(c.a, cw, x, y, ccx, ccy);
    else if (c.kind === CHAMFER && c.a > 0) this.line(x, y);
  }

  /* Clockwise (filled) rectangle with corner treatments. */
  crect(r: CRect): void {
    const { x0, y0, x1, y1 } = r;
    const tl = camt(r.c[TL]);
    const tr = camt(r.c[TR]);
    const br = camt(r.c[BR]);
    const bl = camt(r.c[BL]);
    this.move(x0 + tl, y0);
    this.line(x1 - tr, y0);
    this.corner(r.c[TR], true, x1, y0 + tr, x1 - tr, y0 + tr);
    this.line(x1, y1 - br);
    this.corner(r.c[BR], true, x1 - br, y1, x1 - br, y1 - br);
    this.line(x0 + bl, y1);
    this.corner(r.c[BL], true, x0, y1 - bl, x0 + bl, y1 - bl);
    this.line(x0, y0 + tl);
    this.corner(r.c[TL], true, x0 + tl, y0, x0 + tl, y0 + tl);
    this.close();
  }

  /* Counter-clockwise version, used for holes. */
  crectCcw(r: CRect): void {
    const { x0, y0, x1, y1 } = r;
    const tl = camt(r.c[TL]);
    const tr = camt(r.c[TR]);
    const br = camt(r.c[BR]);
    const bl = camt(r.c[BL]);
    this.move(x0, y0 + tl);
    this.line(x0, y1 - bl);
    this.corner(r.c[BL], false, x0 + bl, y1, x0 + bl, y1 - bl);
    this.line(x1 - br, y1);
    this.corner(r.c[BR], false, x1, y1 - br, x1 - br, y1 - br);
    this.line(x1, y0 + tr);
    this.corner(r.c[TR], false, x1 - tr, y0, x1 - tr, y0 + tr);
    this.line(x0 + tl, y0);
    this.corner(r.c[TL], false, x0, y0 + tl, x0 + tl, y0 + tl);
    this.close();
  }

  /* Four-point star: tips at distance h, concave arcs of radius h*sqrt(2.5)
   * centered at (+-1.5h, +-1.5h). */
  star(cx: number, cy: number, h: number): void {
    const r = Math.sqrt(2.5) * h;
    const c = 1.5 * h;
    this.move(cx, cy - h);
    this.arc(r, false, cx + h, cy, cx + c, cy - c);
    this.arc(r, false, cx, cy + h, cx + c, cy + c);
    this.arc(r, false, cx - h, cy, cx - c, cy + c);
    this.arc(r, false, cx, cy - h, cx - c, cy - c);
    this.close();
  }

  fillet(corner: number, X: number, Y: number, r: number): void {
    switch (corner) {
      case TL:
        this.move(X, Y);
        this.line(X + r, Y);
        this.arc(r, false, X, Y + r, X + r, Y + r);
        break;
      case TR:
        this.move(X + 1, Y);
        this.line(X + 1, Y + r);
        this.arc(r, false, X + 1 - r, Y, X + 1 - r, Y + r);
        break;
      case BR:
        this.move(X + 1, Y + 1);
        this.line(X + 1 - r, Y + 1);
        this.arc(r, false, X + 1, Y + 1 - r, X + 1 - r, Y + 1 - r);
        break;
      default:
        this.move(X, Y + 1);
        this.line(X, Y + 1 - r);
        this.arc(r, false, X + r, Y + 1, X + r, Y + 1 - r);
        break;
    }
    this.close();
  }
}

function camt(c: Corner): number {
  return c.kind === NONE ? 0 : c.a;
}

function emitModules(e: Emitter, L: LayoutContext): void {
  const n = L.n;
  const ox = L.ox;
  const oy = L.oy;
  const s = L.size;
  const d = (1 - s) / 2;

  if ((L.shape === 'square' && s === 1) || L.shape === 'hbars') {
    const bars = L.shape === 'hbars';
    const inset = bars ? d : 0;
    const a = bars ? (L.roundness * s) / 2 : 0;
    for (let y = 0; y < n; y++) {
      for (let x = 0; x < n; ) {
        if (!L.isDataDark(x, y)) {
          x++;
          continue;
        }
        const x0 = x;
        while (x < n && L.isDataDark(x, y)) x++;
        e.crect(crect(ox + x0 + inset, oy + y + inset, ox + x - inset, oy + y + 1 - inset, bars ? ROUND : NONE, a));
      }
    }
    return;
  }
  if (L.shape === 'vbars') {
    const a = (L.roundness * s) / 2;
    for (let x = 0; x < n; x++) {
      for (let y = 0; y < n; ) {
        if (!L.isDataDark(x, y)) {
          y++;
          continue;
        }
        const y0 = y;
        while (y < n && L.isDataDark(x, y)) y++;
        e.crect(crect(ox + x + d, oy + y0 + d, ox + x + 1 - d, oy + y - d, ROUND, a));
      }
    }
    return;
  }

  for (let y = 0; y < n; y++) {
    for (let x = 0; x < n; x++) {
      const X = ox + x;
      const Y = oy + y;
      if (L.isDataDark(x, y)) {
        const r = L.moduleCRect(x, y);
        if (!r) {
          e.star(X + 0.5, Y + 0.5, s / 2);
          continue;
        }
        r.x0 += X;
        r.x1 += X;
        r.y0 += Y;
        r.y1 += Y;
        e.crect(r);
      } else if (L.isDataCell(x, y)) {
        const m = L.filletMask(x, y);
        for (let c = 0; c < 4; c++) if (m & (1 << c)) e.fillet(c, X, Y, L.roundness / 2);
      }
    }
  }
}

/* One eye-like pattern: frame (ring) or ball of size s at (X, Y). */
function emitEyePart(e: Emitter, shape: EyeShape, X: number, Y: number, s: number, ring: boolean, cc: number): void {
  if (shape === 'dots') {
    const k = Math.trunc(s);
    for (let j = 0; j < k; j++)
      for (let i = 0; i < k; i++) {
        if (ring && !(i === 0 || j === 0 || i === k - 1 || j === k - 1)) continue;
        e.crect(crect(X + i, Y + j, X + i + 1, Y + j + 1, ROUND, 0.5));
      }
    return;
  }
  const outer: CRect = { x0: X, y0: Y, x1: X + s, y1: Y + s, c: eyeCorners(shape, s, cc) };
  e.crect(outer);
  if (ring) {
    const inner: CRect = { x0: X + 1, y0: Y + 1, x1: X + s - 1, y1: Y + s - 1, c: innerCorners(outer.c, s - 2) };
    e.crectCcw(inner);
  }
}

function emitEyes(e: Emitter, L: LayoutContext, frames: boolean): void {
  const st = L.style;
  const n = L.n;
  const ex = [0, n - 7, 0];
  const ey = [0, 0, n - 7];
  const cc = [BR, BL, TR];
  const fs = validEyeShape(st.eyes.frame) || 'square';
  const bs = ballShape(st.eyes.ball);
  for (let i = 0; i < 3; i++) {
    const X = L.ox + ex[i];
    const Y = L.oy + ey[i];
    if (frames) emitEyePart(e, fs, X, Y, 7, true, cc[i]);
    else emitEyePart(e, bs, X + 2, Y + 2, 3, false, cc[i]);
  }
  if (!L.alignStyled) return;
  const afs = validEyeShape(st.alignment.frame) as EyeShape;
  const abs = ballShape(st.alignment.ball);
  const pos = L.alignPos;
  const k = pos.length;
  for (let i = 0; i < k; i++)
    for (let j = 0; j < k; j++) {
      if (!isAlignmentCenter(k, i, j)) continue;
      const X = L.ox + pos[j];
      const Y = L.oy + pos[i];
      if (frames) emitEyePart(e, afs, X - 2, Y - 2, 5, true, BR);
      else emitEyePart(e, abs, X, Y, 1, false, BR);
    }
}

function canvasCRect(L: LayoutContext, inset: number): CRect {
  const a = L.bgRadius - inset;
  return crect(inset, inset, L.W - inset, L.H - inset, a > 0 ? ROUND : NONE, a > 0 ? a : 0);
}

/** @internal Emits a layer for an existing layout context. */
export function emitLayerFor(L: LayoutContext, layer: Layer, sink: PathSink | null): number {
  const e = new Emitter(sink);
  switch (layer) {
    case 'background':
      e.crect(canvasCRect(L, 0));
      break;
    case 'border':
      if (L.border > 0) {
        e.crect(canvasCRect(L, 0));
        e.crectCcw(canvasCRect(L, L.border));
      }
      break;
    case 'modules':
      emitModules(e, L);
      break;
    case 'eyeFrames':
      emitEyes(e, L, true);
      break;
    case 'eyeBalls':
      emitEyes(e, L, false);
      break;
    case 'logoBackground':
      if (L.hasLogo && (L.style.logo.background & 0xff) !== 0) e.crect(L.logoBgCRect());
      break;
    default:
      break;
  }
  return e.count;
}

/**
 * Emits the geometry of one layer into a path sink (pass null to only count).
 * Returns the number of sub-paths. This is the shape model shared by the SVG
 * writer and the canvas renderer.
 */
export function emitLayer(code: QRCode, style: Style, layer: Layer, sink?: PathSink | null): number {
  return emitLayerFor(new LayoutContext(code, style), layer, sink || null);
}

/* ---- logo damage --------------------------------------------------------- */

/**
 * Worst-case logo damage: max over RS blocks of (codewords hidden by the logo
 * area) / (codewords the block can correct). 0 = no logo; above 1 the symbol
 * cannot be decoded.
 */
export function logoDamage(code: QRCode, style: Style): number {
  const L = new LayoutContext(code, style);
  if (!L.hasLogo) return 0;

  const ver = code.version;
  const ecc = eccIndex(code.ecc);
  const size = code.size;
  const numBlocks = numEccBlocks(ecc, ver);
  const blockEcc = eccCodewordsPerBlock(ecc, ver);
  const raw = rawCodewords(ver);
  const dataLen = raw - numBlocks * blockEcc;
  const numShort = numBlocks - (raw % numBlocks);
  const shortLen = Math.floor(raw / numBlocks) - blockEcc;
  const fmap = functionMap(ver);

  const damaged = new Array<number>(numBlocks).fill(0);
  let lastCw = -1;
  let lastHit = false;
  let bit = 0;
  for (let right = size - 1; right >= 1; right -= 2) {
    if (right === 6) right = 5;
    for (let vert = 0; vert < size; vert++) {
      for (let j = 0; j < 2; j++) {
        const x = right - j;
        const upward = ((right + 1) & 2) === 0;
        const y = upward ? size - 1 - vert : vert;
        if (fmap[y * size + x] || bit >= raw * 8) continue;
        const cw = Math.floor(bit / 8);
        bit++;
        if (cw !== lastCw) {
          lastCw = cw;
          lastHit = false;
        }
        if (lastHit || !L.inLogoArea(x, y)) continue;
        lastHit = true;
        let block: number;
        if (cw < dataLen)
          block = cw < shortLen * numBlocks ? cw % numBlocks : numShort + (cw - shortLen * numBlocks);
        else block = (cw - dataLen) % numBlocks;
        damaged[block]++;
      }
    }
  }

  let worst = 0;
  const correctable = Math.floor(blockEcc / 2);
  for (let b = 0; b < numBlocks; b++) {
    const ratio = damaged[b] / correctable;
    if (ratio > worst) worst = ratio;
  }
  return worst;
}
