/*
 * QRKude Qt - common enums and helpers.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Terms used in the comments of this package:
 *   module  one square cell of the QR code grid (a "pixel" of the code).
 *           All sizes and coordinates are measured in modules.
 *   eye     one of the three large squares in the corners (finder
 *           pattern): a 7x7 ring ("frame") around a 3x3 center ("ball").
 *   quiet zone  the empty margin around the code that scanners need.
 *   ECC     error correction: redundant data that lets a scanner read a
 *           damaged or partly covered code.
 * See docs/GUIDE.md for a full introduction and docs/STYLES.md for pictures
 * of every shape.
 */
#ifndef QRKUDEGLOBAL_H
#define QRKUDEGLOBAL_H

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QColor>

#include <optional>

#include "qrkude.h"

namespace QRKude {

/* The numeric values of all enums match the C core (qrk_module_shape,
 * qrk_eye_shape, ...), so static_cast between them is safe. */

/* Shape of the small data modules. */
enum class ModuleShape : quint8 {
    Square = QRK_MODULE_SQUARE,   /* classic squares; size < 1 adds gaps */
    Dot = QRK_MODULE_DOT,         /* circles */
    Rounded = QRK_MODULE_ROUNDED, /* squares with rounded corners (roundness) */
    Diamond = QRK_MODULE_DIAMOND, /* squares rotated by 45 degrees */
    Star = QRK_MODULE_STAR,       /* four-point sparkles */
    Fluid = QRK_MODULE_FLUID,     /* neighbours join; outer corners rounded */
    Liquid = QRK_MODULE_LIQUID,   /* fluid plus rounded inner corners */
    Bevel = QRK_MODULE_BEVEL,     /* neighbours join; outer corners cut at 45 degrees */
    Classy = QRK_MODULE_CLASSY,   /* neighbours join; only top-left and bottom-right round */
    HBars = QRK_MODULE_HBARS,     /* horizontal runs become pills */
    VBars = QRK_MODULE_VBARS,     /* vertical runs become pills */
};

/* Shape of an eye frame or ball (also used for alignment patterns). */
enum class EyeShape : quint8 {
    Square = QRK_EYE_SQUARE,
    Rounded = QRK_EYE_ROUNDED,
    Circle = QRK_EYE_CIRCLE,
    Leaf = QRK_EYE_LEAF,       /* two opposite corners rounded */
    Drop = QRK_EYE_DROP,       /* rounded except the corner facing the code center */
    Bevel = QRK_EYE_BEVEL,     /* corners cut */
    Diamond = QRK_EYE_DIAMOND, /* fully cut: a diamond */
    Dots = QRK_EYE_DOTS,       /* frames: one dot per module; balls: a circle */
    None = QRK_EYE_NONE,       /* alignment patterns only: draw them as data modules */
};

/* Shape of the area cleared behind a logo. */
enum class LogoShape : quint8 {
    Square = QRK_LOGO_SQUARE,
    Rounded = QRK_LOGO_ROUNDED,
    Circle = QRK_LOGO_CIRCLE,
};

/* Where the optional text label goes. */
enum class LabelPosition : quint8 {
    Bottom = QRK_LABEL_BOTTOM,
    Top = QRK_LABEL_TOP,
};

/* Error correction level: how much of the code may be damaged or covered
 * (by a logo, for example) and still be read. Higher levels make the code
 * denser. */
enum class Ecc : quint8 {
    Low = QRK_ECC_LOW,           /* L, about 7% */
    Medium = QRK_ECC_MEDIUM,     /* M, about 15% */
    Quartile = QRK_ECC_QUARTILE, /* Q, about 25% */
    High = QRK_ECC_HIGH,         /* H, about 30% */
};

/* The drawing layers, painted in this order (bottom to top). The logo
 * image and the label are drawn after them. */
enum class Layer : quint8 {
    Background = QRK_LAYER_BACKGROUND,          /* canvas, with optional corner radius */
    Border = QRK_LAYER_BORDER,                  /* frame around the canvas */
    Modules = QRK_LAYER_MODULES,                /* the data modules */
    EyeFrames = QRK_LAYER_EYE_FRAMES,           /* 7x7 eye rings (and alignment rings) */
    EyeBalls = QRK_LAYER_EYE_BALLS,             /* 3x3 eye centers (and alignment centers) */
    LogoBackground = QRK_LAYER_LOGO_BACKGROUND, /* fill behind the logo */
};
constexpr int LayerCount = QRK_LAYER_COUNT; /* number of Layer values */

/* Kind of fill, see QRKude::Paint. */
enum class PaintType : quint8 {
    None = QRK_PAINT_NONE,     /* transparent, or "same as the modules" for eye paints */
    Solid = QRK_PAINT_SOLID,   /* one color */
    Linear = QRK_PAINT_LINEAR, /* gradient along an angle */
    Radial = QRK_PAINT_RADIAL, /* gradient from a center outwards */
};

/* Scannability warnings returned by Renderer::lintFlags() (bit flags).
 * Their names are listed by lintNames(); docs/SPEC.md section 11 has the
 * exact conditions. */
namespace Lint {
enum Flag : quint32 {
    LogoRisky = QRK_LINT_LOGO_RISKY,           /* "logo-risky": logo hides 70-100% of what ECC can repair */
    LogoUnreadable = QRK_LINT_LOGO_UNREADABLE, /* "logo-unreadable": logo hides more than ECC can repair */
    LowContrast = QRK_LINT_LOW_CONTRAST,       /* "low-contrast": below 3:1 */
    Inverted = QRK_LINT_INVERTED,              /* "inverted": light modules on a dark background */
    TransparentBackground = QRK_LINT_TRANSPARENT_BACKGROUND, /* "transparent-background" */
    QuietZone = QRK_LINT_QUIET_ZONE,           /* "quiet-zone": margin below 2 modules */
    SmallModules = QRK_LINT_SMALL_MODULES,     /* "small-modules": module size below 0.6 */
    EyeShapes = QRK_LINT_EYE_SHAPES,           /* "eye-shapes": risky eye frame/ball combination */
    Invisible = QRK_LINT_INVISIBLE,            /* "invisible": modules or eyes have no visible color */
};
} // namespace Lint

/* The names of the set bits ("logo-risky", ..., "invisible") in bit order,
 * which is the order of the table in docs/SPEC.md section 11. Unknown bits
 * are named "lint-0x...". */
QStringList lintNames(quint32 flags);

/* Version of the bundled C core, e.g. "0.1.0". */
QString coreVersion();

/* Shape names as used by the JSON format ("fluid", "leaf", ...). The
 * *FromName functions ignore case and surrounding spaces and return
 * std::nullopt for unknown names. */
QString moduleShapeName(ModuleShape shape);
std::optional<ModuleShape> moduleShapeFromName(const QString &name);
QStringList moduleShapeNames(); /* all module shapes, in enum order */

QString eyeShapeName(EyeShape shape); /* None -> empty string */
std::optional<EyeShape> eyeShapeFromName(const QString &name);
QStringList eyeShapeNames(); /* all eye shapes except None, in enum order */

QString logoShapeName(LogoShape shape);
std::optional<LogoShape> logoShapeFromName(const QString &name);

QString eccName(Ecc ecc); /* "L", "M", "Q" or "H" */
std::optional<Ecc> eccFromName(const QString &name);

/* Conversion between QColor and the core's 0xRRGGBBAA integers. An invalid
 * QColor becomes transparent. */
qrk_color toQrkColor(const QColor &color);
QColor fromQrkColor(qrk_color color);
/* "#rrggbb", or "#rrggbbaa" when not opaque; lower case, as in the JSON
 * format. */
QString colorName(const QColor &color);
/* Parses "#rgb", "#rrggbb", "#rrggbbaa" or "transparent", like the JSON
 * format; std::nullopt for anything else. */
std::optional<QColor> parseColor(const QString &text);

} // namespace QRKude

#endif // QRKUDEGLOBAL_H
