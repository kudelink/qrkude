// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Reader for the cross-language style document (feature `json`), following
//! SPEC §2.0 and the C reader (`core/src/qrk_json.c`):
//!
//! - members apply in document order onto the defaults; a repeated object key
//!   merges field by field, scalars, arrays and paints replace;
//! - unknown keys of any length are ignored, whatever their value;
//! - `logo` takes its default size 0.2 only when no logo is present yet, and
//!   `logo: null`, `label: null`, `alignment: null` reset those features to
//!   their defaults (absent), forgetting every member set before;
//! - numbers parse to the nearest double (serde_json with `float_roundtrip`);
//!   a number token longer than 400 characters or a non-finite number is a
//!   parse error anywhere, unknown keys included; `minVersion`, `maxVersion`
//!   and `mask` clamp to [-1000, 1000] and truncate toward zero;
//! - nesting deeper than 64 objects/arrays (top-level object = 1) is a parse
//!   error; a value of the wrong type for a known key is a parse error;
//! - every string value and key ends at its first U+0000 (`"#f00\u0000x"` is
//!   the color `#f00`), and a surrogate escape that is not part of a valid
//!   high/low pair reads as U+FFFD.
//!
//! Lengths are clamped later, at layout time, as in C. The depth, finiteness
//! and U+0000 rules also apply when the `Deserialize` impls are used directly;
//! the number-token length limit and the lone-surrogate rule need the raw
//! text and are applied by [`StyleDocument::from_json`] (serde_json alone
//! rejects lone surrogates).

use alloc::borrow::Cow;
use alloc::string::String;
use alloc::vec::Vec;
use core::fmt;

use serde::de::{self, DeserializeSeed, Deserializer, IgnoredAny, MapAccess, SeqAccess, Unexpected, Visitor};
use serde::Deserialize;

use crate::document::StyleDocument;
use crate::encode::{Ecc, EncodeOptions};
use crate::layout::clamp;
use crate::style::{
    AlignmentStyle, Background, Border, Color, EyeShape, EyeStyle, Label, LabelPosition,
    LinearGradient, Logo, LogoShape, ModuleShape, ModuleStyle, Paint, RadialGradient, Stop, Style,
    until_nul, MAX_STOPS,
};
use crate::svg::SvgOptions;
use crate::Error;

/// Deepest accepted nesting of objects/arrays (the top-level object is 1).
const MAX_DEPTH: u32 = 64;
/// Longest accepted number token, in characters.
const MAX_NUMBER_LEN: usize = 400;

impl StyleDocument {
    /// Parses `{"encode": {...}, "style": {...}, "svg": {...}}` or a bare
    /// style object (top-level style keys are accepted too), starting from
    /// the defaults (SPEC §2.0). Every failure is [`Error::Parse`]; options
    /// that are out of range (e.g. `minVersion: 0`) are reported later by
    /// [`StyleDocument::encode`] as [`Error::InvalidArgument`].
    ///
    /// ```
    /// use qrkude::{Error, ModuleShape, StyleDocument};
    /// let doc = StyleDocument::from_json(r#"{"style": {"modules": {"shape": "dot"}}, "svg": {"width": 300}}"#)?;
    /// assert_eq!(doc.style.modules.shape, ModuleShape::Dot);
    /// assert_eq!(doc.svg.width, 300.0);
    /// assert!(doc.encode.ecc.is_none()); // automatic: H with a logo, else M
    /// assert_eq!(StyleDocument::from_json(r#"{"margin": 1e999}"#), Err(Error::Parse));
    /// # Ok::<(), qrkude::Error>(())
    /// ```
    pub fn from_json(json: &str) -> Result<StyleDocument, Error> {
        let json = prescan(json)?;
        serde_json::from_str(&json).map_err(|_| Error::Parse)
    }
}

impl Style {
    /// Parses a style document (see [`StyleDocument::from_json`]) and
    /// returns its style.
    pub fn from_json(json: &str) -> Result<Style, Error> {
        StyleDocument::from_json(json).map(|d| d.style)
    }
}

/// Checks the limits of SPEC §2.0 on the raw text (serde_json skips unknown
/// numbers without converting them, has no token length limit and allows 128
/// levels) and rewrites every surrogate escape that is not part of a valid
/// high/low pair to `\uFFFD` (same length; serde_json would reject it).
/// Malformed JSON is left for serde_json to reject.
fn prescan(json: &str) -> Result<Cow<'_, str>, Error> {
    let b = json.as_bytes();
    let mut lone = Vec::new(); // byte offsets of lone surrogate escapes
    let mut depth = 0u32;
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'"' => {
                i += 1;
                while i < b.len() && b[i] != b'"' {
                    if b[i] != b'\\' {
                        i += 1;
                    } else if b.get(i + 1) != Some(&b'u') {
                        i += 2;
                    } else {
                        match hex4(b, i + 2) {
                            Some(0xD800..=0xDBFF) => {
                                // a high surrogate pairs only with a following low one
                                if b.get(i + 6) == Some(&b'\\') && b.get(i + 7) == Some(&b'u') {
                                    if let Some(0xDC00..=0xDFFF) = hex4(b, i + 8) {
                                        i += 12;
                                        continue;
                                    }
                                }
                                lone.push(i);
                                i += 6; // a following escape is read on its own
                            }
                            Some(0xDC00..=0xDFFF) => {
                                lone.push(i);
                                i += 6;
                            }
                            Some(_) => i += 6,
                            None => i += 2,
                        }
                    }
                }
                i += 1;
            }
            b'{' | b'[' => {
                depth += 1;
                if depth > MAX_DEPTH {
                    return Err(Error::Parse);
                }
                i += 1;
            }
            b'}' | b']' => {
                depth = depth.saturating_sub(1);
                i += 1;
            }
            b'-' | b'0'..=b'9' => {
                let start = i;
                while i < b.len() && matches!(b[i], b'0'..=b'9' | b'-' | b'+' | b'.' | b'e' | b'E') {
                    i += 1;
                }
                if i - start > MAX_NUMBER_LEN {
                    return Err(Error::Parse);
                }
                // ASCII-only token; a malformed one is rejected by serde_json
                if let Ok(v) = json[start..i].parse::<f64>() {
                    if !v.is_finite() {
                        return Err(Error::Parse);
                    }
                }
            }
            _ => i += 1,
        }
    }
    if lone.is_empty() {
        return Ok(Cow::Borrowed(json));
    }
    let mut out = String::from(json);
    for at in lone {
        // ASCII escape replaced by an ASCII escape of the same length
        out.replace_range(at..at + 6, "\\uFFFD");
    }
    Ok(Cow::Owned(out))
}

/// The value of the 4 hex digits at `b[at..at + 4]`.
fn hex4(b: &[u8], at: usize) -> Option<u32> {
    let digits = b.get(at..at + 4)?;
    let mut v = 0;
    for &d in digits {
        v = v << 4 | (d as char).to_digit(16)?;
    }
    Some(v)
}

fn check_depth<E: de::Error>(depth: u32) -> Result<(), E> {
    if depth > MAX_DEPTH {
        Err(E::custom("nesting deeper than 64 levels"))
    } else {
        Ok(())
    }
}

fn finite<E: de::Error>(v: f64) -> Result<f64, E> {
    if v.is_finite() {
        Ok(v)
    } else {
        Err(E::custom("number is not finite"))
    }
}

// ---- small values -----------------------------------------------------------

/// Map key matched against a fixed list (no allocation).
#[derive(Clone, Copy)]
struct Key(&'static [&'static str]);

impl<'de> DeserializeSeed<'de> for Key {
    type Value = Option<usize>;
    fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<Option<usize>, D::Error> {
        d.deserialize_str(self)
    }
}

impl<'de> Visitor<'de> for Key {
    type Value = Option<usize>;
    fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("a key")
    }
    fn visit_str<E: de::Error>(self, v: &str) -> Result<Option<usize>, E> {
        let v = until_nul(v);
        Ok(self.0.iter().position(|k| *k == v))
    }
}

/// Skips any value; `.0` is the nesting level a container value has here.
/// Enforces the depth limit and finite numbers inside unknown values too.
#[derive(Clone, Copy)]
struct Skip(u32);

impl<'de> DeserializeSeed<'de> for Skip {
    type Value = ();
    fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<(), D::Error> {
        d.deserialize_any(self)
    }
}

impl<'de> Visitor<'de> for Skip {
    type Value = ();
    fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("any value")
    }
    fn visit_bool<E: de::Error>(self, _: bool) -> Result<(), E> {
        Ok(())
    }
    fn visit_i64<E: de::Error>(self, _: i64) -> Result<(), E> {
        Ok(())
    }
    fn visit_u64<E: de::Error>(self, _: u64) -> Result<(), E> {
        Ok(())
    }
    fn visit_f64<E: de::Error>(self, v: f64) -> Result<(), E> {
        finite(v).map(|_| ())
    }
    fn visit_str<E: de::Error>(self, _: &str) -> Result<(), E> {
        Ok(())
    }
    fn visit_bytes<E: de::Error>(self, _: &[u8]) -> Result<(), E> {
        Ok(())
    }
    fn visit_unit<E: de::Error>(self) -> Result<(), E> {
        Ok(())
    }
    fn visit_none<E: de::Error>(self) -> Result<(), E> {
        Ok(())
    }
    fn visit_some<D: Deserializer<'de>>(self, d: D) -> Result<(), D::Error> {
        self.deserialize(d)
    }
    fn visit_newtype_struct<D: Deserializer<'de>>(self, d: D) -> Result<(), D::Error> {
        self.deserialize(d)
    }
    fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<(), A::Error> {
        check_depth(self.0)?;
        while seq.next_element_seed(Skip(self.0 + 1))?.is_some() {}
        Ok(())
    }
    fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<(), A::Error> {
        check_depth(self.0)?;
        while map.next_key::<IgnoredAny>()?.is_some() {
            map.next_value_seed(Skip(self.0 + 1))?;
        }
        Ok(())
    }
}

/// A string or `null`, cut at its first U+0000 (`null` reads as empty).
struct Text(String);

impl<'de> Deserialize<'de> for Text {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Text, D::Error> {
        let s = Option::<String>::deserialize(d)?.unwrap_or_default();
        Ok(Text(match s.find('\0') {
            Some(i) => String::from(&s[..i]),
            None => s,
        }))
    }
}

fn text<'de, A: MapAccess<'de>>(map: &mut A) -> Result<String, A::Error> {
    map.next_value::<Text>().map(|t| t.0)
}

/// Any finite JSON number as `f64`.
struct Num(f64);

impl<'de> Deserialize<'de> for Num {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Num, D::Error> {
        struct V;
        impl<'de> Visitor<'de> for V {
            type Value = Num;
            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("a number")
            }
            fn visit_f64<E: de::Error>(self, v: f64) -> Result<Num, E> {
                finite(v).map(Num)
            }
            fn visit_i64<E: de::Error>(self, v: i64) -> Result<Num, E> {
                Ok(Num(v as f64))
            }
            fn visit_u64<E: de::Error>(self, v: u64) -> Result<Num, E> {
                Ok(Num(v as f64))
            }
        }
        d.deserialize_f64(V)
    }
}

/// A color string, or `null` (= transparent).
struct ColorValue(Color);

impl<'de> Deserialize<'de> for ColorValue {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<ColorValue, D::Error> {
        struct V;
        impl<'de> Visitor<'de> for V {
            type Value = ColorValue;
            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("a color string or null")
            }
            fn visit_unit<E: de::Error>(self) -> Result<ColorValue, E> {
                Ok(ColorValue(Color::TRANSPARENT))
            }
            fn visit_none<E: de::Error>(self) -> Result<ColorValue, E> {
                Ok(ColorValue(Color::TRANSPARENT))
            }
            fn visit_str<E: de::Error>(self, v: &str) -> Result<ColorValue, E> {
                Color::parse(until_nul(v)).map(ColorValue).ok_or_else(|| E::invalid_value(Unexpected::Str(v), &self))
            }
        }
        d.deserialize_any(V)
    }
}

impl<'de> Deserialize<'de> for Color {
    /// A color string (`#rgb`, `#rrggbb`, `#rrggbbaa`, `transparent`).
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Color, D::Error> {
        struct V;
        impl<'de> Visitor<'de> for V {
            type Value = Color;
            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("a color string")
            }
            fn visit_str<E: de::Error>(self, v: &str) -> Result<Color, E> {
                Color::parse(until_nul(v)).ok_or_else(|| E::invalid_value(Unexpected::Str(v), &self))
            }
        }
        d.deserialize_str(V)
    }
}

macro_rules! named_deserialize {
    ($($t:ty => $what:literal),+ $(,)?) => {$(
        impl<'de> Deserialize<'de> for $t {
            /// Deserializes from the JSON name.
            fn deserialize<D: Deserializer<'de>>(d: D) -> Result<$t, D::Error> {
                struct V;
                impl<'de> Visitor<'de> for V {
                    type Value = $t;
                    fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                        f.write_str($what)
                    }
                    fn visit_str<E: de::Error>(self, v: &str) -> Result<$t, E> {
                        <$t>::from_name(until_nul(v)).ok_or_else(|| E::invalid_value(Unexpected::Str(v), &self))
                    }
                }
                d.deserialize_str(V)
            }
        }
    )+};
}

named_deserialize! {
    ModuleShape => "a module shape name",
    EyeShape => "an eye shape name",
    LogoShape => "\"square\", \"rounded\" or \"circle\"",
    LabelPosition => "\"bottom\" or \"top\"",
}

impl<'de> Deserialize<'de> for Ecc {
    /// `"L"`, `"M"`, `"Q"` or `"H"`.
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Ecc, D::Error> {
        struct V;
        impl<'de> Visitor<'de> for V {
            type Value = Ecc;
            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("\"L\", \"M\", \"Q\" or \"H\"")
            }
            fn visit_str<E: de::Error>(self, v: &str) -> Result<Ecc, E> {
                let mut chars = until_nul(v).chars();
                match (chars.next(), chars.next()) {
                    (Some(c), None) => Ecc::from_letter(c),
                    _ => None,
                }
                .ok_or_else(|| E::invalid_value(Unexpected::Str(v), &self))
            }
        }
        d.deserialize_str(V)
    }
}

// ---- paints -----------------------------------------------------------------

const PAINT_KEYS: &[&str] = &["type", "angle", "cx", "cy", "r", "stops"];
const STOP_KEYS: &[&str] = &["offset", "color"];

#[derive(Clone, Copy, PartialEq)]
enum GradientType {
    Linear,
    Radial,
}

impl<'de> Deserialize<'de> for GradientType {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<GradientType, D::Error> {
        struct V;
        impl<'de> Visitor<'de> for V {
            type Value = GradientType;
            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("\"linear\" or \"radial\"")
            }
            fn visit_str<E: de::Error>(self, v: &str) -> Result<GradientType, E> {
                match until_nul(v) {
                    "linear" => Ok(GradientType::Linear),
                    "radial" => Ok(GradientType::Radial),
                    _ => Err(E::invalid_value(Unexpected::Str(v), &self)),
                }
            }
        }
        d.deserialize_str(V)
    }
}

/// One gradient stop object at nesting level `.0`.
struct StopSeed(u32);

impl<'de> DeserializeSeed<'de> for StopSeed {
    type Value = Stop;
    fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<Stop, D::Error> {
        d.deserialize_map(self)
    }
}

impl<'de> Visitor<'de> for StopSeed {
    type Value = Stop;
    fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("a gradient stop object")
    }
    fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<Stop, A::Error> {
        check_depth(self.0)?;
        let mut stop = Stop::default();
        while let Some(k) = map.next_key_seed(Key(STOP_KEYS))? {
            match k {
                Some(0) => stop.offset = map.next_value::<Num>()?.0,
                Some(1) => stop.color = map.next_value::<ColorValue>()?.0,
                _ => map.next_value_seed(Skip(self.0 + 1))?,
            }
        }
        Ok(stop)
    }
}

/// Stops array at nesting level `.0`; at most [`MAX_STOPS`] are kept, the
/// rest are validated and dropped.
struct StopsSeed(u32);

impl<'de> DeserializeSeed<'de> for StopsSeed {
    type Value = Vec<Stop>;
    fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<Vec<Stop>, D::Error> {
        d.deserialize_seq(self)
    }
}

impl<'de> Visitor<'de> for StopsSeed {
    type Value = Vec<Stop>;
    fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("an array of gradient stops")
    }
    fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<Vec<Stop>, A::Error> {
        check_depth(self.0)?;
        let mut stops = Vec::new();
        while let Some(s) = seq.next_element_seed(StopSeed(self.0 + 1))? {
            if stops.len() < MAX_STOPS {
                stops.push(s);
            }
        }
        Ok(stops)
    }
}

/// A paint (color string, `null` = none, or gradient object at level `.0`).
/// A paint always replaces the previous one.
struct PaintSeed(u32);

impl<'de> DeserializeSeed<'de> for PaintSeed {
    type Value = Paint;
    fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<Paint, D::Error> {
        d.deserialize_any(self)
    }
}

impl<'de> Visitor<'de> for PaintSeed {
    type Value = Paint;
    fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("a color string, null or a gradient object")
    }
    fn visit_unit<E: de::Error>(self) -> Result<Paint, E> {
        Ok(Paint::None)
    }
    fn visit_none<E: de::Error>(self) -> Result<Paint, E> {
        Ok(Paint::None)
    }
    fn visit_str<E: de::Error>(self, v: &str) -> Result<Paint, E> {
        Color::parse(until_nul(v)).map(Paint::Solid).ok_or_else(|| E::invalid_value(Unexpected::Str(v), &self))
    }
    fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<Paint, A::Error> {
        check_depth(self.0)?;
        let mut kind = GradientType::Linear;
        let mut angle = 0.0;
        let mut radial = RadialGradient::default();
        let mut stops = Vec::new();
        while let Some(k) = map.next_key_seed(Key(PAINT_KEYS))? {
            match k {
                Some(0) => kind = map.next_value()?,
                Some(1) => angle = map.next_value::<Num>()?.0,
                Some(2) => radial.cx = map.next_value::<Num>()?.0,
                Some(3) => radial.cy = map.next_value::<Num>()?.0,
                Some(4) => radial.r = map.next_value::<Num>()?.0,
                Some(5) => stops = map.next_value_seed(StopsSeed(self.0 + 1))?,
                _ => map.next_value_seed(Skip(self.0 + 1))?,
            }
        }
        Ok(match kind {
            GradientType::Linear => Paint::Linear(LinearGradient { angle, stops }),
            GradientType::Radial => Paint::Radial(RadialGradient { stops, ..radial }),
        })
    }
}

// ---- objects applied onto existing values -----------------------------------

/// Declares a seed that applies the members of a JSON object onto `&mut T`
/// (so a repeated key merges into what earlier members set). The second
/// field is the object's nesting level, bound to `$depth` in the body.
/// `init` (optional) runs when an object starts; `null` (optional) handles a
/// `null` value, without it `null` is an error.
macro_rules! object_seed {
    ($name:ident, $t:ty, $keys:expr, $what:literal,
     $(init: |$itarget:ident| $init:block,)?
     |$target:ident, $key:ident, $map:ident, $depth:ident| $body:block
     $(, null: |$ntarget:ident| $null:block)?) => {
        struct $name<'a>(&'a mut $t, u32);

        impl<'de, 'a> DeserializeSeed<'de> for $name<'a> {
            type Value = ();
            fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<(), D::Error> {
                object_seed!(@dispatch d, self $(, $null)?)
            }
        }

        impl<'de, 'a> Visitor<'de> for $name<'a> {
            type Value = ();
            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str($what)
            }
            fn visit_map<A: MapAccess<'de>>(self, mut $map: A) -> Result<(), A::Error> {
                let $depth = self.1;
                check_depth($depth)?;
                let $target = self.0;
                $({
                    let $itarget = &mut *$target;
                    $init
                })?
                while let Some($key) = $map.next_key_seed(Key($keys))? {
                    $body
                }
                Ok(())
            }
            $(
                fn visit_unit<E: de::Error>(self) -> Result<(), E> {
                    let $ntarget = self.0;
                    $null
                    Ok(())
                }
                fn visit_none<E: de::Error>(self) -> Result<(), E> {
                    self.visit_unit()
                }
            )?
        }
    };
    (@dispatch $d:ident, $s:ident) => { $d.deserialize_map($s) };
    (@dispatch $d:ident, $s:ident, $null:block) => { $d.deserialize_any($s) };
}

/// Skips the value of an unknown member of an object at level `depth`.
fn skip<'de, A: MapAccess<'de>>(map: &mut A, depth: u32) -> Result<(), A::Error> {
    map.next_value_seed(Skip(depth + 1))
}

fn num<'de, A: MapAccess<'de>>(map: &mut A) -> Result<f64, A::Error> {
    map.next_value::<Num>().map(|n| n.0)
}

/// Integer field: clamp to [-1000, 1000], then truncate toward zero.
fn int_field(d: f64) -> i32 {
    clamp(d, -1000.0, 1000.0) as i32
}

/// Version bound: out-of-range values map to 0 or 255, which stay invalid.
fn version_field(d: f64) -> u8 {
    int_field(d).clamp(0, 255) as u8
}

object_seed!(ModulesSeed, ModuleStyle, &["shape", "size", "roundness", "color"], "a modules object",
    |m, k, map, d| {
        match k {
            Some(0) => m.shape = map.next_value()?,
            Some(1) => m.size = num(&mut map)?,
            Some(2) => m.roundness = num(&mut map)?,
            Some(3) => m.color = map.next_value_seed(PaintSeed(d + 1))?,
            _ => skip(&mut map, d)?,
        }
    });

object_seed!(EyesSeed, EyeStyle, &["frame", "ball", "frameColor", "ballColor"], "an eyes object",
    |e, k, map, d| {
        match k {
            Some(0) => e.frame = map.next_value()?,
            Some(1) => e.ball = map.next_value()?,
            Some(2) => e.frame_color = map.next_value_seed(PaintSeed(d + 1))?,
            Some(3) => e.ball_color = map.next_value_seed(PaintSeed(d + 1))?,
            _ => skip(&mut map, d)?,
        }
    });

// `null` turns styled alignment patterns off.
object_seed!(AlignmentSeed, AlignmentStyle, &["frame", "ball"], "an alignment object or null",
    |a, k, map, d| {
        match k {
            Some(0) => a.frame = map.next_value()?,
            Some(1) => a.ball = map.next_value()?,
            _ => skip(&mut map, d)?,
        }
    },
    null: |a| {
        a.frame = None;
        a.ball = None;
    });

object_seed!(BackgroundSeed, Background, &["color", "radius"], "a background object",
    |b, k, map, d| {
        match k {
            Some(0) => b.color = map.next_value_seed(PaintSeed(d + 1))?,
            Some(1) => b.radius = num(&mut map)?,
            _ => skip(&mut map, d)?,
        }
    });

object_seed!(BorderSeed, Border, &["width", "color"], "a border object",
    |b, k, map, d| {
        match k {
            Some(0) => b.width = num(&mut map)?,
            Some(1) => b.color = map.next_value::<ColorValue>()?.0,
            _ => skip(&mut map, d)?,
        }
    });

// `null` resets the logo to its defaults (absent); an object merges into the
// current logo and takes the default size 0.2 only when no logo is present.
object_seed!(LogoSeed, Option<Logo>, &["href", "size", "margin", "excavate", "background", "shape"],
    "a logo object or null",
    init: |slot| {
        let logo = slot.get_or_insert_with(Logo::default);
        if !(logo.size > 0.0) {
            logo.size = 0.2;
        }
    },
    |slot, k, map, d| {
        let logo = slot.get_or_insert_with(Logo::default);
        match k {
            Some(0) => logo.href = map.next_value::<Option<String>>()?.map(|h| String::from(until_nul(&h))),
            Some(1) => logo.size = num(&mut map)?,
            Some(2) => logo.margin = num(&mut map)?,
            Some(3) => logo.excavate = map.next_value()?,
            Some(4) => logo.background = map.next_value::<ColorValue>()?.0,
            Some(5) => logo.shape = map.next_value()?,
            _ => skip(&mut map, d)?,
        }
    },
    null: |slot| {
        *slot = None;
    });

// `null` resets the label to its defaults (absent); an object updates it.
object_seed!(LabelSeed, Option<Label>, &["text", "font", "size", "color", "bold", "position"],
    "a label object or null",
    init: |slot| {
        slot.get_or_insert_with(Label::default);
    },
    |slot, k, map, d| {
        let label = slot.get_or_insert_with(Label::default);
        match k {
            Some(0) => label.text = text(&mut map)?,
            Some(1) => label.font = text(&mut map)?,
            Some(2) => label.size = num(&mut map)?,
            Some(3) => label.color = map.next_value::<ColorValue>()?.0,
            Some(4) => label.bold = map.next_value()?,
            Some(5) => label.position = map.next_value()?,
            _ => skip(&mut map, d)?,
        }
    },
    null: |slot| {
        *slot = None;
    });

object_seed!(EncodeSeed, EncodeOptions, &["ecc", "minVersion", "maxVersion", "mask", "boostEcc", "eci"],
    "an encode object",
    |e, k, map, d| {
        match k {
            Some(0) => e.ecc = Some(map.next_value()?),
            Some(1) => e.min_version = version_field(num(&mut map)?),
            Some(2) => e.max_version = version_field(num(&mut map)?),
            Some(3) => {
                // -1 = automatic; other out-of-range values stay invalid
                e.mask = match int_field(num(&mut map)?) {
                    -1 => None,
                    m @ 0..=255 => Some(m as u8),
                    _ => Some(255),
                };
            }
            Some(4) => e.boost_ecc = map.next_value()?,
            Some(5) => e.eci = map.next_value()?,
            _ => skip(&mut map, d)?,
        }
    });

object_seed!(SvgSeed, SvgOptions, &["width", "idPrefix"], "an svg object",
    |o, k, map, d| {
        match k {
            Some(0) => o.width = num(&mut map)?,
            Some(1) => o.id_prefix = text(&mut map)?,
            _ => skip(&mut map, d)?,
        }
    });

const STYLE_KEYS: &[&str] = &["margin", "modules", "eyes", "alignment", "background", "border", "logo", "label"];

/// Applies one style member of an object at level `depth` (C: `parse_style_key`).
fn style_key<'de, A: MapAccess<'de>>(map: &mut A, s: &mut Style, key: Option<usize>, depth: u32) -> Result<(), A::Error> {
    let d = depth + 1;
    match key {
        Some(0) => s.margin = num(map)?,
        Some(1) => map.next_value_seed(ModulesSeed(&mut s.modules, d))?,
        Some(2) => map.next_value_seed(EyesSeed(&mut s.eyes, d))?,
        Some(3) => map.next_value_seed(AlignmentSeed(&mut s.alignment, d))?,
        Some(4) => map.next_value_seed(BackgroundSeed(&mut s.background, d))?,
        Some(5) => map.next_value_seed(BorderSeed(&mut s.border, d))?,
        Some(6) => map.next_value_seed(LogoSeed(&mut s.logo, d))?,
        Some(7) => map.next_value_seed(LabelSeed(&mut s.label, d))?,
        _ => skip(map, depth)?,
    }
    Ok(())
}

object_seed!(StyleSeed, Style, STYLE_KEYS, "a style object",
    |s, k, map, d| {
        style_key(&mut map, s, k, d)?;
    });

const DOC_KEYS: &[&str] = &[
    "encode", "svg", "style", "margin", "modules", "eyes", "alignment", "background", "border", "logo", "label",
];

object_seed!(DocSeed, StyleDocument, DOC_KEYS, "a style document object",
    |doc, k, map, d| {
        match k {
            Some(0) => map.next_value_seed(EncodeSeed(&mut doc.encode, d + 1))?,
            Some(1) => map.next_value_seed(SvgSeed(&mut doc.svg, d + 1))?,
            Some(2) => map.next_value_seed(StyleSeed(&mut doc.style, d + 1))?,
            Some(i) => style_key(&mut map, &mut doc.style, Some(i - 3), d)?,
            None => skip(&mut map, d)?,
        }
    });

impl<'de> Deserialize<'de> for StyleDocument {
    /// Reads the cross-language document, starting from the defaults.
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<StyleDocument, D::Error> {
        let mut doc = StyleDocument::default();
        DocSeed(&mut doc, 1).deserialize(d)?;
        Ok(doc)
    }
}

impl<'de> Deserialize<'de> for Style {
    /// Reads a bare style object (the value of `"style"`), starting from the
    /// defaults.
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Style, D::Error> {
        let mut style = Style::default();
        StyleSeed(&mut style, 1).deserialize(d)?;
        Ok(style)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::format;

    #[test]
    fn defaults_and_nulls() {
        let doc = StyleDocument::from_json("{}").unwrap();
        assert_eq!(doc, StyleDocument::default());
        let d = StyleDocument::from_json(
            r#"{"logo": null, "label": null, "alignment": null, "background": {"color": null},
                "eyes": {"frameColor": null}, "border": {"color": null}}"#,
        )
        .unwrap();
        assert!(d.style.logo.is_none() && d.style.label.is_none());
        assert_eq!(d.style.background.color, Paint::None);
        assert_eq!(d.style.border.color, Color::TRANSPARENT);
    }

    #[test]
    fn document_order() {
        let d = StyleDocument::from_json(r#"{"margin": 1, "style": {"margin": 2}, "logo": {"href": "a"}, "logo": null}"#)
            .unwrap();
        assert_eq!(d.style.margin, 2.0);
        assert!(d.style.logo.is_none());
        // null forgets earlier members
        let d = StyleDocument::from_json(r#"{"logo": {"href": "a", "margin": 3}, "logo": null, "logo": {}}"#).unwrap();
        assert_eq!(d.style.logo, Some(Logo::default()));
        let d = StyleDocument::from_json(r#"{"label": {"text": "A", "size": 2}, "label": null, "label": {"text": "B"}}"#).unwrap();
        assert_eq!(d.style.label, Some(Label::new("B")));
        // a present logo keeps its size; after null the default 0.2 comes back
        let d = StyleDocument::from_json(r#"{"logo": {"href": "a", "size": 0.3}, "logo": {"margin": 2}}"#).unwrap();
        let logo = d.style.logo.as_ref().unwrap();
        assert_eq!((logo.size, logo.margin, logo.href.as_deref()), (0.3, 2.0, Some("a")));
        let d = StyleDocument::from_json(r#"{"logo": {"size": 0.3}, "logo": null, "logo": {}}"#).unwrap();
        assert_eq!(d.style.logo.as_ref().unwrap().size, 0.2);
        // objects merge, paints replace
        let d = StyleDocument::from_json(
            r##"{"modules": {"shape": "dot", "color": {"type": "radial", "stops": [{"color": "#f00"}]}},
                "modules": {"size": 0.8, "color": {"stops": []}}}"##,
        )
        .unwrap();
        assert_eq!((d.style.modules.shape, d.style.modules.size), (ModuleShape::Dot, 0.8));
        assert_eq!(d.style.modules.color, Paint::Linear(LinearGradient::default()));
        // null turns styled alignment patterns off
        let d = StyleDocument::from_json(r#"{"alignment": {"frame": "circle", "ball": "dots"}, "alignment": null}"#).unwrap();
        assert_eq!(d.style.alignment, AlignmentStyle::default());
    }

    #[test]
    fn strings() {
        let d = StyleDocument::from_json(
            r#"{"svg": {"idPrefix": "\u0000x"}, "m\u006fdules\u0000junk": {"shape": "dot\u0000x"},
                "label": {"text": "ab\u0000cd", "font": "\u0000"}, "encode": {"ecc": "L\u0000x"}}"#,
        )
        .unwrap();
        assert_eq!((d.svg.id_prefix.as_str(), d.style.modules.shape), ("", ModuleShape::Dot));
        let label = d.style.label.as_ref().unwrap();
        assert_eq!((label.text.as_str(), label.font.as_str()), ("ab", ""));
        assert_eq!(d.encode.ecc, Some(Ecc::Low));
        // colors and enum values are cut too
        let d = StyleDocument::from_json(
            r##"{"modules": {"color": "#f00\u0000x"}, "border": {"color": "#00f\u0000"},
                "label": {"text": "t", "position": "top\u0000x"}, "encode": {"ecc": "L\u0000xxxxxxxxxxxxxxxxxxxxxxxxxxx"}}"##,
        )
        .unwrap();
        assert_eq!(d.style.modules.color, Paint::Solid(Color::hex(0xff0000)));
        assert_eq!(d.style.border.color, Color::hex(0x0000ff));
        assert_eq!(d.style.label.as_ref().unwrap().position, LabelPosition::Top);
        assert_eq!(d.encode.ecc, Some(Ecc::Low));
        // lone surrogates become U+FFFD; the escape after a lone high one is read on its own
        let d = StyleDocument::from_json(
            r#"{"label": {"text": "\ud800 \udc00 \ud83d\ude00 \udc00\ud800 \ud800\u0041 \uD83D\uDE00\\ud800"}}"#,
        )
        .unwrap();
        assert_eq!(d.style.label.unwrap().text, "\u{fffd} \u{fffd} \u{1f600} \u{fffd}\u{fffd} \u{fffd}A \u{1f600}\\ud800");
        // in keys too
        assert!(StyleDocument::from_json(r#"{"\ud800": 1, "x\udfff": [1]}"#).is_ok());
        // without the prescan, serde_json rejects them
        assert!(serde_json::from_str::<StyleDocument>(r#"{"label": {"text": "\ud800"}}"#).is_err());
    }

    #[test]
    fn limits() {
        let nested = |n: usize| format!(r#"{{"style": {{"x": {}{}}}}}"#, "[".repeat(n), "]".repeat(n));
        // top-level object = 1, style = 2: 62 arrays reach level 64
        assert!(StyleDocument::from_json(&nested(62)).is_ok());
        assert_eq!(StyleDocument::from_json(&nested(63)), Err(Error::Parse));
        // same limits through the serde impls (no prescan)
        assert!(serde_json::from_str::<StyleDocument>(&nested(62)).is_ok());
        assert!(serde_json::from_str::<StyleDocument>(&nested(63)).is_err());
        let deep_known = format!(r#"{{"modules": {{"color": {{"stops": [{{"x": {}{}}}]}}}}}}"#, "[".repeat(58), "]".repeat(58));
        assert!(StyleDocument::from_json(&deep_known).is_ok()); // 1+1+1+1+1 + 58 + 1 = 64
        let number = |len: usize| format!(r#"{{"x": 0.{}}}"#, "1".repeat(len - 2));
        assert!(StyleDocument::from_json(&number(400)).is_ok());
        assert_eq!(StyleDocument::from_json(&number(401)), Err(Error::Parse));
        for bad in [r#"{"margin": 1e999}"#, r#"{"future": -1e999}"#, r#"{"a": [{"b": 1e999999999999}]}"#] {
            assert_eq!(StyleDocument::from_json(bad), Err(Error::Parse), "{bad}");
            assert!(serde_json::from_str::<StyleDocument>(bad).is_err(), "{bad}");
        }
        // strings that look like numbers are not numbers
        assert!(StyleDocument::from_json(r#"{"x": "1e999", "y": "[[[[", "\"1e999": 1}"#).is_ok());
        assert!(StyleDocument::from_json(r#"{"margin": 1e-999}"#).is_ok());
    }

    #[test]
    fn gradients() {
        let d = StyleDocument::from_json(
            r##"{"modules": {"color": {"type": "radial", "cx": 0.3, "stops": [{"offset": 0.2, "color": "#ff000080", "x": [1]}]}}}"##,
        )
        .unwrap();
        match &d.style.modules.color {
            Paint::Radial(g) => {
                assert_eq!((g.cx, g.cy, g.r), (0.3, 0.5, 0.75));
                assert_eq!(g.stops, [Stop { offset: 0.2, color: Color(0xff000080) }]);
            }
            p => panic!("{p:?}"),
        }
        let many = r##"{"modules": {"color": {"stops": [{},{},{},{},{},{},{},{},{},{"color": "#fff"}]}}}"##;
        match StyleDocument::from_json(many).unwrap().style.modules.color {
            Paint::Linear(g) => assert_eq!((g.angle, g.stops.len()), (0.0, 8)),
            p => panic!("{p:?}"),
        }
    }

    #[test]
    fn encode_options() {
        let d = StyleDocument::from_json(r#"{"encode": {"ecc": "Q", "minVersion": 2.9, "maxVersion": 50, "mask": -1, "boostEcc": false, "eci": true}}"#).unwrap();
        assert_eq!(
            d.encode,
            EncodeOptions { ecc: Some(Ecc::Quartile), min_version: 2, max_version: 50, mask: None, boost_ecc: false, eci: true }
        );
        let d = StyleDocument::from_json(r#"{"encode": {"mask": 3}}"#).unwrap();
        assert_eq!(d.encode.mask, Some(3));
        let d = StyleDocument::from_json(r#"{"encode": {"mask": -4}}"#).unwrap();
        assert_eq!(d.encode.mask, Some(255));
        assert_eq!(d.encode(b"x"), Err(Error::InvalidArgument));
        let d = StyleDocument::from_json(r#"{"encode": {"minVersion": 1e100, "maxVersion": -1e100, "mask": 1e100}}"#).unwrap();
        assert_eq!((d.encode.min_version, d.encode.max_version, d.encode.mask), (255, 0, Some(255)));
        assert_eq!(d.encode(b"x"), Err(Error::InvalidArgument));
    }

    #[test]
    fn errors() {
        for bad in [
            r#"[]"#,
            r#"{"margin": "4"}"#,
            r#"{"modules": null}"#,
            r#"{"modules": {"shape": "hexagon"}}"#,
            r#"{"modules": {"shape": null}}"#,
            r#"{"eyes": {"frame": null}}"#,
            r#"{"modules": {"color": "red"}}"#,
            r#"{"modules": {"color": {"type": "conic"}}}"#,
            r#"{"border": {"color": {"stops": []}}}"#,
            r#"{"encode": {"ecc": "X"}}"#,
            r#"{"encode": {"ecc": "LM"}}"#,
            r#"{"encode": null}"#,
            r#"{"logo": {"shape": "star"}}"#,
            r#"{"label": {"position": "left"}}"#,
            r#"{"label": {"bold": 1}}"#,
            r#"{"svg": {"idPrefix": 3}}"#,
        ] {
            assert_eq!(StyleDocument::from_json(bad), Err(Error::Parse), "{bad}");
        }
    }

    #[test]
    fn long_unknown_keys_are_ignored() {
        let long = "k".repeat(200);
        let json = format!(
            r##"{{"{long}": 1, "style": {{"{long}": [1], "modules": {{"{long}": null, "shape": "dot",
                "color": {{"{long}": 2, "stops": [{{"{long}": {{"a": 1}}, "color": "#f00"}}]}}}}}},
                "encode": {{"{long}": true}}, "svg": {{"{long}": "x"}}}}"##
        );
        let d = StyleDocument::from_json(&json).unwrap();
        assert_eq!(d.style.modules.shape, ModuleShape::Dot);
        assert_eq!(d.style.modules.color.stops()[0].color, Color::hex(0xff0000));
    }

    #[test]
    fn numbers_are_nearest_double() {
        for s in [
            "0.30000000000000004",
            "2.2250738585072011e-308",
            "9007199254740993",
            "1.00000000000000011102230246251565404236316680908203125",
            "123456789012345678901234567890",
            "4.35",
            "-0.0625",
            "1e23",
            "8.98846567431158e307",
            "0.1e-30",
        ] {
            let d = StyleDocument::from_json(&format!(r#"{{"margin": {s}}}"#)).unwrap();
            assert_eq!(d.style.margin.to_bits(), s.parse::<f64>().unwrap().to_bits(), "{s}");
        }
    }

    #[test]
    fn integer_fields_truncate_toward_zero() {
        let d = StyleDocument::from_json(r#"{"encode": {"minVersion": 3.99, "maxVersion": 7.5, "mask": 6.9}}"#).unwrap();
        assert_eq!((d.encode.min_version, d.encode.max_version, d.encode.mask), (3, 7, Some(6)));
        let d = StyleDocument::from_json(r#"{"encode": {"mask": -1.7}}"#).unwrap();
        assert_eq!(d.encode.mask, None); // (int)-1.7 == -1: automatic
        let d = StyleDocument::from_json(r#"{"encode": {"minVersion": -0.5}}"#).unwrap();
        assert_eq!(d.encode.min_version, 0); // invalid at encode time
        assert_eq!(d.encode(b"x"), Err(Error::InvalidArgument));
    }

    #[test]
    fn bare_style_and_values() {
        let s: Style = serde_json::from_str(r#"{"modules": {"shape": "liquid"}, "future": {"a": [1, null]}}"#).unwrap();
        assert_eq!(s.modules.shape, ModuleShape::Liquid);
        let c: Color = serde_json::from_str("\"#abc\"").unwrap();
        assert_eq!(c, Color::hex(0xaabbcc));
        let e: Ecc = serde_json::from_str("\"H\"").unwrap();
        assert_eq!(e, Ecc::High);
    }
}
