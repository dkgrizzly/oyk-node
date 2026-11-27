#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Validate Markdown relative links and ensure every port is documented."""

import os
import re
import subprocess
import sys

LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")


def collect_md_files() -> list:
    tracked = subprocess.check_output(["git", "ls-files", "-z"]).split(b"\x00")
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z"]).split(b"\x00")
    paths = set()
    for raw in tracked + untracked:
        if not raw:
            continue
        p = raw.decode("utf-8", "replace")
        if p.endswith(".md") and os.path.isfile(p):
            paths.add(p)
    return sorted(paths)


def check_links(md_files: list) -> tuple:
    broken = []
    total = 0
    for path in md_files:
        with open(path, encoding="utf-8", errors="replace") as fp:
            txt = fp.read()
        txt = re.sub(r"```.*?```", "", txt, flags=re.S)
        base = os.path.dirname(path)
        for m in LINK_RE.finditer(txt):
            target = m.group(1).strip().split()[0]
            if target.startswith(("http://", "https://", "mailto:", "#", "file://")):
                continue
            path_part = target.split("#", 1)[0]
            if not path_part:
                continue
            total += 1
            resolved = os.path.normpath(os.path.join(base, path_part))
            if not os.path.exists(resolved):
                broken.append(f"{path}: broken link ({target}) -> {resolved}")
    return total, broken


def check_port_coverage() -> list:
    errors = []
    ports = sorted(
        d for d in os.listdir("ports")
        if os.path.isdir(os.path.join("ports", d)) and d != "compat"
    )
    required_docs = ["README.md", os.path.join("docs", "user", "feature-matrix.md")]
    for doc in required_docs:
        if not os.path.isfile(doc):
            errors.append(f"missing required documentation file: {doc}")
            continue
        with open(doc, encoding="utf-8", errors="replace") as fp:
            body = fp.read()
        for port in ports:
            token = f"ports/{port}/"
            if token not in body:
                errors.append(f"{doc} does not reference '{token}'")
    return errors


def main() -> int:
    md_files = collect_md_files()
    total_links, link_errors = check_links(md_files)
    port_errors = check_port_coverage()
    all_errors = link_errors + port_errors
    if all_errors:
        print("DOCUMENTATION CHECK FAILED:", file=sys.stderr)
        for err in all_errors:
            print(f"  - {err}", file=sys.stderr)
        return 1
    print(f"[PASS] Documentation links ({total_links} links across {len(md_files)} files) and port coverage verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
