/*
 * QRKude Qt - designer example: live style editor.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef DESIGNERWINDOW_H
#define DESIGNERWINDOW_H

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QToolButton>

#include "qrkudeqt.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSlider;

/* Button showing a color swatch; opens a QColorDialog (with alpha). */
class ColorButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ColorButton(const QColor &color, QWidget *parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);

signals:
    void colorChanged(const QColor &color);

private:
    void pick();
    QColor m_color;
};

class DesignerWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit DesignerWindow(QWidget *parent = nullptr);

    QRKudeWidget *preview() const { return m_preview; }
    bool loadLogo(const QString &path);
    bool importJson(const QString &path);

private:
    QWidget *buildControls();
    void connectControls();
    QRKude::Style styleFromControls() const;
    QRKude::Paint paintFromControls() const;
    void setControls(const QRKude::Style &style, const QRKude::EncodeOptions &options);
    void updatePreview();
    void updateStatus();
    void updateEnabled();

    void chooseLogo();
    void clearLogo();
    void exportPng();
    void exportSvg();
    void exportJson();
    void importJsonDialog();

    QRKudeWidget *m_preview = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_updating = false;

    /* content */
    QPlainTextEdit *m_text = nullptr;
    QComboBox *m_ecc = nullptr;
    /* modules */
    QComboBox *m_moduleShape = nullptr;
    QSlider *m_moduleSize = nullptr;
    QSlider *m_roundness = nullptr;
    QComboBox *m_fillType = nullptr;
    ColorButton *m_color1 = nullptr;
    ColorButton *m_color2 = nullptr;
    QSlider *m_angle = nullptr;
    /* eyes */
    QComboBox *m_eyeFrame = nullptr;
    QComboBox *m_eyeBall = nullptr;
    QCheckBox *m_eyeColorOn = nullptr;
    ColorButton *m_eyeColor = nullptr;
    QComboBox *m_alignment = nullptr;
    /* canvas */
    QSlider *m_margin = nullptr;
    QCheckBox *m_transparent = nullptr;
    ColorButton *m_background = nullptr;
    QSlider *m_radius = nullptr;
    QDoubleSpinBox *m_borderWidth = nullptr;
    ColorButton *m_borderColor = nullptr;
    /* logo */
    QLineEdit *m_logoPath = nullptr;
    QSlider *m_logoSize = nullptr;
    QComboBox *m_logoShape = nullptr;
    QCheckBox *m_excavate = nullptr;
    QCheckBox *m_logoBgOn = nullptr;
    ColorButton *m_logoBg = nullptr;
    QDoubleSpinBox *m_logoMargin = nullptr;
    /* label */
    QLineEdit *m_label = nullptr;
    QFontComboBox *m_font = nullptr;
    QComboBox *m_labelPosition = nullptr;
    QSlider *m_labelSize = nullptr;
    QCheckBox *m_bold = nullptr;
    ColorButton *m_labelColor = nullptr;

    QImage m_logo;
    QString m_logoHref;
};

#endif // DESIGNERWINDOW_H
