# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)
project(oyk_node_amiga LANGUAGES NONE)

set(AMIGA_GCC_PREFIX "$ENV{HOME}/opt/amiga-gcc" CACHE PATH "Path to installed m68k-amigaos-gcc toolchain")
find_program(AMIGA_CXX NAMES m68k-amigaos-g++ HINTS "${AMIGA_GCC_PREFIX}/bin")

if(AMIGA_CXX)
    set(_am_build "${CMAKE_CURRENT_BINARY_DIR}/amiga_out")
    set(_am_app   "${_am_build}/OYKNode")
    set(_am_test  "${_am_build}/OYKTest")

    add_custom_command(
        OUTPUT "${_am_app}" "${_am_test}"
        COMMAND make -C "${CMAKE_CURRENT_SOURCE_DIR}"
                AMIGA_GCC_PREFIX="${AMIGA_GCC_PREFIX}"
                BUILD_DIR="${_am_build}"
                all
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/Makefile"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_main.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_ui.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_ui.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_net.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_net.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_policy.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_policy.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_capture.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga_capture.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_amiga.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_portable.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_gui.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_gui_theme.cpp"
        COMMENT "Building Amiga Workbench 68k binaries (OYKNode, OYKTest)"
    )
    add_custom_target(oyk-node-amiga ALL DEPENDS "${_am_app}" "${_am_test}")
else()
    message(WARNING "m68k-amigaos-g++ toolchain not found in ${AMIGA_GCC_PREFIX}; skipping Amiga binary target.")
endif()
