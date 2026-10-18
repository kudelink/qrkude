/*
 * QRKude - QR Code encoder (ISO/IEC 18004, model 2, versions 1-40).
 * Port of core/src/qrk_encode.c; the module matrices are identical.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */

/** Error correction level, from lowest (~7% recovery) to highest (~30%). */
export type Ecc = 'L' | 'M' | 'Q' | 'H';

/** Data mode of the single segment. */
export type Mode = 'numeric' | 'alphanumeric' | 'byte';

/** Encoder options (the `encode` object of a style document). */
export interface EncodeOptions {
  /** Minimum error correction level. Default `"M"`. */
  ecc?: Ecc;
  /** Smallest version to try, 1..40. Default 1. */
  minVersion?: number;
  /** Largest version to try, 1..40. Default 40. */
  maxVersion?: number;
  /** -1 = automatic (default), 0..7 = forced mask. */
  mask?: number;
  /** Raise the ECC level while the chosen version still fits. Default true. */
  boostEcc?: boolean;
  /** Prepend an ECI designator 26 (UTF-8). Default false. */
  eci?: boolean;
}

/** Error codes thrown as {@link QRKudeError}. */
export type QRKudeErrorCode = 'invalid-argument' | 'data-too-long' | 'parse';

/** The cross-port error kinds of SPEC 2.0. */
export type QRKudeErrorKind = 'parse error' | 'invalid argument' | 'data too long';

const KINDS: Record<QRKudeErrorCode, QRKudeErrorKind> = {
  parse: 'parse error',
  'invalid-argument': 'invalid argument',
  'data-too-long': 'data too long',
};

export class QRKudeError extends Error {
  readonly code: QRKudeErrorCode;
  /** `"parse error"`, `"invalid argument"` or `"data too long"`. */
  readonly kind: QRKudeErrorKind;
  constructor(code: QRKudeErrorCode, message: string) {
    super(message);
    this.name = 'QRKudeError';
    this.code = code;
    this.kind = KINDS[code];
    Object.setPrototypeOf(this, QRKudeError.prototype);
  }
}

/* No mode stores more than 7089 characters (numeric, version 40-L): longer
 * input is rejected before any per-byte work. */
const MAX_PAYLOAD_CHARS = 7089;

export const ECC_LEVELS: readonly Ecc[] = ['L', 'M', 'Q', 'H'];
const MODE_BITS: Record<Mode, number> = { numeric: 1, alphanumeric: 2, byte: 4 };

/* Index: [ecc][version], ecc in L, M, Q, H order. */
const ECC_CODEWORDS_PER_BLOCK: readonly (readonly number[])[] = [
  [-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
    28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
  [-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
    26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28],
  [-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
    28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
  [-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
    30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
];

const NUM_ECC_BLOCKS: readonly (readonly number[])[] = [
  [-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8,
    8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25],
  [-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16,
    17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49],
  [-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20,
    23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68],
  [-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
    25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81],
];

/* Format information ECC bits for L, M, Q, H. */
const ECC_FORMAT_BITS = [1, 0, 3, 2];

const ALPHANUMERIC_CHARSET = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:';

/** An encoded QR symbol: an immutable `size` x `size` module matrix. */
export class QRCode {
  /** 1..40 */
  readonly version: number;
  /** Modules per side (`4 * version + 17`). */
  readonly size: number;
  /** ECC level actually used (after boosting). */
  readonly ecc: Ecc;
  /** Mask pattern 0..7. */
  readonly mask: number;
  /** Data mode of the payload. */
  readonly mode: Mode;
  /** Row-major modules, 1 = dark. */
  readonly modules: Uint8Array;

  constructor(version: number, ecc: Ecc, mask: number, mode: Mode, modules: Uint8Array) {
    this.version = version;
    this.size = version * 4 + 17;
    this.ecc = ecc;
    this.mask = mask;
    this.mode = mode;
    this.modules = modules;
  }

  /** true = dark module. Out-of-range coordinates return false. */
  isDark(x: number, y: number): boolean {
    const n = this.size;
    if (x < 0 || y < 0 || x >= n || y >= n) return false;
    return this.modules[y * n + x] === 1;
  }

  /** Rows of booleans (true = dark). */
  toMatrix(): boolean[][] {
    const rows: boolean[][] = [];
    for (let y = 0; y < this.size; y++) {
      const row: boolean[] = [];
      for (let x = 0; x < this.size; x++) row.push(this.modules[y * this.size + x] === 1);
      rows.push(row);
    }
    return rows;
  }
}

/* ---- tables -------------------------------------------------------------- */

/** @internal */
export function eccIndex(ecc: Ecc): number {
  return ECC_LEVELS.indexOf(ecc);
}

/** @internal */
export function eccCodewordsPerBlock(ecc: number, version: number): number {
  return ECC_CODEWORDS_PER_BLOCK[ecc][version];
}

/** @internal */
export function numEccBlocks(ecc: number, version: number): number {
  return NUM_ECC_BLOCKS[ecc][version];
}

function rawDataModules(ver: number): number {
  let result = (16 * ver + 128) * ver + 64;
  if (ver >= 2) {
    const numAlign = Math.floor(ver / 7) + 2;
    result -= (25 * numAlign - 10) * numAlign - 55;
    if (ver >= 7) result -= 36;
  }
  return result;
}

/** @internal */
export function rawCodewords(version: number): number {
  return Math.floor(rawDataModules(version) / 8);
}

function dataCodewords(ver: number, ecc: number): number {
  return rawCodewords(ver) - ECC_CODEWORDS_PER_BLOCK[ecc][ver] * NUM_ECC_BLOCKS[ecc][ver];
}

/** Alignment pattern center coordinates for a version (empty for version 1). */
export function alignmentPositions(ver: number): number[] {
  if (ver === 1) return [];
  const numAlign = Math.floor(ver / 7) + 2;
  const step = Math.floor((ver * 8 + numAlign * 3 + 5) / (numAlign * 4 - 4)) * 2;
  const out: number[] = new Array(numAlign);
  for (let i = numAlign - 1, pos = ver * 4 + 10; i >= 1; i--, pos -= step) out[i] = pos;
  out[0] = 6;
  return out;
}

function isFunctionModuleSlow(ver: number, x: number, y: number, align: number[]): boolean {
  const size = ver * 4 + 17;
  if (x <= 8 && y <= 8) return true;
  if (x >= size - 8 && y <= 8) return true;
  if (x <= 8 && y >= size - 8) return true;
  if (x === 6 || y === 6) return true;
  if (ver >= 7) {
    if (x >= size - 11 && x < size - 8 && y < 6) return true;
    if (y >= size - 11 && y < size - 8 && x < 6) return true;
  }
  const n = align.length;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      if ((i === 0 && j === 0) || (i === 0 && j === n - 1) || (i === n - 1 && j === 0)) continue;
      const dx = x - align[j];
      const dy = y - align[i];
      if (dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2) return true;
    }
  }
  return false;
}

const functionMaps: (Uint8Array | undefined)[] = [];

/** @internal Function-module map of a version (1 = finder, timing, format, ...). */
export function functionMap(ver: number): Uint8Array {
  let map = functionMaps[ver];
  if (!map) {
    const size = ver * 4 + 17;
    const align = alignmentPositions(ver);
    map = new Uint8Array(size * size);
    for (let y = 0; y < size; y++)
      for (let x = 0; x < size; x++) map[y * size + x] = isFunctionModuleSlow(ver, x, y, align) ? 1 : 0;
    functionMaps[ver] = map;
  }
  return map;
}

/* ---- Reed-Solomon over GF(2^8 / 0x11D) ---------------------------------- */

function gfMul(x: number, y: number): number {
  let z = 0;
  for (let i = 7; i >= 0; i--) {
    z = ((z << 1) ^ ((z >>> 7) * 0x11d)) & 0x1ff;
    z ^= ((y >>> i) & 1) * x;
  }
  return z & 0xff;
}

function rsDivisor(degree: number): Uint8Array {
  const result = new Uint8Array(degree);
  result[degree - 1] = 1;
  let root = 1;
  for (let i = 0; i < degree; i++) {
    for (let j = 0; j < degree; j++) {
      result[j] = gfMul(result[j], root);
      if (j + 1 < degree) result[j] ^= result[j + 1];
    }
    root = gfMul(root, 0x02);
  }
  return result;
}

function rsRemainder(data: Uint8Array, start: number, len: number, divisor: Uint8Array): Uint8Array {
  const degree = divisor.length;
  const result = new Uint8Array(degree);
  for (let i = 0; i < len; i++) {
    const factor = data[start + i] ^ result[0];
    result.copyWithin(0, 1);
    result[degree - 1] = 0;
    for (let j = 0; j < degree; j++) result[j] ^= gfMul(divisor[j], factor);
  }
  return result;
}

/* ---- data segment -------------------------------------------------------- */

function alnumIndex(c: number): number {
  return c ? ALPHANUMERIC_CHARSET.indexOf(String.fromCharCode(c)) : -1;
}

function detectMode(data: Uint8Array): Mode {
  let numeric = true;
  let alnum = true;
  for (let i = 0; i < data.length; i++) {
    const c = data[i];
    if (c < 0x30 || c > 0x39) numeric = false;
    if (c >= 0x80 || alnumIndex(c) < 0) alnum = false;
  }
  if (numeric) return 'numeric';
  if (alnum) return 'alphanumeric';
  return 'byte';
}

function charCountBits(mode: Mode, ver: number): number {
  const i = Math.floor((ver + 7) / 17);
  if (mode === 'numeric') return [10, 12, 14][i];
  if (mode === 'alphanumeric') return [9, 11, 13][i];
  return [8, 16, 16][i];
}

function payloadBits(mode: Mode, len: number): number {
  if (mode === 'numeric') return Math.floor(len / 3) * 10 + [0, 4, 7][len % 3];
  if (mode === 'alphanumeric') return Math.floor(len / 2) * 11 + (len % 2) * 6;
  return len * 8;
}

/* Total bit length for this version, or -1 if the count field overflows. */
function totalBits(mode: Mode, len: number, eci: boolean, ver: number): number {
  let bits = eci ? 12 : 0;
  if (len > 0) {
    const ccb = charCountBits(mode, ver);
    if (len >= 1 << ccb) return -1;
    bits += 4 + ccb + payloadBits(mode, len);
  }
  return bits;
}

class BitBuffer {
  bits = 0;
  constructor(readonly buf: Uint8Array) {}
  append(value: number, n: number): void {
    for (let i = n - 1; i >= 0; i--, this.bits++) {
      if ((value >>> i) & 1) this.buf[this.bits >> 3] |= 0x80 >> (this.bits & 7);
    }
  }
}

/* ---- function patterns --------------------------------------------------- */

class Matrix {
  readonly m: Uint8Array;
  constructor(readonly size: number) {
    this.m = new Uint8Array(size * size);
  }
  set(x: number, y: number, dark: boolean | number): void {
    this.m[y * this.size + x] = dark ? 1 : 0;
  }
  setChecked(x: number, y: number, dark: boolean): void {
    if (x >= 0 && y >= 0 && x < this.size && y < this.size) this.set(x, y, dark);
  }
}

function drawFormatBits(qr: Matrix, ecc: number, mask: number): void {
  const data = (ECC_FORMAT_BITS[ecc] << 3) | mask;
  let rem = data;
  for (let i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
  const bits = ((data << 10) | rem) ^ 0x5412;
  const size = qr.size;

  for (let i = 0; i <= 5; i++) qr.set(8, i, (bits >>> i) & 1);
  qr.set(8, 7, (bits >>> 6) & 1);
  qr.set(8, 8, (bits >>> 7) & 1);
  qr.set(7, 8, (bits >>> 8) & 1);
  for (let i = 9; i < 15; i++) qr.set(14 - i, 8, (bits >>> i) & 1);

  for (let i = 0; i < 8; i++) qr.set(size - 1 - i, 8, (bits >>> i) & 1);
  for (let i = 8; i < 15; i++) qr.set(8, size - 15 + i, (bits >>> i) & 1);
  qr.set(8, size - 8, true);
}

function drawFinder(qr: Matrix, cx: number, cy: number): void {
  for (let dy = -4; dy <= 4; dy++) {
    for (let dx = -4; dx <= 4; dx++) {
      const dist = Math.max(Math.abs(dx), Math.abs(dy));
      qr.setChecked(cx + dx, cy + dy, dist !== 2 && dist !== 4);
    }
  }
}

function drawFunctionPatterns(qr: Matrix, ver: number, ecc: number): void {
  const size = qr.size;
  for (let i = 0; i < size; i++) {
    qr.set(6, i, i % 2 === 0);
    qr.set(i, 6, i % 2 === 0);
  }
  drawFinder(qr, 3, 3);
  drawFinder(qr, size - 4, 3);
  drawFinder(qr, 3, size - 4);

  const pos = alignmentPositions(ver);
  const n = pos.length;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      if ((i === 0 && j === 0) || (i === 0 && j === n - 1) || (i === n - 1 && j === 0)) continue;
      for (let dy = -2; dy <= 2; dy++)
        for (let dx = -2; dx <= 2; dx++)
          qr.set(pos[j] + dx, pos[i] + dy, Math.max(Math.abs(dx), Math.abs(dy)) !== 1);
    }
  }

  drawFormatBits(qr, ecc, 0); /* placeholder, redrawn after masking */

  if (ver >= 7) {
    let rem = ver;
    for (let i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >>> 11) * 0x1f25);
    const bits = (ver << 12) | rem;
    for (let i = 0; i < 18; i++) {
      const bit = ((bits >>> i) & 1) === 1;
      const a = size - 11 + (i % 3);
      const b = Math.floor(i / 3);
      qr.set(a, b, bit);
      qr.set(b, a, bit);
    }
  }
}

function placeCodewords(qr: Matrix, fmap: Uint8Array, data: Uint8Array): void {
  const size = qr.size;
  const total = data.length * 8;
  let i = 0;
  for (let right = size - 1; right >= 1; right -= 2) {
    if (right === 6) right = 5;
    for (let vert = 0; vert < size; vert++) {
      for (let j = 0; j < 2; j++) {
        const x = right - j;
        const upward = ((right + 1) & 2) === 0;
        const y = upward ? size - 1 - vert : vert;
        if (!fmap[y * size + x] && i < total) {
          qr.set(x, y, (data[i >> 3] >>> (7 - (i & 7))) & 1);
          i++;
        }
      }
    }
  }
}

function maskBit(mask: number, x: number, y: number): boolean {
  switch (mask) {
    case 0: return (x + y) % 2 === 0;
    case 1: return y % 2 === 0;
    case 2: return x % 3 === 0;
    case 3: return (x + y) % 3 === 0;
    case 4: return (Math.floor(x / 3) + Math.floor(y / 2)) % 2 === 0;
    case 5: return ((x * y) % 2) + ((x * y) % 3) === 0;
    case 6: return (((x * y) % 2) + ((x * y) % 3)) % 2 === 0;
    default: return (((x + y) % 2) + ((x * y) % 3)) % 2 === 0;
  }
}

function applyMask(qr: Matrix, mask: number, fmap: Uint8Array): void {
  const size = qr.size;
  for (let y = 0; y < size; y++)
    for (let x = 0; x < size; x++) {
      const index = y * size + x;
      if (!fmap[index] && maskBit(mask, x, y)) qr.m[index] ^= 1;
    }
}

/* ---- mask penalty -------------------------------------------------------- */

function runHistoryAdd(run: number, history: number[], size: number): void {
  if (history[0] === 0) run += size; /* light border before the first run */
  for (let i = 6; i > 0; i--) history[i] = history[i - 1];
  history[0] = run;
}

function finderLikeCount(h: number[]): number {
  const n = h[1];
  const core = n > 0 && h[2] === n && h[3] === n * 3 && h[4] === n && h[5] === n;
  return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) + (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
}

function finderTerminate(color: boolean, run: number, history: number[], size: number): number {
  if (color) {
    runHistoryAdd(run, history, size);
    run = 0;
  }
  run += size;
  runHistoryAdd(run, history, size);
  return finderLikeCount(history);
}

function penaltyScore(qr: Matrix): number {
  const size = qr.size;
  const m = qr.m;
  let result = 0;

  for (let pass = 0; pass < 2; pass++) {
    for (let a = 0; a < size; a++) {
      let color = false;
      let run = 0;
      const history = [0, 0, 0, 0, 0, 0, 0];
      for (let b = 0; b < size; b++) {
        const dark = (pass === 0 ? m[a * size + b] : m[b * size + a]) === 1;
        if (dark === color) {
          run++;
          if (run === 5) result += 3;
          else if (run > 5) result++;
        } else {
          runHistoryAdd(run, history, size);
          if (!color) result += finderLikeCount(history) * 40;
          color = dark;
          run = 1;
        }
      }
      result += finderTerminate(color, run, history, size) * 40;
    }
  }

  for (let y = 0; y < size - 1; y++)
    for (let x = 0; x < size - 1; x++) {
      const c = m[y * size + x];
      if (c === m[y * size + x + 1] && c === m[(y + 1) * size + x] && c === m[(y + 1) * size + x + 1])
        result += 3;
    }

  let dark = 0;
  for (let i = 0; i < size * size; i++) dark += m[i];
  const total = size * size;
  let diff = dark * 20 - total * 10;
  if (diff < 0) diff = -diff;
  const k = Math.floor((diff + total - 1) / total) - 1;
  result += k * 10;
  return result;
}

/* ---- text ---------------------------------------------------------------- */

/** UTF-8 bytes of a string (lone surrogates become U+FFFD, like TextEncoder). */
export function utf8(text: string): Uint8Array {
  const out: number[] = [];
  for (let i = 0; i < text.length; i++) {
    let cp = text.charCodeAt(i);
    if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < text.length) {
      const lo = text.charCodeAt(i + 1);
      if (lo >= 0xdc00 && lo <= 0xdfff) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
        i++;
      } else {
        cp = 0xfffd;
      }
    } else if (cp >= 0xd800 && cp <= 0xdfff) {
      cp = 0xfffd;
    }
    if (cp < 0x80) out.push(cp);
    else if (cp < 0x800) out.push(0xc0 | (cp >> 6), 0x80 | (cp & 63));
    else if (cp < 0x10000) out.push(0xe0 | (cp >> 12), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63));
    else
      out.push(0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 63), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63));
  }
  return new Uint8Array(out);
}

/* ---- public API ---------------------------------------------------------- */

function intOption(value: number | undefined, def: number, name: string): number {
  if (value === undefined || value === null) return def;
  if (typeof value !== 'number' || !Number.isInteger(value))
    throw new QRKudeError('invalid-argument', `invalid argument: ${name} must be an integer`);
  return value;
}

/**
 * Encodes text (as UTF-8) or raw bytes. The mode (numeric / alphanumeric /
 * byte) is chosen automatically. Throws {@link QRKudeError} with the message
 * `"data too long"` when no version in range fits.
 */
export function encode(data: string | Uint8Array | ArrayLike<number>, options: EncodeOptions = {}): QRCode {
  if (typeof data !== 'string' && !(data && typeof data.length === 'number'))
    throw new QRKudeError('invalid-argument', 'invalid argument: data must be a string or bytes');
  options = options || {};
  const eccName = options.ecc === undefined || options.ecc === null ? 'M' : options.ecc;
  const minEcc = ECC_LEVELS.indexOf(eccName);
  const minVersion = intOption(options.minVersion, 1, 'minVersion');
  const maxVersion = intOption(options.maxVersion, 40, 'maxVersion');
  const forcedMask = intOption(options.mask, -1, 'mask');
  const boostEcc = options.boostEcc === undefined || options.boostEcc === null ? true : !!options.boostEcc;
  const eci = !!options.eci;
  if (minEcc < 0 || minVersion < 1 || maxVersion > 40 || minVersion > maxVersion || forcedMask < -1 || forcedMask > 7)
    throw new QRKudeError('invalid-argument', 'invalid argument');
  if (data.length > MAX_PAYLOAD_CHARS) throw new QRKudeError('data-too-long', 'data too long');
  const bytes =
    typeof data === 'string' ? utf8(data) : data instanceof Uint8Array ? data : Uint8Array.from(data);

  const len = bytes.length;
  const mode = detectMode(bytes);

  let ver = 0;
  let used = 0;
  for (let v = minVersion; v <= maxVersion; v++) {
    const bits = totalBits(mode, len, eci, v);
    if (bits >= 0 && bits <= dataCodewords(v, minEcc) * 8) {
      ver = v;
      used = bits;
      break;
    }
  }
  if (ver === 0) throw new QRKudeError('data-too-long', 'data too long');

  let ecc = minEcc;
  if (boostEcc) {
    for (let e = ecc + 1; e <= 3; e++) if (used <= dataCodewords(ver, e) * 8) ecc = e;
  }

  /* data codewords */
  const dataLen = dataCodewords(ver, ecc);
  const raw = rawCodewords(ver);
  const bb = new BitBuffer(new Uint8Array(raw));
  if (eci) {
    bb.append(0x7, 4);
    bb.append(26, 8);
  }
  if (len > 0) {
    bb.append(MODE_BITS[mode], 4);
    bb.append(len, charCountBits(mode, ver));
    if (mode === 'numeric') {
      let i = 0;
      for (; i + 3 <= len; i += 3)
        bb.append((bytes[i] - 48) * 100 + (bytes[i + 1] - 48) * 10 + (bytes[i + 2] - 48), 10);
      if (len - i === 2) bb.append((bytes[i] - 48) * 10 + (bytes[i + 1] - 48), 7);
      else if (len - i === 1) bb.append(bytes[i] - 48, 4);
    } else if (mode === 'alphanumeric') {
      let i = 0;
      for (; i + 2 <= len; i += 2) bb.append(alnumIndex(bytes[i]) * 45 + alnumIndex(bytes[i + 1]), 11);
      if (i < len) bb.append(alnumIndex(bytes[i]), 6);
    } else {
      for (let i = 0; i < len; i++) bb.append(bytes[i], 8);
    }
  }
  const capacity = dataLen * 8;
  const term = capacity - bb.bits;
  bb.append(0, term < 4 ? term : 4);
  bb.append(0, (8 - (bb.bits % 8)) % 8);
  for (let pad = 0xec; bb.bits < capacity; pad ^= 0xec ^ 0x11) bb.append(pad, 8);
  const dat = bb.buf;

  /* error correction + interleaving */
  const numBlocks = NUM_ECC_BLOCKS[ecc][ver];
  const blockEccLen = ECC_CODEWORDS_PER_BLOCK[ecc][ver];
  const numShort = numBlocks - (raw % numBlocks);
  const shortLen = Math.floor(raw / numBlocks) - blockEccLen;
  const divisor = rsDivisor(blockEccLen);
  const out = new Uint8Array(raw);
  let offset = 0;
  for (let i = 0; i < numBlocks; i++) {
    const datLen = shortLen + (i < numShort ? 0 : 1);
    const eccBuf = rsRemainder(dat, offset, datLen, divisor);
    for (let j = 0, k = i; j < datLen; j++, k += numBlocks) {
      if (j === shortLen) k -= numShort;
      out[k] = dat[offset + j];
    }
    for (let j = 0, k = dataLen + i; j < blockEccLen; j++, k += numBlocks) out[k] = eccBuf[j];
    offset += datLen;
  }

  /* matrix */
  const qr = new Matrix(ver * 4 + 17);
  const fmap = functionMap(ver);
  drawFunctionPatterns(qr, ver, ecc);
  placeCodewords(qr, fmap, out);

  let mask = forcedMask;
  if (mask < 0) {
    let best = -1;
    for (let m = 0; m < 8; m++) {
      applyMask(qr, m, fmap);
      drawFormatBits(qr, ecc, m);
      const p = penaltyScore(qr);
      if (best < 0 || p < best) {
        best = p;
        mask = m;
      }
      applyMask(qr, m, fmap);
    }
  }
  applyMask(qr, mask, fmap);
  drawFormatBits(qr, ecc, mask);
  return new QRCode(ver, ECC_LEVELS[ecc], mask, mode, qr.m);
}
