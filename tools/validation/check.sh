#!/bin/sh
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")/../.."
export PLATFORMIO_RUN_JOBS=${QRKUDE_TEST_JOBS:-4}

case "${1:-core}" in
core)
    make -C core all test
    gcc -std=c99 -g -O1 -fsanitize=address,undefined,float-cast-overflow \
        -fno-sanitize-recover=all -Icore/include core/src/*.c core/tests/test_core.c \
        -lm -o /tmp/test_core_san
    /tmp/test_core_san
    gcc -m32 -std=c99 -g -O1 -fsanitize=undefined,float-cast-overflow \
        -fno-sanitize-recover=all -Icore/include core/src/*.c core/tests/test_core.c \
        -lm -o /tmp/test_core_32
    /tmp/test_core_32
    gcc -m32 -std=c99 -O1 -Icore/include core/src/*.c core/tools/qrkude_cli.c \
        -lm -o /tmp/qrkude_32
    gcc -m32 -std=c99 -O1 -Icore/include core/src/*.c core/tools/payload_cli.c \
        -lm -o /tmp/qrkude_payload_32
    python spec/tools/gen_vectors.py --cli /tmp/qrkude_32 \
        --payload-cli /tmp/qrkude_payload_32 --out /tmp/qrkude-vectors-32
    diff -qr spec/vectors /tmp/qrkude-vectors-32
    sha256sum spec/vectors/*.json > /tmp/qrkude-vectors.sha256
    python spec/tools/gen_vectors.py
    sha256sum -c /tmp/qrkude-vectors.sha256
    python spec/tools/check_decode.py
    ;;
qt)
    cmake -S packages/qt -B build/qt -G Ninja -DQRKUDE_TEST_PYTHON="$(command -v python)"
    cmake --build build/qt -j 4
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        ctest --test-dir build/qt --output-on-failure
    ;;
lvgl)
    PLATFORMIO_WORKSPACE_DIR=/tmp/qrkude-lvgl-ws \
        sh packages/lvgl/test/run_tests.sh /tmp/qrkude-lvgl-out "$(command -v python)"
    ;;
embedded)
    make -C packages/arduino/test/native check BUILD=/tmp/qrkude-native PYTHON="$(command -v python)" \
        CFLAGS='-O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all' \
        CXXFLAGS='-O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all'
    sh packages/arduino/test/ci.sh /tmp/qrkude-boards
    ;;
*)
    echo "usage: sh tools/validation/check.sh [core|qt|lvgl|embedded]" >&2
    exit 2
    ;;
esac
