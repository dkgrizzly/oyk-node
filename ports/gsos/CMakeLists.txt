# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)
project(oyk_node_gsos LANGUAGES NONE)

set(VBCC_PREFIX "$ENV{HOME}/opt/vbcc" CACHE PATH "Path to installed vbcc 65816 toolchain")
find_program(VBCC_VC NAMES vc HINTS "${VBCC_PREFIX}/bin")

if(VBCC_VC)
    set(_gs_build "${CMAKE_CURRENT_BINARY_DIR}/gsos_out")
    set(_gs_app   "${_gs_build}/OYKNODE.SYS16")
    set(_gs_test  "${_gs_build}/OYKTEST.SYS16")

    add_custom_command(
        OUTPUT "${_gs_app}" "${_gs_test}"
        COMMAND make -C "${CMAKE_CURRENT_SOURCE_DIR}"
                VBCC_PREFIX="${VBCC_PREFIX}"
                BUILD_DIR="${_gs_build}"
                all
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/Makefile"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_main.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_ui.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_ui.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_net.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_net.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_capture.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_capture.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_core.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/gsos_core.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_gsos.c"
        COMMENT "Building Apple IIGS GS/OS S16 binaries (OYKNODE.SYS16, OYKTEST.SYS16)"
    )
    add_custom_target(oyk-node-gsos ALL DEPENDS "${_gs_app}" "${_gs_test}")
else()
    message(WARNING "vbcc 65816 toolchain not found in ${VBCC_PREFIX}; skipping GS/OS binary target.")
endif()
