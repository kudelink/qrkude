/*
 * QRKude Qt - QWidget that shows a styled QR code.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "qrkudewidget.h"

#include <QtGui/QPainter>

QRKudeWidget::QRKudeWidget(QWidget *parent) : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    rebuild();
}

QRKudeWidget::~QRKudeWidget() = default;

void QRKudeWidget::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    rebuild();
    emit textChanged(m_text);
}

QString QRKudeWidget::styleJson() const
{
    return QString::fromUtf8(m_style.toJsonData(QJsonDocument::Compact));
}

void QRKudeWidget::setStyleJson(const QString &json)
{
    const QByteArray bytes = json.toUtf8();
    bool ok = false;
    QString error;
    const QRKude::Style style = QRKude::Style::fromJson(bytes, &ok, &error);
    if (!ok) {
        m_jsonError = error;
        m_error = error;
        update();
        emit codeChanged();
        return;
    }
    m_jsonError.clear();
    m_style = style;
    m_options = QRKude::EncodeOptions::fromJson(bytes);
    rebuild();
    emit styleChanged();
}

void QRKudeWidget::setCodeStyle(const QRKude::Style &style)
{
    if (style == m_style && m_jsonError.isEmpty())
        return;
    m_jsonError.clear();
    m_style = style;
    rebuild();
    emit styleChanged();
}

void QRKudeWidget::setEncodeOptions(const QRKude::EncodeOptions &options)
{
    if (options == m_options)
        return;
    m_options = options;
    rebuild();
}

void QRKudeWidget::setLogo(const QImage &logo)
{
    m_logo = logo;
    update();
    emit logoChanged();
}

void QRKudeWidget::rebuild()
{
    const QRKude::Code code = QRKude::Code::encode(m_text, m_style, m_options);
    m_renderer.setStyle(m_style);
    m_renderer.setCode(code);
    m_error = code.isValid() ? m_jsonError : code.errorString();
    updateGeometry();
    update();
    emit codeChanged();
}

QSize QRKudeWidget::sizeHint() const
{
    return QSize(256, heightForWidth(256));
}

QSize QRKudeWidget::minimumSizeHint() const
{
    return QSize(64, heightForWidth(64));
}

int QRKudeWidget::heightForWidth(int width) const
{
    const QSizeF canvas = m_renderer.canvasSize();
    if (canvas.isEmpty())
        return width;
    return qRound(width * canvas.height() / canvas.width());
}

void QRKudeWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (!m_renderer.isValid()) {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_error);
        return;
    }
    m_renderer.paint(painter, QRectF(rect()), m_logo);
}
