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
mkdir -p "${TMP_CAP}" "${OUT_IMG_DIR}" "${TMP_WORK}/logs" \
    "${TMP_WORK}/incoming/W3TRM-1" "${TMP_WORK}/files/bulletins"
printf 'Field Day schedule\n' > "${TMP_WORK}/incoming/W3TRM-1/fieldday.txt"
printf 'Welcome bulletin\n' > "${TMP_WORK}/files/bulletins/welcome.txt"
printf 'General node info\n' > "${TMP_WORK}/files/readme.txt"
trap 'rm -rf "${TMP_WORK}"' EXIT

if [[ ! -x "${BUILD_DIR}/oyk-node" || ! -x "${BUILD_DIR}/oyk_portable_tests" || ! -x "${BUILD_DIR}/oyk_tinytmux_tests" || ! -x "${BUILD_DIR}/oyk_pico_tests" || ! -x "${BUILD_DIR}/oyk_esp32c6_tests" ]]; then
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
bbs_files_path = ${TMP_WORK}/files
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

echo "[3/7] Capturing Teensy 4.1 / TinyTMUX ANSI screens..."
OYK_UI_CAPTURE_DIR="${TMP_CAP}" "${BUILD_DIR}/oyk_tinytmux_tests"

echo "[4/7] Capturing Raspberry Pi Pico (RP2040/RP2350) ANSI screens..."
OYK_UI_CAPTURE_DIR="${TMP_CAP}" "${BUILD_DIR}/oyk_pico_tests"

echo "[4b/7] Capturing ESP32-C6 (M5Stack Unit C6L) ANSI screens..."
OYK_UI_CAPTURE_DIR="${TMP_CAP}" "${BUILD_DIR}/oyk_esp32c6_tests"

echo "[4/5] Capturing X11 GTK+ 2.0 screens under Xvfb..."
if [[ -x "${BUILD_DIR}/oyk-node-gtk" ]] && command -v xvfb-run >/dev/null 2>&1; then
    timeout 60 xvfb-run -a -s "-screen 0 1280x960x24" \
        bash -c '
            WM_PID=""
            if command -v xfwm4 >/dev/null 2>&1; then
                xfwm4 --compositor=off >/dev/null 2>&1 &
                WM_PID=$!
                sleep 0.8
            fi
            cd "$4"
            OYK_UI_TEST_MODE=1 OYK_UI_CAPTURE_DIR="$1" "$2" --config "$3"
            rc=$?
            if [[ -n "${WM_PID}" ]] && kill -0 "${WM_PID}" 2>/dev/null; then
                kill "${WM_PID}" >/dev/null 2>&1 || true
                wait "${WM_PID}" >/dev/null 2>&1 || true
            fi
            exit $rc
        ' _ "${TMP_CAP}" "${BUILD_DIR}/oyk-node-gtk" "${TMP_WORK}/node.cfg" "${TMP_WORK}"
else
    echo "Warning: oyk-node-gtk or xvfb-run unavailable; skipping GTK+ 2.0 capture." >&2
fi

echo "[5/5] Capturing Windows GUI screens under Xvfb + Wine..."
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && command -v cmake >/dev/null 2>&1; then
    WIN_BUILD_DIR="${TMPDIR:-/tmp}/oyk-win64-build"
    if [[ ! -f "${WIN_BUILD_DIR}/CMakeCache.txt" ]] || ! grep -q "BUILD_WINDOWS_PORT:BOOL=ON" "${WIN_BUILD_DIR}/CMakeCache.txt"; then
        rm -rf "${WIN_BUILD_DIR}"
        cmake -S "${ROOT_DIR}" -B "${WIN_BUILD_DIR}" -DBUILD_WINDOWS_PORT=ON >/dev/null
    fi
    cmake --build "${WIN_BUILD_DIR}" --target oyk-node-win64 -j >/dev/null
    cp -f "${WIN_BUILD_DIR}/oykw64.exe" "${BUILD_DIR}/oykw64.exe"
fi
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
bbs_files_path = files
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
        WIN_EXE_Z="Z:${WIN_EXE//\//\\}"
        WIN_CFG_Z="Z:${TMP_WORK//\//\\}\\win_node.cfg"
        WIN_CAP_Z="Z:${TMP_CAP//\//\\}"
        timeout 60 env WINEPREFIX="${WIN_PREFIX}" WINEDEBUG=-all \
            OYK_UI_TEST_MODE=1 OYK_UI_CAPTURE_DIR="${WIN_CAP_Z}" \
            xvfb-run -a -s "-screen 0 1280x960x24 -ac" \
            bash -c '
                wine explorer /desktop=OYK,1280x960 "$1" --config "$2" >/dev/null 2>&1 &
                EXP_PID=$!
                waited=0
                while kill -0 "${EXP_PID}" 2>/dev/null && [[ "${waited}" -lt 80 ]]; do
                    cnt=$(ls -1 "$3"/win_*.bmp 2>/dev/null | wc -l)
                    if [[ "${cnt}" -ge 23 ]]; then
                        sleep 0.6
                        break
                    fi
                    sleep 0.4
                    waited=$((waited + 1))
                done
                wineserver -k >/dev/null 2>&1 || true
                wait "${EXP_PID}" >/dev/null 2>&1 || true
                exit 0
            ' _ "${WIN_EXE_Z}" "${WIN_CFG_Z}" "${TMP_CAP}"
    )
    WINEPREFIX="${WIN_PREFIX}" wineserver -k >/dev/null 2>&1 || true
else
    echo "Warning: Wine prefix (~/.wine64 or ~/.wine32), Windows binary, or xvfb-run missing; skipping Windows capture." >&2
fi

ANDROID_SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-${HOME}/Android/Sdk}}"
echo "[6/6] Capturing Android Compose UI screens under Robolectric..."
if [[ -x "${ROOT_DIR}/ports/android/gradlew" && -d "${ANDROID_SDK}" ]] && command -v java >/dev/null 2>&1; then
    (
        cd "${ROOT_DIR}/ports/android"
        OYK_UI_CAPTURE_DIR="${TMP_CAP}" ANDROID_HOME="${ANDROID_SDK}" \
            ./gradlew :app:testPlayStoreDebugUnitTest \
            --tests "com.kr4oyk.oyknode.OykUiGalleryTest" \
            --rerun-tasks --no-daemon >/dev/null
    )
else
    echo "Warning: Android Gradle wrapper, Java, or Android SDK unavailable; skipping Android capture." >&2
fi

echo "[7/7] Capturing Classic Macintosh (68k/PPC) screens under Mini vMac..."
RETRO68_DIR="${RETRO68_PREFIX:-${HOME}/opt/Retro68}"
MC_RUNNER="${ROOT_DIR}/ports/macclassic/run_minivmac_test.sh"
if [[ -x "${RETRO68_DIR}/bin/m68k-apple-macos-g++" && -x "${MC_RUNNER}" && -x "${HOME}/minivmac/minivmac-plus-bg" ]]; then
    make -C "${ROOT_DIR}/ports/macclassic" RETRO68_PREFIX="${RETRO68_DIR}" all >/dev/null
    "${MC_RUNNER}" --capture-bw "${TMP_CAP}" || echo "Warning: Classic Mac B&W capture failed." >&2
    if [[ -x "${HOME}/minivmac/minivmac-ii" && -f "${HOME}/minivmac/MacII.ROM" ]]; then
        "${MC_RUNNER}" --capture-color "${TMP_CAP}" || echo "Warning: Classic Mac Color capture failed." >&2
    fi
else
    echo "Warning: Retro68 or Mini vMac binaries unavailable; skipping Classic Macintosh capture." >&2
fi

echo "[8/9] Capturing modern macOS (Cocoa) screens..."
if [[ "$(uname -s)" == "Darwin" ]]; then
    MACOS_APP=""
    for cand in "${ROOT_DIR}/build-macos" "${ROOT_DIR}/build"; do
        MACOS_APP="$(find "${cand}" -maxdepth 3 -name 'OYK Node.app' 2>/dev/null | head -n 1 || true)"
        [[ -n "${MACOS_APP}" ]] && break
    done
    if [[ -n "${MACOS_APP}" && -x "${MACOS_APP}/Contents/MacOS/OYK Node" ]]; then
        OYK_UI_TEST_MODE=1 OYK_UI_CAPTURE_DIR="${TMP_CAP}" \
            "${MACOS_APP}/Contents/MacOS/OYK Node" --config "${TMP_WORK}/node.cfg" \
            || echo "Warning: macOS UI capture exited non-zero." >&2
    else
        echo "Warning: OYK Node.app not built on macOS; skipping native capture." >&2
    fi
elif [[ -n "${OYK_MACOS_CAPTURE_DIR:-}" && -d "${OYK_MACOS_CAPTURE_DIR}" ]]; then
    for mf in "${OYK_MACOS_CAPTURE_DIR}"/macos_*.png; do
        [[ -e "${mf}" ]] || continue
        [[ "$(basename "${mf}")" == "macos_contact_sheet.png" ]] && continue
        cp -f "${mf}" "${TMP_CAP}/"
    done
else
    for mf in "${OUT_IMG_DIR}"/macos_*.png; do
        [[ -e "${mf}" ]] || continue
        [[ "$(basename "${mf}")" == "macos_contact_sheet.png" ]] && continue
        cp -f "${mf}" "${TMP_CAP}/"
    done
fi

echo "[9/9] Capturing Apple IIGS GS/OS Super Hi-Res screens under GSplus..."
VBCC_DIR="${VBCC:-${HOME}/opt/vbcc}"
GSOS_DIR="${OYK_GSPLUS_DIR:-${HOME}/gsplus}"
GSOS_RUNNER="${ROOT_DIR}/ports/gsos/run_gsplus_test.sh"
if [[ -x "${VBCC_DIR}/bin/vc" && -x "${GSOS_RUNNER}" && -x "${GSOS_DIR}/gsplus/gsplus" && -f "${GSOS_DIR}/ROM.01" && -f "${GSOS_DIR}/System.Disk.po" ]]; then
    make -C "${ROOT_DIR}/ports/gsos" VBCC_PREFIX="${VBCC_DIR}" all >/dev/null
    "${GSOS_RUNNER}" --capture "${TMP_CAP}" || echo "Warning: GS/OS capture failed." >&2
else
    echo "Warning: vbcc or GSplus unavailable; skipping Apple IIGS GS/OS capture." >&2
fi

echo "[10/10] Capturing Amiga Workbench screens under FS-UAE + AROS m68k..."
AMIGA_GCC_DIR="${AMIGA_GCC_PREFIX:-${HOME}/opt/amiga-gcc}"
AMIGA_RUNNER="${ROOT_DIR}/ports/amiga/run_fsuae_test.sh"
if [[ -x "${AMIGA_GCC_DIR}/bin/m68k-amigaos-g++" && -x "${AMIGA_RUNNER}" ]] && command -v fs-uae >/dev/null 2>&1; then
    make -C "${ROOT_DIR}/ports/amiga" AMIGA_GCC_PREFIX="${AMIGA_GCC_DIR}" all >/dev/null
    "${AMIGA_RUNNER}" --capture "${TMP_CAP}" || echo "Warning: Amiga capture failed." >&2
else
    echo "Warning: amiga-gcc or fs-uae unavailable; skipping Amiga Workbench capture." >&2
fi

echo "[11/11] Capturing Atari ST GEM screens (ST High Mono & 16-color VDI) under Hatari + EmuTOS..."
MINTELF_DIR="${MINTELF_PREFIX:-${HOME}/opt/mintelf}"
ATARI_RUNNER="${ROOT_DIR}/ports/atarist/run_hatari_test.sh"
if [[ -x "${MINTELF_DIR}/usr/bin/m68k-atari-mintelf-g++" && -x "${ATARI_RUNNER}" ]] && command -v hatari >/dev/null 2>&1; then
    make -C "${ROOT_DIR}/ports/atarist" MINTELF_PREFIX="${MINTELF_DIR}" all >/dev/null
    "${ATARI_RUNNER}" --capture-mono "${TMP_CAP}" || echo "Warning: Atari ST Mono capture failed." >&2
    "${ATARI_RUNNER}" --capture-color "${TMP_CAP}" || echo "Warning: Atari ST Color capture failed." >&2
else
    echo "Warning: mintelf or Hatari unavailable; skipping Atari ST capture." >&2
fi

echo "Validating cross-TUI layout parity..."
PYTHONDONTWRITEBYTECODE=1 python3 "${ROOT_DIR}/scripts/check_tui_parity.py" "${TMP_CAP}"

echo "Rendering PNG images into ${OUT_IMG_DIR}..."
rm -f "${OUT_IMG_DIR}"/*.png
rm -rf "${OUT_IMG_DIR}/thumbs"
mkdir -p "${OUT_IMG_DIR}/thumbs"

FLOWS=(main menu settings settings_bbs settings_chat settings_ifaces peers settings_rig settings_printer add_iface_1 add_iface_2 add_iface_3 add_iface_4 edit_iface edit_peer terminate ban exit theme terminal terminal_help files about)
PORTS=(ncurses dos dos_gfx tinytmux pico esp32c6 gtk win android macclassic_bw macclassic_color macos gsos amiga atarist_mono atarist_color)

for f in "${TMP_CAP}"/*.ansi; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f" .ansi)"
    PYTHONDONTWRITEBYTECODE=1 python3 "${RENDERER}" --ansi "$f" "${OUT_IMG_DIR}/${base}.png"
done

for f in "${TMP_CAP}"/*.dosbuf; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f" .dosbuf)"
    PYTHONDONTWRITEBYTECODE=1 python3 "${RENDERER}" --dosbuf "$f" "${OUT_IMG_DIR}/${base}.png"
done

for f in "${TMP_CAP}"/*.bmp; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f" .bmp)"
    PYTHONDONTWRITEBYTECODE=1 python3 "${RENDERER}" --bmp "$f" "${OUT_IMG_DIR}/${base}.png"
done

for f in "${TMP_CAP}"/gtk_*.png "${TMP_CAP}"/android_*.png "${TMP_CAP}"/macos_*.png; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f")"
    [[ "${base}" == *_contact_sheet.png ]] && continue
    cp -f "$f" "${OUT_IMG_DIR}/${base}"
done

for f in "${OUT_IMG_DIR}"/*.png; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f")"
    PYTHONDONTWRITEBYTECODE=1 python3 "${RENDERER}" --scale "$f" "${OUT_IMG_DIR}/thumbs/${base}" 320 200
done

port_label() {
    case "$1" in
        ncurses)          echo "POSIX ncurses" ;;
        dos)              echo "MS-DOS (80x25 Text)" ;;
        dos_gfx)          echo "MS-DOS 386+ Graphical (VBE/VGA/EGA)" ;;
        tinytmux)         echo "Teensy 4.1 / TinyTMUX (VT340/xterm-256color)" ;;
        pico)             echo "Raspberry Pi Pico (RP2040/RP2350 VT340/xterm-256color)" ;;
        esp32c6)          echo "ESP32-C6 (M5Stack Unit C6L VT340/xterm-256color)" ;;
        gtk)              echo "X11 GTK+ 2.0" ;;
        win)              echo "Windows GUI (Wine / Win32/64)" ;;
        android)          echo "Android (Material 3 / Jetpack Compose)" ;;
        macclassic_bw)    echo "Classic Macintosh (System 6.0.8 B&W 512x342)" ;;
        macclassic_color) echo "Classic Macintosh (Color QuickDraw 640x480)" ;;
        macos)            echo "macOS (Cocoa / Universal ARM64 + Intel + PowerPC)" ;;
        gsos)             echo "Apple IIGS GS/OS (QuickDraw II 640x200)" ;;
        amiga)            echo "Amiga Workbench (Intuition/GadTools OS 3.1/2.04/1.3 + AROS)" ;;
        atarist_mono)     echo "Atari ST GEM (ST High 640x400 Monochrome + EmuTOS)" ;;
        atarist_color)    echo "Atari TT / VDI GEM (640x480 16-Color + EmuTOS)" ;;
    esac
}

flow_title() {
    case "$1" in
        main)             echo "1. Operator Console (Main View)" ;;
        menu)             echo "2. Node Dropdown Menu" ;;
        settings)         echo "3. Settings — Node Tab" ;;
        settings_bbs)     echo "4. Settings — BBS Tab" ;;
        settings_chat)    echo "5. Settings — CHAT Tab" ;;
        settings_ifaces)  echo "6. Settings — Interfaces Tab" ;;
        peers)            echo "7. Settings — Peers Tab" ;;
        settings_rig)     echo "8. Settings — Rig Control Tab" ;;
        settings_printer) echo "9. Settings — Printer Tab" ;;
        add_iface_1)      echo "10. Add Interface Wizard — Step 1 (Driver)" ;;
        add_iface_2)      echo "11. Add Interface Wizard — Step 2 (Identity)" ;;
        add_iface_3)      echo "12. Add Interface Wizard — Step 3 (Endpoint & Policy)" ;;
        add_iface_4)      echo "13. Add Interface Wizard — Step 4 (Review Summary)" ;;
        edit_iface)       echo "14. Edit Interface Dialog" ;;
        edit_peer)        echo "15. Edit / Add Peer Dialog" ;;
        terminate)        echo "16. Terminate Connection Modal" ;;
        ban)              echo "17. Ban / Unban Callsign Dialog" ;;
        exit)             echo "18. Auto-Learned Peers Exit / Bootloader Prompt" ;;
        theme)            echo "19. Theme Settings Dialog" ;;
        terminal)         echo "20. Local Packet Terminal (Ctrl-A Hotkeys & BBS Session)" ;;
        terminal_help)    echo "21. Terminal Hotkey Help Modal" ;;
        files)            echo "22. Dual-Pane File Manager (Incoming | Files)" ;;
        about)            echo "23. About OYK-Node Dialog" ;;
    esac
}

flow_short() {
    case "$1" in
        main)             echo "1. Console" ;;
        menu)             echo "2. Node Menu" ;;
        settings)         echo "3. Set: Node" ;;
        settings_bbs)     echo "4. Set: BBS" ;;
        settings_chat)    echo "5. Set: CHAT" ;;
        settings_ifaces)  echo "6. Set: Ifaces" ;;
        peers)            echo "7. Set: Peers" ;;
        settings_rig)     echo "8. Set: Rig" ;;
        settings_printer) echo "9. Set: Printer" ;;
        add_iface_1)      echo "10. Wiz 1: Driver" ;;
        add_iface_2)      echo "11. Wiz 2: Identity" ;;
        add_iface_3)      echo "12. Wiz 3: Policy" ;;
        add_iface_4)      echo "13. Wiz 4: Review" ;;
        edit_iface)       echo "14. Edit Iface" ;;
        edit_peer)        echo "15. Edit Peer" ;;
        terminate)        echo "16. Terminate" ;;
        ban)              echo "17. Ban/Unban" ;;
        exit)             echo "18. Exit/Boot" ;;
        theme)            echo "19. Theme" ;;
        terminal)         echo "20. Terminal" ;;
        terminal_help)    echo "21. Term Help" ;;
        files)            echo "22. Files" ;;
        about)            echo "23. About" ;;
    esac
}

{
    cat <<'HEADER'
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK-Node UI Gallery

Automated native-resolution screenshots and per-port thumbnail contact sheets generated across all 23 canonical operator screens (`./scripts/generate_ui_gallery.sh`).

Click any thumbnail in the port tables below to open the full-resolution native PNG capture.

## Table of Contents

HEADER
    for port in "${PORTS[@]}"; do
        has_any=0
        for flow in "${FLOWS[@]}"; do
            if [[ -f "${OUT_IMG_DIR}/${port}_${flow}.png" ]]; then
                has_any=1
                break
            fi
        done
        if [[ "${has_any}" -eq 1 ]]; then
            lbl="$(port_label "${port}")"
            echo "- [${lbl}](#${port})"
        fi
    done
    echo ""

    for port in "${PORTS[@]}"; do
        cells=()
        for flow in "${FLOWS[@]}"; do
            img="${port}_${flow}.png"
            if [[ -f "${OUT_IMG_DIR}/${img}" && -f "${OUT_IMG_DIR}/thumbs/${img}" ]]; then
                short="$(flow_short "${flow}")"
                cells+=("[![${short}](images/ui-gallery/thumbs/${img})](images/ui-gallery/${img})<br>**${short}**")
            fi
        done
        if [[ ${#cells[@]} -eq 0 ]]; then
            continue
        fi
        echo "<a id=\"${port}\"></a>"
        echo ""
        echo "## $(port_label "${port}")"
        echo ""
        echo "| | | | |"
        echo "| :---: | :---: | :---: | :---: |"
        idx=0
        total="${#cells[@]}"
        while [[ "${idx}" -lt "${total}" ]]; do
            row="|"
            for col in 0 1 2 3; do
                pos=$((idx + col))
                if [[ "${pos}" -lt "${total}" ]]; then
                    row="${row} ${cells[${pos}]} |"
                else
                    row="${row}  |"
                fi
            done
            echo "${row}"
            idx=$((idx + 4))
        done
        echo ""
    done
} > "${OUT_MD}"

png_count="$(ls -1 "${OUT_IMG_DIR}"/*.png 2>/dev/null | wc -l)"
echo "Generated ${png_count} native PNG screenshots (+ thumbnails) and ${OUT_MD}"
