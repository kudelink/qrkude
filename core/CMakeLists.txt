# QRKude core (C99 reference implementation)
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(qrkude VERSION 0.1.0 LANGUAGES C)

option(QRKUDE_BUILD_TOOLS "Build the qrkude command line tools" ${PROJECT_IS_TOP_LEVEL})
option(QRKUDE_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
set(QRKUDE_MAX_VERSION 40 CACHE STRING "Largest QR version supported (1-40)")

add_library(qrkude
    src/qrk_encode.c
    src/qrk_layout.c
    src/qrk_svg.c
    src/qrk_raster.c
    src/qrk_json.c
    src/qrk_payload.c
    src/qrk_lint.c)
add_library(qrkude::qrkude ALIAS qrkude)
target_include_directories(qrkude PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(qrkude PUBLIC c_std_99)
target_compile_definitions(qrkude PUBLIC QRK_MAX_VERSION=${QRKUDE_MAX_VERSION})
if(NOT MSVC)
    target_link_libraries(qrkude PUBLIC m)
endif()
set_target_properties(qrkude PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(QRKUDE_BUILD_TOOLS)
    add_executable(qrkude_cli tools/qrkude_cli.c)
    target_link_libraries(qrkude_cli PRIVATE qrkude)
    set_target_properties(qrkude_cli PROPERTIES OUTPUT_NAME qrkude)
    add_executable(qrkude_payload tools/payload_cli.c)
    target_link_libraries(qrkude_payload PRIVATE qrkude)
    set_target_properties(qrkude_payload PROPERTIES OUTPUT_NAME qrkude-payload)
endif()

if(QRKUDE_BUILD_TESTS)
    enable_testing()
    add_executable(test_core tests/test_core.c)
    target_link_libraries(test_core PRIVATE qrkude)
    add_test(NAME qrkude_core COMMAND test_core)
endif()

include(GNUInstallDirs)
install(TARGETS qrkude EXPORT qrkudeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/qrkude.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT qrkudeTargets NAMESPACE qrkude:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qrkude)
