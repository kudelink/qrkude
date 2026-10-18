# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Unit tests of the public API (beyond the conformance vectors)."""

from __future__ import annotations

import math
from typing import List, Tuple

import pytest

import qrkude
from qrkude import Layer, Paint, Style


# ---- numbers and colors -----------------------------------------------------------


@pytest.mark.parametrize("value, text", [
    (0, "0"), (1, "1"), (-1, "-1"), (0.5, "0.5"), (0.25, "0.25"), (0.125, "0.125"),
    (0.0625, "0.063"), (-0.0625, "-0.062"), (1.0005, "1.001"), (12.3456, "12.346"),
    (-0.0001, "0"), (100, "100"), (0.1 + 0.2, "0.3"), (2.9999, "3"), (-2.5, "-2.5"),
])
def test_format_number(value: float, text: str) -> None:
    assert qrkude.format_number(value) == text


@pytest.mark.parametrize("value", [math.nan, math.inf, -math.inf, 1e15, -1e15, 1e300])
def test_format_number_out_of_range_is_zero(value: float) -> None:
    assert qrkude.format_number(value) == "0"
    assert qrkude.format_number(123456789.25) == "123456789.25"


def test_parse_color() -> None:
    assert qrkude.parse_color("#fff") == 0xFFFFFFFF
    assert qrkude.parse_color("#1a2B3c") == 0x1A2B3CFF
    assert qrkude.parse_color("#11223344") == 0x11223344
    assert qrkude.parse_color("transparent") == 0
    for bad in ("red", "#12345", "#ggg", "", "#", "TRANSPARENT"):
        with pytest.raises(qrkude.StyleError):
            qrkude.parse_color(bad)
    assert qrkude.format_color(0x1A2B3CFF) == "#1a2b3c"
    assert qrkude.format_color(0x1A2B3C80) == "#1a2b3c80"


@pytest.mark.parametrize("literal", [
    "0", "-0", "1", "0.1", "0.35", "12.5", "1e3", "2.5E-1", "1e-7", "-273.15",
    "0.1234567890123456789012", "123456789012345678901234e-23", "3.0000000000000000001",
    "1.7976931348623157e308", "4.94e-324",
])
def test_json_numbers_parse_to_nearest_double(literal: str) -> None:
    doc = qrkude.load_document('{"margin": %s, "svg": {"width": %s}}' % (literal, literal))
    assert doc.style.margin == float(literal)
    assert doc.svg.width == float(literal)


def test_json_number_limits() -> None:
    ok = "0." + "1" * 398  # a 400-character token is accepted
    assert len(ok) == 400
    qrkude.load_document('{"style": {"x": %s}}' % ok)
    for doc in ('{"style": {"x": %s1}}' % ok,  # 401 characters
                '{"margin": 1e999}', '{"future": -1e999}', '{"future": [1, 1e999999999999]}',
                '{"margin": %s}' % ("1" + "0" * 400)):
        with pytest.raises(qrkude.StyleError) as info:
            qrkude.load_document(doc)
        assert info.value.kind == "parse error"
    # caller mappings follow the same rules
    for mapping in ({"margin": math.inf}, {"future": [math.nan]}, {"margin": 10 ** 400}):
        with pytest.raises(qrkude.StyleError):
            qrkude.load_document(mapping)


def test_integer_fields_clamp_then_truncate() -> None:
    enc = qrkude.load_document('{"encode": {"mask": 7.9, "minVersion": 2.99}}').encode
    assert enc.mask == 7 and enc.min_version == 2
    assert qrkude.load_document('{"encode": {"mask": -1.5}}').encode.mask == -1
    enc = qrkude.load_document('{"encode": {"minVersion": 1e100, "maxVersion": -1e100}}').encode
    assert (enc.min_version, enc.max_version) == (1000, -1000)
    with pytest.raises(qrkude.InvalidArgumentError) as info:
        qrkude.svg("x", '{"encode": {"minVersion": 1e100}}')
    assert info.value.kind == "invalid argument"


def test_nesting_limit() -> None:
    def nested(levels: int) -> str:  # the top-level object is level 1
        return '{"style": {"x": ' + "[" * (levels - 2) + "]" * (levels - 2) + "}}"

    qrkude.load_document(nested(64))
    for doc in (nested(65), "[" * 100000 + "]" * 100000, '{"a":' * 50000 + "1" + "}" * 50000):
        with pytest.raises(qrkude.StyleError, match="nesting"):
            qrkude.load_document(doc)
    # brackets inside strings do not count
    qrkude.load_document('{"label": {"text": "%s"}}' % ("[{" * 100))
    deep: dict = {}
    node = deep
    for _ in range(70):
        node["x"] = {}
        node = node["x"]
    with pytest.raises(qrkude.StyleError):
        qrkude.load_document(deep)
    loop: dict = {}
    loop["self"] = loop
    with pytest.raises(qrkude.StyleError):
        qrkude.load_document(loop)


def test_unknown_keys_of_any_length_are_ignored() -> None:
    long_key = "k" * 100
    doc = qrkude.load_document('{"%s": 1, "modules": {"%s": {"a": [1, null]}, "shape": "dot"},'
                               ' "style": {"%s": true}}' % (long_key, long_key, long_key))
    assert doc.style.modules.shape == "dot"


def test_duplicate_members_apply_in_order() -> None:
    s = Style.from_json('{"modules": {"shape": "dot"}, "modules": {"size": 0.8}}')
    assert (s.modules.shape, s.modules.size) == ("dot", 0.8)
    # paints replace: the second gradient starts from the defaults (linear)
    s = Style.from_json('{"modules": {"color": {"type": "radial", "stops": []}},'
                        ' "modules": {"color": {"stops": [{"color": "#f00"}]}}}')
    assert s.modules.color.type == qrkude.PaintType.LINEAR
    # the logo keeps its fields; the default size applies only without a logo
    s = Style.from_json('{"logo": {"size": 0.3}, "logo": {"href": "x.png"}}')
    assert s.logo is not None and (s.logo.size, s.logo.href) == (0.3, "x.png")
    s = Style.from_json('{"logo": {"size": 0.3, "href": "a.png"}, "logo": null}')
    assert s.logo is None
    # null resets to the defaults, forgetting earlier members
    s = Style.from_json('{"logo": {"size": 0.3, "href": "a.png", "margin": 3, "shape": "circle"},'
                        ' "logo": null, "logo": {}}')
    assert s.logo is not None
    assert (s.logo.size, s.logo.href, s.logo.margin, s.logo.shape) == (0.2, None, 1, "square")
    s = Style.from_json('{"label": {"text": "A", "font": "serif"}, "label": null,'
                        ' "label": {"size": 3}}')
    assert s.label is None
    s = Style.from_json('{"label": {"text": "A", "font": "serif", "position": "top"},'
                        ' "label": null, "label": {"text": "B"}}')
    assert s.label is not None
    assert (s.label.text, s.label.font, s.label.position) == ("B", "sans-serif", "bottom")
    s = Style.from_json('{"alignment": {"frame": "circle", "ball": "dots"}, "alignment": null}')
    assert (s.alignment.frame, s.alignment.ball) == (None, None)
    doc = qrkude.load_document('{"encode": {"ecc": "L"}, "encode": {"mask": 3},'
                               ' "style": {"margin": 1}, "margin": 2, "style": {"eyes": {}}}')
    assert (doc.encode.ecc, doc.encode.mask, doc.style.margin) == (qrkude.Ecc.L, 3, 2)


def test_logo_default_ecc_ignores_unknown_ecc_keys() -> None:
    doc = qrkude.load_document('{"style": {"future": {"ecc": "L"}, "logo": {}}}')
    assert doc.encode.ecc is None and doc.ecc_for() == qrkude.Ecc.H
    doc = qrkude.load_document(r'{"encode": {"\u0065cc": "L"}}')
    assert doc.encode.ecc == qrkude.Ecc.L


# ---- encoder ----------------------------------------------------------------------


def _version_for(text: str, ecc: str) -> int:
    try:
        return qrkude.encode(text, ecc=ecc, boost_ecc=False).version
    except qrkude.DataTooLongError:
        return -1


def test_capacity_table() -> None:
    # Version 1 capacities from ISO/IEC 18004 table 7 (numeric, alnum, byte).
    caps = {"L": (41, 25, 17), "M": (34, 20, 14), "Q": (27, 16, 11), "H": (17, 10, 7)}
    for ecc, (num, alnum, byte) in caps.items():
        for fill, n in (("7", num), ("K", alnum), ("k", byte)):
            assert _version_for(fill * n, ecc) == 1
            assert _version_for(fill * (n + 1), ecc) == 2
    assert _version_for("k" * 2953, "L") == 40
    assert _version_for("k" * 2954, "L") == -1
    assert _version_for("", "M") == 1


def test_modes_and_options() -> None:
    assert qrkude.encode("0123456789").mode == qrkude.Mode.NUMERIC
    assert qrkude.encode("HELLO WORLD $%*+-./:").mode == qrkude.Mode.ALPHANUMERIC
    assert qrkude.encode("hello").mode == qrkude.Mode.BYTE
    assert qrkude.encode(b"\x00\xff").mode == qrkude.Mode.BYTE
    assert qrkude.encode("").mode == qrkude.Mode.NUMERIC
    assert qrkude.encode("mask", mask=5).mask == 5
    code = qrkude.encode("v7", min_version=7)
    assert code.version == 7 and code.size == 45
    # boost: a short text at version 1 ends up with ECC H
    code = qrkude.encode("1")
    assert code.ecc == qrkude.Ecc.H and str(code.ecc) == "H"
    assert qrkude.encode("1", boost_ecc=False).ecc == qrkude.Ecc.M
    # finder corners and the dark module
    assert code.is_dark(0, 0) and code.is_dark(6, 6) and not code.is_dark(7, 7)
    assert code.is_dark(8, code.size - 8)
    assert not code.is_dark(-1, 0) and not code.is_dark(code.size, 0)
    assert len(code.matrix()) == code.size
    assert "version=1" in repr(code)


def test_encode_options_object_and_overrides() -> None:
    opts = qrkude.EncodeOptions(ecc="Q", boost_ecc=False)
    assert qrkude.encode("abc", opts).ecc == qrkude.Ecc.Q
    assert qrkude.encode("abc", opts, ecc="L").ecc == qrkude.Ecc.L


@pytest.mark.parametrize("kwargs", [
    {"min_version": 0}, {"max_version": 41}, {"min_version": 5, "max_version": 4},
    {"mask": 8}, {"mask": -2},
])
def test_encode_invalid_arguments(kwargs: dict) -> None:
    with pytest.raises(qrkude.InvalidArgumentError) as info:
        qrkude.encode("x", **kwargs)
    assert info.value.kind == "invalid argument"


def test_encode_invalid_ecc() -> None:
    with pytest.raises(qrkude.QRKudeError):
        qrkude.encode("x", ecc="X")


def test_data_too_long() -> None:
    with pytest.raises(qrkude.DataTooLongError) as info:
        qrkude.encode("y" * 100, max_version=3)
    assert info.value.kind == "data too long"


def test_encode_for_style() -> None:
    logo = Style.from_json({"logo": {}})
    assert qrkude.encode_for_style("x", None, logo).ecc == qrkude.Ecc.H
    assert qrkude.encode_for_style("x", None, Style(), ).ecc == qrkude.Ecc.H  # boosted
    assert qrkude.encode_for_style("x" * 30, qrkude.EncodeOptions(boost_ecc=False),
                                   Style()).ecc == qrkude.Ecc.M
    assert qrkude.encode_for_style("x", qrkude.EncodeOptions(ecc="L", boost_ecc=False),
                                   logo).ecc == qrkude.Ecc.L


# ---- style model ------------------------------------------------------------------


def test_style_defaults() -> None:
    s = Style()
    assert s.margin == 4
    assert s.modules.shape == "square" and s.modules.size == 1 and s.modules.roundness == 1
    assert s.modules.color == Paint.solid("#000000")
    assert s.eyes.frame == "square" and s.eyes.frame_color.type == qrkude.PaintType.NONE
    assert s.alignment.frame is None
    assert s.background.color == Paint.solid("#ffffff")
    assert s.logo is None and s.label is None
    assert qrkude.recommended_ecc(s) == qrkude.Ecc.M


def test_style_from_json_document() -> None:
    doc = qrkude.load_document(
        '{"encode": {"ecc": "Q", "mask": 3}, "svg": {"width": 300, "idPrefix": "x"},'
        ' "style": {"modules": {"shape": "fluid"}}, "margin": 2, "logo": {}}')
    assert doc.encode.ecc == qrkude.Ecc.Q and doc.encode.mask == 3
    assert doc.svg.width == 300 and doc.svg.id_prefix == "x"
    assert doc.style.modules.shape == "fluid" and doc.style.margin == 2
    assert doc.style.logo is not None and doc.style.logo.size == 0.2
    assert Style.from_json({"modules": {"shape": "dot"}}).modules.shape == "dot"


def test_default_ecc_rule() -> None:
    assert qrkude.load_document({}).ecc_for() == qrkude.Ecc.M
    assert qrkude.load_document({"logo": {"href": "x.png"}}).ecc_for() == qrkude.Ecc.H
    assert qrkude.load_document({"logo": {"size": 0}}).ecc_for() == qrkude.Ecc.M
    assert qrkude.load_document({"encode": {"ecc": "L"}, "logo": {}}).ecc_for() == qrkude.Ecc.L
    svg_text = qrkude.svg("https://kudelink.com/logo", {"logo": {"href": "x.png"}})
    code = qrkude.encode("https://kudelink.com/logo", ecc="H")
    assert svg_text == qrkude.to_svg(code, Style.from_json({"logo": {"href": "x.png"}}))


@pytest.mark.parametrize("doc", [
    '{"modules": {"shape": null}}', '{"margin": "x"}', '{"logo": 5}', '{"eyes": {"frame": "x"}}',
    '{"modules": {"color": 5}}', '{"label": {"position": "middle"}}', '{"encode": {"ecc": "X"}}',
    '{"background": null}', '{"logo": {"excavate": 1}}', '{"modules": {"color": "red"}}',
    '{"modules": {"color": {"type": "conic"}}}', '[]', '{"margin": NaN}', '{"margin": 1',
    '{"svg": {"width": true}}',
])
def test_style_errors(doc: str) -> None:
    with pytest.raises(qrkude.StyleError):
        qrkude.load_document(doc)


def test_style_null_handling() -> None:
    doc = qrkude.load_document('{"alignment": null, "logo": null, "label": null,'
                               ' "background": {"color": null}, "eyes": {"ballColor": null}}')
    assert doc.style.alignment.frame is None and doc.style.logo is None
    assert doc.style.background.color.type == qrkude.PaintType.NONE
    assert "transparent-background" in qrkude.lint(qrkude.encode("x"), doc.style)


def test_dataclass_coercion() -> None:
    s = qrkude.Style(
        modules=qrkude.ModuleStyle(shape=qrkude.ModuleShape.DOT, color="#ff0000"),
        eyes=qrkude.EyeStyle(frame="circle", ball=qrkude.EyeShape.DOTS, frame_color=None),
        border=qrkude.BorderStyle(width=1, color="#00ff0080"),
        logo=qrkude.LogoStyle(background="#fff"),
        label=qrkude.LabelStyle(text="Hi", color="#123"),
    )
    assert s.modules.shape == "dot" and s.modules.color == Paint.solid(0xFF0000FF)
    assert s.eyes.ball == "dots" and s.border.color == 0x00FF0080
    assert s.logo is not None and s.logo.background == 0xFFFFFFFF
    with pytest.raises(qrkude.StyleError):
        qrkude.ModuleStyle(shape="hexagon")
    with pytest.raises(qrkude.StyleError):
        qrkude.LogoStyle(shape="star")


def test_paint_helpers() -> None:
    p = Paint.linear(45, "#f00", "#00f")
    assert p.type == qrkude.PaintType.LINEAR and p.angle == 45
    assert [s.offset for s in p.stops] == [0, 1]
    r = Paint.radial("#fff", (0.5, "#888"), "#000", r=0.6)
    assert [s.offset for s in r.stops] == [0, 0.5, 1] and r.r == 0.6
    assert Paint.from_value(p.to_value()) == p
    assert Paint.from_value(None).type == qrkude.PaintType.NONE


# ---- geometry ---------------------------------------------------------------------


class Recorder:
    def __init__(self) -> None:
        self.ops: List[Tuple] = []

    def move_to(self, x: float, y: float) -> None:
        self.ops.append(("M", x, y))

    def line_to(self, x: float, y: float) -> None:
        self.ops.append(("L", x, y))

    def arc_to(self, r: float, clockwise: bool, x: float, y: float, cx: float,
               cy: float) -> None:
        self.ops.append(("A", r, clockwise, x, y, cx, cy))

    def close(self) -> None:
        self.ops.append(("Z",))


def test_emit_layer_eye_frames() -> None:
    code = qrkude.encode("QRKude")
    rec = Recorder()
    n = qrkude.emit_layer(code, Style(), Layer.EYE_FRAMES, rec)
    assert n == 6  # 3 finders x (outer + hole)
    # top-left finder: outer clockwise square, then the counter-clockwise hole
    assert rec.ops[:10] == [
        ("M", 4, 4), ("L", 11, 4), ("L", 11, 11), ("L", 4, 11), ("Z",),
        ("M", 5, 5), ("L", 5, 10), ("L", 10, 10), ("L", 10, 5), ("Z",),
    ]
    assert qrkude.emit_layer(code, Style(), Layer.EYE_FRAMES) == 6


def test_emit_layer_arcs_and_counts() -> None:
    code = qrkude.encode("QRKude")
    style = Style.from_json({"modules": {"shape": "dot"}, "eyes": {"frame": "circle"}})
    rec = Recorder()
    count = qrkude.emit_layer(code, style, Layer.MODULES, rec)
    assert count == sum(1 for op in rec.ops if op[0] == "M") > 0
    arcs = [op for op in rec.ops if op[0] == "A"]
    assert arcs and all(op[1] == 0.5 and op[2] is True for op in arcs)
    for op in arcs:  # end point lies on the circle around the center
        assert math.isclose(math.hypot(op[3] - op[5], op[4] - op[6]), op[1])
    assert qrkude.emit_layer(code, style, Layer.LOGO_BACKGROUND, rec) == 0
    assert qrkude.emit_layer(code, style, Layer.BORDER, rec) == 0
    assert qrkude.emit_layer(code, style, Layer.BACKGROUND) == 1


def test_layout_and_paints() -> None:
    code = qrkude.encode("QRKude")
    style = Style.from_json({"margin": 2, "border": {"width": 1}, "label": {"text": "x", "size": 2},
                             "logo": {"size": 0.2}, "eyes": {"ballColor": "#f00"}})
    lay = qrkude.get_layout(code, style)
    assert lay.width == 21 + 6 and lay.height == lay.width + 3
    assert lay.symbol_x == 3 and lay.symbol_y == 3 and lay.modules == 21
    assert lay.has_logo and math.isclose(lay.logo_size, 4.2)
    assert lay.has_label and lay.label_x == lay.width / 2
    assert lay.label_baseline == pytest.approx(27 - 1 + 2.2)
    assert qrkude.layer_paint(style, Layer.EYE_BALLS) == Paint.solid("#f00")
    assert qrkude.layer_paint(style, Layer.EYE_FRAMES) == style.modules.color
    g = qrkude.gradient_geometry(code, Style.from_json(
        {"modules": {"color": {"angle": 90, "stops": []}}}), Layer.MODULES)
    assert (g.x1, g.y1, g.x2, g.y2) == (14.5, 4.0, 14.5, 25.0)


# ---- lint and logo damage -----------------------------------------------------------


def test_lint_flags() -> None:
    code = qrkude.encode("https://kudelink.com")
    assert qrkude.lint(code, Style()) == []
    s = Style.from_json({"margin": 1, "modules": {"shape": "dot", "size": 0.5,
                                                  "color": "#dddddd"}})
    assert qrkude.lint(code, s) == ["low-contrast", "quiet-zone", "small-modules"]
    dark = Style.from_json({"modules": {"color": "#ffffff"}, "background": {"color": "#000000"}})
    assert qrkude.lint(code, dark) == ["inverted"]
    assert qrkude.LINT_FLAGS[0] == "logo-risky"


@pytest.mark.parametrize("frame, ball, flagged", [
    ("diamond", "diamond", True), ("diamond", "circle", True), ("square", "diamond", True),
    ("leaf", "diamond", True), ("circle", "diamond", False), ("rounded", "diamond", False),
    ("bevel", "diamond", False), ("square", "square", False), ("dots", "dots", False),
])
def test_lint_eye_shapes(frame: str, ball: str, flagged: bool) -> None:
    code = qrkude.encode("https://kudelink.com")
    style = Style.from_json({"eyes": {"frame": frame, "ball": ball}})
    assert ("eye-shapes" in qrkude.lint(code, style)) == flagged


def test_logo_damage() -> None:
    code = qrkude.encode("https://kudelink.com/logo", ecc="H")
    assert qrkude.logo_damage(code, Style()) == 0
    small = qrkude.logo_damage(code, Style.from_json({"logo": {"size": 0.15}}))
    big = qrkude.logo_damage(code, Style.from_json({"logo": {"size": 0.4}}))
    assert 0 < small < big
    assert "logo-unreadable" in qrkude.lint(code, Style.from_json({"logo": {"size": 0.9}}))


# ---- payloads -----------------------------------------------------------------------


def test_payload_builders() -> None:
    assert qrkude.wifi("My;Net", 'p@ss:"word"') == 'WIFI:T:WPA;S:My\\;Net;P:p@ss\\:\\"word\\";;'
    assert qrkude.wifi("Open", "ignored", "nopass", True) == "WIFI:T:nopass;S:Open;H:true;;"
    assert qrkude.email("a@b.c", "Hi there", "x&y=z") == \
        "mailto:a@b.c?subject=Hi%20there&body=x%26y%3Dz"
    assert qrkude.email("a@b.c", None, "only body") == "mailto:a@b.c?body=only%20body"
    assert qrkude.sms("+34600000000", "Hola") == "SMSTO:+34600000000:Hola"
    assert qrkude.tel("+1 555") == "tel:+1 555"
    assert qrkude.geo(40.4168, -3.7038) == "geo:40.4168,-3.7038"
    assert qrkude.geo(0, 1.5) == "geo:0,1.5"
    assert qrkude.vcard(first_name="Ana", last_name="García", organization="Kude; Labs") == (
        "BEGIN:VCARD\nVERSION:3.0\nN:García;Ana;;;\nFN:Ana García\nORG:Kude\\; Labs\nEND:VCARD")
    assert qrkude.event("Launch", start="20261017T100000Z", location="Online") == (
        "BEGIN:VEVENT\nSUMMARY:Launch\nDTSTART:20261017T100000Z\nLOCATION:Online\nEND:VEVENT")
    assert qrkude.event(None) == "BEGIN:VEVENT\nSUMMARY:\nEND:VEVENT"


# ---- hardening: id prefix, clamps, lint, payloads --------------------------------------


@pytest.mark.parametrize("prefix, expected", [
    (None, "qrk"), ("", "qrk"), ("qrk", "qrk"), ("ks", "ks"), ("a_b-c9", "a_b-c9"),
    ("1abc", "q1abc"), ("-x", "q-x"), ("_x", "_x"), ("ñ-ü", "__-__"), ("a b", "a_b"),
    ('x"/><svg onload="alert(1)">', "x____svg_onload__alert_1___"), ("a\x00b", "a"),
    ("\x00x", "qrk"), ("\ud800", "___"),
])
def test_id_prefix_sanitized(prefix: object, expected: str) -> None:
    assert qrkude.sanitize_id_prefix(prefix) == expected  # type: ignore[arg-type]


def test_id_prefix_cannot_inject_markup() -> None:
    import xml.etree.ElementTree as ET

    hostile = 'x"/><svg onload="globalThis.__x=1"></svg><linearGradient id="y'
    out = qrkude.svg("x", {"modules": {"color": {"stops": [{"color": "#000"}]}},
                           "svg": {"idPrefix": hostile}})
    root = ET.fromstring(out)
    assert not any("onload" in e.attrib for e in root.iter())
    assert len(list(root.iter("{http://www.w3.org/2000/svg}svg"))) == 1


def test_length_clamps() -> None:
    code = qrkude.encode("QRKude")
    lay = qrkude.get_layout(code, Style.from_json({"margin": 1e6, "border": {"width": 5000}}))
    assert lay.width == 21 + 2 * (1000 + 1000)
    lay = qrkude.get_layout(code, Style.from_json({"label": {"text": "x", "size": 1e9}}))
    assert lay.label_size == 1000 and lay.height == lay.width + 1500
    lay = qrkude.get_layout(code, Style.from_json({"label": {"text": "x", "size": -3}}))
    assert lay.label_size == 0.1
    s = Style()
    s.margin = math.nan  # programmatic only: NaN clamps to the lower bound
    s.modules.size = math.nan
    assert qrkude.get_layout(code, s).width == 21
    assert 'viewBox="0 0 21 21"' in qrkude.to_svg(code, s)


def test_layer_visible() -> None:
    code = qrkude.encode("QRKude")
    s = Style.from_json({"modules": {"color": None}, "background": {"color": "#ffffff00"}})
    assert not qrkude.layer_visible(code, s, Layer.MODULES)
    assert not qrkude.layer_visible(code, s, Layer.BACKGROUND)
    assert not qrkude.layer_visible(code, s, Layer.BORDER)
    assert qrkude.layer_visible(code, Style(), Layer.MODULES)
    assert qrkude.layer_visible(code, Style.from_json({"border": {"width": 1}}), Layer.BORDER)
    assert not qrkude.layer_visible(code, Style.from_json({"logo": {}}), Layer.LOGO_BACKGROUND)
    assert qrkude.layer_visible(code, Style.from_json({"logo": {"background": "#fff"}}),
                                Layer.LOGO_BACKGROUND)
    # a gradient without stops paints nothing
    assert not qrkude.layer_visible(code, Style.from_json({"modules": {"color": {"stops": []}}}),
                                    Layer.MODULES)
    clear = {"stops": [{"color": "#ff000000"}, {"offset": 1, "color": "#00000000"}]}
    assert not qrkude.layer_visible(code, Style.from_json({"modules": {"color": clear}}),
                                    Layer.MODULES)
    half = {"stops": [{"color": "#ff000000"}, {"offset": 1, "color": "#00000001"}]}
    assert qrkude.layer_visible(code, Style.from_json({"modules": {"color": half}}),
                                Layer.MODULES)
    assert qrkude.layer_visible(code, Style.from_json({"background": {"color": half}}),
                                Layer.BACKGROUND)


@pytest.mark.parametrize("style, flags", [
    ({"modules": {"color": None}}, ["low-contrast", "invisible"]),
    ({"modules": {"color": "#00000000"}, "eyes": {"frameColor": "#000"}},
     ["low-contrast", "invisible"]),
    ({"modules": {"color": {"stops": []}}}, ["low-contrast", "invisible"]),
    ({"modules": {"color": "#00000080"}, "background": {"color": "#000000"}}, ["low-contrast"]),
    ({"modules": {"color": "#00000080"}}, []),
    ({"background": {"color": {"stops": [{"color": "#000"}, {"offset": 1, "color": "#fff"}]}}},
     ["low-contrast"]),
    ({"background": {"color": {"stops": [{"color": "#ffffff00"},
                                         {"offset": 1, "color": "#00000000"}]}}},
     ["transparent-background"]),
    ({"background": {"color": "#00000000"}, "modules": {"color": "#fff"}},
     ["low-contrast", "transparent-background"]),
])
def test_lint_contrast_model(style: dict, flags: List[str]) -> None:
    code = qrkude.encode("https://kudelink.com")
    assert qrkude.lint(code, Style.from_json(style)) == flags


def test_geo_clamps() -> None:
    assert qrkude.geo(91, 181) == "geo:90,180"
    assert qrkude.geo(-1e300, -200.5) == "geo:-90,-180"
    assert qrkude.geo(math.nan, math.inf) == "geo:-90,180"


def test_error_kinds() -> None:
    assert qrkude.StyleError("x").kind == "parse error"
    assert qrkude.InvalidArgumentError("x").kind == "invalid argument"
    assert qrkude.DataTooLongError().kind == "data too long"
    assert issubclass(qrkude.StyleError, qrkude.QRKudeError)



def test_strings_end_at_nul_and_repair_surrogates() -> None:
    doc = qrkude.load_document(
        r'{"svg": {"idPrefix": "\u0000x"}, "modules\u0000junk": {"shape": "dot\u0000xyz"},'
        r' "label": {"text": "ab\u0000cd", "font": "\ud800x"},'
        r' "logo": {"href": "\u0000", "background": "#f00\u0000zz"}}')
    assert doc.svg.id_prefix == "" and doc.style.modules.shape == "dot"
    assert doc.style.label is not None
    assert (doc.style.label.text, doc.style.label.font) == ("ab", "�x")
    assert doc.style.logo is not None and doc.style.logo.href == ""
    assert doc.style.logo.background == 0xFF0000FF  # colors and enums truncate too
    doc = qrkude.load_document(r'{"label": {"text": "x", "position": "top\u0000x"},'
                               r' "encode": {"ecc": "L\u0000xx"}}')
    assert doc.style.label is not None and doc.style.label.position == "top"
    assert doc.encode.ecc == qrkude.Ecc.L
    s = Style.from_json(r'{"label": {"text": "lone \ud800 high, \udc00 low, pair 😀,'
                        r' swapped \udc00\ud800, \ud800A"}}')
    assert s.label is not None
    assert s.label.text == "lone � high, � low, pair \U0001F600, swapped ��," \
                           " �A"
    assert qrkude.c_string("a\x00b\udfff") == "a"
    assert qrkude.c_string("x\udfff") == "x�"


def test_svg_text_drops_xml_invalid_controls() -> None:
    style = Style.from_json(r'{"label": {"text": "a\u0001b\u001fc\td\nend", "font": "x\u0007y"},'
                            r' "logo": {"href": "data:,a\u0002b", "size": 0.1}}')
    out = qrkude.svg("https://kudelink.com", style.to_dict())
    assert 'xlink:href="data:,ab"' in out and 'font-family="xy"' in out
    assert ">abc\td\nend</text>" in out
    # programmatic strings get the same treatment (and stay encodable)
    style.label = qrkude.LabelStyle(text="\x00hidden")
    assert "<text" not in qrkude.to_svg(qrkude.encode("x"), style)
    style.label = qrkude.LabelStyle(text="a\udc00")
    qrkude.to_svg(qrkude.encode("x"), style).encode("utf-8")
