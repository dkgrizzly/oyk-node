#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
# Remote build & native test runner for AT&T 3B2 SVR3.2 via telnet + local FTP.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

DO_BUILD=0
DO_TESTS=0
REMOTE_DIR="${OYK_3B2_REMOTE_DIR:-\$HOME/oyk-node}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)      DO_BUILD=1; shift ;;
        --unit-tests) DO_TESTS=1; shift ;;
        --remote-dir) REMOTE_DIR="$2"; shift 2 ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

: "${OYK_3B2_HOST:?Set OYK_3B2_HOST in environment}"
: "${OYK_3B2_PORT:?Set OYK_3B2_PORT in environment}"
: "${OYK_3B2_USER:?Set OYK_3B2_USER in environment}"

TMP_WORK="$(mktemp -d /tmp/oyk_3b2_remote.XXXXXX)"
trap 'rm -rf "${TMP_WORK}"' EXIT

tar -czf "${TMP_WORK}/oyk_src.tar.gz" -C "${ROOT_DIR}" \
    ports/sysv/sysv_core.h ports/sysv/sysv_core.c \
    ports/sysv/sysv_tui.h ports/sysv/sysv_tui.c \
    ports/sysv/sysv_net.h ports/sysv/sysv_net.c \
    ports/sysv/sysv_logo_data.h \
    ports/att3b2/att3b2_tui_main.c ports/att3b2/test_3b2_native.c ports/att3b2/Makefile

python3 - "${OYK_3B2_HOST}" "${OYK_3B2_PORT}" "${OYK_3B2_USER}" \
    "${REMOTE_DIR}" "${TMP_WORK}" "${DO_BUILD}" "${DO_TESTS}" <<'PYEOF'
import os
import re
import socket
import sys
import threading
import time

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

host, port_s, user, remote_dir, ftp_root, do_build, do_tests = sys.argv[1:]
port = int(port_s)
do_build = int(do_build)
do_tests = int(do_tests)

def detect_ftp_host(target_host):
    override = os.environ.get("OYK_FTP_HOST", "").strip()
    if override:
        return override
    if target_host in ("127.0.0.1", "localhost", "::1"):
        # Local 3B2 simulator uses SLiRP user-mode NAT whose host gateway is 10.0.2.2
        return "10.0.2.2"
    ps = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        ps.connect((target_host, 9))
        return ps.getsockname()[0]
    finally:
        ps.close()

ftp_server = None
ftp_port = 0
ftp_host = detect_ftp_host(host)
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
    print(f"FTP server listening for 3B2 via {ftp_host}:{ftp_port} serving {ftp_root}", flush=True)

s = socket.create_connection((host, port), timeout=20)
s.settimeout(2.0)

def send_paced(text, chunk=2, delay=0.015):
    data = text if isinstance(text, bytes) else text.encode("ascii")
    for i in range(0, len(data), chunk):
        s.sendall(data[i:i + chunk])
        time.sleep(delay)

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
    send_paced(cmd + sep + 'echo "__3B2_RC="$?\n')
    out = recv_until([], timeout=timeout, regex=r"__3B2_RC=(\d+)")
    m = re.search(r"__3B2_RC=(\d+)", out)
    if not m:
        raise RuntimeError(f"Timeout waiting for __3B2_RC after: {cmd}\nGot:\n{out}")
    code = int(m.group(1))
    print(out, flush=True)
    if code != 0:
        raise RuntimeError(f"Remote command failed (rc={code}): {cmd}")
    return out

PASV_GET_C = r"""#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/in.h>
extern int atoi();
extern int read();
extern int write();
extern int close();
static int dial(char *ip, int port) {
    struct sockaddr_in a; unsigned char *p; int s; int b[4];
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    memset((char *)&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    p = (unsigned char *)&a.sin_port;
    p[0] = (unsigned char)((port >> 8) & 255);
    p[1] = (unsigned char)(port & 255);
    sscanf(ip, "%d.%d.%d.%d", &b[0], &b[1], &b[2], &b[3]);
    p = (unsigned char *)&a.sin_addr;
    p[0]=b[0]; p[1]=b[1]; p[2]=b[2]; p[3]=b[3];
    if (connect(s, (struct sockaddr *)&a, sizeof(a)) < 0) { close(s); return -1; }
    return s;
}
static int rdline(int s, char *buf, int cap) {
    int n = 0; char c;
    while (n + 1 < cap && read(s, &c, 1) == 1) {
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = 0; return n;
}
static int fcmd(int s, char *out, char *buf, int cap) {
    if (out) write(s, out, strlen(out));
    while (rdline(s, buf, cap) > 0) {
        if (buf[0]>='0' && buf[0]<='9' && buf[1]>='0' && buf[1]<='9' &&
            buf[2]>='0' && buf[2]<='9' && buf[3]==' ') return atoi(buf);
    }
    return -1;
}
int main(int argc, char **argv) {
    int c, d, p1, p2, n; char buf[512], rq[256]; FILE *f; char *q; int h[4];
    if (argc < 5) return 2;
    c = dial(argv[1], atoi(argv[2])); if (c < 0) return 3;
    if (fcmd(c, 0, buf, 512) != 220) return 4;
    if (fcmd(c, "USER anonymous\r\n", buf, 512) != 331) return 5;
    if (fcmd(c, "PASS oyk\r\n", buf, 512) != 230) return 6;
    if (fcmd(c, "TYPE I\r\n", buf, 512) != 200) return 7;
    if (fcmd(c, "PASV\r\n", buf, 512) != 227) return 8;
    q = strchr(buf, '('); if (!q) return 9;
    if (sscanf(q, "(%d,%d,%d,%d,%d,%d)", &h[0],&h[1],&h[2],&h[3],&p1,&p2)!=6) return 10;
    d = dial(argv[1], p1*256 + p2); if (d < 0) return 11;
    sprintf(rq, "RETR %s\r\n", argv[3]);
    write(c, rq, strlen(rq));
    if (fcmd(c, 0, buf, 512) >= 400) return 12;
    f = fopen(argv[4], "w"); if (!f) return 13;
    while ((n = read(d, buf, 512)) > 0) fwrite(buf, 1, n, f);
    fclose(f); close(d);
    fcmd(c, 0, buf, 512); fcmd(c, "QUIT\r\n", buf, 512); close(c);
    return 0;
}
"""

try:
    recv_until(["Connected to", "login:", "$ ", "B2_READY> ", "> "], timeout=4.0)
    time.sleep(0.5)
    send_paced("\n", chunk=1, delay=0.02)
    banner = recv_until(["login:", "$ ", "B2_READY> ", "> "], timeout=6.0)
    if "login:" in banner:
        send_paced(user + "\n", chunk=2, delay=0.02)
        recv_until(["$ ", "\n$"], timeout=15.0)
    elif "> " in banner and "$ " not in banner and "B2_READY> " not in banner:
        send_paced("\x03\x04\n", chunk=1, delay=0.05)
        b2 = recv_until(["$ ", "B2_READY> ", "login:"], timeout=8.0)
        if "login:" in b2:
            send_paced(user + "\n", chunk=2, delay=0.02)
            recv_until(["$ ", "\n$"], timeout=15.0)
    elif "$ " not in banner and "B2_READY> " not in banner:
        send_paced("\x03\x04\n", chunk=1, delay=0.05)
        recv_until(["login:"], timeout=10.0)
        send_paced(user + "\n", chunk=2, delay=0.02)
        recv_until(["$ ", "\n$"], timeout=15.0)
    time.sleep(0.8)
    recv_until([], timeout=0.5)
    send_paced("stty erase \x08 kill \x15\n", chunk=2, delay=0.02)
    recv_until(["$ ", "B2_READY> "], timeout=5.0)
    send_paced("PS1='B2_READY> '\n", chunk=2, delay=0.02)
    recv_until(["B2_READY> "], timeout=10.0)
    send_cmd("PATH=/bin:/usr/bin:/usr/local/bin; export PATH; stty -echo")

    if do_build:
        send_cmd(f"(test -d {remote_dir} || mkdir {remote_dir}) && cd {remote_dir} && rm -rf ports/sysv ports/att3b2 oyk_src.tar.gz")
        chk = send_cmd(f"test -x {remote_dir}/oyk_ftpget && echo HAVE_GET || echo NEED_GET")
        if "NEED_GET" in chk:
            hex_s = PASV_GET_C.strip().encode("ascii").hex()
            send_cmd(f"rm -f {remote_dir}/_oyk_ftpget.c {remote_dir}/_f.hex")
            for idx in range(0, len(hex_s), 64):
                send_cmd(f"echo {hex_s[idx:idx + 64]} >> {remote_dir}/_f.hex", timeout=30.0)
            send_cmd(
                f"cd {remote_dir} && /usr/local/bin/perl -e "
                f"'open(O,\">_oyk_ftpget.c\");while(<>){{s/[^0-9a-f]//g;print O pack(\"H*\",$_)}}' _f.hex "
                f"&& rm -f _f.hex && /usr/local/bin/gcc -O -o oyk_ftpget _oyk_ftpget.c -lnet -lnsl_s && rm -f _oyk_ftpget.c",
                timeout=120.0,
            )

        send_cmd(f"cd {remote_dir} && ./oyk_ftpget {ftp_host} {ftp_port} oyk_src.tar.gz oyk_src.tar.gz", timeout=120.0)
        send_cmd(f"cd {remote_dir} && test -s oyk_src.tar.gz && /usr/local/bin/gzip -dc oyk_src.tar.gz | /usr/local/bin/tar xf - && rm -f oyk_src.tar.gz")
        send_cmd(f"cd {remote_dir}/ports/att3b2 && /usr/local/bin/make clean && /usr/local/bin/make", timeout=300.0)

    if do_tests:
        send_cmd(f"cd {remote_dir}/ports/att3b2 && /usr/local/bin/make test && ./oyk-node-3b2 --headless && ./oyk-node-3b2 --ansi > /dev/null", timeout=120.0)

    send_paced("exit\n")
finally:
    s.close()
    if ftp_server is not None:
        ftp_server.close_all()
PYEOF
