# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

if(APPLE)
    option(OYK_MACOS_PORTABLE_CORE "Use C++98 OykPortableNode core (required for PowerPC / Mac OS X 10.4-10.6)" OFF)
    set(OYK_MACOS_MIN_VERSION "11.0" CACHE STRING "macOS deployment target")

    add_executable(oyk-node-macos MACOSX_BUNDLE
        macos_main.mm
        macos_engine_bridge.cpp
        macos_engine_bridge.hpp
        ../compat/oyk_portable.cpp
        ../compat/oyk_yapp.cpp
        ../compat/oyk_rig.cpp
        ../compat/oyk_printer.cpp
        ../compat/oyk_term.cpp
        ../compat/oyk_gui.cpp
        ../compat/oyk_gui_theme.cpp
    )

    if(OYK_MACOS_PORTABLE_CORE)
        target_compile_definitions(oyk-node-macos PRIVATE OYK_MACOS_USE_PORTABLE=1)
        set_target_properties(oyk-node-macos PROPERTIES CXX_STANDARD 98)
    else()
        target_compile_definitions(oyk-node-macos PRIVATE OYK_MACOS_USE_PORTABLE=0)
        target_link_libraries(oyk-node-macos PRIVATE oyknode_core)
    endif()

    target_include_directories(oyk-node-macos PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../compat"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    )

    target_compile_options(oyk-node-macos PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-arc>
        -Wall -Wextra
    )

    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    target_link_libraries(oyk-node-macos PRIVATE ${COCOA_FRAMEWORK})

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY
    )
    set_target_properties(oyk-node-macos PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        OUTPUT_NAME "OYK Node"
    )
endif()
