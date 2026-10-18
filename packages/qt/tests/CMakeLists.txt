# QRKude Qt - tests
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
set(QRKUDE_SPEC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../spec" CACHE PATH "QRKude spec/ directory (vectors and cases)")
set(QRKUDE_TEST_PYTHON "" CACHE FILEPATH "Python with zxingcpp, cairosvg, pillow and numpy (enables the decode test)")

add_executable(tst_qrkude tst_qrkude.cpp)
target_link_libraries(tst_qrkude PRIVATE QRKude::Qt Qt${QT_VERSION_MAJOR}::Test)
if(TARGET QRKude::Quick)
    # also tests QRKudeItem (QRKUDE_HAS_QUICK comes with the target)
    target_link_libraries(tst_qrkude PRIVATE QRKude::Quick)
endif()
target_compile_definitions(tst_qrkude PRIVATE
    QRKUDE_SPEC_DIR="${QRKUDE_SPEC_DIR}"
    QRKUDE_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(tst_qrkude PROPERTIES AUTOMOC ON)

add_test(NAME qrkude_qt COMMAND tst_qrkude)
set_tests_properties(qrkude_qt PROPERTIES
    TIMEOUT 600
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QRKUDE_PYTHON=${QRKUDE_TEST_PYTHON};QRKUDE_TEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/output")
