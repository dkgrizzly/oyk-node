/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_net_bridge.hpp"

#include <stdio.h>
#include <string.h>

PicoNetBridge::PicoNetBridge()
    : node_(NULL), bt_iface_idx_(-1) {
    pico_radio_default_config(&radio_cfg_);
    memset(slots_, 0, sizeof(slots_));
    for (int i = 0; i < OYK_PICO_UART_COUNT; ++i) {
        uart_role_[i] = PICO_ROLE_UNUSED;
        uart_iface_[i] = -1;
        oyk_kiss_decoder_init(&uart_kiss_[i]);
    }
    oyk_kiss_decoder_init(&bt_kiss_);
}

PicoNetBridge::~PicoNetBridge() {
    pico_radio_deinit();
}

void PicoNetBridge::init(OykPortableNode* node, const PicoRadioConfig* radio_cfg, int sim_rm2_present) {
    node_ = node;
    if (radio_cfg) {
        radio_cfg_ = *radio_cfg;
    } else {
        pico_radio_default_config(&radio_cfg_);
    }
    pico_radio_init(&radio_cfg_, sim_rm2_present);
    pico_radio_set_bt_rx_callback(bt_kiss_rx_trampoline, this);
    reconfig();
}

void PicoNetBridge::reconfig() {
    memset(slots_, 0, sizeof(slots_));
    bt_iface_idx_ = -1;
    for (int u = 0; u < OYK_PICO_UART_COUNT; ++u) {
        uart_role_[u] = PICO_ROLE_UNUSED;
        uart_iface_[u] = -1;
        oyk_kiss_decoder_init(&uart_kiss_[u]);
    }
    if (!node_) return;

    const PicoRadioState* rs = pico_radio_state();
    int s_idx = 0;
    for (int i = 0; i < node_->iface_count() && s_idx < PICO_BRIDGE_MAX_SOCKETS; ++i) {
        const OykIfaceConfig& ic = node_->iface(i).cfg;
        PicoBridgeSlot& sl = slots_[s_idx++];
        sl.used = 1;
        sl.iface_index = i;
        sl.listen_port = static_cast<oyk_u16>(ic.port ? ic.port : 10093U);
        sl.attached_uart = -1;

        if (strcmp(ic.type, "ppp") == 0 || strcmp(ic.type, "kiss-serial") == 0) {
            int u_idx = (strcmp(ic.device, "ser1") == 0 || strcmp(ic.device, "uart1") == 0) ? 1 : 0;
            const PicoBoardProfile* prof = pico_current_board_profile();
            if (!pico_uart_info(u_idx)) {
                sl.active = 0;
                sprintf(sl.status, "%s UNAVAIL", (u_idx == 0) ? "ser0" : "ser1");
                node_->set_iface_status(i, 0, sl.status);
                continue;
            }
            sl.attached_uart = u_idx;
            uart_iface_[u_idx] = i;
            PicoUartConfig ucfg;
            ucfg.baudrate = ic.baud ? ic.baud : 115200UL;
            ucfg.cts_rts = (ucfg.baudrate >= 57600UL && (!prof || !prof->has_picocalc_hw)) ? 1 : 0;
            if (strcmp(ic.type, "ppp") == 0) {
                uart_role_[u_idx] = PICO_ROLE_PPP_RADIO;
                ucfg.role = PICO_ROLE_PPP_RADIO;
                pico_uart_init(u_idx, &ucfg);
                oyk_u32 lip = 0;
                if (!oyk_parse_ipv4(ic.host, &lip)) lip = 0x01002C0AUL;
                ppp_[u_idx].init(lip, 0UL, sl.listen_port,
                                 0x50494330UL + static_cast<oyk_u32>(u_idx),
                                 (u_idx == 0) ? ppp_serial_tx0 : ppp_serial_tx1, this);
                ppp_[u_idx].set_ax25_rx_callback(
                    (u_idx == 0) ? ppp_ax25_rx_trampoline0 : ppp_ax25_rx_trampoline1, this);
                ppp_[u_idx].open(pico_board_millis());
                sprintf(sl.status, "PPP:%s READY", (u_idx == 0) ? "ser0" : "ser1");
            } else {
                uart_role_[u_idx] = PICO_ROLE_KISS_TNC;
                ucfg.role = PICO_ROLE_KISS_TNC;
                pico_uart_init(u_idx, &ucfg);
                sprintf(sl.status, "KISS:%s %lu", (u_idx == 0) ? "ser0" : "ser1",
                        static_cast<unsigned long>(ucfg.baudrate));
            }
            sl.active = 1;
            node_->set_iface_status(i, 1, sl.status);
        } else if (strcmp(ic.type, "kiss-bt") == 0 || strcmp(ic.type, "bluetooth") == 0) {
            sl.is_bt_kiss = 1;
            bt_iface_idx_ = i;
            if (rs->module_present && rs->bt_enabled) {
                sl.active = 1;
                strcpy(sl.status, "BT-KISS READY");
            } else {
                sl.active = 0;
                strcpy(sl.status, rs->module_present ? "BT DISABLED" : "NO RM2/BT MODULE");
            }
            node_->set_iface_status(i, sl.active, sl.status);
        } else {
            sl.is_axudp = (strcmp(ic.type, "axudp") == 0);
            sl.is_axip = (strcmp(ic.type, "axip") == 0);
            sl.is_kiss_tcp = (strcmp(ic.type, "kiss-tcp") == 0);
            sl.is_telnet = (strcmp(ic.type, "telnet") == 0);
            if (rs->module_present && rs->wifi_enabled) {
                sl.active = 1;
                sprintf(sl.status, "%s WiFi:%u", ic.type, static_cast<unsigned int>(sl.listen_port));
            } else {
                sl.active = 0;
                strcpy(sl.status, rs->module_present ? "WiFi OFF" : "NO RM2/WiFi (USB/UART OK)");
            }
            node_->set_iface_status(i, sl.active, sl.status);
        }
    }
}

void PicoNetBridge::poll(oyk_u32 now_ms) {
    pico_radio_poll();
    for (int u = 0; u < OYK_PICO_UART_COUNT; ++u) {
        if (uart_role_[u] == PICO_ROLE_UNUSED) continue;
        oyk_u8 buf[128];
        size_t got = pico_uart_read(u, buf, sizeof(buf));
        if (got > 0) {
            sim_feed_uart_bytes(u, buf, got, now_ms);
        }
        if (uart_role_[u] == PICO_ROLE_PPP_RADIO) {
            ppp_[u].tick(now_ms);
        }
    }
}

void PicoNetBridge::sim_feed_uart_bytes(int uart_idx, const oyk_u8* data, size_t len, oyk_u32 now_ms) {
    if (uart_idx < 0 || uart_idx >= OYK_PICO_UART_COUNT || !data || len == 0) return;
    if (uart_role_[uart_idx] == PICO_ROLE_PPP_RADIO) {
        ppp_[uart_idx].feed_serial_bytes(data, len, now_ms);
    } else if (uart_role_[uart_idx] == PICO_ROLE_KISS_TNC && uart_iface_[uart_idx] >= 0) {
        for (size_t i = 0; i < len; ++i) {
            on_kiss_byte(uart_iface_[uart_idx], data[i]);
        }
    }
}

void PicoNetBridge::sim_inject_bt_kiss(const oyk_u8* data, size_t len) {
    pico_radio_sim_inject_bt(data, len);
}

void PicoNetBridge::bt_kiss_rx_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    PicoNetBridge* self = static_cast<PicoNetBridge*>(ctx);
    if (!self || self->bt_iface_idx_ < 0 || !data || !self->node_) return;
    for (size_t i = 0; i < len; ++i) {
        oyk_u8 port = 0, cmd = 0;
        if (oyk_kiss_decoder_feed_byte(&self->bt_kiss_, data[i], &port, &cmd)) {
            if ((cmd & 0x0FU) == 0 && self->bt_kiss_.len >= 14) {
                OykFrame f;
                if (oyk_ax25_decode(self->bt_kiss_.buf, self->bt_kiss_.len, &f)) {
                    self->node_->on_rx_frame(self->bt_iface_idx_, &f, 0UL, 0U, 0U);
                }
            }
        }
    }
}

void PicoNetBridge::on_kiss_byte(int iface_idx, oyk_u8 b) {
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

void PicoNetBridge::ppp_ax25_rx_trampoline0(int is_axip, const OykFrame* frame,
                                            oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx) {
    PicoNetBridge* self = static_cast<PicoNetBridge*>(ctx);
    if (self) self->on_ppp_ax25_rx(0, is_axip, frame, src_ip_net, src_port_host);
}

void PicoNetBridge::ppp_ax25_rx_trampoline1(int is_axip, const OykFrame* frame,
                                            oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx) {
    PicoNetBridge* self = static_cast<PicoNetBridge*>(ctx);
    if (self) self->on_ppp_ax25_rx(1, is_axip, frame, src_ip_net, src_port_host);
}

void PicoNetBridge::on_ppp_ax25_rx(int uart_idx, int is_axip, const OykFrame* frame,
                                   oyk_u32 src_ip_net, oyk_u16 src_port_host) {
    if (!node_ || !frame) return;
    int iface_idx = uart_iface_[uart_idx];
    if (iface_idx < 0) return;
    (void)is_axip;
    node_->on_rx_frame(iface_idx, frame, src_ip_net, src_port_host, 0U);
}

size_t PicoNetBridge::ppp_serial_tx0(const oyk_u8* data, size_t len, void* /*ctx*/) {
    return pico_uart_write(0, data, len);
}

size_t PicoNetBridge::ppp_serial_tx1(const oyk_u8* data, size_t len, void* /*ctx*/) {
    return pico_uart_write(1, data, len);
}

int PicoNetBridge::tx_callback_trampoline(int iface_idx, const OykFrame* frame,
                                          oyk_u32 peer_ip, oyk_u16 peer_port,
                                          oyk_u16 peer_handle, void* ctx) {
    PicoNetBridge* self = static_cast<PicoNetBridge*>(ctx);
    return self ? self->tx_frame(iface_idx, frame, peer_ip, peer_port, peer_handle) : 0;
}

int PicoNetBridge::tx_frame(int iface_index, const OykFrame* frame,
                            oyk_u32 peer_ip, oyk_u16 peer_port, oyk_u16 /*peer_handle*/) {
    if (!node_ || !frame || iface_index < 0 || iface_index >= node_->iface_count()) return 0;
    const OykIfaceConfig& ic = node_->iface(iface_index).cfg;

    if (strcmp(ic.type, "ppp") == 0) {
        int u = (iface_index == uart_iface_[1]) ? 1 : 0;
        if (peer_ip == 0) return 0;
        return ppp_[u].send_axudp(frame, peer_ip, peer_port ? peer_port : 10093U);
    }

    static oyk_u8 raw[512];
    size_t rlen = oyk_ax25_encode(frame, raw, sizeof(raw));
    if (rlen == 0) return 0;

    static oyk_u8 kiss[1050];
    if (strcmp(ic.type, "kiss-serial") == 0) {
        int u = (iface_index == uart_iface_[1]) ? 1 : 0;
        size_t pos = oyk_kiss_encode(ic.kiss_port, raw, rlen, kiss, sizeof(kiss));
        return (pos > 0 && pico_uart_write(u, kiss, pos) > 0) ? 1 : 0;
    }

    if (strcmp(ic.type, "kiss-bt") == 0 || strcmp(ic.type, "bluetooth") == 0) {
        size_t pos = oyk_kiss_encode(ic.kiss_port, raw, rlen, kiss, sizeof(kiss));
        return (pos > 0 && pico_radio_bt_send(kiss, pos) > 0) ? 1 : 0;
    }

    return 0;
}

const PicoBridgeSlot* PicoNetBridge::slot(int idx) const {
    if (idx < 0 || idx >= PICO_BRIDGE_MAX_SOCKETS) return NULL;
    return &slots_[idx];
}

int PicoNetBridge::active_uart_count() const {
    int n = 0;
    for (int u = 0; u < OYK_PICO_UART_COUNT; ++u) {
        if (uart_role_[u] != PICO_ROLE_UNUSED) ++n;
    }
    return n;
}

const OykPppLink* PicoNetBridge::ppp_link(int uart_idx) const {
    if (uart_idx < 0 || uart_idx >= OYK_PICO_UART_COUNT) return NULL;
    return &ppp_[uart_idx];
}

const PicoRadioState* PicoNetBridge::radio_state() const {
    return pico_radio_state();
}

void PicoNetBridge::format_short_radio_status(char* out, size_t out_sz) const {
    if (!out || out_sz == 0) return;
    const PicoRadioState* rs = pico_radio_state();
    if (!rs->module_present) {
        strncpy(out, "NONE", out_sz - 1);
    } else if (rs->wifi_connected && rs->bt_enabled) {
        strncpy(out, "WF+BT", out_sz - 1);
    } else if (rs->wifi_connected) {
        strncpy(out, "WIFI", out_sz - 1);
    } else if (rs->bt_enabled) {
        strncpy(out, "BT", out_sz - 1);
    } else {
        strncpy(out, "READY", out_sz - 1);
    }
    out[out_sz - 1] = '\0';
}
