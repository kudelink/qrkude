// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

// The crate documentation is the README when the `json` and `png` features are
// enabled (docs.rs builds with all features), so the README examples run as
// doc tests with `cargo test --all-features`. Other configurations get the
// short page below, which points to it.
#![cfg_attr(all(feature = "json", feature = "png"), doc = include_str!("../README.md"))]
#![cfg_attr(
    not(all(feature = "json", feature = "png")),
    doc = r##"
# qrkude (Rust)

`qrkude` turns text or bytes into a QR code and draws it with a custom look
(round or connected dots, rounded corner squares, gradients, a logo, a frame
with a caption) as SVG, as raw pixels or as PNG. It is the Rust package of
QRKude, a family of libraries that produce identical output from the same
style description.

This is the short page. The full documentation (installation, styling,
output, content helpers, scanning checks, untrusted input, API reference,
platform notes) is the crate's `README.md`, which is shown here when the
`json` and `png` features are enabled, as on docs.rs.

```
use qrkude::{encode_text, to_svg, EncodeOptions, Style, SvgOptions};

// Encode: text -> grid of dark and light squares ("modules").
let code = encode_text("https://example.com", &EncodeOptions::default())?;
// Draw: grid + style -> SVG document.
let svg = to_svg(&code, &Style::default(), &SvgOptions::default());
assert!(svg.starts_with("<svg"));
# Ok::<(), qrkude::Error>(())
```

Features: `std` (default; without it the crate is `no_std` and needs only
`alloc`), `json` (the shared JSON style document) and `png` (PNG output).
"##
)]

#![no_std]
#![forbid(unsafe_code)]
#![warn(missing_docs)]

extern crate alloc;
#[cfg(feature = "std")]
extern crate std;

mod document;
mod encode;
mod error;
mod geometry;
mod layout;
mod lint;
mod math;
pub mod payload;
mod raster;
mod style;
mod svg;

#[cfg(feature = "json")]
mod json;
#[cfg(feature = "png")]
mod png;

pub use document::StyleDocument;
pub use encode::{encode, encode_for_style, encode_text, Ecc, EncodeOptions, Mode, QrCode, MAX_VERSION};
pub use error::Error;
pub use geometry::{
    emit_layer, gradient_geometry, layer_paint, layer_visible, GradientGeometry, Layer, PathCommand,
    PathSink,
};
pub use layout::{layout, Layout};
pub use lint::{lint, lint_bits, logo_damage, LintFlag};
pub use raster::{
    pixel_buffer_size, render_buffer, render_rgb565, render_rgba, render_spans, Image, PixelFormat,
    RasterOptions,
};
pub use style::{
    AlignmentStyle, Background, Border, Color, EyeShape, EyeStyle, Label, LabelPosition,
    LinearGradient, Logo, LogoShape, ModuleShape, ModuleStyle, Paint, RadialGradient, Stop, Style,
    MAX_STOPS,
};
pub use svg::{format_number, sanitize_id_prefix, to_svg, write_svg, SvgOptions};

#[cfg(feature = "png")]
pub use crate::png::to_png;

/// Library version (matches the C reference `QRK_LIB_VERSION`).
pub const LIB_VERSION: &str = "0.1.0";
