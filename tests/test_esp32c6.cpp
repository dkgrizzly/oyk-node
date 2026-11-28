/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"
#include "oyk_yapp.hpp"
#include "oyk_rig.hpp"
#include "oyk_printer.hpp"
#include "oyk_ppp.hpp"
#include "oyk_term.hpp"
#include "oyk_tui.hpp"
#include "oyk_gui.hpp"
#include "oyk_port_parity_tests.hpp"
#include "board_esp32c6.hpp"
#include "esp32c6_lora.hpp"
#include "esp32c6_oled.hpp"
#include "esp32c6_fs.hpp"
#include "esp32c6_radio.hpp"
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "esp32c6_net_bridge.hpp"
#include "esp32c6_console.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct C6lCapture {
    char buf[65536];
    size_t len;
};

static size_t c6l_cap_tx(const char* data, size_t len, void* ctx) {
    C6lCapture* cap = static_cast<C6lCapture*>(ctx);
    if (!cap || !data || len == 0) return 0;
    size_t avail = (cap->len + 1 < sizeof(cap->buf)) ? (sizeof(cap->buf) - 1 - cap->len) : 0;
    size_t n = (len < avail) ? len : avail;
    if (n > 0) {
        memcpy(cap->buf + cap->len, data, n);
        cap->len += n;
        cap->buf[cap->len] = '\0';
    }
    return len;
}

static void c6l_save_capture(const char* name, Esp32C6Console* con, C6lCapture* cap) {
    const char* dir = getenv("OYK_UI_CAPTURE_DIR");
    if (!dir || !dir[0]) dir = getenv("OYK_ESP32C6_CAPTURE_DIR");
    if (!dir || !dir[0] || !cap || !name || !con) return;

    cap->len = 0;
    cap->buf[0] = '\0';
    con->render();

    char path[512];
    sprintf(path, "%s/esp32c6_%s.ansi", dir, name);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(cap->buf, 1, cap->len, f);
        fclose(f);
    }
}

static void test_esp32c6_board_and_peripherals() {
    esp32c6_board_init();
    esp32c6_fs_init();
    esp32c6_oled_init();

    const Esp32C6BoardProfile* prof = esp32c6_current_board_profile();
    assert(strcmp(prof->board_id, "m5stack_unit_c6l") == 0);
    assert(prof->flash_bytes == 16UL * 1024UL * 1024UL);
    assert(prof->spi_sck == 20U && prof->spi_mosi == 21U && prof->spi_miso == 22U);
    assert(prof->lora_cs == 23U && prof->lora_dio1 == 7U && prof->lora_busy == 19U);
    assert(prof->oled_cs == 6U && prof->oled_dc == 18U && prof->oled_rst == 15U);
    assert(prof->i2c_sda == 10U && prof->i2c_scl == 8U && prof->pi4io_addr == 0x43U);
    assert(prof->rgb_led_pin == 2U && prof->buzzer_pin == 11U);
    assert(prof->grove_tx_pin == 5U && prof->grove_rx_pin == 4U);
    assert(esp32c6_uart_info(0)->tx_pin == 5U && esp32c6_uart_info(0)->rx_pin == 4U);

    esp32c6_board_set_lora_rf_path(1, 1, 1);
    assert((esp32c6_board_pi4io_out_reg() & (OYK_C6L_PI4IO_LNA_BIT | OYK_C6L_PI4IO_ANT_BIT | OYK_C6L_PI4IO_NRST_BIT)) ==
           (OYK_C6L_PI4IO_LNA_BIT | OYK_C6L_PI4IO_ANT_BIT | OYK_C6L_PI4IO_NRST_BIT));

    /* User Button P0 debounced press cycles 4 OLED pages */
    assert(esp32c6_oled_page() == C6L_OLED_PAGE_NODE);
    esp32c6_board_sim_set_button(1);
    assert(esp32c6_board_poll_button_press(100UL) == 1);
    esp32c6_oled_next_page();
    assert(esp32c6_oled_page() == C6L_OLED_PAGE_LORA);
    esp32c6_board_sim_set_button(0);
    assert(esp32c6_board_poll_button_press(160UL) == 0);

    esp32c6_board_buzzer_beep(1760U, 25U);
    assert(esp32c6_board_last_buzzer_freq() == 1760U && esp32c6_board_last_buzzer_ms() == 25U);

    /* LittleFS FORMAT confirmation guard */
    assert(esp32c6_fs_write_text_file("node.cfg", "[node]\ncallsign = KR4OYK-7\n") == 1);
    assert(esp32c6_fs_exists("node.cfg") == 1);
    assert(esp32c6_fs_format_storage("NOPE") == 0);
    assert(esp32c6_fs_exists("node.cfg") == 1);
    assert(esp32c6_fs_format_storage("FORMAT") == 1);
    assert(esp32c6_fs_exists("node.cfg") == 0);
    printf("[PASS] test_esp32c6_board_and_peripherals\n");
}

static void test_esp32c6_lora_wifi_ble_bridge_and_oled() {
    OykConfig* cfg = new OykConfig;
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    cfg->iface_count = 4;
    strcpy(cfg->ifaces[0].id, "lora0"); strcpy(cfg->ifaces[0].type, "kiss-serial");
    strcpy(cfg->ifaces[0].device, "lora0");
    strcpy(cfg->ifaces[1].id, "wifi0"); strcpy(cfg->ifaces[1].type, "axudp");
    strcpy(cfg->ifaces[1].host, "0.0.0.0"); cfg->ifaces[1].port = 10093;
    strcpy(cfg->ifaces[2].id, "ppp0"); strcpy(cfg->ifaces[2].type, "ppp");
    strcpy(cfg->ifaces[2].device, "ser0"); cfg->ifaces[2].baud = 115200UL;
    strcpy(cfg->ifaces[3].id, "bt0"); strcpy(cfg->ifaces[3].type, "kiss-bt");

    OykPortableNode* node = new OykPortableNode;
    Esp32C6NetBridge* bridge = new Esp32C6NetBridge;
    char err[96];
    assert(node->init(cfg, Esp32C6NetBridge::tx_callback_trampoline, bridge, err, sizeof(err)) == 1);
    bridge->init(node, NULL, NULL);

    /* Inject SX1262 LoRa KISS SABM frame -> creates live MHEARD + active connection */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    oyk_parse_callsign("KR4OYK-7", &sabm.dst);
    oyk_parse_callsign("W3TRM-1", &sabm.src);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    oyk_u8 raw[320], kiss[650];
    size_t rlen = oyk_ax25_encode(&sabm, raw, sizeof(raw));
    size_t klen = oyk_kiss_encode(0U, raw, rlen, kiss, sizeof(kiss));
    bridge->sim_inject_lora_kiss(kiss, klen, -78, 32);
    assert(esp32c6_lora_stats()->rx_packets == 1);
    assert(node->new_heard_count() >= 1);
    assert(node->total_active_connections() >= 1);

    /* Render all 4 64x48 SSD1306 OLED status pages and ensure non-empty framebuffer */
    Esp32C6OledSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    strcpy(snap.callsign, "KR4OYK-7"); strcpy(snap.alias, "OYKNOD");
    snap.accept_incoming = 1; snap.active_conns = 1;
    snap.lora_freq_hz = 433775000UL; snap.lora_rx_pkts = 1; snap.last_rssi_dbm = -78;
    snap.wifi_connected = 1; snap.bt_enabled = 1; snap.grove_baud = 115200UL;
    strcpy(snap.last_heard, "W3TRM-1");
    for (int pg = 0; pg < C6L_OLED_PAGE_COUNT; ++pg) {
        esp32c6_oled_set_page(pg);
        esp32c6_oled_render(&snap);
        int lit = 0;
        for (int y = 8; y < OYK_C6L_OLED_HEIGHT; ++y) {
            for (int x = 0; x < OYK_C6L_OLED_WIDTH; ++x) lit += esp32c6_oled_pixel(x, y);
        }
        assert(lit > 20);
    }

    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_esp32c6_lora_wifi_ble_bridge_and_oled\n");
}

static void test_esp32c6_ui_flows_and_captures() {
    esp32c6_fs_init();
    esp32c6_oled_init();

    OykConfig* cfg = new OykConfig;
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    strcpy(cfg->theme_name, "dark");
    strcpy(cfg->bbs_files_path, "/tmp/oyk_c6l_fm/files");
    cfg->iface_count = 3;
    strcpy(cfg->ifaces[0].id, "lora0"); strcpy(cfg->ifaces[0].type, "kiss-serial");
    strcpy(cfg->ifaces[0].device, "lora0");
    strcpy(cfg->ifaces[1].id, "wifi0"); strcpy(cfg->ifaces[1].type, "axudp");
    strcpy(cfg->ifaces[1].host, "0.0.0.0"); cfg->ifaces[1].port = 10093;
    strcpy(cfg->ifaces[2].id, "ppp0"); strcpy(cfg->ifaces[2].type, "ppp");
    strcpy(cfg->ifaces[2].device, "ser0"); cfg->ifaces[2].baud = 115200UL;

    cfg->peer_count = 1;
    strcpy(cfg->peers[0].id, "remote0");
    strcpy(cfg->peers[0].callsign, "R3MOT-7");
    strcpy(cfg->peers[0].interface_id, "wifi0");
    strcpy(cfg->peers[0].host, "12.34.56.78");
    cfg->peers[0].port = 10093;
    cfg->peers[0].allow_inbound = 1;
    cfg->peers[0].allow_outbound = 1;

    OykPortableNode* node = new OykPortableNode;
    Esp32C6NetBridge* bridge = new Esp32C6NetBridge;
    char err[96];
    assert(node->init(cfg, Esp32C6NetBridge::tx_callback_trampoline, bridge, err, sizeof(err)) == 1);
    bridge->init(node, NULL, NULL);

    /* Add demo session + MHEARD + auto-learned peer */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    oyk_parse_callsign("KR4OYK-7", &sabm.dst);
    oyk_parse_callsign("W3TRM-1", &sabm.src);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    oyk_u8 raw[320], kiss[650];
    size_t rlen = oyk_ax25_encode(&sabm, raw, sizeof(raw));
    size_t klen = oyk_kiss_encode(0U, raw, rlen, kiss, sizeof(kiss));
    bridge->sim_inject_lora_kiss(kiss, klen, -81, 28);

    OykFrame auto_ui;
    memset(&auto_ui, 0, sizeof(auto_ui));
    oyk_parse_callsign("KR4OYK-7", &auto_ui.dst);
    oyk_parse_callsign("N9LRN-5", &auto_ui.src);
    auto_ui.control = 0x03U;
    auto_ui.has_pid = 1;
    auto_ui.pid = 0xF0U;
    node->on_rx_frame(1, &auto_ui, 0x281E140AUL, 10093U, 0U);
    assert(node->auto_learned_peer_count() >= 1);

    Esp32C6Console* con = new Esp32C6Console;
    C6lCapture* cap = new C6lCapture;
    cap->len = 0; cap->buf[0] = '\0';
    con->init(node, bridge, c6l_cap_tx, cap);
    con->on_usb_connect();
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[?65;22c"), 9);

    /* 1. Main Console */
    cap->len = 0; cap->buf[0] = '\0';
    con->render();
    assert(strstr(cap->buf, "W3TRM-1") != NULL);
    assert(strstr(cap->buf, "Node Activity Log") != NULL);
    c6l_save_capture("main", con, cap);

    /* 2. Closed-menu hotkey rejection + Drop-down Node Menu (Ctrl-N / Esc) */
    con->feed_input(reinterpret_cast<const oyk_u8*>("sbfht?u"), 7);
    assert(con->view_mode() == C6L_VIEW_CONSOLE);
    assert(con->prompt_kind() == C6L_PROMPT_NONE);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e"), 1);
    assert(con->view_mode() == C6L_VIEW_MENU);
    assert(strstr(cap->buf, "Theme...") == NULL);
    assert(strstr(cap->buf, "Peers...") == NULL);
    c6l_save_capture("menu", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("x"), 1);
    assert(con->view_mode() == C6L_VIEW_CONSOLE);

    /* 3. Terminate */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e" "t"), 2);
    assert(strstr(cap->buf, "Terminate Active Connection") != NULL);
    c6l_save_capture("terminate", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e\t"), 2); /* Preempted by Terminate modal */
    assert(con->prompt_kind() == C6L_PROMPT_TERM_SID);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);

    /* 4. Ban */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e" "bB4NTST\r"), 9);
    cap->len = 0; cap->buf[0] = '\0';
    con->render();
    assert(strstr(cap->buf, "Ban / Unban Callsign") != NULL);
    c6l_save_capture("ban", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e\t"), 2); /* Preempted by Ban modal */
    assert(con->prompt_kind() == C6L_PROMPT_BAN_CALL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(300UL);
    assert(con->prompt_kind() == C6L_PROMPT_NONE);

    /* 5. Settings (All 8 Tabs + Wizard 1..4 + Edit Iface + Edit Peer) */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e" "s"), 2);
    assert(con->view_mode() == C6L_VIEW_SETTINGS && con->settings_tab() == 0);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e\033[21~\033[20~"), 11); /* Preempted by Settings */
    assert(con->view_mode() == C6L_VIEW_SETTINGS);
    c6l_save_capture("settings", con, cap);

    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("2"), 1);
    c6l_save_capture("settings_bbs", con, cap);
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("3"), 1);
    c6l_save_capture("settings_chat", con, cap);
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("4"), 1);
    c6l_save_capture("settings_ifaces", con, cap);

    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("n"), 1);
    c6l_save_capture("add_iface_1", con, cap);
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("1\r"), 2);
    c6l_save_capture("add_iface_2", con, cap);
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    assert(strstr(cap->buf, "Same-Iface Fwd") != NULL);
    c6l_save_capture("add_iface_3", con, cap);
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    c6l_save_capture("add_iface_4", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); con->tick(600UL);

    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "Edit Interface") != NULL);
    c6l_save_capture("edit_iface", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); con->tick(900UL);

    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("5"), 1);
    assert(con->settings_tab() == 4);
    c6l_save_capture("peers", con, cap);

    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "Edit Peer") != NULL);
    c6l_save_capture("edit_peer", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); con->tick(1200UL);

    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("6"), 1);
    c6l_save_capture("settings_rig", con, cap);
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("7"), 1);
    c6l_save_capture("settings_printer", con, cap);

    /* 6. Settings Tab 8 Theme + live preview + Esc revert */
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("8"), 1);
    assert(con->settings_tab() == 7);
    assert(strstr(cap->buf, "Active Theme:") != NULL);
    c6l_save_capture("settings_theme", con, cap);
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("\033[C"), 3);
    assert(strstr(cap->buf, "light") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); con->tick(1500UL);
    assert(con->view_mode() == C6L_VIEW_CONSOLE);
    assert(strcmp(node->config().theme_name, "light") != 0);

    /* 7. Local Terminal */
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("\t"), 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "b"), 2);
    assert(strstr(cap->buf, "LOCAL TERMINAL") != NULL);
    assert(strstr(cap->buf, "Ctrl-A Z:Help") != NULL);
    assert(strstr(cap->buf, "Tab/F9:Console") != NULL);
    assert(strstr(cap->buf, "Terminal Output") == NULL);
    c6l_save_capture("terminal", con, cap);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "z"), 2);
    assert(strstr(cap->buf, "Terminal Hotkey Help") != NULL);
    c6l_save_capture("terminal_help", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e\t\033[20~"), 7); /* Preempted by Help overlay */
    assert(con->view_mode() == C6L_VIEW_LOCAL_TERM);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); con->tick(1950UL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\t"), 1);
    assert(con->view_mode() == C6L_VIEW_CONSOLE);

    /* 8. Files (Dual-pane jailed navigation) */
    {
        char orig_cwd[256];
        orig_cwd[0] = '\0';
        const char* cap_dir = getenv("OYK_UI_CAPTURE_DIR");
        char abs_cap[512];
        abs_cap[0] = '\0';
#ifndef _WIN32
        if (getcwd(orig_cwd, sizeof(orig_cwd)) == NULL) orig_cwd[0] = '\0';
        if (cap_dir && cap_dir[0] && cap_dir[0] != '/' && orig_cwd[0]) {
            snprintf(abs_cap, sizeof(abs_cap), "%s/%s", orig_cwd, cap_dir);
            setenv("OYK_UI_CAPTURE_DIR", abs_cap, 1);
        }
        mkdir("/tmp/oyk_c6l_fm", 0755);
        mkdir("/tmp/oyk_c6l_fm/incoming", 0755);
        mkdir("/tmp/oyk_c6l_fm/incoming/W3TRM-1", 0755);
        mkdir("/tmp/oyk_c6l_fm/files", 0755);
        mkdir("/tmp/oyk_c6l_fm/files/bulletins", 0755);
        if (FILE* sf = fopen("/tmp/oyk_c6l_fm/incoming/W3TRM-1/fieldday.txt", "wb")) {
            fputs("Field Day Log 2026\n", sf); fclose(sf);
        }
        if (FILE* sf = fopen("/tmp/oyk_c6l_fm/files/bulletins/welcome.txt", "wb")) {
            fputs("Welcome to OYK BBS\n", sf); fclose(sf);
        }
        assert(chdir("/tmp/oyk_c6l_fm") == 0);
#endif
        cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e" "f"), 2);
        assert(strstr(cap->buf, "Manage BBS Files Area") != NULL);
        assert(strstr(cap->buf, "W3TRM-1") != NULL);
        assert(strstr(cap->buf, "bulletins") != NULL);
        assert(strstr(cap->buf, "<UP>") == NULL);
        c6l_save_capture("files", con, cap);
        cap->len = 0; cap->buf[0] = '\0';
        con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
        assert(strstr(cap->buf, "<UP>") != NULL);
        assert(strstr(cap->buf, "fieldday.txt") != NULL);
        con->feed_input(reinterpret_cast<const oyk_u8*>("\x08"), 1);
        con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); con->tick(2100UL);
        assert(con->view_mode() == C6L_VIEW_CONSOLE);
#ifndef _WIN32
        if (orig_cwd[0]) assert(chdir(orig_cwd) == 0);
        if (abs_cap[0]) setenv("OYK_UI_CAPTURE_DIR", cap_dir, 1);
#endif
    }

    /* 9. About */
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e?"), 2);
    assert(con->view_mode() == C6L_VIEW_ABOUT);
    c6l_save_capture("about", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("q"), 1);
    assert(con->view_mode() == C6L_VIEW_CONSOLE);

    /* 10. Dynamic CPR resize check */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[36;120R"), 9);
    assert(con->term_rows() == 36 && con->term_cols() == 120);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[25;80R"), 8);
    assert(con->term_rows() == 25 && con->term_cols() == 80);

    /* 11. Bootloader / Auto-Learned Peers exit */
    cap->len = 0; cap->buf[0] = '\0'; con->feed_input(reinterpret_cast<const oyk_u8*>("\x0e" "u"), 2);
    assert(strstr(cap->buf, "Auto-Learned Peers") != NULL);
    c6l_save_capture("exit", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("n"), 1);

    delete cap;
    delete con;
    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_esp32c6_ui_flows_and_captures\n");
}

int main() {
    OykPortableNode* parity_node = new OykPortableNode;
    int fails = 0;
    assert(oyk_run_shared_port_parity_tests(parity_node, stdout, &fails) == 1);
    assert(fails == 0);
    delete parity_node;
    test_esp32c6_board_and_peripherals();
    test_esp32c6_lora_wifi_ble_bridge_and_oled();
    test_esp32c6_ui_flows_and_captures();
    printf("ALL ESP32-C6 UNIT TESTS PASSED.\n");
    return 0;
}
