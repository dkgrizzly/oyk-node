/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_USB_HOST_HPP
#define OYK_TINYTMUX_USB_HOST_HPP

/*
 * Teensy 4.1 USB_OTG2 (RHPORT1) USB Host RadioFoundry Transport
 *
 * Strict C++98, zero STL, zero exceptions, zero RTTI.
 *
 * Supports connecting one or more RadioFoundry radios (directly or via a USB hub)
 * to the Teensy 4.1 USB Host header using TinyUSB Host mode (`USB_OTG2`):
 *   1. USB CDC-ECM / CDC-NCM network interfaces (`usb-cdc-net`):
 *      Handles Ethernet + ARP + IPv4/UDP + ICMP Echo + AXUDP + AXIP (IP proto 93)
 *      plus RadioFoundry UDP 4191 zone/channel control datagrams.
 *   2. USB CDC-ACM serial interfaces (`usb-cdc-serial`):
 *      Streams raw bytes into `OykPppLink` (AXUDP/AXIP/UDP 4191 over PPP) or
 *      `OykKissDecoder` (KISS-over-CDC-ACM).
 *
 * Also compiles cleanly without TinyUSB on Linux/macOS/Windows for host unit tests
 * via fault-free simulation hooks.
 */

#include "../compat/oyk_portable.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#define TINYTMUX_MAX_USB_RADIOS 4

enum TinyTmuxUsbDevKind {
    TMUX_USBH_EMPTY   = 0,
    TMUX_USBH_CDC_ECM = 1,
    TMUX_USBH_CDC_NCM = 2,
    TMUX_USBH_CDC_ACM = 3
};

struct TinyTmuxUsbRadioSlot {
    int in_use;
    int kind;                 /* TinyTmuxUsbDevKind */
    oyk_u8 daddr;             /* TinyUSB device address */
    oyk_u8 idx;               /* TinyUSB CDC/NCM interface instance */
    oyk_u8 itf_num;
    oyk_u16 vid;
    oyk_u16 pid;
    oyk_u8 mac[6];
    oyk_u32 local_ip;         /* Network byte order */
    oyk_u32 peer_ip;          /* Network byte order */
    oyk_u8 peer_mac[6];
    int has_peer_mac;
    oyk_u16 ip_id;
    oyk_u16 ncm_seq;
    char label[24];
    unsigned long rx_packets;
    unsigned long tx_packets;
};

/* USB CDC-NCM 16-bit (NTH16 + NDP16) wrap & unwrap helpers */
size_t tinytmux_usb_ncm16_wrap(const oyk_u8* eth_frame, size_t eth_len,
                               oyk_u16 seq, oyk_u8* out, size_t out_cap);
size_t tinytmux_usb_ncm16_unwrap(const oyk_u8* ntb, size_t ntb_len,
                                 oyk_u8* eth_out, size_t eth_cap);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

typedef void (*TinyTmuxUsbEthTxFn)(int slot, const oyk_u8* eth_frame, size_t eth_len, void* ctx);
typedef size_t (*TinyTmuxUsbAcmTxFn)(int slot, const oyk_u8* data, size_t len, void* ctx);
typedef void (*TinyTmuxUsbAcmRxFn)(int slot, const oyk_u8* data, size_t len, void* ctx);
typedef void (*TinyTmuxUsbAx25RxFn)(int slot, int is_axip,
                                    const OykFrame* frame,
                                    oyk_u32 src_ip_net,
                                    oyk_u16 src_port_host,
                                    void* ctx);
typedef void (*TinyTmuxUsbUdpRxFn)(int slot,
                                   oyk_u32 src_ip_net,
                                   oyk_u16 src_port_host,
                                   oyk_u16 dst_port_host,
                                   const oyk_u8* payload,
                                   size_t len,
                                   void* ctx);

class TinyTmuxUsbHost {
public:
    TinyTmuxUsbHost();

    void init();
    void poll();

    /* Parse "usbh0".."usbh3" -> 0..3, or -1 if invalid/auto */
    static int parse_device_slot(const char* dev_name);

    /* Attach/detach radio devices (called by TinyUSB Host callbacks or tests) */
    int attach_slot(int slot, int kind, oyk_u8 daddr, oyk_u8 idx,
                    oyk_u16 vid, oyk_u16 pid, const oyk_u8 mac[6],
                    const char* label);
    void detach_slot(int slot);
    void detach_by_dev(oyk_u8 daddr, int kind, oyk_u8 idx);

    void configure_slot_ips(int slot, oyk_u32 local_ip_net, oyk_u32 peer_ip_net);

    const TinyTmuxUsbRadioSlot* slot_info(int slot) const;
    int find_slot_by_kind(int kind, int start_slot) const;
    int find_first_net_slot() const;
    int find_first_acm_slot() const;
    int count_active() const;
    int has_net_device() const;
    int has_acm_device() const;

    /* Formats short status token for header bar: "NET", "SER", "N+S", or "-" */
    void format_short_status(char* out, size_t out_sz) const;

    void set_eth_tx_hook(TinyTmuxUsbEthTxFn cb, void* ctx);
    void set_acm_tx_hook(TinyTmuxUsbAcmTxFn cb, void* ctx);
    void set_acm_rx_callback(TinyTmuxUsbAcmRxFn cb, void* ctx);
    void set_ax25_rx_callback(TinyTmuxUsbAx25RxFn cb, void* ctx);
    void set_udp_rx_callback(TinyTmuxUsbUdpRxFn cb, void* ctx);

    /* Inbound RX injection (from TinyUSB Host drivers or unit tests) */
    void on_eth_rx(int slot, const oyk_u8* frame, size_t len);
    void on_ncm_rx(int slot, const oyk_u8* ntb, size_t ntb_len);
    void on_acm_rx(int slot, const oyk_u8* data, size_t len);

    /* Outbound TX APIs */
    int send_eth_frame(int slot, const oyk_u8* eth_frame, size_t eth_len);
    int send_arp_request(int slot, oyk_u32 target_ip_net);
    int send_udp(int slot, oyk_u32 dst_ip_net, oyk_u16 src_port_host,
                 oyk_u16 dst_port_host, const oyk_u8* payload, size_t pay_len);
    int send_axudp(int slot, const OykFrame* frame, oyk_u32 dst_ip_net,
                   oyk_u16 src_port_host, oyk_u16 dst_port_host);
    int send_axip(int slot, const OykFrame* frame, oyk_u32 dst_ip_net);
    size_t send_acm_bytes(int slot, const oyk_u8* data, size_t len);

private:
    TinyTmuxUsbRadioSlot slots_[TINYTMUX_MAX_USB_RADIOS];
    oyk_u8 scratch_tx_[1600];
    oyk_u8 scratch_rx_[1600];

    TinyTmuxUsbEthTxFn eth_tx_hook_;
    void* eth_tx_ctx_;
    TinyTmuxUsbAcmTxFn acm_tx_hook_;
    void* acm_tx_ctx_;
    TinyTmuxUsbAcmRxFn acm_rx_cb_;
    void* acm_rx_ctx_;
    TinyTmuxUsbAx25RxFn ax25_rx_cb_;
    void* ax25_rx_ctx_;
    TinyTmuxUsbUdpRxFn udp_rx_cb_;
    void* udp_rx_ctx_;

    int send_ipv4_packet(int slot, oyk_u32 dst_ip_net, oyk_u8 proto,
                         const oyk_u8* payload, size_t pay_len);
    void handle_arp(int slot, const oyk_u8* frame, size_t len);
    void handle_ipv4(int slot, const oyk_u8* frame, size_t len);
};

void tinytmux_usb_host_set_active(TinyTmuxUsbHost* host);
TinyTmuxUsbHost* tinytmux_usb_host_active();

#endif /* __cplusplus */

#endif /* OYK_TINYTMUX_USB_HOST_HPP */
