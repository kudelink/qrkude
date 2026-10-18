/*
 * QRKude Qt - unit and rendering tests (QtTest, run with QT_QPA_PLATFORM=offscreen).
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Environment:
 *   QRKUDE_TEST_OUTPUT  directory for the PNG/SVG files (default: temp dir)
 *   QRKUDE_PYTHON       python with zxingcpp, cairosvg, pillow, numpy; enables
 *                       the decode + cairosvg comparison test
 */
#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtGui/QPainter>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include <cmath>

#include "qrkudeqt.h"
#ifdef QRKUDE_HAS_QUICK
#include "qrkudeitem.h"
#endif

using namespace QRKude;

#ifndef QRKUDE_SPEC_DIR
#error "QRKUDE_SPEC_DIR must point to the spec/ directory"
#endif
#ifndef QRKUDE_TESTS_DIR
#error "QRKUDE_TESTS_DIR must point to packages/qt/tests"
#endif

namespace {

const char *const kDefaultText = "https://kudelink.com/qt";

/* (moc cannot parse a macro concatenated with a literal inside QStringLiteral) */
QString specPath(const char *relative)
{
    return QString::fromUtf8(QRKUDE_SPEC_DIR) + QLatin1Char('/') + QLatin1String(relative);
}

struct Case
{
    QString name;
    QString text;
    QByteArray json; /* full document */
    bool hasLogo = false;
};

struct Diff
{
    double mean = 0;
    int max = 0;
    double over64 = 0;
};

QJsonDocument readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QJsonDocument();
    return QJsonDocument::fromJson(f.readAll());
}

QImage makeLogo(int w = 64, int h = 64)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(230, 57, 70));
    p.drawEllipse(QRectF(2, 2, w - 4, h - 4));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(w * 0.3, h * 0.3, w * 0.4, h * 0.4));
    p.end();
    return img;
}

QString pngDataUri(const QImage &img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QImage flatten(const QImage &img)
{
    QImage out(img.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.drawImage(0, 0, img);
    p.end();
    return out;
}

/* Mean / max absolute RGB difference; rows inside `excludeRows` are skipped. */
Diff compare(const QImage &a, const QImage &b, int excludeFrom = -1, int excludeTo = -1)
{
    Diff d;
    const QImage x = a.convertToFormat(QImage::Format_RGB32), y = b.convertToFormat(QImage::Format_RGB32);
    if (x.size() != y.size())
        return Diff{255, 255, 1};
    double sum = 0;
    qint64 count = 0, over = 0;
    for (int row = 0; row < x.height(); row++) {
        if (row >= excludeFrom && row < excludeTo)
            continue;
        const QRgb *p = reinterpret_cast<const QRgb *>(x.constScanLine(row));
        const QRgb *q = reinterpret_cast<const QRgb *>(y.constScanLine(row));
        for (int col = 0; col < x.width(); col++) {
            const int dr = std::abs(qRed(p[col]) - qRed(q[col]));
            const int dg = std::abs(qGreen(p[col]) - qGreen(q[col]));
            const int db = std::abs(qBlue(p[col]) - qBlue(q[col]));
            sum += dr + dg + db;
            const int m = qMax(dr, qMax(dg, db));
            d.max = qMax(d.max, m);
            over += m > 64;
            count++;
        }
    }
    d.mean = count ? sum / (3.0 * count) : 0;
    d.over64 = count ? double(over) / count : 0;
    return d;
}

/* Reference rasterizer of the C core at the same scale as Renderer::toImage. */
QImage coreRaster(const Renderer &r, const QSize &size, const QImage &logo)
{
    qrk_raster_opts o;
    qrk_raster_opts_default(&o, size.width(), size.height());
    o.supersample = 4;
    o.integer_scale = false;
    o.clear_color = QRK_HEX(0xFFFFFF);
    const QImage rgba = logo.convertToFormat(QImage::Format_RGBA8888);
    qrk_image li;
    li.width = rgba.width();
    li.height = rgba.height();
    li.format = QRK_PIXEL_RGBA8888;
    li.pixels = rgba.constBits();
    li.stride = size_t(rgba.bytesPerLine());
    if (!logo.isNull())
        o.logo = &li;
    QImage out(size, QImage::Format_RGBA8888);
    const qrk_style s = r.style().toRaw();
    qrk_render_buffer(r.code().raw(), &s, &o, QRK_PIXEL_RGBA8888, out.bits(), size_t(out.bytesPerLine()));
    return out;
}

/* Flattened sub-paths in canvas units. QPainterPath flattens with a tolerance
 * meant for pixels, so flatten at 1000x and scale back. */
QList<QPolygonF> fineSubpaths(const QPainterPath &path)
{
    QList<QPolygonF> out = path.toSubpathPolygons(QTransform::fromScale(1000, 1000));
    for (QPolygonF &poly : out)
        for (QPointF &p : poly)
            p /= 1000;
    return out;
}

double signedArea(const QPolygonF &poly)
{
    double a = 0;
    for (int i = 0; i + 1 < poly.size(); i++)
        a += poly[i].x() * poly[i + 1].y() - poly[i + 1].x() * poly[i].y();
    return a / 2;
}

/* JSON fixtures. They live outside the test class because moc mis-parses
 * raw string literals containing "# inside a class body. */
const char *const kJsonLabelTop =
    R"({"modules":{"shape":"rounded","size":0.9},"label":{"text":"Kude Label",)"
    R"("position":"top","bold":false,"size":3,"font":"Georgia, serif","color":"#1d3557"}})";
const char *const kJsonLogoLarge =
    R"({"modules":{"shape":"dot","size":0.85},"eyes":{"frame":"leaf","ball":"leaf"},)"
    R"("logo":{"href":"$LOGO","size":0.25,"shape":"rounded","background":"#f1faee"}})";
const char *const kJsonRadialAlpha =
    R"({"modules":{"shape":"classy","color":{"type":"radial","cx":0.3,"cy":0.7,"r":0.9,)"
    R"("stops":[{"offset":0.2,"color":"#0b3d91e0"},{"offset":0.8,"color":"#00704aff"}]}},)"
    R"("background":{"color":{"type":"linear","angle":30,"stops":[{"offset":0,"color":"#ffffff"},)"
    R"({"offset":1,"color":"#e0f7fa"}]},"radius":5},"border":{"width":0.5,"color":"#1d355780"}})";
const char *const kJsonSetters = R"({"margin": 2,
            "modules": {"shape": "liquid", "roundness": 0.9, "color": {"type": "linear", "angle": 45,
                "stops": [{"offset": 0, "color": "#6a11cb"}, {"offset": 1, "color": "#2575fc"}]}},
            "eyes": {"frame": "leaf", "ball": "drop", "frameColor": "#0b3d91",
                "ballColor": {"type": "radial", "cx": 0.4, "cy": 0.6, "r": 0.5,
                    "stops": [{"offset": 0, "color": "#ff0000"}, {"offset": 1, "color": "#0000ff80"}]}},
            "alignment": {"frame": "circle"},
            "background": {"color": "#fff8e7", "radius": 3},
            "border": {"width": 1, "color": "#1d3557"},
            "logo": {"href": "logo.png", "size": 0.2, "shape": "circle", "background": "#ffffff"},
            "label": {"text": "SCAN ME", "color": "#1d3557", "position": "top"}})";
const char *const kJsonBrushes =
    R"({"modules":{"color":{"type":"linear","angle":30,"stops":[{"offset":0,"color":"#ff0000"},{"offset":1,"color":"#0000ff"}]}},)"
    R"("eyes":{"frameColor":{"type":"radial","cx":0.25,"r":0.5,"stops":[{"offset":0,"color":"#000"},{"offset":1,"color":"#0f0"}]}},)"
    R"("background":{"color":null}})";
const char *const kJsonLabel =
    R"({"modules":{"color":"#000"},"label":{"text":"SCAN ME","size":4,"color":"#ff0000"}})";

} // namespace

class TestQRKude : public QObject
{
    Q_OBJECT

private:
    QList<Case> m_cases;
    QJsonArray m_vectors;
    QImage m_logo;
    QString m_logoUri;
    QDir m_out;

    Renderer rendererFor(const Case &c) const
    {
        const Style style = Style::fromJson(c.json);
        return Renderer(Code::encode(c.text, style, EncodeOptions::fromJson(c.json)), style);
    }

    void addCaseRows()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < m_cases.size(); i++)
            QTest::newRow(m_cases[i].name.toUtf8().constData()) << i;
    }

private slots:
    void initTestCase()
    {
        m_logo = makeLogo();
        m_logoUri = pngDataUri(m_logo);

        const QString outDir = qEnvironmentVariableIsSet("QRKUDE_TEST_OUTPUT")
                                   ? qEnvironmentVariable("QRKUDE_TEST_OUTPUT")
                                   : QDir::temp().filePath(QStringLiteral("qrkude-qt-test"));
        QVERIFY(QDir().mkpath(outDir));
        m_out = QDir(outDir);

        const QJsonArray gallery =
            readJson(specPath("cases/gallery.json")).array();
        QVERIFY2(!gallery.isEmpty(), "spec/cases/gallery.json not found");
        for (const QJsonValue &v : gallery) {
            QJsonObject o = v.toObject();
            Case c;
            c.name = o.value(QStringLiteral("name")).toString();
            c.text = o.value(QStringLiteral("text")).toString(QString::fromLatin1(kDefaultText));
            QJsonObject style = o.value(QStringLiteral("style")).toObject();
            QJsonObject logo = style.value(QStringLiteral("logo")).toObject();
            if (logo.value(QStringLiteral("href")).toString() == QLatin1String("$LOGO")) {
                logo.insert(QStringLiteral("href"), m_logoUri);
                style.insert(QStringLiteral("logo"), logo);
                c.hasLogo = true;
            }
            QJsonObject doc;
            doc.insert(QStringLiteral("style"), style);
            for (const char *key : {"encode", "svg"})
                if (o.contains(QLatin1String(key)))
                    doc.insert(QLatin1String(key), o.value(QLatin1String(key)));
            c.json = QJsonDocument(doc).toJson(QJsonDocument::Compact);
            m_cases << c;
        }
        /* a few extra cases: label fonts, a larger symbol with a logo, alpha gradients */
        const auto extra = [this](const char *name, const QString &text, const QString &json, bool logo) {
            Case c;
            c.name = QLatin1String(name);
            c.text = text;
            c.json = QString(json).replace(QLatin1String("$LOGO"), m_logoUri).toUtf8();
            c.hasLogo = logo;
            m_cases << c;
        };
        extra("label-top-serif", QStringLiteral("https://kudelink.com/label"),
              QString::fromUtf8(kJsonLabelTop),
              false);
        extra("logo-large-v7", QStringLiteral("https://kudelink.com/") + QString(90, QLatin1Char('x')),
              QString::fromUtf8(kJsonLogoLarge),
              true);
        extra("radial-alpha", QStringLiteral("https://kudelink.com/alpha"),
              QString::fromUtf8(kJsonRadialAlpha),
              false);

        m_vectors = readJson(specPath("vectors/render.json")).array();
        QVERIFY2(!m_vectors.isEmpty(), "spec/vectors/render.json not found");
    }

    void names()
    {
        QCOMPARE(moduleShapeNames().size(), int(QRK_MODULE_SHAPE_COUNT));
        QCOMPARE(eyeShapeNames().size(), int(QRK_EYE_SHAPE_COUNT));
        for (const QString &n : moduleShapeNames())
            QCOMPARE(moduleShapeName(*moduleShapeFromName(n)), n);
        QCOMPARE(*eyeShapeFromName(QStringLiteral("Leaf")), EyeShape::Leaf);
        QVERIFY(!moduleShapeFromName(QStringLiteral("triangle")));
        QCOMPARE(colorName(QColor(0x12, 0x34, 0x56)), QStringLiteral("#123456"));
        QCOMPARE(colorName(QColor(0x12, 0x34, 0x56, 0x80)), QStringLiteral("#12345680"));
        QCOMPARE(*parseColor(QStringLiteral("#abc")), QColor(0xaa, 0xbb, 0xcc));
        QCOMPARE(parseColor(QStringLiteral("transparent"))->alpha(), 0);
        QCOMPARE(eccName(Ecc::Quartile), QStringLiteral("Q"));
    }

    /* Strings belong to the Style, not to the JSON buffer. */
    void styleOwnsStrings()
    {
        Style a;
        {
            QByteArray json = R"({"logo":{"href":"data:x","size":0.2},"label":{"text":"Hi","font":"Inter"}})";
            bool ok = false;
            a = Style::fromJson(json, &ok);
            QVERIFY(ok);
            json.fill('X');
        }
        QCOMPARE(a.logoHref(), QStringLiteral("data:x"));
        QCOMPARE(a.labelText(), QStringLiteral("Hi"));
        Style b = a;
        b.setLabelText(QStringLiteral("Other"));
        b.setLogoHref(QStringLiteral("other.png"));
        QCOMPARE(a.labelText(), QStringLiteral("Hi"));
        QCOMPARE(b.labelText(), QStringLiteral("Other"));
        const qrk_style raw = a.toRaw();
        QCOMPARE(QByteArray(raw.label.text), QByteArray("Hi"));
        QCOMPARE(QByteArray(raw.label.font), QByteArray("Inter"));
        QCOMPARE(QByteArray(raw.logo.href), QByteArray("data:x"));
        QVERIFY(a != b);

        QString error;
        bool ok = true;
        Style bad = Style::fromJson(QByteArray(R"({"modules":{"shape":"triangle"}})"), &ok, &error);
        QVERIFY(!ok);
        QVERIFY(!error.isEmpty());
        QVERIFY(bad == Style());
        Style::fromJson(QByteArray("{\"margin\": "), &ok, &error);
        QVERIFY(!ok);
        QVERIFY(error.contains(QLatin1String("offset")));
    }

    void settersMatchJson()
    {
        Style a;
        a.setMargin(2);
        a.setModuleShape(ModuleShape::Liquid);
        a.setModuleRoundness(0.9);
        a.setModulePaint(Paint::linear({{0, QColor("#6a11cb")}, {1, QColor("#2575fc")}}, 45));
        a.setEyeFrame(EyeShape::Leaf);
        a.setEyeBall(EyeShape::Drop);
        a.setEyeFramePaint(QColor("#0b3d91"));
        a.setEyeBallPaint(Paint::radial({{0, Qt::red}, {1, QColor(0, 0, 255, 128)}}, QPointF(0.4, 0.6), 0.5));
        a.setAlignmentFrame(EyeShape::Circle);
        a.setBackground(QColor("#fff8e7"));
        a.setBackgroundRadius(3);
        a.setBorderWidth(1);
        a.setBorderColor(QColor("#1d3557"));
        a.setLogoSize(0.2);
        a.setLogoShape(LogoShape::Circle);
        a.setLogoBackground(Qt::white);
        a.setLogoHref(QStringLiteral("logo.png"));
        a.setLabelText(QStringLiteral("SCAN ME"));
        a.setLabelColor(QColor("#1d3557"));
        a.setLabelPosition(LabelPosition::Top);

        const QByteArray json = kJsonSetters;
        bool ok = false;
        const Style b = Style::fromJson(json, &ok);
        QVERIFY(ok);
        QVERIFY(a == b);
        const Code code = Code::encode("https://kudelink.com/setters", a);
        QCOMPARE(Renderer(code, a).toSvg(), Renderer(code, b).toSvg());
        QCOMPARE(a.eyeBallPaint().center(), QPointF(0.4, 0.6));
        QCOMPARE(a.modulePaint().stops().size(), 2);
    }

    void eccDefaultRule()
    {
        EncodeOptions noBoost;
        noBoost.boostEcc = false;
        Style plain;
        Style withLogo;
        withLogo.setLogoSize(0.2);
        QCOMPARE(Code::encode("https://kudelink.com", plain, noBoost).ecc(), Ecc::Medium);
        QCOMPARE(Code::encode("https://kudelink.com", withLogo, noBoost).ecc(), Ecc::High);
        EncodeOptions low = noBoost;
        low.ecc = Ecc::Low;
        QCOMPARE(Code::encode("https://kudelink.com", withLogo, low).ecc(), Ecc::Low);

        const EncodeOptions parsed = EncodeOptions::fromJson(R"({"encode": {"mask": 3}})");
        QVERIFY(!parsed.ecc.has_value());
        QCOMPARE(int(parsed.toRaw().ecc), int(QRK_ECC_AUTO));
        QCOMPARE(parsed.mask, 3);
        /* duplicate "encode" objects merge in document order (SPEC 2.0) */
        const EncodeOptions merged = EncodeOptions::fromJson(R"({"encode":{"ecc":"L"},"encode":{"mask":3}})");
        QVERIFY(merged.ecc == std::optional<Ecc>(Ecc::Low));
        QCOMPARE(merged.mask, 3);
        EncodeOptions badRange;
        badRange.minVersion = 0;
        QCOMPARE(Code::encode("x", Style(), badRange).errorString(), QStringLiteral("invalid argument"));
        QCOMPARE(*EncodeOptions::fromJson(R"({"encode": {"ecc": "Q"}})").ecc, Ecc::Quartile);

        const Code tooLong = Code::encode(QByteArray(4000, 'x'));
        QVERIFY(tooLong.isNull());
        QCOMPARE(tooLong.errorString(), QString::fromLatin1(qrk_status_str(QRK_ERR_DATA_TOO_LONG)));

        const Code c = Code::encode(QStringLiteral("HELLO"));
        QVERIFY(c.isValid());
        QCOMPARE(c.size(), 21);
        QVERIFY(c.isDark(0, 0));    /* finder corner */
        QVERIFY(!c.isDark(7, 0));   /* separator */
        QVERIFY(!c.isDark(-1, 3));  /* out of range */
    }

    /* Every conformance vector: SVG byte for byte, version/ecc/mask, lint, damage. */
    void conformanceVectors_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < m_vectors.size(); i++)
            QTest::newRow(m_vectors[i].toObject().value(QStringLiteral("name")).toString().toUtf8().constData())
                << i;
    }

    void conformanceVectors()
    {
        QFETCH(int, index);
        const QJsonObject v = m_vectors[index].toObject();
        QByteArray json;
        if (v.value(QStringLiteral("json")).isString()) {
            /* raw document: duplicate keys, escapes and limits judged on the text */
            json = v.value(QStringLiteral("json")).toString().toUtf8();
        } else {
            QJsonObject doc;
            for (const char *key : {"encode", "style", "svg"})
                if (v.contains(QLatin1String(key)))
                    doc.insert(QLatin1String(key), v.value(QLatin1String(key)));
            json = QJsonDocument(doc).toJson(QJsonDocument::Compact);
        }
        const QString expectedError = v.value(QStringLiteral("error")).toString();

        bool ok = false;
        QString error;
        const Style style = Style::fromJson(json, &ok, &error);
        if (expectedError == QLatin1String("parse error")) {
            QVERIFY(!ok);
            QVERIFY(error.startsWith(QLatin1String("parse error")));
            QVERIFY(!EncodeOptions::fromJson(json, &ok).ecc && !ok);
            SvgOptions::fromJson(json, &ok);
            QVERIFY(!ok);
            return;
        }
        QVERIFY2(ok, qPrintable(error));
        const EncodeOptions enc = EncodeOptions::fromJson(json, &ok);
        QVERIFY(ok);
        const SvgOptions svg = SvgOptions::fromJson(json, &ok);
        QVERIFY(ok);
        const Code code = Code::encode(v.value(QStringLiteral("text")).toString(), style, enc);
        if (!expectedError.isEmpty()) {
            QVERIFY(!code.isValid());
            QCOMPARE(code.errorString(), expectedError);
            return;
        }
        QVERIFY2(code.isValid(), qPrintable(code.errorString()));
        QCOMPARE(code.version(), v.value(QStringLiteral("version")).toInt());
        QCOMPARE(eccName(code.ecc()), v.value(QStringLiteral("ecc")).toString());
        QCOMPARE(code.mask(), v.value(QStringLiteral("mask")).toInt());

        const Renderer r(code, style);
        QCOMPARE(QString::fromUtf8(r.toSvg(svg)), v.value(QStringLiteral("output")).toString());
        QStringList lint;
        for (const QJsonValue &l : v.value(QStringLiteral("lint")).toArray())
            lint << l.toString();
        QCOMPARE(r.lint(), lint);
        QCOMPARE(lintNames(r.lintFlags()), lint);
        if (v.contains(QStringLiteral("logoDamage")))
            QVERIFY(std::abs(r.logoDamage() - v.value(QStringLiteral("logoDamage")).toDouble()) < 1e-6);

        /* lossless JSON round trip (style and encode options) */
        const Style again = Style::fromJson(style.toJsonData(QJsonDocument::Compact), &ok);
        QVERIFY(ok);
        QVERIFY(again == style);
        QCOMPARE(Renderer(code, again).toSvg(svg), r.toSvg(svg));
        QJsonObject encDoc;
        encDoc.insert(QStringLiteral("encode"), enc.toJson());
        QVERIFY(EncodeOptions::fromJson(QJsonDocument(encDoc).toJson()) == enc);
    }

    void lintFlags()
    {
        const Style invisible = Style::fromJson(QByteArray(R"({"modules": {"color": null}})"));
        const Renderer r(QStringLiteral("lint"), invisible);
        QVERIFY(r.lintFlags() & Lint::Invisible);
        QVERIFY(r.lint().contains(QStringLiteral("invisible")));
        QCOMPARE(lintNames(Lint::LogoRisky | Lint::Invisible),
                 QStringList({QStringLiteral("logo-risky"), QStringLiteral("invisible")}));
        QCOMPARE(lintNames(0x80000000u), QStringList(QStringLiteral("lint-0x80000000")));
        QVERIFY(Renderer(QStringLiteral("lint"), Style()).lint().isEmpty());
    }

    /* Untrusted documents fail cleanly everywhere (no crash, no hang, the
     * previous style stays in place). */
    void hostileJson_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("message");
        QTest::newRow("deep-nesting-100k") << (QByteArray("{\"x\":") + QByteArray(100000, '[')) << QStringLiteral("nesting");
        QTest::newRow("depth-65") << (QByteArray("{\"style\":{\"x\":") + QByteArray(63, '[') + QByteArray(63, ']') + "}}")
                                  << QStringLiteral("nesting");
        QTest::newRow("number-401") << (QByteArray("{\"margin\":0.") + QByteArray(399, '1') + "}") << QStringLiteral("400");
        QTest::newRow("number-1e999") << QByteArray(R"({"margin":1e999})") << QStringLiteral("non-finite");
        QTest::newRow("unknown-1e999999999999") << QByteArray(R"({"future":-1e999999999999})") << QStringLiteral("non-finite");
        QTest::newRow("nan-literal") << QByteArray(R"({"margin":NaN})") << QStringLiteral("parse error");
        QTest::newRow("truncated") << QByteArray(R"({"modules":{"shape":"dot")") << QStringLiteral("parse error");
        QTest::newRow("not-an-object") << QByteArray("[1,2,3]") << QStringLiteral("parse error");
        QTest::newRow("garbage") << QByteArray("\x01\xff\xfe{\"\x00") << QStringLiteral("parse error");
        QTest::newRow("bad-shape") << QByteArray(R"({"modules":{"shape":"nope"}})") << QStringLiteral("invalid value");
        QTest::newRow("huge-unterminated-string") << (QByteArray("{\"label\":{\"text\":\"") + QByteArray(4 << 20, 'a'))
                                                  << QStringLiteral("parse error");
    }

    void hostileJson()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, message);
        bool ok = true;
        QString error;
        const Style s = Style::fromJson(json, &ok, &error);
        QVERIFY(!ok);
        QVERIFY(s == Style());
        qInfo("%s", qPrintable(error.left(120)));
        QVERIFY2(error.startsWith(QLatin1String("parse error")) && error.contains(message), qPrintable(error));
        EncodeOptions::fromJson(json, &ok);
        QVERIFY(!ok);

        QRKudeWidget w;
        w.setText(QStringLiteral("https://kudelink.com/hostile"));
        w.setStyleJson(QStringLiteral(R"({"modules":{"shape":"dot"}})"));
        QVERIFY(w.errorString().isEmpty());
        w.setStyleJson(QString::fromUtf8(json));
        QVERIFY(!w.errorString().isEmpty());
        QCOMPARE(w.codeStyle().moduleShape(), ModuleShape::Dot); /* previous style kept */
        QVERIFY(w.isValid());
        w.resize(200, 200);
        QVERIFY(!w.grab().isNull());

#ifdef QRKUDE_HAS_QUICK
        QRKudeItem item;
        item.setText(QStringLiteral("https://kudelink.com/hostile"));
        item.setStyleJson(QStringLiteral(R"({"modules":{"shape":"dot"}})"));
        QVERIFY(item.errorString().isEmpty());
        item.setStyleJson(QString::fromUtf8(json));
        QVERIFY(!item.errorString().isEmpty());
        QVERIFY(item.isValid());
        QCOMPARE(item.effectiveStyle().moduleShape(), ModuleShape::Dot);
#endif
    }

    /* Huge but finite lengths are clamped by the core (SPEC 2.2), also when drawing. */
    void hugeLengthsRender()
    {
        const QByteArray json = R"({"margin":1e300,"border":{"width":1e300},"label":{"text":"big","size":1e300},)"
                                R"("logo":{"size":0.2,"margin":1e300},"background":{"radius":1e300}})";
        bool ok = false;
        const Style style = Style::fromJson(json, &ok);
        QVERIFY(ok);
        const Renderer r(QStringLiteral("huge"), style);
        QVERIFY(r.isValid());
        const Layout L = r.layout();
        QVERIFY(L.canvas.width() <= 4000 + 177 && L.labelSize <= 1000);
        QElapsedTimer timer;
        timer.start();
        const QImage img = r.toImage(400);
        QVERIFY(!img.isNull());
        QVERIFY(timer.elapsed() < 20000);
        QVERIFY(!r.toSvg().isEmpty());

        QRKudeWidget w;
        w.setText(QStringLiteral("huge"));
        w.setStyleJson(QString::fromUtf8(json));
        QVERIFY(w.errorString().isEmpty());
        w.resize(200, w.heightForWidth(200));
        QVERIFY(!w.grab().isNull());
    }

    void jsonRoundTrip_data() { addCaseRows(); }

    void jsonRoundTrip()
    {
        QFETCH(int, index);
        const Case &c = m_cases[index];
        bool ok = false;
        const Style style = Style::fromJson(c.json, &ok);
        QVERIFY(ok);
        const QByteArray indented = style.toJsonData();
        const Style back = Style::fromJson(indented, &ok);
        QVERIFY(ok);
        QVERIFY(back == style);
        QCOMPARE(back.toJsonData(), indented);
        const Code code = Code::encode(c.text, style);
        QCOMPARE(Renderer(code, back).toSvg(), Renderer(code, style).toSvg());
        /* through QJsonObject and Paint JSON */
        QVERIFY(Style::fromJson(style.toJson()) == style);
        QVERIFY(Paint::fromJson(style.modulePaint().toJson()) == style.modulePaint());
    }

    void pathsClosedAndNonEmpty_data() { addCaseRows(); }

    void pathsClosedAndNonEmpty()
    {
        QFETCH(int, index);
        const Renderer r = rendererFor(m_cases[index]);
        QVERIFY(r.isValid());
        const qrk_style s = r.style().toRaw();
        const QSizeF canvas = r.canvasSize();
        const QRectF bounds = QRectF(QPointF(0, 0), canvas).adjusted(-1e-6, -1e-6, 1e-6, 1e-6);
        for (int layer = 0; layer < LayerCount; layer++) {
            const QPainterPath path = r.layerPath(Layer(layer));
            const int expected = qrk_emit_layer(r.code().raw(), &s, qrk_layer(layer), nullptr, nullptr);
            int moves = 0;
            for (int i = 0; i < path.elementCount(); i++)
                moves += path.elementAt(i).type == QPainterPath::MoveToElement;
            QCOMPARE(moves, expected);
            QCOMPARE(path.isEmpty(), expected == 0);
            if (!path.isEmpty())
                QCOMPARE(path.fillRule(), Qt::WindingFill);
            if (layer == int(Layer::Background) || layer == int(Layer::Modules) ||
                layer == int(Layer::EyeFrames) || layer == int(Layer::EyeBalls))
                QVERIFY2(!path.isEmpty(), qPrintable(QStringLiteral("layer %1 empty").arg(layer)));
            const QList<QPolygonF> polys = path.toSubpathPolygons();
            QCOMPARE(polys.size(), expected);
            for (const QPolygonF &poly : polys) {
                QVERIFY(poly.size() >= 3);
                QVERIFY(poly.isClosed());
            }
            if (!path.isEmpty())
                QVERIFY(bounds.contains(path.boundingRect()));
        }
    }

    /* arc_to -> cubic conversion: circles stay circles, fillets keep their area. */
    void arcGeometry()
    {
        Style dots;
        dots.setModuleShape(ModuleShape::Dot);
        const Renderer r(QStringLiteral("ARC TEST"), dots);
        const QList<QPolygonF> polys = fineSubpaths(r.layerPath(Layer::Modules));
        QVERIFY(polys.size() > 20);
        for (const QPolygonF &poly : polys) {
            const QPointF c = poly.boundingRect().center();
            for (const QPointF &p : poly)
                QVERIFY(std::abs(std::hypot(p.x() - c.x(), p.y() - c.y()) - 0.5) < 1e-3);
            QVERIFY2(std::abs(signedArea(poly) - M_PI / 4) < 1e-3, qPrintable(QString::number(signedArea(poly)))); /* clockwise disc */
        }

        /* eye ring: outer circle clockwise, inner hole counter-clockwise */
        Style circles;
        circles.setEyeFrame(EyeShape::Circle);
        const Renderer e(QStringLiteral("ARC TEST"), circles);
        const QList<QPolygonF> rings = fineSubpaths(e.layerPath(Layer::EyeFrames));
        QCOMPARE(rings.size(), 6);
        QVERIFY(std::abs(signedArea(rings[0]) - M_PI * 3.5 * 3.5) < 0.01);
        QVERIFY(std::abs(signedArea(rings[1]) + M_PI * 2.5 * 2.5) < 0.01);

        /* liquid fillets: r^2 - pi r^2 / 4 with r = roundness / 2 */
        Style liquid;
        liquid.setModuleShape(ModuleShape::Liquid);
        const Renderer l(QStringLiteral("https://kudelink.com/liquid"), liquid);
        int fillets = 0;
        const double rr = 0.5, expected = rr * rr - M_PI * rr * rr / 4;
        for (const QPolygonF &poly : fineSubpaths(l.layerPath(Layer::Modules))) {
            const QRectF b = poly.boundingRect();
            if (std::abs(b.width() - rr) < 1e-6 && std::abs(b.height() - rr) < 1e-6) {
                fillets++;
                QVERIFY(std::abs(std::abs(signedArea(poly)) - expected) < 1e-3);
            }
        }
        QVERIFY(fillets > 0);
    }

    void brushes()
    {
        const Style style = Style::fromJson(QByteArray(kJsonBrushes));
        const Renderer r(QStringLiteral("brush"), style);
        const qrk_style s = style.toRaw();
        qrk_gradient_geometry g;

        const QBrush lb = r.layerBrush(Layer::Modules);
        QCOMPARE(lb.style(), Qt::LinearGradientPattern);
        qrk_gradient_geometry_for(r.code().raw(), &s, QRK_LAYER_MODULES, &g);
        const auto *lin = static_cast<const QLinearGradient *>(lb.gradient());
        QCOMPARE(lin->start(), QPointF(g.x1, g.y1));
        QCOMPARE(lin->finalStop(), QPointF(g.x2, g.y2));
        QCOMPARE(lin->interpolationMode(), QGradient::ComponentInterpolation);

        const QBrush rb = r.layerBrush(Layer::EyeFrames);
        QCOMPARE(rb.style(), Qt::RadialGradientPattern);
        qrk_gradient_geometry_for(r.code().raw(), &s, QRK_LAYER_EYE_FRAMES, &g);
        const auto *rad = static_cast<const QRadialGradient *>(rb.gradient());
        QCOMPARE(rad->center(), QPointF(g.cx, g.cy));
        QCOMPARE(rad->radius(), g.r);

        QCOMPARE(r.layerBrush(Layer::EyeBalls).style(), Qt::LinearGradientPattern); /* inherits */
        QCOMPARE(r.layerBrush(Layer::Background).style(), Qt::NoBrush);
        QCOMPARE(r.layerBrush(Layer::Border).color(), QColor(Qt::black));
    }

    /* Qt rendering against the core rasterizer (labels excluded: the raster
     * backend has no font engine). */
    void renderMatchesCore_data() { addCaseRows(); }

    void renderMatchesCore()
    {
        QFETCH(int, index);
        const Case &c = m_cases[index];
        const Renderer r = rendererFor(c);
        const QImage qt = flatten(r.toImage(480));
        const QImage core = coreRaster(r, qt.size(), c.hasLogo ? m_logo : QImage());
        int from = -1, to = -1;
        const Layout L = r.layout();
        if (L.hasLabel) {
            const double scale = qt.width() / L.canvas.width();
            const double f = L.labelSize;
            from = int(std::floor((L.labelAnchor.y() - 1.1 * f) * scale)) - 1;
            to = int(std::ceil((L.labelAnchor.y() + 0.4 * f) * scale)) + 1;
        }
        const Diff d = compare(qt, core, from, to);
        qInfo("core-diff %-18s %dx%d mean %.3f max %3d over64 %.4f%%", qPrintable(c.name), qt.width(),
              qt.height(), d.mean, d.max, d.over64 * 100);
        qt.save(m_out.filePath(c.name + QStringLiteral(".qt.png")));
        core.save(m_out.filePath(c.name + QStringLiteral(".core.png")));
        QVERIFY2(d.mean < 1.5, qPrintable(QStringLiteral("mean diff %1").arg(d.mean)));
        QVERIFY2(d.over64 < 0.01, qPrintable(QStringLiteral("over64 %1").arg(d.over64)));
    }

    void labelPlacement()
    {
        const Style style = Style::fromJson(QByteArray(kJsonLabel));
        const Renderer r(QStringLiteral("label"), style);
        const QImage img = flatten(r.toImage(400));
        const Layout L = r.layout();
        const double scale = img.width() / L.canvas.width();
        int left = img.width(), right = -1, top = img.height(), bottom = -1;
        for (int y = 0; y < img.height(); y++)
            for (int x = 0; x < img.width(); x++) {
                const QRgb p = img.pixel(x, y);
                if (qRed(p) > 200 && qGreen(p) < 100 && qBlue(p) < 100) { /* red label pixels */
                    left = qMin(left, x);
                    right = qMax(right, x);
                    top = qMin(top, y);
                    bottom = qMax(bottom, y);
                }
            }
        QVERIFY(right > left);
        const double centerX = (left + right + 1) / 2.0, baseline = L.labelAnchor.y() * scale;
        qInfo("label box x %d..%d y %d..%d, anchor %.1f baseline %.1f, cap height %.1f px (font %.1f px)",
              left, right, top, bottom, L.labelAnchor.x() * scale, baseline, baseline - top, 4 * scale);
        QVERIFY(std::abs(centerX - L.labelAnchor.x() * scale) < 3);
        QVERIFY(std::abs((bottom + 1) - baseline) < 2.5); /* no descenders in "SCAN ME" */
        const double capHeight = (baseline - top) / (4 * scale);
        QVERIFY2(capHeight > 0.6 && capHeight < 0.8, qPrintable(QString::number(capHeight)));
    }

    void logoRendering()
    {
        Style style;
        style.setModuleShape(ModuleShape::Fluid);
        style.setLogoSize(0.25);
        style.setLogoBackground(QColor("#ffffff"));
        const Code code = Code::encode(QStringLiteral("https://kudelink.com/logo/test"), style);
        QCOMPARE(code.ecc(), Ecc::High);
        Renderer explicitLogo(code, style);
        const QImage a = explicitLogo.toImage(400, m_logo);

        /* the same logo through a data: URI in the style */
        Style withHref = style;
        withHref.setLogoHref(m_logoUri);
        const Renderer fromHref(code, withHref);
        QVERIFY(!fromHref.styleLogo().isNull());
        const QImage b = fromHref.toImage(400);
        const Diff d = compare(a, b);
        QCOMPARE(d.max, 0);

        /* logo pixels where the logo is */
        const Layout L = explicitLogo.layout();
        const double scale = 400 / L.canvas.width();
        const QPointF ring = (L.logoBox.center() + QPointF(L.logoBox.width() * 0.4, 0)) * scale;
        const QColor ringColor = a.pixelColor(ring.toPoint());
        QVERIFY2(ringColor.red() > 200 && ringColor.green() < 90, qPrintable(ringColor.name()));
        const QColor centerColor = a.pixelColor((L.logoBox.center() * scale).toPoint());
        QCOMPARE(centerColor, QColor(Qt::white));

        /* xMidYMid meet: a wide logo is letterboxed vertically */
        const QImage wide = makeLogo(128, 32);
        const QImage c = flatten(explicitLogo.toImage(400, wide));
        const QPointF above = (L.logoBox.center() - QPointF(0, L.logoBox.height() * 0.35)) * scale;
        QCOMPARE(c.pixelColor(above.toPoint()), QColor(Qt::white)); /* logo background */

        /* agrees with the core rasterizer */
        const Diff core = compare(flatten(a), coreRaster(explicitLogo, a.size(), m_logo));
        qInfo("logo vs core: mean %.3f max %d over64 %.4f%%", core.mean, core.max, core.over64 * 100);
        QVERIFY(core.mean < 1.5);
        QVERIFY(explicitLogo.logoDamage() > 0);
        a.save(m_out.filePath(QStringLiteral("logo-test.qt.png")));
    }

    void svgOutput()
    {
        const Renderer r(QStringLiteral("svg"), Style());
        const QByteArray svg = r.toSvg(300);
        QVERIFY(svg.startsWith("<svg xmlns=\"http://www.w3.org/2000/svg\""));
        QVERIFY(svg.contains("width=\"300\""));
        QVERIFY(svg.endsWith("</svg>\n"));
        QVERIFY(Renderer().toSvg().isEmpty());
        QVERIFY(Renderer().toImage(100).isNull());
    }

    void widget()
    {
        QRKudeWidget w;
        QSignalSpy codeSpy(&w, &QRKudeWidget::codeChanged);
        QSignalSpy textSpy(&w, &QRKudeWidget::textChanged);
        QSignalSpy styleSpy(&w, &QRKudeWidget::styleChanged);
        w.setText(QStringLiteral("https://kudelink.com/widget"));
        QCOMPARE(textSpy.count(), 1);
        QCOMPARE(codeSpy.count(), 1);
        w.setText(QStringLiteral("https://kudelink.com/widget")); /* no change */
        QCOMPARE(textSpy.count(), 1);
        QVERIFY(w.isValid());

        w.setStyleJson(QString::fromUtf8(R"({"modules":{"shape":"dot"},"encode":{"ecc":"Q"}})"));
        QCOMPARE(styleSpy.count(), 1);
        QCOMPARE(w.codeStyle().moduleShape(), ModuleShape::Dot);
        QVERIFY(w.encodeOptions().ecc == std::optional<Ecc>(Ecc::Quartile));
        QVERIFY(w.styleJson().contains(QLatin1String("\"dot\"")));

        w.setStyleJson(QStringLiteral("{\"modules\": "));
        QVERIFY(!w.errorString().isEmpty());
        QCOMPARE(w.codeStyle().moduleShape(), ModuleShape::Dot);
        QCOMPARE(styleSpy.count(), 1);

        Style labelled = w.codeStyle();
        labelled.setLabelText(QStringLiteral("SCAN ME"));
        w.setCodeStyle(labelled);
        QVERIFY(w.errorString().isEmpty());
        QVERIFY(w.heightForWidth(300) > 300);

        w.resize(300, w.heightForWidth(300));
        const QImage grab = w.grab().toImage();
        QVERIFY(!grab.isNull());
        int dark = 0;
        for (int y = 0; y < grab.height(); y++)
            for (int x = 0; x < grab.width(); x++)
                dark += qGray(grab.pixel(x, y)) < 64;
        QVERIFY(dark > grab.width() * grab.height() / 5);

        w.setText(QString(5000, QLatin1Char('x')));
        QVERIFY(!w.isValid());
        QVERIFY(!w.errorString().isEmpty());
        QVERIFY(!w.grab().isNull());
    }

    /* zxing-cpp decode of the Qt PNGs and comparison against cairosvg. */
    void decodeAndCompareWithCairo()
    {
        const QString python = qEnvironmentVariable("QRKUDE_PYTHON");
        if (python.isEmpty())
            QSKIP("set QRKUDE_PYTHON to a python with zxingcpp, cairosvg, pillow and numpy");

        QJsonArray manifest;
        for (const Case &c : m_cases) {
            const Renderer r = rendererFor(c);
            const QString png = m_out.filePath(c.name + QStringLiteral(".qt.png"));
            const QString svg = m_out.filePath(c.name + QStringLiteral(".svg"));
            QVERIFY(r.toImage(480).save(png));
            QFile f(svg);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(r.toSvg());
            f.close();
            QJsonObject e;
            e.insert(QStringLiteral("name"), c.name);
            e.insert(QStringLiteral("text"), c.text);
            e.insert(QStringLiteral("png"), png);
            e.insert(QStringLiteral("svg"), svg);
            manifest.append(e);
        }
        const QString manifestPath = m_out.filePath(QStringLiteral("manifest.json"));
        const QString resultsPath = m_out.filePath(QStringLiteral("results.json"));
        QFile mf(manifestPath);
        QVERIFY(mf.open(QIODevice::WriteOnly));
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        QProcess proc;
        proc.setProcessChannelMode(QProcess::ForwardedChannels);
        proc.start(python, {QStringLiteral("-I"), QString::fromUtf8(QRKUDE_TESTS_DIR) + QStringLiteral("/check_images.py"),
                            manifestPath, resultsPath});
        QVERIFY(proc.waitForFinished(300000));
        QCOMPARE(proc.exitCode(), 0);

        const QJsonArray results = readJson(resultsPath).array();
        QCOMPARE(results.size(), manifest.size());
        int decoded = 0;
        double worstMean = 0;
        for (const QJsonValue &v : results) {
            const QJsonObject o = v.toObject();
            const bool ok = o.value(QStringLiteral("decoded")).toBool();
            decoded += ok;
            worstMean = qMax(worstMean, o.value(QStringLiteral("mean")).toDouble());
            qInfo("cairo %-18s decoded %-3s cairo-decoded %-3s mean %.3f max %3d over64 %.4f%%",
                  qPrintable(o.value(QStringLiteral("name")).toString()), ok ? "yes" : "NO",
                  o.value(QStringLiteral("cairoDecoded")).toBool() ? "yes" : "NO",
                  o.value(QStringLiteral("mean")).toDouble(), o.value(QStringLiteral("max")).toInt(),
                  o.value(QStringLiteral("over64")).toDouble() * 100);
        }
        qInfo("decoded %d / %d, worst mean diff vs cairosvg %.3f", decoded, int(results.size()), worstMean);
        QCOMPARE(decoded, int(results.size()));
        QVERIFY(worstMean < 2.5);
    }
};

QTEST_MAIN(TestQRKude)
#include "tst_qrkude.moc"
