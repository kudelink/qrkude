# QRKude Qt - designer example
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
add_executable(qrkude_designer main.cpp designerwindow.h designerwindow.cpp)
target_link_libraries(qrkude_designer PRIVATE QRKude::Qt)
set_target_properties(qrkude_designer PROPERTIES AUTOMOC ON OUTPUT_NAME designer)
