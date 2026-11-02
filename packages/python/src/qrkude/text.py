# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Text output for terminals and serial monitors (SPEC section 14), a port of
``core/src/qrk_text.c``. The output is identical in every QRKude library."""

from __future__ import annotations

import numbers
from typing import List, Tuple

from .encoder import InvalidArgumentError, QRCode

__all__ = ["to_text", "text_size"]

_MAX_MARGIN = 16

# The cell for each ink value: half blocks index ink(x, y) + 2 * ink(x, y + 1).
_CELLS = {
    "half_blocks": (" ", "▀", "▄", "█"),
    "full_blocks": ("  ", "██"),
    "hash": ("  ", "##"),
}

_ANSI_START = "\x1b[30;107m"  # black ink on bright white paper
_ANSI_END = "\x1b[0m"


def _cells(style: str) -> Tuple[str, ...]:
    cells = _CELLS.get(style) if isinstance(style, str) else None
    if cells is None:
        raise InvalidArgumentError("unknown text style: %r" % (style,))
    return cells


def _margin(margin: int) -> int:
    """Clamps to 0..16. Other numbers truncate toward zero; NaN is 0."""
    if isinstance(margin, bool) or not isinstance(margin, numbers.Real):
        raise InvalidArgumentError("margin: expected a number, got %r" % (margin,))
    if isinstance(margin, numbers.Integral):
        return max(0, min(_MAX_MARGIN, int(margin)))
    value = float(margin)
    if not value > 0:  # also NaN
        return 0
    return int(min(value, _MAX_MARGIN))


def to_text(code: QRCode, style: str = "half_blocks", margin: int = 2,
            invert: bool = False, ansi: bool = False) -> str:
    """The code as text, one ``"\\n"``-terminated line per text row.

    ``style`` is ``"half_blocks"`` (two modules per character), ``"full_blocks"``
    or ``"hash"`` (two characters per module). ``margin`` is the light border
    in modules, clamped to 0..16. ``invert`` swaps ink and paper. ``ansi``
    wraps every line in color codes for black on bright white. Raises
    :class:`InvalidArgumentError` (a ``ValueError``) for an unknown style.
    """
    cells = _cells(style)
    m = _margin(margin)
    n = code.size
    invert = bool(invert)
    columns = range(-m, n + m)

    def ink(x: int, y: int) -> bool:
        return code.is_dark(x, y) != invert

    lines: List[str] = []
    if len(cells) == 4:
        for y in range(-m, n + m, 2):
            lines.append("".join(cells[ink(x, y) | ink(x, y + 1) << 1] for x in columns))
    else:
        for y in range(-m, n + m):
            lines.append("".join(cells[ink(x, y)] for x in columns))
    if ansi:
        return "".join(_ANSI_START + line + _ANSI_END + "\n" for line in lines)
    return "".join(line + "\n" for line in lines)


def text_size(code: QRCode, style: str = "half_blocks", margin: int = 2,
              invert: bool = False, ansi: bool = False) -> Tuple[int, int]:
    """``(columns, lines)`` that :func:`to_text` fills on screen.

    Takes the same arguments as :func:`to_text`. ``invert`` and ``ansi`` do
    not change the size: color codes take no columns.
    """
    cells = _cells(style)
    side = code.size + 2 * _margin(margin)
    if len(cells) == 4:
        return side, (side + 1) // 2
    return 2 * side, side
