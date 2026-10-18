<!--
Copyright 2026 The QRKude Authors
SPDX-License-Identifier: Apache-2.0
-->

# qrkude

`qrkude` is a Python library and command line tool that draws styled QR
codes: round dots, connected "fluid" shapes, custom corner eyes, gradients, a
logo in the middle and a caption, written as SVG or PNG.

It is the Python package of [QRKude](../../README.md) (the `README.md` at the
root of the QRKude repository), a family of libraries that draw the same QR
code, byte for byte, in C, Python, JavaScript, Rust, Dart and more.

<p>
<img src="../../docs/images/fluid.svg" width="140" alt="QR code with connected fluid modules">
<img src="../../docs/images/logo.svg" width="140" alt="QR code with a logo in the middle">
<img src="../../docs/images/frame.svg" width="140" alt="QR code in a frame with a SCAN ME caption">
<img src="../../docs/images/gradient.svg" width="140" alt="QR code with a color gradient">
</p>

## Status and installation

**Pre-release.** Version 0.1.0 is not published on PyPI yet. Install it from
a clone of the QRKude repository. From the root of the clone:

```sh
python3 -m venv .venv
. .venv/bin/activate                  # on Windows: .venv\Scripts\activate
pip install "./packages/python[png]"  # or "./packages/python" for SVG only
```

Once the package is published, this will be:

```sh
pip install qrkude          # SVG output, no dependencies
pip install "qrkude[png]"   # adds PNG output through Pillow
```

| Extra | Installs | Needed for |
|---|---|---|
| (none) | nothing | encoding, SVG, lint, payload helpers, CLI with SVG output |
| `png` | Pillow 9 or newer | `to_png`, `to_image`, `qrkude ... -o file.png` |
| `test` | pytest, Pillow | running the test suite |

Python 3.9 or newer is required. The package is pure Python and has no
runtime dependencies.

## Quick start

```python
import qrkude

svg = qrkude.svg("https://example.com", {"modules": {"shape": "dot"}})

with open("qr.svg", "w", encoding="utf-8") as f:
    f.write(svg)
```

This writes `qr.svg`: a QR code for `https://example.com` made of round dots,
black on white, with the standard 4-module quiet zone (the empty border that
scanners need). Open it in a browser or point a phone camera at the screen.

New to QR codes? Read the [guide](../../docs/GUIDE.md) (`docs/GUIDE.md` in the
QRKude repository).

## Styling

A style says how the code is drawn: the shape of the small squares (the
*modules*), the three corner squares (the *eyes*, or finder patterns),
colors, background, border, logo and caption. You can build a style in two
ways. Both give exactly the same result.

**1. Python dataclasses.** Every part of the style is a dataclass with
defaults, so you only set what you change:

```python
import qrkude
from qrkude import EyeStyle, ModuleStyle, Paint, Style

style = Style(
    modules=ModuleStyle(shape="fluid", color="#1d3557"),
    eyes=EyeStyle(frame="rounded", ball="circle", frame_color="#e63946"),
)
code = qrkude.encode("https://example.com")
svg = qrkude.to_svg(code, style)
```

`encode` turns the text into a grid of dark and light modules (a `QRCode`);
`to_svg` draws that grid with the style.

**2. The JSON style document.** The same style as JSON. Every QRKude library
reads this format, so a style can be stored in a database or shared between
a web front end and a Python back end. A document can also hold encoding
options (`encode`) and SVG options (`svg`):

```json
{
  "encode": {"ecc": "H"},
  "style": {
    "margin": 3,
    "modules": {
      "shape": "liquid",
      "color": {"type": "linear", "angle": 45,
                "stops": [{"offset": 0, "color": "#6a11cb"},
                          {"offset": 1, "color": "#2575fc"}]}
    },
    "eyes": {"frame": "leaf", "ball": "leaf", "frameColor": "#6a11cb"},
    "background": {"color": "#ffffff", "radius": 2},
    "label": {"text": "SCAN ME", "color": "#6a11cb"}
  },
  "svg": {"width": 512}
}
```

Save it as `style.json` and use it like this:

```python
import qrkude

with open("style.json", encoding="utf-8") as f:
    svg = qrkude.svg("https://example.com", f.read())

doc = qrkude.load_document(open("style.json", encoding="utf-8").read())
print(doc.style.modules.shape, doc.encode.ecc, doc.svg.width)  # -> liquid H 512.0
```

`qrkude.svg(text, document)` encodes and draws in one call.
`load_document` returns a `Document` with `.style`, `.encode` and `.svg`, and
`Style.from_json` returns only the style. Both accept a JSON string or a
`dict`. `style.to_dict()` and `style.to_json()` convert back.

**3. A logo with a light background behind it**:

```python
import qrkude
from qrkude import LogoStyle, ModuleStyle, Style

style = Style(
    modules=ModuleStyle(shape="dot", size=0.9),
    logo=LogoStyle(href="https://example.com/logo.png", size=0.22,
                   shape="circle", background="#ffffff"),
)
code = qrkude.encode_for_style("https://example.com", None, style)
print(code.ecc)                                   # -> H
print(round(qrkude.logo_damage(code, style), 2))  # -> 0.64
svg = qrkude.to_svg(code, style)
```

The logo covers part of the code. QR codes contain redundant data (*error
correction*), so a scanner can rebuild what the logo hides, within limits.
`logo_damage` measures how much of that capacity the logo uses: 0 means no
logo, `lint` warns above 0.7, and above 1 the code cannot be read (see
[Making sure it scans](#making-sure-it-scans)).
`encode_for_style` picks error correction level H (the highest) when the
style has a logo, and M otherwise. See the guide on
[logos](../../docs/GUIDE.md#logos) and
[error correction](../../docs/GUIDE.md#error-correction).

Every option, with pictures, is in [STYLES.md](../../docs/STYLES.md)
(`docs/STYLES.md` in the QRKude repository):
[modules](../../docs/STYLES.md#modules), [eyes](../../docs/STYLES.md#eyes),
[alignment patterns](../../docs/STYLES.md#alignment-patterns),
[colors and gradients](../../docs/STYLES.md#colors-and-gradients),
[background and border](../../docs/STYLES.md#background-and-border),
[logo](../../docs/STYLES.md#logo), [label](../../docs/STYLES.md#label),
[encoding options](../../docs/STYLES.md#encoding-options),
[SVG options](../../docs/STYLES.md#svg-options) and a
[complete example](../../docs/STYLES.md#complete-example).

Python names follow Python style (`frame_color`); JSON keys keep the shared
names (`frameColor`). The Python fields:

| Dataclass | Fields (default) |
|---|---|
| `Style` | `margin` (4.0), `modules`, `eyes`, `alignment`, `background`, `border`, `logo` (None), `label` (None) |
| `ModuleStyle` | `shape` ("square"), `size` (1.0), `roundness` (1.0), `color` (black) |
| `EyeStyle` | `frame` ("square"), `ball` ("square"), `frame_color` (none: same as modules), `ball_color` (none: same as modules) |
| `AlignmentStyle` | `frame` (None: draw like data modules), `ball` (None: "square") |
| `BackgroundStyle` | `color` (white; `Paint.none()` = transparent), `radius` (0.0) |
| `BorderStyle` | `width` (0.0), `color` (black) |
| `LogoStyle` | `href` (None), `size` (0.2), `margin` (1.0), `shape` ("square"), `excavate` (True), `background` (0 = none) |
| `LabelStyle` | `text` (None), `font` ("sans-serif"), `size` (4.0), `color` (black), `position` ("bottom"), `bold` (True) |

Lengths are in modules. Shapes are strings (or the `ModuleShape` and
`EyeShape` enums). Paint fields (`ModuleStyle.color`,
`EyeStyle.frame_color`, `EyeStyle.ball_color`, `BackgroundStyle.color`)
take a `Paint`, a color string, `None` or a gradient `dict`. The other color
fields hold a 32-bit `0xRRGGBBAA` integer; their constructors also accept a
color string, but when you assign one later, convert it with
`qrkude.parse_color("#e63946")`.

## Output

**SVG** needs no dependencies and is the best choice for print and for the
web: it scales to any size. `to_svg(code, style, SvgOptions(width=...))` adds
`width` and `height` attributes. Without a width the SVG only has a
`viewBox` and fills its container.

**PNG** needs the `png` extra (Pillow):

```python
import qrkude
from PIL import Image

style = qrkude.Style.from_json({"modules": {"shape": "liquid"},
                                "logo": {"size": 0.2, "background": "#ffffff"},
                                "label": {"text": "SCAN ME"}})
code = qrkude.encode_for_style("https://example.com", None, style)

logo = Image.new("RGBA", (64, 64), "#e63946")         # or a path, or PNG bytes
png_bytes = qrkude.to_png(code, style, size=600, logo=logo)
with open("qr.png", "wb") as f:
    f.write(png_bytes)

image = qrkude.to_image(code, style, size=600, logo=logo)   # a PIL.Image in RGBA
print(image.size)   # -> (600, 697)
```

- `size` is the width in pixels. The height follows the canvas shape: a
  caption adds a band below or above the code.
- Edges are anti-aliased by supersampling (`supersample=4`, the default,
  takes 4 x 4 samples per pixel).
- The PNG draws the same shapes as the SVG. On the test vectors it differs
  from a cairosvg rendering of the SVG by less than 1 level in 255 on
  average, and it decodes identically.
- The logo is fitted into its box and keeps its aspect ratio. `logo` may be a
  `PIL.Image`, a file path or image bytes. Without it, a raster `data:` URI in
  `style.logo.href` is used. PNG output cannot load `http` URLs or SVG logos.
- The caption uses a TrueType font found on the system (DejaVu, Noto,
  Liberation, Arial...). Without one it falls back to Pillow's built-in font.

**Choosing a size.** A scanner needs each module to cover several pixels.
`get_layout` tells you how many modules wide the drawing is:

```python
import qrkude

code = qrkude.encode("https://example.com")
layout = qrkude.get_layout(code, qrkude.Style())
for size in (200, 512):
    print(size, "px:", round(size / layout.width, 1), "px per module")
# -> 200 px: 6.1 px per module
# -> 512 px: 15.5 px per module
```

A whole number of pixels per module gives the sharpest edges. For print,
use the SVG or make sure each module is large enough at the printed size. See
[size in print and on screen](../../docs/GUIDE.md#size-in-print-and-on-screen).

## Content helpers

A QR code holds text. Phones recognise some text formats and offer an action,
such as joining a Wi-Fi network or saving a contact. These functions build
that text. The result is a normal string: pass it to `encode` or `svg`.

| Function | Phone action | Example result |
|---|---|---|
| `wifi(ssid, password=None, auth="WPA", hidden=False)` | join a Wi-Fi network | `WIFI:T:WPA;S:Cafe;P:secret;;` |
| `email(to, subject=None, body=None)` | write an e-mail | `mailto:hi@example.com?subject=Hello%20there` |
| `sms(number, message=None)` | write an SMS | `SMSTO:+34600000000:Hola` |
| `tel(number)` | call a number | `tel:+34600000000` |
| `geo(latitude, longitude)` | open a map | `geo:40.4168,-3.7038` |
| `vcard(first_name=None, last_name=None, organization=None, title=None, phone=None, email=None, url=None, address=None, note=None)` | save a contact (vCard 3.0) | `BEGIN:VCARD` ... `END:VCARD` |
| `event(summary, start=None, end=None, location=None, description=None)` | add a calendar event | `BEGIN:VEVENT` ... `END:VEVENT` |

```python
>>> import qrkude
>>> qrkude.wifi("Cafe", "secret")
'WIFI:T:WPA;S:Cafe;P:secret;;'
>>> qrkude.wifi("Guest network", auth="nopass")
'WIFI:T:nopass;S:Guest network;;'
>>> qrkude.email("hi@example.com", subject="Hello there")
'mailto:hi@example.com?subject=Hello%20there'
>>> qrkude.sms("+34600000000", "Hola")
'SMSTO:+34600000000:Hola'
>>> qrkude.tel("+34600000000")
'tel:+34600000000'
>>> qrkude.geo(40.4168, -3.7038)
'geo:40.4168,-3.7038'
>>> print(qrkude.vcard(first_name="Ana", last_name="García", phone="+34 600 000 000"))
BEGIN:VCARD
VERSION:3.0
N:García;Ana;;;
FN:Ana García
TEL:+34 600 000 000
END:VCARD
>>> print(qrkude.event("Launch", start="20261017T100000Z", location="Online"))
BEGIN:VEVENT
SUMMARY:Launch
DTSTART:20261017T100000Z
LOCATION:Online
END:VEVENT
```

- `auth` is `"WPA"`, `"WEP"`, `"nopass"` or `None` (no `T:` field). The
  password is left out with `"nopass"`.
- Special characters are escaped as each format requires. `geo` keeps up to
  6 decimals and clamps the latitude to [-90, 90] and the longitude to
  [-180, 180].
- Times for `event` use the iCalendar form `YYYYMMDDTHHMMSSZ` (UTC).

## Making sure it scans

Styling can make a code harder to read. `lint(code, style)` returns a list of
warnings, and `logo_damage(code, style)` measures how much of the error
correction a logo uses up.

```python
import qrkude

style = qrkude.Style.from_json({"margin": 1, "modules": {"color": "#bbbbbb"},
                                "logo": {"size": 0.3}})
code = qrkude.encode("https://example.com", ecc="M")
print(qrkude.lint(code, style))
# -> ['logo-unreadable', 'low-contrast', 'quiet-zone']
print(round(qrkude.logo_damage(code, style), 2))
# -> 2.0
```

| Warning | Meaning | What to do |
|---|---|---|
| `logo-risky` | the logo hides 70% to 100% of what error correction can repair in the worst block | make the logo or its `margin` smaller, use ECC H, test on several phones |
| `logo-unreadable` | the logo hides more than error correction can repair: the code cannot be read | same as above; `encode_for_style` already picks H for logos |
| `low-contrast` | modules and background are too similar (contrast below 3:1) | darken the modules or lighten the background |
| `inverted` | light modules on a dark background | many scanners handle it, some do not; prefer dark on light |
| `transparent-background` | no background color | make sure the code is placed on a light, plain surface |
| `quiet-zone` | margin below 2 modules | use `margin` 2 or more (4 is the standard) |
| `small-modules` | module `size` below 0.6 for shapes with gaps | raise `modules.size` |
| `eye-shapes` | a diamond eye frame, or a diamond ball in a square or leaf frame | pick other eye shapes: scanners fail to find these eyes |
| `invisible` | the modules or eyes have no visible color | give them a color with alpha above 0 |

`logo_damage` is 0 without a logo. Lower is safer: `lint` warns above 0.7
(`logo-risky`), and above 1 the code cannot be decoded (`logo-unreadable`).
Always test real codes on real phones before printing. See
[styling without breaking scanning](../../docs/GUIDE.md#styling-without-breaking-scanning),
[colors and contrast](../../docs/GUIDE.md#colors-and-contrast),
[testing your codes](../../docs/GUIDE.md#testing-your-codes) and
[troubleshooting](../../docs/GUIDE.md#troubleshooting).

## Untrusted styles

Styles often come from users, web forms or databases. Reading and drawing
them is safe:

- **Limits.** A number token longer than 400 characters, a number that is not
  finite (`1e999`), and nesting deeper than 64 objects or arrays are errors.
  They never crash the program or exhaust the stack.
- **Ranges.** `margin`, `border.width` and `logo.margin` are clamped to
  0..1000 modules, and `label.size` to 0.1..1000. `minVersion`, `maxVersion`
  and `mask` are clamped to -1000..1000, then rounded toward zero.
- **No markup injection.** `svg.idPrefix` is sanitized: every byte outside
  `A-Z a-z 0-9 _ -` becomes `_`. `href`, `font` and the caption are escaped
  for XML, and control characters that XML forbids are dropped.
- **Strings.** A string ends at its first U+0000 character. Broken UTF-16
  surrogates become U+FFFD.
- **Repeated keys.** A key given twice is applied in order. Two `modules`
  objects merge; later values win.
- **PNG limits.** At most 16384 pixels per side and 2^26 pixels in all. Logos
  above 2^24 pixels are refused before decoding. A `data:` URI logo that
  cannot be decoded is skipped.

Errors caused by the input (the text, the options or the style) are raised
as `QRKudeError` (a subclass of `ValueError`) with a `kind` attribute. The
kinds are the same in every QRKude library:

| Exception | `kind` | Raised when |
|---|---|---|
| `StyleError` | `"parse error"` | the JSON or a style value is invalid (bad JSON, unknown shape, bad color, limits above) |
| `InvalidArgumentError` | `"invalid argument"` | encoding options are out of range (`minVersion: 0`, `mask: 9`), a PNG size is out of range, or a logo image cannot be read |
| `DataTooLongError` | `"data too long"` | the text does not fit in a QR code with the given options |

```python
import qrkude

for document in ('{"modules": {"shape": "hexagon"}}', '{"encode": {"maxVersion": 41}}'):
    try:
        qrkude.svg("hello", document)
    except qrkude.QRKudeError as error:
        print(error.kind, "-", error)
# -> parse error - unknown module shape: 'hexagon'
# -> invalid argument - invalid argument
```

## Command line

Installing the package adds a `qrkude` command. `python -m qrkude` does the
same thing.

```sh
qrkude "https://example.com" -o qr.svg --shape fluid --eye-frame circle --eye-ball circle
qrkude "https://example.com" -o qr.png --size 1024 --style style.json --logo logo.png
qrkude "https://example.com" --color "#1d3557" --background none --lint > qr.svg
qrkude "https://example.com" --info
echo -n "raw bytes" | qrkude --stdin -o raw.svg
qrkude wifi --ssid Cafe --password secret -o wifi.png
qrkude vcard --first-name Ana --last-name García --phone "+34 600 000 000" -o card.svg
qrkude geo --lat 40.4168 --lon -3.7038 --print
```

(`style.json` is the file from the [Styling](#styling) section and
`logo.png` is any image.)

Usage: `qrkude [TEXT] [options]` or `qrkude SUBCOMMAND [payload options] [options]`.
Without `-o` the SVG goes to standard output. The output format comes from
the file extension (`.svg` or `.png`) or from `--format`. `--style` and
`--json` take the JSON document described in [Styling](#styling); the
individual flags are applied on top of it. If `encode.ecc` and `--ecc` are
both absent, a logo selects ECC H and otherwise ECC M.

**General**

| Option | Meaning |
|---|---|
| `TEXT` | the text to encode (UTF-8) |
| `--stdin` | read the text from standard input, as raw bytes |
| `-h`, `--help` | show help |
| `--version` | print the version |

**Output**

| Option | Meaning |
|---|---|
| `-o FILE`, `--output FILE` | output file, `.svg` or `.png`; `-` means standard output |
| `-f {svg,png}`, `--format {svg,png}` | output format (default: from the extension, else svg) |
| `--size PX` | PNG width in pixels (default 512) |
| `--supersample N` | PNG anti-aliasing samples per axis, 1 to 8 (default 4) |
| `--width PX` | SVG `width` attribute (default: none, only a `viewBox`) |
| `--lint` | print scannability warnings to standard error |
| `--info` | print the logo flag, version, size, ECC, mask, mode, logo damage and lint; writes no image unless `-o` is given |
| `--matrix` | print the module grid (`1` = dark); writes no image unless `-o` is given |

**Style**

| Option | Meaning |
|---|---|
| `-s FILE`, `--style FILE` | style JSON file (a document or a bare style) |
| `-j JSON`, `--json JSON` | style JSON string, applied after `--style` |
| `--shape SHAPE` | module shape: square, dot, rounded, diamond, star, fluid, liquid, bevel, classy, hbars, vbars |
| `--module-size S` | module size 0.1 to 1 |
| `--roundness R` | module roundness 0 to 1 |
| `--eye-frame SHAPE` | eye frame shape: square, rounded, circle, leaf, drop, bevel, diamond, dots |
| `--eye-ball SHAPE` | eye ball shape (same names) |
| `--alignment SHAPE` | draw the alignment patterns as small eyes with this frame shape |
| `--color PAINT` | module color: `#rgb`, `#rrggbb`, `#rrggbbaa`, or a JSON gradient object |
| `--eye-color PAINT` | eye frame color |
| `--ball-color PAINT` | eye ball color |
| `--background PAINT` | background color or gradient; `none` = transparent |
| `--radius R` | background corner radius, in modules |
| `--margin M` | quiet zone, in modules |
| `--border W` | border width, in modules |
| `--border-color COLOR` | border color |
| `--label TEXT` | caption text, below the code |
| `--label-color COLOR` | caption color |
| `--label-top` | put the caption above the code |
| `--logo FILE` | logo image: embedded as a `data:` URI in SVG, drawn as a bitmap in PNG |
| `--logo-size F` | logo size as a fraction of the code width (default 0.2) |
| `--logo-shape {square,rounded,circle}` | shape of the cleared area around the logo |

**Encoding**

| Option | Meaning |
|---|---|
| `--ecc {L,M,Q,H}` | minimum error correction (default: H with a logo, else M) |
| `--min-version V` | smallest QR version, 1 to 40 |
| `--max-version V` | largest QR version, 1 to 40 |
| `--mask N` | force the mask pattern, 0 to 7 |
| `--no-boost` | do not raise the error correction when the code stays the same size |
| `--eci` | add a header that declares the text as UTF-8 |

**Subcommands.** Each builds the text with a [content helper](#content-helpers)
and accepts all the options above. Add `--print` to print the text instead
of drawing it.

| Subcommand | Options (* = required) |
|---|---|
| `wifi` | `--ssid`*, `--password`, `--auth` (WPA (default), WEP, nopass or none), `--hidden` |
| `email` | `--to`*, `--subject`, `--body` |
| `sms` | `--number`*, `--message` |
| `tel` | `--number`* |
| `geo` | `--lat`*, `--lon`* (write a negative value as `--lon=-3.7`) |
| `vcard` | `--first-name`, `--last-name`, `--org`, `--title`, `--phone`, `--email`, `--url`, `--address`, `--note` |
| `event` | `--summary`*, `--start`, `--end`, `--location`, `--description` |

To encode a text that is also a subcommand name, write `qrkude -- wifi`.
Exit status: 0 on success, 1 on an error (the message starts with its kind,
for example `qrkude: parse error: ...`), 2 for invalid command line usage. A
logo file that cannot be drawn in a PNG gives a warning, and the code is
drawn without it.

## API reference

Everything below is importable from `qrkude` unless a module is named.
Colors (`Color`) are integers `0xRRGGBBAA`. Lengths are in modules.

**Encoding**

| Symbol | Description |
|---|---|
| `encode(data: str \| bytes \| bytearray \| memoryview, options: EncodeOptions \| None = None, *, ecc=None, min_version=None, max_version=None, mask=None, boost_ecc=None, eci=None) -> QRCode` | Encode text (as UTF-8) or bytes. Keyword arguments override `options`. ECC defaults to M. |
| `encode_for_style(data: str \| bytes, options: EncodeOptions \| None = None, style: Style \| None = None) -> QRCode` | Like `encode`, but when `options.ecc` is None it uses `recommended_ecc(style)`. |
| `EncodeOptions(ecc: Ecc \| None = None, min_version: int = 1, max_version: int = 40, mask: int = -1, boost_ecc: bool = True, eci: bool = False)` | Encoder options. `mask=-1` chooses the best mask. `boost_ecc` raises the ECC while the size does not change. `eci` adds a UTF-8 header. |
| `QRCode(version: int, ecc: Ecc, mask: int, mode: Mode, modules: bytes)` | An encoded code. Usually created by `encode`. |
| `QRCode.version`, `.size`, `.ecc`, `.mask`, `.mode` | Version 1-40, modules per side (`4 * version + 17`), error correction, mask 0-7, data mode. |
| `QRCode.is_dark(x: int, y: int) -> bool` | True for a dark module; outside the grid is light. |
| `QRCode.modules -> bytes` | Row-major module bytes, 1 = dark. |
| `QRCode.matrix() -> list[list[bool]]` | The grid as rows of booleans. |
| `QRCode.to_text(dark: str = "1", light: str = "0") -> str` | The grid as lines of text. |
| `Ecc` | `IntEnum`: `L`, `M`, `Q`, `H` (about 7, 15, 25 and 30% recovery). `str(Ecc.H) == "H"`. |
| `Ecc.parse(value: Ecc \| str \| int) -> Ecc` | Accepts an `Ecc`, `"L"`/`"M"`/`"Q"`/`"H"` or 0-3. |
| `Mode` | `IntEnum`: `NUMERIC`, `ALPHANUMERIC`, `BYTE`. `str(Mode.BYTE) == "byte"`. |
| `recommended_ecc(style: Style \| None) -> Ecc` | `H` when the style has a logo, else `M`. |

**Style**

| Symbol | Description |
|---|---|
| `Style(margin=4.0, modules=ModuleStyle(), eyes=EyeStyle(), alignment=AlignmentStyle(), background=BackgroundStyle(), border=BorderStyle(), logo=None, label=None)` | The complete style (see the [Styling](#styling) table). |
| `Style.from_json(source: str \| bytes \| Mapping) -> Style` | Read a style or the `style` part of a document. |
| `Style.to_dict() -> dict`, `Style.to_json(**kwargs) -> str` | Convert to the JSON style format. `kwargs` go to `json.dumps`. |
| `Style.copy() -> Style` | A deep copy. |
| `Style.has_logo -> bool` | True when `logo` is set and its size is above 0. |
| `ModuleStyle(shape="square", size=1.0, roundness=1.0, color=Paint.solid("#000000"))` | Data modules. |
| `EyeStyle(frame="square", ball="square", frame_color=Paint.none(), ball_color=Paint.none())` | The three finder eyes. `Paint.none()` uses the module paint. |
| `AlignmentStyle(frame: str \| None = None, ball: str \| None = None)` | Draw alignment patterns as small eyes when `frame` is set. |
| `BackgroundStyle(color=Paint.solid("#ffffff"), radius=0.0)` | Background paint and corner radius. |
| `BorderStyle(width=0.0, color=0x000000FF)` | Border outside the quiet zone. |
| `LogoStyle(href=None, size=0.2, margin=1.0, shape="square", excavate=True, background=0)` | Logo box (fraction of the code width), clear space, area shape, module removal, area fill. |
| `LabelStyle(text=None, font="sans-serif", size=4.0, color=0x000000FF, position="bottom", bold=True)` | Caption. No text means no caption. |
| `ModuleShape`, `EyeShape` | `str` enums of the shape names (`ModuleShape.FLUID == "fluid"`). |
| `Paint(type=PaintType.NONE, color=0, angle=0.0, cx=0.5, cy=0.5, r=0.75, stops=())` | A frozen paint: nothing, a solid color or a gradient. |
| `Paint.none()`, `Paint.solid(color)` | No paint; a solid color (string or int). |
| `Paint.linear(angle: float, *colors)` | Linear gradient; 0 degrees = left to right, 90 = top to bottom. Colors are evenly spaced unless given as `Stop` or `(offset, color)`. |
| `Paint.radial(*colors, cx=0.5, cy=0.5, r=0.75)` | Radial gradient, center color first; `cx`, `cy`, `r` are fractions of the painted area. |
| `Paint.from_value(value) -> Paint`, `Paint.to_value()` | Convert from and to the JSON form (`None`, color string, gradient dict). |
| `Paint.is_gradient -> bool` | True for linear and radial paints. |
| `PaintType` | `IntEnum`: `NONE`, `SOLID`, `LINEAR`, `RADIAL`. |
| `Stop(offset: float, color: int)` | A frozen gradient stop. At most 8 stops are drawn. |
| `parse_color(value: str \| int) -> int` | `#rgb`, `#rrggbb`, `#rrggbbaa` or `transparent` to `0xRRGGBBAA`. |
| `format_color(color: int) -> str` | `#rrggbb`, or `#rrggbbaa` when not opaque. |
| `c_string(value: str) -> str` | A string as every QRKude library reads it: cut at U+0000, broken surrogates replaced. |

**Documents**

| Symbol | Description |
|---|---|
| `load_document(source: str \| bytes \| Mapping \| Document \| None, base: Document \| None = None) -> Document` | Read a `{encode, style, svg}` document or a bare style, applied on top of `base`. |
| `Document(style=Style(), encode=EncodeOptions(), svg=SvgOptions())` | A parsed document. |
| `Document.ecc_for() -> Ecc` | The explicit `encode.ecc`, else `recommended_ecc(style)`. |
| `SvgOptions(width: float = 0.0, id_prefix: str \| None = "qrk")` | SVG `width` attribute (0 = none) and gradient id prefix. |

**Output**

| Symbol | Description |
|---|---|
| `to_svg(code: QRCode, style: Style \| None = None, options: SvgOptions \| None = None) -> str` | The SVG document. |
| `svg(text: str \| bytes, doc: str \| bytes \| Mapping \| Document \| None = None) -> str` | Encode with `encode_for_style` and draw, in one call. |
| `to_png(code, style=None, size=512, logo=None, **kwargs) -> bytes` | PNG file bytes (`png` extra); `kwargs` as for `to_image`. |
| `to_image(code, style=None, size=512, logo=None, **kwargs) -> PIL.Image.Image` | RGBA image (`png` extra). |
| `qrkude.png.to_image(code, style=None, size=512, logo=None, *, height=None, supersample=4, label=True, font=None)` | `height` (default from the canvas shape), samples per axis (1-8), draw the caption or not, a Pillow font for the caption. |
| `qrkude.png.PolygonSink(scale=1.0, dx=0.0, dy=0.0, tolerance=0.1)` | A path sink that flattens arcs into point lists in `.polygons`. |
| `qrkude.png.MAX_SIDE`, `MAX_PIXELS`, `MAX_SAMPLES`, `MAX_LOGO_PIXELS` | PNG limits: 16384 px per side, 2^26 pixels, 2^26 samples per mask, 2^24 logo pixels. |
| `format_number(value: float) -> str` | Numbers as written in the SVG (up to 3 decimals). |
| `sanitize_id_prefix(prefix: str \| None) -> str` | The gradient id prefix as written. |

**Checks**

| Symbol | Description |
|---|---|
| `lint(code: QRCode, style: Style) -> list[str]` | Scannability warnings, in the order of `LINT_FLAGS`. |
| `LINT_FLAGS` | The tuple of all warning names. |
| `logo_damage(code: QRCode, style: Style) -> float` | Worst-case share of the error correction used by the logo (above 1 = unreadable). |

**Geometry, for your own renderer**

| Symbol | Description |
|---|---|
| `emit_layer(code: QRCode, style: Style, layer: Layer, sink: PathSink \| None = None) -> int` | Send the shapes of one layer to `sink`; returns the number of sub-paths. |
| `PathSink` | Protocol with `move_to(x, y)`, `line_to(x, y)`, `arc_to(r, clockwise, x, y, cx, cy)` and `close()`. |
| `Layer` | `IntEnum`, in drawing order: `BACKGROUND`, `BORDER`, `MODULES`, `EYE_FRAMES`, `EYE_BALLS`, `LOGO_BACKGROUND`. |
| `layer_paint(style: Style, layer: Layer) -> Paint` | The paint of a layer (eye paints resolved). |
| `layer_visible(code: QRCode, style: Style, layer: Layer) -> bool` | True when the layer has a color with alpha above 0 and something to draw. |
| `get_layout(code: QRCode, style: Style) -> LayoutInfo` | Canvas geometry. |
| `LayoutInfo` | `width`, `height`, `symbol_x`, `symbol_y`, `modules`, `has_logo`, `logo_x`, `logo_y`, `logo_size`, `has_label`, `label_x`, `label_baseline`, `label_size`. |
| `gradient_geometry(code: QRCode, style: Style, layer: Layer) -> GradientGeometry` | Gradient end points in canvas units. |
| `GradientGeometry` | `x1`, `y1`, `x2`, `y2` (linear), `cx`, `cy`, `r` (radial). |

**Content helpers**: `wifi`, `email`, `sms`, `tel`, `geo`, `vcard`, `event`,
also available as `qrkude.payloads.*` (signatures in
[Content helpers](#content-helpers)).

**Errors**

| Symbol | Description |
|---|---|
| `QRKudeError` | Base class (a `ValueError`); `.kind` is `"parse error"`, `"invalid argument"` or `"data too long"`. |
| `StyleError` | `kind == "parse error"`. |
| `InvalidArgumentError` | `kind == "invalid argument"`. |
| `DataTooLongError(message: str = "data too long")` | `kind == "data too long"`. |

**Other**

| Symbol | Description |
|---|---|
| `__version__` | `"0.1.0"`. |
| `qrkude.cli.main(argv: Sequence[str] \| None = None) -> int` | Run the command line tool; returns the exit status. |
| `qrkude.style.MAX_STOPS`, `qrkude.style.Color` | `8`; the `int` alias used for colors. |
| `qrkude.encoder.alignment_positions(version: int) -> tuple[int, ...]` | Centers of the alignment patterns of a version. |
| `qrkude.encoder.is_function_module(version: int, x: int, y: int) -> bool` | True for modules that are not data (eyes, timing, format and version information, alignment). |

Note that `qrkude.svg` and `qrkude.lint` are the functions above. Their
modules hold no other public names.

A minimal custom renderer prints the path commands of the data modules:

```python
import qrkude

class Printer:
    def move_to(self, x, y): print("move", x, y)
    def line_to(self, x, y): print("line", x, y)
    def arc_to(self, r, clockwise, x, y, cx, cy): print("arc", r, clockwise, x, y)
    def close(self): print("close")

code = qrkude.encode("hi")
style = qrkude.Style.from_json({"modules": {"shape": "dot"}})
count = qrkude.emit_layer(code, style, qrkude.Layer.MODULES, Printer())
print(count, "dots")  # -> 133 dots
```

Coordinates are in modules with y pointing down. Fill each layer with the
non-zero rule, in the order of `Layer`, then draw the logo and the caption.

## Platform notes

- **Python** 3.9 to 3.14, pure Python, any operating system. Tested on
  CPython 3.9.25 and 3.14.4.
- **Pillow** 9.0 or newer for PNG output. Tested with 9.0.1, 11.3 and 13.0.
- **Speed** (CPython 3.14, a laptop CPU): a short URL encodes in about 2 ms,
  its SVG takes about 3 ms and a 512 px PNG about 80 ms. The largest code
  (version 40) takes about 0.1 s to encode, 0.2 s for the SVG and 1 s for a
  1024 px PNG.
- **SVG output is identical** to the other QRKude libraries for the same
  input. This is checked against the shared test vectors.
- **Limitations**:
  - The encoder uses one data mode for the whole text, so mixed text can make
    a slightly larger code than an optimizing encoder would. Kanji mode,
    Micro QR and structured append are not supported.
  - PNG captions depend on the fonts installed on the system.
  - PNG output does not download remote logos and does not draw SVG logos.

## Development

From the root of a clone of the QRKude repository:

```sh
cd packages/python
python3 -m venv .venv
. .venv/bin/activate
pip install -e ".[png,test]" zxing-cpp cairosvg
pytest
```

The tests replay every conformance vector in `spec/vectors/` (the sdist
bundles a copy in `tests/vectors/`; `$QRKUDE_VECTORS` can point elsewhere).
They also decode PNG renders with zxing-cpp and compare them with cairosvg.
Those two packages are optional; their tests are skipped without them.
To build the wheel and the source distribution, run `pip install build` and
then `python -m build`.

Port authors: the normative behaviour is in [SPEC.md](../../docs/SPEC.md)
(`docs/SPEC.md` in the QRKude repository).

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
"QR Code" is a registered trademark of DENSO WAVE INCORPORATED.
