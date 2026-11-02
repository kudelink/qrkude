// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Public API behaviour beyond the conformance vectors.

use qrkude::{
    emit_layer, encode_for_style, encode_text, gradient_geometry, layout, render_rgba, render_spans, text_size, to_svg,
    to_text, write_text, Color, Ecc, EncodeOptions, EyeShape, Label, LabelPosition, Layer, Logo, ModuleShape, Paint,
    PathSink, RasterOptions, Style, SvgOptions, TextColors, TextOptions, TextStyle,
};

/// A sink that tracks the bounding box of all end points.
#[derive(Default)]
struct Bounds {
    subpaths: usize,
    open: bool,
    min: (f64, f64),
    max: (f64, f64),
}

impl Bounds {
    fn point(&mut self, x: f64, y: f64) {
        if self.subpaths == 1 && !self.open {
            self.min = (x, y);
            self.max = (x, y);
        }
        self.open = true;
        self.min = (self.min.0.min(x), self.min.1.min(y));
        self.max = (self.max.0.max(x), self.max.1.max(y));
    }
}

impl PathSink for Bounds {
    fn move_to(&mut self, x: f64, y: f64) {
        self.subpaths += 1;
        self.point(x, y);
    }
    fn line_to(&mut self, x: f64, y: f64) {
        self.point(x, y);
    }
    fn arc_to(&mut self, r: f64, _cw: bool, x: f64, y: f64, cx: f64, cy: f64) {
        assert!(r > 0.0);
        // the end point lies on the circle around the center
        assert!(((x - cx).hypot(y - cy) - r).abs() < 1e-9);
        self.point(x, y);
    }
    fn close(&mut self) {}
}

#[test]
fn path_sink_sees_the_svg_geometry() {
    let code = encode_text("https://kudelink.com", &EncodeOptions::default()).unwrap();
    for &shape in ModuleShape::ALL {
        let style = Style::default().with_module_shape(shape);
        let l = layout(&code, &style);
        let mut b = Bounds::default();
        let n = emit_layer(&code, &style, Layer::Modules, &mut b);
        assert_eq!(n, b.subpaths);
        assert!(n > 0, "{shape}");
        let n_f = l.modules as f64;
        assert!(b.min.0 >= l.symbol_x && b.min.1 >= l.symbol_y, "{shape}");
        assert!(b.max.0 <= l.symbol_x + n_f && b.max.1 <= l.symbol_y + n_f, "{shape}");
        // one "M" per sub-path in the SVG modules path
        let svg = to_svg(&code, &style, &SvgOptions::default());
        let modules = svg.lines().nth(2).unwrap();
        assert_eq!(modules.matches('M').count(), n, "{shape}");
    }
}

#[test]
fn layout_with_label_and_logo() {
    let code = encode_text("layout", &EncodeOptions::default()).unwrap();
    let mut style = Style::default();
    style.label = Some(Label { size: 2.0, position: LabelPosition::Top, ..Label::new("TOP") });
    style.logo = Some(Logo { size: 0.5, ..Logo::default() });
    let l = layout(&code, &style);
    assert_eq!((l.width, l.height), (29.0, 32.0));
    assert_eq!((l.symbol_x, l.symbol_y), (4.0, 7.0));
    assert!(l.has_label && l.has_logo);
    assert_eq!((l.label_x, l.label_baseline), (14.5, 2.2));
    assert_eq!((l.logo_x, l.logo_y, l.logo_size), (9.25, 12.25, 10.5));
}

#[test]
fn gradient_geometry_special_angles() {
    let code = encode_text("gradient", &EncodeOptions::default()).unwrap();
    let style = Style::default().with_color(Paint::linear(90.0, Color::BLACK, Color::hex(0x0000ff)));
    let g = gradient_geometry(&code, &style, Layer::Modules);
    assert_eq!((g.x1, g.y1, g.x2, g.y2), (14.5, 4.0, 14.5, 25.0));
    // eye layers inherit the module paint
    assert_eq!(gradient_geometry(&code, &style, Layer::EyeBalls), g);
}

#[test]
fn ecc_default_rule() {
    let style = Style::default().with_logo(Logo::new("logo.svg"));
    let code = encode_for_style(b"https://kudelink.com", &EncodeOptions::default(), &style).unwrap();
    assert_eq!(code.ecc(), Ecc::High);
    let opts = EncodeOptions { ecc: Some(Ecc::Low), boost_ecc: false, ..Default::default() };
    let code = encode_for_style(b"https://kudelink.com", &opts, &style).unwrap();
    assert_eq!(code.ecc(), Ecc::Low);
    let code = encode_for_style(b"https://kudelink.com", &EncodeOptions::default(), &Style::default()).unwrap();
    assert_eq!(code.ecc(), Ecc::Quartile); // M boosted: the version does not grow
}

/// The rasterizer evaluates the same shape model: module centers are dark
/// exactly where the matrix is dark (at 10 px per module, no anti-aliasing).
#[test]
fn raster_matches_matrix() {
    let code = encode_text("https://kudelink.com/raster", &EncodeOptions::default()).unwrap();
    let n = code.size() as i32;
    for &shape in ModuleShape::ALL {
        for &eye in EyeShape::ALL {
            let style = Style::default().with_module_shape(shape).with_eyes(eye, eye);
            let side = (n as u32 + 8) * 10;
            let opts = RasterOptions { supersample: 1, ..RasterOptions::new(side, side) };
            let rgba = render_rgba(&code, &style, &opts).unwrap();
            for y in 0..n {
                for x in 0..n {
                    let o = (((y + 4) * 10 + 5) as usize * side as usize + ((x + 4) * 10 + 5) as usize) * 4;
                    let finder = (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7);
                    // styled eyes clip their corners: check finders with square eyes only
                    if finder && eye != EyeShape::Square {
                        continue;
                    }
                    let dark = rgba[o] < 128;
                    assert_eq!(dark, code.is_dark(x, y), "{shape}/{eye} at {x},{y}");
                }
            }
        }
    }
}

#[test]
fn spans_are_maximal_runs() {
    let code = encode_text("spans", &EncodeOptions::default()).unwrap();
    let opts = RasterOptions::new(87, 87);
    let mut last: Option<(u32, u32, Color)> = None;
    render_spans(&code, &Style::default(), &opts, |x, y, len, c| {
        assert!(len > 0);
        if let Some((ly, end, lc)) = last {
            if ly == y {
                assert_eq!(end, x);
                assert_ne!(lc, c, "adjacent spans must differ");
            }
        }
        last = Some((y, x + len, c));
    })
    .unwrap();
}

const TEXT_STYLES: [TextStyle; 3] = [TextStyle::HalfBlocks, TextStyle::FullBlocks, TextStyle::Hash];

#[test]
fn text_defaults() {
    let want = TextOptions { style: TextStyle::HalfBlocks, margin: 2, invert: false, colors: TextColors::None };
    assert_eq!(TextOptions::default(), want);
    assert_eq!(TextStyle::default(), TextStyle::HalfBlocks);
    assert_eq!(TextColors::default(), TextColors::None);
}

#[test]
fn text_margin_is_clamped_to_16() {
    let code = encode_text("QRKude", &EncodeOptions::default()).unwrap();
    for style in TEXT_STYLES {
        let at = |margin| TextOptions { style, margin, ..TextOptions::default() };
        let want = to_text(&code, &at(16));
        for margin in [17, 255] {
            assert_eq!(to_text(&code, &at(margin)), want, "{style:?} margin {margin}");
            assert_eq!(text_size(&code, &at(margin)), text_size(&code, &at(16)), "{style:?} margin {margin}");
        }
        assert_ne!(to_text(&code, &at(15)), want, "{style:?}");
    }
}

#[test]
fn text_size_versions_1_and_40() {
    let v1 = encode_text("QRKude", &EncodeOptions::default()).unwrap();
    let v40 = encode_text("QRKude", &EncodeOptions { min_version: 40, ..Default::default() }).unwrap();
    assert_eq!((v1.version(), v40.version()), (1, 40));
    let half = TextOptions::default();
    assert_eq!(text_size(&v1, &half), (25, 13));
    assert_eq!(text_size(&v40, &half), (181, 91));
    let hash = TextOptions { style: TextStyle::Hash, ..TextOptions::default() };
    assert_eq!(text_size(&v40, &hash), (362, 181));
    let full = TextOptions { style: TextStyle::FullBlocks, margin: 0, ..TextOptions::default() };
    assert_eq!(text_size(&v40, &full), (354, 177));

    // the output has that size; ANSI codes take no columns
    for style in TEXT_STYLES {
        for colors in [TextColors::None, TextColors::Ansi] {
            let opts = TextOptions { style, colors, ..TextOptions::default() };
            let text = to_text(&v40, &opts);
            let (columns, lines) = text_size(&v40, &opts);
            assert_eq!(text.lines().count(), lines, "{opts:?}");
            for line in text.lines() {
                let line = line.strip_prefix("\x1b[30;107m").and_then(|l| l.strip_suffix("\x1b[0m")).unwrap_or(line);
                assert_eq!(line.chars().count(), columns, "{opts:?}");
            }
        }
    }

    // without a margin, the last half-block line has an empty bottom half (SPEC 14.3)
    let text = to_text(&v40, &TextOptions { margin: 0, ..TextOptions::default() });
    let last = text.lines().last().unwrap();
    assert!(last.chars().all(|c| c == ' ' || c == '\u{2580}'), "{last:?}");
    let inverted = to_text(&v40, &TextOptions { margin: 0, invert: true, ..TextOptions::default() });
    let last = inverted.lines().last().unwrap();
    assert!(last.chars().all(|c| c == '\u{2584}' || c == '\u{2588}'), "{last:?}");
}

/// A writer that fails once it has received `budget` bytes.
struct Failing {
    budget: usize,
}

impl core::fmt::Write for Failing {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        self.budget = self.budget.checked_sub(s.len()).ok_or(core::fmt::Error)?;
        Ok(())
    }
}

#[test]
fn write_text_stops_at_the_first_error() {
    let code = encode_text("QRKude", &EncodeOptions::default()).unwrap();
    let opts = TextOptions::default();
    let len = to_text(&code, &opts).len();
    assert!(write_text(&code, &opts, &mut Failing { budget: len }).is_ok());
    assert!(write_text(&code, &opts, &mut Failing { budget: len - 1 }).is_err());
    // also through a trait object
    let out: &mut dyn core::fmt::Write = &mut Failing { budget: 0 };
    assert!(write_text(&code, &opts, out).is_err());
}
