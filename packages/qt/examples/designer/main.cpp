/*
 * QRKude Qt - designer example.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 *   designer [--load style.json] [--logo image.png] [--screenshot out.png]
 *
 * --screenshot renders the window into a PNG and exits (useful headless,
 * with QT_QPA_PLATFORM=offscreen).
 */
#include <QtCore/QCommandLineParser>
#include <QtWidgets/QApplication>

#include "designerwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QRKude Designer"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption load(QStringLiteral("load"), QStringLiteral("Style JSON to load."),
                                  QStringLiteral("file"));
    const QCommandLineOption logo(QStringLiteral("logo"), QStringLiteral("Logo image."), QStringLiteral("file"));
    const QCommandLineOption shot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save a screenshot of the window and quit."),
                                  QStringLiteral("file"));
    parser.addOptions({load, logo, shot});
    parser.process(app);

    DesignerWindow window;
    if (parser.isSet(load) && !window.importJson(parser.value(load)))
        return 1;
    if (parser.isSet(logo) && !window.loadLogo(parser.value(logo)))
        return 1;
    window.show();
    if (parser.isSet(shot)) {
        QApplication::processEvents();
        return window.grab().save(parser.value(shot)) ? 0 : 1;
    }
    return app.exec();
}
