/*
 * QRKude Qt - Qt Quick item (QML type "QRKudeItem" in module "QRKude 1.0").
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef QRKUDEITEM_H
#define QRKUDEITEM_H

#include <QtCore/QUrl>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtQuick/QQuickPaintedItem>

#include "qrkuderenderer.h"

/*
 * A Qt Quick item that shows a QR code. Register it with
 * QRKude::registerQmlTypes(), then:
 *
 *   import QRKude 1.0
 *   QRKudeItem {
 *       width: 300; height: width * implicitHeight / implicitWidth
 *       text: "https://example.com"
 *       styleJson: '{"modules": {"shape": "fluid"}}'   // base style
 *       moduleShape: "liquid"                           // overrides
 *       eyeFrame: "rounded"; eyeBall: "circle"
 *       color: "#1d3557"; backgroundColor: "white"
 *       logoSource: "qrc:/logo.png"
 *       label: "SCAN ME"
 *   }
 *
 * styleJson gives the base style. The other style properties override it
 * when they are set (a non-empty string or a valid color). Shape names are
 * the JSON names ("square", "dot", "fluid", ... and "rounded", "circle",
 * "leaf", ...). An invalid styleJson keeps the last valid one and an
 * unknown name is ignored; both set errorString. logoSource accepts file:, qrc: and data: URLs (no
 * network); when it is set and the style has no logo size, 0.2 is used.
 * The implicit size is 200 px wide with the height of the canvas aspect.
 */
class QRKudeItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString styleJson READ styleJson WRITE setStyleJson NOTIFY styleJsonChanged)
    Q_PROPERTY(QString moduleShape READ moduleShape WRITE setModuleShape NOTIFY moduleShapeChanged)
    Q_PROPERTY(QString eyeFrame READ eyeFrame WRITE setEyeFrame NOTIFY eyeFrameChanged)
    Q_PROPERTY(QString eyeBall READ eyeBall WRITE setEyeBall NOTIFY eyeBallChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QUrl logoSource READ logoSource WRITE setLogoSource NOTIFY logoSourceChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString ecc READ ecc WRITE setEcc NOTIFY eccChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY codeChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY codeChanged)
    Q_PROPERTY(QStringList lint READ lint NOTIFY codeChanged)
    Q_PROPERTY(int version READ version NOTIFY codeChanged)
    Q_PROPERTY(qreal logoDamage READ logoDamage NOTIFY codeChanged)

public:
    explicit QRKudeItem(QQuickItem *parent = nullptr);
    ~QRKudeItem() override;

    QString text() const { return m_text; }
    void setText(const QString &text);
    QString styleJson() const { return m_styleJson; }
    void setStyleJson(const QString &json);
    QString moduleShape() const { return m_moduleShape; }
    void setModuleShape(const QString &shape);
    QString eyeFrame() const { return m_eyeFrame; }
    void setEyeFrame(const QString &shape);
    QString eyeBall() const { return m_eyeBall; }
    void setEyeBall(const QString &shape);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);
    QUrl logoSource() const { return m_logoSource; }
    void setLogoSource(const QUrl &source);
    QString label() const { return m_label; }
    void setLabel(const QString &label);
    /* Minimum error correction: "" = automatic (H with a logo, else M), or
     * "L", "M", "Q", "H". */
    QString ecc() const { return m_ecc; }
    void setEcc(const QString &ecc);

    bool isValid() const { return m_renderer.isValid(); }  /* text was encoded */
    QString errorString() const { return m_error; }        /* empty when fine */
    QStringList lint() const { return m_renderer.lint(); } /* see Renderer::lint() */
    int version() const { return m_renderer.code().version(); } /* 1..40, 0 if invalid */
    qreal logoDamage() const { return m_renderer.logoDamage(); } /* see Renderer::logoDamage() */

    /* The effective style (styleJson plus the overriding properties). */
    QRKude::Style effectiveStyle() const { return m_renderer.style(); }
    const QRKude::Renderer &renderer() const { return m_renderer; }

    /* From QML: the effective style as JSON, the code as an SVG document,
     * and saving to a local file (".svg" writes SVG; other suffixes such as
     * ".png" write an image `width` pixels wide). save() returns false on
     * failure. */
    Q_INVOKABLE QString effectiveStyleJson() const;
    Q_INVOKABLE QString toSvg(qreal widthPx = 0) const;
    Q_INVOKABLE bool save(const QUrl &fileUrl, int width = 1024) const;

    void paint(QPainter *painter) override;

signals:
    void textChanged();
    void styleJsonChanged();
    void moduleShapeChanged();
    void eyeFrameChanged();
    void eyeBallChanged();
    void colorChanged();
    void backgroundColorChanged();
    void logoSourceChanged();
    void labelChanged();
    void eccChanged();
    void codeChanged();

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
#else
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;
#endif

private:
    void rebuild();

    QString m_text;
    QString m_styleJson;
    QRKude::Style m_baseStyle;          /* last valid styleJson */
    QRKude::EncodeOptions m_baseOptions;
    QString m_styleError;
    QString m_moduleShape, m_eyeFrame, m_eyeBall;
    QColor m_color, m_backgroundColor;
    QUrl m_logoSource;
    QImage m_logo;
    QString m_label;
    QString m_ecc;
    QString m_error;
    QRKude::Renderer m_renderer;
};

namespace QRKude {
/* Registers QRKudeItem as `uri` 1.0 (default "QRKude"). Call it before
 * loading QML that imports the module. */
void registerQmlTypes(const char *uri = "QRKude");
} // namespace QRKude

#endif // QRKUDEITEM_H
