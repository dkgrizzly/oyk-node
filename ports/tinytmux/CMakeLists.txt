# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# Standalone CMake build for OYK Packet Node on Freescale/NXP IMXRT1060
# (Teensy 4.1 / TinyTMUX 8-UART + 10/100 Ethernet + Dual USB-CDC board).
#
# Usage:
#   cmake -S ports/tinytmux -B build-tinytmux \
#         -DCMAKE_TOOLCHAIN_FILE=ports/tinytmux/arm-imxrt1062.cmake \
#         -DLWIP_DIR=/path/to/lwip \
#         -DTINYUSB_DIR=/path/to/tinyusb
#   cmake --build build-tinytmux -j

cmake_minimum_required(VERSION 3.16)
project(oyk-tinytmux VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LWIP_DIR "" CACHE PATH "Path to lwIP source tree (BSD-3-Clause, GPL-3.0 compatible)")
set(TINYUSB_DIR "" CACHE PATH "Path to TinyUSB source tree (MIT, GPL-3.0 compatible)")
set(IMXRT_SDK_DIR "" CACHE PATH "Optional path to NXP MCUXpresso SDK / Teensy4 core headers")

set(OYK_COMPAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../compat")

set(TINYTMUX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tinytmux_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/board_tinytmux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tinytmux_psram.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tinytmux_usb_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/oyk_ppp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lwip_net_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tinytmux_console.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c
    ${OYK_COMPAT_DIR}/oyk_portable.cpp
    ${OYK_COMPAT_DIR}/oyk_yapp.cpp
    ${OYK_COMPAT_DIR}/oyk_rig.cpp
)

add_executable(oyk-tinytmux.elf ${TINYTMUX_SOURCES})

target_include_directories(oyk-tinytmux.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OYK_COMPAT_DIR}
)

target_compile_definitions(oyk-tinytmux.elf PRIVATE
    OYK_TARGET_IMXRT1060=1
    OYK_MAX_INTERFACES=10
    OYK_MAX_SESSIONS=16
)

target_compile_options(oyk-tinytmux.elf PRIVATE -Os -Wall -Wextra)

# Optional lwIP integration (Ethernet ENET + PPPoS + AXUDP + AXIP raw proto 93 + Telnet)
if(LWIP_DIR AND EXISTS "${LWIP_DIR}/src/include/lwip/init.h")
    message(STATUS "TinyTMUX: enabling lwIP from ${LWIP_DIR}")
    file(GLOB LWIP_CORE_SRCS
        "${LWIP_DIR}/src/core/*.c"
        "${LWIP_DIR}/src/core/ipv4/*.c"
        "${LWIP_DIR}/src/netif/ethernet.c"
        "${LWIP_DIR}/src/netif/ppp/*.c"
    )
    target_sources(oyk-tinytmux.elf PRIVATE ${LWIP_CORE_SRCS})
    target_include_directories(oyk-tinytmux.elf PRIVATE
        "${LWIP_DIR}/src/include"
    )
    target_compile_definitions(oyk-tinytmux.elf PRIVATE OYK_HAS_LWIP=1)
else()
    message(STATUS "TinyTMUX: LWIP_DIR not provided; building with standalone PPP/transport bridge")
endif()

# Optional TinyUSB integration (USB OTG1 High-Speed Dual CDC Device + USB OTG2 Host)
if(TINYUSB_DIR AND EXISTS "${TINYUSB_DIR}/src/tusb.h")
    message(STATUS "TinyTMUX: enabling TinyUSB from ${TINYUSB_DIR}")
    target_sources(oyk-tinytmux.elf PRIVATE
        "${TINYUSB_DIR}/src/tusb.c"
        "${TINYUSB_DIR}/src/common/tusb_fifo.c"
        "${TINYUSB_DIR}/src/device/usbd.c"
        "${TINYUSB_DIR}/src/device/usbd_control.c"
        "${TINYUSB_DIR}/src/class/cdc/cdc_device.c"
        "${TINYUSB_DIR}/src/portable/chipidea/ci_hs/dcd_ci_hs.c"
        "${TINYUSB_DIR}/src/host/usbh.c"
        "${TINYUSB_DIR}/src/host/hub.c"
        "${TINYUSB_DIR}/src/class/cdc/cdc_host.c"
        "${TINYUSB_DIR}/src/class/net/ncm_host.c"
        "${TINYUSB_DIR}/src/portable/ehci/ehci.c"
    )
    target_include_directories(oyk-tinytmux.elf PRIVATE "${TINYUSB_DIR}/src")
    target_compile_definitions(oyk-tinytmux.elf PRIVATE
        OYK_HAS_TINYUSB=1
        CFG_TUSB_MCU=OPT_MCU_MIMXRT10XX
    )
else()
    message(STATUS "TinyTMUX: TINYUSB_DIR not provided; USB-CDC stubs enabled")
endif()

if(IMXRT_SDK_DIR AND EXISTS "${IMXRT_SDK_DIR}")
    target_include_directories(oyk-tinytmux.elf PRIVATE "${IMXRT_SDK_DIR}")
endif()

target_link_options(oyk-tinytmux.elf PRIVATE
    "-T${CMAKE_CURRENT_SOURCE_DIR}/imxrt1062_t41.ld"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/oyk-tinytmux.map"
)

if(CMAKE_OBJCOPY)
    add_custom_command(TARGET oyk-tinytmux.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom
                $<TARGET_FILE:oyk-tinytmux.elf>
                ${CMAKE_CURRENT_BINARY_DIR}/oyk-tinytmux.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:oyk-tinytmux.elf>
        COMMENT "Generating Teensy 4.1 Intel HEX image (oyk-tinytmux.hex)"
    )
endif()
