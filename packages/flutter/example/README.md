<!--
Copyright 2026 The QRKude Authors
SPDX-License-Identifier: Apache-2.0
-->

# qrkude_flutter example

A small Flutter app that shows what [`qrkude_flutter`](..) can do: a gallery
of QR code styles and a live editor where you change a style and see the
result, its scannability warnings and its JSON. It is part of QRKude, a
family of libraries that draw the same styled QR codes on every platform.

<p>
  <img src="../../../docs/images/dots.svg" width="110" alt="Round dots">
  <img src="../../../docs/images/gradient.svg" width="110" alt="Gradient colors">
  <img src="../../../docs/images/logo.svg" width="110" alt="Code with a logo">
</p>

## Status and installation

The app is not published anywhere; run it from a copy of the QRKude
repository. It uses `qrkude_flutter` (and through it `qrkude`) with path
dependencies, so keep the repository folders as they are. You need Flutter
3.22 or newer (tested with 3.44).

```sh
cd packages/flutter/example
flutter pub get
flutter run -d chrome
```

Only the web platform folder is included. To run it on another platform,
add that platform first, for example:

```sh
flutter create --platforms=android,ios,linux,macos,windows .
```

## What it shows

New to QR codes? Read the [guide](../../../docs/GUIDE.md).

**Gallery** (first tab): twelve styles, each a `QRKudeView` fed with the
cross-language JSON form of a style (classic, dots, fluid with a gradient, liquid,
classy leaf eyes, radial gradient, bars, stars, dark background, frame with
a label, logo, styled alignment patterns). The JSON maps are at the top of
`lib/main.dart` (`galleryStyles`); copy any of them into your app.

**Editor** (second tab):

- a text field for the data to encode;
- drop-down menus for the module shape, the eye frame and the eye ball;
- sliders for the module size and roundness;
- color swatches for the modules, the eyes and the background, and a switch
  that turns the module color into a gradient;
- a logo switch (the logo is drawn at runtime, no asset needed);
- a label text field and its position (top or bottom);
- an "Export PNG" button that renders the code at 1024 pixels with
  `qrkudeToPngBytes` and reports the size.

Under the preview the app shows the symbol version, the error correction
level, the mask and the encoding mode, the `lint` warnings (for example
`low-contrast` when you pick light modules on a light background), and the
style as JSON, ready to paste into any QRKude library. All style options are
described in the [style reference](../../../docs/STYLES.md).

On wide windows the preview and the controls sit side by side; on phones
they are stacked.

## Code tour

Everything is in `lib/main.dart`:

| Part | What it does |
|---|---|
| `QRKudeExampleApp`, `HomePage` | Material app with two tabs. |
| `galleryStyles`, `GalleryPage` | The gallery: JSON styles in a grid of `QRKudeView`s. |
| `EditorPage` | The editor state; `_style` builds a `QRKudeStyle` from the controls. |
| `_Preview` | The preview, the encoding details, the lint chips and the JSON. |
| `_LogoImage` | An `ImageProvider` that draws the demo logo with a `PictureRecorder`. |

## Development

```sh
flutter analyze
flutter test        # opens the gallery and the editor in a widget test
```

## License

Apache License 2.0, like the rest of QRKude (see the `LICENSE` file of the
`qrkude_flutter` package).
