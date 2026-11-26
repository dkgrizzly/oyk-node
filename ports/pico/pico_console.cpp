/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_console.hpp"
#include "pico_fs.hpp"
#include "pico_psram.hpp"
#include "pico_radio.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum PicoKeyCode {
    PK_NONE = 0,
    PK_UP = 1001,
    PK_DOWN,
    PK_RIGHT,
    PK_LEFT,
    PK_HOME,
    PK_END,
    PK_BACKTAB,
    PK_F2,
    PK_F10,
    PK_ESC = 27,
    PK_ENTER = 13,
    PK_TAB = 9,
    PK_BS = 127
};

static const char* const kWizardDrivers[5] = {
    "axudp", "axip", "ppp", "kiss-serial", "kiss-bt"
};

static const char* pico_format_sgr(int slot, oyk_u32 fg_rgb, oyk_u32 bg_rgb,
                                   int use_bg, int bold, PicoTermType tt) {
    static char bufs[8][32];
    char* b = bufs[slot & 7];
    if (tt >= PICO_TERM_XTERM256) {
        int fg = oyk_rgb_to_xterm256(fg_rgb);
        if (use_bg) {
            int bg = oyk_rgb_to_xterm256(bg_rgb);
            sprintf(b, "\033[%s38;5;%d;48;5;%dm", bold ? "1;" : "", fg, bg);
        } else {
            sprintf(b, "\033[%s38;5;%dm", bold ? "1;" : "", fg);
        }
        return b;
    }
    int fg16 = oyk_rgb_to_ansi16(fg_rgb);
    int fgc = (fg16 < 8) ? (30 + fg16) : (82 + fg16);
    if (use_bg) {
        int bg16 = oyk_rgb_to_ansi16(bg_rgb);
        int bgc = (bg16 < 8) ? (40 + bg16) : (92 + bg16);
        sprintf(b, "\033[%s%d;%dm", bold ? "1;" : "", fgc, bgc);
    } else {
        sprintf(b, "\033[%s%dm", bold ? "1;" : "", fgc);
    }
    return b;
}

PicoConsole::PicoConsole()
    : node_(NULL), bridge_(NULL), tx_fn_(NULL), tx_ctx_(NULL),
      term_type_(PICO_TERM_VT100), view_(PICO_VIEW_CONSOLE),
      prompt_kind_(PICO_PROMPT_NONE), menu_sel_(0), settings_tab_(0),
      settings_sel_(0), storage_sel_(0), modal_sel_(0), wiz_step_(0), wiz_drv_(0),
      bootloader_pending_(0), bootloader_ms_(0), prompt_len_(0),
      lterm_sid_(0), lterm_line_count_(0), lterm_input_len_(0),
      esc_state_(0), esc_len_(0), esc_ms_(0), last_ms_(0) {
    oyk_config_init_default(&draft_);
    prompt_buf_[0] = '\0';
    status_fb_[0] = '\0';
    lterm_input_[0] = '\0';
    esc_buf_[0] = '\0';
}

void PicoConsole::init(OykPortableNode* node, PicoNetBridge* bridge,
                       PicoConsoleTxFn tx_fn, void* tx_ctx) {
    node_ = node;
    bridge_ = bridge;
    tx_fn_ = tx_fn;
    tx_ctx_ = tx_ctx;
    if (node_) draft_ = node_->config();
    lterm_line_count_ = 0;
    strcpy(lterm_lines_[lterm_line_count_++],
           "*** OYK Pico Local Terminal (Esc:Console, /local, /bbs, /chat, /disc)");
}

const char* PicoConsole::term_type_label() const {
    switch (term_type_) {
        case PICO_TERM_VT220:    return "VT220";
        case PICO_TERM_VT340:    return "VT340";
        case PICO_TERM_XTERM256: return "xterm-256";
        default:                 return "VT100";
    }
}

void PicoConsole::write_str(const char* s) {
    if (tx_fn_ && s && s[0]) {
        tx_fn_(s, strlen(s), tx_ctx_);
    }
}

const OykConfig* PicoConsole::active_theme_cfg() const {
    if (prompt_kind_ == PICO_PROMPT_THEME) return &draft_;
    return node_ ? &node_->config() : NULL;
}

const char* PicoConsole::sgr_border() const {
    if (term_type_ < PICO_TERM_VT220) return "";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(0, tc.colors[OYK_TR_BORDER], tc.colors[OYK_TR_BG], 0, 0, term_type_);
}

const char* PicoConsole::sgr_title() const {
    if (term_type_ < PICO_TERM_VT220) return "\033[1m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(1, tc.colors[OYK_TR_ACCENT], tc.colors[OYK_TR_BG], 0, 1, term_type_);
}

const char* PicoConsole::sgr_header() const {
    if (term_type_ < PICO_TERM_VT220) return "\033[1m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(2, tc.colors[OYK_TR_HEADER_FG], tc.colors[OYK_TR_HEADER_BG], 1, 1, term_type_);
}

const char* PicoConsole::sgr_menubar() const {
    if (term_type_ < PICO_TERM_VT220) return "\033[7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(3, tc.colors[OYK_TR_BUTTON_FG], tc.colors[OYK_TR_BUTTON_BG], 1, 0, term_type_);
}

const char* PicoConsole::sgr_menusel() const {
    if (term_type_ < PICO_TERM_VT220) return "\033[7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(4, tc.colors[OYK_TR_SELECTION_FG], tc.colors[OYK_TR_SELECTION_BG], 1, 1, term_type_);
}

const char* PicoConsole::sgr_list_hdr() const {
    if (term_type_ < PICO_TERM_VT220) return "\033[1;7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(5, tc.colors[OYK_TR_PANEL_FG], tc.colors[OYK_TR_PANEL_BG], 1, 1, term_type_);
}

const char* PicoConsole::sgr_sel() const {
    if (term_type_ < PICO_TERM_VT220) return "\033[7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(6, tc.colors[OYK_TR_SELECTION_FG], tc.colors[OYK_TR_SELECTION_BG], 1, 1, term_type_);
}

const char* PicoConsole::sgr_warn() const {
    if (term_type_ < PICO_TERM_VT220) return "\033[1m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return pico_format_sgr(7, tc.colors[OYK_TR_WARN], tc.colors[OYK_TR_BG], 0, 1, term_type_);
}

void PicoConsole::draw_box(int row, int col, int h, int w, const char* title, int clear_interior) {
    if (h < 2 || w < 4) return;
    char line[112];
    int inner = w - 2;
    if (inner > 78) inner = 78;

    sprintf(line, "\033[%d;%dH%s\033(0l", row, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'k';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");

    if (title && title[0] && inner > 4) {
        sprintf(line, "\033[%d;%dH%s %.*s \033[0m", row, col + 2, sgr_title(), inner - 4, title);
        write_str(line);
    }

    for (int r = 1; r < h - 1; ++r) {
        if (clear_interior) {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B\033[0m%*s%s\033(0x\033(B\033[0m",
                    row + r, col, sgr_border(), inner, "", sgr_border());
        } else {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B\033[0m\033[%d;%dH%s\033(0x\033(B\033[0m",
                    row + r, col, sgr_border(), row + r, col + w - 1, sgr_border());
        }
        write_str(line);
    }

    sprintf(line, "\033[%d;%dH%s\033(0m", row + h - 1, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'j';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");
}

void PicoConsole::draw_shadow(int row, int col, int h, int w) {
    if (h < 2 || w < 4) return;
    char line[128];
    const char* sattr = (term_type_ >= PICO_TERM_VT220) ? "\033[0;90;40m" : "\033[0m";
    int r_end = row + h;
    int c_right = col + w;
    if (c_right <= 80) {
        int rw = (c_right + 1 <= 80) ? 2 : 1;
        for (int r = row + 1; r <= r_end && r <= 24; ++r) {
            if (r < 1) continue;
            sprintf(line, "\033[%d;%dH%s\033(0%s\033(B\033[0m",
                    r, c_right, sattr, (rw == 2) ? "aa" : "a");
            write_str(line);
        }
    }
    if (r_end >= 1 && r_end <= 24) {
        int bx = col + 2;
        if (bx <= 80) {
            int bw = w - 2;
            if (bx + bw - 1 > 80) bw = 80 - bx + 1;
            if (bw > 0) {
                char shades[82];
                if (bw > 80) bw = 80;
                for (int i = 0; i < bw; ++i) shades[i] = 'a';
                shades[bw] = '\0';
                sprintf(line, "\033[%d;%dH%s\033(0%s\033(B\033[0m",
                        r_end, bx, sattr, shades);
                write_str(line);
            }
        }
    }
}

void PicoConsole::draw_hdiv(int row, int col, int w) {
    if (w < 4) return;
    char line[96];
    int inner = w - 2;
    if (inner > 78) inner = 78;
    sprintf(line, "\033[%d;%dH%s\033(0t", row, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'u';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");
}

void PicoConsole::on_usb_connect() {
    term_type_ = PICO_TERM_VT100;
    esc_state_ = 0;
    esc_len_ = 0;
    write_str("\033[c\033[>c");
    render();
}

void PicoConsole::render() {
    if (!node_ || !tx_fn_) return;
    if (term_type_ >= PICO_TERM_VT220) {
        write_str("\033[?25l");
    }
    switch (view_) {
        case PICO_VIEW_SETTINGS:   render_settings(); break;
        case PICO_VIEW_STORAGE:    render_storage(); break;
        case PICO_VIEW_PSRAM:      render_psram(); break;
        case PICO_VIEW_BOOTLOADER: render_bootloader(); break;
        case PICO_VIEW_LOCAL_TERM: render_local_term(); break;
        case PICO_VIEW_MENU:       render_console(); render_menu(); break;
        default:                   render_console(); break;
    }
    if (prompt_kind_ == PICO_PROMPT_ADD_WIZARD) render_wizard_modal();
    else if (prompt_kind_ == PICO_PROMPT_EDIT_IFACE) render_edit_iface_modal();
    else if (prompt_kind_ == PICO_PROMPT_EDIT_PEER) render_edit_peer_modal();
    else if (prompt_kind_ == PICO_PROMPT_TERM_SID) render_terminate_modal();
    else if (prompt_kind_ == PICO_PROMPT_BAN_CALL) render_ban_modal();
    else if (prompt_kind_ == PICO_PROMPT_THEME) render_theme_modal();
    else if (prompt_kind_ == PICO_PROMPT_CONFIRM_FORMAT) render_confirm_format_modal();
    else if (prompt_kind_ == PICO_PROMPT_SET_TEXT || prompt_kind_ == PICO_PROMPT_SET_BOOL) render_text_prompt();
}

void PicoConsole::render_console() {
    char buf[192], call_s[16], psram_s[8], rad_s[8];
    const PicoBoardProfile* prof = pico_current_board_profile();
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    pico_psram_format_short(pico_psram_info(), psram_s, sizeof(psram_s));
    if (bridge_) bridge_->format_short_radio_status(rad_s, sizeof(rad_s));
    else strcpy(rad_s, "NONE");

    write_str("\033[H\033[2J");
    sprintf(buf, "%s%-80s\033[0m\033[1;2H%s Node \033[0m\033[1;48H%s %-19s  [%9s]\033[0m",
            sgr_menubar(), "",
            sgr_menusel(),
            sgr_menubar(),
            lterm_sid_ ? "Tab/F9:Term(ACTIVE)" : "Tab/F9:Term | F10/M",
            term_type_label());
    write_str(buf);

    sprintf(buf, "\033[2;1H%s OYK PICO(%s): %-9s Up:%-4lus C:%-2d IN:%-5s PSRAM:%-3s RF:%-5s \033[0m",
            sgr_header(), prof->board_id, call_s,
            static_cast<unsigned long>(node_->uptime_seconds()),
            node_->total_active_connections(),
            node_->accept_incoming() ? "ALLOW" : "DENY",
            psram_s, rad_s);
    write_str(buf);

    draw_box(3, 1, 8, 45, "Interfaces (2 UART + RF)", 0);
    draw_box(3, 46, 8, 35, "Last 5 Callsigns Heard", 0);
    sprintf(buf, "\033[4;2H%s %-8s %-11s %-2s %-15s\033[0m",
            sgr_list_hdr(), "Port", "Type", "C", "Status");
    write_str(buf);
    sprintf(buf, "\033[4;47H%s %-2s %-9s %-7s %-8s \033[0m",
            sgr_list_hdr(), "#", "Callsign", "Iface", "Time");
    write_str(buf);

    for (int i = 0; i < 5; ++i) {
        char lft[48], rgt[40];
        if (i < node_->iface_count()) {
            const OykIfaceRuntime& r = node_->iface(i);
            sprintf(lft, " %-8.8s %-11.11s %-2d %-15.15s",
                    r.cfg.id, r.cfg.type, node_->active_connections_on(i), r.status);
        } else lft[0] = '\0';
        if (i < node_->new_heard_count()) {
            const OykNewHeard& nh = node_->new_heard(i);
            sprintf(rgt, " %d %-9.9s %-7.7s %-8.8s", i + 1, nh.callsign, nh.iface_id, nh.time_hms);
        } else rgt[0] = '\0';
        sprintf(buf, "\033[%d;2H%-43.43s\033[%d;47H%-33.33s", 5 + i, lft, 5 + i, rgt);
        write_str(buf);
    }

    char ct[48];
    sprintf(ct, "Active Connections (%d) | Bans: %d",
            node_->total_active_connections(), node_->ban_count());
    draw_box(11, 1, 6, 80, ct, 0);
    sprintf(buf, "\033[12;2H%s %-4s %-10s %-8s %-8s %-43s\033[0m",
            sgr_list_hdr(), "#", "Callsign", "Iface", "Since", "Service / Role");
    write_str(buf);

    int shown = 0;
    for (int i = 0; i < node_->max_sessions() && shown < 3; ++i) {
        const OykSession& s = node_->session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(buf, "\033[%d;2H #%-3lu %-10.10s %-8.8s %-8.8s %-43.43s",
                    13 + shown, static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local",
                    s.since_hms, s.is_local_term ? "LOCAL TERM" : "NODE SHELL");
            write_str(buf);
            ++shown;
        }
    }
    if (shown == 0) {
        write_str("\033[13;3H(No active packet sessions)");
    }

    draw_box(17, 1, 7, 80, "Status & Event Log", 0);
    const int tot_logs = node_->console_log_count();
    const int start_log = (tot_logs > 4) ? (tot_logs - 4) : 0;
    for (int i = 0; i < 4; ++i) {
        const char* ln = ((start_log + i) < tot_logs) ? node_->console_log_line(start_log + i) : "";
        sprintf(buf, "\033[%d;2H%-78.78s", 18 + i, ln ? ln : "");
        write_str(buf);
    }
    sprintf(buf, "\033[24;1H%s %-79.79s\033[0m",
            sgr_warn(),
            status_fb_[0] ? status_fb_ : "M:Menu  S:Settings  T:TermConn  B:Ban  H:Theme  G:Storage  P:PSRAM  U:BOOTSEL");
    write_str(buf);
}

void PicoConsole::render_menu() {
    static const char* const kItems[12] = {
        " [A] Allow Incoming Connections   ",
        " [D] Deny Incoming Connections    ",
        " [L] Local Terminal...            ",
        " [S] Settings...                  ",
        " [E] Peers...                     ",
        " [H] Theme...                     ",
        " [T] Terminate Connection...      ",
        " [B] Ban / Unban Callsign...      ",
        " [P] PSRAM Diagnostics...         ",
        " [G] Storage Manager...           ",
        " [U] Enter BOOTSEL (UF2)...       ",
        " [X] Close Menu                   "
    };
    static const int kItemRows[12] = { 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16 };
    draw_box(2, 2, 16, 36, "Node Menu", 1);
    draw_shadow(2, 2, 16, 36);
    draw_hdiv(5, 2, 36);
    draw_hdiv(15, 2, 36);
    char buf[96];
    for (int i = 0; i < 12; ++i) {
        sprintf(buf, "\033[%d;3H%s%.34s\033[0m",
                kItemRows[i], (i == menu_sel_) ? sgr_menusel() : "", kItems[i]);
        write_str(buf);
    }
}

void PicoConsole::render_settings() {
    static const char* const kTabs[5] = {
        "1:Node", "2:Local", "3:Radio", "4:Interfaces", "5:Peers"
    };
    write_str("\033[H\033[2J");
    draw_box(1, 1, 24, 80, "Settings [1-5:Tabs | F2/S:Save | Esc:Back]", 1);
    char buf[160];
    write_str("\033[2;3H");
    for (int t = 0; t < 5; ++t) {
        sprintf(buf, "%s %s \033[0m ", (t == settings_tab_) ? sgr_sel() : sgr_menubar(), kTabs[t]);
        write_str(buf);
    }
    draw_hdiv(3, 1, 80);

    if (settings_tab_ == 0) {
        char call_s[16], alias_s[16];
        oyk_format_callsign(&draft_.callsign, call_s, sizeof(call_s));
        oyk_format_callsign(&draft_.alias, alias_s, sizeof(alias_s));
        const char* labels[4] = { "Node Callsign", "Node Alias", "Themes File", "Theme Name" };
        const char* vals[4] = { call_s, alias_s, draft_.themes_file, draft_.theme_name };
        for (int i = 0; i < 4; ++i) {
            sprintf(buf, "\033[%d;4H%s %-16s : %-40.40s \033[0m",
                    5 + i, (settings_sel_ == i) ? sgr_sel() : "", labels[i], vals[i]);
            write_str(buf);
        }
    } else if (settings_tab_ == 1) {
        sprintf(buf, "\033[5;4H%sBBS Enabled (-%u): %s\033[0m",
                (settings_sel_ == 0) ? sgr_sel() : "", draft_.bbs_ssid, draft_.bbs_enabled ? "true" : "false");
        write_str(buf);
        sprintf(buf, "\033[6;4H%sChat Enabled (-%u): %s (Topic: %s)\033[0m",
                (settings_sel_ == 1) ? sgr_sel() : "", draft_.chat_ssid, draft_.chat_enabled ? "true" : "false", draft_.chat_topic);
        write_str(buf);
    } else if (settings_tab_ == 2) {
        const PicoRadioState* rs = pico_radio_state();
        sprintf(buf, "\033[5;4HWireless Module : %s (Wi-Fi: %s, BT: %s)",
                rs->module_present ? "PRESENT" : "ABSENT (non-fatal)",
                rs->wifi_enabled ? "ON" : "OFF",
                rs->bt_enabled ? "ON" : "OFF");
        write_str(buf);
        sprintf(buf, "\033[6;4HRadio Status    : %s", rs->status_text);
        write_str(buf);
        sprintf(buf, "\033[7;4HHardware UARTs  : ser0 (UART0 TX=0 RX=1) | ser1 (UART1 TX=4 RX=5)");
        write_str(buf);
    } else if (settings_tab_ == 3) {
        sprintf(buf, "\033[4;3H%s %-8s %-11s %-18s %-10s %-14s\033[0m",
                sgr_list_hdr(), "ID", "Type", "Host/Device", "In/Out", "Learned");
        write_str(buf);
        for (int i = 0; i < draft_.iface_count && i < 12; ++i) {
            const OykIfaceConfig& ic = draft_.ifaces[i];
            const char* ep = ic.device[0] ? ic.device : ic.host;
            sprintf(buf, "\033[%d;3H%s %-8.8s %-11.11s %-18.18s %s/%-7s %-14s\033[0m",
                    5 + i, (settings_sel_ == i) ? sgr_sel() : "",
                    ic.id, ic.type, ep,
                    ic.block_inbound ? "N" : "Y",
                    ic.block_outbound ? "N" : "Y",
                    oyk_format_learned_policy(ic.learned_peer_policy));
            write_str(buf);
        }
        write_str("\033[21;3HN:Add Interface Wizard  E/Enter:Edit Interface  K:Remove");
    } else {
        sprintf(buf, "\033[4;3H%s %-10s %-10s %-8s %-20s %-10s\033[0m",
                sgr_list_hdr(), "ID", "Callsign", "Iface", "Host / DMR ID", "In/Out");
        write_str(buf);
        for (int i = 0; i < draft_.peer_count && i < 12; ++i) {
            const OykPeerConfig& p = draft_.peers[i];
            sprintf(buf, "\033[%d;3H%s %-10.10s %-10.10s %-8.8s %-20.20s %s/%s\033[0m",
                    5 + i, (settings_sel_ == i) ? sgr_sel() : "",
                    p.id, p.callsign, p.interface_id, p.host,
                    p.allow_inbound ? "Y" : "N", p.allow_outbound ? "Y" : "N");
            write_str(buf);
        }
        write_str("\033[21;3HA:Add Peer  E/Enter:Edit Peer  K:Remove");
    }
    write_str("\033[23;3HS/F2:Save & Apply Config   H:Theme Settings   Esc:Return");
}

void PicoConsole::render_storage() {
    const PicoFsStatus* fs = pico_fs_status();
    char buf[128];
    write_str("\033[H\033[2J");
    draw_box(2, 4, 18, 72, "Storage Manager (SD / QSPI Flash)", 1);
    draw_shadow(2, 4, 18, 72);
    sprintf(buf, "\033[4;6HActive Medium : %s", fs->medium_label);
    write_str(buf);
    sprintf(buf, "\033[5;6HSD Card       : %s  | Flash KV: %s",
            fs->sd_present ? "DETECTED" : "ABSENT",
            fs->flash_ready ? "READY" : "N/A");
    write_str(buf);
    sprintf(buf, "\033[6;6HCapacity      : %lu KB Total (%lu KB Used)",
            static_cast<unsigned long>(fs->total_kb),
            static_cast<unsigned long>(fs->used_kb));
    write_str(buf);
    write_str("\033[9;6H[ F ] Format Active Storage (Confirmation Modal Required)");
    if (status_fb_[0]) {
        sprintf(buf, "\033[12;6H%s%-60.60s\033[0m", sgr_warn(), status_fb_);
        write_str(buf);
    }
    write_str("\033[18;6HEsc:Back to Console");
}

void PicoConsole::render_psram() {
    const PicoPsramInfo* pi = pico_psram_info();
    const PicoBoardProfile* prof = pico_current_board_profile();
    char buf[128];
    write_str("\033[H\033[2J");
    draw_box(2, 4, 16, 72, "PSRAM & Hardware Diagnostics", 1);
    draw_shadow(2, 4, 16, 72);
    sprintf(buf, "\033[4;6HBoard Profile : %s (%s)", prof->board_id, prof->board_label);
    write_str(buf);
    sprintf(buf, "\033[5;6HFlash / SRAM  : %lu MB Flash | %lu KB SRAM",
            static_cast<unsigned long>(prof->flash_bytes / (1024UL * 1024UL)),
            static_cast<unsigned long>(prof->sram_bytes / 1024UL));
    write_str(buf);
    sprintf(buf, "\033[6;6HPSRAM Status  : %s", pi->chip_label);
    write_str(buf);
    sprintf(buf, "\033[7;6HPSRAM Pool    : %lu / %lu bytes used (SRAM fallback: %lu bytes)",
            static_cast<unsigned long>(pi->pool_used_bytes),
            static_cast<unsigned long>(pi->pool_total_bytes),
            static_cast<unsigned long>(pi->sram_fallback_used_bytes));
    write_str(buf);
    write_str("\033[16;6HEsc:Return");
}

void PicoConsole::render_bootloader() {
    char call_s[16], buf[128];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    write_str("\033[H\033[2J");
    const int auto_cnt = node_->auto_learned_peer_count();
    if (bootloader_pending_) {
        draw_box(8, 12, 7, 56, "BOOTSEL UF2 Reboot", 1);
        draw_shadow(8, 12, 7, 56);
        sprintf(buf, "\033[10;14HEntering RP2xxx USB BOOTSEL mode...");
        write_str(buf);
        sprintf(buf, "\033[11;14H%s73 de %s\033[0m", sgr_title(), call_s);
        write_str(buf);
        return;
    }
    if (auto_cnt > 0) {
        draw_box(6, 6, 11, 68, "Auto-Learned Peers — BOOTSEL", 1);
        draw_shadow(6, 6, 11, 68);
        sprintf(buf, "\033[8;8H%d auto-learned peer(s) added during this session.", auto_cnt);
        write_str(buf);
        write_str("\033[9;8HSave to peers config, discard, or review before BOOTSEL?");
        draw_hdiv(13, 6, 68);
        write_str("\033[14;8HS:Save & Reboot   D:Discard & Reboot   E:Review Peers   Esc:Cancel");
    } else {
        draw_box(7, 10, 9, 60, "Enter BOOTSEL Mode (UF2 Flash)", 1);
        draw_shadow(7, 10, 9, 60);
        write_str("\033[9;12HPress Y or Enter to reboot into RP2xxx BOOTSEL mode.");
        write_str("\033[10;12HThe device will mount as RPI-RP2 / RP2350 USB drive.");
        draw_hdiv(13, 10, 60);
        write_str("\033[14;12HY/Enter:Enter BOOTSEL   Esc:Cancel");
    }
}

void PicoConsole::render_local_term() {
    char buf[128];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s Local Packet Terminal (Esc:Console | Type /local /bbs /chat /disc) \033[0m",
            sgr_menubar());
    write_str(buf);
    for (int i = 0; i < lterm_line_count_ && i < 12; ++i) {
        sprintf(buf, "\033[%d;1H%-79.79s", 3 + i, lterm_lines_[i]);
        write_str(buf);
    }
    sprintf(buf, "\033[23;1H> %-76.76s", lterm_input_);
    write_str(buf);
}

void PicoConsole::render_wizard_modal() {
    char prog[24], buf[120];
    int fill = wiz_step_ * 2;
    for (int i = 0; i < 8; ++i) prog[i] = (i < fill) ? '#' : '.';
    prog[8] = '\0';
    draw_box(5, 10, 14, 60, "Add Interface", 1);
    draw_shadow(5, 10, 14, 60);
    sprintf(buf, "\033[6;52H%s[%s] %d/4\033[0m", sgr_title(), prog, wiz_step_);
    write_str(buf);
    if (wiz_step_ == 1) {
        write_str("\033[7;12HSelect driver (1-5 or Up/Down, Enter:Next):");
        for (int i = 0; i < 5; ++i) {
            sprintf(buf, "\033[%d;14H%s %d. %-20s \033[0m",
                    8 + i, (wiz_drv_ == i) ? sgr_sel() : "", i + 1, kWizardDrivers[i]);
            write_str(buf);
        }
    } else if (wiz_step_ == 2) {
        write_str("\033[7;12HEndpoint Parameters (Defaults derived for driver):");
        sprintf(buf, "\033[9;14HDriver: %s | Bind/Device: 0.0.0.0 / ser0", kWizardDrivers[wiz_drv_]);
        write_str(buf);
        write_str("\033[10;14HPress Enter:Next   B:Back");
    } else if (wiz_step_ == 3) {
        write_str("\033[7;12HDirection & Learned Policy:");
        write_str("\033[9;14HInbound: ON  Outbound: ON  Same-Iface: ON  Learned: REJECT");
        write_str("\033[10;14HPress Enter:Summary   B:Back");
    } else {
        write_str("\033[7;12HSummary — Commit new interface?");
        sprintf(buf, "\033[9;14HDriver: %s  (Enter:Create  B:Back  Esc:Cancel)", kWizardDrivers[wiz_drv_]);
        write_str(buf);
    }
    draw_hdiv(16, 10, 60);
    write_str("\033[17;12HEnter:Next/Create   B:Back   Esc:Cancel");
}

void PicoConsole::render_edit_iface_modal() {
    if (settings_sel_ < 0 || settings_sel_ >= draft_.iface_count) return;
    const OykIfaceConfig& ic = draft_.ifaces[settings_sel_];
    char buf[120];
    draw_box(5, 10, 14, 60, "Edit Interface", 1);
    draw_shadow(5, 10, 14, 60);
    sprintf(buf, "\033[7;12HID: %s  Type: %s", ic.id, ic.type);
    write_str(buf);
    sprintf(buf, "\033[9;12HInbound : %s (I:toggle)  Outbound: %s (O:toggle)",
            ic.block_inbound ? "OFF" : "ON", ic.block_outbound ? "OFF" : "ON");
    write_str(buf);
    sprintf(buf, "\033[10;12HSame-If : %s (S:toggle)  Learned : %s (L:cycle)",
            ic.allow_same_interface ? "ON" : "OFF", oyk_format_learned_policy(ic.learned_peer_policy));
    write_str(buf);
    draw_hdiv(16, 10, 60);
    write_str("\033[17;12HI/O/S/L:Toggle   Enter:Done   Esc:Close");
}

void PicoConsole::render_edit_peer_modal() {
    draw_box(6, 10, 12, 60, "Edit Peer", 1);
    draw_shadow(6, 10, 12, 60);
    write_str("\033[8;12HAllow Inbound / Allow Outbound toggle (I / O)");
    write_str("\033[15;12HEnter:Save   Esc:Cancel");
}

void PicoConsole::render_terminate_modal() {
    draw_box(6, 10, 12, 60, "Terminate Active Connection", 1);
    draw_shadow(6, 10, 12, 60);
    write_str("\033[8;12HEnter:Terminate First Session   Esc:Close");
}

void PicoConsole::render_ban_modal() {
    draw_box(6, 10, 12, 60, "Ban / Unban Callsign (banlist.cfg)", 1);
    draw_shadow(6, 10, 12, 60);
    write_str("\033[8;12HEnter Callsign to ban or Esc:Close");
}

void PicoConsole::render_theme_modal() {
    char buf[120];
    draw_box(4, 8, 16, 64, "Theme Settings", 1);
    draw_shadow(4, 8, 16, 64);
    sprintf(buf, "\033[6;10HActive Theme : < %-18s >",
            draft_.theme_name[0] ? draft_.theme_name : "dark");
    write_str(buf);
    write_str("\033[7;10HBuilt-in dark/light are never written to node.cfg.");
    OykThemeConfig tc;
    oyk_theme_resolve_effective(&draft_, 0, 1, &tc);
    for (int i = 0; i < 6; ++i) {
        char hx[16];
        oyk_theme_format_hex(tc.colors[i], hx, sizeof(hx));
        sprintf(buf, "\033[%d;10H%-14s %-12s %s ", 9 + i,
                oyk_theme_role_label(i), oyk_theme_role_key(i), hx);
        write_str(buf);
        if (term_type_ >= PICO_TERM_VT220) {
            write_str(pico_format_sgr(4, tc.colors[i], tc.colors[i], 1, 0, term_type_));
            write_str("    \033[0m");
        } else {
            write_str("[##]");
        }
    }
    write_str("\033[16;10HLive preview active (unsaved). Press S/Enter to save or Esc to revert.");
    draw_hdiv(17, 8, 64);
    write_str("\033[18;10HLt/Rt:Theme   S/Enter:Save & Apply   Esc:Cancel");
}

void PicoConsole::render_confirm_format_modal() {
    char buf[112];
    draw_box(8, 12, 8, 56, "Confirm Storage Format", 1);
    draw_shadow(8, 12, 8, 56);
    write_str("\033[10;14HType FORMAT to erase active storage and press Enter:");
    sprintf(buf, "\033[12;14H> %s%-16s\033[0m", sgr_sel(), prompt_buf_);
    write_str(buf);
    write_str("\033[14;14HEsc:Cancel");
}

void PicoConsole::render_text_prompt() {
    char buf[112];
    draw_box(9, 14, 6, 52, "Edit Setting", 1);
    draw_shadow(9, 14, 6, 52);
    sprintf(buf, "\033[11;16H> %s%-44s\033[0m", sgr_sel(), prompt_buf_);
    write_str(buf);
}

void PicoConsole::open_settings(int tab) {
    if (node_) draft_ = node_->config();
    view_ = PICO_VIEW_SETTINGS;
    settings_tab_ = (tab >= 0 && tab < 5) ? tab : 0;
    settings_sel_ = 0;
    render();
}

void PicoConsole::commit_settings() {
    if (!node_) return;
    OykConfigApplyResult res;
    node_->apply_config(&draft_, 1, &res);
    if (bridge_) bridge_->reconfig();
    strcpy(status_fb_, "Settings saved & applied.");
}

void PicoConsole::on_local_term_event(oyk_u32 sid, int event_type,
                                      const oyk_u8* data, size_t len, const char* /*reason*/) {
    if (event_type == 3) {
        lterm_sid_ = 0UL;
    } else {
        lterm_sid_ = sid;
    }
    if (event_type == 2 && data && len > 0) {
        if (lterm_line_count_ < 12) {
            size_t n = (len < 78) ? len : 78;
            memcpy(lterm_lines_[lterm_line_count_], data, n);
            lterm_lines_[lterm_line_count_][n] = '\0';
            ++lterm_line_count_;
        }
    }
    if (view_ == PICO_VIEW_LOCAL_TERM) render();
}

void PicoConsole::feed_input(const oyk_u8* data, size_t len) {
    if (!data || len == 0) return;
    for (size_t i = 0; i < len; ++i) {
        handle_char(data[i], last_ms_);
    }
}

void PicoConsole::tick(oyk_u32 now_ms) {
    last_ms_ = now_ms;
    if (esc_state_ == 1 && (now_ms - esc_ms_) >= 150UL) {
        esc_state_ = 0;
        esc_len_ = 0;
        handle_key_code(PK_ESC);
    }
    if (bootloader_pending_ && (now_ms - bootloader_ms_) >= 300UL) {
        bootloader_pending_ = 0;
        pico_board_enter_bootloader();
    }
}

void PicoConsole::handle_char(oyk_u8 ch, oyk_u32 now_ms) {
    if (esc_state_ == 0) {
        if (ch == 0x1BU) {
            esc_state_ = 1;
            esc_len_ = 0;
            esc_ms_ = now_ms;
            return;
        }
        if (ch == '\r' || ch == '\n') { handle_key_code(PK_ENTER); return; }
        if (ch == '\t') { handle_key_code(PK_TAB); return; }
        if (ch == 0x08U || ch == 0x7FU) { handle_key_code(PK_BS); return; }
        handle_key_code(static_cast<int>(ch));
        return;
    }

    if (esc_state_ == 1) {
        if (ch == '[') { esc_state_ = 2; esc_len_ = 0; return; }
        if (ch == 'O') { esc_state_ = 3; esc_len_ = 0; return; }
        esc_state_ = 0;
        handle_key_code(PK_ESC);
        handle_key_code(static_cast<int>(ch));
        return;
    }

    if (esc_len_ + 1 < sizeof(esc_buf_)) {
        esc_buf_[esc_len_++] = static_cast<char>(ch);
        esc_buf_[esc_len_] = '\0';
    }
    if (esc_state_ == 3) {
        esc_state_ = 0;
        if (ch == 'Q') handle_key_code(PK_F2);
        else if (ch == 'A') handle_key_code(PK_UP);
        else if (ch == 'B') handle_key_code(PK_DOWN);
        else if (ch == 'C') handle_key_code(PK_RIGHT);
        else if (ch == 'D') handle_key_code(PK_LEFT);
        return;
    }
    if ((ch >= '@' && ch <= '~')) {
        esc_state_ = 0;
        if (ch == 'c' && (esc_buf_[0] == '?' || esc_buf_[0] == '>')) {
            if (esc_buf_[0] == '?') {
                if (strstr(esc_buf_, ";22") != NULL) term_type_ = PICO_TERM_VT340;
                else if (term_type_ < PICO_TERM_VT220) term_type_ = PICO_TERM_VT220;
            } else {
                term_type_ = PICO_TERM_XTERM256;
            }
            render();
            return;
        }
        if (ch == 'A') handle_key_code(PK_UP);
        else if (ch == 'B') handle_key_code(PK_DOWN);
        else if (ch == 'C') handle_key_code(PK_RIGHT);
        else if (ch == 'D') handle_key_code(PK_LEFT);
        else if (ch == 'H') handle_key_code(PK_HOME);
        else if (ch == 'F') handle_key_code(PK_END);
        else if (ch == 'Z') handle_key_code(PK_BACKTAB);
        else if (ch == '~' && strcmp(esc_buf_, "21~") == 0) handle_key_code(PK_F10);
    }
}

void PicoConsole::handle_key_code(int key) {
    if (prompt_kind_ != PICO_PROMPT_NONE) { handle_modal_key(key); return; }
    switch (view_) {
        case PICO_VIEW_MENU:       handle_menu_key(key); break;
        case PICO_VIEW_SETTINGS:   handle_settings_key(key); break;
        case PICO_VIEW_STORAGE:    handle_storage_key(key); break;
        case PICO_VIEW_BOOTLOADER: handle_bootloader_key(key); break;
        case PICO_VIEW_PSRAM:
            if (key == PK_ESC) { view_ = PICO_VIEW_CONSOLE; render(); }
            break;
        case PICO_VIEW_LOCAL_TERM:
            if (key == PK_ESC || key == PK_TAB) { view_ = PICO_VIEW_CONSOLE; render(); }
            break;
        default:                   handle_console_key(key); break;
    }
}

void PicoConsole::handle_console_key(int key) {
    if (key == 'm' || key == 'M' || key == PK_F10) {
        view_ = PICO_VIEW_MENU; menu_sel_ = 0; render();
    } else if (key == 's' || key == 'S') {
        open_settings(0);
    } else if (key == 'e' || key == 'E') {
        open_settings(4);
    } else if (key == 'g' || key == 'G') {
        view_ = PICO_VIEW_STORAGE; render();
    } else if (key == 'p' || key == 'P') {
        view_ = PICO_VIEW_PSRAM; render();
    } else if (key == 'u' || key == 'U') {
        bootloader_pending_ = 0; view_ = PICO_VIEW_BOOTLOADER; render();
    } else if (key == 'h' || key == 'H') {
        if (node_) draft_ = node_->config();
        prompt_kind_ = PICO_PROMPT_THEME; render();
    } else if (key == 't' || key == 'T') {
        prompt_kind_ = PICO_PROMPT_TERM_SID; render();
    } else if (key == 'b' || key == 'B') {
        prompt_kind_ = PICO_PROMPT_BAN_CALL; render();
    } else if (key == PK_TAB) {
        view_ = PICO_VIEW_LOCAL_TERM; render();
    }
}

void PicoConsole::handle_menu_key(int key) {
    if (key == PK_ESC || key == PK_F10 || key == 'x' || key == 'X') {
        view_ = PICO_VIEW_CONSOLE;
        render();
        return;
    }
    if (key == PK_UP) { menu_sel_ = (menu_sel_ + 11) % 12; render(); return; }
    if (key == PK_DOWN) { menu_sel_ = (menu_sel_ + 1) % 12; render(); return; }
    static const int kKeys[12] = { 'a', 'd', 'l', 's', 'e', 'h', 't', 'b', 'p', 'g', 'u', 'x' };
    int chosen_key = 0;
    if (key == PK_ENTER) {
        chosen_key = kKeys[menu_sel_];
    } else {
        for (int i = 0; i < 11; ++i) {
            if (key == kKeys[i] || key == (kKeys[i] - 32)) {
                chosen_key = kKeys[i];
                break;
            }
        }
    }
    if (!chosen_key) return;
    view_ = PICO_VIEW_CONSOLE;
    if (chosen_key == 'a' && node_) {
        node_->set_accept_incoming(1);
        strcpy(status_fb_, "New incoming connections are now ALLOWED.");
        render();
    } else if (chosen_key == 'd' && node_) {
        node_->set_accept_incoming(0);
        strcpy(status_fb_, "New incoming connections are now DENIED.");
        render();
    } else if (chosen_key == 'l') {
        view_ = PICO_VIEW_LOCAL_TERM;
        render();
    } else if (chosen_key == 'x') {
        render();
    } else {
        handle_console_key(chosen_key);
    }
}

void PicoConsole::handle_settings_key(int key) {
    if (key == PK_ESC) { view_ = PICO_VIEW_CONSOLE; render(); return; }
    if (key >= '1' && key <= '5') {
        settings_tab_ = key - '1'; settings_sel_ = 0; render(); return;
    }
    if (key == PK_TAB) { settings_tab_ = (settings_tab_ + 1) % 5; settings_sel_ = 0; render(); return; }
    if (key == PK_UP && settings_sel_ > 0) { --settings_sel_; render(); return; }
    if (key == PK_DOWN) { ++settings_sel_; render(); return; }
    if (key == 'h' || key == 'H') { prompt_kind_ = PICO_PROMPT_THEME; render(); return; }
    if (key == 's' || key == 'S' || key == PK_F2) { commit_settings(); render(); return; }
    if (settings_tab_ == 3) {
        if (key == 'n' || key == 'N') {
            prompt_kind_ = PICO_PROMPT_ADD_WIZARD; wiz_step_ = 1; wiz_drv_ = 0; render();
        } else if ((key == 'e' || key == 'E' || key == PK_ENTER) && draft_.iface_count > 0) {
            if (settings_sel_ >= draft_.iface_count) settings_sel_ = 0;
            prompt_kind_ = PICO_PROMPT_EDIT_IFACE; render();
        }
    } else if (settings_tab_ == 4) {
        if ((key == 'e' || key == 'E' || key == PK_ENTER) && draft_.peer_count > 0) {
            if (settings_sel_ >= draft_.peer_count) settings_sel_ = 0;
            prompt_kind_ = PICO_PROMPT_EDIT_PEER; render();
        }
    }
}

void PicoConsole::handle_storage_key(int key) {
    if (key == PK_ESC) { view_ = PICO_VIEW_CONSOLE; render(); }
    else if (key == 'f' || key == 'F') {
        prompt_kind_ = PICO_PROMPT_CONFIRM_FORMAT;
        prompt_len_ = 0;
        prompt_buf_[0] = '\0';
        render();
    }
}

void PicoConsole::handle_bootloader_key(int key) {
    const int auto_cnt = node_ ? node_->auto_learned_peer_count() : 0;
    if (key == PK_ESC) { view_ = PICO_VIEW_CONSOLE; render(); return; }
    if (auto_cnt > 0) {
        if (key == 'e' || key == 'E') { open_settings(4); return; }
        if (key == 's' || key == 'S') {
            node_->persist_auto_learned_peers(NULL, 0);
            bootloader_pending_ = 1; bootloader_ms_ = last_ms_; render();
        } else if (key == 'd' || key == 'D') {
            node_->discard_auto_learned_peers();
            bootloader_pending_ = 1; bootloader_ms_ = last_ms_; render();
        }
    } else if (key == 'y' || key == 'Y' || key == PK_ENTER) {
        bootloader_pending_ = 1; bootloader_ms_ = last_ms_; render();
    }
}

void PicoConsole::handle_modal_key(int key) {
    if (key == PK_ESC) { prompt_kind_ = PICO_PROMPT_NONE; render(); return; }
    if (prompt_kind_ == PICO_PROMPT_ADD_WIZARD) {
        if (wiz_step_ == 1 && key >= '1' && key <= '5') {
            wiz_drv_ = key - '1'; wiz_step_ = 2; render(); return;
        }
        if ((key == 'b' || key == 'B' || key == 2) && wiz_step_ > 1) {
            --wiz_step_; render(); return;
        }
        if (key == PK_ENTER) {
            if (wiz_step_ < 4) { ++wiz_step_; render(); return; }
            if (draft_.iface_count < OYK_MAX_INTERFACES) {
                OykIfaceConfig& nc = draft_.ifaces[draft_.iface_count];
                memset(&nc, 0, sizeof(nc));
                strcpy(nc.type, kWizardDrivers[wiz_drv_]);
                oyk_derive_default_iface_id(&draft_, nc.type, nc.id, sizeof(nc.id));
                nc.block_inbound = 0;
                nc.block_outbound = 0;
                nc.allow_same_interface = 1;
                nc.port = 10093U;
                strcpy(nc.host, "0.0.0.0");
                ++draft_.iface_count;
                commit_settings();
            }
            prompt_kind_ = PICO_PROMPT_NONE;
            render();
        }
        return;
    }
    if (prompt_kind_ == PICO_PROMPT_THEME) {
        static const char* const kPresets[3] = { "dark", "light", "amber-crt" };
        int cur = 0;
        for (int i = 0; i < 3; ++i) {
            if (strcmp(draft_.theme_name, kPresets[i]) == 0) cur = i;
        }
        if (key == PK_RIGHT) { strcpy(draft_.theme_name, kPresets[(cur + 1) % 3]); render(); return; }
        if (key == PK_LEFT) { strcpy(draft_.theme_name, kPresets[(cur + 2) % 3]); render(); return; }
        if (key == 's' || key == 'S' || key == PK_ENTER) {
            commit_settings();
            prompt_kind_ = PICO_PROMPT_NONE;
            render();
        }
        return;
    }
    if (prompt_kind_ == PICO_PROMPT_EDIT_IFACE) {
        OykIfaceConfig& ic = draft_.ifaces[settings_sel_];
        if (key == 'i' || key == 'I') ic.block_inbound = !ic.block_inbound;
        else if (key == 'o' || key == 'O') ic.block_outbound = !ic.block_outbound;
        else if (key == 's' || key == 'S') ic.allow_same_interface = !ic.allow_same_interface;
        else if (key == 'l' || key == 'L') {
            ic.learned_peer_policy = static_cast<oyk_u8>((ic.learned_peer_policy + 1U) % 5U);
        } else if (key == PK_ENTER) {
            commit_settings();
            prompt_kind_ = PICO_PROMPT_NONE;
        }
        render();
        return;
    }
    if (prompt_kind_ == PICO_PROMPT_CONFIRM_FORMAT) {
        if (key == PK_ENTER) {
            if (pico_fs_format_storage(prompt_buf_)) strcpy(status_fb_, "Storage formatted.");
            else strcpy(status_fb_, "Format aborted (must type FORMAT).");
            prompt_kind_ = PICO_PROMPT_NONE;
            render();
        } else if (key == PK_BS && prompt_len_ > 0) {
            prompt_buf_[--prompt_len_] = '\0'; render();
        } else if (key >= 32 && key < 127 && prompt_len_ + 1 < sizeof(prompt_buf_)) {
            prompt_buf_[prompt_len_++] = static_cast<char>(key);
            prompt_buf_[prompt_len_] = '\0';
            render();
        }
        return;
    }
    if (key == PK_ENTER) {
        prompt_kind_ = PICO_PROMPT_NONE;
        render();
    }
}
