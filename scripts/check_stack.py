#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Static worst-case per-function stack usage check for ports/compat/ C++98 sources."""

import argparse
import glob
import os
import subprocess
import sys
import tempfile


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify ports/compat/ stack frames stay within budget.")
    ap.add_argument("--limit", type=int, default=1024, help="Maximum allowed frame bytes per function.")
    ap.add_argument("--cxx", default="g++", help="C++ compiler binary supporting -fstack-usage.")
    args = ap.parse_args()

    sources = sorted(glob.glob(os.path.join("ports", "compat", "*.cpp")))
    if not sources:
        print("Error: no C++ sources found under ports/compat/.", file=sys.stderr)
        return 1

    frames = []
    with tempfile.TemporaryDirectory() as td:
        for src in sources:
            base = os.path.basename(src)
            obj = os.path.join(td, base + ".o")
            su = os.path.join(td, base + ".su")
            cmd = [
                args.cxx,
                "-std=c++98",
                "-Os",
                "-fstack-protector-strong",
                "-fno-exceptions",
                "-fno-rtti",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fstack-usage",
                "-Iports/compat",
                "-c",
                src,
                "-o",
                obj,
            ]
            subprocess.run(cmd, check=True)
            if not os.path.isfile(su):
                print(f"Error: expected stack-usage file {su} was not generated.", file=sys.stderr)
                return 1
            with open(su, encoding="utf-8", errors="replace") as fp:
                for line in fp:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) >= 3:
                        frames.append((int(parts[1]), parts[2], parts[0]))

    frames.sort(reverse=True)
    offenders = [f for f in frames if f[0] > args.limit]
    print(f"Stack audit: checked {len(frames)} functions across {len(sources)} files (limit={args.limit} B).")
    for size, qual, loc in frames[:8]:
        print(f"  {size:5d} B  [{qual}]  {loc}")

    if offenders:
        print(f"STACK BUDGET EXCEEDED ({len(offenders)} function(s) > {args.limit} B):", file=sys.stderr)
        for size, qual, loc in offenders:
            print(f"  - {size} B [{qual}] {loc}", file=sys.stderr)
        return 1

    print(f"[PASS] All portable core functions fit within <= {args.limit} B per frame.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
