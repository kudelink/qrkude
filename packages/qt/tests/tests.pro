# QRKude Qt - tests (qmake)
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
#   qmake && make && QT_QPA_PLATFORM=offscreen ./tst_qrkude
#   (set QRKUDE_PYTHON to enable the zxing-cpp / cairosvg checks)
TEMPLATE = app
TARGET = tst_qrkude
QT += testlib
CONFIG += console testcase   # QRKudeItem is tested too when Qt Quick exists
CONFIG -= app_bundle

include(../qrkude.pri)

SOURCES += tst_qrkude.cpp
DEFINES += QRKUDE_SPEC_DIR=\\\"$$clean_path($$PWD/../../../spec)\\\" \
           QRKUDE_TESTS_DIR=\\\"$$PWD\\\"
