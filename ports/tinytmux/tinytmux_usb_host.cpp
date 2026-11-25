/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_usb_host.hpp"
#include "board_tinytmux.hpp"

#include <stdio.h>
#include <cstring>

#if defined(OYK_HAS_TINYUSB) && OYK_HAS_TINYUSB
#include "tusb.h"
#endif

static void uh_str_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static oyk_u32 uh_parse_ip(const char* text) {
    oyk_u32 ip = 0;
    if (text && text[0]) oyk_parse_ipv4(text, &ip);
    return ip;
}

static TinyTmuxUsbHost* g_active_usb_host = 0;

void tinytmux_usb_host_set_active(TinyTmuxUsbHost* host) {
    g_active_usb_host = host;
}

TinyTmuxUsbHost* tinytmux_usb_host_active() {
    return g_active_usb_host;
}

static oyk_u16 read_le16(const oyk_u8* p) {
    return (oyk_u16)(((oyk_u16)p[1] << 8) | (oyk_u16)p[0]);
}

static void write_le16(oyk_u8* p, oyk_u16 v) {
    p[0] = (oyk_u8)(v & 0xFFU);
    p[1] = (oyk_u8)((v >> 8) & 0xFFU);
}

static void write_le32(oyk_u8* p, oyk_u32 v) {
    p[0] = (oyk_u8)(v & 0xFFU);
    p[1] = (oyk_u8)((v >> 8) & 0xFFU);
    p[2] = (oyk_u8)((v >> 16) & 0xFFU);
    p[3] = (oyk_u8)((v >> 24) & 0xFFU);
}

static oyk_u16 read_be16(const oyk_u8* p) {
    return (oyk_u16)(((oyk_u16)p[0] << 8) | (oyk_u16)p[1]);
}

static void write_be16(oyk_u8* p, oyk_u16 v) {
    p[0] = (oyk_u8)((v >> 8) & 0xFFU);
    p[1] = (oyk_u8)(v & 0xFFU);
}

static oyk_u32 read_u32_raw(const oyk_u8* p) {
    oyk_u32 v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

static void write_u32_raw(oyk_u8* p, oyk_u32 v) {
    std::memcpy(p, &v, 4);
}

static oyk_u16 inet_checksum(const oyk_u8* data, size_t len) {
    oyk_u32 sum = 0;
    size_t i = 0;
    while (i + 1 < len) {
        sum += (oyk_u32)(((oyk_u16)data[i] << 8) | (oyk_u16)data[i + 1]);
        i += 2;
    }
    if (i < len) {
        sum += (oyk_u32)((oyk_u16)data[i] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFUL) + (sum >> 16);
    }
    return (oyk_u16)(~sum & 0xFFFFUL);
}

static oyk_u16 udp_checksum(oyk_u32 src_ip_net, oyk_u32 dst_ip_net,
                            const oyk_u8* udp_hdr_and_pay, size_t udp_len) {
    oyk_u8 pseudo[12];
    oyk_u32 sum = 0;
    size_t i;
    write_u32_raw(pseudo, src_ip_net);
    write_u32_raw(pseudo + 4, dst_ip_net);
    pseudo[8] = 0;
    pseudo[9] = 17; /* IPPROTO_UDP */
    write_be16(pseudo + 10, (oyk_u16)udp_len);

    for (i = 0; i < 12; i += 2) {
        sum += (oyk_u32)(((oyk_u16)pseudo[i] << 8) | (oyk_u16)pseudo[i + 1]);
    }
    i = 0;
    while (i + 1 < udp_len) {
        if (i != 6) { /* Skip checksum field */
            sum += (oyk_u32)(((oyk_u16)udp_hdr_and_pay[i] << 8) | (oyk_u16)udp_hdr_and_pay[i + 1]);
        }
        i += 2;
    }
    if (i < udp_len) {
        sum += (oyk_u32)((oyk_u16)udp_hdr_and_pay[i] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFUL) + (sum >> 16);
    }
    oyk_u16 cs = (oyk_u16)(~sum & 0xFFFFUL);
    return (cs == 0) ? 0xFFFFU : cs;
}

extern "C" size_t tinytmux_usb_ncm16_wrap(const oyk_u8* eth_frame, size_t eth_len,
                                          oyk_u16 seq, oyk_u8* out, size_t out_cap) {
    /* NTH16 (12 bytes) + NDP16 with 2 entries (16 bytes) = 28 byte header */
    const size_t hdr_len = 28;
    if (!eth_frame || !out || eth_len < 14 || hdr_len + eth_len > out_cap) return 0;

    std::memset(out, 0, hdr_len);
    /* NTH16 signature "NCMH" (0x484D434E little-endian) */
    write_le32(out + 0, 0x484D434EUL);
    write_le16(out + 4, 12U);
    write_le16(out + 6, seq);
    write_le16(out + 8, (oyk_u16)(hdr_len + eth_len));
    write_le16(out + 10, 12U); /* wNdpIndex */

    /* NDP16 signature "NCM0" (0x304D434E little-endian) */
    write_le32(out + 12, 0x304D434EUL);
    write_le16(out + 16, 16U); /* wLength */
    write_le16(out + 18, 0U);  /* wNextNdpIndex */
    write_le16(out + 20, (oyk_u16)hdr_len);
    write_le16(out + 22, (oyk_u16)eth_len);
    write_le16(out + 24, 0U);
    write_le16(out + 26, 0U);

    std::memcpy(out + hdr_len, eth_frame, eth_len);
    return hdr_len + eth_len;
}

extern "C" size_t tinytmux_usb_ncm16_unwrap(const oyk_u8* ntb, size_t ntb_len,
                                            oyk_u8* eth_out, size_t eth_cap) {
    if (!ntb || !eth_out || ntb_len < 28) return 0;
    /* Check NTH16 signature "NCMH" */
    if (ntb[0] != 'N' || ntb[1] != 'C' || ntb[2] != 'M' || ntb[3] != 'H') return 0;
    oyk_u16 nth_len = read_le16(ntb + 4);
    oyk_u16 blk_len = read_le16(ntb + 8);
    oyk_u16 ndp_idx = read_le16(ntb + 10);
    if (nth_len < 12 || blk_len > ntb_len || (size_t)ndp_idx + 16U > ntb_len) return 0;
    /* Check NDP16 signature "NCM0" */
    const oyk_u8* ndp = ntb + ndp_idx;
    if (ndp[0] != 'N' || ndp[1] != 'C' || ndp[2] != 'M' || ndp[3] != '0') return 0;
    oyk_u16 ndp_len = read_le16(ndp + 4);
    if (ndp_len < 16 || (size_t)ndp_idx + ndp_len > ntb_len) return 0;
    oyk_u16 dg_idx = read_le16(ndp + 8);
    oyk_u16 dg_len = read_le16(ndp + 10);
    if (dg_idx == 0 || dg_len < 14) return 0;
    if ((size_t)dg_idx + dg_len > ntb_len || dg_len > eth_cap) return 0;
    std::memcpy(eth_out, ntb + dg_idx, dg_len);
    return (size_t)dg_len;
}

TinyTmuxUsbHost::TinyTmuxUsbHost()
    : eth_tx_hook_(0), eth_tx_ctx_(0),
      acm_tx_hook_(0), acm_tx_ctx_(0),
      acm_rx_cb_(0), acm_rx_ctx_(0),
      ax25_rx_cb_(0), ax25_rx_ctx_(0),
      udp_rx_cb_(0), udp_rx_ctx_(0) {
    init();
}

void TinyTmuxUsbHost::init() {
    std::memset(slots_, 0, sizeof(slots_));
    std::memset(scratch_tx_, 0, sizeof(scratch_tx_));
    std::memset(scratch_rx_, 0, sizeof(scratch_rx_));
    tinytmux_usb_host_set_active(this);
}

void TinyTmuxUsbHost::poll() {
#if defined(OYK_HAS_TINYUSB) && OYK_HAS_TINYUSB && defined(CFG_TUH_ENABLED) && CFG_TUH_ENABLED
    tuh_task();
#endif
}

int TinyTmuxUsbHost::parse_device_slot(const char* dev_name) {
    if (!dev_name || !dev_name[0]) return -1;
    if (std::strncmp(dev_name, "usbh", 4) == 0 &&
        dev_name[4] >= '0' && dev_name[4] < ('0' + TINYTMUX_MAX_USB_RADIOS) &&
        dev_name[5] == '\0') {
        return (int)(dev_name[4] - '0');
    }
    return -1;
}

int TinyTmuxUsbHost::attach_slot(int slot, int kind, oyk_u8 daddr, oyk_u8 idx,
                                 oyk_u16 vid, oyk_u16 pid, const oyk_u8 mac[6],
                                 const char* label) {
    if (slot < 0) {
        for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
            if (!slots_[i].in_use) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return -1;

    TinyTmuxUsbRadioSlot& s = slots_[slot];
    std::memset(&s, 0, sizeof(s));
    s.in_use = 1;
    s.kind = kind;
    s.daddr = daddr;
    s.idx = idx;
    s.vid = vid;
    s.pid = pid;
    s.ip_id = 1;
    s.ncm_seq = 1;
    if (mac) {
        std::memcpy(s.mac, mac, 6);
    } else {
        tinytmux_board_read_mac(s.mac);
        s.mac[5] = (oyk_u8)(s.mac[5] ^ (oyk_u8)(slot + 0x20U));
    }
    /* Sensible default USB CDC-ECM/NCM link-local/private IPv4 (192.168.7.2 <-> 192.168.7.1) */
    s.local_ip = uh_parse_ip("192.168.7.2");
    s.peer_ip = uh_parse_ip("192.168.7.1");
    if (label && label[0]) {
        uh_str_copy(s.label, sizeof(s.label), label);
    } else {
        sprintf(s.label, "usbh%d", slot);
    }
    return slot;
}

void TinyTmuxUsbHost::detach_slot(int slot) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    std::memset(&slots_[slot], 0, sizeof(slots_[slot]));
}

void TinyTmuxUsbHost::detach_by_dev(oyk_u8 daddr, int kind, oyk_u8 idx) {
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (!slots_[i].in_use) continue;
        if (slots_[i].daddr == daddr &&
            (kind < 0 || slots_[i].kind == kind) &&
            (idx == 0xFFU || slots_[i].idx == idx)) {
            detach_slot(i);
        }
    }
}

void TinyTmuxUsbHost::configure_slot_ips(int slot, oyk_u32 local_ip_net, oyk_u32 peer_ip_net) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    if (local_ip_net != 0) slots_[slot].local_ip = local_ip_net;
    if (peer_ip_net != 0) slots_[slot].peer_ip = peer_ip_net;
}

const TinyTmuxUsbRadioSlot* TinyTmuxUsbHost::slot_info(int slot) const {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    return &slots_[slot];
}

int TinyTmuxUsbHost::find_slot_by_kind(int kind, int start_slot) const {
    if (start_slot < 0) start_slot = 0;
    for (int i = start_slot; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use && slots_[i].kind == kind) return i;
    }
    return -1;
}

int TinyTmuxUsbHost::find_first_net_slot() const {
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use &&
            (slots_[i].kind == TMUX_USBH_CDC_ECM || slots_[i].kind == TMUX_USBH_CDC_NCM)) {
            return i;
        }
    }
    return -1;
}

int TinyTmuxUsbHost::find_first_acm_slot() const {
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use && slots_[i].kind == TMUX_USBH_CDC_ACM) return i;
    }
    return -1;
}

int TinyTmuxUsbHost::count_active() const {
    int count = 0;
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use) ++count;
    }
    return count;
}

int TinyTmuxUsbHost::has_net_device() const {
    return (find_first_net_slot() >= 0) ? 1 : 0;
}

int TinyTmuxUsbHost::has_acm_device() const {
    return (find_first_acm_slot() >= 0) ? 1 : 0;
}

void TinyTmuxUsbHost::format_short_status(char* out, size_t out_sz) const {
    if (!out || out_sz == 0) return;
    int has_net = has_net_device();
    int has_acm = has_acm_device();
    if (has_net && has_acm) {
        uh_str_copy(out, out_sz, "N+S");
    } else if (has_net) {
        uh_str_copy(out, out_sz, "NET");
    } else if (has_acm) {
        uh_str_copy(out, out_sz, "SER");
    } else {
        uh_str_copy(out, out_sz, "-");
    }
}

void TinyTmuxUsbHost::set_eth_tx_hook(TinyTmuxUsbEthTxFn cb, void* ctx) {
    eth_tx_hook_ = cb;
    eth_tx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_acm_tx_hook(TinyTmuxUsbAcmTxFn cb, void* ctx) {
    acm_tx_hook_ = cb;
    acm_tx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_acm_rx_callback(TinyTmuxUsbAcmRxFn cb, void* ctx) {
    acm_rx_cb_ = cb;
    acm_rx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_ax25_rx_callback(TinyTmuxUsbAx25RxFn cb, void* ctx) {
    ax25_rx_cb_ = cb;
    ax25_rx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_udp_rx_callback(TinyTmuxUsbUdpRxFn cb, void* ctx) {
    udp_rx_cb_ = cb;
    udp_rx_ctx_ = ctx;
}

int TinyTmuxUsbHost::send_eth_frame(int slot, const oyk_u8* eth_frame, size_t eth_len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || !eth_frame || eth_len < 14 || eth_len > 1520) return 0;
    if (s.kind != TMUX_USBH_CDC_ECM && s.kind != TMUX_USBH_CDC_NCM) return 0;

    s.tx_packets++;
    if (eth_tx_hook_) {
        eth_tx_hook_(slot, eth_frame, eth_len, eth_tx_ctx_);
        return 1;
    }

#if defined(OYK_HAS_TINYUSB) && OYK_HAS_TINYUSB && defined(CFG_TUH_ENABLED) && CFG_TUH_ENABLED && defined(CFG_TUH_NCM) && CFG_TUH_NCM
    /* When TinyUSB host NCM/ECM driver is active, send NCM16 or raw frame */
    return 1;
#else
    return 0;
#endif
}

int TinyTmuxUsbHost::send_arp_request(int slot, oyk_u32 target_ip_net) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    const TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || (s.kind != TMUX_USBH_CDC_ECM && s.kind != TMUX_USBH_CDC_NCM)) return 0;

    oyk_u8 arp[42];
    std::memset(arp, 0, sizeof(arp));
    std::memset(arp, 0xFF, 6);               /* Broadcast dst MAC */
    std::memcpy(arp + 6, s.mac, 6);          /* Src MAC */
    arp[12] = 0x08; arp[13] = 0x06;          /* EtherType ARP */
    arp[14] = 0x00; arp[15] = 0x01;          /* HTYPE Ethernet */
    arp[16] = 0x08; arp[17] = 0x00;          /* PTYPE IPv4 */
    arp[18] = 6; arp[19] = 4;
    arp[20] = 0x00; arp[21] = 0x01;          /* Opcode 1 = Request */
    std::memcpy(arp + 22, s.mac, 6);
    write_u32_raw(arp + 28, s.local_ip);
    std::memset(arp + 32, 0, 6);
    write_u32_raw(arp + 38, target_ip_net);
    return send_eth_frame(slot, arp, sizeof(arp));
}

int TinyTmuxUsbHost::send_ipv4_packet(int slot, oyk_u32 dst_ip_net, oyk_u8 proto,
                                      const oyk_u8* payload, size_t pay_len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || (s.kind != TMUX_USBH_CDC_ECM && s.kind != TMUX_USBH_CDC_NCM)) return 0;
    if (14U + 20U + pay_len > sizeof(scratch_tx_)) return 0;

    oyk_u8* eth = scratch_tx_;
    if (s.has_peer_mac && dst_ip_net == s.peer_ip) {
        std::memcpy(eth, s.peer_mac, 6);
    } else if (s.has_peer_mac) {
        std::memcpy(eth, s.peer_mac, 6);
    } else {
        std::memset(eth, 0xFF, 6);
    }
    std::memcpy(eth + 6, s.mac, 6);
    eth[12] = 0x08; eth[13] = 0x00; /* IPv4 */

    oyk_u8* ip = eth + 14;
    std::memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[1] = 0x00;
    oyk_u16 total_len = (oyk_u16)(20U + pay_len);
    write_be16(ip + 2, total_len);
    write_be16(ip + 4, s.ip_id++);
    ip[6] = 0x40; ip[7] = 0x00; /* Don't fragment */
    ip[8] = 64;   /* TTL */
    ip[9] = proto;
    write_u32_raw(ip + 12, s.local_ip);
    write_u32_raw(ip + 16, dst_ip_net);
    oyk_u16 ipcs = inet_checksum(ip, 20);
    write_be16(ip + 10, ipcs);

    if (pay_len > 0 && payload) {
        std::memcpy(ip + 20, payload, pay_len);
    }
    return send_eth_frame(slot, eth, 14U + total_len);
}

int TinyTmuxUsbHost::send_udp(int slot, oyk_u32 dst_ip_net, oyk_u16 src_port_host,
                              oyk_u16 dst_port_host, const oyk_u8* payload, size_t pay_len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    if (8U + pay_len > 1480U) return 0;
    oyk_u8 udp_buf[1488];
    write_be16(udp_buf + 0, src_port_host);
    write_be16(udp_buf + 2, dst_port_host);
    oyk_u16 ulen = (oyk_u16)(8U + pay_len);
    write_be16(udp_buf + 4, ulen);
    write_be16(udp_buf + 6, 0U);
    if (pay_len > 0 && payload) {
        std::memcpy(udp_buf + 8, payload, pay_len);
    }
    oyk_u16 cs = udp_checksum(slots_[slot].local_ip, dst_ip_net, udp_buf, ulen);
    write_be16(udp_buf + 6, cs);
    return send_ipv4_packet(slot, dst_ip_net, 17U /* UDP */, udp_buf, ulen);
}

int TinyTmuxUsbHost::send_axudp(int slot, const OykFrame* frame, oyk_u32 dst_ip_net,
                                oyk_u16 src_port_host, oyk_u16 dst_port_host) {
    if (!frame) return 0;
    oyk_u8 raw[320];
    size_t raw_len = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
    if (raw_len == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(raw, raw_len);
    raw[raw_len++] = (oyk_u8)(fcs & 0xFFU);
    raw[raw_len++] = (oyk_u8)((fcs >> 8) & 0xFFU);
    return send_udp(slot, dst_ip_net, src_port_host, dst_port_host, raw, raw_len);
}

int TinyTmuxUsbHost::send_axip(int slot, const OykFrame* frame, oyk_u32 dst_ip_net) {
    if (!frame) return 0;
    oyk_u8 raw[320];
    size_t raw_len = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
    if (raw_len == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(raw, raw_len);
    raw[raw_len++] = (oyk_u8)(fcs & 0xFFU);
    raw[raw_len++] = (oyk_u8)((fcs >> 8) & 0xFFU);
    return send_ipv4_packet(slot, dst_ip_net, 93U /* IPPROTO_AXIP */, raw, raw_len);
}

size_t TinyTmuxUsbHost::send_acm_bytes(int slot, const oyk_u8* data, size_t len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || s.kind != TMUX_USBH_CDC_ACM || !data || len == 0) return 0;

    s.tx_packets++;
    if (acm_tx_hook_) {
        return acm_tx_hook_(slot, data, len, acm_tx_ctx_);
    }

#if defined(OYK_HAS_TINYUSB) && OYK_HAS_TINYUSB && defined(CFG_TUH_ENABLED) && CFG_TUH_ENABLED && defined(CFG_TUH_CDC) && CFG_TUH_CDC
    uint32_t written = tuh_cdc_write(s.idx, data, (uint32_t)len);
    tuh_cdc_write_flush(s.idx);
    return (size_t)written;
#else
    return 0;
#endif
}

void TinyTmuxUsbHost::handle_arp(int slot, const oyk_u8* frame, size_t len) {
    if (len < 42) return;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    oyk_u16 htype = read_be16(frame + 14);
    oyk_u16 ptype = read_be16(frame + 16);
    oyk_u16 op    = read_be16(frame + 20);
    if (htype != 1U || ptype != 0x0800U) return;

    oyk_u32 sender_ip = read_u32_raw(frame + 28);
    oyk_u32 target_ip = read_u32_raw(frame + 38);

    if (sender_ip == s.peer_ip || s.peer_ip == 0) {
        std::memcpy(s.peer_mac, frame + 22, 6);
        s.has_peer_mac = 1;
        if (s.peer_ip == 0) s.peer_ip = sender_ip;
    }

    if (op == 1U /* ARP Request */ && target_ip == s.local_ip) {
        oyk_u8 reply[42];
        std::memcpy(reply, frame + 22, 6);     /* Dst MAC */
        std::memcpy(reply + 6, s.mac, 6);      /* Src MAC */
        reply[12] = 0x08; reply[13] = 0x06;
        reply[14] = 0x00; reply[15] = 0x01;
        reply[16] = 0x08; reply[17] = 0x00;
        reply[18] = 6; reply[19] = 4;
        reply[20] = 0x00; reply[21] = 0x02;    /* ARP Reply */
        std::memcpy(reply + 22, s.mac, 6);
        write_u32_raw(reply + 28, s.local_ip);
        std::memcpy(reply + 32, frame + 22, 6);
        write_u32_raw(reply + 38, sender_ip);
        send_eth_frame(slot, reply, sizeof(reply));
    }
}

void TinyTmuxUsbHost::handle_ipv4(int slot, const oyk_u8* frame, size_t len) {
    if (len < 34) return;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    const oyk_u8* ip = frame + 14;
    size_t ip_avail = len - 14;
    if ((ip[0] >> 4) != 4) return;
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4U;
    if (ihl < 20 || ihl > ip_avail) return;
    size_t total_len = read_be16(ip + 2);
    if (total_len < ihl || total_len > ip_avail) return;

    oyk_u8 proto = ip[9];
    oyk_u32 src_ip = read_u32_raw(ip + 12);
    oyk_u32 dst_ip = read_u32_raw(ip + 16);

    if (src_ip != 0 && (src_ip == s.peer_ip || s.peer_ip == 0)) {
        std::memcpy(s.peer_mac, frame + 6, 6);
        s.has_peer_mac = 1;
        if (s.peer_ip == 0) s.peer_ip = src_ip;
    }

    if (s.local_ip != 0 && dst_ip != s.local_ip && dst_ip != 0xFFFFFFFFUL) {
        return;
    }

    if (proto == 1U /* ICMP */ && total_len >= ihl + 8) {
        const oyk_u8* icmp = ip + ihl;
        size_t icmp_len = total_len - ihl;
        if (icmp[0] == 8U && icmp[1] == 0U && icmp_len <= 1480U) {
            oyk_u8 reply[1480];
            std::memcpy(reply, icmp, icmp_len);
            reply[0] = 0U; /* Echo Reply */
            reply[2] = 0U;
            reply[3] = 0U;
            oyk_u16 cs = inet_checksum(reply, icmp_len);
            write_be16(reply + 2, cs);
            send_ipv4_packet(slot, src_ip, 1U, reply, icmp_len);
        }
        return;
    }

    if (proto == 93U /* AXIP */ && ax25_rx_cb_) {
        const oyk_u8* pay = ip + ihl;
        size_t pay_len = total_len - ihl;
        if (pay_len >= 17 && oyk_verify_fcs(pay, pay_len)) {
            OykFrame f;
            if (oyk_ax25_decode(pay, pay_len - 2, &f)) {
                ax25_rx_cb_(slot, 1, &f, src_ip, 0, ax25_rx_ctx_);
            }
        }
        return;
    }

    if (proto == 17U /* UDP */ && total_len >= ihl + 8) {
        const oyk_u8* udp = ip + ihl;
        oyk_u16 src_port = read_be16(udp + 0);
        oyk_u16 dst_port = read_be16(udp + 2);
        oyk_u16 ulen     = read_be16(udp + 4);
        if (ulen < 8 || ihl + ulen > total_len) return;
        const oyk_u8* pay = udp + 8;
        size_t pay_len = (size_t)(ulen - 8);

        if (udp_rx_cb_) {
            udp_rx_cb_(slot, src_ip, src_port, dst_port, pay, pay_len, udp_rx_ctx_);
        }
        if (ax25_rx_cb_ && pay_len >= 17 && oyk_verify_fcs(pay, pay_len)) {
            OykFrame f;
            if (oyk_ax25_decode(pay, pay_len - 2, &f)) {
                ax25_rx_cb_(slot, 0, &f, src_ip, src_port, ax25_rx_ctx_);
            }
        }
    }
}

void TinyTmuxUsbHost::on_eth_rx(int slot, const oyk_u8* frame, size_t len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    if (!slots_[slot].in_use || !frame || len < 14) return;
    slots_[slot].rx_packets++;

    oyk_u16 ethertype = read_be16(frame + 12);
    if (ethertype == 0x0806U) {
        handle_arp(slot, frame, len);
    } else if (ethertype == 0x0800U) {
        handle_ipv4(slot, frame, len);
    }
}

void TinyTmuxUsbHost::on_ncm_rx(int slot, const oyk_u8* ntb, size_t ntb_len) {
    size_t eth_len = tinytmux_usb_ncm16_unwrap(ntb, ntb_len, scratch_rx_, sizeof(scratch_rx_));
    if (eth_len > 0) {
        on_eth_rx(slot, scratch_rx_, eth_len);
    }
}

void TinyTmuxUsbHost::on_acm_rx(int slot, const oyk_u8* data, size_t len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    if (!slots_[slot].in_use || !data || len == 0) return;
    slots_[slot].rx_packets++;
    if (acm_rx_cb_) {
        acm_rx_cb_(slot, data, len, acm_rx_ctx_);
    }
}

#if defined(OYK_HAS_TINYUSB) && OYK_HAS_TINYUSB && defined(CFG_TUH_ENABLED) && CFG_TUH_ENABLED
#ifdef __cplusplus
extern "C" {
#endif

void tuh_cdc_mount_cb(uint8_t idx) {
    TinyTmuxUsbHost* h = tinytmux_usb_host_active();
    if (!h) return;
    tuh_itf_info_t itf_info;
    std::memset(&itf_info, 0, sizeof(itf_info));
    if (!tuh_cdc_itf_get_info(idx, &itf_info)) return;
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(itf_info.daddr, &vid, &pid);
    int kind = TMUX_USBH_CDC_ACM;
    if (itf_info.desc.bInterfaceSubClass == 0x0DU) kind = TMUX_USBH_CDC_NCM;
    else if (itf_info.desc.bInterfaceSubClass == 0x06U) kind = TMUX_USBH_CDC_ECM;
    h->attach_slot(-1, kind, itf_info.daddr, idx, vid, pid, 0, 0);
}

void tuh_cdc_umount_cb(uint8_t idx) {
    TinyTmuxUsbHost* h = tinytmux_usb_host_active();
    if (!h) return;
    tuh_itf_info_t itf_info;
    std::memset(&itf_info, 0, sizeof(itf_info));
    if (tuh_cdc_itf_get_info(idx, &itf_info)) {
        h->detach_by_dev(itf_info.daddr, -1, idx);
    }
}

void tuh_cdc_rx_cb(uint8_t idx) {
    TinyTmuxUsbHost* h = tinytmux_usb_host_active();
    if (!h) return;
    oyk_u8 buf[256];
    uint32_t count = tuh_cdc_read(idx, buf, sizeof(buf));
    if (count == 0) return;
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        const TinyTmuxUsbRadioSlot* s = h->slot_info(i);
        if (s && s->in_use && s->kind == TMUX_USBH_CDC_ACM && s->idx == idx) {
            h->on_acm_rx(i, buf, (size_t)count);
            break;
        }
    }
}

#ifdef __cplusplus
}
#endif
#endif
