/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_NET_BRIDGE_HPP
#define OYK_LYRA_NET_BRIDGE_HPP

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"
#include "board_lyra.hpp"
#include "oyk_ppp.hpp"
#include "lyra_radio.hpp"

#define LYRA_BRIDGE_MAX_SOCKETS 8

struct LyraBridgeSlot {
    int used;
    int iface_index;
    int is_axudp;
    int is_axip;
    int is_kiss_tcp;
    int is_telnet;
    int is_bt_kiss;
    int sock_fd;
    oyk_u16 listen_port;
    int attached_uart;
    int active;
    char status[48];
};

class LyraNetBridge {
public:
    LyraNetBridge();
    ~LyraNetBridge();

    void init(OykPortableNode* node, const LyraRadioConfig* radio_cfg, int sim_net_up);
    void reconfig();
    void poll(oyk_u32 now_ms);

    static int tx_callback_trampoline(int iface_idx, const OykFrame* frame,
                                      oyk_u32 peer_ip, oyk_u16 peer_port,
                                      oyk_u16 peer_handle, void* ctx);
    int tx_frame(int iface_index, const OykFrame* frame,
                 oyk_u32 peer_ip, oyk_u16 peer_port, oyk_u16 peer_handle);

    const LyraBridgeSlot* slot(int idx) const;
    int active_uart_count() const;
    const OykPppLink* ppp_link(int uart_idx) const;
    const LyraRadioState* radio_state() const;
    void format_short_net_status(char* out, size_t out_sz) const;

    void sim_feed_uart_bytes(int uart_idx, const oyk_u8* data, size_t len, oyk_u32 now_ms);
    void sim_inject_ble_kiss(const oyk_u8* data, size_t len);

private:
    static void ppp_ax25_rx_trampoline0(int is_axip, const OykFrame* frame,
                                        oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx);
    static void ppp_ax25_rx_trampoline1(int is_axip, const OykFrame* frame,
                                        oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx);
    static size_t ppp_serial_tx0(const oyk_u8* data, size_t len, void* ctx);
    static size_t ppp_serial_tx1(const oyk_u8* data, size_t len, void* ctx);
    static void ble_kiss_rx_trampoline(const oyk_u8* data, size_t len, void* ctx);

    void close_all_sockets();
    void on_ppp_ax25_rx(int uart_idx, int is_axip, const OykFrame* frame,
                        oyk_u32 src_ip_net, oyk_u16 src_port_host);
    void on_kiss_byte(int iface_idx, oyk_u8 b);

    OykPortableNode* node_;
    LyraRadioConfig radio_cfg_;
    LyraBridgeSlot slots_[LYRA_BRIDGE_MAX_SOCKETS];
    OykPppLink ppp_[OYK_LYRA_UART_COUNT];
    int uart_role_[OYK_LYRA_UART_COUNT];
    int uart_iface_[OYK_LYRA_UART_COUNT];
    OykKissDecoder uart_kiss_[OYK_LYRA_UART_COUNT];
    OykKissDecoder ble_kiss_;
    int ble_iface_idx_;
    oyk_u8 tx_raw_[512];
    oyk_u8 tx_kiss_[600];
};

#endif /* OYK_LYRA_NET_BRIDGE_HPP */
