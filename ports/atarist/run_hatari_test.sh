#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${OYK_ATARI_BUILD:-${SCRIPT_DIR}/build_atarist}"
HATARI_BIN="${OYK_HATARI_BIN:-hatari}"

EMUTOS_IMG="${OYK_EMUTOS_IMG:-}"
if [[ -z "${EMUTOS_IMG}" ]]; then
    for cand in \
        "${HOME}/opt/emutos/etos512k.img" \
        "${HOME}/emutos/etos512k.img" \
        "/usr/share/emutos/etos512k.img" \
        "/usr/share/hatari/etos512k.img"; do
        if [[ -f "${cand}" ]]; then
            EMUTOS_IMG="${cand}"
            break
        fi
    done
fi

usage() {
    echo "Usage: $0 [--unit-tests | --capture-mono <dir> | --capture-color <dir>]" >&2
    exit 2
}

MODE="${1:-}"
OUT_DIR="${2:-}"
[[ -n "${MODE}" ]] || usage

if ! command -v "${HATARI_BIN}" >/dev/null 2>&1; then
    echo "Error: hatari binary not found." >&2
    exit 1
fi
if [[ -z "${EMUTOS_IMG}" || ! -f "${EMUTOS_IMG}" ]]; then
    echo "Error: EmuTOS ROM image not found (checked OYK_EMUTOS_IMG and ~/opt/emutos/etos512k.img)." >&2
    exit 1
fi
if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "Error: xvfb-run is required for headless Hatari execution." >&2
    exit 1
fi

run_in_hatari() {
    local prg_bin="$1"
    local with_uitest="$2"
    local mode="$3"
    local work_dir="$4"

    local hdd="${work_dir}/hdd"
    mkdir -p "${hdd}"
    cp -f "${prg_bin}" "${hdd}/OYKAPP.PRG"
    if [[ "${with_uitest}" == "1" ]]; then
        printf "UITEST\n" > "${hdd}/UITEST.FLG"
    fi

    local extra_args=()
    if [[ "${mode}" == "color" ]]; then
        extra_args=(--machine tt --monitor vga --vdi yes --vdi-planes 4 --vdi-width 640 --vdi-height 480)
    else
        extra_args=(--machine st --monitor mono)
    fi

    (
        cd "${work_dir}"
        xvfb-run -a -s "-screen 0 800x600x24" \
            "${HATARI_BIN}" \
            --tos "${EMUTOS_IMG}" \
            --memsize 4 \
            --harddrive "${hdd}" \
            --fast-boot on \
            --sound off \
            --fast-forward on \
            --auto "C:\\OYKAPP.PRG" \
            "${extra_args[@]}" >/dev/null 2>&1 &
        local emu_pid=$!
        local waited=0
        while kill -0 "${emu_pid}" 2>/dev/null && [[ "${waited}" -lt 60 ]]; do
            if [[ -f "${hdd}/DONE.TXT" ]]; then
                sleep 0.3
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
        TEST_BIN="${BUILD_DIR}/OYKTEST.PRG"
        [[ -f "${TEST_BIN}" ]] || { echo "Missing ${TEST_BIN}; run 'make -C ports/atarist' first." >&2; exit 1; }
        run_in_hatari "${TEST_BIN}" "0" "color" "${TMP_WORK}"
        if [[ ! -f "${TMP_WORK}/hdd/TESTLOG.TXT" ]]; then
            echo "FAIL: OYKTEST.PRG did not produce C:\\TESTLOG.TXT under Hatari." >&2
            exit 1
        fi
        cat "${TMP_WORK}/hdd/TESTLOG.TXT"
        grep -q "ALL_TESTS_PASSED" "${TMP_WORK}/hdd/TESTLOG.TXT"
        ;;
    --capture-mono|--capture-color)
        [[ -n "${OUT_DIR}" ]] || usage
        mkdir -p "${OUT_DIR}"
        APP_BIN="${BUILD_DIR}/OYKNODE.PRG"
        [[ -f "${APP_BIN}" ]] || { echo "Missing ${APP_BIN}; run 'make -C ports/atarist' first." >&2; exit 1; }
        if [[ "${MODE}" == "--capture-mono" ]]; then
            HMODE="mono"; OUT_PFX="atarist_mono"
        else
            HMODE="color"; OUT_PFX="atarist_color"
        fi
        run_in_hatari "${APP_BIN}" "1" "${HMODE}" "${TMP_WORK}"
        declare -A MAP=(
            [AT_MAIN.BMP]="${OUT_PFX}_main.bmp"
            [AT_SETT.BMP]="${OUT_PFX}_settings.bmp"
            [AT_ADDI.BMP]="${OUT_PFX}_add_iface.bmp"
            [AT_EDTI.BMP]="${OUT_PFX}_edit_iface.bmp"
            [AT_PEER.BMP]="${OUT_PFX}_peers.bmp"
            [AT_EDTP.BMP]="${OUT_PFX}_edit_peer.bmp"
            [AT_TERM.BMP]="${OUT_PFX}_terminate.bmp"
            [AT_BAN.BMP]="${OUT_PFX}_ban.bmp"
            [AT_EXIT.BMP]="${OUT_PFX}_exit.bmp"
            [AT_THME.BMP]="${OUT_PFX}_theme.bmp"
            [AT_TML.BMP]="${OUT_PFX}_terminal.bmp"
            [AT_FILE.BMP]="${OUT_PFX}_files.bmp"
        )
        copied=0
        for src_name in "${!MAP[@]}"; do
            if [[ -f "${TMP_WORK}/hdd/${src_name}" ]]; then
                cp -f "${TMP_WORK}/hdd/${src_name}" "${OUT_DIR}/${MAP[${src_name}]}"
                copied=$((copied + 1))
            fi
        done
        echo "Extracted ${copied}/12 ${OUT_PFX} BMP screenshots into ${OUT_DIR}"
        [[ "${copied}" -eq 12 ]]
        ;;
    *)
        usage
        ;;
esac
