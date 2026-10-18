# QRKude Qt - Qt 5 / Qt 6 bindings for the QRKude C core
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(QRKudeQt VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(QRKUDE_QT_TOP_LEVEL ON)
else()
    set(QRKUDE_QT_TOP_LEVEL OFF)
endif()

option(QRKUDE_QT_BUILD_QML "Build the Qt Quick item QRKudeItem (skipped when Qt Quick is missing)" ON)
option(QRKUDE_QT_BUILD_EXAMPLES "Build the examples" ${QRKUDE_QT_TOP_LEVEL})
option(QRKUDE_QT_BUILD_TESTS "Build the tests" ${QRKUDE_QT_TOP_LEVEL})
set(QRKUDE_QT_MAJOR "" CACHE STRING "Qt major version to use (5 or 6); empty = Qt 6 if found, else Qt 5")
set(QRKUDE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../core" CACHE PATH "Directory of the QRKude C core")

# ---- Qt -----------------------------------------------------------------------
if(QRKUDE_QT_MAJOR)
    find_package(QT NAMES Qt${QRKUDE_QT_MAJOR} REQUIRED COMPONENTS Core)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
endif()
if(QT_VERSION_MAJOR EQUAL 5)
    find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets)
else()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
endif()
message(STATUS "QRKude Qt: using Qt ${Qt${QT_VERSION_MAJOR}_VERSION}")

# ---- C core -------------------------------------------------------------------
if(NOT TARGET qrkude::qrkude)
    if(NOT EXISTS "${QRKUDE_CORE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "QRKude core not found in ${QRKUDE_CORE_DIR} (set QRKUDE_CORE_DIR)")
    endif()
    add_subdirectory("${QRKUDE_CORE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/qrkude-core" EXCLUDE_FROM_ALL)
endif()

# ---- QRKude::Qt ---------------------------------------------------------------
add_library(qrkude_qt STATIC
    src/qrkudeglobal.h
    src/qrkudestyle.h
    src/qrkudestyle.cpp
    src/qrkudecode.h
    src/qrkudecode.cpp
    src/qrkuderenderer.h
    src/qrkuderenderer.cpp
    src/qrkudewidget.h
    src/qrkudewidget.cpp
    src/qrkudeqt.h)
add_library(QRKude::Qt ALIAS qrkude_qt)
target_include_directories(qrkude_qt PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_link_libraries(qrkude_qt
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
    PRIVATE
        qrkude::qrkude)
# The public headers include qrkude.h, so consumers need the core's include
# path and QRK_MAX_VERSION (it sizes qrk_code). Its C language requirement
# (c_std_99) is not passed on, so C++-only projects can link QRKude::Qt.
target_include_directories(qrkude_qt PUBLIC
    $<TARGET_PROPERTY:qrkude::qrkude,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(qrkude_qt PUBLIC
    $<TARGET_PROPERTY:qrkude::qrkude,INTERFACE_COMPILE_DEFINITIONS>)
target_compile_features(qrkude_qt PUBLIC cxx_std_17)
set_target_properties(qrkude_qt PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(qrkude_qt PRIVATE /W4)
else()
    target_compile_options(qrkude_qt PRIVATE -Wall -Wextra)
endif()

# ---- QRKude::Quick (optional) -------------------------------------------------
set(QRKUDE_QT_HAS_QUICK OFF)
if(QRKUDE_QT_BUILD_QML)
    find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Qml Quick)
    if(TARGET Qt${QT_VERSION_MAJOR}::Quick AND TARGET Qt${QT_VERSION_MAJOR}::Qml)
        set(QRKUDE_QT_HAS_QUICK ON)
        add_library(qrkude_qt_quick STATIC src/qrkudeitem.h src/qrkudeitem.cpp)
        add_library(QRKude::Quick ALIAS qrkude_qt_quick)
        target_link_libraries(qrkude_qt_quick PUBLIC
            qrkude_qt Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick)
        target_compile_definitions(qrkude_qt_quick PUBLIC QRKUDE_HAS_QUICK=1)
        set_target_properties(qrkude_qt_quick PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
    else()
        message(STATUS "QRKude Qt: Qt Quick not found, QRKudeItem (QRKude::Quick) is not built")
    endif()
endif()

# ---- examples and tests -------------------------------------------------------
if(QRKUDE_QT_BUILD_EXAMPLES)
    add_subdirectory(examples/designer)
    if(QRKUDE_QT_HAS_QUICK)
        add_subdirectory(examples/qml)
    endif()
endif()

if(QRKUDE_QT_BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
