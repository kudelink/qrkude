// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! SVG writer (SPEC §8). The output is byte-identical across all ports.

use alloc::string::String;
use core::fmt::{self, Write};

use crate::encode::QrCode;
use crate::geometry::{gradient_geometry_for, layer_count, Emitter, Layer, PathSink};
use crate::layout::Ctx;
use crate::math;
use crate::style::{until_nul, Color, Paint, Style};

/// SVG document options (the `svg` object of a style document).
#[derive(Clone, Debug, PartialEq)]
pub struct SvgOptions {
    /// `width` attribute in px; `height` follows the aspect ratio.
    /// 0 (default) omits both.
    pub width: f64,
    /// Gradient id prefix (empty = `"qrk"`, the default). It is sanitized
    /// before use (see [`sanitize_id_prefix`]), so an untrusted value cannot
    /// inject markup. Gradient ids are document-global in HTML: give each SVG
    /// inlined in the same page a distinct prefix.
    pub id_prefix: String,
}

impl Default for SvgOptions {
    fn default() -> Self {
        SvgOptions { width: 0.0, id_prefix: String::from("qrk") }
    }
}

/// The gradient id prefix actually written (SPEC §8.3): every byte of the
/// UTF-8 encoding outside `[A-Za-z0-9_-]` becomes `_`, a `q` is prepended
/// when it starts with a digit or `-`, and an empty prefix is `qrk`.
///
/// ```
/// use qrkude::sanitize_id_prefix;
/// assert_eq!(sanitize_id_prefix(""), "qrk");
/// assert_eq!(sanitize_id_prefix("1abc"), "q1abc");
/// assert_eq!(sanitize_id_prefix("-x"), "q-x");
/// assert_eq!(sanitize_id_prefix("ñ\"/>"), "_____");
/// ```
pub fn sanitize_id_prefix(prefix: &str) -> String {
    let prefix = until_nul(prefix);
    if prefix.is_empty() {
        return String::from("qrk");
    }
    let bytes = prefix.as_bytes();
    let mut out = String::with_capacity(bytes.len() + 1);
    if bytes[0].is_ascii_digit() || bytes[0] == b'-' {
        out.push('q');
    }
    for &b in bytes {
        out.push(if b.is_ascii_alphanumeric() || b == b'_' || b == b'-' { b as char } else { '_' });
    }
    out
}

/// Stack buffer holding one formatted number.
struct Num {
    buf: [u8; 32],
    len: usize,
}

impl Num {
    fn new(value: f64) -> Num {
        let mut out = Num { buf: [0; 32], len: 0 };
        // NaN, infinities and absurd sizes (only reachable from code-built styles)
        let value = if value > -1e15 && value < 1e15 { value } else { 0.0 };
        let n = math::floor(value * 1000.0 + 0.5) as i64;
        let neg = n < 0;
        let n = n.unsigned_abs();
        let mut ip = n / 1000;
        let fp = (n % 1000) as u32;
        if neg {
            out.push(b'-');
        }
        let mut tmp = [0u8; 24];
        let mut k = 0;
        loop {
            tmp[k] = b'0' + (ip % 10) as u8;
            k += 1;
            ip /= 10;
            if ip == 0 {
                break;
            }
        }
        while k > 0 {
            k -= 1;
            out.push(tmp[k]);
        }
        if fp != 0 {
            out.push(b'.');
            out.push(b'0' + (fp / 100) as u8);
            if fp % 100 != 0 {
                out.push(b'0' + (fp / 10 % 10) as u8);
                if fp % 10 != 0 {
                    out.push(b'0' + (fp % 10) as u8);
                }
            }
        }
        out
    }

    fn push(&mut self, b: u8) {
        self.buf[self.len] = b;
        self.len += 1;
    }

    fn as_str(&self) -> &str {
        // only ASCII digits, '-' and '.' are ever written
        core::str::from_utf8(&self.buf[..self.len]).unwrap_or("0")
    }
}

/// Formats a number the way every QRKude port does (SPEC §8.1): rounded
/// half up to 3 decimals, trailing zeros removed. NaN and values with
/// `|v| >= 1e15` format as `"0"`.
///
/// ```
/// use qrkude::format_number;
/// let f = |v| { let mut s = String::new(); format_number(v, &mut s).unwrap(); s };
/// assert_eq!(f(0.0625), "0.063");
/// assert_eq!(f(-0.0625), "-0.062");
/// assert_eq!(f(-0.0001), "0");
/// assert_eq!(f(12.5), "12.5");
/// assert_eq!(f(f64::NAN), "0");
/// ```
pub fn format_number<W: Write + ?Sized>(value: f64, out: &mut W) -> fmt::Result {
    out.write_str(Num::new(value).as_str())
}

/// Writer state shared with the path sink; remembers the first error.
struct Out<'w, W: Write + ?Sized> {
    w: &'w mut W,
    err: fmt::Result,
    lx: f64,
    ly: f64,
}

impl<W: Write + ?Sized> Out<'_, W> {
    fn s(&mut self, s: &str) {
        if self.err.is_ok() {
            self.err = self.w.write_str(s);
        }
    }

    fn n(&mut self, v: f64) {
        self.s(Num::new(v).as_str());
    }

    /// Escapes text for an attribute or element (SPEC §8.3): the string ends
    /// at its first U+0000 and U+0001-U+001F other than tab, LF and CR are
    /// dropped (XML 1.0 does not allow them).
    fn esc(&mut self, text: &str) {
        let text = until_nul(text);
        let mut start = 0;
        for (i, b) in text.bytes().enumerate() {
            let rep = match b {
                b'&' => "&amp;",
                b'<' => "&lt;",
                b'>' => "&gt;",
                b'"' => "&quot;",
                b'\t' | b'\n' | b'\r' => continue,
                0x01..=0x1F => "",
                _ => continue,
            };
            self.s(&text[start..i]);
            self.s(rep);
            start = i + 1;
        }
        self.s(&text[start..]);
    }

    fn color(&mut self, c: Color) {
        const HEX: &[u8; 16] = b"0123456789abcdef";
        let mut buf = [b'#'; 7];
        for i in 0..6 {
            buf[1 + i] = HEX[((c.0 >> (28 - 4 * i)) & 15) as usize];
        }
        self.s(core::str::from_utf8(&buf).unwrap_or("#000000"));
    }

    fn opacity(&mut self, attr: &str, c: Color) {
        let a = c.a();
        if a != 255 {
            self.s(" ");
            self.s(attr);
            self.s("=\"");
            self.n(a as f64 / 255.0);
            self.s("\"");
        }
    }

    fn fill(&mut self, p: &Paint, prefix: &str, id: &str) {
        match p {
            Paint::Linear(_) | Paint::Radial(_) => {
                self.s(" fill=\"url(#");
                self.s(prefix);
                self.s("-");
                self.s(id);
                self.s(")\"");
            }
            Paint::Solid(c) => {
                self.s(" fill=\"");
                self.color(*c);
                self.s("\"");
                self.opacity("fill-opacity", *c);
            }
            Paint::None => self.s(" fill=\"none\""),
        }
    }

    fn gradient(&mut self, l: &Ctx<'_>, layer: Layer, p: &Paint, prefix: &str, id: &str) {
        let g = gradient_geometry_for(l, layer, p);
        let linear = matches!(p, Paint::Linear(_));
        self.s(if linear { "<linearGradient id=\"" } else { "<radialGradient id=\"" });
        self.s(prefix);
        self.s("-");
        self.s(id);
        self.s("\" gradientUnits=\"userSpaceOnUse\"");
        if linear {
            self.s(" x1=\"");
            self.n(g.x1);
            self.s("\" y1=\"");
            self.n(g.y1);
            self.s("\" x2=\"");
            self.n(g.x2);
            self.s("\" y2=\"");
            self.n(g.y2);
        } else {
            self.s(" cx=\"");
            self.n(g.cx);
            self.s("\" cy=\"");
            self.n(g.cy);
            self.s("\" r=\"");
            self.n(g.r);
        }
        self.s("\">");
        for stop in p.stops() {
            self.s("<stop offset=\"");
            self.n(stop.offset);
            self.s("\" stop-color=\"");
            self.color(stop.color);
            self.s("\"");
            self.opacity("stop-opacity", stop.color);
            self.s("/>");
        }
        self.s(if linear { "</linearGradient>" } else { "</radialGradient>" });
    }

    fn layer_path(&mut self, l: &Ctx<'_>, layer: Layer, paint: &Paint, prefix: &str, id: &str) {
        if layer_count(l, layer) == 0 {
            return;
        }
        self.s("<path d=\"");
        Emitter::new(&mut *self).layer(l, layer);
        self.s("\"");
        self.fill(paint, prefix, id);
        self.s("/>\n");
    }
}

impl<W: Write + ?Sized> PathSink for Out<'_, W> {
    fn move_to(&mut self, x: f64, y: f64) {
        self.s("M");
        self.n(x);
        self.s(" ");
        self.n(y);
        self.lx = x;
        self.ly = y;
    }

    fn line_to(&mut self, x: f64, y: f64) {
        if y == self.ly {
            self.s("H");
            self.n(x);
        } else if x == self.lx {
            self.s("V");
            self.n(y);
        } else {
            self.s("L");
            self.n(x);
            self.s(" ");
            self.n(y);
        }
        self.lx = x;
        self.ly = y;
    }

    fn arc_to(&mut self, r: f64, clockwise: bool, x: f64, y: f64, _cx: f64, _cy: f64) {
        self.s("A");
        self.n(r);
        self.s(" ");
        self.n(r);
        self.s(if clockwise { " 0 0 1 " } else { " 0 0 0 " });
        self.n(x);
        self.s(" ");
        self.n(y);
        self.lx = x;
        self.ly = y;
    }

    fn close(&mut self) {
        self.s("Z");
    }
}

/// Streams the SVG document into any [`core::fmt::Write`] (a `String`, a
/// UART adapter, ...). Nothing is buffered beyond one number.
pub fn write_svg<W: Write + ?Sized>(code: &QrCode, style: &Style, opts: &SvgOptions, out: &mut W) -> fmt::Result {
    let prefix = sanitize_id_prefix(&opts.id_prefix);
    let prefix = prefix.as_str();
    let l = Ctx::new(code, style);
    let mut c = Out { w: out, err: Ok(()), lx: 0.0, ly: 0.0 };

    c.s("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 ");
    c.n(l.w);
    c.s(" ");
    c.n(l.h);
    c.s("\"");
    if opts.width > 0.0 {
        c.s(" width=\"");
        c.n(opts.width);
        c.s("\" height=\"");
        c.n(opts.width * l.h / l.w);
        c.s("\"");
    }
    c.s(">\n");

    let bg = &style.background.color;
    let fg = &style.modules.color;
    let ef = &style.eyes.frame_color;
    let eb = &style.eyes.ball_color;
    if bg.is_gradient() || fg.is_gradient() || ef.is_gradient() || eb.is_gradient() {
        c.s("<defs>");
        if bg.is_gradient() {
            c.gradient(&l, Layer::Background, bg, prefix, "bg");
        }
        if fg.is_gradient() {
            c.gradient(&l, Layer::Modules, fg, prefix, "fg");
        }
        if ef.is_gradient() {
            c.gradient(&l, Layer::EyeFrames, ef, prefix, "ef");
        }
        if eb.is_gradient() {
            c.gradient(&l, Layer::EyeBalls, eb, prefix, "eb");
        }
        c.s("</defs>\n");
    }

    let bg_visible = match bg {
        Paint::Linear(_) | Paint::Radial(_) => true,
        Paint::Solid(color) => color.a() != 0,
        Paint::None => false,
    };
    if bg_visible {
        c.s("<rect width=\"");
        c.n(l.w);
        c.s("\" height=\"");
        c.n(l.h);
        c.s("\"");
        if l.bg_radius > 0.0 {
            c.s(" rx=\"");
            c.n(l.bg_radius);
            c.s("\"");
        }
        c.fill(bg, prefix, "bg");
        c.s("/>\n");
    }

    c.layer_path(&l, Layer::Border, &Paint::Solid(style.border.color), prefix, "");
    c.layer_path(&l, Layer::Modules, fg, prefix, "fg");
    let (efp, efid) = if ef.is_none() { (fg, "fg") } else { (ef, "ef") };
    c.layer_path(&l, Layer::EyeFrames, efp, prefix, efid);
    let (ebp, ebid) = if eb.is_none() { (fg, "fg") } else { (eb, "eb") };
    c.layer_path(&l, Layer::EyeBalls, ebp, prefix, ebid);
    let logo_bg = style.logo.as_ref().map(|g| g.background).unwrap_or_default();
    c.layer_path(&l, Layer::LogoBackground, &Paint::Solid(logo_bg), prefix, "");

    if let Some(href) = style.active_logo().and_then(|g| g.href.as_deref()).map(until_nul).filter(|h| !h.is_empty()) {
        c.s("<image x=\"");
        c.n(l.logo_cx - l.logo_half);
        c.s("\" y=\"");
        c.n(l.logo_cy - l.logo_half);
        c.s("\" width=\"");
        c.n(2.0 * l.logo_half);
        c.s("\" height=\"");
        c.n(2.0 * l.logo_half);
        c.s("\" preserveAspectRatio=\"xMidYMid meet\" xlink:href=\"");
        c.esc(href);
        c.s("\"/>\n");
    }

    if let Some(label) = style.active_label() {
        let f = l.label_size;
        c.s("<text x=\"");
        c.n(l.label_x);
        c.s("\" y=\"");
        c.n(l.label_baseline);
        c.s("\" text-anchor=\"middle\" font-family=\"");
        let font = until_nul(&label.font);
        c.esc(if font.is_empty() { "sans-serif" } else { font });
        c.s("\" font-size=\"");
        c.n(f);
        c.s("\"");
        if label.bold {
            c.s(" font-weight=\"bold\"");
        }
        c.s(" fill=\"");
        c.color(label.color);
        c.s("\"");
        c.opacity("fill-opacity", label.color);
        c.s(">");
        c.esc(&label.text);
        c.s("</text>\n");
    }

    c.s("</svg>\n");
    c.err
}

/// Renders the SVG document into a `String`.
///
/// ```
/// use qrkude::{encode_text, to_svg, EncodeOptions, Style, SvgOptions};
/// let code = encode_text("QRKude", &EncodeOptions::default())?;
/// let svg = to_svg(&code, &Style::default(), &SvgOptions::default());
/// assert!(svg.starts_with("<svg xmlns=\"http://www.w3.org/2000/svg\""));
/// assert!(svg.ends_with("</svg>\n"));
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn to_svg(code: &QrCode, style: &Style, opts: &SvgOptions) -> String {
    let mut s = String::new();
    // writing into a String cannot fail
    let _ = write_svg(code, style, opts, &mut s);
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    fn f(v: f64) -> String {
        let mut s = String::new();
        format_number(v, &mut s).unwrap();
        s
    }

    #[test]
    fn numbers() {
        assert_eq!(f(0.0), "0");
        assert_eq!(f(-0.0), "0");
        assert_eq!(f(1.0), "1");
        assert_eq!(f(0.5), "0.5");
        assert_eq!(f(0.25), "0.25");
        assert_eq!(f(0.0625), "0.063");
        assert_eq!(f(-0.0625), "-0.062");
        assert_eq!(f(-0.0001), "0");
        assert_eq!(f(-1.5), "-1.5");
        assert_eq!(f(1234567.0405), "1234567.041");
        assert_eq!(f(0.1 + 0.2), "0.3");
        assert_eq!(f(2.0 / 3.0), "0.667");
        assert_eq!(f(f64::NAN), "0");
        assert_eq!(f(f64::INFINITY), "0");
        assert_eq!(f(-1e15), "0");
        assert_eq!(f(1e15), "0");
        assert_eq!(f(123_456_789.0625), "123456789.063");
        assert_eq!(f(-999_999_999_999.9), "-999999999999.9");
    }

    #[test]
    fn prefixes() {
        assert_eq!(sanitize_id_prefix("qrk"), "qrk");
        assert_eq!(sanitize_id_prefix("a_B-9"), "a_B-9");
        assert_eq!(sanitize_id_prefix("x\" onload=\"y"), "x__onload__y");
        assert_eq!(sanitize_id_prefix("9"), "q9");
        assert_eq!(sanitize_id_prefix("é"), "__");
        assert_eq!(sanitize_id_prefix("\0x"), "qrk");
        assert_eq!(sanitize_id_prefix("a\0x"), "a");
    }

    #[test]
    fn text_escaping() {
        let mut s = String::new();
        let mut out = Out { w: &mut s, err: Ok(()), lx: 0.0, ly: 0.0 };
        out.esc("a\u{1}b\u{1f}c\td\ne\rf\u{7f}<&>\"\0gone");
        assert_eq!(s, "abc\td\ne\rf\u{7f}&lt;&amp;&gt;&quot;");
    }
}
