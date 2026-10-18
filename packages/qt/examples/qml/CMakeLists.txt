# QRKude Qt - QML example (built only when Qt Quick is available)
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
add_executable(qrkude_qml main.cpp qml.qrc)
target_link_libraries(qrkude_qml PRIVATE QRKude::Quick)
set_target_properties(qrkude_qml PROPERTIES AUTOMOC ON AUTORCC ON OUTPUT_NAME qmldemo)
