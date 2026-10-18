# QRKude documentation

| Document | Read it when you want to |
|---|---|
| [Guide](GUIDE.md) | understand how QR codes work, what you can style safely, logos, colors, print sizes, and fix a code that does not scan |
| [Style reference](STYLES.md) | look up any style option, with pictures, defaults and ranges |
| [Specification](SPEC.md) | write or maintain a QRKude package: the exact rules behind identical output |

Each package has its own README with installation instructions, examples and
its complete API: see the table in the [main README](../README.md#which-package-do-i-need).

The pictures in `images/guide` and `images/styles` are generated from the C
reference implementation:

```sh
make -C core
python3 docs/tools/gen_images.py
```
