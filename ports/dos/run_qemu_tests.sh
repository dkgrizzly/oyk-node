#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# Headless MS-DOS unit test runner using qemu-system-i386 + mtools.
#
# Creates a temporary 1.44MB FAT12 test floppy containing TESTDOS.EXE and
# RUN_TEST.BAT, prepares a temporary working copy of a FreeDOS/MS-DOS boot
# image (offering to download FreeDOS 1.4 Floppy Edition if none is present),
# boots QEMU headlessly with COM1 -> stdio, streams test output, and exits 0
# only if all DOS unit tests pass.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FREEDOS_DIR="${SCRIPT_DIR}/freedos"
DEFAULT_BOOT_IMG="${FREEDOS_DIR}/x86BOOT.img"
FREEDOS_URL="${FREEDOS_URL:-https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-FloppyEdition.zip}"
FREEDOS_FALLBACK_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official/FD13-FloppyEdition.zip"

TEST_EXE="${SCRIPT_DIR}/testdos.exe"
BOOT_IMG="${FREEDOS_BOOT_IMG:-}"
TIMEOUT_SEC=45
MEMORY_MB=2
KEEP_ARTIFACTS=0
CHECK_ENV_ONLY=0
AUTO_DOWNLOAD=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --test-exe <path>      Path to 16-bit DOS testdos.exe (default: ports/dos/testdos.exe)
  --boot-img <path>      Path to FreeDOS/MS-DOS boot floppy or disk image (or set FREEDOS_BOOT_IMG)
  --download-freedos     Download FreeDOS Floppy Edition automatically without prompting
  --timeout <seconds>    Max seconds to wait for QEMU test run (default: 45)
  --memory <MB>          Guest RAM in MB (default: 2 for 640KB + optional EMS 4.0)
  --keep-artifacts       Keep temporary QEMU disk images on exit for debugging
  --check-env            Verify host tools (qemu-system-i386, mformat, mcopy, mtype) and exit
  -h, --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test-exe)         TEST_EXE="$2"; shift 2 ;;
        --boot-img)         BOOT_IMG="$2"; shift 2 ;;
        --download-freedos) AUTO_DOWNLOAD=1; shift ;;
        --timeout)          TIMEOUT_SEC="$2"; shift 2 ;;
        --memory)           MEMORY_MB="$2"; shift 2 ;;
        --keep-artifacts)   KEEP_ARTIFACTS=1; shift ;;
        --check-env)        CHECK_ENV_ONLY=1; shift ;;
        -h|--help)          usage; exit 0 ;;
        *)                  echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

download_freedos_floppy() {
    mkdir -p "$FREEDOS_DIR"
    local zip_path="${FREEDOS_DIR}/FD-FloppyEdition.zip"

    echo "Downloading FreeDOS Floppy Edition from ${FREEDOS_URL}..."
    local dl_ok=0
    if command -v curl >/dev/null 2>&1; then
        if curl -fL --progress-bar "$FREEDOS_URL" -o "$zip_path"; then
            dl_ok=1
        elif curl -fL --progress-bar "$FREEDOS_FALLBACK_URL" -o "$zip_path"; then
            dl_ok=1
        fi
    elif command -v wget >/dev/null 2>&1; then
        if wget -O "$zip_path" "$FREEDOS_URL"; then
            dl_ok=1
        elif wget -O "$zip_path" "$FREEDOS_FALLBACK_URL"; then
            dl_ok=1
        fi
    else
        echo "ERROR: Neither 'curl' nor 'wget' is available to download FreeDOS." >&2
        return 1
    fi

    if [[ "$dl_ok" -ne 1 ]]; then
        rm -f "$zip_path"
        echo "ERROR: Failed to download FreeDOS Floppy Edition." >&2
        return 1
    fi

    echo "Extracting 1.44MB FreeDOS boot floppy image to ${DEFAULT_BOOT_IMG}..."
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$zip_path" "$DEFAULT_BOOT_IMG" <<'PYEOF'
import sys, zipfile

zip_path, out_path = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path, "r") as zf:
    names = zf.namelist()
    # Prefer 1.44MB x86BOOT.img (typically 144m/x86BOOT.img or x86BOOT.img)
    preferred = [n for n in names if n.lower().endswith("x86boot.img") and "144" in n.lower()]
    if not preferred:
        preferred = [n for n in names if n.lower().endswith("x86boot.img")]
    if not preferred:
        preferred = [n for n in names if n.lower().endswith(".img") and "boot" in n.lower()]
    if not preferred:
        sys.stderr.write("ERROR: Could not locate x86BOOT.img inside archive.\n")
        sys.exit(1)
    chosen = preferred[0]
    with zf.open(chosen) as src, open(out_path, "wb") as dst:
        dst.write(src.read())
    print(f"Extracted {chosen} -> {out_path}")
PYEOF
    elif command -v unzip >/dev/null 2>&1; then
        local member
        member="$(unzip -Z1 "$zip_path" | grep -iE '144.*x86boot\.img$|x86boot\.img$' | head -n 1 || true)"
        if [[ -z "$member" ]]; then
            echo "ERROR: Could not locate x86BOOT.img inside archive." >&2
            return 1
        fi
        unzip -p "$zip_path" "$member" > "$DEFAULT_BOOT_IMG"
        echo "Extracted ${member} -> ${DEFAULT_BOOT_IMG}"
    else
        echo "ERROR: Neither 'python3' nor 'unzip' is available to extract ${zip_path}." >&2
        return 1
    fi

    BOOT_IMG="$DEFAULT_BOOT_IMG"
    return 0
}

for tool in qemu-system-i386 mformat mcopy mtype; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: Required host tool '$tool' not found in PATH." >&2
        exit 2
    fi
done

if [[ "$CHECK_ENV_ONLY" -eq 1 ]]; then
    echo "OK: qemu-system-i386 and mtools (mformat, mcopy, mtype) are available."
    exit 0
fi

# Resolve default downloaded boot image if none specified
if [[ -z "$BOOT_IMG" && -f "$DEFAULT_BOOT_IMG" ]]; then
    BOOT_IMG="$DEFAULT_BOOT_IMG"
fi

if [[ -z "$BOOT_IMG" || ! -f "$BOOT_IMG" ]]; then
    if [[ "$AUTO_DOWNLOAD" -eq 1 ]]; then
        download_freedos_floppy || exit 2
    elif [[ -t 0 ]]; then
        echo "FreeDOS boot floppy image not found."
        read -r -p "Download FreeDOS 1.4 Floppy Edition (~4 MB) from ibiblio.org? [y/N]: " reply
        if [[ "$reply" =~ ^[Yy]$ ]]; then
            download_freedos_floppy || exit 2
        else
            echo "Skipped download. Provide an image with --boot-img <path> or re-run with --download-freedos." >&2
            exit 2
        fi
    else
        echo "ERROR: FreeDOS boot floppy not found: '${BOOT_IMG:-$DEFAULT_BOOT_IMG}'" >&2
        echo "Run with --download-freedos or pass --boot-img /path/to/x86BOOT.img" >&2
        exit 2
    fi
fi

if [[ ! -f "$TEST_EXE" ]]; then
    echo "ERROR: DOS test executable not found: $TEST_EXE" >&2
    echo "Build it first with OpenWatcom v2:" >&2
    echo "  cd ports/dos && wmake -f makefile testdos.exe" >&2
    exit 2
fi

WORKDIR="$(mktemp -d /tmp/oyk_dos_qemu.XXXXXX)"
cleanup() {
    if [[ "$KEEP_ARTIFACTS" -eq 0 ]]; then
        rm -rf "$WORKDIR"
    else
        echo "Kept temporary artifacts in: $WORKDIR"
    fi
}
trap cleanup EXIT

WORK_BOOT="${WORKDIR}/boot.img"
TEST_DISK="${WORKDIR}/test_disk.img"
SERIAL_LOG="${WORKDIR}/serial.log"
cp "$BOOT_IMG" "$WORK_BOOT"

# Determine whether boot image is a floppy (<= 2.88MB) or hard disk image
BOOT_SIZE="$(stat -c%s "$WORK_BOOT")"
if [[ "$BOOT_SIZE" -le 2949120 ]]; then
    IS_FLOPPY_BOOT=1
    TEST_DRIVE="B"
else
    IS_FLOPPY_BOOT=0
    TEST_DRIVE="A"
fi

# Create a clean 1.44MB FAT12 floppy image for TESTDOS.EXE and RUN_TEST.BAT
dd if=/dev/zero of="$TEST_DISK" bs=1024 count=1440 status=none
mformat -i "$TEST_DISK" -f 1440 ::

# Write RUN_TEST.BAT with DOS CRLF line endings
printf '@echo off\r\n%s:\r\necho === OYK QEMU DOS HARNESS === > COM1\r\n%s:\\TESTDOS.EXE > COM1\r\nif errorlevel 1 goto fail\r\necho PASS > %s:\\STATUS.TXT\r\necho === OYK_EXIT_CODE:0 === > COM1\r\ngoto poweroff\r\n:fail\r\necho FAIL > %s:\\STATUS.TXT\r\necho === OYK_EXIT_CODE:1 === > COM1\r\n:poweroff\r\nif exist A:\\FREEDOS\\BIN\\FDAPM.COM A:\\FREEDOS\\BIN\\FDAPM.COM POWEROFF\r\nif exist A:\\FDOS\\BIN\\FDAPM.COM A:\\FDOS\\BIN\\FDAPM.COM POWEROFF\r\nif exist A:\\FDAPM.COM A:\\FDAPM.COM POWEROFF\r\nif exist C:\\FDOS\\BIN\\FDAPM.COM C:\\FDOS\\BIN\\FDAPM.COM POWEROFF\r\n' \
    "$TEST_DRIVE" "$TEST_DRIVE" "$TEST_DRIVE" "$TEST_DRIVE" > "${WORKDIR}/RUN_TEST.BAT"

mcopy -i "$TEST_DISK" "$TEST_EXE" ::TESTDOS.EXE
mcopy -i "$TEST_DISK" "${WORKDIR}/RUN_TEST.BAT" ::RUN_TEST.BAT

# For floppy boot images, auto-detect COMMAND.COM location and inject unattended
# FDCONFIG.SYS + AUTOEXEC/FDAUTO.BAT into the temporary working copy.
if [[ "$IS_FLOPPY_BOOT" -eq 1 ]]; then
    SHELL_LINE='SHELL=A:\COMMAND.COM A:\ /E:512 /P=A:\AUTOEXEC.BAT'
    if mdir -i "$WORK_BOOT" ::/FREEDOS/BIN/COMMAND.COM >/dev/null 2>&1; then
        SHELL_LINE='SHELL=A:\FREEDOS\BIN\COMMAND.COM A:\FREEDOS\BIN /E:1024 /P=A:\AUTOEXEC.BAT'
    elif mdir -i "$WORK_BOOT" ::/FDOS/BIN/COMMAND.COM >/dev/null 2>&1; then
        SHELL_LINE='SHELL=A:\FDOS\BIN\COMMAND.COM A:\FDOS\BIN /E:1024 /P=A:\AUTOEXEC.BAT'
    fi
    printf 'FILES=20\r\nBUFFERS=20\r\nLASTDRIVE=Z\r\n%s\r\n' "$SHELL_LINE" \
        > "${WORKDIR}/FDCONFIG.SYS"
    printf '@echo off\r\n%s:\\RUN_TEST.BAT\r\n' "$TEST_DRIVE" > "${WORKDIR}/AUTOEXEC.BAT"
    mcopy -o -i "$WORK_BOOT" "${WORKDIR}/FDCONFIG.SYS" ::FDCONFIG.SYS
    mcopy -o -i "$WORK_BOOT" "${WORKDIR}/FDCONFIG.SYS" ::CONFIG.SYS
    mcopy -o -i "$WORK_BOOT" "${WORKDIR}/AUTOEXEC.BAT" ::AUTOEXEC.BAT
    mcopy -o -i "$WORK_BOOT" "${WORKDIR}/AUTOEXEC.BAT" ::FDAUTO.BAT
    QEMU_DRIVES=(-fda "$WORK_BOOT" -fdb "$TEST_DISK" -boot a)
else
    QEMU_DRIVES=(-hda "$WORK_BOOT" -fda "$TEST_DISK" -boot c)
fi

QEMU_CMD=(
    qemu-system-i386
    -M pc
    -cpu 486
    -m "${MEMORY_MB}"
    -display none
    -no-reboot
    -serial "file:${SERIAL_LOG}"
    "${QEMU_DRIVES[@]}"
)

: > "$SERIAL_LOG"
"${QEMU_CMD[@]}" >/dev/null 2>&1 &
QEMU_PID=$!

DEADLINE=$((SECONDS + TIMEOUT_SEC))
RESULT=""
while kill -0 "$QEMU_PID" 2>/dev/null; do
    if grep -q "=== OYK_EXIT_CODE:0 ===" "$SERIAL_LOG" 2>/dev/null; then
        RESULT="PASS"
        sleep 1
        kill "$QEMU_PID" 2>/dev/null || true
        break
    fi
    if grep -q "=== OYK_EXIT_CODE:1 ===" "$SERIAL_LOG" 2>/dev/null; then
        RESULT="FAIL"
        sleep 1
        kill "$QEMU_PID" 2>/dev/null || true
        break
    fi
    if [[ "$SECONDS" -ge "$DEADLINE" ]]; then
        RESULT="TIMEOUT"
        kill "$QEMU_PID" 2>/dev/null || true
        break
    fi
    sleep 0.2
done
wait "$QEMU_PID" 2>/dev/null || true

# Print captured DOS COM1 serial output for CTest visibility
tr -d '\r' < "$SERIAL_LOG"

# Fallback check via STATUS.TXT on the FAT12 test disk if QEMU powered off before sentinel poll
if [[ -z "$RESULT" ]]; then
    if grep -q "=== OYK_EXIT_CODE:0 ===" "$SERIAL_LOG" 2>/dev/null; then
        RESULT="PASS"
    elif grep -q "=== OYK_EXIT_CODE:1 ===" "$SERIAL_LOG" 2>/dev/null; then
        RESULT="FAIL"
    elif mtype -i "$TEST_DISK" ::STATUS.TXT 2>/dev/null | tr -d '\r\n' | grep -qx "PASS"; then
        RESULT="PASS"
    else
        RESULT="FAIL"
    fi
fi

if [[ "$RESULT" == "PASS" ]]; then
    exit 0
fi

echo "ERROR: MS-DOS QEMU test run ended with status: ${RESULT}" >&2
exit 1
