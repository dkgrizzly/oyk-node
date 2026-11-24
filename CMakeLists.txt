# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)
project(oyk-node VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_NCURSES_UI   "Build with ncurses local text-mode UI" ON)
option(BUILD_GTK2_UI      "Automatically build X11 GTK+ 2.0 GUI when libgtk2.0-dev is installed" ON)
option(BUILD_TESTS        "Build unit and integration tests" ON)

# Retro & Windows ports are ONLY built when explicitly requested
option(BUILD_MSDOS_PORT   "Add target to build MS-DOS (OpenWatcom v2/1.9 + mTCP) executable" OFF)
option(BUILD_WIN16_PORT   "Add target to build 16-bit Windows 3.1 (OpenWatcom) executable" OFF)
option(BUILD_WINDOWS_PORT "Build 32/64-bit Windows native GUI executable (Win95..ME, NT 3.51..Win11)" OFF)
set(MTCP_DIR "$ENV{HOME}/mTCP" CACHE PATH "Path to Michael Brutman's mTCP source tree (for BUILD_MSDOS_PORT)")
set(FREEDOS_BOOT_IMG "" CACHE FILEPATH "Path to FreeDOS/MS-DOS boot floppy/disk image for QEMU DOS unit tests")

if(NOT WIN32)
    find_package(Threads REQUIRED)

    add_library(oyknode_core STATIC
        src/core/line_ending.cpp
        src/ax25/callsign.cpp
        src/ax25/crc.cpp
        src/ax25/frame.cpp
        src/radio/interface_registry.cpp
        src/radio/kiss.cpp
        src/radio/axudp_interface.cpp
        src/radio/axip_interface.cpp
        src/radio/kiss_serial_interface.cpp
        src/radio/kiss_tcp_interface.cpp
        src/radio/telnet_interface.cpp
        src/node/logger.cpp
        src/node/ban_list.cpp
        src/node/node_config.cpp
        src/node/session.cpp
        src/node/command_handler.cpp
        src/node/node_engine.cpp
    )

    target_include_directories(oyknode_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(oyknode_core
        PUBLIC
            Threads::Threads
    )

    target_compile_options(oyknode_core
        PRIVATE
            -Wall -Wextra -Wpedantic
    )

    add_executable(oyk-node
        src/main.cpp
    )

    target_link_libraries(oyk-node
        PRIVATE
            oyknode_core
    )

    target_compile_options(oyk-node
        PRIVATE
            -Wall -Wextra -Wpedantic
    )

    if(BUILD_NCURSES_UI)
        set(CURSES_NEED_NCURSES TRUE)
        set(CURSES_NEED_WIDE TRUE)
        find_package(Curses REQUIRED)
        target_sources(oyk-node PRIVATE src/ui/ncurses_ui.cpp)
        target_include_directories(oyk-node PRIVATE ${CURSES_INCLUDE_DIRS})
        target_link_libraries(oyk-node PRIVATE ${CURSES_LIBRARIES})
        target_compile_definitions(oyk-node PRIVATE OYK_HAS_NCURSES=1)
    endif()

    # Automatically build X11 GTK+ 2.0 GUI if libgtk2.0-dev is installed
    if(BUILD_GTK2_UI)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GTK2 QUIET gtk+-2.0)
        endif()
        if(GTK2_FOUND)
            message(STATUS "Found GTK+ 2.0 (${GTK2_VERSION}): enabling oyk-node-gtk X11 GUI")
            add_executable(oyk-node-gtk
                src/main_gtk.cpp
                src/ui/gtk2_ui.cpp
            )
            target_include_directories(oyk-node-gtk SYSTEM PRIVATE ${GTK2_INCLUDE_DIRS})
            target_link_directories(oyk-node-gtk PRIVATE ${GTK2_LIBRARY_DIRS})
            target_link_libraries(oyk-node-gtk PRIVATE oyknode_core ${GTK2_LIBRARIES})
            target_compile_options(oyk-node-gtk PRIVATE ${GTK2_CFLAGS_OTHER} -Wall -Wextra)
        else()
            message(STATUS "libgtk2.0-dev (gtk+-2.0) not found; skipping oyk-node-gtk X11 GUI")
        endif()
    endif()
endif()

# Optional MS-DOS (OpenWatcom v2/1.9 + mTCP) targets - built only if requested
if(BUILD_MSDOS_PORT)
    find_program(WMAKE_EXE wmake
        HINTS
            "$ENV{WATCOM}/binl64"
            "$ENV{WATCOM}/binl"
            "/opt/watcom/binl64"
            "/opt/watcom/binl"
            "$ENV{HOME}/open-watcom-v2/rel/binl64"
            "$ENV{HOME}/open-watcom-v2/rel/binl"
    )
    if(NOT WMAKE_EXE)
        message(FATAL_ERROR "BUILD_MSDOS_PORT requires OpenWatcom 'wmake' in PATH, WATCOM, /opt/watcom/binl64, or ~/open-watcom-v2/rel/binl64")
    endif()
    set(_mtcp_arg "")
    if(MTCP_DIR)
        set(_mtcp_arg "MTCP_DIR=${MTCP_DIR}")
    endif()
    add_custom_target(oyk-node-dos
        COMMAND ${WMAKE_EXE} -f makefile oyknode.exe ${_mtcp_arg}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos
        COMMENT "Building MS-DOS (8088/V20 + mTCP) executable with OpenWatcom"
    )
    add_custom_target(oyk-dos-tests-build
        COMMAND ${WMAKE_EXE} -f makefile testdos.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos
        COMMENT "Building 16-bit MS-DOS unit test executable (testdos.exe) with OpenWatcom"
    )
endif()

# Optional 16-bit Windows 3.1 (OpenWatcom 1.9/v2) target - built only if requested
if(BUILD_WIN16_PORT)
    find_program(WMAKE_EXE wmake
        HINTS
            "$ENV{WATCOM}/binl64"
            "$ENV{WATCOM}/binl"
            "/opt/watcom/binl64"
            "/opt/watcom/binl"
            "$ENV{HOME}/open-watcom-v2/rel/binl64"
            "$ENV{HOME}/open-watcom-v2/rel/binl"
    )
    if(NOT WMAKE_EXE)
        message(FATAL_ERROR "BUILD_WIN16_PORT requires OpenWatcom 'wmake' in PATH, WATCOM, or /opt/watcom/binl64")
    endif()
    add_custom_target(oyk-node-win16
        COMMAND ${WMAKE_EXE} -f makefile.win16
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/windows
        COMMENT "Building 16-bit Windows 3.1 executable with OpenWatcom 1.9"
    )
endif()

# Optional 32/64-bit Windows native GUI targets - built only if requested
if(BUILD_WINDOWS_PORT)
    if(WIN32)
        add_subdirectory(ports/windows)
    else()
        set(_win_targets "")

        find_program(MINGW64_CXX x86_64-w64-mingw32-g++)
        find_program(MINGW64_WINDRES x86_64-w64-mingw32-windres)
        if(MINGW64_CXX AND MINGW64_WINDRES)
            message(STATUS "Found MinGW-w64 64-bit (${MINGW64_CXX}): enabling oyk-node-win64 (oykw64.exe)")
            set(_win64_res "${CMAKE_CURRENT_BINARY_DIR}/oyk_win64.res")
            set(_win64_exe "${CMAKE_CURRENT_BINARY_DIR}/oykw64.exe")
            add_custom_command(
                OUTPUT "${_win64_res}"
                COMMAND ${MINGW64_WINDRES}
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows"
                        -i "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/oyk_win.rc"
                        -O coff -o "${_win64_res}"
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/oyk_win.rc"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/resource.h"
                COMMENT "Compiling 64-bit Windows resources (oyk_win64.res)"
            )
            add_custom_command(
                OUTPUT "${_win64_exe}"
                COMMAND ${MINGW64_CXX}
                        -std=c++98 -Os -fno-exceptions -fno-rtti -Wall -Wextra
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows"
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_main.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                        "${_win64_res}"
                        -o "${_win64_exe}"
                        -mwindows -static -lwsock32 -luser32 -lgdi32
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_main.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/resource.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.hpp"
                    "${_win64_res}"
                COMMENT "Building 64-bit Windows GUI executable (oykw64.exe)"
            )
            add_custom_target(oyk-node-win64 ALL DEPENDS "${_win64_exe}")
            list(APPEND _win_targets oyk-node-win64)
        endif()

        find_program(MINGW32_CXX i686-w64-mingw32-g++)
        find_program(MINGW32_WINDRES i686-w64-mingw32-windres)
        if(MINGW32_CXX AND MINGW32_WINDRES)
            message(STATUS "Found MinGW-w64 32-bit (${MINGW32_CXX}): enabling oyk-node-win32 (oykw32.exe)")
            set(_win32_res "${CMAKE_CURRENT_BINARY_DIR}/oyk_win32.res")
            set(_win32_exe "${CMAKE_CURRENT_BINARY_DIR}/oykw32.exe")
            add_custom_command(
                OUTPUT "${_win32_res}"
                COMMAND ${MINGW32_WINDRES}
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows"
                        -i "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/oyk_win.rc"
                        -O coff -o "${_win32_res}"
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/oyk_win.rc"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/resource.h"
                COMMENT "Compiling 32-bit Windows resources (oyk_win32.res)"
            )
            add_custom_command(
                OUTPUT "${_win32_exe}"
                COMMAND ${MINGW32_CXX}
                        -std=c++98 -Os -fno-exceptions -fno-rtti -Wall -Wextra
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows"
                        -I "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_main.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                        "${_win32_res}"
                        -o "${_win32_exe}"
                        -mwindows -static -lwsock32 -luser32 -lgdi32
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_main.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/resource.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.hpp"
                    "${_win32_res}"
                COMMENT "Building 32-bit Windows GUI executable (oykw32.exe)"
            )
            add_custom_target(oyk-node-win32 ALL DEPENDS "${_win32_exe}")
            list(APPEND _win_targets oyk-node-win32)
        endif()

        if(NOT _win_targets)
            find_program(WMAKE_EXE wmake
                HINTS
                    "$ENV{WATCOM}/binl64"
                    "$ENV{WATCOM}/binl"
                    "$ENV{HOME}/open-watcom-v2/rel/binl64"
                    "$ENV{HOME}/open-watcom-v2/rel/binl"
            )
            if(WMAKE_EXE)
                add_custom_target(oyk-node-win32 ALL
                    COMMAND ${WMAKE_EXE} -f makefile.win32
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/windows
                    COMMENT "Building 32-bit Windows executable with OpenWatcom"
                )
                list(APPEND _win_targets oyk-node-win32)
            else()
                message(FATAL_ERROR "BUILD_WINDOWS_PORT requires MinGW-w64 (x86_64-w64-mingw32-g++ / i686-w64-mingw32-g++), a Windows CMake toolchain, or OpenWatcom 'wmake'")
            endif()
        endif()

        add_custom_target(oyk-node-win DEPENDS ${_win_targets})
    endif()
endif()

if(BUILD_TESTS AND NOT WIN32)
    enable_testing()

    add_executable(oyk_node_tests tests/test_node.cpp)
    target_link_libraries(oyk_node_tests PRIVATE oyknode_core)
    target_compile_options(oyk_node_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME oyk_node_tests COMMAND oyk_node_tests)

    # Strict C++98 verification of the portable MS-DOS / Win16 / Win32 shared core
    add_executable(oyk_portable_tests
        tests/test_portable.cpp
        ports/compat/oyk_portable.cpp
    )
    target_include_directories(oyk_portable_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat)
    set_target_properties(oyk_portable_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_portable_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME oyk_portable_tests COMMAND oyk_portable_tests)

    # Optional headless 16-bit MS-DOS unit tests in qemu-system-i386
    if(BUILD_MSDOS_PORT)
        find_program(QEMU_I386_EXE qemu-system-i386)
        set(_dos_boot_img "${FREEDOS_BOOT_IMG}")
        if(NOT _dos_boot_img AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ports/dos/freedos/x86BOOT.img")
            set(_dos_boot_img "${CMAKE_CURRENT_SOURCE_DIR}/ports/dos/freedos/x86BOOT.img")
        endif()
        if(QEMU_I386_EXE AND _dos_boot_img)
            message(STATUS "Enabling oyk_dos_qemu_tests via ${QEMU_I386_EXE} (boot: ${_dos_boot_img})")
            add_test(NAME oyk_dos_qemu_tests
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos/run_qemu_tests.sh
                        --test-exe ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos/testdos.exe
                        --boot-img ${_dos_boot_img}
            )
        else()
            message(STATUS "Skipping oyk_dos_qemu_tests (run ports/dos/run_qemu_tests.sh --download-freedos or pass -DFREEDOS_BOOT_IMG=...)")
        endif()
    endif()
endif()
