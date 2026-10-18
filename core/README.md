# QRKude core (C99)

A small C library that creates styled QR codes: it encodes your text, applies
a style (module shapes, eyes, colors, logo, frame) and writes an SVG file or
draws pixels for a screen. It is the reference implementation of
[QRKude](../README.md): every other QRKude package either wraps it (Qt,
Arduino, ESP-IDF, LVGL) or reproduces its output exactly (JavaScript, Python,
Rust, Dart).

<p>
<img src="../docs/images/liquid.svg" width="140" alt="Liquid modules">
<img src="../docs/images/gradient.svg" width="140" alt="Gradient with red eye frames">
<img src="../docs/images/frame.svg" width="140" alt="Framed code with a label">
</p>

Highlights for C developers:

- **No dependencies** besides the C standard library and `libm`.
- **No dynamic memory** unless you ask for it (see
  [Platform notes](#platform-notes)), so it fits microcontrollers.
- **Streaming output**: SVG goes through a write callback and pixels through
  a "span" callback, so nothing proportional to the image size is buffered.
- Builds warning-free with `-std=c99 -Wall -Wextra -Wpedantic -Wconversion`,
  and is tested with AddressSanitizer and UndefinedBehaviorSanitizer.

## Status and installation

Pre-release: there is no packaged release yet. Use it from a clone of this
repository in one of three ways.

**CMake** (3.16 or newer):

```cmake
add_subdirectory(path/to/qrkude/core)
target_link_libraries(my_app PRIVATE qrkude::qrkude)
```

**Make** (builds `build/libqrkude.a`, the command-line tool and the tests):

```sh
make -C core
```

**Copy the sources**: compile the files in `core/src/` and add
`core/include/` to your include path. That is all it needs.

For Arduino, ESP-IDF and LVGL there are ready-made packages:
[Arduino](../packages/arduino/README.md),
[ESP-IDF](../packages/esp-idf/README.md),
[LVGL](../packages/lvgl/README.md).

## Quick start

This program writes `qr.svg`, a QR code for `https://kudelink.com` with
connected round modules, a purple-to-blue gradient and round eyes.

```c
#include <stdio.h>
#include <string.h>
#include "qrkude.h"

/* SVG output arrives in pieces through this callback */
static void to_file(void *user, const char *data, size_t len)
{
    fwrite(data, 1, len, (FILE *)user);
}

int main(void)
{
    static qrk_code code; /* about 3.9 KB: keep it off small stacks */
    qrk_style style;
    qrk_style_default(&style);
    style.modules.shape = QRK_MODULE_LIQUID;
    style.modules.paint = qrk_paint_linear(45, QRK_HEX(0x6a11cb), QRK_HEX(0x2575fc));
    style.eyes.frame = QRK_EYE_ROUNDED;
    style.eyes.ball = QRK_EYE_CIRCLE;

    const char *text = "https://kudelink.com";
    /* NULL options = defaults; NULL scratch = the library allocates it once */
    if (qrk_encode_for_style(&code, text, strlen(text), NULL, &style, NULL) != QRK_OK)
        return 1;

    FILE *f = fopen("qr.svg", "wb");
    if (!f)
        return 1;
    qrk_svg_write(&code, &style, NULL, to_file, f);
    fclose(f);
    printf("version %d, %d x %d modules\n", code.version, code.size, code.size);
    return 0;
}
```

Build and run it:

```sh
cc -std=c99 -Icore/include quick.c core/src/*.c -lm -o quick && ./quick
```

New to QR codes? Read the [guide](../docs/GUIDE.md).

## Styling

A `qrk_style` holds every option of the
[style reference](../docs/STYLES.md). Start from `qrk_style_default()` and
change the fields you need:

| JSON key | C field | Type |
|---|---|---|
| `margin` | `style.margin` | `double` (modules) |
| `modules.shape` / `size` / `roundness` / `color` | `style.modules.shape` / `.size` / `.roundness` / `.paint` | `qrk_module_shape`, `double`, `double`, `qrk_paint` |
| `eyes.frame` / `ball` / `frameColor` / `ballColor` | `style.eyes.frame` / `.ball` / `.frame_paint` / `.ball_paint` | `qrk_eye_shape`, `qrk_paint` (`QRK_PAINT_NONE` = same as modules) |
| `alignment.frame` / `ball` | `style.alignment.frame` / `.ball` | `qrk_eye_shape` (`QRK_EYE_NONE` = draw as modules) |
| `background.color` / `radius` | `style.background.paint` / `.radius` | `qrk_paint`, `double` |
| `border.width` / `color` | `style.border.width` / `.color` | `double`, `qrk_color` |
| `logo.*` | `style.logo.size`, `.margin`, `.shape`, `.excavate`, `.background`, `.href` | `size` 0 means no logo |
| `label.*` | `style.label.text`, `.font`, `.size`, `.color`, `.position`, `.bold` | `text` NULL or empty means no label |

Colors are `qrk_color` values in `0xRRGGBBAA` form. `QRK_HEX(0x1d3557)`
gives an opaque color, `QRK_RGBA(r, g, b, a)` sets the alpha too. Paints are
built with `qrk_paint_solid()`, `qrk_paint_linear()` and
`qrk_paint_radial()`; for more than two gradient colors fill
`paint.stops[]` and `paint.stop_count` (up to `QRK_MAX_STOPS`).

A framed code with a logo area and a caption:

```c
qrk_style style;
qrk_style_default(&style);
style.margin = 2;
style.modules.shape = QRK_MODULE_DOT;
style.modules.paint = qrk_paint_solid(QRK_HEX(0x1d3557));
style.eyes.frame = QRK_EYE_CIRCLE;
style.eyes.ball = QRK_EYE_CIRCLE;
style.eyes.ball_paint = qrk_paint_solid(QRK_HEX(0xe63946));
style.background.paint = qrk_paint_solid(QRK_HEX(0xfff8e7));
style.background.radius = 3;
style.border.width = 1;
style.border.color = QRK_HEX(0x1d3557);
style.logo.size = 0.2;                 /* 20% of the code width */
style.logo.background = QRK_HEX(0xffffff);
style.logo.href = "logo.png";          /* used by the SVG output */
style.label.text = "SCAN ME";
```

### Styles from JSON

`qrk_style_from_json()` reads the shared JSON style document. It needs a
small "arena" buffer for the strings it finds (logo URL, label text, font);
the buffer must live as long as the style.

```c
static char arena[512];
qrk_style style;
qrk_encode_opts opts;
qrk_svg_opts svg;
qrk_style_default(&style);
qrk_encode_opts_default(&opts);
qrk_svg_opts_default(&svg);

const char *json = "{\"modules\":{\"shape\":\"dot\",\"size\":0.9},"
                   "\"eyes\":{\"frame\":\"circle\",\"ball\":\"circle\"}}";
qrk_status st = qrk_style_from_json(json, strlen(json), &style, &opts, &svg,
                                    arena, sizeof arena);
if (st != QRK_OK)
    printf("style error: %s\n", qrk_status_str(st));
```

Keys missing from the JSON keep their current value. In particular, when the
document has no `encode.ecc`, `opts.ecc` stays `QRK_ECC_AUTO` and
`qrk_encode_for_style()` picks M, or H when there is a logo.

## Output

### SVG

```c
qrk_svg_opts svg;
qrk_svg_opts_default(&svg);
svg.width = 512;          /* write width/height attributes, in pixels */
svg.id_prefix = "promo";  /* unique per SVG when several share a web page */
qrk_svg_write(&code, &style, &svg, to_file, f);

/* or into a buffer, snprintf-style: returns the full length */
char buf[65536];
size_t len = qrk_svg_to_buffer(&code, &style, &svg, buf, sizeof buf);
```

The SVG is identical, byte for byte, to what every other QRKude package
produces for the same style.

### Pixels (displays, image files)

The rasterizer produces horizontal **spans**: runs of pixels with the same
color, delivered row by row. That maps directly to `drawFastHLine()` on most
display libraries and needs no frame buffer.

```c
static void span(void *user, int x, int y, int len, qrk_color c)
{
    /* e.g. tft.drawFastHLine(x, y, len, rgb565(c)); */
}

qrk_raster_opts ro;
qrk_raster_opts_default(&ro, 240, 240);  /* output size in pixels */
ro.supersample = 2;                      /* anti-aliasing: 1 (off) to 4 */
ro.integer_scale = true;                 /* whole pixels per module (crisper) */
ro.clear_color = QRK_HEX(0x000000);      /* what shows outside the code */
qrk_render_spans(&code, &style, &ro, span, NULL);
```

Or render straight into a pixel buffer in one of seven formats (RGBA8888,
BGRA8888, RGB888, RGB565, RGB565 byte-swapped for SPI displays, GRAY8, 1-bit
MONO):

```c
static uint16_t fb[240 * 240];
qrk_raster_opts ro;
qrk_raster_opts_default(&ro, 240, 240);
qrk_render_buffer(&code, &style, &ro, QRK_PIXEL_RGB565, fb, 0);
```

A logo bitmap for raster output goes in `ro.logo` as a `qrk_image`
(`width`, `height`, `format`, `pixels`, `stride`). The rasterizer trusts
these dimensions, so `pixels` must really hold `height` rows. Labels are not
drawn by the rasterizer (there is no font engine); the label area stays
background-colored.

Advice: use at least 3 to 4 pixels per module, and keep `integer_scale`
on for small displays. See
[Size in print and on screen](../docs/GUIDE.md#size-in-print-and-on-screen).

### Vector geometry (QPainter, Skia, Cairo, PDF...)

To draw with your own graphics library, ask for the outline of each layer.
Coordinates are in modules; multiply by your scale.

```c
static void move_to(void *u, double x, double y) { /* start a sub-path */ }
static void line_to(void *u, double x, double y) { /* straight line */ }
static void arc_to(void *u, double r, bool cw, double x, double y,
                   double cx, double cy) { /* circular arc around (cx, cy), at most 90 degrees */ }
static void close_path(void *u) { /* close the sub-path */ }

qrk_path_sink sink = {move_to, line_to, arc_to, close_path};
for (int layer = 0; layer < QRK_LAYER_COUNT; layer++) {
    if (!qrk_layer_visible(&code, &style, (qrk_layer)layer))
        continue;
    qrk_paint paint = qrk_layer_paint(&style, (qrk_layer)layer);
    qrk_emit_layer(&code, &style, (qrk_layer)layer, &sink, my_context);
    /* fill the path with `paint`, non-zero winding rule */
}
```

`qrk_get_layout()` gives the canvas size, the position of the code, the logo
box and the label position. `qrk_gradient_geometry_for()` gives the exact
gradient coordinates for a layer.

## Content helpers

Each helper writes a ready-to-encode text into your buffer and returns the
full length, like `snprintf` (so you can call it with a NULL buffer to get
the size first).

| Function | Produces |
|---|---|
| `qrk_payload_wifi(out, cap, ssid, password, auth, hidden)` | `WIFI:T:WPA;S:...;P:...;;` (`auth`: `"WPA"`, `"WEP"`, `"nopass"` or NULL) |
| `qrk_payload_vcard(out, cap, &card)` | a vCard 3.0 contact from a `qrk_vcard` struct |
| `qrk_payload_event(out, cap, &event)` | a calendar event from a `qrk_event` struct |
| `qrk_payload_email(out, cap, to, subject, body)` | `mailto:` link |
| `qrk_payload_sms(out, cap, number, message)` | `SMSTO:` text |
| `qrk_payload_tel(out, cap, number)` | `tel:` link |
| `qrk_payload_geo(out, cap, latitude, longitude)` | `geo:` location |

```c
char text[256];
qrk_payload_wifi(text, sizeof text, "Kude Guest", "welcome-2026", "WPA", false);
qrk_encode_for_style(&code, text, strlen(text), NULL, &style, NULL);

qrk_vcard card = {.first_name = "Ana", .last_name = "García", .phone = "+34 600 000 000"};
qrk_payload_vcard(text, sizeof text, &card);
```

## Making sure it scans

```c
uint32_t warnings = qrk_lint(&code, &style);
for (uint32_t flag = 1; flag <= QRK_LINT_INVISIBLE; flag <<= 1)
    if (warnings & flag)
        printf("warning: %s\n", qrk_lint_name(flag));

double damage = qrk_logo_damage(&code, &style);  /* 0 without a logo, > 1 = unreadable */
```

The [guide](../docs/GUIDE.md#styling-without-breaking-scanning) explains
each warning and how to fix it.

## Untrusted styles

`qrk_style_from_json()` is safe to use on styles from users or the network:

- documents nested deeper than 64 levels, numbers longer than 400
  characters and non-finite numbers (`1e999`) give `QRK_ERR_PARSE`;
- strings longer than the arena give `QRK_ERR_BUFFER_TOO_SMALL`;
- the parser does bounded work and bounded recursion on any input;
- lengths are clamped (margins, borders, label size up to 1000 modules);
- the SVG writer escapes all text, drops control characters, and reduces the
  gradient id prefix to `[A-Za-z0-9_-]`.

Encoding options out of range (for example `minVersion` 0) make
`qrk_encode()` return `QRK_ERR_INVALID_ARGUMENT`.

## API reference

All declarations are in [`include/qrkude.h`](include/qrkude.h).

**Status and version**

| Symbol | Description |
|---|---|
| `QRK_LIB_VERSION` | Library version string, `"0.1.0"` |
| `qrk_status` | `QRK_OK`, `QRK_ERR_INVALID_ARGUMENT`, `QRK_ERR_DATA_TOO_LONG`, `QRK_ERR_OUT_OF_MEMORY`, `QRK_ERR_BUFFER_TOO_SMALL`, `QRK_ERR_PARSE` |
| `const char *qrk_status_str(qrk_status)` | Readable text for a status |

**Encoding**

| Symbol | Description |
|---|---|
| `qrk_ecc` | `QRK_ECC_AUTO`, `QRK_ECC_LOW`, `QRK_ECC_MEDIUM`, `QRK_ECC_QUARTILE`, `QRK_ECC_HIGH` |
| `qrk_mode` | `QRK_MODE_NUMERIC`, `QRK_MODE_ALPHANUMERIC`, `QRK_MODE_BYTE` (chosen automatically) |
| `qrk_encode_opts` | `ecc`, `min_version`, `max_version`, `mask` (-1 = automatic), `boost_ecc`, `eci` |
| `void qrk_encode_opts_default(qrk_encode_opts *)` | Defaults: AUTO, versions 1 to `QRK_MAX_VERSION`, automatic mask, boost on, no ECI |
| `qrk_code` | Result: `version`, `size`, `ecc`, `mask`, `mode`, `modules[]` (bit matrix) |
| `qrk_status qrk_encode(qrk_code *, const void *data, size_t len, const qrk_encode_opts *, uint8_t *tmp)` | Encode bytes. `tmp` is NULL or a `QRK_BUFFER_LEN_MAX` scratch buffer. AUTO means M |
| `qrk_status qrk_encode_for_style(qrk_code *, const void *data, size_t len, const qrk_encode_opts *, const qrk_style *, uint8_t *tmp)` | Same, but AUTO means H when the style has a logo |
| `qrk_status qrk_encode_text(qrk_code *, const char *text, const qrk_encode_opts *, uint8_t *tmp)` | Encode a NUL-terminated string |
| `bool qrk_get_module(const qrk_code *, int x, int y)` | True for a dark module; false outside the grid |
| `QRK_MAX_VERSION`, `QRK_SIZE_FOR_VERSION(v)`, `QRK_BUFFER_LEN_FOR_VERSION(v)`, `QRK_BUFFER_LEN_MAX` | Size limits and buffer sizes |

**Style**

| Symbol | Description |
|---|---|
| `qrk_style` | All style options (see [Styling](#styling)) |
| `void qrk_style_default(qrk_style *)` | Black square modules on white, margin 4, no logo, no label |
| `qrk_color`, `QRK_RGBA`, `QRK_RGB`, `QRK_HEX`, `QRK_TRANSPARENT` | Colors in `0xRRGGBBAA` form |
| `qrk_paint`, `qrk_stop`, `qrk_paint_type`, `QRK_MAX_STOPS` | Solid colors and gradients |
| `qrk_paint qrk_paint_solid(qrk_color)` | Solid paint |
| `qrk_paint qrk_paint_linear(double angle_deg, qrk_color from, qrk_color to)` | Two-color linear gradient |
| `qrk_paint qrk_paint_radial(qrk_color center, qrk_color edge)` | Two-color radial gradient |
| `qrk_module_shape`, `qrk_eye_shape`, `qrk_logo_shape`, `qrk_label_position` | Shape and position enums |
| `int qrk_module_shape_from_name(const char *)`, `const char *qrk_module_shape_name(int)` | Convert between enum values and JSON names |
| `int qrk_eye_shape_from_name(const char *)`, `const char *qrk_eye_shape_name(int)` | Same for eye shapes |
| `bool qrk_parse_color(const char *str, size_t len, qrk_color *out)` | Parse `#rgb`, `#rrggbb`, `#rrggbbaa`, `transparent` |
| `qrk_ecc qrk_recommended_ecc(const qrk_style *)` | H with a logo, otherwise M |
| `qrk_status qrk_style_from_json(const char *json, size_t len, qrk_style *, qrk_encode_opts *, qrk_svg_opts *, char *arena, size_t arena_len)` | Read the shared JSON document; any output pointer may be NULL |

**Scannability**

| Symbol | Description |
|---|---|
| `uint32_t qrk_lint(const qrk_code *, const qrk_style *)` | Warning flags `QRK_LINT_*` |
| `QRK_LINT_LOGO_RISKY`, `QRK_LINT_LOGO_UNREADABLE`, `QRK_LINT_LOW_CONTRAST`, `QRK_LINT_INVERTED`, `QRK_LINT_TRANSPARENT_BACKGROUND`, `QRK_LINT_QUIET_ZONE`, `QRK_LINT_SMALL_MODULES`, `QRK_LINT_EYE_SHAPES`, `QRK_LINT_INVISIBLE` | The flags |
| `const char *qrk_lint_name(uint32_t flag)` | `"logo-risky"`, `"low-contrast"`, ... |
| `double qrk_logo_damage(const qrk_code *, const qrk_style *)` | Share of the error correction used by the logo |

**Geometry**

| Symbol | Description |
|---|---|
| `qrk_layout`, `void qrk_get_layout(const qrk_code *, const qrk_style *, qrk_layout *)` | Canvas size, code origin, logo box, label anchor (in modules) |
| `qrk_layer` | `QRK_LAYER_BACKGROUND`, `_BORDER`, `_MODULES`, `_EYE_FRAMES`, `_EYE_BALLS`, `_LOGO_BACKGROUND` |
| `qrk_path_sink` | Callbacks `move_to`, `line_to`, `arc_to`, `close` |
| `int qrk_emit_layer(const qrk_code *, const qrk_style *, qrk_layer, const qrk_path_sink *, void *user)` | Outline of one layer; returns the number of sub-paths |
| `bool qrk_layer_visible(const qrk_code *, const qrk_style *, qrk_layer)` | Whether a layer draws anything |
| `qrk_paint qrk_layer_paint(const qrk_style *, qrk_layer)` | Paint of a layer (eye colors resolved) |
| `qrk_gradient_geometry`, `void qrk_gradient_geometry_for(const qrk_code *, const qrk_style *, qrk_layer, qrk_gradient_geometry *)` | Gradient coordinates |

**SVG**

| Symbol | Description |
|---|---|
| `qrk_svg_opts`, `void qrk_svg_opts_default(qrk_svg_opts *)` | `width` (pixels, 0 = none) and `id_prefix` |
| `qrk_write_fn` | `void (*)(void *user, const char *data, size_t len)` |
| `qrk_status qrk_svg_write(const qrk_code *, const qrk_style *, const qrk_svg_opts *, qrk_write_fn, void *user)` | Stream the SVG |
| `size_t qrk_svg_to_buffer(const qrk_code *, const qrk_style *, const qrk_svg_opts *, char *buf, size_t cap)` | SVG into a buffer; returns the full length |
| `size_t qrk_format_number(double, char out[32])` | The number format used in the SVG |

**Raster**

| Symbol | Description |
|---|---|
| `qrk_pixel_format` | `QRK_PIXEL_RGBA8888`, `_BGRA8888`, `_RGB888`, `_RGB565`, `_RGB565_SWAPPED`, `_GRAY8`, `_MONO` |
| `qrk_image` | A logo bitmap: `width`, `height`, `format`, `pixels`, `stride` |
| `qrk_raster_opts`, `void qrk_raster_opts_default(qrk_raster_opts *, int width, int height)` | Size, `supersample`, `integer_scale`, `clear_color`, `logo` |
| `qrk_span_fn` | `void (*)(void *user, int x, int y, int len, qrk_color)` |
| `qrk_status qrk_render_spans(const qrk_code *, const qrk_style *, const qrk_raster_opts *, qrk_span_fn, void *user)` | Render as spans |
| `size_t qrk_pixel_buffer_size(qrk_pixel_format, int width, int height)` | Bytes needed for a buffer |
| `qrk_status qrk_render_buffer(const qrk_code *, const qrk_style *, const qrk_raster_opts *, qrk_pixel_format, void *pixels, size_t stride)` | Render into a buffer (`stride` 0 = packed rows) |

**Content helpers**: see [Content helpers](#content-helpers); also the
`qrk_vcard` and `qrk_event` structs.

## Platform notes

**Memory**

| Item | Size |
|---|---|
| `qrk_code` with `QRK_MAX_VERSION` 40 / 20 / 10 / 4 | 3922 / 1182 / 412 / 142 bytes |
| Encoding scratch buffer | `QRK_BUFFER_LEN_MAX` bytes (about the size of `qrk_code`) |
| `qrk_style` (64-bit build) | 808 bytes |
| Rendering | about 2.6 KB of stack on ESP32, no heap |
| JSON reader | about 1.5 KB of stack plus your arena |

**Compile-time options**

| Define | Effect |
|---|---|
| `QRK_MAX_VERSION=n` | Largest code size supported (default 40). Lower it on small microcontrollers. |
| `QRK_NO_MALLOC` | `qrk_encode` never allocates; you must pass a scratch buffer. |
| `QRK_NO_LOCALE` | Do not use `localeconv()` in the JSON reader (set automatically on AVR). |

**Speed** (desktop, one core): encoding a version 40 code takes about 9 ms;
a version 10 code under 1 ms. Rendering 240 × 240 pixels with 2 × 2
anti-aliasing takes about 5 ms. Microcontrollers are roughly 20 to 50 times
slower.

**Standards**: QR Code Model 2, versions 1 to 40 (ISO/IEC 18004). Micro QR
and rMQR are not supported.

## Development

```sh
make -C core test               # build everything and run the unit tests
core/build/qrkude --help        # the command-line tool
core/build/qrkude "https://kudelink.com" -s style.json -o qr.png -p 1024
echo -n "data" | core/build/qrkude --stdin --info --matrix

python3 spec/tools/gen_vectors.py     # regenerate the shared test cases
python3 spec/tools/check_decode.py    # decode every gallery style with zxing-cpp
```

See [CONTRIBUTING.md](../CONTRIBUTING.md) for how changes flow to the other
packages.

## License

Apache License 2.0. See [LICENSE](../LICENSE).
