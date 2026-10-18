# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Scannability checks (SPEC section 11), a port of ``core/src/qrk_lint.c``."""

from __future__ import annotations

import math
from typing import List, Tuple

from .encoder import QRCode
from .layout import Layer, _Layout, ball_shape, eye_shape_index, layer_paint, logo_damage
from .style import MAX_STOPS, Paint, PaintType, Style

__all__ = ["LINT_FLAGS", "lint"]

#: Every flag, in the order :func:`lint` reports them.
LINT_FLAGS: Tuple[str, ...] = (
    "logo-risky",  # logo damage > 0.7
    "logo-unreadable",  # logo damage > 1: cannot decode
    "low-contrast",  # WCAG contrast below 3:1
    "inverted",  # light modules on dark ground
    "transparent-background",
    "quiet-zone",  # margin below 2 modules
    "small-modules",  # module size below 0.6
    "eye-shapes",  # eye frame/ball combination that breaks finder detection
    "invisible",  # modules or eyes have no visible color
)

_SIZED_SHAPES = ("square", "dot", "rounded", "diamond", "star", "hbars", "vbars")
_DIAMOND = eye_shape_index("diamond")
_SQUARE = eye_shape_index("square")
_LEAF = eye_shape_index("leaf")


_WEIGHTS = (0.2126, 0.7152, 0.0722)


def _channel(v: float) -> float:
    return v / 12.92 if v <= 0.04045 else math.pow((v + 0.055) / 1.055, 2.4)


def _luminance_over(c: int, under: int) -> float:
    """WCAG relative luminance of ``c`` composited over the opaque ``under``."""
    a = (c & 0xFF) / 255.0
    lum = 0.0
    for i in range(3):
        shift = 24 - 8 * i
        v = ((c >> shift) & 0xFF) / 255.0
        u = ((under >> shift) & 0xFF) / 255.0
        lum += _WEIGHTS[i] * _channel(v * a + u * (1 - a))
    return lum


def _paint_colors(p: Paint) -> List[int]:
    """The colors a paint can produce; a missing paint is fully transparent."""
    if p.type == PaintType.SOLID:
        return [p.color]
    if p.is_gradient and len(p.stops) > 0:
        return [stop.color for stop in p.stops[:MAX_STOPS]]
    return [0]


def _opaque_over_white(c: int) -> int:
    a = (c & 0xFF) / 255.0
    out = 0xFF
    for shift in (8, 16, 24):
        v = ((c >> shift) & 0xFF) * a + 255 * (1 - a)
        out |= int(v + 0.5) << shift
    return out


def lint(code: QRCode, style: Style) -> List[str]:
    """Returns the scannability warnings for ``code`` drawn with ``style``."""
    flags = set()
    L = _Layout(code, style)

    if L.has_logo:
        d = logo_damage(code, style)
        if d > 1:
            flags.add("logo-unreadable")
        elif d > 0.7:
            flags.add("logo-risky")

    # background colors, made opaque over white (what a page usually shows)
    original = _paint_colors(Paint.from_value(style.background.color))
    if all((c & 0xFF) == 0 for c in original):
        flags.add("transparent-background")
    bg = [_opaque_over_white(c) for c in original]
    bg_lo = 1.0
    bg_hi = 0.0
    for c in bg:
        lum = _luminance_over(c, c)
        if lum < bg_lo:
            bg_lo = lum
        if lum > bg_hi:
            bg_hi = lum

    # foreground: modules and eyes composited over every background color
    fg_lo = 1.0
    fg_hi = 0.0
    for layer in (Layer.MODULES, Layer.EYE_FRAMES, Layer.EYE_BALLS):
        colors = _paint_colors(layer_paint(style, layer))
        if not any(c & 0xFF for c in colors):
            flags.add("invisible")
        for f in colors:
            for b in bg:
                lum = _luminance_over(f, b)
                if lum < fg_lo:
                    fg_lo = lum
                if lum > fg_hi:
                    fg_hi = lum

    if (fg_lo + fg_hi) / 2 > (bg_lo + bg_hi) / 2:
        flags.add("inverted")
        contrast = (fg_lo + 0.05) / (bg_hi + 0.05)
    else:
        contrast = (bg_lo + 0.05) / (fg_hi + 0.05)
    if contrast < 3:
        flags.add("low-contrast")

    if L.margin < 2:
        flags.add("quiet-zone")

    if str(style.modules.shape) in _SIZED_SHAPES and L.size < 0.6:
        flags.add("small-modules")

    # Measured with zxing: diamond frames break finder detection with any
    # ball; diamond balls only work inside round or beveled frames.
    frame = eye_shape_index(str(style.eyes.frame))
    ball = ball_shape(eye_shape_index(str(style.eyes.ball)))
    if frame == _DIAMOND or (ball == _DIAMOND and (frame == _SQUARE or frame == _LEAF)):
        flags.add("eye-shapes")

    return [f for f in LINT_FLAGS if f in flags]
