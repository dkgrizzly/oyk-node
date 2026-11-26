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
option(BUILD_MSDOS_PORT    "Add target to build MS-DOS (OpenWatcom v2/1.9 + mTCP) executable" OFF)
option(BUILD_MSDOS_GFX_PORT "Add target to build 386+ graphical MS-DOS (OpenWatcom + VBE/VGA/EGA) executable" OFF)
option(BUILD_WIN16_PORT    "Add target to build 16-bit Windows 3.1 (OpenWatcom) executable" OFF)
option(BUILD_WINDOWS_PORT  "Build 32/64-bit Windows native GUI executable (Win95..ME, NT 3.51..Win11)" OFF)
option(BUILD_TINYTMUX_PORT "Add target to build Teensy 4.1 / IMXRT1060 (TinyTMUX) firmware" OFF)
option(BUILD_PICO_PORT     "Add targets to build Raspberry Pi Pico (RP2040/RP2350) firmware UF2s" OFF)
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
        src/radio/rig_control.cpp
        src/protocol/yapp.cpp
        src/node/logger.cpp
        src/node/ban_list.cpp
        src/node/node_config.cpp
        src/node/session.cpp
        src/node/bbs_server.cpp
        src/node/chat_room.cpp
        src/node/command_handler.cpp
        src/node/node_engine.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
    )

    target_include_directories(oyknode_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
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
    get_filename_component(_ow_dos_bin "${WMAKE_EXE}" DIRECTORY)
    get_filename_component(_ow_dos_root "${_ow_dos_bin}" DIRECTORY)
    set(_mtcp_arg "")
    if(MTCP_DIR)
        set(_mtcp_arg "MTCP_DIR=${MTCP_DIR}")
    endif()
    add_custom_target(oyk-node-dos
        COMMAND ${CMAKE_COMMAND} -E env
                "WATCOM=${_ow_dos_root}"
                "PATH=${_ow_dos_bin}:$ENV{PATH}"
                "INCLUDE=${_ow_dos_root}/h"
                ${WMAKE_EXE} -f makefile oyknode.exe ${_mtcp_arg}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos
        COMMENT "Building MS-DOS (8088/V20 + mTCP) executable with OpenWatcom"
    )
    add_custom_target(oyk-dos-tests-build
        COMMAND ${CMAKE_COMMAND} -E env
                "WATCOM=${_ow_dos_root}"
                "PATH=${_ow_dos_bin}:$ENV{PATH}"
                "INCLUDE=${_ow_dos_root}/h"
                ${WMAKE_EXE} -f makefile testdos.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos
        COMMENT "Building 16-bit MS-DOS unit test executable (testdos.exe) with OpenWatcom"
    )
endif()

# Optional 16-bit Windows 3.1 (OpenWatcom v2/1.9) target - built only if requested
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
    get_filename_component(_ow_win16_bin "${WMAKE_EXE}" DIRECTORY)
    get_filename_component(_ow_win16_root "${_ow_win16_bin}" DIRECTORY)
    add_custom_target(oyk-node-win16
        COMMAND ${CMAKE_COMMAND} -E env
                "WATCOM=${_ow_win16_root}"
                "PATH=${_ow_win16_bin}:$ENV{PATH}"
                "INCLUDE=${_ow_win16_root}/h:${_ow_win16_root}/h/win"
                ${WMAKE_EXE} -f makefile.win16 WATCOM_DIR=${_ow_win16_root}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/windows
        COMMENT "Building 16-bit Windows 3.1 executable (oykw16.exe) with OpenWatcom"
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
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_service.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_term.cpp"
                        "${_win64_res}"
                        -o "${_win64_exe}"
                        -mwindows -static -lwsock32 -luser32 -lgdi32 -lcomdlg32 -ladvapi32
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_main.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_service.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_service.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/resource.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_yapp.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_rig.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_printer.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_term.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_term.hpp"
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
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_service.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_term.cpp"
                        "${_win32_res}"
                        -o "${_win32_exe}"
                        -mwindows -static -lwsock32 -luser32 -lgdi32 -lcomdlg32 -ladvapi32
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_main.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_service.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/win_service.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/resource.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_portable.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_yapp.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_yapp.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_rig.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_rig.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_printer.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_printer.hpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_term.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_term.hpp"
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
                    "/opt/watcom/binl64"
                    "/opt/watcom/binl"
                    "$ENV{HOME}/open-watcom-v2/rel/binl64"
                    "$ENV{HOME}/open-watcom-v2/rel/binl"
            )
            if(WMAKE_EXE)
                get_filename_component(_ow_win32_bin "${WMAKE_EXE}" DIRECTORY)
                get_filename_component(_ow_win32_root "${_ow_win32_bin}" DIRECTORY)
                add_custom_target(oyk-node-win32 ALL
                    COMMAND ${CMAKE_COMMAND} -E env
                            "WATCOM=${_ow_win32_root}"
                            "PATH=${_ow_win32_bin}:$ENV{PATH}"
                            "INCLUDE=${_ow_win32_root}/h:${_ow_win32_root}/h/nt"
                            ${WMAKE_EXE} -f makefile.win32 WATCOM_DIR=${_ow_win32_root}
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

# Optional Teensy 4.1 / IMXRT1060 (TinyTMUX) firmware target - built only if requested
if(BUILD_TINYTMUX_PORT)
    set(_tinytmux_build_dir "${CMAKE_CURRENT_BINARY_DIR}/tinytmux")
    set(_tinytmux_cmake_args "")
    if(DEFINED OYK_TINYTMUX_FETCH_QNETHERNET)
        list(APPEND _tinytmux_cmake_args "-DOYK_TINYTMUX_FETCH_QNETHERNET=${OYK_TINYTMUX_FETCH_QNETHERNET}")
    endif()
    foreach(_var ARDUINO_DIR QNETHERNET_DIR)
        if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
            list(APPEND _tinytmux_cmake_args "-D${_var}=${${_var}}")
        endif()
    endforeach()
    add_custom_target(oyk-tinytmux
        COMMAND ${CMAKE_COMMAND} -S "${CMAKE_CURRENT_SOURCE_DIR}/ports/tinytmux"
                -B "${_tinytmux_build_dir}"
                ${_tinytmux_cmake_args}
        COMMAND ${CMAKE_COMMAND} --build "${_tinytmux_build_dir}" -j
        COMMENT "Building Teensy 4.1 / TinyTMUX firmware via Teensyduino (oyk-tinytmux.hex)"
    )
endif()

# Optional Raspberry Pi Pico (RP2040 / RP2350) firmware targets - built only if requested
if(BUILD_PICO_PORT)
    set(_pico_sdk_found "")
    if(DEFINED ENV{PICO_SDK_PATH} AND EXISTS "$ENV{PICO_SDK_PATH}/pico_sdk_init.cmake")
        set(_pico_sdk_found "$ENV{PICO_SDK_PATH}")
    else()
        file(GLOB _dot_sdks "$ENV{HOME}/.pico-sdk/sdk/*")
        foreach(_d IN LISTS _dot_sdks "$ENV{HOME}/pico-sdk")
            if(EXISTS "${_d}/pico_sdk_init.cmake" AND EXISTS "${_d}/lib/cyw43-driver/src" AND EXISTS "${_d}/lib/lwip/src")
                set(_pico_sdk_found "${_d}")
                break()
            endif()
        endforeach()
        if(NOT _pico_sdk_found AND EXISTS "$ENV{HOME}/pico-sdk/pico_sdk_init.cmake")
            set(_pico_sdk_found "$ENV{HOME}/pico-sdk")
        endif()
    endif()
    file(GLOB _arm_gcc_cands "$ENV{HOME}/.pico-sdk/toolchain/*/bin/arm-none-eabi-gcc")
    find_program(PICO_ARM_GCC_EXE arm-none-eabi-gcc
        HINTS "$ENV{HOME}/.pico-sdk/toolchain/15_2_Rel1/bin" ${_arm_gcc_cands}
    )
    if(NOT _pico_sdk_found)
        message(FATAL_ERROR "BUILD_PICO_PORT requires Pico SDK in ~/pico-sdk or ~/.pico-sdk/sdk/<ver>")
    endif()
    if(NOT PICO_ARM_GCC_EXE)
        message(FATAL_ERROR "BUILD_PICO_PORT requires arm-none-eabi-gcc in PATH or ~/.pico-sdk/toolchain/<ver>/bin")
    endif()
    set(PICO_BOARDS pico_w pico2_w waveshare_core2350b pimoroni_pga2350)
    set(_pico_board_targets)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pico")
    foreach(_pb IN LISTS PICO_BOARDS)
        set(_pb_dir "${CMAKE_CURRENT_BINARY_DIR}/pico/${_pb}")
        add_custom_target(oyk-pico-${_pb}
            COMMAND ${CMAKE_COMMAND} -S "${CMAKE_CURRENT_SOURCE_DIR}/ports/pico"
                    -B "${_pb_dir}"
                    "-DPICO_BOARD=${_pb}"
                    "-DPICO_SDK_PATH=${_pico_sdk_found}"
            COMMAND ${CMAKE_COMMAND} --build "${_pb_dir}" -j
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_pb_dir}/oyk-pico-${_pb}.uf2"
                    "${CMAKE_CURRENT_BINARY_DIR}/pico/oyk-pico-${_pb}.uf2"
            COMMENT "Building Raspberry Pi Pico UF2 firmware for ${_pb}"
        )
        list(APPEND _pico_board_targets oyk-pico-${_pb})
    endforeach()
    add_custom_target(oyk-pico DEPENDS ${_pico_board_targets})
endif()

if(BUILD_TESTS AND NOT WIN32)
    enable_testing()

    add_executable(oyk_node_tests tests/test_node.cpp)
    target_link_libraries(oyk_node_tests PRIVATE oyknode_core)
    target_compile_options(oyk_node_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME oyk_node_tests
        COMMAND oyk_node_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Strict C++98 verification of the portable MS-DOS / Win16 / Win32 shared core
    add_executable(oyk_portable_tests
        tests/test_portable.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
        ports/dos/dos_usb.cpp
        ports/dos/dos_video.cpp
        ports/dos/dos_mouse.cpp
        ports/dos/ems.cpp
        ports/dos/dos_xms.cpp
        ports/dos/dos_font8x16.cpp
        ports/dos/dos_gfx.cpp
        ports/windows/win_service.cpp
    )
    target_include_directories(oyk_portable_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/windows
    )
    set_target_properties(oyk_portable_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_portable_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME oyk_portable_tests COMMAND oyk_portable_tests)

    # Strict C++98 host verification of Teensy 4.1 / IMXRT1060 (TinyTMUX) PSRAM, FS hooks, USB Host, PPP, Ethernet bridge & VT100 console
    add_executable(oyk_tinytmux_tests
        tests/test_tinytmux.cpp
        ports/tinytmux/oyk_tinytmux/board_tinytmux.cpp
        ports/tinytmux/oyk_tinytmux/tinytmux_psram.cpp
        ports/tinytmux/oyk_tinytmux/tinytmux_fs.cpp
        ports/tinytmux/oyk_tinytmux/tinytmux_enet.cpp
        ports/tinytmux/oyk_tinytmux/tinytmux_usb_host.cpp
        ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp
        ports/tinytmux/oyk_tinytmux/lwip_net_bridge.cpp
        ports/tinytmux/oyk_tinytmux/tinytmux_console.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
    )
    target_include_directories(oyk_tinytmux_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/tinytmux/oyk_tinytmux
    )
    set_target_properties(oyk_tinytmux_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_tinytmux_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME oyk_tinytmux_tests COMMAND oyk_tinytmux_tests)

    # Strict C++98 host verification of Raspberry Pi Pico (RP2040 / RP2350) boards, PSRAM, storage, RM2 optional radio & 2-UART console
    add_executable(oyk_pico_tests
        tests/test_pico.cpp
        ports/pico/board_pico.cpp
        ports/pico/pico_psram.cpp
        ports/pico/pico_fs.cpp
        ports/pico/pico_radio.cpp
        ports/pico/pico_net_bridge.cpp
        ports/pico/pico_console.cpp
        ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
    )
    target_include_directories(oyk_pico_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/tinytmux/oyk_tinytmux
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/pico
    )
    set_target_properties(oyk_pico_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_pico_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME oyk_pico_tests COMMAND oyk_pico_tests)

    if(TARGET oyk-node)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME oyk_ncurses_ui_tests
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ncurses_ui.py
                        $<TARGET_FILE:oyk-node>
            )
        endif()
    endif()

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
