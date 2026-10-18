/*
 * QRKude Qt - designer example: live style editor.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "designerwindow.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMimeDatabase>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFontComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QVBoxLayout>

#include <cmath>

using namespace QRKude;

/* ---- ColorButton -------------------------------------------------------- */

ColorButton::ColorButton(const QColor &color, QWidget *parent) : QToolButton(parent)
{
    setColor(color);
    setMinimumWidth(56);
    connect(this, &QToolButton::clicked, this, &ColorButton::pick);
}

void ColorButton::setColor(const QColor &color)
{
    m_color = color;
    QPixmap swatch(36, 16);
    swatch.fill(Qt::white);
    QPainter p(&swatch);
    /* checkerboard so that transparency is visible */
    for (int y = 0; y < 16; y += 4)
        for (int x = 0; x < 36; x += 4)
            if ((x / 4 + y / 4) % 2)
                p.fillRect(x, y, 4, 4, QColor(204, 204, 204));
    p.fillRect(swatch.rect(), color);
    p.setPen(Qt::darkGray);
    p.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    p.end();
    setIcon(QIcon(swatch));
    setIconSize(swatch.size());
    setToolTip(colorName(color));
}

void ColorButton::pick()
{
    const QColor c = QColorDialog::getColor(m_color, this, tr("Choose a color"),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid() && c != m_color) {
        setColor(c);
        emit colorChanged(c);
    }
}

/* ---- helpers ------------------------------------------------------------ */

static QSlider *slider(int min, int max, int value)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

static void setCombo(QComboBox *combo, const QString &text)
{
    const int i = combo->findText(text);
    if (i >= 0)
        combo->setCurrentIndex(i);
}

/* ---- DesignerWindow ----------------------------------------------------- */

DesignerWindow::DesignerWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("QRKude Designer"));

    m_preview = new QRKudeWidget;
    m_preview->setMinimumSize(320, 320);

    auto *scroll = new QScrollArea;
    scroll->setWidget(buildControls());
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(360);

    auto *splitter = new QSplitter;
    splitter->addWidget(scroll);
    splitter->addWidget(m_preview);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    QMenu *file = menuBar()->addMenu(tr("&File"));
    QToolBar *tools = addToolBar(tr("Export"));
    const auto action = [&](const QString &text, const QKeySequence &key, void (DesignerWindow::*slot)()) {
        QAction *a = file->addAction(text);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        tools->addAction(a);
    };
    action(tr("Import JSON..."), QKeySequence::Open, &DesignerWindow::importJsonDialog);
    action(tr("Export PNG..."), QKeySequence(tr("Ctrl+E")), &DesignerWindow::exportPng);
    action(tr("Export SVG..."), QKeySequence(tr("Ctrl+Shift+E")), &DesignerWindow::exportSvg);
    action(tr("Export JSON..."), QKeySequence::Save, &DesignerWindow::exportJson);
    file->addSeparator();
    QAction *quit = file->addAction(tr("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    m_statusLabel = new QLabel;
    m_statusLabel->setTextFormat(Qt::PlainText);
    statusBar()->addWidget(m_statusLabel, 1);

    connectControls();
    connect(m_preview, &QRKudeWidget::codeChanged, this, &DesignerWindow::updateStatus);
    updateEnabled();
    updatePreview();
    resize(980, 700);
}

QWidget *DesignerWindow::buildControls()
{
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    const auto group = [&](const QString &title) {
        auto *box = new QGroupBox(title);
        auto *form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addWidget(box);
        return form;
    };

    QFormLayout *content = group(tr("Content"));
    m_text = new QPlainTextEdit(QStringLiteral("https://kudelink.com"));
    m_text->setMaximumHeight(64);
    content->addRow(tr("Text"), m_text);
    m_ecc = new QComboBox;
    m_ecc->addItems({tr("Auto"), QStringLiteral("L"), QStringLiteral("M"), QStringLiteral("Q"), QStringLiteral("H")});
    m_ecc->setToolTip(tr("Auto = H with a logo, else M"));
    content->addRow(tr("Error correction"), m_ecc);

    QFormLayout *modules = group(tr("Modules"));
    m_moduleShape = new QComboBox;
    m_moduleShape->addItems(moduleShapeNames());
    setCombo(m_moduleShape, QStringLiteral("fluid"));
    modules->addRow(tr("Shape"), m_moduleShape);
    m_moduleSize = slider(10, 100, 100);
    modules->addRow(tr("Size"), m_moduleSize);
    m_roundness = slider(0, 100, 100);
    modules->addRow(tr("Roundness"), m_roundness);
    m_fillType = new QComboBox;
    m_fillType->addItems({tr("Solid"), tr("Linear gradient"), tr("Radial gradient")});
    modules->addRow(tr("Fill"), m_fillType);
    auto *colors = new QWidget;
    auto *colorsLayout = new QHBoxLayout(colors);
    colorsLayout->setContentsMargins(0, 0, 0, 0);
    m_color1 = new ColorButton(QColor(0x1d, 0x35, 0x57));
    m_color2 = new ColorButton(QColor(0x25, 0x75, 0xfc));
    colorsLayout->addWidget(m_color1);
    colorsLayout->addWidget(m_color2);
    colorsLayout->addStretch();
    modules->addRow(tr("Colors"), colors);
    m_angle = slider(0, 359, 45);
    modules->addRow(tr("Angle"), m_angle);

    QFormLayout *eyes = group(tr("Eyes"));
    m_eyeFrame = new QComboBox;
    m_eyeFrame->addItems(eyeShapeNames());
    setCombo(m_eyeFrame, QStringLiteral("rounded"));
    eyes->addRow(tr("Frame"), m_eyeFrame);
    m_eyeBall = new QComboBox;
    m_eyeBall->addItems(eyeShapeNames());
    setCombo(m_eyeBall, QStringLiteral("circle"));
    eyes->addRow(tr("Ball"), m_eyeBall);
    auto *eyeColor = new QWidget;
    auto *eyeColorLayout = new QHBoxLayout(eyeColor);
    eyeColorLayout->setContentsMargins(0, 0, 0, 0);
    m_eyeColorOn = new QCheckBox(tr("Custom"));
    m_eyeColor = new ColorButton(QColor(0xe6, 0x39, 0x46));
    eyeColorLayout->addWidget(m_eyeColorOn);
    eyeColorLayout->addWidget(m_eyeColor);
    eyeColorLayout->addStretch();
    eyes->addRow(tr("Color"), eyeColor);
    m_alignment = new QComboBox;
    m_alignment->addItem(tr("as modules"));
    m_alignment->addItems(eyeShapeNames());
    eyes->addRow(tr("Alignment"), m_alignment);

    QFormLayout *canvas = group(tr("Canvas"));
    m_margin = slider(0, 10, 4);
    canvas->addRow(tr("Margin"), m_margin);
    auto *bg = new QWidget;
    auto *bgLayout = new QHBoxLayout(bg);
    bgLayout->setContentsMargins(0, 0, 0, 0);
    m_background = new ColorButton(Qt::white);
    m_transparent = new QCheckBox(tr("Transparent"));
    bgLayout->addWidget(m_background);
    bgLayout->addWidget(m_transparent);
    bgLayout->addStretch();
    canvas->addRow(tr("Background"), bg);
    m_radius = slider(0, 20, 0);
    canvas->addRow(tr("Corner radius"), m_radius);
    auto *border = new QWidget;
    auto *borderLayout = new QHBoxLayout(border);
    borderLayout->setContentsMargins(0, 0, 0, 0);
    m_borderWidth = new QDoubleSpinBox;
    m_borderWidth->setRange(0, 4);
    m_borderWidth->setSingleStep(0.25);
    m_borderWidth->setSuffix(tr(" mod"));
    m_borderColor = new ColorButton(QColor(0x1d, 0x35, 0x57));
    borderLayout->addWidget(m_borderWidth);
    borderLayout->addWidget(m_borderColor);
    borderLayout->addStretch();
    canvas->addRow(tr("Border"), border);

    QFormLayout *logo = group(tr("Logo"));
    auto *logoFile = new QWidget;
    auto *logoFileLayout = new QHBoxLayout(logoFile);
    logoFileLayout->setContentsMargins(0, 0, 0, 0);
    m_logoPath = new QLineEdit;
    m_logoPath->setReadOnly(true);
    m_logoPath->setPlaceholderText(tr("no logo"));
    auto *browse = new QPushButton(tr("Choose..."));
    auto *clear = new QPushButton(tr("Clear"));
    connect(browse, &QPushButton::clicked, this, &DesignerWindow::chooseLogo);
    connect(clear, &QPushButton::clicked, this, &DesignerWindow::clearLogo);
    logoFileLayout->addWidget(m_logoPath, 1);
    logoFileLayout->addWidget(browse);
    logoFileLayout->addWidget(clear);
    logo->addRow(tr("Image"), logoFile);
    m_logoSize = slider(5, 40, 22);
    logo->addRow(tr("Size"), m_logoSize);
    m_logoShape = new QComboBox;
    m_logoShape->addItems({QStringLiteral("square"), QStringLiteral("rounded"), QStringLiteral("circle")});
    setCombo(m_logoShape, QStringLiteral("circle"));
    logo->addRow(tr("Shape"), m_logoShape);
    m_logoMargin = new QDoubleSpinBox;
    m_logoMargin->setRange(0, 4);
    m_logoMargin->setSingleStep(0.5);
    m_logoMargin->setValue(1);
    m_logoMargin->setSuffix(tr(" mod"));
    logo->addRow(tr("Margin"), m_logoMargin);
    m_excavate = new QCheckBox(tr("Remove modules behind the logo"));
    m_excavate->setChecked(true);
    logo->addRow(QString(), m_excavate);
    auto *logoBg = new QWidget;
    auto *logoBgLayout = new QHBoxLayout(logoBg);
    logoBgLayout->setContentsMargins(0, 0, 0, 0);
    m_logoBgOn = new QCheckBox(tr("Fill"));
    m_logoBgOn->setChecked(true);
    m_logoBg = new ColorButton(Qt::white);
    logoBgLayout->addWidget(m_logoBgOn);
    logoBgLayout->addWidget(m_logoBg);
    logoBgLayout->addStretch();
    logo->addRow(tr("Background"), logoBg);

    QFormLayout *label = group(tr("Label"));
    m_label = new QLineEdit;
    m_label->setPlaceholderText(tr("e.g. SCAN ME"));
    label->addRow(tr("Text"), m_label);
    m_font = new QFontComboBox;
    label->addRow(tr("Font"), m_font);
    m_labelPosition = new QComboBox;
    m_labelPosition->addItems({tr("bottom"), tr("top")});
    label->addRow(tr("Position"), m_labelPosition);
    m_labelSize = slider(10, 80, 40);
    label->addRow(tr("Size"), m_labelSize);
    auto *labelStyle = new QWidget;
    auto *labelStyleLayout = new QHBoxLayout(labelStyle);
    labelStyleLayout->setContentsMargins(0, 0, 0, 0);
    m_bold = new QCheckBox(tr("Bold"));
    m_bold->setChecked(true);
    m_labelColor = new ColorButton(QColor(0x1d, 0x35, 0x57));
    labelStyleLayout->addWidget(m_bold);
    labelStyleLayout->addWidget(m_labelColor);
    labelStyleLayout->addStretch();
    label->addRow(tr("Style"), labelStyle);

    layout->addStretch();
    return root;
}

void DesignerWindow::connectControls()
{
    const auto changed = [this] {
        if (m_updating)
            return;
        updateEnabled();
        updatePreview();
    };
    connect(m_text, &QPlainTextEdit::textChanged, this, changed);
    for (QComboBox *c : {m_ecc, m_moduleShape, m_fillType, m_eyeFrame, m_eyeBall, m_alignment, m_logoShape,
                         m_labelPosition})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
    connect(m_font, &QFontComboBox::currentFontChanged, this, changed);
    for (QSlider *s : {m_moduleSize, m_roundness, m_angle, m_margin, m_radius, m_logoSize, m_labelSize})
        connect(s, &QSlider::valueChanged, this, changed);
    for (ColorButton *b : {m_color1, m_color2, m_eyeColor, m_background, m_borderColor, m_logoBg, m_labelColor})
        connect(b, &ColorButton::colorChanged, this, changed);
    for (QCheckBox *c : {m_eyeColorOn, m_transparent, m_excavate, m_logoBgOn, m_bold})
        connect(c, &QCheckBox::toggled, this, changed);
    for (QDoubleSpinBox *s : {m_borderWidth, m_logoMargin})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, changed);
    connect(m_label, &QLineEdit::textChanged, this, changed);
}

void DesignerWindow::updateEnabled()
{
    const int fill = m_fillType->currentIndex();
    m_color2->setEnabled(fill != 0);
    m_angle->setEnabled(fill == 1);
    const QString shape = m_moduleShape->currentText();
    const bool sized = !QStringList{QStringLiteral("fluid"), QStringLiteral("liquid"), QStringLiteral("bevel"),
                                    QStringLiteral("classy")}
                            .contains(shape);
    m_moduleSize->setEnabled(sized);
    m_roundness->setEnabled(!QStringList{QStringLiteral("square"), QStringLiteral("dot"),
                                         QStringLiteral("diamond"), QStringLiteral("star")}
                                 .contains(shape));
    m_eyeColor->setEnabled(m_eyeColorOn->isChecked());
    m_background->setEnabled(!m_transparent->isChecked());
    m_borderColor->setEnabled(m_borderWidth->value() > 0);
    const bool logo = !m_logo.isNull();
    for (QWidget *w : std::initializer_list<QWidget *>{m_logoSize, m_logoShape, m_excavate, m_logoBgOn,
                                                       m_logoBg, m_logoMargin})
        w->setEnabled(logo);
    m_logoBg->setEnabled(logo && m_logoBgOn->isChecked());
}

Paint DesignerWindow::paintFromControls() const
{
    switch (m_fillType->currentIndex()) {
    case 1: return Paint::linear(m_color1->color(), m_color2->color(), m_angle->value());
    case 2: return Paint::radial(m_color1->color(), m_color2->color());
    default: return Paint::solid(m_color1->color());
    }
}

Style DesignerWindow::styleFromControls() const
{
    Style s;
    s.setMargin(m_margin->value());
    s.setModuleShape(*moduleShapeFromName(m_moduleShape->currentText()));
    s.setModuleSize(m_moduleSize->value() / 100.0);
    s.setModuleRoundness(m_roundness->value() / 100.0);
    s.setModulePaint(paintFromControls());

    s.setEyeFrame(*eyeShapeFromName(m_eyeFrame->currentText()));
    s.setEyeBall(*eyeShapeFromName(m_eyeBall->currentText()));
    if (m_eyeColorOn->isChecked()) {
        s.setEyeFramePaint(m_eyeColor->color());
        s.setEyeBallPaint(m_eyeColor->color());
    }
    if (m_alignment->currentIndex() > 0) {
        const EyeShape a = *eyeShapeFromName(m_alignment->currentText());
        s.setAlignmentFrame(a);
        s.setAlignmentBall(a);
    }

    s.setBackground(m_transparent->isChecked() ? Paint::none() : Paint::solid(m_background->color()));
    s.setBackgroundRadius(m_radius->value() / 2.0);
    s.setBorderWidth(m_borderWidth->value());
    s.setBorderColor(m_borderColor->color());

    if (!m_logo.isNull()) {
        s.setLogoSize(m_logoSize->value() / 100.0);
        s.setLogoShape(*logoShapeFromName(m_logoShape->currentText()));
        s.setLogoMargin(m_logoMargin->value());
        s.setLogoExcavate(m_excavate->isChecked());
        s.setLogoBackground(m_logoBgOn->isChecked() ? m_logoBg->color() : QColor(Qt::transparent));
        s.setLogoHref(m_logoHref);
    }

    s.setLabelText(m_label->text());
    s.setLabelFont(m_font->currentFont().family() + QStringLiteral(", sans-serif"));
    s.setLabelPosition(m_labelPosition->currentIndex() == 1 ? LabelPosition::Top : LabelPosition::Bottom);
    s.setLabelSize(m_labelSize->value() / 10.0);
    s.setLabelBold(m_bold->isChecked());
    s.setLabelColor(m_labelColor->color());
    return s;
}

void DesignerWindow::setControls(const Style &s, const EncodeOptions &options)
{
    m_updating = true;
    m_ecc->setCurrentIndex(options.ecc ? int(*options.ecc) + 1 : 0);
    m_margin->setValue(qRound(s.margin()));
    setCombo(m_moduleShape, moduleShapeName(s.moduleShape()));
    m_moduleSize->setValue(qRound(s.moduleSize() * 100));
    m_roundness->setValue(qRound(s.moduleRoundness() * 100));
    const Paint p = s.modulePaint();
    const QGradientStops stops = p.stops();
    switch (p.type()) {
    case PaintType::Linear:
    case PaintType::Radial:
        m_fillType->setCurrentIndex(p.type() == PaintType::Linear ? 1 : 2);
        if (!stops.isEmpty()) {
            m_color1->setColor(stops.first().second);
            m_color2->setColor(stops.last().second);
        }
        m_angle->setValue(int(std::fmod(std::fmod(p.angle(), 360.0) + 360.0, 360.0)));
        break;
    default:
        m_fillType->setCurrentIndex(0);
        m_color1->setColor(p.color());
        break;
    }
    setCombo(m_eyeFrame, eyeShapeName(s.eyeFrame()));
    setCombo(m_eyeBall, eyeShapeName(s.eyeBall()));
    const Paint eye = s.eyeFramePaint().isNone() ? s.eyeBallPaint() : s.eyeFramePaint();
    m_eyeColorOn->setChecked(!eye.isNone());
    if (!eye.isNone())
        m_eyeColor->setColor(eye.color());
    if (s.alignmentFrame() == EyeShape::None)
        m_alignment->setCurrentIndex(0);
    else
        setCombo(m_alignment, eyeShapeName(s.alignmentFrame()));

    const Paint bg = s.background();
    m_transparent->setChecked(bg.isNone() || bg.color().alpha() == 0);
    if (!bg.isNone())
        m_background->setColor(bg.color());
    m_radius->setValue(qRound(s.backgroundRadius() * 2));
    m_borderWidth->setValue(s.borderWidth());
    m_borderColor->setColor(s.borderColor());

    if (s.hasLogo()) {
        m_logoSize->setValue(qRound(s.logoSize() * 100));
        setCombo(m_logoShape, logoShapeName(s.logoShape()));
        m_logoMargin->setValue(s.logoMargin());
        m_excavate->setChecked(s.logoExcavate());
        m_logoBgOn->setChecked(s.logoBackground().alpha() > 0);
        if (s.logoBackground().alpha() > 0)
            m_logoBg->setColor(s.logoBackground());
        m_logoHref = s.logoHref();
        m_logo = Renderer::loadImage(m_logoHref);
        m_logoPath->setText(m_logo.isNull() ? QString() : tr("(from JSON)"));
    }

    m_label->setText(s.labelText());
    const QString family = s.labelFont().section(QLatin1Char(','), 0, 0).trimmed().remove(QLatin1Char('"'));
    if (!family.isEmpty())
        m_font->setCurrentFont(QFont(family));
    m_labelPosition->setCurrentIndex(s.labelPosition() == LabelPosition::Top ? 1 : 0);
    m_labelSize->setValue(qRound(s.labelSize() * 10));
    m_bold->setChecked(s.labelBold());
    m_labelColor->setColor(s.labelColor());
    m_updating = false;
    updateEnabled();
    updatePreview();
}

void DesignerWindow::updatePreview()
{
    EncodeOptions options;
    if (m_ecc->currentIndex() > 0)
        options.ecc = Ecc(m_ecc->currentIndex() - 1);
    m_preview->setEncodeOptions(options);
    m_preview->setCodeStyle(styleFromControls());
    m_preview->setLogo(m_logo);
    m_preview->setText(m_text->toPlainText());
    updateStatus();
}

void DesignerWindow::updateStatus()
{
    const Renderer &r = m_preview->renderer();
    if (!r.isValid()) {
        m_statusLabel->setText(tr("Error: %1").arg(m_preview->errorString()));
        return;
    }
    const Code &code = r.code();
    QString text = tr("Version %1 (%2x%2), ECC %3, mask %4")
                       .arg(code.version())
                       .arg(code.size())
                       .arg(eccName(code.ecc()))
                       .arg(code.mask());
    if (r.style().hasLogo())
        text += tr(", logo damage %1").arg(r.logoDamage(), 0, 'f', 2);
    const QStringList warnings = r.lint();
    text += warnings.isEmpty() ? tr("  |  no warnings") : tr("  |  warnings: %1").arg(warnings.join(QStringLiteral(", ")));
    m_statusLabel->setText(text);
}

bool DesignerWindow::loadLogo(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    const QImage image = QImage::fromData(bytes);
    if (image.isNull())
        return false;
    const QString mime = QMimeDatabase().mimeTypeForFileNameAndData(path, bytes).name();
    m_logo = image;
    /* embedded so that exported SVG and JSON carry the logo */
    m_logoHref = QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(bytes.toBase64()));
    m_logoPath->setText(QFileInfo(path).fileName());
    updateEnabled();
    updatePreview();
    return true;
}

void DesignerWindow::chooseLogo()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose a logo"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg)"));
    if (!path.isEmpty() && !loadLogo(path))
        QMessageBox::warning(this, tr("Logo"), tr("Cannot load %1").arg(path));
}

void DesignerWindow::clearLogo()
{
    m_logo = QImage();
    m_logoHref.clear();
    m_logoPath->clear();
    updateEnabled();
    updatePreview();
}

void DesignerWindow::exportPng()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PNG"), QStringLiteral("qrcode.png"),
                                                      tr("PNG images (*.png)"));
    if (path.isEmpty())
        return;
    bool ok = false;
    const int width = QInputDialog::getInt(this, tr("Export PNG"), tr("Width in pixels"), 1024, 64, 8192, 64, &ok);
    if (ok && !m_preview->toImage(width).save(path, "PNG"))
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(path));
}

void DesignerWindow::exportSvg()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export SVG"), QStringLiteral("qrcode.svg"),
                                                      tr("SVG images (*.svg)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(m_preview->renderer().toSvg()) < 0)
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(path));
}

void DesignerWindow::exportJson()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export JSON"), QStringLiteral("style.json"),
                                                      tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QJsonObject doc;
    doc.insert(QStringLiteral("text"), m_text->toPlainText());
    const EncodeOptions options = m_preview->encodeOptions();
    if (options.ecc)
        doc.insert(QStringLiteral("encode"), QJsonObject{{QStringLiteral("ecc"), eccName(*options.ecc)}});
    doc.insert(QStringLiteral("style"), m_preview->codeStyle().toJson());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(doc).toJson()) < 0)
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(path));
}

bool DesignerWindow::importJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray json = f.readAll();
    bool ok = false;
    QString error;
    const Style style = Style::fromJson(json, &ok, &error);
    if (!ok) {
        QMessageBox::warning(this, tr("Import"), tr("%1: %2").arg(path, error));
        return false;
    }
    const QJsonObject doc = QJsonDocument::fromJson(json).object();
    if (doc.value(QStringLiteral("text")).isString()) {
        m_updating = true;
        m_text->setPlainText(doc.value(QStringLiteral("text")).toString());
        m_updating = false;
    }
    m_logo = QImage();
    m_logoHref.clear();
    m_logoPath->clear();
    setControls(style, EncodeOptions::fromJson(json));
    return true;
}

void DesignerWindow::importJsonDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import JSON"), QString(), tr("JSON (*.json)"));
    if (!path.isEmpty())
        importJson(path);
}
