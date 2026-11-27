#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Cross-TUI parity validator checking canonical layout invariants across all TUI captures."""

import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import render_ui_png as rup  # noqa: E402

BUTTON_RE = re.compile(r"\[ ([^\[\]]*?\S[^\[\]]*?) \]")
BUTTON_FLOWS = ("add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
                "edit_iface", "edit_peer", "exit")


def ansi_to_lines_and_bg(path: str):
    with open(path, "rb") as f:
        rgb_grid = rup.parse_ansi_screen(f.read())
    lines = ["".join(cell[0] for cell in row) for row in rgb_grid]
    bg_grid = [[cell[2] for cell in row] for row in rgb_grid]
    return lines, bg_grid


def dosbuf_to_lines_and_bg(path: str):
    with open(path, "rb") as f:
        raw = f.read()
    row_bytes = rup.COLS * 2
    rows = len(raw) // row_bytes
    lines = []
    bg_grid = []
    idx = 0
    for _ in range(rows):
        chars = []
        bgs = []
        for _ in range(rup.COLS):
            code = raw[idx]
            attr = raw[idx + 1]
            idx += 2
            chars.append(rup.CP437_UNICODE.get(code, chr(code) if 0x20 <= code <= 0x7E else " "))
            bgs.append((attr >> 4) & 0x0F)
        lines.append("".join(chars))
        bg_grid.append(bgs)
    return lines, bg_grid


def meta_to_lines_and_bg(path: str):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("lines", []), None


def load_port_captures(cap_dir: str) -> dict:
    ports = {}
    for fname in sorted(os.listdir(cap_dir)):
        fpath = os.path.join(cap_dir, fname)
        if fname.endswith(".ansi"):
            stem = fname[:-5]
            loader = ansi_to_lines_and_bg
        elif fname.endswith(".dosbuf"):
            stem = fname[:-7]
            loader = dosbuf_to_lines_and_bg
        elif fname.endswith(".meta.json"):
            stem = fname[:-10]
            loader = meta_to_lines_and_bg
        else:
            continue
        for prefix in ("ncurses", "dos_gfx", "dos", "tinytmux", "pico", "esp32c6", "lyra"):
            tag = prefix + "_"
            if stem.startswith(tag):
                flow = stem[len(tag):]
                ports.setdefault(prefix, {})[flow] = loader(fpath)
                break
    return ports


def check_button_tightness(port: str, flow: str, lines: list, bg_grid, errors: list) -> None:
    if bg_grid is None or flow not in BUTTON_FLOWS:
        return
    border_chars = set("|│║")
    found_buttons = 0
    for r_idx, line in enumerate(lines):
        if r_idx < 3 or r_idx >= len(bg_grid):
            continue
        matches = list(BUTTON_RE.finditer(line))
        if not matches:
            continue
        bg_row = bg_grid[r_idx]
        first_s = matches[0].start()
        last_e = matches[-1].end()
        left_b = max((c for c in range(first_s) if line[c] in border_chars), default=-1)
        right_b = min((c for c in range(last_e, len(line)) if line[c] in border_chars), default=-1)
        if left_b < 0 or right_b <= left_b:
            continue
        occupied = set()
        for m in matches:
            for c in range(m.start() - 1, m.end() + 1):
                occupied.add(c)
        bg_samples = [
            bg_row[c]
            for c in range(left_b + 1, right_b)
            if c not in occupied and c < len(bg_row) and line[c] == " "
        ]
        if not bg_samples:
            continue
        modal_bg = max(set(bg_samples), key=bg_samples.count)
        for m in matches:
            found_buttons += 1
            s, e = m.start(), m.end()
            if s - 1 <= left_b or e >= right_b:
                errors.append(f"[{port}/{flow}] row {r_idx}: button {m.group(0)!r} missing 1-cell padding")
                continue
            if line[s - 1] != " " or line[e] != " ":
                errors.append(f"[{port}/{flow}] row {r_idx}: button {m.group(0)!r} lacks tight ' [ Label ] ' space padding")
                continue
            btn_bg = bg_row[s]
            if any(bg_row[c] != btn_bg for c in range(s - 1, e + 1)):
                errors.append(f"[{port}/{flow}] row {r_idx}: button {m.group(0)!r} inconsistent bg inside ' [ Label ] '")
            if btn_bg != modal_bg:
                if s - 2 > left_b and bg_row[s - 2] == btn_bg:
                    errors.append(f"[{port}/{flow}] row {r_idx}: button {m.group(0)!r} background bleeds left")
                if e + 1 < right_b and bg_row[e + 1] == btn_bg:
                    errors.append(f"[{port}/{flow}] row {r_idx}: button {m.group(0)!r} background bleeds right")
    if found_buttons == 0:
        errors.append(f"[{port}/{flow}] expected canonical ' [ Label ] ' button controls")


def check_flow(port: str, flow: str, lines: list, bg_grid, errors: list) -> None:
    text = "\n".join(lines)
    row0 = lines[0] if lines else ""

    def need(sub: str, desc: str = "") -> None:
        if sub not in text:
            errors.append(f"[{port}/{flow}] missing {desc or repr(sub)}")

    if flow in ("settings", "settings_bbs", "settings_chat", "settings_ifaces",
                "settings_rig", "settings_printer", "add_iface", "add_iface_1",
                "add_iface_2", "add_iface_3", "add_iface_4", "edit_iface",
                "peers", "edit_peer", "terminate", "ban", "exit", "theme",
                "terminal_help", "files", "about"):
        need("[X]", "modal close button [X]")
    elif flow in ("main", "terminal"):
        if "[X]" in text:
            errors.append(f"[{port}/{flow}] unexpected '[X]' close button outside modal")

    if flow == "main":
        if "Node" not in row0:
            errors.append(f"[{port}/main] Row 1 missing 'Node' menu bar: {row0!r}")
        need("Interfaces")
        need("Last 5")
        need("Active Connections")
        need("Activity Log")
    elif flow == "menu":
        need("Allow Incoming")
        need("Settings")
    elif flow in ("settings", "settings_bbs", "settings_chat", "settings_ifaces",
                  "settings_rig", "settings_printer"):
        if "Node" not in row0:
            errors.append(f"[{port}/{flow}] Settings must float over main console (Row 1 missing 'Node'): {row0!r}")
        need("Node Settings")
        for tab in ("1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn"):
            need(tab, f"canonical tab {tab}")
        if flow == "settings_bbs":
            need("BBS")
        elif flow == "settings_chat":
            need("CHAT")
        elif flow == "settings_ifaces":
            need("axudp")
        elif flow == "settings_rig":
            need("Rig")
        elif flow == "settings_printer":
            need("Printer")
    elif flow == "add_iface_1":
        need("Add Interface")
        need("axudp")
    elif flow == "add_iface_2":
        need("Add Interface")
        need("Interface ID")
    elif flow in ("add_iface", "add_iface_3"):
        need("Add Interface")
        need("Same-Iface")
    elif flow == "add_iface_4":
        need("Add Interface")
        need("Review")
    elif flow == "about":
        need("About OYK")
        need("GPL-3.0-or-later")
        need(".-------------.", "ASCII logo top")
        need("O Y K", "ASCII logo O Y K")
        need("N O D E", "ASCII logo N O D E")
    elif flow == "edit_iface":
        need("Edit Interface")
        need("Same-Iface")
    elif flow == "peers":
        need("5:Peers")
        need("ID")
        need("Callsign")
    elif flow == "edit_peer":
        need("Edit Peer")
        need("Allow Inbound")
        need("Allow Outbound")
    elif flow == "terminate":
        need("Terminate Active Connection")
        need("ID")
        need("Callsign")
        need("Port")
        need("State")
    elif flow == "ban":
        need("Ban / Unban Callsign")
        need("Callsign")
        need("Active Bans")
    elif flow == "exit":
        need("Auto-Learned Peers")
    elif flow == "theme":
        need("Theme Settings")
        need("Preview")
        need("Lt/Rt:Theme")
    elif flow == "terminal":
        need("LOCAL TERMINAL")
        need("[EOL:", "EOL indicator")
        need("Ctrl-A Z:Help", "Ctrl-A Help hint")
        need("Tab/F9", "Tab/F9 Console hint")
        if "Terminal Output" in text:
            errors.append(f"[{port}/terminal] forbidden inner 'Terminal Output' box label")
    elif flow == "terminal_help":
        need("Terminal Hotkey Help")
        need("Command prefix: Ctrl-A")
        need("Z / ?")
    elif flow == "files":
        need("Incoming:")
        need("Files:")

    check_button_tightness(port, flow, lines, bg_grid, errors)


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: check_tui_parity.py <capture_dir>", file=sys.stderr)
        return 2
    cap_dir = sys.argv[1]
    ports = load_port_captures(cap_dir)
    if not ports:
        print(f"No TUI captures found in {cap_dir}", file=sys.stderr)
        return 1

    errors = []
    for port, flows in sorted(ports.items()):
        for flow, (lines, bg_grid) in sorted(flows.items()):
            check_flow(port, flow, lines, bg_grid, errors)

    if errors:
        print("Cross-TUI parity check FAILED:", file=sys.stderr)
        for e in errors:
            print("  -", e, file=sys.stderr)
        return 1

    summary = ", ".join(f"{p}({len(f)})" for p, f in sorted(ports.items()))
    print(f"[PASS] Cross-TUI parity check passed: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
