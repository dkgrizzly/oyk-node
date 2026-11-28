# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

if(BUILD_OPENVMS_PORT)
    add_executable(oyk_openvms_console_host
        vms_console_main.c
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_core.c"
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_tui.c"
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_net.c"
    )
    target_include_directories(oyk_openvms_console_host PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/ports/sysv"
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oyk_openvms_console_host PRIVATE -std=c89 -pedantic -Wall -Wextra -Werror)
    endif()

    add_executable(oyk_openvms_native_test
        test_vms_native.c
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_core.c"
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_tui.c"
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_net.c"
    )
    target_include_directories(oyk_openvms_native_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/ports/sysv"
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oyk_openvms_native_test PRIVATE -std=c89 -pedantic -Wall -Wextra -Werror)
    endif()
endif()

if(BUILD_TESTS)
    add_executable(oyk_openvms_tests
        "${CMAKE_SOURCE_DIR}/tests/test_openvms.cpp"
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_core.c"
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_tui.c"
        "${CMAKE_SOURCE_DIR}/ports/sysv/sysv_net.c"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_tui.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_term.cpp"
        "${CMAKE_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
    )
    target_include_directories(oyk_openvms_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/ports/sysv"
        "${CMAKE_SOURCE_DIR}/ports/compat"
    )
    set_target_properties(oyk_openvms_tests PROPERTIES
        C_STANDARD 90
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oyk_openvms_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(COMMAND oyk_apply_host_hardening)
        oyk_apply_host_hardening(oyk_openvms_tests)
    endif()
    add_test(NAME oyk_openvms_tests COMMAND oyk_openvms_tests)
endif()
