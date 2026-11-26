#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${OYK_MACCLASSIC_BUILD:-${SCRIPT_DIR}/build_macclassic}"
MINIVMAC_DIR="${OYK_MINIVMAC_DIR:-${HOME}/minivmac}"
HFS_BIN="${OYK_HFSUTILS_BIN:-${HOME}/opt/hfsutils/bin}"
SYS_IMG="${MINIVMAC_DIR}/MacOS_6.0.8_System_Startup.img"
PLUS_EMU="${MINIVMAC_DIR}/minivmac-plus-bg"
II_EMU="${MINIVMAC_DIR}/minivmac-ii"
PLUS_ROM="${MINIVMAC_DIR}/MacPlus.ROM"
II_ROM="${MINIVMAC_DIR}/MacII.ROM"

export PATH="${HFS_BIN}:${PATH}"

usage() {
    echo "Usage: $0 [--unit-tests | --capture-bw <dir> | --capture-color <dir>]" >&2
    exit 2
}

MODE="${1:-}"
OUT_DIR="${2:-}"
[[ -n "${MODE}" ]] || usage

for req in hformat hmount hcopy hdel humount; do
    if ! command -v "${req}" >/dev/null 2>&1; then
        echo "Error: missing hfsutils tool '${req}' (checked PATH and ${HFS_BIN})." >&2
        exit 1
    fi
done

if ! command -v Xvfb >/dev/null 2>&1; then
    echo "Error: Xvfb is required for headless Mini vMac execution." >&2
    exit 1
fi

run_in_minivmac() {
    local app_bin="$1"
    local emu_bin="$2"
    local rom_file="$3"
    local rom_link_name="$4"
    local with_uitest="$5"
    local work_dir="$6"

    mkdir -p "${work_dir}"
    local boot_dsk="${work_dir}/disk1.dsk"
    local out_dsk="${work_dir}/disk2.dsk"

    # Strip 84-byte DiskCopy 4.2 header from System 6.0.8 startup image -> 1474560 byte HFS image.
    tail -c +85 "${SYS_IMG}" > "${boot_dsk}"
    hmount "${boot_dsk}" 0 >/dev/null
    hdel ":Installer" >/dev/null 2>&1 || true
    hdel ":Apple HD SC Setup" >/dev/null 2>&1 || true
    hdel ":System Folder:Finder" >/dev/null 2>&1 || true
    # Keep Finder's slot filled by our app (retaining APPL creator/type for direct System 6 boot).
    hcopy -m "${app_bin}" ":System Folder:Finder" >/dev/null
    if [[ "${with_uitest}" == "1" ]]; then
        printf "UITEST\n" > "${work_dir}/uitest_flag.txt"
        hcopy -r "${work_dir}/uitest_flag.txt" ":System Folder:UITEST.FLG" >/dev/null
    fi
    humount >/dev/null

    # Create writable 20 MB HFS scratch disk labeled OYKOUT for logs & BMP captures.
    dd if=/dev/zero of="${out_dsk}" bs=1048576 count=20 status=none
    hformat -l OYKOUT "${out_dsk}" >/dev/null
    if [[ "${with_uitest}" == "1" ]]; then
        hmount "${out_dsk}" 0 >/dev/null
        hcopy -r "${work_dir}/uitest_flag.txt" ":UITEST.FLG" >/dev/null
        humount >/dev/null
    fi

    ln -sf "${rom_file}" "${work_dir}/${rom_link_name}"

    local xdisp=$((90 + ($$ % 50)))
    while [[ -e "/tmp/.X${xdisp}-lock" ]]; do
        xdisp=$((xdisp + 1))
    done

    (
        cd "${work_dir}"
        Xvfb ":${xdisp}" -screen 0 800x600x24 -ac >/dev/null 2>&1 &
        local xvfb_pid=$!
        sleep 0.4
        DISPLAY=":${xdisp}" "${emu_bin}" "${boot_dsk}" "${out_dsk}" >/dev/null 2>&1 &
        local emu_pid=$!
        local waited=0
        while kill -0 "${emu_pid}" 2>/dev/null && [[ "${waited}" -lt 120 ]]; do
            if grep -aq "OYK_TEST_COMPLETE_MARKER" "${out_dsk}" 2>/dev/null; then
                sleep 1
                break
            fi
            sleep 0.5
            waited=$((waited + 1))
        done
        if kill -0 "${emu_pid}" 2>/dev/null; then
            kill "${emu_pid}" >/dev/null 2>&1 || true
            sleep 0.5
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
        APP_BIN="${BUILD_DIR}/OYKTest.bin"
        [[ -f "${APP_BIN}" ]] || { echo "Missing ${APP_BIN}; build ports/macclassic first." >&2; exit 1; }
        run_in_minivmac "${APP_BIN}" "${PLUS_EMU}" "${PLUS_ROM}" "vMac.ROM" "0" "${TMP_WORK}"
        hmount "${TMP_WORK}/disk2.dsk" 0 >/dev/null
        hcopy -r ":TESTLOG.TXT" "${TMP_WORK}/TESTLOG.TXT" >/dev/null 2>&1 || true
        humount >/dev/null
        if [[ ! -f "${TMP_WORK}/TESTLOG.TXT" ]]; then
            hmount "${TMP_WORK}/disk1.dsk" 0 >/dev/null
            hcopy -r ":System Folder:TESTLOG.TXT" "${TMP_WORK}/TESTLOG.TXT" >/dev/null 2>&1 || true
            humount >/dev/null
        fi
        if [[ ! -f "${TMP_WORK}/TESTLOG.TXT" ]]; then
            echo "FAIL: OYKTEST did not produce TESTLOG.TXT on OYKOUT or System Startup." >&2
            exit 1
        fi
        cat "${TMP_WORK}/TESTLOG.TXT"
        grep -q "ALL_TESTS_PASSED" "${TMP_WORK}/TESTLOG.TXT"
        ;;
    --capture-bw|--capture-color)
        [[ -n "${OUT_DIR}" ]] || usage
        mkdir -p "${OUT_DIR}"
        APP_BIN="${BUILD_DIR}/OYKNode-Fat.bin"
        [[ -f "${APP_BIN}" ]] || { echo "Missing ${APP_BIN}; build ports/macclassic first." >&2; exit 1; }
        if [[ "${MODE}" == "--capture-bw" ]]; then
            EMU="${PLUS_EMU}"; ROM="${PLUS_ROM}"; ROM_NAME="vMac.ROM"; IN_PFX="mcbw"; OUT_PFX="macclassic_bw"
        else
            EMU="${II_EMU}"; ROM="${II_ROM}"; ROM_NAME="MacII.ROM"; IN_PFX="mccl"; OUT_PFX="macclassic_color"
        fi
        run_in_minivmac "${APP_BIN}" "${EMU}" "${ROM}" "${ROM_NAME}" "1" "${TMP_WORK}"
        hmount "${TMP_WORK}/disk2.dsk" 0 >/dev/null
        FLOWS=(main settings add_iface edit_iface peers edit_peer terminate ban exit theme terminal files)
        copied=0
        for flow in "${FLOWS[@]}"; do
            if hcopy -r ":${IN_PFX}_${flow}.bmp" "${OUT_DIR}/${OUT_PFX}_${flow}.bmp" >/dev/null 2>&1; then
                copied=$((copied + 1))
            fi
        done
        humount >/dev/null
        echo "Extracted ${copied}/${#FLOWS[@]} ${OUT_PFX} BMP screenshots into ${OUT_DIR}"
        [[ "${copied}" -eq 12 ]]
        ;;
    *)
        usage
        ;;
esac
