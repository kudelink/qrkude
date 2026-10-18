#!/bin/sh
# Compiles the Arduino examples of the assembled library (dist/arduino/QRKude)
# with PlatformIO for several boards and prints the flash / RAM usage.
#
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
# usage: ci.sh [BUILD_DIR] [JOB...]     (default: all jobs, BUILD_DIR=/tmp/qrkude-ci)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:-/tmp/qrkude-ci}
[ $# -gt 0 ] && shift
JOBS=${*:-"basic_esp32 webserver_esp32 tft_esp32 ssd1306_esp32 logo_esp32 lvgl9_esp32 lvgl8_esp32 basic_uno basic_uno_v3_nomalloc basic_mega ssd1306_mega basic_esp8266 webserver_esp8266 basic_samd ssd1306_samd basic_rp2040 tft_rp2040"}

python3 "$ROOT/packages/arduino/tools/package.py" --only arduino || exit 1
LIB="$ROOT/dist/arduino/QRKude"
EX="$LIB/examples"

# TFT_eSPI configured with build flags instead of User_Setup.h (ILI9341 on VSPI).
TFT="-DUSER_SETUP_LOADED=1 -DILI9341_DRIVER=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=320 \
-DTFT_MISO=19 -DTFT_MOSI=23 -DTFT_SCLK=18 -DTFT_CS=15 -DTFT_DC=2 -DTFT_RST=4 \
-DLOAD_GLCD=1 -DSPI_FREQUENCY=40000000"
LVGL="-DLV_CONF_INCLUDE_SIMPLE -I$EX/LVGL_Widget"
# avr-libc's <locale.h> is a stub without struct lconv (used by qrk_json.c).
AVR="" # extra AVR flags (QRK_NO_LOCALE is now automatic on __AVR__)

job() { # name board example [pio ci options...]
    name=$1 board=$2 example=$3
    shift 3
    printf '%-24s ' "$name"
    rm -rf "${OUT:?}/$name" # pio ci --keep-build-dir would keep stale sources
    if pio ci "$EX/$example" --lib "$LIB" --board "$board" --build-dir "$OUT/$name" \
        --keep-build-dir "$@" > "$OUT/$name.log" 2>&1; then
        grep -E "^(RAM|Flash):" "$OUT/$name.log" | sed 's/\[=* *\]//' | tr -s ' ' | tr '\n' ' '
        echo
    else
        echo "FAILED (see $OUT/$name.log)"
        status=1
    fi
}

mkdir -p "$OUT"
status=0
for j in $JOBS; do
    case $j in
    basic_esp32) job $j esp32dev BasicSerial ;;
    webserver_esp32) job $j esp32dev ESP32_WebServer_SVG ;;
    tft_esp32) job $j esp32dev TFT_eSPI_Styled -O "lib_deps=bodmer/TFT_eSPI@^2.5.43" -O "build_flags=$TFT" ;;
    ssd1306_esp32) job $j esp32dev Adafruit_SSD1306_Mono \
        -O "lib_deps=adafruit/Adafruit SSD1306@^2.5.7, adafruit/Adafruit GFX Library@^1.11.9" ;;
    logo_esp32) job $j esp32dev LogoFromFlash -O "lib_deps=bodmer/TFT_eSPI@^2.5.43" -O "build_flags=$TFT" ;;
    lvgl9_esp32) job $j esp32dev LVGL_Widget -O "lib_deps=bodmer/TFT_eSPI@^2.5.43, lvgl/lvgl@^9.2.2" \
        -O "build_flags=$TFT $LVGL" ;;
    lvgl8_esp32) job $j esp32dev LVGL_Widget -O "lib_deps=bodmer/TFT_eSPI@^2.5.43, lvgl/lvgl@~8.3.11" \
        -O "build_flags=$TFT $LVGL" ;;
    basic_uno) job $j uno BasicSerial -O "build_flags=$AVR" ;;
    basic_uno_v3_nomalloc) job $j uno BasicSerial -O "build_flags=$AVR -DQRK_MAX_VERSION=3 -DQRK_NO_MALLOC" ;;
    basic_mega) job $j megaatmega2560 BasicSerial -O "build_flags=$AVR" ;;
    ssd1306_mega) job $j megaatmega2560 Adafruit_SSD1306_Mono -O "build_flags=$AVR" \
        -O "lib_deps=adafruit/Adafruit SSD1306@^2.5.7, adafruit/Adafruit GFX Library@^1.11.9" ;;
    basic_esp8266) job $j d1_mini BasicSerial ;;
    webserver_esp8266) job $j d1_mini ESP32_WebServer_SVG ;;
    basic_samd) job $j zeroUSB BasicSerial ;;
    ssd1306_samd) job $j zeroUSB Adafruit_SSD1306_Mono \
        -O "lib_deps=adafruit/Adafruit SSD1306@^2.5.7, adafruit/Adafruit GFX Library@^1.11.9" ;;
    basic_rp2040) job $j pico BasicSerial ;;
    tft_rp2040) job $j pico TFT_eSPI_Styled -O "lib_deps=bodmer/TFT_eSPI@^2.5.43" \
        -O "build_flags=-DUSER_SETUP_LOADED=1 -DST7789_DRIVER=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=240 -DTFT_MOSI=19 -DTFT_SCLK=18 -DTFT_CS=17 -DTFT_DC=16 -DTFT_RST=20 -DLOAD_GLCD=1 -DSPI_FREQUENCY=40000000" ;;
    *) echo "unknown job $j"; status=1 ;;
    esac
done
exit $status
