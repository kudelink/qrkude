# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""SVG writer. The output is byte-identical to the reference (SPEC section 8)."""

from __future__ import annotations

import math
import re
from typing import Any, List, Mapping, Optional, Union

from .encoder import EncodeOptions, QRCode, encode
from .layout import Layer, _emit, _gradient_geometry, _Layout
from .style import (
    MAX_STOPS,
    Document,
    Paint,
    PaintType,
    Style,
    SvgOptions,
    c_string,
    load_document,
    recommended_ecc,
)

__all__ = ["format_number", "sanitize_id_prefix", "to_svg", "svg", "encode_for_style"]

_HEX = "0123456789abcdef"


def format_number(value: float) -> str:
    """Formats a number the way every QRKude port does (max 3 decimals).

    ``n = floor(v * 1000 + 0.5)``, then the integer part and up to three
    fraction digits without trailing zeros. NaN and ``|v| >= 1e15`` give "0".
    """
    v = float(value)
    if not (-1e15 < v < 1e15):  # NaN, infinities, absurd sizes (SPEC 8.1)
        v = 0.0
    n = math.floor(v * 1000.0 + 0.5)
    neg = n < 0
    if neg:
        n = -n
    ip, fp = divmod(n, 1000)
    out = ("-" if neg else "") + str(ip)
    if fp:
        out += "." + ("%03d" % fp).rstrip("0")
    return out


_fmt = format_number


_ID_SAFE = frozenset(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")


def sanitize_id_prefix(prefix: Optional[str]) -> str:
    """The gradient id prefix as written (SPEC 8.3).

    Every byte of the UTF-8 encoding outside ``[A-Za-z0-9_-]`` becomes ``_``,
    a leading digit or ``-`` gets a ``q`` in front, and an empty or absent
    prefix is ``qrk``, so the prefix can never carry markup.
    """
    prefix = c_string(prefix) if prefix else ""
    if not prefix:
        return "qrk"
    raw = prefix.encode("utf-8")
    out = "".join(chr(b) if b in _ID_SAFE else "_" for b in raw)
    return "q" + out if raw[:1] in (b"-", b"0", b"1", b"2", b"3", b"4", b"5", b"6", b"7",
                                    b"8", b"9") else out


_XML_INVALID_RE = re.compile("[\x01-\x08\x0b\x0c\x0e-\x1f]")


def xml_text(text: str) -> str:
    """Text as written into the SVG before escaping (SPEC 8.3): the string as
    read (see :func:`c_string`) without the control characters U+0001-U+001F
    other than tab, LF and CR, which XML 1.0 does not allow."""
    return _XML_INVALID_RE.sub("", c_string(text))


def _escape(text: str) -> str:
    return (xml_text(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;"))


def _color(color: int) -> str:
    return "#" + "".join(_HEX[(color >> (28 - 4 * i)) & 15] for i in range(6))


def _opacity(attr: str, color: int) -> str:
    a = color & 0xFF
    if a != 255:
        return ' %s="%s"' % (attr, _fmt(a / 255.0))
    return ""


class _SvgSink:
    """Path sink writing SVG path data (SPEC 8.2)."""

    __slots__ = ("out", "lx", "ly")

    def __init__(self) -> None:
        self.out: List[str] = []
        self.lx = 0.0
        self.ly = 0.0

    def move_to(self, x: float, y: float) -> None:
        self.out.append("M%s %s" % (_fmt(x), _fmt(y)))
        self.lx = x
        self.ly = y

    def line_to(self, x: float, y: float) -> None:
        if y == self.ly:
            self.out.append("H" + _fmt(x))
        elif x == self.lx:
            self.out.append("V" + _fmt(y))
        else:
            self.out.append("L%s %s" % (_fmt(x), _fmt(y)))
        self.lx = x
        self.ly = y

    def arc_to(self, r: float, clockwise: bool, x: float, y: float, cx: float,
               cy: float) -> None:
        rs = _fmt(r)
        self.out.append("A%s %s%s%s %s" % (rs, rs, " 0 0 1 " if clockwise else " 0 0 0 ",
                                            _fmt(x), _fmt(y)))
        self.lx = x
        self.ly = y

    def close(self) -> None:
        self.out.append("Z")


def _fill(p: Paint, prefix: str, ident: str) -> str:
    if p.type == PaintType.LINEAR or p.type == PaintType.RADIAL:
        return ' fill="url(#%s-%s)"' % (prefix, ident)
    if p.type == PaintType.SOLID:
        return ' fill="%s"%s' % (_color(p.color), _opacity("fill-opacity", p.color))
    return ' fill="none"'


def _gradient(L: _Layout, layer: Layer, p: Paint, prefix: str, ident: str) -> str:
    g = _gradient_geometry(L, layer, p)
    linear = p.type == PaintType.LINEAR
    parts = ["<linearGradient" if linear else "<radialGradient",
             ' id="%s-%s" gradientUnits="userSpaceOnUse"' % (prefix, ident)]
    if linear:
        parts.append(' x1="%s" y1="%s" x2="%s" y2="%s">' % (_fmt(g.x1), _fmt(g.y1), _fmt(g.x2),
                                                           _fmt(g.y2)))
    else:
        parts.append(' cx="%s" cy="%s" r="%s">' % (_fmt(g.cx), _fmt(g.cy), _fmt(g.r)))
    for stop in p.stops[:MAX_STOPS]:
        parts.append('<stop offset="%s" stop-color="%s"%s/>' % (
            _fmt(stop.offset), _color(stop.color), _opacity("stop-opacity", stop.color)))
    parts.append("</linearGradient>" if linear else "</radialGradient>")
    return "".join(parts)


def _layer_path(out: List[str], L: _Layout, layer: Layer, paint: Paint, prefix: str,
                ident: str) -> None:
    sink = _SvgSink()
    if _emit(L, layer, sink) == 0:
        return
    out.append('<path d="%s"%s/>\n' % ("".join(sink.out), _fill(paint, prefix, ident)))


def to_svg(code: QRCode, style: Optional[Style] = None,
           options: Optional[SvgOptions] = None) -> str:
    """Renders ``code`` with ``style`` as an SVG document (SPEC 8.3).

    Gradient ids are ``{id_prefix}-bg/fg/ef/eb``; use a distinct
    ``options.id_prefix`` for each SVG inlined in the same HTML page.
    """
    style = style if style is not None else Style()
    o = options if options is not None else SvgOptions()
    prefix = sanitize_id_prefix(o.id_prefix)

    L = _Layout(code, style)
    out: List[str] = []
    head = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"'
            ' viewBox="0 0 %s %s"' % (_fmt(L.W), _fmt(L.H)))
    if o.width > 0:
        head += ' width="%s" height="%s"' % (_fmt(o.width), _fmt(o.width * L.H / L.W))
    out.append(head + ">\n")

    bg = Paint.from_value(style.background.color)
    fg = Paint.from_value(style.modules.color)
    ef = Paint.from_value(style.eyes.frame_color)
    eb = Paint.from_value(style.eyes.ball_color)
    if bg.is_gradient or fg.is_gradient or ef.is_gradient or eb.is_gradient:
        out.append("<defs>")
        if bg.is_gradient:
            out.append(_gradient(L, Layer.BACKGROUND, bg, prefix, "bg"))
        if fg.is_gradient:
            out.append(_gradient(L, Layer.MODULES, fg, prefix, "fg"))
        if ef.is_gradient:
            out.append(_gradient(L, Layer.EYE_FRAMES, ef, prefix, "ef"))
        if eb.is_gradient:
            out.append(_gradient(L, Layer.EYE_BALLS, eb, prefix, "eb"))
        out.append("</defs>\n")

    if bg.is_gradient or (bg.type == PaintType.SOLID and (bg.color & 0xFF) != 0):
        rect = '<rect width="%s" height="%s"' % (_fmt(L.W), _fmt(L.H))
        if L.bg_radius > 0:
            rect += ' rx="%s"' % _fmt(L.bg_radius)
        out.append(rect + _fill(bg, prefix, "bg") + "/>\n")

    _layer_path(out, L, Layer.BORDER, Paint(PaintType.SOLID, style.border.color), prefix, "")
    _layer_path(out, L, Layer.MODULES, fg, prefix, "fg")
    has_ef = ef.type != PaintType.NONE
    has_eb = eb.type != PaintType.NONE
    _layer_path(out, L, Layer.EYE_FRAMES, ef if has_ef else fg, prefix, "ef" if has_ef else "fg")
    _layer_path(out, L, Layer.EYE_BALLS, eb if has_eb else fg, prefix, "eb" if has_eb else "fg")
    logo = style.logo
    logo_bg = logo.background if logo is not None else 0
    _layer_path(out, L, Layer.LOGO_BACKGROUND, Paint(PaintType.SOLID, logo_bg), prefix, "")

    if L.has_logo and logo is not None and logo.href and c_string(logo.href):
        out.append('<image x="%s" y="%s" width="%s" height="%s" preserveAspectRatio='
                   '"xMidYMid meet" xlink:href="%s"/>\n' % (
                       _fmt(L.logo_cx - L.logo_half), _fmt(L.logo_cy - L.logo_half),
                       _fmt(2 * L.logo_half), _fmt(2 * L.logo_half), _escape(logo.href)))

    label = style.label
    if L.has_label and label is not None:
        f = L.label_size
        out.append('<text x="%s" y="%s" text-anchor="middle" font-family="%s" font-size="%s"%s'
                   ' fill="%s"%s>%s</text>\n' % (
                       _fmt(L.label_x), _fmt(L.label_baseline),
                       _escape(label.font if label.font and c_string(label.font)
                               else "sans-serif"), _fmt(f),
                       ' font-weight="bold"' if label.bold else "", _color(label.color),
                       _opacity("fill-opacity", label.color), _escape(label.text or "")))

    out.append("</svg>\n")
    return "".join(out)


def svg(text: Union[str, bytes], doc: Union[str, bytes, Mapping[str, Any], Document, None] = None,
        ) -> str:
    """Encodes ``text`` and renders it with a JSON style document in one call.

    ``doc`` is ``{"encode": {...}, "style": {...}, "svg": {...}}`` (or a bare
    style), as JSON text, a mapping or a :class:`Document`. When
    ``encode.ecc`` is absent, ECC is ``H`` if the style has a logo, else ``M``.
    """
    d = load_document(doc)
    code = encode_for_style(text, d.encode, d.style)
    return to_svg(code, d.style, d.svg)


def encode_for_style(data: Union[str, bytes], options: Optional[EncodeOptions] = None,
                     style: Optional[Style] = None) -> QRCode:
    """Like :func:`encode`, but an absent ECC (``options.ecc is None``) becomes
    ``H`` when ``style`` has a logo and ``M`` otherwise (SPEC 1.3)."""
    o = options if options is not None else EncodeOptions()
    ecc = o.ecc if o.ecc is not None else recommended_ecc(style)
    return encode(data, o, ecc=ecc)
