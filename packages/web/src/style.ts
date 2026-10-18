/*
 * QRKude - style model, colors, paints and the cross-language JSON format.
 * Same keys, defaults and clamping as core/src/qrk_json.c and qrk_layout.c.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { Ecc, EncodeOptions, QRKudeError } from './encode.js';
import { JsonObject, JsonValue, fromPlain, parseJson } from './json.js';

/** A color as an unsigned 32-bit integer `0xRRGGBBAA`. */
export type Color = number;

export const MODULE_SHAPES = [
  'square', 'dot', 'rounded', 'diamond', 'star', 'fluid', 'liquid', 'bevel', 'classy', 'hbars', 'vbars',
] as const;
export type ModuleShape = (typeof MODULE_SHAPES)[number];

export const EYE_SHAPES = ['square', 'rounded', 'circle', 'leaf', 'drop', 'bevel', 'diamond', 'dots'] as const;
export type EyeShape = (typeof EYE_SHAPES)[number];

export const LOGO_SHAPES = ['square', 'rounded', 'circle'] as const;
export type LogoShape = (typeof LOGO_SHAPES)[number];

export type LabelPosition = 'bottom' | 'top';

export interface GradientStop {
  /** 0..1 */
  offset: number;
  color: Color;
}

/** No paint: transparent, or "same as modules" for the eye paints. */
export interface NonePaint {
  type: 'none';
}

export interface SolidPaint {
  type: 'solid';
  color: Color;
}

export interface GradientPaint {
  type: 'linear' | 'radial';
  /** Linear: degrees, 0 = left to right, 90 = top to bottom. */
  angle: number;
  /** Radial center and radius, relative to the painted area. */
  cx: number;
  cy: number;
  r: number;
  /** At most 8 stops are used. */
  stops: GradientStop[];
}

export type Paint = NonePaint | SolidPaint | GradientPaint;

/** A fully resolved style (see docs/SPEC.md section 2). */
export interface Style {
  /** Quiet zone in modules. */
  margin: number;
  modules: {
    shape: ModuleShape;
    /** 0.1..1 (square, dot, rounded, diamond, star, bars). */
    size: number;
    /** 0..1 (rounded, fluid, liquid, bevel, classy, bars). */
    roundness: number;
    color: Paint;
  };
  eyes: {
    frame: EyeShape;
    ball: EyeShape;
    /** `none` = same as modules. */
    frameColor: Paint;
    ballColor: Paint;
  };
  alignment: {
    /** null = draw alignment patterns as data modules. */
    frame: EyeShape | null;
    /** null = square (when frame is set). */
    ball: EyeShape | null;
  };
  background: {
    /** `none` = transparent. */
    color: Paint;
    radius: number;
  };
  border: {
    width: number;
    color: Color;
  };
  logo: {
    /** Fraction of the symbol width; 0 = no logo. */
    size: number;
    /** Clear space around the logo, in modules. */
    margin: number;
    shape: LogoShape;
    /** Remove the modules behind the logo area. */
    excavate: boolean;
    /** Fill of the logo area (alpha 0 = none). */
    background: Color;
    /** Image URL or data: URI (SVG output). */
    href: string | null;
  };
  label: {
    /** null / empty = no label. */
    text: string | null;
    font: string | null;
    /** Font size in modules. */
    size: number;
    color: Color;
    position: LabelPosition;
    bold: boolean;
  };
}

/** SVG writer options. */
export interface SvgOptions {
  /** width attribute in px (0 = omit width/height). */
  width?: number;
  /** Gradient id prefix (default "qrk"). */
  idPrefix?: string | null;
}

/** A parsed `{encode, style, svg}` document. `encode` only contains the keys
 * present in the input, so `encode.ecc === undefined` means "use the default
 * rule" (H with a logo, else M). */
export interface QRDocument {
  encode: EncodeOptions;
  style: Style;
  svg: SvgOptions;
}

/* ---- colors -------------------------------------------------------------- */

export const TRANSPARENT: Color = 0;

/** Builds a color from 0..255 channels. */
export function rgba(r: number, g: number, b: number, a = 255): Color {
  return (((r & 255) << 24) | ((g & 255) << 16) | ((b & 255) << 8) | (a & 255)) >>> 0;
}

/** Parses `#rgb`, `#rrggbb`, `#rrggbbaa` or `transparent`; null if invalid. */
export function parseColor(str: string): Color | null {
  if (str === 'transparent') return TRANSPARENT;
  if (str.length < 1 || str[0] !== '#') return null;
  let v = 0;
  for (let i = 1; i < str.length; i++) {
    const c = str[i];
    const d = /[0-9a-fA-F]/.test(c) ? parseInt(c, 16) : -1;
    if (d < 0) return null;
    v = (v * 16 + d) % 4294967296;
  }
  switch (str.length) {
    case 4:
      return rgba(((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17);
    case 7:
      return ((v * 256) % 4294967296) + 0xff;
    case 9:
      return v >>> 0;
    default:
      return null;
  }
}

function hex2(v: number): string {
  return (v < 16 ? '0' : '') + v.toString(16);
}

/** `#rrggbb` (lower case, alpha dropped). */
export function colorHex(c: Color): string {
  return '#' + hex2(c >>> 24) + hex2((c >>> 16) & 255) + hex2((c >>> 8) & 255);
}

/** `#rrggbb` or `#rrggbbaa` when not opaque (round-trips through parseColor). */
export function formatColor(c: Color): string {
  const a = c & 255;
  return a === 255 ? colorHex(c) : colorHex(c) + hex2(a);
}

/** Alpha channel 0..255. */
export function colorAlpha(c: Color): number {
  return c & 255;
}

/* ---- paints -------------------------------------------------------------- */

export function solidPaint(color: Color): SolidPaint {
  return { type: 'solid', color };
}

export function linearPaint(angle: number, from: Color, to: Color): GradientPaint {
  return { type: 'linear', angle, cx: 0.5, cy: 0.5, r: 0.75, stops: [{ offset: 0, color: from }, { offset: 1, color: to }] };
}

export function radialPaint(center: Color, edge: Color): GradientPaint {
  return { type: 'radial', angle: 0, cx: 0.5, cy: 0.5, r: 0.75, stops: [{ offset: 0, color: center }, { offset: 1, color: edge }] };
}

export function isGradient(p: Paint): p is GradientPaint {
  return p.type === 'linear' || p.type === 'radial';
}

/* ---- defaults ------------------------------------------------------------ */

/** The default style (classic black squares on white, 4-module quiet zone). */
export function defaultStyle(): Style {
  return {
    margin: 4,
    modules: { shape: 'square', size: 1, roundness: 1, color: solidPaint(0x000000ff) },
    eyes: { frame: 'square', ball: 'square', frameColor: { type: 'none' }, ballColor: { type: 'none' } },
    alignment: { frame: null, ball: null },
    background: { color: solidPaint(0xffffffff), radius: 0 },
    border: { width: 0, color: 0x000000ff },
    logo: { size: 0, margin: 1, shape: 'square', excavate: true, background: TRANSPARENT, href: null },
    label: { text: null, font: 'sans-serif', size: 4, color: 0x000000ff, position: 'bottom', bold: true },
  };
}

/** Deep copy of a style. */
export function cloneStyle(s: Style): Style {
  const paint = (p: Paint): Paint => (isGradient(p) ? { ...p, stops: p.stops.map((st) => ({ ...st })) } : { ...p });
  return {
    margin: s.margin,
    modules: { ...s.modules, color: paint(s.modules.color) },
    eyes: { ...s.eyes, frameColor: paint(s.eyes.frameColor), ballColor: paint(s.eyes.ballColor) },
    alignment: { ...s.alignment },
    background: { ...s.background, color: paint(s.background.color) },
    border: { ...s.border },
    logo: { ...s.logo },
    label: { ...s.label },
  };
}

/** HIGH when the style has a logo, else MEDIUM. */
export function recommendedEcc(style: Style): Ecc {
  return style.logo.size > 0 ? 'H' : 'M';
}

/* ---- JSON reader --------------------------------------------------------- */

function fail(what: string): never {
  throw new QRKudeError('parse', `parse error: ${what}`);
}

function members(v: JsonValue, key: string): [string, JsonValue][] {
  if (!(v instanceof JsonObject)) fail(`"${key}" must be an object`);
  return v.entries;
}

function num(v: JsonValue, key: string): number {
  if (typeof v !== 'number') fail(`"${key}" must be a number`);
  return v;
}

function bool(v: JsonValue, key: string): boolean {
  if (typeof v !== 'boolean') fail(`"${key}" must be a boolean`);
  return v;
}

function str(v: JsonValue, key: string): string {
  if (typeof v !== 'string') fail(`"${key}" must be a string`);
  return v;
}

function strOrNull(v: JsonValue, key: string): string | null {
  return v === null ? null : str(v, key);
}

/* A color value: string or null (= transparent). */
function colorValue(v: JsonValue, key: string): Color {
  if (v === null) return TRANSPARENT;
  const c = parseColor(str(v, key));
  if (c === null) fail(`invalid color for "${key}"`);
  return c;
}

function paintValue(v: JsonValue, key: string): Paint {
  if (v === null) return { type: 'none' };
  if (typeof v === 'string') return solidPaint(colorValue(v, key));
  const p: GradientPaint = { type: 'linear', angle: 0, cx: 0.5, cy: 0.5, r: 0.75, stops: [] };
  for (const [k, val] of members(v, key)) {
    if (k === 'type') {
      const t = str(val, 'type');
      if (t === 'radial' || t === 'linear') p.type = t;
      else fail(`unknown paint type "${t}"`);
    } else if (k === 'angle') p.angle = num(val, k);
    else if (k === 'cx') p.cx = num(val, k);
    else if (k === 'cy') p.cy = num(val, k);
    else if (k === 'r') p.r = num(val, k);
    else if (k === 'stops') {
      if (!Array.isArray(val)) fail('"stops" must be an array');
      p.stops = [];
      for (const item of val) {
        const st: GradientStop = { offset: 0, color: TRANSPARENT };
        for (const [sk, sv] of members(item, 'stop')) {
          if (sk === 'offset') st.offset = num(sv, sk);
          else if (sk === 'color') st.color = colorValue(sv, sk);
        }
        if (p.stops.length < 8) p.stops.push(st);
      }
    }
  }
  return p;
}

function moduleShape(v: JsonValue): ModuleShape {
  const name = str(v, 'shape');
  if ((MODULE_SHAPES as readonly string[]).indexOf(name) < 0) fail(`unknown module shape "${name}"`);
  return name as ModuleShape;
}

function eyeShape(v: JsonValue, key: string): EyeShape {
  const name = str(v, key);
  if ((EYE_SHAPES as readonly string[]).indexOf(name) < 0) fail(`unknown eye shape "${name}"`);
  return name as EyeShape;
}

function readStyleKey(k: string, v: JsonValue, s: Style): void {
  switch (k) {
    case 'margin':
      s.margin = num(v, k);
      return;
    case 'modules':
      for (const [key, val] of members(v, k)) {
        if (key === 'shape') s.modules.shape = moduleShape(val);
        else if (key === 'size') s.modules.size = num(val, key);
        else if (key === 'roundness') s.modules.roundness = num(val, key);
        else if (key === 'color') s.modules.color = paintValue(val, key);
      }
      return;
    case 'eyes':
      for (const [key, val] of members(v, k)) {
        if (key === 'frame') s.eyes.frame = eyeShape(val, key);
        else if (key === 'ball') s.eyes.ball = eyeShape(val, key);
        else if (key === 'frameColor') s.eyes.frameColor = paintValue(val, key);
        else if (key === 'ballColor') s.eyes.ballColor = paintValue(val, key);
      }
      return;
    case 'alignment':
      if (v === null) {
        s.alignment.frame = s.alignment.ball = null;
        return;
      }
      for (const [key, val] of members(v, k)) {
        if (key === 'frame') s.alignment.frame = val === null ? null : eyeShape(val, key);
        else if (key === 'ball') s.alignment.ball = val === null ? null : eyeShape(val, key);
      }
      return;
    case 'background':
      for (const [key, val] of members(v, k)) {
        if (key === 'color') s.background.color = paintValue(val, key);
        else if (key === 'radius') s.background.radius = num(val, key);
      }
      return;
    case 'border':
      for (const [key, val] of members(v, k)) {
        if (key === 'width') s.border.width = num(val, key);
        else if (key === 'color') s.border.color = colorValue(val, key);
      }
      return;
    case 'logo':
      if (v === null) {
        /* off, and earlier members are forgotten */
        s.logo = defaultStyle().logo;
        return;
      }
      /* a repeated "logo" keeps the size set by the earlier one */
      if (!(s.logo.size > 0)) s.logo.size = 0.2;
      for (const [key, val] of members(v, k)) {
        if (key === 'href') s.logo.href = strOrNull(val, key);
        else if (key === 'size') s.logo.size = num(val, key);
        else if (key === 'margin') s.logo.margin = num(val, key);
        else if (key === 'excavate') s.logo.excavate = bool(val, key);
        else if (key === 'background') s.logo.background = colorValue(val, key);
        else if (key === 'shape') {
          const name = str(val, key);
          if ((LOGO_SHAPES as readonly string[]).indexOf(name) < 0) fail(`unknown logo shape "${name}"`);
          s.logo.shape = name as LogoShape;
        }
      }
      return;
    case 'label':
      if (v === null) {
        s.label = defaultStyle().label;
        return;
      }
      for (const [key, val] of members(v, k)) {
        if (key === 'text') s.label.text = strOrNull(val, key);
        else if (key === 'font') s.label.font = strOrNull(val, key);
        else if (key === 'size') s.label.size = num(val, key);
        else if (key === 'color') s.label.color = colorValue(val, key);
        else if (key === 'bold') s.label.bold = bool(val, key);
        else if (key === 'position') {
          const name = str(val, key);
          if (name !== 'bottom' && name !== 'top') fail(`unknown label position "${name}"`);
          s.label.position = name;
        }
      }
      return;
    default:
      return; /* unknown keys are ignored */
  }
}

/* Integer fields: clamp to [-1000, 1000], then truncate toward zero. */
function int(v: JsonValue, key: string): number {
  const n = num(v, key);
  return Math.trunc(n < -1000 ? -1000 : n > 1000 ? 1000 : n);
}

function readEncode(v: JsonValue, e: EncodeOptions): void {
  for (const [key, val] of members(v, 'encode')) {
    if (key === 'ecc') {
      const level = str(val, key);
      if (level !== 'L' && level !== 'M' && level !== 'Q' && level !== 'H') fail(`invalid ecc "${level}"`);
      e.ecc = level;
    } else if (key === 'minVersion') e.minVersion = int(val, key);
    else if (key === 'maxVersion') e.maxVersion = int(val, key);
    else if (key === 'mask') e.mask = int(val, key);
    else if (key === 'boostEcc') e.boostEcc = bool(val, key);
    else if (key === 'eci') e.eci = bool(val, key);
  }
}

function readSvg(v: JsonValue, o: SvgOptions): void {
  for (const [key, val] of members(v, 'svg')) {
    if (key === 'width') o.width = num(val, key);
    else if (key === 'idPrefix') o.idPrefix = strOrNull(val, key);
  }
}

/** JSON text, or the equivalent plain object. */
export type DocumentInput = string | Record<string, unknown> | JsonObject;

/**
 * Parses a style document: `{"encode": {...}, "style": {...}, "svg": {...}}`
 * or a bare style object (top-level style keys are accepted too). Members
 * apply in document order: a repeated object merges field by field, scalars
 * and paints replace. Missing keys keep their defaults; unknown keys are
 * ignored. Throws {@link QRKudeError} (kind `"parse error"`) on malformed
 * input, including the limits of SPEC 2.0 (number tokens over 400
 * characters, non-finite numbers, nesting deeper than 64).
 */
export function parseDocument(input: DocumentInput | null | undefined): QRDocument {
  const doc: QRDocument = { encode: {}, style: defaultStyle(), svg: {} };
  if (input === null || input === undefined) return doc;
  const root = typeof input === 'string' ? parseJson(input) : fromPlain(input);
  for (const [key, val] of members(root, 'document')) {
    if (key === 'encode') readEncode(val, doc.encode);
    else if (key === 'svg') readSvg(val, doc.svg);
    else if (key === 'style') {
      for (const [k2, v2] of members(val, 'style')) readStyleKey(k2, v2, doc.style);
    } else readStyleKey(key, val, doc.style);
  }
  return doc;
}

/** Parses a style (bare or wrapped in a document) on top of the defaults. */
export function parseStyle(input: DocumentInput | null | undefined): Style {
  return parseDocument(input).style;
}

/* ---- JSON writer --------------------------------------------------------- */

function paintToJson(p: Paint): unknown {
  if (p.type === 'none') return null;
  if (p.type === 'solid') return formatColor(p.color);
  const stops = p.stops.map((s) => ({ offset: s.offset, color: formatColor(s.color) }));
  return p.type === 'linear'
    ? { type: 'linear', angle: p.angle, stops }
    : { type: 'radial', cx: p.cx, cy: p.cy, r: p.r, stops };
}

/** Converts a style back to its JSON object form (round-trips through
 * {@link parseStyle}). */
export function styleToJson(s: Style): Record<string, unknown> {
  const out: Record<string, unknown> = {
    margin: s.margin,
    modules: { shape: s.modules.shape, size: s.modules.size, roundness: s.modules.roundness, color: paintToJson(s.modules.color) },
    eyes: { frame: s.eyes.frame, ball: s.eyes.ball, frameColor: paintToJson(s.eyes.frameColor), ballColor: paintToJson(s.eyes.ballColor) },
    alignment: { frame: s.alignment.frame, ball: s.alignment.ball },
    background: { color: paintToJson(s.background.color), radius: s.background.radius },
    border: { width: s.border.width, color: formatColor(s.border.color) },
    logo:
      s.logo.size > 0
        ? {
            href: s.logo.href,
            size: s.logo.size,
            margin: s.logo.margin,
            shape: s.logo.shape,
            excavate: s.logo.excavate,
            background: (s.logo.background & 255) === 0 ? null : formatColor(s.logo.background),
          }
        : null,
    label: s.label.text
      ? {
          text: s.label.text,
          font: s.label.font,
          size: s.label.size,
          color: formatColor(s.label.color),
          position: s.label.position,
          bold: s.label.bold,
        }
      : null,
  };
  return out;
}
