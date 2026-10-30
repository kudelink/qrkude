# Validation containers

Build a Linux environment for C (64 and 32 bit), Qt 5 with QML, Python and
PlatformIO tests, from the repository root:

```sh
docker build -t qrkude-validation:local tools/validation
```

Run a suite on a source snapshot so builds cannot change the checkout.
Choose `core`, `qt`, `lvgl` or `embedded` in the last argument:

```sh
tar --exclude=.git --exclude=node_modules --exclude=target --exclude=build \
    --exclude=dist --exclude=.dart_tool --exclude=.pio --exclude=.venv \
    --exclude=__pycache__ -cf - . |
  docker run --rm -i --cpus=4 --memory=6g qrkude-validation:local sh -c \
    'tar -xf -; sh tools/validation/check.sh core'
```

The suites include sanitizers, checked size arithmetic on 32-bit C, decoding
rendered QR codes and LVGL allocation failures. PlatformIO downloads the
LVGL versions and board toolchains it needs. `embedded` also builds the
Arduino examples for ESP32, ESP8266, RP2040, SAMD and AVR.

Dart and Flutter can be tested in a Flutter SDK container. Copy the same
snapshot into it, run `dart pub get`, `dart analyze`, `dart test` and
`dart test -p node` in `packages/dart`, then `flutter pub get`,
`flutter analyze` and `flutter test` in `packages/flutter`. Node.js is
needed for Dart's Node tests.
