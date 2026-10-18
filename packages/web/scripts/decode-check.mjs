// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
//
// Decode check: writes SVGs for a set of styles (every module shape, eye
// shapes, gradients, logo, label, frame) and decodes them with Python
// (cairosvg -> zxing-cpp).
//
//   node scripts/decode-check.mjs [--out DIR] [--python PYTHON] [--size PX]
//
// Needs: pip install cairosvg zxing-cpp pillow
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { deflateSync } from 'node:zlib';
import * as Q from '../dist/index.mjs';

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};
const outDir = resolve(opt('--out', process.env.QRK_OUT || join(tmpdir(), 'qrkude-decode')));
const python = opt('--python', process.env.QRK_PYTHON || 'python3');
const size = Number(opt('--size', '480'));
mkdirSync(outDir, { recursive: true });

/* A 64x64 RGBA PNG logo (red disc, white square), encoded with zlib. */
function logoPng() {
  const n = 64;
  const raw = Buffer.alloc(n * (n * 4 + 1));
  for (let y = 0; y < n; y++) {
    raw[y * (n * 4 + 1)] = 0;
    for (let x = 0; x < n; x++) {
      const o = y * (n * 4 + 1) + 1 + x * 4;
      const dx = x - n / 2 + 0.5;
      const dy = y - n / 2 + 0.5;
      const disc = dx * dx + dy * dy <= (n / 2 - 2) ** 2;
      const square = Math.abs(dx) < n * 0.2 && Math.abs(dy) < n * 0.2;
      const [r, g, b, a] = square ? [255, 255, 255, 255] : disc ? [230, 57, 70, 255] : [0, 0, 0, 0];
      raw[o] = r;
      raw[o + 1] = g;
      raw[o + 2] = b;
      raw[o + 3] = a;
    }
  }
  const crcTable = Array.from({ length: 256 }, (_, k) => {
    let c = k;
    for (let i = 0; i < 8; i++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  });
  const crc = (buf) => {
    let c = 0xffffffff;
    for (const b of buf) c = crcTable[(c ^ b) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type, data) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]);
    const c = Buffer.alloc(4);
    c.writeUInt32BE(crc(td));
    return Buffer.concat([len, td, c]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(n, 0);
  ihdr.writeUInt32BE(n, 4);
  ihdr.set([8, 6, 0, 0, 0], 8);
  const png = Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
  return 'data:image/png;base64,' + png.toString('base64');
}

const LOGO = logoPng();
const lin = (angle, ...colors) => ({
  type: 'linear',
  angle,
  stops: colors.map((color, i) => ({ offset: i / (colors.length - 1), color })),
});

const styles = [
  ...Q.MODULE_SHAPES.map((shape) => ({
    name: `module-${shape}`,
    style: { modules: { shape, size: shape === 'square' ? 1 : 0.9 } },
  })),
  ...Q.EYE_SHAPES.filter((s) => s !== 'diamond').map((shape) => ({
    name: `eyes-${shape}`,
    style: { modules: { shape: 'dot' }, eyes: { frame: shape, ball: shape } },
  })),
  { name: 'gradient-linear', style: { modules: { shape: 'fluid', color: lin(45, '#6a11cb', '#2575fc') }, eyes: { frame: 'rounded', ball: 'circle', frameColor: '#e63946' } } },
  { name: 'gradient-linear-3stops', style: { modules: { shape: 'liquid', color: lin(135, '#0b3d91', '#00a896', '#0b3d91') } } },
  { name: 'gradient-radial', style: { modules: { shape: 'dot', color: { type: 'radial', stops: [{ offset: 0, color: '#0b3d91' }, { offset: 1, color: '#00a896' }] } }, eyes: { frame: 'circle', ball: 'circle', ballColor: '#e63946' } } },
  { name: 'gradient-background', style: { modules: { shape: 'classy', color: '#1d3557' }, background: { color: lin(90, '#ffffff', '#e0f7fa'), radius: 3 } } },
  { name: 'alignment-styled', style: { modules: { shape: 'dot' }, eyes: { frame: 'circle', ball: 'circle' }, alignment: { frame: 'circle', ball: 'circle' } } },
  { name: 'logo-circle', style: { modules: { shape: 'fluid' }, eyes: { frame: 'rounded', ball: 'rounded' }, logo: { href: LOGO, size: 0.22, shape: 'circle', background: '#ffffff' } } },
  { name: 'logo-square', style: { modules: { shape: 'square' }, logo: { href: LOGO, size: 0.25, margin: 0.5 } } },
  { name: 'logo-rounded-liquid', style: { modules: { shape: 'liquid' }, logo: { href: LOGO, size: 0.2, shape: 'rounded', background: '#fdfcdc' }, background: { color: '#fdfcdc' } } },
  { name: 'label-frame', style: { margin: 2, modules: { shape: 'liquid' }, background: { color: '#fff8e7', radius: 3 }, border: { width: 1, color: '#1d3557' }, label: { text: 'SCAN ME', color: '#1d3557' } } },
  { name: 'label-top', style: { modules: { shape: 'hbars', size: 0.85 }, label: { text: 'kudelink.com', position: 'top', size: 3 } } },
  { name: 'transparent', style: { modules: { shape: 'bevel', color: '#1d3557cc' }, background: { color: null } } },
];

const texts = ['https://kudelink.com', 'QRKude ñ € ✓ https://kudelink.com/' + 'x'.repeat(90)];

const files = [];
for (const s of styles) {
  texts.forEach((text, ti) => {
    const file = join(outDir, `${s.name}-${ti}.svg`);
    const g = Q.generate(text, { style: s.style });
    writeFileSync(file, g.svg);
    files.push({ name: s.name, file, text, version: g.code.version, ecc: g.code.ecc, lint: g.lint });
  });
}
writeFileSync(join(outDir, 'manifest.json'), JSON.stringify(files, null, 1));

const PY = `
import io, json, sys
import cairosvg, zxingcpp
from PIL import Image
files = json.load(open(sys.argv[1]))
size = int(sys.argv[2])
out = []
for f in files:
    png = cairosvg.svg2png(url=f['file'], output_width=size)
    img = Image.open(io.BytesIO(png)).convert('RGBA')
    bg = Image.new('RGBA', img.size, 'white')
    bg.alpha_composite(img)
    bg.convert('RGB').save(f['file'][:-4] + '.png')
    got = [r.text for r in zxingcpp.read_barcodes(bg.convert('L'))]
    out.append({'file': f['file'], 'ok': got == [f['text']], 'got': got})
print(json.dumps(out))
`;
const r = spawnSync(python, ['-I', '-c', PY, join(outDir, 'manifest.json'), String(size)], { encoding: 'utf8', maxBuffer: 1 << 26 });
if (r.status !== 0) {
  console.error(r.stderr || r.error);
  process.exit(2);
}
const results = JSON.parse(r.stdout);
let failures = 0;
results.forEach((res, i) => {
  const f = files[i];
  if (!res.ok) failures++;
  const lintText = f.lint.length ? ` lint=${f.lint.join(',')}` : '';
  console.log(`${res.ok ? 'ok  ' : 'FAIL'} ${f.name} text#${i % texts.length} v${f.version}-${f.ecc}${lintText}${res.ok ? '' : ' got ' + JSON.stringify(res.got)}`);
});
console.log(`\n${styles.length} styles x ${texts.length} texts = ${files.length} SVGs in ${outDir}: ${files.length - failures} decoded, ${failures} failed`);
process.exit(failures ? 1 : 0);
