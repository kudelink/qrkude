# Host test of the QRKude Arduino wrapper (fake Arduino.h + fake displays).
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
#   make            build and run, images + manifest in $(OUT)
#   make check      also decode the images with zxing-cpp (check_decode.py)
#
# Uses the core sources of the monorepo in place (../../../../core).

ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../..)
CORE    := $(ROOT)/core
PKG     := $(ROOT)/packages/arduino
BUILD   ?= build
OUT     ?= $(BUILD)/out
PYTHON  ?= python3
CC      ?= cc
CXX     ?= c++
WARN    := -Wall -Wextra -Werror
CFLAGS  ?= -O2 -g
CXXFLAGS ?= -O2 -g
DEFS    := -DARDUINO=10819 $(EXTRA_DEFS)

CORE_SRC := $(wildcard $(CORE)/src/*.c)
CORE_OBJ := $(patsubst $(CORE)/src/%.c,$(BUILD)/core/%.o,$(CORE_SRC))

all: run

$(BUILD)/core/%.o: $(CORE)/src/%.c $(CORE)/include/qrkude.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c99 $(DEFS) -I$(CORE)/include -c $< -o $@

$(BUILD)/QRKude.o: $(PKG)/src/QRKude.cpp $(PKG)/src/QRKude.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -std=c++11 $(WARN) -Wshadow -Wconversion -Wno-sign-conversion $(DEFS) -I$(PKG)/test/native/fake -I$(PKG)/src -c $< -o $@

$(BUILD)/test_native: $(PKG)/test/native/test_native.cpp $(BUILD)/QRKude.o $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -std=c++11 $(WARN) $(DEFS) -I$(PKG)/test/native/fake -I$(PKG)/src $^ -lm -o $@

run: $(BUILD)/test_native
	@mkdir -p $(OUT)
	$(BUILD)/test_native $(OUT)

# tools/img2qrk.py: one header per pixel format, rendered as a logo.
LOGO_FORMATS := RGB565 RGB565_SWAPPED RGBA8888 BGRA8888 RGB888 GRAY8 MONO
LOGO_PNG := $(PKG)/examples/LogoFromFlash/logo.png

$(BUILD)/logos/logo_%.h: $(LOGO_PNG) $(PKG)/tools/img2qrk.py
	@mkdir -p $(dir $@)
	$(PYTHON) $(PKG)/tools/img2qrk.py $(LOGO_PNG) -n logo_$* -f $* -s 28 -o $@

$(BUILD)/test_img2qrk: $(PKG)/test/native/test_img2qrk.c $(CORE_OBJ) $(LOGO_FORMATS:%=$(BUILD)/logos/logo_%.h)
	$(CC) $(CFLAGS) -std=c99 $(WARN) -I$(CORE)/include -I$(BUILD)/logos $< $(CORE_OBJ) -lm -o $@

img2qrk: $(BUILD)/test_img2qrk
	@mkdir -p $(OUT)/img2qrk
	$(BUILD)/test_img2qrk $(OUT)/img2qrk

check: run img2qrk
	$(PYTHON) $(PKG)/test/native/check_decode.py $(OUT)
	$(PYTHON) $(PKG)/test/native/check_decode.py $(OUT)/img2qrk

clean:
	rm -rf $(BUILD)

.PHONY: all run img2qrk check clean
