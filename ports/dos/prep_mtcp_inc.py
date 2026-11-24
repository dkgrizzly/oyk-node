#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

"""Creates case-bridging header symlinks for mTCP on case-sensitive Unix hosts."""

import glob
import os
import re
import sys


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("Usage: prep_mtcp_inc.py <mtcp_src_dir> <out_inc_dir>\n")
        return 2

    mtcp_src = os.path.abspath(sys.argv[1])
    out_dir = sys.argv[2]

    files = {}
    for sub in ("TCPINC", "INCLUDE"):
        for path in glob.glob(os.path.join(mtcp_src, sub, "*")):
            if os.path.isfile(path):
                files[os.path.basename(path).lower()] = path

    if not files:
        sys.stderr.write(f"ERROR: No mTCP headers found under {mtcp_src}/TCPINC\n")
        return 1

    os.makedirs(out_dir, exist_ok=True)

    def link(name: str, target: str) -> None:
        dst = os.path.join(out_dir, name)
        if os.path.lexists(dst):
            os.unlink(dst)
        os.symlink(target, dst)

    for low, path in files.items():
        orig = os.path.basename(path)
        title = low[0].upper() + low[1:]
        for alias in (low, orig, title):
            link(alias, path)

    inc_re = re.compile(r'#\s*include\s+"([^"]+)"')
    for sub in ("TCPINC", "TCPLIB", "INCLUDE"):
        for path in glob.glob(os.path.join(mtcp_src, sub, "*")):
            if not os.path.isfile(path):
                continue
            with open(path, "r", errors="ignore") as fp:
                for match in inc_re.findall(fp.read()):
                    low = match.lower()
                    if low in files:
                        link(match, files[low])

    return 0


if __name__ == "__main__":
    sys.exit(main())
