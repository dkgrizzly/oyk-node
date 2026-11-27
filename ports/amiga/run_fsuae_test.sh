#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${OYK_AMIGA_BUILD:-${SCRIPT_DIR}/build_amiga}"
FSUAE_BIN="${OYK_FSUAE_BIN:-fs-uae}"

usage() {
    echo "Usage: $0 [--unit-tests | --capture <dir>]" >&2
    exit 2
}

MODE="${1:-}"
OUT_DIR="${2:-}"
[[ -n "${MODE}" ]] || usage

if ! command -v "${FSUAE_BIN}" >/dev/null 2>&1; then
    echo "Error: fs-uae binary not found." >&2
    exit 1
fi
if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "Error: xvfb-run is required for headless FS-UAE execution." >&2
    exit 1
fi

run_in_fsuae() {
    local app_bin="$1"
    local app_args="$2"
    local with_uitest="$3"
    local work_dir="$4"

    local dh0="${work_dir}/DH0"
    mkdir -p "${dh0}/S"
    cp -f "${app_bin}" "${dh0}/OYKApp"
    chmod +x "${dh0}/OYKApp"

    if [[ "${with_uitest}" == "1" ]]; then
        printf "UITEST\n" > "${dh0}/UITEST.FLG"
    fi
    printf "DH0:OYKApp %s\n" "${app_args}" > "${dh0}/S/Startup-Sequence"

    local cfg="${work_dir}/oyk.fs-uae"
    cat > "${cfg}" <<EOF
[fs-uae]
amiga_model = A1200
kickstart_file = internal
hard_drive_0 = ${dh0}
floppy_drive_0_volume = 0
audio = 0
video_sync = 0
warp_mode = 1
EOF

    (
        cd "${work_dir}"
        xvfb-run -a -s "-screen 0 800x600x24" "${FSUAE_BIN}" "${cfg}" >/dev/null 2>&1 &
        local emu_pid=$!
        local waited=0
        while kill -0 "${emu_pid}" 2>/dev/null && [[ "${waited}" -lt 80 ]]; do
            if [[ -f "${dh0}/DONE.TXT" ]]; then
                sleep 0.4
                break
            fi
            sleep 0.5
            waited=$((waited + 1))
        done
        if kill -0 "${emu_pid}" 2>/dev/null; then
            pkill -P "${emu_pid}" >/dev/null 2>&1 || true
            kill "${emu_pid}" >/dev/null 2>&1 || true
            sleep 0.3
            kill -9 "${emu_pid}" >/dev/null 2>&1 || true
        fi
        wait "${emu_pid}" >/dev/null 2>&1 || true
    )
}

TMP_WORK="$(mktemp -d)"
trap 'rm -rf "${TMP_WORK}"' EXIT

case "${MODE}" in
    --unit-tests)
        TEST_BIN="${BUILD_DIR}/OYKTest"
        [[ -f "${TEST_BIN}" ]] || { echo "Missing ${TEST_BIN}; run 'make -C ports/amiga' first." >&2; exit 1; }
        run_in_fsuae "${TEST_BIN}" "" "0" "${TMP_WORK}"
        if [[ ! -f "${TMP_WORK}/DH0/TESTLOG.TXT" ]]; then
            echo "FAIL: OYKTest did not produce DH0:TESTLOG.TXT under FS-UAE." >&2
            exit 1
        fi
        cat "${TMP_WORK}/DH0/TESTLOG.TXT"
        grep -q "ALL_TESTS_PASSED" "${TMP_WORK}/DH0/TESTLOG.TXT"
        ;;
    --capture)
        [[ -n "${OUT_DIR}" ]] || usage
        mkdir -p "${OUT_DIR}"
        APP_BIN="${BUILD_DIR}/OYKNode"
        [[ -f "${APP_BIN}" ]] || { echo "Missing ${APP_BIN}; run 'make -C ports/amiga' first." >&2; exit 1; }
        run_in_fsuae "${APP_BIN}" "--ui-test" "1" "${TMP_WORK}"
        declare -A MAP=(
            [AM_MAIN.BMP]=amiga_main.bmp
            [AM_MENU.BMP]=amiga_menu.bmp
            [AM_SET1.BMP]=amiga_settings.bmp
            [AM_SET2.BMP]=amiga_settings_bbs.bmp
            [AM_SET3.BMP]=amiga_settings_chat.bmp
            [AM_SET4.BMP]=amiga_settings_ifaces.bmp
            [AM_PEER.BMP]=amiga_peers.bmp
            [AM_SET6.BMP]=amiga_settings_rig.bmp
            [AM_SET7.BMP]=amiga_settings_printer.bmp
            [AM_ADD1.BMP]=amiga_add_iface_1.bmp
            [AM_ADD2.BMP]=amiga_add_iface_2.bmp
            [AM_ADD3.BMP]=amiga_add_iface_3.bmp
            [AM_ADD4.BMP]=amiga_add_iface_4.bmp
            [AM_EDTI.BMP]=amiga_edit_iface.bmp
            [AM_EDTP.BMP]=amiga_edit_peer.bmp
            [AM_TERM.BMP]=amiga_terminate.bmp
            [AM_BAN.BMP]=amiga_ban.bmp
            [AM_EXIT.BMP]=amiga_exit.bmp
            [AM_THME.BMP]=amiga_theme.bmp
            [AM_TML.BMP]=amiga_terminal.bmp
            [AM_TMLH.BMP]=amiga_terminal_help.bmp
            [AM_FILE.BMP]=amiga_files.bmp
            [AM_ABT.BMP]=amiga_about.bmp
        )
        copied=0
        for src_name in "${!MAP[@]}"; do
            if [[ -f "${TMP_WORK}/DH0/${src_name}" ]]; then
                cp -f "${TMP_WORK}/DH0/${src_name}" "${OUT_DIR}/${MAP[${src_name}]}"
                copied=$((copied + 1))
            fi
        done
        echo "Extracted ${copied}/23 amiga BMP screenshots into ${OUT_DIR}"
        [[ "${copied}" -eq 23 ]]
        ;;
    *)
        usage
        ;;
esac
