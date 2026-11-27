/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ESP32C6_LORA_HPP
#define OYK_ESP32C6_LORA_HPP

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"

struct Esp32C6LoraConfig {
    int enabled;
    oyk_u32 freq_hz;
    oyk_u8 sf;          /* Spreading factor: 7..12 */
    oyk_u8 bw_idx;      /* 0=125kHz, 1=250kHz, 2=500kHz */
    oyk_u8 cr;          /* Coding rate: 5..8 (4/5..4/8) */
    signed char tx_power_dbm;/* -9..22 dBm */
    oyk_u16 preamble_len;
    oyk_u16 sync_word;
};

struct Esp32C6LoraStats {
    int initialized;
    int tx_active;
    int rx_active;
    oyk_u32 rx_packets;
    oyk_u32 tx_packets;
    oyk_u32 crc_errors;
    int last_rssi_dbm;
    int last_snr_x4;
    char status_text[48];
};

typedef void (*Esp32C6LoraRxFn)(const oyk_u8* kiss_bytes, size_t len, void* ctx);

void esp32c6_lora_default_config(Esp32C6LoraConfig* cfg);
int  esp32c6_lora_init(const Esp32C6LoraConfig* cfg);
void esp32c6_lora_deinit();
void esp32c6_lora_poll();
void esp32c6_lora_set_rx_callback(Esp32C6LoraRxFn cb, void* ctx);
int  esp32c6_lora_send_kiss(const oyk_u8* kiss_bytes, size_t len);
void esp32c6_lora_sim_inject_rx(const oyk_u8* kiss_bytes, size_t len, int rssi_dbm, int snr_x4);
const Esp32C6LoraConfig* esp32c6_lora_config();
const Esp32C6LoraStats*  esp32c6_lora_stats();

#endif /* OYK_ESP32C6_LORA_HPP */
