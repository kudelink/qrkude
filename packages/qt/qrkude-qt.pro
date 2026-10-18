# QRKude Qt - examples and tests (qmake)
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
#   qmake qrkude-qt.pro && make
#   QT_QPA_PLATFORM=offscreen ./tests/tst_qrkude
#
# Applications use the library through include(qrkude.pri).
TEMPLATE = subdirs
SUBDIRS = tests examples/designer
qtHaveModule(quick): SUBDIRS += examples/qml
