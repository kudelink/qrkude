#!/usr/bin/env python3
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Renders every style in spec/cases/gallery.json with the reference CLI
(SVG via cairosvg and the built-in rasterizer) and decodes the result with
zxing-cpp. Optionally writes a contact sheet.

    pip install zxing-cpp cairosvg pillow
    python spec/tools/check_decode.py --cli core/build/qrkude [--sheet out.png]
"""
import argparse
import io
import json
import os
import subprocess
import sys
import tempfile

import cairosvg
import zxingcpp
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEXTS = [
    "https://kudelink.com",
    "HELLO QRKUDE 2026",
    "Wi-Fi: WIFI:T:WPA;S:Kude;P:secret;; ñ ü €",
    "https://example.com/" + "a" * 180,
]


def flatten(img):
    img = img.convert("RGBA")
    bg = Image.new("RGBA", img.size, "white")
    bg.alpha_composite(img)
    return bg.convert("RGB")


def decode(img):
    results = zxingcpp.read_barcodes(img.convert("L"))
    return [r.text for r in results]


def logo_png(path, size=64):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((2, 2, size - 3, size - 3), fill=(230, 57, 70, 255))
    d.rectangle((size * 0.3, size * 0.3, size * 0.7, size * 0.7), fill=(255, 255, 255, 255))
    img.save(path)
    with open(path + ".rgba", "wb") as f:
        f.write(img.tobytes())
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default=os.path.join(ROOT, "core", "build", "qrkude"))
    ap.add_argument("--cases", default=os.path.join(ROOT, "spec", "cases", "gallery.json"))
    ap.add_argument("--sheet", help="write a contact sheet PNG")
    ap.add_argument("--size", type=int, default=360)
    args = ap.parse_args()

    cases = json.load(open(args.cases))
    tmp = tempfile.mkdtemp(prefix="qrkude-")
    logo = logo_png(os.path.join(tmp, "logo.png"))
    import base64
    logo_uri = "data:image/png;base64," + base64.b64encode(open(logo, "rb").read()).decode()

    failures = 0
    tiles = []
    for case in cases:
        name = case["name"]
        doc = {k: v for k, v in case.items() if k in ("encode", "style", "svg")}
        style = doc.setdefault("style", {})
        if isinstance(style.get("logo"), dict) and style["logo"].get("href") == "$LOGO":
            style["logo"]["href"] = logo_uri
        style_path = os.path.join(tmp, name + ".json")
        json.dump(doc, open(style_path, "w"))
        texts = [case["text"]] if "text" in case else TEXTS
        for ti, text in enumerate(texts):
            svg_path = os.path.join(tmp, f"{name}-{ti}.svg")
            png_path = os.path.join(tmp, f"{name}-{ti}.png")
            base = [args.cli, "--stdin", "-s", style_path]
            subprocess.run(base + ["-o", svg_path], input=text.encode(), check=True)
            extra = ["-l", logo + ".rgba", "-L", "64x64"] if "logo" in style else []
            subprocess.run(base + ["-o", png_path, "-p", str(args.size)] + extra,
                           input=text.encode(), check=True)
            svg_img = flatten(Image.open(io.BytesIO(
                cairosvg.svg2png(url=svg_path, output_width=args.size))))
            ras_img = flatten(Image.open(png_path))
            for kind, img in (("svg", svg_img), ("raster", ras_img)):
                got = decode(img)
                ok = got == [text]
                if not ok:
                    failures += 1
                    print(f"FAIL {name} text#{ti} {kind}: {got!r}")
            if ti == 0:
                tiles.append((name, svg_img, ras_img))
        print(f"ok   {name}" if True else "")

    if args.sheet:
        cols, tile, pad = 6, 200, 26
        rows = (len(tiles) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * tile, rows * (tile + pad)), "white")
        d = ImageDraw.Draw(sheet)
        for i, (name, svg_img, _) in enumerate(tiles):
            x, y = (i % cols) * tile, (i // cols) * (tile + pad)
            im = svg_img.copy()
            im.thumbnail((tile - 10, tile - 10))
            sheet.paste(im, (x + 5, y + 5))
            d.text((x + 8, y + tile), name, fill=(0, 0, 0))
        sheet.save(args.sheet)
        print("sheet:", args.sheet)

    print(f"{len(cases)} styles, {failures} decode failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
