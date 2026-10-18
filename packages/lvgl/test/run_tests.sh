#!/bin/sh
# Builds every environment of platformio.ini, runs it and decodes the
# rendered screens with zxing-cpp.
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
# usage: run_tests.sh [OUT_DIR] [PYTHON]
#   PLATFORMIO_WORKSPACE_DIR=/tmp/ws keeps .pio out of the source tree.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/out}
PYTHON=${2:-python3}
WS=${PLATFORMIO_WORKSPACE_DIR:-$HERE/.pio}
ENVS=$(sed -n 's/^\[env:\(.*\)\]$/\1/p' "$HERE/platformio.ini")
status=0
for env in $ENVS; do
    echo "=== $env"
    if ! pio run -d "$HERE" -e "$env" > "$OUT.$env.log" 2>&1; then
        echo "build failed, see $OUT.$env.log"
        status=1
        continue
    fi
    mkdir -p "$OUT/$env"
    "$WS/build/$env/program" "$OUT/$env" || status=1
    "$PYTHON" "$HERE/../../arduino/test/native/check_decode.py" "$OUT/$env" || status=1
done
exit $status
