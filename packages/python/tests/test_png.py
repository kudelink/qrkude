# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""PNG backend: smoke tests, decoding (zxing-cpp) and comparison with cairosvg."""

from __future__ import annotations

import base64
import io
import zlib
from typing import Any, Optional

import pytest

import qrkude
from conftest import doc_of, load_vectors

pytest.importorskip("PIL")
from PIL import Image, ImageChops, ImageDraw, ImageStat  # noqa: E402

try:
    import zxingcpp
except ImportError:  # pragma: no cover
    zxingcpp = None

try:
    import cairosvg
except (ImportError, OSError):  # pragma: no cover - cairosvg needs the cairo library
    cairosvg = None

# vectors that render (error cases have no output)
RENDER = [c for c in load_vectors("render.json") if "error" not in c]
# styles that lint flags as unreadable or risky (they fail with cairosvg too, at
# least with some zxing-cpp versions)
_RISKY = {"logo-unreadable", "eye-shapes", "small-modules", "low-contrast", "invisible"}

needs_zxing = pytest.mark.skipif(zxingcpp is None, reason="zxing-cpp not installed")
needs_cairo = pytest.mark.skipif(cairosvg is None, reason="cairosvg not installed")


def _flatten(img: "Image.Image") -> "Image.Image":
    bg = Image.new("RGBA", img.size, "white")
    bg.alpha_composite(img.convert("RGBA"))
    return bg.convert("RGB")


def _decode(img: "Image.Image") -> list:
    gray = _flatten(img).convert("L")
    try:
        results = zxingcpp.read_barcodes(gray)
    except TypeError:  # older Pillow + zxing-cpp combinations need an array
        import numpy

        results = zxingcpp.read_barcodes(numpy.asarray(gray))
    return [r.text for r in results]


def _mean_diff(a: "Image.Image", b: "Image.Image") -> float:
    diff = ImageChops.difference(_flatten(a), _flatten(b))
    return sum(ImageStat.Stat(diff).mean) / 3


def _render_case(case: Any, size: int = 400) -> "tuple[Image.Image, qrkude.Document]":
    source = case["json"] if "json" in case else doc_of(case)
    doc = qrkude.load_document(source)
    code = qrkude.encode_for_style(case["text"], doc.encode, doc.style)
    return qrkude.to_image(code, doc.style, size), doc


def _logo(w: int = 80, h: int = 50) -> "Image.Image":
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((2, 2, w - 3, h - 3), fill=(230, 57, 70, 255))
    d.rectangle((w * 0.3, h * 0.3, w * 0.7, h * 0.7), fill=(255, 255, 255, 255))
    return img


def _png_bytes(img: "Image.Image") -> bytes:
    out = io.BytesIO()
    img.save(out, "PNG")
    return out.getvalue()


def _module_px(case: Any, size: int = 400) -> float:
    """Module size in pixels when the case is rendered ``size`` px wide."""
    doc = qrkude.load_document(case["json"] if "json" in case else doc_of(case))
    code = qrkude.encode_for_style(case["text"], doc.encode, doc.style)
    return size / qrkude.get_layout(code, doc.style).width


def _params(predicate: Any) -> list:
    cases = [c for c in RENDER if predicate(c)]
    if not cases:
        return [pytest.param(None, marks=pytest.mark.skip(reason="vectors not found"))]
    return [pytest.param(c, id=c["name"]) for c in cases]


# ---- smoke tests -----------------------------------------------------------------------


def test_to_png_bytes_and_size() -> None:
    code = qrkude.encode("https://kudelink.com")
    data = qrkude.to_png(code, qrkude.Style(), 300)
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    img = Image.open(io.BytesIO(data))
    assert img.size == (300, 300) and img.mode == "RGBA"
    labeled = qrkude.Style.from_json({"label": {"text": "SCAN ME"}})
    lay = qrkude.get_layout(code, labeled)
    img = qrkude.to_image(code, labeled, 330)
    assert img.size == (330, int(330 * lay.height / lay.width + 0.5))
    assert qrkude.to_image(code, None, 100, height=150, supersample=1).size == (100, 150)
    with pytest.raises(ValueError):
        qrkude.to_image(code, None, 0)


def test_modules_are_drawn_where_the_matrix_is_dark() -> None:
    code = qrkude.encode("QRKude")  # 21 modules + 2 x 4 margin = 29
    img = qrkude.to_image(code, qrkude.Style(), 290).convert("RGBA")  # 10 px per module
    for y in range(code.size):
        for x in range(code.size):
            px = img.getpixel((40 + x * 10 + 5, 40 + y * 10 + 5))
            assert px == ((0, 0, 0, 255) if code.is_dark(x, y) else (255, 255, 255, 255))
    assert img.getpixel((5, 5)) == (255, 255, 255, 255)


def test_transparent_background() -> None:
    code = qrkude.encode("QRKude")
    img = qrkude.to_image(code, qrkude.Style.from_json({"background": {"color": None}}), 290)
    assert img.getpixel((5, 5))[3] == 0
    assert img.getpixel((45, 45)) == (0, 0, 0, 255)


def test_gradient_direction() -> None:
    code = qrkude.encode("QRKude")
    style = qrkude.Style.from_json({"modules": {"color": {
        "type": "linear", "angle": 0, "stops": [{"offset": 0, "color": "#ff0000"},
                                                {"offset": 1, "color": "#0000ff"}]}}})
    img = qrkude.to_image(code, style, 290)
    left = img.getpixel((45, 45))  # top-left finder (inherits the module paint)
    right = img.getpixel((290 - 45, 45))  # top-right finder
    assert left[0] > 200 and left[2] < 60
    assert right[2] > 200 and right[0] < 60


def test_logo_sources_and_aspect_fit() -> None:
    text = "https://kudelink.com/logo"
    style = qrkude.Style.from_json({"logo": {"size": 0.25, "background": "#ffffff"}})
    code = qrkude.encode(text, ecc="H")
    logo = _logo()
    by_image = qrkude.to_image(code, style, 400, logo)
    center = by_image.getpixel((200, 200 - 15))  # inside the ellipse, above the white box
    assert center[0] > 200 and center[1] < 100
    # wide logo: the box is fitted horizontally, so rows above the logo stay clear
    lay = qrkude.get_layout(code, style)
    scale = 400 / lay.width
    top = int(lay.logo_y * scale) + 2
    assert by_image.getpixel((200, top)) == (255, 255, 255, 255)

    uri = "data:image/png;base64," + base64.b64encode(_png_bytes(logo)).decode()
    style_uri = qrkude.Style.from_json({"logo": {"size": 0.25, "background": "#ffffff",
                                                 "href": uri}})
    by_uri = qrkude.to_image(code, style_uri, 400)
    assert _mean_diff(by_image, by_uri) < 0.01
    by_bytes = qrkude.to_image(code, style, 400, _png_bytes(logo))
    assert _mean_diff(by_image, by_bytes) < 0.01
    if zxingcpp is not None:
        assert _decode(by_image) == [text]


def test_label_fonts() -> None:
    code = qrkude.encode("https://kudelink.com")
    for font in ("sans-serif", "serif", "monospace", "No Such Font", None):
        style = qrkude.Style.from_json({"label": {"text": "SCAN ME", "font": font,
                                                  "color": "#ff0000"}})
        img = qrkude.to_image(code, style, 300)
        band = img.crop((0, 300, 300, img.height)).convert("RGB")
        colors = band.getcolors(1 << 16) or []
        assert any(c[0] > 200 and c[1] < 80 and c[2] < 80 for _, c in colors), font
        no_label = qrkude.to_image(code, style, 300, label=False).crop((0, 300, 300, img.height))
        assert no_label.convert("RGB").getcolors(4) == [(no_label.width * no_label.height,
                                                         (255, 255, 255))]


# ---- decoding ----------------------------------------------------------------------------


@needs_zxing
@pytest.mark.parametrize("case", _params(
    lambda c: c["text"] and not (_RISKY & set(c["lint"])) and _module_px(c) >= 3))
def test_png_decodes(case: Optional[Any]) -> None:
    img, _ = _render_case(case)
    assert _decode(img) == [case["text"]]


@needs_zxing
@pytest.mark.parametrize("kind", ["wifi", "vcard", "event"])
def test_payload_png_decodes(kind: str) -> None:
    text = {
        "wifi": qrkude.wifi("Kude", "secret"),
        "vcard": qrkude.vcard("Ana", "García", phone="+34 600", email="ana@kudelink.com"),
        "event": qrkude.event("Launch", "20261017T100000Z", location="Online"),
    }[kind]
    img = qrkude.to_image(qrkude.encode(text), qrkude.Style.from_json(
        {"modules": {"shape": "liquid"}, "eyes": {"frame": "circle", "ball": "circle"}}), 512)
    assert _decode(img) == [text]


# ---- comparison with cairosvg ------------------------------------------------------------


def _cairo(case: Any, img: "Image.Image") -> "Image.Image":
    return Image.open(io.BytesIO(cairosvg.svg2png(
        bytestring=case["output"].encode(), output_width=img.width, output_height=img.height)))


@needs_zxing
@needs_cairo
@pytest.mark.parametrize("case", _params(lambda c: True))
def test_png_decodes_like_cairosvg(case: Optional[Any]) -> None:
    img, _ = _render_case(case)
    assert _decode(img) == _decode(_cairo(case, img))


@needs_cairo
@pytest.mark.parametrize("case", _params(lambda c: True))
def test_png_matches_cairosvg(case: Optional[Any]) -> None:
    img, doc = _render_case(case)
    ref = _cairo(case, img)
    # Text rendering (fonts, glyph fallback) differs between FreeType/Pillow
    # and cairo: the geometry is compared outside the label band.
    code = qrkude.encode_for_style(case["text"], doc.encode, doc.style)
    lay = qrkude.get_layout(code, doc.style)
    if lay.has_label:
        scale = img.width / lay.width
        band = int(lay.label_size * 1.5 * scale) + 2
        top = doc.style.label is not None and doc.style.label.position == "top"
        box = (0, band, img.width, img.height) if top else (0, 0, img.width, img.height - band)
        img, ref = img.crop(box), ref.crop(box)
    diff = _mean_diff(img, ref)
    assert diff < 2.0, "mean difference %.3f" % diff


# ---- hostile inputs ------------------------------------------------------------------------


@pytest.mark.parametrize("data", [b"", b"\x00", b"\x89PNG\r\n\x1a\n", b"GIF89a\x01\x00",
                                  b"\x89PNG\r\n\x1a\n" + b"\x00" * 64])
def test_invalid_logo_bytes_fail_cleanly(data: bytes) -> None:
    code = qrkude.encode("x", ecc="H")
    with pytest.raises(qrkude.QRKudeError, match="logo"):
        qrkude.to_png(code, qrkude.Style.from_json({"logo": {}}), 64, data)


def test_oversized_logo_is_refused_before_decoding() -> None:
    # a tiny PNG header that declares 20000 x 20000 pixels
    header = Image.new("L", (1, 1))
    raw = io.BytesIO()
    header.save(raw, "PNG")
    data = bytearray(raw.getvalue())
    data[16:24] = (5000).to_bytes(4, "big") + (5000).to_bytes(4, "big")  # > 2^24 pixels
    data[29:33] = zlib.crc32(bytes(data[12:29])).to_bytes(4, "big")  # IHDR checksum
    code = qrkude.encode("x", ecc="H")
    with pytest.raises(qrkude.QRKudeError, match="too large"):
        qrkude.to_png(code, qrkude.Style.from_json({"logo": {}}), 64, bytes(data))


def test_untrusted_logo_href_is_ignored_when_invalid() -> None:
    code = qrkude.encode("https://kudelink.com/logo", ecc="H")
    for href in ("data:image/png;base64,iVBORw0KGgo=", "data:image/png;base64,!!!",
                 "data:image/svg+xml;base64,PHN2Zy8+", "https://example.com/x.png"):
        style = qrkude.Style.from_json({"logo": {"href": href}})
        assert qrkude.to_image(code, style, 64).size == (64, 64)


@pytest.mark.parametrize("size", [0, -5, 16385, 10 ** 9])
def test_image_size_limits(size: int) -> None:
    with pytest.raises(qrkude.QRKudeError, match="out of range"):
        qrkude.to_image(qrkude.encode("x"), None, size)


def test_total_pixel_limit() -> None:
    with pytest.raises(qrkude.QRKudeError):
        qrkude.to_image(qrkude.encode("x"), None, 8193)
    # a huge (clamped) label makes the canvas tall: refused at this width
    style = qrkude.Style.from_json({"margin": 0, "label": {"text": "BIG", "size": 1e9}})
    with pytest.raises(qrkude.QRKudeError):
        qrkude.to_image(qrkude.encode("x"), style, 512)


def test_clamped_extremes_render() -> None:
    code = qrkude.encode("https://kudelink.com")
    for doc in ({"margin": 1e6}, {"border": {"width": 5000}, "label": {"text": "big", "size": 1e9}},
                {"logo": {"size": 0.1, "margin": 1e5}, "background": {"radius": 1e9}}):
        style = qrkude.Style.from_json(doc)
        lay = qrkude.get_layout(code, style)
        img = qrkude.to_image(code, style, 128)
        assert img.size == (128, int(128 * lay.height / lay.width + 0.5))


def test_huge_label_font_and_long_text_are_bounded() -> None:
    code = qrkude.encode("x")
    style = qrkude.Style.from_json({"margin": 0, "label": {"text": "W", "size": 400}})
    img = qrkude.to_image(code, style, 64)  # ~1200 px font, drawn reduced and scaled up
    assert img.height > 64 * 20
    band = img.crop((0, 64, 64, img.height)).convert("RGB")
    assert band.getextrema()[0][0] < 50  # some dark text pixels
    long_text = qrkude.Style.from_json({"label": {"text": "W" * 200000}})
    assert qrkude.to_image(code, long_text, 128).width == 128


def test_non_finite_programmatic_values_render() -> None:
    import math

    code = qrkude.encode("https://kudelink.com")
    s = qrkude.Style()
    s.margin = math.inf
    s.modules.color = qrkude.Paint.linear(math.inf, "#f00", "#00f")
    s.eyes.frame_color = qrkude.Paint.radial("#f00", "#00f", cx=math.nan, r=math.inf)
    s.background.radius = math.nan
    assert qrkude.to_image(code, s, 64).size == (64, 64)
