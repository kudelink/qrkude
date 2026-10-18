/*
 * QRKude Qt - style value types (Paint, Style, EncodeOptions, SvgOptions).
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef QRKUDESTYLE_H
#define QRKUDESTYLE_H

#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QMetaType>
#include <QtCore/QPointF>
#include <QtCore/QSharedDataPointer>
#include <QtGui/QBrush>
#include <QtGui/QColor>
#include <QtGui/QGradient>

#include "qrkudeglobal.h"

namespace QRKude {

/*
 * How an area is filled: nothing, one color, or a linear or radial gradient.
 *
 * Gradients span the "painted area": the code itself (without the quiet
 * zone) for modules and eyes, the whole canvas for the background.
 *   linear  an angle in degrees: 0 = left to right, 90 = top to bottom.
 *   radial  a center relative to the painted area ((0.5, 0.5) = middle) and
 *           a radius relative to its larger side.
 * Stop offsets go from 0 to 1. At most QRK_MAX_STOPS (8) stops are kept.
 * The exact geometry is in docs/SPEC.md section 8.4; pictures are in
 * docs/STYLES.md#colors-and-gradients.
 */
class Paint
{
public:
    Paint();                    /* no fill (PaintType::None) */
    Paint(const QColor &color); /* NOLINT: implicit on purpose; one solid color */

    static Paint none();
    static Paint solid(const QColor &color);
    static Paint linear(const QGradientStops &stops, double angleDegrees = 0);
    /* Two stops: `from` at 0, `to` at 1. */
    static Paint linear(const QColor &from, const QColor &to, double angleDegrees = 0);
    static Paint radial(const QGradientStops &stops, const QPointF &center = QPointF(0.5, 0.5),
                        double radius = 0.75);
    /* `center` color in the middle, `edge` color at radius 0.75. */
    static Paint radial(const QColor &center, const QColor &edge);

    PaintType type() const;
    bool isNone() const { return type() == PaintType::None; }
    bool isSolid() const { return type() == PaintType::Solid; }
    bool isGradient() const { return type() == PaintType::Linear || type() == PaintType::Radial; }

    /* The solid color; for gradients the color of the first stop. */
    QColor color() const;
    QGradientStops stops() const; /* gradients; empty otherwise */
    double angle() const;         /* linear gradients, degrees */
    QPointF center() const;       /* radial gradients, relative (0..1) */
    double radius() const;        /* radial gradients, relative to the larger side */

    void setColor(const QColor &color); /* also makes the paint solid */
    void setStops(const QGradientStops &stops); /* keeps the type; at most 8 stops */
    void setAngle(double degrees);
    void setCenter(const QPointF &center);
    void setRadius(double radius);

    /* JSON form: null, "#rrggbb[aa]" or {"type": "linear", ...}
     * (docs/SPEC.md section 2.1). fromJson returns none() and sets
     * *ok = false for an invalid value. */
    QJsonValue toJson() const;
    static Paint fromJson(const QJsonValue &value, bool *ok = nullptr);

    /* The C struct (qrk_paint) for use with the core API. */
    const qrk_paint &raw() const { return m_paint; }
    static Paint fromRaw(const qrk_paint &paint);

    bool operator==(const Paint &other) const;
    bool operator!=(const Paint &other) const { return !(*this == other); }

private:
    qrk_paint m_paint;
};

class StyleData;

/*
 * Everything about how a QR code looks: module and eye shapes, colors,
 * background, border, logo and label. It does not contain the text; that is
 * encoded by QRKude::Code.
 *
 * Build it with the setters or from the JSON format shared by all QRKude
 * ports (docs/SPEC.md section 2, examples in docs/STYLES.md). Style is a
 * cheap-to-copy value (implicitly shared, like QString) and owns its strings.
 *
 * Setters store values as given. Out-of-range values are clamped when the
 * code is drawn, the same way in every port (docs/SPEC.md section 2.2): for
 * example a module size of 5 is drawn as 1, and lengths above 1000 modules
 * are drawn as 1000.
 */
class Style
{
public:
    /* The default style: black square modules and eyes on white, a quiet
     * zone of 4 modules, no border, logo or label. */
    Style();
    Style(const Style &other);
    Style(Style &&other) noexcept;
    Style &operator=(const Style &other);
    Style &operator=(Style &&other) noexcept;
    ~Style();

    /* Reads a style object, or a whole document
     * {"encode": {...}, "style": {...}, "svg": {...}} (only "style" is used).
     * Keys that are missing keep their default. The bytes go unchanged to
     * the C core, so the rules of docs/SPEC.md section 2.0 apply to the text
     * as written (duplicate keys merge in document order; nesting deeper
     * than 64, number tokens longer than 400 characters and non-finite
     * numbers are errors). Safe for untrusted input.
     * On error: returns the default style, sets *ok to false and
     * *errorString to a message that starts with "parse error: ". */
    static Style fromJson(const QByteArray &json, bool *ok = nullptr, QString *errorString = nullptr);
    /* Convenience: the object is serialized first (a QJsonObject has already
     * lost duplicate keys and the original number spelling). */
    static Style fromJson(const QJsonObject &json, bool *ok = nullptr, QString *errorString = nullptr);
    /* The complete style object: every key is written, so fromJson(toJson())
     * gives back an equal Style. */
    QJsonObject toJson() const;
    QByteArray toJsonData(QJsonDocument::JsonFormat format = QJsonDocument::Indented) const;

    /* Quiet zone: empty space around the code, in modules (default 4).
     * Scanners need at least 2; see docs/GUIDE.md#anatomy-of-a-qr-code. */
    double margin() const;
    void setMargin(double modules);

    /* Data modules (docs/STYLES.md#modules). */
    ModuleShape moduleShape() const; /* default Square */
    void setModuleShape(ModuleShape shape);
    /* Drawn size of a module, 0.1..1 of its cell (default 1). Used by
     * Square, Dot, Rounded, Diamond, Star, HBars and VBars. */
    double moduleSize() const;
    void setModuleSize(double size);
    /* Corner rounding 0..1 (default 1). Used by Rounded, Fluid, Liquid,
     * Bevel, Classy, HBars and VBars. */
    double moduleRoundness() const;
    void setModuleRoundness(double roundness);
    Paint modulePaint() const; /* default solid black */
    void setModulePaint(const Paint &paint);
    /* Shortcuts for setModulePaint(). */
    void setColor(const QColor &color) { setModulePaint(Paint::solid(color)); }
    void setLinearGradient(const QGradientStops &stops, double angleDegrees)
    {
        setModulePaint(Paint::linear(stops, angleDegrees));
    }
    void setRadialGradient(const QGradientStops &stops, const QPointF &center = QPointF(0.5, 0.5),
                           double radius = 0.75)
    {
        setModulePaint(Paint::radial(stops, center, radius));
    }

    /* Eyes, the three large corner squares (docs/STYLES.md#eyes). The frame
     * is the 7x7 ring, the ball the 3x3 center. EyeShape::None is not
     * allowed here and is stored as Square. */
    EyeShape eyeFrame() const; /* default Square */
    void setEyeFrame(EyeShape shape);
    EyeShape eyeBall() const;  /* default Square; Dots is drawn as Circle */
    void setEyeBall(EyeShape shape);
    /* Eye colors; Paint::none() (the default) = same paint as the modules. */
    Paint eyeFramePaint() const;
    void setEyeFramePaint(const Paint &paint);
    Paint eyeBallPaint() const;
    void setEyeBallPaint(const Paint &paint);

    /* Alignment patterns, the smaller squares inside codes of version 2 and
     * up (docs/STYLES.md#alignment-patterns). EyeShape::None (the default)
     * draws them as ordinary modules. A ball of None with a frame set is
     * drawn as Square. They use the eye paints. */
    EyeShape alignmentFrame() const;
    void setAlignmentFrame(EyeShape shape);
    EyeShape alignmentBall() const;
    void setAlignmentBall(EyeShape shape);

    /* Canvas background (docs/STYLES.md#background-and-border); default
     * solid white. Paint::none() = transparent. */
    Paint background() const;
    void setBackground(const Paint &paint);
    void setBackgroundColor(const QColor &color) { setBackground(Paint::solid(color)); }
    /* Corner radius of the canvas in modules (default 0). */
    double backgroundRadius() const;
    void setBackgroundRadius(double modules);

    /* Border drawn around the canvas, outside the quiet zone, in modules
     * (default 0 = no border). */
    double borderWidth() const;
    void setBorderWidth(double modules);
    QColor borderColor() const; /* default black; a solid color only */
    void setBorderColor(const QColor &color);

    /* Logo (docs/STYLES.md#logo, docs/GUIDE.md#logos). A logo is present
     * when logoSize() > 0. It covers part of the code, so it relies on error
     * correction; check Renderer::logoDamage(). */
    bool hasLogo() const { return logoSize() > 0; }
    /* Width of the logo box as a fraction of the code width, 0..1
     * (default 0 = no logo; the JSON format uses 0.2 when "logo" is given). */
    double logoSize() const;
    void setLogoSize(double fraction);
    /* Free space around the logo box, in modules (default 1). */
    double logoMargin() const;
    void setLogoMargin(double modules);
    LogoShape logoShape() const; /* shape of the cleared area (default Square) */
    void setLogoShape(LogoShape shape);
    /* true (default): remove ("excavate") the modules under the logo area,
     * so the logo sits on a clean background. */
    bool logoExcavate() const;
    void setLogoExcavate(bool excavate);
    /* Fill of the cleared area; transparent (the default) = no fill. */
    QColor logoBackground() const;
    void setLogoBackground(const QColor &color);
    /* Image URL or data: URI. toSvg() writes it into the SVG as is. When
     * paint() gets no logo image, the Qt renderer loads it from here if it
     * is a data: URI, a local file or a Qt resource (":/..." or "qrc:/...").
     * Remote URLs are never fetched. */
    QString logoHref() const;
    void setLogoHref(const QString &href);

    /* Text label in a band below or above the code (docs/STYLES.md#label).
     * The label is shown when the text is not empty. */
    bool hasLabel() const { return !labelText().isEmpty(); }
    QString labelText() const;
    void setLabelText(const QString &text);
    /* Font families as in CSS, first available wins, e.g.
     * "Inter, Helvetica, sans-serif" (default "sans-serif"). The generic
     * names sans-serif, serif and monospace are understood. */
    QString labelFont() const;
    void setLabelFont(const QString &family);
    double labelSize() const; /* font size in modules (default 4) */
    void setLabelSize(double modules);
    QColor labelColor() const; /* default black */
    void setLabelColor(const QColor &color);
    LabelPosition labelPosition() const; /* default Bottom */
    void setLabelPosition(LabelPosition position);
    bool labelBold() const; /* default true */
    void setLabelBold(bool bold);

    /* The error correction level used when EncodeOptions::ecc is not set:
     * High when the style has a logo, else Medium. */
    Ecc recommendedEcc() const;

    /* The C struct (qrk_style) for the core API. Its string pointers point
     * into this Style: they stay valid while this Style exists and is not
     * modified. */
    qrk_style toRaw() const;
    /* Builds a Style from a C struct; the strings are copied. */
    static Style fromRaw(const qrk_style &style);

    bool operator==(const Style &other) const;
    bool operator!=(const Style &other) const { return !(*this == other); }

private:
    QSharedDataPointer<StyleData> d;
};

/*
 * How the text is turned into modules: the "encode" object of the JSON
 * format (docs/STYLES.md#encoding-options). The defaults are right for
 * almost every use.
 */
struct EncodeOptions
{
    /* Minimum error correction level (docs/GUIDE.md#error-correction).
     * Unset (default) = automatic: High when the style has a logo, else
     * Medium. */
    std::optional<Ecc> ecc;
    /* Range of QR versions (sizes) to try, 1..40: version v has 4v+17
     * modules per side (docs/GUIDE.md#versions-and-capacity). The smallest
     * version that fits is used. */
    int minVersion = 1;
    int maxVersion = QRK_MAX_VERSION;
    /* Mask pattern 0..7, or -1 (default) to pick the best one. */
    int mask = -1;
    /* Raise the ECC level while the code does not get bigger (default true). */
    bool boostEcc = true;
    /* Add an ECI header that marks the text as UTF-8 (default false). Some
     * older scanners do not support it. */
    bool eci = false;

    /* Reads the "encode" object of a JSON document; missing keys keep their
     * default. On a parse error returns the defaults and sets *ok = false.
     * Out-of-range values (e.g. minVersion 0) are reported when encoding:
     * Code::errorString() is then "invalid argument". */
    static EncodeOptions fromJson(const QByteArray &json, bool *ok = nullptr);
    QJsonObject toJson() const;
    /* The C struct for the core API; an unset ecc becomes QRK_ECC_AUTO,
     * which qrk_encode_for_style() resolves. */
    qrk_encode_opts toRaw() const;

    bool operator==(const EncodeOptions &o) const
    {
        return ecc == o.ecc && minVersion == o.minVersion && maxVersion == o.maxVersion &&
               mask == o.mask && boostEcc == o.boostEcc && eci == o.eci;
    }
    bool operator!=(const EncodeOptions &o) const { return !(*this == o); }
};

/* Options for Renderer::toSvg(): the "svg" object of the JSON format
 * (docs/STYLES.md#svg-options). */
struct SvgOptions
{
    /* Value of the SVG width attribute in px (height follows the aspect
     * ratio); 0 (default) = no width/height, the SVG scales to its container. */
    double width = 0;
    /* Prefix of the gradient ids. Give each SVG on one HTML page its own
     * prefix. Empty = "qrk"; characters outside [A-Za-z0-9_-] become '_'. */
    QString idPrefix;

    /* Reads the "svg" object of a JSON document; *ok = false on a parse error. */
    static SvgOptions fromJson(const QByteArray &json, bool *ok = nullptr);
    QJsonObject toJson() const;
};

} // namespace QRKude

Q_DECLARE_METATYPE(QRKude::Paint)
Q_DECLARE_METATYPE(QRKude::Style)

#endif // QRKUDESTYLE_H
