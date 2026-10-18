/*
 * QRKude - styled QR codes for the web (TypeScript port of the C reference).
 *
 *   const code = encode('https://kudelink.com', { ecc: 'H' });
 *   const style = parseStyle({ modules: { shape: 'fluid' } });
 *   const markup = toSvg(code, style);
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { EncodeOptions, QRCode, encode } from './encode.js';
import { logoDamage } from './layout.js';
import { LintFlag, lint } from './lint.js';
import { DocumentInput, QRDocument, Style, SvgOptions, parseDocument, recommendedEcc } from './style.js';
import { toSvg } from './svg.js';

export const VERSION = '0.1.0';

export {
  ECC_LEVELS,
  QRCode,
  QRKudeError,
  alignmentPositions,
  encode,
  utf8,
} from './encode.js';
export type { Ecc, EncodeOptions, Mode, QRKudeErrorCode, QRKudeErrorKind } from './encode.js';

export {
  EYE_SHAPES,
  LOGO_SHAPES,
  MODULE_SHAPES,
  TRANSPARENT,
  cloneStyle,
  colorAlpha,
  colorHex,
  defaultStyle,
  formatColor,
  isGradient,
  linearPaint,
  parseColor,
  parseDocument,
  parseStyle,
  radialPaint,
  recommendedEcc,
  rgba,
  solidPaint,
  styleToJson,
} from './style.js';
export type {
  Color,
  DocumentInput,
  EyeShape,
  GradientPaint,
  GradientStop,
  LabelPosition,
  LogoShape,
  ModuleShape,
  NonePaint,
  Paint,
  QRDocument,
  SolidPaint,
  Style,
  SvgOptions,
} from './style.js';

export {
  LAYERS,
  MAX_LENGTH,
  ballShape,
  emitLayer,
  getLayout,
  gradientGeometry,
  layerPaint,
  layerVisible,
  logoDamage,
} from './layout.js';
export type { GradientGeometry, Layer, Layout, PathSink } from './layout.js';

export { SvgPathWriter, escapeXml, formatNumber, sanitizeIdPrefix, toSvg } from './svg.js';
export { LINT_FLAGS, LINT_MESSAGES, lint } from './lint.js';
export type { LintFlag } from './lint.js';
export { email, event, geo, sms, tel, vcard, wifi } from './payload.js';
export type { CalendarEvent, VCard } from './payload.js';

export {
  CanvasPathSink,
  cssColor,
  layerToCanvasPath,
  loadImage,
  renderToCanvas,
  toBlob,
  toDataURL,
} from './canvas.js';
export type {
  Canvas2DContext,
  CanvasGradientLike,
  CanvasImage,
  CanvasLike,
  CanvasPathTarget,
  CanvasRenderOptions,
  CanvasRenderResult,
  ImageExportOptions,
} from './canvas.js';

/**
 * Encodes with the style's ECC default: when `options.ecc` is absent the
 * level is H if the style has a logo, else M (SPEC 1.3).
 */
export function encodeForStyle(
  data: string | Uint8Array | ArrayLike<number>,
  options: EncodeOptions | null | undefined,
  style: Style,
): QRCode {
  const o = options || {};
  return encode(data, { ...o, ecc: o.ecc !== undefined && o.ecc !== null ? o.ecc : recommendedEcc(style) });
}

/** Everything produced for one payload and document. */
export interface Generated {
  code: QRCode;
  style: Style;
  svgOptions: SvgOptions;
  /** The SVG document. */
  svg: string;
  lint: LintFlag[];
  logoDamage: number;
}

/* A parsed document has numeric colors where JSON input can only have strings. */
function isParsed(doc: unknown): doc is QRDocument {
  const d = doc as { style?: { border?: { color?: unknown }; logo?: { background?: unknown }; label?: { color?: unknown } } };
  const s = d && typeof d === 'object' ? d.style : undefined;
  return (
    !!s &&
    typeof s === 'object' &&
    typeof (s.border && s.border.color) === 'number' &&
    typeof (s.logo && s.logo.background) === 'number' &&
    typeof (s.label && s.label.color) === 'number'
  );
}

function resolve(doc: DocumentInput | QRDocument | null | undefined): QRDocument {
  if (isParsed(doc)) return { encode: doc.encode || {}, style: doc.style, svg: doc.svg || {} };
  return parseDocument(doc as DocumentInput);
}

/**
 * Encodes `text` with the document's `encode` options (ECC defaults to H when
 * the style has a logo, else M) and renders it.
 */
export function generate(text: string | Uint8Array, doc?: DocumentInput | QRDocument | null): Generated {
  const d = resolve(doc);
  const code = encodeForStyle(text, d.encode, d.style);
  return {
    code,
    style: d.style,
    svgOptions: d.svg,
    svg: toSvg(code, d.style, d.svg),
    lint: lint(code, d.style),
    logoDamage: logoDamage(code, d.style),
  };
}

/** One-call SVG: `svg('https://kudelink.com', { style: { modules: { shape: 'dot' } } })`. */
export function svg(text: string | Uint8Array, doc?: DocumentInput | QRDocument | null): string {
  const d = resolve(doc);
  return toSvg(encodeForStyle(text, d.encode, d.style), d.style, d.svg);
}
