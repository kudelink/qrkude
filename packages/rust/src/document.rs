// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! The cross-language style document `{"encode": ..., "style": ..., "svg": ...}`.

use alloc::string::String;

use crate::encode::{encode_for_style, EncodeOptions, QrCode};
use crate::style::Style;
use crate::svg::{to_svg, SvgOptions};
use crate::Error;

/// A complete style document: encoder options, style and SVG options.
///
/// With the `json` feature it can be parsed from the cross-language JSON
/// format with `StyleDocument::from_json` (or any serde deserializer).
#[derive(Clone, Debug, PartialEq, Default)]
pub struct StyleDocument {
    /// Encoder options (`ecc: None` = automatic, see [`StyleDocument::encode`]).
    pub encode: EncodeOptions,
    /// Visual style.
    pub style: Style,
    /// SVG options.
    pub svg: SvgOptions,
}

impl StyleDocument {
    /// Encodes `data` with the document's options. When `encode.ecc` is
    /// absent the ECC is `H` if the style has a logo, else `M` (SPEC §1.3).
    pub fn encode(&self, data: &[u8]) -> Result<QrCode, Error> {
        encode_for_style(data, &self.encode, &self.style)
    }

    /// Encodes `data` and renders the SVG document.
    ///
    /// ```
    /// let doc = qrkude::StyleDocument::default();
    /// let svg = doc.render_svg(b"QRKude")?;
    /// assert!(svg.contains("viewBox=\"0 0 29 29\""));
    /// # Ok::<(), qrkude::Error>(())
    /// ```
    pub fn render_svg(&self, data: &[u8]) -> Result<String, Error> {
        let code = self.encode(data)?;
        Ok(to_svg(&code, &self.style, &self.svg))
    }
}
