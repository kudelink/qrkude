# QRKude Specification v0.1

> **Who this is for.** This document is for people who write or maintain a
> QRKude package. It is precise rather than friendly. To *use* QRKude, read
> the [guide](GUIDE.md) and the [style reference](STYLES.md) instead; the
> words used below (module, finder pattern, quiet zone...) are explained in
> the guide's [glossary](GUIDE.md#glossary).

This document defines what every QRKude implementation must do. The C library
in [`core/`](../core) is the reference implementation; the conformance vectors
in [`spec/vectors/`](../spec/vectors) are generated from it. A port is
conformant when it reproduces every vector **byte for byte** (SVG output,
module matrices and payload strings), the `version`/`ecc`/`mask` values, the
`lint` flags, and `logoDamage` within `1e-6`.

Keywords MUST/SHOULD follow RFC 2119. All coordinates are in **modules**
(canvas units) with the y axis pointing down.

---

## 1. Encoding

QR Code Model 2, versions 1–40, as in ISO/IEC 18004.

### 1.1 Mode selection

The whole payload is a single segment (no mixed-mode optimization):

1. `numeric` if every byte is `0`–`9`;
2. else `alphanumeric` if every byte is in `0-9 A-Z space $ % * + - . / :`;
3. else `byte` (the UTF-8 bytes of the text).

An empty payload produces **no segment** (only terminator and padding).

### 1.2 Options (`encode` object)

| key          | type    | default | meaning                                                   |
|--------------|---------|---------|-----------------------------------------------------------|
| `ecc`        | string  | see 1.3 | `"L"`, `"M"`, `"Q"`, `"H"` — minimum error correction     |
| `minVersion` | int     | 1       | smallest version to try                                   |
| `maxVersion` | int     | 40      | largest version to try                                    |
| `mask`       | int     | -1      | -1 = automatic, 0–7 = forced                              |
| `boostEcc`   | bool    | true    | raise ECC while the chosen version still fits            |
| `eci`        | bool    | false   | prepend ECI designator 26 (UTF-8): `0111` + 8-bit `26`    |

### 1.3 Default ECC

When `encode.ecc` is absent the ECC is **`H` if the style has a logo** (see 4.4),
otherwise **`M`**.

### 1.4 Version and ECC choice

For `v` from `minVersion` to `maxVersion`, the first version where the bit
length fits the data capacity of `(v, ecc)` is chosen. The bit length is
`12` (if `eci`) + `4 + charCountBits(mode, v) + payloadBits` (if the payload
is non-empty). The character count must also be `< 2^charCountBits`.
Character count bits: numeric 10/12/14, alphanumeric 9/11/13, byte 8/16/16 for
versions 1–9/10–26/27–40. If no version fits the result is the error
**`data too long`**.

With `boostEcc`, for `e` in `ecc+1 .. H` (in order L<M<Q<H): if the bit length
fits `(v, e)`, `ecc = e`.

### 1.5 Codewords, matrix and masking

Standard ISO procedure: terminator (up to 4 zero bits), zero-pad to a byte,
alternate pad bytes `0xEC`, `0x11`; Reed–Solomon over GF(256) with polynomial
`0x11D`, generator roots `2^0 .. 2^(n-1)`; block interleaving with short
blocks first; zig-zag placement starting at the bottom-right, skipping
column 6; remainder bits light.

The automatic mask is the one with the lowest penalty (N1=3, N2=3, N3=40,
N4=10). Ties go to the **lowest mask number**. The penalty computation MUST
match the reference exactly, including the finder-like pattern rule that
treats the area outside the symbol as light (see `penalty_score` in
`core/src/qrk_encode.c`). The format information is drawn before the penalty
is evaluated for each candidate mask.

---

## 2. Style object

Unknown keys MUST be ignored. `null` is accepted where noted.

```jsonc
{
  "margin": 4,                         // quiet zone, modules (>= 0)
  "modules": {
    "shape": "square",                 // see 5.2
    "size": 1,                         // clamp to [0.1, 1]
    "roundness": 1,                    // clamp to [0, 1]
    "color": "#000000"                 // paint (2.1)
  },
  "eyes": {
    "frame": "square",                 // 7x7 ring, see 6
    "ball": "square",                  // 3x3 center
    "frameColor": null,                // paint or null = modules.color
    "ballColor": null
  },
  "alignment": {                       // null or absent frame = draw as data modules
    "frame": null,
    "ball": null                       // null = "square" when frame is set
  },
  "background": { "color": "#ffffff", "radius": 0 },   // color: paint or null (none)
  "border": { "width": 0, "color": "#000000" },        // color: solid only
  "logo": {                            // null / absent = no logo
    "href": null,                      // SVG <image> URL or data: URI
    "size": 0.2,                       // fraction of the symbol width, clamp [0, 1]
    "margin": 1,                       // clear space in modules (>= 0)
    "shape": "square",                 // "square" | "rounded" | "circle"
    "excavate": true,                  // remove modules under the logo area
    "background": null                 // solid color or null
  },
  "label": {                           // null / absent / empty text = no label
    "text": "SCAN ME",
    "font": "sans-serif",
    "size": 4,                         // font size in modules (min 0.1)
    "color": "#000000",
    "position": "bottom",              // "bottom" | "top"
    "bold": true
  }
}
```

A document may wrap it: `{"encode": {...}, "style": {...}, "svg": {...}}`.
Top-level style keys outside `"style"` are also accepted.

### 2.0 Reading the JSON

Readers MUST behave as follows, because styles often come from untrusted
sources (web forms, databases, devices on a network):

- **Unknown keys** of any length are ignored, whatever their value.
- **Document order**: members apply in order. A repeated key whose value is
  an object (e.g. two `modules` objects) merges field by field into what the
  earlier members set; scalars, arrays and paints replace the earlier value.
  `logo` takes its default size 0.2 only when no logo is present yet.
  `logo: null`, `label: null` and `alignment: null` reset that feature to
  its defaults (absent), forgetting every member set before.
- **Numbers** parse to the nearest IEEE double. A number token longer than
  400 characters, or one that is not finite (`1e999`), is a parse error
  wherever it appears, unknown keys included. Integer fields (`minVersion`,
  `maxVersion`, `mask`) clamp to [-1000, 1000] and then truncate toward zero.
- **Nesting** deeper than 64 objects/arrays (the top-level object is level 1)
  is a parse error.
- **Strings**: `\uXXXX` escapes decode to UTF-8; a surrogate that is not
  part of a valid high/low pair becomes U+FFFD. A string ends at its first
  U+0000 (C strings), so `"\u0000x"` reads as `""`; this applies to every
  string value and key (`"#f00\u0000x"` is the color `#f00`).
- Errors have three kinds: `parse error` (reading the JSON), `invalid
  argument` (raised when encoding, for options out of range such as
  `minVersion: 0`) and `data too long`.

The behaviour for JSON that is not well-formed is otherwise unspecified (the
reference reader is lenient). Conformance vectors only use numbers with at
most 15 significant digits and decimal exponents within ±22.

### 2.1 Colors and paints

Color strings: `#rgb`, `#rrggbb`, `#rrggbbaa` (case-insensitive) or
`"transparent"` (= `#00000000`). Internally `0xRRGGBBAA`.

A **paint** is a color string, `null` (none), or a gradient:

```jsonc
{ "type": "linear", "angle": 45,                 // degrees, 0 = left→right, 90 = top→bottom
  "stops": [ {"offset": 0, "color": "#6a11cb"}, {"offset": 1, "color": "#2575fc"} ] }
{ "type": "radial", "cx": 0.5, "cy": 0.5, "r": 0.75,  // relative to the painted area
  "stops": [ ... ] }
```

`type` defaults to `"linear"`, `angle` to 0, `cx`/`cy` to 0.5, `r` to 0.75.
At most 8 stops are used. A gradient without stops paints nothing (the SVG
still writes the empty gradient element) and counts as a missing paint for
`lint`.

### 2.2 Clamping summary

`clamp` maps NaN to the lower bound (only reachable through styles built in
code; JSON numbers are finite).

| value | rule |
|---|---|
| `margin`, `border.width`, `logo.margin` | `clamp(v, 0, 1000)` |
| `modules.size` | `clamp(v, 0.1, 1)` |
| `modules.roundness` | `clamp(v, 0, 1)` |
| `background.radius` | `clamp(v, 0, min(W, H) / 2)` |
| `logo.size` | `clamp(v, 0, 1)`; logo present iff `size > 0` |
| `label.size` | `clamp(v, 0.1, 1000)` |

---

## 3. Layout

Let `n` be the modules per side, `m` the margin, `b` the border width.

```
W  = n + 2(m + b)          canvas width
H  = W (+ bandH if label)  canvas height
ox = m + b                 x of module column 0
oy = m + b (+ bandH if label at top)
```

**Label band** (only when `label.text` is non-empty): `f = max(label.size, 0.1)`,
`bandH = 1.5 f`. Bottom: `bandY = W - b`. Top: `bandY = b`. Text anchor
`x = W / 2`, baseline `y = bandY + 1.1 f`.

**Logo** (when present): `cx = ox + n/2`, `cy = oy + n/2`,
`half = n * size / 2` (image box), `areaHalf = half + logo.margin`.

---

## 4. Module classification

For module `(x, y)` with `0 <= x, y < n`:

- **finder**: `(x<7 && y<7) || (x>=n-7 && y<7) || (x<7 && y>=n-7)`.
- **styled alignment**: alignment patterns are styled when `alignment.frame`
  is a valid shape. Then every module within ±2 of an alignment center that
  does not overlap a finder (the usual exclusion of the three corner
  positions) belongs to it.
- **excavated**: logo present, `excavate` true and the module is in the
  *logo area*:
  - `dx = |x + 0.5 - n/2|`, `dy = |y + 0.5 - n/2|`, `h = areaHalf`
  - `circle`: `max(dx-0.5,0)^2 + max(dy-0.5,0)^2 < h^2`
  - otherwise: `dx < h + 0.5 && dy < h + 0.5`
- **data cell**: in range and not finder, not styled alignment, not excavated.
- **data dark** `D(x,y)`: data cell and dark. Out of range is never dark.

Everything that is *data dark* is drawn with the module shape, including
timing, format, version and (unstyled) alignment modules.

---

## 5. Module geometry

### 5.1 Corner rectangles

Every shape is built from a **corner rectangle** `(x0, y0, x1, y1)` with a
treatment per corner in the order **TL, TR, BR, BL**: `none`, `round(a)` or
`chamfer(a)`. A corner with `a <= 0` behaves as `none`.

Clockwise emission (`a_k` = amount, 0 for `none`):

```
move (x0 + a_TL, y0)
line (x1 - a_TR, y0);  corner TR → (x1, y0 + a_TR)   center (x1 - a_TR, y0 + a_TR)
line (x1, y1 - a_BR);  corner BR → (x1 - a_BR, y1)   center (x1 - a_BR, y1 - a_BR)
line (x0 + a_BL, y1);  corner BL → (x0, y1 - a_BL)   center (x0 + a_BL, y1 - a_BL)
line (x0, y0 + a_TL);  corner TL → (x0 + a_TL, y0)   center (x0 + a_TL, y0 + a_TL)
close
```

A `round` corner is a clockwise arc of radius `a`; a `chamfer` corner is a
line; `none` emits nothing. The **counter-clockwise** variant (used for holes)
is:

```
move (x0, y0 + a_TL)
line (x0, y1 - a_BL);  corner BL → (x0 + a_BL, y1)
line (x1 - a_BR, y1);  corner BR → (x1, y1 - a_BR)
line (x1, y0 + a_TR);  corner TR → (x1 - a_TR, y0)
line (x0 + a_TL, y0);  corner TL → (x0, y0 + a_TL)
close
```

with counter-clockwise arcs and the same centers.

### 5.2 Shapes

`s` = size, `ρ` = roundness, `d = (1 - s) / 2`. Cell-local coordinates
(cell = `[0,1]²`, then translated by `(ox + x, oy + y)`).

| shape | geometry of a dark data module |
|---|---|
| `square` | rect `[d, 1-d]²`, no corners |
| `dot` | rect `[d, 1-d]²`, all `round(s/2)` |
| `rounded` | rect `[d, 1-d]²`, all `round(ρ s / 2)` |
| `diamond` | rect `[d, 1-d]²`, all `chamfer(s/2)` |
| `star` | see 5.3 with `h = s/2` |
| `fluid` | rect `[0,1]²`; corner `k` is `round(ρ/2)` when **both** orthogonal neighbours touching that corner are not `D` (TL: up & left, TR: up & right, BR: down & right, BL: down & left) |
| `liquid` | as `fluid`, plus fillets (5.4) |
| `bevel` | as `fluid` with `chamfer(ρ/2)` |
| `classy` | as `fluid` but only TL and BR may be rounded |
| `hbars` | `x0 = D(x-1,y) ? 0 : d`, `x1 = D(x+1,y) ? 1 : 1-d`, `y0 = d`, `y1 = 1-d`; TL/BL `round(ρ s/2)` if left is not `D`, TR/BR if right is not `D` |
| `vbars` | transposed `hbars` |

`fluid`, `liquid`, `bevel` and `classy` ignore `size`.

### 5.3 Star

Center `(cx, cy)`, tips at distance `h`, `r = sqrt(2.5) * h`, `c = 1.5 * h`:

```
move (cx, cy - h)
arc ccw r → (cx + h, cy)   center (cx + c, cy - c)
arc ccw r → (cx, cy + h)   center (cx + c, cy + c)
arc ccw r → (cx - h, cy)   center (cx - c, cy + c)
arc ccw r → (cx, cy - h)   center (cx - c, cy - c)
close
```

### 5.4 Liquid fillets

For a **data cell that is not dark** at `(x, y)` (canvas `X = ox+x`, `Y = oy+y`)
and `ρ > 0`, a fillet of radius `r = ρ/2` is added at corner:

- TL if `D(x,y-1) && D(x-1,y) && D(x-1,y-1)`: `move (X,Y) line (X+r,Y) arc ccw → (X, Y+r) close`
- TR if `D(x,y-1) && D(x+1,y) && D(x+1,y-1)`: `move (X+1,Y) line (X+1,Y+r) arc ccw → (X+1-r, Y) close`
- BR if `D(x,y+1) && D(x+1,y) && D(x+1,y+1)`: `move (X+1,Y+1) line (X+1-r,Y+1) arc ccw → (X+1, Y+1-r) close`
- BL if `D(x,y+1) && D(x-1,y) && D(x-1,y+1)`: `move (X,Y+1) line (X,Y+1-r) arc ccw → (X+r, Y+1) close`

Arc centers are the cell corner moved by `r` towards the cell center.

### 5.5 Emission order (`modules` layer)

- `square` with `size == 1`, and `hbars`: row by row (`y` ascending), each
  maximal horizontal run `[x0, x1)` of `D` is **one** corner rectangle
  `[ox+x0+i, oy+y+i, ox+x1-i, oy+y+1-i]` where `i = d` for `hbars` and `0` for
  squares; `hbars` corners are all `round(ρ s/2)`, squares none.
- `vbars`: column by column (`x` ascending), vertical runs `[y0, y1)` →
  `[ox+x+d, oy+y0+d, ox+x+1-d, oy+y1-d]`, all `round(ρ s/2)`.
- every other shape: row-major over all cells; a `D` cell emits its shape,
  otherwise (only `liquid`) a data cell emits its fillets in order TL, TR, BR, BL.

---

## 6. Eyes and alignment patterns

Shapes: `square`, `rounded`, `circle`, `leaf`, `drop`, `bevel`, `diamond`, `dots`.
For a box of size `s` whose corner `cc` faces the symbol center:

| shape | corners |
|---|---|
| `square` | none |
| `rounded` | all `round(0.25 s)` |
| `circle` | all `round(s/2)` |
| `leaf` | `cc` and the opposite corner `round(0.36 s)`, others none |
| `drop` | all `round(0.36 s)` except `cc` |
| `bevel` | all `chamfer(0.25 s)` |
| `diamond` | all `chamfer(s/2)` |
| `dots` | frames only: every ring module is a circle (`[cell], round(0.5)`) |

A **ball** with shape `dots` is drawn as `circle`.

Finders (top-left corners in module space, `cc`): `(0,0)` BR, `(n-7,0)` BL,
`(0,n-7)` TR. Alignment patterns use `cc = BR`.

**Frame** (size 7, or 5 for alignment) at `(X, Y)`: outer corner rectangle
`[X, Y, X+s, Y+s]` clockwise, then the inner one `[X+1, Y+1, X+s-1, Y+s-1]`
counter-clockwise with inner corners derived from the outer ones:
`round: a-1`, `chamfer: a-(2-sqrt(2))`, clamped to `[0, (s-2)/2]`; a clamped
value of 0 means `none`. `dots` frames emit circles for ring cells in
row-major order instead.

**Ball** (size 3 at `(X+2, Y+2)`, or 1 at the alignment center): one clockwise
corner rectangle.

Layer `eyeFrames` emits the three finder frames (TL, TR, BL), then the
styled alignment frames in row-major order of their centers (`y` outer,
`x` inner, from the alignment position list). Layer `eyeBalls` likewise.

---

## 7. Path normalization

All layers go through the same emitter, which MUST:

1. drop a `line` whose end equals the current point (exact float equality);
2. delay each `line`; when `close` follows and the pending line ends at the
   sub-path start, omit it;
3. update the current point after arcs to the arc end; after `close` to the
   sub-path start.

Fill rule: non-zero. Shapes are clockwise; holes counter-clockwise.

---

## 8. SVG serialization

### 8.1 Numbers

```
fmt(v): n = floor(v * 1000 + 0.5)   (as a 64-bit integer)
        sign "-" if n < 0, then |n| / 1000, then if |n| % 1000 != 0:
        "." followed by the 3-digit fraction with trailing zeros removed
```

Examples: `0.0625 → "0.063"`, `-0.0625 → "-0.062"`, `-0.0001 → "0"`.
NaN and values with `|v| >= 1e15` format as `"0"` (only reachable through
programmatic styles: JSON input is finite and lengths are clamped).

### 8.2 Path data

`M{x} {y}`; a line is `H{x}` if `y` equals the previous point's `y`, else
`V{y}` if `x` equals the previous `x`, else `L{x} {y}`;
`A{r} {r} 0 0 {1|0} {x} {y}` (1 = clockwise); `Z`. No separators between
commands.

### 8.3 Document

Elements, each followed by `\n`, in this order (bracketed parts optional):

```
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {W} {H}"[ width="{w}" height="{w*H/W}"]>
[<defs>{gradients}</defs>]
[<rect width="{W}" height="{H}"[ rx="{radius}"]{fill(background,"bg")}/>]
[<path d="{border}"{fill(border)}/>]
[<path d="{modules}"{fill(modules,"fg")}/>]
<path d="{eyeFrames}"{fill(frameColor ?? modules, "ef" or "fg")}/>
<path d="{eyeBalls}"{fill(ballColor ?? modules, "eb" or "fg")}/>
[<path d="{logoBackground}"{fill(logo.background)}/>]
[<image x="{cx-half}" y="{cy-half}" width="{2half}" height="{2half}" preserveAspectRatio="xMidYMid meet" xlink:href="{href}"/>]
[<text x="{W/2}" y="{baseline}" text-anchor="middle" font-family="{font}" font-size="{f}"[ font-weight="bold"] fill="{#rrggbb}"[ fill-opacity="{a}"]>{text}</text>]
</svg>
```

- `width`/`height` only when `svg.width > 0`.
- A `<path>` element is omitted when its layer emits no sub-path.
- The background `<rect>` is written when the paint is a gradient or a solid
  color with alpha > 0. `rx` only when the clamped radius is > 0.
- Border: outer canvas rectangle (all corners `round(radius)` if radius > 0)
  clockwise + inner rectangle inset by `b` (radius `radius - b` if > 0)
  counter-clockwise. Only when `b > 0`.
- Logo background: only when logo present and its alpha > 0. Rectangle
  `[cx-h, cy-h, cx+h, cy+h]` with `h = areaHalf`; `square`: none, `rounded`:
  all `round(0.3 h)`, `circle`: all `round(h)`.
- `<image>` only when the logo is present and `href` is non-empty. `<text>`
  only when the label text is non-empty. `font` empty/null → `sans-serif`.
- Text escaping (href, font, label text): `&`→`&amp;`, `<`→`&lt;`,
  `>`→`&gt;`, `"`→`&quot;`; characters U+0001–U+001F other than tab, LF
  and CR are dropped (XML 1.0 does not allow them).

**Id prefix.** `prefix` is `svg.idPrefix` sanitized: every byte of its UTF-8
encoding outside `[A-Za-z0-9_-]` becomes `_`; when it starts with a digit or
`-`, `q` is prepended; an empty or absent prefix is `qrk`. It is written
inside `id="..."` and `url(#...)`, so it can never carry markup (a style
from an untrusted source must not be able to inject attributes or elements).
Gradient ids are document-global in HTML: when several SVGs are inlined in
one page, give each a distinct `svg.idPrefix`.

**fill(paint, id)**: solid → ` fill="#rrggbb"` (lower-case) plus
` fill-opacity="{fmt(a/255)}"` when alpha ≠ 255; gradient →
` fill="url(#{prefix}-{id})"`; none (e.g. `"modules": {"color": null}`) →
` fill="none"`.

### 8.4 Gradients

`<defs>` is written when any of background, modules, frameColor, ballColor is
a gradient, containing in that order the gradients with ids `bg`, `fg`, `ef`,
`eb`:

```
<linearGradient id="{prefix}-{id}" gradientUnits="userSpaceOnUse" x1="" y1="" x2="" y2="">{stops}</linearGradient>
<radialGradient id="{prefix}-{id}" gradientUnits="userSpaceOnUse" cx="" cy="" r="">{stops}</radialGradient>
<stop offset="{o}" stop-color="#rrggbb"[ stop-opacity="{a}"]/>
```

Painted area: background → `(0, 0, W, H)`, everything else →
`(ox, oy, n, n)`. With area `(ax, ay, aw, ah)` and center `(cx, cy)`:

- **linear**: `a = angle mod 360` (into `[0, 360)`); direction `(dx, dy)` is
  exactly `(1,0)`, `(0,1)`, `(-1,0)`, `(0,-1)` for 0/90/180/270, otherwise
  `(cos, sin)` of `a·π/180`. `half = (|dx| aw + |dy| ah) / 2`;
  `x1 = cx - dx·half`, `y1 = cy - dy·half`, `x2 = cx + dx·half`, `y2 = cy + dy·half`.
- **radial**: `cx' = ax + aw·cx`, `cy' = ay + ah·cy`, `r' = r·max(aw, ah)`.

### 8.5 Layer visibility

Ports that expose `layerVisible(layer)` (C: `qrk_layer_visible`) return true
when the layer's resolved paint has at least one color with alpha > 0 (a
solid color, or any gradient stop), and additionally: border → `width > 0`;
logo background → a logo is present; modules → at least one sub-path. It is
a hint for native renderers that skip invisible layers; the SVG writer keeps
its own rules above.

---

## 9. Raster output (informative)

Raster renderers SHOULD draw exactly the SVG geometry (the reference
rasterizer point-samples the same shape model with supersampling and
matches cairo/librsvg output). Layers composite in SVG order with
source-over. Gradient colors interpolate per channel in sRGB, unpremultiplied,
clamped outside the first/last stop. Labels need a font engine and MAY be
omitted by raster backends without one (the band stays background-colored).
Backends that draw labels SHOULD follow SVG 1.1 whitespace handling (remove
newlines, turn tabs into spaces, collapse runs of spaces). Raster outputs
SHOULD refuse images larger than 16384 px on either side, since an untrusted
style can request very tall canvases (label size up to 1000 modules).

---

## 10. Logo damage

Estimates whether a logo still allows decoding. Walk the codeword placement
order (zig-zag, skipping function modules, ignoring remainder bits). A
codeword is **hidden** when any of its 8 modules is in the logo area (section
4, regardless of `excavate`). Map the interleaved index `k` to its block:
data part (`k < dataLen`): `k % numBlocks` while `k < shortLen·numBlocks`,
else `numShort + (k - shortLen·numBlocks)`; ECC part: `(k - dataLen) % numBlocks`.

`logoDamage = max over blocks of hidden / floor(eccPerBlock / 2)`;
0 without logo. Above 1 the symbol cannot be decoded.

## 11. Lint

`lint(code, style)` returns the set of:

| flag | condition |
|---|---|
| `logo-risky` | logo damage in `(0.7, 1]` |
| `logo-unreadable` | logo damage `> 1` |
| `low-contrast` | contrast < 3 (see below) |
| `inverted` | mean foreground luminance > mean background luminance |
| `transparent-background` | background is none or solid with alpha 0 (treated as white below) |
| `quiet-zone` | clamped margin `< 2` |
| `small-modules` | `size < 0.6` for square, dot, rounded, diamond, star, hbars, vbars |
| `eye-shapes` | eye frame is `diamond`, or the eye ball (after the dots→circle rule) is `diamond` and the frame is `square` or `leaf` |
| `invisible` | the resolved modules, eye frame or eye ball paint is none, or all of its colors have alpha 0 |

**Colors of a paint**: a solid paint has its color, a gradient its stop
colors; a missing paint is one fully transparent color (`#00000000`).

**Background**: its colors, each composited over white
(`channel = floor(c·a + 255·(1 − a) + 0.5)` per 8-bit channel with `a = alpha / 255`, result alpha 255). It is
`transparent-background` when every original color has alpha 0.

**Foreground**: for every color `f` of the resolved modules, eye frame and eye
ball paints and every background color `b`, the luminance of `f` composited
over `b` (in 0..1 floating point, `f·a + b·(1 − a)` per channel, not rounded).

Luminance is the WCAG relative luminance. `[bgLo, bgHi]` and `[fgLo, fgHi]`
are the ranges of those values; means are `(lo + hi) / 2`. Contrast =
`(bgLo + 0.05) / (fgHi + 0.05)`, or `(fgLo + 0.05) / (bgHi + 0.05)` when
inverted. Vectors list flags in the order of the table.

## 12. Payloads

| builder | output |
|---|---|
| `wifi(ssid, password, auth, hidden)` | `WIFI:` + (`T:{auth};` if auth) + `S:{esc ssid};` + (`P:{esc password};` if password and auth ≠ `nopass`) + (`H:true;` if hidden) + `;` — esc escapes `\ ; , : "` with `\` |
| `email(to, subject, body)` | `mailto:{to}` + `?subject=` / `&body=` (only non-empty, in that order, percent-encoded: keep `A-Z a-z 0-9 - _ . ~`, others `%XX` upper-case per UTF-8 byte) |
| `sms(number, message)` | `SMSTO:{number}` + (`:{message}` if message) |
| `tel(number)` | `tel:{number}` |
| `geo(lat, lon)` | `geo:{f6(lat)},{f6(lon)}` — latitude clamped to [-90, 90], longitude to [-180, 180]; `f6` is `fmt` with 6 decimals |
| `vcard(...)` | `BEGIN:VCARD\nVERSION:3.0\nN:{last};{first};;;\nFN:{first}[ ]{last}\n` + optional `ORG:`, `TITLE:`, `TEL:`, `EMAIL:`, `URL:`, `ADR:;;{address};;;;`, `NOTE:` lines + `END:VCARD`. Values escape `\ ; ,` with `\` and newline as `\n` |
| `event(...)` | `BEGIN:VEVENT\nSUMMARY:{summary}\n` + optional `DTSTART:`, `DTEND:`, `LOCATION:`, `DESCRIPTION:` lines + `END:VEVENT` (same escaping) |

"Optional" means omitted when null or empty. No trailing newline.

## 13. Conformance vectors

- `spec/vectors/encode.json`: `{text, encode, version, ecc, mask, mode, matrix[]}`
  (rows of `0`/`1`), or `{text, encode, error: "data too long"}`.
- `spec/vectors/render.json`: `{name, text, style, encode?, svg?, version, ecc,
  mask, lint[], logoDamage?, output}` — `output` is the exact SVG. Cases with
  a `json` string instead of `style`/`encode`/`svg` must be read from that
  raw text (they test duplicate keys, limits and errors); cases with `error`
  (`parse error`, `invalid argument`) must fail with that kind of error.
- `spec/vectors/payloads.json`: `{type, args, output}`.

Regenerate with `make -C core && python3 spec/tools/gen_vectors.py`.
