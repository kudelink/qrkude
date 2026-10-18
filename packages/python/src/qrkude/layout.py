# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Layout, module classification, shape model and the normalizing path emitter.

A port of ``core/src/qrk_layout.c``. Every renderer (SVG, PNG, your own) gets
its geometry from :func:`emit_layer`, so all outputs share one shape model.
Arithmetic follows the reference expression by expression so that the
doubles, and therefore the SVG text, are identical.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from enum import IntEnum
from typing import List, Optional, Protocol, Tuple

from .encoder import (
    QRCode,
    alignment_positions,
    ecc_codewords_per_block,
    num_ecc_blocks,
    placement_order,
    raw_codewords,
)
from .style import EYE_SHAPES, MAX_STOPS, MODULE_SHAPES, Paint, PaintType, Style, c_string

__all__ = [
    "Layer",
    "PathSink",
    "LayoutInfo",
    "GradientGeometry",
    "emit_layer",
    "get_layout",
    "layer_paint",
    "gradient_geometry",
    "logo_damage",
    "layer_visible",
]


class Layer(IntEnum):
    BACKGROUND = 0
    BORDER = 1
    MODULES = 2
    EYE_FRAMES = 3
    EYE_BALLS = 4
    LOGO_BACKGROUND = 5


class PathSink(Protocol):
    """Receives normalized path commands (see SPEC section 7).

    All sub-paths are closed; fill with the non-zero rule. ``arc_to`` draws a
    circular arc of radius ``r`` from the current point to ``(x, y)`` around
    ``(cx, cy)``; ``clockwise`` refers to the screen (y down). Arcs never
    exceed 90 degrees.
    """

    def move_to(self, x: float, y: float) -> None: ...

    def line_to(self, x: float, y: float) -> None: ...

    def arc_to(self, r: float, clockwise: bool, x: float, y: float, cx: float,
               cy: float) -> None: ...

    def close(self) -> None: ...


# Corner kinds and order (TL, TR, BR, BL), as in qrk_internal.h.
NONE = 0
ROUND = 1
CHAMFER = 2
TL = 0
TR = 1
BR = 2
BL = 3

(_SQUARE, _DOT, _ROUNDED, _DIAMOND, _STAR, _FLUID, _LIQUID, _BEVEL, _CLASSY, _HBARS,
 _VBARS) = range(11)
_E_SQUARE, _E_ROUNDED, _E_CIRCLE, _E_LEAF, _E_DROP, _E_BEVEL, _E_DIAMOND, _E_DOTS = range(8)
_LOGO_SQUARE, _LOGO_ROUNDED, _LOGO_CIRCLE = range(3)

Corner = Tuple[int, float]  # (kind, amount)


class CRect:
    """Axis-aligned rectangle with per-corner rounding / chamfering."""

    __slots__ = ("x0", "y0", "x1", "y1", "c")

    def __init__(self, x0: float, y0: float, x1: float, y1: float,
                 corners: Optional[List[Corner]] = None) -> None:
        self.x0 = x0
        self.y0 = y0
        self.x1 = x1
        self.y1 = y1
        self.c: List[Corner] = corners if corners is not None else [(NONE, 0.0)] * 4


#: Upper bound for lengths given in modules (margin, border, logo margin, label size).
MAX_LENGTH = 1000.0


def _clamp(v: float, lo: float, hi: float) -> float:
    """Clamps v to [lo, hi]; NaN clamps to lo (like the reference)."""
    if not v >= lo:
        return lo
    return hi if v > hi else v


def _corners(kind: int, a: float) -> List[Corner]:
    return [(kind, a), (kind, a), (kind, a), (kind, a)]


def eye_shape_index(name: str) -> int:
    return EYE_SHAPES.index(name)


def ball_shape(index: int) -> int:
    """A grid of dots breaks the 1:1:3:1:1 finder ratio: draw a circle."""
    if index == _E_DOTS:
        return _E_CIRCLE
    return index if 0 <= index < len(EYE_SHAPES) else _E_SQUARE


@dataclass(frozen=True)
class LayoutInfo:
    """Canvas geometry in modules (multiply by your scale)."""

    width: float
    height: float
    symbol_x: float  # top-left of module (0, 0)
    symbol_y: float
    modules: int  # modules per side
    has_logo: bool
    logo_x: float  # logo image box (square)
    logo_y: float
    logo_size: float
    has_label: bool
    label_x: float  # text anchor (middle)
    label_baseline: float
    label_size: float  # font size


@dataclass(frozen=True)
class GradientGeometry:
    """Gradient geometry in canvas units, as written to the SVG."""

    x1: float
    y1: float
    x2: float
    y2: float
    cx: float
    cy: float
    r: float


class _Layout:
    """Clamped style values, canvas geometry and module classification."""

    def __init__(self, code: QRCode, style: Style) -> None:
        self.code = code
        self.style = style
        n = self.n = code.size

        self.margin = _clamp(style.margin, 0.0, MAX_LENGTH)
        self.border = _clamp(style.border.width, 0.0, MAX_LENGTH)
        self.shape = MODULE_SHAPES.index(str(style.modules.shape))
        self.size = _clamp(style.modules.size, 0.1, 1)
        self.roundness = _clamp(style.modules.roundness, 0, 1)

        frame = self.margin + self.border
        self.W = n + 2 * frame
        self.H = self.W
        self.ox = frame
        self.oy = frame

        label = style.label
        self.has_label = bool(label is not None and label.text and c_string(label.text))
        self.band_y = self.band_h = self.label_x = self.label_baseline = 0.0
        self.label_size = 0.0
        if self.has_label:
            assert label is not None
            f = self.label_size = _clamp(label.size, 0.1, MAX_LENGTH)
            self.band_h = f * 1.5
            self.H += self.band_h
            if label.position == "top":
                self.band_y = self.border
                self.oy += self.band_h
            else:
                self.band_y = self.W - self.border
            self.label_x = self.W / 2
            self.label_baseline = self.band_y + f * 1.1
        min_side = self.W if self.W < self.H else self.H
        self.bg_radius = _clamp(style.background.radius, 0, min_side / 2)

        logo = style.logo
        self.has_logo = bool(logo is not None and logo.size > 0)
        self.excavate = False
        self.logo_cx = self.logo_cy = self.logo_half = self.logo_area_half = 0.0
        self.logo_shape = _LOGO_SQUARE
        if self.has_logo:
            assert logo is not None
            self.excavate = bool(logo.excavate)
            self.logo_shape = ("square", "rounded", "circle").index(str(logo.shape))
            self.logo_cx = self.ox + n / 2.0
            self.logo_cy = self.oy + n / 2.0
            self.logo_half = n * _clamp(logo.size, 0, 1) / 2
            self.logo_area_half = self.logo_half + _clamp(logo.margin, 0.0, MAX_LENGTH)

        align_frame = style.alignment.frame
        self.align_styled = align_frame is not None
        self.align_pos: Tuple[int, ...] = ()
        if self.align_styled:
            self.align_pos = alignment_positions(code.version)

        self._classify()

    # ---- classification ----------------------------------------------------

    def in_logo_area(self, x: int, y: int) -> bool:
        """Module overlaps the logo area (box + margin)."""
        c = self.n / 2.0
        h = self.logo_area_half
        dx = abs(x + 0.5 - c)
        dy = abs(y + 0.5 - c)
        if self.logo_shape == _LOGO_CIRCLE:
            nx = dx - 0.5 if dx - 0.5 > 0 else 0
            ny = dy - 0.5 if dy - 0.5 > 0 else 0
            return nx * nx + ny * ny < h * h
        return dx < h + 0.5 and dy < h + 0.5

    def _classify(self) -> None:
        n = self.n
        stride = self.stride = n + 2
        cell = bytearray(stride * stride)
        styled_align = bytearray(n * n)
        if self.align_styled:
            pos = self.align_pos
            k = len(pos)
            for i in range(k):
                for j in range(k):
                    if not _is_alignment_center(k, i, j):
                        continue
                    for y in range(pos[i] - 2, pos[i] + 3):
                        for x in range(pos[j] - 2, pos[j] + 3):
                            styled_align[y * n + x] = 1
        for y in range(n):
            for x in range(n):
                if (x < 7 and y < 7) or (x >= n - 7 and y < 7) or (x < 7 and y >= n - 7):
                    continue
                if styled_align[y * n + x]:
                    continue
                if self.excavate and self.in_logo_area(x, y):
                    continue
                cell[(y + 1) * stride + x + 1] = 1
        modules = self.code.modules
        dark = bytearray(stride * stride)
        for y in range(n):
            row = (y + 1) * stride + 1
            for x in range(n):
                if cell[row + x] and modules[y * n + x]:
                    dark[row + x] = 1
        self.cell = cell
        self.dark = dark

    def is_data_cell(self, x: int, y: int) -> bool:
        if x < 0 or y < 0 or x >= self.n or y >= self.n:
            return False
        return self.cell[(y + 1) * self.stride + x + 1] != 0

    def is_data_dark(self, x: int, y: int) -> bool:
        if x < 0 or y < 0 or x >= self.n or y >= self.n:
            return False
        return self.dark[(y + 1) * self.stride + x + 1] != 0

    # ---- shape model ---------------------------------------------------------

    def module_crect(self, x: int, y: int) -> Optional[CRect]:
        """Cell-local shape of a dark data module; None for the star shape."""
        s = self.size
        d = (1 - s) / 2
        rho = self.roundness
        r = CRect(d, d, 1 - d, 1 - d)
        shape = self.shape
        if shape == _SQUARE:
            return r
        if shape == _DOT:
            r.c = _corners(ROUND, s / 2)
            return r
        if shape == _ROUNDED:
            r.c = _corners(ROUND, rho * s / 2)
            return r
        if shape == _DIAMOND:
            r.c = _corners(CHAMFER, s / 2)
            return r
        if shape == _STAR:
            return None
        D = self.is_data_dark
        if shape == _HBARS:
            left = D(x - 1, y)
            right = D(x + 1, y)
            if left:
                r.x0 = 0
            if right:
                r.x1 = 1
            a = rho * s / 2
            c = list(r.c)
            if not left:
                c[TL] = c[BL] = (ROUND, a)
            if not right:
                c[TR] = c[BR] = (ROUND, a)
            r.c = c
            return r
        if shape == _VBARS:
            up = D(x, y - 1)
            down = D(x, y + 1)
            if up:
                r.y0 = 0
            if down:
                r.y1 = 1
            a = rho * s / 2
            c = list(r.c)
            if not up:
                c[TL] = c[TR] = (ROUND, a)
            if not down:
                c[BL] = c[BR] = (ROUND, a)
            r.c = c
            return r

        # connected shapes always fill the whole cell
        r.x0 = r.y0 = 0
        r.x1 = r.y1 = 1
        up = D(x, y - 1)
        down = D(x, y + 1)
        left = D(x - 1, y)
        right = D(x + 1, y)
        is_open = (not up and not left, not up and not right, not down and not right,
                   not down and not left)
        kind = CHAMFER if shape == _BEVEL else ROUND
        c = list(r.c)
        for i in range(4):
            use = is_open[i]
            if shape == _CLASSY and (i == TR or i == BL):
                use = False
            if use:
                c[i] = (kind, rho / 2)
        r.c = c
        return r

    def fillet_mask(self, x: int, y: int) -> int:
        """Which corners of a light data cell get a liquid fillet (bit i = corner i)."""
        if self.shape != _LIQUID or self.roundness <= 0:
            return 0
        D = self.is_data_dark
        up = D(x, y - 1)
        down = D(x, y + 1)
        left = D(x - 1, y)
        right = D(x + 1, y)
        m = 0
        if up and left and D(x - 1, y - 1):
            m |= 1 << TL
        if up and right and D(x + 1, y - 1):
            m |= 1 << TR
        if down and right and D(x + 1, y + 1):
            m |= 1 << BR
        if down and left and D(x - 1, y + 1):
            m |= 1 << BL
        return m

    def logo_bg_crect(self) -> CRect:
        h = self.logo_area_half
        r = CRect(self.logo_cx - h, self.logo_cy - h, self.logo_cx + h, self.logo_cy + h)
        if self.logo_shape == _LOGO_ROUNDED:
            r.c = _corners(ROUND, h * 0.3)
        elif self.logo_shape == _LOGO_CIRCLE:
            r.c = _corners(ROUND, h)
        return r


def _is_alignment_center(k: int, i: int, j: int) -> bool:
    return not ((i == 0 and j == 0) or (i == 0 and j == k - 1) or (i == k - 1 and j == 0))


def eye_corners(shape: int, s: float, cc: int) -> List[Corner]:
    c = _corners(NONE, 0.0)
    if shape == _E_ROUNDED:
        c = _corners(ROUND, s * 0.25)
    elif shape == _E_CIRCLE:
        c = _corners(ROUND, s / 2)
    elif shape == _E_LEAF:
        # leaf/drop use 0.36 * s: fully rounded versions break finder detection
        c[cc] = c[(cc + 2) % 4] = (ROUND, s * 0.36)
    elif shape == _E_DROP:
        c = _corners(ROUND, s * 0.36)
        c[cc] = (NONE, 0.0)
    elif shape == _E_BEVEL:
        c = _corners(CHAMFER, s * 0.25)
    elif shape == _E_DIAMOND:
        c = _corners(CHAMFER, s / 2)
    return c


def inner_corners(outer: List[Corner], s_inner: float) -> List[Corner]:
    out: List[Corner] = []
    for kind, a in outer:
        if kind == ROUND:
            a -= 1
        elif kind == CHAMFER:
            a -= 2 - math.sqrt(2.0)
        a = _clamp(a, 0, s_inner / 2)
        out.append((kind if a > 0 else NONE, a if a > 0 else 0))
    return out


# ---- path emitter --------------------------------------------------------------------


class _Emitter:
    """Normalizes the command stream so that every sink (and every port) sees
    the same thing: zero-length lines are dropped and a line that returns to
    the sub-path start right before close is omitted."""

    __slots__ = ("sink", "sx", "sy", "cx", "cy", "pending", "px", "py", "count")

    def __init__(self, sink: Optional[PathSink]) -> None:
        self.sink = sink
        self.sx = self.sy = self.cx = self.cy = 0.0
        self.pending = False
        self.px = self.py = 0.0
        self.count = 0

    def flush(self) -> None:
        if self.pending:
            if self.sink is not None:
                self.sink.line_to(self.px, self.py)
            self.pending = False

    def move(self, x: float, y: float) -> None:
        self.flush()
        self.sx = self.cx = x
        self.sy = self.cy = y
        self.count += 1
        if self.sink is not None:
            self.sink.move_to(x, y)

    def line(self, x: float, y: float) -> None:
        if x == self.cx and y == self.cy:
            return
        self.flush()
        self.pending = True
        self.px = self.cx = x
        self.py = self.cy = y

    def arc(self, r: float, cw: bool, x: float, y: float, ccx: float, ccy: float) -> None:
        self.flush()
        self.cx = x
        self.cy = y
        if self.sink is not None:
            self.sink.arc_to(r, cw, x, y, ccx, ccy)

    def close(self) -> None:
        if self.pending and not (self.px == self.sx and self.py == self.sy):
            self.flush()
        self.pending = False
        self.cx = self.sx
        self.cy = self.sy
        if self.sink is not None:
            self.sink.close()

    def corner(self, c: Corner, cw: bool, x: float, y: float, ccx: float, ccy: float) -> None:
        kind, a = c
        if kind == ROUND and a > 0:
            self.arc(a, cw, x, y, ccx, ccy)
        elif kind == CHAMFER and a > 0:
            self.line(x, y)

    def crect(self, r: CRect) -> None:
        """Clockwise (filled) rectangle with corner treatments."""
        x0, y0, x1, y1 = r.x0, r.y0, r.x1, r.y1
        c = r.c
        tl = 0 if c[TL][0] == NONE else c[TL][1]
        tr = 0 if c[TR][0] == NONE else c[TR][1]
        br = 0 if c[BR][0] == NONE else c[BR][1]
        bl = 0 if c[BL][0] == NONE else c[BL][1]
        self.move(x0 + tl, y0)
        self.line(x1 - tr, y0)
        self.corner(c[TR], True, x1, y0 + tr, x1 - tr, y0 + tr)
        self.line(x1, y1 - br)
        self.corner(c[BR], True, x1 - br, y1, x1 - br, y1 - br)
        self.line(x0 + bl, y1)
        self.corner(c[BL], True, x0, y1 - bl, x0 + bl, y1 - bl)
        self.line(x0, y0 + tl)
        self.corner(c[TL], True, x0 + tl, y0, x0 + tl, y0 + tl)
        self.close()

    def crect_ccw(self, r: CRect) -> None:
        """Counter-clockwise version, used for holes."""
        x0, y0, x1, y1 = r.x0, r.y0, r.x1, r.y1
        c = r.c
        tl = 0 if c[TL][0] == NONE else c[TL][1]
        tr = 0 if c[TR][0] == NONE else c[TR][1]
        br = 0 if c[BR][0] == NONE else c[BR][1]
        bl = 0 if c[BL][0] == NONE else c[BL][1]
        self.move(x0, y0 + tl)
        self.line(x0, y1 - bl)
        self.corner(c[BL], False, x0 + bl, y1, x0 + bl, y1 - bl)
        self.line(x1 - br, y1)
        self.corner(c[BR], False, x1, y1 - br, x1 - br, y1 - br)
        self.line(x1, y0 + tr)
        self.corner(c[TR], False, x1 - tr, y0, x1 - tr, y0 + tr)
        self.line(x0 + tl, y0)
        self.corner(c[TL], False, x0, y0 + tl, x0 + tl, y0 + tl)
        self.close()

    def star(self, cx: float, cy: float, h: float) -> None:
        """Four-point star: tips at distance h, concave arcs of radius
        h*sqrt(2.5) centered at (+-1.5h, +-1.5h)."""
        r = math.sqrt(2.5) * h
        c = 1.5 * h
        self.move(cx, cy - h)
        self.arc(r, False, cx + h, cy, cx + c, cy - c)
        self.arc(r, False, cx, cy + h, cx + c, cy + c)
        self.arc(r, False, cx - h, cy, cx - c, cy + c)
        self.arc(r, False, cx, cy - h, cx - c, cy - c)
        self.close()

    def fillet(self, corner: int, X: float, Y: float, r: float) -> None:
        if corner == TL:
            self.move(X, Y)
            self.line(X + r, Y)
            self.arc(r, False, X, Y + r, X + r, Y + r)
        elif corner == TR:
            self.move(X + 1, Y)
            self.line(X + 1, Y + r)
            self.arc(r, False, X + 1 - r, Y, X + 1 - r, Y + r)
        elif corner == BR:
            self.move(X + 1, Y + 1)
            self.line(X + 1 - r, Y + 1)
            self.arc(r, False, X + 1, Y + 1 - r, X + 1 - r, Y + 1 - r)
        else:
            self.move(X, Y + 1)
            self.line(X, Y + 1 - r)
            self.arc(r, False, X + r, Y + 1, X + r, Y + 1 - r)
        self.close()


def _emit_modules(e: _Emitter, L: _Layout) -> None:
    n = L.n
    ox = L.ox
    oy = L.oy
    s = L.size
    d = (1 - s) / 2
    dark = L.dark
    stride = L.stride

    if (L.shape == _SQUARE and s == 1) or L.shape == _HBARS:
        bars = L.shape == _HBARS
        inset = d if bars else 0
        a = L.roundness * s / 2 if bars else 0
        kind = ROUND if bars else NONE
        for y in range(n):
            row = (y + 1) * stride + 1
            x = 0
            while x < n:
                if not dark[row + x]:
                    x += 1
                    continue
                x0 = x
                while x < n and dark[row + x]:
                    x += 1
                e.crect(CRect(ox + x0 + inset, oy + y + inset, ox + x - inset, oy + y + 1 - inset,
                              _corners(kind, a)))
        return
    if L.shape == _VBARS:
        a = L.roundness * s / 2
        for x in range(n):
            y = 0
            while y < n:
                if not dark[(y + 1) * stride + x + 1]:
                    y += 1
                    continue
                y0 = y
                while y < n and dark[(y + 1) * stride + x + 1]:
                    y += 1
                e.crect(CRect(ox + x + d, oy + y0 + d, ox + x + 1 - d, oy + y - d,
                              _corners(ROUND, a)))
        return

    cell = L.cell
    liquid = L.shape == _LIQUID
    for y in range(n):
        row = (y + 1) * stride + 1
        for x in range(n):
            X = ox + x
            Y = oy + y
            if dark[row + x]:
                r = L.module_crect(x, y)
                if r is None:
                    e.star(X + 0.5, Y + 0.5, s / 2)
                    continue
                r.x0 += X
                r.x1 += X
                r.y0 += Y
                r.y1 += Y
                e.crect(r)
            elif liquid and cell[row + x]:
                m = L.fillet_mask(x, y)
                for c in range(4):
                    if m & (1 << c):
                        e.fillet(c, X, Y, L.roundness / 2)


def _emit_eye_part(e: _Emitter, shape: int, X: float, Y: float, s: float, ring: bool,
                   cc: int) -> None:
    """One eye-like pattern: frame (size 7 or 5) or ball at (X, Y)."""
    if shape == _E_DOTS:
        k = int(s)
        for j in range(k):
            for i in range(k):
                if ring and not (i == 0 or j == 0 or i == k - 1 or j == k - 1):
                    continue
                e.crect(CRect(X + i, Y + j, X + i + 1, Y + j + 1, _corners(ROUND, 0.5)))
        return
    outer = CRect(X, Y, X + s, Y + s, eye_corners(shape, s, cc))
    e.crect(outer)
    if ring:
        e.crect_ccw(CRect(X + 1, Y + 1, X + s - 1, Y + s - 1, inner_corners(outer.c, s - 2)))


def _emit_eyes(e: _Emitter, L: _Layout, frames: bool) -> None:
    st = L.style
    n = L.n
    ex = (0, n - 7, 0)
    ey = (0, 0, n - 7)
    cc = (BR, BL, TR)
    fs = eye_shape_index(str(st.eyes.frame))
    bs = ball_shape(eye_shape_index(str(st.eyes.ball)))
    for i in range(3):
        X = L.ox + ex[i]
        Y = L.oy + ey[i]
        if frames:
            _emit_eye_part(e, fs, X, Y, 7, True, cc[i])
        else:
            _emit_eye_part(e, bs, X + 2, Y + 2, 3, False, cc[i])
    if not L.align_styled:
        return
    afs = eye_shape_index(str(st.alignment.frame))
    ball = st.alignment.ball
    abs_ = ball_shape(eye_shape_index(str(ball)) if ball is not None else -1)
    pos = L.align_pos
    k = len(pos)
    for i in range(k):
        for j in range(k):
            if not _is_alignment_center(k, i, j):
                continue
            X = L.ox + pos[j]
            Y = L.oy + pos[i]
            if frames:
                _emit_eye_part(e, afs, X - 2, Y - 2, 5, True, BR)
            else:
                _emit_eye_part(e, abs_, X, Y, 1, False, BR)


def _canvas_crect(L: _Layout, inset: float) -> CRect:
    a = L.bg_radius - inset
    return CRect(inset, inset, L.W - inset, L.H - inset,
                 _corners(ROUND if a > 0 else NONE, a if a > 0 else 0))


def _emit(L: _Layout, layer: Layer, sink: Optional[PathSink]) -> int:
    e = _Emitter(sink)
    if layer == Layer.BACKGROUND:
        e.crect(_canvas_crect(L, 0))
    elif layer == Layer.BORDER:
        if L.border > 0:
            e.crect(_canvas_crect(L, 0))
            e.crect_ccw(_canvas_crect(L, L.border))
    elif layer == Layer.MODULES:
        _emit_modules(e, L)
    elif layer == Layer.EYE_FRAMES:
        _emit_eyes(e, L, True)
    elif layer == Layer.EYE_BALLS:
        _emit_eyes(e, L, False)
    elif layer == Layer.LOGO_BACKGROUND:
        logo = L.style.logo
        if L.has_logo and logo is not None and (logo.background & 0xFF) != 0:
            e.crect(L.logo_bg_crect())
    return e.count


def emit_layer(code: QRCode, style: Style, layer: Layer,
               sink: Optional[PathSink] = None) -> int:
    """Emits the geometry of one layer into ``sink``; returns the number of sub-paths.

    Coordinates are in modules (canvas units, y down). ``sink=None`` only counts.
    """
    return _emit(_Layout(code, style), Layer(layer), sink)


def get_layout(code: QRCode, style: Style) -> LayoutInfo:
    L = _Layout(code, style)
    return LayoutInfo(
        width=L.W, height=L.H, symbol_x=L.ox, symbol_y=L.oy, modules=L.n,
        has_logo=L.has_logo, logo_x=L.logo_cx - L.logo_half, logo_y=L.logo_cy - L.logo_half,
        logo_size=2 * L.logo_half, has_label=L.has_label, label_x=L.label_x,
        label_baseline=L.label_baseline, label_size=L.label_size,
    )


# ---- paints ----------------------------------------------------------------------------


def layer_paint(style: Style, layer: Layer) -> Paint:
    """Paint used by a layer (eye paints resolved, border/logo as solid)."""
    if layer == Layer.BACKGROUND:
        return Paint.from_value(style.background.color)
    if layer == Layer.BORDER:
        return Paint(PaintType.SOLID, style.border.color)
    modules = Paint.from_value(style.modules.color)
    if layer == Layer.EYE_FRAMES:
        p = Paint.from_value(style.eyes.frame_color)
        return p if p.type != PaintType.NONE else modules
    if layer == Layer.EYE_BALLS:
        p = Paint.from_value(style.eyes.ball_color)
        return p if p.type != PaintType.NONE else modules
    if layer == Layer.LOGO_BACKGROUND:
        return Paint(PaintType.SOLID, style.logo.background if style.logo is not None else 0)
    return modules


def _gradient_geometry(L: _Layout, layer: Layer, p: Paint) -> GradientGeometry:
    if layer == Layer.BACKGROUND:
        ax = 0.0
        ay = 0.0
        aw = L.W
        ah = L.H
    else:
        ax = L.ox
        ay = L.oy
        aw = float(L.n)
        ah = float(L.n)
    cx = ax + aw / 2
    cy = ay + ah / 2

    angle = float(p.angle)
    # C's fmod gives NaN for infinities; NaN coordinates then format as "0"
    a = math.fmod(angle, 360.0) if math.isfinite(angle) else math.nan
    if a < 0:
        a += 360.0
    if a == 0:
        dx, dy = 1.0, 0.0
    elif a == 90:
        dx, dy = 0.0, 1.0
    elif a == 180:
        dx, dy = -1.0, 0.0
    elif a == 270:
        dx, dy = 0.0, -1.0
    else:
        rad = a * math.pi / 180.0
        dx = math.cos(rad)
        dy = math.sin(rad)
    half = (abs(dx) * aw + abs(dy) * ah) / 2
    return GradientGeometry(
        x1=cx - dx * half, y1=cy - dy * half, x2=cx + dx * half, y2=cy + dy * half,
        cx=ax + aw * p.cx, cy=ay + ah * p.cy, r=p.r * (aw if aw > ah else ah),
    )


def layer_visible(code: QRCode, style: Style, layer: Layer) -> bool:
    """Whether a layer draws anything visible, with the SVG writer's rules.

    The resolved paint needs at least one color with alpha > 0 (the solid
    color or any of the first 8 gradient stops, SPEC 8.5); the border also
    needs a width > 0, the logo background a logo, the modules a sub-path.
    """
    layer = Layer(layer)
    p = layer_paint(style, layer)
    if p.type == PaintType.SOLID:
        painted = (p.color & 0xFF) != 0
    elif p.is_gradient:
        painted = any(stop.color & 0xFF for stop in p.stops[:MAX_STOPS])
    else:
        painted = False
    if layer == Layer.BORDER:
        return painted and style.border.width > 0
    if layer == Layer.LOGO_BACKGROUND:
        return painted and style.logo is not None and style.logo.size > 0
    if layer == Layer.MODULES:
        return painted and emit_layer(code, style, layer) > 0
    return painted


def gradient_geometry(code: QRCode, style: Style, layer: Layer) -> GradientGeometry:
    L = _Layout(code, style)
    return _gradient_geometry(L, Layer(layer), layer_paint(style, layer))


# ---- logo damage ----------------------------------------------------------------------


def logo_damage(code: QRCode, style: Style) -> float:
    """Worst-case logo damage: max over RS blocks of hidden / correctable codewords.

    0 = no logo; above 1 the symbol cannot be decoded.
    """
    L = _Layout(code, style)
    if not L.has_logo:
        return 0.0
    ver = code.version
    ecc = int(code.ecc)
    size = code.size
    num_blocks = num_ecc_blocks(ecc, ver)
    block_ecc = ecc_codewords_per_block(ecc, ver)
    raw = raw_codewords(ver)
    data_len = raw - num_blocks * block_ecc
    num_short = num_blocks - raw % num_blocks
    short_len = raw // num_blocks - block_ecc

    damaged = [0] * num_blocks
    last_cw = -1
    last_hit = False
    nbits = raw * 8
    for bit, index in enumerate(placement_order(ver)):
        if bit >= nbits:
            break
        cw = bit // 8
        if cw != last_cw:
            last_cw = cw
            last_hit = False
        if last_hit or not L.in_logo_area(index % size, index // size):
            continue
        last_hit = True
        if cw < data_len:
            block = cw % num_blocks if cw < short_len * num_blocks else \
                num_short + (cw - short_len * num_blocks)
        else:
            block = (cw - data_len) % num_blocks
        damaged[block] += 1

    worst = 0.0
    correctable = block_ecc // 2
    for b in range(num_blocks):
        ratio = damaged[b] / correctable
        if ratio > worst:
            worst = ratio
    return worst

