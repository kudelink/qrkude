# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Conformance: every vector in spec/vectors/ must be reproduced exactly."""

from __future__ import annotations

import json
import re
from typing import Any

import pytest

import qrkude
from conftest import doc_of, load_vectors

ENCODE = load_vectors("encode.json")
RENDER = load_vectors("render.json")
PAYLOADS = load_vectors("payloads.json")

_skip = pytest.mark.skip(reason="conformance vectors not found (set QRKUDE_VECTORS)")


def _params(cases: Any, ident: Any) -> Any:
    if not cases:
        return [pytest.param(None, marks=_skip)]
    return [pytest.param(c, id=ident(i, c)) for i, c in enumerate(cases)]


def _encode_id(i: int, c: Any) -> str:
    text = c["text"]
    short = text if len(text) <= 16 else "%s..x%d" % (text[:8], len(text))
    return "%02d-%s" % (i, short)


@pytest.mark.parametrize("case", _params(ENCODE, _encode_id))
def test_encode_vector(case: Any) -> None:
    options = qrkude.load_document({"encode": case["encode"]}).encode
    if "error" in case:
        assert case["error"] == "data too long"
        with pytest.raises(qrkude.DataTooLongError, match="data too long"):
            qrkude.encode(case["text"], options)
        return
    code = qrkude.encode(case["text"], options)
    assert code.version == case["version"]
    assert code.size == code.version * 4 + 17
    assert code.ecc.name == case["ecc"]
    assert code.mask == case["mask"]
    assert str(code.mode) == case["mode"]
    assert code.to_text().split("\n") == case["matrix"]
    rows = case["matrix"]
    assert all(code.is_dark(x, y) == (rows[y][x] == "1")
               for y in range(code.size) for x in range(code.size))


def _source(case: Any) -> Any:
    """Cases with a ``json`` string must be parsed from that exact text."""
    return case["json"] if "json" in case else json.dumps(doc_of(case))


def _check_render(case: Any, source: Any) -> None:
    if "error" in case:
        with pytest.raises(qrkude.QRKudeError) as info:
            doc = qrkude.load_document(source)
            qrkude.encode_for_style(case["text"], doc.encode, doc.style)
        assert info.value.kind == case["error"]
        return
    doc = qrkude.load_document(source)
    code = qrkude.encode_for_style(case["text"], doc.encode, doc.style)
    assert (code.version, code.ecc.name, code.mask) == (case["version"], case["ecc"], case["mask"])
    assert qrkude.to_svg(code, doc.style, doc.svg) == case["output"]
    assert qrkude.lint(code, doc.style) == case["lint"]
    if "logoDamage" in case:
        assert abs(qrkude.logo_damage(code, doc.style) - case["logoDamage"]) <= 1e-6


def _plain(case: Any) -> bool:
    """Vectors expressible as a mapping (no raw JSON text, no error)."""
    return "json" not in case and "error" not in case


@pytest.mark.parametrize("case", _params(RENDER, lambda i, c: c["name"]))
def test_render_vector(case: Any) -> None:
    """The document as JSON text (raw ``json`` when the vector has one)."""
    _check_render(case, _source(case))


@pytest.mark.parametrize("case", _params([c for c in RENDER if _plain(c)],
                                         lambda i, c: c["name"]))
def test_render_vector_mapping(case: Any) -> None:
    """The document as an already decoded mapping, through svg()."""
    assert qrkude.svg(case["text"], doc_of(case)) == case["output"]
    _check_render(case, doc_of(case))


@pytest.mark.parametrize("case", _params([c for c in RENDER if "error" not in c],
                                         lambda i, c: c["name"]))
def test_render_vector_style_round_trip(case: Any) -> None:
    """Style.to_dict() / from_json() preserve the rendering."""
    doc = qrkude.load_document(_source(case))
    again = qrkude.Style.from_json(doc.style.to_json())
    code = qrkude.encode_for_style(case["text"], doc.encode, doc.style)
    assert qrkude.to_svg(code, again, doc.svg) == case["output"]


def _snake(key: str) -> str:
    return re.sub(r"([A-Z])", lambda m: "_" + m.group(1).lower(), key)


@pytest.mark.parametrize("case", _params(PAYLOADS, lambda i, c: "%02d-%s" % (i, c["type"])))
def test_payload_vector(case: Any) -> None:
    build = getattr(qrkude.payloads, case["type"])
    args = {_snake(k): v for k, v in case["args"].items()}
    assert build(**args) == case["output"]
