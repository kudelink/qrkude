// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Prints a QR code in the terminal, with colors only when it makes sense.
//!
//!     cargo run --example terminal -- [TEXT]

use std::io::IsTerminal;

use qrkude::{encode_text, text_size, to_text, EncodeOptions, TextColors, TextOptions};

fn main() -> Result<(), qrkude::Error> {
    let text = std::env::args().nth(1).unwrap_or_else(|| "https://kudelink.com".into());
    let code = encode_text(&text, &EncodeOptions::default())?;

    // The recommended color rule: ANSI colors (black on bright white) when
    // the output is a terminal and the user has not set NO_COLOR, else none.
    let no_color = std::env::var_os("NO_COLOR").is_some_and(|v| !v.is_empty());
    let ansi = std::io::stdout().is_terminal() && !no_color;
    let opts = TextOptions { colors: if ansi { TextColors::Ansi } else { TextColors::None }, ..TextOptions::default() };

    print!("{}", to_text(&code, &opts));
    let (columns, lines) = text_size(&code, &opts);
    println!("version {}: {columns} columns x {lines} lines", code.version());
    Ok(())
}
