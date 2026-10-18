// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Payload builders (SPEC §12): Wi-Fi, e-mail, SMS, telephone, geo, vCard
//! and calendar event. Empty strings mean "absent".
//!
//! ```
//! use qrkude::payload;
//! assert_eq!(payload::wifi("Kude", "secret", "WPA", false), "WIFI:T:WPA;S:Kude;P:secret;;");
//! assert_eq!(payload::geo(40.4168, -3.7038), "geo:40.4168,-3.7038");
//! ```

use alloc::string::String;

use crate::layout::clamp;
use crate::math;

/// Backslash-escapes every character of `special`; newlines become `\n`.
fn put_escaped(out: &mut String, s: &str, special: &str) {
    for c in s.chars() {
        if c == '\n' {
            out.push_str("\\n");
            continue;
        }
        if special.contains(c) {
            out.push('\\');
        }
        out.push(c);
    }
}

fn put_percent(out: &mut String, s: &str) {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    for &c in s.as_bytes() {
        if c.is_ascii_alphanumeric() || matches!(c, b'-' | b'_' | b'.' | b'~') {
            out.push(c as char);
        } else {
            out.push('%');
            out.push(HEX[(c >> 4) as usize] as char);
            out.push(HEX[(c & 15) as usize] as char);
        }
    }
}

/// Wi-Fi network: `WIFI:T:{auth};S:{ssid};P:{password};H:true;;`.
/// `auth` is `"WPA"`, `"WEP"`, `"nopass"` or empty; the password is omitted
/// when empty or with `nopass`.
pub fn wifi(ssid: &str, password: &str, auth: &str, hidden: bool) -> String {
    const SPECIAL: &str = "\\;,:\"";
    let mut b = String::from("WIFI:");
    if !auth.is_empty() {
        b.push_str("T:");
        b.push_str(auth);
        b.push(';');
    }
    b.push_str("S:");
    put_escaped(&mut b, ssid, SPECIAL);
    b.push(';');
    if !password.is_empty() && auth != "nopass" {
        b.push_str("P:");
        put_escaped(&mut b, password, SPECIAL);
        b.push(';');
    }
    if hidden {
        b.push_str("H:true;");
    }
    b.push(';');
    b
}

/// `mailto:` link with percent-encoded subject and body.
pub fn email(to: &str, subject: &str, body: &str) -> String {
    let mut b = String::from("mailto:");
    b.push_str(to);
    let mut sep = '?';
    if !subject.is_empty() {
        b.push(sep);
        b.push_str("subject=");
        put_percent(&mut b, subject);
        sep = '&';
    }
    if !body.is_empty() {
        b.push(sep);
        b.push_str("body=");
        put_percent(&mut b, body);
    }
    b
}

/// `SMSTO:{number}:{message}`.
pub fn sms(number: &str, message: &str) -> String {
    let mut b = String::from("SMSTO:");
    b.push_str(number);
    if !message.is_empty() {
        b.push(':');
        b.push_str(message);
    }
    b
}

/// `tel:{number}`.
pub fn tel(number: &str) -> String {
    let mut b = String::from("tel:");
    b.push_str(number);
    b
}

/// Same rounding rule as the SVG number format, with 6 decimals.
fn put_fixed6(out: &mut String, v: f64) {
    let n = math::floor(v * 1000000.0 + 0.5) as i64;
    if n < 0 {
        out.push('-');
    }
    let n = n.unsigned_abs();
    let ip = n / 1_000_000;
    let fp = n % 1_000_000;
    let mut digits = [0u8; 24];
    let mut k = 0;
    let mut i = ip;
    loop {
        digits[k] = b'0' + (i % 10) as u8;
        k += 1;
        i /= 10;
        if i == 0 {
            break;
        }
    }
    while k > 0 {
        k -= 1;
        out.push(digits[k] as char);
    }
    if fp != 0 {
        let mut frac = [0u8; 6];
        let mut f = fp;
        for d in frac.iter_mut().rev() {
            *d = b'0' + (f % 10) as u8;
            f /= 10;
        }
        let mut len = 6;
        while frac[len - 1] == b'0' {
            len -= 1;
        }
        out.push('.');
        for &d in &frac[..len] {
            out.push(d as char);
        }
    }
}

/// `geo:{lat},{lon}` with up to 6 decimals; the latitude is clamped to
/// `[-90, 90]` and the longitude to `[-180, 180]` (NaN to the lower bound).
pub fn geo(latitude: f64, longitude: f64) -> String {
    let mut b = String::from("geo:");
    put_fixed6(&mut b, clamp(latitude, -90.0, 90.0));
    b.push(',');
    put_fixed6(&mut b, clamp(longitude, -180.0, 180.0));
    b
}

const VSPECIAL: &str = "\\;,";

fn vline(b: &mut String, key: &str, value: &str, always: bool) {
    if !always && value.is_empty() {
        return;
    }
    b.push_str(key);
    b.push(':');
    put_escaped(b, value, VSPECIAL);
    b.push('\n');
}

/// Contact card fields for [`vcard`]. Empty fields are omitted.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VCard<'a> {
    /// Given name.
    pub first_name: &'a str,
    /// Family name.
    pub last_name: &'a str,
    /// `ORG`.
    pub organization: &'a str,
    /// `TITLE`.
    pub title: &'a str,
    /// `TEL`.
    pub phone: &'a str,
    /// `EMAIL`.
    pub email: &'a str,
    /// `URL`.
    pub url: &'a str,
    /// Street address (`ADR` street field).
    pub address: &'a str,
    /// `NOTE`.
    pub note: &'a str,
}

/// vCard 3.0 contact.
///
/// ```
/// use qrkude::payload::{vcard, VCard};
/// let card = VCard { first_name: "Ana", last_name: "García", ..Default::default() };
/// assert_eq!(vcard(&card), "BEGIN:VCARD\nVERSION:3.0\nN:García;Ana;;;\nFN:Ana García\nEND:VCARD");
/// ```
pub fn vcard(c: &VCard<'_>) -> String {
    let mut b = String::from("BEGIN:VCARD\nVERSION:3.0\nN:");
    put_escaped(&mut b, c.last_name, VSPECIAL);
    b.push(';');
    put_escaped(&mut b, c.first_name, VSPECIAL);
    b.push_str(";;;\nFN:");
    put_escaped(&mut b, c.first_name, VSPECIAL);
    if !c.first_name.is_empty() && !c.last_name.is_empty() {
        b.push(' ');
    }
    put_escaped(&mut b, c.last_name, VSPECIAL);
    b.push('\n');
    vline(&mut b, "ORG", c.organization, false);
    vline(&mut b, "TITLE", c.title, false);
    vline(&mut b, "TEL", c.phone, false);
    vline(&mut b, "EMAIL", c.email, false);
    vline(&mut b, "URL", c.url, false);
    if !c.address.is_empty() {
        b.push_str("ADR:;;");
        put_escaped(&mut b, c.address, VSPECIAL);
        b.push_str(";;;;\n");
    }
    vline(&mut b, "NOTE", c.note, false);
    b.push_str("END:VCARD");
    b
}

/// Calendar event fields for [`event`]. Empty fields are omitted (except
/// the summary line, which is always written).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Event<'a> {
    /// `SUMMARY`.
    pub summary: &'a str,
    /// `DTSTART`, e.g. `"20261017T120000Z"`.
    pub start: &'a str,
    /// `DTEND`.
    pub end: &'a str,
    /// `LOCATION`.
    pub location: &'a str,
    /// `DESCRIPTION`.
    pub description: &'a str,
}

/// iCalendar `VEVENT`.
pub fn event(e: &Event<'_>) -> String {
    let mut b = String::from("BEGIN:VEVENT\n");
    vline(&mut b, "SUMMARY", e.summary, true);
    vline(&mut b, "DTSTART", e.start, false);
    vline(&mut b, "DTEND", e.end, false);
    vline(&mut b, "LOCATION", e.location, false);
    vline(&mut b, "DESCRIPTION", e.description, false);
    b.push_str("END:VEVENT");
    b
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn escapes() {
        assert_eq!(wifi("a;b", "", "nopass", true), "WIFI:T:nopass;S:a\\;b;H:true;;");
        assert_eq!(email("x@y.z", "", "a b"), "mailto:x@y.z?body=a%20b");
        assert_eq!(sms("1", ""), "SMSTO:1");
        assert_eq!(tel("+1"), "tel:+1");
        assert_eq!(geo(-0.0000004, 1.0000005), "geo:0,1.000001");
        assert_eq!(geo(95.5, -200.0), "geo:90,-180");
        assert_eq!(geo(f64::NAN, f64::INFINITY), "geo:-90,180");
        assert_eq!(event(&Event { summary: "a,b", ..Default::default() }), "BEGIN:VEVENT\nSUMMARY:a\\,b\nEND:VEVENT");
    }
}
