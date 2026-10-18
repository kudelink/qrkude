# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Payload builders: Wi-Fi, e-mail, SMS, phone, geo location, vCard, event.

A port of ``core/src/qrk_payload.c`` (SPEC section 12). ``None`` and empty
strings are equivalent for every optional field.
"""

from __future__ import annotations

import math
from typing import Optional

__all__ = ["wifi", "email", "sms", "tel", "geo", "vcard", "event"]

_WIFI_SPECIAL = '\\;,:"'
_VALUE_SPECIAL = "\\;,"
_UNRESERVED = frozenset(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~")


def _escaped(s: Optional[str], special: str) -> str:
    """Backslash-escapes every character of ``special``; newlines become ``\\n``."""
    if not s:
        return ""
    out = []
    for ch in s:
        if ch == "\n":
            out.append("\\n")
            continue
        if ch in special:
            out.append("\\")
        out.append(ch)
    return "".join(out)


def _percent(s: str) -> str:
    return "".join(chr(b) if b in _UNRESERVED else "%%%02X" % b for b in s.encode("utf-8"))


def wifi(ssid: str, password: Optional[str] = None, auth: Optional[str] = "WPA",
         hidden: bool = False) -> str:
    """``WIFI:T:{auth};S:{ssid};P:{password};;`` (auth: "WPA", "WEP", "nopass" or None)."""
    out = "WIFI:"
    if auth:
        out += "T:" + auth + ";"
    out += "S:" + _escaped(ssid, _WIFI_SPECIAL) + ";"
    if password and auth != "nopass":
        out += "P:" + _escaped(password, _WIFI_SPECIAL) + ";"
    if hidden:
        out += "H:true;"
    return out + ";"


def email(to: str, subject: Optional[str] = None, body: Optional[str] = None) -> str:
    """``mailto:`` URI with percent-encoded subject and body."""
    out = "mailto:" + (to or "")
    sep = "?"
    if subject:
        out += sep + "subject=" + _percent(subject)
        sep = "&"
    if body:
        out += sep + "body=" + _percent(body)
    return out


def sms(number: str, message: Optional[str] = None) -> str:
    """``SMSTO:{number}:{message}``."""
    out = "SMSTO:" + (number or "")
    if message:
        out += ":" + message
    return out


def tel(number: str) -> str:
    """``tel:{number}``."""
    return "tel:" + (number or "")


def _fixed6(v: float) -> str:
    """Same rounding rule as format_number, with 6 decimals."""
    n = math.floor(float(v) * 1000000.0 + 0.5)
    neg = n < 0
    if neg:
        n = -n
    ip, fp = divmod(n, 1000000)
    out = ("-" if neg else "") + str(ip)
    if fp:
        out += "." + ("%06d" % fp).rstrip("0")
    return out


def geo(latitude: float, longitude: float) -> str:
    """``geo:{lat},{lon}`` with up to 6 decimals; latitude is clamped to
    [-90, 90] and longitude to [-180, 180]."""
    return "geo:" + _fixed6(_clamp(latitude, -90.0, 90.0)) + "," + \
        _fixed6(_clamp(longitude, -180.0, 180.0))


def _clamp(v: float, lo: float, hi: float) -> float:
    """NaN clamps to lo, like the reference."""
    v = float(v)
    if not v >= lo:
        return lo
    return hi if v > hi else v


def _vline(key: str, value: Optional[str], always: bool = False) -> str:
    if not always and not value:
        return ""
    return key + ":" + _escaped(value, _VALUE_SPECIAL) + "\n"


def vcard(
    first_name: Optional[str] = None,
    last_name: Optional[str] = None,
    organization: Optional[str] = None,
    title: Optional[str] = None,
    phone: Optional[str] = None,
    email: Optional[str] = None,
    url: Optional[str] = None,
    address: Optional[str] = None,
    note: Optional[str] = None,
) -> str:
    """vCard 3.0 contact."""
    first = _escaped(first_name, _VALUE_SPECIAL)
    last = _escaped(last_name, _VALUE_SPECIAL)
    out = "BEGIN:VCARD\nVERSION:3.0\nN:" + last + ";" + first + ";;;\nFN:" + first
    if first_name and last_name:
        out += " "
    out += last + "\n"
    out += _vline("ORG", organization)
    out += _vline("TITLE", title)
    out += _vline("TEL", phone)
    out += _vline("EMAIL", email)
    out += _vline("URL", url)
    if address:
        out += "ADR:;;" + _escaped(address, _VALUE_SPECIAL) + ";;;;\n"
    out += _vline("NOTE", note)
    return out + "END:VCARD"


def event(
    summary: Optional[str],
    start: Optional[str] = None,
    end: Optional[str] = None,
    location: Optional[str] = None,
    description: Optional[str] = None,
) -> str:
    """iCalendar ``VEVENT``; ``start``/``end`` like ``"20261017T120000Z"``."""
    out = "BEGIN:VEVENT\n"
    out += _vline("SUMMARY", summary, True)
    out += _vline("DTSTART", start)
    out += _vline("DTEND", end)
    out += _vline("LOCATION", location)
    out += _vline("DESCRIPTION", description)
    return out + "END:VEVENT"
