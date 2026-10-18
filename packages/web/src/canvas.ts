/*
 * QRKude - Canvas 2D renderer. Draws exactly the SVG geometry: every layer is
 * built from the shared path emitter into a Path2D and filled (non-zero) in
 * SVG order, with the same gradient geometry.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { QRCode } from './encode.js';
import { Layer, LayoutContext, PathSink, emitLayerFor, gradientGeometryFor, layerPaint } from './layout.js';
import { Color, Paint, Style, isGradient } from './style.js';

/* The types below are structural so that the declarations do not depend on
 * the DOM library; HTMLCanvasElement, OffscreenCanvas and their 2D contexts
 * all satisfy them. */

/** Anything `drawImage` accepts (HTMLImageElement, ImageBitmap, canvas...). */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type CanvasImage = any;

/** The path-building part of `CanvasRenderingContext2D` / `Path2D`. */
export interface CanvasPathTarget {
  moveTo(x: number, y: number): void;
  lineTo(x: number, y: number): void;
  arc(x: number, y: number, radius: number, startAngle: number, endAngle: number, counterclockwise?: boolean): void;
  closePath(): void;
}

export interface CanvasGradientLike {
  addColorStop(offset: number, color: string): void;
}

/** The subset of `CanvasRenderingContext2D` used by the renderer. */
export interface Canvas2DContext extends CanvasPathTarget {
  canvas?: unknown;
  fillStyle: unknown;
  font: string;
  textAlign: string;
  textBaseline: string;
  save(): void;
  restore(): void;
  scale(x: number, y: number): void;
  setTransform(a: number, b: number, c: number, d: number, e: number, f: number): void;
  beginPath(): void;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  fill(...args: any[]): void;
  fillText(text: string, x: number, y: number): void;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  drawImage(image: any, dx: number, dy: number, dw: number, dh: number): void;
  createLinearGradient(x0: number, y0: number, x1: number, y1: number): CanvasGradientLike;
  createRadialGradient(x0: number, y0: number, r0: number, x1: number, y1: number, r1: number): CanvasGradientLike;
}

/** An HTMLCanvasElement or OffscreenCanvas. */
export interface CanvasLike {
  width: number;
  height: number;
  getContext(contextId: '2d'): unknown;
}

export interface CanvasRenderOptions {
  /** Output width in CSS pixels (height follows the canvas aspect ratio).
   * Default: the current canvas width. */
  size?: number;
  /** Logo image, aspect-fitted into the logo box. */
  logo?: CanvasImage | null;
  /** Backing-store scale (e.g. `devicePixelRatio`). Default 1. */
  pixelRatio?: number;
  /** Draw the label with fillText (default true). */
  label?: boolean;
}

export interface CanvasRenderResult {
  /** CSS size of the drawing. */
  width: number;
  height: number;
  /** Device pixels per module. */
  scale: number;
}

/** Path sink that builds a canvas path (`Path2D` or a context) with `arc`. */
export class CanvasPathSink implements PathSink {
  private x = 0;
  private y = 0;
  private sx = 0;
  private sy = 0;

  constructor(readonly target: CanvasPathTarget) {}

  moveTo(x: number, y: number): void {
    this.target.moveTo(x, y);
    this.x = this.sx = x;
    this.y = this.sy = y;
  }

  lineTo(x: number, y: number): void {
    this.target.lineTo(x, y);
    this.x = x;
    this.y = y;
  }

  arcTo(r: number, clockwise: boolean, x: number, y: number, cx: number, cy: number): void {
    const a0 = Math.atan2(this.y - cy, this.x - cx);
    const a1 = Math.atan2(y - cy, x - cx);
    /* y points down: increasing angles run clockwise on screen */
    this.target.arc(cx, cy, r, a0, a1, !clockwise);
    this.x = x;
    this.y = y;
  }

  close(): void {
    this.target.closePath();
    this.x = this.sx;
    this.y = this.sy;
  }
}

/** Appends the geometry of one layer (in canvas units) to a Path2D or a
 * context path. Returns the number of sub-paths. */
export function layerToCanvasPath(target: CanvasPathTarget, code: QRCode, style: Style, layer: Layer): number {
  return emitLayerFor(new LayoutContext(code, style), layer, new CanvasPathSink(target));
}

/** CSS color string of a 0xRRGGBBAA color. */
export function cssColor(c: Color): string {
  const a = (c & 255) / 255;
  return `rgba(${c >>> 24},${(c >>> 16) & 255},${(c >>> 8) & 255},${Math.round(a * 1000) / 1000})`;
}

function fillStyleFor(ctx: Canvas2DContext, L: LayoutContext, layer: Layer, p: Paint): unknown {
  if (p.type === 'none') return null;
  if (p.type === 'solid') return (p.color & 255) === 0 ? null : cssColor(p.color);
  const stops = p.stops.slice(0, 8);
  if (stops.length === 0) return null;
  const g = gradientGeometryFor(L, layer, p);
  if (p.type === 'radial' && !(g.r > 0)) return cssColor(stops[stops.length - 1].color);
  const grad =
    p.type === 'linear'
      ? ctx.createLinearGradient(g.x1, g.y1, g.x2, g.y2)
      : ctx.createRadialGradient(g.cx, g.cy, 0, g.cx, g.cy, g.r);
  /* SVG semantics: offsets clamped to [0, 1] and never decreasing */
  let prev = 0;
  for (const st of stops) {
    let o = st.offset < 0 ? 0 : st.offset > 1 ? 1 : st.offset;
    if (!(o >= prev)) o = prev;
    prev = o;
    grad.addColorStop(o, cssColor(st.color));
  }
  return grad;
}

function fillLayer(ctx: Canvas2DContext, L: LayoutContext, layer: Layer, paint: Paint): void {
  const style = fillStyleFor(ctx, L, layer, paint);
  if (style === null) return;
  const P2D = (globalThis as { Path2D?: new () => CanvasPathTarget }).Path2D;
  if (P2D) {
    const path = new P2D();
    if (emitLayerFor(L, layer, new CanvasPathSink(path)) === 0) return;
    ctx.fillStyle = style;
    ctx.fill(path, 'nonzero');
  } else {
    ctx.beginPath();
    if (emitLayerFor(L, layer, new CanvasPathSink(ctx)) === 0) return;
    ctx.fillStyle = style;
    ctx.fill('nonzero');
  }
}

function imageSize(img: CanvasImage): [number, number] {
  const pick = (...v: unknown[]): number => {
    for (const x of v) {
      if (typeof x === 'number' && x > 0) return x;
      if (x && typeof x === 'object' && 'baseVal' in (x as object)) {
        const bv = (x as { baseVal: { value: number } }).baseVal.value;
        if (bv > 0) return bv;
      }
    }
    return 0;
  };
  const w = pick(img.naturalWidth, img.videoWidth, img.displayWidth, img.codedWidth, img.width);
  const h = pick(img.naturalHeight, img.videoHeight, img.displayHeight, img.codedHeight, img.height);
  return w > 0 && h > 0 ? [w, h] : [1, 1];
}

function isCanvas(t: unknown): t is CanvasLike {
  return !!t && typeof (t as CanvasLike).getContext === 'function';
}

/**
 * Renders a code onto a canvas (resized to `size` x `size * H / W` times the
 * pixel ratio) or into an existing 2D context (drawn at the origin of its
 * current transform). Layers are drawn in SVG order with Path2D; gradients use
 * the exact SVG gradient geometry; the label is drawn with fillText.
 */
export function renderToCanvas(
  target: CanvasLike | Canvas2DContext,
  code: QRCode,
  style: Style,
  options: CanvasRenderOptions = {},
): CanvasRenderResult {
  const L = new LayoutContext(code, style);
  const pr = options.pixelRatio && options.pixelRatio > 0 ? options.pixelRatio : 1;
  let ctx: Canvas2DContext;
  let width: number;
  if (isCanvas(target)) {
    const c = target.getContext('2d') as Canvas2DContext | null;
    if (!c) throw new Error('qrkude: 2D canvas context unavailable');
    ctx = c;
    width = options.size && options.size > 0 ? options.size : target.width / pr;
    const height = (width * L.H) / L.W;
    target.width = Math.round(width * pr);
    target.height = Math.round(height * pr);
    const cssStyle = (target as { style?: { width: string; height: string } }).style;
    if (pr !== 1 && cssStyle) {
      cssStyle.width = `${width}px`;
      cssStyle.height = `${height}px`;
    }
    ctx.setTransform(1, 0, 0, 1, 0, 0);
  } else {
    ctx = target;
    const canvas = ctx.canvas as { width?: number } | undefined;
    width = options.size && options.size > 0 ? options.size : ((canvas && canvas.width) || 256) / pr;
  }
  const height = (width * L.H) / L.W;
  const scale = (width / L.W) * pr;

  ctx.save();
  ctx.scale(scale, scale);

  const bg = style.background.color;
  if (isGradient(bg) || (bg.type === 'solid' && (bg.color & 0xff) !== 0)) fillLayer(ctx, L, 'background', bg);
  fillLayer(ctx, L, 'border', layerPaint(style, 'border'));
  fillLayer(ctx, L, 'modules', layerPaint(style, 'modules'));
  fillLayer(ctx, L, 'eyeFrames', layerPaint(style, 'eyeFrames'));
  fillLayer(ctx, L, 'eyeBalls', layerPaint(style, 'eyeBalls'));
  fillLayer(ctx, L, 'logoBackground', layerPaint(style, 'logoBackground'));

  const logo = options.logo;
  if (L.hasLogo && logo) {
    const box = 2 * L.logoHalf;
    const [iw, ih] = imageSize(logo);
    const k = box / Math.max(iw, ih);
    const w = iw * k;
    const h = ih * k;
    ctx.drawImage(logo, L.logoCx - L.logoHalf + (box - w) / 2, L.logoCy - L.logoHalf + (box - h) / 2, w, h);
  }
  ctx.restore();

  if (L.hasLabel && options.label !== false) {
    /* drawn in device space so that the font is hinted at its final size */
    ctx.save();
    ctx.scale(pr, pr);
    const k = width / L.W;
    const font = style.label.font ? style.label.font : 'sans-serif';
    ctx.font = `${style.label.bold ? 'bold ' : ''}${L.labelSize * k}px ${font}`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = cssColor(style.label.color);
    ctx.fillText(style.label.text as string, L.labelX * k, L.labelBaseline * k);
    ctx.restore();
  }
  return { width, height, scale };
}

/* ---- export helpers ------------------------------------------------------ */

export interface ImageExportOptions extends CanvasRenderOptions {
  /** MIME type, default "image/png". */
  type?: string;
  /** JPEG / WebP quality 0..1. */
  quality?: number;
}

interface AnyCanvas extends CanvasLike {
  toDataURL?(type?: string, quality?: number): string;
  toBlob?(cb: (b: Blob | null) => void, type?: string, quality?: number): void;
  convertToBlob?(opts?: { type?: string; quality?: number }): Promise<Blob>;
}

function createCanvas(): AnyCanvas {
  const g = globalThis as {
    document?: { createElement(tag: string): unknown };
    OffscreenCanvas?: new (w: number, h: number) => unknown;
  };
  if (g.document && typeof g.document.createElement === 'function') return g.document.createElement('canvas') as AnyCanvas;
  if (g.OffscreenCanvas) return new g.OffscreenCanvas(1, 1) as AnyCanvas;
  throw new Error('qrkude: no canvas implementation available');
}

/** Loads an image (URL or data: URI) for use as a canvas logo. */
export function loadImage(src: string): Promise<CanvasImage> {
  const g = globalThis as {
    Image?: new () => {
      crossOrigin: string | null;
      decoding: string;
      onload: (() => void) | null;
      onerror: ((e: unknown) => void) | null;
      src: string;
    };
    fetch?: (url: string) => Promise<{ blob(): Promise<Blob> }>;
    createImageBitmap?: (b: Blob) => Promise<unknown>;
  };
  if (g.Image) {
    const ImageCtor = g.Image;
    return new Promise((resolve, reject) => {
      const img = new ImageCtor();
      if (!/^(data|blob):/i.test(src)) img.crossOrigin = 'anonymous';
      img.decoding = 'async';
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error(`qrkude: cannot load image ${src.slice(0, 64)}`));
      img.src = src;
    });
  }
  if (g.fetch && g.createImageBitmap) {
    const create = g.createImageBitmap;
    return g.fetch(src).then((r) => r.blob()).then((b) => create(b));
  }
  return Promise.reject(new Error('qrkude: cannot load images in this environment'));
}

async function renderForExport(code: QRCode, style: Style, options: ImageExportOptions): Promise<AnyCanvas> {
  let logo = options.logo;
  if (logo === undefined && style.logo.size > 0 && style.logo.href) logo = await loadImage(style.logo.href);
  const canvas = createCanvas();
  renderToCanvas(canvas, code, style, { size: 512, ...options, logo });
  return canvas;
}

/** Renders to a new canvas and returns a data URL (PNG by default). The logo
 * is loaded from `style.logo.href` unless `options.logo` is given (null = none). */
export async function toDataURL(code: QRCode, style: Style, options: ImageExportOptions = {}): Promise<string> {
  const canvas = await renderForExport(code, style, options);
  if (canvas.toDataURL) return canvas.toDataURL(options.type || 'image/png', options.quality);
  const blob = await blobOf(canvas, options);
  const bytes = new Uint8Array(await blob.arrayBuffer());
  let bin = '';
  for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, Array.from(bytes.subarray(i, i + 0x8000)));
  return `data:${blob.type || options.type || 'image/png'};base64,${btoa(bin)}`;
}

function blobOf(canvas: AnyCanvas, options: ImageExportOptions): Promise<Blob> {
  const type = options.type || 'image/png';
  if (canvas.convertToBlob) return canvas.convertToBlob({ type, quality: options.quality });
  return new Promise((resolve, reject) => {
    if (!canvas.toBlob) return reject(new Error('qrkude: canvas cannot export blobs'));
    canvas.toBlob((b) => (b ? resolve(b) : reject(new Error('qrkude: toBlob failed'))), type, options.quality);
  });
}

/** Renders to a new canvas and returns an image Blob (PNG by default). */
export async function toBlob(code: QRCode, style: Style, options: ImageExportOptions = {}): Promise<Blob> {
  return blobOf(await renderForExport(code, style, options), options);
}
