#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Verify SPDX-License-Identifier and Copyright headers across tracked text files."""

import os
import subprocess
import sys

SKIP_EXACT = {
    "LICENSE",
    "AUTHORS",
    ".gitignore",
    ".gitattributes",
    "REUSE.toml",
}
SKIP_PREFIXES = (
    "docs/specs/rfc",
    "ports/android/gradle/",
)
SKIP_SUFFIXES = (
    "/gradlew",
    "/gradlew.bat",
)
BINARY_EXTS = {
    ".png", ".bmp", ".jpg", ".jpeg", ".gif", ".ico", ".zip", ".img",
    ".bin", ".jar", ".woff", ".ttf", ".keystore", ".jks", ".so", ".a", ".o",
}


def should_skip(path: str) -> bool:
    if path in SKIP_EXACT:
        return True
    if any(path.startswith(p) for p in SKIP_PREFIXES):
        return True
    if any(path.endswith(s) for s in SKIP_SUFFIXES):
        return True
    _, ext = os.path.splitext(path.lower())
    return ext in BINARY_EXTS


def main() -> int:
    tracked = subprocess.check_output(["git", "ls-files", "-z"]).split(b"\x00")
    paths = [p.decode("utf-8", "replace") for p in tracked if p]
    checked = 0
    missing = []
    for path in paths:
        if should_skip(path) or not os.path.isfile(path):
            continue
        with open(path, "rb") as fp:
            head = fp.read(768)
        if b"\x00" in head:
            continue
        checked += 1
        txt = head.decode("utf-8", errors="replace")
        if "SPDX-License-Identifier" not in txt or "Copyright (C)" not in txt:
            missing.append(path)

    if missing:
        print(f"SPDX/COPYRIGHT HEADER CHECK FAILED ({len(missing)} file(s)):", file=sys.stderr)
        for m in missing:
            print(f"  - {m}", file=sys.stderr)
        return 1

    print(f"[PASS] SPDX & copyright headers verified across {checked} tracked text files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
