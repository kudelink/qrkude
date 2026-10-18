# QRKude - ESP-IDF component "qrkude"
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
# In the QRKude monorepo the C core is compiled in place from ../../core and
# the LVGL widget from ../lvgl. The assembled component
# (packages/arduino/tools/package.py -> dist/esp-idf/qrkude) ships them in
# src/, include/ and lvgl/.

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/qrk_encode.c")
    set(qrkude_core "${CMAKE_CURRENT_LIST_DIR}")
    set(qrkude_lvgl "${CMAKE_CURRENT_LIST_DIR}/lvgl")
else()
    get_filename_component(qrkude_core "${CMAKE_CURRENT_LIST_DIR}/../../core" ABSOLUTE)
    get_filename_component(qrkude_lvgl "${CMAKE_CURRENT_LIST_DIR}/../lvgl" ABSOLUTE)
endif()

set(srcs
    "${qrkude_core}/src/qrk_encode.c"
    "${qrkude_core}/src/qrk_layout.c"
    "${qrkude_core}/src/qrk_svg.c"
    "${qrkude_core}/src/qrk_raster.c"
    "${qrkude_core}/src/qrk_json.c"
    "${qrkude_core}/src/qrk_payload.c"
    "${qrkude_core}/src/qrk_lint.c")
set(includes "${qrkude_core}/include")
set(requires "")

# The lv_qrkude widget is built when the project has an LVGL component
# ("lvgl", or "lvgl__lvgl" from the component registry). Requirements are
# collected before sdkconfig exists, so this is a detection over the known
# components (which works in both CMake passes) instead of a Kconfig switch.
idf_build_get_property(component_targets __COMPONENT_TARGETS)
foreach(target ${component_targets})
    __component_get_property(name ${target} COMPONENT_NAME)
    if(name STREQUAL "lvgl__lvgl" OR name STREQUAL "lvgl")
        list(APPEND srcs "${qrkude_lvgl}/lv_qrkude.c")
        list(APPEND includes "${qrkude_lvgl}")
        list(APPEND requires ${name})
        break()
    endif()
endforeach()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${includes}
                       REQUIRES ${requires})

# QRK_MAX_VERSION changes the size of qrk_code: it must be the same for the
# library and its users, hence PUBLIC.
if(NOT DEFINED CONFIG_QRKUDE_MAX_VERSION)
    set(CONFIG_QRKUDE_MAX_VERSION 40)
endif()
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    QRK_MAX_VERSION=${CONFIG_QRKUDE_MAX_VERSION}
    LV_QRKUDE_MAX_VERSION=${CONFIG_QRKUDE_MAX_VERSION})
if(CONFIG_QRKUDE_NO_MALLOC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE QRK_NO_MALLOC)
endif()
if(CONFIG_QRKUDE_OPTIMIZE_SPEED)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
endif()
