# QRKude core - plain Makefile (CMakeLists.txt is also provided)
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Iinclude
LDLIBS  += -lm

SRC  := src/qrk_encode.c src/qrk_layout.c src/qrk_svg.c src/qrk_raster.c src/qrk_json.c src/qrk_payload.c src/qrk_lint.c
OBJ  := $(SRC:src/%.c=build/%.o)

all: build/libqrkude.a build/qrkude build/qrkude-payload build/test_core

build/%.o: src/%.c include/qrkude.h src/qrk_internal.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/libqrkude.a: $(OBJ)
	$(AR) rcs $@ $^

build/qrkude: tools/qrkude_cli.c build/libqrkude.a
	$(CC) $(CFLAGS) -Wno-conversion $< build/libqrkude.a $(LDLIBS) -o $@

build/qrkude-payload: tools/payload_cli.c build/libqrkude.a
	$(CC) $(CFLAGS) -Wno-conversion $< build/libqrkude.a $(LDLIBS) -o $@

build/test_core: tests/test_core.c build/libqrkude.a
	$(CC) $(CFLAGS) -Wno-conversion $< build/libqrkude.a $(LDLIBS) -o $@

build:
	mkdir -p build

test: build/test_core
	./build/test_core

clean:
	rm -rf build

.PHONY: all test clean
