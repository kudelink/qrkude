// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Canvas layout and module classification (SPEC §3, §4).

use crate::encode::{alignment_positions, QrCode};
use crate::math;
use crate::style::{LabelPosition, LogoShape, ModuleShape, Style};

/// Canvas geometry in modules ("canvas units"); multiply by your scale.
#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Layout {
    /// Canvas width.
    pub width: f64,
    /// Canvas height (larger than `width` with a label).
    pub height: f64,
    /// x of module column 0.
    pub symbol_x: f64,
    /// y of module row 0.
    pub symbol_y: f64,
    /// Modules per side.
    pub modules: usize,
    /// A logo is present.
    pub has_logo: bool,
    /// Left of the logo image box.
    pub logo_x: f64,
    /// Top of the logo image box.
    pub logo_y: f64,
    /// Side of the (square) logo image box.
    pub logo_size: f64,
    /// A label is present.
    pub has_label: bool,
    /// Text anchor x (middle).
    pub label_x: f64,
    /// Text baseline y.
    pub label_baseline: f64,
}

/// Computes the canvas layout of `code` drawn with `style`.
pub fn layout(code: &QrCode, style: &Style) -> Layout {
    let l = Ctx::new(code, style);
    Layout {
        width: l.w,
        height: l.h,
        symbol_x: l.ox,
        symbol_y: l.oy,
        modules: l.n as usize,
        has_logo: l.has_logo,
        logo_x: l.logo_cx - l.logo_half,
        logo_y: l.logo_cy - l.logo_half,
        logo_size: 2.0 * l.logo_half,
        has_label: l.has_label,
        label_x: l.label_x,
        label_baseline: l.label_baseline,
    }
}

/// Upper bound for lengths given in modules (margin, border, label size...),
/// SPEC §2.2.
pub(crate) const MAX_LENGTH: f64 = 1000.0;

/// Clamps `v` to `[lo, hi]`; NaN clamps to `lo` (as in the C reference).
#[inline]
pub(crate) fn clamp(v: f64, lo: f64, hi: f64) -> f64 {
    if !(v >= lo) {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

/// Internal layout with clamped style values (C: `qrk__layout`).
pub(crate) struct Ctx<'a> {
    pub code: &'a QrCode,
    pub style: &'a Style,
    pub n: i32,
    pub margin: f64,
    pub border: f64,
    pub bg_radius: f64,
    pub shape: ModuleShape,
    pub size: f64,
    pub roundness: f64,
    pub w: f64,
    pub h: f64,
    pub ox: f64,
    pub oy: f64,
    pub has_label: bool,
    pub label_size: f64,
    pub label_x: f64,
    pub label_baseline: f64,
    pub has_logo: bool,
    pub excavate: bool,
    pub logo_shape: LogoShape,
    pub logo_cx: f64,
    pub logo_cy: f64,
    pub logo_half: f64,
    pub logo_area_half: f64,
    pub align_styled: bool,
    pub align_count: usize,
    pub align_pos: [u8; 7],
}

impl<'a> Ctx<'a> {
    pub(crate) fn new(code: &'a QrCode, s: &'a Style) -> Ctx<'a> {
        let n = code.size() as i32;
        let margin = clamp(s.margin, 0.0, MAX_LENGTH);
        let border = clamp(s.border.width, 0.0, MAX_LENGTH);
        let frame = margin + border;
        let w = n as f64 + 2.0 * frame;
        let mut h = w;
        let ox = frame;
        let mut oy = frame;

        let mut label_size = 0.0;
        let mut label_x = 0.0;
        let mut label_baseline = 0.0;
        let label = s.active_label();
        if let Some(label) = label {
            let f = clamp(label.size, 0.1, MAX_LENGTH);
            label_size = f;
            let band_h = f * 1.5;
            h += band_h;
            let band_y = if label.position == LabelPosition::Top {
                oy += band_h;
                border
            } else {
                w - border
            };
            label_x = w / 2.0;
            label_baseline = band_y + f * 1.1;
        }
        let min_side = if w < h { w } else { h };
        let bg_radius = clamp(s.background.radius, 0.0, min_side / 2.0);

        let mut ctx = Ctx {
            code,
            style: s,
            n,
            margin,
            border,
            bg_radius,
            shape: s.modules.shape,
            size: clamp(s.modules.size, 0.1, 1.0),
            roundness: clamp(s.modules.roundness, 0.0, 1.0),
            w,
            h,
            ox,
            oy,
            has_label: label.is_some(),
            label_size,
            label_x,
            label_baseline,
            has_logo: false,
            excavate: false,
            logo_shape: LogoShape::Square,
            logo_cx: 0.0,
            logo_cy: 0.0,
            logo_half: 0.0,
            logo_area_half: 0.0,
            align_styled: false,
            align_count: 0,
            align_pos: [0; 7],
        };

        if let Some(logo) = s.active_logo() {
            ctx.has_logo = true;
            ctx.excavate = logo.excavate;
            ctx.logo_shape = logo.shape;
            ctx.logo_cx = ox + n as f64 / 2.0;
            ctx.logo_cy = oy + n as f64 / 2.0;
            ctx.logo_half = n as f64 * clamp(logo.size, 0.0, 1.0) / 2.0;
            ctx.logo_area_half = ctx.logo_half + clamp(logo.margin, 0.0, MAX_LENGTH);
        }

        if s.alignment.frame.is_some() {
            ctx.align_styled = true;
            let (pos, count) = alignment_positions(code.version());
            ctx.align_pos = pos;
            ctx.align_count = count;
        }
        ctx
    }

    pub(crate) fn is_finder(&self, x: i32, y: i32) -> bool {
        let n = self.n;
        (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7)
    }

    pub(crate) fn is_alignment_center(&self, i: usize, j: usize) -> bool {
        let k = self.align_count;
        !((i == 0 && j == 0) || (i == 0 && j == k - 1) || (i == k - 1 && j == 0))
    }

    pub(crate) fn in_styled_alignment(&self, x: i32, y: i32) -> bool {
        if !self.align_styled {
            return false;
        }
        let k = self.align_count;
        for i in 0..k {
            let dy = y - self.align_pos[i] as i32;
            if !(-2..=2).contains(&dy) {
                continue;
            }
            for j in 0..k {
                let dx = x - self.align_pos[j] as i32;
                if (-2..=2).contains(&dx) && self.is_alignment_center(i, j) {
                    return true;
                }
            }
        }
        false
    }

    /// Module overlaps the logo area (box + margin). Used for excavation and
    /// for the damage estimate.
    pub(crate) fn in_logo_area(&self, x: i32, y: i32) -> bool {
        let c = self.n as f64 / 2.0;
        let h = self.logo_area_half;
        let dx = math::fabs(x as f64 + 0.5 - c);
        let dy = math::fabs(y as f64 + 0.5 - c);
        if self.logo_shape == LogoShape::Circle {
            let nx = if dx - 0.5 > 0.0 { dx - 0.5 } else { 0.0 };
            let ny = if dy - 0.5 > 0.0 { dy - 0.5 } else { 0.0 };
            return nx * nx + ny * ny < h * h;
        }
        dx < h + 0.5 && dy < h + 0.5
    }

    pub(crate) fn is_excavated(&self, x: i32, y: i32) -> bool {
        self.excavate && self.in_logo_area(x, y)
    }

    pub(crate) fn is_data_cell(&self, x: i32, y: i32) -> bool {
        let n = self.n;
        if x < 0 || y < 0 || x >= n || y >= n {
            return false;
        }
        !self.is_finder(x, y) && !self.in_styled_alignment(x, y) && !self.is_excavated(x, y)
    }

    pub(crate) fn is_data_dark(&self, x: i32, y: i32) -> bool {
        self.code.is_dark(x, y) && self.is_data_cell(x, y)
    }
}
