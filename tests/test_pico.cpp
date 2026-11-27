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
#include <stdlib.h>
#include <string.h>

struct PicoCapture {
    char buf[65536];
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

    /* Drop-down Node menu (M) matches ncurses/DOS + shaded drop shadow + live theme preview */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("m"), 1);
    assert(con->view_mode() == PICO_VIEW_MENU);
    assert(strstr(cap->buf, " Node ") != NULL);
    assert(strstr(cap->buf, "Node Menu") != NULL);
    assert(strstr(cap->buf, "\033[0;90;40m") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("h"), 1);
    assert(con->prompt_kind() == PICO_PROMPT_THEME);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[C"), 3); /* Right -> live preview light */
    assert(strstr(cap->buf, "light") != NULL);
    assert(strstr(cap->buf, "48;5;231m") != NULL || strstr(cap->buf, "38;5;") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); /* Esc reverts */
    con->tick(300UL);
    assert(con->prompt_kind() == PICO_PROMPT_NONE);
    assert(strcmp(node->config().theme_name, "light") != 0);

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
    con->tick(600UL);
    const int base_bl = pico_board_sim_bootloader_count();
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("uy"), 2);
    assert(strstr(cap->buf, "73 de KR4OYK-7") != NULL);
    con->tick(1300UL);
    assert(pico_board_sim_bootloader_count() == base_bl + 1);

    delete cap;
    delete con;
    delete net;
    delete node;
    delete cfg;
    printf("[PASS] test_pico_console_inquiry_storage_format_and_bootsel\n");
}

static void pico_save_capture(const char* name, PicoConsole* con, PicoCapture* cap) {
    const char* dir = getenv("OYK_UI_CAPTURE_DIR");
    if (!dir || !dir[0]) return;
    cap->len = 0;
    cap->buf[0] = '\0';
    con->render();
    char path[512];
    sprintf(path, "%s/pico_%s.ansi", dir, name);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(cap->buf, 1, cap->len, f);
        fclose(f);
    }
}

static void pico_ui_local_cb(oyk_u32 sid, int ev, const oyk_u8* data, size_t len, const char* r, void* ctx) {
    PicoConsole* con = static_cast<PicoConsole*>(ctx);
    if (con) con->on_local_term_event(sid, ev, data, len, r);
}

static void test_pico_ui_flows_and_captures() {
    pico_board_set_sim_profile("pico2_w");
    pico_psram_set_sim_available(0, 0, NULL);
    pico_psram_init_and_test();
    pico_fs_init(1, 0);

    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");
    cfg->peer_count = 1;
    strcpy(cfg->peers[0].id, "remote0");
    strcpy(cfg->peers[0].callsign, "R3MOT-7");
    strcpy(cfg->peers[0].description, "Remote PoPT Node");
    strcpy(cfg->peers[0].interface_id, "axudp0");
    strcpy(cfg->peers[0].host, "12.34.56.78");
    cfg->peers[0].port = 10093;
    cfg->peers[0].allow_inbound = 1;
    cfg->peers[0].allow_outbound = 1;

    OykPortableNode* node = new OykPortableNode();
    PicoNetBridge* net = new PicoNetBridge();
    char err[96];
    assert(node->init(cfg, PicoNetBridge::tx_callback_trampoline, net, err, sizeof(err)) == 1);
    net->init(node, NULL, 1);

    PicoCapture* cap = new PicoCapture();
    cap->len = 0; cap->buf[0] = '\0';
    PicoConsole* con = new PicoConsole();
    con->init(node, net, pico_cap_tx, cap);
    node->set_local_event_callback(pico_ui_local_cb, con);
    con->tick(500UL);
    con->on_usb_connect();
    const char* da_x256 = "\033[>41;354;0c";
    con->feed_input(reinterpret_cast<const oyk_u8*>(da_x256), strlen(da_x256));

    /* Connect an inbound AXUDP peer to populate Active Connections, Heard, and Auto-Learned */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    sabm.dst = cfg->callsign;
    assert(oyk_parse_callsign("W3TRM-1", &sabm.src) == 1);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    node->on_rx_frame(0, &sabm, 0x0A000005UL, 10093U, 0);
    assert(node->total_active_connections() == 1);
    assert(node->auto_learned_peer_count() == 1);

    /* 1. Main Console */
    cap->len = 0; cap->buf[0] = '\0';
    con->render();
    assert(strstr(cap->buf, "W3TRM-1") != NULL);
    assert(strstr(cap->buf, "Node Activity Log") != NULL);
    pico_save_capture("main", con, cap);

    /* 1b. Drop-down Node Menu */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("m"), 1);
    assert(con->view_mode() == PICO_VIEW_MENU);
    pico_save_capture("menu", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("x"), 1);
    assert(con->view_mode() == PICO_VIEW_CONSOLE);

    /* 2. Terminate Connection modal */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("t"), 1);
    assert(strstr(cap->buf, "Terminate Active Connection") != NULL);
    assert(strstr(cap->buf, "W3TRM-1") != NULL);
    pico_save_capture("terminate", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    assert(node->total_active_connections() == 0);

    /* 3. Ban / Unban Callsign modal */
    con->feed_input(reinterpret_cast<const oyk_u8*>("bB4NTST\r"), 8);
    assert(node->ban_count() >= 1);
    cap->len = 0; cap->buf[0] = '\0';
    con->render();
    assert(strstr(cap->buf, "Ban / Unban Callsign") != NULL);
    assert(strstr(cap->buf, "Active Bans (1):") != NULL);
    pico_save_capture("ban", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(700UL);

    /* 4. Settings (All 7 Canonical Tabs) */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    assert(con->view_mode() == PICO_VIEW_SETTINGS && con->settings_tab() == 0);
    assert(strstr(cap->buf, "Node Settings") != NULL);
    assert(strstr(cap->buf, " Node ") != NULL);
    assert(strstr(cap->buf, "7:Prn") != NULL);
    pico_save_capture("settings", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("2"), 1);
    assert(con->settings_tab() == 1);
    pico_save_capture("settings_bbs", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("3"), 1);
    assert(con->settings_tab() == 2);
    pico_save_capture("settings_chat", con, cap);

    /* 5. Settings Ifaces & Add Interface Wizard (Steps 1..4) */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("4"), 1);
    assert(con->settings_tab() == 3);
    pico_save_capture("settings_ifaces", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("n"), 1);
    assert(strstr(cap->buf, "Add Interface") != NULL);
    pico_save_capture("add_iface_1", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("1\r"), 2);
    assert(strstr(cap->buf, "Add Interface") != NULL);
    pico_save_capture("add_iface_2", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    assert(strstr(cap->buf, "Add Interface") != NULL);
    assert(strstr(cap->buf, "Same-Iface Fwd") != NULL);
    pico_save_capture("add_iface_3", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    assert(strstr(cap->buf, "Add Interface") != NULL);
    pico_save_capture("add_iface_4", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(900UL);

    /* 6. Edit Interface modal */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "Edit Interface") != NULL);
    assert(strstr(cap->buf, "Same-Iface Fwd") != NULL);
    pico_save_capture("edit_iface", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1100UL);

    /* 7. Peers Tab */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("5"), 1);
    assert(con->settings_tab() == 4);
    assert(strstr(cap->buf, "R3MOT-7") != NULL);
    pico_save_capture("peers", con, cap);

    /* 8. Edit Peer Modal */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "Edit Peer") != NULL);
    assert(strstr(cap->buf, "Allow Inbound") != NULL);
    assert(strstr(cap->buf, "Allow Outbound") != NULL);
    pico_save_capture("edit_peer", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1300UL);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("6"), 1);
    assert(con->settings_tab() == 5);
    pico_save_capture("settings_rig", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("7"), 1);
    assert(con->settings_tab() == 6);
    pico_save_capture("settings_printer", con, cap);

    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1500UL);
    assert(con->view_mode() == PICO_VIEW_CONSOLE);

    /* 8b. Theme Settings Modal & whole-screen live preview (dark -> light -> Esc revert) */
    OykThemeConfig dt_pal, lt_pal;
    oyk_theme_init_builtin(&dt_pal, 0);
    oyk_theme_init_builtin(&lt_pal, 1);
    char dark_menu_sgr[32], light_menu_sgr[32], dark_bdr_sgr[32], light_bdr_sgr[32];
    sprintf(dark_menu_sgr, "\033[38;5;%d;48;5;%dm",
            oyk_rgb_to_xterm256(dt_pal.colors[OYK_TR_BUTTON_FG]),
            oyk_rgb_to_xterm256(dt_pal.colors[OYK_TR_BUTTON_BG]));
    sprintf(light_menu_sgr, "\033[38;5;%d;48;5;%dm",
            oyk_rgb_to_xterm256(lt_pal.colors[OYK_TR_BUTTON_FG]),
            oyk_rgb_to_xterm256(lt_pal.colors[OYK_TR_BUTTON_BG]));
    sprintf(dark_bdr_sgr, "\033[38;5;%dm", oyk_rgb_to_xterm256(dt_pal.colors[OYK_TR_BORDER]));
    sprintf(light_bdr_sgr, "\033[38;5;%dm", oyk_rgb_to_xterm256(lt_pal.colors[OYK_TR_BORDER]));

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("h"), 1);
    assert(strstr(cap->buf, "Theme Settings") != NULL);
    assert(strstr(cap->buf, dark_menu_sgr) != NULL);
    assert(strstr(cap->buf, dark_bdr_sgr) != NULL);
    pico_save_capture("theme", con, cap);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[C"), 3); /* dark -> light */
    assert(strstr(cap->buf, "Previewing 'light'") != NULL);
    assert(strstr(cap->buf, light_menu_sgr) != NULL);
    assert(strstr(cap->buf, light_bdr_sgr) != NULL);
    assert(strstr(cap->buf, dark_menu_sgr) == NULL);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1700UL);
    assert(strstr(cap->buf, dark_menu_sgr) != NULL); /* reverted cleanly to dark */
    assert(strstr(cap->buf, dark_bdr_sgr) != NULL);

    /* 8b2. Local Terminal: Ctrl+A B BBS, Help overlay, EOL cycle, and CHAT / passthrough */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\t\x01" "b"), 3);
    assert(con->view_mode() == PICO_VIEW_LOCAL_TERM);
    assert(strstr(cap->buf, "LOCAL TERMINAL") != NULL);
    assert(strstr(cap->buf, "[EOL:CR]") != NULL);
    assert(strstr(cap->buf, "Packet BBS") != NULL);
    assert(strstr(cap->buf, "de KR4OYK-11") != NULL);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("?\r"), 2);
    pico_save_capture("terminal", con, cap);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "z"), 2);
    assert(strstr(cap->buf, "Terminal Hotkey Help") != NULL);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "[EOL:CR*]") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "e"), 2);
    assert(strstr(cap->buf, "[EOL:LF*]") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "e"), 2);
    assert(strstr(cap->buf, "[EOL:CRLF*]") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "e"), 2);
    assert(strstr(cap->buf, "[EOL:CR]") != NULL);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "t"), 2);
    assert(strstr(cap->buf, "Chat") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("/h\r"), 3);
    assert(strstr(cap->buf, "Chat Commands:") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("/u\r"), 3);
    assert(strstr(cap->buf, "Chat Users:") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1900UL);
    assert(con->view_mode() == PICO_VIEW_CONSOLE);

    /* 8b3. Files Area Manager */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("f"), 1);
    assert(con->view_mode() == PICO_VIEW_FILES);
    pico_save_capture("files", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(2100UL);
    assert(con->view_mode() == PICO_VIEW_CONSOLE);

    /* 8b2. About OYK-Node modal */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("?"), 1);
    assert(con->view_mode() == PICO_VIEW_ABOUT);
    pico_save_capture("about", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("q"), 1);
    assert(con->view_mode() == PICO_VIEW_CONSOLE);

    /* 8c. Mouse & dynamic CPR size verification: VT100 must NOT enable mouse, emits \033[6n probe,
     * SGR left click + wheel + X10 right-click navigate menus, [X] closes Settings, CPR resizes display. */
    {
        PicoConsole* vt_con = new PicoConsole;
        PicoCapture* vt_cap = new PicoCapture;
        vt_con->init(node, net, pico_cap_tx, vt_cap);
        vt_con->on_usb_connect();
        assert(strstr(vt_cap->buf, "\033[?1000h") == NULL);
        assert(strstr(vt_cap->buf, "\033[6n") != NULL);
        delete vt_cap;
        delete vt_con;
    }
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[<0;4;1M"), 9); /* Click ' Node ' */
    assert(con->view_mode() == PICO_VIEW_MENU);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[<65;8;5M"), 10); /* Wheel down */
    assert(con->view_mode() == PICO_VIEW_MENU);
    const unsigned char x10_rclick[6] = { 0x1B, '[', 'M', 32 + 2, 32 + 8, 32 + 5 };
    con->feed_input(x10_rclick, 6);
    assert(con->view_mode() == PICO_VIEW_CONSOLE);

    /* Open Settings, verify [X] rendered, and close via SGR click on [X] (row 3, col 74 1-based) */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    assert(con->view_mode() == PICO_VIEW_SETTINGS);
    assert(strstr(cap->buf, "[X]") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[<0;74;3M"), 10);
    assert(con->view_mode() == PICO_VIEW_CONSOLE);

    /* Verify dynamic CPR terminal size probe response scales to 120x36 and back to 80x25 */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[36;120R"), 9);
    assert(con->term_rows() == 36 && con->term_cols() == 120);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[25;80R"), 8);
    assert(con->term_rows() == 25 && con->term_cols() == 80);

    /* 9. Auto-Learned Peers BOOTSEL modal */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("u"), 1);
    assert(con->view_mode() == PICO_VIEW_BOOTLOADER);
    assert(strstr(cap->buf, "Auto-Learned Peers") != NULL);
    pico_save_capture("exit", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("d"), 1);
    assert(strstr(cap->buf, "\033[?1006l\033[?1000l") != NULL);
    con->tick(2300UL);

    delete cap;
    delete con;
    delete net;
    delete node;
    delete cfg;
    printf("[PASS] test_pico_ui_flows_and_captures\n");
}

int main() {
    test_all_board_profiles_and_mac();
    test_psram_detection_and_sram_fallback();
    test_storage_sd_flash_fallback_and_format_guard();
    test_rm2_missing_non_fatal_and_2uart_operation();
    test_pico_console_inquiry_storage_format_and_bootsel();
    test_pico_ui_flows_and_captures();
    printf("All Pico (RP2040 / RP2350) host tests passed!\n");
    return 0;
}
