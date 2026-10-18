// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Conformance vectors from `spec/vectors/` (SPEC §13): every case must be
//! reproduced byte for byte.

mod common;

use common::{document, encode_options, first_diff, render_doc_value, vectors};
use qrkude::payload::{self, Event, VCard};
use qrkude::{encode_text, lint, logo_damage, to_svg, Error, StyleDocument};
use serde_json::Value;

/// Reads a raw JSON document with the crate's reader (`None` without the
/// `json` feature: such cases test the reader itself and are skipped).
#[cfg(feature = "json")]
fn parse_raw(raw: &str) -> Option<Result<StyleDocument, Error>> {
    Some(StyleDocument::from_json(raw))
}

#[cfg(not(feature = "json"))]
fn parse_raw(_raw: &str) -> Option<Result<StyleDocument, Error>> {
    None
}

fn s<'a>(args: &'a Value, key: &str) -> &'a str {
    args.get(key).and_then(Value::as_str).unwrap_or("")
}

#[test]
fn encode_vectors() {
    let Some(cases) = vectors("encode") else { return };
    assert!(!cases.is_empty());
    let mut failures = Vec::new();
    for case in &cases {
        let text = case["text"].as_str().unwrap();
        let opts = encode_options(case.get("encode"));
        let label = format!("{:?}/{}", &text[..text.len().min(24)], case["encode"]);
        match encode_text(text, &opts) {
            Err(e) => {
                if case.get("error").and_then(Value::as_str) != Some(e.as_str()) {
                    failures.push(format!("{label}: unexpected error {e}"));
                }
            }
            Ok(code) => {
                if case.get("error").is_some() {
                    failures.push(format!("{label}: expected an error"));
                    continue;
                }
                let got = (code.version() as u64, code.ecc().letter().to_string(), code.mask() as u64, code.mode().name());
                let want = (
                    case["version"].as_u64().unwrap(),
                    case["ecc"].as_str().unwrap().to_string(),
                    case["mask"].as_u64().unwrap(),
                    case["mode"].as_str().unwrap(),
                );
                if got != want {
                    failures.push(format!("{label}: got {got:?}, want {want:?}"));
                    continue;
                }
                let n = code.size() as i32;
                let rows: Vec<String> = (0..n)
                    .map(|y| (0..n).map(|x| if code.is_dark(x, y) { '1' } else { '0' }).collect())
                    .collect();
                let want_rows: Vec<&str> = case["matrix"].as_array().unwrap().iter().map(|r| r.as_str().unwrap()).collect();
                if rows != want_rows {
                    failures.push(format!("{label}: matrix differs"));
                }
            }
        }
    }
    assert!(failures.is_empty(), "{} of {} encode vectors failed:\n{}", failures.len(), cases.len(), failures.join("\n"));
    println!("{} encode vectors ok", cases.len());
}

#[test]
fn data_too_long_message() {
    assert_eq!(Error::DataTooLong.to_string(), "data too long");
}

#[test]
fn payload_vectors() {
    let Some(cases) = vectors("payloads") else { return };
    assert!(!cases.is_empty());
    for case in &cases {
        let a = &case["args"];
        let got = match case["type"].as_str().unwrap() {
            "wifi" => payload::wifi(s(a, "ssid"), s(a, "password"), s(a, "auth"), a["hidden"].as_bool().unwrap()),
            "email" => payload::email(s(a, "to"), s(a, "subject"), s(a, "body")),
            "sms" => payload::sms(s(a, "number"), s(a, "message")),
            "tel" => payload::tel(s(a, "number")),
            "geo" => payload::geo(a["latitude"].as_f64().unwrap(), a["longitude"].as_f64().unwrap()),
            "vcard" => payload::vcard(&VCard {
                first_name: s(a, "firstName"),
                last_name: s(a, "lastName"),
                organization: s(a, "organization"),
                title: s(a, "title"),
                phone: s(a, "phone"),
                email: s(a, "email"),
                url: s(a, "url"),
                address: s(a, "address"),
                note: s(a, "note"),
            }),
            "event" => payload::event(&Event {
                summary: s(a, "summary"),
                start: s(a, "start"),
                end: s(a, "end"),
                location: s(a, "location"),
                description: s(a, "description"),
            }),
            t => panic!("unknown payload type {t}"),
        };
        assert_eq!(got, case["output"].as_str().unwrap(), "{case}");
    }
    println!("{} payload vectors ok", cases.len());
}

#[test]
fn render_vectors() {
    let Some(cases) = vectors("render") else { return };
    assert!(!cases.is_empty());
    let mut failures = Vec::new();
    let mut skipped = 0;
    for case in &cases {
        let name = case["name"].as_str().unwrap();
        let text = case["text"].as_str().unwrap();
        let want_error = case.get("error").and_then(Value::as_str);

        let parsed = if let Some(raw) = case.get("json").and_then(Value::as_str) {
            // raw text: duplicate keys, limits and errors (SPEC §13)
            match parse_raw(raw) {
                Some(result) => result,
                None => {
                    skipped += 1;
                    continue;
                }
            }
        } else {
            let doc_value = render_doc_value(case);
            let doc = document(&doc_value);
            #[cfg(feature = "json")]
            {
                let parsed = StyleDocument::from_json(&doc_value.to_string());
                if parsed.as_ref() != Ok(&doc) {
                    failures.push(format!("{name}: JSON reader disagrees: {parsed:?}"));
                }
            }
            Ok(doc)
        };

        // `doc.encode` applies the ECC default rule (H with a logo, else M)
        let result = parsed.and_then(|doc| doc.encode(text.as_bytes()).map(|code| (doc, code)));
        let (doc, code) = match (result, want_error) {
            (Err(e), Some(want)) if e.as_str() == want => continue,
            (Err(e), want) => {
                failures.push(format!("{name}: error {e:?}, want {want:?}"));
                continue;
            }
            (Ok(_), Some(want)) => {
                failures.push(format!("{name}: expected error {want:?}"));
                continue;
            }
            (Ok(ok), None) => ok,
        };

        let got = (code.version() as u64, code.ecc().letter().to_string(), code.mask() as u64);
        let want =
            (case["version"].as_u64().unwrap(), case["ecc"].as_str().unwrap().to_string(), case["mask"].as_u64().unwrap());
        if got != want {
            failures.push(format!("{name}: got {got:?}, want {want:?}"));
        }

        let svg = to_svg(&code, &doc.style, &doc.svg);
        let want_svg = case["output"].as_str().unwrap();
        if svg != want_svg {
            failures.push(format!("{name}: SVG differs {}", first_diff(&svg, want_svg)));
        }

        let flags: Vec<&str> = lint(&code, &doc.style).iter().map(|f| f.name()).collect();
        let want_flags: Vec<&str> = case["lint"].as_array().unwrap().iter().map(|f| f.as_str().unwrap()).collect();
        if flags != want_flags {
            failures.push(format!("{name}: lint {flags:?}, want {want_flags:?}"));
        }

        let damage = logo_damage(&code, &doc.style);
        let want_damage = case.get("logoDamage").and_then(Value::as_f64).unwrap_or(0.0);
        if (damage - want_damage).abs() > 1e-6 {
            failures.push(format!("{name}: logoDamage {damage}, want {want_damage}"));
        }
    }
    assert!(failures.is_empty(), "{} of {} render vectors failed:\n{}", failures.len(), cases.len(), failures.join("\n"));
    if skipped > 0 {
        println!("{} render vectors ok, {skipped} raw-JSON cases skipped (needs the `json` feature)", cases.len() - skipped);
    } else {
        println!("{} render vectors ok", cases.len());
    }
}
