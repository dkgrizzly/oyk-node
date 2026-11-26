#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build_macos}"
ARCHS="${OYK_MACOS_ARCHS:-arm64;x86_64}"
MIN_VER="${OYK_MACOS_MIN_VERSION:-11.0}"
PORTABLE_FLAG="${OYK_MACOS_PORTABLE_CORE:-OFF}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DBUILD_MACOS_PORT=ON \
    -DOYK_MACOS_PORTABLE_CORE="${PORTABLE_FLAG}" \
    -DOYK_MACOS_MIN_VERSION="${MIN_VER}" \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MIN_VER}"

cmake --build "${BUILD_DIR}" -j
ctest --test-dir "${BUILD_DIR}" --output-on-failure

APP_BUNDLE="$(find "${BUILD_DIR}" -maxdepth 3 -name 'OYK Node.app' | head -n 1)"
if [[ -n "${APP_BUNDLE}" ]] && command -v hdiutil >/dev/null 2>&1; then
    DMG_OUT="${BUILD_DIR}/OYK-Node-macOS-Universal.dmg"
    rm -f "${DMG_OUT}"
    hdiutil create -volname "OYK Node" -srcfolder "${APP_BUNDLE}" -ov -format UDZO "${DMG_OUT}"
    echo "Created ${DMG_OUT}"
fi
