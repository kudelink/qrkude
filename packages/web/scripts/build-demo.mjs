// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
//
// Builds demo/dist/playground.html: the playground with dist/qrkude.min.js
// inlined, so the page is a single file that makes no external requests.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const html = readFileSync(join(root, 'demo/index.html'), 'utf8');
const bundle = readFileSync(join(root, 'dist/qrkude.min.js'), 'utf8').replace(/<\/script/gi, '<\\/script');

const tag = '<script src="../dist/qrkude.min.js"></script>';
if (!html.includes(tag)) throw new Error(`demo/index.html must load the bundle with ${tag}`);
const out = html.replace(tag, () => `<script>\n${bundle.trim()}\n</script>`);

// Guard: nothing may be fetched from the network.
const external = [
  /<script[^>]+src=/i,
  /<link[^>]+href=/i,
  /<(img|iframe|video|audio|source)[^>]+src=["']?(https?:)?\/\//i,
  /url\(\s*["']?(https?:)?\/\//i,
  /@import/i,
];
for (const re of external) {
  const m = out.match(re);
  if (m) throw new Error(`playground.html would load an external resource: ${m[0]}`);
}

mkdirSync(join(root, 'demo/dist'), { recursive: true });
writeFileSync(join(root, 'demo/dist/playground.html'), out);
console.log(`built demo/dist/playground.html (${(out.length / 1024).toFixed(1)} KiB, self-contained)`);
