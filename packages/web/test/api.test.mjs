// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
//
// API behaviour beyond the conformance vectors.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import * as Q from '../dist/index.mjs';

const renderCases = JSON.parse(
  readFileSync(new URL('../../../spec/vectors/render.json', import.meta.url), 'utf8'),
);

test('formatNumber follows SPEC 8.1', () => {
  const cases = [
    [0.0625, '0.063'], [-0.0625, '-0.062'], [-0.0001, '0'], [0, '0'], [-0, '0'], [12, '12'],
    [1.5, '1.5'], [2.52, '2.52'], [0.001, '0.001'], [-3.25, '-3.25'], [1234567.8915, '1234567.892'],
    [0.0005, '0.001'], [-0.0005, '0'], [29.999999, '30'],
    [NaN, '0'], [Infinity, '0'], [-Infinity, '0'], [1e15, '0'], [-1e15, '0'], [1e14, '100000000000000'],
  ];
  for (const [v, s] of cases) assert.equal(Q.formatNumber(v), s, String(v));
});

test('parseStyle without input returns the defaults', () => {
  assert.deepEqual(Q.parseStyle(null), Q.defaultStyle());
  assert.deepEqual(Q.parseStyle('{}'), Q.defaultStyle());
  assert.deepEqual(Q.parseStyle({ style: {} }), Q.defaultStyle());
});

test('top-level and wrapped style keys are equivalent', () => {
  const a = Q.parseStyle({ margin: 2, modules: { shape: 'dot' } });
  const b = Q.parseStyle({ style: { margin: 2, modules: { shape: 'dot' } } });
  assert.deepEqual(a, b);
  assert.equal(a.modules.shape, 'dot');
});

test('reader matches the reference number syntax', () => {
  assert.equal(Q.parseStyle('{"margin": .5}').margin, 0.5);
  assert.equal(Q.parseStyle('{"margin": 2.}').margin, 2);
  assert.equal(Q.parseStyle('{"margin": 25e-1}').margin, 2.5);
  assert.equal(Q.parseStyle('{"margin": -1E+1}').margin, -10);
  assert.equal(Q.parseStyle('{"margin": 0.1}').margin, 0.1);
  assert.equal(Q.parseStyle('{"margin": 123456789012345678901}').margin, 123456789012345678000);
  assert.equal(Q.parseDocument('{"encode": {"minVersion": 2.9}}').encode.minVersion, 2);
});

test('unknown keys are ignored, whatever their length or value', () => {
  const long = 'x'.repeat(80);
  const s = Q.parseStyle(`{"${long}": {"a": [1, null, {"b": true}]}, "modules": {"${long}": "?", "shape": "star"}}`);
  assert.equal(s.modules.shape, 'star');
});

test('malformed documents throw QRKudeError("parse")', () => {
  const bad = [
    '', '[]', '{"margin": "4"}', '{"modules": {"shape": "blob"}}', '{"eyes": {"frame": null}}',
    '{"modules": {"color": "#12345"}}', '{"modules": {"color": {"type": "conic"}}}', '{"encode": {"ecc": "X"}}',
    '{"logo": {"excavate": 1}}', '{"label": {"position": "left"}}', '{"modules": null}', '{"margin": 1',
  ];
  for (const text of bad) {
    assert.throws(() => Q.parseDocument(text), (e) => e instanceof Q.QRKudeError && e.kind === 'parse error', text);
  }
});

test('SPEC 2.0 limits: number length, finiteness, depth', () => {
  const parseErr = (input) => (e) => e instanceof Q.QRKudeError && e.kind === 'parse error';
  assert.equal(Q.parseStyle('{"x": 0.' + '1'.repeat(398) + ', "margin": 1}').margin, 1);
  assert.throws(() => Q.parseStyle('{"x": 0.' + '1'.repeat(399) + '}'), parseErr());
  assert.throws(() => Q.parseStyle('{"x": 1e999}'), parseErr());
  assert.throws(() => Q.parseStyle('{"x": [{"y": -1e999}]}'), parseErr());
  assert.equal(Q.parseStyle('{"x": 1e-999, "margin": 0e99999}').margin, 0);
  assert.equal(Q.parseStyle('{"x": ' + '['.repeat(63) + ']'.repeat(63) + '}').margin, 4);
  assert.throws(() => Q.parseStyle('{"x": ' + '['.repeat(64) + ']'.repeat(64) + '}'), parseErr());
  // plain objects get the same limits (and cycles cannot hang the reader)
  const cyclic = { modules: {} };
  cyclic.modules.self = cyclic;
  assert.throws(() => Q.parseStyle(cyclic), parseErr());
  assert.throws(() => Q.parseStyle({ margin: Infinity }), parseErr());
  assert.throws(() => Q.parseStyle({ future: { x: NaN } }), parseErr());
});

test('hostile numbers finish quickly', () => {
  const t0 = Date.now();
  for (const n of ['1e9999999999999999999999999999999999999', '-1e-99999999999999999', '9'.repeat(400), '0.' + '0'.repeat(397) + '1']) {
    try {
      Q.parseDocument(`{"unknown": ${n}}`);
    } catch (e) {
      assert.equal(e.kind, 'parse error');
    }
  }
  assert.throws(() => Q.parseDocument('{"unknown": 1e' + '9'.repeat(5000) + '}'), (e) => e.kind === 'parse error');
  assert.ok(Date.now() - t0 < 1000);
});

test('integer encode fields clamp to [-1000, 1000] then truncate', () => {
  assert.equal(Q.parseDocument('{"encode": {"minVersion": 1e100}}').encode.minVersion, 1000);
  assert.equal(Q.parseDocument('{"encode": {"mask": -1.7}}').encode.mask, -1);
  assert.equal(Q.parseDocument('{"encode": {"mask": 6.9}}').encode.mask, 6);
  assert.throws(() => Q.svg('x', '{"encode": {"minVersion": 1e100}}'), (e) => e.kind === 'invalid argument');
});

test('document order: objects merge, paints replace, logo/label/alignment null', () => {
  const s = Q.parseStyle('{"modules":{"shape":"dot"},"modules":{"size":0.8}}');
  assert.equal(s.modules.shape, 'dot');
  assert.equal(s.modules.size, 0.8);
  assert.equal(Q.parseStyle('{"logo":{"size":0.3},"logo":{"href":"x.png"}}').logo.size, 0.3);
  assert.equal(Q.parseStyle('{"logo":{"size":0.3},"logo":null,"logo":{}}').logo.size, 0.2);
  assert.equal(Q.parseStyle('{"alignment":{"frame":"circle"},"alignment":null}').alignment.frame, null);
  assert.equal(Q.parseStyle('{"label":{"text":"a"},"label":null}').label.text, null);
  assert.deepEqual(Q.parseStyle('{"logo":{"href":"a","margin":3},"logo":null,"logo":{}}').logo, { ...Q.defaultStyle().logo, size: 0.2 });
  assert.deepEqual(Q.parseStyle('{"label":{"text":"A","size":2},"label":null}').label, Q.defaultStyle().label);
  const nul = Q.parseDocument('{"encode":{"ecc":"L\\u0000xx"},"style":{"modules":{"shape":"dot\\u0000no","color":"#f00\\u0000x"},"label":{"position":"top\\u0000x"}}}');
  assert.equal(nul.encode.ecc, 'L');
  assert.equal(nul.style.modules.shape, 'dot');
  assert.equal(nul.style.modules.color.color, 0xff0000ff);
  assert.equal(nul.style.label.position, 'top');
  const p = Q.parseStyle('{"modules":{"color":{"type":"radial","stops":[{"offset":0,"color":"#f00"}]}},"modules":{"color":{"stops":[]}}}');
  assert.equal(p.modules.color.type, 'linear');
});

test('svg.idPrefix is sanitized (SPEC 8.3)', () => {
  assert.equal(Q.sanitizeIdPrefix(null), 'qrk');
  assert.equal(Q.sanitizeIdPrefix(''), 'qrk');
  assert.equal(Q.sanitizeIdPrefix('ok_-9'), 'ok_-9');
  assert.equal(Q.sanitizeIdPrefix('1abc'), 'q1abc');
  assert.equal(Q.sanitizeIdPrefix('-x'), 'q-x');
  assert.equal(Q.sanitizeIdPrefix('ñ-ü'), '__-__');
  assert.equal(Q.sanitizeIdPrefix('a"b<c>'), 'a_b_c_');
  const evil = 'x"/><svg onload="alert(1)"></svg><linearGradient id="y';
  const out = Q.svg('x', { style: { modules: { color: { stops: [{ offset: 0, color: '#000' }] } } }, svg: { idPrefix: evil } });
  assert.ok(!out.includes(' onload="'), 'no injected attribute');
  assert.ok(out.includes('<linearGradient id="x____svg_onload__alert_1_____svg__linearGradient_id__y-fg"'));
  assert.equal((out.match(/</g) || []).length, (Q.svg('x', { style: { modules: { color: { stops: [{ offset: 0, color: '#000' }] } } } }).match(/</g) || []).length);
});

test('programmatic styles cannot inject markup', () => {
  const style = Q.parseStyle({ logo: { href: 'a.png' }, label: { text: 'x' } });
  style.label.text = ['</text><script>alert(1)</script>'];
  style.label.font = { toString: () => '"><script>' };
  style.logo.href = ['" onload="alert(1)'];
  style.modules.shape = '"><script>';
  style.margin = '"><script>';
  const out = Q.toSvg(Q.encode('x'), style, { idPrefix: '"><script>', width: '"' });
  assert.ok(!out.includes('<script'));
  assert.ok(!out.includes(' onload="'), 'quotes stay escaped');
  assert.ok(out.includes('xlink:href="&quot; onload=&quot;alert(1)"'));
});

test('strings: NUL ends a string, unpaired surrogates become U+FFFD, controls are dropped', () => {
  assert.equal(Q.parseStyle('{"label":{"text":"ab\\u0000cd"}}').label.text, 'ab');
  assert.equal(Q.parseStyle('{"label":{"text":"\\ud800\\u0041|\\udc00|\\ud83d\\ude00|\\ud800\\ud800\\udc00"}}').label.text, '\ufffdA|\ufffd|\u{1f600}|\ufffd\u{10000}');
  assert.equal(Q.parseStyle({ label: { text: 'x\ud800y' } }).label.text, 'x\ufffdy');
  assert.equal(Q.escapeXml('a\u0001b\u001fc\td\ne\rf\u007f'), 'abc\td\ne\rf\u007f');
});

test('geo clamps latitude and longitude', () => {
  assert.equal(Q.geo(95.5, -200), 'geo:90,-180');
  assert.equal(Q.geo(-91, 180.0000001), 'geo:-90,180');
  assert.equal(Q.geo(NaN, NaN), 'geo:-90,-180');
});

test('encode rejects oversized input before doing per-byte work', () => {
  const t0 = Date.now();
  assert.throws(() => Q.encode('x'.repeat(5e7)), (e) => e.kind === 'data too long');
  assert.ok(Date.now() - t0 < 500);
  assert.throws(() => Q.encode('x', { minVersion: 0 }), (e) => e.kind === 'invalid argument');
});

test('null handling follows the reference reader', () => {
  const s = Q.parseStyle({
    background: { color: null },
    eyes: { frameColor: null },
    alignment: { frame: null, ball: null },
    logo: null,
    label: null,
    border: { color: null },
  });
  assert.equal(s.background.color.type, 'none');
  assert.equal(s.eyes.frameColor.type, 'none');
  assert.equal(s.alignment.frame, null);
  assert.equal(s.logo.size, 0);
  assert.equal(s.label.text, null);
  assert.equal(s.border.color, 0);
  // an empty logo object means "logo with defaults"
  assert.equal(Q.parseStyle({ logo: {} }).logo.size, 0.2);
});

test('colors', () => {
  assert.equal(Q.parseColor('#a1c'), 0xaa11ccff);
  assert.equal(Q.parseColor('#A1B2C3'), 0xa1b2c3ff);
  assert.equal(Q.parseColor('#11223380'), 0x11223380);
  assert.equal(Q.parseColor('transparent'), 0);
  assert.equal(Q.parseColor('red'), null);
  assert.equal(Q.parseColor('#12345'), null);
  assert.equal(Q.formatColor(0x11223380), '#11223380');
  assert.equal(Q.formatColor(0xffffffff), '#ffffff');
});

test('styleToJson round-trips every render vector', () => {
  for (const c of renderCases.filter((r) => !r.error)) {
    const doc = Q.parseDocument(c.json !== undefined ? c.json : { encode: c.encode || {}, style: c.style, svg: c.svg || {} });
    const again = Q.parseStyle(JSON.stringify(Q.styleToJson(doc.style)));
    assert.equal(Q.svg(c.text, { ...doc, style: again }), c.output, c.name);
  }
});

test('default ECC: H with a logo, M otherwise, explicit ecc wins', () => {
  assert.equal(Q.generate('hello').code.ecc, 'H'); // boosted: fits at H in version 1
  const long = 'https://kudelink.com/a-rather-long-url-that-needs-version-three';
  assert.equal(Q.generate(long, { encode: { boostEcc: false } }).code.ecc, 'M');
  assert.equal(Q.generate(long, { style: { logo: {} }, encode: { boostEcc: false } }).code.ecc, 'H');
  assert.equal(Q.generate(long, { style: { logo: {} }, encode: { ecc: 'L', boostEcc: false } }).code.ecc, 'L');
});

test('encode accepts bytes and validates options', () => {
  const a = Q.encode('héllo');
  const b = Q.encode(Q.utf8('héllo'));
  assert.deepEqual(a.modules, b.modules);
  assert.equal(a.mode, 'byte');
  assert.equal(Q.encode('12345').mode, 'numeric');
  assert.equal(Q.encode('HELLO WORLD').mode, 'alphanumeric');
  assert.throws(() => Q.encode('x', { minVersion: 0 }), (e) => e.code === 'invalid-argument');
  assert.throws(() => Q.encode('x', { mask: 8 }), (e) => e.code === 'invalid-argument');
  assert.throws(() => Q.encode('x', { ecc: 'Z' }), (e) => e.code === 'invalid-argument');
  assert.throws(() => Q.encode('x'.repeat(3000)), (e) => e.code === 'data-too-long' && e.message === 'data too long');
});

test('layerVisible follows SPEC 8.5', () => {
  const code = Q.encode('visible');
  const vis = (style, layer) => Q.layerVisible(code, Q.parseStyle(style), layer);
  assert.equal(vis({}, 'modules'), true);
  assert.equal(vis({ modules: { color: null } }, 'modules'), false);
  assert.equal(vis({ modules: { color: '#00000000' } }, 'modules'), false);
  assert.equal(vis({ modules: { color: { stops: [] } } }, 'modules'), false);
  assert.equal(vis({ modules: { color: { stops: [{ offset: 0, color: '#00000000' }, { offset: 1, color: '#000000ff' }] } } }, 'modules'), true);
  assert.equal(vis({ background: { color: { type: 'radial', stops: [{ offset: 0, color: '#ffffff00' }] } } }, 'background'), false);
  assert.equal(vis({ modules: { color: null } }, 'eyeFrames'), false);
  assert.equal(vis({ modules: { color: null }, eyes: { frameColor: '#000' } }, 'eyeFrames'), true);
  assert.equal(vis({}, 'border'), false);
  assert.equal(vis({ border: { width: 1 } }, 'border'), true);
  assert.equal(vis({ logo: { background: '#fff' } }, 'logoBackground'), true);
  assert.equal(vis({ logo: null }, 'logoBackground'), false);
});

test('emitLayer feeds a custom path sink', () => {
  const code = Q.encode('sink');
  const style = Q.parseStyle({ modules: { shape: 'dot' } });
  const ops = { M: 0, L: 0, A: 0, Z: 0 };
  const n = Q.emitLayer(code, style, 'modules', {
    moveTo: () => ops.M++,
    lineTo: () => ops.L++,
    arcTo: () => ops.A++,
    close: () => ops.Z++,
  });
  assert.equal(n, ops.M);
  assert.equal(ops.Z, ops.M);
  assert.equal(ops.A, 4 * ops.M);
  assert.equal(Q.emitLayer(code, style, 'modules'), n);
  assert.equal(Q.emitLayer(code, style, 'border'), 0);
});

test('lint messages exist for every flag', () => {
  for (const f of Q.LINT_FLAGS) assert.equal(typeof Q.LINT_MESSAGES[f], 'string');
  assert.equal(Q.LINT_FLAGS[Q.LINT_FLAGS.length - 1], 'invisible');
  const g = Q.generate('x', { style: { modules: { color: '#eeeeee' }, margin: 1 } });
  assert.deepEqual(g.lint, ['low-contrast', 'quiet-zone']);
  // a transparent color shows the background: also low contrast (SPEC 11)
  assert.deepEqual(Q.generate('x', { style: { eyes: { ballColor: '#ff000000' } } }).lint, ['low-contrast', 'invisible']);
});

test('the element entry can be imported outside the browser', async () => {
  const mod = await import('../dist/element.mjs');
  assert.equal(typeof mod.QRKudeElement, 'function');
  assert.equal(typeof mod.defineQRKudeElement, 'function');
});

test('the browser bundle exposes window.QRKude', async () => {
  const { runInNewContext } = await import('node:vm');
  const src = readFileSync(new URL('../dist/qrkude.min.js', import.meta.url), 'utf8');
  const sandbox = { console };
  sandbox.window = sandbox;
  runInNewContext(src, sandbox);
  const c = renderCases.find((r) => r.name === 'eye-paints');
  assert.equal(sandbox.QRKude.svg(c.text, { style: c.style }), c.output);
  assert.equal(typeof sandbox.QRKude.QRKudeElement, 'function');
});
