#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Run oyk_lyra_tests with temporary ANSI capture output and validate 23-screen TUI parity."""

import os
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: run_lyra_tui_parity.py <oyk_lyra_tests_exe>", file=sys.stderr)
        return 2
    exe = os.path.abspath(sys.argv[1])
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    checker = os.path.join(root, "scripts", "check_tui_parity.py")

    with tempfile.TemporaryDirectory() as tmpdir:
        env = os.environ.copy()
        env["OYK_LYRA_CAPTURE_DIR"] = tmpdir
        subprocess.run([exe], env=env, check=True)
        files = sorted(f for f in os.listdir(tmpdir) if f.startswith("lyra_") and f.endswith(".ansi"))
        if len(files) < 23:
            print(f"Expected >= 23 lyra_*.ansi captures, found {len(files)}", file=sys.stderr)
            return 1
        subprocess.run([sys.executable, checker, tmpdir], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
