// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Encodes a URL, styles it and writes `basic.svg` and `basic.png`.
//!
//!     cargo run --example basic --features png -- [OUT_DIR]

use std::path::PathBuf;

use qrkude::{
    encode_for_style, lint, logo_damage, to_png, to_svg, Color, EncodeOptions, EyeShape, ModuleShape, Paint,
    RasterOptions, Style, SvgOptions,
};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let out_dir = PathBuf::from(std::env::args().nth(1).unwrap_or_else(|| ".".into()));
    std::fs::create_dir_all(&out_dir)?;

    let text = "https://kudelink.com";
    let style = Style::default()
        .with_module_shape(ModuleShape::Fluid)
        .with_eyes(EyeShape::Rounded, EyeShape::Circle)
        .with_color(Paint::linear(45.0, Color::hex(0x6a11cb), Color::hex(0x2575fc)));

    // `ecc: None` applies the default rule: H with a logo, else M.
    let code = encode_for_style(text.as_bytes(), &EncodeOptions::default(), &style)?;
    println!(
        "version {} ({}x{}), ecc {}, mask {}, mode {}",
        code.version(),
        code.size(),
        code.size(),
        code.ecc().letter(),
        code.mask(),
        code.mode().name()
    );
    let warnings: Vec<&str> = lint(&code, &style).iter().map(|f| f.name()).collect();
    println!("lint: {:?}, logo damage {}", warnings, logo_damage(&code, &style));

    let svg = to_svg(&code, &style, &SvgOptions { width: 512.0, ..SvgOptions::default() });
    std::fs::write(out_dir.join("basic.svg"), svg)?;

    let opts = RasterOptions { supersample: 3, ..RasterOptions::with_width(&code, &style, 512) };
    std::fs::write(out_dir.join("basic.png"), to_png(&code, &style, &opts)?)?;
    println!("wrote {}", out_dir.join("basic.{svg,png}").display());
    Ok(())
}
