/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "lwip_net_bridge.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(OYK_HAS_LWIP)
#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ppp/ppp.h"
#include "netif/ppp/pppos.h"
#endif

static int parse_ser_index(const char* dev) {
    if (!dev || !dev[0]) return -1;
    if ((dev[0] == 's' || dev[0] == 'S') &&
        (dev[1] == 'e' || dev[1] == 'E') &&
        (dev[2] == 'r' || dev[2] == 'R') &&
        dev[3] >= '0' && dev[3] <= '7' && dev[4] == '\0') {
        return dev[3] - '0';
    }
    return -1;
}

static int strieq_local(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? static_cast<char>(*b - 32) : *b;
        if (ca != cb) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static void bridge_str_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static oyk_u32 bridge_parse_ip(const char* text) {
    oyk_u32 ip = 0;
    if (text && text[0]) oyk_parse_ipv4(text, &ip);
    return ip;
}

TinyTmuxNetBridge::TinyTmuxNetBridge()
    : node_(NULL),
      enet_up_(0), enet_100m_(0), enet_ip_(0), last_poll_ms_(0),
      enet_tx_fn_(NULL), enet_tx_ctx_(NULL),
      enet_udp_tx_fn_(NULL), enet_udp_tx_ctx_(NULL),
      rig_tcp_fn_(NULL), rig_tcp_ctx_(NULL),
      rig_last_poll_ms_(0) {
    memset(bindings_, 0, sizeof(bindings_));
    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        bindings_[i].ppp_slot = -1;
        bindings_[i].logical_uart = -1;
        bindings_[i].usb_slot = -1;
    }
    for (int i = 0; i < TINYTMUX_MAX_PPP_LINKS; ++i) {
        ppp_slots_[i].active = 0;
        ppp_slots_[i].logical_uart = -1;
        ppp_slots_[i].usb_slot = -1;
        ppp_slots_[i].usb_attached = 0;
        ppp_slots_[i].axudp_iface_idx = -1;
        ppp_slots_[i].axip_iface_idx = -1;
        ppp_slots_[i].owner = this;
    }
    oyk_rig_status_init(&rig_status_, OYK_RIG_PROTO_NONE);
}

void TinyTmuxNetBridge::set_ethernet_tx_hook(TinyTmuxEnetTxFn fn, void* ctx) {
    enet_tx_fn_ = fn;
    enet_tx_ctx_ = ctx;
}

void TinyTmuxNetBridge::set_ethernet_udp_tx_hook(TinyTmuxEnetUdpTxFn fn, void* ctx) {
    enet_udp_tx_fn_ = fn;
    enet_udp_tx_ctx_ = ctx;
}

void TinyTmuxNetBridge::set_rig_tcp_hook(TinyTmuxRigTcpExchangeFn fn, void* ctx) {
    rig_tcp_fn_ = fn;
    rig_tcp_ctx_ = ctx;
}

void TinyTmuxNetBridge::set_ethernet_link_state(int link_up, int speed_100mbps, oyk_u32 ip_net) {
    enet_up_ = link_up ? 1 : 0;
    enet_100m_ = (link_up && speed_100mbps) ? 1 : 0;
    enet_ip_ = ip_net;
    tinytmux_board_set_link_led(enet_100m_);

    if (!node_) return;
    const oyk_u8* ip = reinterpret_cast<const oyk_u8*>(&enet_ip_);
    for (int i = 0; i < node_->iface_count(); ++i) {
        if (bindings_[i].kind == TMUX_IF_ENET_AXUDP) {
            char st[36];
            if (enet_up_) {
                sprintf(st, "UP %u.%u.%u.%u:%u",
                        static_cast<unsigned int>(ip[0]), static_cast<unsigned int>(ip[1]),
                        static_cast<unsigned int>(ip[2]), static_cast<unsigned int>(ip[3]),
                        static_cast<unsigned int>(node_->iface(i).cfg.port));
            } else {
                strcpy(st, "LINK DOWN");
            }
            node_->set_iface_status(i, enet_up_, st);
        } else if (bindings_[i].kind == TMUX_IF_ENET_AXIP) {
            char st[36];
            if (enet_up_) {
                sprintf(st, "AXIP %u.%u.%u.%u",
                        static_cast<unsigned int>(ip[0]), static_cast<unsigned int>(ip[1]),
                        static_cast<unsigned int>(ip[2]), static_cast<unsigned int>(ip[3]));
            } else {
                strcpy(st, "LINK DOWN");
            }
            node_->set_iface_status(i, enet_up_, st);
        } else if (bindings_[i].kind == TMUX_IF_ENET_TELNET) {
            node_->set_iface_status(i, enet_up_, enet_up_ ? "TELNET UP" : "LINK DOWN");
        }
    }
}

int TinyTmuxNetBridge::init(OykPortableNode* node, oyk_u32 now_ms) {
    node_ = node;
    last_poll_ms_ = now_ms;
    if (!node_) return 0;

#if defined(OYK_HAS_LWIP)
    lwip_init();
#endif

    usb_host_.init();
    usb_host_.set_acm_rx_callback(usb_acm_rx_trampoline, this);
    usb_host_.set_ax25_rx_callback(usb_ax25_rx_trampoline, this);
    usb_host_.set_udp_rx_callback(usb_udp_rx_trampoline, this);

    return rebind_interfaces(now_ms);
}

int TinyTmuxNetBridge::rebind_interfaces(oyk_u32 now_ms) {
    if (!node_) return 0;
    last_poll_ms_ = now_ms;

    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        bindings_[i].kind = TMUX_IF_NONE;
        bindings_[i].ppp_slot = -1;
        bindings_[i].logical_uart = -1;
        bindings_[i].usb_slot = -1;
        oyk_kiss_decoder_init(&bindings_[i].kiss_dec);
    }
    for (int s = 0; s < TINYTMUX_MAX_PPP_LINKS; ++s) {
        ppp_slots_[s].active = 0;
        ppp_slots_[s].logical_uart = -1;
        ppp_slots_[s].usb_slot = -1;
        ppp_slots_[s].usb_attached = 0;
        ppp_slots_[s].axudp_iface_idx = -1;
        ppp_slots_[s].axip_iface_idx = -1;
        ppp_slots_[s].owner = this;
    }

    int next_ppp = 0;
    for (int i = 0; i < node_->iface_count(); ++i) {
        const OykIfaceConfig& ic = node_->iface(i).cfg;
        int ser_idx = parse_ser_index(ic.device);
        int usb_idx = TinyTmuxUsbHost::parse_device_slot(ic.device);

        if (strieq_local(ic.type, "usb-cdc-net")) {
            int target_slot = (usb_idx >= 0) ? usb_idx : 0;
            bindings_[i].kind = TMUX_IF_USB_NET_AXUDP;
            bindings_[i].usb_slot = target_slot;
            oyk_u32 loc_ip = bridge_parse_ip(ic.host);
            oyk_u32 peer_ip = (ic.peer_count > 0) ? ic.peers[0].ip : 0;
            usb_host_.configure_slot_ips(target_slot, loc_ip, peer_ip);
            const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(target_slot);
            int up = (us && us->in_use &&
                      (us->kind == TMUX_USBH_CDC_ECM || us->kind == TMUX_USBH_CDC_NCM)) ? 1 : 0;
            node_->set_iface_status(i, up, up ? "USB NET UP" : "USB NET WAIT");
        } else if (strieq_local(ic.type, "usb-cdc-serial")) {
            int target_slot = (usb_idx >= 0) ? usb_idx : 0;
            if ((ic.port > 0 || ic.peer_count > 0) && next_ppp < TINYTMUX_MAX_PPP_LINKS) {
                bindings_[i].kind = TMUX_IF_PPP_AXUDP;
                bindings_[i].ppp_slot = next_ppp;
                bindings_[i].usb_slot = target_slot;
                oyk_u32 loc_ip = bridge_parse_ip(ic.host);
                oyk_u32 peer_ip = (ic.peer_count > 0) ? ic.peers[0].ip : 0;
                configure_usb_ppp_slot(next_ppp, target_slot, i, i, loc_ip, peer_ip,
                                       ic.port ? ic.port : static_cast<oyk_u16>(10093),
                                       now_ms);
                ++next_ppp;
            } else {
                bindings_[i].kind = TMUX_IF_USB_ACM_KISS;
                bindings_[i].usb_slot = target_slot;
                const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(target_slot);
                int up = (us && us->in_use && us->kind == TMUX_USBH_CDC_ACM) ? 1 : 0;
                node_->set_iface_status(i, up, up ? "USB KISS UP" : "USB SER WAIT");
            }
        } else if (strieq_local(ic.type, "axudp")) {
            if (usb_idx >= 0) {
                bindings_[i].kind = TMUX_IF_USB_NET_AXUDP;
                bindings_[i].usb_slot = usb_idx;
                oyk_u32 loc_ip = bridge_parse_ip(ic.host);
                oyk_u32 peer_ip = (ic.peer_count > 0) ? ic.peers[0].ip : 0;
                usb_host_.configure_slot_ips(usb_idx, loc_ip, peer_ip);
                const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(usb_idx);
                int up = (us && us->in_use &&
                          (us->kind == TMUX_USBH_CDC_ECM || us->kind == TMUX_USBH_CDC_NCM)) ? 1 : 0;
                node_->set_iface_status(i, up, up ? "USB AXUDP UP" : "USB NET WAIT");
            } else if (ser_idx >= 0 && next_ppp < TINYTMUX_MAX_PPP_LINKS) {
                bindings_[i].kind = TMUX_IF_PPP_AXUDP;
                bindings_[i].ppp_slot = next_ppp;
                bindings_[i].logical_uart = ser_idx;
                oyk_u32 loc_ip = bridge_parse_ip(ic.host);
                oyk_u32 peer_ip = (ic.peer_count > 0) ? ic.peers[0].ip : 0;
                configure_ppp_slot(next_ppp, ser_idx, i, -1, loc_ip, peer_ip,
                                   ic.port ? ic.port : static_cast<oyk_u16>(10093),
                                   NULL, NULL, now_ms);
                ++next_ppp;
            } else {
                bindings_[i].kind = TMUX_IF_ENET_AXUDP;
                node_->set_iface_status(i, enet_up_, enet_up_ ? "ENET AXUDP UP" : "ENET INIT");
            }
        } else if (strieq_local(ic.type, "axip")) {
            if (usb_idx >= 0) {
                bindings_[i].kind = TMUX_IF_USB_NET_AXIP;
                bindings_[i].usb_slot = usb_idx;
                const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(usb_idx);
                int up = (us && us->in_use &&
                          (us->kind == TMUX_USBH_CDC_ECM || us->kind == TMUX_USBH_CDC_NCM)) ? 1 : 0;
                node_->set_iface_status(i, up, up ? "USB AXIP UP" : "USB NET WAIT");
            } else if (ser_idx >= 0) {
                int shared = -1;
                for (int s = 0; s < TINYTMUX_MAX_PPP_LINKS; ++s) {
                    if (ppp_slots_[s].active && ppp_slots_[s].logical_uart == ser_idx) {
                        shared = s;
                        break;
                    }
                }
                if (shared >= 0) {
                    bindings_[i].kind = TMUX_IF_PPP_AXIP;
                    bindings_[i].ppp_slot = shared;
                    bindings_[i].logical_uart = ser_idx;
                    ppp_slots_[shared].axip_iface_idx = i;
                } else if (next_ppp < TINYTMUX_MAX_PPP_LINKS) {
                    bindings_[i].kind = TMUX_IF_PPP_AXIP;
                    bindings_[i].ppp_slot = next_ppp;
                    bindings_[i].logical_uart = ser_idx;
                    oyk_u32 loc_ip = bridge_parse_ip(ic.host);
                    oyk_u32 peer_ip = (ic.peer_count > 0) ? ic.peers[0].ip : 0;
                    configure_ppp_slot(next_ppp, ser_idx, -1, i, loc_ip, peer_ip, 10093,
                                       NULL, NULL, now_ms);
                    ++next_ppp;
                }
            } else {
                bindings_[i].kind = TMUX_IF_ENET_AXIP;
                node_->set_iface_status(i, enet_up_, enet_up_ ? "ENET AXIP UP" : "ENET INIT");
            }
        } else if (strieq_local(ic.type, "ppp") && next_ppp < TINYTMUX_MAX_PPP_LINKS) {
            bindings_[i].kind = TMUX_IF_PPP_AXUDP;
            bindings_[i].ppp_slot = next_ppp;
            oyk_u32 loc_ip = bridge_parse_ip(ic.host);
            oyk_u32 peer_ip = (ic.peer_count > 0) ? ic.peers[0].ip : 0;
            oyk_u16 port = ic.port ? ic.port : static_cast<oyk_u16>(10093);
            if (usb_idx >= 0) {
                bindings_[i].usb_slot = usb_idx;
                configure_usb_ppp_slot(next_ppp, usb_idx, i, i, loc_ip, peer_ip, port, now_ms);
                ++next_ppp;
            } else if (ser_idx >= 0) {
                bindings_[i].logical_uart = ser_idx;
                configure_ppp_slot(next_ppp, ser_idx, i, i, loc_ip, peer_ip, port,
                                   NULL, NULL, now_ms);
                ++next_ppp;
            }
        } else if (strieq_local(ic.type, "kiss-serial") && ser_idx >= 0) {
            bindings_[i].kind = TMUX_IF_KISS_SERIAL;
            bindings_[i].logical_uart = ser_idx;
            TinyTmuxUartConfig ucfg;
            ucfg.baudrate = ic.baud ? ic.baud : 9600UL;
            ucfg.cts_rts = 1;
            ucfg.role = TMUX_ROLE_KISS_TNC;
            int ok = tinytmux_uart_init(ser_idx, &ucfg);
            char st[48];
            sprintf(st, "KISS %.16s %lu", ic.device, static_cast<unsigned long>(ucfg.baudrate));
            node_->set_iface_status(i, ok, st);
        } else if (strieq_local(ic.type, "telnet")) {
            bindings_[i].kind = TMUX_IF_ENET_TELNET;
            node_->set_iface_status(i, enet_up_, enet_up_ ? "TELNET UP" : "ENET INIT");
        }
    }

    sync_rig_config();
    return 1;
}

size_t TinyTmuxNetBridge::uart_serial_tx_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    PppSlot* ps = static_cast<PppSlot*>(ctx);
    if (!ps || ps->logical_uart < 0) return 0;
    return tinytmux_uart_write(ps->logical_uart, data, len);
}

size_t TinyTmuxNetBridge::usb_acm_tx_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    PppSlot* ps = static_cast<PppSlot*>(ctx);
    if (!ps || !ps->owner || ps->usb_slot < 0) return 0;
    return ps->owner->usb_host_.send_acm_bytes(ps->usb_slot, data, len);
}

void TinyTmuxNetBridge::ppp_ax25_rx_trampoline(int is_axip,
                                                const OykFrame* frame,
                                                oyk_u32 src_ip_net,
                                                oyk_u16 src_port_host,
                                                void* ctx) {
    PppSlot* ps = static_cast<PppSlot*>(ctx);
    if (!ps || !ps->owner || !ps->owner->node_ || !frame) return;
    int if_idx = is_axip ? ps->axip_iface_idx : ps->axudp_iface_idx;
    if (if_idx < 0) if_idx = (ps->axudp_iface_idx >= 0) ? ps->axudp_iface_idx : ps->axip_iface_idx;
    if (if_idx >= 0) {
        ps->owner->node_->on_rx_frame(if_idx, frame, src_ip_net, src_port_host,
                                      static_cast<oyk_u16>(is_axip ? 1U : 0U));
    }
}

void TinyTmuxNetBridge::ppp_udp_rx_trampoline(oyk_u32 src_ip_net,
                                               oyk_u16 src_port_host,
                                               oyk_u16 dst_port_host,
                                               const oyk_u8* payload,
                                               size_t len,
                                               void* ctx) {
    (void)src_ip_net;
    PppSlot* ps = static_cast<PppSlot*>(ctx);
    if (!ps || !ps->owner || !payload || len == 0) return;
    oyk_u16 rport = OYK_RF_DEFAULT_PORT;
    if (ps->owner->node_ && ps->owner->node_->config().rig_port > 0) {
        rport = ps->owner->node_->config().rig_port;
    }
    if (src_port_host == rport || dst_port_host == rport ||
        src_port_host == OYK_RF_DEFAULT_PORT || dst_port_host == OYK_RF_DEFAULT_PORT) {
        ps->owner->handle_rf_udp_payload(payload, len);
    }
}

void TinyTmuxNetBridge::usb_acm_rx_trampoline(int slot, const oyk_u8* data, size_t len, void* ctx) {
    TinyTmuxNetBridge* self = static_cast<TinyTmuxNetBridge*>(ctx);
    if (!self || !data || len == 0) return;

    for (int s = 0; s < TINYTMUX_MAX_PPP_LINKS; ++s) {
        if (self->ppp_slots_[s].active && self->ppp_slots_[s].usb_slot == slot) {
            self->feed_ppp_serial(s, data, len, self->last_poll_ms_);
            return;
        }
    }

    if (self->node_) {
        for (int i = 0; i < self->node_->iface_count(); ++i) {
            if (self->bindings_[i].kind == TMUX_IF_USB_ACM_KISS &&
                self->bindings_[i].usb_slot == slot) {
                for (size_t k = 0; k < len; ++k) {
                    oyk_u8 kport = 0, kcmd = 0;
                    if (oyk_kiss_decoder_feed_byte(&self->bindings_[i].kiss_dec, data[k], &kport, &kcmd)) {
                        if (kcmd == 0 && kport == self->node_->iface(i).cfg.kiss_port) {
                            OykFrame f;
                            if (oyk_ax25_decode(self->bindings_[i].kiss_dec.buf,
                                                self->bindings_[i].kiss_dec.len, &f)) {
                                self->node_->on_rx_frame(i, &f, 0, 0, 0);
                            }
                        }
                    }
                }
                break;
            }
        }
    }
}

void TinyTmuxNetBridge::usb_ax25_rx_trampoline(int slot, int is_axip, const OykFrame* frame,
                                                oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx) {
    TinyTmuxNetBridge* self = static_cast<TinyTmuxNetBridge*>(ctx);
    if (!self || !self->node_ || !frame) return;

    int fallback = -1;
    for (int i = 0; i < self->node_->iface_count(); ++i) {
        TinyTmuxIfTransport kind = self->bindings_[i].kind;
        if ((kind == TMUX_IF_USB_NET_AXUDP || kind == TMUX_IF_USB_NET_AXIP) &&
            self->bindings_[i].usb_slot == slot) {
            if (fallback < 0) fallback = i;
            if ((is_axip && kind == TMUX_IF_USB_NET_AXIP) ||
                (!is_axip && kind == TMUX_IF_USB_NET_AXUDP)) {
                self->node_->on_rx_frame(i, frame, src_ip_net, src_port_host,
                                         static_cast<oyk_u16>(is_axip ? 1U : 0U));
                return;
            }
        }
    }
    if (fallback >= 0) {
        self->node_->on_rx_frame(fallback, frame, src_ip_net, src_port_host,
                                 static_cast<oyk_u16>(is_axip ? 1U : 0U));
    }
}

void TinyTmuxNetBridge::usb_udp_rx_trampoline(int slot, oyk_u32 src_ip_net, oyk_u16 src_port_host,
                                               oyk_u16 dst_port_host, const oyk_u8* payload,
                                               size_t len, void* ctx) {
    (void)slot;
    (void)src_ip_net;
    TinyTmuxNetBridge* self = static_cast<TinyTmuxNetBridge*>(ctx);
    if (!self || !payload || len == 0) return;
    oyk_u16 rport = OYK_RF_DEFAULT_PORT;
    if (self->node_ && self->node_->config().rig_port > 0) {
        rport = self->node_->config().rig_port;
    }
    if (src_port_host == rport || dst_port_host == rport ||
        src_port_host == OYK_RF_DEFAULT_PORT || dst_port_host == OYK_RF_DEFAULT_PORT) {
        self->handle_rf_udp_payload(payload, len);
    }
}

int TinyTmuxNetBridge::configure_ppp_slot(int ppp_slot,
                                          int logical_uart,
                                          int axudp_iface_idx,
                                          int axip_iface_idx,
                                          oyk_u32 local_ip_net,
                                          oyk_u32 peer_ip_net,
                                          oyk_u16 axudp_port,
                                          OykPppSerialTxFn custom_serial_tx,
                                          void* custom_serial_ctx,
                                          oyk_u32 now_ms) {
    if (ppp_slot < 0 || ppp_slot >= TINYTMUX_MAX_PPP_LINKS) return 0;
    PppSlot& ps = ppp_slots_[ppp_slot];
    ps.active = 1;
    ps.logical_uart = logical_uart;
    ps.usb_slot = -1;
    ps.axudp_iface_idx = axudp_iface_idx;
    ps.axip_iface_idx = axip_iface_idx;
    ps.owner = this;

    if (logical_uart >= 0 && !custom_serial_tx) {
        TinyTmuxUartConfig ucfg;
        ucfg.baudrate = 115200UL;
        if (node_ && axudp_iface_idx >= 0 && node_->iface(axudp_iface_idx).cfg.baud > 0) {
            ucfg.baudrate = node_->iface(axudp_iface_idx).cfg.baud;
        }
        ucfg.cts_rts = 1;
        ucfg.role = TMUX_ROLE_PPP_RADIO;
        tinytmux_uart_init(logical_uart, &ucfg);
    }

    if (axudp_iface_idx >= 0 && axudp_iface_idx < OYK_MAX_INTERFACES) {
        bindings_[axudp_iface_idx].kind = TMUX_IF_PPP_AXUDP;
        bindings_[axudp_iface_idx].ppp_slot = ppp_slot;
        bindings_[axudp_iface_idx].logical_uart = logical_uart;
        bindings_[axudp_iface_idx].usb_slot = -1;
    }
    if (axip_iface_idx >= 0 && axip_iface_idx < OYK_MAX_INTERFACES && axip_iface_idx != axudp_iface_idx) {
        bindings_[axip_iface_idx].kind = TMUX_IF_PPP_AXIP;
        bindings_[axip_iface_idx].ppp_slot = ppp_slot;
        bindings_[axip_iface_idx].logical_uart = logical_uart;
        bindings_[axip_iface_idx].usb_slot = -1;
    }

    OykPppSerialTxFn tx_cb = custom_serial_tx ? custom_serial_tx : uart_serial_tx_trampoline;
    void* tx_ctx = custom_serial_tx ? custom_serial_ctx : &ps;
    ps.link.init(local_ip_net, peer_ip_net, axudp_port,
                 0x4F594B10UL + static_cast<oyk_u32>(ppp_slot),
                 tx_cb, tx_ctx);
    ps.link.set_ax25_rx_callback(ppp_ax25_rx_trampoline, &ps);
    ps.link.set_udp_rx_callback(ppp_udp_rx_trampoline, &ps);
    ps.link.open(now_ms);

    if (node_) {
        char st[36];
        ps.link.format_status(st, sizeof(st));
        if (axudp_iface_idx >= 0) node_->set_iface_status(axudp_iface_idx, ps.link.is_up(), st);
        if (axip_iface_idx >= 0 && axip_iface_idx != axudp_iface_idx) {
            node_->set_iface_status(axip_iface_idx, ps.link.is_up(), st);
        }
    }
    return 1;
}

int TinyTmuxNetBridge::configure_usb_ppp_slot(int ppp_slot,
                                              int usb_slot,
                                              int axudp_iface_idx,
                                              int axip_iface_idx,
                                              oyk_u32 local_ip_net,
                                              oyk_u32 peer_ip_net,
                                              oyk_u16 axudp_port,
                                              oyk_u32 now_ms) {
    if (ppp_slot < 0 || ppp_slot >= TINYTMUX_MAX_PPP_LINKS) return 0;
    if (usb_slot < 0 || usb_slot >= TINYTMUX_MAX_USB_RADIOS) return 0;

    PppSlot& ps = ppp_slots_[ppp_slot];
    ps.active = 1;
    ps.logical_uart = -1;
    ps.usb_slot = usb_slot;
    const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(usb_slot);
    ps.usb_attached = (us && us->in_use && us->kind == TMUX_USBH_CDC_ACM) ? 1 : 0;
    ps.axudp_iface_idx = axudp_iface_idx;
    ps.axip_iface_idx = axip_iface_idx;
    ps.owner = this;

    if (axudp_iface_idx >= 0 && axudp_iface_idx < OYK_MAX_INTERFACES) {
        bindings_[axudp_iface_idx].kind = TMUX_IF_PPP_AXUDP;
        bindings_[axudp_iface_idx].ppp_slot = ppp_slot;
        bindings_[axudp_iface_idx].logical_uart = -1;
        bindings_[axudp_iface_idx].usb_slot = usb_slot;
    }
    if (axip_iface_idx >= 0 && axip_iface_idx < OYK_MAX_INTERFACES && axip_iface_idx != axudp_iface_idx) {
        bindings_[axip_iface_idx].kind = TMUX_IF_PPP_AXIP;
        bindings_[axip_iface_idx].ppp_slot = ppp_slot;
        bindings_[axip_iface_idx].logical_uart = -1;
        bindings_[axip_iface_idx].usb_slot = usb_slot;
    }

    ps.link.init(local_ip_net, peer_ip_net, axudp_port,
                 0x4F594B30UL + static_cast<oyk_u32>(ppp_slot),
                 usb_acm_tx_trampoline, &ps);
    ps.link.set_ax25_rx_callback(ppp_ax25_rx_trampoline, &ps);
    ps.link.set_udp_rx_callback(ppp_udp_rx_trampoline, &ps);
    if (ps.usb_attached) {
        ps.link.open(now_ms);
    }

    if (node_) {
        char st[36];
        ps.link.format_status(st, sizeof(st));
        if (axudp_iface_idx >= 0) node_->set_iface_status(axudp_iface_idx, ps.link.is_up(), st);
        if (axip_iface_idx >= 0 && axip_iface_idx != axudp_iface_idx) {
            node_->set_iface_status(axip_iface_idx, ps.link.is_up(), st);
        }
    }
    return 1;
}

void TinyTmuxNetBridge::feed_ppp_serial(int ppp_slot, const oyk_u8* data, size_t len, oyk_u32 now_ms) {
    if (ppp_slot < 0 || ppp_slot >= TINYTMUX_MAX_PPP_LINKS) return;
    PppSlot& ps = ppp_slots_[ppp_slot];
    if (!ps.active) return;

    int prev_up = ps.link.is_up();
    ps.link.feed_serial_bytes(data, len, now_ms);
    if (node_ && ps.link.is_up() != prev_up) {
        char st[36];
        ps.link.format_status(st, sizeof(st));
        if (ps.axudp_iface_idx >= 0) node_->set_iface_status(ps.axudp_iface_idx, ps.link.is_up(), st);
        if (ps.axip_iface_idx >= 0 && ps.axip_iface_idx != ps.axudp_iface_idx) {
            node_->set_iface_status(ps.axip_iface_idx, ps.link.is_up(), st);
        }
    }
}

void TinyTmuxNetBridge::on_ethernet_rx(int is_axip,
                                       oyk_u32 src_ip_net,
                                       oyk_u16 src_port_host,
                                       const oyk_u8* payload_with_fcs,
                                       size_t len) {
    if (!node_ || !payload_with_fcs || len < 17) return;
    if (!oyk_verify_fcs(payload_with_fcs, len)) return;
    OykFrame f;
    if (!oyk_ax25_decode(payload_with_fcs, len - 2, &f)) return;

    TinyTmuxIfTransport want = is_axip ? TMUX_IF_ENET_AXIP : TMUX_IF_ENET_AXUDP;
    for (int i = 0; i < node_->iface_count(); ++i) {
        if (bindings_[i].kind == want) {
            node_->on_rx_frame(i, &f, src_ip_net, src_port_host, 0);
            break;
        }
    }
}

void TinyTmuxNetBridge::on_ethernet_udp_rx(oyk_u32 src_ip_net,
                                           oyk_u16 src_port_host,
                                           oyk_u16 dst_port_host,
                                           const oyk_u8* payload,
                                           size_t len) {
    (void)src_ip_net;
    if (!payload || len == 0) return;
    oyk_u16 rport = OYK_RF_DEFAULT_PORT;
    if (node_ && node_->config().rig_port > 0) {
        rport = node_->config().rig_port;
    }
    if (src_port_host == rport || dst_port_host == rport ||
        src_port_host == OYK_RF_DEFAULT_PORT || dst_port_host == OYK_RF_DEFAULT_PORT) {
        handle_rf_udp_payload(payload, len);
    }
}

void TinyTmuxNetBridge::poll(oyk_u32 now_ms) {
    last_poll_ms_ = now_ms;
#if defined(OYK_HAS_LWIP)
    sys_check_timeouts();
#endif
    usb_host_.poll();

    oyk_u8 rx_tmp[128];
    for (int s = 0; s < TINYTMUX_MAX_PPP_LINKS; ++s) {
        if (!ppp_slots_[s].active) continue;
        if (ppp_slots_[s].usb_slot >= 0) {
            const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(ppp_slots_[s].usb_slot);
            int attached = (us && us->in_use && us->kind == TMUX_USBH_CDC_ACM) ? 1 : 0;
            if (attached && !ppp_slots_[s].usb_attached) {
                ppp_slots_[s].usb_attached = 1;
                ppp_slots_[s].link.open(now_ms);
            } else if (!attached && ppp_slots_[s].usb_attached) {
                ppp_slots_[s].usb_attached = 0;
                ppp_slots_[s].link.close();
            }
        }
        if (ppp_slots_[s].logical_uart >= 0) {
            size_t n = tinytmux_uart_read(ppp_slots_[s].logical_uart, rx_tmp, sizeof(rx_tmp));
            if (n > 0) {
                feed_ppp_serial(s, rx_tmp, n, now_ms);
            }
        }
        ppp_slots_[s].link.tick(now_ms);
    }

    if (node_) {
        for (int i = 0; i < node_->iface_count(); ++i) {
            TinyTmuxIfTransport kind = bindings_[i].kind;
            if (kind == TMUX_IF_KISS_SERIAL && bindings_[i].logical_uart >= 0) {
                size_t n = tinytmux_uart_read(bindings_[i].logical_uart, rx_tmp, sizeof(rx_tmp));
                for (size_t k = 0; k < n; ++k) {
                    oyk_u8 kport = 0, kcmd = 0;
                    if (oyk_kiss_decoder_feed_byte(&bindings_[i].kiss_dec, rx_tmp[k], &kport, &kcmd)) {
                        if (kcmd == 0 && kport == node_->iface(i).cfg.kiss_port) {
                            OykFrame f;
                            if (oyk_ax25_decode(bindings_[i].kiss_dec.buf, bindings_[i].kiss_dec.len, &f)) {
                                node_->on_rx_frame(i, &f, 0, 0, 0);
                            }
                        }
                    }
                }
            } else if ((kind == TMUX_IF_USB_NET_AXUDP || kind == TMUX_IF_USB_NET_AXIP) &&
                       bindings_[i].usb_slot >= 0) {
                const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(bindings_[i].usb_slot);
                int up = (us && us->in_use &&
                          (us->kind == TMUX_USBH_CDC_ECM || us->kind == TMUX_USBH_CDC_NCM)) ? 1 : 0;
                if (up != node_->iface(i).running) {
                    node_->set_iface_status(i, up, up ? "USB NET UP" : "USB NET WAIT");
                }
            } else if (kind == TMUX_IF_USB_ACM_KISS && bindings_[i].usb_slot >= 0) {
                const TinyTmuxUsbRadioSlot* us = usb_host_.slot_info(bindings_[i].usb_slot);
                int up = (us && us->in_use && us->kind == TMUX_USBH_CDC_ACM) ? 1 : 0;
                if (up != node_->iface(i).running) {
                    node_->set_iface_status(i, up, up ? "USB KISS UP" : "USB SER WAIT");
                }
            }
        }

        if (node_->config().rig_enabled && node_->config().rig_poll_sec > 0) {
            oyk_u32 interval_ms = static_cast<oyk_u32>(node_->config().rig_poll_sec) * 1000UL;
            if (rig_last_poll_ms_ == 0 || (now_ms - rig_last_poll_ms_) >= interval_ms) {
                rig_poll_now(now_ms);
            }
        }
    }
}

void TinyTmuxNetBridge::sync_rig_config() {
    int proto = OYK_RIG_PROTO_NONE;
    if (node_ && node_->config().rig_enabled) {
        proto = oyk_rig_parse_protocol(node_->config().rig_protocol);
    }
    if (proto != rig_status_.protocol) {
        oyk_rig_status_init(&rig_status_, proto);
    } else {
        rig_status_.protocol = proto;
    }
    rig_last_poll_ms_ = 0;
}

void TinyTmuxNetBridge::handle_rf_udp_payload(const oyk_u8* payload, size_t len) {
    if (!payload || len == 0) return;
    char text[256];
    size_t copy_n = (len < sizeof(text) - 1) ? len : (sizeof(text) - 1);
    memcpy(text, payload, copy_n);
    text[copy_n] = '\0';

    if (oyk_rf_parse_status(text, &rig_status_)) {
        rig_status_.reachable = 1;
        rig_status_.last_error[0] = '\0';
        if (node_ && node_->config().rig_notify) {
            rig_status_.notifications_active = 1;
        }
    }
}

int TinyTmuxNetBridge::send_rf_udp_datagram(const char* payload, size_t len) {
    if (!node_ || !payload || len == 0) return 0;
    oyk_u32 dst_ip = bridge_parse_ip(node_->config().rig_host);
    oyk_u16 dst_port = node_->config().rig_port ? node_->config().rig_port : OYK_RF_DEFAULT_PORT;

    /* Prefer matching USB Host CDC-ECM/NCM net slot */
    int usb_slot = -1;
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        const TinyTmuxUsbRadioSlot* s = usb_host_.slot_info(i);
        if (s && s->in_use && (s->kind == TMUX_USBH_CDC_ECM || s->kind == TMUX_USBH_CDC_NCM)) {
            if (dst_ip != 0 && s->peer_ip == dst_ip) {
                usb_slot = i;
                break;
            }
            if (usb_slot < 0) usb_slot = i;
        }
    }
    if (usb_slot >= 0) {
        const TinyTmuxUsbRadioSlot* s = usb_host_.slot_info(usb_slot);
        oyk_u32 target = (dst_ip != 0) ? dst_ip : s->peer_ip;
        if (target != 0 &&
            usb_host_.send_udp(usb_slot, target, dst_port, dst_port,
                               reinterpret_cast<const oyk_u8*>(payload), len)) {
            return 1;
        }
    }

    /* Next check active PPP links (USB CDC-ACM or UART ser0..ser7) */
    int ppp_match = -1;
    for (int s = 0; s < TINYTMUX_MAX_PPP_LINKS; ++s) {
        if (!ppp_slots_[s].active || !ppp_slots_[s].link.is_up()) continue;
        if (dst_ip != 0 && ppp_slots_[s].link.remote_ip() == dst_ip) {
            ppp_match = s;
            break;
        }
        if (ppp_match < 0) ppp_match = s;
    }
    if (ppp_match >= 0) {
        oyk_u32 target = (dst_ip != 0) ? dst_ip : ppp_slots_[ppp_match].link.remote_ip();
        if (ppp_slots_[ppp_match].link.send_udp(target, dst_port, dst_port,
                                                reinterpret_cast<const oyk_u8*>(payload), len)) {
            return 1;
        }
    }

    /* Fall back to Ethernet UDP hook when present */
    if (enet_udp_tx_fn_ && (enet_up_ || dst_ip != 0)) {
        if (enet_udp_tx_fn_(dst_ip, dst_port, dst_port,
                            reinterpret_cast<const oyk_u8*>(payload), len, enet_udp_tx_ctx_)) {
            return 1;
        }
    }

    bridge_str_copy(rig_status_.last_error, sizeof(rig_status_.last_error),
                    "No active RadioFoundry transport");
    return 0;
}

int TinyTmuxNetBridge::rigctl_exchange(const char* cmd, size_t cmd_len,
                                       char* reply_out, size_t reply_cap) {
    if (!node_ || !cmd || cmd_len == 0) return 0;
    oyk_u32 host_ip = bridge_parse_ip(node_->config().rig_host);
    oyk_u16 port = node_->config().rig_port ? node_->config().rig_port : OYK_RIGCTL_DEFAULT_PORT;
    if (rig_tcp_fn_) {
        if (rig_tcp_fn_(host_ip, port, cmd, cmd_len, reply_out, reply_cap, rig_tcp_ctx_)) {
            rig_status_.reachable = 1;
            rig_status_.last_error[0] = '\0';
            return 1;
        }
    }
    rig_status_.reachable = 0;
    bridge_str_copy(rig_status_.last_error, sizeof(rig_status_.last_error),
                    rig_tcp_fn_ ? "rigctld exchange failed" : "No rigctld TCP driver bound");
    return 0;
}

int TinyTmuxNetBridge::rig_poll_now(oyk_u32 now_ms) {
    rig_last_poll_ms_ = now_ms;
    if (!node_ || !node_->config().rig_enabled) {
        bridge_str_copy(rig_status_.last_error, sizeof(rig_status_.last_error), "Rig control disabled");
        return 0;
    }
    int proto = oyk_rig_parse_protocol(node_->config().rig_protocol);
    rig_status_.protocol = proto;

    if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
        char cmd[32];
        size_t clen = oyk_rf_build_select(-1, -1, cmd, sizeof(cmd));
        return send_rf_udp_datagram(cmd, clen);
    }

    if (proto == OYK_RIG_PROTO_RIGCTL) {
        char cmd[32], reply[128];
        int ok = 0;
        size_t clen = oyk_rigctl_build_get_freq(cmd, sizeof(cmd));
        if (rigctl_exchange(cmd, clen, reply, sizeof(reply))) {
            oyk_u32 hz = 0;
            int rprt = 0;
            if (oyk_rigctl_parse_freq_reply(reply, &hz, &rprt)) {
                rig_status_.freq_hz = hz;
                ok = 1;
            }
        }
        clen = oyk_rigctl_build_get_mode(cmd, sizeof(cmd));
        if (rigctl_exchange(cmd, clen, reply, sizeof(reply))) {
            char mode[16];
            oyk_u32 pb = 0;
            int rprt = 0;
            if (oyk_rigctl_parse_mode_reply(reply, mode, sizeof(mode), &pb, &rprt)) {
                bridge_str_copy(rig_status_.mode, sizeof(rig_status_.mode), mode);
                rig_status_.passband_hz = pb;
                ok = 1;
            }
        }
        clen = oyk_rigctl_build_get_ptt(cmd, sizeof(cmd));
        if (rigctl_exchange(cmd, clen, reply, sizeof(reply))) {
            int ptt = -1, rprt = 0;
            if (oyk_rigctl_parse_ptt_reply(reply, &ptt, &rprt)) {
                rig_status_.ptt = ptt;
                ok = 1;
            }
        }
        clen = oyk_rigctl_build_get_vfo(cmd, sizeof(cmd));
        if (rigctl_exchange(cmd, clen, reply, sizeof(reply))) {
            char vfo[16];
            int rprt = 0;
            if (oyk_rigctl_parse_vfo_reply(reply, vfo, sizeof(vfo), &rprt)) {
                bridge_str_copy(rig_status_.vfo, sizeof(rig_status_.vfo), vfo);
                ok = 1;
            }
        }
        return ok;
    }

    bridge_str_copy(rig_status_.last_error, sizeof(rig_status_.last_error), "Unsupported rig protocol");
    return 0;
}

int TinyTmuxNetBridge::rig_set_freq(oyk_u32 hz) {
    char cmd[32], reply[64];
    size_t clen = oyk_rigctl_build_set_freq(hz, cmd, sizeof(cmd));
    if (clen == 0 || !rigctl_exchange(cmd, clen, reply, sizeof(reply))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(reply, &rprt) && rprt != 0) {
        sprintf(rig_status_.last_error, "rigctld error %d", rprt);
        return 0;
    }
    rig_status_.freq_hz = hz;
    return 1;
}

int TinyTmuxNetBridge::rig_set_mode(const char* mode, oyk_u32 passband_hz) {
    char cmd[48], reply[64];
    size_t clen = oyk_rigctl_build_set_mode(mode, passband_hz, cmd, sizeof(cmd));
    if (clen == 0 || !rigctl_exchange(cmd, clen, reply, sizeof(reply))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(reply, &rprt) && rprt != 0) {
        sprintf(rig_status_.last_error, "rigctld error %d", rprt);
        return 0;
    }
    bridge_str_copy(rig_status_.mode, sizeof(rig_status_.mode), mode);
    rig_status_.passband_hz = passband_hz;
    return 1;
}

int TinyTmuxNetBridge::rig_set_ptt(int tx) {
    char cmd[16], reply[64];
    size_t clen = oyk_rigctl_build_set_ptt(tx, cmd, sizeof(cmd));
    if (clen == 0 || !rigctl_exchange(cmd, clen, reply, sizeof(reply))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(reply, &rprt) && rprt != 0) {
        sprintf(rig_status_.last_error, "rigctld error %d", rprt);
        return 0;
    }
    rig_status_.ptt = tx ? 1 : 0;
    return 1;
}

int TinyTmuxNetBridge::rig_set_vfo(const char* vfo) {
    char cmd[32], reply[64];
    size_t clen = oyk_rigctl_build_set_vfo(vfo, cmd, sizeof(cmd));
    if (clen == 0 || !rigctl_exchange(cmd, clen, reply, sizeof(reply))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(reply, &rprt) && rprt != 0) {
        sprintf(rig_status_.last_error, "rigctld error %d", rprt);
        return 0;
    }
    bridge_str_copy(rig_status_.vfo, sizeof(rig_status_.vfo), vfo);
    return 1;
}

int TinyTmuxNetBridge::rig_rf_select(int zone, int pos) {
    char cmd[40];
    size_t clen = oyk_rf_build_select(zone, pos, cmd, sizeof(cmd));
    return (clen > 0) ? send_rf_udp_datagram(cmd, clen) : 0;
}

int TinyTmuxNetBridge::rig_rf_step_channel(int delta) {
    char cmd[40];
    size_t clen = oyk_rf_build_step_channel(delta, cmd, sizeof(cmd));
    return (clen > 0) ? send_rf_udp_datagram(cmd, clen) : 0;
}

int TinyTmuxNetBridge::rig_rf_step_zone(int delta) {
    char cmd[40];
    size_t clen = oyk_rf_build_step_zone(delta, cmd, sizeof(cmd));
    return (clen > 0) ? send_rf_udp_datagram(cmd, clen) : 0;
}

int TinyTmuxNetBridge::rig_rf_stop_notify() {
    char cmd[32];
    size_t clen = oyk_rf_build_stop_notify(cmd, sizeof(cmd));
    int ok = (clen > 0) ? send_rf_udp_datagram(cmd, clen) : 0;
    if (ok) rig_status_.notifications_active = 0;
    return ok;
}

int TinyTmuxNetBridge::node_tx_callback(int iface_idx,
                                        const OykFrame* frame,
                                        oyk_u32 peer_ip,
                                        oyk_u16 peer_port,
                                        oyk_u16 peer_handle,
                                        void* user_ctx) {
    TinyTmuxNetBridge* self = static_cast<TinyTmuxNetBridge*>(user_ctx);
    if (!self) return 0;
    return self->handle_node_tx(iface_idx, frame, peer_ip, peer_port, peer_handle);
}

int TinyTmuxNetBridge::handle_node_tx(int iface_idx,
                                      const OykFrame* frame,
                                      oyk_u32 peer_ip,
                                      oyk_u16 peer_port,
                                      oyk_u16 peer_handle) {
    if (!node_ || !frame || iface_idx < 0 || iface_idx >= OYK_MAX_INTERFACES) return 0;
    const IfaceBinding& b = bindings_[iface_idx];

    if (b.kind == TMUX_IF_USB_NET_AXUDP || b.kind == TMUX_IF_USB_NET_AXIP) {
        if (b.usb_slot < 0) return 0;
        const TinyTmuxUsbRadioSlot* s = usb_host_.slot_info(b.usb_slot);
        if (!s || !s->in_use) return 0;
        oyk_u32 dst_ip = peer_ip ? peer_ip : s->peer_ip;
        if (b.kind == TMUX_IF_USB_NET_AXIP || peer_handle == 1U) {
            return usb_host_.send_axip(b.usb_slot, frame, dst_ip);
        }
        oyk_u16 sport = node_->iface(iface_idx).cfg.port ? node_->iface(iface_idx).cfg.port : static_cast<oyk_u16>(10093);
        oyk_u16 dport = peer_port ? peer_port : sport;
        return usb_host_.send_axudp(b.usb_slot, frame, dst_ip, sport, dport);
    }

    if (b.kind == TMUX_IF_PPP_AXUDP || b.kind == TMUX_IF_PPP_AXIP) {
        if (b.ppp_slot < 0 || b.ppp_slot >= TINYTMUX_MAX_PPP_LINKS) return 0;
        PppSlot& ps = ppp_slots_[b.ppp_slot];
        if (!ps.active || !ps.link.is_up()) return 0;
        int send_as_axip = (b.kind == TMUX_IF_PPP_AXIP || peer_handle == 1U) ? 1 : 0;
        if (send_as_axip) {
            return ps.link.send_axip(frame, peer_ip);
        }
        return ps.link.send_axudp(frame, peer_ip, peer_port);
    }

    if (b.kind == TMUX_IF_ENET_AXUDP || b.kind == TMUX_IF_ENET_AXIP) {
        if (!enet_up_) return 0;
        oyk_u8 raw[320];
        size_t n = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
        if (n == 0) return 0;
        oyk_u16 fcs = oyk_crc16_fcs(raw, n);
        raw[n++] = static_cast<oyk_u8>(fcs & 0xFFU);
        raw[n++] = static_cast<oyk_u8>((fcs >> 8) & 0xFFU);

        int is_axip = (b.kind == TMUX_IF_ENET_AXIP) ? 1 : 0;
        if (enet_tx_fn_) {
            return enet_tx_fn_(is_axip, peer_ip, peer_port, raw, n, enet_tx_ctx_);
        }
        return 1;
    }

    if (b.kind == TMUX_IF_KISS_SERIAL && b.logical_uart >= 0) {
        oyk_u8 ax[320], kiss[650];
        size_t ax_len = oyk_ax25_encode(frame, ax, sizeof(ax));
        if (ax_len == 0) return 0;
        size_t klen = oyk_kiss_encode(node_->iface(iface_idx).cfg.kiss_port, ax, ax_len, kiss, sizeof(kiss));
        if (klen == 0) return 0;
        return (tinytmux_uart_write(b.logical_uart, kiss, klen) == klen) ? 1 : 0;
    }

    if (b.kind == TMUX_IF_USB_ACM_KISS && b.usb_slot >= 0) {
        oyk_u8 ax[320], kiss[650];
        size_t ax_len = oyk_ax25_encode(frame, ax, sizeof(ax));
        if (ax_len == 0) return 0;
        size_t klen = oyk_kiss_encode(node_->iface(iface_idx).cfg.kiss_port, ax, ax_len, kiss, sizeof(kiss));
        if (klen == 0) return 0;
        return (usb_host_.send_acm_bytes(b.usb_slot, kiss, klen) == klen) ? 1 : 0;
    }

    return 0;
}

OykPppLink* TinyTmuxNetBridge::ppp_link(int slot) {
    if (slot < 0 || slot >= TINYTMUX_MAX_PPP_LINKS) return NULL;
    return &ppp_slots_[slot].link;
}

const OykPppLink* TinyTmuxNetBridge::ppp_link(int slot) const {
    if (slot < 0 || slot >= TINYTMUX_MAX_PPP_LINKS) return NULL;
    return &ppp_slots_[slot].link;
}
