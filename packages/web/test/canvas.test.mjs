// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
//
// The canvas renderer must draw exactly the SVG geometry. A recording Path2D /
// 2D context stands in for the browser: the recorded canvas commands are
// turned back into SVG path data and compared with the conformance vectors.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import * as Q from '../dist/index.mjs';

const renderCases = JSON.parse(
  readFileSync(new URL('../../../spec/vectors/render.json', import.meta.url), 'utf8'),
);

class RecordingPath {
  constructor() {
    this.ops = [];
  }
  moveTo(x, y) {
    this.ops.push(['M', x, y]);
  }
  lineTo(x, y) {
    this.ops.push(['L', x, y]);
  }
  arc(cx, cy, r, a0, a1, ccw) {
    this.ops.push(['A', cx, cy, r, a0, a1, !!ccw]);
  }
  closePath() {
    this.ops.push(['Z']);
  }
}

const fmt = Q.formatNumber;
const TAU = 2 * Math.PI;

/* Canvas commands -> SVG path data, with the SVG writer's H/V/L rules. Arc
 * end points are recomputed from the canvas end angle, as a browser would. */
function toPathData(ops) {
  let d = '';
  let lx = 0;
  let ly = 0;
  for (const op of ops) {
    if (op[0] === 'M') {
      d += `M${fmt(op[1])} ${fmt(op[2])}`;
      [lx, ly] = [op[1], op[2]];
    } else if (op[0] === 'L') {
      const [, x, y] = op;
      // the previous point may be a recomputed arc end (cos/sin, ~1e-16 off)
      if (Math.abs(y - ly) < 1e-9) d += `H${fmt(x)}`;
      else if (Math.abs(x - lx) < 1e-9) d += `V${fmt(y)}`;
      else d += `L${fmt(x)} ${fmt(y)}`;
      [lx, ly] = [x, y];
    } else if (op[0] === 'A') {
      const [, cx, cy, r, , a1, ccw] = op;
      const x = cx + r * Math.cos(a1);
      const y = cy + r * Math.sin(a1);
      d += `A${fmt(r)} ${fmt(r)} 0 0 ${ccw ? 0 : 1} ${fmt(x)} ${fmt(y)}`;
      [lx, ly] = [x, y];
    } else {
      d += 'Z';
    }
  }
  return d;
}

function svgPaths(svg) {
  return [...svg.matchAll(/<path d="([^"]*)"/g)].map((m) => m[1]);
}

function setup(c) {
  const doc = Q.parseDocument(c.json !== undefined ? c.json : { encode: c.encode || {}, style: c.style, svg: c.svg || {} });
  return { code: Q.encodeForStyle(c.text, doc.encode, doc.style), style: doc.style, svgOptions: doc.svg };
}

const PATH_LAYERS = ['border', 'modules', 'eyeFrames', 'eyeBalls', 'logoBackground'];

for (const c of renderCases.filter((r) => !r.error)) {
  test(`canvas path geometry equals SVG geometry: ${c.name}`, () => {
    const { code, style } = setup(c);
    const expected = svgPaths(c.output);
    const got = [];
    for (const layer of PATH_LAYERS) {
      const path = new RecordingPath();
      const count = Q.layerToCanvasPath(path, code, style, layer);
      assert.equal(count, Q.emitLayer(code, style, layer), `${layer} sub-path count`);
      if (count > 0) got.push(toPathData(path.ops));

      // every arc is the short (<= 90 degree) arc in the emitted direction and
      // ends exactly where the shape model says
      const exact = [];
      Q.emitLayer(code, style, layer, {
        moveTo() {},
        lineTo() {},
        arcTo(r, cw, x, y) {
          exact.push([r, cw, x, y]);
        },
        close() {},
      });
      const arcs = path.ops.filter((op) => op[0] === 'A');
      assert.equal(arcs.length, exact.length);
      arcs.forEach(([, cx, cy, r, a0, a1, ccw], i) => {
        const [er, cw, ex, ey] = exact[i];
        assert.equal(r, er);
        assert.equal(ccw, !cw);
        assert.ok(Math.abs(cx + r * Math.cos(a1) - ex) < 1e-9 && Math.abs(cy + r * Math.sin(a1) - ey) < 1e-9);
        let sweep = ccw ? a0 - a1 : a1 - a0;
        sweep = ((sweep % TAU) + TAU) % TAU;
        assert.ok(sweep > 0 && sweep <= Math.PI / 2 + 1e-9, `arc sweep ${sweep}`);
      });
    }
    assert.deepEqual(got, expected);
  });
}

/* Minimal 2D context recording what renderToCanvas paints. */
function recordingContext() {
  const calls = [];
  const ctx = {
    canvas: { width: 290, height: 290 },
    fillStyle: '#000',
    font: '',
    textAlign: 'start',
    textBaseline: 'alphabetic',
    save: () => calls.push(['save']),
    restore: () => calls.push(['restore']),
    scale: (x, y) => calls.push(['scale', x, y]),
    setTransform: (...a) => calls.push(['setTransform', ...a]),
    beginPath: () => calls.push(['beginPath']),
    moveTo() {},
    lineTo() {},
    arc() {},
    closePath() {},
    fill: (path, rule) => calls.push(['fill', ctx.fillStyle, path, rule]),
    fillText: (text, x, y) => calls.push(['fillText', text, x, y, ctx.font, ctx.textAlign, ctx.fillStyle]),
    drawImage: (img, x, y, w, h) => calls.push(['drawImage', img, x, y, w, h]),
    createLinearGradient: (...a) => ({ kind: 'linear', args: a, stops: [], addColorStop(o, col) { this.stops.push([o, col]); } }),
    createRadialGradient: (...a) => ({ kind: 'radial', args: a, stops: [], addColorStop(o, col) { this.stops.push([o, col]); } }),
  };
  return { ctx, calls };
}

test('renderToCanvas paints the SVG layers in order with the SVG gradient geometry', () => {
  globalThis.Path2D = RecordingPath;
  try {
    const c = renderCases.find((r) => r.name === 'kitchen-sink');
    const { code, style } = setup(c);
    const { ctx, calls } = recordingContext();
    const logo = { naturalWidth: 200, naturalHeight: 100 };
    const res = Q.renderToCanvas(ctx, code, style, { size: 400, logo, pixelRatio: 2 });
    const layout = Q.getLayout(code, style);
    assert.equal(res.width, 400);
    assert.equal(res.height, (400 * layout.height) / layout.width);
    assert.equal(res.scale, (400 / layout.width) * 2);

    const fills = calls.filter((k) => k[0] === 'fill');
    // background rect, border, modules, eye frames, eye balls, logo background
    assert.equal(fills.length, 6);
    const expected = svgPaths(c.output);
    assert.deepEqual(fills.slice(1).map((f) => toPathData(f[2].ops)), expected);
    assert.ok(fills.every((f) => f[3] === 'nonzero'));

    // module gradient: same geometry and stops as <linearGradient id="ks-fg">
    const fg = fills[2][1];
    const m = c.output.match(/<linearGradient id="ks-fg"[^>]* x1="([^"]+)" y1="([^"]+)" x2="([^"]+)" y2="([^"]+)">(.*?)<\/linearGradient>/);
    assert.equal(fg.kind, 'linear');
    assert.deepEqual(fg.args.map(fmt), m.slice(1, 5));
    assert.deepEqual(fg.stops.map((s) => fmt(s[0])), [...m[5].matchAll(/offset="([^"]+)"/g)].map((x) => x[1]));
    // eye ball radial gradient
    const eb = fills[4][1];
    const r = c.output.match(/<radialGradient id="ks-eb"[^>]* cx="([^"]+)" cy="([^"]+)" r="([^"]+)">/);
    assert.equal(eb.kind, 'radial');
    assert.deepEqual([eb.args[3], eb.args[4], eb.args[5]].map(fmt), r.slice(1, 4));
    // solid paints
    assert.equal(fills[0][1], Q.cssColor(Q.parseColor('#fdfcdc')));
    assert.equal(fills[3][1], Q.cssColor(Q.parseColor('#0b3d91')));

    // logo: aspect-fitted (2:1) and centered in the <image> box
    const img = c.output.match(/<image x="([^"]+)" y="([^"]+)" width="([^"]+)"/);
    const draw = calls.find((k) => k[0] === 'drawImage');
    const box = layout.logoSize;
    assert.equal(fmt(draw[2]), img[1]);
    assert.equal(fmt(draw[4]), img[3]);
    assert.equal(draw[5], box / 2);
    assert.equal(draw[3], layout.logoY + box / 4);

    // label: centered text at the SVG anchor, scaled to CSS pixels
    const text = calls.find((k) => k[0] === 'fillText');
    const k = 400 / layout.width;
    assert.equal(text[1], 'kudelink.com');
    assert.equal(text[2], layout.labelX * k);
    assert.equal(text[3], layout.labelBaseline * k);
    assert.equal(text[5], 'center');
    assert.match(text[4], /^bold [\d.]+px sans-serif$/);
  } finally {
    delete globalThis.Path2D;
  }
});

test('renderToCanvas skips invisible layers and works without Path2D', () => {
  const c = renderCases.find((r) => r.name === 'background-transparent');
  const { code, style } = setup(c);
  const { ctx, calls } = recordingContext();
  Q.renderToCanvas(ctx, code, style, { size: 100 });
  const fills = calls.filter((k) => k[0] === 'fill');
  // no background, no border: modules, eye frames, eye balls
  assert.equal(fills.length, 3);
  assert.ok(fills.every((f) => f[2] === 'nonzero'));
  assert.ok(calls.filter((k) => k[0] === 'beginPath').length >= 3);
});

test('renderToCanvas resizes a canvas target', () => {
  const c = renderCases.find((r) => r.name === 'label-bottom');
  const { code, style } = setup(c);
  const { ctx } = recordingContext();
  const canvas = { width: 10, height: 10, style: {}, getContext: () => ctx };
  const res = Q.renderToCanvas(canvas, code, style, { size: 300, pixelRatio: 2 });
  assert.equal(canvas.width, 600);
  assert.equal(canvas.height, Math.round(res.height * 2));
  assert.equal(canvas.style.width, '300px');
});
