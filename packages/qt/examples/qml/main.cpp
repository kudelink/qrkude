/*
 * QRKude Qt - QML example.
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 *   qmldemo [--screenshot out.png]
 *
 * --screenshot grabs the window once it has rendered and exits (headless:
 * QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software).
 */
#include <QtCore/QCommandLineParser>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QScreen>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickWindow>

#include "qrkudeitem.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption shot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save a screenshot of the window and quit."),
                                  QStringLiteral("file"));
    parser.addOption(shot);
    parser.process(app);

    QRKude::registerQmlTypes(); /* import QRKude 1.0 */

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(shot)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QString path = parser.value(shot);
        /* retry until the scene graph has produced a frame (max ~10 s) */
        auto *timer = new QTimer(&app);
        int attempts = 0;
        QObject::connect(timer, &QTimer::timeout, &app, [window, path, timer, attempts]() mutable {
            QImage image = window ? window->grabWindow() : QImage();
            /* Qt 5 software backend on the offscreen platform cannot grab the
             * scene; read the platform's backing store instead */
            if (image.isNull() && window && window->screen())
                image = window->screen()->grabWindow(window->winId()).toImage();
            if (!image.isNull() || ++attempts >= 40) {
                timer->stop();
                const bool saved = !image.isNull() && image.save(path);
                if (!saved)
                    qWarning("screenshot failed (image %dx%d, window exposed: %d)", image.width(),
                             image.height(), window && window->isExposed());
                QCoreApplication::exit(saved ? 0 : 1);
            }
        });
        timer->start(250);
    }
    return app.exec();
}
