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
            "$ENV{HOME}/open-watcom-v2/rel/binl64"
            "$ENV{HOME}/open-watcom-v2/rel/binl"
    )
    if(NOT WMAKE_EXE)
        message(FATAL_ERROR "BUILD_MSDOS_PORT requires OpenWatcom 'wmake' in PATH, WATCOM, or ~/open-watcom-v2/rel/binl64")
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

# Optional 16-bit Windows 3.1 (OpenWatcom 1.9) target - built only if requested
if(BUILD_WIN16_PORT)
    find_program(WMAKE_EXE wmake)
    if(NOT WMAKE_EXE)
        message(FATAL_ERROR "BUILD_WIN16_PORT requires OpenWatcom 1.9 'wmake' in PATH")
    endif()
    add_custom_target(oyk-node-win16
        COMMAND ${WMAKE_EXE} -f makefile.win16
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/windows
        COMMENT "Building 16-bit Windows 3.1 executable with OpenWatcom 1.9"
    )
endif()

# Optional 32/64-bit Windows native GUI target - built only if requested
if(BUILD_WINDOWS_PORT)
    if(WIN32)
        add_subdirectory(ports/windows)
    else()
        find_program(WMAKE_EXE wmake)
        if(WMAKE_EXE)
            add_custom_target(oyk-node-win32
                COMMAND ${WMAKE_EXE} -f makefile.win32
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/windows
                COMMENT "Building 32-bit Windows executable with OpenWatcom 1.9"
            )
        else()
            message(FATAL_ERROR "BUILD_WINDOWS_PORT requires a Windows CMake toolchain (MinGW/MSVC) or OpenWatcom 'wmake'")
        endif()
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
        if(QEMU_I386_EXE AND FREEDOS_BOOT_IMG)
            message(STATUS "Enabling oyk_dos_qemu_tests via ${QEMU_I386_EXE} (boot: ${FREEDOS_BOOT_IMG})")
            add_test(NAME oyk_dos_qemu_tests
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos/run_qemu_tests.sh
                        --test-exe ${CMAKE_CURRENT_SOURCE_DIR}/ports/dos/testdos.exe
                        --boot-img ${FREEDOS_BOOT_IMG}
            )
        else()
            message(STATUS "Skipping oyk_dos_qemu_tests (set -DFREEDOS_BOOT_IMG=/path/to/freedos.img with qemu-system-i386 installed)")
        endif()
    endif()
endif()
