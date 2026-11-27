#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""End-to-end PTY UI flow test and optional ANSI screen capture for ncurses oyk-node."""

import fcntl
import os
import pty
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time


COLS = 80
ROWS = 25


def ax25_fcs(data: bytes) -> bytes:
    fcs = 0xFFFF
    for b in data:
        fcs ^= b
        for _ in range(8):
            if fcs & 1:
                fcs = (fcs >> 1) ^ 0x8408
            else:
                fcs >>= 1
    fcs ^= 0xFFFF
    return bytes((fcs & 0xFF, (fcs >> 8) & 0xFF))


def encode_ax25_call(call_str: str, last: bool, command_bit: bool) -> bytes:
    if "-" in call_str:
        base, ssid_s = call_str.split("-", 1)
        ssid = int(ssid_s)
    else:
        base, ssid = call_str, 0
    padded = (base.upper() + "      ")[:6]
    out = bytearray((ord(c) << 1) & 0xFE for c in padded)
    ctrl = 0x60 | ((ssid & 0x0F) << 1)
    if command_bit:
        ctrl |= 0x80
    if last:
        ctrl |= 0x01
    out.append(ctrl)
    return bytes(out)


def build_sabm_packet(dst: str, src: str) -> bytes:
    body = encode_ax25_call(dst, False, True) + encode_ax25_call(src, True, False) + b"\x3F"
    return body + ax25_fcs(body)


ACS_TO_UNICODE = {
    "l": "\u250c",
    "k": "\u2510",
    "m": "\u2514",
    "j": "\u2518",
    "q": "\u2500",
    "x": "\u2502",
    "t": "\u251c",
    "u": "\u2524",
    "w": "\u252c",
    "v": "\u2534",
    "n": "\u253c",
    "a": "\u2592",
    "`": "\u25c6",
    "~": "\u00b7",
}


class AnsiScreen:
    """Minimal 80x25 VT/ncurses screen tracker for assertions and standalone ANSI export."""

    def __init__(self, rows: int = ROWS, cols: int = COLS) -> None:
        self.rows = rows
        self.cols = cols
        self.grid = [[(" ", 7, 0, False, False) for _ in range(cols)] for _ in range(rows)]
        self.r = 0
        self.c = 0
        self.fg = 7
        self.bg = 0
        self.bold = False
        self.rev = False
        self.g0_acs = False
        self.state = "TEXT"
        self.esc_buf = ""

    def _clear_cell(self, r: int, c: int) -> None:
        if 0 <= r < self.rows and 0 <= c < self.cols:
            self.grid[r][c] = (" ", self.fg, self.bg, False, False)

    def _put_ch(self, ch: str) -> None:
        if self.g0_acs and ch in ACS_TO_UNICODE:
            ch = ACS_TO_UNICODE[ch]
        if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
            self.grid[self.r][self.c] = (ch, self.fg, self.bg, self.bold, self.rev)
        if self.c + 1 < self.cols:
            self.c += 1

    def _apply_sgr(self, params_str: str) -> None:
        parts = [int(p) if p else 0 for p in params_str.split(";")] if params_str else [0]
        i = 0
        while i < len(parts):
            p = parts[i]
            if p == 0:
                self.fg, self.bg, self.bold, self.rev = 7, 0, False, False
            elif p == 1:
                self.bold = True
            elif p == 22:
                self.bold = False
            elif p == 7:
                self.rev = True
            elif p == 27:
                self.rev = False
            elif 30 <= p <= 37:
                self.fg = p - 30
            elif p == 39:
                self.fg = 7
            elif 40 <= p <= 47:
                self.bg = p - 40
            elif p == 49:
                self.bg = 0
            elif 90 <= p <= 97:
                self.fg = (p - 90) + 8
            elif 100 <= p <= 107:
                self.bg = (p - 100) + 8
            elif p == 38 and i + 2 < len(parts) and parts[i + 1] == 5:
                self.fg = parts[i + 2]
                i += 2
            elif p == 48 and i + 2 < len(parts) and parts[i + 1] == 5:
                self.bg = parts[i + 2]
                i += 2
            i += 1

    def feed(self, raw: bytes) -> None:
        s = raw.decode("utf-8", errors="replace")
        for ch in s:
            if self.state == "TEXT":
                if ch == "\x1b":
                    self.state = "ESC"
                    self.esc_buf = ""
                elif ch == "\r":
                    self.c = 0
                elif ch == "\n":
                    if self.r + 1 < self.rows:
                        self.r += 1
                elif ch == "\x08":
                    if self.c > 0:
                        self.c -= 1
                elif ch == "\x0e":
                    self.g0_acs = True
                elif ch == "\x0f":
                    self.g0_acs = False
                elif ord(ch) >= 0x20:
                    self._put_ch(ch)
            elif self.state == "ESC":
                if ch == "[":
                    self.state = "CSI"
                    self.esc_buf = ""
                elif ch == "(":
                    self.state = "SCS0"
                elif ch == ")":
                    self.state = "SCS1"
                elif ch in (">", "=", "M"):
                    self.state = "TEXT"
                else:
                    self.state = "TEXT"
            elif self.state == "SCS0":
                self.g0_acs = (ch == "0")
                self.state = "TEXT"
            elif self.state == "SCS1":
                self.state = "TEXT"
            elif self.state == "CSI":
                if 0x40 <= ord(ch) <= 0x7E:
                    self._handle_csi(self.esc_buf, ch)
                    self.state = "TEXT"
                else:
                    self.esc_buf += ch

    def _handle_csi(self, buf: str, final: str) -> None:
        if buf.startswith("?"):
            return
        nums = []
        if buf:
            for tok in buf.split(";"):
                nums.append(int(tok) if tok.isdigit() else 0)
        p1 = nums[0] if len(nums) >= 1 else 0
        p2 = nums[1] if len(nums) >= 2 else 0
        if final in ("H", "f"):
            self.r = max(0, min(self.rows - 1, (p1 or 1) - 1))
            self.c = max(0, min(self.cols - 1, (p2 or 1) - 1))
        elif final == "A":
            self.r = max(0, self.r - (p1 or 1))
        elif final == "B":
            self.r = min(self.rows - 1, self.r + (p1 or 1))
        elif final == "C":
            self.c = min(self.cols - 1, self.c + (p1 or 1))
        elif final == "D":
            self.c = max(0, self.c - (p1 or 1))
        elif final == "G":
            self.c = max(0, min(self.cols - 1, (p1 or 1) - 1))
        elif final == "d":
            self.r = max(0, min(self.rows - 1, (p1 or 1) - 1))
        elif final == "J":
            if p1 == 2:
                for rr in range(self.rows):
                    for cc in range(self.cols):
                        self._clear_cell(rr, cc)
                self.r, self.c = 0, 0
            elif p1 == 0:
                for cc in range(self.c, self.cols):
                    self._clear_cell(self.r, cc)
                for rr in range(self.r + 1, self.rows):
                    for cc in range(self.cols):
                        self._clear_cell(rr, cc)
        elif final == "K":
            if p1 == 0:
                for cc in range(self.c, self.cols):
                    self._clear_cell(self.r, cc)
            elif p1 == 1:
                for cc in range(0, self.c + 1):
                    self._clear_cell(self.r, cc)
            elif p1 == 2:
                for cc in range(self.cols):
                    self._clear_cell(self.r, cc)
        elif final == "m":
            self._apply_sgr(buf)
        elif final == "X":
            cnt = p1 or 1
            for k in range(cnt):
                if self.c + k < self.cols:
                    self._clear_cell(self.r, self.c + k)
        elif final == "b" and self.c > 0:
            prev = self.grid[self.r][self.c - 1]
            for _ in range(p1 or 1):
                if 0 <= self.c < self.cols:
                    self.grid[self.r][self.c] = prev
                    if self.c + 1 < self.cols:
                        self.c += 1

    def text(self) -> str:
        return "\n".join("".join(cell[0] for cell in row) for row in self.grid)

    def to_ansi(self) -> bytes:
        out = ["\x1b[2J\x1b[H"]
        prev_style = None
        for r in range(self.rows):
            out.append(f"\x1b[{r + 1};1H")
            for c in range(self.cols):
                ch, fg, bg, bold, rev = self.grid[r][c]
                style = (fg, bg, bold, rev)
                if style != prev_style:
                    fg_code = f"38;5;{fg}" if fg >= 16 else (f"{30 + fg}" if fg < 8 else f"{90 + (fg - 8)}")
                    bg_code = f"48;5;{bg}" if bg >= 16 else (f"{40 + bg}" if bg < 8 else f"{100 + (bg - 8)}")
                    seq = ["0"]
                    if bold:
                        seq.append("1")
                    if rev:
                        seq.append("7")
                    seq.append(fg_code)
                    seq.append(bg_code)
                    out.append(f"\x1b[{';'.join(seq)}m")
                    prev_style = style
                out.append(ch)
        out.append("\x1b[0m\r\n")
        return "".join(out).encode("utf-8")


def drain_and_wait(master_fd: int, scr: AnsiScreen, needle: str, timeout: float = 4.0) -> bytes:
    collected = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rlist, _, _ = select.select([master_fd], [], [], 0.05)
        if master_fd in rlist:
            try:
                chunk = os.read(master_fd, 8192)
            except OSError:
                break
            if not chunk:
                break
            collected.extend(chunk)
            scr.feed(chunk)
            if needle and needle in scr.text():
                # Drain any immediate trailing repaint bytes
                time.sleep(0.05)
                while True:
                    r2, _, _ = select.select([master_fd], [], [], 0.02)
                    if master_fd not in r2:
                        break
                    try:
                        more = os.read(master_fd, 8192)
                    except OSError:
                        break
                    if not more:
                        break
                    collected.extend(more)
                    scr.feed(more)
                return bytes(collected)
    raise AssertionError(f"Timed out waiting for {needle!r}; screen was:\n{scr.text()}")


def drain_quiet(master_fd: int, scr: AnsiScreen, settle: float = 0.2) -> None:
    deadline = time.monotonic() + settle
    while time.monotonic() < deadline:
        rlist, _, _ = select.select([master_fd], [], [], 0.04)
        if master_fd not in rlist:
            continue
        try:
            chunk = os.read(master_fd, 8192)
        except OSError:
            break
        if not chunk:
            break
        scr.feed(chunk)


def save_cap(cap_dir: str, name: str, scr: AnsiScreen) -> None:
    if cap_dir:
        with open(os.path.join(cap_dir, f"ncurses_{name}.ansi"), "wb") as f:
            f.write(scr.to_ansi())


def main() -> int:
    exe = sys.argv[1] if len(sys.argv) > 1 else "./build/oyk-node"
    cap_dir = os.environ.get("OYK_UI_CAPTURE_DIR", "")
    if cap_dir:
        os.makedirs(cap_dir, exist_ok=True)

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    udp_port = probe.getsockname()[1]
    probe.close()

    tmp = tempfile.mkdtemp(prefix="oyk_ncurses_ui_")
    try:
        cfg_path = os.path.join(tmp, "node.cfg")
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write(
                f"[node]\n"
                f"callsign = KR4OYK-7\n"
                f"alias = OYKNOD\n"
                f"logpath = {os.path.join(tmp, 'logs')}\n"
                f"banlist = {os.path.join(tmp, 'banlist.cfg')}\n"
                f"welcome = Welcome to KR4OYK-7\n\n"
                f"[bbs]\n"
                f"enabled = true\n"
                f"callsign = KR4OYK-1\n\n"
                f"[interface axudp0]\n"
                f"type = axudp\n"
                f"bind_host = 127.0.0.1\n"
                f"bind_port = {udp_port}\n\n"
                f"[peer remote0]\n"
                f"callsign = R3MOT-7\n"
                f"description = Remote PoPT Node\n"
                f"interface = axudp0\n"
                f"host = 12.34.56.78\n"
                f"allow_inbound = true\n"
                f"allow_outbound = true\n"
            )

        master_fd, slave_fd = pty.openpty()
        fcntl.ioctl(slave_fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        env = os.environ.copy()
        env["TERM"] = "xterm-256color"
        env["COLUMNS"] = str(COLS)
        env["LINES"] = str(ROWS)

        proc = subprocess.Popen(
            [exe, "--config", cfg_path],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            env=env,
            close_fds=True,
        )
        os.close(slave_fd)

        scr = AnsiScreen()
        try:
            drain_and_wait(master_fd, scr, "axudp0")

            # Inject a real AXUDP SABM so Main/Terminate/Auto-Learned Exit show live state
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.sendto(build_sabm_packet("KR4OYK-7", "W3TRM-1"), ("127.0.0.1", udp_port))
            sock.close()

            drain_and_wait(master_fd, scr, "W3TRM-1")
            drain_quiet(master_fd, scr, 0.25)
            save_cap(cap_dir, "main", scr)

            # 2. Terminate Connection modal
            os.write(master_fd, b"t")
            drain_and_wait(master_fd, scr, "Terminate Active Connection")
            drain_quiet(master_fd, scr, 0.15)
            term_txt = scr.text()
            for col_hdr in ("ID", "Callsign", "Port", "State"):
                assert col_hdr in term_txt, f"Missing column {col_hdr!r} in Terminate modal:\n{term_txt}"
            save_cap(cap_dir, "terminate", scr)
            os.write(master_fd, b"\x1b")
            drain_quiet(master_fd, scr, 0.15)

            # 3. Ban / Unban Callsign modal
            os.write(master_fd, b"b")
            drain_and_wait(master_fd, scr, "Ban / Unban Callsign")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "ban", scr)
            os.write(master_fd, b"\x1b")
            drain_quiet(master_fd, scr, 0.15)

            # 4. Settings (Node tab)
            os.write(master_fd, b"s")
            drain_and_wait(master_fd, scr, "Node Settings")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "settings", scr)

            # 5. Add Interface Wizard (Tab 4 -> Add -> step 3 with Same-Iface Fwd)
            os.write(master_fd, b"4")
            drain_and_wait(master_fd, scr, "axudp0")
            os.write(master_fd, b"a")
            drain_and_wait(master_fd, scr, "Add Interface")
            os.write(master_fd, b"\r\r")
            drain_and_wait(master_fd, scr, "Same-Iface Fwd")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "add_iface", scr)
            os.write(master_fd, b"\x1b")
            drain_and_wait(master_fd, scr, "Node Settings")

            # 6. Edit Interface Single View
            os.write(master_fd, b"e")
            drain_and_wait(master_fd, scr, "Edit Interface")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "edit_iface", scr)
            os.write(master_fd, b"\x1b")
            drain_and_wait(master_fd, scr, "Node Settings")

            # 7. Peers Tab
            os.write(master_fd, b"5")
            drain_and_wait(master_fd, scr, "R3MOT-7")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "peers", scr)

            # 8. Edit Peer Modal
            os.write(master_fd, b"e")
            drain_and_wait(master_fd, scr, "Edit Peer")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "edit_peer", scr)
            os.write(master_fd, b"\x1b")
            drain_and_wait(master_fd, scr, "Node Settings")
            os.write(master_fd, b"\x1b")
            drain_and_wait(master_fd, scr, "Tab/F9: Terminal")

            # 8b. Theme Settings Modal (live preview whole-screen repaint + [X] close)
            os.write(master_fd, b"h")
            drain_and_wait(master_fd, scr, "Theme Settings")
            drain_quiet(master_fd, scr, 0.15)
            theme_txt = scr.text()
            assert "[X]" in theme_txt.splitlines()[2], f"Missing [X] close button in Theme modal title bar:\n{theme_txt}"
            save_cap(cap_dir, "theme", scr)

            # Switch dark -> light and confirm BOTH background console & foreground modal change bg
            dark_bg_cell = scr.grid[10][1][2]
            dark_dlg_cell = scr.grid[5][7][2]
            os.write(master_fd, b"\x1bOC")
            drain_and_wait(master_fd, scr, "Previewing 'light'")
            drain_quiet(master_fd, scr, 0.1)
            light_bg_cell = scr.grid[10][1][2]
            light_dlg_cell = scr.grid[5][7][2]
            assert light_bg_cell != dark_bg_cell, f"Background console did not repaint on theme switch ({dark_bg_cell} -> {light_bg_cell})"
            assert light_dlg_cell != dark_dlg_cell, f"Foreground Theme modal did not repaint on theme switch ({dark_dlg_cell} -> {light_dlg_cell})"
            os.write(master_fd, b"\x1bOD")
            drain_and_wait(master_fd, scr, "Previewing 'dark'")
            drain_quiet(master_fd, scr, 0.1)
            assert scr.grid[10][1][2] == dark_bg_cell, "Background console did not revert on dark switch"
            assert scr.grid[5][7][2] == dark_dlg_cell, "Foreground modal did not revert on dark switch"

            os.write(master_fd, b"\x1b[<0;74;3M\x1b[<0;74;3m")
            drain_and_wait(master_fd, scr, "Radio Interfaces")
            assert "Theme Settings" not in scr.text(), f"Theme modal did not close on [X] click:\n{scr.text()}"

            # 8d. Local Packet Terminal: Ctrl+A B BBS, Ctrl+A Z Help Overlay, EOL cycle, CHAT / passthrough
            os.write(master_fd, b"\t")
            drain_and_wait(master_fd, scr, "LOCAL TERMINAL")
            assert "[EOL:CR]" in scr.text(), f"Missing [EOL:CR] indicator:\n{scr.text()}"
            os.write(master_fd, b"\x01b")
            drain_and_wait(master_fd, scr, "Packet BBS")
            os.write(master_fd, b"HELP\r")
            drain_and_wait(master_fd, scr, "BBS")
            drain_quiet(master_fd, scr, 0.2)
            save_cap(cap_dir, "terminal", scr)
            os.write(master_fd, b"\x01z")
            drain_and_wait(master_fd, scr, "Terminal Hotkey Help")
            os.write(master_fd, b"e")
            drain_and_wait(master_fd, scr, "[EOL:CR*]")
            os.write(master_fd, b"\x01e")
            drain_and_wait(master_fd, scr, "[EOL:LF*]")
            os.write(master_fd, b"\x01e")
            drain_and_wait(master_fd, scr, "[EOL:CRLF*]")
            os.write(master_fd, b"\x01e")
            drain_and_wait(master_fd, scr, "[EOL:CR]")
            os.write(master_fd, b"\x01t")
            drain_and_wait(master_fd, scr, "Packet Chat")
            os.write(master_fd, b"/h\r")
            drain_and_wait(master_fd, scr, "/q")
            os.write(master_fd, b"/u\r")
            drain_quiet(master_fd, scr, 0.15)
            os.write(master_fd, b"\x1b")
            drain_and_wait(master_fd, scr, "Tab/F9: Terminal")

            # 8e. Manage BBS Files Area modal
            os.write(master_fd, b"f")
            drain_and_wait(master_fd, scr, "Manage BBS Files Area")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "files", scr)
            os.write(master_fd, b"\x1b")
            drain_and_wait(master_fd, scr, "Tab/F9: Terminal")

            # 8c. Mouse support: SGR click on " Node " (row 1, col 4 1-based) opens Node menu,
            # wheel down selects item 2, right-click closes menu cleanly.
            os.write(master_fd, b"\x1b[<0;4;1M\x1b[<0;4;1m")
            drain_and_wait(master_fd, scr, "[A] Allow Incoming")
            os.write(master_fd, b"\x1b[<65;5;5M")
            os.write(master_fd, b"\x1b[<2;5;5M\x1b[<2;5;5m")
            drain_and_wait(master_fd, scr, "Radio Interfaces")
            assert "[A] Allow Incoming" not in scr.text(), f"Node menu did not close on right-click:\n{scr.text()}"

            # 9. Auto-Learned Peers Exit Modal + verify 73 de KR4OYK-7 stdout sign-off
            os.write(master_fd, b"q")
            drain_and_wait(master_fd, scr, "Auto-Learned Peers")
            drain_quiet(master_fd, scr, 0.15)
            save_cap(cap_dir, "exit", scr)
            os.write(master_fd, b"d")

            tail = bytearray()
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                rlist, _, _ = select.select([master_fd], [], [], 0.1)
                if master_fd in rlist:
                    try:
                        chunk = os.read(master_fd, 4096)
                    except OSError:
                        break
                    if not chunk:
                        break
                    tail.extend(chunk)
                elif proc.poll() is not None:
                    break
            rc = proc.wait(timeout=3)
            tail_txt = tail.decode("utf-8", errors="replace")
            assert rc == 0, f"Unexpected ncurses exit code: {rc}\nOutput tail:\n{tail_txt[-4000:]}"
            assert "73 de KR4OYK-7" in tail_txt, f"Missing sign-off in stdout: {tail_txt!r}"
        finally:
            os.close(master_fd)
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=3)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("[PASS] test_ncurses_ui.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
