/*
 * QRKude - SVG writer. The output is byte-identical across all ports.
 * Port of core/src/qrk_svg.c.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { QRCode, utf8 } from './encode.js';
import { Layer, LayoutContext, PathSink, emitLayerFor, gradientGeometryFor } from './layout.js';
import { Color, Paint, Style, SvgOptions, colorHex, isGradient, solidPaint } from './style.js';

/**
 * Formats a number the way every QRKude port does: rounded half up to at most
 * 3 decimals, trailing zeros removed (`0.0625 -> "0.063"`, `-0.0625 -> "-0.062"`).
 * NaN and values with `|v| >= 1e15` format as `"0"`.
 */
export function formatNumber(value: number): string {
  if (!(value > -1e15 && value < 1e15)) value = 0;
  let n = Math.floor(value * 1000.0 + 0.5);
  let out = '';
  if (n < 0) {
    out = '-';
    n = -n;
  }
  const fp = n % 1000;
  const ip = (n - fp) / 1000;
  out += String(ip);
  if (fp) {
    out += '.' + Math.floor(fp / 100);
    if (fp % 100) {
      out += Math.floor(fp / 10) % 10;
      if (fp % 10) out += fp % 10;
    }
  }
  return out;
}

/** Escapes `& < > "` for SVG text and attribute values and drops control
 * characters other than tab, LF and CR (SPEC 8.3). Non-string input is
 * converted with String() first. */
export function escapeXml(value: string): string {
  const s = String(value);
  let out = '';
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '&') out += '&amp;';
    else if (c === '<') out += '&lt;';
    else if (c === '>') out += '&gt;';
    else if (c === '"') out += '&quot;';
    else if (c >= ' ' || c === '\t' || c === '\n' || c === '\r') out += c;
  }
  return out;
}

/** Path sink that writes SVG path data (`M`, `H`, `V`, `L`, `A`, `Z`). */
export class SvgPathWriter implements PathSink {
  private parts: string[] = [];
  private lx = 0;
  private ly = 0;

  moveTo(x: number, y: number): void {
    this.parts.push('M' + formatNumber(x) + ' ' + formatNumber(y));
    this.lx = x;
    this.ly = y;
  }

  lineTo(x: number, y: number): void {
    if (y === this.ly) this.parts.push('H' + formatNumber(x));
    else if (x === this.lx) this.parts.push('V' + formatNumber(y));
    else this.parts.push('L' + formatNumber(x) + ' ' + formatNumber(y));
    this.lx = x;
    this.ly = y;
  }

  arcTo(r: number, clockwise: boolean, x: number, y: number): void {
    const rs = formatNumber(r);
    this.parts.push('A' + rs + ' ' + rs + (clockwise ? ' 0 0 1 ' : ' 0 0 0 ') + formatNumber(x) + ' ' + formatNumber(y));
    this.lx = x;
    this.ly = y;
  }

  close(): void {
    this.parts.push('Z');
  }

  toString(): string {
    return this.parts.join('');
  }
}

/**
 * Gradient id prefix (SPEC 8.3): every byte of the UTF-8 encoding outside
 * `[A-Za-z0-9_-]` becomes `_`, a leading digit or `-` gets a `q` in front, and
 * an empty or absent prefix is `qrk`. The result can never carry markup.
 */
export function sanitizeIdPrefix(prefix: string | null | undefined): string {
  if (prefix === null || prefix === undefined) return 'qrk';
  const bytes = utf8(String(prefix));
  if (bytes.length === 0) return 'qrk';
  let out = (bytes[0] >= 0x30 && bytes[0] <= 0x39) || bytes[0] === 0x2d ? 'q' : '';
  for (let i = 0; i < bytes.length; i++) {
    const c = bytes[i];
    const ok =
      (c >= 0x61 && c <= 0x7a) || (c >= 0x41 && c <= 0x5a) || (c >= 0x30 && c <= 0x39) || c === 0x5f || c === 0x2d;
    out += ok ? String.fromCharCode(c) : '_';
  }
  return out;
}

function opacityAttr(attr: string, color: Color): string {
  const a = color & 0xff;
  return a !== 255 ? ` ${attr}="${formatNumber(a / 255.0)}"` : '';
}

function fillAttr(p: Paint, prefix: string, id: string): string {
  if (isGradient(p)) return ` fill="url(#${prefix}-${id})"`;
  if (p.type === 'solid') return ` fill="${colorHex(p.color)}"` + opacityAttr('fill-opacity', p.color);
  return ' fill="none"';
}

function gradientElement(L: LayoutContext, layer: Layer, p: Paint, prefix: string, id: string): string {
  if (!isGradient(p)) return '';
  const g = gradientGeometryFor(L, layer, p);
  const linear = p.type === 'linear';
  let out = (linear ? '<linearGradient id="' : '<radialGradient id="') + prefix + '-' + id + '" gradientUnits="userSpaceOnUse"';
  if (linear)
    out += ` x1="${formatNumber(g.x1)}" y1="${formatNumber(g.y1)}" x2="${formatNumber(g.x2)}" y2="${formatNumber(g.y2)}"`;
  else out += ` cx="${formatNumber(g.cx)}" cy="${formatNumber(g.cy)}" r="${formatNumber(g.r)}"`;
  out += '>';
  const count = Math.min(p.stops.length, 8);
  for (let i = 0; i < count; i++) {
    const st = p.stops[i];
    out += `<stop offset="${formatNumber(st.offset)}" stop-color="${colorHex(st.color)}"` + opacityAttr('stop-opacity', st.color) + '/>';
  }
  return out + (linear ? '</linearGradient>' : '</radialGradient>');
}

function layerPath(L: LayoutContext, layer: Layer, paint: Paint, prefix: string, id: string): string {
  const w = new SvgPathWriter();
  if (emitLayerFor(L, layer, w) === 0) return '';
  return '<path d="' + w.toString() + '"' + fillAttr(paint, prefix, id) + '/>\n';
}

/** Writes the SVG document of a code with a style (docs/SPEC.md section 8). */
export function toSvg(code: QRCode, style: Style, options: SvgOptions = {}): string {
  options = options || {};
  const prefix = sanitizeIdPrefix(options.idPrefix);
  const width = typeof options.width === 'number' ? options.width : 0;
  const L = new LayoutContext(code, style);
  let out =
    '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 ' +
    formatNumber(L.W) + ' ' + formatNumber(L.H) + '"';
  if (width > 0) out += ` width="${formatNumber(width)}" height="${formatNumber((width * L.H) / L.W)}"`;
  out += '>\n';

  const bg = style.background.color;
  const fg = style.modules.color;
  const ef = style.eyes.frameColor;
  const eb = style.eyes.ballColor;
  if (isGradient(bg) || isGradient(fg) || isGradient(ef) || isGradient(eb)) {
    out += '<defs>';
    out += gradientElement(L, 'background', bg, prefix, 'bg');
    out += gradientElement(L, 'modules', fg, prefix, 'fg');
    out += gradientElement(L, 'eyeFrames', ef, prefix, 'ef');
    out += gradientElement(L, 'eyeBalls', eb, prefix, 'eb');
    out += '</defs>\n';
  }

  const bgVisible = isGradient(bg) || (bg.type === 'solid' && (bg.color & 0xff) !== 0);
  if (bgVisible) {
    out += `<rect width="${formatNumber(L.W)}" height="${formatNumber(L.H)}"`;
    if (L.bgRadius > 0) out += ` rx="${formatNumber(L.bgRadius)}"`;
    out += fillAttr(bg, prefix, 'bg') + '/>\n';
  }

  out += layerPath(L, 'border', solidPaint(style.border.color), prefix, '');
  out += layerPath(L, 'modules', fg, prefix, 'fg');
  out += layerPath(L, 'eyeFrames', ef.type !== 'none' ? ef : fg, prefix, ef.type !== 'none' ? 'ef' : 'fg');
  out += layerPath(L, 'eyeBalls', eb.type !== 'none' ? eb : fg, prefix, eb.type !== 'none' ? 'eb' : 'fg');
  out += layerPath(L, 'logoBackground', solidPaint(style.logo.background), prefix, '');

  if (L.hasLogo && style.logo.href && String(style.logo.href)) {
    out +=
      `<image x="${formatNumber(L.logoCx - L.logoHalf)}" y="${formatNumber(L.logoCy - L.logoHalf)}"` +
      ` width="${formatNumber(2 * L.logoHalf)}" height="${formatNumber(2 * L.logoHalf)}"` +
      ` preserveAspectRatio="xMidYMid meet" xlink:href="${escapeXml(style.logo.href)}"/>\n`;
  }

  if (L.hasLabel) {
    const f = L.labelSize;
    out +=
      `<text x="${formatNumber(L.labelX)}" y="${formatNumber(L.labelBaseline)}" text-anchor="middle" font-family="` +
      escapeXml(style.label.font ? style.label.font : 'sans-serif') +
      `" font-size="${formatNumber(f)}"` +
      (style.label.bold ? ' font-weight="bold"' : '') +
      ` fill="${colorHex(style.label.color)}"` +
      opacityAttr('fill-opacity', style.label.color) +
      '>' +
      escapeXml(style.label.text as string) +
      '</text>\n';
  }

  return out + '</svg>\n';
}
