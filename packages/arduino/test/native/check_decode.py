#!/usr/bin/env python3
# Decodes the images written by test_native (or the LVGL host test) with
# zxing-cpp and compares the text with the manifest.
#
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
# usage: check_decode.py OUT_DIR    (needs zxingcpp, pillow; cairosvg for .svg)
import io
import json
import os
import sys

import zxingcpp
from PIL import Image


def load(path):
    if path.endswith(".svg"):
        import cairosvg

        png = cairosvg.svg2png(url=path, output_width=600)
        return Image.open(io.BytesIO(png)).convert("RGB")
    return Image.open(path).convert("RGB")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    with open(os.path.join(out_dir, "manifest.json"), encoding="utf-8") as f:
        cases = json.load(f)
    failed = 0
    for case in cases:
        path = os.path.join(out_dir, case["file"])
        img = load(path)
        # PNG copy for humans and for the record.
        img.save(os.path.splitext(path)[0] + ".png")
        # A white frame helps when the quiet zone is clipped by the display.
        framed = Image.new("RGB", (img.width + 40, img.height + 40), "white")
        framed.paste(img, (20, 20))
        results = zxingcpp.read_barcodes(framed)
        texts = [r.text for r in results]
        ok = case["expect"] in texts
        failed += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {case['file']:32s} {img.width}x{img.height} -> {texts!r}")
    print(f"{len(cases) - failed}/{len(cases)} decoded")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
