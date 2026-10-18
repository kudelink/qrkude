// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Shape model, normalizing path emitter and layers (SPEC §5-§7, §8.4).
//! Every renderer (SVG writer, user sinks) sees the same command stream.

use alloc::vec::Vec;

use crate::encode::QrCode;
use crate::layout::{clamp, Ctx};
use crate::math;
use crate::style::{EyeShape, LogoShape, ModuleShape, Paint, Style};

/// Receives the geometry of a layer. All sub-paths are closed; fill with the
/// non-zero rule. Coordinates are in modules (canvas units), y down.
pub trait PathSink {
    /// Starts a sub-path at `(x, y)`.
    fn move_to(&mut self, x: f64, y: f64);
    /// Straight line to `(x, y)`.
    fn line_to(&mut self, x: f64, y: f64);
    /// Circular arc of radius `r` from the current point to `(x, y)` around
    /// the center `(cx, cy)`. `clockwise` refers to the screen (y down).
    /// Arcs never exceed 90 degrees.
    fn arc_to(&mut self, r: f64, clockwise: bool, x: f64, y: f64, cx: f64, cy: f64);
    /// Closes the current sub-path.
    fn close(&mut self);
}

impl<S: PathSink + ?Sized> PathSink for &mut S {
    fn move_to(&mut self, x: f64, y: f64) {
        (**self).move_to(x, y)
    }
    fn line_to(&mut self, x: f64, y: f64) {
        (**self).line_to(x, y)
    }
    fn arc_to(&mut self, r: f64, clockwise: bool, x: f64, y: f64, cx: f64, cy: f64) {
        (**self).arc_to(r, clockwise, x, y, cx, cy)
    }
    fn close(&mut self) {
        (**self).close()
    }
}

/// A recorded path command (see [`PathSink`]); `Vec<PathCommand>` is a sink.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PathCommand {
    /// Start a sub-path.
    MoveTo {
        /// x
        x: f64,
        /// y
        y: f64,
    },
    /// Straight line.
    LineTo {
        /// x
        x: f64,
        /// y
        y: f64,
    },
    /// Circular arc around `(cx, cy)`.
    ArcTo {
        /// Radius.
        r: f64,
        /// Clockwise on screen (y down).
        clockwise: bool,
        /// End x.
        x: f64,
        /// End y.
        y: f64,
        /// Center x.
        cx: f64,
        /// Center y.
        cy: f64,
    },
    /// Close the sub-path.
    Close,
}

impl PathSink for Vec<PathCommand> {
    fn move_to(&mut self, x: f64, y: f64) {
        self.push(PathCommand::MoveTo { x, y });
    }
    fn line_to(&mut self, x: f64, y: f64) {
        self.push(PathCommand::LineTo { x, y });
    }
    fn arc_to(&mut self, r: f64, clockwise: bool, x: f64, y: f64, cx: f64, cy: f64) {
        self.push(PathCommand::ArcTo { r, clockwise, x, y, cx, cy });
    }
    fn close(&mut self) {
        self.push(PathCommand::Close);
    }
}

/// A sink that ignores everything (for counting sub-paths).
pub(crate) struct NullSink;

impl PathSink for NullSink {
    fn move_to(&mut self, _: f64, _: f64) {}
    fn line_to(&mut self, _: f64, _: f64) {}
    fn arc_to(&mut self, _: f64, _: bool, _: f64, _: f64, _: f64, _: f64) {}
    fn close(&mut self) {}
}

/// Drawing layers, in SVG paint order.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Layer {
    /// Canvas rectangle (rounded with `background.radius`).
    Background,
    /// Border ring (only when `border.width > 0`).
    Border,
    /// Data modules.
    Modules,
    /// Finder frames, then styled alignment frames.
    EyeFrames,
    /// Finder balls, then styled alignment balls.
    EyeBalls,
    /// Logo background (only with a logo and a visible `logo.background`).
    LogoBackground,
}

impl Layer {
    /// All layers in paint order.
    pub const ALL: [Layer; 6] = [
        Layer::Background,
        Layer::Border,
        Layer::Modules,
        Layer::EyeFrames,
        Layer::EyeBalls,
        Layer::LogoBackground,
    ];
}

// ---- shape model ------------------------------------------------------------

/// Corner order everywhere: top-left, top-right, bottom-right, bottom-left.
pub(crate) const TL: usize = 0;
pub(crate) const TR: usize = 1;
pub(crate) const BR: usize = 2;
pub(crate) const BL: usize = 3;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum CornerKind {
    None,
    Round,
    Chamfer,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct Corner {
    pub kind: CornerKind,
    pub a: f64, // radius or chamfer length
}

const NO_CORNER: Corner = Corner { kind: CornerKind::None, a: 0.0 };

impl Corner {
    fn amount(&self) -> f64 {
        if self.kind == CornerKind::None {
            0.0
        } else {
            self.a
        }
    }
}

/// Axis-aligned rectangle with per-corner rounding / chamfering.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct CRect {
    pub x0: f64,
    pub y0: f64,
    pub x1: f64,
    pub y1: f64,
    pub c: [Corner; 4],
}

impl CRect {
    pub(crate) fn simple(x0: f64, y0: f64, x1: f64, y1: f64, kind: CornerKind, a: f64) -> CRect {
        CRect { x0, y0, x1, y1, c: [Corner { kind, a }; 4] }
    }
}

/// Eye ball shape: a grid of dots breaks the 1:1:3:1:1 ratio, use a circle.
pub(crate) fn ball_shape(shape: EyeShape) -> EyeShape {
    if shape == EyeShape::Dots {
        EyeShape::Circle
    } else {
        shape
    }
}

pub(crate) fn eye_corners(shape: EyeShape, s: f64, cc: usize) -> [Corner; 4] {
    let all = |kind, a| [Corner { kind, a }; 4];
    match shape {
        EyeShape::Rounded => all(CornerKind::Round, s * 0.25),
        EyeShape::Circle => all(CornerKind::Round, s / 2.0),
        // leaf/drop use 0.36 * s: fully rounded versions break finder detection
        EyeShape::Leaf => {
            let mut c = [NO_CORNER; 4];
            c[cc] = Corner { kind: CornerKind::Round, a: s * 0.36 };
            c[(cc + 2) % 4] = c[cc];
            c
        }
        EyeShape::Drop => {
            let mut c = all(CornerKind::Round, s * 0.36);
            c[cc] = NO_CORNER;
            c
        }
        EyeShape::Bevel => all(CornerKind::Chamfer, s * 0.25),
        EyeShape::Diamond => all(CornerKind::Chamfer, s / 2.0),
        EyeShape::Square | EyeShape::Dots => [NO_CORNER; 4],
    }
}

pub(crate) fn inner_corners(outer: &[Corner; 4], s_inner: f64) -> [Corner; 4] {
    let mut out = [NO_CORNER; 4];
    for i in 0..4 {
        let mut a = outer[i].a;
        match outer[i].kind {
            CornerKind::Round => a -= 1.0,
            CornerKind::Chamfer => a -= 2.0 - math::sqrt(2.0),
            CornerKind::None => {}
        }
        a = clamp(a, 0.0, s_inner / 2.0);
        out[i] = if a > 0.0 { Corner { kind: outer[i].kind, a } } else { NO_CORNER };
    }
    out
}

impl Ctx<'_> {
    /// Cell-local shape of a dark data module; `None` for the star shape.
    pub(crate) fn module_crect(&self, x: i32, y: i32) -> Option<CRect> {
        let s = self.size;
        let d = (1.0 - s) / 2.0;
        let rho = self.roundness;
        let mut r = CRect::simple(d, d, 1.0 - d, 1.0 - d, CornerKind::None, 0.0);

        match self.shape {
            ModuleShape::Square => return Some(r),
            ModuleShape::Dot => {
                r.c = [Corner { kind: CornerKind::Round, a: s / 2.0 }; 4];
                return Some(r);
            }
            ModuleShape::Rounded => {
                r.c = [Corner { kind: CornerKind::Round, a: rho * s / 2.0 }; 4];
                return Some(r);
            }
            ModuleShape::Diamond => {
                r.c = [Corner { kind: CornerKind::Chamfer, a: s / 2.0 }; 4];
                return Some(r);
            }
            ModuleShape::Star => return None,
            ModuleShape::HBars => {
                let l = self.is_data_dark(x - 1, y);
                let rt = self.is_data_dark(x + 1, y);
                if l {
                    r.x0 = 0.0;
                }
                if rt {
                    r.x1 = 1.0;
                }
                let round = Corner { kind: CornerKind::Round, a: rho * s / 2.0 };
                if !l {
                    r.c[TL] = round;
                    r.c[BL] = round;
                }
                if !rt {
                    r.c[TR] = round;
                    r.c[BR] = round;
                }
                return Some(r);
            }
            ModuleShape::VBars => {
                let u = self.is_data_dark(x, y - 1);
                let dn = self.is_data_dark(x, y + 1);
                if u {
                    r.y0 = 0.0;
                }
                if dn {
                    r.y1 = 1.0;
                }
                let round = Corner { kind: CornerKind::Round, a: rho * s / 2.0 };
                if !u {
                    r.c[TL] = round;
                    r.c[TR] = round;
                }
                if !dn {
                    r.c[BL] = round;
                    r.c[BR] = round;
                }
                return Some(r);
            }
            ModuleShape::Fluid | ModuleShape::Liquid | ModuleShape::Bevel | ModuleShape::Classy => {}
        }

        // connected shapes always fill the whole cell
        r.x0 = 0.0;
        r.y0 = 0.0;
        r.x1 = 1.0;
        r.y1 = 1.0;
        let up = self.is_data_dark(x, y - 1);
        let down = self.is_data_dark(x, y + 1);
        let left = self.is_data_dark(x - 1, y);
        let right = self.is_data_dark(x + 1, y);
        let open = [!up && !left, !up && !right, !down && !right, !down && !left];
        let kind = if self.shape == ModuleShape::Bevel { CornerKind::Chamfer } else { CornerKind::Round };
        for (i, &is_open) in open.iter().enumerate() {
            let mut used = is_open;
            if self.shape == ModuleShape::Classy && (i == TR || i == BL) {
                used = false;
            }
            if used {
                r.c[i] = Corner { kind, a: rho / 2.0 };
            }
        }
        Some(r)
    }

    /// Which corners of a light data cell get a liquid fillet (bit i = corner i).
    pub(crate) fn fillet_mask(&self, x: i32, y: i32) -> u8 {
        if self.shape != ModuleShape::Liquid || self.roundness <= 0.0 {
            return 0;
        }
        let up = self.is_data_dark(x, y - 1);
        let down = self.is_data_dark(x, y + 1);
        let left = self.is_data_dark(x - 1, y);
        let right = self.is_data_dark(x + 1, y);
        let mut m = 0u8;
        if up && left && self.is_data_dark(x - 1, y - 1) {
            m |= 1 << TL;
        }
        if up && right && self.is_data_dark(x + 1, y - 1) {
            m |= 1 << TR;
        }
        if down && right && self.is_data_dark(x + 1, y + 1) {
            m |= 1 << BR;
        }
        if down && left && self.is_data_dark(x - 1, y + 1) {
            m |= 1 << BL;
        }
        m
    }

    pub(crate) fn logo_bg_crect(&self) -> CRect {
        let h = self.logo_area_half;
        let (kind, a) = match self.logo_shape {
            LogoShape::Rounded => (CornerKind::Round, h * 0.3),
            LogoShape::Circle => (CornerKind::Round, h),
            LogoShape::Square => (CornerKind::None, 0.0),
        };
        CRect::simple(self.logo_cx - h, self.logo_cy - h, self.logo_cx + h, self.logo_cy + h, kind, a)
    }

    /// Canvas rectangle inset by `inset`, rounded with the background radius.
    pub(crate) fn canvas_crect(&self, inset: f64) -> CRect {
        let a = self.bg_radius - inset;
        CRect::simple(
            inset,
            inset,
            self.w - inset,
            self.h - inset,
            if a > 0.0 { CornerKind::Round } else { CornerKind::None },
            if a > 0.0 { a } else { 0.0 },
        )
    }
}

// ---- paints -----------------------------------------------------------------

/// Paint used by a layer: eye paints resolved (`None` = modules paint),
/// border and logo background as solid colors.
pub fn layer_paint(style: &Style, layer: Layer) -> Paint {
    resolve_paint(style, layer).into_owned()
}

pub(crate) enum PaintRef<'a> {
    Borrowed(&'a Paint),
    Owned(Paint),
}

impl PaintRef<'_> {
    pub(crate) fn get(&self) -> &Paint {
        match self {
            PaintRef::Borrowed(p) => p,
            PaintRef::Owned(p) => p,
        }
    }
    fn into_owned(self) -> Paint {
        match self {
            PaintRef::Borrowed(p) => p.clone(),
            PaintRef::Owned(p) => p,
        }
    }
}

pub(crate) fn resolve_paint(s: &Style, layer: Layer) -> PaintRef<'_> {
    match layer {
        Layer::Background => PaintRef::Borrowed(&s.background.color),
        Layer::Border => PaintRef::Owned(Paint::Solid(s.border.color)),
        Layer::EyeFrames if !s.eyes.frame_color.is_none() => PaintRef::Borrowed(&s.eyes.frame_color),
        Layer::EyeBalls if !s.eyes.ball_color.is_none() => PaintRef::Borrowed(&s.eyes.ball_color),
        Layer::LogoBackground => PaintRef::Owned(Paint::Solid(
            s.logo.as_ref().map(|l| l.background).unwrap_or_default(),
        )),
        _ => PaintRef::Borrowed(&s.modules.color),
    }
}

/// Gradient geometry in canvas units, as written to the SVG (SPEC §8.4).
#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct GradientGeometry {
    /// Linear start x.
    pub x1: f64,
    /// Linear start y.
    pub y1: f64,
    /// Linear end x.
    pub x2: f64,
    /// Linear end y.
    pub y2: f64,
    /// Radial center x.
    pub cx: f64,
    /// Radial center y.
    pub cy: f64,
    /// Radial radius.
    pub r: f64,
}

/// Gradient geometry of the paint of `layer` (see [`layer_paint`]).
/// Linear fields are computed for the paint's angle (0 for non-linear
/// paints) and radial fields for its center/radius (defaults for non-radial
/// paints).
pub fn gradient_geometry(code: &QrCode, style: &Style, layer: Layer) -> GradientGeometry {
    let l = Ctx::new(code, style);
    let p = resolve_paint(style, layer);
    gradient_geometry_for(&l, layer, p.get())
}

pub(crate) fn gradient_geometry_for(l: &Ctx<'_>, layer: Layer, p: &Paint) -> GradientGeometry {
    let (ax, ay, aw, ah) = if layer == Layer::Background {
        (0.0, 0.0, l.w, l.h)
    } else {
        (l.ox, l.oy, l.n as f64, l.n as f64)
    };
    let cx = ax + aw / 2.0;
    let cy = ay + ah / 2.0;

    let (angle, pcx, pcy, pr) = match p {
        Paint::Linear(g) => (g.angle, 0.5, 0.5, 0.75),
        Paint::Radial(g) => (0.0, g.cx, g.cy, g.r),
        _ => (0.0, 0.5, 0.5, 0.75),
    };
    let mut a = math::fmod(angle, 360.0);
    if a < 0.0 {
        a += 360.0;
    }
    let (dx, dy) = if a == 0.0 {
        (1.0, 0.0)
    } else if a == 90.0 {
        (0.0, 1.0)
    } else if a == 180.0 {
        (-1.0, 0.0)
    } else if a == 270.0 {
        (0.0, -1.0)
    } else {
        let rad = a * core::f64::consts::PI / 180.0;
        (math::cos(rad), math::sin(rad))
    };
    let half = (math::fabs(dx) * aw + math::fabs(dy) * ah) / 2.0;
    GradientGeometry {
        x1: cx - dx * half,
        y1: cy - dy * half,
        x2: cx + dx * half,
        y2: cy + dy * half,
        cx: ax + aw * pcx,
        cy: ay + ah * pcy,
        r: pr * if aw > ah { aw } else { ah },
    }
}

// ---- path emitter -----------------------------------------------------------

/// Normalizes the command stream so that every sink (and every port) sees the
/// same thing: zero-length lines are dropped and a line that returns to the
/// sub-path start right before close is omitted.
pub(crate) struct Emitter<S: PathSink> {
    sink: S,
    sx: f64,
    sy: f64,
    cx: f64,
    cy: f64,
    pending: bool,
    px: f64,
    py: f64,
    pub count: usize,
}

impl<S: PathSink> Emitter<S> {
    pub(crate) fn new(sink: S) -> Self {
        Emitter { sink, sx: 0.0, sy: 0.0, cx: 0.0, cy: 0.0, pending: false, px: 0.0, py: 0.0, count: 0 }
    }

    fn flush(&mut self) {
        if self.pending {
            self.sink.line_to(self.px, self.py);
            self.pending = false;
        }
    }

    pub(crate) fn move_to(&mut self, x: f64, y: f64) {
        self.flush();
        self.sx = x;
        self.cx = x;
        self.sy = y;
        self.cy = y;
        self.count += 1;
        self.sink.move_to(x, y);
    }

    pub(crate) fn line_to(&mut self, x: f64, y: f64) {
        if x == self.cx && y == self.cy {
            return;
        }
        self.flush();
        self.pending = true;
        self.px = x;
        self.cx = x;
        self.py = y;
        self.cy = y;
    }

    pub(crate) fn arc_to(&mut self, r: f64, cw: bool, x: f64, y: f64, ccx: f64, ccy: f64) {
        self.flush();
        self.cx = x;
        self.cy = y;
        self.sink.arc_to(r, cw, x, y, ccx, ccy);
    }

    pub(crate) fn close(&mut self) {
        if self.pending && !(self.px == self.sx && self.py == self.sy) {
            self.flush();
        }
        self.pending = false;
        self.cx = self.sx;
        self.cy = self.sy;
        self.sink.close();
    }

    fn corner(&mut self, c: &Corner, cw: bool, x: f64, y: f64, ccx: f64, ccy: f64) {
        if c.kind == CornerKind::Round && c.a > 0.0 {
            self.arc_to(c.a, cw, x, y, ccx, ccy);
        } else if c.kind == CornerKind::Chamfer && c.a > 0.0 {
            self.line_to(x, y);
        }
    }

    /// Clockwise (filled) rectangle with corner treatments.
    pub(crate) fn crect(&mut self, r: &CRect) {
        let (x0, y0, x1, y1) = (r.x0, r.y0, r.x1, r.y1);
        let tl = r.c[TL].amount();
        let tr = r.c[TR].amount();
        let br = r.c[BR].amount();
        let bl = r.c[BL].amount();
        self.move_to(x0 + tl, y0);
        self.line_to(x1 - tr, y0);
        self.corner(&r.c[TR], true, x1, y0 + tr, x1 - tr, y0 + tr);
        self.line_to(x1, y1 - br);
        self.corner(&r.c[BR], true, x1 - br, y1, x1 - br, y1 - br);
        self.line_to(x0 + bl, y1);
        self.corner(&r.c[BL], true, x0, y1 - bl, x0 + bl, y1 - bl);
        self.line_to(x0, y0 + tl);
        self.corner(&r.c[TL], true, x0 + tl, y0, x0 + tl, y0 + tl);
        self.close();
    }

    /// Counter-clockwise version, used for holes.
    pub(crate) fn crect_ccw(&mut self, r: &CRect) {
        let (x0, y0, x1, y1) = (r.x0, r.y0, r.x1, r.y1);
        let tl = r.c[TL].amount();
        let tr = r.c[TR].amount();
        let br = r.c[BR].amount();
        let bl = r.c[BL].amount();
        self.move_to(x0, y0 + tl);
        self.line_to(x0, y1 - bl);
        self.corner(&r.c[BL], false, x0 + bl, y1, x0 + bl, y1 - bl);
        self.line_to(x1 - br, y1);
        self.corner(&r.c[BR], false, x1, y1 - br, x1 - br, y1 - br);
        self.line_to(x1, y0 + tr);
        self.corner(&r.c[TR], false, x1 - tr, y0, x1 - tr, y0 + tr);
        self.line_to(x0 + tl, y0);
        self.corner(&r.c[TL], false, x0, y0 + tl, x0 + tl, y0 + tl);
        self.close();
    }

    /// Four-point star: tips at distance h, concave arcs of radius
    /// h * sqrt(2.5) centered at (+-1.5h, +-1.5h).
    fn star(&mut self, cx: f64, cy: f64, h: f64) {
        let r = math::sqrt(2.5) * h;
        let c = 1.5 * h;
        self.move_to(cx, cy - h);
        self.arc_to(r, false, cx + h, cy, cx + c, cy - c);
        self.arc_to(r, false, cx, cy + h, cx + c, cy + c);
        self.arc_to(r, false, cx - h, cy, cx - c, cy + c);
        self.arc_to(r, false, cx, cy - h, cx - c, cy - c);
        self.close();
    }

    fn fillet(&mut self, corner: usize, x: f64, y: f64, r: f64) {
        match corner {
            TL => {
                self.move_to(x, y);
                self.line_to(x + r, y);
                self.arc_to(r, false, x, y + r, x + r, y + r);
            }
            TR => {
                self.move_to(x + 1.0, y);
                self.line_to(x + 1.0, y + r);
                self.arc_to(r, false, x + 1.0 - r, y, x + 1.0 - r, y + r);
            }
            BR => {
                self.move_to(x + 1.0, y + 1.0);
                self.line_to(x + 1.0 - r, y + 1.0);
                self.arc_to(r, false, x + 1.0, y + 1.0 - r, x + 1.0 - r, y + 1.0 - r);
            }
            _ => {
                self.move_to(x, y + 1.0);
                self.line_to(x, y + 1.0 - r);
                self.arc_to(r, false, x + r, y + 1.0, x + r, y + 1.0 - r);
            }
        }
        self.close();
    }

    fn modules(&mut self, l: &Ctx<'_>) {
        let n = l.n;
        let (ox, oy, s) = (l.ox, l.oy, l.size);
        let d = (1.0 - s) / 2.0;

        if (l.shape == ModuleShape::Square && s == 1.0) || l.shape == ModuleShape::HBars {
            let bars = l.shape == ModuleShape::HBars;
            let inset = if bars { d } else { 0.0 };
            let a = if bars { l.roundness * s / 2.0 } else { 0.0 };
            let kind = if bars { CornerKind::Round } else { CornerKind::None };
            for y in 0..n {
                let mut x = 0;
                while x < n {
                    if !l.is_data_dark(x, y) {
                        x += 1;
                        continue;
                    }
                    let x0 = x;
                    while x < n && l.is_data_dark(x, y) {
                        x += 1;
                    }
                    let (yf, x0f, xf) = (y as f64, x0 as f64, x as f64);
                    self.crect(&CRect::simple(
                        ox + x0f + inset,
                        oy + yf + inset,
                        ox + xf - inset,
                        oy + yf + 1.0 - inset,
                        kind,
                        a,
                    ));
                }
            }
            return;
        }
        if l.shape == ModuleShape::VBars {
            let a = l.roundness * s / 2.0;
            for x in 0..n {
                let mut y = 0;
                while y < n {
                    if !l.is_data_dark(x, y) {
                        y += 1;
                        continue;
                    }
                    let y0 = y;
                    while y < n && l.is_data_dark(x, y) {
                        y += 1;
                    }
                    let (xf, y0f, yf) = (x as f64, y0 as f64, y as f64);
                    self.crect(&CRect::simple(
                        ox + xf + d,
                        oy + y0f + d,
                        ox + xf + 1.0 - d,
                        oy + yf - d,
                        CornerKind::Round,
                        a,
                    ));
                }
            }
            return;
        }

        for y in 0..n {
            for x in 0..n {
                let cx = ox + x as f64;
                let cy = oy + y as f64;
                if l.is_data_dark(x, y) {
                    match l.module_crect(x, y) {
                        None => self.star(cx + 0.5, cy + 0.5, s / 2.0),
                        Some(mut r) => {
                            r.x0 += cx;
                            r.x1 += cx;
                            r.y0 += cy;
                            r.y1 += cy;
                            self.crect(&r);
                        }
                    }
                } else if l.is_data_cell(x, y) {
                    let m = l.fillet_mask(x, y);
                    for c in 0..4 {
                        if m & (1 << c) != 0 {
                            self.fillet(c, cx, cy, l.roundness / 2.0);
                        }
                    }
                }
            }
        }
    }

    /// One eye-like pattern: frame (ring) or ball of size `s` at `(x, y)`.
    fn eye_part(&mut self, shape: EyeShape, x: f64, y: f64, s: f64, ring: bool, cc: usize) {
        if shape == EyeShape::Dots {
            let k = s as i32;
            for j in 0..k {
                for i in 0..k {
                    if ring && !(i == 0 || j == 0 || i == k - 1 || j == k - 1) {
                        continue;
                    }
                    let (fi, fj) = (i as f64, j as f64);
                    self.crect(&CRect::simple(
                        x + fi,
                        y + fj,
                        x + fi + 1.0,
                        y + fj + 1.0,
                        CornerKind::Round,
                        0.5,
                    ));
                }
            }
            return;
        }
        let outer = CRect { x0: x, y0: y, x1: x + s, y1: y + s, c: eye_corners(shape, s, cc) };
        self.crect(&outer);
        if ring {
            let inner = CRect {
                x0: x + 1.0,
                y0: y + 1.0,
                x1: x + s - 1.0,
                y1: y + s - 1.0,
                c: inner_corners(&outer.c, s - 2.0),
            };
            self.crect_ccw(&inner);
        }
    }

    fn eyes(&mut self, l: &Ctx<'_>, frames: bool) {
        let st = l.style;
        let n = l.n;
        let ex = [0, n - 7, 0];
        let ey = [0, 0, n - 7];
        let cc = [BR, BL, TR];
        let fs = st.eyes.frame;
        let bs = ball_shape(st.eyes.ball);
        for i in 0..3 {
            let x = l.ox + ex[i] as f64;
            let y = l.oy + ey[i] as f64;
            if frames {
                self.eye_part(fs, x, y, 7.0, true, cc[i]);
            } else {
                self.eye_part(bs, x + 2.0, y + 2.0, 3.0, false, cc[i]);
            }
        }
        let afs = match st.alignment.frame {
            Some(f) if l.align_styled => f,
            _ => return,
        };
        let abs = ball_shape(st.alignment.ball.unwrap_or(EyeShape::Square));
        let k = l.align_count;
        for i in 0..k {
            for j in 0..k {
                if !l.is_alignment_center(i, j) {
                    continue;
                }
                let x = l.ox + l.align_pos[j] as f64;
                let y = l.oy + l.align_pos[i] as f64;
                if frames {
                    self.eye_part(afs, x - 2.0, y - 2.0, 5.0, true, BR);
                } else {
                    self.eye_part(abs, x, y, 1.0, false, BR);
                }
            }
        }
    }

    pub(crate) fn layer(&mut self, l: &Ctx<'_>, layer: Layer) {
        match layer {
            Layer::Background => self.crect(&l.canvas_crect(0.0)),
            Layer::Border => {
                if l.border > 0.0 {
                    self.crect(&l.canvas_crect(0.0));
                    self.crect_ccw(&l.canvas_crect(l.border));
                }
            }
            Layer::Modules => self.modules(l),
            Layer::EyeFrames => self.eyes(l, true),
            Layer::EyeBalls => self.eyes(l, false),
            Layer::LogoBackground => {
                if l.has_logo && l.style.logo.as_ref().map_or(0, |g| g.background.a()) != 0 {
                    self.crect(&l.logo_bg_crect());
                }
            }
        }
    }
}

/// Emits the geometry of one layer into `sink` and returns the number of
/// sub-paths. This is the shared shape model used by the SVG writer.
///
/// ```
/// use qrkude::{emit_layer, encode_text, EncodeOptions, Layer, PathCommand, Style};
/// let code = encode_text("hi", &EncodeOptions::default())?;
/// let mut path: Vec<PathCommand> = Vec::new();
/// let n = emit_layer(&code, &Style::default(), Layer::EyeBalls, &mut path);
/// assert_eq!(n, 3);
/// assert_eq!(path[0], PathCommand::MoveTo { x: 6.0, y: 6.0 });
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn emit_layer<S: PathSink + ?Sized>(code: &QrCode, style: &Style, layer: Layer, sink: &mut S) -> usize {
    let l = Ctx::new(code, style);
    let mut e = Emitter::new(sink);
    e.layer(&l, layer);
    e.count
}

/// Whether a layer draws anything visible (SPEC §8.5): its resolved paint has
/// at least one color with alpha > 0 (the solid color or any of the first
/// [`MAX_STOPS`](crate::MAX_STOPS) gradient stops; a gradient without stops
/// or with only transparent stops is not visible), plus `border.width > 0`
/// for the border, a logo for the logo background, and at least one
/// sub-path for the modules. A hint for native renderers; the SVG writer
/// keeps its own rules.
///
/// ```
/// use qrkude::{encode_text, layer_visible, EncodeOptions, Layer, Paint, Style};
/// let code = encode_text("hi", &EncodeOptions::default())?;
/// let mut style = Style::default();
/// assert!(layer_visible(&code, &style, Layer::Modules));
/// assert!(!layer_visible(&code, &style, Layer::Border));
/// style.modules.color = Paint::None;
/// assert!(!layer_visible(&code, &style, Layer::Modules));
/// style.modules.color = Paint::linear(0.0, qrkude::Color::TRANSPARENT, qrkude::Color(0x00000001));
/// assert!(layer_visible(&code, &style, Layer::Modules));
/// style.modules.color = Paint::Linear(Default::default()); // no stops
/// assert!(!layer_visible(&code, &style, Layer::Modules));
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn layer_visible(code: &QrCode, style: &Style, layer: Layer) -> bool {
    let p = resolve_paint(style, layer);
    let painted = match p.get() {
        Paint::Solid(c) => c.a() != 0,
        g => g.stops().iter().any(|s| s.color.a() != 0),
    };
    match layer {
        Layer::Border => painted && style.border.width > 0.0,
        Layer::LogoBackground => painted && style.has_logo(),
        Layer::Modules => painted && layer_count(&Ctx::new(code, style), layer) > 0,
        _ => painted,
    }
}

/// Number of sub-paths of a layer (0 = the layer is empty).
pub(crate) fn layer_count(l: &Ctx<'_>, layer: Layer) -> usize {
    let mut e = Emitter::new(NullSink);
    e.layer(l, layer);
    e.count
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn emitter_normalizes() {
        let mut out: Vec<PathCommand> = Vec::new();
        let mut e = Emitter::new(&mut out);
        e.move_to(0.0, 0.0);
        e.line_to(0.0, 0.0); // zero length: dropped
        e.line_to(1.0, 0.0);
        e.line_to(1.0, 1.0);
        e.line_to(0.0, 0.0); // back to start before close: omitted
        e.close();
        assert_eq!(e.count, 1);
        assert_eq!(
            out,
            [
                PathCommand::MoveTo { x: 0.0, y: 0.0 },
                PathCommand::LineTo { x: 1.0, y: 0.0 },
                PathCommand::LineTo { x: 1.0, y: 1.0 },
                PathCommand::Close
            ]
        );
    }

    #[test]
    fn inner_corner_rules() {
        let outer = eye_corners(EyeShape::Bevel, 7.0, BR);
        let inner = inner_corners(&outer, 5.0);
        assert_eq!(inner[0].kind, CornerKind::Chamfer);
        assert_eq!(inner[0].a, 1.75 - (2.0 - math::sqrt(2.0)));
        let outer = eye_corners(EyeShape::Leaf, 5.0, BR);
        let inner = inner_corners(&outer, 3.0);
        assert_eq!(inner[TL].kind, CornerKind::Round);
        assert_eq!(inner[TR].kind, CornerKind::None);
        assert_eq!(inner[BR].a, 5.0 * 0.36 - 1.0);
    }
}
