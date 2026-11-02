// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Text output for terminals and serial monitors (SPEC §14). Port of
//! `core/src/qrk_text.c`; the output is byte-identical across all ports.

use alloc::string::String;
use core::fmt::{self, Write};

use crate::encode::QrCode;

/// Largest margin; larger values count as this one (SPEC §14.1).
const MARGIN_MAX: u8 = 16;

const HALF: [&str; 4] = [" ", "\u{2580}", "\u{2584}", "\u{2588}"]; // " ", "▀", "▄", "█"
const FULL: [&str; 2] = ["  ", "\u{2588}\u{2588}"]; // "██"
const HASH: [&str; 2] = ["  ", "##"];

/// Sets black ink on bright white paper (SPEC §14.5).
const ANSI_START: &str = "\x1b[30;107m";
/// Resets the colors before the end of the line.
const ANSI_END: &str = "\x1b[0m";

/// Characters used for the modules (SPEC §14.3, §14.4).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub enum TextStyle {
    /// One character for two modules stacked (`" "`, `"▀"`, `"▄"` or `"█"`),
    /// one line per two module rows. The most compact (default).
    #[default]
    HalfBlocks,
    /// `"██"` for ink and two spaces for paper, one line per module row.
    FullBlocks,
    /// `"##"` for ink and two spaces for paper, one line per module row.
    /// Plain ASCII: the fallback for terminals without UTF-8, or that show
    /// the block characters two columns wide.
    Hash,
}

/// Color codes written around each line (SPEC §14.5).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub enum TextColors {
    /// No codes: ink and paper use the terminal's own colors (default).
    #[default]
    None,
    /// Each line starts with `ESC[30;107m` (black on bright white) and ends
    /// with `ESC[0m`, so the code shows dark on light in any terminal theme.
    Ansi,
}

/// Text output options (SPEC §14.1).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct TextOptions {
    /// Characters used (default [`TextStyle::HalfBlocks`]).
    pub style: TextStyle,
    /// Light modules on each side, the quiet zone (default 2). Values above
    /// 16 count as 16.
    pub margin: u8,
    /// Swap ink and paper (default false). With [`TextColors::Ansi`] it
    /// normally stays false.
    pub invert: bool,
    /// Color codes (default [`TextColors::None`]).
    pub colors: TextColors,
}

impl Default for TextOptions {
    fn default() -> Self {
        TextOptions { style: TextStyle::HalfBlocks, margin: 2, invert: false, colors: TextColors::None }
    }
}

/// Streams the code as text into any [`core::fmt::Write`] (a `String`, a
/// serial port adapter, ...). Every line ends with `"\n"`. Nothing is
/// buffered, and `std` is not needed.
///
/// ```
/// use qrkude::{encode_text, write_text, EncodeOptions, TextOptions, TextStyle};
/// let code = encode_text("QRKude", &EncodeOptions::default())?;
/// let mut out = String::new();
/// write_text(&code, &TextOptions { style: TextStyle::Hash, ..TextOptions::default() }, &mut out).unwrap();
/// assert!(out.lines().all(|line| line.len() == 2 * (21 + 2 * 2)));
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn write_text<W: Write + ?Sized>(code: &QrCode, opts: &TextOptions, out: &mut W) -> fmt::Result {
    let n = code.size() as i32;
    let m = i32::from(opts.margin.min(MARGIN_MAX));
    let ink = |x: i32, y: i32| usize::from(code.is_dark(x, y) != opts.invert);
    let (step, cells): (usize, &[&str]) = match opts.style {
        TextStyle::HalfBlocks => (2, &HALF),
        TextStyle::FullBlocks => (1, &FULL),
        TextStyle::Hash => (1, &HASH),
    };
    let ansi = opts.colors == TextColors::Ansi;
    for y in (-m..n + m).step_by(step) {
        if ansi {
            out.write_str(ANSI_START)?;
        }
        for x in -m..n + m {
            let mut index = ink(x, y);
            if step == 2 {
                // on the last line, the row below the grid and margin is light
                index |= ink(x, y + 1) << 1;
            }
            out.write_str(cells[index])?;
        }
        if ansi {
            out.write_str(ANSI_END)?;
        }
        out.write_str("\n")?;
    }
    Ok(())
}

/// Renders the code as text into a `String` (see [`write_text`]).
///
/// ```
/// use qrkude::{encode_text, to_text, EncodeOptions, TextOptions};
/// let code = encode_text("QRKude", &EncodeOptions::default())?;
/// let text = to_text(&code, &TextOptions::default());
/// assert_eq!(text.lines().count(), 13); // 21 modules + 2 * 2 margin, two rows per line
/// assert!(text.ends_with('\n'));
/// print!("{text}");
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn to_text(code: &QrCode, opts: &TextOptions) -> String {
    let mut s = String::new();
    // writing into a String cannot fail
    let _ = write_text(code, opts, &mut s);
    s
}

/// The size of the text on screen, `(columns, lines)`. ANSI codes take no
/// columns. Use it to check that the code fits the terminal before printing.
///
/// ```
/// use qrkude::{encode_text, text_size, EncodeOptions, TextOptions, TextStyle};
/// let code = encode_text("QRKude", &EncodeOptions::default())?; // version 1, 21 x 21 modules
/// assert_eq!(text_size(&code, &TextOptions::default()), (25, 13));
/// let hash = TextOptions { style: TextStyle::Hash, ..TextOptions::default() };
/// assert_eq!(text_size(&code, &hash), (50, 25));
/// # Ok::<(), qrkude::Error>(())
/// ```
pub fn text_size(code: &QrCode, opts: &TextOptions) -> (usize, usize) {
    let side = code.size() + 2 * usize::from(opts.margin.min(MARGIN_MAX));
    match opts.style {
        TextStyle::HalfBlocks => (side, (side + 1) / 2),
        TextStyle::FullBlocks | TextStyle::Hash => (2 * side, side),
    }
}
