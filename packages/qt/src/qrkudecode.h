/*
 * QRKude Qt - encoded QR code (module matrix).
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef QRKUDECODE_H
#define QRKUDECODE_H

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <memory>

#include "qrkudestyle.h"

namespace QRKude {

/*
 * The text turned into a QR code: a square grid of dark and light modules,
 * plus the version (size), error correction level and mask that were chosen.
 * Draw it with QRKude::Renderer. Cheap to copy (the grid is shared).
 *
 * Encoding needs the style only for one decision: when EncodeOptions::ecc
 * is not set, the error correction level is High if the style has a logo
 * and Medium otherwise (docs/GUIDE.md#error-correction).
 */
class Code
{
public:
    Code(); /* a null code: isNull() is true */

    /* Encodes the UTF-8 bytes of `text`. On failure the result is null and
     * errorString() says why. */
    static Code encode(const QString &text, const Style &style = Style(),
                       const EncodeOptions &options = EncodeOptions());
    static Code encode(const char *utf8, const Style &style = Style(),
                       const EncodeOptions &options = EncodeOptions());
    /* Encodes raw bytes as they are. */
    static Code encode(const QByteArray &data, const Style &style = Style(),
                       const EncodeOptions &options = EncodeOptions());

    bool isNull() const { return !m_code; }
    bool isValid() const { return bool(m_code); }
    /* Why encoding failed: "data too long" (the text does not fit in the
     * allowed versions) or "invalid argument" (EncodeOptions out of range).
     * Empty for a valid code. */
    QString errorString() const { return m_error; }

    int version() const;  /* 1..40, 0 when null */
    int size() const;     /* modules per side (4 * version + 17), 0 when null */
    Ecc ecc() const;      /* the level actually used (may be above the minimum) */
    int mask() const;     /* mask pattern 0..7, -1 when null */
    int mode() const;     /* QRK_MODE_NUMERIC, _ALPHANUMERIC or _BYTE; 0 when null */

    /* true for a dark module. (0, 0) is the top-left module; coordinates
     * outside 0..size()-1 return false. */
    bool isDark(int x, int y) const;

    /* The C struct (qrk_code) for the core API; nullptr when null. */
    const qrk_code *raw() const { return m_code.get(); }

    bool operator==(const Code &other) const;
    bool operator!=(const Code &other) const { return !(*this == other); }

private:
    std::shared_ptr<const qrk_code> m_code;
    QString m_error;
};

} // namespace QRKude

Q_DECLARE_METATYPE(QRKude::Code)

#endif // QRKUDECODE_H
