<!--
Copyright 2026 The QRKude Authors
SPDX-License-Identifier: Apache-2.0
-->

# QRKude for Qt

A C++ library that draws styled QR codes with Qt: round or connected
modules, custom corner eyes, gradients, logos, borders and text labels.

It is the Qt package of QRKude. It wraps the QRKude C core, so its shapes,
its JSON style format and its SVG output are the same as in every other
QRKude package.

<p>
<img src="../../docs/images/liquid.svg" width="120" alt="liquid modules">
<img src="../../docs/images/dots.svg" width="120" alt="dot modules">
<img src="../../docs/images/gradient.svg" width="120" alt="gradient">
<img src="../../docs/images/logo.svg" width="120" alt="logo">
<img src="../../docs/images/frame.svg" width="120" alt="border and label">
</p>

## Status and installation

QRKude is pre-release (version 0.1.0). The API may still change. There are
no binary packages: you build the library from a local clone of the QRKude
repository, as part of your own project.

Supported: Qt 5.12 or newer and Qt 6, a C++17 compiler, and CMake 3.16 or
newer (3.18 for `FetchContent` with `SOURCE_SUBDIR`) or qmake. The library
needs the Qt modules Core, Gui and Widgets. The QML item also needs Qml and
Quick.

### CMake

Add the package directory to your project. The package finds Qt itself
(Qt 6 if installed, otherwise Qt 5) and builds the C core with it.

```cmake
add_subdirectory(/path/to/qrkude/packages/qt qrkude-qt)
target_link_libraries(myapp PRIVATE QRKude::Qt)
```

Or with `FetchContent`, pointing at the local clone:

```cmake
include(FetchContent)
FetchContent_Declare(qrkude
    SOURCE_DIR /path/to/qrkude      # the root of your QRKude clone
    SOURCE_SUBDIR packages/qt)
FetchContent_MakeAvailable(qrkude)
target_link_libraries(myapp PRIVATE QRKude::Qt)
```

`FetchContent_Declare` also accepts `GIT_REPOSITORY` and `GIT_TAG` instead
of `SOURCE_DIR` if you host the repository in git.

Targets:

| Target | Contents | Qt modules |
|---|---|---|
| `QRKude::Qt` | `Style`, `Code`, `Renderer`, `QRKudeWidget` and the C core | Core, Gui, Widgets |
| `QRKude::Quick` | `QRKudeItem` for QML; only exists when Qt Quick is found | adds Qml, Quick |

Options (set them with `-D` or before `add_subdirectory`):

| Option | Default | Meaning |
|---|---|---|
| `QRKUDE_QT_MAJOR` | empty | `5` or `6` to choose the Qt version; empty = Qt 6 if found, else Qt 5 |
| `QRKUDE_QT_BUILD_QML` | `ON` | build `QRKude::Quick` (skipped with a message when Qt Quick is missing) |
| `QRKUDE_QT_BUILD_EXAMPLES` | `ON` only for a top-level build | build `examples/designer` and `examples/qml` |
| `QRKUDE_QT_BUILD_TESTS` | `ON` only for a top-level build | build `tests/tst_qrkude` |
| `QRKUDE_CORE_DIR` | `../../core` | where the C core is |

### qmake

Include the project include file. It adds the Qt modules, C++17, the include
paths, the library sources and the C core sources to your project.

```qmake
include(/path/to/qrkude/packages/qt/qrkude.pri)
```

`QRKudeItem` is compiled in when Qt Quick is available. Write
`CONFIG += qrkude_no_quick` before the `include` to leave it out, and
`QRKUDE_CORE_DIR = ...` to use a core from another place.

## Quick start

This program writes a QR code for `https://example.com` as a PNG and an SVG
file.

`main.cpp`:

```cpp
#include <QDebug>
#include <QFile>
#include <QGuiApplication>

#include <qrkudeqt.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv); // fonts and images need a GUI application

    QRKude::Style style;
    style.setModuleShape(QRKude::ModuleShape::Fluid);
    style.setEyeFrame(QRKude::EyeShape::Rounded);
    style.setEyeBall(QRKude::EyeShape::Circle);
    style.setColor(QColor("#1d3557"));

    QRKude::Renderer qr(QStringLiteral("https://example.com"), style);
    if (!qr.isValid()) {
        qWarning() << "cannot encode:" << qr.code().errorString();
        return 1;
    }

    qr.toImage(512).save("qrcode.png"); // 512 x 512 pixels

    QFile svg("qrcode.svg");
    if (svg.open(QIODevice::WriteOnly))
        svg.write(qr.toSvg());

    qInfo() << "version" << qr.code().version() << "warnings" << qr.lint();
    return 0;
}
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hello_qr LANGUAGES CXX)

add_subdirectory(/path/to/qrkude/packages/qt qrkude-qt)

add_executable(hello_qr main.cpp)
target_link_libraries(hello_qr PRIVATE QRKude::Qt)
```

Build and run:

```sh
cmake -S . -B build
cmake --build build
./build/hello_qr        # on a machine without a display: QT_QPA_PLATFORM=offscreen ./build/hello_qr
```

It prints `version 2 warnings ()` and writes two files: `qrcode.png`, a
512 x 512 image with dark blue connected modules, rounded corner eyes with
round centers and a white margin, and `qrcode.svg`, the same code as a
vector image. The version is the size of the code: version 1 has 21 x 21
modules, and each version adds 4 per side, so version 2 has 25 x 25. Longer
text needs a larger version. The empty list means the code passed all
scannability checks.

The same program builds with qmake from this `hello_qr.pro`:

```qmake
TEMPLATE = app
CONFIG += qrkude_no_quick
include(/path/to/qrkude/packages/qt/qrkude.pri)
SOURCES += main.cpp
```

New to QR codes? Read the [guide](../../docs/GUIDE.md).

## Styling

A `QRKude::Style` describes everything about the look of a code: the shape
of the small squares ("modules"), the shape and color of the three corner
squares ("eyes", see [anatomy of a QR code](../../docs/GUIDE.md#anatomy-of-a-qr-code)),
the background, an optional border, logo and label. It does not contain the
text. Unfamiliar terms are explained in the
[glossary](../../docs/GUIDE.md#glossary).

Every style option, with pictures, is listed in
[STYLES.md](../../docs/STYLES.md): [modules](../../docs/STYLES.md#modules),
[eyes](../../docs/STYLES.md#eyes),
[alignment patterns](../../docs/STYLES.md#alignment-patterns),
[colors and gradients](../../docs/STYLES.md#colors-and-gradients),
[background and border](../../docs/STYLES.md#background-and-border),
[logo](../../docs/STYLES.md#logo), [label](../../docs/STYLES.md#label), and a
[complete example](../../docs/STYLES.md#complete-example).

You can build a style in two ways:

- with setters that take Qt types (`QColor`, `QGradientStops`, enums);
- from a JSON document. The JSON format is the same in every QRKude
  package, so a style designed in one place can be used everywhere.

`Style::toJson()` turns a style back into JSON. Sizes and distances are
measured in modules (one module = one small square of the code).

### Example: brand colors with a gradient

```cpp
QRKude::Style style;
style.setModuleShape(QRKude::ModuleShape::Dot);
style.setModuleSize(0.9); // dots slightly smaller than their cell
style.setLinearGradient({{0.0, QColor("#0b3d91")}, {1.0, QColor("#00a896")}}, 45);
style.setEyeFrame(QRKude::EyeShape::Rounded);
style.setEyeBall(QRKude::EyeShape::Rounded);
style.setEyeFramePaint(QColor("#e63946")); // eyes in an accent color

QRKude::Renderer qr(QStringLiteral("https://example.com/shop"), style);
qr.toImage(600).save("shop.png");
```

The gradient runs at 45 degrees (top-left to bottom-right) across the code.
The eye balls have no paint of their own, so they use the module gradient.

### Example: a JSON style with a frame and a label

```cpp
const QByteArray json = R"({
    "margin": 2,
    "modules": {"shape": "liquid", "color": "#1d3557"},
    "eyes": {"frame": "rounded", "ball": "circle"},
    "background": {"color": "#fff8e7", "radius": 3},
    "border": {"width": 1, "color": "#1d3557"},
    "label": {"text": "SCAN ME", "color": "#1d3557"}
})";

bool ok = false;
QString error;
const QRKude::Style style = QRKude::Style::fromJson(json, &ok, &error);
if (!ok)
    qWarning() << error;

QRKude::Renderer qr(QStringLiteral("https://example.com/event"), style);
qr.toImage(600).save("event.png"); // taller than wide: the label band is added below
```

`fromJson` accepts a bare style object, as here, or a whole document
`{"encode": {...}, "style": {...}, "svg": {...}}`. Read the `encode` part
with `QRKude::EncodeOptions::fromJson(json)` and the `svg` part with
`QRKude::SvgOptions::fromJson(json)`.

### Example: a logo in the middle

```cpp
QRKude::Style style;
style.setModuleShape(QRKude::ModuleShape::Rounded);
style.setLogoSize(0.2);                          // 20% of the code width
style.setLogoMargin(0.5);                        // half a module of free space around it
style.setLogoShape(QRKude::LogoShape::Circle);   // clear a round area
style.setLogoBackground(Qt::white);

// With a logo, the error correction level is High automatically.
QRKude::Renderer qr(QStringLiteral("https://example.com/menu"), style);

const QImage logo("logo.png");
qr.toImage(800, logo).save("menu.png");
qInfo() << "logo damage" << qr.logoDamage(); // 0.545; keep it below about 0.6
```

A logo hides part of the code. The code still scans because of
[error correction](../../docs/GUIDE.md#error-correction): the code carries
redundant data that lets a scanner rebuild what is missing. Read
[logos](../../docs/GUIDE.md#logos) before choosing a size. Instead of passing the image, you can store it in the style
with `style.setLogoHref(...)`: a `data:` URI, a file path or a Qt resource
path such as `":/logo.png"`. Only `data:` URIs end up inside the SVG file.

## Output

All output starts from a `QRKude::Renderer`, which holds a `Code` (the
encoded text) and a `Style`.

**QImage.** `toImage(width, logo)` draws the code into a new image of
`width` pixels. The height follows the aspect ratio. Parts without
background stay transparent.

**Any QPainter.** `paint(painter, rect, logo)` draws into a rectangle of a
widget, image, printer or PDF. The code is centered in the rectangle and
keeps its aspect ratio. Drawing on a `QPdfWriter` or `QPrinter` gives vector
output, which is the best choice for print:

```cpp
QRKude::Renderer qr(QStringLiteral("https://example.com/poster"), QRKude::Style());

QPdfWriter pdf("poster.pdf");
pdf.setPageSize(QPageSize(QPageSize::A6));
pdf.setResolution(300);                         // 300 units per inch
QPainter painter(&pdf);
const double side = 4.0 / 2.54 * 300;           // 4 cm in PDF units
qr.paint(painter, QRectF(150, 150, side, side));
painter.end();
```

**SVG.** `toSvg()` returns a complete SVG document. It is written by the C
core and is byte-for-byte the same as the SVG from every other QRKude
package. `QRKude::SvgOptions` sets a `width` attribute and a prefix for the
gradient ids (give each SVG on one web page its own prefix):

```cpp
QRKude::Renderer qr(QStringLiteral("https://example.com"), QRKude::Style());
QRKude::SvgOptions options;
options.width = 300;            // width="300" height="300"
options.idPrefix = "footer";    // gradient ids become "footer-fg", ... (if there are gradients)
const QByteArray svg = qr.toSvg(options);
```

**One layer at a time.** For effects that the style cannot express, draw
the layers yourself. `layerPath(layer)` returns the shapes of one layer as a
`QPainterPath` and `layerBrush(layer)` its fill (a color or a gradient).
Both use modules as units; `canvasTransform(rect)` maps them into your
rectangle. The layers, from bottom to top, are `Background`, `Border`,
`Modules`, `EyeFrames`, `EyeBalls` and `LogoBackground`. This example adds a
soft shadow below the modules and eyes:

```cpp
QRKude::Style style;
style.setModuleShape(QRKude::ModuleShape::Fluid);
QRKude::Renderer qr(QStringLiteral("https://example.com"), style);

QImage image(600, 600, QImage::Format_ARGB32_Premultiplied);
image.fill(Qt::white);
QPainter painter(&image);
painter.setRenderHint(QPainter::Antialiasing);
painter.setTransform(qr.canvasTransform(QRectF(image.rect())));

using QRKude::Layer;
for (Layer layer : {Layer::Modules, Layer::EyeFrames, Layer::EyeBalls}) {
    painter.fillPath(qr.layerPath(layer).translated(0.15, 0.15), QColor(0, 0, 0, 60));
    painter.fillPath(qr.layerPath(layer), qr.layerBrush(layer));
}
painter.end();
image.save("shadow.png");
```

`layout()` tells you where things are: the canvas size, the position of the
first module, the logo box and the label position, all in modules.

**Resolution.** A QR code scans reliably when each module is big enough.
For screens, use at least 3 to 4 pixels per module. The canvas is
`renderer.canvasSize().width()` modules wide, so `toImage(qRound(canvasWidth * 4))`
gives 4 pixels per module. For print, prefer vector output (PDF or SVG). If
you need a bitmap, compute the width from the printed size:
`pixels = centimeters / 2.54 * dpi`, for example 3 cm at 300 dpi = 354
pixels. See [size in print and on screen](../../docs/GUIDE.md#size-in-print-and-on-screen)
for minimum sizes and scanning distance.

## Widgets and QML

### QRKudeWidget

A `QWidget` that shows a code. It repaints when a property changes, keeps
the aspect ratio of the code, and shows the error message when the text
cannot be encoded.

```cpp
auto *qrWidget = new QRKudeWidget;
QObject::connect(qrWidget, &QRKudeWidget::codeChanged, [qrWidget] {
    qInfo() << "warnings:" << qrWidget->lint() << qrWidget->errorString();
});
qrWidget->setText(QStringLiteral("https://example.com"));
qrWidget->setStyleJson(QStringLiteral(R"({"modules": {"shape": "dot"}})"));
qrWidget->setLogo(QImage(":/logo.png"));      // used when the style has a logo size
qrWidget->show();
```

| Property | Type | Meaning |
|---|---|---|
| `text` | `QString` | the text to encode |
| `styleJson` | `QString` | the style as JSON; invalid JSON keeps the old style and sets `errorString` |
| `logo` | `QImage` | the logo image (null: load it from the style's logo href) |
| `valid` | `bool`, read-only | `false` when the text could not be encoded |
| `errorString` | `QString`, read-only | the last error, empty when fine |
| `lint` | `QStringList`, read-only | scannability warnings |

Use `setCodeStyle()` and `setEncodeOptions()` to pass C++ objects instead of
JSON, `toImage(width)` to export, and `renderer()` for everything else
(SVG, layout, logo damage).

### QRKudeItem (QML)

`QRKudeItem` is a Qt Quick item. Link `QRKude::Quick` (CMake) or use the
`.pri` file with Qt Quick installed. Register the type once, before loading
QML:

```cpp
#include <qrkudeitem.h>

QRKude::registerQmlTypes(); // QML module "QRKude", version 1.0
```

```qml
import QtQuick 2.12
import QRKude 1.0

QRKudeItem {
    width: 300
    height: width * implicitHeight / implicitWidth   // keep the aspect ratio
    text: "https://example.com"
    styleJson: '{"background": {"radius": 3}, "logo": {"size": 0.22, "shape": "circle"}}'
    moduleShape: "liquid"
    eyeFrame: "rounded"
    eyeBall: "circle"
    color: "#1d3557"
    backgroundColor: "white"
    logoSource: "qrc:/logo.png"
    label: "SCAN ME"
}
```

`styleJson` is the base style. The other style properties change one value
of it when they are set (a non-empty string or a valid color). Shape names
are the JSON names, for example `"square"`, `"dot"`, `"fluid"`, `"liquid"`
for modules and `"square"`, `"rounded"`, `"circle"`, `"leaf"` for eyes.

| Property | Type | Meaning |
|---|---|---|
| `text` | `string` | the text to encode |
| `styleJson` | `string` | base style as JSON; invalid JSON keeps the last valid style |
| `moduleShape` | `string` | module shape name, overrides `styleJson` |
| `eyeFrame`, `eyeBall` | `string` | eye shape names, override `styleJson` |
| `color` | `color` | module color, overrides `styleJson` |
| `backgroundColor` | `color` | background color, overrides `styleJson` |
| `logoSource` | `url` | logo image: `file:`, `qrc:` or `data:` URL (no network) |
| `label` | `string` | label text, overrides `styleJson` |
| `ecc` | `string` | minimum error correction `"L"`, `"M"`, `"Q"`, `"H"`; `""` = automatic |
| `valid` | `bool`, read-only | `false` when the text could not be encoded |
| `errorString` | `string`, read-only | the last error, empty when fine |
| `lint` | `list<string>`, read-only | scannability warnings |
| `version` | `int`, read-only | QR version 1 to 40 |
| `logoDamage` | `real`, read-only | see [Making sure it scans](#making-sure-it-scans) |

Methods callable from QML: `toSvg(width)` returns the SVG text,
`effectiveStyleJson()` returns the final style as JSON, and
`save(fileUrl, width)` writes a file (`.svg` gives SVG, other suffixes such
as `.png` an image).

## Content helpers

A QR code holds plain text. Phones recognize some text formats and offer an
action, for example joining a Wi-Fi network or saving a contact. The C core
has helpers that write these formats correctly (with the escaping each
format needs). The Qt package has no wrappers for them yet; call them
directly. They work like `snprintf`: they return the full length, so call
them once to get the length and once to fill a buffer:

```cpp
#include <qrkude.h>
#include <vector>

// Calls a qrk_payload_* function twice: to get the length, then to fill a buffer.
template <typename Build>
QByteArray payload(Build build)
{
    const size_t length = build(nullptr, 0);
    std::vector<char> buffer(length + 1);
    build(buffer.data(), buffer.size());
    return QByteArray(buffer.data(), int(length));
}

void wifiExample()
{
    const QByteArray ssid = QStringLiteral("Café Kude").toUtf8(); // strings are UTF-8
    const QByteArray wifi = payload([&](char *out, size_t cap) {
        return qrk_payload_wifi(out, cap, ssid.constData(), "secret;123", "WPA", false);
    });
    // wifi == "WIFI:T:WPA;S:Café Kude;P:secret\;123;;"
    QRKude::Renderer qr(QRKude::Code::encode(wifi), QRKude::Style());
    qr.toImage(400).save("wifi.png");
}
```

| Function | Produces |
|---|---|
| `qrk_payload_wifi(out, cap, ssid, password, auth, hidden)` | Wi-Fi login; `auth` is `"WPA"`, `"WEP"`, `"nopass"` or `nullptr` |
| `qrk_payload_email(out, cap, to, subject, body)` | `mailto:` link |
| `qrk_payload_sms(out, cap, number, message)` | text message (`SMSTO:`) |
| `qrk_payload_tel(out, cap, number)` | phone call (`tel:`) |
| `qrk_payload_geo(out, cap, latitude, longitude)` | map location (`geo:`) |
| `qrk_payload_vcard(out, cap, &card)` | contact card (vCard 3.0) from a `qrk_vcard` struct |
| `qrk_payload_event(out, cap, &event)` | calendar event from a `qrk_event` struct |

The exact output of each helper is defined in [SPEC.md](../../docs/SPEC.md)
section 12.

## Making sure it scans

Styling can make a code harder to read. Two functions help you check a
design before you ship it:

- `lint()` returns a list of warnings (an empty list means no problem was
  found). `lintFlags()` returns the same as bits of `QRKude::Lint::Flag`.
- `logoDamage()` estimates how much of the error correction the logo uses
  up: 0 = none, 1 = all of it. Above 1 the code cannot be read.

```cpp
QRKude::Style style;
style.setColor(QColor("#9ab")); // a light gray-blue: poor contrast on white
QRKude::Renderer qr(QStringLiteral("https://example.com"), style);
if (qr.lintFlags() & QRKude::Lint::LowContrast)
    qWarning() << "colors too light:" << qr.lint(); // ("low-contrast")
```

What each warning means and what to do:

| Warning | Meaning | What to do |
|---|---|---|
| `logo-risky` | the logo uses 70% to 100% of the error correction | make the logo smaller, or reduce the logo margin |
| `logo-unreadable` | the logo hides more than error correction can repair: the code will not scan | make the logo smaller |
| `low-contrast` | modules and background differ too little (contrast below 3:1) | darken the modules or lighten the background |
| `inverted` | light modules on a dark background | many scanners fail: use dark modules on a light background |
| `transparent-background` | no background color | the code depends on what is behind it; set a light background |
| `quiet-zone` | margin below 2 modules | set `setMargin(4)` (at least 2) |
| `small-modules` | module size below 0.6 | raise `setModuleSize()` |
| `eye-shapes` | a diamond eye frame, or a diamond ball in a square or leaf frame | choose other eye shapes |
| `invisible` | the modules or eyes have no visible color | give them a color with some opacity |

These checks catch common mistakes but do not prove that a code scans.
Always test with several phones; see
[testing your codes](../../docs/GUIDE.md#testing-your-codes),
[styling without breaking scanning](../../docs/GUIDE.md#styling-without-breaking-scanning),
[colors and contrast](../../docs/GUIDE.md#colors-and-contrast) and
[troubleshooting](../../docs/GUIDE.md#troubleshooting).

## Untrusted styles

Styles often come from outside: a web form, a database, another device.
`Style::fromJson(const QByteArray &)` is safe for such input. It passes the
bytes unchanged to the C core, which follows the reading rules of
[SPEC.md](../../docs/SPEC.md) section 2.0:

- unknown keys are ignored;
- a key that appears twice is applied twice, in document order (objects
  merge, other values replace);
- nesting deeper than 64 levels, numbers written with more than 400
  characters, and numbers too large for a double (such as `1e999`) are
  errors;
- very large but valid lengths are limited to 1000 modules when drawing, so
  a style cannot make the output huge.

On error, `fromJson` returns the default style, sets `ok` to `false` and
writes a message. The messages start with `parse error: `, followed by one
of:

| Message after `parse error: ` | Cause |
|---|---|
| `nesting deeper than 64 levels at offset N` | too many nested objects or arrays |
| `number longer than 400 characters at offset N` | a very long number |
| `non-finite number at offset N` | a number too large for a double |
| a JSON syntax error with an offset, such as `unterminated string at offset N` | the text is not valid JSON |
| `the document must be a JSON object` | for example an array at the top level |
| `invalid value (unknown shape name, color or gradient type, or wrong value type)` | valid JSON, but a value the format does not allow |

Encoding can fail too. `Code::errorString()` is then `data too long` (the
text does not fit into a QR code, even at version 40) or `invalid argument`
(encode options out of range, such as `"minVersion": 0`).

```cpp
// Returns an empty string on success, otherwise a message for the user.
QString renderUntrusted(const QByteArray &json, const QString &text, QImage *out)
{
    bool ok = false;
    QString error;
    const QRKude::Style style = QRKude::Style::fromJson(json, &ok, &error);
    if (!ok)
        return error;
    const QRKude::EncodeOptions options = QRKude::EncodeOptions::fromJson(json);
    const QRKude::Code code = QRKude::Code::encode(text, style, options);
    if (!code.isValid())
        return code.errorString();
    *out = QRKude::Renderer(code, style).toImage(512); // you choose the pixel size
    return QString();
}
```

`QRKudeWidget::setStyleJson()` and the `styleJson` property of `QRKudeItem`
keep the last valid style when they get invalid JSON, and set
`errorString`. Logo URLs from a style are only loaded from `data:` URIs,
local files and Qt resources, never from the network. If the style comes
from strangers, consider removing `logo.href` so that it cannot point at
local files. Also limit the size of the JSON you accept and the image size
you render.

## API reference

All C++ names are in the namespace `QRKude`, except `QRKudeWidget` and
`QRKudeItem`. Include `<qrkudeqt.h>` for everything except the QML item,
which is in `<qrkudeitem.h>`. Units are modules unless stated otherwise.

### Enums (`qrkudeglobal.h`)

| Name | Values |
|---|---|
| `ModuleShape` | `Square`, `Dot`, `Rounded`, `Diamond`, `Star`, `Fluid`, `Liquid`, `Bevel`, `Classy`, `HBars`, `VBars` |
| `EyeShape` | `Square`, `Rounded`, `Circle`, `Leaf`, `Drop`, `Bevel`, `Diamond`, `Dots`, `None` (alignment patterns only) |
| `LogoShape` | `Square`, `Rounded`, `Circle` |
| `LabelPosition` | `Bottom`, `Top` |
| `Ecc` | `Low`, `Medium`, `Quartile`, `High` (error correction L, M, Q, H) |
| `Layer` | `Background`, `Border`, `Modules`, `EyeFrames`, `EyeBalls`, `LogoBackground`; `LayerCount` = 6 |
| `PaintType` | `None`, `Solid`, `Linear`, `Radial` |
| `Lint::Flag` | `LogoRisky`, `LogoUnreadable`, `LowContrast`, `Inverted`, `TransparentBackground`, `QuietZone`, `SmallModules`, `EyeShapes`, `Invisible` |

### Functions (`qrkudeglobal.h`)

| Function | Description |
|---|---|
| `QString coreVersion()` | version of the C core, e.g. `"0.1.0"` |
| `QString moduleShapeName(ModuleShape)` | JSON name, e.g. `"fluid"` |
| `std::optional<ModuleShape> moduleShapeFromName(const QString &)` | parses a name (case-insensitive) |
| `QStringList moduleShapeNames()` | all module shape names |
| `QString eyeShapeName(EyeShape)` | JSON name; empty for `None` |
| `std::optional<EyeShape> eyeShapeFromName(const QString &)` | parses a name |
| `QStringList eyeShapeNames()` | all eye shape names except `None` |
| `QString logoShapeName(LogoShape)` | `"square"`, `"rounded"` or `"circle"` |
| `std::optional<LogoShape> logoShapeFromName(const QString &)` | parses a name |
| `QString eccName(Ecc)` | `"L"`, `"M"`, `"Q"` or `"H"` |
| `std::optional<Ecc> eccFromName(const QString &)` | parses a level name |
| `QStringList lintNames(quint32 flags)` | warning names of the set bits |
| `qrk_color toQrkColor(const QColor &)` | `QColor` to the core's `0xRRGGBBAA` |
| `QColor fromQrkColor(qrk_color)` | the reverse |
| `QString colorName(const QColor &)` | `"#rrggbb"`, or `"#rrggbbaa"` when not opaque |
| `std::optional<QColor> parseColor(const QString &)` | parses `#rgb`, `#rrggbb`, `#rrggbbaa`, `transparent` |

### `Paint` (`qrkudestyle.h`)

A fill: none, a solid color, or a gradient.

| Member | Description |
|---|---|
| `Paint()`, `static none()` | no fill |
| `Paint(const QColor &)`, `static solid(const QColor &)` | one color (the constructor is implicit) |
| `static linear(const QGradientStops &, double angle = 0)` | linear gradient; angle in degrees, 0 = left to right, 90 = top to bottom |
| `static linear(const QColor &from, const QColor &to, double angle = 0)` | two-color linear gradient |
| `static radial(const QGradientStops &, QPointF center = (0.5, 0.5), double radius = 0.75)` | radial gradient; center and radius relative to the painted area |
| `static radial(const QColor &center, const QColor &edge)` | two-color radial gradient |
| `type()`, `isNone()`, `isSolid()`, `isGradient()` | kind of fill |
| `color()` / `setColor()` | the solid color (for gradients: first stop); `setColor` makes the paint solid |
| `stops()` / `setStops()` | gradient stops (at most 8) |
| `angle()` / `setAngle()` | linear gradient angle |
| `center()` / `setCenter()`, `radius()` / `setRadius()` | radial gradient geometry |
| `toJson()`, `static fromJson(const QJsonValue &, bool *ok)` | JSON form |
| `raw()`, `static fromRaw(const qrk_paint &)` | the C struct |
| `operator==`, `operator!=` | comparison |

### `Style` (`qrkudestyle.h`)

| Member | Description |
|---|---|
| `Style()` | default style: black squares on white, margin 4 |
| `static fromJson(const QByteArray &, bool *ok, QString *error)` | reads JSON (see [Untrusted styles](#untrusted-styles)) |
| `static fromJson(const QJsonObject &, bool *ok, QString *error)` | the same for a `QJsonObject` (serialized first) |
| `toJson()`, `toJsonData(format)` | complete style as JSON object or bytes |
| `margin()` / `setMargin(double)` | quiet zone (empty border), default 4 |
| `moduleShape()` / `setModuleShape(ModuleShape)` | module shape |
| `moduleSize()` / `setModuleSize(double)` | module size 0.1 to 1 of its cell |
| `moduleRoundness()` / `setModuleRoundness(double)` | corner rounding 0 to 1 |
| `modulePaint()` / `setModulePaint(const Paint &)` | module fill |
| `setColor(const QColor &)` | solid module color |
| `setLinearGradient(stops, angle)`, `setRadialGradient(stops, center, radius)` | module gradients |
| `eyeFrame()` / `setEyeFrame(EyeShape)`, `eyeBall()` / `setEyeBall(EyeShape)` | eye shapes |
| `eyeFramePaint()` / `setEyeFramePaint(const Paint &)` | eye frame fill; none = like the modules |
| `eyeBallPaint()` / `setEyeBallPaint(const Paint &)` | eye ball fill; none = like the modules |
| `alignmentFrame()` / `setAlignmentFrame(EyeShape)`, `alignmentBall()` / `setAlignmentBall(EyeShape)` | alignment pattern shapes; `None` = draw as modules |
| `background()` / `setBackground(const Paint &)`, `setBackgroundColor(const QColor &)` | canvas fill; none = transparent |
| `backgroundRadius()` / `setBackgroundRadius(double)` | canvas corner radius |
| `borderWidth()` / `setBorderWidth(double)`, `borderColor()` / `setBorderColor(const QColor &)` | border around the canvas |
| `hasLogo()` | true when the logo size is above 0 |
| `logoSize()` / `setLogoSize(double)` | logo width as a fraction 0 to 1 of the code width |
| `logoMargin()` / `setLogoMargin(double)` | free space around the logo |
| `logoShape()` / `setLogoShape(LogoShape)` | shape of the cleared area |
| `logoExcavate()` / `setLogoExcavate(bool)` | remove the modules under the logo (default true) |
| `logoBackground()` / `setLogoBackground(const QColor &)` | fill behind the logo; transparent = none |
| `logoHref()` / `setLogoHref(const QString &)` | logo URL or `data:` URI |
| `hasLabel()` | true when the label text is not empty |
| `labelText()` / `setLabelText(const QString &)` | label text |
| `labelFont()` / `setLabelFont(const QString &)` | font families, e.g. `"Inter, sans-serif"` |
| `labelSize()` / `setLabelSize(double)` | font size (default 4) |
| `labelColor()` / `setLabelColor(const QColor &)` | text color |
| `labelPosition()` / `setLabelPosition(LabelPosition)` | below or above the code |
| `labelBold()` / `setLabelBold(bool)` | bold text (default true) |
| `recommendedEcc()` | High with a logo, else Medium |
| `toRaw()`, `static fromRaw(const qrk_style &)` | the C struct |
| `operator==`, `operator!=` | comparison |

### `EncodeOptions` and `SvgOptions` (`qrkudestyle.h`)

| Member | Description |
|---|---|
| `EncodeOptions::ecc` | `std::optional<Ecc>`: minimum error correction; unset = automatic (High with a logo, else Medium) |
| `EncodeOptions::minVersion`, `maxVersion` | allowed QR versions, 1 to 40 (see [versions and capacity](../../docs/GUIDE.md#versions-and-capacity)) |
| `EncodeOptions::mask` | mask pattern 0 to 7; -1 = best (default) |
| `EncodeOptions::boostEcc` | raise the error correction while the code does not grow (default true) |
| `EncodeOptions::eci` | mark the text as UTF-8 with an ECI header (default false) |
| `EncodeOptions::fromJson(const QByteArray &, bool *ok)`, `toJson()`, `toRaw()` | JSON `encode` object, C struct |
| `SvgOptions::width` | SVG `width` attribute in pixels; 0 = none |
| `SvgOptions::idPrefix` | gradient id prefix; empty = `"qrk"` |
| `SvgOptions::fromJson(const QByteArray &, bool *ok)`, `toJson()` | JSON `svg` object |

See [encoding options](../../docs/STYLES.md#encoding-options) and
[SVG options](../../docs/STYLES.md#svg-options).

### `Code` (`qrkudecode.h`)

| Member | Description |
|---|---|
| `Code()` | a null code |
| `static encode(const QString &, const Style & = {}, const EncodeOptions & = {})` | encodes text as UTF-8 |
| `static encode(const char *utf8, ...)`, `static encode(const QByteArray &, ...)` | encodes a C string or raw bytes |
| `isNull()`, `isValid()` | whether encoding worked |
| `errorString()` | `"data too long"` or `"invalid argument"`; empty when valid |
| `version()` | QR version 1 to 40 |
| `size()` | modules per side (4 × version + 17) |
| `ecc()` | error correction level used |
| `mask()` | mask pattern used, 0 to 7 |
| `mode()` | data mode: `QRK_MODE_NUMERIC`, `QRK_MODE_ALPHANUMERIC` or `QRK_MODE_BYTE` |
| `isDark(int x, int y)` | true for a dark module; (0, 0) is top-left |
| `raw()` | the C struct |
| `operator==`, `operator!=` | comparison |

### `Renderer` and `Layout` (`qrkuderenderer.h`)

| Member | Description |
|---|---|
| `Renderer()`, `Renderer(const Code &, const Style &)` | constructors |
| `Renderer(const QString &text, const Style & = {}, const EncodeOptions & = {})` | encodes `text` |
| `code()` / `setCode()`, `style()` / `setStyle()` | the code and style |
| `isValid()` | false when the code is null |
| `layout()` | a `Layout`: positions in modules |
| `canvasSize()` | size of the whole canvas in modules |
| `layerPath(Layer)` | shapes of one layer as a `QPainterPath`, winding fill rule |
| `layerBrush(Layer)` | fill of one layer; `Qt::NoBrush` when invisible |
| `labelFont(int pixelSize)` | the label font at a pixel size |
| `fittedRect(const QRectF &)` | where `paint()` draws inside a rectangle |
| `canvasTransform(const QRectF &)` | maps modules into a rectangle, as `paint()` does |
| `paint(QPainter &, const QRectF &, const QImage &logo = {})` | draws everything, antialiased |
| `toImage(int width, const QImage &logo = {})` | draws into a new transparent image |
| `toSvg(double width = 0)`, `toSvg(const SvgOptions &)` | the SVG document |
| `lint()`, `lintFlags()` | scannability warnings |
| `logoDamage()` | share of the error correction used by the logo (0 to 1, above 1 = unreadable) |
| `styleLogo()` | the image loaded from the style's logo href, or null |
| `static loadImage(const QString &href)` | loads a `data:` URI, file or Qt resource |
| `Layout::canvas`, `symbolOrigin`, `modules` | canvas size, position of module (0, 0), modules per side |
| `Layout::hasLogo`, `logoBox` | logo box |
| `Layout::hasLabel`, `labelAnchor`, `labelSize` | label center and baseline, font size |

### `QRKudeWidget` (`qrkudewidget.h`)

| Member | Description |
|---|---|
| properties `text`, `styleJson`, `logo`, `valid`, `errorString`, `lint` | see the table in [QRKudeWidget](#qrkudewidget) |
| `codeStyle()` / `setCodeStyle(const Style &)` | the style as a C++ object |
| `encodeOptions()` / `setEncodeOptions(const EncodeOptions &)` | encoding options |
| `renderer()` | the `Renderer` in use |
| `toImage(int width)` | the current code as an image |
| `sizeHint()`, `minimumSizeHint()`, `heightForWidth(int)` | keep the canvas aspect ratio |
| signals `textChanged(QString)`, `styleChanged()`, `logoChanged()`, `codeChanged()` | change notifications; `codeChanged` follows every new encoding |

### `QRKudeItem` (`qrkudeitem.h`)

| Member | Description |
|---|---|
| properties | see the table in [QRKudeItem (QML)](#qrkudeitem-qml) |
| `effectiveStyle()` | the final `Style` (base plus overrides) |
| `renderer()` | the `Renderer` in use |
| invokable `effectiveStyleJson()`, `toSvg(width)`, `save(fileUrl, width)` | from QML: style JSON, SVG text, write a file |
| signals | one `...Changed()` per writable property, and `codeChanged()` |
| `QRKude::registerQmlTypes(const char *uri = "QRKude")` | registers `QRKudeItem` in the QML module `uri` 1.0 |

`Paint`, `Style` and `Code` are registered with `Q_DECLARE_METATYPE`, so
they can be stored in a `QVariant`.

## Platform notes

- **Qt versions.** Qt 5.12 or newer and Qt 6. Tested here with Qt 5.15.2,
  5.15.18 and 6.8.3 on Linux (x86-64, GCC 15), with both CMake and qmake.
  Windows, macOS, MSVC and Clang were not tested; the code uses only
  standard C++17, C99 and Qt APIs.
- **Qt 5.12.** Label fonts: Qt 5.12 uses only the first family of a font
  list such as `"Inter, sans-serif"`; Qt 5.13 and newer try them in order.
- **QML.** The QML item needs the Qt Qml and Quick modules. CMake builds
  `QRKude::Quick` only when they are found; the `.pri` file only when
  `qtHaveModule(quick)` is true.
- **Labels.** Text is drawn with the fonts installed on the system, like in
  SVG viewers. The label can therefore look slightly different from the
  same SVG in a browser. Everything else matches the SVG geometry.
- **Logo images.** `QImage` reads PNG, JPEG and other bitmap formats. SVG
  logos need the Qt SVG image format plugin (Qt SVG module); without it,
  use a bitmap.
- **No display.** On servers and in CI, set `QT_QPA_PLATFORM=offscreen`.
  For Qt Quick also set `QT_QUICK_BACKEND=software`.
- **Threads.** `toImage()` works in worker threads (it paints on a
  `QImage`). A `Renderer` caches data on first use, so give each thread its
  own copy.
- **High-DPI screens.** `QRKudeWidget` and `QRKudeItem` paint at the
  device pixel ratio, so the code stays sharp (checked for `QRKudeItem`
  with Qt 6 at a scale factor of 2).

## Development

Build the library, the examples and the tests with CMake:

```sh
cmake -S packages/qt -B build -DQRKUDE_TEST_PYTHON=/path/to/python3
cmake --build build
ctest --test-dir build --output-on-failure
```

Or with qmake (`qrkude-qt.pro` builds the tests and the examples):

```sh
mkdir build-qmake && cd build-qmake
qmake ../packages/qt/qrkude-qt.pro && make
QT_QPA_PLATFORM=offscreen ./tests/tst_qrkude
```

The tests (`tests/tst_qrkude`) check:

- every case of the shared conformance file `spec/vectors/render.json`:
  the SVG byte for byte, version, error correction, mask, warnings and logo
  damage; cases with an `error` must fail with that error;
- the JSON round trip, the setters, the automatic error correction rule;
- the layer paths (closed shapes, correct arcs) and the gradient brushes;
- `toImage()` against the C core's own rasterizer, for 24 styles;
- labels, logos, the widget, and hostile JSON (deep nesting, huge numbers,
  garbage).

With `QRKUDE_PYTHON` set to a Python that has `zxingcpp`, `cairosvg`,
`pillow` and `numpy`, the test also decodes every image with a QR reader
and compares it with the SVG rendered by cairosvg. Images go to
`QRKUDE_TEST_OUTPUT` (default: a temporary directory).

Examples:

- `examples/designer`: a style editor with live preview, warnings in the
  status bar, and PNG, SVG and JSON export.
  `designer [--load style.json] [--logo image.png] [--screenshot out.png]`.
- `examples/qml`: `QRKudeItem` with buttons that change the style.
  `qmldemo [--screenshot out.png]`. Built only with Qt Quick.

## License

Apache License 2.0. See [LICENSE](../../LICENSE).
