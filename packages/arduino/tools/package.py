#!/usr/bin/env python3
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
#
# Assembles the standalone embedded packages of QRKude from the monorepo:
#
#   dist/arduino/QRKude/   Arduino / PlatformIO library (core in src/core/)
#   dist/esp-idf/qrkude/   ESP-IDF component (core in src/ + include/)
#   dist/lvgl/             lv_qrkude widget + core, flat, for any build
#
# The monorepo never contains copies of the C core; this script does the
# copying. With --zip it also writes dist/QRKude-arduino-<version>.zip,
# dist/qrkude-esp-idf-<version>.zip and dist/qrkude-lvgl-<version>.zip.
#
# usage: package.py [--out DIR] [--zip] [--only arduino,esp-idf,lvgl]
import argparse
import json
import os
import re
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CORE = os.path.join(ROOT, "core")
PKG = os.path.join(ROOT, "packages")
CORE_SOURCES = ["qrk_encode.c", "qrk_layout.c", "qrk_svg.c", "qrk_raster.c", "qrk_json.c",
                "qrk_payload.c", "qrk_lint.c", "qrk_internal.h"]
IGNORE = shutil.ignore_patterns(".pio", "build", "__pycache__", "*.pyc", "sdkconfig",
                                "sdkconfig.old", "sdkconfig.esp32*", "dependencies.lock",
                                "managed_components", ".vscode", ".gitignore")


IDF_EXAMPLE_MANIFEST = """\
dependencies:
  idf: ">=5.1"
  kudelink/qrkude:
    version: "*"
    # The example builds against the component it ships with.
    override_path: "../../../"
"""


def core_version():
    with open(os.path.join(CORE, "include", "qrkude.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+QRK_LIB_VERSION\s+"([^"]+)"', f.read())
    if not m:
        sys.exit("QRK_LIB_VERSION not found in core/include/qrkude.h")
    return m.group(1)


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def copy_tree(src, dst):
    shutil.copytree(src, dst, ignore=IGNORE, dirs_exist_ok=True)


def copy_core(src_dir, include_dir):
    for name in CORE_SOURCES:
        copy(os.path.join(CORE, "src", name), os.path.join(src_dir, name))
    copy(os.path.join(CORE, "include", "qrkude.h"), os.path.join(include_dir, "qrkude.h"))


def copy_legal(dst):
    for name in ("LICENSE", "NOTICE"):
        path = os.path.join(ROOT, name)
        if os.path.exists(path):
            copy(path, os.path.join(dst, name))


def stamp(path, pattern, replacement):
    """Replaces the version in a manifest so that it follows the core."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    new, n = re.subn(pattern, replacement, text, count=1, flags=re.M)
    if n != 1:
        sys.exit(f"cannot stamp the version in {path}")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)


def build_arduino(out, version):
    src = os.path.join(PKG, "arduino")
    dst = os.path.join(out, "arduino", "QRKude")
    shutil.rmtree(dst, ignore_errors=True)
    for name in ("library.properties", "library.json", "README.md", "keywords.txt"):
        copy(os.path.join(src, name), os.path.join(dst, name))
    copy_legal(dst)
    copy_tree(os.path.join(src, "src"), os.path.join(dst, "src"))
    # The core goes to src/core/: "qrkude.h" next to "QRKude.h" would clash
    # on case-insensitive file systems. Arduino compiles src/ recursively.
    copy_core(os.path.join(dst, "src", "core"), os.path.join(dst, "src", "core"))
    # The LVGL widget compiles to nothing unless the sketch uses LVGL.
    for name in ("lv_qrkude.h", "lv_qrkude.c"):
        copy(os.path.join(PKG, "lvgl", name), os.path.join(dst, "src", name))
    copy_tree(os.path.join(src, "examples"), os.path.join(dst, "examples"))
    copy(os.path.join(src, "tools", "img2qrk.py"), os.path.join(dst, "extras", "img2qrk.py"))
    stamp(os.path.join(dst, "library.properties"), r"^version=.*$", f"version={version}")
    stamp(os.path.join(dst, "library.json"), r'"version":\s*"[^"]*"', f'"version": "{version}"')
    json.load(open(os.path.join(dst, "library.json"), encoding="utf-8"))  # sanity check
    return dst


def build_esp_idf(out, version):
    src = os.path.join(PKG, "esp-idf")
    dst = os.path.join(out, "esp-idf", "qrkude")
    shutil.rmtree(dst, ignore_errors=True)
    for name in ("CMakeLists.txt", "Kconfig", "idf_component.yml", "README.md"):
        copy(os.path.join(src, name), os.path.join(dst, name))
    copy_legal(dst)
    copy_core(os.path.join(dst, "src"), os.path.join(dst, "include"))
    for name in ("lv_qrkude.h", "lv_qrkude.c"):
        copy(os.path.join(PKG, "lvgl", name), os.path.join(dst, "lvgl", name))
    copy_tree(os.path.join(src, "examples"), os.path.join(dst, "examples"))
    stamp(os.path.join(dst, "idf_component.yml"), r'^version:\s*".*"$', f'version: "{version}"')
    # Registry-style dependency of each example on the component it ships
    # with (create-project-from-example drops the override_path).
    for example in sorted(os.listdir(os.path.join(dst, "examples"))):
        main_dir = os.path.join(dst, "examples", example, "main")
        if os.path.isdir(main_dir):
            with open(os.path.join(main_dir, "idf_component.yml"), "w", encoding="utf-8") as f:
                f.write(IDF_EXAMPLE_MANIFEST)
    return dst


def build_lvgl(out, version):
    src = os.path.join(PKG, "lvgl")
    dst = os.path.join(out, "lvgl")
    shutil.rmtree(dst, ignore_errors=True)
    for name in ("lv_qrkude.h", "lv_qrkude.c", "README.md"):
        copy(os.path.join(src, name), os.path.join(dst, name))
    copy_legal(dst)
    copy_core(dst, dst)
    with open(os.path.join(dst, "VERSION"), "w", encoding="utf-8") as f:
        f.write(version + "\n")
    return dst


def make_zip(directory, base_name, root_dir, base_dir):
    path = shutil.make_archive(base_name, "zip", root_dir=root_dir, base_dir=base_dir)
    print(f"  {os.path.relpath(path, ROOT)}")


def main(argv=None):
    p = argparse.ArgumentParser(description="Assemble the QRKude embedded packages.")
    p.add_argument("--out", default=os.path.join(ROOT, "dist"), help="output directory (default: dist/)")
    p.add_argument("--zip", action="store_true", help="also write zip archives")
    p.add_argument("--only", default="arduino,esp-idf,lvgl",
                   help="comma separated subset of arduino, esp-idf, lvgl")
    args = p.parse_args(argv)

    version = core_version()
    out = os.path.abspath(args.out)
    wanted = {x.strip() for x in args.only.split(",") if x.strip()}
    unknown = wanted - {"arduino", "esp-idf", "lvgl"}
    if unknown:
        sys.exit(f"unknown package(s): {', '.join(sorted(unknown))}")

    print(f"QRKude {version} -> {out}")
    built = {}
    if "arduino" in wanted:
        built["arduino"] = build_arduino(out, version)
    if "esp-idf" in wanted:
        built["esp-idf"] = build_esp_idf(out, version)
    if "lvgl" in wanted:
        built["lvgl"] = build_lvgl(out, version)
    for name, path in built.items():
        print(f"  {name:8s} {os.path.relpath(path, ROOT)}")

    if args.zip:
        if "arduino" in built:
            make_zip(built["arduino"], os.path.join(out, f"QRKude-arduino-{version}"),
                     os.path.join(out, "arduino"), "QRKude")
        if "esp-idf" in built:
            make_zip(built["esp-idf"], os.path.join(out, f"qrkude-esp-idf-{version}"),
                     os.path.join(out, "esp-idf"), "qrkude")
        if "lvgl" in built:
            make_zip(built["lvgl"], os.path.join(out, f"qrkude-lvgl-{version}"), out, "lvgl")
    return 0


if __name__ == "__main__":
    sys.exit(main())
