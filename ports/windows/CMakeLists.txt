# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# CMake target for 32-bit and 64-bit Windows native GUI builds
# Compatible with Windows 95..ME, Windows NT 3.51..Windows 11 (MinGW-w64 or MSVC)

add_executable(oyk-node-win WIN32
    win_main.cpp
    oyk_win.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_portable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_yapp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat/oyk_rig.cpp
)

target_include_directories(oyk-node-win
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../compat
)

target_link_libraries(oyk-node-win
    PRIVATE
        wsock32
        user32
        gdi32
        comdlg32
)

if(MSVC)
    target_compile_definitions(oyk-node-win PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(MINGW)
    target_compile_options(oyk-node-win PRIVATE -std=c++98 -Os -fno-exceptions -fno-rtti -Wall -Wextra)
    target_link_options(oyk-node-win PRIVATE -static)
endif()
