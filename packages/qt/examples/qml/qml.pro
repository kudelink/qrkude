# QRKude Qt - QML example (qmake, needs Qt Quick)
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
TEMPLATE = app
TARGET = qmldemo
QT += quick
CONFIG -= app_bundle

include(../../qrkude.pri)

SOURCES += main.cpp
RESOURCES += qml.qrc
