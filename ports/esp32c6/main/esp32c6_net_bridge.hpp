/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ESP32C6_NET_BRIDGE_HPP
#define OYK_ESP32C6_NET_BRIDGE_HPP

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"
#include "oyk_ppp.hpp"
#include "board_esp32c6.hpp"
#include "esp32c6_lora.hpp"
#include "esp32c6_radio.hpp"

#define C6L_BRIDGE_MAX_SLOTS 8

struct Esp32C6BridgeSlot {
    int used;
    int iface_index;
    int is_axudp;
    int is_axip;
    int is_kiss_tcp;
    int is_telnet;
    int is_lora;
    int is_bt_kiss;
    oyk_u16 listen_port;
    int attached_uart;
    int active;
    char status[48];
};

class Esp32C6NetBridge {
public:
    Esp32C6NetBridge();
    ~Esp32C6NetBridge();

    void init(OykPortableNode* node, const Esp32C6RadioConfig* rcfg, const Esp32C6LoraConfig* lcfg);
    void reconfig();
    void poll(oyk_u32 now_ms);

    static int tx_callback_trampoline(int iface_idx, const OykFrame* frame,
                                      oyk_u32 peer_ip, oyk_u16 peer_port,
                                      oyk_u16 peer_handle, void* ctx);
    int tx_frame(int iface_index, const OykFrame* frame,
                 oyk_u32 peer_ip, oyk_u16 peer_port, oyk_u16 peer_handle);

    const Esp32C6BridgeSlot* slot(int idx) const;
    const OykPppLink* ppp_link(int uart_idx) const;
    void format_short_radio_status(char* out, size_t out_sz) const;

    void sim_feed_uart_bytes(int uart_idx, const oyk_u8* data, size_t len, oyk_u32 now_ms);
    void sim_inject_bt_kiss(const oyk_u8* data, size_t len);
    void sim_inject_lora_kiss(const oyk_u8* data, size_t len, int rssi_dbm, int snr_x4);

private:
    static void ppp_ax25_rx0(int is_axip, const OykFrame* frame,
                             oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx);
    static size_t ppp_serial_tx0(const oyk_u8* data, size_t len, void* ctx);
    static void ble_kiss_rx_trampoline(const oyk_u8* data, size_t len, void* ctx);
    static void lora_kiss_rx_trampoline(const oyk_u8* data, size_t len, void* ctx);

    void on_kiss_byte(int iface_idx, oyk_u8 b);

    OykPortableNode* node_;
    Esp32C6RadioConfig radio_cfg_;
    Esp32C6LoraConfig lora_cfg_;
    Esp32C6BridgeSlot slots_[C6L_BRIDGE_MAX_SLOTS];
    OykPppLink ppp_[OYK_ESP32C6_UART_COUNT];
    int uart_role_[OYK_ESP32C6_UART_COUNT];
    int uart_iface_[OYK_ESP32C6_UART_COUNT];
    OykKissDecoder uart_kiss_[OYK_ESP32C6_UART_COUNT];
    OykKissDecoder lora_kiss_;
    OykKissDecoder bt_kiss_;
    int lora_iface_idx_;
    int bt_iface_idx_;
};

#endif /* OYK_ESP32C6_NET_BRIDGE_HPP */
