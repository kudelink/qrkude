/*
 * QRKude Qt - QWidget that shows a styled QR code.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef QRKUDEWIDGET_H
#define QRKUDEWIDGET_H

#include <QtGui/QImage>
#include <QtWidgets/QWidget>

#include "qrkuderenderer.h"

/*
 * A widget that shows `text` as a QR code. Style it with a JSON string
 * (styleJson) or a QRKude::Style (setCodeStyle). The code is centered and
 * keeps its aspect ratio; the widget repaints whenever a property changes.
 * When encoding fails (text too long), the error text is shown instead.
 */
class QRKudeWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString styleJson READ styleJson WRITE setStyleJson NOTIFY styleChanged)
    Q_PROPERTY(QImage logo READ logo WRITE setLogo NOTIFY logoChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY codeChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY codeChanged)
    Q_PROPERTY(QStringList lint READ lint NOTIFY codeChanged)

public:
    explicit QRKudeWidget(QWidget *parent = nullptr);
    ~QRKudeWidget() override;

    QString text() const { return m_text; }
    void setText(const QString &text);

    /* Reading gives the current style as compact JSON. Setting it parses the
     * text like Style::fromJson() and also applies an "encode" object of the
     * document. Invalid JSON keeps the previous style and sets errorString();
     * styleChanged() is emitted only for valid JSON. */
    QString styleJson() const;
    void setStyleJson(const QString &json);

    /* The style as a C++ value (named codeStyle because QWidget::style()
     * already exists). */
    QRKude::Style codeStyle() const { return m_style; }
    void setCodeStyle(const QRKude::Style &style);
    QRKude::EncodeOptions encodeOptions() const { return m_options; }
    void setEncodeOptions(const QRKude::EncodeOptions &options);

    /* Logo image. Null (default) = load it from the style's logo href when
     * that is a data: URI, a local file or a Qt resource. The style must
     * still have a logo size > 0 for a logo to appear. */
    QImage logo() const { return m_logo; }
    void setLogo(const QImage &logo);

    /* false when the text could not be encoded. */
    bool isValid() const { return m_renderer.isValid(); }
    /* The encoding or JSON error; empty when everything is fine. */
    QString errorString() const { return m_error; }
    /* Scannability warnings of the current code, see Renderer::lint(). */
    QStringList lint() const { return m_renderer.lint(); }
    /* The renderer used for painting (for toSvg(), layout(), logoDamage()...). */
    const QRKude::Renderer &renderer() const { return m_renderer; }

    /* The current code as an image `width` pixels wide, with the logo. */
    QImage toImage(int width) const { return m_renderer.toImage(width, m_logo); }

    QSize sizeHint() const override;        /* 256 px wide */
    QSize minimumSizeHint() const override; /* 64 px wide */
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override; /* keeps the canvas aspect ratio */

signals:
    void textChanged(const QString &text);
    void styleChanged();
    void logoChanged();
    /* The code was encoded or styled again; valid, errorString and lint may
     * have changed. */
    void codeChanged();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuild();

    QString m_text;
    QRKude::Style m_style;
    QRKude::EncodeOptions m_options;
    QImage m_logo;
    QRKude::Renderer m_renderer;
    QString m_error;
    QString m_jsonError;
};

#endif // QRKUDEWIDGET_H
