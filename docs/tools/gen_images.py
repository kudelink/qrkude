#!/usr/bin/env python3
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Generates the illustrations used by docs/GUIDE.md and docs/STYLES.md with
the reference CLI, so the pictures always show what the library really does.

    make -C core && python3 docs/tools/gen_images.py
"""
import json
import os
import re
import subprocess
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "core", "build", "qrkude")
OUT = os.path.join(ROOT, "docs", "images")
URL = "https://kudelink.com"

INK = "#1f2937"     # caption text
MUTED = "#6b7280"   # secondary caption text
PAPER = "#ffffff"   # sheet background (keeps sheets readable on dark pages)
RULE = "#e5e7eb"

LOGO_SVG = ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'>"
            "<rect width='100' height='100' rx='24' fill='#3451d1'/>"
            "<circle cx='50' cy='50' r='24' fill='none' stroke='#fff' stroke-width='12'/>"
            "<path d='M58 58 L76 76' stroke='#fff' stroke-width='12' stroke-linecap='round'/></svg>")
LOGO = "data:image/svg+xml," + urllib.parse.quote(LOGO_SVG)


def run(text, doc, *args):
    r = subprocess.run([CLI, "--stdin", "-j", json.dumps(doc), *args], input=text.encode(),
                       capture_output=True, check=True)
    return r.stdout.decode()


def svg(text, style=None, encode=None, prefix="t"):
    doc = {"style": style or {}, "svg": {"idPrefix": prefix}}
    if encode:
        doc["encode"] = encode
    return run(text, doc)


def info(text, style=None, encode=None):
    doc = {"style": style or {}}
    if encode:
        doc["encode"] = encode
    out = run(text, doc, "--info")
    return {k: v.strip() for k, v in ((line + " ").split(" ", 1) for line in out.strip().splitlines())}


def matrix(text, encode=None):
    out = run(text, {"encode": encode or {}}, "--matrix")
    return [[c == "1" for c in row] for row in out.split()]


def split_svg(markup):
    """(width, height, inner markup) of a QRKude SVG document."""
    m = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"[^>]*>\n', markup)
    inner = markup[m.end():markup.rindex("</svg>")]
    return float(m.group(1)), float(m.group(2)), inner


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def sheet(name, tiles, cols=None, tile=150, title=None):
    """Lays out tiles = [(caption, sub, markup, crop)] with captions below.
    crop = (x, y, w, h) in canvas units, or None for the whole code."""
    cols = cols or len(tiles)
    gap, pad, cap_h = 18, 20, 44
    title_h = 30 if title else 0
    rows = (len(tiles) + cols - 1) // cols
    width = pad * 2 + cols * tile + (cols - 1) * gap
    height = pad * 2 + title_h + rows * (tile + cap_h) + (rows - 1) * gap
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
             f'viewBox="0 0 {width} {height}" width="{width}" height="{height}" '
             f'font-family="-apple-system, Segoe UI, Helvetica, Arial, sans-serif">',
             f'<rect width="{width}" height="{height}" rx="12" fill="{PAPER}" stroke="{RULE}"/>']
    if title:
        parts.append(f'<text x="{pad}" y="{pad + 16}" font-size="15" font-weight="600" fill="{INK}">{esc(title)}</text>')
    for i, (caption, sub, markup, crop) in enumerate(tiles):
        r, c = divmod(i, cols)
        x = pad + c * (tile + gap)
        y = pad + title_h + r * (tile + cap_h + gap)
        w, h, inner = split_svg(markup)
        vb = crop or (0, 0, w, h)
        th = tile if crop or w == h else tile * h / w
        parts.append(f'<rect x="{x - 1}" y="{y - 1}" width="{tile + 2}" height="{th + 2}" rx="6" '
                     f'fill="none" stroke="{RULE}"/>')
        parts.append(f'<svg x="{x}" y="{y}" width="{tile}" height="{th:.1f}" '
                     f'viewBox="{vb[0]} {vb[1]} {vb[2]} {vb[3]}">{inner}</svg>')
        ty = y + th + 18
        parts.append(f'<text x="{x + tile / 2}" y="{ty}" text-anchor="middle" font-size="13" '
                     f'font-weight="600" fill="{INK}">{esc(caption)}</text>')
        if sub:
            parts.append(f'<text x="{x + tile / 2}" y="{ty + 17}" text-anchor="middle" font-size="11.5" '
                         f'fill="{MUTED}">{esc(sub)}</text>')
    parts.append("</svg>\n")
    path = os.path.join(OUT, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(parts))
    return path


def eye_crop(margin=4, size=7, pad=0.6):
    return (margin - pad, margin - pad, size + 2 * pad, size + 2 * pad)


# ---------------------------------------------------------------------------
# Guide illustrations
# ---------------------------------------------------------------------------

def alignment_centers(version):
    if version == 1:
        return []
    n = version // 7 + 2
    step = (version * 8 + n * 3 + 5) // (n * 4 - 4) * 2
    pos = [0] * n
    pos[0] = 6
    p = version * 4 + 10
    for i in range(n - 1, 0, -1):
        pos[i] = p
        p -= step
    return [(x, y) for i, y in enumerate(pos) for j, x in enumerate(pos)
            if not ((i == 0 and j == 0) or (i == 0 and j == n - 1) or (i == n - 1 and j == 0))]


def anatomy():
    text = "https://kudelink.com/docs/guide"
    m = matrix(text, {"minVersion": 7})
    n = len(m)
    version = (n - 17) // 4
    regions = {
        "finder": ("#dc2626", "Finder patterns (the three eyes)"),
        "separator": ("#fca5a5", "Separators (always light)"),
        "timing": ("#2563eb", "Timing patterns"),
        "alignment": ("#059669", "Alignment patterns"),
        "format": ("#ea580c", "Format information (ECC level + mask)"),
        "version": ("#7c3aed", "Version information (version 7 and up)"),
        "data": ("#374151", "Data and error correction"),
    }
    centers = alignment_centers(version)

    def region(x, y):
        if (x < 7 and y < 7) or (x >= n - 7 and y < 7) or (x < 7 and y >= n - 7):
            return "finder"
        if (x < 8 and y < 8) or (x >= n - 8 and y < 8) or (x < 8 and y >= n - 8):
            return "separator"
        if any(abs(x - cx) <= 2 and abs(y - cy) <= 2 for cx, cy in centers):
            return "alignment"
        if x == 6 or y == 6:
            return "timing"
        if (y == 8 and (x <= 8 or x >= n - 8)) or (x == 8 and (y <= 8 or y >= n - 8)):
            return "format"
        if version >= 7 and ((n - 11 <= x < n - 8 and y < 6) or (n - 11 <= y < n - 8 and x < 6)):
            return "version"
        return "data"

    s, q = 8, 4  # px per module, quiet zone in modules
    side = (n + 2 * q) * s
    legend_x = side + 30
    width, height = legend_x + 330, side + 20
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" '
           f'height="{height}" font-family="-apple-system, Segoe UI, Helvetica, Arial, sans-serif">',
           f'<rect width="{width}" height="{height}" rx="12" fill="{PAPER}" stroke="{RULE}"/>',
           f'<g transform="translate(10 10)">',
           f'<rect width="{side}" height="{side}" fill="#f9fafb"/>']
    for y in range(n):
        for x in range(n):
            reg = region(x, y)
            color = regions[reg][0]
            dark = m[y][x]
            if reg == "data":
                fill = color if dark else "#ffffff"
            else:
                fill = color if dark else color + "26"
            out.append(f'<rect x="{(x + q) * s}" y="{(y + q) * s}" width="{s}" height="{s}" fill="{fill}"/>')
    out.append(f'<rect x="1" y="1" width="{side - 2}" height="{side - 2}" fill="none" stroke="#9ca3af" '
               f'stroke-dasharray="6 5" stroke-width="1.5"/>')
    out.append(f'<rect x="{q * s}" y="{q * s}" width="{n * s}" height="{n * s}" fill="none" stroke="#9ca3af"/>')
    out.append("</g>")
    ly = 40
    out.append(f'<text x="{legend_x}" y="{ly - 10}" font-size="15" font-weight="600" fill="{INK}">'
               f'Version {version}: {n} × {n} modules</text>')
    for key in ("finder", "separator", "timing", "alignment", "format", "version", "data"):
        color, label = regions[key]
        ly += 30
        out.append(f'<rect x="{legend_x}" y="{ly - 13}" width="16" height="16" rx="3" fill="{color}"/>')
        out.append(f'<text x="{legend_x + 26}" y="{ly}" font-size="13.5" fill="{INK}">{esc(label)}</text>')
    ly += 30
    out.append(f'<rect x="{legend_x}" y="{ly - 13}" width="16" height="16" rx="3" fill="#f9fafb" '
               f'stroke="#9ca3af" stroke-dasharray="3 2"/>')
    out.append(f'<text x="{legend_x + 26}" y="{ly}" font-size="13.5" fill="{INK}">Quiet zone (empty margin, 4 modules)</text>')
    ly += 34
    out.append(f'<text x="{legend_x}" y="{ly}" font-size="12" fill="{MUTED}">Dark modules are drawn in full color,</text>')
    out.append(f'<text x="{legend_x}" y="{ly + 17}" font-size="12" fill="{MUTED}">light modules as a pale tint.</text>')
    out.append("</svg>\n")
    with open(os.path.join(OUT, "guide", "anatomy.svg"), "w") as f:
        f.write("\n".join(out))


def guide():
    os.makedirs(os.path.join(OUT, "guide"), exist_ok=True)
    anatomy()

    texts = [("12 characters", "kudelink.com"),
             ("33 characters", "https://kudelink.com/docs/guide/x"),
             ("120 characters", "https://kudelink.com/" + "a" * 99),
             ("400 characters", "https://kudelink.com/" + "b" * 379)]
    tiles = []
    for caption, t in texts:
        i = info(t)
        tiles.append((caption, f'version {i["version"]} · {i["size"]}×{i["size"]}', svg(t, prefix="v"), None))
    sheet("guide/versions.svg", tiles, tile=150)

    t = "https://kudelink.com/docs/guide/error-correction"
    tiles = []
    for level, pct in (("L", "7%"), ("M", "15%"), ("Q", "25%"), ("H", "30%")):
        enc = {"ecc": level, "boostEcc": False}
        i = info(t, encode=enc)
        tiles.append((f"ECC {level} (~{pct})", f'version {i["version"]} · {i["size"]}×{i["size"]}',
                      svg(t, encode=enc, prefix="e" + level), None))
    sheet("guide/ecc-levels.svg", tiles, tile=150)

    t = "https://kudelink.com/logo"
    tiles = []
    for size in (0.15, 0.22, 0.3, 0.38):
        style = {"modules": {"shape": "fluid"}, "eyes": {"frame": "rounded", "ball": "rounded"},
                 "logo": {"href": LOGO, "size": size, "shape": "rounded", "background": "#ffffff"}}
        i = info(t, style)
        damage = float(i["logo_damage"])
        verdict = ("will not scan" if "logo-unreadable" in i["lint"] else
                   "risky" if "logo-risky" in i["lint"] else "safe")
        tiles.append((f"logo {round(size * 100)}% wide", f"damage {round(damage * 100)}% · {verdict}",
                      svg(t, style, prefix=f"l{round(size * 100)}"), None))
    sheet("guide/logo-sizes.svg", tiles, tile=150)

    tiles = []
    for caption, style in (
            ("dark on light", {"modules": {"color": "#1d3557"}}),
            ("low contrast", {"modules": {"color": "#a8b3c7"}}),
            ("inverted", {"modules": {"color": "#ffffff"}, "background": {"color": "#1d3557"}}),
            ("gradient, still dark", {"modules": {"color": {"type": "linear", "angle": 45, "stops": [
                {"offset": 0, "color": "#6a11cb"}, {"offset": 1, "color": "#2575fc"}]}}})):
        i = info(URL, style)
        flags = i["lint"].split()
        sub = ", ".join(f for f in flags if f in ("low-contrast", "inverted")) or "no contrast warning"
        tiles.append((caption, sub, svg(URL, style, prefix="c" + caption[:3]), None))
    sheet("guide/contrast.svg", tiles, tile=150)

    tiles = []
    for margin in (0, 1, 4):
        sub = "quiet-zone warning" if margin < 2 else "recommended" if margin == 4 else ""
        tiles.append((f"margin {margin}", sub, svg(URL, {"margin": margin}, prefix=f"m{margin}"), None))
    sheet("guide/quiet-zone.svg", tiles, tile=150)


# ---------------------------------------------------------------------------
# Style reference sheets
# ---------------------------------------------------------------------------

MODULE_SHAPES = [
    ("square", "classic"), ("dot", "circles"), ("rounded", "rounded squares"), ("diamond", "rotated squares"),
    ("star", "four-point stars"), ("fluid", "connected, round"), ("liquid", "fluid + inner fillets"),
    ("bevel", "connected, cut corners"), ("classy", "two round corners"), ("hbars", "horizontal bars"),
    ("vbars", "vertical bars")]
EYE_SHAPES = ["square", "rounded", "circle", "leaf", "drop", "bevel", "diamond", "dots"]


def styles():
    os.makedirs(os.path.join(OUT, "styles"), exist_ok=True)
    sheet("styles/modules.svg", [
        (name, sub, svg(URL, {"modules": {"shape": name}}, prefix="ms" + name), None)
        for name, sub in MODULE_SHAPES], cols=4, tile=150)

    tiles = []
    for size in (0.5, 0.7, 0.9, 1.0):
        tiles.append((f"dot, size {size}", "", svg(URL, {"modules": {"shape": "dot", "size": size}}, prefix="d"), None))
    for size in (0.6, 0.8):
        tiles.append((f"square, size {size}", "", svg(URL, {"modules": {"shape": "square", "size": size}}, prefix="s"), None))
    for size in (0.6, 0.9):
        tiles.append((f"hbars, size {size}", "", svg(URL, {"modules": {"shape": "hbars", "size": size}}, prefix="h"), None))
    sheet("styles/module-size.svg", tiles, cols=4, tile=150)

    tiles = []
    for r in (0, 0.4, 0.7, 1.0):
        tiles.append((f"fluid, roundness {r}", "", svg(URL, {"modules": {"shape": "fluid", "roundness": r}}, prefix="f"), None))
    for r in (0.3, 1.0):
        tiles.append((f"rounded, roundness {r}", "", svg(URL, {"modules": {"shape": "rounded", "size": 0.9, "roundness": r}}, prefix="r"), None))
    for r in (0.5, 1.0):
        tiles.append((f"bevel, roundness {r}", "", svg(URL, {"modules": {"shape": "bevel", "roundness": r}}, prefix="b"), None))
    sheet("styles/module-roundness.svg", tiles, cols=4, tile=150)

    sheet("styles/eye-frames.svg", [
        (name, {"dots": "frames only", "diamond": "risky, see lint"}.get(name, ""),
         svg(URL, {"eyes": {"frame": name, "ball": "square"}}, prefix="ef" + name), eye_crop())
        for name in EYE_SHAPES], cols=4, tile=120)
    sheet("styles/eye-balls.svg", [
        (name, {"dots": "drawn as a circle", "diamond": "round frames only"}.get(name, ""),
         svg(URL, {"eyes": {"frame": "square", "ball": name}}, prefix="eb" + name), eye_crop())
        for name in EYE_SHAPES], cols=4, tile=120)
    sheet("styles/eye-colors.svg", [
        ("frameColor + ballColor", "", svg(URL, {"modules": {"shape": "dot", "color": "#1d3557"}, "eyes": {
            "frame": "rounded", "ball": "circle", "frameColor": "#1d3557", "ballColor": "#e63946"}}, prefix="ec1"), None),
        ("gradient ball", "", svg(URL, {"modules": {"shape": "fluid"}, "eyes": {"frame": "circle", "ball": "circle",
            "ballColor": {"type": "radial", "stops": [{"offset": 0, "color": "#f4a261"},
                                                       {"offset": 1, "color": "#9d0208"}]}}}, prefix="ec2"), None),
        ("leaf eyes", "orientated to the center", svg(URL, {"modules": {"shape": "classy"}, "eyes": {
            "frame": "leaf", "ball": "leaf", "frameColor": "#0b3d91"}}, prefix="ec3"), None),
    ], tile=170)

    v7 = "https://kudelink.com/" + "alignment-patterns-" * 6
    sheet("styles/alignment.svg", [
        ("alignment: null", "drawn like data modules", svg(v7, {"modules": {"shape": "dot"}}, prefix="a1"), None),
        ("frame circle, ball circle", "styled like small eyes", svg(v7, {"modules": {"shape": "dot"}, "eyes": {
            "frame": "circle", "ball": "circle"}, "alignment": {"frame": "circle", "ball": "circle"}}, prefix="a2"), None),
        ("frame rounded, ball dots", "", svg(v7, {"modules": {"shape": "fluid"}, "eyes": {"frame": "rounded",
            "ball": "rounded"}, "alignment": {"frame": "rounded", "ball": "dots"}}, prefix="a3"), None),
    ], tile=210)

    def lin(angle, *colors):
        return {"type": "linear", "angle": angle, "stops": [
            {"offset": round(i / (len(colors) - 1), 3), "color": c} for i, c in enumerate(colors)]}

    sheet("styles/colors.svg", [
        ("solid", '"#1d3557"', svg(URL, {"modules": {"shape": "fluid", "color": "#1d3557"}}, prefix="k1"), None),
        ("solid with alpha", '"#1d3557b3"', svg(URL, {"modules": {"shape": "fluid", "color": "#1d3557b3"}}, prefix="k2"), None),
        ("linear, angle 0", "left to right", svg(URL, {"modules": {"shape": "fluid", "color": lin(0, "#6a11cb", "#2575fc")}}, prefix="k3"), None),
        ("linear, angle 90", "top to bottom", svg(URL, {"modules": {"shape": "fluid", "color": lin(90, "#0b3d91", "#00a896")}}, prefix="k4"), None),
        ("linear, angle 45", "three stops", svg(URL, {"modules": {"shape": "fluid", "color": lin(45, "#e63946", "#f4a261", "#9d0208")}}, prefix="k5"), None),
        ("radial", "center to edge", svg(URL, {"modules": {"shape": "dot", "color": {"type": "radial", "stops": [
            {"offset": 0, "color": "#0b3d91"}, {"offset": 1, "color": "#00a896"}]}}}, prefix="k6"), None),
        ("radial, moved", "cx 0.2, cy 0.2, r 1", svg(URL, {"modules": {"shape": "dot", "color": {"type": "radial",
            "cx": 0.2, "cy": 0.2, "r": 1, "stops": [{"offset": 0, "color": "#2b2d42"}, {"offset": 1, "color": "#ef233c"}]}}}, prefix="k7"), None),
        ("gradient background", "light colors only", svg(URL, {"modules": {"color": "#1d3557"}, "background": {
            "color": lin(90, "#ffffff", "#dbeafe")}}, prefix="k8"), None),
    ], cols=4, tile=150)

    sheet("styles/background.svg", [
        ("radius 0", "default", svg(URL, {"background": {"color": "#f1faee"}}, prefix="g1"), None),
        ("radius 4", "", svg(URL, {"background": {"color": "#f1faee", "radius": 4}}, prefix="g2"), None),
        ("border 1", "", svg(URL, {"margin": 2, "background": {"color": "#ffffff", "radius": 3},
                                   "border": {"width": 1, "color": "#1d3557"}}, prefix="g3"), None),
        ("transparent", "color: null", svg(URL, {"modules": {"color": "#1d3557"}, "background": {"color": None}}, prefix="g4"), None),
    ], tile=150)

    sheet("styles/logo.svg", [
        ("shape square", "", svg(URL + "/logo", {"logo": {"href": LOGO, "shape": "square", "background": "#ffffff"}}, prefix="o1"), None),
        ("shape rounded", "", svg(URL + "/logo", {"modules": {"shape": "fluid"}, "logo": {"href": LOGO, "shape": "rounded", "background": "#ffffff"}}, prefix="o2"), None),
        ("shape circle", "", svg(URL + "/logo", {"modules": {"shape": "dot"}, "logo": {"href": LOGO, "shape": "circle", "background": "#ffffff"}}, prefix="o3"), None),
        ("excavate false", "modules stay under the logo", svg(URL + "/logo", {"logo": {"href": LOGO, "excavate": False, "margin": 0}}, prefix="o4"), None),
    ], tile=150)

    sheet("styles/label.svg", [
        ("bottom", "default position", svg(URL, {"margin": 2, "label": {"text": "SCAN ME"}}, prefix="b1"), None),
        ("top, regular weight", "", svg(URL, {"margin": 2, "label": {"text": "kudelink.com", "position": "top",
                                                                       "bold": False, "size": 3}}, prefix="b2"), None),
        ("framed", "background + border + label", svg(URL, {"margin": 2, "modules": {"shape": "liquid", "color": "#1d3557"},
            "background": {"color": "#fff8e7", "radius": 3}, "border": {"width": 1, "color": "#1d3557"},
            "label": {"text": "SCAN ME", "color": "#1d3557"}}, prefix="b3"), None),
    ], tile=170)


if __name__ == "__main__":
    guide()
    styles()
    total = 0
    for d in ("guide", "styles"):
        for f in sorted(os.listdir(os.path.join(OUT, d))):
            total += os.path.getsize(os.path.join(OUT, d, f))
            print(f"docs/images/{d}/{f}")
    print(f"{total // 1024} KiB")
