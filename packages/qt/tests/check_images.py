#!/usr/bin/env python3
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Checks the PNGs written by tst_qrkude.

For every manifest entry:
  * decodes the Qt rendering with zxing-cpp and compares the text,
  * renders the core SVG with cairosvg at the same pixel size and reports the
    mean / max absolute RGB difference (both flattened on white) and the share
    of pixels that differ by more than 64,
  * decodes the cairosvg rendering too.

    python check_images.py manifest.json results.json

Needs: zxing-cpp, cairosvg, pillow, numpy.
"""
import io
import json
import sys

import cairosvg
import numpy as np
import zxingcpp
from PIL import Image


def flatten(img):
    img = img.convert("RGBA")
    bg = Image.new("RGBA", img.size, "white")
    bg.alpha_composite(img)
    return bg.convert("RGB")


def decode(img):
    return [r.text for r in zxingcpp.read_barcodes(img.convert("L"))]


def main():
    manifest_path, results_path = sys.argv[1], sys.argv[2]
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    results = []
    for entry in manifest:
        qt = flatten(Image.open(entry["png"]))
        with open(entry["svg"], "rb") as f:
            svg = f.read()
        png = cairosvg.svg2png(bytestring=svg, output_width=qt.width, output_height=qt.height)
        ref = flatten(Image.open(io.BytesIO(png)))
        ref.save(entry["png"].replace(".qt.png", ".cairo.png"))
        a = np.asarray(qt, dtype=np.int16)
        b = np.asarray(ref, dtype=np.int16)
        diff = np.abs(a - b)
        per_pixel = diff.max(axis=2)
        qt_texts = decode(qt)
        ref_texts = decode(ref)
        results.append({
            "name": entry["name"],
            "decoded": entry["text"] in qt_texts,
            "decodedText": qt_texts[0] if qt_texts else None,
            "cairoDecoded": entry["text"] in ref_texts,
            "mean": float(diff.mean()),
            "max": int(diff.max()),
            "over64": float((per_pixel > 64).mean()),
        })
    with open(results_path, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
