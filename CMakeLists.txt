# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)
project(oyk-node VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_NCURSES_UI   "Build with ncurses local text-mode UI" ON)
option(BUILD_GTK2_UI      "Automatically build X11 GTK+ 2.0 GUI when libgtk2.0-dev is installed" ON)
option(BUILD_TESTS        "Build unit and integration tests" ON)
option(OYK_WERROR         "Treat compiler warnings as errors on native host targets" OFF)
option(OYK_SANITIZE       "Enable AddressSanitizer and UndefinedBehaviorSanitizer on native host targets" OFF)

function(oyk_apply_host_hardening tgt)
    if(OYK_WERROR)
        target_compile_options(${tgt} PRIVATE -Werror)
    endif()
    if(OYK_SANITIZE)
        target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
endfunction()

# Retro & Windows ports are ONLY built when explicitly requested
option(BUILD_MSDOS_PORT    "Add target to build MS-DOS (OpenWatcom v2/1.9 + mTCP) executable" OFF)
option(BUILD_MSDOS_GFX_PORT "Add target to build 386+ graphical MS-DOS (OpenWatcom + VBE/VGA/EGA) executable" OFF)
option(BUILD_WIN16_PORT    "Add target to build 16-bit Windows 3.1 (OpenWatcom) executable" OFF)
option(BUILD_WINDOWS_PORT  "Build 32/64-bit Windows native GUI executable (Win95..ME, NT 3.51..Win11)" OFF)
option(BUILD_TINYTMUX_PORT   "Add target to build Teensy 4.1 / IMXRT1060 (TinyTMUX) firmware" OFF)
option(BUILD_PICO_PORT       "Add targets to build Raspberry Pi Pico (RP2040/RP2350) firmware UF2s" OFF)
option(BUILD_LYRA_PORT       "Add targets to build/verify Luckfox Lyra (RK3506 RT-Thread SMP + PicoCalc) firmware" OFF)
option(BUILD_ESP32C6_PORT    "Add target to build ESP32-C6 (M5Stack Unit C6L) firmware via ESP-IDF" OFF)
option(BUILD_MACCLASSIC_PORT "Add targets to build Classic Macintosh (68k + PPC Fat) via Retro68" OFF)
option(BUILD_GSOS_PORT       "Add targets to build Apple IIGS GS/OS (65C816 S16) via vbcc" OFF)
option(BUILD_AMIGA_PORT      "Add targets to build Amiga Workbench (m68k OS 3.1/2.04/1.3) via amiga-gcc" OFF)
option(BUILD_ATARIST_PORT    "Add targets to build Atari ST / TT GEM (m68k PRG) via m68k-atari-mintelf-g++" OFF)
option(BUILD_M88K_PORT       "Build/verify Motorola 88K SVR4 (curses TUI + OSF/Motif 1.2) port" OFF)
option(BUILD_ATT3B2_PORT     "Build/verify AT&T 3B2 SVR3.2 (curses/VT100 TUI + WIN/TCP) port" OFF)
option(BUILD_TI99_PORT       "Build/verify TI-99/4A (SAMS 1MB, TIPI, F18A/40-col, RS-232 KISS) port" OFF)
option(BUILD_OPENVMS_PORT    "Build/verify OpenVMS (SMG$ console & DECwindows Motif 1.2 for VAX/Alpha/IA64/x86-64) port" OFF)
option(BUILD_OS2_PORT        "Build/verify OS/2 2.0 / Warp 3.0 / Warp 4.0 (VIO console & Presentation Manager GUI) port" OFF)
if(APPLE)
    option(BUILD_MACOS_PORT "Build native macOS AppKit port (PowerPC, Intel, Apple Silicon)" ON)
else()
    option(BUILD_MACOS_PORT "Build native macOS AppKit port (PowerPC, Intel, Apple Silicon)" OFF)
endif()
set(MTCP_DIR "$ENV{HOME}/mTCP" CACHE PATH "Path to Michael Brutman's mTCP source tree (for BUILD_MSDOS_PORT)")
set(FREEDOS_BOOT_IMG "" CACHE FILEPATH "Path to FreeDOS/MS-DOS boot floppy/disk image for QEMU DOS unit tests")
set(RTTHREAD_ROOT "$ENV{RTTHREAD_ROOT}" CACHE PATH "Path to RT-Thread source tree (for BUILD_LYRA_PORT)")
set(RK3506_BSP_ROOT "$ENV{RK3506_BSP_ROOT}" CACHE PATH "Path to Rockchip RK3506 RT-Thread BSP (for BUILD_LYRA_PORT)")

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
        ports/compat/oyk_gui.cpp
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
    oyk_apply_host_hardening(oyknode_core)

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
    oyk_apply_host_hardening(oyk-node)

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
            oyk_apply_host_hardening(oyk-node-gtk)
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
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/oyk_node.ico"
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
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui_theme.cpp"
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
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui_theme.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui.hpp"
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
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/windows/oyk_node.ico"
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
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
                        "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui_theme.cpp"
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
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui_theme.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ports/compat/oyk_gui.hpp"
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

# Optional ESP32-C6 (M5Stack Unit C6L) firmware target via ESP-IDF v5.x
if(BUILD_ESP32C6_PORT)
    set(_esp_idf_dir "")
    if(DEFINED ENV{IDF_PATH} AND EXISTS "$ENV{IDF_PATH}/export.sh")
        set(_esp_idf_dir "$ENV{IDF_PATH}")
    elseif(EXISTS "$ENV{HOME}/opt/esp-idf/export.sh")
        set(_esp_idf_dir "$ENV{HOME}/opt/esp-idf")
    endif()
    if(NOT _esp_idf_dir)
        message(FATAL_ERROR "BUILD_ESP32C6_PORT requires ESP-IDF (set IDF_PATH or install in ~/opt/esp-idf)")
    endif()
    set(_esp_tools_dir "$ENV{HOME}/opt/espressif")
    if(DEFINED ENV{IDF_TOOLS_PATH} AND NOT "$ENV{IDF_TOOLS_PATH}" STREQUAL "")
        set(_esp_tools_dir "$ENV{IDF_TOOLS_PATH}")
    endif()
    add_custom_target(oyk-esp32c6
        COMMAND /usr/bin/env bash -lc
                "IDF_TOOLS_PATH='${_esp_tools_dir}' source '${_esp_idf_dir}/export.sh' >/dev/null && cd '${CMAKE_CURRENT_SOURCE_DIR}/ports/esp32c6' && idf.py set-target esp32c6 && idf.py build && mkdir -p '${CMAKE_CURRENT_BINARY_DIR}/esp32c6' && cp -f build/oyk-esp32c6-unit_c6l.bin '${CMAKE_CURRENT_BINARY_DIR}/esp32c6/oyk-esp32c6-unit_c6l.bin'"
        COMMENT "Building ESP32-C6 (M5Stack Unit C6L) firmware via ESP-IDF"
    )
endif()

# Optional Luckfox Lyra (Rockchip RK3506 RT-Thread SMP + PicoCalc) targets - built only if requested
if(BUILD_LYRA_PORT)
    add_subdirectory(ports/lyra)
endif()

if(BUILD_MACCLASSIC_PORT)
    add_subdirectory(ports/macclassic)
endif()

if(BUILD_GSOS_PORT)
    add_subdirectory(ports/gsos)
endif()

if(BUILD_AMIGA_PORT)
    add_subdirectory(ports/amiga)
endif()

if(BUILD_ATARIST_PORT)
    add_subdirectory(ports/atarist)
endif()

if(BUILD_MACOS_PORT)
    add_subdirectory(ports/macos)
endif()

if(BUILD_TESTS AND NOT WIN32)
    enable_testing()

    add_executable(oyk_node_tests tests/test_node.cpp)
    target_link_libraries(oyk_node_tests PRIVATE oyknode_core)
    target_compile_options(oyk_node_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_node_tests)
    add_test(NAME oyk_node_tests
        COMMAND oyk_node_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Strict C++98 verification of the portable MS-DOS / Win16 / Win32 shared core
    add_executable(oyk_portable_tests
        tests/test_portable.cpp
        tests/test_portable_term.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
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
    oyk_apply_host_hardening(oyk_portable_tests)
    add_test(NAME oyk_portable_tests COMMAND oyk_portable_tests)

    # Strict C++98 tests for the shared GUI controller & schema layer (oyk_gui)
    add_executable(oyk_gui_tests
        tests/test_gui_middleware.cpp
        ports/compat/oyk_gui.cpp
        ports/compat/oyk_gui_theme.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_tui.cpp
    )
    target_include_directories(oyk_gui_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
    )
    set_target_properties(oyk_gui_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_gui_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_gui_tests)
    add_test(NAME oyk_gui_tests COMMAND oyk_gui_tests)

    # Host C++98 tests for the dual-core macOS AppKit bridge & OykPortableNode transport layer
    add_executable(oyk_macos_bridge_tests
        tests/test_macos_bridge.cpp
        ports/macos/macos_engine_bridge.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
        ports/compat/oyk_gui_theme.cpp
    )
    target_compile_definitions(oyk_macos_bridge_tests PRIVATE OYK_MACOS_USE_PORTABLE=1)
    target_include_directories(oyk_macos_bridge_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/macos
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
    )
    set_target_properties(oyk_macos_bridge_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_macos_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_macos_bridge_tests)
    add_test(NAME oyk_macos_bridge_tests COMMAND oyk_macos_bridge_tests)

    if(BUILD_MACCLASSIC_PORT AND EXISTS "$ENV{HOME}/minivmac/minivmac-plus-bg")
        add_test(NAME oyk_macclassic_tests
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ports/macclassic/run_minivmac_test.sh --unit-tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()

    # Host C++98 verification of Apple IIGS GS/OS core, config EOLs, QuickDraw II theme preview, and terminal helpers
    set_source_files_properties(
        ports/gsos/gsos_core.c
        ports/gsos/gsos_net.c
        ports/gsos/gsos_capture.c
        ports/gsos/gsos_ui.c
        PROPERTIES LANGUAGE CXX
    )
    add_executable(oyk_gsos_tests
        tests/test_gsos.cpp
        ports/gsos/gsos_core.c
        ports/gsos/gsos_net.c
        ports/gsos/gsos_capture.c
        ports/gsos/gsos_ui.c
    )
    target_include_directories(oyk_gsos_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/gsos
    )
    set_target_properties(oyk_gsos_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_gsos_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_gsos_tests)
    add_test(NAME oyk_gsos_tests COMMAND oyk_gsos_tests)

    if(BUILD_GSOS_PORT AND EXISTS "$ENV{HOME}/gsplus/gsplus/gsplus" AND EXISTS "$ENV{HOME}/gsplus/ROM.01")
        add_test(NAME oyk_gsos_emulator_tests
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ports/gsos/run_gsplus_test.sh --unit-tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()

    # Host C++98 verification of Amiga Workbench policy (KS 3.1/2.04/1.3, <32-color native lock, Private Screen default OFF)
    add_executable(oyk_amiga_tests
        tests/test_amiga_policy.cpp
        ports/amiga/amiga_policy.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
        ports/compat/oyk_gui_theme.cpp
    )
    target_include_directories(oyk_amiga_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/amiga
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
    )
    set_target_properties(oyk_amiga_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_amiga_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_amiga_tests)
    add_test(NAME oyk_amiga_tests COMMAND oyk_amiga_tests)

    if(BUILD_AMIGA_PORT AND EXISTS "/usr/bin/fs-uae")
        add_test(NAME oyk_amiga_emulator_tests
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ports/amiga/run_fsuae_test.sh --unit-tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()

    # Host C++98 verification of Atari ST/TT display tiers, VDI RGB conversion, and 4-plane hardware-VDI color mapping
    add_executable(oyk_atarist_tests
        tests/test_atarist_policy.cpp
        ports/atarist/atari_policy.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
        ports/compat/oyk_gui_theme.cpp
    )
    target_include_directories(oyk_atarist_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/atarist
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
    )
    set_target_properties(oyk_atarist_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_atarist_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_atarist_tests)
    add_test(NAME oyk_atarist_tests COMMAND oyk_atarist_tests)

    if(BUILD_ATARIST_PORT AND EXISTS "/usr/bin/hatari")
        add_test(NAME oyk_atarist_emulator_tests
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ports/atarist/run_hatari_test.sh --unit-tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()

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
        ports/compat/oyk_gui.cpp
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
    target_compile_definitions(oyk_tinytmux_tests PRIVATE OYK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(oyk_tinytmux_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_tinytmux_tests)
    add_test(NAME oyk_tinytmux_tests COMMAND oyk_tinytmux_tests)

    # Strict C++98 + C99 host verification of Raspberry Pi Pico & ClockworkPi PicoCalc boards, PSRAM, storage, RM2 & oyk-vt100 LCD/KBD
    add_executable(oyk_pico_tests
        tests/test_pico.cpp
        ports/pico/board_pico.cpp
        ports/pico/pico_psram.cpp
        ports/pico/pico_fs.cpp
        ports/pico/pico_radio.cpp
        ports/pico/pico_net_bridge.cpp
        ports/pico/pico_console.cpp
        ports/pico/pico_picocalc_lcd.cpp
        ports/pico/pico_picocalc_kbd.cpp
        ports/pico/pico_picocalc_audio.cpp
        ports/pico/pico_picocalc_console.cpp
        third_party/oyk-vt100/src/oyk_vt100.c
        third_party/oyk-vt100/src/oyk_vt_font.c
        third_party/oyk-vt100/src/vt_font_4x8.c
        third_party/oyk-vt100/src/vt_font_6x10.c
        third_party/oyk-vt100/src/oyk_vt_fb.c
        ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
    )
    target_include_directories(oyk_pico_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/tinytmux/oyk_tinytmux
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/pico
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/oyk-vt100/include
    )
    set_target_properties(oyk_pico_tests PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_definitions(oyk_pico_tests PRIVATE
        OYK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        OYK_VT_ENABLE_256COLOR=1
        OYK_VT_ENABLE_TRUECOLOR=0
        OYK_VT_MAX_COLS=80
        OYK_VT_MAX_ROWS=48
    )
    target_compile_options(oyk_pico_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_pico_tests)
    add_test(NAME oyk_pico_tests COMMAND oyk_pico_tests)

    # Strict C++98 host verification of ESP32-C6 (M5Stack Unit C6L) board, SX1262 LoRa, OLED, LittleFS, Wi-Fi/BLE & VT340 TUI
    add_executable(oyk_esp32c6_tests
        tests/test_esp32c6.cpp
        ports/esp32c6/main/board_esp32c6.cpp
        ports/esp32c6/main/esp32c6_lora.cpp
        ports/esp32c6/main/esp32c6_oled.cpp
        ports/esp32c6/main/esp32c6_fs.cpp
        ports/esp32c6/main/esp32c6_radio.cpp
        ports/esp32c6/main/esp32c6_net_bridge.cpp
        ports/esp32c6/main/esp32c6_console.cpp
        ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
    )
    target_include_directories(oyk_esp32c6_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/tinytmux/oyk_tinytmux
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/esp32c6/main
    )
    set_target_properties(oyk_esp32c6_tests PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_definitions(oyk_esp32c6_tests PRIVATE OYK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(oyk_esp32c6_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_esp32c6_tests)
    add_test(NAME oyk_esp32c6_tests COMMAND oyk_esp32c6_tests)

    # Strict C++98 + C99 host verification of Luckfox Lyra (RK3506 RT-Thread SMP & ClockworkPi PicoCalc)
    add_executable(oyk_lyra_tests
        tests/test_lyra.cpp
        ports/lyra/board_lyra.cpp
        ports/lyra/lyra_fs.cpp
        ports/lyra/lyra_radio.cpp
        ports/lyra/lyra_net_bridge.cpp
        ports/lyra/lyra_console.cpp
        ports/lyra/lyra_picocalc_lcd.cpp
        ports/lyra/lyra_picocalc_kbd.cpp
        ports/lyra/lyra_picocalc_audio.cpp
        ports/lyra/lyra_picocalc_console.cpp
        ports/pico/board_pico.cpp
        ports/pico/pico_psram.cpp
        ports/pico/pico_fs.cpp
        ports/pico/pico_radio.cpp
        ports/pico/pico_net_bridge.cpp
        ports/pico/pico_console.cpp
        ports/pico/pico_picocalc_lcd.cpp
        ports/pico/pico_picocalc_kbd.cpp
        ports/pico/pico_picocalc_audio.cpp
        ports/pico/pico_picocalc_console.cpp
        third_party/oyk-vt100/src/oyk_vt100.c
        third_party/oyk-vt100/src/oyk_vt_font.c
        third_party/oyk-vt100/src/vt_font_4x8.c
        third_party/oyk-vt100/src/vt_font_6x10.c
        third_party/oyk-vt100/src/oyk_vt_fb.c
        ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
    )
    target_include_directories(oyk_lyra_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/tinytmux/oyk_tinytmux
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/lyra
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/pico
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/oyk-vt100/include
    )
    set_target_properties(oyk_lyra_tests PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_definitions(oyk_lyra_tests PRIVATE
        OYK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        OYK_VT_ENABLE_256COLOR=1
        OYK_VT_ENABLE_TRUECOLOR=0
        OYK_VT_MAX_COLS=80
        OYK_VT_MAX_ROWS=48
    )
    target_compile_options(oyk_lyra_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_lyra_tests)
    add_test(NAME oyk_lyra_tests COMMAND oyk_lyra_tests)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME oyk_lyra_tui_parity_test
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_lyra_tui_parity.py
                    $<TARGET_FILE:oyk_lyra_tests>
        )
    endif()

    # Strict ANSI C89 host verification of shared System V core, TUI, and net layer
    add_executable(oyk_sysv_tests
        tests/test_sysv.cpp
        ports/sysv/sysv_core.c
        ports/sysv/sysv_tui.c
        ports/sysv/sysv_net.c
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/compat/oyk_rig.cpp
        ports/compat/oyk_printer.cpp
        ports/compat/oyk_tui.cpp
        ports/compat/oyk_term.cpp
        ports/compat/oyk_gui.cpp
    )
    target_include_directories(oyk_sysv_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/sysv
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
    )
    set_target_properties(oyk_sysv_tests PROPERTIES
        C_STANDARD 90
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(oyk_sysv_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_sysv_tests)
    add_test(NAME oyk_sysv_tests COMMAND oyk_sysv_tests)

    # Motorola 88K SVR4 host C89 + TUI/Motif parity verification
    add_subdirectory(ports/m88k)

    # AT&T 3B2 SVR3.2 host C89 + TUI parity verification
    add_subdirectory(ports/att3b2)

    # TI-99/4A host C99 + SAMS/TIPI/F18A/40-col parity verification
    add_subdirectory(ports/ti99)

    # OpenVMS (VAX/Alpha/IA64/x86-64) SMG$ console & DECwindows Motif parity verification
    add_subdirectory(ports/openvms)

    # OS/2 2.0+ VIO console & Presentation Manager parity verification
    add_subdirectory(ports/os2)

    # Seeded mutation fuzz harness across AX.25, KISS, config, banlist, YAPP, terminal, and PPP parsers
    add_executable(oyk_fuzz_tests
        tests/test_fuzz.cpp
        ports/compat/oyk_portable.cpp
        ports/compat/oyk_yapp.cpp
        ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp
    )
    target_include_directories(oyk_fuzz_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ports/tinytmux/oyk_tinytmux
    )
    target_link_libraries(oyk_fuzz_tests PRIVATE oyknode_core)
    target_compile_options(oyk_fuzz_tests PRIVATE -Wall -Wextra -Wpedantic)
    oyk_apply_host_hardening(oyk_fuzz_tests)
    add_test(NAME oyk_fuzz_tests COMMAND oyk_fuzz_tests)

    if(TARGET oyk-node)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME oyk_ncurses_ui_tests
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ncurses_ui.py
                        $<TARGET_FILE:oyk-node>
            )
            if(OYK_SANITIZE)
                set_tests_properties(oyk_ncurses_ui_tests PROPERTIES
                    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/lsan_suppressions.txt"
                )
            endif()
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
