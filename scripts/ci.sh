#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
# Unified local and CI verification runner for oyk-node.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

JOBS="$(nproc 2>/dev/null || echo 4)"
STEPS="spdx,docs,secrets,stack,parity,host,sanitizers"

usage() {
    cat <<'EOF'
Usage: scripts/ci.sh [options]

Options:
  --quick              Run standard CI gate (spdx,docs,secrets,stack,parity,host,sanitizers)
  --steps <list>       Comma-separated subset of:
                         spdx,docs,secrets,stack,parity,host,sanitizers,emulators
  --jobs <N>           Parallel build jobs (default: nproc)
  -h, --help           Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)
            STEPS="spdx,docs,secrets,stack,parity,host,sanitizers"
            shift
            ;;
        --steps)
            [[ $# -ge 2 ]] || { echo "Error: --steps requires an argument" >&2; exit 2; }
            STEPS="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 ]] || { echo "Error: --jobs requires an argument" >&2; exit 2; }
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

TMP_BASE="$(mktemp -d)"
trap 'rm -rf "${TMP_BASE}"' EXIT

run_step() {
    local step="$1"
    echo "=== CI STEP: ${step} ==="
    case "${step}" in
        spdx)
            python3 scripts/check_spdx.py
            ;;
        docs)
            python3 scripts/check_docs.py
            ;;
        secrets)
            if [[ "${CI:-}" == "true" || "${GITHUB_ACTIONS:-}" == "true" ]]; then
                python3 scripts/check_secrets.py --all
            else
                python3 scripts/check_secrets.py
            fi
            ;;
        stack)
            python3 scripts/check_stack.py --limit 1024
            ;;
        parity)
            python3 scripts/check_port_parity.py
            ;;
        host)
            local bdir="${TMP_BASE}/host"
            cmake -S "${ROOT_DIR}" -B "${bdir}" -DOYK_WERROR=ON
            cmake --build "${bdir}" -j "${JOBS}"
            ctest --test-dir "${bdir}" --output-on-failure
            ;;
        sanitizers)
            local sdir="${TMP_BASE}/san"
            cmake -S "${ROOT_DIR}" -B "${sdir}" \
                -DCMAKE_BUILD_TYPE=Debug \
                -DOYK_SANITIZE=ON \
                -DOYK_WERROR=ON
            cmake --build "${sdir}" -j "${JOBS}"
            ASAN_OPTIONS=detect_leaks=1 \
            UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
            ctest --test-dir "${sdir}" --output-on-failure
            ;;
        emulators)
            local ran=0
            if command -v gsplus >/dev/null 2>&1 || [[ -x "${HOME}/gsplus/gsplus/gsplus" ]]; then
                if [[ -f "${HOME}/gsplus/ROM.01" && -x "${ROOT_DIR}/ports/gsos/build_gsos/OYKTEST.SYS16" ]]; then
                    "${ROOT_DIR}/ports/gsos/run_gsplus_test.sh" --unit-tests
                    ran=$((ran + 1))
                fi
            fi
            if command -v fs-uae >/dev/null 2>&1 && [[ -x "${ROOT_DIR}/ports/amiga/build_amiga/OYKTest" ]]; then
                "${ROOT_DIR}/ports/amiga/run_fsuae_test.sh" --unit-tests
                ran=$((ran + 1))
            fi
            if command -v hatari >/dev/null 2>&1 && [[ -x "${ROOT_DIR}/ports/atarist/build_atarist/OYKTEST.PRG" ]]; then
                "${ROOT_DIR}/ports/atarist/run_hatari_test.sh" --unit-tests
                ran=$((ran + 1))
            fi
            if [[ -x "${HOME}/minivmac/minivmac-plus-bg" && -f "${ROOT_DIR}/ports/macclassic/build_68k/OYKNode.bin" ]]; then
                "${ROOT_DIR}/ports/macclassic/run_minivmac_test.sh" --unit-tests
                ran=$((ran + 1))
            fi
            echo "[INFO] Emulator checks executed: ${ran}"
            ;;
        *)
            echo "Error: unknown CI step '${step}'" >&2
            exit 2
            ;;
    esac
}

IFS=',' read -r -a step_arr <<< "${STEPS}"
for s in "${step_arr[@]}"; do
    [[ -n "${s}" ]] && run_step "${s}"
done

echo "[PASS] oyk-node CI pipeline succeeded (${STEPS})."
