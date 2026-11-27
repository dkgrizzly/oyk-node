#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Cross-compile ports/lyra for ARMv7 Cortex-A7 hardfp and check <= 1,024 B stack frames."""

import argparse
import os
import subprocess
import sys
import tempfile


def compile_and_check(cxx_bin: str, cc_bin: str, root: str, is_picocalc: int, board_id: str) -> int:
    lyra_dir = os.path.join(root, "ports", "lyra")
    compat_dir = os.path.join(root, "ports", "compat")
    ppp_dir = os.path.join(root, "ports", "tinytmux", "oyk_tinytmux")
    vt_dir = os.path.join(root, "third_party", "oyk-vt100")
    stubs_dir = os.path.join(lyra_dir, "rtthread_stubs")

    cpp_sources = [
        os.path.join(lyra_dir, "board_lyra.cpp"),
        os.path.join(lyra_dir, "lyra_fs.cpp"),
        os.path.join(lyra_dir, "lyra_radio.cpp"),
        os.path.join(lyra_dir, "lyra_net_bridge.cpp"),
        os.path.join(lyra_dir, "lyra_console.cpp"),
        os.path.join(lyra_dir, "lyra_picocalc_lcd.cpp"),
        os.path.join(lyra_dir, "lyra_picocalc_kbd.cpp"),
        os.path.join(lyra_dir, "lyra_picocalc_audio.cpp"),
        os.path.join(lyra_dir, "lyra_picocalc_console.cpp"),
        os.path.join(lyra_dir, "main_lyra.cpp"),
        os.path.join(compat_dir, "oyk_portable.cpp"),
        os.path.join(compat_dir, "oyk_yapp.cpp"),
        os.path.join(compat_dir, "oyk_rig.cpp"),
        os.path.join(compat_dir, "oyk_printer.cpp"),
        os.path.join(compat_dir, "oyk_tui.cpp"),
        os.path.join(compat_dir, "oyk_term.cpp"),
        os.path.join(compat_dir, "oyk_gui.cpp"),
        os.path.join(compat_dir, "oyk_gui_theme.cpp"),
        os.path.join(ppp_dir, "oyk_ppp.cpp"),
    ]
    c_sources = [
        os.path.join(vt_dir, "src", "oyk_vt100.c"),
        os.path.join(vt_dir, "src", "oyk_vt_font.c"),
        os.path.join(vt_dir, "src", "vt_font_4x8.c"),
        os.path.join(vt_dir, "src", "vt_font_6x10.c"),
        os.path.join(vt_dir, "src", "oyk_vt_fb.c"),
    ]

    common_flags = [
        "-mcpu=cortex-a7",
        "-mfpu=neon-vfpv4",
        "-mfloat-abi=hard",
        "-Os",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fstack-usage",
        f"-I{stubs_dir}",
        f"-I{lyra_dir}",
        f"-I{compat_dir}",
        f"-I{ppp_dir}",
        f"-I{os.path.join(vt_dir, 'include')}",
        "-DOYK_PORTABLE_NODE=1",
        "-DOYK_LYRA_RTTHREAD=1",
        f"-DOYK_LYRA_IS_PICOCALC={is_picocalc}",
        f'-DOYK_LYRA_BOARD_ID="{board_id}"',
        "-DOYK_VT_ENABLE_256COLOR=1",
        "-DOYK_VT_ENABLE_TRUECOLOR=0",
        "-DOYK_VT_MAX_COLS=80",
        "-DOYK_VT_MAX_ROWS=48",
    ]

    frames = []
    with tempfile.TemporaryDirectory() as td:
        for idx, src in enumerate(cpp_sources):
            obj = os.path.join(td, f"cpp_{idx}.o")
            su = os.path.join(td, f"cpp_{idx}.su")
            cmd = [cxx_bin, "-std=c++98", "-fno-exceptions", "-fno-rtti"] + common_flags + ["-c", src, "-o", obj]
            subprocess.run(cmd, check=True)
            with open(su, encoding="utf-8", errors="replace") as fp:
                for line in fp:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) >= 3:
                        frames.append((int(parts[1]), parts[2], parts[0]))
        for idx, src in enumerate(c_sources):
            obj = os.path.join(td, f"c_{idx}.o")
            su = os.path.join(td, f"c_{idx}.su")
            cmd = [cc_bin, "-std=c99"] + common_flags + ["-c", src, "-o", obj]
            subprocess.run(cmd, check=True)
            with open(su, encoding="utf-8", errors="replace") as fp:
                for line in fp:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) >= 3:
                        frames.append((int(parts[1]), parts[2], parts[0]))

    frames.sort(reverse=True)
    offenders = [f for f in frames if f[0] > 1024]
    max_sz = frames[0][0] if frames else 0
    if offenders:
        print(f"[FAIL] ARMv7 Cortex-A7 stack check ({board_id}): max {max_sz} B > 1024 B", file=sys.stderr)
        for sz, qual, loc in offenders:
            print(f"STACK LIMIT EXCEEDED: {sz} B [{qual}] {loc}", file=sys.stderr)
        return 1
    print(f"[PASS] ARMv7 Cortex-A7 verify ({board_id}): {len(frames)} functions compiled, max stack {max_sz} B <= 1024 B")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cxx", required=True)
    ap.add_argument("--cc", required=True)
    ap.add_argument("--root", required=True)
    args = ap.parse_args()

    rc = compile_and_check(args.cxx, args.cc, args.root, 0, "lyra_plus")
    if rc != 0:
        return rc
    return compile_and_check(args.cxx, args.cc, args.root, 1, "picocalc_lyra")


if __name__ == "__main__":
    raise SystemExit(main())
