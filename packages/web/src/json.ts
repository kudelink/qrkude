/*
 * QRKude - JSON reader with the limits of SPEC 2.0, so that styles from
 * untrusted sources cannot make it do unbounded work:
 *   - numbers become the nearest double; tokens longer than 400 characters
 *     and non-finite values (1e999) are parse errors, wherever they appear;
 *   - nesting deeper than 64 objects/arrays (top-level object = 1) is a
 *     parse error;
 *   - members keep document order and duplicates (the style reader applies
 *     them in order).
 * Like the reference reader it is otherwise lenient (`.5`, unknown escapes,
 * trailing text after the document). A string ends at its first U+0000 and
 * unpaired surrogates become U+FFFD (SPEC 2.0).
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import { QRKudeError } from './encode.js';

/** Object members in document order (duplicates are kept). */
export class JsonObject {
  constructor(readonly entries: [string, JsonValue][]) {}
}

export type JsonValue = null | boolean | number | string | JsonValue[] | JsonObject;

/** Deepest allowed object/array nesting (the top-level object is level 1). */
export const MAX_DEPTH = 64;
/** Longest allowed number token. */
export const MAX_NUMBER_LENGTH = 400;

function parseError(message: string): QRKudeError {
  return new QRKudeError('parse', `parse error: ${message}`);
}

/* SPEC 2.0 strings: a string ends at its first U+0000, and a surrogate that is
 * not part of a valid pair (raw in the input text) becomes U+FFFD. */
function cutNul(s: string): string {
  const z = s.indexOf('\0');
  if (z >= 0) s = s.slice(0, z);
  return s.replace(/[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g, '�');
}

function isDigit(s: string, i: number): boolean {
  const c = s.charCodeAt(i);
  return c >= 0x30 && c <= 0x39;
}

class Reader {
  pos = 0;
  constructor(readonly s: string) {}

  ws(): void {
    const s = this.s;
    while (this.pos < s.length) {
      const c = s.charCodeAt(this.pos);
      if (c !== 0x20 && c !== 0x09 && c !== 0x0a && c !== 0x0d) break;
      this.pos++;
    }
  }

  peek(): string {
    this.ws();
    return this.pos < this.s.length ? this.s[this.pos] : '';
  }

  eat(c: string): boolean {
    if (this.peek() === c) {
      this.pos++;
      return true;
    }
    return false;
  }

  literal(lit: string): boolean {
    this.ws();
    if (this.s.startsWith(lit, this.pos)) {
      this.pos += lit.length;
      return true;
    }
    return false;
  }

  hex4(): number {
    if (this.pos + 4 > this.s.length) throw parseError('bad \\u escape');
    let v = 0;
    for (let i = 0; i < 4; i++) {
      const h = parseInt(this.s[this.pos++], 16);
      if (Number.isNaN(h)) throw parseError('bad \\u escape');
      v = (v << 4) | h;
    }
    return v;
  }

  string(): string {
    if (!this.eat('"')) throw parseError(`string expected at ${this.pos}`);
    const s = this.s;
    let out = '';
    let run = this.pos;
    while (this.pos < s.length) {
      const c = s.charCodeAt(this.pos);
      if (c !== 0x22 && c !== 0x5c) {
        this.pos++;
        continue;
      }
      out += s.slice(run, this.pos);
      this.pos++;
      if (c === 0x22) return cutNul(out);
      if (this.pos >= s.length) break;
      const e = s[this.pos++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          let cp = this.hex4();
          if (cp >= 0xd800 && cp < 0xdc00) {
            /* a high surrogate pairs only with a following low one; otherwise
             * it becomes U+FFFD and the next escape is read on its own */
            let lo = 0;
            if (this.pos + 6 <= s.length && s[this.pos] === '\\' && s[this.pos + 1] === 'u') {
              const hex = s.slice(this.pos + 2, this.pos + 6);
              const v = /^[0-9a-fA-F]{4}$/.test(hex) ? parseInt(hex, 16) : -1;
              if (v >= 0xdc00 && v <= 0xdfff) {
                lo = v;
                this.pos += 6;
              }
            }
            cp = lo ? 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00) : 0xfffd;
          } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            cp = 0xfffd; /* lone low surrogate */
          }
          out += String.fromCodePoint(cp);
          break;
        }
        default: out += e; break;
      }
      run = this.pos;
    }
    throw parseError('unterminated string');
  }

  /* The token is rebuilt in canonical form and converted with Number(),
   * which rounds to the nearest double (as the reference reader does). */
  number(): number {
    this.ws();
    const s = this.s;
    const start = this.pos;
    let neg = false;
    if (this.pos < s.length && s[this.pos] === '-') {
      neg = true;
      this.pos++;
    }
    let p = this.pos;
    while (isDigit(s, this.pos)) this.pos++;
    const intPart = s.slice(p, this.pos);
    let fracPart = '';
    if (this.pos < s.length && s[this.pos] === '.') {
      this.pos++;
      p = this.pos;
      while (isDigit(s, this.pos)) this.pos++;
      fracPart = s.slice(p, this.pos);
    }
    if (!intPart && !fracPart) throw parseError(`number expected at ${start}`);
    let exp = '';
    if (this.pos < s.length && (s[this.pos] === 'e' || s[this.pos] === 'E')) {
      this.pos++;
      let sign = '+';
      if (this.pos < s.length && (s[this.pos] === '+' || s[this.pos] === '-')) sign = s[this.pos++];
      p = this.pos;
      while (isDigit(s, this.pos)) this.pos++;
      if (this.pos > p) exp = 'e' + sign + s.slice(p, this.pos);
    }
    if (this.pos - start > MAX_NUMBER_LENGTH) throw parseError(`number longer than ${MAX_NUMBER_LENGTH} characters`);
    const v = Number((neg ? '-' : '') + (intPart || '0') + (fracPart ? '.' + fracPart : '') + exp);
    if (!isFinite(v)) throw parseError('number is not finite');
    return v;
  }

  value(depth: number): JsonValue {
    const c = this.peek();
    if (c === '"') return this.string();
    if (c === '{' || c === '[') {
      if (depth + 1 > MAX_DEPTH) throw parseError(`nesting deeper than ${MAX_DEPTH}`);
      this.pos++;
      if (c === '{') {
        const entries: [string, JsonValue][] = [];
        if (this.eat('}')) return new JsonObject(entries);
        do {
          const key = this.string();
          if (!this.eat(':')) throw parseError(`':' expected at ${this.pos}`);
          entries.push([key, this.value(depth + 1)]);
        } while (this.eat(','));
        if (!this.eat('}')) throw parseError(`'}' expected at ${this.pos}`);
        return new JsonObject(entries);
      }
      const items: JsonValue[] = [];
      if (this.eat(']')) return items;
      do items.push(this.value(depth + 1));
      while (this.eat(','));
      if (!this.eat(']')) throw parseError(`']' expected at ${this.pos}`);
      return items;
    }
    if (this.literal('true')) return true;
    if (this.literal('false')) return false;
    if (this.literal('null')) return null;
    return this.number();
  }
}

/** Parses a JSON document. Like the reference reader, trailing text after
 * the first value is ignored. */
export function parseJson(text: string): JsonValue {
  return new Reader(String(text)).value(0);
}

/** Converts a plain JavaScript value into the reader's value model, with the
 * same limits (non-finite numbers and nesting deeper than 64 are parse errors,
 * which also rejects cyclic objects). `undefined` members are dropped. */
export function fromPlain(v: unknown, depth = 0): JsonValue {
  if (v === null || v === undefined) return null;
  if (typeof v === 'number') {
    if (!isFinite(v)) throw parseError('number is not finite');
    return v;
  }
  if (typeof v === 'boolean') return v;
  if (typeof v === 'string') return cutNul(v);
  if (v instanceof JsonObject) return v;
  if (typeof v === 'object') {
    if (depth + 1 > MAX_DEPTH) throw parseError(`nesting deeper than ${MAX_DEPTH}`);
    if (Array.isArray(v)) return v.map((item) => fromPlain(item, depth + 1));
    const entries: [string, JsonValue][] = [];
    for (const key of Object.keys(v as object)) {
      const item = (v as Record<string, unknown>)[key];
      if (item !== undefined) entries.push([cutNul(key), fromPlain(item, depth + 1)]);
    }
    return new JsonObject(entries);
  }
  throw parseError(`unsupported value of type ${typeof v}`);
}
