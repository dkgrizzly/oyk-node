# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)
project(oyk_node_macclassic LANGUAGES NONE)

set(RETRO68_PREFIX "$ENV{HOME}/opt/Retro68" CACHE PATH "Path to installed Retro68 toolchain")
find_program(RETRO68_68K_CXX NAMES m68k-apple-macos-g++ HINTS "${RETRO68_PREFIX}/bin")
find_program(RETRO68_PPC_CXX NAMES powerpc-apple-macos-g++ HINTS "${RETRO68_PREFIX}/bin")
find_program(RETRO68_REZ     NAMES Rez                    HINTS "${RETRO68_PREFIX}/bin")
find_program(RETRO68_MAKEPEF NAMES MakePEF                HINTS "${RETRO68_PREFIX}/bin")

if(RETRO68_68K_CXX AND RETRO68_PPC_CXX AND RETRO68_REZ AND RETRO68_MAKEPEF)
    set(_mc_build "${CMAKE_CURRENT_BINARY_DIR}/macclassic_out")
    set(_mc_fat   "${_mc_build}/OYKNode-Fat.bin")
    set(_mc_test  "${_mc_build}/OYKTest.bin")

    add_custom_command(
        OUTPUT "${_mc_fat}" "${_mc_test}"
        COMMAND make -C "${CMAKE_CURRENT_SOURCE_DIR}"
                RETRO68_PREFIX="${RETRO68_PREFIX}"
                BUILD_DIR="${_mc_build}"
                all
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/Makefile"
            "${CMAKE_CURRENT_SOURCE_DIR}/oyk_mac.r"
            "${CMAKE_CURRENT_SOURCE_DIR}/mac_main.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/mac_ui.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/mac_ui.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/mac_net.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/mac_net.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/mac_capture.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/mac_capture.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_macclassic.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_portable.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_gui.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_gui_theme.cpp"
        COMMENT "Building Classic Macintosh 68k + PowerPC Fat Binary (OYKNode-Fat.bin)"
    )
    add_custom_target(oyk-node-macclassic ALL DEPENDS "${_mc_fat}" "${_mc_test}")
else()
    message(WARNING "Retro68 toolchain not found in ${RETRO68_PREFIX}; skipping Classic Macintosh binary target.")
endif()
