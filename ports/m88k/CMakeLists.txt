# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# Host syntax/parity test target for Motorola 88K SVR4 (TUI + OSF/Motif 1.2)
if(BUILD_TESTS)
    add_executable(oyk_m88k_tests
        ${CMAKE_SOURCE_DIR}/tests/test_m88k.cpp
        ${CMAKE_SOURCE_DIR}/ports/sysv/sysv_core.c
        ${CMAKE_SOURCE_DIR}/ports/sysv/sysv_tui.c
        ${CMAKE_SOURCE_DIR}/ports/sysv/sysv_net.c
        ${CMAKE_SOURCE_DIR}/ports/compat/oyk_portable.cpp
        ${CMAKE_SOURCE_DIR}/ports/compat/oyk_yapp.cpp
        ${CMAKE_SOURCE_DIR}/ports/compat/oyk_rig.cpp
        ${CMAKE_SOURCE_DIR}/ports/compat/oyk_printer.cpp
        ${CMAKE_SOURCE_DIR}/ports/compat/oyk_tui.cpp
        ${CMAKE_SOURCE_DIR}/ports/compat/oyk_term.cpp
        ${CMAKE_SOURCE_DIR}/ports/compat/oyk_gui.cpp
    )
    target_include_directories(oyk_m88k_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/ports/sysv
        ${CMAKE_SOURCE_DIR}/ports/m88k
        ${CMAKE_SOURCE_DIR}/ports/compat
    )
    set_target_properties(oyk_m88k_tests PROPERTIES
        C_STANDARD 90
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_definitions(oyk_m88k_tests PRIVATE OYK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(oyk_m88k_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_m88k_tests)
    add_test(NAME oyk_m88k_tests COMMAND oyk_m88k_tests)
endif()
