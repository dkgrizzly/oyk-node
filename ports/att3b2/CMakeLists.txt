# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# Host parity verification target for AT&T 3B2 SVR3.2 (TUI + WIN/TCP)
if(BUILD_TESTS)
    add_executable(oyk_att3b2_tests
        ${CMAKE_SOURCE_DIR}/tests/test_att3b2.cpp
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
    target_include_directories(oyk_att3b2_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/ports/sysv
        ${CMAKE_SOURCE_DIR}/ports/att3b2
        ${CMAKE_SOURCE_DIR}/ports/compat
    )
    set_target_properties(oyk_att3b2_tests PROPERTIES
        C_STANDARD 90
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_definitions(oyk_att3b2_tests PRIVATE OYK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(oyk_att3b2_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_att3b2_tests)
    add_test(NAME oyk_att3b2_tests COMMAND oyk_att3b2_tests)
endif()
