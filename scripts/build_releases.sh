#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/dist"
TARGET_FILTER=""
SKIP_SRC=0

usage() {
    cat <<'EOF'
Usage: build_releases.sh [OPTIONS]

Build all OYK-Node ports whose toolchains are installed at runtime and package
per-target binary release archives plus GitHub-matching source archives.

Options:
  --out-dir <dir>    Destination directory for release archives (default: ./dist)
  --targets <csv>    Comma-separated subset of targets to build:
                     linux-ncurses,linux-gtk,win64,win32,win16,dos,dos-gfx,teensy41,
                     pico_w,pico2_w,waveshare_core2350b,pimoroni_pga2350,
                     android-playstore,android-sideload
  --skip-src         Do not generate source release archives
  -h, --help         Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir)
            OUT_DIR="${2:?Missing argument for --out-dir}"
            shift 2
            ;;
        --targets)
            TARGET_FILTER="${2:?Missing argument for --targets}"
            shift 2
            ;;
        --skip-src)
            SKIP_SRC=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

ALL_VALID_TARGETS="linux-ncurses linux-gtk win64 win32 win16 dos dos-gfx teensy41 pico_w pico2_w waveshare_core2350b pimoroni_pga2350 android-playstore android-sideload"

target_requested() {
    local t="$1"
    if [[ -z "${TARGET_FILTER}" ]]; then
        return 0
    fi
    local item
    IFS=',' read -r -a _req <<< "${TARGET_FILTER}"
    for item in "${_req[@]}"; do
        if [[ "${item}" == "${t}" ]]; then
            return 0
        fi
    done
    return 1
}

if [[ -n "${TARGET_FILTER}" ]]; then
    IFS=',' read -r -a _check <<< "${TARGET_FILTER}"
    for item in "${_check[@]}"; do
        valid=0
        for v in ${ALL_VALID_TARGETS}; do
            if [[ "${item}" == "${v}" ]]; then
                valid=1
                break
            fi
        done
        if [[ "${valid}" -eq 0 ]]; then
            echo "Unknown target in --targets: ${item}" >&2
            exit 1
        fi
    done
fi

mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

TMP_WORK="$(mktemp -d /tmp/oyk_releases.XXXXXX)"
DOS_BUILT=0
WIN16_BUILT=0
cleanup() {
    if [[ "${DOS_BUILT}" -eq 1 && -n "${WATCOM_DIR:-}" ]]; then
        (export WATCOM="${WATCOM_DIR}" INCLUDE="${WATCOM_DIR}/h" EDPATH="${WATCOM_DIR}/eddat" PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
         cd "${ROOT_DIR}/ports/dos" && wmake -h -f makefile clean </dev/null >/dev/null 2>&1 || true
         cd "${ROOT_DIR}/ports/dos" && wmake -h -f makefile.gfx clean </dev/null >/dev/null 2>&1 || true
         rm -rf "${ROOT_DIR}/ports/dos/mtcp_inc")
    fi
    if [[ "${WIN16_BUILT}" -eq 1 && -n "${WATCOM_DIR:-}" ]]; then
        (export WATCOM="${WATCOM_DIR}" INCLUDE="${WATCOM_DIR}/h:${WATCOM_DIR}/h/win" EDPATH="${WATCOM_DIR}/eddat" PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
         cd "${ROOT_DIR}/ports/windows" && wmake -h -f makefile.win16 clean </dev/null >/dev/null 2>&1 || true)
    fi
    rm -rf "${TMP_WORK}"
}
trap cleanup EXIT

# Derive version string: exact git tag on HEAD, else 7-hex short commit SHA; append -dirty if dirty.
EXACT_TAG="$(git -C "${ROOT_DIR}" describe --tags --exact-match 2>/dev/null || true)"
if [[ -n "${EXACT_TAG}" ]]; then
    BASE_VER="${EXACT_TAG}"
else
    BASE_VER="$(git -C "${ROOT_DIR}" rev-parse --short=7 HEAD)"
fi

IS_DIRTY=0
if [[ -n "$(git -C "${ROOT_DIR}" status --porcelain)" ]]; then
    IS_DIRTY=1
    VER="${BASE_VER}-dirty"
else
    VER="${BASE_VER}"
fi

echo "OYK-Node release version: ${VER}"
echo "Output directory: ${OUT_DIR}"

# Generate CHANGELOG for binary archives
CHANGELOG_FILE="${TMP_WORK}/CHANGELOG"
{
    echo "OYK-Node Release Changelog (${VER})"
    echo "========================================"
    echo ""
    if [[ "${IS_DIRTY}" -eq 1 ]]; then
        echo "WARNING: Built from a dirty working tree (uncommitted local modifications present)."
        echo ""
    fi
    LATEST_TAG="$(git -C "${ROOT_DIR}" describe --tags --abbrev=0 2>/dev/null || true)"
    if [[ -n "${LATEST_TAG}" ]]; then
        echo "Latest tag: ${LATEST_TAG}"
    else
        echo "Commit: $(git -C "${ROOT_DIR}" rev-parse HEAD)"
    fi
    echo ""
    echo "Recent commits:"
    git -C "${ROOT_DIR}" -c log.showSignature=false log -n 40 --date=short --pretty=format:"  %h %ad %s (%an)"
    echo ""
} > "${CHANGELOG_FILE}"

# Copy common runtime docs, configs, and port-filtered UI screenshot gallery into a staging root.
stage_binary_tree() {
    local target="$1"
    local img_prefix="$2"
    local stage_dir="${TMP_WORK}/stage-${target}/oyknode-${VER}-${target}"

    rm -rf "${TMP_WORK}/stage-${target}"
    mkdir -p "${stage_dir}/configs" "${stage_dir}/docs/user/images/ui-gallery"

    cp "${ROOT_DIR}/README.md" "${ROOT_DIR}/LICENSE" "${ROOT_DIR}/AUTHORS" "${stage_dir}/"
    cp "${CHANGELOG_FILE}" "${stage_dir}/CHANGELOG"

    for cfg in "${ROOT_DIR}"/configs/*.cfg; do
        [[ -f "${cfg}" ]] && cp "${cfg}" "${stage_dir}/configs/"
    done

    for doc in "${ROOT_DIR}"/docs/user/*.md; do
        [[ -f "${doc}" ]] || continue
        if [[ "$(basename "${doc}")" == "ui-gallery.md" ]]; then
            continue
        fi
        cp "${doc}" "${stage_dir}/docs/user/"
    done

    local copied_pngs=0
    if [[ -n "${img_prefix}" ]]; then
        for img in "${ROOT_DIR}/docs/user/images/ui-gallery/${img_prefix}"_*.png; do
            if [[ -f "${img}" ]]; then
                if [[ "${img_prefix}" == "dos" && "$(basename "${img}")" == dos_gfx_* ]]; then
                    continue
                fi
                cp "${img}" "${stage_dir}/docs/user/images/ui-gallery/"
                copied_pngs=$((copied_pngs + 1))
            fi
        done
    fi

    if [[ -f "${ROOT_DIR}/docs/user/ui-gallery.md" ]]; then
        python3 - "${ROOT_DIR}/docs/user/ui-gallery.md" "${stage_dir}/docs/user/ui-gallery.md" "${img_prefix}" "${target}" <<'PY'
import sys

src_path, dst_path, prefix, target = sys.argv[1:5]
with open(src_path, "r", encoding="utf-8") as f:
    lines = f.read().splitlines()

out_hdr = []
sections = []
cur_h2 = None
cur_sub = None
cur_sub_lines = []

for line in lines:
    if line.startswith("## "):
        if cur_h2 is not None and cur_sub is not None:
            sections[-1][1].append((cur_sub, cur_sub_lines))
        cur_h2 = line
        sections.append((cur_h2, []))
        cur_sub = None
        cur_sub_lines = []
    elif line.startswith("### ") and cur_h2 is not None:
        if cur_sub is not None:
            sections[-1][1].append((cur_sub, cur_sub_lines))
        cur_sub = line
        cur_sub_lines = []
    elif cur_h2 is None:
        out_hdr.append(line)
    elif cur_sub is not None:
        cur_sub_lines.append(line)

if cur_h2 is not None and cur_sub is not None:
    sections[-1][1].append((cur_sub, cur_sub_lines))

filtered_sections = []
needle = f"images/ui-gallery/{prefix}_" if prefix else None
for h2, subs in sections:
    kept_subs = []
    for sub, slines in subs:
        body = "\n".join(slines)
        if needle and needle in body and not (prefix == "dos" and "images/ui-gallery/dos_gfx_" in body):
            kept_subs.append((sub, slines))
    if kept_subs:
        filtered_sections.append((h2, kept_subs))

out_lines = [
    "<!-- SPDX-License-Identifier: GPL-3.0-or-later -->",
    "<!-- Copyright (C) 2026 KR4OYK - David Kuder -->",
    "",
    f"# OYK-Node UI Gallery ({target})",
    "",
    f"Operator interface screenshots for `{target}`.",
    "",
]
if not filtered_sections:
    out_lines.append("_No port-specific UI screenshots available in this release archive._")
    out_lines.append("")
else:
    for h2, subs in filtered_sections:
        out_lines.append(h2)
        out_lines.append("")
        for sub, slines in subs:
            out_lines.append(sub)
            for sline in slines:
                out_lines.append(sline)
            if slines and slines[-1] != "":
                out_lines.append("")

with open(dst_path, "w", encoding="utf-8") as f:
    f.write("\n".join(out_lines).rstrip() + "\n")
PY
    fi

    if [[ "${copied_pngs}" -eq 0 ]]; then
        rmdir "${stage_dir}/docs/user/images/ui-gallery" 2>/dev/null || true
        rmdir "${stage_dir}/docs/user/images" 2>/dev/null || true
    fi

    echo "${stage_dir}"
}

package_tar_gz() {
    local target="$1"
    local stage_root="${TMP_WORK}/stage-${target}"
    local arc_name="oyknode-${VER}-${target}.tar.gz"
    rm -f "${OUT_DIR}/${arc_name}"
    tar -C "${stage_root}" -czf "${OUT_DIR}/${arc_name}" "oyknode-${VER}-${target}"
    echo "  -> Packaged ${arc_name}"
}

package_zip() {
    local target="$1"
    local stage_root="${TMP_WORK}/stage-${target}"
    local arc_name="oyknode-${VER}-${target}.zip"
    rm -f "${OUT_DIR}/${arc_name}"
    (cd "${stage_root}" && zip -qr "${OUT_DIR}/${arc_name}" "oyknode-${VER}-${target}")
    echo "  -> Packaged ${arc_name}"
}

PACKAGED=()
SKIPPED=()

# Locate toolchains at runtime
HAS_CMAKE=0; command -v cmake >/dev/null 2>&1 && HAS_CMAKE=1
HAS_GTK2=0; command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-2.0 2>/dev/null && HAS_GTK2=1
HAS_MINGW64=0; command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && command -v x86_64-w64-mingw32-windres >/dev/null 2>&1 && HAS_MINGW64=1
HAS_MINGW32=0; command -v i686-w64-mingw32-g++ >/dev/null 2>&1 && command -v i686-w64-mingw32-windres >/dev/null 2>&1 && HAS_MINGW32=1

WATCOM_DIR="${WATCOM:-}"
if [[ -z "${WATCOM_DIR}" || ! -d "${WATCOM_DIR}" ]]; then
    if [[ -d "/opt/watcom" ]]; then
        WATCOM_DIR="/opt/watcom"
    elif [[ -d "${HOME}/open-watcom-v2/rel" ]]; then
        WATCOM_DIR="${HOME}/open-watcom-v2/rel"
    fi
fi
HAS_WATCOM_DOS=0
HAS_WATCOM_WIN16=0
if [[ -n "${WATCOM_DIR}" && ( -x "${WATCOM_DIR}/binl64/wmake" || -x "${WATCOM_DIR}/binl/wmake" ) ]]; then
    if [[ -x "${WATCOM_DIR}/binl64/wpp" || -x "${WATCOM_DIR}/binl/wpp" ]]; then
        HAS_WATCOM_DOS=1
        if [[ -f "${WATCOM_DIR}/h/win/windows.h" ]]; then
            HAS_WATCOM_WIN16=1
        fi
    fi
fi

ARDUINO_FOUND="${ARDUINO_DIR:-}"
if [[ -z "${ARDUINO_FOUND}" && -d "${HOME}/arduino-1.8.19" ]]; then
    ARDUINO_FOUND="${HOME}/arduino-1.8.19"
fi
HAS_TEENSY=0
if [[ "${HAS_CMAKE}" -eq 1 && -n "${ARDUINO_FOUND}" && -x "${ARDUINO_FOUND}/arduino-builder" ]]; then
    HAS_TEENSY=1
fi

ANDROID_SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-${HOME}/Android/Sdk}}"
HAS_ANDROID=0
if [[ -x "${ROOT_DIR}/ports/android/gradlew" && -d "${ANDROID_SDK}" ]] && command -v java >/dev/null 2>&1; then
    HAS_ANDROID=1
fi

PICO_SDK_FOUND="${PICO_SDK_PATH:-}"
if [[ -z "${PICO_SDK_FOUND}" ]]; then
    for cand in "${HOME}"/.pico-sdk/sdk/* "${HOME}/pico-sdk"; do
        if [[ -f "${cand}/pico_sdk_init.cmake" && -d "${cand}/lib/cyw43-driver/src" && -d "${cand}/lib/lwip/src" ]]; then
            PICO_SDK_FOUND="${cand}"
            break
        fi
    done
    if [[ -z "${PICO_SDK_FOUND}" && -f "${HOME}/pico-sdk/pico_sdk_init.cmake" ]]; then
        PICO_SDK_FOUND="${HOME}/pico-sdk"
    fi
fi
PICO_ARM_GCC=""
for gcand in "${HOME}"/.pico-sdk/toolchain/*/bin/arm-none-eabi-gcc; do
    if [[ -x "${gcand}" ]]; then
        PICO_ARM_GCC="${gcand}"
        break
    fi
done
if [[ -z "${PICO_ARM_GCC}" ]]; then
    PICO_ARM_GCC="$(command -v arm-none-eabi-gcc || true)"
fi
HAS_PICO=0
if [[ -n "${PICO_SDK_FOUND}" && -n "${PICO_ARM_GCC}" ]] && command -v cmake >/dev/null 2>&1; then
    HAS_PICO=1
fi

# 1. Host CMake / MinGW builds in an isolated temporary build directory
CMAKE_TARGETS=()
if target_requested "linux-ncurses"; then
    if [[ "${HAS_CMAKE}" -eq 1 ]]; then
        CMAKE_TARGETS+=("oyk-node")
    else
        SKIPPED+=("linux-ncurses (cmake not installed)")
    fi
fi
if target_requested "linux-gtk"; then
    if [[ "${HAS_CMAKE}" -eq 1 && "${HAS_GTK2}" -eq 1 ]]; then
        CMAKE_TARGETS+=("oyk-node-gtk")
    else
        SKIPPED+=("linux-gtk (gtk+-2.0 development headers not found)")
    fi
fi
if target_requested "win64"; then
    if [[ "${HAS_CMAKE}" -eq 1 && "${HAS_MINGW64}" -eq 1 ]]; then
        CMAKE_TARGETS+=("oyk-node-win64")
    else
        SKIPPED+=("win64 (x86_64-w64-mingw32 toolchain not found)")
    fi
fi
if target_requested "win32"; then
    if [[ "${HAS_CMAKE}" -eq 1 && "${HAS_MINGW32}" -eq 1 ]]; then
        CMAKE_TARGETS+=("oyk-node-win32")
    else
        SKIPPED+=("win32 (i686-w64-mingw32 toolchain not found)")
    fi
fi

if [[ ${#CMAKE_TARGETS[@]} -gt 0 ]]; then
    echo "Building CMake host/MinGW targets: ${CMAKE_TARGETS[*]}"
    BUILD_HOST="${TMP_WORK}/build-host"
    cmake -S "${ROOT_DIR}" -B "${BUILD_HOST}" -DBUILD_TESTS=OFF -DBUILD_WINDOWS_PORT=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "${BUILD_HOST}" -j --target "${CMAKE_TARGETS[@]}"

    if target_requested "linux-ncurses" && [[ -x "${BUILD_HOST}/oyk-node" ]]; then
        st="$(stage_binary_tree "linux-ncurses" "ncurses")"
        cp "${BUILD_HOST}/oyk-node" "${st}/"
        chmod +x "${st}/oyk-node"
        package_tar_gz "linux-ncurses"
        PACKAGED+=("oyknode-${VER}-linux-ncurses.tar.gz")
    fi
    if target_requested "linux-gtk" && [[ -x "${BUILD_HOST}/oyk-node-gtk" ]]; then
        st="$(stage_binary_tree "linux-gtk" "gtk")"
        cp "${BUILD_HOST}/oyk-node-gtk" "${st}/"
        chmod +x "${st}/oyk-node-gtk"
        package_tar_gz "linux-gtk"
        PACKAGED+=("oyknode-${VER}-linux-gtk.tar.gz")
    fi
    if target_requested "win64" && [[ -f "${BUILD_HOST}/oykw64.exe" ]]; then
        st="$(stage_binary_tree "win64" "win")"
        cp "${BUILD_HOST}/oykw64.exe" "${st}/"
        package_zip "win64"
        PACKAGED+=("oyknode-${VER}-win64.zip")
    fi
    if target_requested "win32" && [[ -f "${BUILD_HOST}/oykw32.exe" ]]; then
        st="$(stage_binary_tree "win32" "win")"
        cp "${BUILD_HOST}/oykw32.exe" "${st}/"
        package_zip "win32"
        PACKAGED+=("oyknode-${VER}-win32.zip")
    fi
fi

# 2. MS-DOS (OpenWatcom)
if target_requested "dos"; then
    if [[ "${HAS_WATCOM_DOS}" -eq 1 ]]; then
        echo "Building MS-DOS target (OpenWatcom)..."
        MTCP_FLAG="USE_MTCP=0"
        if [[ -f "${MTCP_DIR:-${HOME}/mTCP}/src/TCPLIB/PACKET.CPP" ]]; then
            MTCP_FLAG="USE_MTCP=1"
        fi
        DOS_BUILT=1
        (
            export WATCOM="${WATCOM_DIR}"
            export INCLUDE="${WATCOM_DIR}/h"
            export EDPATH="${WATCOM_DIR}/eddat"
            export PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
            cd "${ROOT_DIR}/ports/dos"
            wmake -h -f makefile clean </dev/null >/dev/null 2>&1 || true
            wmake -h -f makefile "${MTCP_FLAG}" oyknode.exe </dev/null
        )
        st="$(stage_binary_tree "dos" "dos")"
        cp "${ROOT_DIR}/ports/dos/oyknode.exe" "${st}/"
        (
            export WATCOM="${WATCOM_DIR}"
            export INCLUDE="${WATCOM_DIR}/h"
            export EDPATH="${WATCOM_DIR}/eddat"
            export PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
            cd "${ROOT_DIR}/ports/dos"
            wmake -h -f makefile clean </dev/null >/dev/null 2>&1 || true
            rm -rf "${ROOT_DIR}/ports/dos/mtcp_inc"
        )
        DOS_BUILT=0
        package_zip "dos"
        PACKAGED+=("oyknode-${VER}-dos.zip")
    else
        SKIPPED+=("dos (OpenWatcom 16-bit DOS compiler not found)")
    fi
fi

# 2b. 386+ Graphical MS-DOS (OpenWatcom + VBE/VGA/EGA)
if target_requested "dos-gfx"; then
    if [[ "${HAS_WATCOM_DOS}" -eq 1 ]]; then
        echo "Building 386+ graphical MS-DOS target (OpenWatcom)..."
        MTCP_FLAG="USE_MTCP=0"
        if [[ -f "${MTCP_DIR:-${HOME}/mTCP}/src/TCPLIB/PACKET.CPP" ]]; then
            MTCP_FLAG="USE_MTCP=1"
        fi
        DOS_BUILT=1
        (
            export WATCOM="${WATCOM_DIR}"
            export INCLUDE="${WATCOM_DIR}/h"
            export EDPATH="${WATCOM_DIR}/eddat"
            export PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
            cd "${ROOT_DIR}/ports/dos"
            wmake -h -f makefile.gfx clean </dev/null >/dev/null 2>&1 || true
            wmake -h -f makefile.gfx "${MTCP_FLAG}" oyknodeg.exe </dev/null
        )
        st="$(stage_binary_tree "dos-gfx" "dos_gfx")"
        cp "${ROOT_DIR}/ports/dos/oyknodeg.exe" "${st}/"
        (
            export WATCOM="${WATCOM_DIR}"
            export INCLUDE="${WATCOM_DIR}/h"
            export EDPATH="${WATCOM_DIR}/eddat"
            export PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
            cd "${ROOT_DIR}/ports/dos"
            wmake -h -f makefile.gfx clean </dev/null >/dev/null 2>&1 || true
            rm -rf "${ROOT_DIR}/ports/dos/mtcp_inc"
        )
        DOS_BUILT=0
        package_zip "dos-gfx"
        PACKAGED+=("oyknode-${VER}-dos-gfx.zip")
    else
        SKIPPED+=("dos-gfx (OpenWatcom DOS compiler not found)")
    fi
fi

# 3. 16-bit Windows 3.1 (OpenWatcom Win16)
if target_requested "win16"; then
    if [[ "${HAS_WATCOM_WIN16}" -eq 1 ]]; then
        echo "Building 16-bit Windows target (OpenWatcom Win16)..."
        WIN16_BUILT=1
        (
            export WATCOM="${WATCOM_DIR}"
            export INCLUDE="${WATCOM_DIR}/h:${WATCOM_DIR}/h/win"
            export EDPATH="${WATCOM_DIR}/eddat"
            export PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
            cd "${ROOT_DIR}/ports/windows"
            wmake -h -f makefile.win16 clean </dev/null >/dev/null 2>&1 || true
            wmake -h -f makefile.win16 oykw16.exe </dev/null
        )
        st="$(stage_binary_tree "win16" "win")"
        cp "${ROOT_DIR}/ports/windows/oykw16.exe" "${st}/"
        (
            export WATCOM="${WATCOM_DIR}"
            export INCLUDE="${WATCOM_DIR}/h:${WATCOM_DIR}/h/win"
            export EDPATH="${WATCOM_DIR}/eddat"
            export PATH="${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:${PATH}"
            cd "${ROOT_DIR}/ports/windows"
            wmake -h -f makefile.win16 clean </dev/null >/dev/null 2>&1 || true
        )
        WIN16_BUILT=0
        package_zip "win16"
        PACKAGED+=("oyknode-${VER}-win16.zip")
    else
        SKIPPED+=("win16 (OpenWatcom Win16 SDK headers not found)")
    fi
fi

# 4. Teensy 4.1 / TinyTMUX
if target_requested "teensy41"; then
    if [[ "${HAS_TEENSY}" -eq 1 ]]; then
        echo "Building Teensy 4.1 / TinyTMUX firmware..."
        BUILD_T41="${TMP_WORK}/build-t41"
        T41_ARGS=("-DARDUINO_DIR=${ARDUINO_FOUND}")
        if [[ -d "${ROOT_DIR}/build-t41/_deps/qnethernet-src" ]]; then
            T41_ARGS+=("-DQNETHERNET_DIR=${ROOT_DIR}/build-t41/_deps/qnethernet-src")
        fi
        cmake -S "${ROOT_DIR}/ports/tinytmux" -B "${BUILD_T41}" "${T41_ARGS[@]}" >/dev/null
        cmake --build "${BUILD_T41}" -j
        st="$(stage_binary_tree "teensy41" "tinytmux")"
        cp "${BUILD_T41}/oyk-tinytmux.hex" "${st}/"
        package_zip "teensy41"
        PACKAGED+=("oyknode-${VER}-teensy41.zip")
    else
        SKIPPED+=("teensy41 (Arduino / Teensyduino toolchain not found)")
    fi
fi

# 4b. Raspberry Pi Pico (RP2040 / RP2350) UF2 boards
PICO_CMAKE_BIN="$(command -v cmake || true)"
for pc in "${HOME}"/.pico-sdk/cmake/*/bin/cmake; do
    [[ -x "${pc}" ]] && PICO_CMAKE_BIN="${pc}" && break
done
PICO_NINJA_ARG=()
for pn in "${HOME}"/.pico-sdk/ninja/*/ninja; do
    [[ -x "${pn}" ]] && PICO_NINJA_ARG=("-G" "Ninja" "-DCMAKE_MAKE_PROGRAM=${pn}") && break
done

for pico_board in pico_w pico2_w waveshare_core2350b pimoroni_pga2350; do
    if target_requested "${pico_board}"; then
        if [[ "${HAS_PICO}" -eq 1 && -n "${PICO_CMAKE_BIN}" ]]; then
            echo "Building Raspberry Pi Pico firmware (${pico_board})..."
            BUILD_PICO="${TMP_WORK}/build-${pico_board}"
            "${PICO_CMAKE_BIN}" -S "${ROOT_DIR}/ports/pico" -B "${BUILD_PICO}" \
                "${PICO_NINJA_ARG[@]}" \
                "-DPICO_BOARD=${pico_board}" \
                "-DPICO_SDK_PATH=${PICO_SDK_FOUND}" >/dev/null
            "${PICO_CMAKE_BIN}" --build "${BUILD_PICO}" -j
            st="$(stage_binary_tree "${pico_board}" "pico")"
            cp "${ROOT_DIR}/ports/pico/node_pico.cfg" "${st}/configs/"
            cp "${ROOT_DIR}/ports/pico/README.md" "${st}/PICO-README.md"
            cp "${BUILD_PICO}/oyk-pico-${pico_board}.uf2" "${st}/"
            package_zip "${pico_board}"
            PACKAGED+=("oyknode-${VER}-${pico_board}.zip")
        else
            SKIPPED+=("${pico_board} (Pico SDK or arm-none-eabi-gcc not found)")
        fi
    fi
done

# 5. Android (Play Store & Sideload flavors, packaged when ports/android is present)
if target_requested "android-playstore"; then
    if [[ "${HAS_ANDROID}" -eq 1 ]]; then
        echo "Building Android Play Store release APK..."
        (cd "${ROOT_DIR}/ports/android" && ANDROID_HOME="${ANDROID_SDK}" ./gradlew :app:assemblePlayStoreRelease --no-daemon)
        apk_path="$(find "${ROOT_DIR}/ports/android/app/build/outputs/apk/playStore/release" -maxdepth 1 -name '*.apk' | head -n 1 || true)"
        if [[ -n "${apk_path}" && -f "${apk_path}" ]]; then
            st="$(stage_binary_tree "android-playstore" "android")"
            cp "${apk_path}" "${st}/oyknode-${VER}-playstore.apk"
            package_zip "android-playstore"
            PACKAGED+=("oyknode-${VER}-android-playstore.zip")
        else
            SKIPPED+=("android-playstore (APK output not produced)")
        fi
    else
        SKIPPED+=("android-playstore (ports/android or Android SDK not available)")
    fi
fi

if target_requested "android-sideload"; then
    if [[ "${HAS_ANDROID}" -eq 1 ]]; then
        echo "Building Android Sideload release APK..."
        (cd "${ROOT_DIR}/ports/android" && ANDROID_HOME="${ANDROID_SDK}" ./gradlew :app:assembleSideloadRelease --no-daemon)
        apk_path="$(find "${ROOT_DIR}/ports/android/app/build/outputs/apk/sideload/release" -maxdepth 1 -name '*.apk' | head -n 1 || true)"
        if [[ -n "${apk_path}" && -f "${apk_path}" ]]; then
            st="$(stage_binary_tree "android-sideload" "android")"
            cp "${apk_path}" "${st}/oyknode-${VER}-sideload.apk"
            package_zip "android-sideload"
            PACKAGED+=("oyknode-${VER}-android-sideload.zip")
        else
            SKIPPED+=("android-sideload (APK output not produced)")
        fi
    else
        SKIPPED+=("android-sideload (ports/android or Android SDK not available)")
    fi
fi

# 6. Source release archives matching GitHub tagged release layout (oyk-node-<ver>/)
if [[ "${SKIP_SRC}" -eq 0 ]]; then
    echo "Packaging source release archives..."
    SRC_TGZ="oyknode-${VER}-src.tar.gz"
    SRC_ZIP="oyknode-${VER}-src.zip"
    rm -f "${OUT_DIR}/${SRC_TGZ}" "${OUT_DIR}/${SRC_ZIP}"

    if [[ "${IS_DIRTY}" -eq 0 ]]; then
        git -C "${ROOT_DIR}" archive --format=tar.gz --prefix="oyk-node-${VER}/" HEAD > "${OUT_DIR}/${SRC_TGZ}"
        git -C "${ROOT_DIR}" archive --format=zip    --prefix="oyk-node-${VER}/" HEAD > "${OUT_DIR}/${SRC_ZIP}"
    else
        SRC_STAGE="${TMP_WORK}/src-stage"
        mkdir -p "${SRC_STAGE}/oyk-node-${VER}"
        (
            cd "${ROOT_DIR}"
            git ls-files -z --cached --others --exclude-standard | \
                tar --null -T - -cf - | \
                tar -xf - -C "${SRC_STAGE}/oyk-node-${VER}"
        )
        rm -rf "${SRC_STAGE}/oyk-node-${VER}/dist" \
               "${SRC_STAGE}/oyk-node-${VER}/build" \
               "${SRC_STAGE}/oyk-node-${VER}/build-t41" \
               "${SRC_STAGE}/oyk-node-${VER}/build-pico"* \
               "${SRC_STAGE}/oyk-node-${VER}/ports/android/.gradle" \
               "${SRC_STAGE}/oyk-node-${VER}/ports/android/local.properties" \
               "${SRC_STAGE}/oyk-node-${VER}/ports/android/build" \
               "${SRC_STAGE}/oyk-node-${VER}/ports/android/app/build" \
               "${SRC_STAGE}/oyk-node-${VER}/ports/android/app/.cxx" \
               "${SRC_STAGE}/oyk-node-${VER}/ports/dos/LOGS"
        tar -C "${SRC_STAGE}" -czf "${OUT_DIR}/${SRC_TGZ}" "oyk-node-${VER}"
        (cd "${SRC_STAGE}" && zip -qr "${OUT_DIR}/${SRC_ZIP}" "oyk-node-${VER}")
    fi
    PACKAGED+=("${SRC_TGZ}" "${SRC_ZIP}")
    echo "  -> Packaged ${SRC_TGZ}"
    echo "  -> Packaged ${SRC_ZIP}"
fi

# 7. Generate SHA256SUMS
if [[ ${#PACKAGED[@]} -gt 0 ]]; then
    (
        cd "${OUT_DIR}"
        rm -f SHA256SUMS
        sha256sum oyknode-"${VER}"-* > SHA256SUMS
    )
    echo "Generated ${OUT_DIR}/SHA256SUMS"
fi

echo ""
echo "Summary:"
echo "  Packaged (${#PACKAGED[@]}): ${PACKAGED[*]:-none}"
if [[ ${#SKIPPED[@]} -gt 0 ]]; then
    echo "  Skipped  (${#SKIPPED[@]}):"
    for s in "${SKIPPED[@]}"; do
        echo "    - ${s}"
    done
fi
