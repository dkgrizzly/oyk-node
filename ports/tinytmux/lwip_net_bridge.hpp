/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_LWIP_BRIDGE_HPP
#define OYK_TINYTMUX_LWIP_BRIDGE_HPP

/*
 * Dual-transport network bridge for Teensy 4.1 / IMXRT1060 (TinyTMUX):
 *   1. 10/100 Ethernet (lwIP ENET + DP83825I): AXUDP (UDP), AXIP (IP proto 93), Telnet (TCP)
 *   2. PPP over Serial (ser0..ser7, RadioFoundry radios): AXUDP & AXIP over RFC 1661/1662/1332 PPP
 *   3. KISS over Serial (ser0..ser7): standard KISS TNC links
 *
 * Bridges all transports concurrently into a single OykPortableNode instance.
 */

#include "../compat/oyk_portable.hpp"
#include "board_tinytmux.hpp"
#include "oyk_ppp.hpp"

#define TINYTMUX_MAX_PPP_LINKS  4

enum TinyTmuxIfTransport {
    TMUX_IF_NONE = 0,
    TMUX_IF_ENET_AXUDP,
    TMUX_IF_ENET_AXIP,
    TMUX_IF_ENET_TELNET,
    TMUX_IF_PPP_AXUDP,
    TMUX_IF_PPP_AXIP,
    TMUX_IF_KISS_SERIAL
};

/* Optional Ethernet raw packet injection/capture hooks (for host testing or custom MAC driver) */
typedef int (*TinyTmuxEnetTxFn)(int is_axip,
                                oyk_u32 dst_ip_net,
                                oyk_u16 dst_port_host,
                                const oyk_u8* payload_with_fcs,
                                size_t len,
                                void* ctx);

class TinyTmuxNetBridge {
public:
    TinyTmuxNetBridge();

    int init(OykPortableNode* node, oyk_u32 now_ms);
    void set_ethernet_tx_hook(TinyTmuxEnetTxFn fn, void* ctx);
    void set_ethernet_link_state(int link_up, int speed_100mbps, oyk_u32 ip_net);

    /* Configure a PPP link slot bound to a TinyTMUX serial port (ser0..ser7) */
    int configure_ppp_slot(int ppp_slot,
                           int logical_uart,
                           int axudp_iface_idx,
                           int axip_iface_idx,
                           oyk_u32 local_ip_net,
                           oyk_u32 peer_ip_net,
                           oyk_u16 axudp_port,
                           OykPppSerialTxFn custom_serial_tx,
                           void* custom_serial_ctx,
                           oyk_u32 now_ms);

    /* Feed raw serial bytes into a PPP slot (from LPUART RX or host test pipe) */
    void feed_ppp_serial(int ppp_slot, const oyk_u8* data, size_t len, oyk_u32 now_ms);

    /* Feed an incoming Ethernet AXUDP or AXIP payload (with 2-byte AX.25 FCS) */
    void on_ethernet_rx(int is_axip,
                        oyk_u32 src_ip_net,
                        oyk_u16 src_port_host,
                        const oyk_u8* payload_with_fcs,
                        size_t len);

    /* Periodic poll: pumps lwIP timers, UART RX (PPP & KISS), and PPP keepalives */
    void poll(oyk_u32 now_ms);

    /* OykPortableNode transmit callback trampoline */
    static int node_tx_callback(int iface_idx,
                                const OykFrame* frame,
                                oyk_u32 peer_ip,
                                oyk_u16 peer_port,
                                oyk_u16 peer_handle,
                                void* user_ctx);

    OykPppLink* ppp_link(int slot);
    const OykPppLink* ppp_link(int slot) const;
    int ethernet_link_up() const { return enet_up_; }
    oyk_u32 ethernet_ip() const { return enet_ip_; }

private:
    struct PppSlot {
        int active;
        int logical_uart;
        int axudp_iface_idx;
        int axip_iface_idx;
        OykPppLink link;
        TinyTmuxNetBridge* owner;
    };

    struct IfaceBinding {
        TinyTmuxIfTransport kind;
        int ppp_slot;       /* Index into ppp_slots_ for PPP links */
        int logical_uart;   /* Index 0..7 for KISS serial */
        OykKissDecoder kiss_dec;
    };

    int handle_node_tx(int iface_idx,
                       const OykFrame* frame,
                       oyk_u32 peer_ip,
                       oyk_u16 peer_port,
                       oyk_u16 peer_handle);

    static size_t uart_serial_tx_trampoline(const oyk_u8* data, size_t len, void* ctx);
    static void ppp_ax25_rx_trampoline(int is_axip,
                                       const OykFrame* frame,
                                       oyk_u32 src_ip_net,
                                       oyk_u16 src_port_host,
                                       void* ctx);

    OykPortableNode* node_;
    int enet_up_;
    int enet_100m_;
    oyk_u32 enet_ip_;
    TinyTmuxEnetTxFn enet_tx_fn_;
    void* enet_tx_ctx_;

    IfaceBinding bindings_[OYK_MAX_INTERFACES];
    PppSlot ppp_slots_[TINYTMUX_MAX_PPP_LINKS];
};

#endif /* OYK_TINYTMUX_LWIP_BRIDGE_HPP */
