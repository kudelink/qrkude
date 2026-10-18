<!--
Copyright 2026 The QRKude Authors
SPDX-License-Identifier: Apache-2.0
-->

# Changelog

## 0.1.0

- First release: pure Dart port of the QRKude C reference implementation
  (spec v0.1).
- QR Code encoder (versions 1-40, numeric/alphanumeric/byte, ECI, ECC boost,
  automatic or forced mask).
- Style model with JSON reader/writer (`QRKudeDocument`, `QRKudeStyle`), the
  shape model and path emitter (`emitLayer`, `QRKudePathSink`), the SVG
  writer, `lint`, `logoDamage` and payload builders.
- Byte-identical with all conformance vectors on the Dart VM and in
  JavaScript.
- Hardened for untrusted styles (SPEC 2.0/2.2/8.3): sanitized `svg.idPrefix`,
  JSON limits (number tokens, non-finite numbers, nesting depth), merge rules
  for repeated keys, clamped lengths and integer fields, `fmt` guard for NaN
  and huge values, `geo` coordinate clamping.
- `lint`: contrast over the real background colors and the new `invisible`
  flag; `layerVisible`, `encodeForStyle` and `sanitizeIdPrefix` helpers.
- The internal geometry engine moved to `lib/src/engine.dart`; the public
  API is unchanged.
- Documentation: rewritten README (installation from the repository, styling,
  output, scannability, untrusted styles, complete API reference) and
  dartdoc comments on every public symbol (`public_member_api_docs` enabled).
