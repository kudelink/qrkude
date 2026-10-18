# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""PNG / Pillow backend (``pip install qrkude[png]``).

Draws exactly the SVG geometry: every layer comes from :func:`emit_layer`,
arcs are flattened into polygons, polygons are filled with the non-zero rule
by a supersampling scanline rasterizer (samples at sub-pixel centers), and
the coverage mask is box-filtered down to the output size. Paints (solid or
gradient images) are composited through those masks in SVG order.
"""

from __future__ import annotations

import base64
import io
import math
import os
import sys
from functools import lru_cache
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

from PIL import Image, ImageChops, ImageDraw, ImageFont

from .encoder import InvalidArgumentError, QRCode, QRKudeError
from .layout import Layer, _emit, _gradient_geometry, _Layout, layer_paint
from .style import MAX_STOPS, Paint, PaintType, Style, c_string
from .svg import xml_text

__all__ = ["to_image", "to_png", "PolygonSink"]

LogoSource = Union["Image.Image", str, "os.PathLike[str]", bytes, None]

#: Largest output side in pixels.
MAX_SIDE = 16384
#: Largest output in pixels (8192 x 8192).
MAX_PIXELS = 1 << 26
#: Supersampling is reduced so that one coverage mask stays below this.
MAX_SAMPLES = 1 << 26
#: Logos with more pixels are refused before decoding.
MAX_LOGO_PIXELS = 1 << 24
_GRADIENT_SIDE = 1024
_MAX_FONT_PX = 1024.0
_RESAMPLE: Any = getattr(Image, "Resampling", Image)

_LAYERS = (Layer.BORDER, Layer.MODULES, Layer.EYE_FRAMES, Layer.EYE_BALLS,
           Layer.LOGO_BACKGROUND)


# ---- geometry ------------------------------------------------------------------------


class PolygonSink:
    """Path sink that flattens arcs and maps canvas units to pixels.

    ``polygons`` receives one closed point list per sub-path, in pixel
    coordinates ``(u * scale + dx, v * scale + dy)``. ``tolerance`` is the
    maximum distance (in pixels) between an arc and its chords.
    """

    def __init__(self, scale: float = 1.0, dx: float = 0.0, dy: float = 0.0,
                 tolerance: float = 0.1) -> None:
        self.scale = scale
        self.dx = dx
        self.dy = dy
        self.tolerance = tolerance
        self.polygons: List[List[Tuple[float, float]]] = []
        self._cur: List[Tuple[float, float]] = []
        self._u = 0.0
        self._v = 0.0

    def _pt(self, u: float, v: float) -> None:
        self._cur.append((u * self.scale + self.dx, v * self.scale + self.dy))
        self._u = u
        self._v = v

    def move_to(self, x: float, y: float) -> None:
        self._cur = []
        self.polygons.append(self._cur)
        self._pt(x, y)

    def line_to(self, x: float, y: float) -> None:
        self._pt(x, y)

    def arc_to(self, r: float, clockwise: bool, x: float, y: float, cx: float,
               cy: float) -> None:
        a0 = math.atan2(self._v - cy, self._u - cx)
        a1 = math.atan2(y - cy, x - cx)
        sweep = a1 - a0
        # arcs never exceed 90 degrees: take the minor arc
        if sweep > math.pi:
            sweep -= 2 * math.pi
        elif sweep <= -math.pi:
            sweep += 2 * math.pi
        rpx = r * self.scale
        steps = 1
        if rpx > self.tolerance:
            step = 2 * math.acos(1 - self.tolerance / rpx)
            steps = max(1, int(math.ceil(abs(sweep) / step)))
        for k in range(1, steps):
            a = a0 + sweep * k / steps
            self._cur.append(((cx + r * math.cos(a)) * self.scale + self.dx,
                              (cy + r * math.sin(a)) * self.scale + self.dy))
        self._pt(x, y)

    def close(self) -> None:
        pass


def _rasterize(polygons: Sequence[Sequence[Tuple[float, float]]], w: int, h: int) -> bytearray:
    """Non-zero fill; a pixel is inside when its center is (0 or 255 per byte)."""
    rows: List[Optional[List[Tuple[float, int]]]] = [None] * h
    ceil = math.ceil
    for pts in polygons:
        n = len(pts)
        if n < 3:
            continue
        px, py = pts[-1]
        for qx, qy in pts:
            if py != qy:
                if py < qy:
                    ya, yb, xa, xb, d = py, qy, px, qx, 1
                else:
                    ya, yb, xa, xb, d = qy, py, qx, px, -1
                j0 = int(ceil(ya - 0.5))
                j1 = int(ceil(yb - 0.5))
                if j0 < 0:
                    j0 = 0
                if j1 > h:
                    j1 = h
                if j0 < j1:
                    slope = (xb - xa) / (yb - ya)
                    x = xa + (j0 + 0.5 - ya) * slope
                    for j in range(j0, j1):
                        row = rows[j]
                        if row is None:
                            rows[j] = [(x, d)]
                        else:
                            row.append((x, d))
                        x += slope
            px, py = qx, qy
    buf = bytearray(w * h)
    full = b"\xff" * w
    for j in range(h):
        row = rows[j]
        if not row:
            continue
        row.sort()
        wind = 0
        start = 0.0
        base = j * w
        for x, d in row:
            if wind == 0:
                start = x
            wind += d
            if wind == 0:
                a = int(ceil(start - 0.5))
                b = int(ceil(x - 0.5))
                if a < 0:
                    a = 0
                if b > w:
                    b = w
                if b > a:
                    buf[base + a:base + b] = full[:b - a]
    return buf


# ---- paints --------------------------------------------------------------------------


def _rgba(color: int) -> Tuple[int, int, int, int]:
    return (color >> 24) & 0xFF, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF


def _lerp_color(a: int, b: int, t: float) -> int:
    out = 0
    for sh in (0, 8, 16, 24):
        ca = (a >> sh) & 0xFF
        cb = (b >> sh) & 0xFF
        v = int(ca + (cb - ca) * t + 0.5)
        out |= (0 if v < 0 else 255 if v > 255 else v) << sh
    return out


def _color_at(p: Paint, t: float) -> int:
    """Gradient color for parameter t (per-channel sRGB, clamped at the ends)."""
    stops = p.stops[:MAX_STOPS]
    n = len(stops)
    if t <= stops[0].offset:
        return stops[0].color
    for i in range(n - 1):
        o0 = stops[i].offset
        o1 = stops[i + 1].offset
        if t <= o1:
            if o1 > o0:
                return _lerp_color(stops[i].color, stops[i + 1].color, (t - o0) / (o1 - o0))
            return stops[i + 1].color
    return stops[n - 1].color


def _gradient_image(p: Paint, geom: Any, w: int, h: int, scale: float, offx: float,
                    offy: float) -> "Image.Image":
    """RGBA image of a gradient paint over the whole output.

    Gradients are smooth, so large outputs are computed at most
    ``_GRADIENT_SIDE`` pixels wide and scaled up (bilinear).
    """
    gw, gh = w, h
    if max(w, h) > _GRADIENT_SIDE:
        k = _GRADIENT_SIDE / max(w, h)
        gw = max(1, int(round(w * k)))
        gh = max(1, int(round(h * k)))
    sx = w / gw  # output pixels per gradient pixel
    sy = h / gh

    offsets = [s.offset for s in p.stops[:MAX_STOPS]]
    lo = min(offsets)
    hi = max(offsets)
    n = 1024
    span = hi - lo
    coords = (geom.x1, geom.y1, geom.x2, geom.y2) if p.type == PaintType.LINEAR else (
        geom.cx, geom.cy, geom.r)
    if not all(math.isfinite(v) for v in coords + (lo, hi, span)):
        # programmatic styles only (JSON numbers are finite): one flat color
        return Image.new("RGBA", (w, h), _rgba(_color_at(p, 0.0)))
    if span <= 0:
        lut = [bytes(_rgba(_color_at(p, lo)))] * n
        k = 0.0
    else:
        lut = [bytes(_rgba(_color_at(p, lo + span * i / (n - 1)))) for i in range(n)]
        k = (n - 1) / span
    last = n - 1
    rows = []
    # pixel center -> canvas (u, v)
    us = [((px + 0.5) * sx - offx) / scale for px in range(gw)]
    vs = [((py + 0.5) * sy - offy) / scale for py in range(gh)]
    if p.type == PaintType.LINEAR:
        gdx = geom.x2 - geom.x1
        gdy = geom.y2 - geom.y1
        len2 = gdx * gdx + gdy * gdy
        if len2 > 0:
            ax = gdx / len2 * k
            ay = gdy / len2 * k
        else:
            ax = ay = 0.0
        base_x = [(u - geom.x1) * ax - lo * k + 0.5 for u in us]
        for v in vs:
            c = (v - geom.y1) * ay
            rows.append(b"".join([
                lut[0 if i < 0 else last if i > last else i]
                for i in [int(bx + c) for bx in base_x]]))
    else:
        r = geom.r
        inv = k / r if r > 0 else 0.0
        dxs = [(u - geom.cx) * (u - geom.cx) for u in us]
        sqrt = math.sqrt
        off = -lo * k + 0.5
        for v in vs:
            dy2 = (v - geom.cy) * (v - geom.cy)
            rows.append(b"".join([
                lut[0 if i < 0 else last if i > last else i]
                for i in [int(sqrt(d2 + dy2) * inv + off) for d2 in dxs]]))
    img = Image.frombytes("RGBA", (gw, gh), b"".join(rows))
    if (gw, gh) != (w, h):
        img = img.resize((w, h), _RESAMPLE.BILINEAR)
    return img


# ---- logo and label ----------------------------------------------------------------------


def _open_image(source: Any) -> "Image.Image":
    """Opens and decodes an image, refusing absurd dimensions before decoding."""
    img = Image.open(source)
    if img.width <= 0 or img.height <= 0 or img.width * img.height > MAX_LOGO_PIXELS:
        raise InvalidArgumentError("logo image too large (%dx%d)" % (img.width, img.height))
    img.load()
    return img


def _load_logo(logo: LogoSource, href: Optional[str]) -> Optional["Image.Image"]:
    """The logo bitmap: the explicit ``logo`` argument (errors are raised as
    InvalidArgumentError), else a raster ``data:`` URI in ``href`` (ignored when it
    cannot be decoded, since styles may come from untrusted sources)."""
    if isinstance(logo, Image.Image):
        return logo
    if logo is not None:
        try:
            if isinstance(logo, (bytes, bytearray)):
                return _open_image(io.BytesIO(bytes(logo)))
            return _open_image(os.fspath(logo))
        except QRKudeError:
            raise
        except Exception as exc:  # Pillow raises many kinds for corrupt files
            raise InvalidArgumentError("cannot read logo image: %s" % exc) from exc
    if href and href.startswith("data:") and ";base64," in href[:100]:
        header, _, data = href.partition(",")
        if header.startswith("data:image/") and "svg" not in header:
            try:
                return _open_image(io.BytesIO(base64.b64decode(data)))
            except Exception:  # noqa: BLE001 - untrusted input: skip the logo
                return None
    return None


_GENERIC_FONTS: Dict[str, Tuple[Tuple[str, ...], Tuple[str, ...]]] = {
    # family: (regular files, bold files)
    "sans-serif": (
        ("NotoSans-Regular.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf", "Arial.ttf",
         "arial.ttf", "Helvetica.ttc", "FreeSans.ttf"),
        ("NotoSans-Bold.ttf", "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf",
         "Arial Bold.ttf", "arialbd.ttf", "Helvetica.ttc", "FreeSansBold.ttf"),
    ),
    "serif": (
        ("NotoSerif-Regular.ttf", "DejaVuSerif.ttf", "LiberationSerif-Regular.ttf",
         "Times New Roman.ttf", "times.ttf", "Times.ttc", "FreeSerif.ttf"),
        ("NotoSerif-Bold.ttf", "DejaVuSerif-Bold.ttf", "LiberationSerif-Bold.ttf",
         "Times New Roman Bold.ttf", "timesbd.ttf", "Times.ttc", "FreeSerifBold.ttf"),
    ),
    "monospace": (
        ("NotoSansMono-Regular.ttf", "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf",
         "Courier New.ttf", "cour.ttf", "Menlo.ttc", "FreeMono.ttf"),
        ("NotoSansMono-Bold.ttf", "DejaVuSansMono-Bold.ttf", "LiberationMono-Bold.ttf",
         "Courier New Bold.ttf", "courbd.ttf", "Menlo.ttc", "FreeMonoBold.ttf"),
    ),
}


def _font_dirs() -> List[str]:
    home = os.path.expanduser("~")
    if sys.platform == "win32":
        windir = os.environ.get("WINDIR", "C:\\Windows")
        return [os.path.join(windir, "Fonts"),
                os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "Windows", "Fonts")]
    if sys.platform == "darwin":
        return ["/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts",
                os.path.join(home, "Library", "Fonts")]
    data_home = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    data_dirs = os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share"
    return ([os.path.join(d, "fonts") for d in [data_home] + data_dirs.split(":") if d]
            + [os.path.join(home, ".fonts")])


@lru_cache(maxsize=1)
def _font_index() -> Dict[str, str]:
    """Lower-case font file name -> path, for the platform font directories."""
    index: Dict[str, str] = {}
    for root_dir in _font_dirs():
        for root, _dirs, files in os.walk(root_dir):
            for name in files:
                if name.lower().endswith((".ttf", ".otf", ".ttc")):
                    index.setdefault(name.lower(), os.path.join(root, name))
    return index


@lru_cache(maxsize=32)
def _font_path(family: str, bold: bool) -> Optional[str]:
    candidates: List[str] = []
    for fam in family.split(","):
        fam = fam.strip().strip("\"'")
        if not fam:
            continue
        if fam.lower().endswith((".ttf", ".otf", ".ttc")):
            if os.path.isfile(fam):
                return fam
            candidates.append(fam)
            continue
        generic = _GENERIC_FONTS.get(fam.lower())
        if generic is not None:
            candidates.extend(generic[1] if bold else generic[0])
            continue
        compact = fam.replace(" ", "")
        if bold:
            candidates += [compact + "-Bold.ttf", fam + " Bold.ttf", compact + "Bold.ttf",
                           compact + "-Bold.otf", compact.lower() + "b.ttf",
                           compact.lower() + "bd.ttf"]
        candidates += [compact + "-Regular.ttf", compact + ".ttf", fam + ".ttf",
                       compact + "-Regular.otf", compact + ".otf", compact.lower() + ".ttf"]
    generic = _GENERIC_FONTS["sans-serif"]
    candidates.extend(generic[1] if bold else generic[0])
    index = _font_index()
    for name in candidates:
        path = index.get(name.lower())
        if path:
            return path
    return None


def _load_font(family: Optional[str], bold: bool, px: float) -> Any:
    size = max(1, int(round(px)))
    path = _font_path(family or "sans-serif", bool(bold))
    if path is not None:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    try:
        return ImageFont.load_default(size=size)  # Pillow >= 10.1 (FreeType)
    except (TypeError, OSError, AttributeError):
        return ImageFont.load_default()


def _text_width(draw: Any, text: str, font: Any, px: float) -> float:
    try:
        return float(draw.textlength(text, font=font))
    except Exception:  # noqa: BLE001 - very old Pillow or bitmap fonts
        return len(text) * px * 0.6


def _draw_label(canvas: "Image.Image", L: _Layout, style: Style, scale: float, offx: float,
                offy: float, font: Any) -> None:
    label = style.label
    if label is None or not L.has_label:
        return
    px = L.label_size * scale
    # Huge fonts are drawn at a bounded size on a smaller layer and scaled up,
    # so a hostile style cannot make FreeType allocate giant glyph bitmaps.
    k = min(1.0, _MAX_FONT_PX / px) if px > 0 else 1.0
    lw = max(1, int(math.ceil(canvas.width * k)))
    lh = max(1, int(math.ceil(canvas.height * k)))
    family = c_string(label.font) if label.font else ""
    fnt = font if font is not None else _load_font(family or None, label.bold, px * k)
    layer = Image.new("RGBA", (lw, lh), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    x = (L.label_x * scale + offx) * k
    y = (L.label_baseline * scale + offy) * k
    # SVG 1.1 whitespace (xml:space="default"), as SVG renderers display it:
    # newlines removed, tabs become spaces, spaces collapsed and trimmed
    text = xml_text(label.text or "").replace("\r", "").replace("\n", "").replace("\t", " ")
    text = " ".join(part for part in text.split(" ") if part)
    # Only the middle of a very long text can be visible: trim both ends.
    width = _text_width(draw, text, fnt, px * k)
    if width > 2 * lw and len(text) > 1:
        keep = max(1, int(len(text) * 2 * lw / width) + 2)
        cut = (len(text) - keep) // 2
        if cut > 0:
            text = text[cut:cut + keep]
    rgba = _rgba(label.color)
    try:
        draw.text((x, y), text, font=fnt, fill=rgba, anchor="ms")
    except (ValueError, TypeError):  # bitmap fonts do not support anchors
        left, top, right, bottom = draw.textbbox((0, 0), text, font=fnt)
        draw.text((x - (right - left) / 2, y - bottom), text, font=fnt, fill=rgba)
    if (lw, lh) != canvas.size:
        layer = layer.resize(canvas.size, _RESAMPLE.BILINEAR)
    canvas.alpha_composite(layer)


# ---- public API ----------------------------------------------------------------------------


def _composite(canvas: "Image.Image", mask: "Image.Image", paint: Paint, L: _Layout,
               layer: Layer, scale: float, offx: float, offy: float) -> None:
    w, h = canvas.size
    if paint.type == PaintType.SOLID:
        if paint.color & 0xFF == 0:
            return
        src = Image.new("RGBA", (w, h), _rgba(paint.color))
    elif paint.is_gradient and paint.stops:
        geom = _gradient_geometry(L, layer, paint)
        src = _gradient_image(paint, geom, w, h, scale, offx, offy)
    else:
        return
    alpha = ImageChops.multiply(src.getchannel("A"), mask)
    src.putalpha(alpha)
    canvas.alpha_composite(src)


def to_image(
    code: QRCode,
    style: Optional[Style] = None,
    size: int = 512,
    logo: LogoSource = None,
    *,
    height: Optional[int] = None,
    supersample: int = 4,
    label: bool = True,
    font: Any = None,
) -> "Image.Image":
    """Renders ``code`` as an RGBA image ``size`` pixels wide.

    ``height`` defaults to ``size`` times the canvas aspect ratio (labels make
    the canvas taller); the drawing is scaled to fit and centered. ``logo`` is
    a ``PIL.Image``, a file path or encoded image bytes; without it a raster
    ``data:`` URI in ``style.logo.href`` is used. The logo is fitted into the
    logo box keeping its aspect ratio. ``supersample`` (1-8) sets the
    anti-aliasing quality; ``font`` overrides the label font (a Pillow font).
    """
    style = style if style is not None else Style()
    L = _Layout(code, style)
    width = int(size)
    if height is None:
        height = int(width * L.H / L.W + 0.5)
    height = int(height)
    if not (1 <= width <= MAX_SIDE and 1 <= height <= MAX_SIDE) or \
            width * height > MAX_PIXELS:
        raise InvalidArgumentError(
            "image size %dx%d out of range (1..%d per side, %d pixels in all)"
            % (width, height, MAX_SIDE, MAX_PIXELS))
    scale = width / L.W
    if height / L.H < scale:
        scale = height / L.H
    offx = (width - L.W * scale) / 2
    offy = (height - L.H * scale) / 2
    ss = max(1, min(8, int(supersample)))
    while ss > 1 and width * height * ss * ss > MAX_SAMPLES:
        ss -= 1

    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))

    def mask_for(layer: Layer) -> Optional["Image.Image"]:
        sink = PolygonSink(scale * ss, offx * ss, offy * ss, tolerance=0.1)
        if _emit(L, layer, sink) == 0:
            return None
        buf = _rasterize(sink.polygons, width * ss, height * ss)
        mask = Image.frombytes("L", (width * ss, height * ss), bytes(buf))
        return mask.reduce(ss) if ss > 1 else mask

    bg = layer_paint(style, Layer.BACKGROUND)
    if bg.is_gradient or (bg.type == PaintType.SOLID and bg.color & 0xFF):
        m = mask_for(Layer.BACKGROUND)
        if m is not None:
            _composite(canvas, m, bg, L, Layer.BACKGROUND, scale, offx, offy)
    for layer in _LAYERS:
        paint = layer_paint(style, layer)
        if paint.type == PaintType.NONE:
            continue
        m = mask_for(layer)
        if m is not None:
            _composite(canvas, m, paint, L, layer, scale, offx, offy)

    if L.has_logo:
        img = _load_logo(logo, style.logo.href if style.logo is not None else None)
        if img is not None and img.width > 0 and img.height > 0:
            img = img.convert("RGBA")
            box = 2 * L.logo_half * scale
            k = box / max(img.width, img.height)
            lw = max(1, int(round(img.width * k)))
            lh = max(1, int(round(img.height * k)))
            img = img.resize((lw, lh), _RESAMPLE.LANCZOS)
            x = (L.logo_cx * scale + offx) - lw / 2
            y = (L.logo_cy * scale + offy) - lh / 2
            canvas.alpha_composite(img, (int(round(x)), int(round(y))))

    if label:
        _draw_label(canvas, L, style, scale, offx, offy, font)
    return canvas


def to_png(code: QRCode, style: Optional[Style] = None, size: int = 512,
           logo: LogoSource = None, **kwargs: Any) -> bytes:
    """Like :func:`to_image` but returns PNG file bytes."""
    img = to_image(code, style, size, logo, **kwargs)
    out = io.BytesIO()
    img.save(out, format="PNG", optimize=True)
    return out.getvalue()
