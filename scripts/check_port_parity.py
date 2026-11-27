#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Enforce cross-port UI, graphical control, gallery, and unit-test parity."""

import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

GALLERY_PREFIXES = (
    "ncurses",
    "dos",
    "dos_gfx",
    "tinytmux",
    "pico",
    "gtk",
    "win",
    "macclassic_color",
    "macclassic_bw",
    "gsos",
    "amiga",
    "atarist_color",
    "atarist_mono",
    "android",
)

GALLERY_SUFFIXES = (
    "main",
    "settings",
    "add_iface",
    "edit_iface",
    "peers",
    "edit_peer",
    "terminate",
    "ban",
    "exit",
    "theme",
    "terminal",
    "files",
    "contact_sheet",
)

RETRO_GUI_FILES = (
    "ports/macclassic/mac_ui.cpp",
    "ports/gsos/gsos_ui.c",
    "ports/amiga/amiga_ui.cpp",
    "ports/atarist/atari_ui.cpp",
)

TEXT_MIMIC_RE = re.compile(r'"[^"]*(?:\[[xX* ]\]|\([x* ]\))[^"]*"')


def read_text(rel_path):
    abs_path = os.path.join(ROOT, rel_path)
    with open(abs_path, "r", encoding="utf-8") as fh:
        return fh.read()


def check_gallery_completeness(errors):
    img_dir = os.path.join(ROOT, "docs", "user", "images", "ui-gallery")
    for prefix in GALLERY_PREFIXES:
        for suffix in GALLERY_SUFFIXES:
            path = os.path.join(img_dir, f"{prefix}_{suffix}.png")
            if not os.path.isfile(path) or os.path.getsize(path) == 0:
                errors.append(f"Missing or empty UI gallery image: {os.path.relpath(path, ROOT)}")


def check_retro_gui_parity(errors):
    for rel_path in RETRO_GUI_FILES:
        src = read_text(rel_path)
        for token in ("_draw_checkbox", "_draw_radio", "_draw_field_row"):
            if token not in src:
                errors.append(f"{rel_path}: missing graphical helper '{token}'")
        matched_tabs = [t for t in range(7) if f"active_tab == {t}" in src]
        if len(matched_tabs) < 6:
            errors.append(
                f"{rel_path}: expected >=6 explicit 'active_tab == k' branches (+ else) for 7 tabs, found {matched_tabs}"
            )
        if "Review Peers" not in src:
            errors.append(f"{rel_path}: missing 4-button Exit action 'Review Peers'")
        for lineno, line in enumerate(src.splitlines(), start=1):
            m = TEXT_MIMIC_RE.search(line)
            if not m:
                continue
            lit = m.group(0)
            if '"[X]"' in line and (("mw -" in line) or ("right -" in line) or ("close" in line.lower())):
                continue
            errors.append(f"{rel_path}:{lineno}: forbidden text control mimic {lit}")


def check_native_and_android_parity(errors):
    gtk_src = read_text("src/ui/gtk2_ui.cpp")
    if "gtk_check_button_new" not in gtk_src or "gtk_combo_box_new" not in gtk_src:
        errors.append("src/ui/gtk2_ui.cpp: missing native GTK check/combo controls")

    win_rc = read_text("ports/windows/oyk_win.rc")
    if "AUTOCHECKBOX" not in win_rc or "COMBOBOX" not in win_rc:
        errors.append("ports/windows/oyk_win.rc: missing native Win32 check/combo controls")

    android_screens = read_text(
        "ports/android/app/src/main/java/com/kr4oyk/oyknode/OykScreens.kt"
    )
    if "Last 5 New Callsigns Heard" not in android_screens:
        errors.append("Android OykScreens.kt: missing 'Last 5 New Callsigns Heard' card")
    if "Checkbox(" not in android_screens:
        errors.append("Android OykScreens.kt: missing Material Checkbox controls")

    android_tests = read_text(
        "ports/android/app/src/test/java/com/kr4oyk/oyknode/OykUiGalleryTest.kt"
    )
    if "Last 5 New Callsigns Heard" not in android_tests or "MHEARD" not in android_tests:
        errors.append("Android OykUiGalleryTest.kt: missing MHEARD unit-test coverage")


def check_unit_test_parity(errors):
    cpp_test_files = (
        "ports/macclassic/test_macclassic.cpp",
        "ports/amiga/test_amiga.cpp",
        "ports/atarist/test_atarist.cpp",
        "tests/test_amiga_policy.cpp",
        "tests/test_atarist_policy.cpp",
    )
    for rel_path in cpp_test_files:
        src = read_text(rel_path)
        if "oyk_run_shared_port_parity_tests" not in src:
            errors.append(f"{rel_path}: missing call to oyk_run_shared_port_parity_tests")

    gsos_native = read_text("ports/gsos/test_gsos.c")
    for req in (
        "parity_parse_callsign_invalid",
        "parity_config_eol_parse",
        "parity_wiz_5_drivers_and_descs",
        "parity_console_header_and_pill",
        "parity_theme_cycle",
    ):
        if req not in gsos_native:
            errors.append(f"ports/gsos/test_gsos.c: missing C89 parity check '{req}'")

    gsos_host = read_text("tests/test_gsos.cpp")
    for req in (
        "oyk_gsos_format_console_header",
        "oyk_gsos_wiz_driver_desc",
        "oyk_gsos_theme_cycle",
    ):
        if req not in gsos_host:
            errors.append(f"tests/test_gsos.cpp: missing host GS/OS parity check '{req}'")


def check_native_window_framing(errors):
    checks = (
        ("src/ui/gtk2_ui.cpp", ("gdk_window_get_frame_extents", "gdk_get_default_root_window"), ()),
        ("ports/windows/win_main.cpp", ("UnionRect",), ()),
        ("scripts/generate_ui_gallery.sh", ("xfwm4", "explorer /desktop="), ()),
        ("ports/macclassic/mac_ui.cpp", ("modal_win", "SetRect(&wrect, 6, 42,"), ()),
        ("ports/amiga/amiga_ui.cpp", ("WFLG_DRAGBAR", "WFLG_CLOSEGADGET"), ("WFLG_BORDERLESS",)),
        ("ports/atarist/atari_ui.cpp", ("wind_create", "wind_open"), ()),
    )
    for rel_path, required_tokens, forbidden_tokens in checks:
        src = read_text(rel_path)
        for tok in required_tokens:
            if tok not in src:
                errors.append(f"{rel_path}: missing native window framing token '{tok}'")
        for tok in forbidden_tokens:
            if tok in src:
                errors.append(f"{rel_path}: forbidden unframed window token '{tok}'")


def main():
    errors = []
    check_gallery_completeness(errors)
    check_retro_gui_parity(errors)
    check_native_and_android_parity(errors)
    check_native_window_framing(errors)
    check_unit_test_parity(errors)
    if errors:
        for err in errors:
            print(f"PARITY FAIL: {err}", file=sys.stderr)
        return 1
    print(
        "Port parity check passed: "
        f"{len(GALLERY_PREFIXES)} gallery sets, {len(RETRO_GUI_FILES)} retro GUI ports, "
        "Android MHEARD, and cross-port test parity verified."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
