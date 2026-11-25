/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_LWIP_BRIDGE_HPP
#define OYK_TINYTMUX_LWIP_BRIDGE_HPP

/*
 * Multi-transport network & rig-control bridge for Teensy 4.1 / IMXRT1060 (TinyTMUX):
 *   1. 10/100 Ethernet (lwIP ENET + DP83825I): AXUDP (UDP), AXIP (IP proto 93), Telnet (TCP)
 *   2. PPP over Serial (ser0..ser7, RadioFoundry radios): AXUDP, AXIP, UDP 4191
 *   3. KISS over Serial (ser0..ser7): standard KISS TNC links
 *   4. USB Host (USB_OTG2) RadioFoundry radios:
 *        - CDC-ECM / CDC-NCM (`usb-cdc-net`, `usbh0..usbh3`): Ethernet/ARP/IPv4/UDP/AXUDP/AXIP + UDP 4191
 *        - CDC-ACM (`usb-cdc-serial`, `usbh0..usbh3`): PPP or KISS over USB Host serial
 *   5. Rig Control (`oyk_rig`): Hamlib `rigctld` TCP (4532) & RadioFoundry UDP (4191)
 *
 * Bridges all transports concurrently into a single OykPortableNode instance.
 */

#include "../compat/oyk_portable.hpp"
#include "../compat/oyk_rig.hpp"
#include "board_tinytmux.hpp"
#include "oyk_ppp.hpp"
#include "tinytmux_usb_host.hpp"

#define TINYTMUX_MAX_PPP_LINKS  4

enum TinyTmuxIfTransport {
    TMUX_IF_NONE = 0,
    TMUX_IF_ENET_AXUDP,
    TMUX_IF_ENET_AXIP,
    TMUX_IF_ENET_TELNET,
    TMUX_IF_PPP_AXUDP,
    TMUX_IF_PPP_AXIP,
    TMUX_IF_KISS_SERIAL,
    TMUX_IF_USB_NET_AXUDP,
    TMUX_IF_USB_NET_AXIP,
    TMUX_IF_USB_ACM_KISS
};

/* Optional Ethernet raw packet injection/capture hooks (for host testing or custom MAC driver) */
typedef int (*TinyTmuxEnetTxFn)(int is_axip,
                                oyk_u32 dst_ip_net,
                                oyk_u16 dst_port_host,
                                const oyk_u8* payload_with_fcs,
                                size_t len,
                                void* ctx);

/* Optional Ethernet UDP TX hook (used for UDP 4191 rig control over Ethernet / simulation) */
typedef int (*TinyTmuxEnetUdpTxFn)(oyk_u32 dst_ip_net,
                                   oyk_u16 src_port_host,
                                   oyk_u16 dst_port_host,
                                   const oyk_u8* payload,
                                   size_t len,
                                   void* ctx);

/* Optional TCP request/reply hook for Hamlib rigctld (port 4532) */
typedef int (*TinyTmuxRigTcpExchangeFn)(oyk_u32 host_ip_net,
                                        oyk_u16 port_host,
                                        const char* req,
                                        size_t req_len,
                                        char* reply_out,
                                        size_t reply_cap,
                                        void* ctx);

class TinyTmuxNetBridge {
public:
    TinyTmuxNetBridge();

    int init(OykPortableNode* node, oyk_u32 now_ms);
    int rebind_interfaces(oyk_u32 now_ms);

    void set_ethernet_tx_hook(TinyTmuxEnetTxFn fn, void* ctx);
    void set_ethernet_udp_tx_hook(TinyTmuxEnetUdpTxFn fn, void* ctx);
    void set_rig_tcp_hook(TinyTmuxRigTcpExchangeFn fn, void* ctx);
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

    /* Configure a PPP link slot bound to a USB Host CDC-ACM slot (usbh0..usbh3) */
    int configure_usb_ppp_slot(int ppp_slot,
                               int usb_slot,
                               int axudp_iface_idx,
                               int axip_iface_idx,
                               oyk_u32 local_ip_net,
                               oyk_u32 peer_ip_net,
                               oyk_u16 axudp_port,
                               oyk_u32 now_ms);

    /* Feed raw serial bytes into a PPP slot (from LPUART RX or host test pipe) */
    void feed_ppp_serial(int ppp_slot, const oyk_u8* data, size_t len, oyk_u32 now_ms);

    /* Feed an incoming Ethernet AXUDP or AXIP payload (with 2-byte AX.25 FCS) */
    void on_ethernet_rx(int is_axip,
                        oyk_u32 src_ip_net,
                        oyk_u16 src_port_host,
                        const oyk_u8* payload_with_fcs,
                        size_t len);

    /* Feed an incoming Ethernet UDP payload (e.g. RadioFoundry UDP 4191 reply/notification) */
    void on_ethernet_udp_rx(oyk_u32 src_ip_net,
                            oyk_u16 src_port_host,
                            oyk_u16 dst_port_host,
                            const oyk_u8* payload,
                            size_t len);

    /* Periodic poll: pumps lwIP timers, USB Host, UART RX (PPP & KISS), PPP & rig keepalives */
    void poll(oyk_u32 now_ms);

    /* Rig Control (Hamlib rigctld TCP 4532 + RadioFoundry UDP 4191) */
    void sync_rig_config();
    const OykRigStatus* rig_status() const { return &rig_status_; }
    OykRigStatus* rig_status_mut() { return &rig_status_; }
    int rig_poll_now(oyk_u32 now_ms);
    int rig_set_freq(oyk_u32 hz);
    int rig_set_mode(const char* mode, oyk_u32 passband_hz);
    int rig_set_ptt(int tx);
    int rig_set_vfo(const char* vfo);
    int rig_rf_select(int zone, int pos);
    int rig_rf_step_channel(int delta);
    int rig_rf_step_zone(int delta);
    int rig_rf_stop_notify();

    /* OykPortableNode transmit callback trampoline */
    static int node_tx_callback(int iface_idx,
                                const OykFrame* frame,
                                oyk_u32 peer_ip,
                                oyk_u16 peer_port,
                                oyk_u16 peer_handle,
                                void* user_ctx);

    OykPppLink* ppp_link(int slot);
    const OykPppLink* ppp_link(int slot) const;
    TinyTmuxUsbHost* usb_host() { return &usb_host_; }
    const TinyTmuxUsbHost* usb_host() const { return &usb_host_; }
    int ethernet_link_up() const { return enet_up_; }
    oyk_u32 ethernet_ip() const { return enet_ip_; }

private:
    struct PppSlot {
        int active;
        int logical_uart;
        int usb_slot;
        int usb_attached;
        int axudp_iface_idx;
        int axip_iface_idx;
        OykPppLink link;
        TinyTmuxNetBridge* owner;
    };

    struct IfaceBinding {
        TinyTmuxIfTransport kind;
        int ppp_slot;       /* Index into ppp_slots_ for PPP links */
        int logical_uart;   /* Index 0..7 for KISS serial */
        int usb_slot;       /* Index 0..3 for USB Host (usbh0..usbh3) */
        OykKissDecoder kiss_dec;
    };

    int handle_node_tx(int iface_idx,
                       const OykFrame* frame,
                       oyk_u32 peer_ip,
                       oyk_u16 peer_port,
                       oyk_u16 peer_handle);

    int send_rf_udp_datagram(const char* payload, size_t len);
    int rigctl_exchange(const char* cmd, size_t cmd_len, char* reply_out, size_t reply_cap);
    void handle_rf_udp_payload(const oyk_u8* payload, size_t len);

    static size_t uart_serial_tx_trampoline(const oyk_u8* data, size_t len, void* ctx);
    static size_t usb_acm_tx_trampoline(const oyk_u8* data, size_t len, void* ctx);
    static void ppp_ax25_rx_trampoline(int is_axip,
                                       const OykFrame* frame,
                                       oyk_u32 src_ip_net,
                                       oyk_u16 src_port_host,
                                       void* ctx);
    static void ppp_udp_rx_trampoline(oyk_u32 src_ip_net,
                                      oyk_u16 src_port_host,
                                      oyk_u16 dst_port_host,
                                      const oyk_u8* payload,
                                      size_t len,
                                      void* ctx);
    static void usb_acm_rx_trampoline(int slot, const oyk_u8* data, size_t len, void* ctx);
    static void usb_ax25_rx_trampoline(int slot, int is_axip, const OykFrame* frame,
                                       oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx);
    static void usb_udp_rx_trampoline(int slot, oyk_u32 src_ip_net, oyk_u16 src_port_host,
                                      oyk_u16 dst_port_host, const oyk_u8* payload,
                                      size_t len, void* ctx);

    OykPortableNode* node_;
    int enet_up_;
    int enet_100m_;
    oyk_u32 enet_ip_;
    oyk_u32 last_poll_ms_;
    TinyTmuxEnetTxFn enet_tx_fn_;
    void* enet_tx_ctx_;
    TinyTmuxEnetUdpTxFn enet_udp_tx_fn_;
    void* enet_udp_tx_ctx_;
    TinyTmuxRigTcpExchangeFn rig_tcp_fn_;
    void* rig_tcp_ctx_;

    TinyTmuxUsbHost usb_host_;
    OykRigStatus rig_status_;
    oyk_u32 rig_last_poll_ms_;

    IfaceBinding bindings_[OYK_MAX_INTERFACES];
    PppSlot ppp_slots_[TINYTMUX_MAX_PPP_LINKS];
};

#endif /* OYK_TINYTMUX_LWIP_BRIDGE_HPP */
