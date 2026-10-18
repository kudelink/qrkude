# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Shared fixtures: locating the conformance vectors of the monorepo."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, List, Optional

import pytest


def vectors_dir() -> Optional[Path]:
    """``$QRKUDE_VECTORS``, ``tests/vectors`` (sdist) or ``spec/vectors`` in a
    parent directory (monorepo)."""
    env = os.environ.get("QRKUDE_VECTORS")
    if env:
        return Path(env)
    here = Path(__file__).resolve()
    bundled = here.parent / "vectors"
    if (bundled / "render.json").is_file():
        return bundled
    for parent in here.parents:
        candidate = parent / "spec" / "vectors"
        if (candidate / "render.json").is_file():
            return candidate
    return None


def load_vectors(name: str) -> List[Any]:
    d = vectors_dir()
    if d is None or not (d / name).is_file():
        return []
    with open(d / name, encoding="utf-8") as f:
        return json.load(f)


def require_vectors(cases: List[Any]) -> None:
    if not cases:
        pytest.skip("conformance vectors not found (set QRKUDE_VECTORS)")


def doc_of(case: Any) -> dict:
    """The ``{encode, style, svg}`` document of a render vector."""
    return {k: case[k] for k in ("encode", "style", "svg") if k in case}
