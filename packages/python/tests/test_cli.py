# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Command line interface tests."""

from __future__ import annotations

import io
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest

import qrkude
from qrkude.cli import main

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None  # type: ignore[assignment]

try:
    import zxingcpp
except ImportError:  # pragma: no cover
    zxingcpp = None

URL = "https://kudelink.com"
needs_pil = pytest.mark.skipif(Image is None, reason="Pillow not installed")


def _decode(path: Path) -> list:
    img = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", img.size, "white")
    bg.alpha_composite(img)
    gray = bg.convert("L")
    try:
        results = zxingcpp.read_barcodes(gray)
    except TypeError:  # older Pillow + zxing-cpp combinations need an array
        import numpy

        results = zxingcpp.read_barcodes(numpy.asarray(gray))
    return [r.text for r in results]


def test_svg_to_stdout(capsysbinary: Any) -> None:
    assert main([URL]) == 0
    out = capsysbinary.readouterr().out.decode()
    assert out == qrkude.to_svg(qrkude.encode(URL), qrkude.Style())


def test_svg_file_with_style_flags(tmp_path: Path) -> None:
    out = tmp_path / "qr.svg"
    assert main([URL, "-o", str(out), "--shape", "fluid", "--eye-frame", "circle",
                 "--eye-ball", "circle", "--color", "#1d3557", "--background", "#fff8e7",
                 "--margin", "2", "--width", "300"]) == 0
    style = qrkude.Style.from_json({
        "margin": 2, "modules": {"shape": "fluid", "color": "#1d3557"},
        "eyes": {"frame": "circle", "ball": "circle"}, "background": {"color": "#fff8e7"}})
    expected = qrkude.to_svg(qrkude.encode(URL), style, qrkude.SvgOptions(width=300))
    assert out.read_text(encoding="utf-8") == expected


def test_style_file_and_json_overrides(tmp_path: Path) -> None:
    doc = {"encode": {"ecc": "Q"}, "style": {"modules": {"shape": "dot"}, "label": {"text": "HI"}},
           "svg": {"idPrefix": "x"}}
    path = tmp_path / "style.json"
    path.write_text(json.dumps(doc), encoding="utf-8")
    out = tmp_path / "qr.svg"
    assert main([URL, "-s", str(path), "-j", '{"margin": 3}', "--label", "SCAN",
                 "-o", str(out)]) == 0
    doc["style"]["margin"] = 3
    doc["style"]["label"]["text"] = "SCAN"
    assert out.read_text(encoding="utf-8") == qrkude.svg(URL, doc)


def test_gradient_color_argument(tmp_path: Path) -> None:
    grad = {"type": "linear", "angle": 45, "stops": [{"offset": 0, "color": "#6a11cb"},
                                                     {"offset": 1, "color": "#2575fc"}]}
    out = tmp_path / "g.svg"
    assert main([URL, "--color", json.dumps(grad), "-o", str(out)]) == 0
    assert out.read_text(encoding="utf-8") == qrkude.svg(URL, {"modules": {"color": grad}})


def test_logo_in_svg_uses_data_uri_and_ecc_h(tmp_path: Path, capsys: Any) -> None:
    logo = tmp_path / "logo.svg"
    logo.write_text("<svg xmlns='http://www.w3.org/2000/svg'/>", encoding="utf-8")
    out = tmp_path / "qr.svg"
    assert main([URL, "--logo", str(logo), "-o", str(out), "--info"]) == 0
    info = capsys.readouterr().out
    assert "ecc H" in info
    text = out.read_text(encoding="utf-8")
    assert 'xlink:href="data:image/svg+xml;base64,' in text


def test_info_and_matrix(capsys: Any) -> None:
    assert main(["QRKude", "--info", "--matrix"]) == 0
    out = capsys.readouterr().out.splitlines()
    code = qrkude.encode("QRKude")
    assert out[:7] == ["logo 0", "version 1", "size 21", "ecc H", "mask %d" % code.mask,
                       "mode byte", "logo_damage 0.000000"]
    assert out[7] == "lint"
    assert out[8:] == code.to_text().splitlines()


def test_lint_to_stderr(capsys: Any) -> None:
    assert main([URL, "--margin", "1", "--color", "#dddddd", "--lint"]) == 0
    err = capsys.readouterr().err
    assert "lint: low-contrast, quiet-zone" in err


def test_stdin(monkeypatch: Any, capsysbinary: Any) -> None:
    monkeypatch.setattr(sys, "stdin", io.TextIOWrapper(io.BytesIO("héllo".encode())))
    assert main(["--stdin"]) == 0
    assert capsysbinary.readouterr().out.decode() == qrkude.to_svg(qrkude.encode("héllo"))


def test_errors(capsys: Any) -> None:
    assert main(["x" * 3000, "--ecc", "H"]) == 1
    assert "data too long" in capsys.readouterr().err
    assert main(["x", "-j", '{"modules": {"shape": "hexagon"}}']) == 1
    assert "hexagon" in capsys.readouterr().err
    assert main(["x", "-s", "/nonexistent/style.json"]) == 1
    with pytest.raises(SystemExit) as exc:
        main([])
    assert exc.value.code == 2
    with pytest.raises(SystemExit) as exc:
        main(["x", "--shape", "hexagon"])
    assert exc.value.code == 2


def test_payload_print(capsys: Any) -> None:
    assert main(["wifi", "--ssid", "My;Net", "--password", "pw", "--print"]) == 0
    assert capsys.readouterr().out == "WIFI:T:WPA;S:My\\;Net;P:pw;;\n"
    assert main(["wifi", "--ssid", "Open", "--auth", "none", "--print"]) == 0
    assert capsys.readouterr().out == "WIFI:S:Open;;\n"
    assert main(["geo", "--lat", "-33.8688197", "--lon", "151.2092955", "--print"]) == 0
    assert capsys.readouterr().out == "geo:-33.86882,151.209296\n"
    assert main(["vcard", "--first-name", "Ana", "--last-name", "García", "--print"]) == 0
    assert capsys.readouterr().out.startswith("BEGIN:VCARD\nVERSION:3.0\nN:García;Ana;;;\n")
    assert main(["email", "--to", "a@b.c", "--subject", "Hi there", "--print"]) == 0
    assert capsys.readouterr().out == "mailto:a@b.c?subject=Hi%20there\n"
    assert main(["sms", "--number", "+1555", "--message", "Hey", "--print"]) == 0
    assert capsys.readouterr().out == "SMSTO:+1555:Hey\n"
    assert main(["tel", "--number", "+1555", "--print"]) == 0
    assert capsys.readouterr().out == "tel:+1555\n"
    assert main(["event", "--summary", "Launch", "--start", "20261017T100000Z", "--print"]) == 0
    assert capsys.readouterr().out == "BEGIN:VEVENT\nSUMMARY:Launch\nDTSTART:20261017T100000Z\n" \
                                      "END:VEVENT\n"


def test_payload_svg(tmp_path: Path) -> None:
    out = tmp_path / "tel.svg"
    assert main(["tel", "--number", "+34 600 000 000", "-o", str(out), "--shape", "dot"]) == 0
    expected = qrkude.svg("tel:+34 600 000 000", {"modules": {"shape": "dot"}})
    assert out.read_text(encoding="utf-8") == expected


@needs_pil
def test_png_output(tmp_path: Path) -> None:
    out = tmp_path / "qr.png"
    assert main([URL, "-o", str(out), "--shape", "liquid", "--size", "300",
                 "--label", "SCAN ME"]) == 0
    img = Image.open(out)
    assert img.format == "PNG" and img.width == 300 and img.height > 300
    if zxingcpp is not None:
        assert _decode(out) == [URL]


@needs_pil
def test_png_payload_with_logo(tmp_path: Path) -> None:
    logo = tmp_path / "logo.png"
    Image.new("RGBA", (40, 40), (230, 57, 70, 255)).save(logo)
    out = tmp_path / "wifi.png"
    assert main(["wifi", "--ssid", "Kude", "--password", "secret", "-o", str(out),
                 "--logo", str(logo), "--eye-frame", "rounded"]) == 0
    img = Image.open(out).convert("RGBA")
    assert img.getpixel((256, 256))[:3] == (230, 57, 70)
    if zxingcpp is not None:
        assert _decode(out) == ["WIFI:T:WPA;S:Kude;P:secret;;"]


@needs_pil
def test_png_format_flag_to_stdout(capsysbinary: Any) -> None:
    assert main([URL, "--format", "png", "--size", "64"]) == 0
    assert capsysbinary.readouterr().out[:8] == b"\x89PNG\r\n\x1a\n"


def test_python_dash_m() -> None:
    r = subprocess.run([sys.executable, "-m", "qrkude", "tel", "--number", "123", "--print"],
                       capture_output=True, check=False)
    assert r.returncode == 0 and r.stdout.decode().strip() == "tel:123"
    r = subprocess.run([sys.executable, "-m", "qrkude", "--version"], capture_output=True,
                       check=False)
    assert r.stdout.decode().strip() == "qrkude " + qrkude.__version__


# ---- hostile inputs ------------------------------------------------------------------------


@pytest.mark.parametrize("args, message", [
    (["--margin", "1e999"], "parse error"),
    (["--margin", "nan"], "parse error"),
    (["--width", "inf"], "parse error"),
    (["--logo-size", "inf"], "parse error"),
    (["-j", '{"margin": 1e999}'], "parse error"),
    (["-j", '{"style": {"x": ' + "[" * 70 + "]" * 70 + "}}"], "parse error"),
    (["-j", "[" * 100000 + "]" * 100000], "parse error"),
    (["-j", '{"margin": ' + "1" * 401 + "}"], "parse error"),
    (["-j", '{"margin": 1'], "parse error"),
    (["-j", '{"encode": {"minVersion": 1e100}}'], "invalid argument"),
    (["--min-version", "0"], "invalid argument"),
])
def test_hostile_documents_fail_cleanly(args: list, message: str, capsys: Any) -> None:
    assert main(["x"] + args) == 1
    assert capsys.readouterr().err.startswith("qrkude: " + message)


def test_id_prefix_injection_is_neutralized(capsysbinary: Any) -> None:
    doc = {"svg": {"idPrefix": 'x"/><svg onload="alert(1)"></svg><a id="y'},
           "modules": {"color": {"stops": [{"color": "#000"}]}}}
    assert main(["x", "-j", json.dumps(doc)]) == 0
    out = capsysbinary.readouterr().out.decode()
    assert "onload=" not in out and 'id="x____svg_onload__alert_1_____svg__a_id__y-fg"' in out


@needs_pil
@pytest.mark.parametrize("content", [b"", b"\x00", b"\x89PNG\r\n\x1a\n", b"not an image"])
def test_png_with_invalid_logo_file_warns(tmp_path: Path, content: bytes, capsys: Any) -> None:
    logo = tmp_path / "logo.png"
    logo.write_bytes(content)
    out = tmp_path / "qr.png"
    assert main([URL, "-o", str(out), "--logo", str(logo), "--size", "128"]) == 0
    assert "warning: cannot draw" in capsys.readouterr().err
    assert Image.open(out).size == (128, 128)


@needs_pil
@pytest.mark.parametrize("args", [["--size", "100000000"], ["--size", "-3"], ["--size", "8193"],
                                  ["-j", '{"margin": 0, "label": {"text": "B", "size": 1e6}}']])
def test_png_size_limits(tmp_path: Path, args: list, capsys: Any) -> None:
    assert main([URL, "-o", str(tmp_path / "x.png")] + args) == 1
    assert "out of range" in capsys.readouterr().err


def test_missing_logo_file(tmp_path: Path, capsys: Any) -> None:
    assert main([URL, "--logo", str(tmp_path / "missing.png")]) == 1
    assert "missing.png" in capsys.readouterr().err
