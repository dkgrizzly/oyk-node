# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

if(BUILD_OS2_PORT)
    add_executable(oyk_os2_console_host
        os2_console_main.cpp
        os2_net.cpp
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_tui.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_term.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_gui_theme.cpp"
    )
    target_include_directories(oyk_os2_console_host PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/ports/compat"
    )
    set_target_properties(oyk_os2_console_host PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oyk_os2_console_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(BUILD_TESTS)
    add_executable(oyk_os2_tests
        "${CMAKE_SOURCE_DIR}/tests/test_os2.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/os2_net.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_tui.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_term.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_gui_theme.cpp"
    )
    target_include_directories(oyk_os2_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/ports/compat"
    )
    set_target_properties(oyk_os2_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oyk_os2_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(COMMAND oyk_apply_host_hardening)
        oyk_apply_host_hardening(oyk_os2_tests)
    endif()
    add_test(NAME oyk_os2_tests COMMAND oyk_os2_tests)
endif()
