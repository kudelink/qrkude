#!/usr/bin/env python3
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Checks relative links and #anchors in every Markdown file of the repo."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKIP = ("node_modules", ".dart_tool", "/dist/", "/build/", ".pytest_cache", "/target/")


def slug(heading):
    s = heading.strip().lower()
    s = re.sub(r"[`*_]", "", s)
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def anchors(path, cache={}):
    if path not in cache:
        text = re.sub(r"```.*?```", "", open(path, encoding="utf-8").read(), flags=re.S)
        seen, out = {}, set()
        for h in re.findall(r"^#{1,6} (.+)$", text, re.M):
            base = slug(h)
            n = seen.get(base, 0)
            out.add(base if n == 0 else f"{base}-{n}")
            seen[base] = n + 1
        cache[path] = out
    return cache[path]


def main():
    problems = 0
    for f in sorted(glob.glob(os.path.join(ROOT, "**", "*.md"), recursive=True)):
        if any(s in f for s in SKIP):
            continue
        text = re.sub(r"```.*?```", "", open(f, encoding="utf-8").read(), flags=re.S)
        text = re.sub(r"`[^`\n]*`", "", text)  # inline code is not a link
        for target in re.findall(r"\]\(([^)\s]+)\)", text) + re.findall(r'src="([^"]+)"', text):
            if target.startswith(("http://", "https://", "mailto:")):
                continue
            path, _, anchor = target.partition("#")
            full = os.path.normpath(os.path.join(os.path.dirname(f), path)) if path else f
            rel = os.path.relpath(f, ROOT)
            if not os.path.exists(full):
                print(f"{rel}: missing file {target}")
                problems += 1
            elif anchor and full.endswith(".md") and anchor not in anchors(full):
                print(f"{rel}: missing anchor {target}")
                problems += 1
    print(f"{problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
