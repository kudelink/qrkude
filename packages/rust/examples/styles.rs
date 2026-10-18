// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Writes a gallery of styles (SVG + PNG for each, plus `index.html`).
//!
//!     cargo run --example styles --features png -- [OUT_DIR]

use std::fmt::Write as _;
use std::path::PathBuf;

use qrkude::{
    encode_for_style, lint, logo_damage, to_png, to_svg, AlignmentStyle, Background, Border, Color, EncodeOptions, EyeShape,
    Image, Label, Logo, LogoShape, ModuleShape, Paint, RadialGradient, RasterOptions, Stop, Style, SvgOptions,
};

const URL: &str = "https://kudelink.com";

/// A 64x64 RGBA logo: red disc with a white square.
fn logo_rgba() -> Vec<u8> {
    let mut px = vec![0u8; 64 * 64 * 4];
    for y in 0..64 {
        for x in 0..64 {
            let (dx, dy) = (x as f32 - 31.5, y as f32 - 31.5);
            let p = &mut px[(y * 64 + x) * 4..(y * 64 + x) * 4 + 4];
            if (19..45).contains(&x) && (19..45).contains(&y) {
                p.copy_from_slice(&[255, 255, 255, 255]);
            } else if dx * dx + dy * dy <= 30.0 * 30.0 {
                p.copy_from_slice(&[230, 57, 70, 255]);
            }
        }
    }
    px
}

fn png_bytes(rgba: &[u8], w: u32, h: u32) -> Vec<u8> {
    let mut out = Vec::new();
    {
        let mut enc = png::Encoder::new(&mut out, w, h);
        enc.set_color(png::ColorType::Rgba);
        enc.set_depth(png::BitDepth::Eight);
        enc.write_header().unwrap().write_image_data(rgba).unwrap();
    }
    out
}

fn base64(data: &[u8]) -> String {
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut s = String::new();
    for chunk in data.chunks(3) {
        let b = [chunk[0], *chunk.get(1).unwrap_or(&0), *chunk.get(2).unwrap_or(&0)];
        let n = (b[0] as u32) << 16 | (b[1] as u32) << 8 | b[2] as u32;
        for i in 0..4 {
            if i <= chunk.len() {
                s.push(T[(n >> (18 - 6 * i) & 63) as usize] as char);
            } else {
                s.push('=');
            }
        }
    }
    s
}

fn gallery(logo_uri: &str) -> Vec<(&'static str, &'static str, Style)> {
    let navy = Color::hex(0x1d3557);
    let mut out = vec![
        ("classic", URL, Style::default()),
        (
            "dots",
            URL,
            Style::default().with_module_shape(ModuleShape::Dot).with_eyes(EyeShape::Circle, EyeShape::Circle),
        ),
        (
            "fluid-gradient",
            URL,
            Style::default()
                .with_module_shape(ModuleShape::Fluid)
                .with_eyes(EyeShape::Rounded, EyeShape::Circle)
                .with_color(Paint::linear(45.0, Color::hex(0x6a11cb), Color::hex(0x2575fc))),
        ),
        (
            "liquid",
            URL,
            Style::default().with_module_shape(ModuleShape::Liquid).with_eyes(EyeShape::Circle, EyeShape::Circle),
        ),
        ("bevel", URL, Style::default().with_module_shape(ModuleShape::Bevel).with_eyes(EyeShape::Bevel, EyeShape::Bevel)),
        ("classy", URL, Style::default().with_module_shape(ModuleShape::Classy).with_eyes(EyeShape::Leaf, EyeShape::Leaf)),
        ("star", URL, Style::default().with_module_shape(ModuleShape::Star).with_eyes(EyeShape::Circle, EyeShape::Circle)),
        ("diamond", URL, Style::default().with_module_shape(ModuleShape::Diamond).with_eyes(EyeShape::Bevel, EyeShape::Diamond)),
        ("hbars", URL, Style::default().with_module_shape(ModuleShape::HBars).with_eyes(EyeShape::Rounded, EyeShape::Rounded)),
        ("vbars", URL, Style::default().with_module_shape(ModuleShape::VBars).with_eyes(EyeShape::Drop, EyeShape::Drop)),
        (
            "eyes-dots",
            URL,
            Style::default().with_module_shape(ModuleShape::Dot).with_eyes(EyeShape::Dots, EyeShape::Dots),
        ),
        (
            "radial",
            URL,
            Style::default()
                .with_module_shape(ModuleShape::Dot)
                .with_eyes(EyeShape::Circle, EyeShape::Circle)
                .with_color(Paint::Radial(RadialGradient {
                    stops: vec![
                        Stop { offset: 0.0, color: Color::hex(0x0b3d91) },
                        Stop { offset: 1.0, color: Color::hex(0x00796b) },
                    ],
                    ..RadialGradient::default()
                })),
        ),
        (
            "dark",
            "https://kudelink.com/dark",
            Style {
                background: Background { color: Paint::Solid(navy), radius: 4.0 },
                ..Style::default().with_module_shape(ModuleShape::Liquid).with_color(Color::hex(0xf1faee))
            },
        ),
    ];

    let mut aligned = Style::default().with_module_shape(ModuleShape::Dot).with_eyes(EyeShape::Circle, EyeShape::Circle);
    aligned.alignment = AlignmentStyle { frame: Some(EyeShape::Circle), ball: Some(EyeShape::Circle) };
    out.push(("alignment", "https://kudelink.com/alignment/patterns/styled/as/eyes/0123456789", aligned));

    let mut framed = Style::default().with_module_shape(ModuleShape::Liquid);
    framed.margin = 2.0;
    framed.background = Background { color: Paint::Solid(Color::hex(0xfff8e7)), radius: 3.0 };
    framed.border = Border { width: 1.0, color: navy };
    framed.label = Some(Label { color: navy, ..Label::new("SCAN ME") });
    out.push(("frame-label", URL, framed));

    let mut logo = Style::default().with_module_shape(ModuleShape::Fluid).with_eyes(EyeShape::Rounded, EyeShape::Rounded);
    logo.logo = Some(Logo { size: 0.2, shape: LogoShape::Circle, background: Color::WHITE, ..Logo::new(logo_uri) });
    out.push(("logo", "https://kudelink.com/logo", logo));
    out
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let out_dir = PathBuf::from(std::env::args().nth(1).unwrap_or_else(|| "qrkude-gallery".into()));
    std::fs::create_dir_all(&out_dir)?;

    let logo_px = logo_rgba();
    let logo_uri = format!("data:image/png;base64,{}", base64(&png_bytes(&logo_px, 64, 64)));

    let mut html = String::from(
        "<!doctype html>\n<meta charset=\"utf-8\">\n<title>QRKude gallery</title>\n\
         <style>body{font-family:sans-serif;display:flex;flex-wrap:wrap;gap:16px}\
         figure{margin:0;width:220px}img{width:220px}</style>\n",
    );
    for (name, text, style) in gallery(&logo_uri) {
        let code = encode_for_style(text.as_bytes(), &EncodeOptions::default(), &style)?;
        std::fs::write(out_dir.join(format!("{name}.svg")), to_svg(&code, &style, &SvgOptions::default()))?;

        let opts = RasterOptions {
            supersample: 3,
            logo: Some(Image::rgba(64, 64, &logo_px)),
            ..RasterOptions::with_width(&code, &style, 400)
        };
        std::fs::write(out_dir.join(format!("{name}.png")), to_png(&code, &style, &opts)?)?;

        let flags: Vec<&str> = lint(&code, &style).iter().map(|f| f.name()).collect();
        let damage = logo_damage(&code, &style);
        println!("{name:14} v{:<2} {} damage {damage:.2} lint {flags:?}", code.version(), code.ecc().letter());
        writeln!(html, "<figure><img src=\"{name}.svg\"><figcaption>{name}</figcaption></figure>")?;
    }
    std::fs::write(out_dir.join("index.html"), html)?;
    println!("wrote {}", out_dir.display());
    Ok(())
}
