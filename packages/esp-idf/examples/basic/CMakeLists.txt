# QRKude ESP-IDF example
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)

# Inside the QRKude monorepo the component lives in packages/esp-idf, and ESP-IDF
# names components after their directory: expose it as "qrkude" through a
# two-line shim. The packaged example gets the component through
# main/idf_component.yml instead (written by packages/arduino/tools/package.py).
get_filename_component(qrkude_dir "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.yml"
   AND EXISTS "${qrkude_dir}/../../core/include/qrkude.h")
    set(qrkude_shim "${CMAKE_BINARY_DIR}/qrkude_shim/qrkude")
    file(WRITE "${qrkude_shim}/CMakeLists.txt" "include(\"${qrkude_dir}/CMakeLists.txt\")\n")
    file(WRITE "${qrkude_shim}/Kconfig" "source \"${qrkude_dir}/Kconfig\"\n")
    list(APPEND EXTRA_COMPONENT_DIRS "${qrkude_shim}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(qrkude_basic)
