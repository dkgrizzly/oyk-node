/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_console.hpp"
#include "pico_fs.hpp"
#include "pico_psram.hpp"
#include "pico_radio.hpp"
#include "oyk_rig.hpp"

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

static const char* const kWizardDriverDescs[5] = {
    "AX.25 over UDP/IPv4 via Wi-Fi / CYW43 / RM2",
    "AX.25 over raw IP proto 93 via Wi-Fi / CYW43 / RM2",
    "Point-to-Point Protocol over UART serial (ser0/ser1)",
    "KISS TNC over UART serial (ser0/ser1)",
    "KISS TNC over Bluetooth SPP / CYW43 / RM2"
};

static void pico_str_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int pico_strieq_ci(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 32);
        if (ca != cb) return 0;
    }
    return (*a == *b) ? 1 : 0;
}

static oyk_u8 pico_cycle_lpol(oyk_u8 cur, int dir) {
    return static_cast<oyk_u8>((static_cast<int>(cur) + dir + 5) % 5);
}

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
      files_tab_(0), pending_count_(0), approved_count_(0),
      storage_sel_(0), bootloader_pending_(0), bootloader_ms_(0),
      prompt_len_(0), wiz_edit_idx_(0), wiz_step_(0), wiz_drv_(0),
      wiz_last_drv_(0), wiz_focus_(0), wiz_btn_(0), wiz_flags_(0),
      lterm_sid_(0), lterm_is_local_(0), lterm_line_count_(0), lterm_input_len_(0),
      esc_state_(0), esc_len_(0), x10_len_(0), mouse_enabled_(0), esc_ms_(0), last_ms_(0) {
    memset(settings_sel_, 0, sizeof(settings_sel_));
    memset(files_sel_, 0, sizeof(files_sel_));
    memset(pending_files_, 0, sizeof(pending_files_));
    memset(approved_files_, 0, sizeof(approved_files_));
    oyk_config_init_default(&draft_);
    prompt_label_[0] = '\0';
    prompt_buf_[0] = '\0';
    status_fb_[0] = '\0';
    dialog_fb_[0] = '\0';
    ban_call_tmp_[0] = '\0';
    ban_iface_tmp_[0] = '\0';
    wiz_id_[0] = '\0';
    wiz_desc_[0] = '\0';
    wiz_ep_[0] = '\0';
    wiz_port_[0] = '\0';
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_err_[0] = '\0';
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
    char line[256];
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
    char line[256];
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
    char line[128];
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
    mouse_enabled_ = 0;
    esc_state_ = 0;
    esc_len_ = 0;
    x10_len_ = 0;
    write_str("\033[c\033[>c");
    render();
}

void PicoConsole::disable_mouse() {
    if (mouse_enabled_) {
        mouse_enabled_ = 0;
        write_str("\033[?1006l\033[?1000l");
    }
}

void PicoConsole::render() {
    if (!node_ || !tx_fn_) return;
    if (term_type_ >= PICO_TERM_VT220) {
        write_str("\033[?25l");
    }
    switch (view_) {
        case PICO_VIEW_LOCAL_TERM: render_local_term(); break;
        case PICO_VIEW_FILES:      render_console(); render_files(); break;
        case PICO_VIEW_RIG:        render_console(); render_rig(); break;
        case PICO_VIEW_SETTINGS:   render_console(); render_settings(); break;
        case PICO_VIEW_STORAGE:    render_console(); render_storage(); break;
        case PICO_VIEW_PSRAM:      render_console(); render_psram(); break;
        case PICO_VIEW_BOOTLOADER: render_console(); render_bootloader(); break;
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

    sprintf(buf, "\033[2;1H%s OYK PICO(%s): %-9s Up:%-4lus C:%-2d [IN:%-5s] PSRAM:%-3s RF:%-5s\033[0m",
            sgr_header(), prof->board_id, call_s,
            static_cast<unsigned long>(node_->uptime_seconds()),
            node_->total_active_connections(),
            node_->accept_incoming() ? "ALLOW" : "DENY",
            psram_s, rad_s);
    write_str(buf);

    draw_box(3, 1, 8, 45, "Interfaces", 0);
    draw_box(3, 46, 8, 35, "Last 5 New Callsigns Heard", 0);
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
            const char* role = s.restricted_ban ? "BAN NOTICE"
                                                : (s.in_bbs ? "BBS"
                                                            : (s.in_chat ? "CHAT"
                                                                         : (s.is_local_term ? "LOCAL TERM" : "NODE SHELL")));
            sprintf(buf, "\033[%d;2H #%-3lu %-10.10s %-8.8s %-8.8s %-43.43s",
                    13 + shown, static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local",
                    s.since_hms, role);
            write_str(buf);
            ++shown;
        }
    }
    for (int r = shown; r < 3; ++r) {
        sprintf(buf, "\033[%d;2H%-78.78s",
                13 + r, (r == 0 && shown == 0) ? " (no active connections)" : "");
        write_str(buf);
    }

    draw_box(17, 1, 7, 80, "Node Activity Log", 0);
    const int tot_logs = node_->console_log_count();
    const int start_log = (tot_logs > 5) ? (tot_logs - 5) : 0;
    for (int i = 0; i < 5; ++i) {
        const char* ln = ((start_log + i) < tot_logs) ? node_->console_log_line(start_log + i) : "";
        sprintf(buf, "\033[%d;2H%-78.78s", 18 + i, ln ? ln : "");
        write_str(buf);
    }
    sprintf(buf, "\033[24;1H%s %-78.78s \033[0m",
            sgr_warn(),
            status_fb_[0] ? status_fb_ : "M:Menu  Tab:Term  S:Settings  T:TermConn  B:Ban  H:Theme  G:Storage");
    write_str(buf);
}

void PicoConsole::render_menu() {
    static const char* const kItems[14] = {
        " [A] Allow Incoming Connections   ",
        " [D] Deny Incoming Connections    ",
        " [L] Local Terminal...            ",
        " [F] Manage Files Area...         ",
        " [R] Radio Control...             ",
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
    static const int kItemRows[14] = { 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18 };
    draw_box(2, 2, 18, 36, "Node Menu", 1);
    draw_shadow(2, 2, 18, 36);
    draw_hdiv(5, 2, 36);
    draw_hdiv(17, 2, 36);
    char buf[96];
    for (int i = 0; i < 14; ++i) {
        sprintf(buf, "\033[%d;3H%s%.34s\033[0m",
                kItemRows[i], (i == menu_sel_) ? sgr_menusel() : "", kItems[i]);
        write_str(buf);
    }
}

int PicoConsole::settings_tab_item_count(int tab) const {
    if (tab == 0) return 11;
    if (tab == 1) return 8;
    if (tab == 2) return 6;
    if (tab == 3) return draft_.iface_count;
    if (tab == 4) return draft_.peer_count;
    return 6;
}

void PicoConsole::render_settings() {
    static const char* const kTabs[7] = {
        "1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn"
    };
    static const char* const kNodeLabels[11] = {
        "Node Callsign", "Node Alias", "Welcome Message", "Info Message",
        "Allow Incoming (Default)", "Allow Logging to Flash",
        "Learned Peer Policy", "New User Welcome",
        "Interfaces File", "Peers File", "Themes File"
    };
    static const char* const kBbsLabels[8] = {
        "Enable Packet BBS", "BBS SSID (0-15)", "Callsign Override", "BBS Alias",
        "Enable BBS Files Area", "Files Area Root Path", "Mail Storage Path", "BBS Welcome Message"
    };
    static const char* const kChatLabels[6] = {
        "Enable Multi-User CHAT", "CHAT SSID (0-15)", "Callsign Override", "CHAT Alias",
        "Default Topic", "CHAT Welcome Message"
    };
    static const char* const kRigLabels[6] = {
        "Enable Rig Control", "Protocol (rigctl/radiofoundry)", "Radio Host / IP",
        "Port (4532 / 4191)", "Poll Interval (sec)", "RadioFoundry Notify"
    };
    static const char* const kPrnLabels[6] = {
        "Enable Line Printer", "Device (usb-printer/serN)", "Protocol (generic/mx80/escp2)",
        "Serial Baud Rate", "Printer Log Mask (0x..)", "Condensed 132-Col Mode"
    };

    char buf[192];
    draw_box(2, 3, 21, 74, "Node Settings", 1);
    draw_shadow(2, 3, 21, 74);
    write_str("\033[3;5H");
    for (int t = 0; t < 7; ++t) {
        sprintf(buf, "%s %s \033[0m ", (t == settings_tab_) ? sgr_menusel() : sgr_menubar(), kTabs[t]);
        write_str(buf);
    }
    draw_hdiv(4, 3, 74);

    int cnt = settings_tab_item_count(settings_tab_);
    if (settings_tab_ != 3 && settings_tab_ != 4) {
        const char* const* labels = (settings_tab_ == 0) ? kNodeLabels
                                  : ((settings_tab_ == 1) ? kBbsLabels
                                                          : ((settings_tab_ == 2) ? kChatLabels
                                                                                  : ((settings_tab_ == 5) ? kRigLabels : kPrnLabels)));
        for (int i = 0; i < cnt && i < 13; ++i) {
            char val[48], cs[16];
            val[0] = '\0';
            if (settings_tab_ == 0) {
                if (i == 0) oyk_format_callsign(&draft_.callsign, val, sizeof(val));
                else if (i == 1) { if (draft_.alias.base[0]) oyk_format_callsign(&draft_.alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 2) pico_str_copy(val, 41, draft_.welcome_text);
                else if (i == 3) pico_str_copy(val, 41, draft_.info_text);
                else if (i == 4) strcpy(val, draft_.accept_incoming ? "[ON]" : "[OFF]");
                else if (i == 5) strcpy(val, draft_.allow_flash_logging ? "[ON]" : "[OFF]");
                else if (i == 6) strcpy(val, oyk_format_learned_policy(draft_.learned_peer_policy));
                else if (i == 7) { if (draft_.new_user_welcome[0]) pico_str_copy(val, 41, draft_.new_user_welcome); else strcpy(val, "(use default)"); }
                else if (i == 8) { if (draft_.interfaces_file[0]) pico_str_copy(val, 41, draft_.interfaces_file); else strcpy(val, "<inline>"); }
                else if (i == 9) { if (draft_.peers_file[0]) pico_str_copy(val, 41, draft_.peers_file); else strcpy(val, "<inline>"); }
                else { if (draft_.themes_file[0]) pico_str_copy(val, 41, draft_.themes_file); else strcpy(val, "<inline>"); }
            } else if (settings_tab_ == 1) {
                if (i == 0) strcpy(val, draft_.bbs_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign bc; oyk_config_resolved_bbs_callsign(&draft_, &bc); oyk_format_callsign(&bc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.bbs_ssid), cs); }
                else if (i == 2) { if (draft_.bbs_callsign.base[0]) oyk_format_callsign(&draft_.bbs_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.bbs_alias.base[0]) oyk_format_callsign(&draft_.bbs_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) strcpy(val, draft_.bbs_files_enabled ? "[ON]" : "[OFF]");
                else if (i == 5) pico_str_copy(val, 41, draft_.bbs_files_path);
                else if (i == 6) pico_str_copy(val, 41, draft_.bbs_mail_path);
                else pico_str_copy(val, 41, draft_.bbs_welcome);
            } else if (settings_tab_ == 2) {
                if (i == 0) strcpy(val, draft_.chat_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign cc; oyk_config_resolved_chat_callsign(&draft_, &cc); oyk_format_callsign(&cc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.chat_ssid), cs); }
                else if (i == 2) { if (draft_.chat_callsign.base[0]) oyk_format_callsign(&draft_.chat_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.chat_alias.base[0]) oyk_format_callsign(&draft_.chat_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) pico_str_copy(val, 41, draft_.chat_topic);
                else pico_str_copy(val, 41, draft_.chat_welcome);
            } else if (settings_tab_ == 5) {
                if (i == 0) strcpy(val, draft_.rig_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) pico_str_copy(val, 41, draft_.rig_protocol[0] ? draft_.rig_protocol : "none");
                else if (i == 2) pico_str_copy(val, 41, draft_.rig_host);
                else if (i == 3) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_port));
                else if (i == 4) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_poll_sec));
                else strcpy(val, draft_.rig_notify ? "[ON]" : "[OFF]");
            } else {
                if (i == 0) strcpy(val, draft_.printer_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) pico_str_copy(val, 41, draft_.printer_device[0] ? draft_.printer_device : "(none)");
                else if (i == 2) pico_str_copy(val, 41, draft_.printer_protocol[0] ? draft_.printer_protocol : "generic");
                else if (i == 3) sprintf(val, "%lu", static_cast<unsigned long>(draft_.printer_baud ? draft_.printer_baud : 9600UL));
                else if (i == 4) sprintf(val, "0x%lX", static_cast<unsigned long>(draft_.printer_level));
                else strcpy(val, draft_.printer_condensed ? "[ON]" : "[OFF]");
            }
            sprintf(buf, "\033[%d;5H%s %-26.26s : %-40.40s\033[0m",
                    5 + i, (i == settings_sel_[settings_tab_]) ? sgr_sel() : "", labels[i], val);
            write_str(buf);
        }
        sprintf(buf, "\033[21;4H%s %-70.70s \033[0m",
                sgr_menubar(), "Tab/1-7:Tabs  Up/Dn:Select  Enter/E:Edit  S/F2:Save  Esc:Cancel");
        write_str(buf);
    } else if (settings_tab_ == 3) {
        sprintf(buf, "\033[5;5H%s %-8s %-13s %-21s %-23s\033[0m",
                sgr_list_hdr(), "ID", "Type", "Dev/Host", "Port/Baud");
        write_str(buf);
        if (draft_.iface_count == 0) {
            write_str("\033[6;5H (no interfaces configured; press N to add)");
        } else {
            for (int i = 0; i < draft_.iface_count && i < 12; ++i) {
                const OykIfaceConfig& ic = draft_.ifaces[i];
                const char* loc = ic.device[0] ? ic.device : ic.host;
                char pb[28];
                sprintf(pb, "%u/%lu", static_cast<unsigned int>(ic.port), static_cast<unsigned long>(ic.baud));
                sprintf(buf, "\033[%d;5H%s %-8.8s %-13.13s %-21.21s %-23.23s\033[0m",
                        6 + i, (i == settings_sel_[3]) ? sgr_sel() : "",
                        ic.id, ic.type, loc, pb);
                write_str(buf);
            }
        }
        sprintf(buf, "\033[21;4H%s %-70.70s \033[0m",
                sgr_menubar(), "N/A:New  Enter/E:Edit  D:Del  H:Theme  S/F2:Save  Esc:Cancel");
        write_str(buf);
    } else {
        sprintf(buf, "\033[5;5H%s %-9s %-9s %-7s %-20s %-7s %-11s\033[0m",
                sgr_list_hdr(), "ID", "Callsign", "Iface", "Endpoint", "ACL", "Tag");
        write_str(buf);
        if (draft_.peer_count == 0) {
            write_str("\033[6;5H (no peers configured; press A to add)");
        } else {
            for (int i = 0; i < draft_.peer_count && i < 12; ++i) {
                const OykPeerConfig& pc = draft_.peers[i];
                char ep[26], acl[10];
                if (pc.host[0] && pc.port > 0) sprintf(ep, "%.14s:%u", pc.host, static_cast<unsigned int>(pc.port));
                else if (pc.host[0]) sprintf(ep, "%.19s", pc.host);
                else strcpy(ep, "(any)");
                sprintf(acl, "%s/%s", pc.allow_inbound ? "IN" : "--", pc.allow_outbound ? "OUT" : "---");
                sprintf(buf, "\033[%d;5H%s %-9.9s %-9.9s %-7.7s %-20.20s %-7.7s %-11.11s\033[0m",
                        6 + i, (i == settings_sel_[4]) ? sgr_sel() : "",
                        pc.id, pc.callsign, pc.interface_id, ep, acl,
                        pc.auto_learned ? "[AUTO]" : "");
                write_str(buf);
            }
        }
        sprintf(buf, "\033[21;4H%s %-70.70s \033[0m",
                sgr_menubar(), "A:Add  E:Edit  D:Del  I:In  O:Out  K:KeepAuto  S/F2:Save  Esc:Back");
        write_str(buf);
    }
    sprintf(buf, "\033[19;5H%s%-70.70s\033[0m",
            sgr_warn(), dialog_fb_[0] ? dialog_fb_ : "Edit settings, then press S or F2 to Save & Apply.");
    write_str(buf);
    draw_hdiv(20, 3, 74);
}

void PicoConsole::refresh_files_lists() {
    if (!node_) return;
    pending_count_ = oyk_bbs_list_incoming("incoming", pending_files_, 8);
    approved_count_ = oyk_bbs_list_approved(node_->config().bbs_files_path, approved_files_, 8);
    if (files_sel_[0] >= pending_count_) files_sel_[0] = (pending_count_ > 0) ? (pending_count_ - 1) : 0;
    if (files_sel_[1] >= approved_count_) files_sel_[1] = (approved_count_ > 0) ? (approved_count_ - 1) : 0;
}

void PicoConsole::render_files() {
    char buf[192];
    const int top = 3, left = 4, h = 19, w = 72;
    draw_box(top, left, h, w, "Files Area Manager", 1);
    draw_shadow(top, left, h, w);
    sprintf(buf, "\033[%d;%dH%s 1:Pending Incoming (%d) \033[0m  %s 2:Approved Files (%d) \033[0m",
            top + 1, left + 2,
            (files_tab_ == 0) ? sgr_menusel() : sgr_menubar(), pending_count_,
            (files_tab_ == 1) ? sgr_menusel() : sgr_menubar(), approved_count_);
    write_str(buf);
    draw_hdiv(top + 2, left, w);

    if (files_tab_ == 0) {
        sprintf(buf, "\033[%d;%dH%s %-15s %-37s %-12s \033[0m",
                top + 3, left + 2, sgr_list_hdr(), "Uploader", "Filename", "Size");
        write_str(buf);
        for (int i = 0; i < 10; ++i) {
            if (pending_count_ == 0 && i == 0) {
                sprintf(buf, "\033[%d;%dH %-66s", top + 4 + i, left + 2, "(no pending uploads in incoming/)");
                write_str(buf);
            } else if (i < pending_count_) {
                sprintf(buf, "\033[%d;%dH%s %-15.15s %-37.37s %-12lu \033[0m",
                        top + 4 + i, left + 2, (i == files_sel_[0]) ? sgr_sel() : "",
                        pending_files_[i].uploader, pending_files_[i].filename,
                        static_cast<unsigned long>(pending_files_[i].size));
                write_str(buf);
            }
        }
        draw_hdiv(top + 16, left, w);
        sprintf(buf, "\033[%d;%dH%s %-68.68s \033[0m",
                top + 17, left + 1, sgr_menubar(),
                "Tab/1-2:Tabs  Enter/A:Approve  D:Delete  R:Refresh  Esc:Close");
        write_str(buf);
    } else {
        sprintf(buf, "\033[%d;%dH%s %-53s %-12s \033[0m",
                top + 3, left + 2, sgr_list_hdr(), "Approved File Path", "Size");
        write_str(buf);
        for (int i = 0; i < 10; ++i) {
            if (approved_count_ == 0 && i == 0) {
                sprintf(buf, "\033[%d;%dH %-66s", top + 4 + i, left + 2, "(no approved files in BBS files area)");
                write_str(buf);
            } else if (i < approved_count_) {
                sprintf(buf, "\033[%d;%dH%s %-53.53s %-12lu \033[0m",
                        top + 4 + i, left + 2, (i == files_sel_[1]) ? sgr_sel() : "",
                        approved_files_[i].rel_path,
                        static_cast<unsigned long>(approved_files_[i].size));
                write_str(buf);
            }
        }
        draw_hdiv(top + 16, left, w);
        sprintf(buf, "\033[%d;%dH%s %-68.68s \033[0m",
                top + 17, left + 1, sgr_menubar(),
                "Tab/1-2:Tabs  D:Remove Approved  R:Refresh  Esc:Close");
        write_str(buf);
    }
    sprintf(buf, "\033[%d;%dH%s%-68.68s\033[0m",
            top + 15, left + 2, sgr_warn(),
            dialog_fb_[0] ? dialog_fb_ : "Select a file or press Esc to close.");
    write_str(buf);
}

void PicoConsole::render_rig() {
    char buf[160];
    draw_box(4, 6, 15, 68, "Radio Control (Hamlib / RadioFoundry)", 1);
    draw_shadow(4, 6, 15, 68);
    const OykConfig& cfg = node_->config();
    if (!cfg.rig_enabled) {
        write_str("\033[6;8HRig control is DISABLED. Configure in Settings (S) -> Tab 6:Rig.");
    } else {
        OykRigStatus st;
        oyk_rig_status_init(&st, oyk_rig_parse_protocol(cfg.rig_protocol));
        char summary[96];
        oyk_rig_format_summary(&st, summary, sizeof(summary));
        sprintf(buf, "\033[6;8HProtocol : %-14s  Target: %s:%u",
                oyk_rig_protocol_name(st.protocol),
                cfg.rig_host[0] ? cfg.rig_host : "(none)",
                static_cast<unsigned int>(cfg.rig_port ? cfg.rig_port : oyk_rig_default_port(st.protocol)));
        write_str(buf);
        sprintf(buf, "\033[8;8HRadio    : %.54s", summary);
        write_str(buf);
    }
    draw_hdiv(16, 6, 68);
    sprintf(buf, "\033[17;7H%s %-64.64s \033[0m", sgr_menubar(), "Esc:Close");
    write_str(buf);
}

void PicoConsole::render_storage() {
    const PicoFsStatus* fs = pico_fs_status();
    char buf[128];
    draw_box(3, 5, 18, 70, "Storage Manager (SD / QSPI Flash)", 1);
    draw_shadow(3, 5, 18, 70);
    sprintf(buf, "\033[5;7HActive Medium : %s", fs->medium_label);
    write_str(buf);
    sprintf(buf, "\033[6;7HSD Card       : %s  | Flash KV: %s",
            fs->sd_present ? "DETECTED" : "ABSENT",
            fs->flash_ready ? "READY" : "N/A");
    write_str(buf);
    sprintf(buf, "\033[7;7HCapacity      : %lu KB Total (%lu KB Used)",
            static_cast<unsigned long>(fs->total_kb),
            static_cast<unsigned long>(fs->used_kb));
    write_str(buf);
    write_str("\033[10;7H[ F ] Format Active Storage (Confirmation Modal Required)");
    if (status_fb_[0]) {
        sprintf(buf, "\033[13;7H%s%-60.60s\033[0m", sgr_warn(), status_fb_);
        write_str(buf);
    }
    draw_hdiv(18, 5, 70);
    sprintf(buf, "\033[19;6H%s %-66.66s \033[0m",
            sgr_menubar(), "F:Format Active Storage   Esc:Back to Console");
    write_str(buf);
}

void PicoConsole::render_psram() {
    const PicoPsramInfo* pi = pico_psram_info();
    const PicoBoardProfile* prof = pico_current_board_profile();
    const PicoRadioState* rs = pico_radio_state();
    char buf[128];
    draw_box(3, 5, 17, 70, "PSRAM & Hardware Diagnostics", 1);
    draw_shadow(3, 5, 17, 70);
    sprintf(buf, "\033[5;7HBoard Profile : %s (%s)", prof->board_id, prof->board_label);
    write_str(buf);
    sprintf(buf, "\033[6;7HFlash / SRAM  : %lu MB Flash | %lu KB SRAM",
            static_cast<unsigned long>(prof->flash_bytes / (1024UL * 1024UL)),
            static_cast<unsigned long>(prof->sram_bytes / 1024UL));
    write_str(buf);
    sprintf(buf, "\033[7;7HPSRAM Status  : %s", pi->chip_label);
    write_str(buf);
    sprintf(buf, "\033[8;7HPSRAM Pool    : %lu / %lu bytes (SRAM fallback: %lu bytes)",
            static_cast<unsigned long>(pi->pool_used_bytes),
            static_cast<unsigned long>(pi->pool_total_bytes),
            static_cast<unsigned long>(pi->sram_fallback_used_bytes));
    write_str(buf);
    sprintf(buf, "\033[10;7HWireless Radio: %s (Wi-Fi: %s, BT: %s)",
            rs->module_present ? "PRESENT" : "ABSENT (non-fatal)",
            rs->wifi_enabled ? "ON" : "OFF",
            rs->bt_enabled ? "ON" : "OFF");
    write_str(buf);
    write_str("\033[11;7HHardware UARTs: ser0 (UART0 TX=0/RX=1) | ser1 (UART1 TX=4/RX=5)");
    draw_hdiv(17, 5, 70);
    sprintf(buf, "\033[18;6H%s %-66.66s \033[0m", sgr_menubar(), "Esc:Return");
    write_str(buf);
}

void PicoConsole::render_bootloader() {
    char call_s[16], buf[128];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    const int auto_cnt = node_->auto_learned_peer_count();
    if (bootloader_pending_) {
        draw_box(8, 12, 7, 56, "BOOTSEL UF2 Reboot", 1);
        draw_shadow(8, 12, 7, 56);
        write_str("\033[10;14HEntering RP2xxx USB BOOTSEL mode...");
        sprintf(buf, "\033[11;14H%s73 de %s\033[0m", sgr_title(), call_s);
        write_str(buf);
        return;
    }
    if (auto_cnt > 0) {
        draw_box(5, 6, 13, 68, "Auto-Learned Peers - BOOTSEL", 1);
        draw_shadow(5, 6, 13, 68);
        sprintf(buf, "\033[7;8H%d auto-learned peer(s) added during this session.", auto_cnt);
        write_str(buf);
        write_str("\033[9;8HSave to peers config, discard, or review before BOOTSEL?");
        write_str("\033[11;8H[S] Save & BOOTSEL        [D] Discard & BOOTSEL");
        write_str("\033[12;8H[R] Review Peers          [N / Esc] Cancel");
        draw_hdiv(15, 6, 68);
        sprintf(buf, "\033[16;7H%s %-64.64s \033[0m",
                sgr_menubar(), "S:Save & Reboot  D:Discard  R/E:Review Peers  N/Esc:Cancel");
        write_str(buf);
    } else {
        draw_box(6, 10, 10, 60, "Enter BOOTSEL Mode (UF2 Flash)", 1);
        draw_shadow(6, 10, 10, 60);
        write_str("\033[8;12HPress Y or Enter to reboot into RP2xxx BOOTSEL mode.");
        write_str("\033[9;12HThe device will mount as RPI-RP2 / RP2350 USB drive.");
        draw_hdiv(13, 10, 60);
        sprintf(buf, "\033[14;11H%s %-56.56s \033[0m",
                sgr_menubar(), "Y/Enter:Enter BOOTSEL   N/Esc:Cancel");
        write_str(buf);
    }
}

void PicoConsole::render_local_term() {
    char buf[128];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s OYK LOCAL TERMINAL (%s) | Esc:Console  /local /bbs /chat /disc   \033[0m\r\n",
            sgr_header(), lterm_sid_ ? "CONNECTED" : "DISCONNECTED");
    write_str(buf);
    for (int i = 0; i < lterm_line_count_ && i < 12; ++i) {
        sprintf(buf, "\033[%d;1H%-79.79s", 3 + i, lterm_lines_[i]);
        write_str(buf);
    }
    sprintf(buf, "\033[23;1H> %-76.76s", lterm_input_);
    write_str(buf);
    sprintf(buf, "\033[24;1H%s Commands: /local /bbs /chat /disc   Esc/Tab:Return to Console        \033[0m",
            sgr_menubar());
    write_str(buf);
}

void PicoConsole::wizard_apply_driver_defaults() {
    wiz_last_drv_ = wiz_drv_;
    const char* drv = kWizardDrivers[wiz_drv_];
    oyk_derive_default_iface_id(&draft_, drv, wiz_id_, sizeof(wiz_id_));
    wiz_desc_[0] = '\0';
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    if (wiz_drv_ == 0) {
        strcpy(wiz_ep_, "0.0.0.0");
        strcpy(wiz_port_, "10093");
    } else if (wiz_drv_ == 1) {
        strcpy(wiz_ep_, "0.0.0.0");
        wiz_port_[0] = '\0';
    } else if (wiz_drv_ == 2) {
        strcpy(wiz_ep_, "ser0");
        strcpy(wiz_port_, "115200");
    } else if (wiz_drv_ == 3) {
        strcpy(wiz_ep_, "ser1");
        strcpy(wiz_port_, "9600");
    } else {
        strcpy(wiz_ep_, "bt0");
        strcpy(wiz_port_, "9600");
    }
    wiz_flags_ = 0x23U;
}

int PicoConsole::wizard_step_field_count() const {
    if (wiz_step_ == 1) return 2;
    if (wiz_step_ == 2) {
        if (wiz_drv_ == 0) return 8;
        if (wiz_drv_ == 1) return 7;
        return 6;
    }
    return 0;
}

int PicoConsole::edit_iface_field_count() const {
    if (wiz_drv_ == 0) return 11;
    if (wiz_drv_ == 1) return 10;
    return 9;
}

void PicoConsole::commit_iface_fields(int slot) {
    if (slot < 0 || slot >= OYK_MAX_INTERFACES) return;
    OykIfaceConfig& ic = draft_.ifaces[slot];
    int is_new = (slot >= draft_.iface_count) ? 1 : 0;
    memset(&ic, 0, sizeof(ic));
    pico_str_copy(ic.id, sizeof(ic.id), wiz_id_);
    pico_str_copy(ic.type, sizeof(ic.type), kWizardDrivers[wiz_drv_]);
    pico_str_copy(ic.description, sizeof(ic.description), wiz_desc_);
    if (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 4) {
        pico_str_copy(ic.device, sizeof(ic.device), wiz_ep_);
        ic.baud = static_cast<oyk_u32>(strtoul(wiz_port_, NULL, 10));
        if (ic.baud == 0) ic.baud = (wiz_drv_ == 2) ? 115200UL : 9600UL;
    } else {
        pico_str_copy(ic.host, sizeof(ic.host), wiz_ep_);
        ic.port = (wiz_drv_ == 1) ? 0U : static_cast<oyk_u16>(strtoul(wiz_port_, NULL, 10));
        pico_str_copy(ic.netmask, sizeof(ic.netmask), wiz_mask_);
        if (wiz_peer_[0] && (wiz_drv_ == 0 || wiz_drv_ == 1)) {
            OykPeerEntry pe;
            memset(&pe, 0, sizeof(pe));
            if (oyk_parse_peer_spec(wiz_peer_, pe.callsign, sizeof(pe.callsign),
                                    &pe.ip, &pe.port, (wiz_drv_ == 0) ? 10093U : 0U)) {
                ic.peers[0] = pe;
                ic.peer_count = 1;
            }
        }
    }
    ic.block_inbound = (wiz_flags_ & 1U) ? 0 : 1;
    ic.block_outbound = (wiz_flags_ & 2U) ? 0 : 1;
    ic.allow_same_interface = (wiz_flags_ & 0x20U) ? 1 : 0;
    ic.learned_peer_policy = static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);
    if (is_new) {
        draft_.iface_count++;
        settings_sel_[3] = slot;
        sprintf(dialog_fb_, "Added interface %s in draft (press S/F2 to save).", ic.id);
    } else {
        sprintf(dialog_fb_, "Updated interface %s in draft (press S/F2 to save).", ic.id);
    }
}

void PicoConsole::open_add_iface_wizard() {
    if (draft_.iface_count >= OYK_MAX_INTERFACES) {
        strcpy(dialog_fb_, "Maximum interface slots reached.");
        render();
        return;
    }
    prompt_kind_ = PICO_PROMPT_ADD_WIZARD;
    wiz_edit_idx_ = -1;
    wiz_step_ = 0;
    wiz_drv_ = 0;
    wiz_last_drv_ = -1;
    wiz_focus_ = 0;
    wiz_btn_ = 1;
    wiz_err_[0] = '\0';
    wizard_apply_driver_defaults();
    render();
}

void PicoConsole::open_edit_iface_dialog(int idx) {
    if (idx < 0 || idx >= draft_.iface_count) return;
    prompt_kind_ = PICO_PROMPT_EDIT_IFACE;
    wiz_edit_idx_ = idx;
    wiz_focus_ = 1;
    wiz_btn_ = 0;
    wiz_err_[0] = '\0';
    const OykIfaceConfig& ic = draft_.ifaces[idx];
    wiz_drv_ = 0;
    for (int i = 0; i < 5; ++i) {
        if (pico_strieq_ci(ic.type, kWizardDrivers[i])) { wiz_drv_ = i; break; }
    }
    wiz_last_drv_ = wiz_drv_;
    pico_str_copy(wiz_id_, sizeof(wiz_id_), ic.id);
    pico_str_copy(wiz_desc_, sizeof(wiz_desc_), ic.description);
    pico_str_copy(wiz_ep_, sizeof(wiz_ep_), ic.device[0] ? ic.device : ic.host);
    if (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 4) {
        sprintf(wiz_port_, "%lu", static_cast<unsigned long>(ic.baud ? ic.baud : 9600UL));
    } else if (wiz_drv_ != 1) {
        sprintf(wiz_port_, "%u", static_cast<unsigned int>(ic.port));
    } else {
        wiz_port_[0] = '\0';
    }
    pico_str_copy(wiz_mask_, sizeof(wiz_mask_), ic.netmask);
    wiz_peer_[0] = '\0';
    if (ic.peer_count > 0 && (wiz_drv_ == 0 || wiz_drv_ == 1)) {
        oyk_format_peer_spec(&ic.peers[0], wiz_peer_, sizeof(wiz_peer_));
    }
    wiz_flags_ = static_cast<oyk_u8>((ic.block_inbound ? 0U : 1U) |
                                     (ic.block_outbound ? 0U : 2U) |
                                     ((ic.learned_peer_policy & 0x07U) << 2) |
                                     (ic.allow_same_interface ? 0x20U : 0U));
    render();
}

void PicoConsole::render_wizard_modal() {
    char prog[24], buf[192];
    const int top = 3, left = 6, h = 19, w = 68;
    draw_box(top, left, h, w, "Add Interface", 1);
    draw_shadow(top, left, h, w);
    int fill = (wiz_step_ + 1) * 2;
    for (int i = 0; i < 8; ++i) prog[i] = (i < fill) ? '#' : '.';
    prog[8] = '\0';
    sprintf(buf, "\033[%d;%dH%s[%s] %d/4\033[0m", top, left + w - 16, sgr_title(), prog, wiz_step_ + 1);
    write_str(buf);

    const char* drv = kWizardDrivers[wiz_drv_];
    const char* hdr = "Select driver (1-5 or Up/Dn, Enter:Next):";
    if (wiz_step_ == 1) hdr = "Enter a unique interface ID and optional description:";
    else if (wiz_step_ == 2) hdr = "Configure endpoint, direction controls, and learned policy:";
    else if (wiz_step_ == 3) hdr = "Review interface configuration before committing:";
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 1, left + 2, sgr_title(), hdr);
    write_str(buf);

    const int ain = (wiz_flags_ & 1U) ? 1 : 0;
    const int aout = (wiz_flags_ & 2U) ? 1 : 0;
    const int asame = (wiz_flags_ & 0x20U) ? 1 : 0;
    const oyk_u8 lpol = static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    if (wiz_step_ == 0) {
        sprintf(buf, "\033[%d;%dH%s %-2s %-15s %-44s \033[0m",
                top + 2, left + 1, sgr_list_hdr(), "#", "Driver", "Description");
        write_str(buf);
        for (int i = 0; i < 5; ++i) {
            sprintf(buf, "\033[%d;%dH%s %d. %-15.15s %-44.44s \033[0m",
                    top + 3 + i, left + 1, (i == wiz_drv_) ? sgr_sel() : "",
                    i + 1, kWizardDrivers[i], kWizardDriverDescs[i]);
            write_str(buf);
        }
    } else if (wiz_step_ == 1) {
        sprintf(buf, "\033[%d;%dHDriver       : %s", top + 3, left + 3, drv);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHInterface ID : %s %-36.36s \033[0m",
                top + 5, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", wiz_id_);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHDescription  : %s %-36.36s \033[0m",
                top + 7, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", wiz_desc_);
        write_str(buf);
    } else if (wiz_step_ == 2) {
        const char* ep_lbl = (wiz_drv_ >= 2) ? "Serial/BT Dev  " : "Bind Host/DMR  ";
        sprintf(buf, "\033[%d;%dHInterface    : %s (%s)", top + 2, left + 3, wiz_id_, drv);
        write_str(buf);
        int r = top + 4;
        int fi = 0;
        sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                r++, left + 3, ep_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_ep_);
        write_str(buf); ++fi;
        if (wiz_drv_ != 1) {
            const char* p_lbl = (wiz_drv_ >= 2) ? "Baud Rate      " : "UDP Port       ";
            sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                    r++, left + 3, p_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_port_);
            write_str(buf); ++fi;
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1) {
            sprintf(buf, "\033[%d;%dHNetmask (opt)  : %s %-36.36s \033[0m",
                    r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_mask_);
            write_str(buf); ++fi;
            sprintf(buf, "\033[%d;%dHPeer (opt)     : %s %-36.36s \033[0m",
                    r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_peer_);
            write_str(buf); ++fi;
        }
        sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-12.12s \033[0m",
                r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", ain ? "[ON]" : "[OFF]");
        write_str(buf); ++fi;
        sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-12.12s \033[0m",
                r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", aout ? "[ON]" : "[OFF]");
        write_str(buf); ++fi;
        sprintf(buf, "\033[%d;%dHSame-Iface Fwd : %s %-12.12s \033[0m",
                r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", asame ? "[ON]" : "[OFF]");
        write_str(buf); ++fi;
        sprintf(buf, "\033[%d;%dHLearned Policy : %s < %s > \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", oyk_format_learned_policy(lpol));
        write_str(buf);
    } else {
        sprintf(buf, "\033[%d;%dHDriver       : %-46.46s", top + 3, left + 3, drv);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHInterface ID : %-46.46s", top + 4, left + 3, wiz_id_);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHEndpoint     : %-46.46s", top + 5, left + 3, wiz_ep_);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHPolicy       : In=%s  Out=%s  SameIf=%s  Learn=%s",
                top + 7, left + 3, ain ? "ON" : "OFF", aout ? "ON" : "OFF",
                asame ? "ON" : "OFF", oyk_format_learned_policy(lpol));
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%sPress Finish to add this interface to Settings draft.\033[0m",
                top + 9, left + 3, sgr_title());
        write_str(buf);
    }

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 15, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }
    draw_hdiv(top + 16, left, w);
    int fcnt = wizard_step_field_count();
    int on_bar = (wiz_step_ == 0 || wiz_step_ == 3 || wiz_focus_ == fcnt);
    sprintf(buf, "\033[%d;%dH%s %s \033[0m  %s %s \033[0m  %s [ Cancel ] \033[0m",
            top + 17, left + 2,
            (wiz_step_ == 0) ? "" : ((on_bar && wiz_btn_ == 0) ? sgr_sel() : sgr_menubar()),
            (wiz_step_ == 0) ? "          " : "[ < Back ]",
            (on_bar && wiz_btn_ == 1) ? sgr_sel() : sgr_menubar(),
            (wiz_step_ == 3) ? "[ Finish ]" : "[ Next > ]",
            (on_bar && wiz_btn_ == 2) ? sgr_sel() : sgr_menubar());
    write_str(buf);
}

void PicoConsole::render_edit_iface_modal() {
    char buf[192];
    const int top = 3, left = 6, h = 19, w = 68;
    draw_box(top, left, h, w, "Edit Interface", 1);
    draw_shadow(top, left, h, w);
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m",
            top + 1, left + 2, sgr_title(), "Edit interface settings and select Save to update draft:");
    write_str(buf);

    const int ain = (wiz_flags_ & 1U) ? 1 : 0;
    const int aout = (wiz_flags_ & 2U) ? 1 : 0;
    const int asame = (wiz_flags_ & 0x20U) ? 1 : 0;
    const oyk_u8 lpol = static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    char drv_lbl[32];
    sprintf(drv_lbl, "< %s >", kWizardDrivers[wiz_drv_]);
    sprintf(buf, "\033[%d;%dHDriver (Lt/Rt) : %s %-36.36s \033[0m",
            top + 3, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", drv_lbl);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface ID   : %s %-36.36s \033[0m",
            top + 4, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHDescription    : %s %-36.36s \033[0m",
            top + 5, left + 3, (wiz_focus_ == 2) ? sgr_menubar() : "", wiz_desc_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
            top + 6, left + 3, (wiz_drv_ >= 2) ? "Serial/BT Dev  " : "Host / DMR ID  ",
            (wiz_focus_ == 3) ? sgr_menubar() : "", wiz_ep_);
    write_str(buf);

    int r = top + 7;
    int fi = 4;
    if (wiz_drv_ != 1) {
        sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                r++, left + 3, (wiz_drv_ >= 2) ? "Baud Rate      " : "UDP Port       ",
                (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_port_);
        write_str(buf); ++fi;
    }
    if (wiz_drv_ == 0 || wiz_drv_ == 1) {
        sprintf(buf, "\033[%d;%dHNetmask (opt)  : %s %-36.36s \033[0m",
                r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_mask_);
        write_str(buf); ++fi;
        sprintf(buf, "\033[%d;%dHStatic Peer    : %s %-36.36s \033[0m",
                r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_peer_);
        write_str(buf); ++fi;
    }
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-12.12s \033[0m",
            r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", ain ? "[ON]" : "[OFF]");
    write_str(buf); ++fi;
    sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-12.12s \033[0m",
            r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", aout ? "[ON]" : "[OFF]");
    write_str(buf); ++fi;
    sprintf(buf, "\033[%d;%dHSame-Iface Fwd : %s %-12.12s \033[0m",
            r++, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", asame ? "[ON]" : "[OFF]");
    write_str(buf); ++fi;
    sprintf(buf, "\033[%d;%dHLearned Policy : %s < %s > \033[0m",
            r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", oyk_format_learned_policy(lpol));
    write_str(buf);

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 15, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }
    draw_hdiv(top + 16, left, w);
    int fcnt = edit_iface_field_count();
    int on_bar = (wiz_focus_ == fcnt);
    sprintf(buf, "\033[%d;%dH%s [  Save  ] \033[0m  %s [ Cancel ] \033[0m  Up/Dn:Field Enter:Save",
            top + 17, left + 2,
            (on_bar && wiz_btn_ == 0) ? sgr_sel() : sgr_menubar(),
            (on_bar && wiz_btn_ == 1) ? sgr_sel() : sgr_menubar());
    write_str(buf);
}

void PicoConsole::open_edit_peer_dialog(int idx) {
    prompt_kind_ = PICO_PROMPT_EDIT_PEER;
    wiz_edit_idx_ = idx;
    wiz_focus_ = (idx >= 0) ? 0 : 1;
    wiz_btn_ = 0;
    wiz_err_[0] = '\0';
    if (idx >= 0 && idx < draft_.peer_count) {
        const OykPeerConfig& pc = draft_.peers[idx];
        pico_str_copy(wiz_id_, sizeof(wiz_id_), pc.id);
        pico_str_copy(ban_call_tmp_, sizeof(ban_call_tmp_), pc.callsign);
        pico_str_copy(wiz_desc_, sizeof(wiz_desc_), pc.description);
        pico_str_copy(ban_iface_tmp_, sizeof(ban_iface_tmp_), pc.interface_id);
        pico_str_copy(wiz_ep_, sizeof(wiz_ep_), pc.host);
        if (pc.port > 0) sprintf(wiz_port_, "%u", static_cast<unsigned int>(pc.port));
        else wiz_port_[0] = '\0';
        wiz_step_ = (pc.allow_inbound ? 1 : 0) | (pc.allow_outbound ? 2 : 0);
        wiz_last_drv_ = 1;
    } else {
        wiz_id_[0] = '\0';
        ban_call_tmp_[0] = '\0';
        wiz_desc_[0] = '\0';
        ban_iface_tmp_[0] = '\0';
        if (draft_.iface_count > 0) pico_str_copy(ban_iface_tmp_, sizeof(ban_iface_tmp_), draft_.ifaces[0].id);
        wiz_ep_[0] = '\0';
        wiz_port_[0] = '\0';
        wiz_step_ = 3;
        wiz_last_drv_ = 0;
    }
    render();
}

void PicoConsole::commit_peer_fields(int slot) {
    if (slot < 0 || slot >= OYK_MAX_CONFIG_PEERS) return;
    int is_new = (slot >= draft_.peer_count) ? 1 : 0;
    OykCallsign cs;
    oyk_parse_callsign(ban_call_tmp_, &cs);
    char cfmt[16];
    oyk_format_callsign(&cs, cfmt, sizeof(cfmt));
    OykPeerConfig& pc = draft_.peers[slot];
    memset(&pc, 0, sizeof(pc));
    pico_str_copy(pc.id, sizeof(pc.id), wiz_id_);
    pico_str_copy(pc.callsign, sizeof(pc.callsign), cfmt);
    pico_str_copy(pc.description, sizeof(pc.description), wiz_desc_);
    pico_str_copy(pc.interface_id, sizeof(pc.interface_id), ban_iface_tmp_);
    pico_str_copy(pc.host, sizeof(pc.host), wiz_ep_);
    pc.port = wiz_port_[0] ? static_cast<oyk_u16>(strtoul(wiz_port_, NULL, 10)) : 0;
    pc.allow_inbound = (wiz_step_ & 1) ? 1 : 0;
    pc.allow_outbound = (wiz_step_ & 2) ? 1 : 0;
    pc.auto_learned = 0;
    if (is_new) {
        draft_.peer_count++;
        settings_sel_[4] = slot;
        sprintf(dialog_fb_, "Added peer %s in draft (press S/F2 to save).", pc.id);
    } else {
        sprintf(dialog_fb_, "Updated peer %s in draft (press S/F2 to save).", pc.id);
    }
}

void PicoConsole::render_edit_peer_modal() {
    char buf[192];
    const int top = 3, left = 8, h = 18, w = 64;
    draw_box(top, left, h, w, (wiz_edit_idx_ >= 0) ? "Edit Peer" : "Add Peer", 1);
    draw_shadow(top, left, h, w);
    sprintf(buf, "\033[%d;%dH%s %-58.58s \033[0m",
            top + 1, left + 2, sgr_title(), "Configure peer mapping and inbound/outbound permissions:");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHPeer ID        : %s %-36.36s \033[0m",
            top + 3, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHCallsign       : %s %-36.36s \033[0m",
            top + 4, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", ban_call_tmp_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHDescription    : %s %-36.36s \033[0m",
            top + 5, left + 3, (wiz_focus_ == 2) ? sgr_menubar() : "", wiz_desc_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface (Lt/Rt): %s < %-12.12s > \033[0m",
            top + 6, left + 3, (wiz_focus_ == 3) ? sgr_menubar() : "", ban_iface_tmp_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHHost / DMR ID  : %s %-36.36s \033[0m",
            top + 7, left + 3, (wiz_focus_ == 4) ? sgr_menubar() : "", wiz_ep_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHPort (opt)     : %s %-36.36s \033[0m",
            top + 8, left + 3, (wiz_focus_ == 5) ? sgr_menubar() : "", wiz_port_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-12.12s \033[0m",
            top + 10, left + 3, (wiz_focus_ == 6) ? sgr_menubar() : "", (wiz_step_ & 1) ? "[ON]" : "[OFF]");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-12.12s \033[0m",
            top + 11, left + 3, (wiz_focus_ == 7) ? sgr_menubar() : "", (wiz_step_ & 2) ? "[ON]" : "[OFF]");
    write_str(buf);
    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-58.58s \033[0m", top + 14, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }
    draw_hdiv(top + 15, left, w);
    sprintf(buf, "\033[%d;%dH%s [  Save  ] \033[0m  %s [ Cancel ] \033[0m  Up/Dn:Field Enter:Save",
            top + 16, left + 2,
            (wiz_focus_ == 8 && wiz_btn_ == 0) ? sgr_sel() : sgr_menubar(),
            (wiz_focus_ == 8 && wiz_btn_ == 1) ? sgr_sel() : sgr_menubar());
    write_str(buf);
}

void PicoConsole::open_terminate_dialog() {
    if (!node_ || node_->total_active_connections() == 0) {
        strcpy(status_fb_, "No active connections to terminate.");
        render();
        return;
    }
    prompt_kind_ = PICO_PROMPT_TERM_SID;
    wiz_edit_idx_ = 0;
    render();
}

void PicoConsole::render_terminate_modal() {
    char buf[192];
    const int top = 4, left = 8, h = 16, w = 64;
    draw_box(top, left, h, w, "Terminate Active Connection", 1);
    draw_shadow(top, left, h, w);
    sprintf(buf, "\033[%d;%dH%s %-5s %-11s %-10s %-30s \033[0m",
            top + 2, left + 2, sgr_list_hdr(), "ID", "Callsign", "Port", "State");
    write_str(buf);
    int row = 0;
    const int list_h = h - 6;
    for (int i = 0; i < node_->max_sessions() && row < list_h; ++i) {
        const OykSession& s = node_->session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[16], id_s[16];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(id_s, "#%lu", static_cast<unsigned long>(s.id));
            const char* ifid = (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local";
            sprintf(buf, "\033[%d;%dH%s %-5s %-11.11s %-10.10s %-30.30s \033[0m",
                    top + 3 + row, left + 2,
                    (row == wiz_edit_idx_) ? sgr_sel() : "",
                    id_s, cs, ifid, "[CONNECTED]");
            write_str(buf);
            ++row;
        }
    }
    if (row == 0) {
        sprintf(buf, "\033[%d;%dH(no active connections)", top + 4, left + 4);
        write_str(buf);
    }
    draw_hdiv(top + h - 3, left, w);
    sprintf(buf, "\033[%d;%dHUp/Down: select | Enter: terminate | Esc: cancel",
            top + h - 2, left + 2);
    write_str(buf);
}

void PicoConsole::open_ban_dialog() {
    prompt_kind_ = PICO_PROMPT_BAN_CALL;
    ban_call_tmp_[0] = '\0';
    strcpy(wiz_id_, "permanent");
    strcpy(wiz_desc_, "Banned by sysop.");
    wiz_flags_ = 0x02U;
    wiz_focus_ = 0;
    wiz_edit_idx_ = 0;
    wiz_btn_ = 0;
    wiz_err_[0] = '\0';
    render();
}

void PicoConsole::render_ban_modal() {
    char buf[192];
    const int top = 2, left = 3, h = 21, w = 74;
    draw_box(top, left, h, w, "Ban / Unban Callsign (banlist.cfg)", 1);
    draw_shadow(top, left, h, w);

    sprintf(buf, "\033[%d;%dHCallsign: %s %-12.12s \033[0m",
            top + 2, left + 2, (wiz_focus_ == 0 && !wiz_btn_) ? sgr_sel() : sgr_menubar(), ban_call_tmp_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHLift At : %s %-15.15s \033[0m  (permanent, 30m, 12h, 7d, YYYY-MM-DD)",
            top + 3, left + 2, (wiz_focus_ == 1 && !wiz_btn_) ? sgr_sel() : sgr_menubar(), wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHReason  : %s %-56.56s \033[0m",
            top + 4, left + 2, (wiz_focus_ == 2 && !wiz_btn_) ? sgr_sel() : sgr_menubar(), wiz_desc_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%sOptions : [%c] Hard Ban (H)   [%c] Appeal Allowed (A)     \033[0m",
            top + 5, left + 2,
            (wiz_focus_ == 3 && !wiz_btn_) ? sgr_sel() : "",
            (wiz_flags_ & 1U) ? 'X' : ' ',
            (wiz_flags_ & 2U) ? 'X' : ' ');
    write_str(buf);

    int bcnt = node_->ban_count();
    sprintf(buf, "\033[%d;%dHActive Bans (%d):", top + 7, left + 2, bcnt);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s %-10s %-5s %-16s %-34s \033[0m",
            top + 8, left + 2, sgr_list_hdr(), "Callsign", "Type", "Lift At", "Reason");
    write_str(buf);
    const int list_h = 7;
    if (bcnt == 0) {
        sprintf(buf, "\033[%d;%dH(none)", top + 9, left + 4);
        write_str(buf);
    } else {
        int off = (wiz_edit_idx_ >= list_h) ? (wiz_edit_idx_ - list_h + 1) : 0;
        for (int i = 0; i < list_h && (off + i) < bcnt; ++i) {
            const OykBanEntry& e = node_->ban_entry(off + i);
            int hi = (wiz_focus_ == 4 && !wiz_btn_ && (off + i) == wiz_edit_idx_);
            sprintf(buf, "\033[%d;%dH%s %-10.10s %-5s %-16.16s %-34.34s \033[0m",
                    top + 9 + i, left + 2, hi ? sgr_sel() : "",
                    e.key, (e.flags & OYK_BAN_HARD_BAN) ? "HARD" : "SOFT",
                    e.lift_at, e.reason);
            write_str(buf);
        }
    }
    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-68.68s \033[0m", top + h - 3, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }
    draw_hdiv(top + h - 2, left, w);
    sprintf(buf, "\033[%d;%dH%-68.68s", top + h - 1, left + 2,
            (wiz_focus_ == 4) ? "Up/Down: pick | Enter: UNBAN (confirm) | Tab: next | Esc: close"
                              : "Enter: BAN | Tab: next field | Esc: close");
    write_str(buf);

    if (wiz_btn_ && bcnt > 0 && wiz_edit_idx_ >= 0 && wiz_edit_idx_ < bcnt) {
        draw_box(9, 17, 6, 46, "Confirm Unban", 1);
        draw_shadow(9, 17, 6, 46);
        sprintf(buf, "\033[11;19HUnban callsign %-24.24s", node_->ban_entry(wiz_edit_idx_).key);
        write_str(buf);
        write_str("\033[12;19H[Y] Yes   [N] No");
    }
}

void PicoConsole::open_theme_dialog() {
    if (!node_) return;
    draft_ = node_->config();
    prompt_kind_ = PICO_PROMPT_THEME;
    wiz_edit_idx_ = 0;
    wiz_focus_ = 0;
    wiz_btn_ = 0;
    wiz_id_[0] = '\0';
    wiz_err_[0] = '\0';
    pico_str_copy(wiz_desc_, sizeof(wiz_desc_),
                  draft_.theme_name[0] ? draft_.theme_name : "dark");
    render();
}

void PicoConsole::render_theme_modal() {
    char buf[192];
    const int top = 2, left = 3, h = 21, w = 74;
    draw_box(top, left, h, w, "Theme Settings", 1);
    draw_shadow(top, left, h, w);

    const char* active = wiz_desc_[0] ? wiz_desc_ : "dark";
    OykThemeConfig pal;
    pico_str_copy(draft_.theme_name, sizeof(draft_.theme_name), active);
    oyk_theme_resolve_effective(&draft_, 0, 1, &pal);

    sprintf(buf, "\033[%d;%dHTheme  : < %-18.18s > (%s)",
            top + 1, left + 2, active,
            oyk_theme_is_builtin(active) ? "Built-in (not saved)" : "Custom");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHEditing built-in dark/light clones custom-dark or custom-light.",
            top + 2, left + 2);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s %-16s %-13s %-8s Preview \033[0m",
            top + 4, left + 2, sgr_list_hdr(), "Role", "Key", "Color");
    write_str(buf);

    const int vis_rows = 10;
    if (wiz_edit_idx_ < wiz_focus_) wiz_focus_ = wiz_edit_idx_;
    if (wiz_edit_idx_ >= wiz_focus_ + vis_rows) wiz_focus_ = wiz_edit_idx_ - vis_rows + 1;

    for (int r = 0; r < vis_rows && (wiz_focus_ + r) < OYK_THEME_COLOR_COUNT; ++r) {
        int idx = wiz_focus_ + r;
        char hx[12];
        oyk_theme_format_hex(pal.colors[idx], hx, sizeof(hx));
        sprintf(buf, "\033[%d;%dH%s %-16.16s %-13.13s %-8.8s\033[0m ",
                top + 5 + r, left + 2,
                (idx == wiz_edit_idx_) ? sgr_sel() : "",
                oyk_theme_role_label(idx), oyk_theme_role_key(idx), hx);
        write_str(buf);
        if (term_type_ >= PICO_TERM_VT220) {
            write_str(pico_format_sgr(6, pal.colors[idx], pal.colors[idx], 1, 0, term_type_));
            write_str("    \033[0m");
        } else {
            write_str("[##]");
        }
    }

    draw_hdiv(top + 16, left, w);
    if (wiz_btn_) {
        sprintf(buf, "\033[%d;%dH%s (#RRGGBB): %s %-12.12s \033[0m",
                top + 17, left + 2, oyk_theme_role_key(wiz_edit_idx_), sgr_sel(), wiz_id_);
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%s Enter:Apply Hex | Esc:Back \033[0m",
                top + 18, left + 2, sgr_menubar());
        write_str(buf);
    } else {
        sprintf(buf, "\033[%d;%dH%-68.68s", top + 17, left + 2,
                wiz_err_[0] ? wiz_err_ : "Live preview active (unsaved). Press S/F2 to save or Esc to revert.");
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%s Lt/Rt:Theme  Enter:Edit  X:Del  S/F2:Save & Apply  Esc:Cancel \033[0m",
                top + 18, left + 2, sgr_menubar());
        write_str(buf);
    }
}

void PicoConsole::render_confirm_format_modal() {
    char buf[120];
    draw_box(7, 10, 9, 60, "Confirm Storage Format", 1);
    draw_shadow(7, 10, 9, 60);
    write_str("\033[9;12HType FORMAT to erase active storage and press Enter:");
    sprintf(buf, "\033[11;12H> %s%-16s\033[0m", sgr_sel(), prompt_buf_);
    write_str(buf);
    draw_hdiv(13, 10, 60);
    sprintf(buf, "\033[14;11H%s %-56.56s \033[0m", sgr_menubar(), "Enter:Confirm   Esc:Cancel");
    write_str(buf);
}

void PicoConsole::render_text_prompt() {
    char buf[128];
    draw_box(8, 12, 7, 56, prompt_label_[0] ? prompt_label_ : "Edit Setting", 1);
    draw_shadow(8, 12, 7, 56);
    sprintf(buf, "\033[10;14H%s> %-48.48s\033[0m", sgr_sel(), prompt_buf_);
    write_str(buf);
    draw_hdiv(12, 12, 56);
    sprintf(buf, "\033[13;13H%s %-52.52s \033[0m", sgr_menubar(), "Enter:OK   Esc:Cancel");
    write_str(buf);
}

void PicoConsole::open_settings(int tab) {
    if (node_) draft_ = node_->config();
    view_ = PICO_VIEW_SETTINGS;
    settings_tab_ = (tab >= 0 && tab < 7) ? tab : 0;
    memset(settings_sel_, 0, sizeof(settings_sel_));
    dialog_fb_[0] = '\0';
    render();
}

void PicoConsole::commit_settings() {
    if (!node_) return;
    OykConfigApplyResult res;
    node_->apply_config(&draft_, 1, &res);
    if (bridge_) bridge_->reconfig();
    strcpy(dialog_fb_, "Settings saved & applied.");
    strcpy(status_fb_, "Settings saved & applied.");
}

void PicoConsole::edit_current_settings_field() {
    int idx = settings_sel_[settings_tab_];
    if (settings_tab_ == 3) {
        if (idx >= 0 && idx < draft_.iface_count) open_edit_iface_dialog(idx);
        return;
    }
    if (settings_tab_ == 4) {
        if (idx >= 0 && idx < draft_.peer_count) open_edit_peer_dialog(idx);
        return;
    }
    char pre[96];
    pre[0] = '\0';
    const char* lbl = "Edit Value";
    if (settings_tab_ == 0) {
        if (idx == 4) { draft_.accept_incoming = !draft_.accept_incoming; render(); return; }
        if (idx == 5) { draft_.allow_flash_logging = !draft_.allow_flash_logging; render(); return; }
        if (idx == 6) { draft_.learned_peer_policy = pico_cycle_lpol(draft_.learned_peer_policy, 1); render(); return; }
        if (idx == 0) { lbl = "Node Callsign"; oyk_format_callsign(&draft_.callsign, pre, sizeof(pre)); }
        else if (idx == 1) { lbl = "Node Alias"; oyk_format_callsign(&draft_.alias, pre, sizeof(pre)); }
        else if (idx == 2) { lbl = "Welcome Message"; pico_str_copy(pre, sizeof(pre), draft_.welcome_text); }
        else if (idx == 3) { lbl = "Info Message"; pico_str_copy(pre, sizeof(pre), draft_.info_text); }
        else if (idx == 7) { lbl = "New User Welcome"; pico_str_copy(pre, sizeof(pre), draft_.new_user_welcome); }
        else if (idx == 8) { lbl = "Interfaces File"; pico_str_copy(pre, sizeof(pre), draft_.interfaces_file); }
        else if (idx == 9) { lbl = "Peers File"; pico_str_copy(pre, sizeof(pre), draft_.peers_file); }
        else { lbl = "Themes File"; pico_str_copy(pre, sizeof(pre), draft_.themes_file); }
    } else if (settings_tab_ == 1) {
        if (idx == 0) { draft_.bbs_enabled = !draft_.bbs_enabled; render(); return; }
        if (idx == 4) { draft_.bbs_files_enabled = !draft_.bbs_files_enabled; render(); return; }
        if (idx == 1) { lbl = "BBS SSID (0-15)"; sprintf(pre, "%u", static_cast<unsigned int>(draft_.bbs_ssid)); }
        else if (idx == 2) { lbl = "BBS Callsign"; oyk_format_callsign(&draft_.bbs_callsign, pre, sizeof(pre)); }
        else if (idx == 3) { lbl = "BBS Alias"; oyk_format_callsign(&draft_.bbs_alias, pre, sizeof(pre)); }
        else if (idx == 5) { lbl = "BBS Files Root"; pico_str_copy(pre, sizeof(pre), draft_.bbs_files_path); }
        else if (idx == 6) { lbl = "BBS Mail Path"; pico_str_copy(pre, sizeof(pre), draft_.bbs_mail_path); }
        else { lbl = "BBS Welcome"; pico_str_copy(pre, sizeof(pre), draft_.bbs_welcome); }
    } else if (settings_tab_ == 2) {
        if (idx == 0) { draft_.chat_enabled = !draft_.chat_enabled; render(); return; }
        if (idx == 1) { lbl = "CHAT SSID (0-15)"; sprintf(pre, "%u", static_cast<unsigned int>(draft_.chat_ssid)); }
        else if (idx == 2) { lbl = "CHAT Callsign"; oyk_format_callsign(&draft_.chat_callsign, pre, sizeof(pre)); }
        else if (idx == 3) { lbl = "CHAT Alias"; oyk_format_callsign(&draft_.chat_alias, pre, sizeof(pre)); }
        else if (idx == 4) { lbl = "Default Topic"; pico_str_copy(pre, sizeof(pre), draft_.chat_topic); }
        else { lbl = "CHAT Welcome"; pico_str_copy(pre, sizeof(pre), draft_.chat_welcome); }
    } else if (settings_tab_ == 5) {
        if (idx == 0) { draft_.rig_enabled = !draft_.rig_enabled; render(); return; }
        if (idx == 1) { strcpy(draft_.rig_protocol, pico_strieq_ci(draft_.rig_protocol, "rigctl") ? "radiofoundry" : "rigctl"); render(); return; }
        if (idx == 5) { draft_.rig_notify = !draft_.rig_notify; render(); return; }
        if (idx == 2) { lbl = "Radio Host / IP"; pico_str_copy(pre, sizeof(pre), draft_.rig_host); }
        else if (idx == 3) { lbl = "Rig Port"; sprintf(pre, "%u", static_cast<unsigned int>(draft_.rig_port)); }
        else { lbl = "Poll Interval (sec)"; sprintf(pre, "%u", static_cast<unsigned int>(draft_.rig_poll_sec)); }
    } else {
        if (idx == 0) { draft_.printer_enabled = !draft_.printer_enabled; render(); return; }
        if (idx == 2) {
            if (pico_strieq_ci(draft_.printer_protocol, "generic")) strcpy(draft_.printer_protocol, "mx80");
            else if (pico_strieq_ci(draft_.printer_protocol, "mx80")) strcpy(draft_.printer_protocol, "escp2");
            else strcpy(draft_.printer_protocol, "generic");
            render(); return;
        }
        if (idx == 5) { draft_.printer_condensed = !draft_.printer_condensed; render(); return; }
        if (idx == 1) { lbl = "Printer Device"; pico_str_copy(pre, sizeof(pre), draft_.printer_device); }
        else if (idx == 3) { lbl = "Printer Baud"; sprintf(pre, "%lu", static_cast<unsigned long>(draft_.printer_baud ? draft_.printer_baud : 9600UL)); }
        else { lbl = "Printer Log Mask"; sprintf(pre, "0x%lX", static_cast<unsigned long>(draft_.printer_level)); }
    }
    pico_str_copy(prompt_label_, sizeof(prompt_label_), lbl);
    pico_str_copy(prompt_buf_, sizeof(prompt_buf_), pre);
    prompt_len_ = strlen(prompt_buf_);
    prompt_kind_ = PICO_PROMPT_SET_TEXT;
    render();
}

void PicoConsole::commit_prompt() {
    char val[96];
    pico_str_copy(val, sizeof(val), prompt_buf_);
    prompt_kind_ = PICO_PROMPT_NONE;
    int idx = settings_sel_[settings_tab_];
    if (settings_tab_ == 0) {
        if (idx == 0) oyk_parse_callsign(val, &draft_.callsign);
        else if (idx == 1) { if (!val[0]) memset(&draft_.alias, 0, sizeof(draft_.alias)); else oyk_parse_callsign(val, &draft_.alias); }
        else if (idx == 2) pico_str_copy(draft_.welcome_text, sizeof(draft_.welcome_text), val);
        else if (idx == 3) pico_str_copy(draft_.info_text, sizeof(draft_.info_text), val);
        else if (idx == 7) pico_str_copy(draft_.new_user_welcome, sizeof(draft_.new_user_welcome), val);
        else if (idx == 8) pico_str_copy(draft_.interfaces_file, sizeof(draft_.interfaces_file), val);
        else if (idx == 9) pico_str_copy(draft_.peers_file, sizeof(draft_.peers_file), val);
        else if (idx == 10) pico_str_copy(draft_.themes_file, sizeof(draft_.themes_file), val);
    } else if (settings_tab_ == 1) {
        if (idx == 1) { long v = strtol(val, NULL, 10); if (v >= 0 && v <= 15) draft_.bbs_ssid = static_cast<oyk_u8>(v); }
        else if (idx == 2) { if (!val[0]) memset(&draft_.bbs_callsign, 0, sizeof(draft_.bbs_callsign)); else oyk_parse_callsign(val, &draft_.bbs_callsign); }
        else if (idx == 3) { if (!val[0]) memset(&draft_.bbs_alias, 0, sizeof(draft_.bbs_alias)); else oyk_parse_callsign(val, &draft_.bbs_alias); }
        else if (idx == 5) pico_str_copy(draft_.bbs_files_path, sizeof(draft_.bbs_files_path), val);
        else if (idx == 6) pico_str_copy(draft_.bbs_mail_path, sizeof(draft_.bbs_mail_path), val);
        else if (idx == 7) pico_str_copy(draft_.bbs_welcome, sizeof(draft_.bbs_welcome), val);
    } else if (settings_tab_ == 2) {
        if (idx == 1) { long v = strtol(val, NULL, 10); if (v >= 0 && v <= 15) draft_.chat_ssid = static_cast<oyk_u8>(v); }
        else if (idx == 2) { if (!val[0]) memset(&draft_.chat_callsign, 0, sizeof(draft_.chat_callsign)); else oyk_parse_callsign(val, &draft_.chat_callsign); }
        else if (idx == 3) { if (!val[0]) memset(&draft_.chat_alias, 0, sizeof(draft_.chat_alias)); else oyk_parse_callsign(val, &draft_.chat_alias); }
        else if (idx == 4) pico_str_copy(draft_.chat_topic, sizeof(draft_.chat_topic), val);
        else if (idx == 5) pico_str_copy(draft_.chat_welcome, sizeof(draft_.chat_welcome), val);
    } else if (settings_tab_ == 5) {
        if (idx == 2) pico_str_copy(draft_.rig_host, sizeof(draft_.rig_host), val);
        else if (idx == 3) { long p = strtol(val, NULL, 10); if (p > 0 && p <= 65535L) draft_.rig_port = static_cast<oyk_u16>(p); }
        else if (idx == 4) { long p = strtol(val, NULL, 10); if (p >= 0 && p <= 3600L) draft_.rig_poll_sec = static_cast<oyk_u16>(p); }
    } else if (settings_tab_ == 6) {
        if (idx == 1) pico_str_copy(draft_.printer_device, sizeof(draft_.printer_device), val);
        else if (idx == 3) { long b = strtol(val, NULL, 10); if (b >= 300L) draft_.printer_baud = static_cast<oyk_u32>(b); }
        else if (idx == 4) oyk_parse_log_mask(val, &draft_.printer_level);
    }
    render();
}

void PicoConsole::on_local_term_event(oyk_u32 sid, int event_type,
                                      const oyk_u8* data, size_t len, const char* /*reason*/) {
    if (event_type == 3) lterm_sid_ = 0UL;
    else lterm_sid_ = sid;
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

void PicoConsole::handle_mouse_event(int row1, int col1, int kind) {
    if (!node_) return;
    if (prompt_kind_ != PICO_PROMPT_NONE) {
        if (kind == 4) handle_key_code(PK_UP);
        else if (kind == 5) handle_key_code(PK_DOWN);
        else if (kind == 2) handle_key_code(PK_ESC);
        else if (kind == 0 && prompt_kind_ == PICO_PROMPT_THEME) {
            if (row1 == 4 && col1 >= 14 && col1 <= 38) {
                handle_key_code((col1 < 25) ? PK_LEFT : PK_RIGHT);
            } else if (row1 == 21 && col1 >= 34 && col1 <= 53) {
                handle_key_code('s');
            } else if (row1 == 21 && col1 >= 54) {
                handle_key_code(PK_ESC);
            }
        }
        return;
    }

    if (kind == 4 || kind == 5) {
        if (view_ == PICO_VIEW_MENU || view_ == PICO_VIEW_SETTINGS ||
            view_ == PICO_VIEW_FILES || view_ == PICO_VIEW_STORAGE) {
            handle_key_code((kind == 4) ? PK_UP : PK_DOWN);
        }
        return;
    }

    if (kind == 2) {
        if (view_ == PICO_VIEW_CONSOLE) handle_key_code(PK_F10);
        else handle_key_code(PK_ESC);
        return;
    }

    if (kind != 0) return;

    if (view_ == PICO_VIEW_CONSOLE) {
        if (row1 == 1) {
            if (col1 <= 9 || col1 >= 69) handle_key_code(PK_F10);
            else if (col1 >= 49 && col1 <= 68) handle_key_code(PK_TAB);
        } else if (row1 == 2 && col1 >= 55) {
            handle_key_code(node_->accept_incoming() ? 'd' : 'a');
        }
    } else if (view_ == PICO_VIEW_MENU) {
        static const int kItemRows[14] = { 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18 };
        if (row1 < 2 || row1 > 19 || col1 < 2 || col1 > 37) {
            handle_key_code(PK_ESC);
            return;
        }
        for (int i = 0; i < 14; ++i) {
            if (row1 == kItemRows[i]) {
                menu_sel_ = i;
                handle_key_code(PK_ENTER);
                break;
            }
        }
    } else if (view_ == PICO_VIEW_SETTINGS) {
        static const int kTabLens[7] = { 8, 7, 8, 10, 9, 7, 7 };
        if (row1 == 3) {
            int tx = 5;
            for (int t = 0; t < 7; ++t) {
                if (col1 >= tx && col1 < tx + kTabLens[t]) {
                    settings_tab_ = t;
                    render();
                    break;
                }
                tx += kTabLens[t] + 1;
            }
        } else if (row1 == 21) {
            if (col1 >= 52) handle_key_code(PK_ESC);
            else if (col1 >= 38) handle_key_code('s');
        } else {
            int row_base = (settings_tab_ == 3 || settings_tab_ == 4) ? 6 : 5;
            int cnt = settings_tab_item_count(settings_tab_);
            if (row1 >= row_base && row1 < row_base + cnt && row1 < 18) {
                int idx = row1 - row_base;
                if (idx == settings_sel_[settings_tab_]) {
                    handle_key_code(PK_ENTER);
                } else {
                    settings_sel_[settings_tab_] = idx;
                    render();
                }
            }
        }
    } else if (view_ == PICO_VIEW_BOOTLOADER && !bootloader_pending_) {
        if (row1 == 11 && col1 >= 8 && col1 <= 34) handle_key_code('s');
        else if (row1 == 11 && col1 >= 36) handle_key_code('d');
        else if (row1 == 12 && col1 >= 8 && col1 <= 34) handle_key_code('r');
        else if (row1 == 12 && col1 >= 36) handle_key_code(PK_ESC);
    }
}

void PicoConsole::handle_char(oyk_u8 ch, oyk_u32 now_ms) {
    if (esc_state_ == 4) {
        x10_buf_[x10_len_++] = ch;
        if (x10_len_ >= 3U) {
            int cb = static_cast<int>(x10_buf_[0]) - 32;
            int col1 = static_cast<int>(x10_buf_[1]) - 32;
            int row1 = static_cast<int>(x10_buf_[2]) - 32;
            esc_state_ = 0;
            x10_len_ = 0;
            int kind = -1;
            if (cb == 64 || (cb & 0x43) == 64) kind = 4;
            else if (cb == 65 || (cb & 0x43) == 65) kind = 5;
            else if ((cb & 3) == 0) kind = 0;
            else if ((cb & 3) == 2) kind = 2;
            if (kind >= 0) handle_mouse_event(row1, col1, kind);
        }
        return;
    }
    if (esc_state_ == 0) {
        if (ch == 0x1BU) {
            esc_state_ = 1; esc_len_ = 0; esc_ms_ = now_ms;
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
    if (esc_state_ == 2 && esc_len_ == 0 && ch == 'M') {
        esc_state_ = 4;
        x10_len_ = 0;
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
        if ((ch == 'M' || ch == 'm') && esc_buf_[0] == '<') {
            if (ch == 'm') return;
            const char* p = esc_buf_ + 1;
            int btn = atoi(p);
            const char* s1 = strchr(p, ';');
            if (!s1) return;
            int col1 = atoi(s1 + 1);
            const char* s2 = strchr(s1 + 1, ';');
            if (!s2) return;
            int row1 = atoi(s2 + 1);
            int kind = -1;
            if (btn == 64 || (btn & 0x43) == 64) kind = 4;
            else if (btn == 65 || (btn & 0x43) == 65) kind = 5;
            else if ((btn & 64) == 0) {
                if ((btn & 3) == 0) kind = 0;
                else if ((btn & 3) == 2) kind = 2;
            }
            if (kind >= 0) handle_mouse_event(row1, col1, kind);
            return;
        }
        if (ch == 'c' && (esc_buf_[0] == '?' || esc_buf_[0] == '>')) {
            if (esc_buf_[0] == '?') {
                if (strstr(esc_buf_, ";22") != NULL) term_type_ = PICO_TERM_VT340;
                else if (term_type_ < PICO_TERM_VT220) term_type_ = PICO_TERM_VT220;
            } else {
                term_type_ = PICO_TERM_XTERM256;
            }
            if (term_type_ >= PICO_TERM_VT340 && !mouse_enabled_) {
                mouse_enabled_ = 1;
                write_str("\033[?1000h\033[?1006h");
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
        else if (ch == '~' && strcmp(esc_buf_, "12~") == 0) handle_key_code(PK_F2);
        else if (ch == '~' && strcmp(esc_buf_, "21~") == 0) handle_key_code(PK_F10);
    }
}

void PicoConsole::handle_key_code(int key) {
    if (prompt_kind_ != PICO_PROMPT_NONE) { handle_modal_key(key); return; }
    switch (view_) {
        case PICO_VIEW_MENU:       handle_menu_key(key); break;
        case PICO_VIEW_SETTINGS:   handle_settings_key(key); break;
        case PICO_VIEW_FILES:      handle_files_key(key); break;
        case PICO_VIEW_RIG:        handle_rig_key(key); break;
        case PICO_VIEW_STORAGE:    handle_storage_key(key); break;
        case PICO_VIEW_BOOTLOADER: handle_bootloader_key(key); break;
        case PICO_VIEW_PSRAM:
            if (key == PK_ESC) { view_ = PICO_VIEW_CONSOLE; render(); }
            break;
        case PICO_VIEW_LOCAL_TERM: handle_local_term_key(key); break;
        default:                   handle_console_key(key); break;
    }
}

void PicoConsole::handle_console_key(int key) {
    if (key == 'm' || key == 'M' || key == PK_F10) {
        view_ = PICO_VIEW_MENU; menu_sel_ = 0; render();
    } else if (key == 'a' || key == 'A') {
        if (node_) node_->set_accept_incoming(1);
        strcpy(status_fb_, "New incoming connections are now ALLOWED.");
        render();
    } else if (key == 'd' || key == 'D') {
        if (node_) node_->set_accept_incoming(0);
        strcpy(status_fb_, "New incoming connections are now DENIED.");
        render();
    } else if (key == 'l' || key == 'L' || key == PK_TAB) {
        view_ = PICO_VIEW_LOCAL_TERM; render();
    } else if (key == 'f' || key == 'F') {
        files_tab_ = 0; dialog_fb_[0] = '\0'; refresh_files_lists();
        view_ = PICO_VIEW_FILES; render();
    } else if (key == 'r' || key == 'R') {
        dialog_fb_[0] = '\0'; view_ = PICO_VIEW_RIG; render();
    } else if (key == 's' || key == 'S') {
        open_settings(0);
    } else if (key == 'e' || key == 'E') {
        open_settings(4);
    } else if (key == 'h' || key == 'H') {
        open_theme_dialog();
    } else if (key == 't' || key == 'T') {
        open_terminate_dialog();
    } else if (key == 'b' || key == 'B') {
        open_ban_dialog();
    } else if (key == 'p' || key == 'P') {
        view_ = PICO_VIEW_PSRAM; render();
    } else if (key == 'g' || key == 'G') {
        status_fb_[0] = '\0'; view_ = PICO_VIEW_STORAGE; render();
    } else if (key == 'u' || key == 'U') {
        bootloader_pending_ = 0; view_ = PICO_VIEW_BOOTLOADER; render();
    }
}

void PicoConsole::handle_menu_key(int key) {
    if (key == PK_ESC || key == PK_F10 || key == 'x' || key == 'X') {
        view_ = PICO_VIEW_CONSOLE; render(); return;
    }
    if (key == PK_UP) { menu_sel_ = (menu_sel_ + 13) % 14; render(); return; }
    if (key == PK_DOWN) { menu_sel_ = (menu_sel_ + 1) % 14; render(); return; }
    if (key == PK_HOME) { menu_sel_ = 0; render(); return; }
    if (key == PK_END) { menu_sel_ = 13; render(); return; }
    static const int kKeys[14] = { 'a', 'd', 'l', 'f', 'r', 's', 'e', 'h', 't', 'b', 'p', 'g', 'u', 'x' };
    int chosen = 0;
    if (key == PK_ENTER) chosen = kKeys[menu_sel_];
    else {
        for (int i = 0; i < 13; ++i) {
            if (key == kKeys[i] || key == (kKeys[i] - 32)) { chosen = kKeys[i]; break; }
        }
    }
    if (!chosen) return;
    view_ = PICO_VIEW_CONSOLE;
    if (chosen == 'x') render();
    else handle_console_key(chosen);
}

void PicoConsole::handle_settings_key(int key) {
    if (key == PK_ESC) {
        view_ = PICO_VIEW_CONSOLE;
        strcpy(status_fb_, "Settings cancelled (no changes applied).");
        render();
        return;
    }
    if (key >= '1' && key <= '7') {
        settings_tab_ = key - '1'; render(); return;
    }
    if (key == PK_TAB) { settings_tab_ = (settings_tab_ + 1) % 7; render(); return; }
    int cnt = settings_tab_item_count(settings_tab_);
    if (key == PK_UP && cnt > 0) {
        settings_sel_[settings_tab_] = (settings_sel_[settings_tab_] + cnt - 1) % cnt;
        render(); return;
    }
    if (key == PK_DOWN && cnt > 0) {
        settings_sel_[settings_tab_] = (settings_sel_[settings_tab_] + 1) % cnt;
        render(); return;
    }
    if (key == 'h' || key == 'H') { open_theme_dialog(); return; }
    if (key == 's' || key == 'S' || key == PK_F2) {
        commit_settings();
        view_ = PICO_VIEW_CONSOLE;
        render();
        return;
    }
    if (settings_tab_ == 3) {
        if (key == 'n' || key == 'N' || key == 'a' || key == 'A') { open_add_iface_wizard(); return; }
        if ((key == 'e' || key == 'E' || key == PK_ENTER) && draft_.iface_count > 0) {
            open_edit_iface_dialog(settings_sel_[3]); return;
        }
        if ((key == 'd' || key == 'D') && draft_.iface_count > 0) {
            int s = settings_sel_[3];
            for (int i = s; i + 1 < draft_.iface_count; ++i) draft_.ifaces[i] = draft_.ifaces[i + 1];
            --draft_.iface_count;
            if (settings_sel_[3] >= draft_.iface_count && settings_sel_[3] > 0) --settings_sel_[3];
            strcpy(dialog_fb_, "Removed interface from draft.");
            render(); return;
        }
    } else if (settings_tab_ == 4) {
        if (key == 'a' || key == 'A') { open_edit_peer_dialog(-1); return; }
        if ((key == 'e' || key == 'E' || key == PK_ENTER) && draft_.peer_count > 0) {
            open_edit_peer_dialog(settings_sel_[4]); return;
        }
        if ((key == 'i' || key == 'I') && draft_.peer_count > 0) {
            draft_.peers[settings_sel_[4]].allow_inbound = !draft_.peers[settings_sel_[4]].allow_inbound;
            render(); return;
        }
        if ((key == 'o' || key == 'O') && draft_.peer_count > 0) {
            draft_.peers[settings_sel_[4]].allow_outbound = !draft_.peers[settings_sel_[4]].allow_outbound;
            render(); return;
        }
        if ((key == 'k' || key == 'K') && draft_.peer_count > 0) {
            draft_.peers[settings_sel_[4]].auto_learned = 0;
            render(); return;
        }
        if ((key == 'd' || key == 'D') && draft_.peer_count > 0) {
            int s = settings_sel_[4];
            for (int i = s; i + 1 < draft_.peer_count; ++i) draft_.peers[i] = draft_.peers[i + 1];
            --draft_.peer_count;
            if (settings_sel_[4] >= draft_.peer_count && settings_sel_[4] > 0) --settings_sel_[4];
            strcpy(dialog_fb_, "Removed peer from draft.");
            render(); return;
        }
    } else if (key == 'e' || key == 'E' || key == PK_ENTER) {
        edit_current_settings_field();
    }
}

void PicoConsole::handle_files_key(int key) {
    if (key == PK_ESC) { view_ = PICO_VIEW_CONSOLE; render(); return; }
    if (key == PK_TAB) { files_tab_ = (files_tab_ + 1) % 2; render(); return; }
    if (key == '1') { files_tab_ = 0; render(); return; }
    if (key == '2') { files_tab_ = 1; render(); return; }
    if (key == 'r' || key == 'R') { refresh_files_lists(); strcpy(dialog_fb_, "Refreshed files lists."); render(); return; }
    int cnt = (files_tab_ == 0) ? pending_count_ : approved_count_;
    if (key == PK_UP && cnt > 0) { files_sel_[files_tab_] = (files_sel_[files_tab_] + cnt - 1) % cnt; render(); return; }
    if (key == PK_DOWN && cnt > 0) { files_sel_[files_tab_] = (files_sel_[files_tab_] + 1) % cnt; render(); return; }
    if (files_tab_ == 0 && (key == 'a' || key == 'A' || key == PK_ENTER) && pending_count_ > 0) {
        const OykPendingIncoming& pi = pending_files_[files_sel_[0]];
        char dest[128], err[96];
        int exists = 0;
        if (oyk_bbs_approve_incoming_file("incoming", node_->config().bbs_files_path,
                                          pi.uploader, pi.filename, NULL, 1, dest, sizeof(dest),
                                          &exists, err, sizeof(err))) {
            sprintf(dialog_fb_, "Approved %.40s", pi.filename);
            refresh_files_lists();
        } else {
            pico_str_copy(dialog_fb_, sizeof(dialog_fb_), err);
        }
        render(); return;
    }
    if ((key == 'd' || key == 'D') && cnt > 0) {
        char err[96];
        if (files_tab_ == 0) {
            const OykPendingIncoming& pi = pending_files_[files_sel_[0]];
            oyk_bbs_delete_incoming_file("incoming", pi.uploader, pi.filename, err, sizeof(err));
        } else {
            oyk_bbs_delete_approved_file(node_->config().bbs_files_path, approved_files_[files_sel_[1]].rel_path, err, sizeof(err));
        }
        refresh_files_lists();
        strcpy(dialog_fb_, "Deleted selected file.");
        render();
    }
}

void PicoConsole::handle_rig_key(int key) {
    if (key == PK_ESC) { view_ = PICO_VIEW_CONSOLE; render(); }
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
    if (key == PK_ESC || key == 'n' || key == 'N') { view_ = PICO_VIEW_CONSOLE; render(); return; }
    if (auto_cnt > 0) {
        if (key == 'e' || key == 'E' || key == 'r' || key == 'R') { open_settings(4); return; }
        if (key == 's' || key == 'S') {
            node_->persist_auto_learned_peers(NULL, 0);
            disable_mouse();
            bootloader_pending_ = 1; bootloader_ms_ = last_ms_; render();
        } else if (key == 'd' || key == 'D') {
            node_->discard_auto_learned_peers();
            disable_mouse();
            bootloader_pending_ = 1; bootloader_ms_ = last_ms_; render();
        }
    } else if (key == 'y' || key == 'Y' || key == PK_ENTER) {
        disable_mouse();
        bootloader_pending_ = 1; bootloader_ms_ = last_ms_; render();
    }
}

void PicoConsole::handle_local_term_key(int key) {
    if (key == PK_ESC || key == PK_TAB) {
        view_ = PICO_VIEW_CONSOLE;
        render();
        return;
    }
    if (key == PK_ENTER) {
        lterm_input_[lterm_input_len_] = '\0';
        char line[80];
        pico_str_copy(line, sizeof(line), lterm_input_);
        lterm_input_len_ = 0;
        lterm_input_[0] = '\0';
        if (strcmp(line, "/console") == 0 || strcmp(line, "/bg") == 0) {
            view_ = PICO_VIEW_CONSOLE; render(); return;
        }
        if (strcmp(line, "/local") == 0 || strcmp(line, "/bbs") == 0 || strcmp(line, "/chat") == 0) {
            const char* target = (line[1] == 'b') ? "BBS" : ((line[1] == 'c') ? "CHAT" : "LOCAL");
            if (lterm_sid_ != 0) node_->close_local_terminal(lterm_sid_);
            char err[64];
            if (!node_->open_local_terminal(-1, target, &lterm_sid_, NULL, &lterm_is_local_, err, sizeof(err))) {
                if (lterm_line_count_ < 12) pico_str_copy(lterm_lines_[lterm_line_count_++], 80, err);
            }
            render();
            return;
        }
        if (strcmp(line, "/disc") == 0) {
            if (lterm_sid_ != 0) { node_->close_local_terminal(lterm_sid_); lterm_sid_ = 0; }
            render(); return;
        }
        if (lterm_line_count_ < 12) sprintf(lterm_lines_[lterm_line_count_++], "> %.76s", line);
        if (lterm_sid_ != 0 && node_) {
            char tx[84];
            sprintf(tx, "%s\r", line);
            node_->send_local_bytes(lterm_sid_, reinterpret_cast<const oyk_u8*>(tx), strlen(tx));
        }
        render();
        return;
    }
    if (key == PK_BS && lterm_input_len_ > 0) {
        lterm_input_[--lterm_input_len_] = '\0'; render();
    } else if (key >= 32 && key < 127 && lterm_input_len_ + 1 < sizeof(lterm_input_)) {
        lterm_input_[lterm_input_len_++] = static_cast<char>(key);
        lterm_input_[lterm_input_len_] = '\0';
        render();
    }
}

int PicoConsole::wizard_try_advance() {
    wiz_err_[0] = '\0';
    if (wiz_step_ == 0) {
        if (wiz_drv_ != wiz_last_drv_) wizard_apply_driver_defaults();
        wiz_step_ = 1; wiz_focus_ = 0; wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 1) {
        if (!wiz_id_[0]) { strcpy(wiz_err_, "Interface ID cannot be empty."); wiz_focus_ = 0; return 0; }
        for (int i = 0; i < draft_.iface_count; ++i) {
            if (pico_strieq_ci(draft_.ifaces[i].id, wiz_id_)) {
                strcpy(wiz_err_, "Duplicate interface ID."); wiz_focus_ = 0; return 0;
            }
        }
        wiz_step_ = 2; wiz_focus_ = 0; wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 2) {
        if (!wiz_ep_[0]) { strcpy(wiz_err_, "Endpoint cannot be empty."); wiz_focus_ = 0; return 0; }
        wiz_step_ = 3; wiz_focus_ = 0; wiz_btn_ = 1;
        return 0;
    }
    commit_iface_fields(draft_.iface_count);
    prompt_kind_ = PICO_PROMPT_NONE;
    return 1;
}

int PicoConsole::edit_iface_try_save() {
    wiz_err_[0] = '\0';
    if (!wiz_id_[0]) { strcpy(wiz_err_, "Interface ID cannot be empty."); wiz_focus_ = 1; return 0; }
    commit_iface_fields(wiz_edit_idx_);
    prompt_kind_ = PICO_PROMPT_NONE;
    return 1;
}

int PicoConsole::edit_peer_try_save() {
    wiz_err_[0] = '\0';
    OykCallsign cs;
    if (!oyk_parse_callsign(ban_call_tmp_, &cs)) {
        strcpy(wiz_err_, "Invalid AX.25 callsign."); wiz_focus_ = 1; return 0;
    }
    char cfmt[16];
    oyk_format_callsign(&cs, cfmt, sizeof(cfmt));
    if (!wiz_id_[0]) oyk_derive_default_peer_id(&draft_, cfmt, wiz_id_, sizeof(wiz_id_));
    if (wiz_ep_[0] && oyk_is_all_digits(wiz_ep_)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(wiz_ep_, &did)) {
            strcpy(wiz_err_, "Invalid DMR ID (allowed 1..16777214)."); wiz_focus_ = 4; return 0;
        }
    }
    int slot = (wiz_edit_idx_ >= 0) ? wiz_edit_idx_ : draft_.peer_count;
    commit_peer_fields(slot);
    prompt_kind_ = PICO_PROMPT_NONE;
    return 1;
}

void PicoConsole::handle_wizard_key(int key) {
    if (key == PK_ESC) {
        prompt_kind_ = PICO_PROMPT_NONE;
        strcpy(dialog_fb_, "Add interface cancelled.");
        render(); return;
    }
    if (key == 'b' || key == 'B' || key == 2) {
        if (wiz_step_ > 0) { --wiz_step_; wiz_focus_ = 0; render(); }
        return;
    }
    if (wiz_step_ == 0) {
        if (key >= '1' && key <= '5') { wiz_drv_ = key - '1'; wizard_apply_driver_defaults(); render(); return; }
        if (key == PK_UP) { wiz_drv_ = (wiz_drv_ + 4) % 5; wizard_apply_driver_defaults(); render(); return; }
        if (key == PK_DOWN) { wiz_drv_ = (wiz_drv_ + 1) % 5; wizard_apply_driver_defaults(); render(); return; }
        if (key == PK_ENTER) { wizard_try_advance(); render(); return; }
        return;
    }
    if (wiz_step_ == 3) {
        if (key == PK_LEFT) wiz_btn_ = 0;
        else if (key == PK_RIGHT) wiz_btn_ = (wiz_btn_ == 2) ? 2 : (wiz_btn_ + 1);
        else if (key == PK_ENTER) {
            if (wiz_btn_ == 0) --wiz_step_;
            else if (wiz_btn_ == 2) prompt_kind_ = PICO_PROMPT_NONE;
            else wizard_try_advance();
        }
        render(); return;
    }
    int fcnt = wizard_step_field_count();
    if (key == PK_UP) { wiz_focus_ = (wiz_focus_ + fcnt) % (fcnt + 1); render(); return; }
    if (key == PK_DOWN || key == PK_TAB) { wiz_focus_ = (wiz_focus_ + 1) % (fcnt + 1); render(); return; }
    if (wiz_focus_ == fcnt) {
        if (key == PK_LEFT && wiz_btn_ > 0) --wiz_btn_;
        else if (key == PK_RIGHT && wiz_btn_ < 2) ++wiz_btn_;
        else if (key == PK_ENTER) {
            if (wiz_btn_ == 0) --wiz_step_;
            else if (wiz_btn_ == 2) prompt_kind_ = PICO_PROMPT_NONE;
            else wizard_try_advance();
        }
        render(); return;
    }
    if (wiz_step_ == 2) {
        int tbase = (wiz_drv_ == 0) ? 4 : ((wiz_drv_ == 1) ? 3 : 2);
        if (wiz_focus_ >= tbase) {
            int rel = wiz_focus_ - tbase;
            if (key == ' ' || key == PK_LEFT || key == PK_RIGHT) {
                if (rel == 0) wiz_flags_ ^= 1U;
                else if (rel == 1) wiz_flags_ ^= 2U;
                else if (rel == 2) wiz_flags_ ^= 0x20U;
                else {
                    oyk_u8 lp = pico_cycle_lpol(static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U),
                                                (key == PK_LEFT) ? -1 : 1);
                    wiz_flags_ = static_cast<oyk_u8>((wiz_flags_ & 0x23U) | ((lp & 0x07U) << 2));
                }
                render(); return;
            }
            if (key == PK_ENTER) { wizard_try_advance(); render(); return; }
        }
    }
    char* tgt = NULL;
    size_t cap = 0;
    if (wiz_step_ == 1) {
        tgt = (wiz_focus_ == 0) ? wiz_id_ : wiz_desc_;
        cap = (wiz_focus_ == 0) ? sizeof(wiz_id_) : sizeof(wiz_desc_);
    } else {
        if (wiz_focus_ == 0) { tgt = wiz_ep_; cap = sizeof(wiz_ep_); }
        else if (wiz_drv_ == 1 && wiz_focus_ == 1) { tgt = wiz_mask_; cap = sizeof(wiz_mask_); }
        else if (wiz_drv_ == 1 && wiz_focus_ == 2) { tgt = wiz_peer_; cap = sizeof(wiz_peer_); }
        else if (wiz_focus_ == 1) { tgt = wiz_port_; cap = sizeof(wiz_port_); }
        else if (wiz_drv_ == 0 && wiz_focus_ == 2) { tgt = wiz_mask_; cap = sizeof(wiz_mask_); }
        else if (wiz_drv_ == 0 && wiz_focus_ == 3) { tgt = wiz_peer_; cap = sizeof(wiz_peer_); }
    }
    if (key == PK_ENTER) { wizard_try_advance(); render(); return; }
    if (tgt && key == PK_BS) { size_t n = strlen(tgt); if (n > 0) tgt[n - 1] = '\0'; render(); }
    else if (tgt && key >= 32 && key < 127) {
        size_t n = strlen(tgt);
        if (n + 1 < cap) { tgt[n] = static_cast<char>(key); tgt[n + 1] = '\0'; }
        render();
    }
}

void PicoConsole::handle_edit_iface_key(int key) {
    if (key == PK_ESC) {
        prompt_kind_ = PICO_PROMPT_NONE;
        strcpy(dialog_fb_, "Edit interface cancelled.");
        render(); return;
    }
    int fcnt = edit_iface_field_count();
    if (key == PK_UP) { wiz_focus_ = (wiz_focus_ + fcnt) % (fcnt + 1); render(); return; }
    if (key == PK_DOWN || key == PK_TAB) { wiz_focus_ = (wiz_focus_ + 1) % (fcnt + 1); render(); return; }
    if (wiz_focus_ == fcnt) {
        if (key == PK_LEFT || key == PK_RIGHT) wiz_btn_ = 1 - wiz_btn_;
        else if (key == PK_ENTER) {
            if (wiz_btn_ == 1) prompt_kind_ = PICO_PROMPT_NONE;
            else edit_iface_try_save();
        }
        render(); return;
    }
    if (wiz_focus_ == 0) {
        if (key == PK_LEFT || key == PK_RIGHT) {
            wiz_drv_ = (key == PK_LEFT) ? (wiz_drv_ + 4) % 5 : (wiz_drv_ + 1) % 5;
            render();
        } else if (key == PK_ENTER) { edit_iface_try_save(); render(); }
        return;
    }
    int tbase = fcnt - 4;
    if (wiz_focus_ >= tbase) {
        int rel = wiz_focus_ - tbase;
        if (key == ' ' || key == PK_LEFT || key == PK_RIGHT) {
            if (rel == 0) wiz_flags_ ^= 1U;
            else if (rel == 1) wiz_flags_ ^= 2U;
            else if (rel == 2) wiz_flags_ ^= 0x20U;
            else {
                oyk_u8 lp = pico_cycle_lpol(static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U),
                                            (key == PK_LEFT) ? -1 : 1);
                wiz_flags_ = static_cast<oyk_u8>((wiz_flags_ & 0x23U) | ((lp & 0x07U) << 2));
            }
            render(); return;
        }
        if (key == PK_ENTER) { edit_iface_try_save(); render(); return; }
    }
    char* tgt = NULL;
    size_t cap = 0;
    if (wiz_focus_ == 1) { tgt = wiz_id_; cap = sizeof(wiz_id_); }
    else if (wiz_focus_ == 2) { tgt = wiz_desc_; cap = sizeof(wiz_desc_); }
    else if (wiz_focus_ == 3) { tgt = wiz_ep_; cap = sizeof(wiz_ep_); }
    else if (wiz_drv_ == 1 && wiz_focus_ == 4) { tgt = wiz_mask_; cap = sizeof(wiz_mask_); }
    else if (wiz_drv_ == 1 && wiz_focus_ == 5) { tgt = wiz_peer_; cap = sizeof(wiz_peer_); }
    else if (wiz_focus_ == 4) { tgt = wiz_port_; cap = sizeof(wiz_port_); }
    else if (wiz_drv_ == 0 && wiz_focus_ == 5) { tgt = wiz_mask_; cap = sizeof(wiz_mask_); }
    else if (wiz_drv_ == 0 && wiz_focus_ == 6) { tgt = wiz_peer_; cap = sizeof(wiz_peer_); }

    if (key == PK_ENTER) { edit_iface_try_save(); render(); }
    else if (tgt && key == PK_BS) { size_t n = strlen(tgt); if (n > 0) tgt[n - 1] = '\0'; render(); }
    else if (tgt && key >= 32 && key < 127) {
        size_t n = strlen(tgt);
        if (n + 1 < cap) { tgt[n] = static_cast<char>(key); tgt[n + 1] = '\0'; }
        render();
    }
}

void PicoConsole::handle_edit_peer_key(int key) {
    if (key == PK_ESC) {
        prompt_kind_ = PICO_PROMPT_NONE;
        strcpy(dialog_fb_, "Edit peer cancelled.");
        render(); return;
    }
    if (key == PK_UP) { wiz_focus_ = (wiz_focus_ + 8) % 9; render(); return; }
    if (key == PK_DOWN || key == PK_TAB) { wiz_focus_ = (wiz_focus_ + 1) % 9; render(); return; }
    if (wiz_focus_ == 8) {
        if (key == PK_LEFT || key == PK_RIGHT) wiz_btn_ = 1 - wiz_btn_;
        else if (key == PK_ENTER || key == ' ') {
            if (wiz_btn_ == 1) prompt_kind_ = PICO_PROMPT_NONE;
            else edit_peer_try_save();
        }
        render(); return;
    }
    if (wiz_focus_ == 3) {
        if (key == PK_LEFT || key == PK_RIGHT || key == ' ') {
            if (draft_.iface_count > 0) {
                int idx = 0;
                for (int i = 0; i < draft_.iface_count; ++i) {
                    if (pico_strieq_ci(draft_.ifaces[i].id, ban_iface_tmp_)) { idx = i; break; }
                }
                idx = (key == PK_LEFT) ? (idx + draft_.iface_count - 1) % draft_.iface_count
                                       : (idx + 1) % draft_.iface_count;
                pico_str_copy(ban_iface_tmp_, sizeof(ban_iface_tmp_), draft_.ifaces[idx].id);
            }
            render();
        } else if (key == PK_ENTER) { edit_peer_try_save(); render(); }
        return;
    }
    if (wiz_focus_ == 6 || wiz_focus_ == 7) {
        if (key == ' ' || key == PK_LEFT || key == PK_RIGHT) {
            wiz_step_ ^= (wiz_focus_ == 6) ? 1 : 2;
            render();
        } else if (key == PK_ENTER) { edit_peer_try_save(); render(); }
        return;
    }
    char* tgt = NULL;
    size_t cap = 0;
    if (wiz_focus_ == 0) { tgt = wiz_id_; cap = sizeof(wiz_id_); }
    else if (wiz_focus_ == 1) { tgt = ban_call_tmp_; cap = sizeof(ban_call_tmp_); }
    else if (wiz_focus_ == 2) { tgt = wiz_desc_; cap = sizeof(wiz_desc_); }
    else if (wiz_focus_ == 4) { tgt = wiz_ep_; cap = sizeof(wiz_ep_); }
    else if (wiz_focus_ == 5) { tgt = wiz_port_; cap = sizeof(wiz_port_); }

    if (key == PK_ENTER) { edit_peer_try_save(); render(); }
    else if (tgt && key == PK_BS) { size_t n = strlen(tgt); if (n > 0) tgt[n - 1] = '\0'; render(); }
    else if (tgt && key >= 32 && key < 127) {
        size_t n = strlen(tgt);
        if (n + 1 < cap) { tgt[n] = static_cast<char>(key); tgt[n + 1] = '\0'; }
        render();
    }
}

void PicoConsole::handle_terminate_key(int key) {
    oyk_u32 sids[OYK_MAX_SESSIONS];
    int scnt = 0;
    for (int i = 0; i < node_->max_sessions() && scnt < OYK_MAX_SESSIONS; ++i) {
        if (node_->session_slot(i).state == OYK_SESS_CONNECTED) {
            sids[scnt++] = node_->session_slot(i).id;
        }
    }
    if (scnt == 0 || key == PK_ESC) {
        prompt_kind_ = PICO_PROMPT_NONE; render(); return;
    }
    if (wiz_edit_idx_ >= scnt) wiz_edit_idx_ = scnt - 1;
    if (key == PK_UP && wiz_edit_idx_ > 0) { --wiz_edit_idx_; render(); }
    else if (key == PK_DOWN && wiz_edit_idx_ + 1 < scnt) { ++wiz_edit_idx_; render(); }
    else if (key == PK_ENTER) {
        char msg[96];
        node_->terminate_session(sids[wiz_edit_idx_], msg, sizeof(msg));
        pico_str_copy(status_fb_, sizeof(status_fb_), msg);
        prompt_kind_ = PICO_PROMPT_NONE;
        render();
    }
}

void PicoConsole::handle_ban_key(int key) {
    int bcnt = node_->ban_count();
    if (bcnt == 0) { if (wiz_focus_ == 4) wiz_focus_ = 0; wiz_edit_idx_ = 0; wiz_btn_ = 0; }
    else if (wiz_edit_idx_ >= bcnt) wiz_edit_idx_ = bcnt - 1;

    if (wiz_btn_) {
        if (key == 'y' || key == 'Y') {
            char msg[96];
            node_->unban_callsign(node_->ban_entry(wiz_edit_idx_).key, msg, sizeof(msg));
            pico_str_copy(wiz_err_, sizeof(wiz_err_), msg);
            pico_str_copy(status_fb_, sizeof(status_fb_), msg);
            wiz_btn_ = 0;
        } else if (key == 'n' || key == 'N' || key == PK_ESC) wiz_btn_ = 0;
        render(); return;
    }
    if (key == PK_ESC) { prompt_kind_ = PICO_PROMPT_NONE; render(); return; }
    if (key == PK_TAB) { wiz_focus_ = (wiz_focus_ + 1) % (bcnt > 0 ? 5 : 4); render(); return; }
    if (wiz_focus_ == 4) {
        if (key == PK_UP && wiz_edit_idx_ > 0) --wiz_edit_idx_;
        else if (key == PK_DOWN && wiz_edit_idx_ + 1 < bcnt) ++wiz_edit_idx_;
        else if (key == PK_ENTER) wiz_btn_ = 1;
        render(); return;
    }
    if (key == PK_ENTER) {
        if (!ban_call_tmp_[0]) strcpy(wiz_err_, "Enter a callsign first.");
        else {
            oyk_u32 flags = ((wiz_flags_ & 1U) ? OYK_BAN_HARD_BAN : 0UL) |
                            ((wiz_flags_ & 2U) ? OYK_BAN_APPEAL_ALLOWED : 0UL);
            char msg[96];
            if (node_->ban_callsign(ban_call_tmp_, flags,
                                    wiz_desc_[0] ? wiz_desc_ : "Banned by sysop.",
                                    wiz_id_[0] ? wiz_id_ : "permanent", msg, sizeof(msg))) {
                ban_call_tmp_[0] = '\0';
                strcpy(wiz_id_, "permanent");
                pico_str_copy(wiz_err_, sizeof(wiz_err_), msg);
                pico_str_copy(status_fb_, sizeof(status_fb_), msg);
            } else {
                pico_str_copy(wiz_err_, sizeof(wiz_err_), msg);
            }
        }
        render(); return;
    }
    if (wiz_focus_ == 3) {
        if (key == 'h' || key == 'H') wiz_flags_ ^= 1U;
        else if (key == 'a' || key == 'A') wiz_flags_ ^= 2U;
        render(); return;
    }
    char* tgt = (wiz_focus_ == 0) ? ban_call_tmp_ : ((wiz_focus_ == 1) ? wiz_id_ : wiz_desc_);
    size_t cap = (wiz_focus_ == 0) ? 12U : ((wiz_focus_ == 1) ? sizeof(wiz_id_) : sizeof(wiz_desc_));
    if (key == PK_BS) { size_t n = strlen(tgt); if (n > 0) tgt[n - 1] = '\0'; render(); }
    else if (key >= 32 && key < 127) {
        size_t n = strlen(tgt);
        if (n + 1 < cap) {
            char c = static_cast<char>(key);
            if (wiz_focus_ == 0 && c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
            tgt[n] = c; tgt[n + 1] = '\0';
        }
        render();
    }
}

void PicoConsole::handle_theme_key(int key) {
    const char* names[OYK_MAX_THEMES + 2];
    int n_names = 0;
    names[n_names++] = "dark";
    names[n_names++] = "light";
    for (int ti = 0; ti < draft_.theme_count && ti < OYK_MAX_THEMES; ++ti) {
        if (!oyk_theme_is_builtin(draft_.themes[ti].name)) names[n_names++] = draft_.themes[ti].name;
    }
    if (wiz_btn_) {
        if (key == PK_ESC) { wiz_btn_ = 0; render(); return; }
        if (key == PK_ENTER) {
            oyk_u32 new_rgb = 0;
            if (!oyk_theme_parse_hex(wiz_id_, &new_rgb)) {
                strcpy(wiz_err_, "Invalid #RRGGBB hex color."); wiz_btn_ = 0; render(); return;
            }
            if (oyk_theme_is_builtin(wiz_desc_)) {
                if (draft_.theme_count >= OYK_MAX_THEMES) {
                    strcpy(wiz_err_, "Maximum custom themes reached."); wiz_btn_ = 0; render(); return;
                }
                OykThemeConfig base_pal;
                pico_str_copy(draft_.theme_name, sizeof(draft_.theme_name), wiz_desc_);
                oyk_theme_resolve_effective(&draft_, 0, 1, &base_pal);
                int dark_b = (strcmp(wiz_desc_, "light") == 0) ? 0 : 1;
                char cname[24];
                oyk_theme_make_clone_name(&draft_, dark_b, cname, sizeof(cname));
                OykThemeConfig* nt = &draft_.themes[draft_.theme_count++];
                *nt = base_pal;
                nt->is_dark_base = dark_b ? 1 : 0;
                pico_str_copy(nt->name, sizeof(nt->name), cname);
                pico_str_copy(wiz_desc_, sizeof(wiz_desc_), nt->name);
            }
            for (int ti = 0; ti < draft_.theme_count; ++ti) {
                if (strcmp(draft_.themes[ti].name, wiz_desc_) == 0) {
                    draft_.themes[ti].colors[wiz_edit_idx_] = new_rgb;
                    break;
                }
            }
            pico_str_copy(draft_.theme_name, sizeof(draft_.theme_name), wiz_desc_);
            sprintf(wiz_err_, "Updated %s (%s).", wiz_desc_, oyk_theme_role_key(wiz_edit_idx_));
            wiz_btn_ = 0; render(); return;
        }
        if (key == PK_BS) { size_t n = strlen(wiz_id_); if (n > 0) wiz_id_[n - 1] = '\0'; render(); }
        else if (key >= 32 && key < 127) {
            size_t n = strlen(wiz_id_);
            if (n + 1 < sizeof(wiz_id_)) { wiz_id_[n] = static_cast<char>(key); wiz_id_[n + 1] = '\0'; }
            render();
        }
        return;
    }
    if (key == PK_ESC) { prompt_kind_ = PICO_PROMPT_NONE; render(); return; }
    if (key == PK_UP) { wiz_edit_idx_ = (wiz_edit_idx_ + OYK_THEME_COLOR_COUNT - 1) % OYK_THEME_COLOR_COUNT; render(); return; }
    if (key == PK_DOWN) { wiz_edit_idx_ = (wiz_edit_idx_ + 1) % OYK_THEME_COLOR_COUNT; render(); return; }
    if (key == PK_LEFT || key == PK_RIGHT) {
        int ci = 0;
        for (int i = 0; i < n_names; ++i) { if (strcmp(names[i], wiz_desc_) == 0) { ci = i; break; } }
        ci = (key == PK_RIGHT) ? ((ci + 1) % n_names) : ((ci + n_names - 1) % n_names);
        pico_str_copy(wiz_desc_, sizeof(wiz_desc_), names[ci]);
        pico_str_copy(draft_.theme_name, sizeof(draft_.theme_name), wiz_desc_);
        sprintf(wiz_err_, "Previewing '%.14s' (S/F2:Save  Esc:Revert).", wiz_desc_);
        render(); return;
    }
    if (key == 'x' || key == 'X') {
        if (!oyk_theme_is_builtin(wiz_desc_)) {
            for (int ti = 0; ti < draft_.theme_count; ++ti) {
                if (strcmp(draft_.themes[ti].name, wiz_desc_) == 0) {
                    for (int k = ti; k + 1 < draft_.theme_count; ++k) draft_.themes[k] = draft_.themes[k + 1];
                    --draft_.theme_count;
                    break;
                }
            }
            strcpy(wiz_desc_, "dark");
            pico_str_copy(draft_.theme_name, sizeof(draft_.theme_name), "dark");
        }
        render(); return;
    }
    if (key == PK_ENTER) {
        OykThemeConfig pal;
        pico_str_copy(draft_.theme_name, sizeof(draft_.theme_name), wiz_desc_);
        oyk_theme_resolve_effective(&draft_, 0, 1, &pal);
        oyk_theme_format_hex(pal.colors[wiz_edit_idx_], wiz_id_, sizeof(wiz_id_));
        wiz_btn_ = 1; render(); return;
    }
    if (key == 's' || key == 'S' || key == PK_F2) {
        pico_str_copy(draft_.theme_name, sizeof(draft_.theme_name), wiz_desc_);
        commit_settings();
        prompt_kind_ = PICO_PROMPT_NONE;
        render();
    }
}

void PicoConsole::handle_modal_key(int key) {
    if (prompt_kind_ == PICO_PROMPT_ADD_WIZARD) { handle_wizard_key(key); return; }
    if (prompt_kind_ == PICO_PROMPT_EDIT_IFACE) { handle_edit_iface_key(key); return; }
    if (prompt_kind_ == PICO_PROMPT_EDIT_PEER) { handle_edit_peer_key(key); return; }
    if (prompt_kind_ == PICO_PROMPT_TERM_SID) { handle_terminate_key(key); return; }
    if (prompt_kind_ == PICO_PROMPT_BAN_CALL) { handle_ban_key(key); return; }
    if (prompt_kind_ == PICO_PROMPT_THEME) { handle_theme_key(key); return; }
    if (prompt_kind_ == PICO_PROMPT_CONFIRM_FORMAT) {
        if (key == PK_ESC) { prompt_kind_ = PICO_PROMPT_NONE; render(); return; }
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
    if (key == PK_ESC) { prompt_kind_ = PICO_PROMPT_NONE; render(); }
    else if (key == PK_ENTER) { commit_prompt(); }
    else if (key == PK_BS && prompt_len_ > 0) { prompt_buf_[--prompt_len_] = '\0'; render(); }
    else if (key >= 32 && key < 127 && prompt_len_ + 1 < sizeof(prompt_buf_)) {
        prompt_buf_[prompt_len_++] = static_cast<char>(key);
        prompt_buf_[prompt_len_] = '\0';
        render();
    }
}
