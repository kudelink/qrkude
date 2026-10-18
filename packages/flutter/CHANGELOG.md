<!--
Copyright 2026 The QRKude Authors
SPDX-License-Identifier: Apache-2.0
-->

# Changelog

## 0.1.0

- First release.
- `QRKudeView` widget: data, style (`QRKudeStyle`, `QRKudeDocument` or JSON),
  encode options, logo `ImageProvider`, size, semantics label, error builder.
- `QRKudePainter` (`CustomPainter`) with `toPicture`, `toImage` and
  `toPngBytes`; `qrkudeToPngBytes` convenience.
- `QRKudeGeometry`: cached Flutter paths and paints built from the shared
  QRKude shape model, with SVG-exact gradient geometry.
- `toPicture` / `toImage` / `toPngBytes` reject image sides outside
  1..16384 pixels (`ArgumentError`), so untrusted styles cannot request
  gigantic bitmaps.
- Documentation: rewritten README (installation from the repository, styling,
  output, scannability, untrusted styles, complete API reference) and
  dartdoc comments on every public symbol (`public_member_api_docs` enabled).
