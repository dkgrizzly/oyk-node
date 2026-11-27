# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

idf_component_register(
    SRCS
        "main_esp32c6.cpp"
        "board_esp32c6.cpp"
        "esp32c6_lora.cpp"
        "esp32c6_oled.cpp"
        "esp32c6_fs.cpp"
        "esp32c6_radio.cpp"
        "esp32c6_net_bridge.cpp"
        "esp32c6_console.cpp"
        "../../compat/oyk_portable.cpp"
        "../../compat/oyk_yapp.cpp"
        "../../compat/oyk_rig.cpp"
        "../../compat/oyk_printer.cpp"
        "../../compat/oyk_tui.cpp"
        "../../compat/oyk_term.cpp"
        "../../compat/oyk_gui.cpp"
        "../../tinytmux/oyk_tinytmux/oyk_ppp.cpp"
    INCLUDE_DIRS
        "."
        "../../compat"
        "../../tinytmux/oyk_tinytmux"
    REQUIRES
        driver
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE OYK_PORTABLE_NODE=1)
