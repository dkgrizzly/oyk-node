/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "board_pico.hpp"
#include "pico_psram.hpp"
#include "pico_fs.hpp"
#include "pico_radio.hpp"
#include "pico_net_bridge.hpp"
#include "pico_console.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct PicoCapture {
    char buf[16384];
    size_t len;
};

static size_t pico_cap_tx(const char* data, size_t len, void* ctx) {
    PicoCapture* c = static_cast<PicoCapture*>(ctx);
    if (!c || !data || len == 0) return 0;
    size_t room = sizeof(c->buf) - 1 - c->len;
    size_t n = (len < room) ? len : room;
    if (n > 0) {
        memcpy(c->buf + c->len, data, n);
        c->len += n;
        c->buf[c->len] = '\0';
    }
    return len;
}

static size_t s_uart_tx_bytes[2] = {0, 0};
static void sim_uart_tx(int port, const oyk_u8* /*data*/, size_t len, void* /*ctx*/) {
    if (port >= 0 && port < 2) s_uart_tx_bytes[port] += len;
}

static void test_all_board_profiles_and_mac() {
    const PicoBoardProfile* pw = pico_board_profile_by_id("pico_w");
    const PicoBoardProfile* p2w = pico_board_profile_by_id("pico2_w");
    const PicoBoardProfile* ws = pico_board_profile_by_id("waveshare_core2350b");
    const PicoBoardProfile* pga = pico_board_profile_by_id("pimoroni_pga2350");

    assert(pw && pw->mcu == PICO_MCU_RP2040 && pw->radio_mode == PICO_RADIO_ONBOARD_CYW43);
    assert(p2w && p2w->mcu == PICO_MCU_RP2350A && p2w->radio_mode == PICO_RADIO_ONBOARD_CYW43);
    assert(ws && ws->mcu == PICO_MCU_RP2350B && ws->radio_mode == PICO_RADIO_OPTIONAL_RM2 && ws->psram_cs_pin == 47);
    assert(pga && pga->mcu == PICO_MCU_RP2350B && pga->radio_mode == PICO_RADIO_OPTIONAL_RM2 && pga->psram_cs_pin == 47);

    assert(OYK_PICO_UART_COUNT == 2);
    assert(strcmp(pico_uart_info(0)->name, "ser0") == 0);
    assert(strcmp(pico_uart_info(1)->name, "ser1") == 0);
    assert(pico_uart_info(2) == NULL);

    oyk_u8 mac[6];
    pico_board_read_mac(mac);
    assert((mac[0] & 0x02U) != 0);
    printf("[PASS] test_all_board_profiles_and_mac\n");
}

static void test_psram_detection_and_sram_fallback() {
    pico_board_set_sim_profile("waveshare_core2350b");
    pico_psram_set_sim_available(1, 8UL * 1024UL * 1024UL, "Sim QMI PSRAM (8 MB)");
    assert(pico_psram_init_and_test() == 1);
    assert(pico_psram_info()->status == PICO_PSRAM_READY);
    void* p = pico_psram_alloc(1024);
    assert(p != NULL);
    assert(pico_psram_info()->pool_used_bytes >= 1024);
    pico_psram_free(p, 1024);

    char short_s[8];
    pico_psram_format_short(pico_psram_info(), short_s, sizeof(short_s));
    assert(strcmp(short_s, "8M") == 0);

    pico_board_set_sim_profile("pico_w");
    pico_psram_set_sim_available(0, 0UL, NULL);
    assert(pico_psram_init_and_test() == 0);
    assert(pico_psram_info()->status == PICO_PSRAM_ABSENT);
    void* s = pico_psram_alloc(256);
    assert(s != NULL);
    assert(pico_psram_info()->sram_fallback_used_bytes == 256);
    pico_psram_free(s, 256);
    printf("[PASS] test_psram_detection_and_sram_fallback\n");
}

static void test_storage_sd_flash_fallback_and_format_guard() {
    pico_board_set_sim_profile("pimoroni_pga2350");
    assert(pico_fs_init(1, 0) == 1);
    assert(pico_fs_status()->active_medium == PICO_FS_FLASH_KV);

    assert(pico_fs_init(1, 1) == 1);
    assert(pico_fs_status()->active_medium == PICO_FS_SD_SPI);

    assert(pico_fs_write_text_file("node.cfg", "[node]\ncallsign = KR4OYK-7\n") == 1);
    char buf[128];
    assert(pico_fs_read_text_file("node.cfg", buf, sizeof(buf)) == 1);
    assert(strstr(buf, "KR4OYK-7") != NULL);

    assert(pico_fs_format_storage("YES") == 0);
    assert(pico_fs_exists("node.cfg") == 1);
    assert(pico_fs_format_storage("FORMAT") == 1);
    assert(pico_fs_exists("node.cfg") == 0);
    printf("[PASS] test_storage_sd_flash_fallback_and_format_guard\n");
}

static void test_rm2_missing_non_fatal_and_2uart_operation() {
    pico_fs_init(1, 0);

    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    cfg->iface_count = 4;
    strcpy(cfg->ifaces[0].id, "wifi0"); strcpy(cfg->ifaces[0].type, "axudp"); cfg->ifaces[0].port = 10093;
    strcpy(cfg->ifaces[1].id, "ppp0"); strcpy(cfg->ifaces[1].type, "ppp"); strcpy(cfg->ifaces[1].device, "ser0");
    strcpy(cfg->ifaces[1].host, "10.44.0.1"); cfg->ifaces[1].baud = 115200UL;
    strcpy(cfg->ifaces[2].id, "kiss0"); strcpy(cfg->ifaces[2].type, "kiss-serial"); strcpy(cfg->ifaces[2].device, "ser1");
    cfg->ifaces[2].baud = 9600UL;
    strcpy(cfg->ifaces[3].id, "bt0"); strcpy(cfg->ifaces[3].type, "kiss-bt");

    OykPortableNode* node = new OykPortableNode();
    PicoNetBridge* net = new PicoNetBridge();
    char err[96];
    assert(node->init(cfg, PicoNetBridge::tx_callback_trampoline, net, err, sizeof(err)) == 1);

    /* Waveshare Core2350B WITHOUT RM2 module attached MUST NOT hard fail! */
    pico_board_set_sim_profile("waveshare_core2350b");
    pico_radio_sim_set_present(0);
    PicoRadioConfig rcfg;
    pico_radio_default_config(&rcfg);
    s_uart_tx_bytes[0] = s_uart_tx_bytes[1] = 0;
    pico_uart_set_sim_tx_hook(sim_uart_tx, NULL);

    net->init(node, &rcfg, 0);

    assert(net->radio_state()->module_present == 0);
    assert(strstr(net->radio_state()->status_text, "non-fatal") != NULL);
    assert(net->active_uart_count() == 2);
    assert(s_uart_tx_bytes[0] > 0);
    assert(strcmp(node->iface(0).status, "NO RM2/WiFi (USB/UART OK)") == 0);

    /* Now test with RM2 PRESENT */
    pico_radio_sim_set_present(1);
    strcpy(rcfg.wifi_ssid, "AmateurRadioNet");
    net->init(node, &rcfg, 1);
    assert(net->radio_state()->module_present == 1);
    assert(net->radio_state()->wifi_enabled == 1);
    assert(net->radio_state()->bt_enabled == 1);

    delete net;
    delete node;
    delete cfg;
    printf("[PASS] test_rm2_missing_non_fatal_and_2uart_operation\n");
}

static void test_pico_console_inquiry_storage_format_and_bootsel() {
    pico_board_set_sim_profile("pico2_w");
    pico_psram_set_sim_available(0, 0, NULL);
    pico_psram_init_and_test();
    pico_fs_init(0, 0);

    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    cfg->iface_count = 1;
    strcpy(cfg->ifaces[0].id, "ppp0"); strcpy(cfg->ifaces[0].type, "ppp"); strcpy(cfg->ifaces[0].device, "ser0");

    OykPortableNode* node = new OykPortableNode();
    PicoNetBridge* net = new PicoNetBridge();
    char err[96];
    assert(node->init(cfg, PicoNetBridge::tx_callback_trampoline, net, err, sizeof(err)) == 1);
    net->init(node, NULL, 1);

    PicoCapture* cap = new PicoCapture();
    cap->len = 0; cap->buf[0] = '\0';
    PicoConsole* con = new PicoConsole();
    con->init(node, net, pico_cap_tx, cap);

    con->on_usb_connect();
    assert(strstr(cap->buf, "\033[c\033[>c") != NULL);
    const char* da_x256 = "\033[>41;354;0c";
    con->feed_input(reinterpret_cast<const oyk_u8*>(da_x256), strlen(da_x256));
    assert(con->term_type() == PICO_TERM_XTERM256);

    con->feed_input(reinterpret_cast<const oyk_u8*>("g"), 1);
    assert(con->view_mode() == PICO_VIEW_STORAGE);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("f"), 1);
    assert(con->prompt_kind() == PICO_PROMPT_CONFIRM_FORMAT);
    assert(strstr(cap->buf, "Type FORMAT") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("FORMAT\r"), 7);
    assert(con->prompt_kind() == PICO_PROMPT_NONE);
    assert(strstr(cap->buf, "Storage formatted.") != NULL);

    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(300UL);
    const int base_bl = pico_board_sim_bootloader_count();
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("uy"), 2);
    assert(strstr(cap->buf, "73 de KR4OYK-7") != NULL);
    con->tick(1000UL);
    assert(pico_board_sim_bootloader_count() == base_bl + 1);

    delete cap;
    delete con;
    delete net;
    delete node;
    delete cfg;
    printf("[PASS] test_pico_console_inquiry_storage_format_and_bootsel\n");
}

int main() {
    test_all_board_profiles_and_mac();
    test_psram_detection_and_sram_fallback();
    test_storage_sd_flash_fallback_and_format_guard();
    test_rm2_missing_non_fatal_and_2uart_operation();
    test_pico_console_inquiry_storage_format_and_bootsel();
    printf("All Pico (RP2040 / RP2350) host tests passed!\n");
    return 0;
}
