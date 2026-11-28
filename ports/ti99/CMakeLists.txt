# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

if(BUILD_TI99_PORT)
    add_executable(oyk_ti99_host
        ti99_main.c
        ti99_core.c
        ti99_sams.c
        ti99_vdp.c
        ti99_tui.c
        ti99_tipi.c
        ti99_rs232.c
    )
    target_include_directories(oyk_ti99_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oyk_ti99_host PRIVATE -std=c99 -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(BUILD_TESTS)
    add_executable(oyk_ti99_tests
        "${CMAKE_SOURCE_DIR}/tests/test_ti99.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ti99_core.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ti99_sams.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ti99_vdp.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ti99_tui.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ti99_tipi.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ti99_rs232.c"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_tui.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_term.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
    )
    target_include_directories(oyk_ti99_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/ports/compat"
    )
    set_target_properties(oyk_ti99_tests PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oyk_ti99_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(COMMAND oyk_apply_host_hardening)
        oyk_apply_host_hardening(oyk_ti99_tests)
    endif()
    add_test(NAME oyk_ti99_tests COMMAND oyk_ti99_tests)
endif()
