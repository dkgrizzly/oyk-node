#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DO_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)
            DO_BUILD=1
            shift
            ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 2
            ;;
        *)
            break
            ;;
    esac
done

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 [--build] <output_dir>" >&2
    exit 2
fi

OUT_DIR="$1"
mkdir -p "${OUT_DIR}"

VMS_HOST="${OYK_VMS_HOST:?Set OYK_VMS_HOST to the OpenVMS host/IP}"
VMS_USER="${OYK_VMS_USER:?Set OYK_VMS_USER to the OpenVMS username}"
VMS_PASS="${OYK_VMS_PASS:?Set OYK_VMS_PASS to the OpenVMS password}"
VMS_WORK="${OYK_VMS_WORK_DIR:-[.OYK.PORTS.OPENVMS]}"
DISP="${OYK_VMS_DISPLAY_NUM:-}"

if [[ -n "${OYK_VMS_DISPLAY_HOST:-}" ]]; then
    DISP_HOST="${OYK_VMS_DISPLAY_HOST}"
else
    DISP_HOST="$(ip -4 route get "${VMS_HOST}" 2>/dev/null | sed -n 's/.*src \([0-9.]*\).*/\1/p' | head -n 1 || true)"
    if [[ -z "${DISP_HOST}" ]]; then
        echo "Set OYK_VMS_DISPLAY_HOST to this host's IP reachable by ${VMS_HOST}." >&2
        exit 2
    fi
fi

for cmd in Xvfb xdpyinfo xwininfo xprop import python3; do
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "Missing required host X11/capture tool: ${cmd}" >&2
        exit 2
    fi
done

if [[ -z "${DISP}" ]]; then
    for cand in $(seq 99 125); do
        if [[ ! -e "/tmp/.X${cand}-lock" && ! -S "/tmp/.X11-unix/X${cand}" ]]; then
            DISP="${cand}"
            break
        fi
    done
fi
: "${DISP:=99}"

XVFB_PID=""
cleanup() {
    if [[ -n "${XVFB_PID}" ]] && kill -0 "${XVFB_PID}" >/dev/null 2>&1; then
        kill "${XVFB_PID}" >/dev/null 2>&1 || true
        wait "${XVFB_PID}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

Xvfb ":${DISP}" -screen 0 1024x768x24 -ac -listen tcp >/tmp/oyk_vms_xvfb.log 2>&1 &
XVFB_PID=$!

for _ in $(seq 1 40); do
    if DISPLAY=":${DISP}" xdpyinfo >/dev/null 2>&1; then
        break
    fi
    sleep 0.15
done
DISPLAY=":${DISP}" xdpyinfo >/dev/null 2>&1

# Configure Motif WM defaults before DECW$MWM starts so windows/dialogs
# place automatically without interactive mouse clicks.
XRES_DATA=$'Mwm*interactivePlacement: False\nMwm*clientAutoPlace: True\nMwm*positionIsFrame: True\nMwm*positionOnScreen: True\nMwm*autoRaiseDelay: 0\nMwm*keyboardFocusPolicy: pointer\nMwm*startupKeyFocus: True\n'
if command -v xrdb >/dev/null 2>&1; then
    printf '%s' "${XRES_DATA}" | DISPLAY=":${DISP}" xrdb -merge
else
    DISPLAY=":${DISP}" xprop -root -f RESOURCE_MANAGER 8s -set RESOURCE_MANAGER "${XRES_DATA}"
fi

export DISPLAY=":${DISP}"
export OYK_VMS_DISPLAY_NUM="${DISP}"
export OYK_VMS_DISPLAY_HOST="${DISP_HOST}"
export OYK_VMS_WORK_DIR="${VMS_WORK}"

python3 - "${ROOT_DIR}" "${OUT_DIR}" "${DO_BUILD}" <<'PY'
import ftplib
import os
import re
import socket
import subprocess
import sys
import time

root_dir = sys.argv[1]
out_dir = sys.argv[2]
do_build = sys.argv[3] == "1"

vms_host = os.environ["OYK_VMS_HOST"]
vms_user = os.environ["OYK_VMS_USER"]
vms_pass = os.environ["OYK_VMS_PASS"]
disp_host = os.environ["OYK_VMS_DISPLAY_HOST"]
disp_num = int(os.environ["OYK_VMS_DISPLAY_NUM"])
work_dir = os.environ["OYK_VMS_WORK_DIR"]
disp = f":{disp_num}"


def ftp_upload_sources():
    import io
    upload_dirs = [
        ("ports/sysv", "oyk/ports/sysv", (".c", ".h")),
        ("ports/openvms", "oyk/ports/openvms", (".c", ".h", ".mms", ".opt", ".com")),
    ]
    ftp = ftplib.FTP(vms_host, timeout=30)
    ftp.login(vms_user, vms_pass)
    home_pwd = ftp.pwd()
    for local_sub, rdir, exts in upload_dirs:
        ftp.cwd(home_pwd)
        for seg in rdir.split("/"):
            try:
                ftp.cwd(seg)
            except ftplib.error_perm:
                ftp.mkd(seg)
                ftp.cwd(seg)
        src_dir = os.path.join(root_dir, local_sub)
        for fname in sorted(os.listdir(src_dir)):
            if not fname.lower().endswith(exts):
                continue
            local_path = os.path.join(src_dir, fname)
            if not os.path.isfile(local_path):
                continue
            with open(local_path, "rb") as fh:
                lines = fh.read().decode("utf-8").splitlines()
            payload = ("\r\n".join(lines) + "\r\n").encode("ascii", errors="replace")
            ftp.storlines(f"STOR {fname}", io.BytesIO(payload))
    ftp.quit()


class VmsTelnet:
    IAC = 255
    DONT = 254
    DO = 253
    WONT = 252
    WILL = 251
    SB = 250
    SE = 240

    def __init__(self, host, port=23, timeout=15):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(0.25)
        self.buf = b""

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def _strip_iac(self, data):
        out = bytearray()
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b == self.IAC and i + 1 < n:
                cmd = data[i + 1]
                if cmd in (self.DO, self.DONT, self.WILL, self.WONT) and i + 2 < n:
                    opt = data[i + 2]
                    if cmd == self.DO:
                        self.sock.sendall(bytes([self.IAC, self.WONT, opt]))
                    elif cmd == self.WILL:
                        self.sock.sendall(bytes([self.IAC, self.DONT, opt]))
                    i += 3
                    continue
                if cmd == self.SB:
                    j = i + 2
                    while j + 1 < n and not (data[j] == self.IAC and data[j + 1] == self.SE):
                        j += 1
                    i = j + 2
                    continue
                i += 2
                continue
            out.append(b)
            i += 1
        return bytes(out)

    def read_chunk(self):
        try:
            raw = self.sock.recv(4096)
            if not raw:
                return ""
            cleaned = self._strip_iac(raw)
            text = cleaned.decode("latin-1", errors="replace")
            return text
        except socket.timeout:
            return ""

    def read_until(self, patterns, timeout=30.0):
        acc = ""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self.read_chunk()
            if chunk:
                acc += chunk
                for p in patterns:
                    if p in acc:
                        return acc, p
            else:
                time.sleep(0.05)
        raise TimeoutError(f"Timed out waiting for {patterns}; got tail={acc[-400:]!r}")

    def send_line(self, line):
        self.sock.sendall((line + "\r\n").encode("ascii"))


def capture_window_union(out_path):
    tree = subprocess.check_output(
        ["xwininfo", "-display", disp, "-root", "-children"],
        text=True,
    )
    rect_re = re.compile(
        r'^\s*(0x[0-9a-fA-F]+)\s+.*?\s+(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)',
    )
    boxes = []
    for line in tree.splitlines():
        m = rect_re.match(line)
        if not m:
            continue
        wid = m.group(1)
        w = int(m.group(2))
        h = int(m.group(3))
        ax = int(m.group(6))
        ay = int(m.group(7))
        if w < 40 or h < 40:
            continue
        if w == 1024 and h == 768 and ax == 0 and ay == 0:
            continue
        info = subprocess.check_output(
            ["xwininfo", "-display", disp, "-id", wid],
            text=True,
        )
        if "Map State: IsViewable" not in info:
            continue
        bw = 0
        bm = re.search(r"Border width:\s*(\d+)", info)
        if bm:
            bw = int(bm.group(1))
        boxes.append((ax, ay, ax + w + 2 * bw, ay + h + 2 * bw))
    if not boxes:
        raise RuntimeError(f"No viewable X11 windows found for {out_path}")
    min_x = max(0, min(b[0] for b in boxes))
    min_y = max(0, min(b[1] for b in boxes))
    max_r = min(1024, max(b[2] for b in boxes))
    max_b = min(768, max(b[3] for b in boxes))
    cw = max(1, max_r - min_x)
    ch = max(1, max_b - min_y)
    subprocess.run(
        [
            "import",
            "-display", disp,
            "-window", "root",
            "-crop", f"{cw}x{ch}+{min_x}+{min_y}",
            "+repage",
            f"PNG24:{out_path}",
        ],
        check=True,
    )


if do_build:
    print("Uploading OpenVMS/SysV Motif sources via FTP...")
    ftp_upload_sources()

tn = VmsTelnet(vms_host)
try:
    tn.read_until(["Username:"], timeout=20)
    tn.send_line(vms_user)
    tn.read_until(["Password:"], timeout=20)
    tn.send_line(vms_pass)
    tn.read_until(["$ "], timeout=25)

    tn.send_line("SET TERMINAL/NOWRAP/WIDTH=132")
    tn.read_until(["$ "], timeout=10)
    tn.send_line(f"SET DEFAULT {work_dir}")
    tn.read_until(["$ "], timeout=10)

    if do_build:
        print("Building OYK_MOTIF.EXE on OpenVMS via MMS...")
        tn.send_line("DELETE *.OBJ;*,OYK_MOTIF.EXE;*")
        tn.read_until(["$ "], timeout=20)
        tn.send_line("MMS /MACRO=(ARCH=VAX) OYK_MOTIF.EXE")
        out, _ = tn.read_until(["$ "], timeout=180)
        if "%MMS-F-" in out or "%CC-E-" in out or "%CC-F-" in out or "%LINK-F-" in out or "%LINK-E-" in out:
            raise RuntimeError(f"OpenVMS MMS build failed:\n{out}")

    tn.send_line(f"SET DISPLAY/CREATE/NODE={disp_host}/TRANSPORT=TCPIP/SERVER={disp_num}/SCREEN=0")
    tn.read_until(["$ "], timeout=15)
    tn.send_line("DEFINE/NOLOG OYK_UI_TEST_MODE \"1\"")
    tn.read_until(["$ "], timeout=10)

    print(f"Starting DECwindows Motif WM (DECW$MWM.EXE) on {disp_host}:{disp_num}...")
    tn.send_line("SPAWN/NOWAIT/OUTPUT=NL: RUN SYS$SYSTEM:DECW$MWM.EXE")
    tn.read_until(["$ "], timeout=15)

    wm_ready = False
    for _ in range(80):
        res = subprocess.run(
            ["xprop", "-display", disp, "-root", "_MOTIF_WM_INFO"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
        if res.returncode == 0 and "_MOTIF_WM_INFO" in res.stdout:
            wm_ready = True
            break
        time.sleep(0.25)
    if not wm_ready:
        raise RuntimeError("DECW$MWM did not register _MOTIF_WM_INFO on Xvfb display")

    print("Launching OYK_MOTIF.EXE on OpenVMS...")
    tn.send_line('OYKMFT == "$" + F$ENVIRONMENT("DEFAULT") + "OYK_MOTIF.EXE"')
    tn.read_until(["$ "], timeout=10)
    tn.send_line("OYKMFT --ui-test")

    acc = ""
    captured = 0
    seen = set()
    deadline = time.monotonic() + 180.0
    ready_re = re.compile(r"OYK_CAPTURE_(PAINT|READY)\s+([A-Za-z0-9_]+)")
    while time.monotonic() < deadline:
        chunk = tn.read_chunk()
        if chunk:
            acc += chunk
            while True:
                m = ready_re.search(acc)
                if not m:
                    break
                phase = m.group(1)
                flow = m.group(2)
                acc = acc[m.end():]
                if phase == "PAINT":
                    time.sleep(0.35)
                    tn.send_line("")
                else:
                    time.sleep(0.15)
                    out_png = os.path.join(out_dir, f"openvms_motif_{flow}.png")
                    capture_window_union(out_png)
                    seen.add(flow)
                    captured += 1
                    print(f"Captured openvms_motif_{flow}.png ({captured}/23)", flush=True)
                    tn.send_line("")
            if "$ " in acc and captured >= 23:
                break
        else:
            time.sleep(0.05)

    if captured < 23:
        raise RuntimeError(f"Only captured {captured}/23 OpenVMS Motif screens (seen={sorted(seen)}); tail={acc[-500:]!r}")

    tn.send_line("LOGOUT")
finally:
    tn.close()
PY
