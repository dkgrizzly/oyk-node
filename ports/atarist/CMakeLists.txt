# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)
project(oyk_node_atarist LANGUAGES NONE)

set(MINTELF_PREFIX "$ENV{HOME}/opt/mintelf" CACHE PATH "Path to installed m68k-atari-mintelf toolchain")
find_program(ATARI_CXX NAMES m68k-atari-mintelf-g++ HINTS "${MINTELF_PREFIX}/usr/bin")

if(ATARI_CXX)
    set(_at_build "${CMAKE_CURRENT_BINARY_DIR}/atarist_out")
    set(_at_app   "${_at_build}/OYKNODE.PRG")
    set(_at_test  "${_at_build}/OYKTEST.PRG")

    add_custom_command(
        OUTPUT "${_at_app}" "${_at_test}"
        COMMAND make -C "${CMAKE_CURRENT_SOURCE_DIR}"
                MINTELF_PREFIX="${MINTELF_PREFIX}"
                BUILD_DIR="${_at_build}"
                all
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/Makefile"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_main.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_ui.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_ui.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_net.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_net.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_policy.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_policy.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_capture.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/atari_capture.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_atarist.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_portable.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_gui.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_gui_theme.cpp"
        COMMENT "Building Atari ST / STE / TT / Falcon GEM binaries (OYKNODE.PRG, OYKTEST.PRG)"
    )
    add_custom_target(oyk-node-atarist ALL DEPENDS "${_at_app}" "${_at_test}")
else()
    message(WARNING "m68k-atari-mintelf-g++ toolchain not found in ${MINTELF_PREFIX}; skipping Atari ST binary target.")
endif()
