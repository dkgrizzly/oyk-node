#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# Headless MS-DOS unit test runner using qemu-system-i386 + mtools.
#
# Creates a temporary 1.44MB FAT12 test floppy containing TESTDOS.EXE and
# RUN_TEST.BAT, injects a temporary AUTOEXEC.BAT into a working copy of a
# FreeDOS/MS-DOS boot image, boots QEMU headlessly with COM1 -> stdio, streams
# test output, and exits 0 only if all DOS unit tests pass.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_EXE="${SCRIPT_DIR}/testdos.exe"
BOOT_IMG="${FREEDOS_BOOT_IMG:-}"
TIMEOUT_SEC=45
MEMORY_MB=1
KEEP_ARTIFACTS=0
CHECK_ENV_ONLY=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --test-exe <path>      Path to 16-bit DOS testdos.exe (default: ports/dos/testdos.exe)
  --boot-img <path>      Path to FreeDOS/MS-DOS boot floppy or disk image (or set FREEDOS_BOOT_IMG)
  --timeout <seconds>    Max seconds to wait for QEMU test run (default: 45)
  --memory <MB>          Guest RAM in MB (default: 1 for 640KB + optional EMS/UMB)
  --keep-artifacts       Keep temporary QEMU disk images on exit for debugging
  --check-env            Verify host tools (qemu-system-i386, mformat, mcopy, mtype) and exit
  -h, --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test-exe)       TEST_EXE="$2"; shift 2 ;;
        --boot-img)       BOOT_IMG="$2"; shift 2 ;;
        --timeout)        TIMEOUT_SEC="$2"; shift 2 ;;
        --memory)         MEMORY_MB="$2"; shift 2 ;;
        --keep-artifacts) KEEP_ARTIFACTS=1; shift ;;
        --check-env)      CHECK_ENV_ONLY=1; shift ;;
        -h|--help)        usage; exit 0 ;;
        *)                echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

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

if [[ ! -f "$TEST_EXE" ]]; then
    echo "ERROR: DOS test executable not found: $TEST_EXE" >&2
    echo "Build it first with OpenWatcom v2:" >&2
    echo "  cd ports/dos && wmake -f makefile testdos.exe" >&2
    exit 2
fi

if [[ -z "$BOOT_IMG" || ! -f "$BOOT_IMG" ]]; then
    echo "ERROR: FreeDOS/MS-DOS boot image not provided or not found: '${BOOT_IMG}'" >&2
    echo "Provide a 1.44MB FreeDOS boot floppy (e.g. FD13FLOPPY / x86BOOT.img) via:" >&2
    echo "  --boot-img /path/to/freedos_boot.img  or  FREEDOS_BOOT_IMG=/path/to/freedos_boot.img" >&2
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
printf '@echo off\r\n%s:\r\necho === OYK QEMU DOS HARNESS === > COM1\r\n%s:\\TESTDOS.EXE > COM1\r\nif errorlevel 1 goto fail\r\necho PASS > %s:\\STATUS.TXT\r\necho === OYK_EXIT_CODE:0 === > COM1\r\ngoto poweroff\r\n:fail\r\necho FAIL > %s:\\STATUS.TXT\r\necho === OYK_EXIT_CODE:1 === > COM1\r\n:poweroff\r\nif exist A:\\FDAPM.COM A:\\FDAPM.COM POWEROFF\r\nif exist C:\\FDOS\\BIN\\FDAPM.COM C:\\FDOS\\BIN\\FDAPM.COM POWEROFF\r\n' \
    "$TEST_DRIVE" "$TEST_DRIVE" "$TEST_DRIVE" "$TEST_DRIVE" > "${WORKDIR}/RUN_TEST.BAT"

mcopy -i "$TEST_DISK" "$TEST_EXE" ::TESTDOS.EXE
mcopy -i "$TEST_DISK" "${WORKDIR}/RUN_TEST.BAT" ::RUN_TEST.BAT

# Inject AUTOEXEC.BAT into working copy of floppy boot image so it runs automatically
if [[ "$IS_FLOPPY_BOOT" -eq 1 ]]; then
    printf '@echo off\r\n%s:\\RUN_TEST.BAT\r\n' "$TEST_DRIVE" > "${WORKDIR}/AUTOEXEC.BAT"
    mcopy -o -i "$WORK_BOOT" "${WORKDIR}/AUTOEXEC.BAT" ::AUTOEXEC.BAT
    QEMU_DRIVES=(-fda "$WORK_BOOT" -fdb "$TEST_DISK" -boot a)
else
    # For a hard disk boot image, pass test floppy as A: and boot from C:
    # (expects C:\AUTOEXEC.BAT to invoke A:\RUN_TEST.BAT if present)
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
        # Give DOS a moment to flush STATUS.TXT / FDAPM poweroff, then stop QEMU cleanly
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
