/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "lyra_net_bridge.hpp"

#include <stdio.h>
#include <string.h>

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
#include <sys/socket.h>
#include <fcntl.h>
#endif

LyraNetBridge::LyraNetBridge()
    : node_(NULL), ble_iface_idx_(-1) {
    lyra_radio_default_config(&radio_cfg_);
    memset(slots_, 0, sizeof(slots_));
    for (int i = 0; i < LYRA_BRIDGE_MAX_SOCKETS; ++i) {
        slots_[i].sock_fd = -1;
    }
    for (int i = 0; i < OYK_LYRA_UART_COUNT; ++i) {
        uart_role_[i] = LYRA_ROLE_UNUSED;
        uart_iface_[i] = -1;
        oyk_kiss_decoder_init(&uart_kiss_[i]);
    }
    oyk_kiss_decoder_init(&ble_kiss_);
}

LyraNetBridge::~LyraNetBridge() {
    close_all_sockets();
    lyra_radio_deinit();
}

void LyraNetBridge::close_all_sockets() {
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    for (int i = 0; i < LYRA_BRIDGE_MAX_SOCKETS; ++i) {
        if (slots_[i].sock_fd >= 0) {
            closesocket(slots_[i].sock_fd);
            slots_[i].sock_fd = -1;
        }
    }
#endif
}

void LyraNetBridge::init(OykPortableNode* node, const LyraRadioConfig* radio_cfg, int sim_net_up) {
    node_ = node;
    if (radio_cfg) {
        radio_cfg_ = *radio_cfg;
    } else {
        lyra_radio_default_config(&radio_cfg_);
    }
    lyra_radio_init(&radio_cfg_, sim_net_up);
    lyra_radio_set_ble_rx_callback(ble_kiss_rx_trampoline, this);
    reconfig();
}

void LyraNetBridge::reconfig() {
    close_all_sockets();
    memset(slots_, 0, sizeof(slots_));
    for (int i = 0; i < LYRA_BRIDGE_MAX_SOCKETS; ++i) {
        slots_[i].sock_fd = -1;
    }
    ble_iface_idx_ = -1;
    for (int u = 0; u < OYK_LYRA_UART_COUNT; ++u) {
        uart_role_[u] = LYRA_ROLE_UNUSED;
        uart_iface_[u] = -1;
        oyk_kiss_decoder_init(&uart_kiss_[u]);
    }
    if (!node_) return;

    const LyraRadioState* rs = lyra_radio_state();
    const char* bearer = (rs->net_kind == LYRA_NET_RMII_ETH) ? "Eth"
                       : ((rs->net_kind == LYRA_NET_WIFI6_BLE) ? "WiFi6" : "RNDIS");
    int s_idx = 0;
    for (int i = 0; i < node_->iface_count() && s_idx < LYRA_BRIDGE_MAX_SOCKETS; ++i) {
        const OykIfaceConfig& ic = node_->iface(i).cfg;
        LyraBridgeSlot& sl = slots_[s_idx++];
        sl.used = 1;
        sl.iface_index = i;
        sl.listen_port = static_cast<oyk_u16>(ic.port ? ic.port : 10093U);
        sl.attached_uart = -1;

        if (strcmp(ic.type, "ppp") == 0 || strcmp(ic.type, "kiss-serial") == 0) {
            int u_idx = (strcmp(ic.device, "ser1") == 0 || strcmp(ic.device, "uart2") == 0) ? 1 : 0;
            if (!lyra_uart_info(u_idx)) {
                sl.active = 0;
                sprintf(sl.status, "%s UNAVAIL", (u_idx == 0) ? "ser0" : "ser1");
                node_->set_iface_status(i, 0, sl.status);
                continue;
            }
            sl.attached_uart = u_idx;
            uart_iface_[u_idx] = i;
            LyraUartConfig ucfg;
            ucfg.baudrate = ic.baud ? ic.baud : 115200UL;
            ucfg.cts_rts = 0U;
            if (strcmp(ic.type, "ppp") == 0) {
                uart_role_[u_idx] = LYRA_ROLE_PPP_RADIO;
                ucfg.role = LYRA_ROLE_PPP_RADIO;
                oyk_u32 lip = 0;
                if (!oyk_parse_ipv4(ic.host, &lip)) lip = 0x01002C0AUL;
                ppp_[u_idx].init(lip, 0UL, sl.listen_port,
                                 0x4C595230UL + static_cast<oyk_u32>(u_idx),
                                 (u_idx == 0) ? ppp_serial_tx0 : ppp_serial_tx1, this);
                ppp_[u_idx].set_ax25_rx_callback(
                    (u_idx == 0) ? ppp_ax25_rx_trampoline0 : ppp_ax25_rx_trampoline1, this);
                ppp_[u_idx].open(lyra_uptime_ms());
                sprintf(sl.status, "PPP:%s READY", (u_idx == 0) ? "ser0" : "ser1");
            } else {
                uart_role_[u_idx] = LYRA_ROLE_KISS_TNC;
                ucfg.role = LYRA_ROLE_KISS_TNC;
                sprintf(sl.status, "KISS:%s %lu", (u_idx == 0) ? "ser0" : "ser1",
                        static_cast<unsigned long>(ucfg.baudrate));
            }
            lyra_uart_init(u_idx, &ucfg);
            sl.active = 1;
            node_->set_iface_status(i, 1, sl.status);
        } else if (strcmp(ic.type, "kiss-bt") == 0 || strcmp(ic.type, "bluetooth") == 0) {
            sl.is_bt_kiss = 1;
            ble_iface_idx_ = i;
            if (rs->ble_enabled) {
                sl.active = 1;
                strcpy(sl.status, "BLE-KISS READY");
            } else {
                sl.active = 0;
                strcpy(sl.status, "NO BLE RADIO");
            }
            node_->set_iface_status(i, sl.active, sl.status);
        } else {
            sl.is_axudp = (strcmp(ic.type, "axudp") == 0);
            sl.is_axip = (strcmp(ic.type, "axip") == 0);
            sl.is_kiss_tcp = (strcmp(ic.type, "kiss-tcp") == 0);
            sl.is_telnet = (strcmp(ic.type, "telnet") == 0);
            if (rs->ip_transport_ready) {
                sl.active = 1;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
                if (sl.is_axudp) {
                    int fd = socket(AF_INET, SOCK_DGRAM, 0);
                    if (fd >= 0) {
                        struct sockaddr_in sa;
                        memset(&sa, 0, sizeof(sa));
                        sa.sin_family = AF_INET;
                        sa.sin_port = htons(sl.listen_port);
                        sa.sin_addr.s_addr = INADDR_ANY;
                        if (bind(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) == 0) {
                            fcntl(fd, F_SETFL, O_NONBLOCK);
                            sl.sock_fd = fd;
                        } else {
                            closesocket(fd);
                        }
                    }
                }
#endif
                sprintf(sl.status, "%s %s:%u", ic.type, bearer, static_cast<unsigned int>(sl.listen_port));
            } else {
                sl.active = 0;
                sprintf(sl.status, "%s DOWN (UART OK)", bearer);
            }
            node_->set_iface_status(i, sl.active, sl.status);
        }
    }
}

void LyraNetBridge::poll(oyk_u32 now_ms) {
    lyra_radio_poll();
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    for (int s = 0; s < LYRA_BRIDGE_MAX_SOCKETS; ++s) {
        if (!slots_[s].used || !slots_[s].is_axudp || slots_[s].sock_fd < 0 || !node_) continue;
        oyk_u8 rx[576];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(slots_[s].sock_fd, rx, sizeof(rx), 0,
                         reinterpret_cast<struct sockaddr*>(&from), &flen);
        if (n >= 14) {
            OykFrame f;
            if (oyk_ax25_decode(rx, static_cast<size_t>(n), &f)) {
                node_->on_rx_frame(slots_[s].iface_index, &f,
                                   from.sin_addr.s_addr, ntohs(from.sin_port), 0U);
            }
        }
    }
#endif
    for (int u = 0; u < OYK_LYRA_UART_COUNT; ++u) {
        if (uart_role_[u] == LYRA_ROLE_UNUSED) continue;
        oyk_u8 buf[128];
        size_t got = lyra_uart_read(u, buf, sizeof(buf));
        if (got > 0) {
            sim_feed_uart_bytes(u, buf, got, now_ms);
        }
        if (uart_role_[u] == LYRA_ROLE_PPP_RADIO) {
            ppp_[u].tick(now_ms);
        }
    }
}

void LyraNetBridge::sim_feed_uart_bytes(int uart_idx, const oyk_u8* data, size_t len, oyk_u32 now_ms) {
    if (uart_idx < 0 || uart_idx >= OYK_LYRA_UART_COUNT || !data || len == 0) return;
    if (uart_role_[uart_idx] == LYRA_ROLE_PPP_RADIO) {
        ppp_[uart_idx].feed_serial_bytes(data, len, now_ms);
    } else if (uart_role_[uart_idx] == LYRA_ROLE_KISS_TNC && uart_iface_[uart_idx] >= 0) {
        for (size_t i = 0; i < len; ++i) {
            on_kiss_byte(uart_iface_[uart_idx], data[i]);
        }
    }
}

void LyraNetBridge::sim_inject_ble_kiss(const oyk_u8* data, size_t len) {
    lyra_radio_sim_inject_ble(data, len);
}

void LyraNetBridge::ble_kiss_rx_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    LyraNetBridge* self = static_cast<LyraNetBridge*>(ctx);
    if (!self || self->ble_iface_idx_ < 0 || !data || !self->node_) return;
    for (size_t i = 0; i < len; ++i) {
        oyk_u8 port = 0, cmd = 0;
        if (oyk_kiss_decoder_feed_byte(&self->ble_kiss_, data[i], &port, &cmd)) {
            if ((cmd & 0x0FU) == 0 && self->ble_kiss_.len >= 14) {
                OykFrame f;
                if (oyk_ax25_decode(self->ble_kiss_.buf, self->ble_kiss_.len, &f)) {
                    self->node_->on_rx_frame(self->ble_iface_idx_, &f, 0UL, 0U, 0U);
                }
            }
        }
    }
}

void LyraNetBridge::on_kiss_byte(int iface_idx, oyk_u8 b) {
    int u = (iface_idx == uart_iface_[1]) ? 1 : 0;
    oyk_u8 port = 0, cmd = 0;
    if (oyk_kiss_decoder_feed_byte(&uart_kiss_[u], b, &port, &cmd)) {
        if ((cmd & 0x0FU) == 0 && uart_kiss_[u].len >= 14 && node_) {
            OykFrame f;
            if (oyk_ax25_decode(uart_kiss_[u].buf, uart_kiss_[u].len, &f)) {
                node_->on_rx_frame(iface_idx, &f, 0UL, 0U, 0U);
            }
        }
    }
}

void LyraNetBridge::ppp_ax25_rx_trampoline0(int is_axip, const OykFrame* frame,
                                            oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx) {
    LyraNetBridge* self = static_cast<LyraNetBridge*>(ctx);
    if (self) self->on_ppp_ax25_rx(0, is_axip, frame, src_ip_net, src_port_host);
}

void LyraNetBridge::ppp_ax25_rx_trampoline1(int is_axip, const OykFrame* frame,
                                            oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx) {
    LyraNetBridge* self = static_cast<LyraNetBridge*>(ctx);
    if (self) self->on_ppp_ax25_rx(1, is_axip, frame, src_ip_net, src_port_host);
}

void LyraNetBridge::on_ppp_ax25_rx(int uart_idx, int is_axip, const OykFrame* frame,
                                   oyk_u32 src_ip_net, oyk_u16 src_port_host) {
    if (!node_ || !frame) return;
    int iface_idx = uart_iface_[uart_idx];
    if (iface_idx < 0) return;
    (void)is_axip;
    node_->on_rx_frame(iface_idx, frame, src_ip_net, src_port_host, 0U);
}

size_t LyraNetBridge::ppp_serial_tx0(const oyk_u8* data, size_t len, void* /*ctx*/) {
    return lyra_uart_write(0, data, len);
}

size_t LyraNetBridge::ppp_serial_tx1(const oyk_u8* data, size_t len, void* /*ctx*/) {
    return lyra_uart_write(1, data, len);
}

int LyraNetBridge::tx_callback_trampoline(int iface_idx, const OykFrame* frame,
                                          oyk_u32 peer_ip, oyk_u16 peer_port,
                                          oyk_u16 peer_handle, void* ctx) {
    LyraNetBridge* self = static_cast<LyraNetBridge*>(ctx);
    return self ? self->tx_frame(iface_idx, frame, peer_ip, peer_port, peer_handle) : 0;
}

int LyraNetBridge::tx_frame(int iface_index, const OykFrame* frame,
                            oyk_u32 peer_ip, oyk_u16 peer_port, oyk_u16 /*peer_handle*/) {
    if (!node_ || !frame || iface_index < 0 || iface_index >= node_->iface_count()) return 0;
    const OykIfaceConfig& ic = node_->iface(iface_index).cfg;

    if (strcmp(ic.type, "ppp") == 0) {
        int u = (iface_index == uart_iface_[1]) ? 1 : 0;
        if (peer_ip == 0) return 0;
        return ppp_[u].send_axudp(frame, peer_ip, peer_port ? peer_port : 10093U);
    }

    size_t rlen = oyk_ax25_encode(frame, tx_raw_, sizeof(tx_raw_));
    if (rlen == 0) return 0;

    if (strcmp(ic.type, "kiss-serial") == 0) {
        int u = (iface_index == uart_iface_[1]) ? 1 : 0;
        size_t pos = oyk_kiss_encode(ic.kiss_port, tx_raw_, rlen, tx_kiss_, sizeof(tx_kiss_));
        return (pos > 0 && lyra_uart_write(u, tx_kiss_, pos) > 0) ? 1 : 0;
    }

    if (strcmp(ic.type, "kiss-bt") == 0 || strcmp(ic.type, "bluetooth") == 0) {
        size_t pos = oyk_kiss_encode(ic.kiss_port, tx_raw_, rlen, tx_kiss_, sizeof(tx_kiss_));
        return (pos > 0 && lyra_radio_ble_send(tx_kiss_, pos) > 0) ? 1 : 0;
    }

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    if (strcmp(ic.type, "axudp") == 0 && peer_ip != 0) {
        for (int s = 0; s < LYRA_BRIDGE_MAX_SOCKETS; ++s) {
            if (slots_[s].used && slots_[s].iface_index == iface_index && slots_[s].sock_fd >= 0) {
                struct sockaddr_in dst;
                memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET;
                dst.sin_port = htons(peer_port ? peer_port : 10093U);
                dst.sin_addr.s_addr = peer_ip;
                int sent = sendto(slots_[s].sock_fd, tx_raw_, rlen, 0,
                                  reinterpret_cast<const struct sockaddr*>(&dst), sizeof(dst));
                return (sent > 0) ? 1 : 0;
            }
        }
    }
#endif

    return 0;
}

const LyraBridgeSlot* LyraNetBridge::slot(int idx) const {
    if (idx < 0 || idx >= LYRA_BRIDGE_MAX_SOCKETS) return NULL;
    return &slots_[idx];
}

int LyraNetBridge::active_uart_count() const {
    int n = 0;
    for (int u = 0; u < OYK_LYRA_UART_COUNT; ++u) {
        if (uart_role_[u] != LYRA_ROLE_UNUSED) ++n;
    }
    return n;
}

const OykPppLink* LyraNetBridge::ppp_link(int uart_idx) const {
    if (uart_idx < 0 || uart_idx >= OYK_LYRA_UART_COUNT) return NULL;
    return &ppp_[uart_idx];
}

const LyraRadioState* LyraNetBridge::radio_state() const {
    return lyra_radio_state();
}

void LyraNetBridge::format_short_net_status(char* out, size_t out_sz) const {
    if (!out || out_sz == 0) return;
    const LyraRadioState* rs = lyra_radio_state();
    if (!rs->ip_transport_ready) {
        strncpy(out, "UART", out_sz - 1);
    } else if (rs->net_kind == LYRA_NET_RMII_ETH) {
        strncpy(out, "RMII", out_sz - 1);
    } else if (rs->net_kind == LYRA_NET_WIFI6_BLE) {
        strncpy(out, rs->ble_enabled ? "WF+B" : "WIFI", out_sz - 1);
    } else {
        strncpy(out, "RNDIS", out_sz - 1);
    }
    out[out_sz - 1] = '\0';
}
