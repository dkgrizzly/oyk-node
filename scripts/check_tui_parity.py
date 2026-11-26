#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Cross-TUI parity validator checking canonical layout invariants across all TUI captures."""

import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import render_ui_png as rup  # noqa: E402


def ansi_to_lines(path: str) -> list:
    with open(path, "rb") as f:
        rgb_grid = rup.parse_ansi_screen(f.read())
    return ["".join(cell[0] for cell in row) for row in rgb_grid]


def dosbuf_to_lines(path: str) -> list:
    with open(path, "rb") as f:
        raw = f.read()
    row_bytes = rup.COLS * 2
    rows = len(raw) // row_bytes
    lines = []
    idx = 0
    for _ in range(rows):
        chars = []
        for _ in range(rup.COLS):
            code = raw[idx]
            idx += 2
            chars.append(rup.CP437_UNICODE.get(code, chr(code) if 0x20 <= code <= 0x7E else " "))
        lines.append("".join(chars))
    return lines


def meta_to_lines(path: str) -> list:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("lines", [])


def load_port_captures(cap_dir: str) -> dict:
    ports = {}
    for fname in sorted(os.listdir(cap_dir)):
        fpath = os.path.join(cap_dir, fname)
        if fname.endswith(".ansi"):
            stem = fname[:-5]
            loader = ansi_to_lines
        elif fname.endswith(".dosbuf"):
            stem = fname[:-7]
            loader = dosbuf_to_lines
        elif fname.endswith(".meta.json"):
            stem = fname[:-10]
            loader = meta_to_lines
        else:
            continue
        for prefix in ("ncurses", "dos_gfx", "dos", "tinytmux", "pico"):
            tag = prefix + "_"
            if stem.startswith(tag):
                flow = stem[len(tag):]
                ports.setdefault(prefix, {})[flow] = loader(fpath)
                break
    return ports


def check_flow(port: str, flow: str, lines: list, errors: list) -> None:
    text = "\n".join(lines)
    row0 = lines[0] if lines else ""

    def need(sub: str, desc: str = "") -> None:
        if sub not in text:
            errors.append(f"[{port}/{flow}] missing {desc or repr(sub)}")

    if flow in ("settings", "add_iface", "edit_iface", "peers", "edit_peer",
                "terminate", "ban", "exit", "theme"):
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
    elif flow == "settings":
        if "Node" not in row0:
            errors.append(f"[{port}/settings] Settings must float over main console (Row 1 missing 'Node'): {row0!r}")
        need("Node Settings")
        for tab in ("1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn"):
            need(tab, f"canonical tab {tab}")
    elif flow == "add_iface":
        need("Add Interface")
        need("Same-Iface")
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
        need("Ctrl+A Z", "Ctrl+A Help hint")
    elif flow == "files":
        need("Pending Incoming")


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
        for flow, lines in sorted(flows.items()):
            check_flow(port, flow, lines, errors)

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
