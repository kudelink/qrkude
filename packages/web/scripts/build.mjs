// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
//
// Builds dist/: ESM + CJS bundles of the library and the element entry,
// .d.mts/.d.cts declarations, and the minified browser bundle (global QRKude).
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import * as esbuild from 'esbuild';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist');
const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
const banner = `/*! qrkude ${pkg.version} | Copyright 2026 The QRKude Authors | SPDX-License-Identifier: Apache-2.0 */`;

rmSync(dist, { recursive: true, force: true });
mkdirSync(dist, { recursive: true });

// 1. type check + declarations (dist/types/src), then .d.mts / .d.cts copies
// whose relative imports point at the matching extension.
execFileSync(process.execPath, [join(root, 'node_modules/typescript/bin/tsc'), '-p', join(root, 'tsconfig.json')], {
  stdio: 'inherit',
});
const declSrc = join(dist, 'types/src');
for (const [flavor, ext] of [['esm', 'mts'], ['cjs', 'cts']]) {
  const out = join(dist, 'types', flavor);
  mkdirSync(out, { recursive: true });
  for (const file of readdirSync(declSrc)) {
    if (!file.endsWith('.d.ts')) continue;
    const text = readFileSync(join(declSrc, file), 'utf8').replace(
      /(from\s+['"]\.\/[\w-]+)\.js(['"])/g,
      `$1.${ext.replace('t', 'j')}$2`,
    );
    writeFileSync(join(out, file.replace(/\.d\.ts$/, `.d.${ext}`)), text);
  }
}
rmSync(declSrc, { recursive: true, force: true });

// 2. bundles
const common = {
  bundle: true,
  target: 'es2019',
  platform: 'neutral',
  banner: { js: banner },
  legalComments: 'none',
  logLevel: 'warning',
};

/* the element entry imports the library from the sibling bundle */
const externalIndex = (ext) => ({
  name: 'external-index',
  setup(build) {
    build.onResolve({ filter: /^\.\/index(\.js)?$/ }, () => ({ path: `./index.${ext}`, external: true }));
  },
});

await Promise.all([
  esbuild.build({ ...common, entryPoints: [join(root, 'src/index.ts')], format: 'esm', outfile: join(dist, 'index.mjs') }),
  esbuild.build({ ...common, entryPoints: [join(root, 'src/index.ts')], format: 'cjs', outfile: join(dist, 'index.cjs') }),
  esbuild.build({
    ...common,
    entryPoints: [join(root, 'src/element.ts')],
    format: 'esm',
    outfile: join(dist, 'element.mjs'),
    plugins: [externalIndex('mjs')],
  }),
  esbuild.build({
    ...common,
    entryPoints: [join(root, 'src/element.ts')],
    format: 'cjs',
    outfile: join(dist, 'element.cjs'),
    plugins: [externalIndex('cjs')],
  }),
  esbuild.build({
    ...common,
    platform: 'browser',
    entryPoints: [join(root, 'src/browser.ts')],
    format: 'iife',
    globalName: 'QRKude',
    minify: true,
    outfile: join(dist, 'qrkude.min.js'),
  }),
]);

const size = (f) => (readFileSync(join(dist, f)).length / 1024).toFixed(1) + ' KiB';
console.log(`built dist/: index.mjs ${size('index.mjs')}, qrkude.min.js ${size('qrkude.min.js')}`);
