// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Scanline rasterizer (port of `core/src/qrk_raster.c`). It evaluates the
//! shape model per sample with `f32` arithmetic, so it needs no path buffers
//! and streams spans: suitable for microcontrollers and display drivers.
//! Pixels are identical to the C reference rasterizer.

use alloc::vec::Vec;

use crate::encode::QrCode;
use crate::geometry::{
    ball_shape, eye_corners, gradient_geometry_for, inner_corners, layer_paint, CRect, CornerKind,
    Layer, BL, BR, TL, TR,
};
use crate::layout::{layout, Ctx};
use crate::math;
use crate::style::{Color, EyeShape, Paint, Style, MAX_STOPS};
use crate::Error;

/// Pixel layout of raster buffers and logo bitmaps.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PixelFormat {
    /// Bytes R, G, B, A (straight alpha).
    Rgba8888,
    /// Bytes B, G, R, A (LVGL 9 ARGB8888, Cairo on little endian).
    Bgra8888,
    /// Bytes R, G, B.
    Rgb888,
    /// `u16` in native endianness.
    Rgb565,
    /// `u16` byte-swapped (typical for SPI displays).
    Rgb565Swapped,
    /// 8-bit luminance.
    Gray8,
    /// 1 bit per pixel, MSB first, 1 = dark (luminance < 50%).
    Mono,
}

impl PixelFormat {
    /// Bytes of one tightly packed row (saturates at `usize::MAX`).
    pub fn row_bytes(self, width: u32) -> usize {
        self.checked_row_bytes(width).unwrap_or(usize::MAX)
    }

    fn checked_row_bytes(self, width: u32) -> Option<usize> {
        let w = usize::try_from(width).ok()?;
        match self {
            PixelFormat::Rgba8888 | PixelFormat::Bgra8888 => w.checked_mul(4),
            PixelFormat::Rgb888 => w.checked_mul(3),
            PixelFormat::Rgb565 | PixelFormat::Rgb565Swapped => w.checked_mul(2),
            PixelFormat::Gray8 => Some(w),
            PixelFormat::Mono => Some(w / 8 + usize::from(w % 8 != 0)),
        }
    }
}

/// Bytes needed for `height` rows of `stride` bytes (0 = tightly packed),
/// the last row only `row_bytes` long; `None` on overflow or a short stride.
fn buffer_len(format: PixelFormat, width: u32, height: u32, stride: usize) -> Option<usize> {
    let row = format.checked_row_bytes(width)?;
    let stride = if stride == 0 { row } else { stride };
    if stride < row {
        return None;
    }
    match usize::try_from(height).ok()? {
        0 => Some(0),
        h => stride.checked_mul(h - 1)?.checked_add(row),
    }
}

/// Bytes needed for a tightly packed image (saturates at `usize::MAX`).
pub fn pixel_buffer_size(format: PixelFormat, width: u32, height: u32) -> usize {
    buffer_len(format, width, height, 0).unwrap_or(usize::MAX)
}

/// A zeroed vector of `len` elements, without aborting on huge sizes.
fn zeroed<T: Copy + Default>(len: usize) -> Result<Vec<T>, Error> {
    let mut v = Vec::new();
    v.try_reserve_exact(len).map_err(|_| Error::OutOfMemory)?;
    v.resize(len, T::default());
    Ok(v)
}

/// A borrowed bitmap (used for the raster logo).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Image<'a> {
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
    /// Pixel format.
    pub format: PixelFormat,
    /// Pixel data.
    pub pixels: &'a [u8],
    /// Bytes per row (0 = tightly packed).
    pub stride: usize,
}

impl<'a> Image<'a> {
    /// A tightly packed image.
    pub fn new(width: u32, height: u32, format: PixelFormat, pixels: &'a [u8]) -> Image<'a> {
        Image { width, height, format, pixels, stride: 0 }
    }

    /// A tightly packed RGBA8888 image.
    pub fn rgba(width: u32, height: u32, pixels: &'a [u8]) -> Image<'a> {
        Image::new(width, height, PixelFormat::Rgba8888, pixels)
    }

    fn stride_bytes(&self) -> usize {
        if self.stride != 0 {
            self.stride
        } else {
            self.format.row_bytes(self.width)
        }
    }

    fn is_valid(&self) -> bool {
        if self.width == 0 || self.height == 0 {
            return true; // ignored
        }
        matches!(buffer_len(self.format, self.width, self.height, self.stride), Some(n) if self.pixels.len() >= n)
    }

    fn fetch(&self, x: usize, y: usize) -> Color {
        let stride = self.stride_bytes();
        let p = self.pixels;
        match self.format {
            PixelFormat::Rgba8888 => {
                let o = y * stride + x * 4;
                Color::rgba(p[o], p[o + 1], p[o + 2], p[o + 3])
            }
            PixelFormat::Bgra8888 => {
                let o = y * stride + x * 4;
                Color::rgba(p[o + 2], p[o + 1], p[o], p[o + 3])
            }
            PixelFormat::Rgb888 => {
                let o = y * stride + x * 3;
                Color::rgb(p[o], p[o + 1], p[o + 2])
            }
            PixelFormat::Rgb565 | PixelFormat::Rgb565Swapped => {
                let o = y * stride + x * 2;
                let mut v = u16::from_ne_bytes([p[o], p[o + 1]]);
                if self.format == PixelFormat::Rgb565Swapped {
                    v = v.swap_bytes();
                }
                let (r, g, b) = ((v >> 11) & 31, (v >> 5) & 63, v & 31);
                Color::rgb((r as u32 * 255 / 31) as u8, (g as u32 * 255 / 63) as u8, (b as u32 * 255 / 31) as u8)
            }
            PixelFormat::Gray8 => {
                let g = p[y * stride + x];
                Color::rgb(g, g, g)
            }
            PixelFormat::Mono => {
                let dark = (p[y * stride + x / 8] >> (7 - x % 8)) & 1 != 0;
                if dark {
                    Color::BLACK
                } else {
                    Color::WHITE
                }
            }
        }
    }
}

/// Raster output options.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RasterOptions<'a> {
    /// Output width in pixels.
    pub width: u32,
    /// Output height in pixels.
    pub height: u32,
    /// Anti-aliasing samples per axis: 1 = off, max 4 (default 2).
    pub supersample: u32,
    /// Snap modules to whole pixels (default true).
    pub integer_scale: bool,
    /// Color outside the canvas and under transparency (default white).
    pub clear_color: Color,
    /// Optional logo bitmap, drawn in the logo box when the style has a logo.
    pub logo: Option<Image<'a>>,
}

impl RasterOptions<'static> {
    /// Defaults for a `width` x `height` image.
    pub fn new(width: u32, height: u32) -> RasterOptions<'static> {
        RasterOptions {
            width,
            height,
            supersample: 2,
            integer_scale: true,
            clear_color: Color::WHITE,
            logo: None,
        }
    }

    /// Options for an image `width` pixels wide whose height follows the
    /// canvas aspect ratio (taller with a label). The canvas fills the image
    /// exactly (`integer_scale = false`).
    pub fn with_width(code: &QrCode, style: &Style, width: u32) -> RasterOptions<'static> {
        let l = layout(code, style);
        let height = (width as f64 * l.height / l.width + 0.5) as u32;
        RasterOptions { integer_scale: false, ..RasterOptions::new(width, height) }
    }
}

// ---- internal ---------------------------------------------------------------

#[derive(Clone, Copy, Default)]
struct Rgbaf {
    r: f32,
    g: f32,
    b: f32,
    a: f32, // premultiplied, 0..1
}

#[derive(Clone, Copy)]
struct FRect {
    x0: f32,
    y0: f32,
    x1: f32,
    y1: f32,
    kind: [CornerKind; 4],
    a: [f32; 4],
}

const EMPTY_RECT: FRect = FRect { x0: 0.0, y0: 0.0, x1: 0.0, y1: 0.0, kind: [CornerKind::None; 4], a: [0.0; 4] };

impl FRect {
    fn from(c: &CRect) -> FRect {
        let mut f = FRect {
            x0: c.x0 as f32,
            y0: c.y0 as f32,
            x1: c.x1 as f32,
            y1: c.y1 as f32,
            kind: [CornerKind::None; 4],
            a: [0.0; 4],
        };
        for i in 0..4 {
            f.kind[i] = if c.c[i].a > 0.0 { c.c[i].kind } else { CornerKind::None };
            f.a[i] = c.c[i].a as f32;
        }
        f
    }

    fn contains(&self, u: f32, v: f32) -> bool {
        if u < self.x0 || u >= self.x1 || v < self.y0 || v >= self.y1 {
            return false;
        }
        for i in 0..4 {
            if self.kind[i] == CornerKind::None {
                continue;
            }
            let dx = if i == TL || i == BL { u - self.x0 } else { self.x1 - u };
            let dy = if i == TL || i == TR { v - self.y0 } else { self.y1 - v };
            let a = self.a[i];
            if self.kind[i] == CornerKind::Round {
                if dx < a && dy < a {
                    let ex = a - dx;
                    let ey = a - dy;
                    if ex * ex + ey * ey > a * a {
                        return false;
                    }
                }
            } else if dx + dy < a {
                return false;
            }
        }
        true
    }
}

fn eye_rects(shape: EyeShape, x: f64, y: f64, s: f64, cc: usize, ring: bool) -> (FRect, FRect) {
    let outer = CRect { x0: x, y0: y, x1: x + s, y1: y + s, c: eye_corners(shape, s, cc) };
    let inner = if ring {
        FRect::from(&CRect {
            x0: x + 1.0,
            y0: y + 1.0,
            x1: x + s - 1.0,
            y1: y + s - 1.0,
            c: inner_corners(&outer.c, s - 2.0),
        })
    } else {
        EMPTY_RECT
    };
    (FRect::from(&outer), inner)
}

fn over(dst: &mut Rgbaf, c: Color) {
    let a = (c.0 & 0xFF) as f32 / 255.0;
    if a <= 0.0 {
        return;
    }
    let k = 1.0 - a;
    dst.r = (c.0 >> 24) as f32 / 255.0 * a + dst.r * k;
    dst.g = ((c.0 >> 16) & 0xFF) as f32 / 255.0 * a + dst.g * k;
    dst.b = ((c.0 >> 8) & 0xFF) as f32 / 255.0 * a + dst.b * k;
    dst.a = a + dst.a * k;
}

fn lerp_color(a: Color, b: Color, t: f32) -> Color {
    let mut out = 0u32;
    let mut sh = 0;
    while sh < 32 {
        let ca = ((a.0 >> sh) & 0xFF) as f32;
        let cb = ((b.0 >> sh) & 0xFF) as f32;
        let v = (ca + (cb - ca) * t + 0.5) as i32;
        out |= (v.clamp(0, 255) as u32) << sh;
        sh += 8;
    }
    Color(out)
}

#[derive(Clone, Copy, Default)]
struct Grad {
    x1: f32,
    y1: f32,
    dx: f32,
    dy: f32,
    len2: f32,
    cx: f32,
    cy: f32,
    r: f32,
}

#[derive(Clone, Copy)]
enum CellKind {
    Empty,
    Rect(FRect),
    Star,
    Fillets(u8),
}

struct Raster<'a> {
    l: Ctx<'a>,
    paint: [Paint; 6],
    grad: [Grad; 6],
    bg_visible: bool,
    logo_bg: Option<Color>,
    w: f32,
    h: f32,
    ox: f32,
    oy: f32,
    star_h: f32,
    fillet_r: f32,
    border: Option<Color>,
    canvas: FRect,
    border_inner: FRect,
    logo_bg_rect: FRect,
    eye_frame: EyeShape,
    eye_ball: EyeShape,
    align_frame: EyeShape,
    align_ball: EyeShape,
    eye_outer: [FRect; 3],
    eye_inner: [FRect; 3],
    eye_ball_rect: [FRect; 3],
    al_outer: FRect,
    al_inner: FRect,
    al_ball: FRect,
    logo: Option<Image<'a>>,
    logo_x0: f32,
    logo_y0: f32,
    logo_w: f32,
    logo_h: f32,
    logo_scale: f32,
    cache_xy: (i32, i32),
    cache: CellKind,
}

const LAYER_INDEX: [Layer; 6] = Layer::ALL;

impl<'a> Raster<'a> {
    fn new(code: &'a QrCode, style: &'a Style, logo: Option<Image<'a>>) -> Raster<'a> {
        let l = Ctx::new(code, style);
        let paint = LAYER_INDEX.map(|layer| layer_paint(style, layer));
        let mut grad = [Grad::default(); 6];
        for (i, &layer) in LAYER_INDEX.iter().enumerate() {
            let g = gradient_geometry_for(&l, layer, &paint[i]);
            let dx = (g.x2 - g.x1) as f32;
            let dy = (g.y2 - g.y1) as f32;
            grad[i] = Grad {
                x1: g.x1 as f32,
                y1: g.y1 as f32,
                dx,
                dy,
                len2: dx * dx + dy * dy,
                cx: g.cx as f32,
                cy: g.cy as f32,
                r: g.r as f32,
            };
        }
        let bg_visible = match &style.background.color {
            Paint::Linear(_) | Paint::Radial(_) => true,
            Paint::Solid(c) => c.a() != 0,
            Paint::None => false,
        };

        let rad = l.bg_radius;
        let canvas = FRect::from(&CRect::simple(0.0, 0.0, l.w, l.h, CornerKind::Round, rad));
        let b = l.border;
        let ri = if rad - b > 0.0 { rad - b } else { 0.0 };
        let border_inner = FRect::from(&CRect::simple(b, b, l.w - b, l.h - b, CornerKind::Round, ri));

        let logo_color = style.logo.as_ref().map(|g| g.background).unwrap_or_default();
        let logo_bg = if l.has_logo && logo_color.a() != 0 { Some(logo_color) } else { None };
        let logo_bg_rect = if l.has_logo { FRect::from(&l.logo_bg_crect()) } else { EMPTY_RECT };

        let n = l.n;
        let eye_frame = style.eyes.frame;
        let eye_ball = ball_shape(style.eyes.ball);
        let ex = [0, n - 7, 0];
        let ey = [0, 0, n - 7];
        let cc = [BR, BL, TR];
        let mut eye_outer = [EMPTY_RECT; 3];
        let mut eye_inner = [EMPTY_RECT; 3];
        let mut eye_ball_rect = [EMPTY_RECT; 3];
        for i in 0..3 {
            let x = l.ox + ex[i] as f64;
            let y = l.oy + ey[i] as f64;
            let (o, inner) = eye_rects(eye_frame, x, y, 7.0, cc[i], true);
            eye_outer[i] = o;
            eye_inner[i] = inner;
            eye_ball_rect[i] = eye_rects(eye_ball, x + 2.0, y + 2.0, 3.0, cc[i], false).0;
        }
        let mut r = Raster {
            paint,
            grad,
            bg_visible,
            logo_bg,
            w: l.w as f32,
            h: l.h as f32,
            ox: l.ox as f32,
            oy: l.oy as f32,
            star_h: (l.size / 2.0) as f32,
            fillet_r: (l.roundness / 2.0) as f32,
            border: if l.border > 0.0 { Some(style.border.color) } else { None },
            canvas,
            border_inner,
            logo_bg_rect,
            eye_frame,
            eye_ball,
            align_frame: EyeShape::Square,
            align_ball: EyeShape::Square,
            eye_outer,
            eye_inner,
            eye_ball_rect,
            al_outer: EMPTY_RECT,
            al_inner: EMPTY_RECT,
            al_ball: EMPTY_RECT,
            logo: None,
            logo_x0: 0.0,
            logo_y0: 0.0,
            logo_w: 0.0,
            logo_h: 0.0,
            logo_scale: 0.0,
            cache_xy: (-1000, -1000),
            cache: CellKind::Empty,
            l,
        };
        if let Some(frame) = style.alignment.frame.filter(|_| r.l.align_styled) {
            r.align_frame = frame;
            r.align_ball = ball_shape(style.alignment.ball.unwrap_or(EyeShape::Square));
            let (o, inner) = eye_rects(frame, 0.0, 0.0, 5.0, BR, true);
            r.al_outer = o;
            r.al_inner = inner;
            r.al_ball = eye_rects(r.align_ball, 2.0, 2.0, 1.0, BR, false).0;
        }
        if let Some(img) = logo.filter(|i| r.l.has_logo && i.width > 0 && i.height > 0) {
            let bx = (2.0 * r.l.logo_half) as f32;
            let big = img.width.max(img.height);
            r.logo_scale = bx / big as f32;
            r.logo_w = img.width as f32 * r.logo_scale;
            r.logo_h = img.height as f32 * r.logo_scale;
            r.logo_x0 = (r.l.logo_cx - r.l.logo_half) as f32 + (bx - r.logo_w) / 2.0;
            r.logo_y0 = (r.l.logo_cy - r.l.logo_half) as f32 + (bx - r.logo_h) / 2.0;
            r.logo = Some(img);
        }
        r
    }

    fn paint_at(&self, layer: usize, u: f32, v: f32) -> Color {
        let p = &self.paint[layer];
        let g = &self.grad[layer];
        let t = match p {
            Paint::Solid(c) => return *c,
            Paint::None => return Color::TRANSPARENT,
            Paint::Linear(_) => {
                if g.len2 > 0.0 {
                    ((u - g.x1) * g.dx + (v - g.y1) * g.dy) / g.len2
                } else {
                    0.0
                }
            }
            Paint::Radial(_) => {
                let dx = u - g.cx;
                let dy = v - g.cy;
                if g.r > 0.0 {
                    math::sqrtf(dx * dx + dy * dy) / g.r
                } else {
                    0.0
                }
            }
        };
        let stops = p.stops();
        let n = stops.len().min(MAX_STOPS);
        if n == 0 {
            return Color::TRANSPARENT;
        }
        if t <= stops[0].offset as f32 {
            return stops[0].color;
        }
        for i in 0..n - 1 {
            let o0 = stops[i].offset as f32;
            let o1 = stops[i + 1].offset as f32;
            if t <= o1 {
                return if o1 > o0 {
                    lerp_color(stops[i].color, stops[i + 1].color, (t - o0) / (o1 - o0))
                } else {
                    stops[i + 1].color
                };
            }
        }
        stops[n - 1].color
    }

    fn cell(&mut self, x: i32, y: i32) -> CellKind {
        if self.cache_xy == (x, y) {
            return self.cache;
        }
        let kind = if self.l.is_data_dark(x, y) {
            match self.l.module_crect(x, y) {
                Some(r) => CellKind::Rect(FRect::from(&r)),
                None => CellKind::Star,
            }
        } else if self.l.is_data_cell(x, y) {
            match self.l.fillet_mask(x, y) {
                0 => CellKind::Empty,
                m => CellKind::Fillets(m),
            }
        } else {
            CellKind::Empty
        };
        self.cache_xy = (x, y);
        self.cache = kind;
        kind
    }

    /// 0 = nothing, 1 = frame, 2 = ball
    fn eye_hit(&self, x: i32, y: i32, u: f32, v: f32) -> u8 {
        let n = self.l.n;
        let (idx, lx, ly) = if x < 7 && y < 7 {
            (0, x, y)
        } else if x >= n - 7 && y < 7 {
            (1, x - (n - 7), y)
        } else if x < 7 && y >= n - 7 {
            (2, x, y - (n - 7))
        } else {
            (usize::MAX, 0, 0)
        };

        let fx = u - self.ox - x as f32; // cell local
        let fy = v - self.oy - y as f32;
        if idx != usize::MAX {
            let ring = lx == 0 || ly == 0 || lx == 6 || ly == 6;
            let ball = (2..=4).contains(&lx) && (2..=4).contains(&ly);
            if self.eye_frame == EyeShape::Dots {
                if ring && in_dot_cell(fx, fy) {
                    return 1;
                }
            } else if self.eye_outer[idx].contains(u, v) && !self.eye_inner[idx].contains(u, v) {
                return 1;
            }
            if self.eye_ball == EyeShape::Dots {
                if ball && in_dot_cell(fx, fy) {
                    return 2;
                }
            } else if self.eye_ball_rect[idx].contains(u, v) {
                return 2;
            }
            return 0;
        }

        if !self.l.align_styled {
            return 0;
        }
        let k = self.l.align_count;
        for i in 0..k {
            let dy = y - self.l.align_pos[i] as i32;
            if !(-2..=2).contains(&dy) {
                continue;
            }
            for j in 0..k {
                let dx = x - self.l.align_pos[j] as i32;
                if !(-2..=2).contains(&dx) || (i == 0 && j == 0) || (i == 0 && j == k - 1) || (i == k - 1 && j == 0) {
                    continue;
                }
                let bu = u - self.ox - (self.l.align_pos[j] as i32 - 2) as f32;
                let bv = v - self.oy - (self.l.align_pos[i] as i32 - 2) as f32;
                let ring = dx == -2 || dx == 2 || dy == -2 || dy == 2;
                if self.align_frame == EyeShape::Dots {
                    if ring && in_dot_cell(fx, fy) {
                        return 1;
                    }
                } else if self.al_outer.contains(bu, bv) && !self.al_inner.contains(bu, bv) {
                    return 1;
                }
                if self.align_ball == EyeShape::Dots {
                    if dx == 0 && dy == 0 && in_dot_cell(fx, fy) {
                        return 2;
                    }
                } else if self.al_ball.contains(bu, bv) {
                    return 2;
                }
                return 0;
            }
        }
        0
    }

    fn sample(&mut self, u: f32, v: f32) -> Rgbaf {
        let mut out = Rgbaf::default();
        if u < 0.0 || v < 0.0 || u >= self.w || v >= self.h {
            return out;
        }
        if !self.canvas.contains(u, v) {
            return out;
        }
        if self.bg_visible {
            over(&mut out, self.paint_at(0, u, v));
        }
        if let Some(c) = self.border {
            if !self.border_inner.contains(u, v) {
                over(&mut out, c);
            }
        }

        let su = u - self.ox;
        let sv = v - self.oy;
        let n = self.l.n;
        if su >= 0.0 && sv >= 0.0 && su < n as f32 && sv < n as f32 {
            let x = su as i32;
            let y = sv as i32;
            let lx = su - x as f32;
            let ly = sv - y as f32;
            let hit = match self.cell(x, y) {
                CellKind::Empty => false,
                CellKind::Rect(r) => r.contains(lx, ly),
                CellKind::Star => {
                    let h = self.star_h;
                    let ax = math::fabsf(lx - 0.5);
                    let ay = math::fabsf(ly - 0.5);
                    if ax <= h && ay <= h {
                        let ex = 1.5 * h - ax;
                        let ey = 1.5 * h - ay;
                        ex * ex + ey * ey > 2.5 * h * h
                    } else {
                        false
                    }
                }
                CellKind::Fillets(m) => (0..4).any(|k| m & (1 << k) != 0 && in_fillet(k, lx, ly, self.fillet_r)),
            };
            if hit {
                over(&mut out, self.paint_at(2, u, v));
            }
            match self.eye_hit(x, y, u, v) {
                1 => over(&mut out, self.paint_at(3, u, v)),
                2 => over(&mut out, self.paint_at(4, u, v)),
                _ => {}
            }
        }

        if let Some(c) = self.logo_bg {
            if self.logo_bg_rect.contains(u, v) {
                over(&mut out, c);
            }
        }
        if let Some(img) = &self.logo {
            if u >= self.logo_x0 && v >= self.logo_y0 && u < self.logo_x0 + self.logo_w && v < self.logo_y0 + self.logo_h {
                let mut ix = ((u - self.logo_x0) / self.logo_scale) as i32;
                let mut iy = ((v - self.logo_y0) / self.logo_scale) as i32;
                if ix >= img.width as i32 {
                    ix = img.width as i32 - 1;
                }
                if iy >= img.height as i32 {
                    iy = img.height as i32 - 1;
                }
                over(&mut out, img.fetch(ix.max(0) as usize, iy.max(0) as usize));
            }
        }
        out
    }
}

fn in_fillet(corner: usize, lx: f32, ly: f32, r: f32) -> bool {
    let dx = if corner == TL || corner == BL { lx } else { 1.0 - lx };
    let dy = if corner == TL || corner == TR { ly } else { 1.0 - ly };
    if dx >= r || dy >= r {
        return false;
    }
    let ex = r - dx;
    let ey = r - dy;
    ex * ex + ey * ey > r * r
}

fn in_dot_cell(lx: f32, ly: f32) -> bool {
    let dx = lx - 0.5;
    let dy = ly - 0.5;
    dx * dx + dy * dy <= 0.25
}

fn to_byte(v: f32) -> u8 {
    ((v * 255.0 + 0.5) as i32).clamp(0, 255) as u8
}

/// Renders the code as horizontal spans of equally colored pixels, row by
/// row, top to bottom: `span(x, y, len, color)`. Colors are already
/// composited over `opts.clear_color`. Labels are not drawn.
///
/// ```
/// use qrkude::{encode_text, render_spans, EncodeOptions, RasterOptions, Style};
/// let code = encode_text("hi", &EncodeOptions::default())?;
/// let mut pixels = 0;
/// render_spans(&code, &Style::default(), &RasterOptions::new(58, 58), |_x, _y, len, _color| {
///     pixels += len;
/// })?;
/// assert_eq!(pixels, 58 * 58);
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn render_spans<F>(code: &QrCode, style: &Style, opts: &RasterOptions<'_>, mut span: F) -> Result<(), Error>
where
    F: FnMut(u32, u32, u32, Color),
{
    if opts.width == 0 || opts.height == 0 || opts.width > i32::MAX as u32 || opts.height > i32::MAX as u32 {
        return Err(Error::InvalidArgument);
    }
    if let Some(img) = &opts.logo {
        if !img.is_valid() {
            return Err(Error::InvalidArgument);
        }
    }
    let mut r = Raster::new(code, style, opts.logo);

    let fw = opts.width as f32;
    let fh = opts.height as f32;
    let mut scale = fw / r.w;
    if fh / r.h < scale {
        scale = fh / r.h;
    }
    if opts.integer_scale && scale >= 1.0 {
        scale = math::floorf(scale);
    }
    let mut offx = (fw - r.w * scale) / 2.0;
    let mut offy = (fh - r.h * scale) / 2.0;
    if opts.integer_scale {
        offx = math::floorf(offx);
        offy = math::floorf(offy);
    }
    let ss = opts.supersample.clamp(1, 4);
    let ssf = ss as f32;
    let inv = 1.0 / (ss * ss) as f32;

    let cc = opts.clear_color;
    let ca = (cc.0 & 0xFF) as f32 / 255.0;
    let clear = Rgbaf {
        r: (cc.0 >> 24) as f32 / 255.0 * ca,
        g: ((cc.0 >> 16) & 0xFF) as f32 / 255.0 * ca,
        b: ((cc.0 >> 8) & 0xFF) as f32 / 255.0 * ca,
        a: ca,
    };

    for py in 0..opts.height {
        let mut run_x = 0u32;
        let mut run_color = Color::TRANSPARENT;
        for px in 0..opts.width {
            let mut acc = Rgbaf::default();
            for j in 0..ss {
                let v = (py as f32 + (j as f32 + 0.5) / ssf - offy) / scale;
                for i in 0..ss {
                    let u = (px as f32 + (i as f32 + 0.5) / ssf - offx) / scale;
                    let s = r.sample(u, v);
                    acc.r += s.r;
                    acc.g += s.g;
                    acc.b += s.b;
                    acc.a += s.a;
                }
            }
            let k = 1.0 - acc.a * inv;
            let red = acc.r * inv + clear.r * k;
            let green = acc.g * inv + clear.g * k;
            let blue = acc.b * inv + clear.b * k;
            let alpha = acc.a * inv + clear.a * k;
            let color = if alpha > 0.0 {
                Color::rgba(to_byte(red / alpha), to_byte(green / alpha), to_byte(blue / alpha), to_byte(alpha))
            } else {
                Color::TRANSPARENT
            };
            if px == 0 {
                run_color = color;
            } else if color != run_color {
                span(run_x, py, px - run_x, run_color);
                run_x = px;
                run_color = color;
            }
        }
        span(run_x, py, opts.width - run_x, run_color);
    }
    Ok(())
}

/// Renders into a caller-provided pixel buffer (`stride` 0 = tightly packed).
pub fn render_buffer(
    code: &QrCode,
    style: &Style,
    opts: &RasterOptions<'_>,
    format: PixelFormat,
    pixels: &mut [u8],
    stride: usize,
) -> Result<(), Error> {
    let needed = buffer_len(format, opts.width, opts.height, stride).ok_or(Error::InvalidArgument)?;
    if pixels.len() < needed {
        return Err(Error::BufferTooSmall);
    }
    let stride = if stride == 0 { format.row_bytes(opts.width) } else { stride };
    render_spans(code, style, opts, |x, y, len, c| {
        let base = y as usize * stride;
        let (r, g, b, a) = (c.r(), c.g(), c.b(), c.a());
        let mut v565 = ((r as u16 >> 3) << 11) | ((g as u16 >> 2) << 5) | (b as u16 >> 3);
        if format == PixelFormat::Rgb565Swapped {
            v565 = v565.swap_bytes();
        }
        let gray = ((r as u32 * 299 + g as u32 * 587 + b as u32 * 114 + 500) / 1000) as u8;
        for i in x as usize..(x + len) as usize {
            match format {
                PixelFormat::Rgba8888 => pixels[base + i * 4..base + i * 4 + 4].copy_from_slice(&[r, g, b, a]),
                PixelFormat::Bgra8888 => pixels[base + i * 4..base + i * 4 + 4].copy_from_slice(&[b, g, r, a]),
                PixelFormat::Rgb888 => pixels[base + i * 3..base + i * 3 + 3].copy_from_slice(&[r, g, b]),
                PixelFormat::Rgb565 | PixelFormat::Rgb565Swapped => {
                    pixels[base + i * 2..base + i * 2 + 2].copy_from_slice(&v565.to_ne_bytes())
                }
                PixelFormat::Gray8 => pixels[base + i] = gray,
                PixelFormat::Mono => {
                    let bit = 0x80u8 >> (i % 8);
                    if gray < 128 {
                        pixels[base + i / 8] |= bit;
                    } else {
                        pixels[base + i / 8] &= !bit;
                    }
                }
            }
        }
    })
}

/// Renders a tightly packed RGBA8888 image (straight alpha).
///
/// ```
/// use qrkude::{encode_text, render_rgba, EncodeOptions, RasterOptions, Style};
/// let code = encode_text("hi", &EncodeOptions::default())?;
/// let rgba = render_rgba(&code, &Style::default(), &RasterOptions::new(116, 116))?;
/// assert_eq!(rgba.len(), 116 * 116 * 4);
/// assert_eq!(&rgba[..4], &[255, 255, 255, 255]); // quiet zone
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn render_rgba(code: &QrCode, style: &Style, opts: &RasterOptions<'_>) -> Result<Vec<u8>, Error> {
    let len = buffer_len(PixelFormat::Rgba8888, opts.width, opts.height, 0).ok_or(Error::InvalidArgument)?;
    let mut buf = zeroed::<u8>(len)?;
    render_buffer(code, style, opts, PixelFormat::Rgba8888, &mut buf, 0)?;
    Ok(buf)
}

/// Renders an RGB565 image as native `u16` values (alpha is composited over
/// `opts.clear_color`). Use [`u16::swap_bytes`] or
/// [`PixelFormat::Rgb565Swapped`] with [`render_buffer`] for SPI displays.
pub fn render_rgb565(code: &QrCode, style: &Style, opts: &RasterOptions<'_>) -> Result<Vec<u16>, Error> {
    let w = opts.width as usize;
    let len = w.checked_mul(opts.height as usize).ok_or(Error::InvalidArgument)?;
    let mut buf = zeroed::<u16>(len)?;
    render_spans(code, style, opts, |x, y, len, c| {
        let v = ((c.r() as u16 >> 3) << 11) | ((c.g() as u16 >> 2) << 5) | (c.b() as u16 >> 3);
        let start = y as usize * w + x as usize;
        buf[start..start + len as usize].fill(v);
    })?;
    Ok(buf)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::encode::{encode_text, EncodeOptions};
    use alloc::vec;

    #[test]
    fn absurd_sizes_are_errors() {
        let code = encode_text("big", &EncodeOptions::default()).unwrap();
        let style = Style::default();
        let huge = RasterOptions::new(u32::MAX, u32::MAX);
        assert!(render_rgba(&code, &style, &huge).is_err());
        assert!(render_rgb565(&code, &style, &huge).is_err());
        let mut small = [0u8; 16];
        assert!(render_buffer(&code, &style, &huge, PixelFormat::Rgba8888, &mut small, 0).is_err());
        assert_eq!(pixel_buffer_size(PixelFormat::Rgba8888, u32::MAX, u32::MAX), usize::MAX);
        assert_eq!(pixel_buffer_size(PixelFormat::Mono, 9, 2), 4);
        // a logo whose pixels do not cover its declared size is rejected
        let mut logo_style = style.clone();
        logo_style.logo = Some(crate::style::Logo::default());
        let short = [0u8; 1];
        let opts = RasterOptions { logo: Some(Image::rgba(64, 64, &short)), ..RasterOptions::new(64, 64) };
        assert_eq!(render_rgba(&code, &logo_style, &opts), Err(Error::InvalidArgument));
        let opts = RasterOptions {
            logo: Some(Image { stride: usize::MAX, ..Image::rgba(64, 64, &short) }),
            ..RasterOptions::new(64, 64)
        };
        assert_eq!(render_rgba(&code, &logo_style, &opts), Err(Error::InvalidArgument));
    }

    #[test]
    fn non_finite_styles_render() {
        // NaN clamps to the lower bound, absurd lengths to 1000 modules
        let code = encode_text("nan", &EncodeOptions::default()).unwrap();
        let mut style = Style::default();
        style.margin = f64::NAN;
        style.border.width = f64::INFINITY;
        style.modules.size = f64::NAN;
        style.background.radius = f64::NAN;
        let l = layout(&code, &style);
        assert_eq!((l.width, l.symbol_x), (21.0 + 2000.0, 1000.0)); // margin 0 + border 1000
        let rgba = render_rgba(&code, &style, &RasterOptions::new(64, 64)).unwrap();
        assert_eq!(rgba.len(), 64 * 64 * 4);
        let svg = crate::svg::to_svg(&code, &style, &crate::svg::SvgOptions { width: f64::NAN, ..Default::default() });
        assert!(svg.contains("viewBox=\"0 0 2021 2021\">"));
        assert!(!svg.contains("NaN") && !svg.contains("inf"));
    }

    #[test]
    fn spans_cover_rows() {
        let code = encode_text("raster", &EncodeOptions::default()).unwrap();
        let style = Style::default();
        let opts = RasterOptions::new(100, 80);
        let mut rows = vec![0u32; 80];
        render_spans(&code, &style, &opts, |x, y, len, _| {
            assert_eq!(x, rows[y as usize]);
            rows[y as usize] += len;
        })
        .unwrap();
        assert!(rows.iter().all(|&r| r == 100));
    }

    #[test]
    fn integer_scale_pixels() {
        // 21 + 8 = 29 modules, 2 px per module: crisp black/white pixels
        let code = encode_text("hello", &EncodeOptions::default()).unwrap();
        let opts = RasterOptions { supersample: 1, ..RasterOptions::new(58, 58) };
        let rgba = render_rgba(&code, &Style::default(), &opts).unwrap();
        for y in 0..29 {
            for x in 0..29 {
                let dark = code.is_dark(x - 4, y - 4);
                let o = ((y * 2) as usize * 58 + (x * 2) as usize) * 4;
                assert_eq!(rgba[o], if dark { 0 } else { 255 }, "module {x},{y}");
            }
        }
    }

    #[test]
    fn formats_agree() {
        let code = encode_text("formats", &EncodeOptions::default()).unwrap();
        let style = Style::default();
        let opts = RasterOptions::new(66, 66);
        let rgba = render_rgba(&code, &style, &opts).unwrap();
        let rgb565 = render_rgb565(&code, &style, &opts).unwrap();
        let mut mono = vec![0u8; pixel_buffer_size(PixelFormat::Mono, 66, 66)];
        render_buffer(&code, &style, &opts, PixelFormat::Mono, &mut mono, 0).unwrap();
        for i in 0..66 * 66 {
            let black = rgba[i * 4] == 0;
            assert_eq!(rgb565[i] == 0, black);
            let (x, y) = (i % 66, i / 66);
            assert_eq!((mono[y * 9 + x / 8] >> (7 - x % 8)) & 1 == 1, black);
        }
        let mut small = vec![0u8; 10];
        assert_eq!(
            render_buffer(&code, &style, &opts, PixelFormat::Gray8, &mut small, 0),
            Err(Error::BufferTooSmall)
        );
        assert_eq!(render_rgba(&code, &style, &RasterOptions::new(0, 10)), Err(Error::InvalidArgument));
    }

    #[test]
    fn logo_bitmap() {
        let code = encode_text("logo", &EncodeOptions::default()).unwrap();
        let mut style = Style::default();
        style.logo = Some(crate::style::Logo::default());
        let red = [255u8, 0, 0, 255].repeat(4);
        let opts = RasterOptions { logo: Some(Image::rgba(2, 2, &red)), ..RasterOptions::new(290, 290) };
        let rgba = render_rgba(&code, &style, &opts).unwrap();
        let c = (145 * 290 + 145) * 4;
        assert_eq!(&rgba[c..c + 4], &[255, 0, 0, 255]);
        let bad = RasterOptions { logo: Some(Image::rgba(4, 4, &red)), ..RasterOptions::new(10, 10) };
        assert_eq!(render_rgba(&code, &style, &bad), Err(Error::InvalidArgument));
    }
}
