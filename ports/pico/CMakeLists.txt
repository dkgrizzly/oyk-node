# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)

# Target board selection:
#   pico_w | pico2_w | waveshare_core2350b | pimoroni_pga2350 |
#   picocalc_pico2_w | picocalc_pico_w | picocalc_pico2 | picocalc_pico
set(PICO_BOARD "pico2_w" CACHE STRING "Raspberry Pi Pico board target")
set(OYK_PICO_BOARD_NAME "${PICO_BOARD}")
set(OYK_PICO_IS_PICOCALC 0)
set(OYK_PICO_IS_RP2040 0)
set(OYK_PICO_VT_256COLOR 1)
if(OYK_PICO_BOARD_NAME STREQUAL "picocalc_pico2_w")
    set(PICO_BOARD "pico2_w")
    set(OYK_PICO_IS_PICOCALC 1)
elseif(OYK_PICO_BOARD_NAME STREQUAL "picocalc_pico_w")
    set(PICO_BOARD "pico_w")
    set(OYK_PICO_IS_PICOCALC 1)
    set(OYK_PICO_IS_RP2040 1)
    set(OYK_PICO_VT_256COLOR 0)
elseif(OYK_PICO_BOARD_NAME STREQUAL "picocalc_pico2")
    set(PICO_BOARD "pico2")
    set(OYK_PICO_IS_PICOCALC 1)
elseif(OYK_PICO_BOARD_NAME STREQUAL "picocalc_pico")
    set(PICO_BOARD "pico")
    set(OYK_PICO_IS_PICOCALC 1)
    set(OYK_PICO_IS_RP2040 1)
    set(OYK_PICO_VT_256COLOR 0)
elseif(OYK_PICO_BOARD_NAME STREQUAL "pico_w")
    set(OYK_PICO_IS_RP2040 1)
endif()

# Discover populated Pico SDK (checking ~/pico-sdk and ~/.pico-sdk/sdk/*)
if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH} AND EXISTS "$ENV{PICO_SDK_PATH}/pico_sdk_init.cmake")
        set(_cand_sdks "$ENV{PICO_SDK_PATH}")
    else()
        file(GLOB _dot_sdks "$ENV{HOME}/.pico-sdk/sdk/*")
        set(_cand_sdks "${_dot_sdks}" "$ENV{HOME}/pico-sdk")
    endif()
    foreach(_d IN LISTS _cand_sdks)
        if(EXISTS "${_d}/pico_sdk_init.cmake" AND EXISTS "${_d}/lib/cyw43-driver/src" AND EXISTS "${_d}/lib/lwip/src")
            set(PICO_SDK_PATH "${_d}")
            break()
        endif()
    endforeach()
    if(NOT PICO_SDK_PATH)
        foreach(_d IN LISTS _cand_sdks)
            if(EXISTS "${_d}/pico_sdk_init.cmake")
                set(PICO_SDK_PATH "${_d}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(NOT PICO_SDK_PATH OR NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "Pico SDK not found. Expected ~/pico-sdk or ~/.pico-sdk/sdk/<ver>.")
endif()

# Register tools installed under ~/.pico-sdk if not already in PATH
file(GLOB _arm_bins "$ENV{HOME}/.pico-sdk/toolchain/*/bin")
foreach(_b IN LISTS _arm_bins)
    if(EXISTS "${_b}/arm-none-eabi-gcc")
        set(ENV{PATH} "${_b}:$ENV{PATH}")
        set(PICO_TOOLCHAIN_PATH "${_b}/..")
        break()
    endif()
endforeach()
file(GLOB _pioasm_dirs "$ENV{HOME}/.pico-sdk/tools/*/pioasm")
foreach(_p IN LISTS _pioasm_dirs)
    if(EXISTS "${_p}/pioasm" OR EXISTS "${_p}/pioasmConfig.cmake")
        set(pioasm_DIR "${_p}")
        break()
    endif()
endforeach()
file(GLOB _picotool_dirs "$ENV{HOME}/.pico-sdk/picotool/*/picotool")
foreach(_pt IN LISTS _picotool_dirs)
    if(EXISTS "${_pt}/picotoolConfig.cmake" OR EXISTS "${_pt}/picotool")
        set(picotool_DIR "${_pt}")
        break()
    endif()
endforeach()

# Enable dynamic CYW43 pin configuration on RP2350B carriers for optional Raspberry Pi RM2 module
if(PICO_BOARD STREQUAL "waveshare_core2350b" OR PICO_BOARD STREQUAL "pimoroni_pga2350")
    set(PICO_CYW43_SUPPORTED 1)
endif()

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(oyk_pico_${PICO_BOARD} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

get_filename_component(OYK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(TARGET_NAME "oyk-pico-${OYK_PICO_BOARD_NAME}")

set(_oyk_pico_sources
    main_pico.cpp
    board_pico.cpp
    pico_psram.cpp
    pico_fs.cpp
    pico_radio.cpp
    pico_net_bridge.cpp
    pico_console.cpp
    "${OYK_ROOT}/ports/compat/oyk_portable.cpp"
    "${OYK_ROOT}/ports/compat/oyk_yapp.cpp"
    "${OYK_ROOT}/ports/compat/oyk_rig.cpp"
    "${OYK_ROOT}/ports/compat/oyk_printer.cpp"
    "${OYK_ROOT}/ports/compat/oyk_tui.cpp"
    "${OYK_ROOT}/ports/compat/oyk_term.cpp"
    "${OYK_ROOT}/ports/compat/oyk_gui.cpp"
    "${OYK_ROOT}/ports/tinytmux/oyk_tinytmux/oyk_ppp.cpp"
)
if(OYK_PICO_IS_PICOCALC)
    list(APPEND _oyk_pico_sources
        pico_picocalc_lcd.cpp
        pico_picocalc_kbd.cpp
        pico_picocalc_audio.cpp
        pico_picocalc_console.cpp
        "${OYK_ROOT}/third_party/oyk-vt100/src/oyk_vt100.c"
        "${OYK_ROOT}/third_party/oyk-vt100/src/oyk_vt_font.c"
        "${OYK_ROOT}/third_party/oyk-vt100/src/vt_font_4x8.c"
        "${OYK_ROOT}/third_party/oyk-vt100/src/vt_font_6x10.c"
        "${OYK_ROOT}/third_party/oyk-vt100/src/oyk_vt_fb.c"
    )
endif()

add_executable(${TARGET_NAME} ${_oyk_pico_sources})

target_include_directories(${TARGET_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OYK_ROOT}/ports/compat"
    "${OYK_ROOT}/ports/tinytmux/oyk_tinytmux"
    "${OYK_ROOT}/third_party/oyk-vt100/include"
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    PICO_ON_DEVICE=1
    PICO_STACK_SIZE=0x1000
    OYK_PICO_BOARD_ID="${OYK_PICO_BOARD_NAME}"
    OYK_PICO_IS_PICOCALC=${OYK_PICO_IS_PICOCALC}
    OYK_VT_ENABLE_256COLOR=${OYK_PICO_VT_256COLOR}
    OYK_VT_ENABLE_TRUECOLOR=0
    OYK_VT_MAX_COLS=80
    OYK_VT_MAX_ROWS=40
)

if(PICO_BOARD STREQUAL "waveshare_core2350b" OR PICO_BOARD STREQUAL "pimoroni_pga2350")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        PICO_CYW43_SUPPORTED=1
        CYW43_PIN_WL_DYNAMIC=1
        CYW43_DEFAULT_PIN_WL_REG_ON=23
        CYW43_DEFAULT_PIN_WL_DATA_OUT=24
        CYW43_DEFAULT_PIN_WL_DATA_IN=24
        CYW43_DEFAULT_PIN_WL_HOST_WAKE=24
        CYW43_DEFAULT_PIN_WL_CLOCK=29
        CYW43_DEFAULT_PIN_WL_CS=25
    )
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    pico_stdlib
    hardware_uart
    hardware_spi
    hardware_flash
    pico_flash
    pico_unique_id
    pico_bootrom
)

if(TARGET hardware_psram)
    target_link_libraries(${TARGET_NAME} PRIVATE hardware_psram)
    target_compile_definitions(${TARGET_NAME} PRIVATE OYK_PICO_HAVE_HARDWARE_PSRAM=1)
endif()

if(TARGET pico_cyw43_arch_lwip_poll)
    target_link_libraries(${TARGET_NAME} PRIVATE pico_cyw43_arch_lwip_poll)
endif()

if(TARGET pico_btstack_cyw43)
    target_link_libraries(${TARGET_NAME} PRIVATE
        pico_btstack_cyw43
        pico_btstack_classic
        pico_btstack_ble
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE OYK_PICO_HAVE_BTSTACK=1)
endif()

if(OYK_PICO_IS_PICOCALC)
    target_link_libraries(${TARGET_NAME} PRIVATE
        hardware_i2c
        hardware_pwm
    )
endif()

# Compact static session/BBS buffer bounds so 264KB RP2040 fits comfortably with CYW43 + BTstack + VT100
if(OYK_PICO_IS_RP2040)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        OYK_MAX_SESSIONS=8
        OYK_MAX_BBS_BODY=1024
    )
endif()

pico_enable_stdio_usb(${TARGET_NAME} 1)
pico_enable_stdio_uart(${TARGET_NAME} 0)
pico_add_extra_outputs(${TARGET_NAME})
