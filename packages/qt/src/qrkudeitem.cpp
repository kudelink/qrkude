/*
 * QRKude Qt - Qt Quick item.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "qrkudeitem.h"

#include <QtCore/QFile>
#include <QtGui/QPainter>
#include <QtQml/qqml.h>

namespace {

QString localPath(const QUrl &url)
{
    if (url.scheme() == QLatin1String("qrc"))
        return QLatin1Char(':') + url.path();
    if (url.isLocalFile())
        return url.toLocalFile();
    if (url.isRelative())
        return url.toString();
    return QString();
}

} // namespace

QRKudeItem::QRKudeItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setImplicitSize(200, 200);
    rebuild();
}

QRKudeItem::~QRKudeItem() = default;

#define QRKUDE_SETTER(member, value, signal) \
    do {                                     \
        if ((member) == (value))             \
            return;                          \
        (member) = (value);                  \
        rebuild();                           \
        emit signal();                       \
    } while (0)

void QRKudeItem::setText(const QString &text) { QRKUDE_SETTER(m_text, text, textChanged); }
void QRKudeItem::setStyleJson(const QString &json)
{
    if (json == m_styleJson)
        return;
    m_styleJson = json;
    /* An invalid document keeps the last valid base style and reports the
     * error; the original bytes go to the core reader (SPEC 2.0 limits). */
    if (json.trimmed().isEmpty()) {
        m_baseStyle = QRKude::Style();
        m_baseOptions = QRKude::EncodeOptions();
        m_styleError.clear();
    } else {
        bool ok = false;
        QString error;
        const QByteArray bytes = json.toUtf8();
        const QRKude::Style style = QRKude::Style::fromJson(bytes, &ok, &error);
        if (ok) {
            m_baseStyle = style;
            m_baseOptions = QRKude::EncodeOptions::fromJson(bytes);
            m_styleError.clear();
        } else {
            m_styleError = error;
        }
    }
    rebuild();
    emit styleJsonChanged();
}
void QRKudeItem::setModuleShape(const QString &shape) { QRKUDE_SETTER(m_moduleShape, shape, moduleShapeChanged); }
void QRKudeItem::setEyeFrame(const QString &shape) { QRKUDE_SETTER(m_eyeFrame, shape, eyeFrameChanged); }
void QRKudeItem::setEyeBall(const QString &shape) { QRKUDE_SETTER(m_eyeBall, shape, eyeBallChanged); }
void QRKudeItem::setColor(const QColor &color) { QRKUDE_SETTER(m_color, color, colorChanged); }
void QRKudeItem::setBackgroundColor(const QColor &color)
{
    QRKUDE_SETTER(m_backgroundColor, color, backgroundColorChanged);
}
void QRKudeItem::setLabel(const QString &label) { QRKUDE_SETTER(m_label, label, labelChanged); }
void QRKudeItem::setEcc(const QString &ecc) { QRKUDE_SETTER(m_ecc, ecc, eccChanged); }

void QRKudeItem::setLogoSource(const QUrl &source)
{
    if (source == m_logoSource)
        return;
    m_logoSource = source;
    m_logo = QImage();
    if (!source.isEmpty()) {
        if (source.scheme() == QLatin1String("data"))
            m_logo = QRKude::Renderer::loadImage(source.toString());
        else
            m_logo = QImage(localPath(source));
    }
    rebuild();
    emit logoSourceChanged();
}

#undef QRKUDE_SETTER

void QRKudeItem::rebuild()
{
    QString error = m_styleError;
    QRKude::Style style = m_baseStyle;
    QRKude::EncodeOptions options = m_baseOptions;
    if (!m_moduleShape.isEmpty()) {
        if (auto shape = QRKude::moduleShapeFromName(m_moduleShape))
            style.setModuleShape(*shape);
        else
            error = QStringLiteral("unknown module shape \"%1\"").arg(m_moduleShape);
    }
    if (!m_eyeFrame.isEmpty()) {
        if (auto shape = QRKude::eyeShapeFromName(m_eyeFrame))
            style.setEyeFrame(*shape);
        else
            error = QStringLiteral("unknown eye shape \"%1\"").arg(m_eyeFrame);
    }
    if (!m_eyeBall.isEmpty()) {
        if (auto shape = QRKude::eyeShapeFromName(m_eyeBall))
            style.setEyeBall(*shape);
        else
            error = QStringLiteral("unknown eye shape \"%1\"").arg(m_eyeBall);
    }
    if (m_color.isValid())
        style.setColor(m_color);
    if (m_backgroundColor.isValid())
        style.setBackgroundColor(m_backgroundColor);
    if (!m_label.isEmpty())
        style.setLabelText(m_label);
    if (!m_logo.isNull() && !style.hasLogo())
        style.setLogoSize(0.2);
    if (!m_logoSource.isEmpty() && m_logo.isNull())
        error = QStringLiteral("cannot load logo %1").arg(m_logoSource.toString());
    if (!m_ecc.isEmpty()) {
        if (auto ecc = QRKude::eccFromName(m_ecc))
            options.ecc = *ecc;
        else
            error = QStringLiteral("unknown ECC level \"%1\"").arg(m_ecc);
    }

    const QRKude::Code code = QRKude::Code::encode(m_text, style, options);
    m_renderer.setStyle(style);
    m_renderer.setCode(code);
    m_error = code.isValid() ? error : code.errorString();

    const QSizeF canvas = m_renderer.canvasSize();
    if (!canvas.isEmpty())
        setImplicitHeight(implicitWidth() * canvas.height() / canvas.width());
    update();
    emit codeChanged();
}

QString QRKudeItem::effectiveStyleJson() const
{
    return QString::fromUtf8(m_renderer.style().toJsonData(QJsonDocument::Indented));
}

QString QRKudeItem::toSvg(qreal widthPx) const
{
    return QString::fromUtf8(m_renderer.toSvg(widthPx));
}

bool QRKudeItem::save(const QUrl &fileUrl, int width) const
{
    const QString path = localPath(fileUrl);
    if (path.isEmpty() || !m_renderer.isValid())
        return false;
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(m_renderer.toSvg()) >= 0;
    }
    return m_renderer.toImage(width, m_logo).save(path);
}

void QRKudeItem::paint(QPainter *painter)
{
    if (painter && m_renderer.isValid())
        m_renderer.paint(*painter, boundingRect(), m_logo);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void QRKudeItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    update();
}
#else
void QRKudeItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChanged(newGeometry, oldGeometry);
    update();
}
#endif

void QRKude::registerQmlTypes(const char *uri)
{
    qmlRegisterType<QRKudeItem>(uri, 1, 0, "QRKudeItem");
}
