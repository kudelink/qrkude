// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Style model (SPEC §2). Plain structs with public fields and `Default`
//! values identical to the C reference (`qrk_style_default`).

use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;
use core::fmt;
use core::str::FromStr;

use crate::encode::Ecc;
use crate::Error;

/// Maximum number of gradient stops that are used (extra stops are ignored).
pub const MAX_STOPS: usize = 8;

/// The part of `s` before its first U+0000: every QRKude string ends there
/// (SPEC §2.0), as in the C reference.
pub(crate) fn until_nul(s: &str) -> &str {
    match s.find('\0') {
        Some(i) => &s[..i],
        None => s,
    }
}

/// An sRGB color with straight alpha, stored as `0xRRGGBBAA`.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct Color(pub u32);

impl Color {
    /// Fully transparent (`#00000000`).
    pub const TRANSPARENT: Color = Color(0);
    /// Opaque black.
    pub const BLACK: Color = Color(0x0000_00FF);
    /// Opaque white.
    pub const WHITE: Color = Color(0xFFFF_FFFF);

    /// Color from components.
    pub const fn rgba(r: u8, g: u8, b: u8, a: u8) -> Color {
        Color((r as u32) << 24 | (g as u32) << 16 | (b as u32) << 8 | a as u32)
    }

    /// Opaque color from components.
    pub const fn rgb(r: u8, g: u8, b: u8) -> Color {
        Color::rgba(r, g, b, 255)
    }

    /// Opaque color from `0xRRGGBB`.
    pub const fn hex(rgb: u32) -> Color {
        Color(rgb << 8 | 0xFF)
    }

    /// Red component.
    pub const fn r(self) -> u8 {
        (self.0 >> 24) as u8
    }
    /// Green component.
    pub const fn g(self) -> u8 {
        (self.0 >> 16) as u8
    }
    /// Blue component.
    pub const fn b(self) -> u8 {
        (self.0 >> 8) as u8
    }
    /// Alpha component (255 = opaque).
    pub const fn a(self) -> u8 {
        self.0 as u8
    }

    /// Parses `#rgb`, `#rrggbb`, `#rrggbbaa` (case-insensitive) or
    /// `transparent`.
    ///
    /// ```
    /// use qrkude::Color;
    /// assert_eq!(Color::parse("#a1c"), Some(Color::hex(0xaa11cc)));
    /// assert_eq!(Color::parse("#11223380"), Some(Color(0x11223380)));
    /// assert_eq!(Color::parse("transparent"), Some(Color::TRANSPARENT));
    /// assert_eq!(Color::parse("red"), None);
    /// ```
    pub fn parse(s: &str) -> Option<Color> {
        if s == "transparent" {
            return Some(Color::TRANSPARENT);
        }
        let bytes = s.as_bytes();
        if bytes.first() != Some(&b'#') {
            return None;
        }
        let mut v: u32 = 0;
        for &c in &bytes[1..] {
            let d = (c as char).to_digit(16)?;
            v = v << 4 | d;
        }
        match bytes.len() {
            4 => Some(Color::rgb(
                ((v >> 8) & 15) as u8 * 17,
                ((v >> 4) & 15) as u8 * 17,
                (v & 15) as u8 * 17,
            )),
            7 => Some(Color(v << 8 | 0xFF)),
            9 => Some(Color(v)),
            _ => None,
        }
    }
}

impl fmt::Debug for Color {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "#{:08x}", self.0)
    }
}

impl FromStr for Color {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self, Error> {
        Color::parse(s).ok_or(Error::Parse)
    }
}

/// A gradient stop.
#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Stop {
    /// Position, 0..=1.
    pub offset: f64,
    /// Color at that position.
    pub color: Color,
}

/// Linear gradient over the painted area.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct LinearGradient {
    /// Degrees: 0 = left to right, 90 = top to bottom.
    pub angle: f64,
    /// Up to [`MAX_STOPS`] stops are used; without stops the gradient paints
    /// nothing (and counts as a missing paint for lint).
    pub stops: Vec<Stop>,
}

/// Radial gradient; center and radius are relative to the painted area.
#[derive(Clone, Debug, PartialEq)]
pub struct RadialGradient {
    /// Center x, fraction of the area width (default 0.5).
    pub cx: f64,
    /// Center y, fraction of the area height (default 0.5).
    pub cy: f64,
    /// Radius, fraction of the larger side (default 0.75).
    pub r: f64,
    /// Up to [`MAX_STOPS`] stops are used.
    pub stops: Vec<Stop>,
}

impl Default for RadialGradient {
    fn default() -> Self {
        RadialGradient { cx: 0.5, cy: 0.5, r: 0.75, stops: Vec::new() }
    }
}

/// How an area is filled.
#[derive(Clone, Debug, PartialEq, Default)]
pub enum Paint {
    /// Nothing: transparent for the background, "same as modules" for the
    /// eye paints.
    #[default]
    None,
    /// A solid color.
    Solid(Color),
    /// A linear gradient.
    Linear(LinearGradient),
    /// A radial gradient.
    Radial(RadialGradient),
}

impl Paint {
    /// Solid color.
    pub fn solid(color: Color) -> Paint {
        Paint::Solid(color)
    }

    /// Two-stop linear gradient.
    pub fn linear(angle_deg: f64, from: Color, to: Color) -> Paint {
        Paint::Linear(LinearGradient { angle: angle_deg, stops: two_stops(from, to) })
    }

    /// Two-stop radial gradient centered in the area (`r` = 0.75).
    pub fn radial(center: Color, edge: Color) -> Paint {
        Paint::Radial(RadialGradient { stops: two_stops(center, edge), ..Default::default() })
    }

    /// `true` for linear and radial gradients.
    pub fn is_gradient(&self) -> bool {
        matches!(self, Paint::Linear(_) | Paint::Radial(_))
    }

    /// `true` for [`Paint::None`].
    pub fn is_none(&self) -> bool {
        matches!(self, Paint::None)
    }

    /// The stops that are used (at most [`MAX_STOPS`]); empty for non-gradients.
    pub fn stops(&self) -> &[Stop] {
        let s: &[Stop] = match self {
            Paint::Linear(g) => &g.stops,
            Paint::Radial(g) => &g.stops,
            _ => &[],
        };
        &s[..s.len().min(MAX_STOPS)]
    }
}

impl From<Color> for Paint {
    fn from(c: Color) -> Paint {
        Paint::Solid(c)
    }
}

fn two_stops(from: Color, to: Color) -> Vec<Stop> {
    vec![Stop { offset: 0.0, color: from }, Stop { offset: 1.0, color: to }]
}

macro_rules! named_enum {
    ($(#[$m:meta])* $name:ident { $($(#[$vm:meta])* $variant:ident = $s:literal),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
        pub enum $name {
            $($(#[$vm])* $variant),+
        }

        impl $name {
            /// Every value, in reference order.
            pub const ALL: &'static [$name] = &[$($name::$variant),+];

            /// Name used by the JSON format.
            pub fn name(self) -> &'static str {
                match self { $($name::$variant => $s),+ }
            }

            /// Parses a JSON name (case-sensitive).
            pub fn from_name(name: &str) -> Option<$name> {
                match name { $($s => Some($name::$variant),)+ _ => None }
            }
        }

        impl FromStr for $name {
            type Err = Error;
            fn from_str(s: &str) -> Result<Self, Error> {
                $name::from_name(s).ok_or(Error::Parse)
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(self.name())
            }
        }
    };
}

named_enum! {
    /// Shape of the data modules (SPEC §5.2).
    ModuleShape {
        /// Classic squares (`size < 1` adds gaps).
        #[default]
        Square = "square",
        /// Circles.
        Dot = "dot",
        /// Individually rounded squares.
        Rounded = "rounded",
        /// Rotated squares.
        Diamond = "diamond",
        /// Four-point sparkles.
        Star = "star",
        /// Connected, outer corners rounded.
        Fluid = "fluid",
        /// Fluid plus concave fillets on inner corners.
        Liquid = "liquid",
        /// Connected, outer corners cut at 45 degrees.
        Bevel = "bevel",
        /// Connected, only top-left / bottom-right corners rounded.
        Classy = "classy",
        /// Horizontal runs merged into pills.
        HBars = "hbars",
        /// Vertical runs merged into pills.
        VBars = "vbars",
    }
}

named_enum! {
    /// Shape of finder (and styled alignment) frames and balls (SPEC §6).
    EyeShape {
        /// Plain square.
        #[default]
        Square = "square",
        /// Rounded corners (25%).
        Rounded = "rounded",
        /// Circle.
        Circle = "circle",
        /// Two opposite corners rounded.
        Leaf = "leaf",
        /// All corners round except the one facing the center.
        Drop = "drop",
        /// Chamfered corners.
        Bevel = "bevel",
        /// Fully chamfered.
        Diamond = "diamond",
        /// Frames: one dot per module. Balls: drawn as a circle.
        Dots = "dots",
    }
}

named_enum! {
    /// Shape of the cleared logo area.
    LogoShape {
        /// Square.
        #[default]
        Square = "square",
        /// Rounded square (30% radius).
        Rounded = "rounded",
        /// Circle.
        Circle = "circle",
    }
}

named_enum! {
    /// Where the label band goes.
    LabelPosition {
        /// Below the symbol.
        #[default]
        Bottom = "bottom",
        /// Above the symbol.
        Top = "top",
    }
}

/// Data module style.
#[derive(Clone, Debug, PartialEq)]
pub struct ModuleStyle {
    /// Module shape (default square).
    pub shape: ModuleShape,
    /// Module size as a fraction of the cell, clamped to `[0.1, 1]`
    /// (square, dot, rounded, diamond, star, bars).
    pub size: f64,
    /// Corner roundness, clamped to `[0, 1]` (rounded, fluid, liquid,
    /// bevel, classy, bars).
    pub roundness: f64,
    /// Module paint (default black).
    pub color: Paint,
}

impl Default for ModuleStyle {
    fn default() -> Self {
        ModuleStyle {
            shape: ModuleShape::Square,
            size: 1.0,
            roundness: 1.0,
            color: Paint::Solid(Color::BLACK),
        }
    }
}

/// Finder pattern ("eye") style.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct EyeStyle {
    /// 7x7 ring shape.
    pub frame: EyeShape,
    /// 3x3 center shape (`Dots` is drawn as `Circle`).
    pub ball: EyeShape,
    /// Frame paint; [`Paint::None`] = same as the modules.
    pub frame_color: Paint,
    /// Ball paint; [`Paint::None`] = same as the modules.
    pub ball_color: Paint,
}

/// Alignment pattern style.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct AlignmentStyle {
    /// `None` (default): alignment patterns are drawn as data modules.
    pub frame: Option<EyeShape>,
    /// Ball shape when the frame is styled (`None` = square).
    pub ball: Option<EyeShape>,
}

/// Canvas background.
#[derive(Clone, Debug, PartialEq)]
pub struct Background {
    /// Background paint; [`Paint::None`] = transparent (default white).
    pub color: Paint,
    /// Corner radius in modules, clamped to `[0, min(W, H) / 2]`.
    pub radius: f64,
}

impl Default for Background {
    fn default() -> Self {
        Background { color: Paint::Solid(Color::WHITE), radius: 0.0 }
    }
}

/// Border drawn outside the quiet zone.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Border {
    /// Width in modules, clamped to `[0, 1000]` (0 = none).
    pub width: f64,
    /// Solid border color.
    pub color: Color,
}

impl Default for Border {
    fn default() -> Self {
        Border { width: 0.0, color: Color::BLACK }
    }
}

/// Center logo. A logo is present when `Style::logo` is `Some` and
/// `size > 0`.
#[derive(Clone, Debug, PartialEq)]
pub struct Logo {
    /// SVG `<image>` URL or `data:` URI (raster output takes a bitmap instead).
    pub href: Option<String>,
    /// Fraction of the symbol width, clamped to `[0, 1]` (default 0.2).
    pub size: f64,
    /// Clear space around the logo in modules, clamped to `[0, 1000]`
    /// (default 1).
    pub margin: f64,
    /// Shape of the cleared area.
    pub shape: LogoShape,
    /// Remove the modules under the logo area (default true).
    pub excavate: bool,
    /// Fill of the cleared area (transparent = none).
    pub background: Color,
}

impl Default for Logo {
    fn default() -> Self {
        Logo {
            href: None,
            size: 0.2,
            margin: 1.0,
            shape: LogoShape::Square,
            excavate: true,
            background: Color::TRANSPARENT,
        }
    }
}

impl Logo {
    /// A default logo (size 0.2) showing `href` in SVG output.
    pub fn new(href: impl Into<String>) -> Logo {
        Logo { href: Some(href.into()), ..Logo::default() }
    }
}

/// Text label in a band below or above the symbol. Drawn only when `text`
/// is not empty. Raster output does not draw text (the band stays
/// background-colored).
#[derive(Clone, Debug, PartialEq)]
pub struct Label {
    /// Label text (empty = no label).
    pub text: String,
    /// CSS font family (empty = `sans-serif`).
    pub font: String,
    /// Font size in modules, clamped to `[0.1, 1000]` (default 4).
    pub size: f64,
    /// Text color.
    pub color: Color,
    /// Band position.
    pub position: LabelPosition,
    /// Bold text (default true).
    pub bold: bool,
}

impl Default for Label {
    fn default() -> Self {
        Label {
            text: String::new(),
            font: String::from("sans-serif"),
            size: 4.0,
            color: Color::BLACK,
            position: LabelPosition::Bottom,
            bold: true,
        }
    }
}

impl Label {
    /// A default label with `text`.
    pub fn new(text: impl Into<String>) -> Label {
        Label { text: text.into(), ..Label::default() }
    }
}

/// Complete style of a QR code. `Style::default()` is a classic black on
/// white code with a 4-module quiet zone.
///
/// Out-of-range values are clamped when rendering (SPEC §2.2), never
/// rejected; NaN clamps to the lower bound, so a style built from untrusted
/// numbers cannot produce invalid output.
#[derive(Clone, Debug, PartialEq)]
pub struct Style {
    /// Quiet zone in modules, clamped to `[0, 1000]` (default 4).
    pub margin: f64,
    /// Data modules.
    pub modules: ModuleStyle,
    /// Finder patterns.
    pub eyes: EyeStyle,
    /// Alignment patterns.
    pub alignment: AlignmentStyle,
    /// Background.
    pub background: Background,
    /// Border.
    pub border: Border,
    /// Optional center logo.
    pub logo: Option<Logo>,
    /// Optional label.
    pub label: Option<Label>,
}

impl Default for Style {
    fn default() -> Self {
        Style {
            margin: 4.0,
            modules: ModuleStyle::default(),
            eyes: EyeStyle::default(),
            alignment: AlignmentStyle::default(),
            background: Background::default(),
            border: Border::default(),
            logo: None,
            label: None,
        }
    }
}

impl Style {
    /// The default style.
    pub fn new() -> Style {
        Style::default()
    }

    /// The logo, when present (`Some` with `size > 0`).
    pub fn active_logo(&self) -> Option<&Logo> {
        self.logo.as_ref().filter(|l| l.size > 0.0)
    }

    /// The label, when present (`Some` with non-empty text; a string ends
    /// at its first U+0000).
    pub fn active_label(&self) -> Option<&Label> {
        self.label.as_ref().filter(|l| !until_nul(&l.text).is_empty())
    }

    /// `true` when a logo is present.
    pub fn has_logo(&self) -> bool {
        self.active_logo().is_some()
    }

    /// Recommended minimum ECC (SPEC §1.3): `H` with a logo, else `M`.
    pub fn recommended_ecc(&self) -> Ecc {
        if self.has_logo() {
            Ecc::High
        } else {
            Ecc::Medium
        }
    }

    /// Chainable setter for the module shape.
    pub fn with_module_shape(mut self, shape: ModuleShape) -> Style {
        self.modules.shape = shape;
        self
    }

    /// Chainable setter for the module paint.
    pub fn with_color(mut self, paint: impl Into<Paint>) -> Style {
        self.modules.color = paint.into();
        self
    }

    /// Chainable setter for the eye frame and ball shapes.
    pub fn with_eyes(mut self, frame: EyeShape, ball: EyeShape) -> Style {
        self.eyes.frame = frame;
        self.eyes.ball = ball;
        self
    }

    /// Chainable setter for the background paint.
    pub fn with_background(mut self, paint: impl Into<Paint>) -> Style {
        self.background.color = paint.into();
        self
    }

    /// Chainable setter for the logo.
    pub fn with_logo(mut self, logo: Logo) -> Style {
        self.logo = Some(logo);
        self
    }

    /// Chainable setter for the label.
    pub fn with_label(mut self, label: Label) -> Style {
        self.label = Some(label);
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colors() {
        assert_eq!(Color::parse("#FED"), Some(Color::hex(0xffeedd)));
        assert_eq!(Color::parse("#000000"), Some(Color::BLACK));
        assert_eq!(Color::parse("#"), None);
        assert_eq!(Color::parse("#12345"), None);
        assert_eq!(Color::parse("#12345g"), None);
        assert_eq!(Color::parse(""), None);
        assert_eq!("#ffffff40".parse::<Color>(), Ok(Color(0xffffff40)));
        let c = Color::rgba(1, 2, 3, 4);
        assert_eq!((c.r(), c.g(), c.b(), c.a()), (1, 2, 3, 4));
    }

    #[test]
    fn names() {
        for &s in ModuleShape::ALL {
            assert_eq!(ModuleShape::from_name(s.name()), Some(s));
        }
        for &s in EyeShape::ALL {
            assert_eq!(s.name().parse::<EyeShape>(), Ok(s));
        }
        assert_eq!(ModuleShape::ALL.len(), 11);
        assert_eq!(EyeShape::from_name("Square"), None);
    }

    #[test]
    fn logo_presence() {
        let mut s = Style::default();
        assert_eq!(s.recommended_ecc(), Ecc::Medium);
        s.logo = Some(Logo { size: 0.0, ..Logo::default() });
        assert!(!s.has_logo());
        s.logo = Some(Logo::new("x.png"));
        assert_eq!(s.recommended_ecc(), Ecc::High);
    }
}
