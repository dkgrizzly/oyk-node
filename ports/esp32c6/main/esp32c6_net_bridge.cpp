/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "esp32c6_net_bridge.hpp"

#include <stdio.h>
#include <string.h>

Esp32C6NetBridge::Esp32C6NetBridge()
    : node_(NULL), lora_iface_idx_(-1), bt_iface_idx_(-1) {
    memset(&radio_cfg_, 0, sizeof(radio_cfg_));
    memset(&lora_cfg_, 0, sizeof(lora_cfg_));
    memset(slots_, 0, sizeof(slots_));
    for (int u = 0; u < OYK_ESP32C6_UART_COUNT; ++u) {
        uart_role_[u] = 0;
        uart_iface_[u] = -1;
        oyk_kiss_decoder_init(&uart_kiss_[u]);
    }
    oyk_kiss_decoder_init(&lora_kiss_);
    oyk_kiss_decoder_init(&bt_kiss_);
}

Esp32C6NetBridge::~Esp32C6NetBridge() {
    esp32c6_lora_deinit();
    esp32c6_radio_deinit();
}

void Esp32C6NetBridge::init(OykPortableNode* node, const Esp32C6RadioConfig* rcfg, const Esp32C6LoraConfig* lcfg) {
    node_ = node;
    if (rcfg) radio_cfg_ = *rcfg;
    else esp32c6_radio_default_config(&radio_cfg_);
    if (lcfg) lora_cfg_ = *lcfg;
    else esp32c6_lora_default_config(&lora_cfg_);

    esp32c6_radio_init(&radio_cfg_);
    esp32c6_radio_set_ble_rx_callback(ble_kiss_rx_trampoline, this);
    esp32c6_lora_init(&lora_cfg_);
    esp32c6_lora_set_rx_callback(lora_kiss_rx_trampoline, this);
    reconfig();
}

void Esp32C6NetBridge::reconfig() {
    memset(slots_, 0, sizeof(slots_));
    lora_iface_idx_ = -1;
    bt_iface_idx_ = -1;
    uart_role_[0] = 0;
    uart_iface_[0] = -1;
    oyk_kiss_decoder_init(&uart_kiss_[0]);
    oyk_kiss_decoder_init(&lora_kiss_);
    oyk_kiss_decoder_init(&bt_kiss_);
    if (!node_) return;

    const OykConfig& cfg = node_->config();
    int sc = 0;
    for (int i = 0; i < cfg.iface_count && sc < C6L_BRIDGE_MAX_SLOTS; ++i) {
        const OykIfaceConfig& ic = cfg.ifaces[i];
        Esp32C6BridgeSlot* s = &slots_[sc++];
        memset(s, 0, sizeof(*s));
        s->used = 1;
        s->iface_index = i;
        s->attached_uart = -1;
        if (strcmp(ic.device, "lora0") == 0) {
            s->is_lora = 1;
            s->active = esp32c6_lora_stats()->initialized ? 1 : 0;
            lora_iface_idx_ = i;
            strcpy(s->status, "SX1262 LoRa KISS");
            node_->set_iface_status(i, s->active, "UP (LORA 433.775M)");
        } else if (strcmp(ic.type, "axudp") == 0) {
            s->is_axudp = 1;
            s->listen_port = ic.port ? ic.port : 10093U;
            s->active = 1;
            sprintf(s->status, "Wi-Fi 6 UDP :%u", static_cast<unsigned int>(s->listen_port));
            node_->set_iface_status(i, 1, "UP (WIFI6 AXUDP)");
        } else if (strcmp(ic.type, "axip") == 0) {
            s->is_axip = 1;
            s->active = 1;
            strcpy(s->status, "Wi-Fi 6 IP93");
            node_->set_iface_status(i, 1, "UP (AXIP)");
        } else if (strcmp(ic.type, "kiss-tcp") == 0) {
            s->is_kiss_tcp = 1;
            s->listen_port = ic.port ? ic.port : 8001U;
            s->active = 1;
            sprintf(s->status, "KISS-TCP :%u", static_cast<unsigned int>(s->listen_port));
            node_->set_iface_status(i, 1, "LISTEN (KISS-TCP)");
        } else if (strcmp(ic.type, "telnet") == 0) {
            s->is_telnet = 1;
            s->listen_port = ic.port ? ic.port : 8023U;
            s->active = 1;
            sprintf(s->status, "Telnet :%u", static_cast<unsigned int>(s->listen_port));
            node_->set_iface_status(i, 1, "LISTEN (TELNET)");
        } else if (strcmp(ic.type, "kiss-bt") == 0) {
            s->is_bt_kiss = 1;
            s->active = 1;
            bt_iface_idx_ = i;
            strcpy(s->status, "BLE 5 KISS");
            node_->set_iface_status(i, 1, "UP (BLE5 KISS)");
        } else if (strcmp(ic.type, "ppp") == 0) {
            s->attached_uart = 0;
            s->active = 1;
            uart_role_[0] = 1;
            uart_iface_[0] = i;
            oyk_u32 local_ip = 0;
            if (!oyk_parse_ipv4(ic.host, &local_ip)) local_ip = 0x01002C0AUL; /* 10.44.0.1 */
            ppp_[0].init(local_ip, 0UL, ic.port ? ic.port : 10093U, 0x43364C30UL,
                         ppp_serial_tx0, this);
            ppp_[0].set_ax25_rx_callback(ppp_ax25_rx0, this);
            ppp_[0].open(esp32c6_board_millis());
            esp32c6_board_set_uart_baud(0, ic.baud ? ic.baud : 115200UL);
            strcpy(s->status, "Grove ser0 PPP");
            node_->set_iface_status(i, 1, "PPP (ser0 TX5/RX4)");
        } else if (strcmp(ic.type, "kiss-serial") == 0) {
            s->attached_uart = 0;
            s->active = 1;
            uart_role_[0] = 2;
            uart_iface_[0] = i;
            esp32c6_board_set_uart_baud(0, ic.baud ? ic.baud : 9600UL);
            strcpy(s->status, "Grove ser0 KISS");
            node_->set_iface_status(i, 1, "UP (ser0 KISS)");
        }
    }
}

void Esp32C6NetBridge::poll(oyk_u32 now_ms) {
    esp32c6_lora_poll();
    esp32c6_radio_poll();
    oyk_u8 rxbuf[64];
    size_t n = esp32c6_board_uart_read(0, rxbuf, sizeof(rxbuf));
    if (n > 0) sim_feed_uart_bytes(0, rxbuf, n, now_ms);
    if (uart_role_[0] == 1) ppp_[0].tick(now_ms);
}

int Esp32C6NetBridge::tx_callback_trampoline(int iface_idx, const OykFrame* frame,
                                             oyk_u32 peer_ip, oyk_u16 peer_port,
                                             oyk_u16 peer_handle, void* ctx) {
    Esp32C6NetBridge* self = static_cast<Esp32C6NetBridge*>(ctx);
    return self ? self->tx_frame(iface_idx, frame, peer_ip, peer_port, peer_handle) : 0;
}

int Esp32C6NetBridge::tx_frame(int iface_index, const OykFrame* frame,
                               oyk_u32 /*peer_ip*/, oyk_u16 /*peer_port*/, oyk_u16 /*peer_handle*/) {
    if (!frame || iface_index < 0) return 0;
    if (iface_index == lora_iface_idx_) {
        oyk_u8 raw[320];
        oyk_u8 kbuf[650];
        size_t rlen = oyk_ax25_encode(frame, raw, sizeof(raw));
        if (rlen == 0) return 0;
        size_t klen = oyk_kiss_encode(0U, raw, rlen, kbuf, sizeof(kbuf));
        return (klen > 0) ? esp32c6_lora_send_kiss(kbuf, klen) : 0;
    }
    if (iface_index == bt_iface_idx_) {
        oyk_u8 raw[320];
        oyk_u8 kbuf[650];
        size_t rlen = oyk_ax25_encode(frame, raw, sizeof(raw));
        if (rlen == 0) return 0;
        size_t klen = oyk_kiss_encode(0U, raw, rlen, kbuf, sizeof(kbuf));
        return (klen > 0) ? esp32c6_radio_ble_send(kbuf, klen) : 0;
    }
    if (uart_iface_[0] == iface_index && uart_role_[0] == 2) {
        oyk_u8 raw[320];
        oyk_u8 kbuf[650];
        size_t rlen = oyk_ax25_encode(frame, raw, sizeof(raw));
        if (rlen == 0) return 0;
        size_t klen = oyk_kiss_encode(0U, raw, rlen, kbuf, sizeof(kbuf));
        return (klen > 0) ? (esp32c6_board_uart_write(0, kbuf, klen) > 0 ? 1 : 0) : 0;
    }
    return 1;
}

const Esp32C6BridgeSlot* Esp32C6NetBridge::slot(int idx) const {
    if (idx < 0 || idx >= C6L_BRIDGE_MAX_SLOTS) return NULL;
    return &slots_[idx];
}

const OykPppLink* Esp32C6NetBridge::ppp_link(int uart_idx) const {
    if (uart_idx < 0 || uart_idx >= OYK_ESP32C6_UART_COUNT) return NULL;
    return &ppp_[uart_idx];
}

void Esp32C6NetBridge::format_short_radio_status(char* out, size_t out_sz) const {
    if (!out || out_sz == 0) return;
    const Esp32C6LoraStats* ls = esp32c6_lora_stats();
    if (ls->initialized) strcpy(out, "LORA+W6");
    else strcpy(out, "WIFI6");
}

void Esp32C6NetBridge::sim_feed_uart_bytes(int uart_idx, const oyk_u8* data, size_t len, oyk_u32 now_ms) {
    if (uart_idx != 0 || !data || len == 0) return;
    if (uart_role_[0] == 1) {
        ppp_[0].feed_serial_bytes(data, len, now_ms);
    } else if (uart_role_[0] == 2 && uart_iface_[0] >= 0) {
        for (size_t i = 0; i < len; ++i) on_kiss_byte(uart_iface_[0], data[i]);
    }
}

void Esp32C6NetBridge::sim_inject_bt_kiss(const oyk_u8* data, size_t len) {
    esp32c6_radio_sim_inject_ble(data, len);
}

void Esp32C6NetBridge::sim_inject_lora_kiss(const oyk_u8* data, size_t len, int rssi_dbm, int snr_x4) {
    esp32c6_lora_sim_inject_rx(data, len, rssi_dbm, snr_x4);
}

void Esp32C6NetBridge::ppp_ax25_rx0(int /*is_axip*/, const OykFrame* frame,
                                    oyk_u32 src_ip_net, oyk_u16 src_port_host, void* ctx) {
    Esp32C6NetBridge* self = static_cast<Esp32C6NetBridge*>(ctx);
    if (self && self->node_ && self->uart_iface_[0] >= 0 && frame) {
        self->node_->on_rx_frame(self->uart_iface_[0], frame, src_ip_net, src_port_host, 0U);
    }
}

size_t Esp32C6NetBridge::ppp_serial_tx0(const oyk_u8* data, size_t len, void* /*ctx*/) {
    int wr = esp32c6_board_uart_write(0, data, len);
    return (wr > 0) ? static_cast<size_t>(wr) : 0;
}

void Esp32C6NetBridge::ble_kiss_rx_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    Esp32C6NetBridge* self = static_cast<Esp32C6NetBridge*>(ctx);
    if (!self || self->bt_iface_idx_ < 0 || !data) return;
    for (size_t i = 0; i < len; ++i) {
        oyk_u8 port = 0, cmd = 0;
        if (oyk_kiss_decoder_feed_byte(&self->bt_kiss_, data[i], &port, &cmd) == 1 && cmd == 0U) {
            OykFrame fr;
            if (self->node_ && oyk_ax25_decode(self->bt_kiss_.buf, self->bt_kiss_.len, &fr)) {
                self->node_->on_rx_frame(self->bt_iface_idx_, &fr, 0UL, 0U, 0U);
            }
        }
    }
}

void Esp32C6NetBridge::lora_kiss_rx_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    Esp32C6NetBridge* self = static_cast<Esp32C6NetBridge*>(ctx);
    if (!self || self->lora_iface_idx_ < 0 || !data) return;
    for (size_t i = 0; i < len; ++i) self->on_kiss_byte(self->lora_iface_idx_, data[i]);
}

void Esp32C6NetBridge::on_kiss_byte(int iface_idx, oyk_u8 b) {
    oyk_u8 port = 0, cmd = 0;
    OykKissDecoder* dec = (iface_idx == lora_iface_idx_) ? &lora_kiss_ : &uart_kiss_[0];
    if (oyk_kiss_decoder_feed_byte(dec, b, &port, &cmd) == 1 && cmd == 0U) {
        OykFrame fr;
        if (node_ && oyk_ax25_decode(dec->buf, dec->len, &fr)) {
            node_->on_rx_frame(iface_idx, &fr, 0UL, 0U, 0U);
        }
    }
}
