/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "esp32c6_lora.hpp"
#include "board_esp32c6.hpp"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/spi_master.h"
#endif

static Esp32C6LoraConfig s_lora_cfg;
static Esp32C6LoraStats  s_lora_stats;
static Esp32C6LoraRxFn   s_lora_rx_cb = NULL;
static void*             s_lora_rx_ctx = NULL;
#ifdef ESP_PLATFORM
static spi_device_handle_t s_lora_spi = NULL;
#endif

static void sx1262_cmd(oyk_u8 opcode, const oyk_u8* params, size_t len) {
#ifdef ESP_PLATFORM
    if (!s_lora_spi) return;
    oyk_u8 tx[16];
    if (len > 15U) len = 15U;
    tx[0] = opcode;
    if (params && len > 0) memcpy(tx + 1, params, len);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (len + 1U) * 8U;
    t.tx_buffer = tx;
    spi_device_polling_transmit(s_lora_spi, &t);
#else
    (void)opcode;
    (void)params;
    (void)len;
#endif
}

void esp32c6_lora_default_config(Esp32C6LoraConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1;
    cfg->freq_hz = 433775000UL;
    cfg->sf = 9U;
    cfg->bw_idx = 0U;
    cfg->cr = 5U;
    cfg->tx_power_dbm = 17;
    cfg->preamble_len = 8U;
    cfg->sync_word = 0x1424U;
}

int esp32c6_lora_init(const Esp32C6LoraConfig* cfg) {
    if (cfg) s_lora_cfg = *cfg;
    else esp32c6_lora_default_config(&s_lora_cfg);

    memset(&s_lora_stats, 0, sizeof(s_lora_stats));
    s_lora_stats.last_rssi_dbm = -112;
    s_lora_stats.last_snr_x4 = 0;

    /* Put PI4IO SX1262 reset low then release high, enable RX LNA + ANT_SW */
    esp32c6_board_set_lora_rf_path(1, 1, 0);
    esp32c6_board_set_lora_rf_path(1, 1, 1);

#ifdef ESP_PLATFORM
    if (!s_lora_spi) {
        spi_device_interface_config_t dcfg;
        memset(&dcfg, 0, sizeof(dcfg));
        dcfg.clock_speed_hz = 2000000;
        dcfg.mode = 0;
        dcfg.spics_io_num = static_cast<int>(OYK_C6L_LORA_CS_PIN);
        dcfg.queue_size = 4;
        spi_bus_add_device(SPI2_HOST, &dcfg, &s_lora_spi);
    }
    gpio_set_direction(static_cast<gpio_num_t>(OYK_C6L_LORA_BUSY_PIN), GPIO_MODE_INPUT);
    gpio_set_direction(static_cast<gpio_num_t>(OYK_C6L_LORA_DIO1_PIN), GPIO_MODE_INPUT);

    /* SX1262 basic standby + LoRa packet type + RF frequency setup */
    oyk_u8 stby = 0x00U; /* STDBY_RC */
    sx1262_cmd(0x80U, &stby, 1);
    oyk_u8 ptype = 0x01U; /* PACKET_TYPE_LORA */
    sx1262_cmd(0x8AU, &ptype, 1);
    oyk_u32 fstep = static_cast<oyk_u32>(
        (static_cast<unsigned long long>(s_lora_cfg.freq_hz) << 25) / 32000000ULL);
    oyk_u8 fbuf[4];
    fbuf[0] = static_cast<oyk_u8>((fstep >> 24) & 0xFFU);
    fbuf[1] = static_cast<oyk_u8>((fstep >> 16) & 0xFFU);
    fbuf[2] = static_cast<oyk_u8>((fstep >> 8) & 0xFFU);
    fbuf[3] = static_cast<oyk_u8>(fstep & 0xFFU);
    sx1262_cmd(0x86U, fbuf, 4);
    oyk_u8 rxcont[3] = { 0xFFU, 0xFFU, 0xFFU };
    sx1262_cmd(0x82U, rxcont, 3);
#else
    sx1262_cmd(0x80U, NULL, 0);
#endif

    s_lora_stats.initialized = 1;
    s_lora_stats.rx_active = 1;
    sprintf(s_lora_stats.status_text, "SX1262 RX %.3fM SF%u",
            static_cast<double>(s_lora_cfg.freq_hz) / 1000000.0,
            static_cast<unsigned int>(s_lora_cfg.sf));
    return 1;
}

void esp32c6_lora_deinit() {
    esp32c6_board_set_lora_rf_path(0, 0, 1);
    s_lora_stats.initialized = 0;
    s_lora_stats.rx_active = 0;
    s_lora_stats.tx_active = 0;
    strcpy(s_lora_stats.status_text, "SX1262 STANDBY");
}

void esp32c6_lora_poll() {
    if (!s_lora_stats.initialized) return;
}

void esp32c6_lora_set_rx_callback(Esp32C6LoraRxFn cb, void* ctx) {
    s_lora_rx_cb = cb;
    s_lora_rx_ctx = ctx;
}

int esp32c6_lora_send_kiss(const oyk_u8* kiss_bytes, size_t len) {
    if (!s_lora_stats.initialized || !kiss_bytes || len == 0 || len > 255U) return 0;
    s_lora_stats.tx_active = 1;
    esp32c6_board_set_lora_rf_path(0, 1, 1); /* Disable LNA during TX */
    esp32c6_board_set_rgb_led(C6L_LED_TX, 64U, 12U, 0U);
#ifdef ESP_PLATFORM
    if (s_lora_spi) {
        oyk_u8 tx_to[3] = { 0x00U, 0x00U, 0x00U };
        sx1262_cmd(0x83U, tx_to, 3);
        oyk_u8 rxcont[3] = { 0xFFU, 0xFFU, 0xFFU };
        sx1262_cmd(0x82U, rxcont, 3);
    }
#endif
    ++s_lora_stats.tx_packets;
    s_lora_stats.tx_active = 0;
    esp32c6_board_set_lora_rf_path(1, 1, 1); /* Back to RX LNA */
    esp32c6_board_set_rgb_led(C6L_LED_IDLE, 0U, 24U, 48U);
    return 1;
}

void esp32c6_lora_sim_inject_rx(const oyk_u8* kiss_bytes, size_t len, int rssi_dbm, int snr_x4) {
    if (!s_lora_stats.initialized || !kiss_bytes || len == 0) return;
    ++s_lora_stats.rx_packets;
    s_lora_stats.last_rssi_dbm = rssi_dbm;
    s_lora_stats.last_snr_x4 = snr_x4;
    esp32c6_board_set_rgb_led(C6L_LED_RX, 0U, 56U, 16U);
    if (s_lora_rx_cb) s_lora_rx_cb(kiss_bytes, len, s_lora_rx_ctx);
    esp32c6_board_set_rgb_led(C6L_LED_IDLE, 0U, 24U, 48U);
}

const Esp32C6LoraConfig* esp32c6_lora_config() { return &s_lora_cfg; }
const Esp32C6LoraStats*  esp32c6_lora_stats()  { return &s_lora_stats; }
