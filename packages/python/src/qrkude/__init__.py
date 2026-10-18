# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""QRKude: styled QR codes (dots, fluid/liquid modules, custom eyes, gradients,
logos, frames and labels) with SVG output identical across languages.

Quick start::

    import qrkude

    svg_text = qrkude.svg("https://kudelink.com", {"modules": {"shape": "fluid"}})

    code = qrkude.encode("https://kudelink.com", ecc="Q")
    style = qrkude.Style.from_json('{"modules": {"shape": "dot"}}')
    svg_text = qrkude.to_svg(code, style)
    png_bytes = qrkude.to_png(code, style, size=512)  # needs qrkude[png]
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any, Optional

from . import payloads
from .encoder import (
    DataTooLongError,
    Ecc,
    EncodeOptions,
    InvalidArgumentError,
    Mode,
    QRCode,
    QRKudeError,
    encode,
)
from .layout import (
    GradientGeometry,
    Layer,
    LayoutInfo,
    PathSink,
    emit_layer,
    get_layout,
    gradient_geometry,
    layer_paint,
    layer_visible,
    logo_damage,
)
from .lint import LINT_FLAGS, lint
from .payloads import email, event, geo, sms, tel, vcard, wifi
from .style import (
    AlignmentStyle,
    BackgroundStyle,
    BorderStyle,
    Document,
    EyeShape,
    EyeStyle,
    LabelStyle,
    LogoStyle,
    ModuleShape,
    ModuleStyle,
    Paint,
    PaintType,
    Stop,
    Style,
    StyleError,
    SvgOptions,
    c_string,
    format_color,
    load_document,
    parse_color,
    recommended_ecc,
)
from .svg import encode_for_style, format_number, sanitize_id_prefix, svg, to_svg

if TYPE_CHECKING:  # pragma: no cover
    from PIL import Image

__version__ = "0.1.0"

__all__ = [
    "__version__",
    # encoding
    "encode",
    "encode_for_style",
    "EncodeOptions",
    "QRCode",
    "Ecc",
    "Mode",
    "QRKudeError",
    "InvalidArgumentError",
    "DataTooLongError",
    # style
    "Style",
    "ModuleStyle",
    "EyeStyle",
    "AlignmentStyle",
    "BackgroundStyle",
    "BorderStyle",
    "LogoStyle",
    "LabelStyle",
    "ModuleShape",
    "EyeShape",
    "Paint",
    "PaintType",
    "Stop",
    "StyleError",
    "SvgOptions",
    "Document",
    "load_document",
    "parse_color",
    "format_color",
    "c_string",
    "recommended_ecc",
    # output
    "to_svg",
    "svg",
    "format_number",
    "sanitize_id_prefix",
    "to_png",
    "to_image",
    # geometry
    "Layer",
    "PathSink",
    "LayoutInfo",
    "GradientGeometry",
    "emit_layer",
    "get_layout",
    "layer_paint",
    "layer_visible",
    "gradient_geometry",
    # checks
    "lint",
    "LINT_FLAGS",
    "logo_damage",
    # payloads
    "payloads",
    "wifi",
    "email",
    "sms",
    "tel",
    "geo",
    "vcard",
    "event",
]


def _require_pillow() -> None:
    try:
        import PIL  # noqa: F401
    except ImportError as exc:  # pragma: no cover - depends on the environment
        raise ImportError("PNG output needs Pillow: pip install 'qrkude[png]'") from exc


def to_image(code: QRCode, style: Optional[Style] = None, size: int = 512,
             logo: Any = None, **kwargs: Any) -> "Image.Image":
    """Renders an RGBA ``PIL.Image`` (``size`` px wide). Requires ``qrkude[png]``.

    See :func:`qrkude.png.to_image` for all options.
    """
    _require_pillow()
    from .png import to_image as _to_image

    return _to_image(code, style, size, logo, **kwargs)


def to_png(code: QRCode, style: Optional[Style] = None, size: int = 512,
           logo: Any = None, **kwargs: Any) -> bytes:
    """Renders PNG bytes (``size`` px wide). Requires ``qrkude[png]``.

    ``logo`` is a ``PIL.Image``, a path, or None (a raster ``data:`` URI in
    ``style.logo.href`` is used when present).
    """
    _require_pillow()
    from .png import to_png as _to_png

    return _to_png(code, style, size, logo, **kwargs)

