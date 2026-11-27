/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "board_lyra.hpp"
#include "lyra_fs.hpp"
#include "lyra_radio.hpp"
#include "lyra_net_bridge.hpp"
#include "lyra_console.hpp"
#include "lyra_picocalc_console.hpp"
#include "oyk_port_parity_tests.hpp"

#include "board_pico.hpp"
#include "pico_psram.hpp"
#include "pico_fs.hpp"
#include "pico_radio.hpp"
#include "pico_net_bridge.hpp"
#include "pico_console.hpp"
#include "pico_picocalc_console.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

struct LyraCapture {
    char buf[65536];
    size_t len;
};

static size_t lyra_cap_tx(const char* data, size_t len, void* ctx) {
    LyraCapture* c = static_cast<LyraCapture*>(ctx);
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

static size_t s_lyra_uart_tx[2] = {0, 0};
static void sim_lyra_uart_tx(int port, const oyk_u8* /*data*/, size_t len, void* /*ctx*/) {
    if (port >= 0 && port < 2) s_lyra_uart_tx[port] += len;
}

static void lyra_ui_local_cb(oyk_u32 sid, int event_type,
                             const oyk_u8* data, size_t len,
                             const char* reason, void* ctx) {
    LyraConsole* con = static_cast<LyraConsole*>(ctx);
    if (con) con->on_local_term_event(sid, event_type, data, len, reason);
}

static void pico_parity_local_cb(oyk_u32 sid, int event_type,
                                 const oyk_u8* data, size_t len,
                                 const char* reason, void* ctx) {
    PicoConsole* con = static_cast<PicoConsole*>(ctx);
    if (con) con->on_local_term_event(sid, event_type, data, len, reason);
}

static void lyra_save_ansi(const char* flow, LyraCapture* cap, LyraConsole* con) {
    const char* dir = getenv("OYK_LYRA_CAPTURE_DIR");
    if (!dir || !dir[0] || !cap || !con) return;
    cap->len = 0;
    cap->buf[0] = '\0';
    con->render();
    char path[512];
    sprintf(path, "%s/lyra_%s.ansi", dir, flow);
    FILE* f = fopen(path, "wb");
    assert(f != NULL);
    if (cap->len > 0) assert(fwrite(cap->buf, 1, cap->len, f) == cap->len);
    fclose(f);
}

static void read_vt_row(const OykVt100* vt, int row, char out[81]) {
    for (int c = 0; c < 80; ++c) {
        const OykVtCell* cell = oyk_vt100_cell_at(vt, static_cast<int16_t>(row), static_cast<int16_t>(c));
        uint16_t g = cell ? cell->glyph : ' ';
        out[c] = (g >= 0x20U && g <= 0x7EU) ? static_cast<char>(g) : ' ';
    }
    out[80] = '\0';
}

static void assert_pixel_box_equal(const char* screen_name,
                                   const LyraPicoCalcLcd* llcd,
                                   const PicoCalcLcd* plcd,
                                   int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            oyk_u16 lp = llcd->sample_pixel(static_cast<oyk_u16>(x), static_cast<oyk_u16>(y));
            oyk_u16 pp = plcd->sample_pixel(static_cast<oyk_u16>(x), static_cast<oyk_u16>(y));
            if (lp != pp) {
                fprintf(stderr, "Pixel mismatch in '%s' at (%d,%d): lyra=0x%04X pico=0x%04X\n",
                        screen_name, x, y, static_cast<unsigned int>(lp), static_cast<unsigned int>(pp));
            }
            assert(lp == pp);
        }
    }
}

static void assert_vt_rows_equal(const char* screen_name,
                                 const OykVt100* lvt,
                                 const OykVt100* pvt,
                                 int r0, int r1) {
    for (int r = r0; r <= r1; ++r) {
        for (int c = 0; c < lvt->cols && c < pvt->cols; ++c) {
            const OykVtCell* lc = oyk_vt100_cell_at(lvt, static_cast<int16_t>(r), static_cast<int16_t>(c));
            const OykVtCell* pc = oyk_vt100_cell_at(pvt, static_cast<int16_t>(r), static_cast<int16_t>(c));
            assert(lc && pc);
            if (lc->glyph != pc->glyph || lc->fg != pc->fg || lc->bg != pc->bg) {
                fprintf(stderr, "VT cell mismatch in '%s' at (%d,%d): lyra(%u/%lu/%lu) != pico(%u/%lu/%lu)\n",
                        screen_name, r, c,
                        static_cast<unsigned int>(lc->glyph),
                        static_cast<unsigned long>(lc->fg),
                        static_cast<unsigned long>(lc->bg),
                        static_cast<unsigned int>(pc->glyph),
                        static_cast<unsigned long>(pc->fg),
                        static_cast<unsigned long>(pc->bg));
            }
            assert(lc->glyph == pc->glyph && lc->fg == pc->fg && lc->bg == pc->bg);
        }
    }
}

static void test_shared_port_parity() {
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");

    OykPortableNode* node = new OykPortableNode();
    char err[96];
    assert(node->init(cfg, NULL, NULL, err, sizeof(err)) == 1);
    int fails = 0;
    assert(oyk_run_shared_port_parity_tests(node, NULL, &fails) == 1);
    assert(fails == 0);
    delete node;
    delete cfg;
    printf("[PASS] test_shared_port_parity\n");
}

static void test_lyra_board_profiles_and_mac() {
    assert(lyra_board_profile_count() == 7);
    const LyraBoardProfile* l  = lyra_board_profile_by_id("lyra");
    const LyraBoardProfile* lb = lyra_board_profile_by_id("lyra_b");
    const LyraBoardProfile* lp = lyra_board_profile_by_id("lyra_plus");
    const LyraBoardProfile* lw = lyra_board_profile_by_id("lyra_zero_w");
    const LyraBoardProfile* p  = lyra_board_profile_by_id("picocalc_lyra");
    const LyraBoardProfile* pb = lyra_board_profile_by_id("picocalc_lyra_b");
    const LyraBoardProfile* pp = lyra_board_profile_by_id("picocalc_lyra_plus");

    assert(l  && l->soc == LYRA_SOC_RK3506G2  && l->ram_mb == 128 && l->nand_mb == 0   && l->net_kind == LYRA_NET_USB_RNDIS_PPP && l->has_picocalc_hw == 0 && l->uart_count == 2);
    assert(lb && lb->soc == LYRA_SOC_RK3506G2 && lb->ram_mb == 128 && lb->nand_mb == 256 && lb->net_kind == LYRA_NET_USB_RNDIS_PPP && lb->has_picocalc_hw == 0 && lb->uart_count == 2);
    assert(lp && lp->soc == LYRA_SOC_RK3506G2 && lp->ram_mb == 128 && lp->nand_mb == 256 && lp->net_kind == LYRA_NET_RMII_ETH && lp->has_picocalc_hw == 0 && lp->uart_count == 2);
    assert(lw && lw->soc == LYRA_SOC_RK3506B  && lw->ram_mb == 512 && lw->nand_mb == 256 && lw->net_kind == LYRA_NET_WIFI6_BLE && lw->has_picocalc_hw == 0 && lw->uart_count == 2);
    assert(p  && p->has_picocalc_hw == 1  && p->uart_count == 1  && p->lcd_cs_pin == 4  && p->kbd_scl_pin == 10);
    assert(pb && pb->has_picocalc_hw == 1 && pb->uart_count == 1 && pb->nand_mb == 256 && pb->has_audio_default_off == 1);
    assert(pp && pp->has_picocalc_hw == 1 && pp->uart_count == 1 && pp->net_kind == LYRA_NET_RMII_ETH);

    lyra_board_set_sim_profile("lyra_plus");
    assert(lyra_uart_info(0) && strcmp(lyra_uart_info(0)->name, "ser0") == 0);
    assert(lyra_uart_info(1) && strcmp(lyra_uart_info(1)->name, "ser1") == 0);
    assert(lyra_uart_info(2) == NULL);

    lyra_board_set_sim_profile("picocalc_lyra");
    assert(lyra_uart_info(0) && strcmp(lyra_uart_info(0)->name, "ser0") == 0);
    assert(lyra_uart_info(1) == NULL);

    oyk_u8 mac[6];
    lyra_board_read_mac(mac);
    assert((mac[0] & 0x02U) != 0);
    printf("[PASS] test_lyra_board_profiles_and_mac\n");
}

static void test_lyra_storage_sd_nand_fallback_and_format_guard() {
    lyra_board_set_sim_profile("lyra_b");
    assert(lyra_fs_init(1, 1, 1) == 1);
    assert(lyra_fs_status()->active_backend == LYRA_STORE_SD_FATFS);

    assert(lyra_fs_init(1, 0, 1) == 1);
    assert(lyra_fs_status()->active_backend == LYRA_STORE_NAND_LITTLEFS);
    assert(lyra_fs_write_text_file("node.cfg", "callsign = N0CALL-7\n") == 1);
    assert(lyra_fs_exists("node.cfg") == 1);
    char buf[64];
    assert(lyra_fs_read_text_file("node.cfg", buf, sizeof(buf)) == 1);
    assert(strstr(buf, "N0CALL-7") != NULL);

    assert(lyra_fs_format_storage("NOPE") == 0);
    assert(lyra_fs_exists("node.cfg") == 1);
    assert(lyra_fs_format_storage("FORMAT") == 1);
    assert(lyra_fs_exists("node.cfg") == 0);

    lyra_board_set_sim_profile("lyra");
    assert(lyra_fs_init(1, 0, 1) == 0);
    assert(lyra_fs_status()->active_backend == LYRA_STORE_NONE);
    printf("[PASS] test_lyra_storage_sd_nand_fallback_and_format_guard\n");
}

static void test_lyra_networking_and_uart_profiles() {
    lyra_uart_set_sim_tx_hook(sim_lyra_uart_tx, NULL);

    /* Standalone lyra_plus: RMII Ethernet + 2 UARTs (PPP ser0 + KISS ser1) */
    lyra_board_set_sim_profile("lyra_plus");
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");
    cfg->iface_count = 3;
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "eth0"); strcpy(cfg->ifaces[0].type, "axudp");
    strcpy(cfg->ifaces[0].host, "0.0.0.0"); cfg->ifaces[0].port = 10093;
    memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
    strcpy(cfg->ifaces[1].id, "ppp0"); strcpy(cfg->ifaces[1].type, "ppp");
    strcpy(cfg->ifaces[1].device, "ser0"); strcpy(cfg->ifaces[1].host, "10.44.0.1");
    cfg->ifaces[1].port = 10093; cfg->ifaces[1].baud = 115200UL;
    memset(&cfg->ifaces[2], 0, sizeof(cfg->ifaces[2]));
    strcpy(cfg->ifaces[2].id, "kiss0"); strcpy(cfg->ifaces[2].type, "kiss-serial");
    strcpy(cfg->ifaces[2].device, "ser1"); cfg->ifaces[2].baud = 9600UL;

    OykPortableNode* node = new OykPortableNode();
    LyraNetBridge* net = new LyraNetBridge();
    char err[96];
    assert(node->init(cfg, LyraNetBridge::tx_callback_trampoline, net, err, sizeof(err)) == 1);

    /* RMII link DOWN degrades non-fatally while UART PPP & KISS stay active */
    lyra_radio_sim_set_link_up(-1);
    net->init(node, NULL, 0);
    assert(net->radio_state()->ip_transport_ready == 0);
    assert(net->slot(0)->active == 0);
    assert(net->slot(1)->active == 1);
    assert(net->slot(2)->active == 1);
    assert(net->active_uart_count() == 2);

    /* RMII link UP activates eth0 */
    net->init(node, NULL, 1);
    assert(net->radio_state()->rmii_link_up == 1 && net->slot(0)->active == 1);

    /* Wi-Fi 6 + BLE on lyra_zero_w */
    lyra_board_set_sim_profile("lyra_zero_w");
    LyraRadioConfig rcfg;
    lyra_radio_default_config(&rcfg);
    strcpy(rcfg.wifi_ssid, "AmateurPacketNet");
    net->init(node, &rcfg, 1);
    assert(net->radio_state()->wifi_connected == 1 && net->radio_state()->ble_enabled == 1);

    /* USB RNDIS + PPP fallback on base lyra */
    lyra_board_set_sim_profile("lyra");
    net->init(node, NULL, 1);
    assert(net->radio_state()->usb_rndis_up == 1);

    /* PicoCalc blocks ser1 safely */
    lyra_board_set_sim_profile("picocalc_lyra");
    net->init(node, NULL, 1);
    assert(net->slot(2)->active == 0);
    assert(strcmp(net->slot(2)->status, "ser1 UNAVAIL") == 0);

    delete net;
    delete node;
    delete cfg;
    printf("[PASS] test_lyra_networking_and_uart_profiles\n");
}

static void test_lyra_picocalc_kbd_battery_backlight_audio_and_fonts() {
    lyra_board_set_sim_profile("picocalc_lyra");

    LyraPicoCalcKbd kbd;
    kbd.init();
    kbd.sim_set_battery(76);
    assert(kbd.battery_percent() == 76);

    const oyk_u8 init_bkl = kbd.lcd_backlight();
    kbd.sim_push_event(LYRA_PICOCALC_KEY_PRESSED, LYRA_PICOCALC_MOD_ALT);
    kbd.sim_push_event(LYRA_PICOCALC_KEY_PRESSED, LYRA_PICOCALC_KEY_UP);
    kbd.sim_push_event(LYRA_PICOCALC_KEY_RELEASED, LYRA_PICOCALC_MOD_ALT);
    oyk_u8 vt_out[32];
    int act = LYRA_PICOCALC_ACT_NONE;
    assert(kbd.poll_vt100(100UL, vt_out, sizeof(vt_out), &act) == 0);
    assert(kbd.lcd_backlight() > init_bkl);

    /* Alt+Right increases keyboard backlight */
    const oyk_u8 init_kb2 = kbd.kbd_backlight();
    kbd.sim_push_event(LYRA_PICOCALC_KEY_PRESSED, LYRA_PICOCALC_MOD_ALT);
    kbd.sim_push_event(LYRA_PICOCALC_KEY_PRESSED, LYRA_PICOCALC_KEY_RIGHT);
    kbd.sim_push_event(LYRA_PICOCALC_KEY_RELEASED, LYRA_PICOCALC_MOD_ALT);
    assert(kbd.poll_vt100(150UL, vt_out, sizeof(vt_out), &act) == 0);
    assert(kbd.kbd_backlight() > init_kb2);

    /* Power off STM32 register request */
    assert(kbd.request_poweroff(5U) == 1);
    assert(kbd.poweroff_requested() == 5);

    /* Console: 4x8 (80x40) <-> 6x10 (53x32 panned) & BEL audio default-OFF -> opt-in */
    LyraPicoCalcConsole* lc = new LyraPicoCalcConsole();
    lc->init(NULL);
    assert(lc->vt()->cols == 80 && lc->vt()->rows == 40);
    assert(lc->vt()->font->width == 4 && lc->vt()->font->height == 8);
    assert(lc->audio()->is_enabled() == 0);
    oyk_vt100_write(lc->vt(), "\007", 1);
    assert(lc->audio()->bel_count() == 0 && lc->audio()->is_beeping() == 0);

    lc->audio()->set_enabled(1);
    assert(lc->audio()->is_enabled() == 1);
    oyk_vt100_write(lc->vt(), "\007", 1);
    assert(lc->audio()->bel_count() == 1 && lc->audio()->is_beeping() == 1);
    lc->poll_tick(500UL);
    assert(lc->audio()->is_beeping() == 0);

    lc->toggle_font();
    assert(lc->zoomed_6x10() == 1 && lc->vt()->font->width == 6 && lc->vt()->font->height == 10);
    assert(lc->vt()->view_cols == 53 && lc->vt()->view_rows == 32 && lc->pan_col() == 0);
    lc->pan_horizontal(8);
    assert(lc->pan_col() == 8 && lc->vt()->pan_col == 8);
    lc->toggle_font();
    assert(lc->zoomed_6x10() == 0 && lc->vt()->cols == 80 && lc->vt()->rows == 40);

    delete lc;
    printf("[PASS] test_lyra_picocalc_kbd_battery_backlight_audio_and_fonts\n");
}

static void init_shared_picocalc_config(OykConfig* cfg) {
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");
    strcpy(cfg->bbs_files_path, "/tmp/oyk_lyra_fm/files");
    cfg->iface_count = 1;
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "ppp0");
    strcpy(cfg->ifaces[0].type, "ppp");
    strcpy(cfg->ifaces[0].device, "ser0");
    strcpy(cfg->ifaces[0].host, "10.44.0.1");
    cfg->ifaces[0].port = 10093;
    cfg->ifaces[0].baud = 115200UL;

    cfg->peer_count = 1;
    strcpy(cfg->peers[0].id, "remote0");
    strcpy(cfg->peers[0].callsign, "R3MOT-7");
    strcpy(cfg->peers[0].description, "Remote PoPT Node");
    strcpy(cfg->peers[0].interface_id, "ppp0");
    strcpy(cfg->peers[0].host, "12.34.56.78");
    cfg->peers[0].port = 10093;
    cfg->peers[0].allow_inbound = 1;
    cfg->peers[0].allow_outbound = 1;
}

static void check_shared_screen_exact(const char* flow,
                                      LyraConsole* lcon, LyraPicoCalcConsole* lpc,
                                      PicoConsole* pcon, PicoCalcConsole* ppc) {
    lcon->render(); lpc->flush();
    pcon->render(); ppc->flush();
    /* Exact 320x320 RGB565 pixel-for-pixel equality on Row 0 (y=0..7) and Rows 2..39 (y=16..319) */
    assert_pixel_box_equal(flow, lpc->lcd(), ppc->lcd(), 0, 0, 319, 7);
    assert_pixel_box_equal(flow, lpc->lcd(), ppc->lcd(), 0, 16, 319, 319);
    assert_vt_rows_equal(flow, lpc->vt(), ppc->vt(), 0, 0);
    assert_vt_rows_equal(flow, lpc->vt(), ppc->vt(), 2, 39);
}

static void test_picocalc_cross_port_ui_parity() {
    /* Prepare jailed dual-pane file manager fixtures under /tmp/oyk_lyra_fm */
    char orig_cwd[256];
    orig_cwd[0] = '\0';
#ifndef _WIN32
    if (getcwd(orig_cwd, sizeof(orig_cwd)) == NULL) orig_cwd[0] = '\0';
    mkdir("/tmp/oyk_lyra_fm", 0755);
    mkdir("/tmp/oyk_lyra_fm/incoming", 0755);
    mkdir("/tmp/oyk_lyra_fm/incoming/W3TRM-1", 0755);
    mkdir("/tmp/oyk_lyra_fm/files", 0755);
    mkdir("/tmp/oyk_lyra_fm/files/bulletins", 0755);
    if (FILE* sf = fopen("/tmp/oyk_lyra_fm/incoming/W3TRM-1/fieldday.txt", "wb")) {
        fputs("Field Day Log 2026\n", sf); fclose(sf);
    }
    if (FILE* sf = fopen("/tmp/oyk_lyra_fm/files/bulletins/welcome.txt", "wb")) {
        fputs("Welcome to OYK BBS\n", sf); fclose(sf);
    }
#endif

    OykConfig* lcfg = new OykConfig();
    OykConfig* pcfg = new OykConfig();
    init_shared_picocalc_config(lcfg);
    init_shared_picocalc_config(pcfg);

    lyra_board_set_sim_profile("picocalc_lyra");
    lyra_fs_init(1, 1, 0);
    pico_board_set_sim_profile("picocalc_pico2_w");
    pico_psram_set_sim_available(1, 8UL * 1024UL * 1024UL, "Carrier SPI PSRAM (8 MB GP20)");
    pico_psram_init_and_test();
    pico_fs_init(1, 0);

    OykPortableNode* lnode = new OykPortableNode();
    OykPortableNode* pnode = new OykPortableNode();
    LyraNetBridge* lnet = new LyraNetBridge();
    PicoNetBridge* pnet = new PicoNetBridge();
    char err[96];
    assert(lnode->init(lcfg, LyraNetBridge::tx_callback_trampoline, lnet, err, sizeof(err)) == 1);
    assert(pnode->init(pcfg, PicoNetBridge::tx_callback_trampoline, pnet, err, sizeof(err)) == 1);
    lnet->init(lnode, NULL, 1);
    pnet->init(pnode, NULL, 1);

    LyraConsole* lcon = new LyraConsole();
    PicoConsole* pcon = new PicoConsole();
    LyraPicoCalcConsole* lpc = new LyraPicoCalcConsole();
    PicoCalcConsole* ppc = new PicoCalcConsole();
    lcon->init(lnode, lnet, LyraPicoCalcConsole::tx_trampoline, lpc);
    pcon->init(pnode, pnet, PicoCalcConsole::tx_trampoline, ppc);
    lpc->init(lcon);
    ppc->init(pcon);
    lpc->kbd()->sim_set_battery(88);
    ppc->kbd()->sim_set_battery(88);
    lcon->set_battery_percent(88);
    pcon->set_battery_percent(88);
    lnode->set_local_event_callback(lyra_ui_local_cb, lcon);
    pnode->set_local_event_callback(pico_parity_local_cb, pcon);
    lcon->tick(500UL);
    pcon->tick(500UL);
    lpc->connect_and_probe();
    ppc->connect_and_probe();
    assert(lcon->term_rows() == 40 && lcon->term_cols() == 80);
    assert(pcon->term_rows() == 40 && pcon->term_cols() == 80);

    /* Inject identical incoming SABM from W3TRM-1 so active conn + MHEARD + auto-learned peer match */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    sabm.dst = lcfg->callsign;
    assert(oyk_parse_callsign("W3TRM-1", &sabm.src) == 1);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    lnode->on_rx_frame(0, &sabm, 0x0A000005UL, 10093U, 0);
    pnode->on_rx_frame(0, &sabm, 0x0A000005UL, 10093U, 0);

    /* 1. main */
    check_shared_screen_exact("main", lcon, lpc, pcon, ppc);
    char lrow1[81], prow1[81];
    read_vt_row(lpc->vt(), 1, lrow1);
    read_vt_row(ppc->vt(), 1, prow1);
    assert(strstr(lrow1, "OYK PC(LYRA):") != NULL && strstr(lrow1, "BAT:88%") != NULL);
    assert(strstr(prow1, "OYK PC(2350):") != NULL && strstr(prow1, "BAT:88%") != NULL);

    /* Zoomed 6x10 (53x32) + panned main parity check */
    lpc->toggle_font(); ppc->toggle_font();
    assert_pixel_box_equal("main_6x10", lpc->lcd(), ppc->lcd(), 0, 0, 319, 9);
    assert_pixel_box_equal("main_6x10", lpc->lcd(), ppc->lcd(), 0, 20, 319, 319);
    lpc->pan_horizontal(8); ppc->pan_horizontal(8);
    assert_pixel_box_equal("main_6x10_pan", lpc->lcd(), ppc->lcd(), 0, 0, 319, 9);
    assert_pixel_box_equal("main_6x10_pan", lpc->lcd(), ppc->lcd(), 0, 20, 319, 319);
    lpc->toggle_font(); ppc->toggle_font();

    /* 2. menu (row 0, borders, shadows, 12 shared items pixel-identical; [P] and [U] verified on Lyra) */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("m"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("m"), 1);
    lcon->render(); lpc->flush();
    pcon->render(); ppc->flush();
    assert_pixel_box_equal("menu", lpc->lcd(), ppc->lcd(), 0, 0, 319, 7);
    assert_pixel_box_equal("menu", lpc->lcd(), ppc->lcd(), 0, 16, 319, 103);
    assert_pixel_box_equal("menu", lpc->lcd(), ppc->lcd(), 0, 128, 319, 319);
    char mrow[81];
    read_vt_row(lpc->vt(), 13, mrow); assert(strstr(mrow, "[P] Hardware Diagnostics...") != NULL);
    read_vt_row(lpc->vt(), 15, mrow); assert(strstr(mrow, "[U] Reboot / Power Off...") != NULL);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("x"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("x"), 1);

    /* 3. terminate */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("t"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("t"), 1);
    check_shared_screen_exact("terminate", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);

    /* 4. ban */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("bB4NTST\r"), 8);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("bB4NTST\r"), 8);
    check_shared_screen_exact("ban", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    lcon->tick(700UL); pcon->tick(700UL);

    /* 5..8. settings tabs 1..4 */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    check_shared_screen_exact("settings", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("2"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("2"), 1);
    check_shared_screen_exact("settings_bbs", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("3"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("3"), 1);
    check_shared_screen_exact("settings_chat", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("4"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("4"), 1);
    check_shared_screen_exact("settings_ifaces", lcon, lpc, pcon, ppc);

    /* 9..12. add_iface_1..4 */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("n"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("n"), 1);
    lcon->render(); lpc->flush();
    pcon->render(); ppc->flush();
    /* Shared header/border/progress/list header + driver IDs (rows above/below 5 driver desc rows) */
    assert_pixel_box_equal("add_iface_1", lpc->lcd(), ppc->lcd(), 0, 0, 319, 7);
    assert_pixel_box_equal("add_iface_1", lpc->lcd(), ppc->lcd(), 0, 16, 319, 103);
    assert_pixel_box_equal("add_iface_1", lpc->lcd(), ppc->lcd(), 0, 144, 319, 319);
    assert_pixel_box_equal("add_iface_1", lpc->lcd(), ppc->lcd(), 0, 104, 111, 143);
    read_vt_row(lpc->vt(), 13, mrow); assert(strstr(mrow, "RMII / Wi-Fi / RNDIS") != NULL);

    lcon->feed_input(reinterpret_cast<const oyk_u8*>("1\r"), 2);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("1\r"), 2);
    check_shared_screen_exact("add_iface_2", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    check_shared_screen_exact("add_iface_3", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    check_shared_screen_exact("add_iface_4", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    lcon->tick(900UL); pcon->tick(900UL);

    /* 13. edit_iface */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    check_shared_screen_exact("edit_iface", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    lcon->tick(1100UL); pcon->tick(1100UL);

    /* 14..15. peers & edit_peer */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("5"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("5"), 1);
    check_shared_screen_exact("peers", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    check_shared_screen_exact("edit_peer", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    lcon->tick(1300UL); pcon->tick(1300UL);

    /* 16..17. settings_rig & settings_printer */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("6"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("6"), 1);
    check_shared_screen_exact("settings_rig", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("7"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("7"), 1);
    check_shared_screen_exact("settings_printer", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    lcon->tick(1500UL); pcon->tick(1500UL);

    /* 18. theme */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("h"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("h"), 1);
    check_shared_screen_exact("theme", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    lcon->tick(1700UL); pcon->tick(1700UL);

    /* 19..20. terminal & terminal_help */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\t\x01" "b?\r"), 5);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\t\x01" "b?\r"), 5);
    check_shared_screen_exact("terminal", lcon, lpc, pcon, ppc);
    read_vt_row(lpc->vt(), 1, lrow1);
    assert(strstr(lrow1, "OYK Lyra Local Terminal") != NULL);

    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "z"), 2);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "z"), 2);
    check_shared_screen_exact("terminal_help", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b\x1b"), 2);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b\x1b"), 2);
    lcon->tick(1900UL); pcon->tick(1900UL);

    /* 21. files (jailed dual-pane file manager) */
#ifndef _WIN32
    assert(chdir("/tmp/oyk_lyra_fm") == 0);
#endif
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("f"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("f"), 1);
    check_shared_screen_exact("files", lcon, lpc, pcon, ppc);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    lcon->tick(2100UL); pcon->tick(2100UL);
#ifndef _WIN32
    if (orig_cwd[0]) assert(chdir(orig_cwd) == 0);
#endif

    /* 22. about (identical frame, [X], and left 15x10 ASCII emblem; Luckfox Lyra in right text column) */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("?"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("?"), 1);
    lcon->render(); lpc->flush();
    pcon->render(); ppc->flush();
    assert_pixel_box_equal("about_row0", lpc->lcd(), ppc->lcd(), 0, 0, 319, 7);
    assert_pixel_box_equal("about_emblem", lpc->lcd(), ppc->lcd(), 16, 112, 119, 207);
    char abrow[81];
    int saw_lyra = 0;
    for (int r = 12; r <= 27; ++r) {
        read_vt_row(lpc->vt(), r, abrow);
        if (strstr(abrow, "Luckfox Lyra") != NULL) saw_lyra = 1;
    }
    assert(saw_lyra == 1);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("q"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("q"), 1);

    /* 23. exit (auto-learned peer modal: identical row 0, borders, divider, 4-button row) */
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("u"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("u"), 1);
    lcon->render(); lpc->flush();
    pcon->render(); ppc->flush();
    assert_pixel_box_equal("exit_row0", lpc->lcd(), ppc->lcd(), 0, 0, 319, 7);
    assert_pixel_box_equal("exit_buttons", lpc->lcd(), ppc->lcd(), 24, 184, 295, 207);
    lcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    pcon->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);

    delete lpc; delete ppc;
    delete lcon; delete pcon;
    delete lnet; delete pnet;
    delete lnode; delete pnode;
    delete lcfg; delete pcfg;
    printf("[PASS] test_picocalc_cross_port_ui_parity\n");
}

static void test_lyra_standalone_ui_parity() {
    lyra_board_set_sim_profile("lyra_plus");
    lyra_fs_init(1, 1, 1);

    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");
    strcpy(cfg->bbs_files_path, "/tmp/oyk_lyra_fm/files");
    cfg->iface_count = 3;
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "eth0"); strcpy(cfg->ifaces[0].type, "axudp");
    strcpy(cfg->ifaces[0].host, "0.0.0.0"); cfg->ifaces[0].port = 10093;
    memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
    strcpy(cfg->ifaces[1].id, "ppp0"); strcpy(cfg->ifaces[1].type, "ppp");
    strcpy(cfg->ifaces[1].device, "ser0"); strcpy(cfg->ifaces[1].host, "10.44.0.1");
    cfg->ifaces[1].port = 10093; cfg->ifaces[1].baud = 115200UL;
    memset(&cfg->ifaces[2], 0, sizeof(cfg->ifaces[2]));
    strcpy(cfg->ifaces[2].id, "kiss0"); strcpy(cfg->ifaces[2].type, "kiss-serial");
    strcpy(cfg->ifaces[2].device, "ser1"); cfg->ifaces[2].baud = 9600UL;

    cfg->peer_count = 1;
    strcpy(cfg->peers[0].id, "remote0");
    strcpy(cfg->peers[0].callsign, "R3MOT-7");
    strcpy(cfg->peers[0].interface_id, "eth0");
    strcpy(cfg->peers[0].host, "12.34.56.78");
    cfg->peers[0].port = 10093;
    cfg->peers[0].allow_inbound = 1;
    cfg->peers[0].allow_outbound = 1;

    OykPortableNode* node = new OykPortableNode();
    LyraNetBridge* net = new LyraNetBridge();
    char err[96];
    assert(node->init(cfg, LyraNetBridge::tx_callback_trampoline, net, err, sizeof(err)) == 1);
    net->init(node, NULL, 1);

    LyraConsole* con = new LyraConsole();
    LyraCapture* cap = new LyraCapture();
    con->init(node, net, lyra_cap_tx, cap);
    node->set_local_event_callback(lyra_ui_local_cb, con);
    con->on_usb_connect();
    static const oyk_u8 kDaReplies[] = "\033[?62;22c\033[>0;1;0c";
    con->feed_input(kDaReplies, sizeof(kDaReplies) - 1U);
    assert(con->term_type() == LYRA_TERM_XTERM256);

    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    sabm.dst = cfg->callsign;
    assert(oyk_parse_callsign("W3TRM-1", &sabm.src) == 1);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    node->on_rx_frame(0, &sabm, 0x0A000005UL, 10093U, 0);

    lyra_save_ansi("main", cap, con);

    con->feed_input(reinterpret_cast<const oyk_u8*>("m"), 1);
    lyra_save_ansi("menu", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("x"), 1);

    con->feed_input(reinterpret_cast<const oyk_u8*>("t"), 1);
    lyra_save_ansi("terminate", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);

    con->feed_input(reinterpret_cast<const oyk_u8*>("bB4NTST\r"), 8);
    lyra_save_ansi("ban", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(700UL);

    con->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    lyra_save_ansi("settings", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("2"), 1);
    lyra_save_ansi("settings_bbs", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("3"), 1);
    lyra_save_ansi("settings_chat", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("4"), 1);
    lyra_save_ansi("settings_ifaces", cap, con);

    con->feed_input(reinterpret_cast<const oyk_u8*>("n"), 1);
    lyra_save_ansi("add_iface_1", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("1\r"), 2);
    lyra_save_ansi("add_iface_2", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    lyra_save_ansi("add_iface_3", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    lyra_save_ansi("add_iface_4", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(900UL);

    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    lyra_save_ansi("edit_iface", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1100UL);

    con->feed_input(reinterpret_cast<const oyk_u8*>("5"), 1);
    lyra_save_ansi("peers", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    lyra_save_ansi("edit_peer", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1300UL);

    con->feed_input(reinterpret_cast<const oyk_u8*>("6"), 1);
    lyra_save_ansi("settings_rig", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("7"), 1);
    lyra_save_ansi("settings_printer", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1500UL);

    con->feed_input(reinterpret_cast<const oyk_u8*>("h"), 1);
    lyra_save_ansi("theme", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(1700UL);

    con->feed_input(reinterpret_cast<const oyk_u8*>("\t\x01" "b?\r"), 5);
    lyra_save_ansi("terminal", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x01" "z"), 2);
    lyra_save_ansi("terminal_help", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b\x1b"), 2);
    con->tick(1900UL);

    {
        char orig_cwd[256];
        orig_cwd[0] = '\0';
        const char* cap_dir = getenv("OYK_LYRA_CAPTURE_DIR");
        char abs_cap[512];
        abs_cap[0] = '\0';
#ifndef _WIN32
        if (getcwd(orig_cwd, sizeof(orig_cwd)) == NULL) orig_cwd[0] = '\0';
        if (cap_dir && cap_dir[0] && cap_dir[0] != '/' && orig_cwd[0]) {
            snprintf(abs_cap, sizeof(abs_cap), "%s/%s", orig_cwd, cap_dir);
            setenv("OYK_LYRA_CAPTURE_DIR", abs_cap, 1);
        }
        assert(chdir("/tmp/oyk_lyra_fm") == 0);
#endif
        con->feed_input(reinterpret_cast<const oyk_u8*>("f"), 1);
        lyra_save_ansi("files", cap, con);
        con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
        con->tick(2100UL);
#ifndef _WIN32
        if (orig_cwd[0]) assert(chdir(orig_cwd) == 0);
        if (abs_cap[0]) setenv("OYK_LYRA_CAPTURE_DIR", cap_dir, 1);
#endif
    }

    con->feed_input(reinterpret_cast<const oyk_u8*>("?"), 1);
    lyra_save_ansi("about", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("q"), 1);

    /* Dynamic CPR resize verification */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[36;120R"), 9);
    assert(con->term_rows() == 36 && con->term_cols() == 120);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\033[25;80R"), 8);
    assert(con->term_rows() == 25 && con->term_cols() == 80);

    con->feed_input(reinterpret_cast<const oyk_u8*>("u"), 1);
    lyra_save_ansi("exit", cap, con);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);

    delete cap;
    delete con;
    delete net;
    delete node;
    delete cfg;
    printf("[PASS] test_lyra_standalone_ui_parity\n");
}

int main() {
    test_shared_port_parity();
    test_lyra_board_profiles_and_mac();
    test_lyra_storage_sd_nand_fallback_and_format_guard();
    test_lyra_networking_and_uart_profiles();
    test_lyra_picocalc_kbd_battery_backlight_audio_and_fonts();
    test_picocalc_cross_port_ui_parity();
    test_lyra_standalone_ui_parity();
    printf("All Luckfox Lyra (RK3506 / PicoCalc) host tests passed!\n");
    return 0;
}
