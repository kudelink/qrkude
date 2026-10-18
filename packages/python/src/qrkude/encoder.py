# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""QR Code encoder (ISO/IEC 18004, model 2, versions 1-40).

A line-by-line port of ``core/src/qrk_encode.c``: the module matrices, the
version/ECC choice and the automatic mask are identical to the reference.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from functools import lru_cache
from typing import List, Optional, Tuple, Union

__all__ = [
    "Ecc",
    "Mode",
    "EncodeOptions",
    "QRCode",
    "QRKudeError",
    "InvalidArgumentError",
    "DataTooLongError",
    "encode",
    "alignment_positions",
    "is_function_module",
]

MAX_VERSION = 40


class QRKudeError(ValueError):
    """Base class of every error raised by qrkude.

    ``kind`` is the cross-port error kind (SPEC 2.0): ``"parse error"``,
    ``"invalid argument"`` or ``"data too long"``.
    """

    kind = "invalid argument"


class InvalidArgumentError(QRKudeError):
    """Encode options out of range (e.g. ``minVersion: 0``)."""

    kind = "invalid argument"


class DataTooLongError(QRKudeError):
    """The payload does not fit in any allowed version."""

    kind = "data too long"

    def __init__(self, message: str = "data too long") -> None:
        super().__init__(message)


class Ecc(IntEnum):
    """Error correction level (``str(Ecc.H) == "H"``)."""

    L = 0  # ~7% recovery
    M = 1  # ~15% recovery
    Q = 2  # ~25% recovery
    H = 3  # ~30% recovery

    def __str__(self) -> str:
        return self.name

    @classmethod
    def parse(cls, value: Union["Ecc", str, int]) -> "Ecc":
        """Accepts an ``Ecc``, ``"L"``/``"M"``/``"Q"``/``"H"`` or 0..3."""
        if isinstance(value, Ecc):
            return value
        if isinstance(value, str):
            if value in ("L", "M", "Q", "H"):
                return cls[value]
            raise InvalidArgumentError("invalid ECC level: %r" % (value,))
        if isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 3:
            return cls(value)
        raise InvalidArgumentError("invalid ECC level: %r" % (value,))


class Mode(IntEnum):
    """Data mode of the single segment."""

    NUMERIC = 1
    ALPHANUMERIC = 2
    BYTE = 4

    def __str__(self) -> str:
        return self.name.lower()


# Index: [ecc][version], ecc in L, M, Q, H order.
_ECC_CODEWORDS_PER_BLOCK = (
    (-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
     28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30),
    (-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
     26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28),
    (-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
     28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30),
    (-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
     30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30),
)

_NUM_ECC_BLOCKS = (
    (-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8,
     8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25),
    (-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16,
     17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49),
    (-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20,
     23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68),
    (-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
     25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81),
)

# Format information ECC bits for L, M, Q, H.
_ECC_FORMAT_BITS = (1, 0, 3, 2)

_ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:"
_ALNUM_INDEX = {ord(c): i for i, c in enumerate(_ALPHANUMERIC_CHARSET)}


# ---- tables ------------------------------------------------------------------


def ecc_codewords_per_block(ecc: int, version: int) -> int:
    return _ECC_CODEWORDS_PER_BLOCK[ecc][version]


def num_ecc_blocks(ecc: int, version: int) -> int:
    return _NUM_ECC_BLOCKS[ecc][version]


def _raw_data_modules(ver: int) -> int:
    result = (16 * ver + 128) * ver + 64
    if ver >= 2:
        num_align = ver // 7 + 2
        result -= (25 * num_align - 10) * num_align - 55
        if ver >= 7:
            result -= 36
    return result


def raw_codewords(version: int) -> int:
    return _raw_data_modules(version) // 8


def _data_codewords(ver: int, ecc: int) -> int:
    return raw_codewords(ver) - _ECC_CODEWORDS_PER_BLOCK[ecc][ver] * _NUM_ECC_BLOCKS[ecc][ver]


@lru_cache(maxsize=None)
def alignment_positions(version: int) -> Tuple[int, ...]:
    """Row/column coordinates of the alignment pattern centers."""
    if version == 1:
        return ()
    num_align = version // 7 + 2
    step = (version * 8 + num_align * 3 + 5) // (num_align * 4 - 4) * 2
    out = [0] * num_align
    pos = version * 4 + 10
    for i in range(num_align - 1, 0, -1):
        out[i] = pos
        pos -= step
    out[0] = 6
    return tuple(out)


def is_function_module(version: int, x: int, y: int) -> bool:
    """True for finders, separators, timing, format/version info and alignment."""
    size = version * 4 + 17
    if x <= 8 and y <= 8:
        return True
    if x >= size - 8 and y <= 8:
        return True
    if x <= 8 and y >= size - 8:
        return True
    if x == 6 or y == 6:
        return True
    if version >= 7:
        if size - 11 <= x < size - 8 and y < 6:
            return True
        if size - 11 <= y < size - 8 and x < 6:
            return True
    pos = alignment_positions(version)
    n = len(pos)
    for i in range(n):
        for j in range(n):
            if (i == 0 and j == 0) or (i == 0 and j == n - 1) or (i == n - 1 and j == 0):
                continue
            dx = x - pos[j]
            dy = y - pos[i]
            if -2 <= dx <= 2 and -2 <= dy <= 2:
                return True
    return False


@lru_cache(maxsize=8)
def function_map(version: int) -> bytes:
    """Row-major map of :func:`is_function_module` (1 = function module)."""
    size = version * 4 + 17
    return bytes(
        1 if is_function_module(version, x, y) else 0 for y in range(size) for x in range(size)
    )


@lru_cache(maxsize=8)
def placement_order(version: int) -> Tuple[int, ...]:
    """Row-major indices of the data modules in zig-zag placement order."""
    size = version * 4 + 17
    fmap = function_map(version)
    order = []
    right = size - 1
    while right >= 1:
        if right == 6:
            right = 5
        upward = ((right + 1) & 2) == 0
        for vert in range(size):
            y = size - 1 - vert if upward else vert
            for j in range(2):
                index = y * size + right - j
                if not fmap[index]:
                    order.append(index)
        right -= 2
    return tuple(order)


# ---- Reed-Solomon over GF(2^8 / 0x11D) ------------------------------------------

_GF_EXP = [0] * 512
_GF_LOG = [0] * 256


def _init_gf() -> None:
    x = 1
    for i in range(255):
        _GF_EXP[i] = x
        _GF_LOG[x] = i
        x <<= 1
        if x & 0x100:
            x ^= 0x11D
    for i in range(255, 512):
        _GF_EXP[i] = _GF_EXP[i - 255]


_init_gf()


def _gf_mul(x: int, y: int) -> int:
    if x == 0 or y == 0:
        return 0
    return _GF_EXP[_GF_LOG[x] + _GF_LOG[y]]


@lru_cache(maxsize=None)
def _rs_divisor(degree: int) -> Tuple[int, ...]:
    result = [0] * degree
    result[degree - 1] = 1
    root = 1
    for _ in range(degree):
        for j in range(degree):
            result[j] = _gf_mul(result[j], root)
            if j + 1 < degree:
                result[j] ^= result[j + 1]
        root = _gf_mul(root, 0x02)
    return tuple(result)


def _rs_remainder(data: bytes, divisor: Tuple[int, ...]) -> List[int]:
    degree = len(divisor)
    result = [0] * degree
    for b in data:
        factor = b ^ result[0]
        del result[0]
        result.append(0)
        if factor:
            lf = _GF_LOG[factor]
            for j in range(degree):
                d = divisor[j]
                if d:
                    result[j] ^= _GF_EXP[_GF_LOG[d] + lf]
    return result


# ---- data segment ------------------------------------------------------------------


def _detect_mode(data: bytes) -> Mode:
    numeric = True
    alnum = True
    for b in data:
        if b < 0x30 or b > 0x39:
            numeric = False
        if b not in _ALNUM_INDEX:
            alnum = False
    if numeric:
        return Mode.NUMERIC
    if alnum:
        return Mode.ALPHANUMERIC
    return Mode.BYTE


def _char_count_bits(mode: int, ver: int) -> int:
    i = (ver + 7) // 17
    if mode == Mode.NUMERIC:
        return (10, 12, 14)[i]
    if mode == Mode.ALPHANUMERIC:
        return (9, 11, 13)[i]
    return (8, 16, 16)[i]


def _payload_bits(mode: int, length: int) -> int:
    if mode == Mode.NUMERIC:
        return (length // 3) * 10 + (0, 4, 7)[length % 3]
    if mode == Mode.ALPHANUMERIC:
        return (length // 2) * 11 + (length % 2) * 6
    return length * 8


def _total_bits(mode: int, length: int, eci: bool, ver: int) -> int:
    """Total bit length for this version, or -1 if the count field overflows."""
    bits = 12 if eci else 0
    if length > 0:
        ccb = _char_count_bits(mode, ver)
        if length >= (1 << ccb):
            return -1
        bits += 4 + ccb + _payload_bits(mode, length)
    return bits


class _BitBuffer:
    __slots__ = ("bits",)

    def __init__(self) -> None:
        self.bits: List[int] = []

    def append(self, value: int, n: int) -> None:
        for i in range(n - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def to_bytes(self) -> bytes:
        bits = self.bits
        out = bytearray(len(bits) // 8)
        for i in range(len(out)):
            v = 0
            for b in bits[i * 8:i * 8 + 8]:
                v = (v << 1) | b
            out[i] = v
        return bytes(out)


# ---- options and result ---------------------------------------------------------


@dataclass
class EncodeOptions:
    """Encoder options (the ``encode`` object of the JSON document).

    ``ecc`` is the minimum error correction level; ``None`` means "not given":
    :func:`encode` then uses ``M`` and :func:`qrkude.svg` applies the default
    rule (``H`` with a logo, else ``M``).
    """

    ecc: Optional[Ecc] = None
    min_version: int = 1
    max_version: int = MAX_VERSION
    mask: int = -1
    boost_ecc: bool = True
    eci: bool = False

    def __post_init__(self) -> None:
        if self.ecc is not None:
            self.ecc = Ecc.parse(self.ecc)


class QRCode:
    """An encoded symbol: a square matrix of dark/light modules."""

    __slots__ = ("version", "size", "ecc", "mask", "mode", "_modules")

    def __init__(self, version: int, ecc: Ecc, mask: int, mode: Mode, modules: bytes) -> None:
        self.version = version
        self.size = version * 4 + 17
        self.ecc = ecc
        self.mask = mask
        self.mode = mode
        self._modules = bytes(modules)

    def is_dark(self, x: int, y: int) -> bool:
        """True for a dark module. Out-of-range coordinates are light."""
        size = self.size
        if x < 0 or y < 0 or x >= size or y >= size:
            return False
        return self._modules[y * size + x] != 0

    @property
    def modules(self) -> bytes:
        """Row-major module bytes (1 = dark), ``size * size`` long."""
        return self._modules

    def matrix(self) -> List[List[bool]]:
        """The module matrix as rows of booleans (True = dark)."""
        size = self.size
        m = self._modules
        return [[m[y * size + x] != 0 for x in range(size)] for y in range(size)]

    def to_text(self, dark: str = "1", light: str = "0") -> str:
        """One line per row, like ``qrkude --matrix``."""
        size = self.size
        m = self._modules
        return "\n".join(
            "".join(dark if m[y * size + x] else light for x in range(size)) for y in range(size)
        )

    def __repr__(self) -> str:
        return "QRCode(version=%d, size=%d, ecc=%s, mask=%d, mode=%s)" % (
            self.version, self.size, self.ecc.name, self.mask, str(self.mode))


# ---- matrix construction ------------------------------------------------------------


def _draw_format_bits(m: bytearray, size: int, ecc: int, mask: int) -> None:
    data = _ECC_FORMAT_BITS[ecc] << 3 | mask
    rem = data
    for _ in range(10):
        rem = (rem << 1) ^ ((rem >> 9) * 0x537)
    bits = (data << 10 | rem) ^ 0x5412

    for i in range(6):
        m[i * size + 8] = (bits >> i) & 1
    m[7 * size + 8] = (bits >> 6) & 1
    m[8 * size + 8] = (bits >> 7) & 1
    m[8 * size + 7] = (bits >> 8) & 1
    for i in range(9, 15):
        m[8 * size + 14 - i] = (bits >> i) & 1

    for i in range(8):
        m[8 * size + size - 1 - i] = (bits >> i) & 1
    for i in range(8, 15):
        m[(size - 15 + i) * size + 8] = (bits >> i) & 1
    m[(size - 8) * size + 8] = 1


def _draw_function_patterns(m: bytearray, version: int, ecc: int) -> None:
    size = version * 4 + 17
    for i in range(size):
        m[i * size + 6] = 1 if i % 2 == 0 else 0
        m[6 * size + i] = 1 if i % 2 == 0 else 0
    for cx, cy in ((3, 3), (size - 4, 3), (3, size - 4)):
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                x = cx + dx
                y = cy + dy
                if 0 <= x < size and 0 <= y < size:
                    dist = max(abs(dx), abs(dy))
                    m[y * size + x] = 1 if dist != 2 and dist != 4 else 0

    pos = alignment_positions(version)
    n = len(pos)
    for i in range(n):
        for j in range(n):
            if (i == 0 and j == 0) or (i == 0 and j == n - 1) or (i == n - 1 and j == 0):
                continue
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    m[(pos[i] + dy) * size + pos[j] + dx] = 1 if max(abs(dx), abs(dy)) != 1 else 0

    _draw_format_bits(m, size, ecc, 0)  # placeholder, redrawn after masking

    if version >= 7:
        rem = version
        for _ in range(12):
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25)
        bits = version << 12 | rem
        for i in range(18):
            bit = (bits >> i) & 1
            a = size - 11 + i % 3
            b = i // 3
            m[b * size + a] = bit
            m[a * size + b] = bit


def _mask_bit(mask: int, x: int, y: int) -> bool:
    if mask == 0:
        return (x + y) % 2 == 0
    if mask == 1:
        return y % 2 == 0
    if mask == 2:
        return x % 3 == 0
    if mask == 3:
        return (x + y) % 3 == 0
    if mask == 4:
        return (x // 3 + y // 2) % 2 == 0
    if mask == 5:
        return x * y % 2 + x * y % 3 == 0
    if mask == 6:
        return (x * y % 2 + x * y % 3) % 2 == 0
    return ((x + y) % 2 + x * y % 3) % 2 == 0


@lru_cache(maxsize=64)
def _mask_pattern(version: int, mask: int) -> bytes:
    """1 where the mask flips a (non-function) module."""
    size = version * 4 + 17
    fmap = function_map(version)
    return bytes(
        1 if not fmap[y * size + x] and _mask_bit(mask, x, y) else 0
        for y in range(size) for x in range(size)
    )


def _apply_mask(m: bytearray, version: int, mask: int) -> None:
    pattern = _mask_pattern(version, mask)
    for i, p in enumerate(pattern):
        if p:
            m[i] ^= 1


# ---- mask penalty -------------------------------------------------------------------


def _finder_like_count(h: List[int]) -> int:
    n = h[1]
    core = n > 0 and h[2] == n and h[3] == n * 3 and h[4] == n and h[5] == n
    return (1 if core and h[0] >= n * 4 and h[6] >= n else 0) + (
        1 if core and h[6] >= n * 4 and h[0] >= n else 0)


def _line_penalty(line: bytes, size: int) -> int:
    result = 0
    color = 0
    run = 0
    history = [0, 0, 0, 0, 0, 0, 0]
    for mod in line:
        if mod == color:
            run += 1
            if run == 5:
                result += 3
            elif run > 5:
                result += 1
        else:
            # run_history_add
            if history[0] == 0:
                run += size  # light border before the first run
            history.pop()
            history.insert(0, run)
            if not color:
                result += _finder_like_count(history) * 40
            color = mod
            run = 1
    # finder_terminate
    if color:
        if history[0] == 0:
            run += size
        history.pop()
        history.insert(0, run)
        run = 0
    run += size
    if history[0] == 0:
        run += size
    history.pop()
    history.insert(0, run)
    result += _finder_like_count(history) * 40
    return result


def _penalty_score(m: bytearray, size: int) -> int:
    result = 0
    rows = [bytes(m[y * size:(y + 1) * size]) for y in range(size)]
    for row in rows:
        result += _line_penalty(row, size)
    for x in range(size):
        result += _line_penalty(bytes(m[x::size]), size)

    for y in range(size - 1):
        r0 = rows[y]
        r1 = rows[y + 1]
        for x in range(size - 1):
            c = r0[x]
            if c == r0[x + 1] and c == r1[x] and c == r1[x + 1]:
                result += 3

    dark = sum(m)
    total = size * size
    diff = abs(dark * 20 - total * 10)
    k = (diff + total - 1) // total - 1
    result += k * 10
    return result


# ---- public API ---------------------------------------------------------------------


def encode(
    data: Union[str, bytes, bytearray, memoryview],
    options: Optional[EncodeOptions] = None,
    *,
    ecc: Union[Ecc, str, int, None] = None,
    min_version: Optional[int] = None,
    max_version: Optional[int] = None,
    mask: Optional[int] = None,
    boost_ecc: Optional[bool] = None,
    eci: Optional[bool] = None,
) -> QRCode:
    """Encodes text (as UTF-8) or bytes into a :class:`QRCode`.

    Keyword arguments override the corresponding ``options`` fields. The mode
    (numeric, alphanumeric or byte) is chosen automatically. Raises
    :class:`DataTooLongError` when the payload does not fit and
    :class:`QRKudeError` for invalid options.
    """
    o = options if options is not None else EncodeOptions()
    level = Ecc.parse(ecc) if ecc is not None else (o.ecc if o.ecc is not None else Ecc.M)
    level = Ecc.parse(level)
    vmin = o.min_version if min_version is None else min_version
    vmax = o.max_version if max_version is None else max_version
    msk = o.mask if mask is None else mask
    boost = o.boost_ecc if boost_ecc is None else boost_ecc
    use_eci = o.eci if eci is None else eci

    if isinstance(data, str):
        payload = data.encode("utf-8")
    else:
        payload = bytes(data)
    if vmin < 1 or vmax > MAX_VERSION or vmin > vmax or msk < -1 or msk > 7:
        raise InvalidArgumentError("invalid argument")

    mode = _detect_mode(payload)
    length = len(payload)

    ver = 0
    used = 0
    for v in range(vmin, vmax + 1):
        bits = _total_bits(mode, length, use_eci, v)
        if 0 <= bits <= _data_codewords(v, level) * 8:
            ver = v
            used = bits
            break
    if ver == 0:
        raise DataTooLongError()

    e = int(level)
    if boost:
        for candidate in range(e + 1, 4):
            if used <= _data_codewords(ver, candidate) * 8:
                e = candidate

    # data codewords
    data_len = _data_codewords(ver, e)
    bb = _BitBuffer()
    if use_eci:
        bb.append(0x7, 4)
        bb.append(26, 8)
    if length > 0:
        bb.append(int(mode), 4)
        bb.append(length, _char_count_bits(mode, ver))
        if mode == Mode.NUMERIC:
            i = 0
            while i + 3 <= length:
                bb.append(int(payload[i:i + 3].decode("ascii")), 10)
                i += 3
            if length - i == 2:
                bb.append(int(payload[i:i + 2].decode("ascii")), 7)
            elif length - i == 1:
                bb.append(payload[i] - 0x30, 4)
        elif mode == Mode.ALPHANUMERIC:
            i = 0
            while i + 2 <= length:
                bb.append(_ALNUM_INDEX[payload[i]] * 45 + _ALNUM_INDEX[payload[i + 1]], 11)
                i += 2
            if i < length:
                bb.append(_ALNUM_INDEX[payload[i]], 6)
        else:
            for b in payload:
                bb.append(b, 8)
    capacity = data_len * 8
    term = capacity - len(bb.bits)
    bb.append(0, term if term < 4 else 4)
    bb.append(0, (8 - len(bb.bits) % 8) % 8)
    pad = 0xEC
    while len(bb.bits) < capacity:
        bb.append(pad, 8)
        pad ^= 0xEC ^ 0x11
    dat = bb.to_bytes()

    # error correction + interleaving
    num_blocks = _NUM_ECC_BLOCKS[e][ver]
    block_ecc_len = _ECC_CODEWORDS_PER_BLOCK[e][ver]
    raw = raw_codewords(ver)
    num_short = num_blocks - raw % num_blocks
    short_len = raw // num_blocks - block_ecc_len
    divisor = _rs_divisor(block_ecc_len)
    out = [0] * raw
    offset = 0
    for i in range(num_blocks):
        dat_len = short_len + (0 if i < num_short else 1)
        block = dat[offset:offset + dat_len]
        ecc_buf = _rs_remainder(block, divisor)
        k = i
        for j in range(dat_len):
            if j == short_len:
                k -= num_short
            out[k] = block[j]
            k += num_blocks
        k = data_len + i
        for j in range(block_ecc_len):
            out[k] = ecc_buf[j]
            k += num_blocks
        offset += dat_len

    # matrix
    size = ver * 4 + 17
    m = bytearray(size * size)
    _draw_function_patterns(m, ver, e)
    order = placement_order(ver)
    nbits = raw * 8
    for i, index in enumerate(order):
        if i >= nbits:
            break
        m[index] = (out[i >> 3] >> (7 - (i & 7))) & 1

    if msk < 0:
        best = -1
        for candidate in range(8):
            _apply_mask(m, ver, candidate)
            _draw_format_bits(m, size, e, candidate)
            p = _penalty_score(m, size)
            if best < 0 or p < best:
                best = p
                msk = candidate
            _apply_mask(m, ver, candidate)
    _apply_mask(m, ver, msk)
    _draw_format_bits(m, size, e, msk)
    return QRCode(ver, Ecc(e), msk, mode, bytes(m))
