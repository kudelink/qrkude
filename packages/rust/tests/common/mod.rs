// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Test helpers: loads the conformance vectors and converts style documents
//! from `serde_json::Value` without the crate's `json` feature, so that the
//! vectors are also checked in `--no-default-features` builds. With the
//! feature enabled, the tests compare this conversion with the real reader.
//!
//! The vectors live in the QRKude repository (`spec/vectors`), outside this
//! crate, so these tests are not part of the published package.
//! `QRKUDE_VECTORS_DIR` points them at another copy. When the directory is
//! missing (e.g. a copy of this directory outside the repository) the vector
//! tests are skipped with a message (visible with `--nocapture`), unless
//! `QRKUDE_REQUIRE_VECTORS` is set to a value other than `0`: then they fail.

#![allow(dead_code)]

use qrkude::{
    Color, Ecc, EncodeOptions, EyeShape, Label, LabelPosition, LinearGradient, Logo, LogoShape, ModuleShape, Paint,
    RadialGradient, Stop, Style, StyleDocument, SvgOptions,
};
use serde_json::Value;

fn vectors_dir() -> std::path::PathBuf {
    match std::env::var_os("QRKUDE_VECTORS_DIR") {
        Some(dir) if !dir.is_empty() => dir.into(),
        _ => std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../spec/vectors"),
    }
}

/// The cases of `spec/vectors/{name}.json`, or `None` (test skipped) when the
/// vectors are not available and not required.
pub(crate) fn vectors(name: &str) -> Option<Vec<Value>> {
    let path = vectors_dir().join(format!("{name}.json"));
    let text = match std::fs::read_to_string(&path) {
        Ok(text) => text,
        Err(e) => {
            let required = std::env::var("QRKUDE_REQUIRE_VECTORS").map_or(false, |v| !v.is_empty() && v != "0");
            if required {
                panic!("{}: {e} (QRKUDE_REQUIRE_VECTORS is set)", path.display());
            }
            // shown with `cargo test -- --nocapture` (the harness captures it)
            eprintln!(
                "SKIPPED {name} vectors: not found at {} ({e}). They live in the QRKude repository \
                 (spec/vectors); set QRKUDE_VECTORS_DIR to run them, QRKUDE_REQUIRE_VECTORS=1 to make this an error.",
                path.display()
            );
            return None;
        }
    };
    match serde_json::from_str(&text).unwrap() {
        Value::Array(cases) => Some(cases),
        _ => panic!("{}: expected an array", path.display()),
    }
}

fn num(v: &Value) -> f64 {
    v.as_f64().unwrap_or_else(|| panic!("number expected: {v}"))
}

fn color(v: &Value) -> Color {
    match v {
        Value::Null => Color::TRANSPARENT,
        Value::String(s) => Color::parse(s).unwrap_or_else(|| panic!("bad color {s}")),
        _ => panic!("color expected: {v}"),
    }
}

fn paint(v: &Value) -> Paint {
    match v {
        Value::Null => Paint::None,
        Value::String(_) => Paint::Solid(color(v)),
        Value::Object(m) => {
            let stops: Vec<Stop> = m
                .get("stops")
                .and_then(Value::as_array)
                .map(|a| {
                    a.iter()
                        .take(qrkude::MAX_STOPS)
                        .map(|s| Stop {
                            offset: s.get("offset").map(num).unwrap_or(0.0),
                            color: s.get("color").map(color).unwrap_or_default(),
                        })
                        .collect()
                })
                .unwrap_or_default();
            if m.get("type").and_then(Value::as_str) == Some("radial") {
                Paint::Radial(RadialGradient {
                    cx: m.get("cx").map(num).unwrap_or(0.5),
                    cy: m.get("cy").map(num).unwrap_or(0.5),
                    r: m.get("r").map(num).unwrap_or(0.75),
                    stops,
                })
            } else {
                Paint::Linear(LinearGradient { angle: m.get("angle").map(num).unwrap_or(0.0), stops })
            }
        }
        _ => panic!("paint expected: {v}"),
    }
}

fn opt_string(v: &Value) -> String {
    v.as_str().unwrap_or("").to_string()
}

fn eye(v: &Value) -> EyeShape {
    EyeShape::from_name(v.as_str().unwrap()).unwrap()
}

fn apply_style_key(s: &mut Style, key: &str, v: &Value) {
    match key {
        "margin" => s.margin = num(v),
        "modules" => {
            for (k, v) in v.as_object().unwrap() {
                match k.as_str() {
                    "shape" => s.modules.shape = ModuleShape::from_name(v.as_str().unwrap()).unwrap(),
                    "size" => s.modules.size = num(v),
                    "roundness" => s.modules.roundness = num(v),
                    "color" => s.modules.color = paint(v),
                    _ => {}
                }
            }
        }
        "eyes" => {
            for (k, v) in v.as_object().unwrap() {
                match k.as_str() {
                    "frame" => s.eyes.frame = eye(v),
                    "ball" => s.eyes.ball = eye(v),
                    "frameColor" => s.eyes.frame_color = paint(v),
                    "ballColor" => s.eyes.ball_color = paint(v),
                    _ => {}
                }
            }
        }
        "alignment" => {
            if v.is_null() {
                s.alignment = Default::default();
            }
            if let Some(m) = v.as_object() {
                for (k, v) in m {
                    let shape = if v.is_null() { None } else { Some(eye(v)) };
                    match k.as_str() {
                        "frame" => s.alignment.frame = shape,
                        "ball" => s.alignment.ball = shape,
                        _ => {}
                    }
                }
            }
        }
        "background" => {
            for (k, v) in v.as_object().unwrap() {
                match k.as_str() {
                    "color" => s.background.color = paint(v),
                    "radius" => s.background.radius = num(v),
                    _ => {}
                }
            }
        }
        "border" => {
            for (k, v) in v.as_object().unwrap() {
                match k.as_str() {
                    "width" => s.border.width = num(v),
                    "color" => s.border.color = color(v),
                    _ => {}
                }
            }
        }
        "logo" => match v.as_object() {
            None => s.logo = None, // null resets to the defaults (absent)
            Some(m) => {
                let l = s.logo.get_or_insert_with(Logo::default);
                if !(l.size > 0.0) {
                    l.size = 0.2;
                }
                for (k, v) in m {
                    match k.as_str() {
                        "href" => l.href = v.as_str().map(str::to_string),
                        "size" => l.size = num(v),
                        "margin" => l.margin = num(v),
                        "excavate" => l.excavate = v.as_bool().unwrap(),
                        "background" => l.background = color(v),
                        "shape" => l.shape = LogoShape::from_name(v.as_str().unwrap()).unwrap(),
                        _ => {}
                    }
                }
            }
        },
        "label" => match v.as_object() {
            None => s.label = None,
            Some(m) => {
                let l = s.label.get_or_insert_with(Label::default);
                for (k, v) in m {
                    match k.as_str() {
                        "text" => l.text = opt_string(v),
                        "font" => l.font = opt_string(v),
                        "size" => l.size = num(v),
                        "color" => l.color = color(v),
                        "bold" => l.bold = v.as_bool().unwrap(),
                        "position" => l.position = LabelPosition::from_name(v.as_str().unwrap()).unwrap(),
                        _ => {}
                    }
                }
            }
        },
        _ => {}
    }
}

pub(crate) fn encode_options(v: Option<&Value>) -> EncodeOptions {
    let mut o = EncodeOptions::default();
    if let Some(m) = v.and_then(Value::as_object) {
        for (k, v) in m {
            match k.as_str() {
                "ecc" => o.ecc = Ecc::from_letter(v.as_str().unwrap().chars().next().unwrap()),
                "minVersion" => o.min_version = num(v).clamp(0.0, 255.0) as u8,
                "maxVersion" => o.max_version = num(v).clamp(0.0, 255.0) as u8,
                "mask" => o.mask = if num(v) as i32 == -1 { None } else { Some(num(v).clamp(0.0, 255.0) as u8) },
                "boostEcc" => o.boost_ecc = v.as_bool().unwrap(),
                "eci" => o.eci = v.as_bool().unwrap(),
                _ => {}
            }
        }
    }
    o
}

/// Converts `{"encode", "style", "svg"}` (plus top-level style keys).
pub(crate) fn document(v: &Value) -> StyleDocument {
    let mut doc = StyleDocument::default();
    for (k, v) in v.as_object().unwrap() {
        match k.as_str() {
            "encode" => doc.encode = encode_options(Some(v)),
            "svg" => {
                let mut svg = SvgOptions::default();
                for (k, v) in v.as_object().unwrap() {
                    match k.as_str() {
                        "width" => svg.width = num(v),
                        "idPrefix" => svg.id_prefix = opt_string(v),
                        _ => {}
                    }
                }
                doc.svg = svg;
            }
            "style" => {
                for (k, v) in v.as_object().unwrap() {
                    apply_style_key(&mut doc.style, k, v);
                }
            }
            _ => apply_style_key(&mut doc.style, k, v),
        }
    }
    doc
}

/// The `{encode, style, svg}` part of a render vector.
pub(crate) fn render_doc_value(case: &Value) -> Value {
    let mut m = serde_json::Map::new();
    for key in ["encode", "style", "svg"] {
        if let Some(v) = case.get(key) {
            m.insert(key.to_string(), v.clone());
        }
    }
    Value::Object(m)
}

/// First differing position, for readable failures.
pub(crate) fn first_diff(a: &str, b: &str) -> String {
    let i = a.bytes().zip(b.bytes()).position(|(x, y)| x != y).unwrap_or(a.len().min(b.len()));
    let lo = i.saturating_sub(60);
    let clip = |s: &str| s.get(lo..(i + 60).min(s.len())).unwrap_or("").to_string();
    format!("at byte {i}:\n  got:  {}\n  want: {}", clip(a), clip(b))
}
