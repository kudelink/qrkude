# QRKude Qt - qmake include file (Qt 5.12+ and Qt 6)
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
# In your .pro file:
#
#     include(path/to/packages/qt/qrkude.pri)
#
# This compiles the C core and the Qt classes into your target.
# Options, set them before the include:
#
#     CONFIG += qrkude_no_quick     never build QRKudeItem (even with Qt Quick)
#     QRKUDE_CORE_DIR = /path/core  location of the C core (default ../../core)

isEmpty(QRKUDE_CORE_DIR): QRKUDE_CORE_DIR = $$clean_path($$PWD/../../core)

QT += core gui widgets
CONFIG += c++17

INCLUDEPATH += $$PWD/src $$QRKUDE_CORE_DIR/include
DEPENDPATH += $$PWD/src $$QRKUDE_CORE_DIR/include

HEADERS += \
    $$PWD/src/qrkudeglobal.h \
    $$PWD/src/qrkudestyle.h \
    $$PWD/src/qrkudecode.h \
    $$PWD/src/qrkuderenderer.h \
    $$PWD/src/qrkudewidget.h \
    $$PWD/src/qrkudeqt.h \
    $$QRKUDE_CORE_DIR/include/qrkude.h

SOURCES += \
    $$PWD/src/qrkudestyle.cpp \
    $$PWD/src/qrkudecode.cpp \
    $$PWD/src/qrkuderenderer.cpp \
    $$PWD/src/qrkudewidget.cpp

# The C core (C99).
SOURCES += \
    $$QRKUDE_CORE_DIR/src/qrk_encode.c \
    $$QRKUDE_CORE_DIR/src/qrk_layout.c \
    $$QRKUDE_CORE_DIR/src/qrk_svg.c \
    $$QRKUDE_CORE_DIR/src/qrk_raster.c \
    $$QRKUDE_CORE_DIR/src/qrk_json.c \
    $$QRKUDE_CORE_DIR/src/qrk_payload.c \
    $$QRKUDE_CORE_DIR/src/qrk_lint.c
HEADERS += $$QRKUDE_CORE_DIR/src/qrk_internal.h
!msvc: QMAKE_CFLAGS += -std=c99
unix: LIBS += -lm

# Qt Quick item, only when the module is available.
!qrkude_no_quick:qtHaveModule(quick) {
    QT += qml quick
    DEFINES += QRKUDE_HAS_QUICK=1
    HEADERS += $$PWD/src/qrkudeitem.h
    SOURCES += $$PWD/src/qrkudeitem.cpp
}
