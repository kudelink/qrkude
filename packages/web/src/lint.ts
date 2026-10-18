/*
 * QRKude - scannability checks. Port of core/src/qrk_lint.c.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { QRCode } from './encode.js';
import { LayoutContext, ballShape, layerPaint, logoDamage } from './layout.js';
import { Color, Paint, Style, TRANSPARENT } from './style.js';

/** Lint flags, in the order they are reported. */
export const LINT_FLAGS = [
  'logo-risky',
  'logo-unreadable',
  'low-contrast',
  'inverted',
  'transparent-background',
  'quiet-zone',
  'small-modules',
  'eye-shapes',
  'invisible',
] as const;
export type LintFlag = (typeof LINT_FLAGS)[number];

/** Human-readable explanation of each lint flag. */
export const LINT_MESSAGES: Record<LintFlag, string> = {
  'logo-risky': 'The logo hides a lot of data; some scanners may fail. Make it smaller or raise the error correction.',
  'logo-unreadable': 'The logo hides more data than error correction can recover: this code will not scan.',
  'low-contrast': 'Foreground and background colors are too similar (contrast below 3:1).',
  inverted: 'Light modules on a dark background: many scanners cannot read inverted codes.',
  'transparent-background': 'The background is transparent: make sure the code is placed on a light surface.',
  'quiet-zone': 'The margin is below 2 modules; scanners need a quiet zone around the code.',
  'small-modules': 'Modules smaller than 60% of a cell are hard to read from a distance.',
  'eye-shapes': 'This eye frame / ball combination can confuse finder detection.',
  invisible: 'The modules or the eyes have no visible color (none, or fully transparent).',
};

function channel(v: number): number {
  return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
}

const WEIGHT = [0.2126, 0.7152, 0.0722];

/* WCAG relative luminance of `c` composited over the opaque color `under`. */
function luminanceOver(c: Color, under: Color): number {
  const a = (c & 0xff) / 255.0;
  let l = 0;
  for (let i = 0; i < 3; i++) {
    const shift = 24 - 8 * i;
    const v = ((c >>> shift) & 0xff) / 255.0;
    const u = ((under >>> shift) & 0xff) / 255.0;
    l += WEIGHT[i] * channel(v * a + u * (1 - a));
  }
  return l;
}

/* The colors a paint can produce: its color or its (first 8) stops. A
 * missing paint is one fully transparent color. */
function paintColors(p: Paint): Color[] {
  if (p.type === 'solid') return [p.color];
  if ((p.type === 'linear' || p.type === 'radial') && p.stops.length > 0)
    return p.stops.slice(0, 8).map((st) => st.color);
  return [TRANSPARENT];
}

/* Composited over white: floor(c*a + 255*(1-a) + 0.5) per channel, alpha 255. */
function opaqueOverWhite(c: Color): Color {
  const a = (c & 0xff) / 255.0;
  let out = 0xff;
  for (let shift = 8; shift <= 24; shift += 8) {
    const v = ((c >>> shift) & 0xff) * a + 255 * (1 - a);
    out |= Math.floor(v + 0.5) << shift;
  }
  return out >>> 0;
}

/** Returns the scannability warnings of a code with a style (docs/SPEC.md section 11). */
export function lint(code: QRCode, style: Style): LintFlag[] {
  const flags = new Set<LintFlag>();
  const L = new LayoutContext(code, style);

  if (L.hasLogo) {
    const d = logoDamage(code, style);
    if (d > 1) flags.add('logo-unreadable');
    else if (d > 0.7) flags.add('logo-risky');
  }

  /* background colors, made opaque over white (what a page usually shows) */
  const original = paintColors(style.background.color);
  if (original.every((c) => (c & 0xff) === 0)) flags.add('transparent-background');
  const bg = original.map(opaqueOverWhite);
  let bgLo = 1;
  let bgHi = 0;
  for (const b of bg) {
    const l = luminanceOver(b, b);
    if (l < bgLo) bgLo = l;
    if (l > bgHi) bgHi = l;
  }

  /* foreground: modules and eyes composited over every background color */
  let fgLo = 1;
  let fgHi = 0;
  for (const layer of ['modules', 'eyeFrames', 'eyeBalls'] as const) {
    let visible = false;
    for (const f of paintColors(layerPaint(style, layer))) {
      if (f & 0xff) visible = true;
      for (const b of bg) {
        const l = luminanceOver(f, b);
        if (l < fgLo) fgLo = l;
        if (l > fgHi) fgHi = l;
      }
    }
    if (!visible) flags.add('invisible');
  }
  let contrast: number;
  if ((fgLo + fgHi) / 2 > (bgLo + bgHi) / 2) {
    flags.add('inverted');
    contrast = (fgLo + 0.05) / (bgHi + 0.05);
  } else {
    contrast = (bgLo + 0.05) / (fgHi + 0.05);
  }
  if (contrast < 3) flags.add('low-contrast');

  if (L.margin < 2) flags.add('quiet-zone');

  switch (L.shape) {
    case 'square':
    case 'dot':
    case 'rounded':
    case 'diamond':
    case 'star':
    case 'hbars':
    case 'vbars':
      if (L.size < 0.6) flags.add('small-modules');
      break;
    default:
      break;
  }

  /* Measured with zxing: diamond frames break finder detection with any
   * ball; diamond balls only work inside round or beveled frames. */
  const frame = style.eyes.frame;
  const ball = ballShape(style.eyes.ball);
  if (frame === 'diamond' || (ball === 'diamond' && (frame === 'square' || frame === 'leaf'))) flags.add('eye-shapes');

  return LINT_FLAGS.filter((f) => flags.has(f));
}
