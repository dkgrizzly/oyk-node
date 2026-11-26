#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${OYK_GSOS_BUILD:-${SCRIPT_DIR}/build_gsos}"
GSPLUS_DIR="${OYK_GSPLUS_DIR:-${HOME}/gsplus}"
GSPLUS_BIN="${OYK_GSPLUS_BIN:-${GSPLUS_DIR}/gsplus/gsplus}"
ROM_FILE="${OYK_GSOS_ROM:-${GSPLUS_DIR}/ROM.01}"
SYS_PO="${OYK_GSOS_DISK:-${GSPLUS_DIR}/System.Disk.po}"
MKPRODOS="${SCRIPT_DIR}/mkprodos.py"

usage() {
    echo "Usage: $0 [--unit-tests | --capture <dir>]" >&2
    exit 2
}

MODE="${1:-}"
OUT_DIR="${2:-}"
[[ -n "${MODE}" ]] || usage

if [[ ! -x "${GSPLUS_BIN}" ]]; then
    echo "Error: GSplus binary not found at ${GSPLUS_BIN}." >&2
    exit 1
fi
if [[ ! -f "${ROM_FILE}" || ! -f "${SYS_PO}" ]]; then
    echo "Error: GS/OS ROM or System.Disk.po missing under ${GSPLUS_DIR}." >&2
    exit 1
fi
if ! command -v Xvfb >/dev/null 2>&1; then
    echo "Error: Xvfb is required for headless GSplus execution." >&2
    exit 1
fi

run_in_gsplus() {
    local app_bin="$1"
    local with_uitest="$2"
    local work_dir="$3"

    mkdir -p "${work_dir}"
    local boot_po="${work_dir}/boot.po"
    local out_po="${work_dir}/out.po"
    local cfg_file="${work_dir}/config.kegs"

    python3 "${MKPRODOS}" format "${out_po}" --name OYKOUT --blocks 32768
    if [[ "${with_uitest}" == "1" ]]; then
        printf "UITEST\n" > "${work_dir}/UITEST.FLG"
        python3 "${MKPRODOS}" put "${out_po}" "${work_dir}/UITEST.FLG" "UITEST.FLG" --type 0x04 --aux 0x0000
    fi

    python3 "${MKPRODOS}" put "${boot_po}" "${app_bin}" "SYSTEM/START" \
        --type 0xB3 --aux 0x0000 --base "${SYS_PO}"

    cat > "${cfg_file}" <<EOF
g_cfg_rom_path = ${ROM_FILE}
g_limit_speed = 0
g_ram_size_exp = 4
s7d1 = ${boot_po}
s7d2 = ${out_po}
EOF

    local xdisp=$((92 + ($$ % 45)))
    while [[ -e "/tmp/.X${xdisp}-lock" ]]; do
        xdisp=$((xdisp + 1))
    done

    (
        cd "${work_dir}"
        Xvfb ":${xdisp}" -screen 0 720x480x24 -ac >/dev/null 2>&1 &
        local xvfb_pid=$!
        sleep 0.4
        DISPLAY=":${xdisp}" "${GSPLUS_BIN}" -cfg "${cfg_file}" -audio 0 -noshm >/dev/null 2>&1 &
        local emu_pid=$!

        local waited=0
        while kill -0 "${emu_pid}" 2>/dev/null && [[ "${waited}" -lt 160 ]]; do
            if python3 "${MKPRODOS}" get "${out_po}" "DONE.TXT" "${work_dir}/DONE.TXT" >/dev/null 2>&1; then
                sleep 0.4
                break
            fi
            sleep 0.5
            waited=$((waited + 1))
        done

        if kill -0 "${emu_pid}" 2>/dev/null; then
            kill "${emu_pid}" >/dev/null 2>&1 || true
            sleep 0.3
            kill -9 "${emu_pid}" >/dev/null 2>&1 || true
        fi
        wait "${emu_pid}" >/dev/null 2>&1 || true
        kill "${xvfb_pid}" >/dev/null 2>&1 || true
        wait "${xvfb_pid}" >/dev/null 2>&1 || true
    )
}

TMP_WORK="$(mktemp -d)"
trap 'rm -rf "${TMP_WORK}"' EXIT

case "${MODE}" in
    --unit-tests)
        TEST_BIN="${BUILD_DIR}/OYKTEST.SYS16"
        [[ -f "${TEST_BIN}" ]] || { echo "Missing ${TEST_BIN}; run 'make -C ports/gsos' first." >&2; exit 1; }
        run_in_gsplus "${TEST_BIN}" "0" "${TMP_WORK}"
        python3 "${MKPRODOS}" get "${TMP_WORK}/out.po" "TESTLOG.TXT" "${TMP_WORK}/TESTLOG.TXT" || {
            echo "FAIL: OYKTEST.SYS16 did not produce TESTLOG.TXT on OYKOUT." >&2
            exit 1
        }
        cat "${TMP_WORK}/TESTLOG.TXT"
        grep -q "ALL_TESTS_PASSED" "${TMP_WORK}/TESTLOG.TXT"
        ;;
    --capture)
        [[ -n "${OUT_DIR}" ]] || usage
        mkdir -p "${OUT_DIR}"
        APP_BIN="${BUILD_DIR}/OYKNODE.SYS16"
        [[ -f "${APP_BIN}" ]] || { echo "Missing ${APP_BIN}; run 'make -C ports/gsos' first." >&2; exit 1; }
        run_in_gsplus "${APP_BIN}" "1" "${TMP_WORK}"
        python3 "${MKPRODOS}" extract-bmps "${TMP_WORK}/out.po" "${OUT_DIR}"
        declare -A MAP=(
            [gmain.bmp]=gsos_main.bmp
            [gsett.bmp]=gsos_settings.bmp
            [gaddi.bmp]=gsos_add_iface.bmp
            [gedti.bmp]=gsos_edit_iface.bmp
            [gpeer.bmp]=gsos_peers.bmp
            [gedtp.bmp]=gsos_edit_peer.bmp
            [gterm.bmp]=gsos_terminate.bmp
            [gban.bmp]=gsos_ban.bmp
            [gexit.bmp]=gsos_exit.bmp
            [gthme.bmp]=gsos_theme.bmp
            [gtml.bmp]=gsos_terminal.bmp
            [gfile.bmp]=gsos_files.bmp
        )
        for src_name in "${!MAP[@]}"; do
            if [[ -f "${OUT_DIR}/${src_name}" ]]; then
                mv -f "${OUT_DIR}/${src_name}" "${OUT_DIR}/${MAP[${src_name}]}"
            fi
        done
        FLOWS=(main settings add_iface edit_iface peers edit_peer terminate ban exit theme terminal files)
        copied=0
        for flow in "${FLOWS[@]}"; do
            if [[ -f "${OUT_DIR}/gsos_${flow}.bmp" ]]; then
                copied=$((copied + 1))
            fi
        done
        echo "Extracted ${copied}/${#FLOWS[@]} gsos BMP screenshots into ${OUT_DIR}"
        [[ "${copied}" -eq 12 ]]
        ;;
    *)
        usage
        ;;
esac
