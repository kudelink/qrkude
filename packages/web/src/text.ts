/*
 * QRKude - text output for terminals and serial monitors (SPEC 14).
 * Port of core/src/qrk_text.c. The output is byte-identical across all ports.
 *
 *   process.stdout.write(toText(encode('https://kudelink.com'), { colors: 'ansi' }));
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { QRCode, QRKudeError } from './encode.js';

/** Text styles (SPEC 14.3, 14.4), in reference order. */
export const TEXT_STYLES = ['half_blocks', 'full_blocks', 'hash'] as const;
export type TextStyle = (typeof TEXT_STYLES)[number];

/** Text color modes (SPEC 14.5), in reference order. */
export const TEXT_COLORS = ['none', 'ansi'] as const;
export type TextColors = (typeof TEXT_COLORS)[number];

/** Text output options (SPEC 14.1). */
export interface TextOptions {
  /** Characters used. Default `"half_blocks"`. */
  style?: TextStyle;
  /** Light modules on each side. Truncated toward zero, then clamped to 0..16; NaN is 0. Default 2. */
  margin?: number;
  /** Swap ink and paper. Default false. */
  invert?: boolean;
  /** `"ansi"` wraps every line in black-on-white color codes. Default `"none"`. */
  colors?: TextColors;
}

/** Size of the text output in character cells. */
export interface TextSize {
  /** Characters per line. ANSI color codes take none. */
  columns: number;
  /** Number of lines. */
  lines: number;
}

const MARGIN_MAX = 16;

/* Indexed by ink(x, y) + 2 * ink(x, y + 1) for half blocks, by ink(x, y) otherwise. */
const HALF = [' ', '▀', '▄', '█'];
const FULL = ['  ', '██'];
const HASH = ['  ', '##'];

const ANSI_START = '\x1b[30;107m';
const ANSI_END = '\x1b[0m';

interface Resolved {
  half: boolean;
  cells: readonly string[];
  margin: number;
  invert: boolean;
  ansi: boolean;
}

function invalid(message: string): QRKudeError {
  return new QRKudeError('invalid-argument', `invalid argument: ${message}`);
}

/* The options with defaults applied and the margin clamped. */
function resolve(options: TextOptions | null | undefined): Resolved {
  const o = options || {};
  const style = o.style === undefined || o.style === null ? 'half_blocks' : o.style;
  const colors = o.colors === undefined || o.colors === null ? 'none' : o.colors;
  if (TEXT_STYLES.indexOf(style) < 0) throw invalid("text style must be 'half_blocks', 'full_blocks' or 'hash'");
  if (TEXT_COLORS.indexOf(colors) < 0) throw invalid("text colors must be 'none' or 'ansi'");
  let margin = 2;
  if (o.margin !== undefined && o.margin !== null) {
    if (typeof o.margin !== 'number') throw invalid('text margin must be a number');
    margin = Math.trunc(o.margin);
    if (!(margin > 0)) margin = 0; // NaN, negative and -0
    else if (margin > MARGIN_MAX) margin = MARGIN_MAX;
  }
  return {
    half: style === 'half_blocks',
    cells: style === 'half_blocks' ? HALF : style === 'full_blocks' ? FULL : HASH,
    margin,
    invert: !!o.invert,
    ansi: colors === 'ansi',
  };
}

/**
 * The module grid as text (SPEC 14): one line per two module rows with half
 * blocks, else one line per row. Every line ends with `"\n"`. Throws
 * {@link QRKudeError} (`"invalid argument"`) for an unknown style or colors
 * value.
 */
export function toText(code: QRCode, options: TextOptions = {}): string {
  const o = resolve(options);
  const n = code.size;
  const m = o.margin;
  const ink = (x: number, y: number): number => (code.isDark(x, y) !== o.invert ? 1 : 0);
  let out = '';
  for (let y = -m; y < n + m; y += o.half ? 2 : 1) {
    let line = o.ansi ? ANSI_START : '';
    for (let x = -m; x < n + m; x++) line += o.cells[o.half ? ink(x, y) | (ink(x, y + 1) << 1) : ink(x, y)];
    out += line + (o.ansi ? ANSI_END : '') + '\n';
  }
  return out;
}

/**
 * Columns and lines of {@link toText} with the same options, without building
 * the text. Throws like `toText`.
 */
export function textSize(code: QRCode, options: TextOptions = {}): TextSize {
  const o = resolve(options);
  const side = code.size + 2 * o.margin;
  return o.half ? { columns: side, lines: (side + 1) >> 1 } : { columns: 2 * side, lines: side };
}
