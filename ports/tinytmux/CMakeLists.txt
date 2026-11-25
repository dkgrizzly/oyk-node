# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)
project(oyk_tinytmux NONE)

option(OYK_TINYTMUX_FETCH_QNETHERNET "Automatically fetch QNEthernet for Teensy 4.1 Ethernet (with lwIP/AXIP)" ON)

set(ARDUINO_DIR "" CACHE PATH "Path to Arduino 1.8.19 + Teensyduino 1.62 installation")
set(QNETHERNET_DIR "" CACHE PATH "Optional local path to QNEthernet library")

if(NOT ARDUINO_DIR)
    if(DEFINED ENV{ARDUINO_DIR} AND EXISTS "$ENV{ARDUINO_DIR}/arduino-builder")
        set(ARDUINO_DIR "$ENV{ARDUINO_DIR}")
    else()
        foreach(_cand
            "$ENV{HOME}/arduino-1.8.19"
            "/opt/arduino-1.8.19"
            "/usr/local/arduino-1.8.19")
            if(EXISTS "${_cand}/arduino-builder")
                set(ARDUINO_DIR "${_cand}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(NOT ARDUINO_DIR OR NOT EXISTS "${ARDUINO_DIR}/arduino-builder")
    message(FATAL_ERROR "Arduino 1.8.19 + Teensyduino not found. Pass -DARDUINO_DIR=/path/to/arduino-1.8.19")
endif()

if(NOT EXISTS "${ARDUINO_DIR}/hardware/teensy/avr/boards.txt")
    message(FATAL_ERROR "Teensyduino hardware package not found inside ARDUINO_DIR=${ARDUINO_DIR}")
endif()

set(_arduino_libs_dir "${CMAKE_CURRENT_BINARY_DIR}/arduino-libs")
file(MAKE_DIRECTORY "${_arduino_libs_dir}")
file(CREATE_LINK
    "${CMAKE_CURRENT_SOURCE_DIR}/../compat"
    "${_arduino_libs_dir}/oyk_compat"
    SYMBOLIC)

if(QNETHERNET_DIR)
    file(CREATE_LINK
        "${QNETHERNET_DIR}"
        "${_arduino_libs_dir}/QNEthernet"
        SYMBOLIC)
elseif(OYK_TINYTMUX_FETCH_QNETHERNET)
    include(FetchContent)
    FetchContent_Declare(qnethernet
        GIT_REPOSITORY https://github.com/ssilverman/QNEthernet.git
        GIT_TAG v0.37.0
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(qnethernet)
    if(NOT qnethernet_POPULATED)
        message(STATUS "Fetching QNEthernet (v0.37.0) for Teensy 4.1...")
        FetchContent_Populate(qnethernet)
    endif()
    file(CREATE_LINK
        "${qnethernet_SOURCE_DIR}"
        "${_arduino_libs_dir}/QNEthernet"
        SYMBOLIC)
endif()

set(_arduino_build_dir "${CMAKE_CURRENT_BINARY_DIR}/arduino-build")
set(_arduino_cache_dir "${CMAKE_CURRENT_BINARY_DIR}/arduino-cache")
file(MAKE_DIRECTORY "${_arduino_build_dir}" "${_arduino_cache_dir}")

set(_sketch "${CMAKE_CURRENT_SOURCE_DIR}/oyk_tinytmux/oyk_tinytmux.ino")
file(GLOB _tinytmux_deps CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/oyk_tinytmux/*.ino"
    "${CMAKE_CURRENT_SOURCE_DIR}/oyk_tinytmux/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/oyk_tinytmux/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/oyk_tinytmux/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../compat/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../compat/*.cpp"
)

set(_extra_arduino_args
    "-prefs=recipe.hooks.postbuild.1.pattern=true"
)
if(NOT QNETHERNET_DIR AND NOT OYK_TINYTMUX_FETCH_QNETHERNET)
    list(APPEND _extra_arduino_args "-prefs=compiler.cpp.extra_flags=-DOYK_USE_NATIVE_ETHERNET=1")
endif()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/oyk-tinytmux.hex"
           "${CMAKE_CURRENT_BINARY_DIR}/oyk-tinytmux.elf"
    COMMAND "${ARDUINO_DIR}/arduino-builder"
        -compile
        -hardware "${ARDUINO_DIR}/hardware"
        -tools "${ARDUINO_DIR}/tools-builder"
        -tools "${ARDUINO_DIR}/hardware/tools/avr"
        -built-in-libraries "${ARDUINO_DIR}/libraries"
        -libraries "${_arduino_libs_dir}"
        -fqbn=teensy:avr:teensy41:usb=serial2,speed=600,opt=o2std,keys=en-us
        ${_extra_arduino_args}
        -build-path "${_arduino_build_dir}"
        -build-cache "${_arduino_cache_dir}"
        "${_sketch}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_arduino_build_dir}/oyk_tinytmux.ino.hex"
        "${CMAKE_CURRENT_BINARY_DIR}/oyk-tinytmux.hex"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_arduino_build_dir}/oyk_tinytmux.ino.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/oyk-tinytmux.elf"
    DEPENDS ${_tinytmux_deps}
    COMMENT "Compiling Teensy 4.1 / TinyTMUX firmware via Teensyduino 1.62 (oyk-tinytmux.hex)"
    VERBATIM
)

add_custom_target(oyk-tinytmux ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/oyk-tinytmux.hex"
)
