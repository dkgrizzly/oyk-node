#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
# Remote build, test, and OSF/Motif 1.2 UI capture runner for Motorola 88K SVR4.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

DO_BUILD=0
DO_TESTS=0
CAPTURE_DIR=""
REMOTE_DIR="${OYK_M88K_REMOTE_DIR:-\$HOME/oyk-node}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)      DO_BUILD=1; shift ;;
        --unit-tests) DO_TESTS=1; shift ;;
        --capture)    CAPTURE_DIR="$2"; shift 2 ;;
        --remote-dir) REMOTE_DIR="$2"; shift 2 ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

: "${OYK_M88K_HOST:?Set OYK_M88K_HOST in environment}"
: "${OYK_M88K_USER:?Set OYK_M88K_USER in environment}"
: "${OYK_M88K_PASS:?Set OYK_M88K_PASS in environment}"

if [[ -n "${CAPTURE_DIR}" ]]; then
    mkdir -p "${CAPTURE_DIR}"
fi

TMP_WORK="$(mktemp -d /tmp/oyk_m88k_remote.XXXXXX)"
XVFB_PID=""
cleanup() {
    if [[ -n "${XVFB_PID}" ]] && kill -0 "${XVFB_PID}" 2>/dev/null; then
        kill "${XVFB_PID}" 2>/dev/null || true
    fi
    rm -rf "${TMP_WORK}"
}
trap cleanup EXIT

tar -cf "${TMP_WORK}/oyk_src.tar" -C "${ROOT_DIR}" \
    ports/sysv/sysv_core.h ports/sysv/sysv_core.c \
    ports/sysv/sysv_tui.h ports/sysv/sysv_tui.c \
    ports/sysv/sysv_net.h ports/sysv/sysv_net.c \
    ports/sysv/sysv_logo_data.h \
    ports/sysv/sysv_motif_ui.h ports/sysv/sysv_motif_ui.c \
    ports/m88k/m88k_tui_main.c ports/m88k/m88k_motif_main.c \
    ports/m88k/test_m88k_native.c ports/m88k/Makefile

XVFB_DISP=""
if [[ -n "${CAPTURE_DIR}" ]]; then
    XVFB_DISP=":99"
    Xvfb "${XVFB_DISP}" -screen 0 900x660x24 -listen tcp -ac >/dev/null 2>&1 &
    XVFB_PID=$!
    sleep 1.2
fi

python3 - "${OYK_M88K_HOST}" "${OYK_M88K_USER}" "${OYK_M88K_PASS}" \
    "${REMOTE_DIR}" "${TMP_WORK}" "${DO_BUILD}" "${DO_TESTS}" \
    "${CAPTURE_DIR}" "${XVFB_DISP}" <<'PYEOF'
import os
import re
import socket
import subprocess
import sys
import threading
import time

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

host, user, passwd, remote_dir, ftp_root, do_build, do_tests, cap_dir, xdisp = sys.argv[1:]
do_build = int(do_build)
do_tests = int(do_tests)

def detect_ftp_host(target_host):
    override = os.environ.get("OYK_FTP_HOST", "").strip()
    if override:
        return override
    ps = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        ps.connect((target_host, 9))
        return ps.getsockname()[0]
    finally:
        ps.close()

ftp_host = detect_ftp_host(host)
ftp_server = None
ftp_port = 0
if do_build:
    auth = DummyAuthorizer()
    auth.add_anonymous(ftp_root, perm="elr")
    handler = FTPHandler
    handler.authorizer = auth
    handler.permit_foreign_addresses = True
    handler.passive_ports = range(40000, 40100)
    ftp_server = FTPServer(("0.0.0.0", 0), handler)
    ftp_port = ftp_server.socket.getsockname()[1]
    t = threading.Thread(target=ftp_server.serve_forever, kwargs={"timeout": 0.2}, daemon=True)
    t.start()
    print(f"FTP server listening for M88K via {ftp_host}:{ftp_port} serving {ftp_root}", flush=True)

s = socket.create_connection((host, 23), timeout=20)
s.settimeout(2.0)

def recv_until(substrs, timeout=30.0, regex=None):
    end = time.time() + timeout
    acc = b""
    while time.time() < end:
        try:
            d = s.recv(4096)
            if not d:
                break
            i = 0
            clean = bytearray()
            while i < len(d):
                if d[i] == 255 and i + 2 < len(d):
                    cmd, opt = d[i + 1], d[i + 2]
                    if cmd == 253:
                        s.sendall(bytes([255, 252, opt]))
                    elif cmd == 251:
                        s.sendall(bytes([255, 254, opt]))
                    i += 3
                else:
                    clean.append(d[i])
                    i += 1
            acc += bytes(clean)
            txt = acc.decode("latin1", "replace")
            if regex is not None and re.search(regex, txt):
                return txt
            for pat in substrs:
                if pat in txt:
                    return txt
        except socket.timeout:
            txt = acc.decode("latin1", "replace")
            if regex is not None and re.search(regex, txt):
                return txt
            for pat in substrs:
                if pat in txt:
                    return txt
    return acc.decode("latin1", "replace")

def send_cmd(cmd, timeout=180.0):
    sep = "\n" if "\n" in cmd else "; "
    s.sendall((cmd + sep + 'echo "__M88K_RC="$?\n').encode())
    out = recv_until([], timeout=timeout, regex=r"__M88K_RC=(\d+)")
    m = re.search(r"__M88K_RC=(\d+)", out)
    if not m:
        raise RuntimeError(f"Timeout waiting for __M88K_RC after: {cmd}\nGot:\n{out}")
    code = int(m.group(1))
    print(out, flush=True)
    if code != 0:
        raise RuntimeError(f"Remote command failed (rc={code}): {cmd}")
    return out

try:
    recv_until(["login:"])
    s.sendall((user + "\n").encode())
    recv_until(["Password:"])
    s.sendall((passwd + "\n").encode())
    recv_until(["$", "~ $"])
    s.sendall(b"stty erase ^H kill ^U; PS1='M88K_READY> '\n")
    recv_until([], regex=r"(?m)^M88K_READY> ")
    send_cmd("PATH=/bin:/usr/bin:/usr/ccs/bin:/usr/local/bin; export PATH; stty -echo")

    if do_build:
        send_cmd(f"(test -d {remote_dir} || mkdir {remote_dir}) && cd {remote_dir} && rm -rf ports/sysv ports/m88k oyk_src.tar")
        ftp_script = (
            f"cd {remote_dir} && ftp -n << '__OYK_FTP__'\n"
            f"open {ftp_host} {ftp_port}\n"
            f"user anonymous oyk\n"
            f"binary\n"
            f"get oyk_src.tar\n"
            f"quit\n"
            f"__OYK_FTP__\n"
        )
        s.sendall(ftp_script.encode())
        ftp_out = recv_until([], timeout=60.0, regex=r"(?m)^M88K_READY> ")
        print(ftp_out, flush=True)
        send_cmd(f"cd {remote_dir} && test -s oyk_src.tar && tar xf oyk_src.tar && rm -f oyk_src.tar")
        send_cmd(f"cd {remote_dir}/ports/m88k && make clean && make", timeout=240.0)

    if do_tests:
        send_cmd(f"cd {remote_dir}/ports/m88k && make test && ./oyk-node-m88k --headless", timeout=120.0)

    if cap_dir:
        disp = f"{ftp_host}{xdisp}"
        mwm_out = send_cmd(f"DISPLAY={disp} /usr/bin/X11/mwm >/dev/null 2>&1 & echo MWM_PID=$!")
        mwm_pid = ""
        for ln in mwm_out.splitlines():
            if "MWM_PID=" in ln:
                mwm_pid = ln.split("MWM_PID=", 1)[1].strip().split()[0]
        time.sleep(1.5)
        s.sendall(f"cd {remote_dir}/ports/m88k && OYK_UI_TEST_MODE=1 DISPLAY={disp} ./oyk-node-m88k-motif\n".encode())
        seen = 0
        buf = ""
        deadline = time.time() + 180.0
        while seen < 23 and time.time() < deadline:
            try:
                d = s.recv(4096)
                if not d:
                    break
                buf += d.decode("latin1", "replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip("\r ")
                    if line.startswith("OYK_CAPTURE_READY "):
                        scr = line.split(" ", 1)[1].strip()
                        out_png = os.path.join(cap_dir, f"m88k_motif_{scr}.png")
                        time.sleep(0.25)
                        subprocess.run(["import", "-display", xdisp, "-window", "root", out_png], check=True)
                        print(f"[CAPTURED] {out_png}", flush=True)
                        seen += 1
                        s.sendall(b"\n")
            except socket.timeout:
                continue
        if seen < 23:
            raise RuntimeError(f"Only captured {seen}/23 Motif screens")
        recv_until(["M88K_READY> "], timeout=15.0)
        if mwm_pid:
            send_cmd(f"kill {mwm_pid} 2>/dev/null || true")

    s.sendall(b"exit\n")
finally:
    s.close()
    if ftp_server is not None:
        ftp_server.close_all()
PYEOF
