# Contributing to QRKude

Thanks for helping! QRKude promises that **the same style document produces
the same QR code in every language**. Most of the rules below exist to keep
that promise.

## How the repository fits together

```
core/            C99 reference implementation (encoder, shapes, SVG, raster)
docs/GUIDE.md    concepts for users (how QR codes work, scannability)
docs/STYLES.md   every style option with pictures
docs/SPEC.md     normative description of the behaviour, for port authors
docs/tools/      generator for the pictures in docs/images/
spec/vectors/    conformance vectors generated from core/
spec/tools/      vector generator, decode checker
packages/*       ports (web, python, rust, dart, flutter) and wrappers
                 (qt, arduino, esp-idf, lvgl)
```

## Changing behaviour (new shape, new option, bug fix in the output)

1. Implement it in `core/` first, with unit tests in `core/tests/`.
2. Describe it in `docs/SPEC.md` precisely enough that a port can be written
   from the text alone (exact geometry, emission order, rounding).
3. Add cases to `spec/tools/gen_vectors.py` and regenerate:
   `make -C core && python3 spec/tools/gen_vectors.py`.
4. Check scannability: `python3 spec/tools/check_decode.py` (needs
   `pip install zxing-cpp cairosvg pillow`). Every new style should decode
   with zxing; if it does not in some combination, add a `lint` rule.
5. Update every port until its vector tests pass again. A pull request that
   changes vectors without updating the ports is fine as long as it says so;
   ports are then fixed in follow-ups before the next release.
6. Update the documentation (next section).

Purely internal changes (performance, refactors, new helpers in one package)
do not need vectors, as long as all vector tests keep passing.

## Determinism rules for ports

- Numbers in SVG go through `fmt()` (SPEC §8.1): `floor(v * 1000 + 0.5)`
  followed by integer formatting. Never use the language's own fixed-point
  formatter.
- Keep the operation order of the C code for geometry (`(1 - s) / 2`,
  `rho * s / 2`, `sqrt(2.5) * h`...). IEEE doubles then agree bit for bit.
- Compare points with exact equality where the reference does (path
  normalization, `H`/`V` selection).
- Gradient directions for 0/90/180/270 degrees are exact constants, not
  `cos`/`sin` results.

## Documentation

QRKude is used by people who are not QR code experts, so documentation is
part of every change:

- A **new or changed style option** goes into `docs/STYLES.md` (table row,
  default, range, a picture) and into `spec/style.schema.json`. Add it to
  `docs/tools/gen_images.py` and run
  `make -C core && python3 docs/tools/gen_images.py`.
- A **new scannability rule** (lint flag) goes into the table in
  `docs/GUIDE.md` with what it means and how to fix it.
- **Package READMEs** share one structure (Status and installation, Quick
  start, Styling, Output, Content helpers, Making sure it scans, Untrusted
  styles, API reference, Platform notes, Development, License). Keep the API
  reference complete: every public symbol, one line each.
- **Every code example must run.** Before merging, extract the examples you
  touched and run or compile them.
- Write plain English with short sentences. Explain a term the first time
  you use it, or link to the [glossary](docs/GUIDE.md#glossary). Do not
  link to URLs that do not exist yet.

## Running the tests

| package | command |
|---|---|
| core | `make -C core test` |
| core under sanitizers | see the `core` job in `.github/workflows/ci.yml` |
| vectors up to date | `python3 spec/tools/gen_vectors.py && git diff --exit-code spec/vectors` |
| decode check | `python3 spec/tools/check_decode.py` |
| others | see each package README |

## Code style

- Match the surrounding code. C: C99, 4 spaces, no allocation in the core.
- Every source file starts with:
  ```
  Copyright 2026 The QRKude Authors
  SPDX-License-Identifier: Apache-2.0
  ```

## License

By contributing you agree that your contributions are licensed under the
Apache License 2.0 (see section 5 of [`LICENSE`](LICENSE)).
