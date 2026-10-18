// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Scannability checks (SPEC §10, §11).

use alloc::vec::Vec;

use crate::encode::{
    ecc_codewords_per_block, for_each_data_module, num_ecc_blocks, raw_codewords, QrCode,
};
use crate::geometry::{ball_shape, resolve_paint, Layer};
use crate::layout::Ctx;
use crate::math;
use crate::style::{Color, EyeShape, ModuleShape, Paint, Style, MAX_STOPS};

/// A scannability warning returned by [`lint`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum LintFlag {
    /// Logo damage in (0.7, 1]: decoding is at risk.
    LogoRisky,
    /// Logo damage > 1: the code cannot be decoded.
    LogoUnreadable,
    /// WCAG contrast below 3:1.
    LowContrast,
    /// Light modules on a dark background.
    Inverted,
    /// No (or fully transparent) background.
    TransparentBackground,
    /// Quiet zone below 2 modules.
    QuietZone,
    /// Module size below 0.6.
    SmallModules,
    /// Diamond eye frame, or diamond ball in a square/leaf frame.
    EyeShapes,
    /// The modules, eye frame or eye ball paint is missing or fully
    /// transparent: nothing (or not all of the code) is drawn.
    Invisible,
}

impl LintFlag {
    /// All flags in the order of SPEC §11 (also the order of [`lint`]).
    pub const ALL: [LintFlag; 9] = [
        LintFlag::LogoRisky,
        LintFlag::LogoUnreadable,
        LintFlag::LowContrast,
        LintFlag::Inverted,
        LintFlag::TransparentBackground,
        LintFlag::QuietZone,
        LintFlag::SmallModules,
        LintFlag::EyeShapes,
        LintFlag::Invisible,
    ];

    /// `"logo-risky"`, `"low-contrast"`, ...
    pub fn name(self) -> &'static str {
        match self {
            LintFlag::LogoRisky => "logo-risky",
            LintFlag::LogoUnreadable => "logo-unreadable",
            LintFlag::LowContrast => "low-contrast",
            LintFlag::Inverted => "inverted",
            LintFlag::TransparentBackground => "transparent-background",
            LintFlag::QuietZone => "quiet-zone",
            LintFlag::SmallModules => "small-modules",
            LintFlag::EyeShapes => "eye-shapes",
            LintFlag::Invisible => "invisible",
        }
    }

    /// Bit value, identical to the C `QRK_LINT_*` constants.
    pub fn bit(self) -> u32 {
        1 << self as u32
    }
}

impl core::fmt::Display for LintFlag {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(self.name())
    }
}

fn channel(v: f64) -> f64 {
    if v <= 0.04045 {
        v / 12.92
    } else {
        math::pow((v + 0.055) / 1.055, 2.4)
    }
}

/// WCAG relative luminance of `c` composited over the opaque color `under`.
fn luminance_over(c: Color, under: Color) -> f64 {
    const WEIGHT: [f64; 3] = [0.2126, 0.7152, 0.0722];
    let a = (c.0 & 0xFF) as f64 / 255.0;
    let mut l = 0.0;
    for (i, w) in WEIGHT.iter().enumerate() {
        let shift = 24 - 8 * i;
        let v = ((c.0 >> shift) & 0xFF) as f64 / 255.0;
        let u = ((under.0 >> shift) & 0xFF) as f64 / 255.0;
        l += w * channel(v * a + u * (1.0 - a));
    }
    l
}

/// The colors a paint can produce: its color or its stops (at most
/// [`MAX_STOPS`]). A missing paint is one fully transparent color.
fn paint_colors(p: &Paint) -> ([Color; MAX_STOPS], usize) {
    let mut out = [Color::TRANSPARENT; MAX_STOPS];
    match p {
        Paint::Solid(c) => {
            out[0] = *c;
            (out, 1)
        }
        Paint::Linear(_) | Paint::Radial(_) if !p.stops().is_empty() => {
            let stops = p.stops();
            for (o, s) in out.iter_mut().zip(stops) {
                *o = s.color;
            }
            (out, stops.len())
        }
        _ => (out, 1),
    }
}

/// `c` made opaque over white, rounded per 8-bit channel.
fn opaque_over_white(c: Color) -> Color {
    let a = (c.0 & 0xFF) as f64 / 255.0;
    let mut out = 0xFFu32;
    for shift in [8u32, 16, 24] {
        let v = ((c.0 >> shift) & 0xFF) as f64 * a + 255.0 * (1.0 - a);
        out |= ((v + 0.5) as i32 as u32) << shift;
    }
    Color(out)
}

/// Lint flags as a bit set (see [`LintFlag::bit`]).
pub fn lint_bits(code: &QrCode, style: &Style) -> u32 {
    let mut w = 0u32;
    let l = Ctx::new(code, style);

    if l.has_logo {
        let d = logo_damage(code, style);
        if d > 1.0 {
            w |= LintFlag::LogoUnreadable.bit();
        } else if d > 0.7 {
            w |= LintFlag::LogoRisky.bit();
        }
    }

    // background colors, made opaque over white (what a page usually shows)
    let (mut bg, nbg) = paint_colors(&style.background.color);
    let bg = &mut bg[..nbg];
    let mut transparent = true;
    for c in bg.iter_mut() {
        if c.a() != 0 {
            transparent = false;
        }
        *c = opaque_over_white(*c);
    }
    if transparent {
        w |= LintFlag::TransparentBackground.bit();
    }
    let (mut bg_lo, mut bg_hi) = (1.0f64, 0.0f64);
    for &c in bg.iter() {
        let v = luminance_over(c, c);
        if v < bg_lo {
            bg_lo = v;
        }
        if v > bg_hi {
            bg_hi = v;
        }
    }

    // foreground: modules and eyes composited over every background color
    let (mut fg_lo, mut fg_hi) = (1.0f64, 0.0f64);
    for layer in [Layer::Modules, Layer::EyeFrames, Layer::EyeBalls] {
        let p = resolve_paint(style, layer);
        let (fg, nfg) = paint_colors(p.get());
        let mut visible = false;
        for &f in &fg[..nfg] {
            if f.a() != 0 {
                visible = true;
            }
            for &b in bg.iter() {
                let v = luminance_over(f, b);
                if v < fg_lo {
                    fg_lo = v;
                }
                if v > fg_hi {
                    fg_hi = v;
                }
            }
        }
        if !visible {
            w |= LintFlag::Invisible.bit();
        }
    }
    let contrast = if (fg_lo + fg_hi) / 2.0 > (bg_lo + bg_hi) / 2.0 {
        w |= LintFlag::Inverted.bit();
        (fg_lo + 0.05) / (bg_hi + 0.05)
    } else {
        (bg_lo + 0.05) / (fg_hi + 0.05)
    };
    if contrast < 3.0 {
        w |= LintFlag::LowContrast.bit();
    }

    if l.margin < 2.0 {
        w |= LintFlag::QuietZone.bit();
    }

    let sized = matches!(
        l.shape,
        ModuleShape::Square
            | ModuleShape::Dot
            | ModuleShape::Rounded
            | ModuleShape::Diamond
            | ModuleShape::Star
            | ModuleShape::HBars
            | ModuleShape::VBars
    );
    if sized && l.size < 0.6 {
        w |= LintFlag::SmallModules.bit();
    }

    // Measured with zxing: diamond frames break finder detection with any
    // ball; diamond balls only work inside round or beveled frames.
    let frame = style.eyes.frame;
    let ball = ball_shape(style.eyes.ball);
    if frame == EyeShape::Diamond
        || (ball == EyeShape::Diamond && (frame == EyeShape::Square || frame == EyeShape::Leaf))
    {
        w |= LintFlag::EyeShapes.bit();
    }
    w
}

/// Scannability warnings, in the order of SPEC §11.
///
/// ```
/// use qrkude::{encode_text, lint, EncodeOptions, LintFlag, Style};
/// let code = encode_text("hi", &EncodeOptions::default())?;
/// let mut style = Style::default();
/// style.margin = 1.0;
/// assert_eq!(lint(&code, &style), [LintFlag::QuietZone]);
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn lint(code: &QrCode, style: &Style) -> Vec<LintFlag> {
    let bits = lint_bits(code, style);
    LintFlag::ALL.iter().copied().filter(|f| bits & f.bit() != 0).collect()
}

/// Worst-case logo damage: max over RS blocks of (codewords hidden by the
/// logo area) / (codewords the block can correct). 0 without a logo; above 1
/// the code cannot be decoded. Keep it at or below ~0.6.
pub fn logo_damage(code: &QrCode, style: &Style) -> f64 {
    let l = Ctx::new(code, style);
    if !l.has_logo {
        return 0.0;
    }
    let ver = code.version();
    let ecc = code.ecc();
    let num_blocks = num_ecc_blocks(ecc, ver);
    let block_ecc = ecc_codewords_per_block(ecc, ver);
    let raw = raw_codewords(ver);
    let data_len = raw - num_blocks * block_ecc;
    let num_short = num_blocks - raw % num_blocks;
    let short_len = raw / num_blocks - block_ecc;

    let mut damaged = [0i32; 81];
    let mut last_cw = -1;
    let mut last_hit = false;
    let mut bit = 0;
    for_each_data_module(ver, |x, y| {
        if bit >= raw * 8 {
            return;
        }
        let cw = bit / 8;
        bit += 1;
        if cw != last_cw {
            last_cw = cw;
            last_hit = false;
        }
        if last_hit || !l.in_logo_area(x, y) {
            return;
        }
        last_hit = true;
        let block = if cw < data_len {
            if cw < short_len * num_blocks {
                cw % num_blocks
            } else {
                num_short + (cw - short_len * num_blocks)
            }
        } else {
            (cw - data_len) % num_blocks
        };
        damaged[block as usize] += 1;
    });

    let mut worst = 0.0;
    let correctable = block_ecc / 2;
    for &d in &damaged[..num_blocks as usize] {
        let ratio = d as f64 / correctable as f64;
        if ratio > worst {
            worst = ratio;
        }
    }
    worst
}
