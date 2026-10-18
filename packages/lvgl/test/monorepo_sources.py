# PlatformIO extra script (pre): compiles lv_qrkude.c and the QRKude C core
# in place from the monorepo (the packages never copy the core).
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
import os

Import("env")  # noqa: F821 (provided by PlatformIO)

root = os.path.abspath(os.path.join(env.subst("$PROJECT_DIR"), "..", "..", ".."))  # noqa: F821
# lv_qrkude.c includes lvgl.h; library include paths are only known after
# the dependency finder ran, so point at the downloaded LVGL directly.
env.Append(CPPPATH=[os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl")])  # noqa: F821
env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "qrkude_core"), os.path.join(root, "core", "src"))
env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "lv_qrkude"), os.path.join(root, "packages", "lvgl"),
    "-<*> +<lv_qrkude.c>")
