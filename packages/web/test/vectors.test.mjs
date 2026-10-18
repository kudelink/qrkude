// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
//
// Conformance: every case of spec/vectors/{encode,render,payloads}.json must
// be reproduced byte for byte by the built package (dist/index.mjs).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { test } from 'node:test';
import * as Q from '../dist/index.mjs';

const vectors = (name) =>
  JSON.parse(readFileSync(new URL(`../../../spec/vectors/${name}.json`, import.meta.url), 'utf8'));

const encodeCases = vectors('encode');
const renderCases = vectors('render');
const payloadCases = vectors('payloads');

test('vector files are not empty', () => {
  assert.ok(encodeCases.length > 0 && renderCases.length > 0 && payloadCases.length > 0);
});

for (const [i, c] of encodeCases.entries()) {
  const label = `encode #${i} ${JSON.stringify(c.text.length > 40 ? c.text.slice(0, 40) + '...' : c.text)} ${JSON.stringify(c.encode)}`;
  test(label, () => {
    if (c.error) {
      assert.throws(() => Q.encode(c.text, c.encode), (e) => e instanceof Q.QRKudeError && e.message === c.error);
      return;
    }
    const code = Q.encode(c.text, c.encode);
    assert.equal(code.version, c.version, 'version');
    assert.equal(code.ecc, c.ecc, 'ecc');
    assert.equal(code.mask, c.mask, 'mask');
    assert.equal(code.mode, c.mode, 'mode');
    const rows = [];
    for (let y = 0; y < code.size; y++) {
      let row = '';
      for (let x = 0; x < code.size; x++) row += code.isDark(x, y) ? '1' : '0';
      rows.push(row);
    }
    assert.deepEqual(rows, c.matrix);
  });
}

/* The document of a case: the raw `json` text when present (duplicate keys,
 * escapes, limits), else the style / encode / svg objects. */
function docOf(c) {
  if (c.json !== undefined) return c.json;
  const doc = { style: c.style };
  if (c.encode) doc.encode = c.encode;
  if (c.svg) doc.svg = c.svg;
  return doc;
}

const docText = (c) => (c.json !== undefined ? c.json : JSON.stringify(docOf(c)));

for (const c of renderCases) {
  test(`render ${c.name}`, () => {
    if (c.error) {
      // must fail with that kind of error, from parsing or from encoding
      const kind = (fn) => {
        try {
          fn();
        } catch (e) {
          assert.ok(e instanceof Q.QRKudeError, String(e));
          return e.kind;
        }
        return 'no error';
      };
      assert.equal(kind(() => Q.svg(c.text, docText(c))), c.error);
      assert.equal(kind(() => Q.generate(c.text, docText(c))), c.error);
      return;
    }
    // through the JSON text reader (same path as the reference CLI)
    const doc = Q.parseDocument(docText(c));
    const code = Q.encodeForStyle(c.text, doc.encode, doc.style);
    assert.equal(code.version, c.version, 'version');
    assert.equal(code.ecc, c.ecc, 'ecc');
    assert.equal(code.mask, c.mask, 'mask');
    assert.deepEqual(Q.lint(code, doc.style), c.lint, 'lint');
    if (c.logoDamage !== undefined) {
      assert.ok(Math.abs(Q.logoDamage(code, doc.style) - c.logoDamage) <= 1e-6, 'logoDamage');
    }
    assert.equal(Q.toSvg(code, doc.style, doc.svg), c.output);
    // convenience API, from a plain object and from JSON text
    assert.equal(Q.svg(c.text, docOf(c)), c.output, 'svg(text, object)');
    assert.equal(Q.svg(c.text, docText(c)), c.output, 'svg(text, json)');
    const g = Q.generate(c.text, doc);
    assert.equal(g.svg, c.output, 'generate(text, parsed)');
    assert.deepEqual(g.lint, c.lint, 'generate lint');
  });
}

const argOrder = {
  wifi: ['ssid', 'password', 'auth', 'hidden'],
  email: ['to', 'subject', 'body'],
  sms: ['number', 'message'],
  tel: ['number'],
  geo: ['latitude', 'longitude'],
};

for (const [i, c] of payloadCases.entries()) {
  test(`payload #${i} ${c.type}`, () => {
    const fn = Q[c.type];
    assert.equal(typeof fn, 'function');
    const out = argOrder[c.type] ? fn(...argOrder[c.type].map((k) => c.args[k])) : fn(c.args);
    assert.equal(out, c.output);
  });
}

test('CommonJS build exposes the same API and output', () => {
  const require = createRequire(import.meta.url);
  const C = require('../dist/index.cjs');
  for (const key of Object.keys(Q)) assert.ok(key in C, `missing ${key}`);
  const c = renderCases.find((r) => r.name === 'kitchen-sink');
  assert.equal(C.svg(c.text, docOf(c)), c.output);
});
