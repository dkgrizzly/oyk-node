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

TinyTmuxNetBridge::TinyTmuxNetBridge()
    : node_(NULL),
      enet_up_(0), enet_100m_(0), enet_ip_(0),
      enet_tx_fn_(NULL), enet_tx_ctx_(NULL) {
    memset(bindings_, 0, sizeof(bindings_));
    for (int i = 0; i < TINYTMUX_MAX_PPP_LINKS; ++i) {
        ppp_slots_[i].active = 0;
        ppp_slots_[i].logical_uart = -1;
        ppp_slots_[i].axudp_iface_idx = -1;
        ppp_slots_[i].axip_iface_idx = -1;
        ppp_slots_[i].owner = this;
    }
}

void TinyTmuxNetBridge::set_ethernet_tx_hook(TinyTmuxEnetTxFn fn, void* ctx) {
    enet_tx_fn_ = fn;
    enet_tx_ctx_ = ctx;
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
        }
    }
}

int TinyTmuxNetBridge::init(OykPortableNode* node, oyk_u32 now_ms) {
    node_ = node;
    if (!node_) return 0;

    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        bindings_[i].kind = TMUX_IF_NONE;
        bindings_[i].ppp_slot = -1;
        bindings_[i].logical_uart = -1;
        oyk_kiss_decoder_init(&bindings_[i].kiss_dec);
    }

#if defined(OYK_HAS_LWIP)
    lwip_init();
#endif

    int next_ppp = 0;
    for (int i = 0; i < node_->iface_count(); ++i) {
        const OykIfaceConfig& ic = node_->iface(i).cfg;
        int ser_idx = parse_ser_index(ic.device);

        if (strieq_local(ic.type, "axudp")) {
            if (ser_idx >= 0 && next_ppp < TINYTMUX_MAX_PPP_LINKS) {
                /* AXUDP over PPP serial link (e.g. RadioFoundry radio on ser0..ser7) */
                bindings_[i].kind = TMUX_IF_PPP_AXUDP;
                bindings_[i].ppp_slot = next_ppp;
                bindings_[i].logical_uart = ser_idx;
                oyk_u32 loc_ip = 0, peer_ip = 0;
                oyk_parse_ipv4(ic.host, &loc_ip);
                if (ic.peer_count > 0) peer_ip = ic.peers[0].ip;
                configure_ppp_slot(next_ppp, ser_idx, i, -1, loc_ip, peer_ip,
                                   ic.port ? ic.port : static_cast<oyk_u16>(10093),
                                   NULL, NULL, now_ms);
                ++next_ppp;
            } else {
                bindings_[i].kind = TMUX_IF_ENET_AXUDP;
                node_->set_iface_status(i, enet_up_, enet_up_ ? "ENET AXUDP UP" : "ENET INIT");
            }
        } else if (strieq_local(ic.type, "axip")) {
            if (ser_idx >= 0) {
                /* Share existing PPP slot on same UART if already configured, else open new slot */
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
                    oyk_u32 loc_ip = 0, peer_ip = 0;
                    oyk_parse_ipv4(ic.host, &loc_ip);
                    if (ic.peer_count > 0) peer_ip = ic.peers[0].ip;
                    configure_ppp_slot(next_ppp, ser_idx, -1, i, loc_ip, peer_ip, 10093,
                                       NULL, NULL, now_ms);
                    ++next_ppp;
                }
            } else {
                bindings_[i].kind = TMUX_IF_ENET_AXIP;
                node_->set_iface_status(i, enet_up_, enet_up_ ? "ENET AXIP UP" : "ENET INIT");
            }
        } else if (strieq_local(ic.type, "ppp") && ser_idx >= 0 && next_ppp < TINYTMUX_MAX_PPP_LINKS) {
            /* Dual-protocol PPP link carrying both AXUDP and AXIP to RadioFoundry radio */
            bindings_[i].kind = TMUX_IF_PPP_AXUDP;
            bindings_[i].ppp_slot = next_ppp;
            bindings_[i].logical_uart = ser_idx;
            oyk_u32 loc_ip = 0, peer_ip = 0;
            oyk_parse_ipv4(ic.host, &loc_ip);
            if (ic.peer_count > 0) peer_ip = ic.peers[0].ip;
            configure_ppp_slot(next_ppp, ser_idx, i, i, loc_ip, peer_ip,
                               ic.port ? ic.port : static_cast<oyk_u16>(10093),
                               NULL, NULL, now_ms);
            ++next_ppp;
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
    return 1;
}

size_t TinyTmuxNetBridge::uart_serial_tx_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    PppSlot* ps = static_cast<PppSlot*>(ctx);
    if (!ps || ps->logical_uart < 0) return 0;
    return tinytmux_uart_write(ps->logical_uart, data, len);
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
    }
    if (axip_iface_idx >= 0 && axip_iface_idx < OYK_MAX_INTERFACES && axip_iface_idx != axudp_iface_idx) {
        bindings_[axip_iface_idx].kind = TMUX_IF_PPP_AXIP;
        bindings_[axip_iface_idx].ppp_slot = ppp_slot;
        bindings_[axip_iface_idx].logical_uart = logical_uart;
    }

    OykPppSerialTxFn tx_cb = custom_serial_tx ? custom_serial_tx : uart_serial_tx_trampoline;
    void* tx_ctx = custom_serial_tx ? custom_serial_ctx : &ps;
    ps.link.init(local_ip_net, peer_ip_net, axudp_port,
                 0x4F594B10UL + static_cast<oyk_u32>(ppp_slot),
                 tx_cb, tx_ctx);
    ps.link.set_ax25_rx_callback(ppp_ax25_rx_trampoline, &ps);
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

void TinyTmuxNetBridge::poll(oyk_u32 now_ms) {
#if defined(OYK_HAS_LWIP)
    sys_check_timeouts();
#endif

    oyk_u8 rx_tmp[128];
    for (int s = 0; s < TINYTMUX_MAX_PPP_LINKS; ++s) {
        if (!ppp_slots_[s].active) continue;
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
            if (bindings_[i].kind == TMUX_IF_KISS_SERIAL && bindings_[i].logical_uart >= 0) {
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
            }
        }
    }
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
