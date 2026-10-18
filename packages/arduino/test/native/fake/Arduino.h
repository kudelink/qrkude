/*
 * Minimal Arduino API stand-in for host tests of the QRKude wrapper.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef QRKUDE_FAKE_ARDUINO_H
#define QRKUDE_FAKE_ARDUINO_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        size_t n = 0;
        while (size--)
            n += write(*buffer++);
        return n;
    }
    size_t print(const char *s) { return write(reinterpret_cast<const uint8_t *>(s), strlen(s)); }
};

class String {
public:
    String(const char *s = "") : s_(s) {}
    const char *c_str() const { return s_.c_str(); }
    unsigned int length() const { return (unsigned int)s_.size(); }

private:
    std::string s_;
};

#endif
