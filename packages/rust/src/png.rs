// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! PNG output (feature `png`).

use alloc::vec::Vec;

use crate::encode::QrCode;
use crate::raster::{render_rgba, RasterOptions};
use crate::style::Style;
use crate::Error;

/// Renders the code with the rasterizer and encodes it as an RGBA PNG.
///
/// ```
/// use qrkude::{encode_text, to_png, EncodeOptions, RasterOptions, Style};
/// let code = encode_text("https://kudelink.com", &EncodeOptions::default())?;
/// let style = Style::default();
/// let png = to_png(&code, &style, &RasterOptions::with_width(&code, &style, 256))?;
/// assert_eq!(&png[1..4], b"PNG");
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn to_png(code: &QrCode, style: &Style, opts: &RasterOptions<'_>) -> Result<Vec<u8>, Error> {
    let rgba = render_rgba(code, style, opts)?;
    let mut out = Vec::new();
    {
        let mut encoder = png::Encoder::new(&mut out, opts.width, opts.height);
        encoder.set_color(png::ColorType::Rgba);
        encoder.set_depth(png::BitDepth::Eight);
        let mut writer = encoder.write_header().map_err(|_| Error::InvalidArgument)?;
        writer.write_image_data(&rgba).map_err(|_| Error::InvalidArgument)?;
    }
    Ok(out)
}
