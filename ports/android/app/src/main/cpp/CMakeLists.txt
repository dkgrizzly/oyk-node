# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.22.1)
project(oyk_android LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(OYK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)

add_library(oyk_android SHARED
    oyk_android_jni.cpp
    "${OYK_ROOT}/src/core/line_ending.cpp"
    "${OYK_ROOT}/src/ax25/callsign.cpp"
    "${OYK_ROOT}/src/ax25/crc.cpp"
    "${OYK_ROOT}/src/ax25/frame.cpp"
    "${OYK_ROOT}/src/radio/interface_registry.cpp"
    "${OYK_ROOT}/src/radio/kiss.cpp"
    "${OYK_ROOT}/src/radio/axudp_interface.cpp"
    "${OYK_ROOT}/src/radio/axip_interface.cpp"
    "${OYK_ROOT}/src/radio/kiss_serial_interface.cpp"
    "${OYK_ROOT}/src/radio/kiss_tcp_interface.cpp"
    "${OYK_ROOT}/src/radio/telnet_interface.cpp"
    "${OYK_ROOT}/src/radio/rig_control.cpp"
    "${OYK_ROOT}/src/protocol/yapp.cpp"
    "${OYK_ROOT}/src/node/logger.cpp"
    "${OYK_ROOT}/src/node/ban_list.cpp"
    "${OYK_ROOT}/src/node/node_config.cpp"
    "${OYK_ROOT}/src/node/session.cpp"
    "${OYK_ROOT}/src/node/bbs_server.cpp"
    "${OYK_ROOT}/src/node/chat_room.cpp"
    "${OYK_ROOT}/src/node/command_handler.cpp"
    "${OYK_ROOT}/src/node/node_engine.cpp"
    "${OYK_ROOT}/ports/compat/oyk_portable.cpp"
    "${OYK_ROOT}/ports/compat/oyk_yapp.cpp"
    "${OYK_ROOT}/ports/compat/oyk_rig.cpp"
    "${OYK_ROOT}/ports/compat/oyk_printer.cpp"
    "${OYK_ROOT}/ports/compat/oyk_term.cpp"
    "${OYK_ROOT}/ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp"
)

target_include_directories(oyk_android PRIVATE
    "${OYK_ROOT}/include"
    "${OYK_ROOT}/ports/compat"
    "${OYK_ROOT}/ports/tinytmux/oyk_tinytmux"
)

target_compile_options(oyk_android PRIVATE -Wall -Wextra -DOYK_ANDROID_PORT=1)

find_library(LOG_LIB log)
find_library(ANDROID_LIB android)

target_link_libraries(oyk_android PRIVATE ${LOG_LIB} ${ANDROID_LIB})
