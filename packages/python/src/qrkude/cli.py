# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Command line interface: ``qrkude`` / ``python -m qrkude``.

    qrkude "https://kudelink.com" -o qr.svg --shape fluid --eye-frame circle
    qrkude "https://kudelink.com" -o qr.png --style style.json --logo logo.png
    qrkude wifi --ssid Kude --password secret -o wifi.png
"""

from __future__ import annotations

import argparse
import base64
import mimetypes
import sys
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple, Union

from . import __version__, payloads
from .encoder import QRKudeError
from .layout import logo_damage
from .lint import lint
from .style import (
    EYE_SHAPES,
    LOGO_SHAPES,
    MODULE_SHAPES,
    Document,
    _loads,
    load_document,
)
from .svg import encode_for_style, to_svg

__all__ = ["main"]

_PAYLOADS = ("wifi", "email", "sms", "tel", "geo", "vcard", "event")

_EPILOG = """\
payload subcommands (run "qrkude NAME -h" for their options):
  wifi    Wi-Fi network          email   mailto: link
  sms     SMS message            tel     phone number
  geo     geographic location    vcard   contact card
  event   calendar event

examples:
  qrkude "https://kudelink.com" -o qr.svg --shape fluid --eye-frame circle --eye-ball circle
  qrkude "https://kudelink.com" -o qr.png --style style.json --logo logo.png --size 1024
  qrkude wifi --ssid Kude --password secret -o wifi.png
"""


def _add_output_options(p: argparse.ArgumentParser) -> None:
    out = p.add_argument_group("output")
    out.add_argument("-o", "--output", metavar="FILE",
                     help="output file, .svg or .png (default: SVG on stdout; '-' = stdout)")
    out.add_argument("-f", "--format", choices=("svg", "png"),
                     help="output format (default: from the file extension, else svg)")
    out.add_argument("--size", type=int, default=512, metavar="PX",
                     help="PNG width in pixels (default 512)")
    out.add_argument("--supersample", type=int, default=4, metavar="N",
                     help="PNG anti-aliasing samples per axis, 1-8 (default 4)")
    out.add_argument("--width", type=float, metavar="PX",
                     help="SVG width attribute (default: none, only a viewBox)")
    out.add_argument("--lint", action="store_true",
                     help="print scannability warnings to stderr")
    out.add_argument("--info", action="store_true",
                     help="print version, ECC, mask, logo damage and lint; "
                          "writes no image unless -o is given")
    out.add_argument("--matrix", action="store_true",
                     help="print the module matrix ('1' = dark); "
                          "writes no image unless -o is given")

    st = p.add_argument_group("style")
    st.add_argument("-s", "--style", metavar="FILE",
                    help='style JSON file ({"encode": .., "style": .., "svg": ..} or a bare style)')
    st.add_argument("-j", "--json", metavar="JSON",
                    help="style JSON string (applied after --style)")
    st.add_argument("--shape", choices=MODULE_SHAPES, help="module shape")
    st.add_argument("--module-size", type=float, metavar="S", help="module size 0.1-1")
    st.add_argument("--roundness", type=float, metavar="R", help="module roundness 0-1")
    st.add_argument("--eye-frame", choices=EYE_SHAPES, help="finder frame shape")
    st.add_argument("--eye-ball", choices=EYE_SHAPES, help="finder ball shape")
    st.add_argument("--alignment", choices=EYE_SHAPES, metavar="SHAPE",
                    help="style alignment patterns like eyes with this frame shape "
                         "(%s)" % ", ".join(EYE_SHAPES))
    st.add_argument("--color", metavar="PAINT",
                    help="module color: '#rrggbb[aa]' or a JSON gradient object")
    st.add_argument("--eye-color", metavar="PAINT", help="finder frame color")
    st.add_argument("--ball-color", metavar="PAINT", help="finder ball color")
    st.add_argument("--background", metavar="PAINT",
                    help="background color or gradient ('none' = transparent)")
    st.add_argument("--radius", type=float, metavar="R", help="background corner radius")
    st.add_argument("--margin", type=float, metavar="M", help="quiet zone in modules")
    st.add_argument("--border", type=float, metavar="W", help="border width in modules")
    st.add_argument("--border-color", metavar="COLOR", help="border color")
    st.add_argument("--label", metavar="TEXT",
                    help="caption text, below the code (or above with --label-top)")
    st.add_argument("--label-color", metavar="COLOR", help="caption color")
    st.add_argument("--label-top", action="store_true", help="put the caption above the code")
    st.add_argument("--logo", metavar="FILE",
                    help="logo image (embedded as a data: URI in SVG output)")
    st.add_argument("--logo-size", type=float, metavar="F",
                    help="logo size as a fraction of the symbol (default 0.2)")
    st.add_argument("--logo-shape", choices=LOGO_SHAPES, help="shape of the cleared logo area")

    enc = p.add_argument_group("encoding")
    enc.add_argument("--ecc", choices=("L", "M", "Q", "H"),
                     help="minimum error correction (default: H with a logo, else M)")
    enc.add_argument("--min-version", type=int, metavar="V", help="smallest QR version (1-40)")
    enc.add_argument("--max-version", type=int, metavar="V", help="largest QR version (1-40)")
    enc.add_argument("--mask", type=int, metavar="N", help="force mask 0-7")
    enc.add_argument("--no-boost", action="store_true", help="do not raise ECC when it fits")
    enc.add_argument("--eci", action="store_true", help="add a UTF-8 ECI header")


def _paint_arg(value: str) -> Any:
    v = value.strip()
    if v.lower() == "none":
        return None
    if v.startswith("{"):
        return _loads(v)
    return v


def _style_overrides(a: argparse.Namespace) -> Dict[str, Any]:
    o: Dict[str, Any] = {}

    def put(section: str, key: str, value: Any) -> None:
        o.setdefault(section, {})[key] = value

    if a.shape is not None:
        put("modules", "shape", a.shape)
    if a.module_size is not None:
        put("modules", "size", a.module_size)
    if a.roundness is not None:
        put("modules", "roundness", a.roundness)
    if a.color is not None:
        put("modules", "color", _paint_arg(a.color))
    if a.eye_frame is not None:
        put("eyes", "frame", a.eye_frame)
    if a.eye_ball is not None:
        put("eyes", "ball", a.eye_ball)
    if a.eye_color is not None:
        put("eyes", "frameColor", _paint_arg(a.eye_color))
    if a.ball_color is not None:
        put("eyes", "ballColor", _paint_arg(a.ball_color))
    if a.alignment is not None:
        put("alignment", "frame", a.alignment)
    if a.background is not None:
        put("background", "color", _paint_arg(a.background))
    if a.radius is not None:
        put("background", "radius", a.radius)
    if a.margin is not None:
        o["margin"] = a.margin
    if a.border is not None:
        put("border", "width", a.border)
    if a.border_color is not None:
        put("border", "color", a.border_color)
    if a.label is not None:
        put("label", "text", a.label)
    if a.label_color is not None:
        put("label", "color", a.label_color)
    if a.label_top:
        put("label", "position", "top")
    return o


def _encode_overrides(a: argparse.Namespace) -> Dict[str, Any]:
    e: Dict[str, Any] = {}
    if a.ecc is not None:
        e["ecc"] = a.ecc
    if a.min_version is not None:
        e["minVersion"] = a.min_version
    if a.max_version is not None:
        e["maxVersion"] = a.max_version
    if a.mask is not None:
        e["mask"] = a.mask
    if a.no_boost:
        e["boostEcc"] = False
    if a.eci:
        e["eci"] = True
    return e


def _data_uri(path: str) -> str:
    mime = mimetypes.guess_type(path)[0] or "application/octet-stream"
    with open(path, "rb") as f:
        return "data:%s;base64,%s" % (mime, base64.b64encode(f.read()).decode("ascii"))


def _build_document(a: argparse.Namespace) -> Document:
    doc = Document()
    if a.style:
        with open(a.style, "rb") as f:
            doc = load_document(f.read())
    if a.json:
        doc = load_document(a.json, base=doc)
    style = _style_overrides(a)
    logo: Dict[str, Any] = {}
    if a.logo_size is not None:
        logo["size"] = a.logo_size
    if a.logo_shape is not None:
        logo["shape"] = a.logo_shape
    if a.logo:
        logo["href"] = _data_uri(a.logo)
    if logo:
        style["logo"] = logo  # adds a logo (default size 0.2) when none is present
    overrides: Dict[str, Any] = {"style": style, "encode": _encode_overrides(a)}
    if a.width is not None:
        overrides["svg"] = {"width": a.width}
    # the same reader as JSON input: values are validated (finite, known names)
    return load_document(overrides, base=doc)


def _write(path: Optional[str], data: bytes) -> None:
    if path is None or path == "-":
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
    else:
        with open(path, "wb") as f:
            f.write(data)


def _run(a: argparse.Namespace, text: Union[str, bytes]) -> int:
    doc = _build_document(a)
    code = encode_for_style(text, doc.encode, doc.style)
    flags = lint(code, doc.style)

    if a.info:
        print("logo %d\nversion %d\nsize %d\necc %s\nmask %d\nmode %s\nlogo_damage %.6f\nlint%s" % (
            1 if doc.style.has_logo else 0, code.version, code.size, code.ecc.name, code.mask,
            str(code.mode), logo_damage(code, doc.style), "".join(" " + f for f in flags)))
    if a.matrix:
        print(code.to_text())
    if a.lint:
        damage = " (logo damage %.2f)" % logo_damage(code, doc.style) if doc.style.has_logo else ""
        print("lint: %s%s" % (", ".join(flags) if flags else "ok", damage), file=sys.stderr)
    if (a.info or a.matrix) and not a.output:
        return 0

    fmt = a.format
    if fmt is None:
        fmt = "png" if a.output and a.output.lower().endswith(".png") else "svg"
    if fmt == "png":
        try:
            from .png import _open_image, to_png
        except ImportError:
            print("qrkude: PNG output needs Pillow: pip install 'qrkude[png]'", file=sys.stderr)
            return 1
        logo_image: Any = None
        if a.logo:
            try:
                logo_image = _open_image(a.logo)
            except Exception as exc:  # noqa: BLE001 - any undecodable file
                print("qrkude: warning: cannot draw %s in a PNG (%s); the logo area stays "
                      "empty" % (a.logo, exc), file=sys.stderr)
        data = to_png(code, doc.style, a.size, logo_image, supersample=a.supersample)
    else:
        data = to_svg(code, doc.style, doc.svg).encode("utf-8", "surrogatepass")
    _write(a.output, data)
    return 0


def _main_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="qrkude",
        usage="qrkude [TEXT] [options]\n       qrkude {%s} [payload options] [options]"
              % ",".join(_PAYLOADS),
        description="Styled QR codes as SVG or PNG.",
        epilog=_EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("text", nargs="?", metavar="TEXT", help="payload text (UTF-8)")
    p.add_argument("--stdin", action="store_true", help="read the payload from stdin")
    p.add_argument("--version", action="version", version="qrkude " + __version__)
    _add_output_options(p)
    return p


_Builder = Callable[[argparse.Namespace], str]


def _payload_parser(kind: str) -> Tuple[argparse.ArgumentParser, _Builder]:
    p = argparse.ArgumentParser(prog="qrkude " + kind)
    g = p.add_argument_group(kind)
    if kind == "wifi":
        p.description = "Wi-Fi network (WIFI: payload)."
        g.add_argument("--ssid", required=True)
        g.add_argument("--password")
        g.add_argument("--auth", default="WPA", help="WPA (default), WEP, nopass or none")
        g.add_argument("--hidden", action="store_true", help="the network is hidden")

        def build(a: argparse.Namespace) -> str:
            auth = None if a.auth in ("", "none") else a.auth
            return payloads.wifi(a.ssid, a.password, auth, a.hidden)
    elif kind == "email":
        p.description = "E-mail (mailto: link)."
        g.add_argument("--to", required=True)
        g.add_argument("--subject")
        g.add_argument("--body")

        def build(a: argparse.Namespace) -> str:
            return payloads.email(a.to, a.subject, a.body)
    elif kind == "sms":
        p.description = "SMS message (SMSTO: payload)."
        g.add_argument("--number", required=True)
        g.add_argument("--message")

        def build(a: argparse.Namespace) -> str:
            return payloads.sms(a.number, a.message)
    elif kind == "tel":
        p.description = "Phone number (tel: link)."
        g.add_argument("--number", required=True)

        def build(a: argparse.Namespace) -> str:
            return payloads.tel(a.number)
    elif kind == "geo":
        p.description = "Geographic location (geo: URI)."
        g.add_argument("--lat", type=float, required=True, help="latitude")
        g.add_argument("--lon", type=float, required=True, help="longitude")

        def build(a: argparse.Namespace) -> str:
            return payloads.geo(a.lat, a.lon)
    elif kind == "vcard":
        p.description = "Contact card (vCard 3.0)."
        for name in ("first-name", "last-name", "org", "title", "phone", "email", "url",
                     "address", "note"):
            g.add_argument("--" + name)

        def build(a: argparse.Namespace) -> str:
            return payloads.vcard(a.first_name, a.last_name, a.org, a.title, a.phone, a.email,
                                  a.url, a.address, a.note)
    else:
        p.description = "Calendar event (VEVENT)."
        g.add_argument("--summary", required=True)
        g.add_argument("--start", help='e.g. "20261017T100000Z"')
        g.add_argument("--end")
        g.add_argument("--location")
        g.add_argument("--description")

        def build(a: argparse.Namespace) -> str:
            return payloads.event(a.summary, a.start, a.end, a.location, a.description)
    g.add_argument("--print", dest="print_payload", action="store_true",
                   help="print the payload text to stdout instead of an image")
    _add_output_options(p)
    return p, build


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Runs the command line tool; returns the exit status."""
    args: List[str] = list(sys.argv[1:] if argv is None else argv)
    text: Union[str, bytes]
    try:
        if args and args[0] in _PAYLOADS:
            pparser, build = _payload_parser(args[0])
            a = pparser.parse_args(args[1:])
            payload = build(a)
            if a.print_payload:
                sys.stdout.write(payload + "\n")
                return 0
            text = payload
        else:
            parser = _main_parser()
            a = parser.parse_args(args)
            if a.stdin:
                if a.text is not None:
                    parser.error("give TEXT or --stdin, not both")
                text = sys.stdin.buffer.read()
            elif a.text is not None:
                text = a.text
            else:
                parser.error("missing TEXT (or --stdin)")
        return _run(a, text)
    except QRKudeError as exc:
        message = str(exc)
        if not message.startswith(exc.kind):
            message = "%s: %s" % (exc.kind, message)
        print("qrkude: %s" % message, file=sys.stderr)
        return 1
    except (ValueError, OSError) as exc:
        print("qrkude: %s" % exc, file=sys.stderr)
        return 1

if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
