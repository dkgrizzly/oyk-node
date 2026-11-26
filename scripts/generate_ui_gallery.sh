#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_IMG_DIR="${ROOT_DIR}/docs/user/images/ui-gallery"
OUT_MD="${ROOT_DIR}/docs/user/ui-gallery.md"
RENDERER="${ROOT_DIR}/scripts/render_ui_png.py"

TMP_WORK="$(mktemp -d /tmp/oyk_ui_gallery.XXXXXX)"
TMP_CAP="${TMP_WORK}/captures"
mkdir -p "${TMP_CAP}" "${OUT_IMG_DIR}" "${TMP_WORK}/logs"
trap 'rm -rf "${TMP_WORK}"' EXIT

if [[ ! -x "${BUILD_DIR}/oyk-node" || ! -x "${BUILD_DIR}/oyk_portable_tests" || ! -x "${BUILD_DIR}/oyk_tinytmux_tests" ]]; then
    echo "Building host binaries in ${BUILD_DIR}..."
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
    cmake --build "${BUILD_DIR}" -j
fi

# Pick a free UDP port for GUI demo sessions
UDP_PORT="$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')"

cat > "${TMP_WORK}/node.cfg" <<EOF
[node]
callsign = KR4OYK-7
alias = OYKNOD
logpath = ${TMP_WORK}/logs
banlist = ${TMP_WORK}/banlist.cfg
welcome = Welcome to KR4OYK-7 Packet Node

[interface axudp0]
type = axudp
bind_host = 127.0.0.1
bind_port = ${UDP_PORT}
netmask = 255.0.0.0
allow_inbound = true
allow_outbound = true
allow_same_interface = false

[peer remote0]
callsign = R3MOT-7
description = Remote PoPT Node
interface = axudp0
host = 12.34.56.78
port = 10093
allow_inbound = true
allow_outbound = true
EOF

echo "[1/5] Capturing POSIX ncurses screens..."
OYK_UI_CAPTURE_DIR="${TMP_CAP}" python3 "${ROOT_DIR}/tests/test_ncurses_ui.py" "${BUILD_DIR}/oyk-node"

echo "[2/5] Capturing MS-DOS 80x25 text-mode screens..."
OYK_UI_CAPTURE_DIR="${TMP_CAP}" "${BUILD_DIR}/oyk_portable_tests"

echo "[3/5] Capturing Teensy 4.1 / TinyTMUX ANSI screens..."
OYK_UI_CAPTURE_DIR="${TMP_CAP}" "${BUILD_DIR}/oyk_tinytmux_tests"

echo "[4/5] Capturing X11 GTK+ 2.0 screens under Xvfb..."
if [[ -x "${BUILD_DIR}/oyk-node-gtk" ]] && command -v xvfb-run >/dev/null 2>&1; then
    timeout 45 xvfb-run -a -s "-screen 0 1280x960x24" \
        env OYK_UI_TEST_MODE=1 OYK_UI_CAPTURE_DIR="${TMP_CAP}" \
        "${BUILD_DIR}/oyk-node-gtk" --config "${TMP_WORK}/node.cfg"
else
    echo "Warning: oyk-node-gtk or xvfb-run unavailable; skipping GTK+ 2.0 capture." >&2
fi

echo "[5/5] Capturing Windows GUI screens under Xvfb + Wine..."
WIN_EXE=""
WIN_PREFIX=""
if [[ -f "${BUILD_DIR}/oykw64.exe" && -d "${HOME}/.wine64" ]]; then
    WIN_EXE="${BUILD_DIR}/oykw64.exe"
    WIN_PREFIX="${HOME}/.wine64"
elif [[ -f "${BUILD_DIR}/oykw32.exe" && -d "${HOME}/.wine32" ]]; then
    WIN_EXE="${BUILD_DIR}/oykw32.exe"
    WIN_PREFIX="${HOME}/.wine32"
fi

if [[ -n "${WIN_EXE}" ]] && command -v xvfb-run >/dev/null 2>&1 && command -v wine >/dev/null 2>&1; then
    cat > "${TMP_WORK}/win_node.cfg" <<EOF
[node]
callsign = KR4OYK-7
alias = OYKNOD
logpath = logs
banlist = banlist.cfg
welcome = Welcome to KR4OYK-7 Packet Node

[interface axudp0]
type = axudp
bind_host = 127.0.0.1
bind_port = ${UDP_PORT}
netmask = 255.0.0.0
allow_inbound = true
allow_outbound = true
allow_same_interface = false

[peer remote0]
callsign = R3MOT-7
description = Remote PoPT Node
interface = axudp0
host = 12.34.56.78
port = 10093
allow_inbound = true
allow_outbound = true
EOF
    WINEPREFIX="${WIN_PREFIX}" wineserver -k >/dev/null 2>&1 || true
    (
        cd "${TMP_WORK}"
        WIN_CFG_Z="Z:${TMP_WORK//\//\\}\\win_node.cfg"
        WIN_CAP_Z="Z:${TMP_CAP//\//\\}"
        timeout 45 env WINEPREFIX="${WIN_PREFIX}" WINEDEBUG=-all \
            OYK_UI_TEST_MODE=1 OYK_UI_CAPTURE_DIR="${WIN_CAP_Z}" \
            xvfb-run -a -s "-screen 0 1280x960x24 -ac" \
            bash -c '"$@"; rc=$?; wineserver -k >/dev/null 2>&1 || true; exit $rc' \
            _ wine "${WIN_EXE}" --config "${WIN_CFG_Z}"
    )
    WINEPREFIX="${WIN_PREFIX}" wineserver -k >/dev/null 2>&1 || true
else
    echo "Warning: Wine prefix (~/.wine64 or ~/.wine32), Windows binary, or xvfb-run missing; skipping Windows capture." >&2
fi

echo "Rendering PNG images into ${OUT_IMG_DIR}..."
rm -f "${OUT_IMG_DIR}"/*.png

FLOWS=(main settings add_iface edit_iface peers edit_peer terminate ban exit theme)
PORTS=(ncurses dos tinytmux gtk win)

for f in "${TMP_CAP}"/*.ansi; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f" .ansi)"
    python3 "${RENDERER}" --ansi "$f" "${OUT_IMG_DIR}/${base}.png"
done

for f in "${TMP_CAP}"/*.dosbuf; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f" .dosbuf)"
    python3 "${RENDERER}" --dosbuf "$f" "${OUT_IMG_DIR}/${base}.png"
done

for f in "${TMP_CAP}"/*.bmp; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f" .bmp)"
    python3 "${RENDERER}" --bmp "$f" "${OUT_IMG_DIR}/${base}.png"
done

for f in "${TMP_CAP}"/gtk_*.png; do
    [[ -e "$f" ]] || continue
    cp -f "$f" "${OUT_IMG_DIR}/"
done

port_label() {
    case "$1" in
        ncurses)  echo "POSIX ncurses" ;;
        dos)      echo "MS-DOS (80x25 Text)" ;;
        tinytmux) echo "Teensy 4.1 / TinyTMUX (VT340/xterm-256color)" ;;
        gtk)      echo "X11 GTK+ 2.0" ;;
        win)      echo "Windows GUI (Wine / Win32/64)" ;;
    esac
}

flow_title() {
    case "$1" in
        main)       echo "1. Operator Console (Main View)" ;;
        settings)   echo "2. Settings — Node Tab" ;;
        add_iface)  echo "3. Add Interface Wizard" ;;
        edit_iface) echo "4. Edit Interface (Single-View Dialog)" ;;
        peers)      echo "5. Settings — Peers Tab" ;;
        edit_peer)  echo "6. Edit / Add Peer Dialog" ;;
        terminate)  echo "7. Terminate Connection Modal" ;;
        ban)        echo "8. Ban / Unban Callsign Dialog" ;;
        exit)       echo "9. Auto-Learned Peers Exit / Bootloader Prompt" ;;
        theme)      echo "10. Theme Settings Dialog" ;;
    esac
}

{
    cat <<'HEADER'
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK-Node UI Gallery

Automated screenshots generated across all five operator interfaces from live UI flow tests (`./scripts/generate_ui_gallery.sh`).

Each tested operator workflow is grouped below so visual layout, tab structure, and feature parity can be compared across **POSIX ncurses**, **MS-DOS**, **Teensy 4.1 TinyTMUX**, **X11 GTK+ 2.0**, and **Windows**.

HEADER
    for flow in "${FLOWS[@]}"; do
        echo "## $(flow_title "${flow}")"
        echo ""
        for port in "${PORTS[@]}"; do
            img="${port}_${flow}.png"
            if [[ -f "${OUT_IMG_DIR}/${img}" ]]; then
                echo "### $(port_label "${port}")"
                echo ""
                echo "![$(port_label "${port}") — $(flow_title "${flow}")](images/ui-gallery/${img})"
                echo ""
            fi
        done
    done
} > "${OUT_MD}"

png_count="$(ls -1 "${OUT_IMG_DIR}"/*.png 2>/dev/null | wc -l)"
echo "Generated ${png_count} PNG screenshots and ${OUT_MD}"
