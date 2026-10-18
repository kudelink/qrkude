# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Style model and the cross-language JSON style reader.

The defaults, accepted keys, ``null`` handling and error cases follow
``core/src/qrk_json.c`` and SPEC section 2: unknown keys (of any length) are
ignored, numbers parse to the nearest IEEE double and integer fields truncate
toward zero.
"""

from __future__ import annotations

import copy
import json
import math
import re
from dataclasses import dataclass, field
from enum import Enum, IntEnum
from typing import Any, Dict, List, Mapping, Optional, Sequence, Tuple, Union

from .encoder import Ecc, EncodeOptions, QRKudeError

__all__ = [
    "MAX_STOPS",
    "Color",
    "StyleError",
    "ModuleShape",
    "EyeShape",
    "PaintType",
    "Stop",
    "Paint",
    "ModuleStyle",
    "EyeStyle",
    "AlignmentStyle",
    "BackgroundStyle",
    "BorderStyle",
    "LogoStyle",
    "LabelStyle",
    "Style",
    "SvgOptions",
    "Document",
    "parse_color",
    "format_color",
    "c_string",
    "load_document",
    "recommended_ecc",
]

MAX_STOPS = 8

#: Colors are 32-bit integers ``0xRRGGBBAA``.
Color = int


class StyleError(QRKudeError):
    """Invalid style document or value (kind ``"parse error"``)."""

    kind = "parse error"


class ModuleShape(str, Enum):
    SQUARE = "square"
    DOT = "dot"
    ROUNDED = "rounded"
    DIAMOND = "diamond"
    STAR = "star"
    FLUID = "fluid"
    LIQUID = "liquid"
    BEVEL = "bevel"
    CLASSY = "classy"
    HBARS = "hbars"
    VBARS = "vbars"

    def __str__(self) -> str:
        return self.value


class EyeShape(str, Enum):
    SQUARE = "square"
    ROUNDED = "rounded"
    CIRCLE = "circle"
    LEAF = "leaf"
    DROP = "drop"
    BEVEL = "bevel"
    DIAMOND = "diamond"
    DOTS = "dots"

    def __str__(self) -> str:
        return self.value


MODULE_SHAPES: Tuple[str, ...] = tuple(s.value for s in ModuleShape)
EYE_SHAPES: Tuple[str, ...] = tuple(s.value for s in EyeShape)
LOGO_SHAPES: Tuple[str, ...] = ("square", "rounded", "circle")
LABEL_POSITIONS: Tuple[str, ...] = ("bottom", "top")


# ---- strings ----------------------------------------------------------------------

_LONE_SURROGATE_RE = re.compile("[\ud800-\udfff]")


def c_string(value: str) -> str:
    """A string as every port sees it (SPEC 2.0): it ends at its first U+0000,
    and surrogates that are not part of a valid pair become U+FFFD."""
    end = value.find("\x00")
    if end >= 0:
        value = value[:end]
    return _LONE_SURROGATE_RE.sub("\ufffd", value)


# ---- colors -----------------------------------------------------------------------


def parse_color(value: Union[str, int]) -> Color:
    """Parses ``#rgb``, ``#rrggbb``, ``#rrggbbaa`` or ``transparent``.

    Integers are taken as ``0xRRGGBBAA`` and returned unchanged.
    """
    if isinstance(value, int) and not isinstance(value, bool):
        if 0 <= value <= 0xFFFFFFFF:
            return value
        raise StyleError("color out of range: %r" % (value,))
    if not isinstance(value, str):
        raise StyleError("invalid color: %r" % (value,))
    if value == "transparent":
        return 0
    if len(value) not in (4, 7, 9) or value[0] != "#":
        raise StyleError("invalid color: %r" % (value,))
    digits = value[1:]
    v = 0
    for ch in digits:
        if ch in "0123456789":
            d = ord(ch) - 48
        elif "a" <= ch <= "f":
            d = ord(ch) - 87
        elif "A" <= ch <= "F":
            d = ord(ch) - 55
        else:
            raise StyleError("invalid color: %r" % (value,))
        v = v << 4 | d
    if len(value) == 4:
        r = ((v >> 8) & 15) * 17
        g = ((v >> 4) & 15) * 17
        b = (v & 15) * 17
        return (r << 24) | (g << 16) | (b << 8) | 0xFF
    if len(value) == 7:
        return (v << 8) | 0xFF
    return v


def format_color(color: Color) -> str:
    """``#rrggbb`` (lower case) or ``#rrggbbaa`` when not opaque."""
    if color & 0xFF == 0xFF:
        return "#%06x" % (color >> 8)
    return "#%08x" % color


# ---- paints -----------------------------------------------------------------------


class PaintType(IntEnum):
    NONE = 0  # transparent, or "inherit" for eye paints
    SOLID = 1
    LINEAR = 2
    RADIAL = 3


@dataclass(frozen=True)
class Stop:
    offset: float
    color: Color


PaintLike = Union["Paint", str, int, None, Mapping[str, Any]]


@dataclass(frozen=True)
class Paint:
    """A solid color, a linear/radial gradient or nothing.

    Gradient geometry is relative to the painted area: ``angle`` in degrees
    (0 = left to right, 90 = top to bottom); ``cx``/``cy``/``r`` for radial.
    """

    type: PaintType = PaintType.NONE
    color: Color = 0
    angle: float = 0.0
    cx: float = 0.5
    cy: float = 0.5
    r: float = 0.75
    stops: Tuple[Stop, ...] = ()

    @staticmethod
    def none() -> "Paint":
        return Paint()

    @staticmethod
    def solid(color: Union[str, int]) -> "Paint":
        return Paint(PaintType.SOLID, parse_color(color))

    @staticmethod
    def _stops(colors: Sequence[Any]) -> Tuple[Stop, ...]:
        out = []
        n = len(colors)
        for i, c in enumerate(colors):
            if isinstance(c, Stop):
                out.append(c)
            elif isinstance(c, (tuple, list)):
                out.append(Stop(float(c[0]), parse_color(c[1])))
            else:
                out.append(Stop(i / (n - 1) if n > 1 else 0.0, parse_color(c)))
        return tuple(out)

    @staticmethod
    def linear(angle: float, *colors: Any) -> "Paint":
        """``Paint.linear(45, "#f00", "#00f")`` or with ``Stop``/``(offset, color)``."""
        stops = Paint._stops(colors)
        return Paint(PaintType.LINEAR, stops[0].color if stops else 0, float(angle), stops=stops)

    @staticmethod
    def radial(*colors: Any, cx: float = 0.5, cy: float = 0.5, r: float = 0.75) -> "Paint":
        """``Paint.radial("#fff", "#000", r=0.6)`` (center color first)."""
        stops = Paint._stops(colors)
        return Paint(PaintType.RADIAL, stops[0].color if stops else 0, 0.0, float(cx), float(cy),
                     float(r), stops)

    @property
    def is_gradient(self) -> bool:
        return self.type == PaintType.LINEAR or self.type == PaintType.RADIAL

    @staticmethod
    def from_value(value: PaintLike) -> "Paint":
        """Converts a JSON paint (``null``, color string, gradient object)."""
        if isinstance(value, Paint):
            return value
        if value is None:
            return Paint()
        if isinstance(value, (str, int)) and not isinstance(value, bool):
            return Paint.solid(value)
        if isinstance(value, Mapping):
            return _parse_gradient(value)
        raise StyleError("invalid paint: %r" % (value,))

    def to_value(self) -> Any:
        """JSON representation (inverse of :meth:`from_value`)."""
        if self.type == PaintType.NONE:
            return None
        if self.type == PaintType.SOLID:
            return format_color(self.color)
        out: Dict[str, Any] = {"type": "linear" if self.type == PaintType.LINEAR else "radial"}
        if self.type == PaintType.LINEAR:
            out["angle"] = self.angle
        else:
            out.update(cx=self.cx, cy=self.cy, r=self.r)
        out["stops"] = [{"offset": s.offset, "color": format_color(s.color)} for s in self.stops]
        return out


# ---- style ------------------------------------------------------------------------


def _coerce_paint(value: Any) -> Paint:
    return Paint.from_value(value)


def _coerce_color(value: Any) -> Color:
    if value is None:
        return 0
    return parse_color(value)


def _shape_name(value: Any, names: Tuple[str, ...], what: str) -> str:
    if isinstance(value, Enum):
        value = value.value
    if not isinstance(value, str) or value not in names:
        raise StyleError("unknown %s: %r" % (what, value))
    return value


@dataclass
class ModuleStyle:
    shape: str = "square"
    size: float = 1.0  # 0.1..1 (square, dot, rounded, diamond, star, bars)
    roundness: float = 1.0  # 0..1 (rounded, fluid, liquid, bevel, classy, bars)
    color: Paint = field(default_factory=lambda: Paint.solid(0x000000FF))

    def __post_init__(self) -> None:
        self.shape = _shape_name(self.shape, MODULE_SHAPES, "module shape")
        self.color = _coerce_paint(self.color)


@dataclass
class EyeStyle:
    frame: str = "square"
    ball: str = "square"
    frame_color: Paint = field(default_factory=Paint)  # NONE = same as modules
    ball_color: Paint = field(default_factory=Paint)

    def __post_init__(self) -> None:
        self.frame = _shape_name(self.frame, EYE_SHAPES, "eye shape")
        self.ball = _shape_name(self.ball, EYE_SHAPES, "eye shape")
        self.frame_color = _coerce_paint(self.frame_color)
        self.ball_color = _coerce_paint(self.ball_color)


@dataclass
class AlignmentStyle:
    """``frame=None`` draws alignment patterns as data modules."""

    frame: Optional[str] = None
    ball: Optional[str] = None  # None = "square" when the frame is set

    def __post_init__(self) -> None:
        if self.frame is not None:
            self.frame = _shape_name(self.frame, EYE_SHAPES, "eye shape")
        if self.ball is not None:
            self.ball = _shape_name(self.ball, EYE_SHAPES, "eye shape")


@dataclass
class BackgroundStyle:
    color: Paint = field(default_factory=lambda: Paint.solid(0xFFFFFFFF))  # NONE = transparent
    radius: float = 0.0

    def __post_init__(self) -> None:
        self.color = _coerce_paint(self.color)


@dataclass
class BorderStyle:
    width: float = 0.0  # modules, drawn outside the quiet zone
    color: Color = 0x000000FF

    def __post_init__(self) -> None:
        self.color = _coerce_color(self.color)


@dataclass
class LogoStyle:
    href: Optional[str] = None  # SVG <image> URL or data: URI
    size: float = 0.2  # fraction of the symbol width (0 = no logo)
    margin: float = 1.0  # clear space around the logo, in modules
    shape: str = "square"  # square | rounded | circle
    excavate: bool = True
    background: Color = 0  # fill of the cleared area (0 = none)

    def __post_init__(self) -> None:
        self.shape = _shape_name(self.shape, LOGO_SHAPES, "logo shape")
        self.background = _coerce_color(self.background)


@dataclass
class LabelStyle:
    text: Optional[str] = None  # None / empty = no label
    font: Optional[str] = "sans-serif"
    size: float = 4.0  # font size in modules
    color: Color = 0x000000FF
    position: str = "bottom"  # bottom | top
    bold: bool = True

    def __post_init__(self) -> None:
        self.position = _shape_name(self.position, LABEL_POSITIONS, "label position")
        self.color = _coerce_color(self.color)


@dataclass
class Style:
    """Complete visual style. All lengths are in modules."""

    margin: float = 4.0
    modules: ModuleStyle = field(default_factory=ModuleStyle)
    eyes: EyeStyle = field(default_factory=EyeStyle)
    alignment: AlignmentStyle = field(default_factory=AlignmentStyle)
    background: BackgroundStyle = field(default_factory=BackgroundStyle)
    border: BorderStyle = field(default_factory=BorderStyle)
    logo: Optional[LogoStyle] = None
    label: Optional[LabelStyle] = None

    @staticmethod
    def from_json(source: Union[str, bytes, Mapping[str, Any]]) -> "Style":
        """Parses a style object or a ``{encode, style, svg}`` document."""
        return load_document(source).style

    def to_dict(self) -> Dict[str, Any]:
        """JSON-compatible representation (round-trips through from_json)."""
        out: Dict[str, Any] = {
            "margin": self.margin,
            "modules": {
                "shape": self.modules.shape,
                "size": self.modules.size,
                "roundness": self.modules.roundness,
                "color": self.modules.color.to_value(),
            },
            "eyes": {
                "frame": self.eyes.frame,
                "ball": self.eyes.ball,
                "frameColor": self.eyes.frame_color.to_value(),
                "ballColor": self.eyes.ball_color.to_value(),
            },
            "alignment": {"frame": self.alignment.frame, "ball": self.alignment.ball},
            "background": {
                "color": self.background.color.to_value(),
                "radius": self.background.radius,
            },
            "border": {"width": self.border.width, "color": format_color(self.border.color)},
            "logo": None,
            "label": None,
        }
        if self.logo is not None:
            out["logo"] = {
                "href": self.logo.href,
                "size": self.logo.size,
                "margin": self.logo.margin,
                "shape": self.logo.shape,
                "excavate": self.logo.excavate,
                "background": format_color(self.logo.background) if self.logo.background else None,
            }
        if self.label is not None:
            out["label"] = {
                "text": self.label.text,
                "font": self.label.font,
                "size": self.label.size,
                "color": format_color(self.label.color),
                "position": self.label.position,
                "bold": self.label.bold,
            }
        return out

    def to_json(self, **kwargs: Any) -> str:
        return json.dumps(self.to_dict(), **kwargs)

    def copy(self) -> "Style":
        return copy.deepcopy(self)

    @property
    def has_logo(self) -> bool:
        return self.logo is not None and self.logo.size > 0


def recommended_ecc(style: Optional[Style]) -> Ecc:
    """``H`` when the style has a logo, else ``M`` (SPEC 1.3)."""
    return Ecc.H if style is not None and style.has_logo else Ecc.M


@dataclass
class SvgOptions:
    width: float = 0.0  # width attribute in px (0 = omit width/height)
    id_prefix: Optional[str] = "qrk"  # gradient id prefix


@dataclass
class Document:
    """A parsed ``{encode, style, svg}`` document."""

    style: Style = field(default_factory=Style)
    encode: EncodeOptions = field(default_factory=EncodeOptions)
    svg: SvgOptions = field(default_factory=SvgOptions)

    def ecc_for(self) -> Ecc:
        """The explicit ``encode.ecc`` or the default rule (SPEC 1.3)."""
        if self.encode.ecc is not None:
            return self.encode.ecc
        return recommended_ecc(self.style)


# ---- JSON reader ------------------------------------------------------------------
#
# SPEC 2.0: members apply in document order (repeated object keys merge field by
# field, scalars/arrays/paints replace), number tokens are limited to 400
# characters and must be finite, nesting is limited to 64 levels, and every
# error is a "parse error". JSON text is decoded into _Pairs lists so that
# repeated keys survive; mappings given by the caller are read the same way.

MAX_DEPTH = 64
MAX_NUMBER_TOKEN = 400
MAX_INT_FIELD = 1000.0

_STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"', re.DOTALL)
_BRACKET_RE = re.compile(r"[\[\]{}]")


class _Pairs(List[Tuple[str, Any]]):
    """A JSON object as its members in document order (duplicates kept)."""


def _number_token(token: str) -> float:
    if len(token) > MAX_NUMBER_TOKEN:
        raise StyleError("number token longer than %d characters"
                         % MAX_NUMBER_TOKEN)
    v = float(token)  # nearest double
    if not math.isfinite(v):
        raise StyleError("number out of range: %s" % token[:40])
    return v


def _reject_constant(name: str) -> Any:
    raise StyleError("invalid JSON number %s" % name)


def _check_depth(text: str) -> None:
    """Rejects documents nested deeper than MAX_DEPTH (top-level object = 1)."""
    depth = 0
    for match in _BRACKET_RE.finditer(_STRING_RE.sub("", text)):
        if match.group() in "[{":
            depth += 1
            if depth > MAX_DEPTH:
                raise StyleError("nesting deeper than %d levels" % MAX_DEPTH)
        else:
            depth -= 1


def _loads(text: Union[str, bytes, bytearray]) -> Any:
    if isinstance(text, (bytes, bytearray)):
        try:
            text = bytes(text).decode(json.detect_encoding(bytes(text)), "surrogatepass")
        except (UnicodeDecodeError, LookupError) as exc:
            raise StyleError("%s" % exc) from None
    _check_depth(text)
    try:
        return json.loads(text, object_pairs_hook=_Pairs, parse_float=_number_token,
                          parse_int=_number_token, parse_constant=_reject_constant)
    except (ValueError, RecursionError) as exc:  # JSONDecodeError is a ValueError
        if isinstance(exc, StyleError):
            raise
        raise StyleError("%s" % exc) from None


def _check_tree(value: Any, depth: int) -> None:
    """Applies the JSON limits to a mapping given by the caller."""
    if isinstance(value, bool) or value is None or isinstance(value, str):
        return
    if isinstance(value, (int, float)):
        try:
            finite = math.isfinite(value)
        except OverflowError:  # huge int
            finite = False
        if not finite:
            raise StyleError("number out of range: %r" % (value,))
        return
    if isinstance(value, (Mapping, list, tuple)):
        if depth + 1 > MAX_DEPTH:
            raise StyleError("nesting deeper than %d levels" % MAX_DEPTH)
        items = value.values() if isinstance(value, Mapping) else (
            [v for _, v in value] if isinstance(value, _Pairs) else value)
        for item in items:
            _check_tree(item, depth + 1)


def _members(value: Any, key: str) -> List[Tuple[str, Any]]:
    if isinstance(value, _Pairs):
        items: List[Tuple[str, Any]] = value
    elif isinstance(value, Mapping):
        items = list(value.items())
    else:
        raise StyleError("%s: expected an object, got %r" % (key, value))
    return [(c_string(k) if isinstance(k, str) else k, v) for k, v in items]


def _num(value: Any, key: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise StyleError("%s: expected a number, got %r" % (key, value))
    try:
        v = float(value)
    except OverflowError:
        v = math.inf
    if not math.isfinite(v):
        raise StyleError("%s: number out of range" % key)
    return v


def _int(value: Any, key: str) -> int:
    """Integer fields clamp to [-1000, 1000], then truncate toward zero."""
    v = _num(value, key)
    v = -MAX_INT_FIELD if not v >= -MAX_INT_FIELD else (MAX_INT_FIELD if v > MAX_INT_FIELD else v)
    return int(v)


def _bool(value: Any, key: str) -> bool:
    if not isinstance(value, bool):
        raise StyleError("%s: expected true or false, got %r" % (key, value))
    return value


def _opt_str(value: Any, key: str) -> Optional[str]:
    if value is not None and not isinstance(value, str):
        raise StyleError("%s: expected a string or null, got %r" % (key, value))
    return c_string(value) if value is not None else None


def _str(value: Any, key: str) -> str:
    if not isinstance(value, str):
        raise StyleError("%s: expected a string, got %r" % (key, value))
    return c_string(value)


def _color_value(value: Any, key: str) -> Color:
    if value is None:
        return 0
    if not isinstance(value, str):
        raise StyleError("%s: expected a color string, got %r" % (key, value))
    return parse_color(c_string(value))


def _paint_value(value: Any, key: str) -> Paint:
    if value is None:
        return Paint()
    if isinstance(value, Paint):
        return value
    if isinstance(value, str):
        return Paint.solid(parse_color(c_string(value)))
    if isinstance(value, (Mapping, _Pairs)):
        return _parse_gradient(value)
    raise StyleError("%s: invalid paint %r" % (key, value))


def _parse_gradient(obj: Any) -> Paint:
    """A gradient object; a paint always replaces the previous one."""
    ptype = PaintType.LINEAR
    angle = 0.0
    cx = 0.5
    cy = 0.5
    r = 0.75
    stops: List[Stop] = []
    for key, value in _members(obj, "paint"):
        if key == "type":
            t = _str(value, "type")
            if t == "radial":
                ptype = PaintType.RADIAL
            elif t == "linear":
                ptype = PaintType.LINEAR
            else:
                raise StyleError("unknown gradient type: %r" % (t,))
        elif key == "angle":
            angle = _num(value, "angle")
        elif key == "cx":
            cx = _num(value, "cx")
        elif key == "cy":
            cy = _num(value, "cy")
        elif key == "r":
            r = _num(value, "r")
        elif key == "stops":
            if not isinstance(value, (list, tuple)) or isinstance(value, _Pairs):
                raise StyleError("stops: expected an array")
            stops = []
            for item in value:
                offset = 0.0
                color = 0
                for k2, v2 in _members(item, "stop"):
                    if k2 == "offset":
                        offset = _num(v2, "offset")
                    elif k2 == "color":
                        color = _color_value(v2, "color")
                if len(stops) < MAX_STOPS:
                    stops.append(Stop(offset, color))
    return Paint(ptype, stops[0].color if stops else 0, angle, cx, cy, r, tuple(stops))


def _eye_shape(value: Any, key: str, allow_null: bool) -> Optional[str]:
    if value is None:
        if not allow_null:
            raise StyleError("%s: null is not allowed" % key)
        return None
    return _shape_name(_str(value, key), EYE_SHAPES, "eye shape")


class _Reader:
    """Applies document members in order, like the reference reader.

    ``logo: null`` and ``label: null`` reset the feature to its defaults
    (switched off), forgetting earlier members: ``{"logo": {"href": "a"},
    "logo": null, "logo": {}}`` is a default logo (size 0.2, no href).
    """

    def __init__(self, doc: Document) -> None:
        self.doc = doc
        style = doc.style
        self.logo = copy.deepcopy(style.logo) if style.logo is not None else LogoStyle(size=0.0)
        self.label = copy.deepcopy(style.label) if style.label is not None else LabelStyle()
        self.logo_seen = False
        self.label_seen = False

    def finish(self) -> Document:
        if self.logo_seen:
            self.doc.style.logo = self.logo if self.logo.size > 0 else None
        if self.label_seen:
            self.doc.style.label = self.label if self.label.text is not None else None
        return self.doc

    def document(self, data: Any) -> None:
        for key, value in _members(data, "document"):
            if key == "encode":
                self.encode(value)
            elif key == "svg":
                self.svg(value)
            elif key == "style":
                for k2, v2 in _members(value, "style"):
                    self.style_key(k2, v2)
            else:
                self.style_key(key, value)

    def encode(self, value: Any) -> None:
        e = self.doc.encode
        for key, v in _members(value, "encode"):
            if key == "ecc":
                level = _str(v, "encode.ecc")
                if level not in ("L", "M", "Q", "H"):
                    raise StyleError("encode.ecc: invalid level %r" % (level,))
                e.ecc = Ecc[level]
            elif key == "minVersion":
                e.min_version = _int(v, "encode.minVersion")
            elif key == "maxVersion":
                e.max_version = _int(v, "encode.maxVersion")
            elif key == "mask":
                e.mask = _int(v, "encode.mask")
            elif key == "boostEcc":
                e.boost_ecc = _bool(v, "encode.boostEcc")
            elif key == "eci":
                e.eci = _bool(v, "encode.eci")

    def svg(self, value: Any) -> None:
        o = self.doc.svg
        for key, v in _members(value, "svg"):
            if key == "width":
                o.width = _num(v, "svg.width")
            elif key == "idPrefix":
                o.id_prefix = _opt_str(v, "svg.idPrefix")

    def style_key(self, k: str, value: Any) -> None:
        s = self.doc.style
        if k == "margin":
            s.margin = _num(value, "margin")
        elif k == "modules":
            for key, v in _members(value, k):
                if key == "shape":
                    if v is None:
                        raise StyleError("modules.shape: null is not allowed")
                    s.modules.shape = _shape_name(_str(v, key), MODULE_SHAPES, "module shape")
                elif key == "size":
                    s.modules.size = _num(v, "modules.size")
                elif key == "roundness":
                    s.modules.roundness = _num(v, "modules.roundness")
                elif key == "color":
                    s.modules.color = _paint_value(v, "modules.color")
        elif k == "eyes":
            for key, v in _members(value, k):
                if key == "frame":
                    s.eyes.frame = _eye_shape(v, "eyes.frame", False) or "square"
                elif key == "ball":
                    s.eyes.ball = _eye_shape(v, "eyes.ball", False) or "square"
                elif key == "frameColor":
                    s.eyes.frame_color = _paint_value(v, "eyes.frameColor")
                elif key == "ballColor":
                    s.eyes.ball_color = _paint_value(v, "eyes.ballColor")
        elif k == "alignment":
            if value is None:
                s.alignment.frame = None
                s.alignment.ball = None
                return
            for key, v in _members(value, k):
                if key == "frame":
                    s.alignment.frame = _eye_shape(v, "alignment.frame", True)
                elif key == "ball":
                    s.alignment.ball = _eye_shape(v, "alignment.ball", True)
        elif k == "background":
            for key, v in _members(value, k):
                if key == "color":
                    s.background.color = _paint_value(v, "background.color")
                elif key == "radius":
                    s.background.radius = _num(v, "background.radius")
        elif k == "border":
            for key, v in _members(value, k):
                if key == "width":
                    s.border.width = _num(v, "border.width")
                elif key == "color":
                    s.border.color = _color_value(v, "border.color")
        elif k == "logo":
            self.logo_seen = True
            if value is None:
                self.logo = LogoStyle(size=0.0)
                return
            logo = self.logo
            members = _members(value, k)
            if not logo.size > 0:
                logo.size = 0.2
            for key, v in members:
                if key == "href":
                    logo.href = _opt_str(v, "logo.href")
                elif key == "size":
                    logo.size = _num(v, "logo.size")
                elif key == "margin":
                    logo.margin = _num(v, "logo.margin")
                elif key == "excavate":
                    logo.excavate = _bool(v, "logo.excavate")
                elif key == "background":
                    logo.background = _color_value(v, "logo.background")
                elif key == "shape":
                    logo.shape = _shape_name(_str(v, "logo.shape"), LOGO_SHAPES, "logo shape")
        elif k == "label":
            self.label_seen = True
            if value is None:
                self.label = LabelStyle()
                return
            label = self.label
            for key, v in _members(value, k):
                if key == "text":
                    label.text = _opt_str(v, "label.text")
                elif key == "font":
                    label.font = _opt_str(v, "label.font")
                elif key == "size":
                    label.size = _num(v, "label.size")
                elif key == "color":
                    label.color = _color_value(v, "label.color")
                elif key == "bold":
                    label.bold = _bool(v, "label.bold")
                elif key == "position":
                    label.position = _shape_name(_str(v, "label.position"), LABEL_POSITIONS,
                                                 "label position")
        # unknown keys are ignored


def load_document(
    source: Union[str, bytes, Mapping[str, Any], Document, None],
    base: Optional[Document] = None,
) -> Document:
    """Parses ``{"encode": {...}, "style": {...}, "svg": {...}}`` or a bare style.

    ``source`` is JSON text or an already decoded mapping. Members apply in
    document order on top of ``base`` (default: all defaults): repeated
    object keys merge field by field, top-level style keys outside ``"style"``
    are accepted and unknown keys are ignored (SPEC 2.0). Raises
    :class:`StyleError` (kind ``"parse error"``) for invalid documents.
    """
    doc = copy.deepcopy(base) if base is not None else Document()
    if source is None:
        return doc
    if isinstance(source, Document):
        return copy.deepcopy(source)
    if isinstance(source, (str, bytes, bytearray)):
        data = _loads(source)
    else:
        _check_tree(source, 0)
        data = source
    reader = _Reader(doc)
    reader.document(data)
    return reader.finish()
