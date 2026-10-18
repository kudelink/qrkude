# QRKude Qt - designer example (qmake)
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
TEMPLATE = app
TARGET = designer
CONFIG += qrkude_no_quick
CONFIG -= app_bundle

include(../../qrkude.pri)

HEADERS += designerwindow.h
SOURCES += main.cpp designerwindow.cpp
