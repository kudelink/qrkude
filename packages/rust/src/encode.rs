// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! QR Code encoder (ISO/IEC 18004, model 2, versions 1-40). Port of
//! `core/src/qrk_encode.c`; see SPEC §1.

use alloc::vec;
use alloc::vec::Vec;

use crate::style::Style;
use crate::Error;

/// Largest QR version (177 x 177 modules).
pub const MAX_VERSION: u8 = 40;

/// Error correction level, in increasing strength.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Ecc {
    /// ~7% recovery.
    Low = 0,
    /// ~15% recovery.
    Medium = 1,
    /// ~25% recovery.
    Quartile = 2,
    /// ~30% recovery.
    High = 3,
}

impl Ecc {
    /// All levels, weakest first.
    pub const ALL: [Ecc; 4] = [Ecc::Low, Ecc::Medium, Ecc::Quartile, Ecc::High];

    /// `'L'`, `'M'`, `'Q'` or `'H'`.
    pub fn letter(self) -> char {
        ['L', 'M', 'Q', 'H'][self as usize]
    }

    /// Parses `'L'`, `'M'`, `'Q'` or `'H'` (upper case only, like the JSON reader).
    pub fn from_letter(c: char) -> Option<Ecc> {
        match c {
            'L' => Some(Ecc::Low),
            'M' => Some(Ecc::Medium),
            'Q' => Some(Ecc::Quartile),
            'H' => Some(Ecc::High),
            _ => None,
        }
    }

    fn index(self) -> usize {
        self as usize
    }
}

/// Data encoding mode of the (single) segment.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Mode {
    /// Digits `0-9`.
    Numeric = 1,
    /// `0-9 A-Z space $ % * + - . / :`.
    Alphanumeric = 2,
    /// Arbitrary bytes (UTF-8 text).
    Byte = 4,
}

impl Mode {
    /// `"numeric"`, `"alphanumeric"` or `"byte"`.
    pub fn name(self) -> &'static str {
        match self {
            Mode::Numeric => "numeric",
            Mode::Alphanumeric => "alphanumeric",
            Mode::Byte => "byte",
        }
    }
}

/// Encoder options (the `encode` object of a style document, SPEC §1.2).
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct EncodeOptions {
    /// Minimum error correction. `None` = automatic: [`Ecc::Medium`] for
    /// [`encode`], or the style's [`Style::recommended_ecc`] for
    /// [`encode_for_style`] (`H` with a logo, else `M`).
    pub ecc: Option<Ecc>,
    /// Smallest version to try (1..=40, default 1).
    pub min_version: u8,
    /// Largest version to try (1..=40, default 40).
    pub max_version: u8,
    /// `None` = automatic (lowest penalty), `Some(0..=7)` = forced.
    pub mask: Option<u8>,
    /// Raise the ECC level while the chosen version still fits (default true).
    pub boost_ecc: bool,
    /// Prepend an ECI designator 26 (UTF-8) (default false).
    pub eci: bool,
}

impl Default for EncodeOptions {
    fn default() -> Self {
        EncodeOptions {
            ecc: None,
            min_version: 1,
            max_version: MAX_VERSION,
            mask: None,
            boost_ecc: true,
            eci: false,
        }
    }
}

/// An encoded QR symbol: a square matrix of dark/light modules.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct QrCode {
    version: u8,
    size: u8,
    ecc: Ecc,
    mask: u8,
    mode: Mode,
    modules: Vec<u8>, // row-major bits, bit i of byte k = module 8k + i
}

impl QrCode {
    /// Version, 1..=40.
    pub fn version(&self) -> u8 {
        self.version
    }

    /// Modules per side (`4 * version + 17`).
    pub fn size(&self) -> usize {
        self.size as usize
    }

    /// Error correction level actually used (after boosting).
    pub fn ecc(&self) -> Ecc {
        self.ecc
    }

    /// Mask pattern, 0..=7.
    pub fn mask(&self) -> u8 {
        self.mask
    }

    /// Mode of the data segment.
    pub fn mode(&self) -> Mode {
        self.mode
    }

    /// `true` for a dark module. Out-of-range coordinates are light.
    #[inline]
    pub fn is_dark(&self, x: i32, y: i32) -> bool {
        let size = self.size as i32;
        if x < 0 || y < 0 || x >= size || y >= size {
            return false;
        }
        let index = (y * size + x) as usize;
        (self.modules[index >> 3] >> (index & 7)) & 1 != 0
    }

    #[inline]
    fn set(&mut self, x: i32, y: i32, dark: bool) {
        let index = (y * self.size as i32 + x) as usize;
        if dark {
            self.modules[index >> 3] |= 1 << (index & 7);
        } else {
            self.modules[index >> 3] &= !(1 << (index & 7));
        }
    }

    fn set_checked(&mut self, x: i32, y: i32, dark: bool) {
        let size = self.size as i32;
        if x >= 0 && y >= 0 && x < size && y < size {
            self.set(x, y, dark);
        }
    }
}

// ---- tables ----------------------------------------------------------------

/// Index: [ecc][version], ecc in L, M, Q, H order.
const ECC_CODEWORDS_PER_BLOCK: [[i8; 41]; 4] = [
    [
        -1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28,
        30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
    ],
    [
        -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26,
        28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    ],
    [
        -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28,
        30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
    ],
    [
        -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30,
        24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
    ],
];

const NUM_ECC_BLOCKS: [[i8; 41]; 4] = [
    [
        -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12,
        13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25,
    ],
    [
        -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21,
        23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49,
    ],
    [
        -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27,
        29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68,
    ],
    [
        -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32,
        35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81,
    ],
];

/// Format information ECC bits for L, M, Q, H.
const ECC_FORMAT_BITS: [i32; 4] = [1, 0, 3, 2];

const ALPHANUMERIC_CHARSET: &[u8] = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

pub(crate) fn ecc_codewords_per_block(ecc: Ecc, version: u8) -> i32 {
    ECC_CODEWORDS_PER_BLOCK[ecc.index()][version as usize] as i32
}

pub(crate) fn num_ecc_blocks(ecc: Ecc, version: u8) -> i32 {
    NUM_ECC_BLOCKS[ecc.index()][version as usize] as i32
}

fn raw_data_modules(ver: i32) -> i32 {
    let mut result = (16 * ver + 128) * ver + 64;
    if ver >= 2 {
        let num_align = ver / 7 + 2;
        result -= (25 * num_align - 10) * num_align - 55;
        if ver >= 7 {
            result -= 36;
        }
    }
    result
}

pub(crate) fn raw_codewords(version: u8) -> i32 {
    raw_data_modules(version as i32) / 8
}

fn data_codewords(ver: u8, ecc: Ecc) -> i32 {
    raw_codewords(ver) - ecc_codewords_per_block(ecc, ver) * num_ecc_blocks(ecc, ver)
}

/// Alignment pattern center coordinates; returns `(positions, count)`.
pub(crate) fn alignment_positions(ver: u8) -> ([u8; 7], usize) {
    let mut out = [0u8; 7];
    if ver == 1 {
        return (out, 0);
    }
    let ver = ver as i32;
    let num_align = ver / 7 + 2;
    let step = (ver * 8 + num_align * 3 + 5) / (num_align * 4 - 4) * 2;
    let mut pos = ver * 4 + 10;
    let mut i = num_align - 1;
    while i >= 1 {
        out[i as usize] = pos as u8;
        i -= 1;
        pos -= step;
    }
    out[0] = 6;
    (out, num_align as usize)
}

pub(crate) fn is_function_module(ver: u8, x: i32, y: i32) -> bool {
    let size = ver as i32 * 4 + 17;
    // finders + separators + format information + dark module
    if x <= 8 && y <= 8 {
        return true;
    }
    if x >= size - 8 && y <= 8 {
        return true;
    }
    if x <= 8 && y >= size - 8 {
        return true;
    }
    // timing patterns
    if x == 6 || y == 6 {
        return true;
    }
    // version information
    if ver >= 7 {
        if x >= size - 11 && x < size - 8 && y < 6 {
            return true;
        }
        if y >= size - 11 && y < size - 8 && x < 6 {
            return true;
        }
    }
    // alignment patterns
    let (pos, n) = alignment_positions(ver);
    for i in 0..n {
        for j in 0..n {
            if (i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0) {
                continue;
            }
            let dx = x - pos[j] as i32;
            let dy = y - pos[i] as i32;
            if (-2..=2).contains(&dx) && (-2..=2).contains(&dy) {
                return true;
            }
        }
    }
    false
}

// ---- Reed-Solomon over GF(2^8 / 0x11D) --------------------------------------

fn gf_mul(x: u8, y: u8) -> u8 {
    let mut z: i32 = 0;
    for i in (0..8).rev() {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y as i32 >> i) & 1) * x as i32;
    }
    z as u8
}

fn rs_divisor(degree: usize) -> [u8; 30] {
    let mut result = [0u8; 30];
    result[degree - 1] = 1;
    let mut root: u8 = 1;
    for _ in 0..degree {
        for j in 0..degree {
            result[j] = gf_mul(result[j], root);
            if j + 1 < degree {
                result[j] ^= result[j + 1];
            }
        }
        root = gf_mul(root, 0x02);
    }
    result
}

fn rs_remainder(data: &[u8], divisor: &[u8], result: &mut [u8]) {
    let degree = divisor.len();
    result.iter_mut().for_each(|b| *b = 0);
    for &d in data {
        let factor = d ^ result[0];
        result.copy_within(1..degree, 0);
        result[degree - 1] = 0;
        for j in 0..degree {
            result[j] ^= gf_mul(divisor[j], factor);
        }
    }
}

// ---- data segment -----------------------------------------------------------

fn alnum_index(c: u8) -> Option<u32> {
    if c == 0 {
        return None;
    }
    ALPHANUMERIC_CHARSET.iter().position(|&a| a == c).map(|p| p as u32)
}

fn detect_mode(data: &[u8]) -> Mode {
    let mut numeric = true;
    let mut alnum = true;
    for &b in data {
        if !b.is_ascii_digit() {
            numeric = false;
        }
        if alnum_index(b).is_none() {
            alnum = false;
        }
    }
    if numeric {
        Mode::Numeric
    } else if alnum {
        Mode::Alphanumeric
    } else {
        Mode::Byte
    }
}

fn char_count_bits(mode: Mode, ver: u8) -> u32 {
    let i = (ver as usize + 7) / 17;
    match mode {
        Mode::Numeric => [10, 12, 14][i],
        Mode::Alphanumeric => [9, 11, 13][i],
        Mode::Byte => [8, 16, 16][i],
    }
}

fn payload_bits(mode: Mode, len: usize) -> i64 {
    let len = len as i64;
    match mode {
        Mode::Numeric => (len / 3) * 10 + [0, 4, 7][(len % 3) as usize],
        Mode::Alphanumeric => (len / 2) * 11 + (len % 2) * 6,
        Mode::Byte => len * 8,
    }
}

/// Total bit length for this version, or `None` if the count field overflows.
fn total_bits(mode: Mode, len: usize, eci: bool, ver: u8) -> Option<i64> {
    let mut bits = if eci { 12 } else { 0 };
    if len > 0 {
        let ccb = char_count_bits(mode, ver);
        if len as u64 >= 1u64 << ccb {
            return None;
        }
        bits += 4 + ccb as i64 + payload_bits(mode, len);
    }
    Some(bits)
}

struct BitBuf<'a> {
    buf: &'a mut [u8],
    bits: usize,
}

impl BitBuf<'_> {
    fn append(&mut self, value: u32, n: u32) {
        for i in (0..n).rev() {
            if (value >> i) & 1 != 0 {
                self.buf[self.bits >> 3] |= 0x80 >> (self.bits & 7);
            }
            self.bits += 1;
        }
    }
}

// ---- function patterns ------------------------------------------------------

fn draw_format_bits(qr: &mut QrCode, mask: u8) {
    let data = (ECC_FORMAT_BITS[qr.ecc.index()] << 3) | mask as i32;
    let mut rem = data;
    for _ in 0..10 {
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    }
    let bits = ((data << 10) | rem) ^ 0x5412;
    let size = qr.size as i32;
    let bit = |i: i32| (bits >> i) & 1 != 0;

    for i in 0..=5 {
        qr.set(8, i, bit(i));
    }
    qr.set(8, 7, bit(6));
    qr.set(8, 8, bit(7));
    qr.set(7, 8, bit(8));
    for i in 9..15 {
        qr.set(14 - i, 8, bit(i));
    }
    for i in 0..8 {
        qr.set(size - 1 - i, 8, bit(i));
    }
    for i in 8..15 {
        qr.set(8, size - 15 + i, bit(i));
    }
    qr.set(8, size - 8, true);
}

fn draw_finder(qr: &mut QrCode, cx: i32, cy: i32) {
    for dy in -4..=4i32 {
        for dx in -4..=4i32 {
            let dist = dx.abs().max(dy.abs());
            qr.set_checked(cx + dx, cy + dy, dist != 2 && dist != 4);
        }
    }
}

fn draw_function_patterns(qr: &mut QrCode) {
    let size = qr.size as i32;
    let ver = qr.version;
    for i in 0..size {
        qr.set(6, i, i % 2 == 0);
        qr.set(i, 6, i % 2 == 0);
    }
    draw_finder(qr, 3, 3);
    draw_finder(qr, size - 4, 3);
    draw_finder(qr, 3, size - 4);

    let (pos, n) = alignment_positions(ver);
    for i in 0..n {
        for j in 0..n {
            if (i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0) {
                continue;
            }
            for dy in -2..=2i32 {
                for dx in -2..=2i32 {
                    let d = dx.abs().max(dy.abs());
                    qr.set(pos[j] as i32 + dx, pos[i] as i32 + dy, d != 1);
                }
            }
        }
    }

    draw_format_bits(qr, 0); // placeholder, redrawn after masking

    if ver >= 7 {
        let mut rem = ver as i32;
        for _ in 0..12 {
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        }
        let bits = ((ver as i64) << 12) | rem as i64;
        for i in 0..18 {
            let bit = (bits >> i) & 1 != 0;
            let a = size - 11 + i % 3;
            let b = i / 3;
            qr.set(a, b, bit);
            qr.set(b, a, bit);
        }
    }
}

/// Calls `f(x, y)` for every codeword module in placement order (zig-zag
/// from the bottom-right, skipping column 6 and function modules).
pub(crate) fn for_each_data_module(ver: u8, mut f: impl FnMut(i32, i32)) {
    let size = ver as i32 * 4 + 17;
    let mut right = size - 1;
    while right >= 1 {
        if right == 6 {
            right = 5;
        }
        for vert in 0..size {
            for j in 0..2 {
                let x = right - j;
                let upward = ((right + 1) & 2) == 0;
                let y = if upward { size - 1 - vert } else { vert };
                if !is_function_module(ver, x, y) {
                    f(x, y);
                }
            }
        }
        right -= 2;
    }
}

fn place_codewords(qr: &mut QrCode, data: &[u8]) {
    let total = data.len() * 8;
    let mut i = 0usize;
    let ver = qr.version;
    for_each_data_module(ver, |x, y| {
        if i < total {
            qr.set(x, y, (data[i >> 3] >> (7 - (i & 7))) & 1 != 0);
            i += 1;
        }
    });
}

fn mask_bit(mask: u8, x: i32, y: i32) -> bool {
    match mask {
        0 => (x + y) % 2 == 0,
        1 => y % 2 == 0,
        2 => x % 3 == 0,
        3 => (x + y) % 3 == 0,
        4 => (x / 3 + y / 2) % 2 == 0,
        5 => x * y % 2 + x * y % 3 == 0,
        6 => (x * y % 2 + x * y % 3) % 2 == 0,
        _ => ((x + y) % 2 + x * y % 3) % 2 == 0,
    }
}

/// `function_map` has one bit per module, set for function patterns.
fn apply_mask(qr: &mut QrCode, mask: u8, function_map: &[u8]) {
    let size = qr.size as i32;
    for y in 0..size {
        for x in 0..size {
            let index = (y * size + x) as usize;
            if (function_map[index >> 3] >> (index & 7)) & 1 == 0 && mask_bit(mask, x, y) {
                qr.modules[index >> 3] ^= 1 << (index & 7);
            }
        }
    }
}

// ---- mask penalty -----------------------------------------------------------

fn run_history_add(mut run: i32, history: &mut [i32; 7], size: i32) {
    if history[0] == 0 {
        run += size; // light border before the first run
    }
    history.copy_within(0..6, 1);
    history[0] = run;
}

fn finder_like_count(h: &[i32; 7]) -> i64 {
    let n = h[1];
    let core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
    (core && h[0] >= n * 4 && h[6] >= n) as i64 + (core && h[6] >= n * 4 && h[0] >= n) as i64
}

fn finder_terminate(color: bool, mut run: i32, history: &mut [i32; 7], size: i32) -> i64 {
    if color {
        run_history_add(run, history, size);
        run = 0;
    }
    run += size;
    run_history_add(run, history, size);
    finder_like_count(history)
}

fn penalty_score(qr: &QrCode) -> i64 {
    let size = qr.size as i32;
    let mut result: i64 = 0;

    for pass in 0..2 {
        for a in 0..size {
            let mut color = false;
            let mut run = 0i32;
            let mut history = [0i32; 7];
            for b in 0..size {
                let m = if pass == 0 { qr.is_dark(b, a) } else { qr.is_dark(a, b) };
                if m == color {
                    run += 1;
                    if run == 5 {
                        result += 3;
                    } else if run > 5 {
                        result += 1;
                    }
                } else {
                    run_history_add(run, &mut history, size);
                    if !color {
                        result += finder_like_count(&history) * 40;
                    }
                    color = m;
                    run = 1;
                }
            }
            result += finder_terminate(color, run, &mut history, size) * 40;
        }
    }

    for y in 0..size - 1 {
        for x in 0..size - 1 {
            let c = qr.is_dark(x, y);
            if c == qr.is_dark(x + 1, y) && c == qr.is_dark(x, y + 1) && c == qr.is_dark(x + 1, y + 1)
            {
                result += 3;
            }
        }
    }

    let mut dark: i64 = 0;
    for y in 0..size {
        for x in 0..size {
            dark += qr.is_dark(x, y) as i64;
        }
    }
    let total = size as i64 * size as i64;
    let diff = (dark * 20 - total * 10).abs();
    let k = (diff + total - 1) / total - 1;
    result += k * 10;
    result
}

// ---- public API -------------------------------------------------------------

fn validate(o: &EncodeOptions) -> Result<(), Error> {
    if o.min_version < 1
        || o.max_version > MAX_VERSION
        || o.min_version > o.max_version
        || matches!(o.mask, Some(m) if m > 7)
    {
        return Err(Error::InvalidArgument);
    }
    Ok(())
}

pub(crate) fn encode_with_ecc(data: &[u8], o: &EncodeOptions, min_ecc: Ecc) -> Result<QrCode, Error> {
    validate(o)?;
    let len = data.len();
    let mode = detect_mode(data);

    let mut found = None;
    for v in o.min_version..=o.max_version {
        if let Some(bits) = total_bits(mode, len, o.eci, v) {
            if bits <= data_codewords(v, min_ecc) as i64 * 8 {
                found = Some((v, bits));
                break;
            }
        }
    }
    let (ver, used) = found.ok_or(Error::DataTooLong)?;

    let mut ecc = min_ecc;
    if o.boost_ecc {
        for e in Ecc::ALL.iter().copied().filter(|&e| e > min_ecc) {
            if used <= data_codewords(ver, e) as i64 * 8 {
                ecc = e;
            }
        }
    }

    // data codewords
    let data_len = data_codewords(ver, ecc) as usize;
    let raw = raw_codewords(ver) as usize;
    let mut buf = vec![0u8; raw];
    let mut bb = BitBuf { buf: &mut buf, bits: 0 };
    if o.eci {
        bb.append(0x7, 4);
        bb.append(26, 8);
    }
    if len > 0 {
        bb.append(mode as u32, 4);
        bb.append(len as u32, char_count_bits(mode, ver));
        match mode {
            Mode::Numeric => {
                let d = |i: usize| (data[i] - b'0') as u32;
                let mut i = 0;
                while i + 3 <= len {
                    bb.append(d(i) * 100 + d(i + 1) * 10 + d(i + 2), 10);
                    i += 3;
                }
                if len - i == 2 {
                    bb.append(d(i) * 10 + d(i + 1), 7);
                } else if len - i == 1 {
                    bb.append(d(i), 4);
                }
            }
            Mode::Alphanumeric => {
                let a = |i: usize| alnum_index(data[i]).unwrap_or(0);
                let mut i = 0;
                while i + 2 <= len {
                    bb.append(a(i) * 45 + a(i + 1), 11);
                    i += 2;
                }
                if i < len {
                    bb.append(a(i), 6);
                }
            }
            Mode::Byte => {
                for &b in data {
                    bb.append(b as u32, 8);
                }
            }
        }
    }
    let capacity = data_len * 8;
    let term = capacity - bb.bits;
    bb.append(0, term.min(4) as u32);
    bb.append(0, ((8 - bb.bits % 8) % 8) as u32);
    let mut pad: u32 = 0xEC;
    while bb.bits < capacity {
        bb.append(pad, 8);
        pad ^= 0xEC ^ 0x11;
    }

    // error correction + interleaving
    let num_blocks = num_ecc_blocks(ecc, ver) as usize;
    let block_ecc_len = ecc_codewords_per_block(ecc, ver) as usize;
    let num_short = num_blocks - raw % num_blocks;
    let short_len = raw / num_blocks - block_ecc_len;
    let divisor = rs_divisor(block_ecc_len);
    let mut ecc_buf = [0u8; 30];
    let mut interleaved = vec![0u8; raw];
    let mut offset = 0usize;
    for i in 0..num_blocks {
        let dat_len = short_len + if i < num_short { 0 } else { 1 };
        let dat = &buf[offset..offset + dat_len];
        rs_remainder(dat, &divisor[..block_ecc_len], &mut ecc_buf[..block_ecc_len]);
        let mut k = i;
        for (j, &byte) in dat.iter().enumerate() {
            if j == short_len {
                k -= num_short;
            }
            interleaved[k] = byte;
            k += num_blocks;
        }
        let mut k = data_len + i;
        for &byte in &ecc_buf[..block_ecc_len] {
            interleaved[k] = byte;
            k += num_blocks;
        }
        offset += dat_len;
    }

    // matrix
    let size = ver as usize * 4 + 17;
    let mut qr = QrCode {
        version: ver,
        size: size as u8,
        ecc,
        mask: 0,
        mode,
        modules: vec![0u8; (size * size + 7) / 8],
    };
    draw_function_patterns(&mut qr);
    place_codewords(&mut qr, &interleaved);

    let mut function_map = vec![0u8; (size * size + 7) / 8];
    for y in 0..size as i32 {
        for x in 0..size as i32 {
            if is_function_module(ver, x, y) {
                let index = y as usize * size + x as usize;
                function_map[index >> 3] |= 1 << (index & 7);
            }
        }
    }

    let mask = match o.mask {
        Some(m) => m,
        None => {
            let mut best: i64 = -1;
            let mut chosen = 0u8;
            for m in 0..8u8 {
                apply_mask(&mut qr, m, &function_map);
                draw_format_bits(&mut qr, m);
                let p = penalty_score(&qr);
                if best < 0 || p < best {
                    best = p;
                    chosen = m;
                }
                apply_mask(&mut qr, m, &function_map);
            }
            chosen
        }
    };
    apply_mask(&mut qr, mask, &function_map);
    draw_format_bits(&mut qr, mask);
    qr.mask = mask;
    Ok(qr)
}

/// Encodes `data` (mode chosen automatically). A missing `opts.ecc` means
/// [`Ecc::Medium`].
///
/// ```
/// let code = qrkude::encode(b"HELLO WORLD", &qrkude::EncodeOptions::default())?;
/// assert_eq!(code.version(), 1);
/// assert_eq!(code.mode(), qrkude::Mode::Alphanumeric);
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn encode(data: &[u8], opts: &EncodeOptions) -> Result<QrCode, Error> {
    encode_with_ecc(data, opts, opts.ecc.unwrap_or(Ecc::Medium))
}

/// Encodes UTF-8 text. Same as [`encode`] on `text.as_bytes()`.
pub fn encode_text(text: &str, opts: &EncodeOptions) -> Result<QrCode, Error> {
    encode(text.as_bytes(), opts)
}

/// Encodes `data` applying the ECC default rule of SPEC §1.3: when
/// `opts.ecc` is `None`, the minimum ECC is `H` if the style has a logo,
/// else `M`.
///
/// ```
/// use qrkude::{encode_for_style, Ecc, EncodeOptions, Logo, Style};
/// let mut style = Style::default();
/// style.logo = Some(Logo::default());
/// let code = encode_for_style(b"https://kudelink.com", &EncodeOptions::default(), &style)?;
/// assert_eq!(code.ecc(), Ecc::High);
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn encode_for_style(data: &[u8], opts: &EncodeOptions, style: &Style) -> Result<QrCode, Error> {
    encode_with_ecc(data, opts, opts.ecc.unwrap_or_else(|| style.recommended_ecc()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn modes() {
        assert_eq!(detect_mode(b""), Mode::Numeric);
        assert_eq!(detect_mode(b"0123"), Mode::Numeric);
        assert_eq!(detect_mode(b"AB-12 $"), Mode::Alphanumeric);
        assert_eq!(detect_mode(b"ab"), Mode::Byte);
    }

    #[test]
    fn alignment() {
        assert_eq!(alignment_positions(1).1, 0);
        let (p, n) = alignment_positions(7);
        assert_eq!(&p[..n], &[6, 22, 38]);
        let (p, n) = alignment_positions(40);
        assert_eq!(&p[..n], &[6, 30, 58, 86, 114, 142, 170]);
    }

    #[test]
    fn invalid_options() {
        let o = EncodeOptions { mask: Some(8), ..Default::default() };
        assert_eq!(encode(b"x", &o), Err(Error::InvalidArgument));
        let o = EncodeOptions { min_version: 3, max_version: 2, ..Default::default() };
        assert_eq!(encode(b"x", &o), Err(Error::InvalidArgument));
        let o = EncodeOptions { min_version: 0, ..Default::default() };
        assert_eq!(encode(b"x", &o), Err(Error::InvalidArgument));
    }

    #[test]
    fn too_long() {
        let o = EncodeOptions { max_version: 1, ..Default::default() };
        assert_eq!(encode(&[b'x'; 100], &o), Err(Error::DataTooLong));
    }

    #[test]
    fn forced_mask_and_boost() {
        let o = EncodeOptions { ecc: Some(Ecc::Low), mask: Some(3), boost_ecc: false, ..Default::default() };
        let c = encode(b"hello", &o).unwrap();
        assert_eq!((c.version(), c.ecc(), c.mask()), (1, Ecc::Low, 3));
        let c = encode(b"hello", &EncodeOptions::default()).unwrap();
        assert_eq!(c.ecc(), Ecc::High);
        assert_eq!(c.size(), 21);
        assert!(c.is_dark(0, 0) && !c.is_dark(-1, 0) && !c.is_dark(21, 0));
    }
}
