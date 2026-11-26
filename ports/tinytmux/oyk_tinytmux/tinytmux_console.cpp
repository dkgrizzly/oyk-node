/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_console.hpp"
#include "board_tinytmux.hpp"
#include "tinytmux_fs.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum TinyTmuxVKey {
    TMUX_KEY_UP    = 0x101,
    TMUX_KEY_DOWN  = 0x102,
    TMUX_KEY_RIGHT = 0x103,
    TMUX_KEY_LEFT  = 0x104,
    TMUX_KEY_F2    = 0x105,
    TMUX_KEY_F10   = 0x106,
    TMUX_KEY_HOME  = 0x107,
    TMUX_KEY_END   = 0x108
};

static int strieq_ci(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? static_cast<char>(*b - 32) : *b;
        if (ca != cb) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static void con_str_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

struct TinyTmuxDriverOpt {
    const char* type;
    const char* desc;
};

static const TinyTmuxDriverOpt TINYTMUX_DRIVERS[7] = {
    { "axudp",          "AX.25 over UDP (Ethernet/USB)" },
    { "axip",           "AX.25 over IP (Protocol 93)" },
    { "ppp",            "PPP over Hardware UART (serN)" },
    { "kiss-serial",    "KISS TNC over UART (serN)" },
    { "usb-cdc-net",    "AXUDP over USB Host CDC-ECM" },
    { "usb-cdc-serial", "KISS over USB Host CDC-ACM" },
    { "telnet",         "Plaintext Telnet User Port" }
};

TinyTmuxConsole::TinyTmuxConsole()
    : node_(NULL), bridge_(NULL), tx_fn_(NULL), tx_ctx_(NULL), now_ms_(0),
      term_type_(TMUX_TERM_VT100), view_(TMUX_VIEW_CONSOLE), menu_sel_(0),
      files_tab_(0), pending_count_(0), approved_count_(0),
      settings_tab_(0), settings_iface_field_(0),
      prompt_kind_(PROMPT_NONE), prompt_len_(0),
      wiz_edit_idx_(-1), wiz_step_(0), wiz_drv_(0), wiz_last_drv_(-1), wiz_focus_(0), wiz_btn_(1),
      lterm_sid_(0), lterm_is_local_(0),
      lterm_line_count_(0), lterm_curline_len_(0), lterm_input_len_(0),
      esc_state_(ESC_IDLE), esc_len_(0), esc_start_ms_(0),
      last_marchc_redraw_ms_(0),
      bootloader_pending_(0), bootloader_start_ms_(0), storage_sel_(0) {
    esc_buf_[0] = '\0';
    strcpy(status_line_, "TinyTMUX Ready. M:Menu  Tab:Term  S:Settings  F:Files  R:Radio");
    dialog_fb_[0] = '\0';
    files_sel_[0] = 0;
    files_sel_[1] = 0;
    memset(pending_files_, 0, sizeof(pending_files_));
    memset(approved_files_, 0, sizeof(approved_files_));
    oyk_config_init_default(&draft_);
    memset(settings_sel_, 0, sizeof(settings_sel_));
    prompt_label_[0] = '\0';
    prompt_buf_[0] = '\0';
    ban_call_tmp_[0] = '\0';
    ban_action_tmp_[0] = '\0';
    wiz_id_[0] = '\0';
    wiz_desc_[0] = '\0';
    wiz_ep_[0] = '\0';
    wiz_port_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_err_[0] = '\0';
    memset(lterm_lines_, 0, sizeof(lterm_lines_));
    lterm_curline_[0] = '\0';
    lterm_input_[0] = '\0';
}

void TinyTmuxConsole::init(OykPortableNode* node, TinyTmuxConsoleTxFn tx_fn, void* tx_ctx) {
    node_ = node;
    tx_fn_ = tx_fn;
    tx_ctx_ = tx_ctx;
    term_type_ = TMUX_TERM_VT100;
    view_ = TMUX_VIEW_CONSOLE;
    menu_sel_ = 0;
    files_tab_ = 0;
    files_sel_[0] = 0;
    files_sel_[1] = 0;
    prompt_kind_ = PROMPT_NONE;
    prompt_len_ = 0;
    wiz_edit_idx_ = -1;
    wiz_step_ = 0;
    wiz_drv_ = 0;
    wiz_last_drv_ = -1;
    wiz_focus_ = 0;
    wiz_btn_ = 1;
    wiz_id_[0] = '\0';
    wiz_desc_[0] = '\0';
    wiz_ep_[0] = '\0';
    wiz_port_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_err_[0] = '\0';
    lterm_sid_ = 0;
    lterm_is_local_ = 0;
    lterm_line_count_ = 0;
    lterm_curline_len_ = 0;
    lterm_input_len_ = 0;
    bootloader_pending_ = 0;
    bootloader_start_ms_ = 0;
    reset_escape();
    if (node_) draft_ = node_->config();
    push_lterm_line("*** OYK TinyTMUX Local Terminal (Esc:Console, /local, /bbs, /chat, /disc)");
}

void TinyTmuxConsole::set_net_bridge(TinyTmuxNetBridge* bridge) {
    bridge_ = bridge;
}

const char* TinyTmuxConsole::term_type_label() const {
    switch (term_type_) {
        case TMUX_TERM_VT220:    return "VT220";
        case TMUX_TERM_VT340:    return "VT340";
        case TMUX_TERM_XTERM256: return "xterm-256";
        default:                 return "VT100";
    }
}

void TinyTmuxConsole::on_usb_connect() {
    term_type_ = TMUX_TERM_VT100;
    reset_escape();
    /* Asynchronous DA1 + DA2 inquiry; safe fallback stays in VT100 if terminal ignores queries. */
    write_str("\033[c\033[>c");
    render();
}

const char* TinyTmuxConsole::sgr_border() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[38;5;38m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[36m";
    return "";
}

const char* TinyTmuxConsole::sgr_title() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[1;38;5;81m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[1;36m";
    return "\033[1m";
}

const char* TinyTmuxConsole::sgr_header() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[1;97;48;5;24m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[1;37;44m";
    return "\033[1m";
}

const char* TinyTmuxConsole::sgr_menubar() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[30;48;5;252m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[30;47m";
    return "\033[7m";
}

const char* TinyTmuxConsole::sgr_menusel() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[1;97;48;5;25m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[1;37;44m";
    return "\033[7m";
}

const char* TinyTmuxConsole::sgr_list_hdr() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[1;97;48;5;238m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[1;97;100m";
    return "\033[1;7m";
}

const char* TinyTmuxConsole::sgr_sel() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[1;30;48;5;45m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[1;30;46m";
    return "\033[7m";
}

const char* TinyTmuxConsole::sgr_warn() const {
    if (term_type_ >= TMUX_TERM_XTERM256) return "\033[1;38;5;220m";
    if (term_type_ >= TMUX_TERM_VT340) return "\033[1;33m";
    return "\033[1m";
}

OYK_FLASHMEM void TinyTmuxConsole::draw_dec_box(int row, int col, int h, int w,
                                                const char* title, int clear_interior) {
    if (h < 2 || w < 4) return;
    char line[112];
    int inner = w - 2;
    if (inner > 78) inner = 78;

    /* Top border */
    sprintf(line, "\033[%d;%dH%s\033(0l", row, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'k';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");

    if (title && title[0] && inner > 4) {
        char tbuf[80];
        int max_t = inner - 4;
        if (max_t > 68) max_t = 68;
        sprintf(tbuf, " %.*s ", max_t, title);
        sprintf(line, "\033[%d;%dH%s%s\033[0m", row, col + 2, sgr_title(), tbuf);
        write_str(line);
    }

    /* Sides */
    for (int r = 1; r < h - 1; ++r) {
        if (clear_interior) {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B\033[0m%*s%s\033(0x\033(B\033[0m",
                    row + r, col, sgr_border(), inner, "", sgr_border());
            write_str(line);
        } else {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B\033[0m\033[%d;%dH%s\033(0x\033(B\033[0m",
                    row + r, col, sgr_border(),
                    row + r, col + w - 1, sgr_border());
            write_str(line);
        }
    }

    /* Bottom border */
    sprintf(line, "\033[%d;%dH%s\033(0m", row + h - 1, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'j';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");
}

OYK_FLASHMEM void TinyTmuxConsole::draw_dec_hdiv(int row, int col, int w) {
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

void TinyTmuxConsole::write_str(const char* s) {
    if (tx_fn_ && s && s[0]) {
        tx_fn_(s, strlen(s), tx_ctx_);
    }
}

void TinyTmuxConsole::push_lterm_line(const char* line) {
    if (!line) return;
    if (lterm_line_count_ < 16) {
        strncpy(lterm_lines_[lterm_line_count_], line, 79);
        lterm_lines_[lterm_line_count_][79] = '\0';
        lterm_line_count_++;
    } else {
        for (int i = 1; i < 16; ++i) {
            memcpy(lterm_lines_[i - 1], lterm_lines_[i], 80);
        }
        strncpy(lterm_lines_[15], line, 79);
        lterm_lines_[15][79] = '\0';
    }
}

void TinyTmuxConsole::on_local_term_event(oyk_u32 sid, int event_type,
                                          const oyk_u8* data, size_t len,
                                          const char* reason) {
    if (sid != lterm_sid_) return;
    if (event_type == 1) {
        push_lterm_line("*** Connected.");
    } else if (event_type == 2 && data && len > 0) {
        for (size_t i = 0; i < len; ++i) {
            oyk_u8 ch = data[i];
            if (ch == '\r' || ch == '\n') {
                if (lterm_curline_len_ > 0 || ch == '\r') {
                    lterm_curline_[lterm_curline_len_] = '\0';
                    push_lterm_line(lterm_curline_);
                    lterm_curline_len_ = 0;
                }
            } else if (ch >= 0x20U && ch < 0x7FU) {
                if (lterm_curline_len_ < 78) {
                    lterm_curline_[lterm_curline_len_++] = static_cast<char>(ch);
                }
            }
        }
    } else if (event_type == 3) {
        lterm_sid_ = 0;
        lterm_is_local_ = 0;
        char msg[80];
        sprintf(msg, "*** Disconnected (%s)", reason ? reason : "Closed");
        push_lterm_line(msg);
    }
}

void TinyTmuxConsole::render() {
    if (!node_ || !tx_fn_) return;
    if (term_type_ >= TMUX_TERM_VT220) {
        write_str("\033[?25l");
    }
    switch (view_) {
        case TMUX_VIEW_LOCAL_TERM:
            render_local_terminal();
            break;
        case TMUX_VIEW_FILES:
            render_files();
            break;
        case TMUX_VIEW_RIG:
            render_rig();
            break;
        case TMUX_VIEW_SETTINGS:
            render_settings();
            break;
        case TMUX_VIEW_PSRAM:
            render_psram();
            break;
        case TMUX_VIEW_BOOTLOADER:
            render_bootloader();
            break;
        case TMUX_VIEW_STORAGE:
            render_storage();
            break;
        case TMUX_VIEW_MENU:
            render_console();
            render_menu();
            break;
        default:
            render_console();
            break;
    }
    if (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD) {
        render_add_iface_wizard();
    } else if (prompt_kind_ == PROMPT_SETTINGS_EDIT_IFACE) {
        render_edit_iface_dialog();
    } else if (prompt_kind_ == PROMPT_SETTINGS_EDIT_PEER) {
        render_edit_peer_dialog();
    } else if (prompt_kind_ == PROMPT_CONFIRM_FORMAT) {
        render_confirm_format_modal();
    } else if (prompt_kind_ != PROMPT_NONE) {
        render_prompt_overlay();
    }
    if (term_type_ >= TMUX_TERM_VT220 &&
        (view_ == TMUX_VIEW_LOCAL_TERM ||
         (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD &&
          (wiz_step_ == 1 ||
           (wiz_step_ == 2 &&
            wiz_focus_ < ((wiz_drv_ == 0 || wiz_drv_ == 4) ? 3 : ((wiz_drv_ == 1) ? 3 : 2))))) ||
         (prompt_kind_ == PROMPT_SETTINGS_EDIT_IFACE &&
          wiz_focus_ >= 1 &&
          wiz_focus_ < ((wiz_drv_ == 0 || wiz_drv_ == 4) ? 6 : ((wiz_drv_ == 1) ? 6 : 5))) ||
         (prompt_kind_ == PROMPT_SETTINGS_EDIT_PEER &&
          (wiz_focus_ == 0 || wiz_focus_ == 1 || wiz_focus_ == 2 || wiz_focus_ == 4 || wiz_focus_ == 5)) ||
         (prompt_kind_ != PROMPT_NONE &&
          prompt_kind_ != PROMPT_SETTINGS_ADD_WIZARD &&
          prompt_kind_ != PROMPT_SETTINGS_EDIT_IFACE &&
          prompt_kind_ != PROMPT_SETTINGS_EDIT_PEER))) {
        write_str("\033[?25h");
    }
}

void TinyTmuxConsole::render_console() {
    char buf[192];
    char call_s[16], psram_s[8], usbh_s[8];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    tinytmux_psram_format_short(tinytmux_psram_info(), psram_s, sizeof(psram_s));
    if (bridge_) {
        bridge_->usb_host()->format_short_status(usbh_s, sizeof(usbh_s));
    } else {
        strcpy(usbh_s, "-");
    }

    write_str("\033[H\033[2J");

    /* Row 1: Top Menu Bar */
    sprintf(buf, "%s Node (M) | Settings (S) | Files (F) | Radio (R) | %-16s [%-9s] \033[0m",
            sgr_menubar(),
            lterm_sid_ ? "Tab:Term(ACTIVE)" : "Tab:Term",
            term_type_label());
    write_str(buf);

    /* Row 2: Header Bar with PSRAM and USBH hardware indicators */
    sprintf(buf, "\033[2;1H%s OYK TMUX: %-9s Up:%-5lus C:%-2d [IN:%-5s] PSRAM:%-3s USBH:%-3s            \033[0m",
            sgr_header(),
            call_s,
            static_cast<unsigned long>(node_->uptime_seconds()),
            node_->total_active_connections(),
            node_->accept_incoming() ? "ALLOW" : "DENY",
            psram_s,
            usbh_s);
    write_str(buf);

    /* Rows 3..10: Side-by-side DEC-bordered Interfaces (left) & New Callsigns (right) */
    draw_dec_box(3, 1, 8, 45, "Interfaces", 0);
    draw_dec_box(3, 46, 8, 35, "Last 5 New Callsigns Heard", 0);

    sprintf(buf, "\033[4;2H%s %-8s %-12s %-2s %-14s\033[0m",
            sgr_list_hdr(), "Port", "Type", "C", "Status / RX-TX");
    write_str(buf);
    sprintf(buf, "\033[4;47H%s %-2s %-9s %-7s %-8s \033[0m",
            sgr_list_hdr(), "#", "Callsign", "Iface", "Time");
    write_str(buf);

    for (int i = 0; i < 5; ++i) {
        char left[48], right[40];
        if (i < node_->iface_count()) {
            const OykIfaceRuntime& r = node_->iface(i);
            sprintf(left, " %-8.8s %-12.12s %-2d %-14.14s",
                    r.cfg.id, r.cfg.type, node_->active_connections_on(i), r.status);
        } else {
            left[0] = '\0';
        }
        if (i < node_->new_heard_count()) {
            const OykNewHeard& nh = node_->new_heard(i);
            sprintf(right, " %d %-9.9s %-7.7s %-8.8s", i + 1, nh.callsign, nh.iface_id, nh.time_hms);
        } else {
            right[0] = '\0';
        }
        sprintf(buf, "\033[%d;2H%-43.43s\033[%d;47H%-33.33s", 5 + i, left, 5 + i, right);
        write_str(buf);
    }

    /* Rows 11..16: Active Connections */
    char conn_title[48];
    sprintf(conn_title, "Active Connections (%d) | Bans: %d",
            node_->total_active_connections(), node_->ban_count());
    draw_dec_box(11, 1, 6, 80, conn_title, 0);
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
                    13 + shown,
                    static_cast<unsigned long>(s.id), cs,
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

    /* Rows 17..23: Node Activity Log */
    draw_dec_box(17, 1, 7, 80, "Node Activity Log", 0);
    int total_logs = node_->console_log_count();
    int start = (total_logs > 5) ? (total_logs - 5) : 0;
    int lrow = 0;
    for (int i = start; i < total_logs && lrow < 5; ++i, ++lrow) {
        sprintf(buf, "\033[%d;2H %-77.77s", 18 + lrow, node_->console_log_line(i));
        write_str(buf);
    }
    for (; lrow < 5; ++lrow) {
        sprintf(buf, "\033[%d;2H%-78s", 18 + lrow, "");
        write_str(buf);
    }

    /* Row 24: Bottom Status Line */
    sprintf(buf, "\033[24;1H%s %-78.78s \033[0m", sgr_warn(), status_line_);
    write_str(buf);
}

void TinyTmuxConsole::render_menu() {
    static const char* ITEMS[13] = {
        " [A] Allow Incoming Connections   ",
        " [D] Deny Incoming Connections    ",
        " [L] Local Terminal...            ",
        " [F] Manage Files Area...         ",
        " [R] Radio Control...             ",
        " [S] Settings...                  ",
        " [E] Peers...                     ",
        " [T] Terminate Connection...      ",
        " [B] Ban / Unban Callsign...      ",
        " [P] PSRAM Diagnostics...         ",
        " [G] Storage Manager...           ",
        " [U] Enter HalfKay Bootloader     ",
        " [X] Close Menu                   "
    };
    static const int ITEM_ROWS[13] = { 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17 };

    draw_dec_box(2, 2, 17, 36, "Node Menu", 0);
    draw_dec_hdiv(5, 2, 36);
    draw_dec_hdiv(16, 2, 36);
    for (int i = 0; i < 13; ++i) {
        char row[80];
        sprintf(row, "\033[%d;3H%s%.34s\033[0m",
                ITEM_ROWS[i], (i == menu_sel_) ? sgr_menusel() : "", ITEMS[i]);
        write_str(row);
    }
}

void TinyTmuxConsole::render_local_terminal() {
    char buf[160];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s OYK LOCAL TERMINAL (%s) | Esc:Console  /local /bbs /chat /disc \033[0m\r\n",
            sgr_header(), lterm_sid_ ? "CONNECTED" : "DISCONNECTED");
    write_str(buf);
    for (int i = 0; i < lterm_line_count_; ++i) {
        write_str(lterm_lines_[i]);
        write_str("\r\n");
    }
    sprintf(buf, "> %s", lterm_input_);
    write_str(buf);
}

void TinyTmuxConsole::refresh_files_lists() {
    pending_count_ = oyk_bbs_list_incoming("incoming", pending_files_, 16);
    approved_count_ = oyk_bbs_list_approved(node_->config().bbs_files_path, approved_files_, 16);
    if (files_sel_[0] >= pending_count_) files_sel_[0] = (pending_count_ > 0) ? (pending_count_ - 1) : 0;
    if (files_sel_[1] >= approved_count_) files_sel_[1] = (approved_count_ > 0) ? (approved_count_ - 1) : 0;
}

void TinyTmuxConsole::render_files() {
    char buf[192];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s FILES AREA MANAGER (TinyTMUX)                       Esc:Back  R:Refresh \033[0m", sgr_header());
    write_str(buf);
    sprintf(buf, "\033[2;2H%s 1:Pending Incoming (%d) \033[0m  %s 2:Approved Files (%d) \033[0m",
            (files_tab_ == 0) ? sgr_menusel() : sgr_menubar(), pending_count_,
            (files_tab_ == 1) ? sgr_menusel() : sgr_menubar(), approved_count_);
    write_str(buf);

    draw_dec_box(3, 1, 19, 80, (files_tab_ == 0) ? "Pending Incoming" : "Approved Files", 0);

    if (files_tab_ == 0) {
        sprintf(buf, "\033[4;2H%s %-15s %-46s %-14s\033[0m",
                sgr_list_hdr(), "Uploader", "Filename", "Size");
        write_str(buf);
        int max_show = 15;
        for (int i = 0; i < max_show; ++i) {
            if (pending_count_ == 0 && i == 0) {
                sprintf(buf, "\033[%d;2H %-77s", 5 + i, "(no pending uploads in incoming/)");
            } else if (i < pending_count_) {
                sprintf(buf, "\033[%d;2H%s %-15.15s %-46.46s %-14lu\033[0m",
                        5 + i, (i == files_sel_[0]) ? sgr_sel() : "",
                        pending_files_[i].uploader,
                        pending_files_[i].filename,
                        static_cast<unsigned long>(pending_files_[i].size));
            } else {
                sprintf(buf, "\033[%d;2H%-78s", 5 + i, "");
            }
            write_str(buf);
        }
        sprintf(buf, "\033[22;1H%s Tab/1-2:Tabs  Enter/A:Approve  D:Delete  R:Refresh  Esc:Close              \033[0m",
                sgr_menubar());
        write_str(buf);
    } else {
        sprintf(buf, "\033[4;2H%s %-62s %-14s\033[0m",
                sgr_list_hdr(), "Approved File Path", "Size");
        write_str(buf);
        int max_show = 15;
        for (int i = 0; i < max_show; ++i) {
            if (approved_count_ == 0 && i == 0) {
                sprintf(buf, "\033[%d;2H %-77s", 5 + i, "(no approved files in BBS files area)");
            } else if (i < approved_count_) {
                sprintf(buf, "\033[%d;2H%s %-62.62s %-14lu\033[0m",
                        5 + i, (i == files_sel_[1]) ? sgr_sel() : "",
                        approved_files_[i].rel_path,
                        static_cast<unsigned long>(approved_files_[i].size));
            } else {
                sprintf(buf, "\033[%d;2H%-78s", 5 + i, "");
            }
            write_str(buf);
        }
        sprintf(buf, "\033[22;1H%s Tab/1-2:Tabs  D:Remove Approved  R:Refresh  Esc:Close                      \033[0m",
                sgr_menubar());
        write_str(buf);
    }
    sprintf(buf, "\033[24;1H%s Status: %-70.70s \033[0m",
            sgr_warn(), dialog_fb_[0] ? dialog_fb_ : "Ready.");
    write_str(buf);
}

void TinyTmuxConsole::render_rig() {
    char buf[192];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s RADIO CONTROL (Hamlib rigctl / RadioFoundry UDP 4191)         Esc:Close \033[0m", sgr_header());
    write_str(buf);
    draw_dec_box(2, 1, 20, 80, "Transceiver Control", 0);

    const OykConfig& cfg = node_->config();
    if (!cfg.rig_enabled) {
        write_str("\033[4;3HRig control is DISABLED. Configure in Settings (S) -> Tab 5:Rig.");
    } else {
        OykRigStatus st;
        oyk_rig_status_init(&st, oyk_rig_parse_protocol(cfg.rig_protocol));
        if (bridge_) st = *bridge_->rig_status();

        sprintf(buf, "\033[4;3HProtocol : %-14s  Target: %s:%u  Notify: %s",
                oyk_rig_protocol_name(st.protocol),
                cfg.rig_host[0] ? cfg.rig_host : "(peer)",
                static_cast<unsigned int>(cfg.rig_port ? cfg.rig_port : oyk_rig_default_port(st.protocol)),
                cfg.rig_notify ? "ON" : "OFF");
        write_str(buf);
        sprintf(buf, "\033[5;3HStatus   : %s", st.reachable ? "REACHABLE" : "NO RESPONSE / IDLE");
        write_str(buf);

        char summary[128];
        oyk_rig_format_summary(&st, summary, sizeof(summary));
        sprintf(buf, "\033[6;3HRadio    : %.64s", summary);
        write_str(buf);

        if (st.protocol == OYK_RIG_PROTO_RADIOFOUNDRY) {
            write_str("\033[8;3HCommands (RadioFoundry / takRF UDP 4191):");
            write_str("\033[9;3H  [J/K] Step Channel -/+1    [H/L] Step Zone -/+1");
            write_str("\033[10;3H  [S]   Select Zone:Pos      [N]   Stop Notify     [R] Poll Now");
        } else {
            write_str("\033[8;3HCommands (Hamlib rigctld TCP 4532):");
            write_str("\033[9;3H  [F] Set Frequency (Hz)     [M] Set Mode          [P] Toggle PTT");
            write_str("\033[10;3H  [V] Set VFO (VFOA/VFOB)    [R] Poll Status");
        }
    }
    sprintf(buf, "\033[24;1H%s Feedback: %-68.68s \033[0m",
            sgr_warn(), dialog_fb_[0] ? dialog_fb_ : "Ready.");
    write_str(buf);
}

int TinyTmuxConsole::settings_tab_item_count(int tab) const {
    if (tab == 0) return 10;
    if (tab == 1) return 8;
    if (tab == 2) return 6;
    if (tab == 3) return draft_.iface_count;
    if (tab == 4) return draft_.peer_count;
    if (tab == 5) return 6;
    return 6;
}

OYK_FLASHMEM void TinyTmuxConsole::render_settings() {
    static const char* TAB_NAMES[7] = {
        "1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn"
    };
    static const char* NODE_LABELS[10] = {
        "Node Callsign", "Node Alias", "Welcome Message", "Info Message",
        "Allow Incoming (Default)", "Allow Logging to Flash",
        "Learned Peer Policy", "New User Welcome",
        "Interfaces File", "Peers File"
    };
    static const char* BBS_LABELS[8] = {
        "Enable Packet BBS", "BBS SSID (0-15)", "Callsign Override", "BBS Alias",
        "Enable BBS Files Area", "Files Area Root Path", "Mail Storage Path", "BBS Welcome Message"
    };
    static const char* CHAT_LABELS[6] = {
        "Enable Multi-User CHAT", "CHAT SSID (0-15)", "Callsign Override", "CHAT Alias",
        "Default Topic", "CHAT Welcome Message"
    };
    static const char* RIG_LABELS[6] = {
        "Enable Rig Control", "Protocol (rigctl/radiofoundry)", "Radio Host / IP",
        "Port (4532 / 4191)", "Poll Interval (sec)", "RadioFoundry Notify"
    };
    static const char* PRN_LABELS[6] = {
        "Enable Line Printer", "Device (usb-printer/serN)", "Protocol (generic/mx80/escp2)",
        "Serial Baud Rate", "Printer Log Mask (0x..)", "Condensed 132-Col Mode"
    };

    char buf[192];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s OYK NODE SETTINGS (TinyTMUX)            F2/S:Save & Apply  Esc:Cancel \033[0m", sgr_header());
    write_str(buf);
    write_str("\033[2;2H");
    for (int t = 0; t < 7; ++t) {
        sprintf(buf, "%s %s \033[0m ", (t == settings_tab_) ? sgr_menusel() : sgr_menubar(), TAB_NAMES[t]);
        write_str(buf);
    }
    draw_dec_box(3, 1, 19, 80, TAB_NAMES[settings_tab_], 0);

    int cnt = settings_tab_item_count(settings_tab_);
    if (settings_tab_ != 3 && settings_tab_ != 4) {
        const char** labels = (settings_tab_ == 0) ? NODE_LABELS
                            : ((settings_tab_ == 1) ? BBS_LABELS
                                                    : ((settings_tab_ == 2) ? CHAT_LABELS
                                                                            : ((settings_tab_ == 5) ? RIG_LABELS : PRN_LABELS)));
        for (int i = 0; i < cnt; ++i) {
            char val[56], cs[16];
            val[0] = '\0';
            if (settings_tab_ == 0) {
                if (i == 0) oyk_format_callsign(&draft_.callsign, val, sizeof(val));
                else if (i == 1) { if (draft_.alias.base[0]) oyk_format_callsign(&draft_.alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 2) { strncpy(val, draft_.welcome_text, 44); val[44] = '\0'; }
                else if (i == 3) { strncpy(val, draft_.info_text, 44); val[44] = '\0'; }
                else if (i == 4) strcpy(val, draft_.accept_incoming ? "[ON]" : "[OFF]");
                else if (i == 5) strcpy(val, draft_.allow_flash_logging ? "[ON]" : "[OFF]");
                else if (i == 6) strcpy(val, oyk_format_learned_policy(draft_.learned_peer_policy));
                else if (i == 7) { if (draft_.new_user_welcome[0]) { strncpy(val, draft_.new_user_welcome, 44); val[44] = '\0'; } else strcpy(val, "(use default)"); }
                else if (i == 8) { strncpy(val, draft_.interfaces_file, 44); val[44] = '\0'; }
                else { strncpy(val, draft_.peers_file, 44); val[44] = '\0'; }
            } else if (settings_tab_ == 1) {
                if (i == 0) strcpy(val, draft_.bbs_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign bc; oyk_config_resolved_bbs_callsign(&draft_, &bc); oyk_format_callsign(&bc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.bbs_ssid), cs); }
                else if (i == 2) { if (draft_.bbs_callsign.base[0]) oyk_format_callsign(&draft_.bbs_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.bbs_alias.base[0]) oyk_format_callsign(&draft_.bbs_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) strcpy(val, draft_.bbs_files_enabled ? "[ON]" : "[OFF]");
                else if (i == 5) { strncpy(val, draft_.bbs_files_path, 44); val[44] = '\0'; }
                else if (i == 6) { strncpy(val, draft_.bbs_mail_path, 44); val[44] = '\0'; }
                else { strncpy(val, draft_.bbs_welcome, 44); val[44] = '\0'; }
            } else if (settings_tab_ == 2) {
                if (i == 0) strcpy(val, draft_.chat_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign cc; oyk_config_resolved_chat_callsign(&draft_, &cc); oyk_format_callsign(&cc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.chat_ssid), cs); }
                else if (i == 2) { if (draft_.chat_callsign.base[0]) oyk_format_callsign(&draft_.chat_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.chat_alias.base[0]) oyk_format_callsign(&draft_.chat_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) { strncpy(val, draft_.chat_topic, 44); val[44] = '\0'; }
                else { strncpy(val, draft_.chat_welcome, 44); val[44] = '\0'; }
            } else if (settings_tab_ == 5) {
                if (i == 0) strcpy(val, draft_.rig_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { strncpy(val, draft_.rig_protocol[0] ? draft_.rig_protocol : "none", 44); val[44] = '\0'; }
                else if (i == 2) { strncpy(val, draft_.rig_host, 44); val[44] = '\0'; }
                else if (i == 3) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_port));
                else if (i == 4) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_poll_sec));
                else strcpy(val, draft_.rig_notify ? "[ON]" : "[OFF]");
            } else {
                if (i == 0) strcpy(val, draft_.printer_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { strncpy(val, draft_.printer_device[0] ? draft_.printer_device : "(none)", 44); val[44] = '\0'; }
                else if (i == 2) { strncpy(val, draft_.printer_protocol[0] ? draft_.printer_protocol : "generic", 44); val[44] = '\0'; }
                else if (i == 3) sprintf(val, "%lu", static_cast<unsigned long>(draft_.printer_baud ? draft_.printer_baud : 9600UL));
                else if (i == 4) sprintf(val, "0x%lX", static_cast<unsigned long>(draft_.printer_level));
                else strcpy(val, draft_.printer_condensed ? "[ON]" : "[OFF]");
            }
            if (settings_tab_ == 0 && (i == 8 || i == 9) && val[0] == '\0' && term_type_ >= TMUX_TERM_VT340) {
                sprintf(buf, "\033[%d;2H%s %-29.29s : \033[2m<inline>\033[22m                                    \033[0m",
                        4 + i, (i == settings_sel_[settings_tab_]) ? sgr_sel() : "", labels[i]);
            } else {
                sprintf(buf, "\033[%d;2H%s %-29.29s : %-44.44s\033[0m",
                        4 + i, (i == settings_sel_[settings_tab_]) ? sgr_sel() : "", labels[i], val);
            }
            write_str(buf);
        }
        sprintf(buf, "\033[22;1H%s Tab/1-7:Tabs  Up/Dn:Select  Enter/E:Edit  S/F2:Save & Apply  Esc:Cancel \033[0m",
                sgr_menubar());
        write_str(buf);
    } else if (settings_tab_ == 3) {
        sprintf(buf, "\033[4;2H%s %-8s %-15s %-25s %-25s\033[0m",
                sgr_list_hdr(), "ID", "Type", "Dev/Host", "Port/Baud");
        write_str(buf);
        if (draft_.iface_count == 0) {
            write_str("\033[5;2H (no interfaces configured; press N to add)");
        } else {
            for (int i = 0; i < draft_.iface_count && i < 14; ++i) {
                const OykIfaceConfig& ic = draft_.ifaces[i];
                const char* loc = ic.device[0] ? ic.device : ic.host;
                sprintf(buf, "\033[%d;2H%s %-8.8s %-15.15s %-25.25s %u/%-22lu\033[0m",
                        5 + i, (i == settings_sel_[3]) ? sgr_sel() : "",
                        ic.id, ic.type, loc,
                        static_cast<unsigned int>(ic.port),
                        static_cast<unsigned long>(ic.baud));
                write_str(buf);
            }
        }
        sprintf(buf, "\033[22;1H%s N/A:New  Enter/E:Edit  D:Delete  H:Host/Dev  S/F2:Save  Esc:Cancel     \033[0m",
                sgr_menubar());
        write_str(buf);
    } else {
        sprintf(buf, "\033[4;2H%s %-10s %-9s %-8s %-22s %-7s %-14s\033[0m",
                sgr_list_hdr(), "ID", "Callsign", "Iface", "Endpoint", "ACL", "Tag");
        write_str(buf);
        if (draft_.peer_count == 0) {
            write_str("\033[5;2H (no peers configured; press N/A to add)");
        } else {
            for (int i = 0; i < draft_.peer_count && i < 14; ++i) {
                const OykPeerConfig& pc = draft_.peers[i];
                char ep[26], acl[10];
                if (pc.host[0] && pc.port > 0) sprintf(ep, "%.15s:%u", pc.host, static_cast<unsigned int>(pc.port));
                else if (pc.host[0]) sprintf(ep, "%.21s", pc.host);
                else strcpy(ep, "(any)");
                sprintf(acl, "%s/%s", pc.allow_inbound ? "IN" : "--", pc.allow_outbound ? "OUT" : "---");
                sprintf(buf, "\033[%d;2H%s %-10.10s %-9.9s %-8.8s %-22.22s %-7.7s %-14s\033[0m",
                        5 + i, (i == settings_sel_[4]) ? sgr_sel() : "",
                        pc.id, pc.callsign, pc.interface_id, ep, acl,
                        pc.auto_learned ? "[AUTO]" : "");
                write_str(buf);
            }
        }
        sprintf(buf, "\033[22;1H%s 1-7:Tabs A:Add E:Edit D:Del I:In O:Out K:KeepAuto S/F2:Save Esc:Back \033[0m",
                sgr_menubar());
        write_str(buf);
    }
    sprintf(buf, "\033[24;1H%s %-78.78s \033[0m",
            sgr_warn(), dialog_fb_[0] ? dialog_fb_ : "Edit settings, then press S or F2 to Save & Apply.");
    write_str(buf);
}

static void format_progress_bar(oyk_u8 pct, char* out, size_t out_sz) {
    if (!out || out_sz < 23) return;
    int filled = (static_cast<int>(pct) * 20) / 100;
    if (filled < 0) filled = 0;
    if (filled > 20) filled = 20;
    out[0] = '[';
    for (int i = 0; i < 20; ++i) {
        out[1 + i] = (i < filled) ? '#' : '.';
    }
    out[21] = ']';
    out[22] = '\0';
}

OYK_FLASHMEM void TinyTmuxConsole::render_psram() {
    char buf[192], tag[8];
    const TinyTmuxPsramInfo* info = tinytmux_psram_info();
    const TinyTmuxMarchCStatus* mc = tinytmux_psram_marchc_status();
    tinytmux_psram_format_short(info, tag, sizeof(tag));
    write_str("\033[H\033[2J");
    sprintf(buf, "%s TEENSY 4.1 FLEXSPI2 PSRAM DIAGNOSTICS   T:Quick  M:MarchC-  Esc:Back \033[0m", sgr_header());
    write_str(buf);
    draw_dec_box(2, 1, 21, 80, "FlexSPI2 Hardware & March C-", 0);

    sprintf(buf, "\033[3;3HStatus      : %-6s  Base: 0x%08lX  Quick test: %lu ms",
            tag,
            static_cast<unsigned long>(info->base_addr),
            static_cast<unsigned long>(info->test_time_ms));
    write_str(buf);
    sprintf(buf, "\033[4;3HCapacity    : %lu MB usable / %lu MB detected (%d/%d chips passed)",
            static_cast<unsigned long>(info->usable_bytes / (1024UL * 1024UL)),
            static_cast<unsigned long>(info->detected_bytes / (1024UL * 1024UL)),
            info->usable_chip_count, info->chip_count);
    write_str(buf);

    int r = 6;
    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        const TinyTmuxPsramChipInfo& c = info->chips[i];
        const char* kind_str = "EMPTY";
        if (c.chip_kind == TMUX_FLEXSPI_PSRAM) kind_str = "PSRAM";
        else if (c.chip_kind == TMUX_FLEXSPI_FLASH_NOR) kind_str = "QSPI NOR";
        else if (c.chip_kind == TMUX_FLEXSPI_FLASH_NAND) kind_str = "QSPI NAND";
        else if (c.detected) kind_str = "DETECTED";
        sprintf(buf, "\033[%d;3HChip %d (CS%d @ 0x%08lX): %-9s  %s  %lu MB  JEDEC=%02X %02X %02X",
                r++, i, i,
                static_cast<unsigned long>(c.base_addr),
                kind_str,
                (c.chip_kind == TMUX_FLEXSPI_PSRAM) ? (c.passed ? "PASS" : "FAIL")
                                                    : (c.detected ? "FLASH" : "-"),
                static_cast<unsigned long>(c.size_bytes / (1024UL * 1024UL)),
                static_cast<unsigned int>(c.jedec_id[0]),
                static_cast<unsigned int>(c.jedec_id[1]),
                static_cast<unsigned int>(c.jedec_id[2]));
        write_str(buf);
        if (c.chip_kind == TMUX_FLEXSPI_PSRAM && mc->state != TMUX_MARCHC_IDLE &&
            mc->chip_result[i] != TMUX_MARCHC_CHIP_NONE) {
            char bar[28];
            format_progress_bar(mc->chip_percent[i], bar, sizeof(bar));
            const char* res_s = "WAIT";
            if (mc->chip_result[i] == TMUX_MARCHC_CHIP_RUNNING) res_s = mc->pass_label;
            else if (mc->chip_result[i] == TMUX_MARCHC_CHIP_PASS) res_s = "PASS";
            else if (mc->chip_result[i] == TMUX_MARCHC_CHIP_FAIL) res_s = "FAIL";
            sprintf(buf, "\033[%d;5HMarch C-: %s %3u%%  %-16s",
                    r++, bar, static_cast<unsigned int>(mc->chip_percent[i]), res_s);
            write_str(buf);
        }
        if (c.detected && c.chip_kind == TMUX_FLEXSPI_PSRAM && !c.passed) {
            sprintf(buf, "\033[%d;5HFault @ 0x%08lX  Check footprint: %s",
                    r++, static_cast<unsigned long>(c.fail_addr),
                    tinytmux_psram_footprint_label(i));
            write_str(buf);
        }
    }

    if (info->misplaced_flash_cs0) {
        sprintf(buf, "\033[%d;3H%s WARNING: QSPI FLASH ON WRONG PADS (CS0 / Pin 48 small footprint)! \033[0m",
                r++, sgr_sel());
        write_str(buf);
        sprintf(buf, "\033[%d;3HTeensy 4.1 LittleFS_QSPI only supports CS1 (large pads / Pin 51).", r++);
        write_str(buf);
        sprintf(buf, "\033[%d;3HStartup CS0 probes PSRAM first; move flash to CS1 to use QSPI storage.", r++);
        write_str(buf);
    }

    ++r;
    if (mc->state == TMUX_MARCHC_RUNNING) {
        sprintf(buf, "\033[%d;3HMarch C- RUNNING: Chip %d (%s)  Elapsed: %lu ms  (Esc: Cancel)",
                r, mc->active_chip, mc->pass_label, static_cast<unsigned long>(mc->elapsed_ms));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_PASSED) {
        sprintf(buf, "\033[%d;3HMarch C- PASSED cleanly on all detected chips in %lu ms.",
                r, static_cast<unsigned long>(mc->elapsed_ms));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_FAILED) {
        sprintf(buf, "\033[%d;3H%s MARCH C- FAIL: Chip %d @ 0x%08lX (exp %08lX got %08lX) \033[0m",
                r++, sgr_sel(), mc->fail_chip,
                static_cast<unsigned long>(mc->fail_addr),
                static_cast<unsigned long>(mc->fail_expected),
                static_cast<unsigned long>(mc->fail_actual));
        write_str(buf);
        sprintf(buf, "\033[%d;3HCheck/resolder footprint: %s", r, tinytmux_psram_footprint_label(mc->fail_chip));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_CANCELLED) {
        sprintf(buf, "\033[%d;3HMarch C- test cancelled by operator.", r);
        write_str(buf);
    } else {
        sprintf(buf, "\033[%d;3HPress T for quick self-test, M for full March C- test, Esc to return.", r);
        write_str(buf);
    }
    sprintf(buf, "\033[24;1H%s T:Quick Test  M:Full March C- Test  Esc:Return                             \033[0m",
            sgr_menubar());
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::render_storage() {
    char buf[200];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s STORAGE MANAGER (Config, Files, Mail & Logs)                 Esc:Back \033[0m", sgr_header());
    write_str(buf);
    draw_dec_box(2, 1, 20, 80, "Non-Volatile Storage Tiers", 0);
    write_str("\033[3;3HSearch order: 1) microSD  2) QSPI Flash  3) Internal 4 MB Flash");
    sprintf(buf, "\033[5;2H%s %-2s %-28s %-9s %-14s %-21s\033[0m",
            sgr_list_hdr(), "#", "Storage Device", "Status", "Used / Total", "Log Policy");
    write_str(buf);

    int active = tinytmux_fs_active_tier();
    for (int t = TMUX_STORAGE_SD; t <= TMUX_STORAGE_INTERNAL; ++t) {
        TinyTmuxStorageTierInfo ti;
        tinytmux_fs_tier_info(t, &ti);
        const char* st = ti.mounted ? "ACTIVE" : (ti.detected ? "DETECTED" : "ABSENT");
        char cap[40];
        if (ti.total_kb >= 1024UL) {
            sprintf(cap, "%lu/%lu MB", static_cast<unsigned long>(ti.used_kb / 1024UL),
                    static_cast<unsigned long>(ti.total_kb / 1024UL));
        } else {
            sprintf(cap, "%lu/%lu KB", static_cast<unsigned long>(ti.used_kb),
                    static_cast<unsigned long>(ti.total_kb));
        }
        const char* logs = (t == TMUX_STORAGE_SD)
                               ? "Logs: ALLOWED"
                               : (node_ && node_->config().allow_flash_logging ? "Logs: ALLOWED (flash)"
                                                                               : "Logs: BLOCKED (SD)");
        sprintf(buf, "\033[%d;2H%s %d. %-28.28s %-9s %-14s %-20s\033[0m",
                5 + t, (t - 1 == storage_sel_) ? sgr_sel() : "",
                t, ti.label, st, cap, logs);
        write_str(buf);
    }
    sprintf(buf, "\033[10;3HActive tier: %d (%s)", active, tinytmux_fs_status_label());
    write_str(buf);
    sprintf(buf, "\033[22;1H%s Up/Dn:Select  F:Partition/Format (ERASES ALL DATA)  R:Rescan  Esc:Back    \033[0m",
            sgr_menubar());
    write_str(buf);
    sprintf(buf, "\033[24;1H%s %-78.78s \033[0m",
            sgr_warn(),
            dialog_fb_[0] ? dialog_fb_ : "Select a tier and press F to partition/format (ERASES ALL DATA).");
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::handle_storage_key(int key) {
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == TMUX_KEY_UP) {
        storage_sel_ = (storage_sel_ + 2) % 3;
        render();
    } else if (key == TMUX_KEY_DOWN) {
        storage_sel_ = (storage_sel_ + 1) % 3;
        render();
    } else if (key == TMUX_KEY_HOME) {
        storage_sel_ = 0;
        render();
    } else if (key == TMUX_KEY_END) {
        storage_sel_ = 2;
        render();
    } else if (key == 'r' || key == 'R') {
        int ok = tinytmux_fs_rescan();
        node_->set_storage_is_flash(tinytmux_fs_is_flash_storage());
        sprintf(dialog_fb_, "Rescan complete: %s.", ok ? tinytmux_fs_status_label() : "NO STORAGE");
        render();
    } else if (key == 'f' || key == 'F') {
        begin_prompt(PROMPT_CONFIRM_FORMAT, "Confirm Format", "");
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_bootloader() {
    char buf[160];
    int active = node_ ? node_->total_active_connections() : 0;
    int disc_left = node_ ? node_->disconnecting_count() : 0;
    int auto_cnt = node_ ? node_->auto_learned_peer_count() : 0;
    write_str("\033[H\033[2J");
    sprintf(buf, "%s ENTER TEENSY 4.1 HALFKAY BOOTLOADER (FIRMWARE UPGRADE)       Esc:Cancel \033[0m",
            sgr_header());
    write_str(buf);
    draw_dec_box(3, 6, 15, 68, "Confirm Bootloader Entry", 1);

    if (!bootloader_pending_) {
        if (auto_cnt > 0) {
            sprintf(buf, "\033[5;8H%d auto-learned peer(s) were added this session.", auto_cnt);
            write_str(buf);
            write_str("\033[7;8HSave them to config, discard them, or review in Peers before reboot?");
            write_str("\033[9;8H[S] Save & Bootloader        [D] Discard & Bootloader");
            write_str("\033[11;8H[R] Review Peers             [N / Esc] Cancel");
        } else {
            if (active > 0) {
                sprintf(buf, "\033[5;8HEnter HalfKay bootloader? (%d active connection(s))", active);
                write_str(buf);
                write_str("\033[7;8HRemote users will be notified that the node is going offline for");
                write_str("\033[8;8Ha firmware upgrade and gracefully disconnected before rebooting.");
            } else {
                write_str("\033[5;8HEnter HalfKay bootloader? (no active connections)");
            }
            sprintf(buf, "\033[10;8H%s WARNING: Node will stay OFFLINE in HalfKay bootloader mode \033[0m",
                    sgr_sel());
            write_str(buf);
            write_str("\033[11;8Huntil you flash new firmware or power-cycle the Teensy 4.1 board.");
            write_str("\033[14;14H[Y] Enter HalfKay Bootloader     [N / Esc] Cancel");
        }
    } else {
        oyk_u32 elapsed = (now_ms_ >= bootloader_start_ms_) ? (now_ms_ - bootloader_start_ms_) : 0UL;
        sprintf(buf, "\033[6;8HNotifying users & draining AX.25 disconnects (%d left, %lu/3000ms)...",
                disc_left, static_cast<unsigned long>(elapsed));
        write_str(buf);
    }
}

static oyk_u8 tmux_cycle_lpol(oyk_u8 cur, int dir) {
    return static_cast<oyk_u8>((static_cast<int>(cur) + dir + 5) % 5);
}

int TinyTmuxConsole::wizard_step_field_count() const {
    if (wiz_step_ == 1) return 2;
    if (wiz_step_ == 2) {
        if (wiz_drv_ == 0) return 8;
        if (wiz_drv_ == 1 || wiz_drv_ == 4) return 7;
        if (wiz_drv_ == 6) return 3;
        return 6;
    }
    return 0;
}

int TinyTmuxConsole::edit_iface_field_count() const {
    if (wiz_drv_ == 0) return 11;
    if (wiz_drv_ == 1 || wiz_drv_ == 4) return 10;
    if (wiz_drv_ == 6) return 6;
    return 9;
}

OYK_FLASHMEM void TinyTmuxConsole::open_add_iface_wizard() {
    if (draft_.iface_count >= OYK_MAX_INTERFACES) {
        strcpy(dialog_fb_, "Maximum interface slots reached.");
        render();
        return;
    }
    prompt_kind_ = PROMPT_SETTINGS_ADD_WIZARD;
    wiz_edit_idx_ = -1;
    wiz_step_ = 0;
    wiz_drv_ = 0;
    wiz_last_drv_ = -1;
    wiz_focus_ = 0;
    wiz_btn_ = 1;
    wiz_err_[0] = '\0';
    wiz_id_[0] = '\0';
    wiz_desc_[0] = '\0';
    wiz_ep_[0] = '\0';
    wiz_port_[0] = '\0';
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_flags_ = 0x03U;
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::open_edit_iface_dialog(int edit_idx) {
    if (edit_idx < 0 || edit_idx >= draft_.iface_count) return;
    prompt_kind_ = PROMPT_SETTINGS_EDIT_IFACE;
    wiz_edit_idx_ = edit_idx;
    wiz_focus_ = 0;
    wiz_btn_ = 0; /* Save */
    wiz_err_[0] = '\0';

    const OykIfaceConfig& ic = draft_.ifaces[edit_idx];
    wiz_drv_ = 0;
    for (int i = 0; i < 7; ++i) {
        if (strieq_ci(ic.type, TINYTMUX_DRIVERS[i].type)) {
            wiz_drv_ = i;
            break;
        }
    }
    wiz_last_drv_ = wiz_drv_;
    con_str_copy(wiz_id_, sizeof(wiz_id_), ic.id);
    con_str_copy(wiz_desc_, sizeof(wiz_desc_), ic.description);
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_flags_ = static_cast<oyk_u8>(
        (ic.block_inbound ? 0U : 1U) |
        ((wiz_drv_ == 6 || ic.block_outbound) ? 0U : 2U) |
        ((static_cast<oyk_u8>(ic.learned_peer_policy) & 0x07U) << 2) |
        ((wiz_drv_ != 6 && ic.allow_same_interface) ? 0x20U : 0U));
    if (strieq_ci(ic.type, "ppp") || strieq_ci(ic.type, "kiss-serial") ||
        strieq_ci(ic.type, "usb-cdc-serial")) {
        con_str_copy(wiz_ep_, sizeof(wiz_ep_), ic.device[0] ? ic.device : "ser0");
        sprintf(wiz_port_, "%lu",
                static_cast<unsigned long>(ic.baud ? ic.baud : (strieq_ci(ic.type, "kiss-serial") ? 9600UL : 115200UL)));
    } else {
        con_str_copy(wiz_ep_, sizeof(wiz_ep_), ic.host[0] ? ic.host : "0.0.0.0");
        sprintf(wiz_port_, "%u", static_cast<unsigned int>(ic.port));
        con_str_copy(wiz_mask_, sizeof(wiz_mask_), ic.netmask);
        if (ic.peer_count > 0 && (wiz_drv_ == 0 || wiz_drv_ == 1)) {
            oyk_format_peer_spec(&ic.peers[0], wiz_peer_, sizeof(wiz_peer_));
        }
    }
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::wizard_apply_driver_defaults() {
    const char* t = TINYTMUX_DRIVERS[wiz_drv_].type;
    if (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD) {
        oyk_derive_default_iface_id(&draft_, t, wiz_id_, sizeof(wiz_id_));
        wiz_desc_[0] = '\0';
    }
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_flags_ = static_cast<oyk_u8>(1U | ((wiz_drv_ == 6) ? 0U : 2U));
    if (strieq_ci(t, "usb-cdc-net")) {
        strcpy(wiz_ep_, "192.168.7.2");
        strcpy(wiz_port_, "10093");
    } else if (strieq_ci(t, "usb-cdc-serial")) {
        strcpy(wiz_ep_, "usbh0");
        strcpy(wiz_port_, "115200");
    } else if (strieq_ci(t, "ppp")) {
        strcpy(wiz_ep_, "ser0");
        strcpy(wiz_port_, "115200");
    } else if (strieq_ci(t, "kiss-serial")) {
        strcpy(wiz_ep_, "ser0");
        strcpy(wiz_port_, "9600");
    } else if (strieq_ci(t, "telnet")) {
        strcpy(wiz_ep_, "0.0.0.0");
        strcpy(wiz_port_, "8023");
    } else {
        strcpy(wiz_ep_, "0.0.0.0");
        strcpy(wiz_port_, strieq_ci(t, "axudp") ? "10093" : "0");
    }
    wiz_last_drv_ = wiz_drv_;
}

OYK_FLASHMEM void TinyTmuxConsole::commit_iface_fields(int slot) {
    if (slot < 0 || slot >= OYK_MAX_INTERFACES) return;
    int is_new = (slot >= draft_.iface_count) ? 1 : 0;
    if (is_new && draft_.iface_count >= OYK_MAX_INTERFACES) {
        strcpy(dialog_fb_, "Maximum interface slots reached.");
        return;
    }
    OykIfaceConfig old_ic = draft_.ifaces[slot];
    OykIfaceConfig& ic = draft_.ifaces[slot];
    memset(&ic, 0, sizeof(ic));
    con_str_copy(ic.id, sizeof(ic.id), wiz_id_);
    con_str_copy(ic.type, sizeof(ic.type), TINYTMUX_DRIVERS[wiz_drv_].type);
    con_str_copy(ic.description, sizeof(ic.description), wiz_desc_);
    ic.block_inbound = (wiz_flags_ & 1U) ? 0 : 1;
    ic.block_outbound = (wiz_drv_ == 6 || (wiz_flags_ & 2U) == 0U) ? 1 : 0;
    ic.allow_same_interface = (wiz_drv_ != 6 && (wiz_flags_ & 0x20U) != 0U) ? 1 : 0;
    ic.learned_peer_policy = (wiz_drv_ == 6) ? OYK_LEARN_INHERIT : static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    if (strieq_ci(ic.type, "usb-cdc-net")) {
        strcpy(ic.device, "usbh0");
        con_str_copy(ic.host, sizeof(ic.host), wiz_ep_[0] ? wiz_ep_ : "192.168.7.2");
        if (wiz_mask_[0]) con_str_copy(ic.netmask, sizeof(ic.netmask), wiz_mask_);
        long pv = strtol(wiz_port_, NULL, 10);
        ic.port = (pv > 0 && pv <= 65535L) ? static_cast<oyk_u16>(pv) : 10093U;
        ic.baud = 115200UL;
    } else if (strieq_ci(ic.type, "usb-cdc-serial")) {
        con_str_copy(ic.device, sizeof(ic.device), wiz_ep_[0] ? wiz_ep_ : "usbh0");
        strcpy(ic.host, "192.168.7.2");
        long bv = strtol(wiz_port_, NULL, 10);
        ic.baud = (bv >= 300L) ? static_cast<oyk_u32>(bv) : 115200UL;
        ic.port = 10093U;
    } else if (strieq_ci(ic.type, "ppp") || strieq_ci(ic.type, "kiss-serial")) {
        con_str_copy(ic.device, sizeof(ic.device), wiz_ep_[0] ? wiz_ep_ : "ser0");
        long bv = strtol(wiz_port_, NULL, 10);
        ic.baud = (bv >= 300L) ? static_cast<oyk_u32>(bv)
                               : (strieq_ci(ic.type, "ppp") ? 115200UL : 9600UL);
        if (strieq_ci(ic.type, "ppp")) ic.port = 10093U;
    } else {
        con_str_copy(ic.host, sizeof(ic.host), wiz_ep_[0] ? wiz_ep_ : "0.0.0.0");
        if ((wiz_drv_ == 0 || wiz_drv_ == 1) && wiz_mask_[0]) {
            con_str_copy(ic.netmask, sizeof(ic.netmask), wiz_mask_);
        }
        long pv = strtol(wiz_port_, NULL, 10);
        if (strieq_ci(ic.type, "telnet")) {
            ic.port = (pv > 0 && pv <= 65535L) ? static_cast<oyk_u16>(pv) : 8023U;
        } else if (strieq_ci(ic.type, "axudp")) {
            ic.port = (pv > 0 && pv <= 65535L) ? static_cast<oyk_u16>(pv) : 10093U;
        }
        if (strieq_ci(ic.type, "axudp") || strieq_ci(ic.type, "axip")) {
            if (!is_new && old_ic.peer_count > 1 &&
                (strieq_ci(old_ic.type, "axudp") || strieq_ci(old_ic.type, "axip"))) {
                for (int p = 1; p < old_ic.peer_count && ic.peer_count < OYK_MAX_PEERS; ++p) {
                    ic.peers[ic.peer_count++] = old_ic.peers[p];
                }
            }
            if (wiz_peer_[0]) {
                OykPeerEntry pe;
                memset(&pe, 0, sizeof(pe));
                if (oyk_parse_peer_spec(wiz_peer_, pe.callsign, sizeof(pe.callsign),
                                        &pe.ip, &pe.port, ic.port ? ic.port : 10093U)) {
                    if (ic.peer_count > 0 && !is_new) {
                        for (int p = (ic.peer_count < OYK_MAX_PEERS ? ic.peer_count : OYK_MAX_PEERS - 1); p > 0; --p) {
                            ic.peers[p] = ic.peers[p - 1];
                        }
                        ic.peers[0] = pe;
                        if (ic.peer_count < OYK_MAX_PEERS) ic.peer_count++;
                    } else {
                        ic.peers[0] = pe;
                        ic.peer_count = 1;
                    }
                }
            }
        }
    }
    settings_sel_[3] = slot;
    if (is_new) {
        draft_.iface_count++;
        sprintf(dialog_fb_, "Added %s (%s) to draft (press S/F2 to save).", ic.id, ic.type);
    } else {
        sprintf(dialog_fb_, "Updated %s (%s) in draft (press S/F2 to save).", ic.id, ic.type);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_add_iface_wizard() {
    static const char* PROG[4] = {
        "[##......] 1/4", "[####....] 2/4", "[######..] 3/4", "[########] 4/4"
    };
    char buf[192];
    const int top = 3, left = 6, h = 19, w = 68;
    draw_dec_box(top, left, h, w, "Add Interface", 1);

    /* Top-right progress indicator on caption border */
    sprintf(buf, "\033[%d;%dH%s %s \033[0m", top, left + w - 18, sgr_title(), PROG[wiz_step_]);
    write_str(buf);

    const char* drv = TINYTMUX_DRIVERS[wiz_drv_].type;
    const char* prompt_txt = "Select the driver for the new interface:";
    if (wiz_step_ == 1) {
        prompt_txt = "Enter a unique interface ID and optional description:";
    } else if (wiz_step_ == 2) {
        if (wiz_drv_ == 0) prompt_txt = "Configure AXUDP bind host/DMR, port, netmask, and policy:";
        else if (wiz_drv_ == 1) prompt_txt = "Configure AXIP bind host/DMR, netmask, and policy:";
        else if (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) prompt_txt = "Configure serial UART/USB device, baud rate, and policy:";
        else if (wiz_drv_ == 4) prompt_txt = "Configure USB-CDC host/DMR, port, netmask, and policy:";
        else prompt_txt = "Configure Telnet bind host IP, TCP port, and policy:";
    } else if (wiz_step_ == 3) {
        prompt_txt = "Review interface configuration before committing:";
    }
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 1, left + 2, sgr_title(), prompt_txt);
    write_str(buf);

    const int ain = (wiz_flags_ & 1U) ? 1 : 0;
    const int aout = (wiz_drv_ != 6 && (wiz_flags_ & 2U)) ? 1 : 0;
    const oyk_u8 lpol = static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    if (wiz_step_ == 0) {
        sprintf(buf, "\033[%d;%dH%s %-2s %-15s %-44s \033[0m",
                top + 2, left + 1, sgr_list_hdr(), "#", "Driver", "Description");
        write_str(buf);
        for (int i = 0; i < 7; ++i) {
            sprintf(buf, "\033[%d;%dH%s %d. %-15.15s %-44.44s \033[0m",
                    top + 3 + i, left + 1, (i == wiz_drv_) ? sgr_sel() : "",
                    i + 1, TINYTMUX_DRIVERS[i].type, TINYTMUX_DRIVERS[i].desc);
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
        const char* ep_lbl = (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                 ? "Serial Device  "
                                 : (wiz_drv_ == 4 ? "Host / DMR ID  " : "Bind Host/DMR  ");
        sprintf(buf, "\033[%d;%dHInterface    : %s (%s)", top + 2, left + 3, wiz_id_, drv);
        write_str(buf);
        int r = top + 4;
        int fi = 0;
        sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                r, left + 3, ep_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_ep_);
        write_str(buf);
        ++r; ++fi;
        if (wiz_drv_ != 1) {
            const char* p_lbl = (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                    ? "Baud Rate      "
                                    : (wiz_drv_ == 6 ? "TCP Port       " : "UDP Port       ");
            sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                    r, left + 3, p_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_port_);
            write_str(buf);
            ++r; ++fi;
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) {
            sprintf(buf, "\033[%d;%dHNetmask (opt)  : %s %-36.36s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_mask_);
            write_str(buf);
            ++r; ++fi;
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1) {
            sprintf(buf, "\033[%d;%dHPeer (opt)     : %s %-36.36s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_peer_);
            write_str(buf);
            ++r; ++fi;
        }
        sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-12.12s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", ain ? "[ON]" : "[OFF]");
        write_str(buf);
        ++r; ++fi;
        if (wiz_drv_ == 6) {
            sprintf(buf, "\033[%d;%dHAllow Outbound : [OFF] (disabled for telnet)", r, left + 3);
            write_str(buf);
            ++r;
            sprintf(buf, "\033[%d;%dHSame-Iface Fwd : [OFF] (disabled for telnet)", r, left + 3);
            write_str(buf);
        } else {
            const int asame = (wiz_flags_ & 0x20U) ? 1 : 0;
            sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-12.12s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", aout ? "[ON]" : "[OFF]");
            write_str(buf);
            ++r; ++fi;
            sprintf(buf, "\033[%d;%dHSame-Iface Fwd : %s %-12.12s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", asame ? "[ON]" : "[OFF]");
            write_str(buf);
            ++r; ++fi;
            sprintf(buf, "\033[%d;%dHLearned Policy : %s < %s > \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", oyk_format_learned_policy(lpol));
            write_str(buf);
        }
    } else {
        sprintf(buf, "\033[%d;%dHDriver       : %-46.46s", top + 3, left + 3, drv);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHInterface ID : %-46.46s", top + 4, left + 3, wiz_id_);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHDescription  : %-46.46s", top + 5, left + 3,
                wiz_desc_[0] ? wiz_desc_ : "(none)");
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%s: %-46.46s", top + 6, left + 3,
                (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) ? "Device       " : "Host / DMR   ",
                wiz_ep_);
        write_str(buf);
        if (wiz_drv_ != 1) {
            sprintf(buf, "\033[%d;%dH%s: %-46.46s", top + 7, left + 3,
                    (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) ? "Baud Rate    " : "Port         ",
                    wiz_port_);
            write_str(buf);
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) {
            sprintf(buf, "\033[%d;%dHNetmask      : %-46.46s", top + 8, left + 3,
                    wiz_mask_[0] ? wiz_mask_ : "(any)");
            write_str(buf);
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1) {
            sprintf(buf, "\033[%d;%dHStatic Peer  : %-46.46s", top + 9, left + 3,
                    wiz_peer_[0] ? wiz_peer_ : "(auto-learn)");
            write_str(buf);
        }
        sprintf(buf, "\033[%d;%dHPolicy       : In=%s  Out=%s  SameIf=%s  Learn=%s",
                top + 10, left + 3,
                ain ? "ON" : "OFF",
                (wiz_drv_ == 6) ? "OFF" : (aout ? "ON" : "OFF"),
                (wiz_drv_ == 6) ? "OFF" : ((wiz_flags_ & 0x20U) ? "ON" : "OFF"),
                (wiz_drv_ == 6) ? "n/a" : oyk_format_learned_policy(lpol));
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%sPress Finish to add this interface to your Settings draft.\033[0m",
                top + 12, left + 3, sgr_title());
        write_str(buf);
    }

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 15, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }

    draw_dec_hdiv(top + 16, left, w);

    int fcnt = wizard_step_field_count();
    int on_bar = (wiz_step_ == 0 || wiz_step_ == 3 || wiz_focus_ == fcnt);
    const char* next_lbl = (wiz_step_ == 3) ? "[ Finish ]" : "[ Next > ]";
    sprintf(buf, "\033[%d;%dH%s %s \033[0m  %s %s \033[0m  %s [ Cancel ] \033[0m  %s",
            top + 17, left + 2,
            (wiz_step_ == 0) ? "" : ((on_bar && wiz_btn_ == 0) ? sgr_sel() : sgr_menubar()),
            (wiz_step_ == 0) ? "          " : "[ < Back ]",
            (on_bar && wiz_btn_ == 1) ? sgr_sel() : sgr_menubar(),
            next_lbl,
            (on_bar && wiz_btn_ == 2) ? sgr_sel() : sgr_menubar(),
            (wiz_step_ > 0) ? "Ctrl+B:Back" : "1-7:Pick");
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::render_edit_iface_dialog() {
    char buf[192];
    const int top = 3, left = 6, h = 19, w = 68;
    draw_dec_box(top, left, h, w, "Edit Interface", 1);

    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m",
            top + 1, left + 2, sgr_title(), "Edit interface settings and select Save to update draft:");
    write_str(buf);

    const int ain = (wiz_flags_ & 1U) ? 1 : 0;
    const int aout = (wiz_drv_ != 6 && (wiz_flags_ & 2U)) ? 1 : 0;
    const oyk_u8 lpol = static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    char drv_lbl[32];
    sprintf(drv_lbl, "< %s >", TINYTMUX_DRIVERS[wiz_drv_].type);
    sprintf(buf, "\033[%d;%dHDriver (Lt/Rt) : %s %-36.36s \033[0m",
            top + 3, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", drv_lbl);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface ID   : %s %-36.36s \033[0m",
            top + 4, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHDescription    : %s %-36.36s \033[0m",
            top + 5, left + 3, (wiz_focus_ == 2) ? sgr_menubar() : "", wiz_desc_);
    write_str(buf);

    const int is_ser = (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) ? 1 : 0;
    const char* ep_lbl = is_ser ? "Serial Device  "
                                : (wiz_drv_ == 4 ? "Host / DMR ID  " : "Bind Host/DMR  ");
    sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
            top + 6, left + 3, ep_lbl, (wiz_focus_ == 3) ? sgr_menubar() : "", wiz_ep_);
    write_str(buf);

    int r = top + 7;
    int fi = 4;
    if (wiz_drv_ != 1) {
        const char* p_lbl = is_ser ? "Baud Rate      "
                                   : (wiz_drv_ == 6 ? "TCP Port       " : "UDP Port       ");
        sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                r, left + 3, p_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_port_);
        write_str(buf);
        ++r; ++fi;
    }
    if (wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) {
        sprintf(buf, "\033[%d;%dHNetmask (opt)  : %s %-36.36s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_mask_);
        write_str(buf);
        ++r; ++fi;
    }
    if (wiz_drv_ == 0 || wiz_drv_ == 1) {
        sprintf(buf, "\033[%d;%dHStatic Peer    : %s %-36.36s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_peer_);
        write_str(buf);
        ++r; ++fi;
    }
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-12.12s \033[0m",
            r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", ain ? "[ON]" : "[OFF]");
    write_str(buf);
    ++r; ++fi;
    if (wiz_drv_ == 6) {
        sprintf(buf, "\033[%d;%dHAllow Outbound : [OFF] (disabled for telnet)", r, left + 3);
        write_str(buf);
        ++r;
        sprintf(buf, "\033[%d;%dHSame-Iface Fwd : [OFF] (disabled for telnet)", r, left + 3);
        write_str(buf);
    } else {
        const int asame = (wiz_flags_ & 0x20U) ? 1 : 0;
        sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-12.12s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", aout ? "[ON]" : "[OFF]");
        write_str(buf);
        ++r; ++fi;
        sprintf(buf, "\033[%d;%dHSame-Iface Fwd : %s %-12.12s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", asame ? "[ON]" : "[OFF]");
        write_str(buf);
        ++r; ++fi;
        sprintf(buf, "\033[%d;%dHLearned Policy : %s < %s > \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", oyk_format_learned_policy(lpol));
        write_str(buf);
    }

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 15, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }

    draw_dec_hdiv(top + 16, left, w);

    int fcnt = edit_iface_field_count();
    int on_bar = (wiz_focus_ == fcnt);
    sprintf(buf, "\033[%d;%dH%s [  Save  ] \033[0m  %s [ Cancel ] \033[0m  Up/Dn:Field Enter:Save",
            top + 17, left + 2,
            (on_bar && wiz_btn_ == 0) ? sgr_sel() : sgr_menubar(),
            (on_bar && wiz_btn_ == 1) ? sgr_sel() : sgr_menubar());
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::render_confirm_format_modal() {
    static const char* NAMES[3] = {
        "microSD Card (SDIO)", "QSPI Flash (FlexSPI2 CS1)", "Internal 4 MB Flash (LittleFS)"
    };
    const int top = 6, left = 6, h = 12, w = 68;
    const char* target = (storage_sel_ >= 0 && storage_sel_ < 3) ? NAMES[storage_sel_] : "Storage";
    char buf[160];
    draw_dec_box(top, left, h, w, "CONFIRM DESTRUCTIVE FORMAT", 1);
    sprintf(buf, "\033[%d;%dHTarget : %-56.56s", top + 2, left + 2, target);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s WARNING: Partitioning/formatting ERASES ALL DATA on this tier, \033[0m",
            top + 4, left + 2, sgr_sel());
    write_str(buf);
    sprintf(buf, "\033[%d;%dHincluding configs, mail, files, and logs!", top + 5, left + 2);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHType FORMAT (uppercase) and press Enter to confirm:", top + 7, left + 2);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s > %-60.60s \033[0m", top + 8, left + 2, sgr_menubar(), prompt_buf_);
    write_str(buf);
    draw_dec_hdiv(top + 9, left, w);
    sprintf(buf, "\033[%d;%dH%s [Enter] Confirm Format                 [Esc] Cancel          \033[0m",
            top + 10, left + 1, sgr_menubar());
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::open_edit_peer_dialog(int edit_idx) {
    prompt_kind_ = PROMPT_SETTINGS_EDIT_PEER;
    wiz_edit_idx_ = edit_idx;
    wiz_focus_ = (edit_idx >= 0) ? 0 : 1;
    wiz_btn_ = 0; /* Save */
    wiz_err_[0] = '\0';
    if (edit_idx >= 0 && edit_idx < draft_.peer_count) {
        const OykPeerConfig& pc = draft_.peers[edit_idx];
        con_str_copy(wiz_id_, sizeof(wiz_id_), pc.id);
        con_str_copy(ban_call_tmp_, sizeof(ban_call_tmp_), pc.callsign);
        con_str_copy(wiz_desc_, sizeof(wiz_desc_), pc.description);
        con_str_copy(ban_action_tmp_, sizeof(ban_action_tmp_), pc.interface_id);
        con_str_copy(wiz_ep_, sizeof(wiz_ep_), pc.host);
        if (pc.port > 0) sprintf(wiz_port_, "%u", static_cast<unsigned int>(pc.port));
        else wiz_port_[0] = '\0';
        wiz_step_ = (pc.allow_inbound ? 1 : 0) | (pc.allow_outbound ? 2 : 0);
        wiz_last_drv_ = 1; /* id_custom */
    } else {
        wiz_id_[0] = '\0';
        ban_call_tmp_[0] = '\0';
        wiz_desc_[0] = '\0';
        ban_action_tmp_[0] = '\0';
        for (int i = 0; i < draft_.iface_count; ++i) {
            if (!strieq_ci(draft_.ifaces[i].type, "telnet") &&
                !strieq_ci(draft_.ifaces[i].type, "local")) {
                con_str_copy(ban_action_tmp_, sizeof(ban_action_tmp_), draft_.ifaces[i].id);
                break;
            }
        }
        wiz_ep_[0] = '\0';
        wiz_port_[0] = '\0';
        wiz_step_ = 3; /* allow_in | allow_out */
        wiz_last_drv_ = 0;
    }
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::render_edit_peer_dialog() {
    char buf[192];
    const int top = 4, left = 6, h = 16, w = 68;
    draw_dec_box(top, left, h, w, (wiz_edit_idx_ >= 0) ? "Edit Peer" : "Add Peer", 1);

    char if_lbl[32], in_lbl[8], out_lbl[8];
    sprintf(if_lbl, "< %s >", ban_action_tmp_[0] ? ban_action_tmp_ : "(any)");
    strcpy(in_lbl, (wiz_step_ & 1) ? "[ON]" : "[OFF]");
    strcpy(out_lbl, (wiz_step_ & 2) ? "[ON]" : "[OFF]");

    sprintf(buf, "\033[%d;%dHPeer ID        : %s %-36.36s \033[0m", top + 2, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHCallsign       : %s %-36.36s \033[0m", top + 3, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", ban_call_tmp_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHDescription    : %s %-36.36s \033[0m", top + 4, left + 3, (wiz_focus_ == 2) ? sgr_menubar() : "", wiz_desc_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface      : %s %-36.36s \033[0m", top + 5, left + 3, (wiz_focus_ == 3) ? sgr_menubar() : "", if_lbl);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHHost / DMR ID  : %s %-36.36s \033[0m", top + 6, left + 3, (wiz_focus_ == 4) ? sgr_menubar() : "", wiz_ep_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHPort (0=def)   : %s %-36.36s \033[0m", top + 7, left + 3, (wiz_focus_ == 5) ? sgr_menubar() : "", wiz_port_[0] ? wiz_port_ : "(default)");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-10.10s \033[0m", top + 8, left + 3, (wiz_focus_ == 6) ? sgr_menubar() : "", in_lbl);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-10.10s \033[0m", top + 9, left + 3, (wiz_focus_ == 7) ? sgr_menubar() : "", out_lbl);
    write_str(buf);

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 11, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }

    draw_dec_hdiv(top + 13, left, w);
    int on_bar = (wiz_focus_ == 8);
    sprintf(buf, "\033[%d;%dH%s [  Save  ] \033[0m  %s [ Cancel ] \033[0m  Up/Dn:Field Lt/Rt/Spc:Toggle",
            top + 14, left + 2,
            (on_bar && wiz_btn_ == 0) ? sgr_sel() : sgr_menubar(),
            (on_bar && wiz_btn_ == 1) ? sgr_sel() : sgr_menubar());
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::commit_peer_fields(int slot) {
    if (slot < 0 || slot >= OYK_MAX_CONFIG_PEERS) return;
    int is_new = (slot >= draft_.peer_count) ? 1 : 0;
    if (is_new && draft_.peer_count >= OYK_MAX_CONFIG_PEERS) {
        strcpy(dialog_fb_, "Maximum peer slots reached.");
        return;
    }
    OykCallsign cs;
    oyk_parse_callsign(ban_call_tmp_, &cs);
    char cfmt[16];
    oyk_format_callsign(&cs, cfmt, sizeof(cfmt));
    OykPeerConfig& pc = draft_.peers[slot];
    memset(&pc, 0, sizeof(pc));
    con_str_copy(pc.id, sizeof(pc.id), wiz_id_);
    con_str_copy(pc.callsign, sizeof(pc.callsign), cfmt);
    con_str_copy(pc.description, sizeof(pc.description), wiz_desc_);
    con_str_copy(pc.interface_id, sizeof(pc.interface_id), ban_action_tmp_);
    con_str_copy(pc.host, sizeof(pc.host), wiz_ep_);
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

OYK_FLASHMEM int TinyTmuxConsole::edit_peer_try_save() {
    wiz_err_[0] = '\0';
    OykCallsign cs;
    if (!oyk_parse_callsign(ban_call_tmp_, &cs)) {
        strcpy(wiz_err_, "Invalid AX.25 callsign.");
        wiz_focus_ = 1;
        return 0;
    }
    char cfmt[16];
    oyk_format_callsign(&cs, cfmt, sizeof(cfmt));
    if (!wiz_id_[0]) {
        oyk_derive_default_peer_id(&draft_, cfmt, wiz_id_, sizeof(wiz_id_));
    }
    for (int i = 0; i < draft_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        if (i == wiz_edit_idx_) continue;
        if (strieq_ci(draft_.peers[i].id, wiz_id_)) {
            sprintf(wiz_err_, "Duplicate peer ID: %.24s", wiz_id_);
            wiz_focus_ = 0;
            return 0;
        }
    }
    if (wiz_port_[0]) {
        long p = strtol(wiz_port_, NULL, 10);
        if (p < 0L || p > 65535L) {
            strcpy(wiz_err_, "Port must be 0..65535.");
            wiz_focus_ = 5;
            return 0;
        }
    }
    if (wiz_ep_[0] && oyk_is_all_digits(wiz_ep_)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(wiz_ep_, &did)) {
            strcpy(wiz_err_, "Invalid DMR ID (allowed 1..16777214).");
            wiz_focus_ = 4;
            return 0;
        }
    }
    int slot = (wiz_edit_idx_ >= 0) ? wiz_edit_idx_ : draft_.peer_count;
    commit_peer_fields(slot);
    prompt_kind_ = PROMPT_NONE;
    return 1;
}

OYK_FLASHMEM void TinyTmuxConsole::handle_edit_peer_key(int key) {
    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        strcpy(dialog_fb_, "Edit peer cancelled.");
        render();
        return;
    }
    if (key == TMUX_KEY_UP) {
        wiz_focus_ = (wiz_focus_ + 8) % 9;
        render();
        return;
    }
    if (key == TMUX_KEY_DOWN || key == '\t') {
        wiz_focus_ = (wiz_focus_ + 1) % 9;
        render();
        return;
    }
    if (wiz_focus_ == 8) {
        if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
            wiz_btn_ = 1 - wiz_btn_;
        } else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 1) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Edit peer cancelled.");
            } else {
                edit_peer_try_save();
            }
        }
        render();
        return;
    }
    if (wiz_focus_ == 3) {
        if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT || key == ' ') {
            const char* opts[OYK_MAX_INTERFACES];
            int oc = 0;
            for (int i = 0; i < draft_.iface_count && oc < OYK_MAX_INTERFACES; ++i) {
                if (!strieq_ci(draft_.ifaces[i].type, "telnet") &&
                    !strieq_ci(draft_.ifaces[i].type, "local")) {
                    opts[oc++] = draft_.ifaces[i].id;
                }
            }
            if (oc > 0) {
                int idx = 0;
                for (int i = 0; i < oc; ++i) {
                    if (strieq_ci(opts[i], ban_action_tmp_)) { idx = i; break; }
                }
                idx = (key == TMUX_KEY_LEFT) ? (idx + oc - 1) % oc : (idx + 1) % oc;
                con_str_copy(ban_action_tmp_, sizeof(ban_action_tmp_), opts[idx]);
            }
            render();
        } else if (key == '\r' || key == '\n') {
            edit_peer_try_save();
            render();
        }
        return;
    }
    if (wiz_focus_ == 6 || wiz_focus_ == 7) {
        if (key == ' ' || key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
            wiz_step_ ^= (wiz_focus_ == 6) ? 1 : 2;
            render();
        } else if (key == '\r' || key == '\n') {
            edit_peer_try_save();
            render();
        }
        return;
    }
    char* target = NULL;
    size_t cap = 0;
    if (wiz_focus_ == 0) { target = wiz_id_; cap = sizeof(wiz_id_); }
    else if (wiz_focus_ == 1) { target = ban_call_tmp_; cap = sizeof(ban_call_tmp_); }
    else if (wiz_focus_ == 2) { target = wiz_desc_; cap = sizeof(wiz_desc_); }
    else if (wiz_focus_ == 4) { target = wiz_ep_; cap = sizeof(wiz_ep_); }
    else if (wiz_focus_ == 5) { target = wiz_port_; cap = sizeof(wiz_port_); }

    if (key == '\r' || key == '\n') {
        edit_peer_try_save();
        render();
    } else if (target && (key == 8 || key == 127)) {
        size_t n = strlen(target);
        if (n > 0) target[n - 1] = '\0';
        if (wiz_focus_ == 0) wiz_last_drv_ = (wiz_id_[0] != '\0');
        if (wiz_focus_ == 1 && !wiz_last_drv_) {
            if (ban_call_tmp_[0]) oyk_derive_default_peer_id(&draft_, ban_call_tmp_, wiz_id_, sizeof(wiz_id_));
            else wiz_id_[0] = '\0';
        }
        wiz_err_[0] = '\0';
        render();
    } else if (target && key >= 0x20 && key < 0x7F) {
        size_t n = strlen(target);
        if (n + 1 < cap) {
            target[n] = static_cast<char>(key);
            target[n + 1] = '\0';
        }
        if (wiz_focus_ == 0) wiz_last_drv_ = 1;
        if (wiz_focus_ == 1 && !wiz_last_drv_) {
            oyk_derive_default_peer_id(&draft_, ban_call_tmp_, wiz_id_, sizeof(wiz_id_));
        }
        wiz_err_[0] = '\0';
        render();
    }
}

void TinyTmuxConsole::render_prompt_overlay() {
    char buf[192];
    const int top = 9, left = 6, h = 6, w = 68;
    draw_dec_box(top, left, h, w, "Input", 1);
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 1, left + 2, sgr_title(), prompt_label_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s > %-60.60s \033[0m", top + 2, left + 2, sgr_menubar(), prompt_buf_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH[Enter] Accept                              [Esc] Cancel", top + 4, left + 2);
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::open_settings(int initial_tab) {
    if (node_) draft_ = node_->config();
    settings_tab_ = (initial_tab >= 0 && initial_tab < 7) ? initial_tab : 0;
    dialog_fb_[0] = '\0';
    view_ = TMUX_VIEW_SETTINGS;
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::save_and_apply_settings() {
    if (!node_) return;
    OykConfigApplyResult res;
    node_->apply_config(&draft_, 1, &res);
    node_->set_storage_is_flash(tinytmux_fs_is_flash_storage());
    if (bridge_) {
        bridge_->rebind_interfaces(now_ms_);
    }
    if (!res.applied) {
        sprintf(dialog_fb_, "Apply failed: %.60s", res.error);
        render();
        return;
    }
    if (res.saved_to_disk) {
        if (res.identity_restarted) {
            sprintf(status_line_, "Settings saved & applied (signed off %d session(s)).",
                    res.signed_off_sessions);
        } else {
            strcpy(status_line_, "Settings saved to disk and applied.");
        }
    } else {
        strcpy(status_line_, "Settings applied IN MEMORY ONLY (config not writable).");
    }
    view_ = TMUX_VIEW_CONSOLE;
    render();
}

void TinyTmuxConsole::begin_prompt(PromptKind kind, const char* label, const char* prefill) {
    prompt_kind_ = kind;
    con_str_copy(prompt_label_, sizeof(prompt_label_), label ? label : "Input:");
    con_str_copy(prompt_buf_, sizeof(prompt_buf_), prefill ? prefill : "");
    prompt_len_ = static_cast<int>(strlen(prompt_buf_));
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::edit_current_settings_field() {
    int idx = settings_sel_[settings_tab_];
    if (settings_tab_ == 0) {
        if (idx == 0) {
            char cs[16]; oyk_format_callsign(&draft_.callsign, cs, sizeof(cs));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Callsign (e.g. N0CALL-7):", cs);
        } else if (idx == 1) {
            char cs[16]; oyk_format_callsign(&draft_.alias, cs, sizeof(cs));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Alias (blank for none):", cs);
        } else if (idx == 2) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Welcome Message:", draft_.welcome_text);
        } else if (idx == 3) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Info Message:", draft_.info_text);
        } else if (idx == 4) {
            draft_.accept_incoming = draft_.accept_incoming ? 0 : 1;
            render();
        } else if (idx == 5) {
            draft_.allow_flash_logging = draft_.allow_flash_logging ? 0 : 1;
            render();
        } else if (idx == 6) {
            if (draft_.learned_peer_policy == OYK_LEARN_NONE) draft_.learned_peer_policy = OYK_LEARN_INBOUND;
            else if (draft_.learned_peer_policy == OYK_LEARN_INBOUND) draft_.learned_peer_policy = OYK_LEARN_OUTBOUND;
            else if (draft_.learned_peer_policy == OYK_LEARN_OUTBOUND) draft_.learned_peer_policy = OYK_LEARN_BOTH;
            else draft_.learned_peer_policy = OYK_LEARN_NONE;
            render();
        } else if (idx == 7) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "New User Welcome:", draft_.new_user_welcome);
        } else if (idx == 8) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Interfaces File (blank=inline):", draft_.interfaces_file);
        } else {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Peers File (blank=inline):", draft_.peers_file);
        }
    } else if (settings_tab_ == 1) {
        if (idx == 0) { draft_.bbs_enabled = draft_.bbs_enabled ? 0 : 1; render(); }
        else if (idx == 1) { char n[8]; sprintf(n, "%u", static_cast<unsigned int>(draft_.bbs_ssid)); begin_prompt(PROMPT_SETTINGS_VALUE, "BBS SSID (0-15):", n); }
        else if (idx == 2) { char cs[16]; oyk_format_callsign(&draft_.bbs_callsign, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Callsign Override (blank=auto):", cs); }
        else if (idx == 3) { char cs[16]; oyk_format_callsign(&draft_.bbs_alias, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Alias (blank=none):", cs); }
        else if (idx == 4) { draft_.bbs_files_enabled = draft_.bbs_files_enabled ? 0 : 1; render(); }
        else if (idx == 5) begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Files Area Root Path:", draft_.bbs_files_path);
        else if (idx == 6) begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Mail Storage Path:", draft_.bbs_mail_path);
        else begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Welcome Message:", draft_.bbs_welcome);
    } else if (settings_tab_ == 2) {
        if (idx == 0) { draft_.chat_enabled = draft_.chat_enabled ? 0 : 1; render(); }
        else if (idx == 1) { char n[8]; sprintf(n, "%u", static_cast<unsigned int>(draft_.chat_ssid)); begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT SSID (0-15):", n); }
        else if (idx == 2) { char cs[16]; oyk_format_callsign(&draft_.chat_callsign, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Callsign Override (blank=auto):", cs); }
        else if (idx == 3) { char cs[16]; oyk_format_callsign(&draft_.chat_alias, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Alias (blank=none):", cs); }
        else if (idx == 4) begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Default Topic:", draft_.chat_topic);
        else begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Welcome Message:", draft_.chat_welcome);
    } else if (settings_tab_ == 3) {
        if (idx >= 0 && idx < draft_.iface_count) {
            open_edit_iface_dialog(idx);
        }
    } else if (settings_tab_ == 4) {
        if (idx >= 0 && idx < draft_.peer_count) {
            open_edit_peer_dialog(idx);
        }
    } else if (settings_tab_ == 5) {
        if (idx == 0) { draft_.rig_enabled = draft_.rig_enabled ? 0 : 1; render(); }
        else if (idx == 1) {
            if (strieq_ci(draft_.rig_protocol, "radiofoundry")) {
                strcpy(draft_.rig_protocol, "rigctl");
                if (draft_.rig_port == 0 || draft_.rig_port == OYK_RF_DEFAULT_PORT) {
                    draft_.rig_port = OYK_RIGCTL_DEFAULT_PORT;
                }
            } else {
                strcpy(draft_.rig_protocol, "radiofoundry");
                if (draft_.rig_port == 0 || draft_.rig_port == OYK_RIGCTL_DEFAULT_PORT) {
                    draft_.rig_port = OYK_RF_DEFAULT_PORT;
                }
            }
            render();
        } else if (idx == 2) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Radio Host / IP:", draft_.rig_host);
        } else if (idx == 3) {
            char p[16]; sprintf(p, "%u", static_cast<unsigned int>(draft_.rig_port));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Rig Port (4532/4191):", p);
        } else if (idx == 4) {
            char p[16]; sprintf(p, "%u", static_cast<unsigned int>(draft_.rig_poll_sec));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Rig Poll Interval (seconds):", p);
        } else {
            draft_.rig_notify = draft_.rig_notify ? 0 : 1;
            render();
        }
    } else {
        if (idx == 0) { draft_.printer_enabled = draft_.printer_enabled ? 0 : 1; render(); }
        else if (idx == 1) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Printer Device (usb-printer / ser0..7):", draft_.printer_device);
        } else if (idx == 2) {
            if (strieq_ci(draft_.printer_protocol, "generic")) strcpy(draft_.printer_protocol, "mx80");
            else if (strieq_ci(draft_.printer_protocol, "mx80")) strcpy(draft_.printer_protocol, "escp2");
            else strcpy(draft_.printer_protocol, "generic");
            render();
        } else if (idx == 3) {
            char p[16]; sprintf(p, "%lu", static_cast<unsigned long>(draft_.printer_baud ? draft_.printer_baud : 9600UL));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Printer Serial Baud Rate:", p);
        } else if (idx == 4) {
            char p[16]; sprintf(p, "0x%lX", static_cast<unsigned long>(draft_.printer_level));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Printer Log Mask (e.g. 0x77 or ALL):", p);
        } else {
            draft_.printer_condensed = draft_.printer_condensed ? 0 : 1;
            render();
        }
    }
}

void TinyTmuxConsole::commit_prompt() {
    PromptKind pk = prompt_kind_;
    char val[128];
    con_str_copy(val, sizeof(val), prompt_buf_);
    prompt_kind_ = PROMPT_NONE;
    prompt_buf_[0] = '\0';
    prompt_len_ = 0;

    if (pk == PROMPT_TERMINATE_SID) {
        oyk_u32 sid = static_cast<oyk_u32>(strtoul(val, NULL, 10));
        char msg[96];
        if (sid > 0 && node_->terminate_session(sid, msg, sizeof(msg))) {
            con_str_copy(status_line_, sizeof(status_line_), msg);
        } else {
            sprintf(status_line_, "Session #%lu not found.", static_cast<unsigned long>(sid));
        }
        render();
        return;
    }

    if (pk == PROMPT_BAN_CALLSIGN) {
        if (!val[0]) { render(); return; }
        con_str_copy(ban_call_tmp_, sizeof(ban_call_tmp_), val);
        begin_prompt(PROMPT_BAN_ACTION, "Action: [B]an, [H]ardban, or [U]nban:", "B");
        return;
    }

    if (pk == PROMPT_BAN_ACTION) {
        con_str_copy(ban_action_tmp_, sizeof(ban_action_tmp_), val);
        if (val[0] == 'u' || val[0] == 'U') {
            char msg[96];
            if (node_->unban_callsign(ban_call_tmp_, msg, sizeof(msg))) {
                con_str_copy(status_line_, sizeof(status_line_), msg);
            } else {
                sprintf(status_line_, "Callsign %s not found in banlist.", ban_call_tmp_);
            }
            render();
            return;
        }
        begin_prompt(PROMPT_BAN_REASON, "Ban reason:", "Sysop ban");
        return;
    }

    if (pk == PROMPT_BAN_REASON) {
        int hard = (ban_action_tmp_[0] == 'h' || ban_action_tmp_[0] == 'H') ? 1 : 0;
        oyk_u32 flags = hard ? OYK_BAN_HARD_BAN : OYK_BAN_APPEAL_ALLOWED;
        char msg[96];
        if (node_->ban_callsign(ban_call_tmp_, flags, val[0] ? val : "Sysop ban",
                                "permanent", msg, sizeof(msg))) {
            con_str_copy(status_line_, sizeof(status_line_), msg);
        } else {
            strcpy(status_line_, "Failed to add ban.");
        }
        render();
        return;
    }

    if (pk == PROMPT_CONFIRM_FORMAT) {
        if (strcmp(val, "FORMAT") == 0) {
            char msg[96];
            int ok = tinytmux_fs_format_tier(storage_sel_ + 1, msg, sizeof(msg));
            node_->set_storage_is_flash(tinytmux_fs_is_flash_storage());
            sprintf(dialog_fb_, "%s%.70s", ok ? "OK: " : "ERROR: ", msg);
        } else {
            strcpy(dialog_fb_, "Format cancelled (confirmation text did not match FORMAT).");
        }
        render();
        return;
    }

    if (pk == PROMPT_SETTINGS_VALUE) {
        int idx = settings_sel_[settings_tab_];
        if (settings_tab_ == 0) {
            if (idx == 0) oyk_parse_callsign(val, &draft_.callsign);
            else if (idx == 1) {
                if (!val[0]) memset(&draft_.alias, 0, sizeof(draft_.alias));
                else oyk_parse_callsign(val, &draft_.alias);
            } else if (idx == 2) con_str_copy(draft_.welcome_text, sizeof(draft_.welcome_text), val);
            else if (idx == 3) con_str_copy(draft_.info_text, sizeof(draft_.info_text), val);
            else if (idx == 7) con_str_copy(draft_.new_user_welcome, sizeof(draft_.new_user_welcome), val);
            else if (idx == 8) con_str_copy(draft_.interfaces_file, sizeof(draft_.interfaces_file), val);
            else if (idx == 9) con_str_copy(draft_.peers_file, sizeof(draft_.peers_file), val);
        } else if (settings_tab_ == 1) {
            if (idx == 1) { long v = strtol(val, NULL, 10); if (v >= 0 && v <= 15) draft_.bbs_ssid = static_cast<oyk_u8>(v); }
            else if (idx == 2) { if (!val[0]) memset(&draft_.bbs_callsign, 0, sizeof(draft_.bbs_callsign)); else oyk_parse_callsign(val, &draft_.bbs_callsign); }
            else if (idx == 3) { if (!val[0]) memset(&draft_.bbs_alias, 0, sizeof(draft_.bbs_alias)); else oyk_parse_callsign(val, &draft_.bbs_alias); }
            else if (idx == 5) con_str_copy(draft_.bbs_files_path, sizeof(draft_.bbs_files_path), val);
            else if (idx == 6) con_str_copy(draft_.bbs_mail_path, sizeof(draft_.bbs_mail_path), val);
            else if (idx == 7) con_str_copy(draft_.bbs_welcome, sizeof(draft_.bbs_welcome), val);
        } else if (settings_tab_ == 2) {
            if (idx == 1) { long v = strtol(val, NULL, 10); if (v >= 0 && v <= 15) draft_.chat_ssid = static_cast<oyk_u8>(v); }
            else if (idx == 2) { if (!val[0]) memset(&draft_.chat_callsign, 0, sizeof(draft_.chat_callsign)); else oyk_parse_callsign(val, &draft_.chat_callsign); }
            else if (idx == 3) { if (!val[0]) memset(&draft_.chat_alias, 0, sizeof(draft_.chat_alias)); else oyk_parse_callsign(val, &draft_.chat_alias); }
            else if (idx == 4) con_str_copy(draft_.chat_topic, sizeof(draft_.chat_topic), val);
            else if (idx == 5) con_str_copy(draft_.chat_welcome, sizeof(draft_.chat_welcome), val);
        } else if (settings_tab_ == 3 && idx >= 0 && idx < draft_.iface_count) {
            if (settings_iface_field_ == 1) {
                if (strncmp(val, "ser", 3) == 0 || strncmp(val, "usbh", 4) == 0) {
                    con_str_copy(draft_.ifaces[idx].device, sizeof(draft_.ifaces[idx].device), val);
                } else {
                    con_str_copy(draft_.ifaces[idx].host, sizeof(draft_.ifaces[idx].host), val);
                }
            }
        } else if (settings_tab_ == 5) {
            if (idx == 2) con_str_copy(draft_.rig_host, sizeof(draft_.rig_host), val);
            else if (idx == 3) { long p = strtol(val, NULL, 10); if (p > 0 && p <= 65535L) draft_.rig_port = static_cast<oyk_u16>(p); }
            else if (idx == 4) { long p = strtol(val, NULL, 10); if (p >= 0 && p <= 3600L) draft_.rig_poll_sec = static_cast<oyk_u16>(p); }
        } else if (settings_tab_ == 6) {
            if (idx == 1) con_str_copy(draft_.printer_device, sizeof(draft_.printer_device), val);
            else if (idx == 3) { long b = strtol(val, NULL, 10); if (b >= 300L) draft_.printer_baud = static_cast<oyk_u32>(b); }
            else if (idx == 4) { oyk_parse_log_mask(val, &draft_.printer_level); }
        }
        render();
        return;
    }

    if (pk == PROMPT_RIG_FREQ && bridge_) {
        oyk_u32 hz = static_cast<oyk_u32>(strtoul(val, NULL, 10));
        strcpy(dialog_fb_, bridge_->rig_set_freq(hz) ? "Frequency updated." : bridge_->rig_status()->last_error);
        render();
        return;
    }

    if (pk == PROMPT_RIG_MODE && bridge_) {
        strcpy(dialog_fb_, bridge_->rig_set_mode(val, 0) ? "Mode updated." : bridge_->rig_status()->last_error);
        render();
        return;
    }

    if (pk == PROMPT_RIG_VFO && bridge_) {
        strcpy(dialog_fb_, bridge_->rig_set_vfo(val) ? "VFO updated." : bridge_->rig_status()->last_error);
        render();
        return;
    }

    if (pk == PROMPT_RIG_RF_SELECT && bridge_) {
        int zone = 0, pos = 0;
        const char* sep = strchr(val, ':');
        if (!sep) sep = strchr(val, ',');
        if (sep) {
            zone = atoi(val);
            pos = atoi(sep + 1);
        } else {
            zone = bridge_->rig_status()->zone >= 0 ? bridge_->rig_status()->zone : 0;
            pos = atoi(val);
        }
        strcpy(dialog_fb_, bridge_->rig_rf_select(zone, pos) ? "RadioFoundry select sent." : bridge_->rig_status()->last_error);
        render();
        return;
    }
}

OYK_FLASHMEM int TinyTmuxConsole::wizard_try_advance() {
    wiz_err_[0] = '\0';
    if (wiz_step_ == 0) {
        if (wiz_drv_ != wiz_last_drv_) {
            wizard_apply_driver_defaults();
        }
        wiz_step_ = 1;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 1) {
        if (!wiz_id_[0]) {
            strcpy(wiz_err_, "Interface ID cannot be empty.");
            wiz_focus_ = 0;
            return 0;
        }
        for (int i = 0; i < draft_.iface_count; ++i) {
            if (strieq_ci(draft_.ifaces[i].id, wiz_id_)) {
                sprintf(wiz_err_, "Duplicate interface ID: %.24s", wiz_id_);
                wiz_focus_ = 0;
                return 0;
            }
        }
        wiz_step_ = 2;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 2) {
        if (!wiz_ep_[0]) {
            strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                 ? "Serial device cannot be empty."
                                 : "Host / IP cannot be empty.");
            wiz_focus_ = 0;
            return 0;
        }
        if (wiz_drv_ != 1 && !wiz_port_[0]) {
            strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                 ? "Baud rate cannot be empty."
                                 : "Port cannot be empty.");
            wiz_focus_ = 1;
            return 0;
        }
        if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && oyk_is_all_digits(wiz_ep_)) {
            oyk_u32 did = 0;
            if (!oyk_parse_dmr_id(wiz_ep_, &did)) {
                strcpy(wiz_err_, "Invalid DMR ID (allowed 1..16777214).");
                wiz_focus_ = 0;
                return 0;
            }
        }
        if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && wiz_mask_[0]) {
            oyk_u32 mip = 0;
            if (!oyk_parse_ipv4(wiz_mask_, &mip)) {
                strcpy(wiz_err_, "Invalid IPv4 netmask (e.g. 255.0.0.0).");
                wiz_focus_ = (wiz_drv_ == 1) ? 1 : 2;
                return 0;
            }
        }
        wiz_step_ = 3;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 3) {
        commit_iface_fields(draft_.iface_count);
        prompt_kind_ = PROMPT_NONE;
        return 1;
    }
    return 0;
}

OYK_FLASHMEM int TinyTmuxConsole::edit_iface_try_save() {
    wiz_err_[0] = '\0';
    if (!wiz_id_[0]) {
        strcpy(wiz_err_, "Interface ID cannot be empty.");
        wiz_focus_ = 1;
        return 0;
    }
    for (int i = 0; i < draft_.iface_count; ++i) {
        if (i == wiz_edit_idx_) continue;
        if (strieq_ci(draft_.ifaces[i].id, wiz_id_)) {
            sprintf(wiz_err_, "Duplicate interface ID: %.24s", wiz_id_);
            wiz_focus_ = 1;
            return 0;
        }
    }
    if (!wiz_ep_[0]) {
        strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                             ? "Serial device cannot be empty."
                             : "Host / IP cannot be empty.");
        wiz_focus_ = 3;
        return 0;
    }
    if (wiz_drv_ != 1 && !wiz_port_[0]) {
        strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                             ? "Baud rate cannot be empty."
                             : "Port cannot be empty.");
        wiz_focus_ = 4;
        return 0;
    }
    if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && oyk_is_all_digits(wiz_ep_)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(wiz_ep_, &did)) {
            strcpy(wiz_err_, "Invalid DMR ID (allowed 1..16777214).");
            wiz_focus_ = 3;
            return 0;
        }
    }
    if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && wiz_mask_[0]) {
        oyk_u32 mip = 0;
        if (!oyk_parse_ipv4(wiz_mask_, &mip)) {
            strcpy(wiz_err_, "Invalid IPv4 netmask (e.g. 255.0.0.0).");
            wiz_focus_ = (wiz_drv_ == 1) ? 4 : 5;
            return 0;
        }
    }
    commit_iface_fields(wiz_edit_idx_);
    prompt_kind_ = PROMPT_NONE;
    wiz_edit_idx_ = -1;
    return 1;
}

OYK_FLASHMEM void TinyTmuxConsole::handle_edit_iface_key(int key) {
    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        wiz_edit_idx_ = -1;
        strcpy(dialog_fb_, "Edit interface cancelled.");
        render();
        return;
    }
    int fcnt = edit_iface_field_count();
    if (wiz_focus_ > fcnt) wiz_focus_ = fcnt;

    if (key == TMUX_KEY_UP) {
        wiz_focus_ = (wiz_focus_ + fcnt) % (fcnt + 1);
        render();
        return;
    }
    if (key == TMUX_KEY_DOWN || key == '\t') {
        wiz_focus_ = (wiz_focus_ + 1) % (fcnt + 1);
        render();
        return;
    }

    if (wiz_focus_ == fcnt) {
        if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) wiz_btn_ = (wiz_btn_ == 0) ? 1 : 0;
        else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 1) {
                prompt_kind_ = PROMPT_NONE;
                wiz_edit_idx_ = -1;
                strcpy(dialog_fb_, "Edit interface cancelled.");
            } else {
                edit_iface_try_save();
            }
        } else {
            return;
        }
        render();
        return;
    }

    if (wiz_focus_ == 0) {
        if (key == TMUX_KEY_LEFT) {
            wiz_drv_ = (wiz_drv_ + 6) % 7;
            wizard_apply_driver_defaults();
            wiz_err_[0] = '\0';
            render();
        } else if (key == TMUX_KEY_RIGHT || key == ' ') {
            wiz_drv_ = (wiz_drv_ + 1) % 7;
            wizard_apply_driver_defaults();
            wiz_err_[0] = '\0';
            render();
        } else if (key == '\r' || key == '\n') {
            edit_iface_try_save();
            render();
        }
        return;
    }

    int tbase = (wiz_drv_ == 0) ? 7 : ((wiz_drv_ == 1 || wiz_drv_ == 4) ? 6 : 5);
    if (wiz_focus_ >= tbase) {
        int rel = wiz_focus_ - tbase;
        if (key == ' ' || key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
            if (rel == 0) wiz_flags_ ^= 1U;
            else if (rel == 1 && wiz_drv_ != 6) wiz_flags_ ^= 2U;
            else if (rel == 2 && wiz_drv_ != 6) wiz_flags_ ^= 0x20U;
            else if (rel == 3 && wiz_drv_ != 6) {
                oyk_u8 lp = tmux_cycle_lpol(static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U),
                                            (key == TMUX_KEY_LEFT) ? -1 : 1);
                wiz_flags_ = static_cast<oyk_u8>((wiz_flags_ & 0x23U) | ((lp & 0x07U) << 2));
            }
            render();
        } else if (key == '\r' || key == '\n') {
            edit_iface_try_save();
            render();
        }
        return;
    }

    char* target = NULL;
    size_t cap = 0;
    if (wiz_focus_ == 1) { target = wiz_id_; cap = sizeof(wiz_id_); }
    else if (wiz_focus_ == 2) { target = wiz_desc_; cap = sizeof(wiz_desc_); }
    else if (wiz_focus_ == 3) { target = wiz_ep_; cap = sizeof(wiz_ep_); }
    else if (wiz_drv_ == 1) {
        if (wiz_focus_ == 4) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
        else if (wiz_focus_ == 5) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
    } else {
        if (wiz_focus_ == 4) { target = wiz_port_; cap = sizeof(wiz_port_); }
        else if ((wiz_drv_ == 0 || wiz_drv_ == 4) && wiz_focus_ == 5) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
        else if (wiz_drv_ == 0 && wiz_focus_ == 6) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
    }

    if (key == '\r' || key == '\n') {
        edit_iface_try_save();
        render();
    } else if (target && (key == 8 || key == 127)) {
        size_t n = strlen(target);
        if (n > 0) target[n - 1] = '\0';
        wiz_err_[0] = '\0';
        render();
    } else if (target && key >= 0x20 && key < 0x7F) {
        size_t n = strlen(target);
        if (n + 1 < cap) {
            target[n] = static_cast<char>(key);
            target[n + 1] = '\0';
        }
        wiz_err_[0] = '\0';
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_add_wizard_key(int key) {
    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        strcpy(dialog_fb_, "Add interface cancelled.");
        render();
        return;
    }
    if (key == 0x02 && wiz_step_ > 0) { /* Ctrl+B -> Back */
        --wiz_step_;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        wiz_err_[0] = '\0';
        render();
        return;
    }

    if (wiz_step_ == 0) {
        if (key == TMUX_KEY_UP) wiz_drv_ = (wiz_drv_ + 6) % 7;
        else if (key == TMUX_KEY_DOWN) wiz_drv_ = (wiz_drv_ + 1) % 7;
        else if (key == TMUX_KEY_HOME) wiz_drv_ = 0;
        else if (key == TMUX_KEY_END) wiz_drv_ = 6;
        else if (key >= '1' && key <= '7') wiz_drv_ = key - '1';
        else if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT || key == '\t') wiz_btn_ = (wiz_btn_ == 1) ? 2 : 1;
        else if (key == '\r' || key == '\n') {
            if (wiz_btn_ == 2) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Add interface cancelled.");
            } else {
                wizard_try_advance();
            }
        } else {
            return;
        }
        render();
        return;
    }

    if (wiz_step_ == 3) {
        if (key == TMUX_KEY_LEFT) wiz_btn_ = (wiz_btn_ + 2) % 3;
        else if (key == TMUX_KEY_RIGHT || key == '\t') wiz_btn_ = (wiz_btn_ + 1) % 3;
        else if (key == 'b' || key == 'B') {
            wiz_step_ = 2;
            wiz_focus_ = 0;
            wiz_btn_ = 1;
            wiz_err_[0] = '\0';
        } else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 0) {
                wiz_step_ = 2;
                wiz_focus_ = 0;
                wiz_btn_ = 1;
                wiz_err_[0] = '\0';
            } else if (wiz_btn_ == 2) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Add interface cancelled.");
            } else {
                wizard_try_advance();
            }
        } else {
            return;
        }
        render();
        return;
    }

    int fcnt = wizard_step_field_count();
    if (key == TMUX_KEY_UP) {
        wiz_focus_ = (wiz_focus_ + fcnt) % (fcnt + 1);
        render();
        return;
    }
    if (key == TMUX_KEY_DOWN || key == '\t') {
        wiz_focus_ = (wiz_focus_ + 1) % (fcnt + 1);
        render();
        return;
    }

    if (wiz_focus_ == fcnt) {
        if (key == TMUX_KEY_LEFT) wiz_btn_ = (wiz_btn_ + 2) % 3;
        else if (key == TMUX_KEY_RIGHT) wiz_btn_ = (wiz_btn_ + 1) % 3;
        else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 0) {
                --wiz_step_;
                wiz_focus_ = 0;
                wiz_btn_ = 1;
                wiz_err_[0] = '\0';
            } else if (wiz_btn_ == 2) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Add interface cancelled.");
            } else {
                wizard_try_advance();
            }
        } else {
            return;
        }
        render();
        return;
    }

    if (wiz_step_ == 2) {
        int tbase = (wiz_drv_ == 0) ? 4 : ((wiz_drv_ == 1 || wiz_drv_ == 4) ? 3 : 2);
        if (wiz_focus_ >= tbase) {
            int rel = wiz_focus_ - tbase;
            if (key == ' ' || key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
                if (rel == 0) wiz_flags_ ^= 1U;
                else if (rel == 1 && wiz_drv_ != 6) wiz_flags_ ^= 2U;
                else if (rel == 2 && wiz_drv_ != 6) wiz_flags_ ^= 0x20U;
                else if (rel == 3 && wiz_drv_ != 6) {
                    oyk_u8 lp = tmux_cycle_lpol(static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U),
                                                (key == TMUX_KEY_LEFT) ? -1 : 1);
                    wiz_flags_ = static_cast<oyk_u8>((wiz_flags_ & 0x23U) | ((lp & 0x07U) << 2));
                }
                render();
            } else if (key == '\r' || key == '\n') {
                wizard_try_advance();
                render();
            }
            return;
        }
    }

    char* target = NULL;
    size_t cap = 0;
    if (wiz_step_ == 1) {
        if (wiz_focus_ == 0) { target = wiz_id_; cap = sizeof(wiz_id_); }
        else { target = wiz_desc_; cap = sizeof(wiz_desc_); }
    } else {
        if (wiz_focus_ == 0) { target = wiz_ep_; cap = sizeof(wiz_ep_); }
        else if (wiz_drv_ == 1) {
            if (wiz_focus_ == 1) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
            else if (wiz_focus_ == 2) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
        } else {
            if (wiz_focus_ == 1) { target = wiz_port_; cap = sizeof(wiz_port_); }
            else if ((wiz_drv_ == 0 || wiz_drv_ == 4) && wiz_focus_ == 2) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
            else if (wiz_drv_ == 0 && wiz_focus_ == 3) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
        }
    }

    if (key == '\r' || key == '\n') {
        wizard_try_advance();
        render();
    } else if (target && (key == 8 || key == 127)) {
        size_t n = strlen(target);
        if (n > 0) target[n - 1] = '\0';
        wiz_err_[0] = '\0';
        render();
    } else if (target && key >= 0x20 && key < 0x7F) {
        size_t n = strlen(target);
        if (n + 1 < cap) {
            target[n] = static_cast<char>(key);
            target[n + 1] = '\0';
        }
        wiz_err_[0] = '\0';
        render();
    }
}

void TinyTmuxConsole::handle_prompt_key(int key) {
    if (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD) {
        handle_add_wizard_key(key);
        return;
    }
    if (prompt_kind_ == PROMPT_SETTINGS_EDIT_IFACE) {
        handle_edit_iface_key(key);
        return;
    }
    if (prompt_kind_ == PROMPT_SETTINGS_EDIT_PEER) {
        handle_edit_peer_key(key);
        return;
    }

    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        prompt_buf_[0] = '\0';
        prompt_len_ = 0;
        render();
    } else if (key == '\r' || key == '\n') {
        commit_prompt();
    } else if ((key == 8 || key == 127) && prompt_len_ > 0) {
        prompt_buf_[--prompt_len_] = '\0';
        render();
    } else if (key >= 0x20 && key < 0x7F && prompt_len_ < 120) {
        prompt_buf_[prompt_len_++] = static_cast<char>(key);
        prompt_buf_[prompt_len_] = '\0';
        render();
    }
}

void TinyTmuxConsole::handle_console_key(int key) {
    if (key == 'm' || key == 'M' || key == TMUX_KEY_F10) {
        view_ = TMUX_VIEW_MENU;
        menu_sel_ = 0;
        render();
    } else if (key == '\t' || key == 'l' || key == 'L') {
        view_ = TMUX_VIEW_LOCAL_TERM;
        render();
    } else if (key == 'a' || key == 'A') {
        node_->set_accept_incoming(1);
        strcpy(status_line_, "New incoming connections are now ALLOWED.");
        render();
    } else if (key == 'd' || key == 'D') {
        node_->set_accept_incoming(0);
        strcpy(status_line_, "New incoming connections are now DENIED.");
        render();
    } else if (key == 'f' || key == 'F') {
        files_tab_ = 0;
        dialog_fb_[0] = '\0';
        refresh_files_lists();
        view_ = TMUX_VIEW_FILES;
        render();
    } else if (key == 'r' || key == 'R') {
        dialog_fb_[0] = '\0';
        if (bridge_) bridge_->sync_rig_config();
        view_ = TMUX_VIEW_RIG;
        render();
    } else if (key == 's' || key == 'S') {
        open_settings(0);
    } else if (key == 'e' || key == 'E') {
        open_settings(4);
    } else if (key == 't' || key == 'T') {
        begin_prompt(PROMPT_TERMINATE_SID, "Terminate session ID (Esc=cancel):", "");
    } else if (key == 'b' || key == 'B') {
        begin_prompt(PROMPT_BAN_CALLSIGN, "Callsign to Ban / Unban (Esc=cancel):", "");
    } else if (key == 'p' || key == 'P') {
        view_ = TMUX_VIEW_PSRAM;
        render();
    } else if (key == 'u' || key == 'U') {
        bootloader_pending_ = 0;
        view_ = TMUX_VIEW_BOOTLOADER;
        render();
    } else if (key == 'g' || key == 'G') {
        storage_sel_ = 0;
        dialog_fb_[0] = '\0';
        view_ = TMUX_VIEW_STORAGE;
        render();
    } else if (key == 0x0C) {
        render();
    }
}

void TinyTmuxConsole::handle_menu_key(int key) {
    if (key == 27 || key == TMUX_KEY_F10 || key == 'x' || key == 'X') {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == TMUX_KEY_UP) {
        menu_sel_ = (menu_sel_ + 12) % 13;
        render();
    } else if (key == TMUX_KEY_DOWN) {
        menu_sel_ = (menu_sel_ + 1) % 13;
        render();
    } else if (key == TMUX_KEY_HOME) {
        menu_sel_ = 0;
        render();
    } else if (key == TMUX_KEY_END) {
        menu_sel_ = 12;
        render();
    } else if (key == '\r' || key == '\n') {
        static const int KEYS[13] = { 'a', 'd', 'l', 'f', 'r', 's', 'e', 't', 'b', 'p', 'g', 'u', 'x' };
        int k = KEYS[menu_sel_];
        view_ = TMUX_VIEW_CONSOLE;
        if (k == 'x') render();
        else handle_console_key(k);
    } else if (key == 'a' || key == 'A' || key == 'd' || key == 'D' ||
               key == 'l' || key == 'L' || key == 'f' || key == 'F' ||
               key == 'r' || key == 'R' || key == 's' || key == 'S' ||
               key == 'e' || key == 'E' ||
               key == 't' || key == 'T' || key == 'b' || key == 'B' ||
               key == 'p' || key == 'P' || key == 'u' || key == 'U' ||
               key == 'g' || key == 'G') {
        view_ = TMUX_VIEW_CONSOLE;
        handle_console_key(key);
    }
}

void TinyTmuxConsole::handle_files_key(int key) {
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == '\t') {
        files_tab_ = (files_tab_ + 1) % 2;
        render();
    } else if (key == '1') {
        files_tab_ = 0;
        render();
    } else if (key == '2') {
        files_tab_ = 1;
        render();
    } else if (key == 'r' || key == 'R') {
        refresh_files_lists();
        strcpy(dialog_fb_, "Refreshed files lists.");
        render();
    } else if (key == TMUX_KEY_UP) {
        int cnt = (files_tab_ == 0) ? pending_count_ : approved_count_;
        if (cnt > 0) files_sel_[files_tab_] = (files_sel_[files_tab_] + cnt - 1) % cnt;
        render();
    } else if (key == TMUX_KEY_DOWN) {
        int cnt = (files_tab_ == 0) ? pending_count_ : approved_count_;
        if (cnt > 0) files_sel_[files_tab_] = (files_sel_[files_tab_] + 1) % cnt;
        render();
    } else if (key == TMUX_KEY_HOME) {
        files_sel_[files_tab_] = 0;
        render();
    } else if (key == TMUX_KEY_END) {
        int cnt = (files_tab_ == 0) ? pending_count_ : approved_count_;
        if (cnt > 0) files_sel_[files_tab_] = cnt - 1;
        render();
    } else if (files_tab_ == 0 && (key == 'a' || key == 'A' || key == '\r' || key == '\n')) {
        if (pending_count_ > 0) {
            int idx = files_sel_[0];
            char dest[128], err[64];
            int exists = 0;
            if (oyk_bbs_approve_incoming_file("incoming", node_->config().bbs_files_path,
                                              pending_files_[idx].uploader, pending_files_[idx].filename,
                                              "", 0, dest, sizeof(dest), &exists, err, sizeof(err))) {
                sprintf(dialog_fb_, "Approved %s/%s", pending_files_[idx].uploader, pending_files_[idx].filename);
                node_->log_message(OYK_LOG_INFO, dialog_fb_);
            } else {
                sprintf(dialog_fb_, "Approve error: %.60s", err);
            }
            refresh_files_lists();
        }
        render();
    } else if (key == 'd' || key == 'D') {
        if (files_tab_ == 0 && pending_count_ > 0) {
            int idx = files_sel_[0];
            char err[64];
            if (oyk_bbs_delete_incoming_file("incoming", pending_files_[idx].uploader,
                                             pending_files_[idx].filename, err, sizeof(err))) {
                sprintf(dialog_fb_, "Deleted pending %s/%s", pending_files_[idx].uploader, pending_files_[idx].filename);
            } else {
                sprintf(dialog_fb_, "Delete error: %.60s", err);
            }
            refresh_files_lists();
        } else if (files_tab_ == 1 && approved_count_ > 0) {
            int idx = files_sel_[1];
            char err[64];
            if (oyk_bbs_delete_approved_file(node_->config().bbs_files_path,
                                             approved_files_[idx].rel_path, err, sizeof(err))) {
                sprintf(dialog_fb_, "Removed approved %.55s", approved_files_[idx].rel_path);
            } else {
                sprintf(dialog_fb_, "Remove error: %.60s", err);
            }
            refresh_files_lists();
        }
        render();
    }
}

void TinyTmuxConsole::handle_rig_key(int key) {
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        render();
        return;
    }
    if (!bridge_ || !node_->config().rig_enabled) {
        strcpy(dialog_fb_, "Rig control not available.");
        render();
        return;
    }
    int proto = oyk_rig_parse_protocol(node_->config().rig_protocol);
    if (key == 'r' || key == 'R') {
        strcpy(dialog_fb_, bridge_->rig_poll_now(now_ms_) ? "Rig polled." : bridge_->rig_status()->last_error);
    } else if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
        if (key == 'j' || key == 'J') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_channel(-1) ? "Channel -1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'k' || key == 'K') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_channel(1) ? "Channel +1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'h' || key == 'H') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_zone(-1) ? "Zone -1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'l' || key == 'L') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_zone(1) ? "Zone +1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'n' || key == 'N') {
            strcpy(dialog_fb_, bridge_->rig_rf_stop_notify() ? "Stop notify sent." : bridge_->rig_status()->last_error);
        } else if (key == 's' || key == 'S') {
            begin_prompt(PROMPT_RIG_RF_SELECT, "Select Zone:Pos (e.g. 0:1):", "0:0");
            return;
        }
    } else {
        if (key == 'f' || key == 'F') {
            begin_prompt(PROMPT_RIG_FREQ, "Frequency in Hz (e.g. 145050000):", "");
            return;
        } else if (key == 'm' || key == 'M') {
            begin_prompt(PROMPT_RIG_MODE, "Mode (e.g. FM, USB, PKTFM):", bridge_->rig_status()->mode);
            return;
        } else if (key == 'p' || key == 'P') {
            int next_ptt = (bridge_->rig_status()->ptt == 1) ? 0 : 1;
            strcpy(dialog_fb_, bridge_->rig_set_ptt(next_ptt) ? "PTT updated." : bridge_->rig_status()->last_error);
        } else if (key == 'v' || key == 'V') {
            begin_prompt(PROMPT_RIG_VFO, "VFO (VFOA/VFOB):", bridge_->rig_status()->vfo);
            return;
        }
    }
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::handle_settings_key(int key) {
    int cnt = settings_tab_item_count(settings_tab_);
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        strcpy(status_line_, "Settings cancelled (no changes applied).");
        render();
    } else if (key == '\t' || key == TMUX_KEY_RIGHT) {
        settings_tab_ = (settings_tab_ + 1) % 7;
        render();
    } else if (key == TMUX_KEY_LEFT) {
        settings_tab_ = (settings_tab_ + 6) % 7;
        render();
    } else if (key >= '1' && key <= '7') {
        settings_tab_ = key - '1';
        render();
    } else if (key == TMUX_KEY_UP && cnt > 0) {
        settings_sel_[settings_tab_] = (settings_sel_[settings_tab_] + cnt - 1) % cnt;
        render();
    } else if (key == TMUX_KEY_DOWN && cnt > 0) {
        settings_sel_[settings_tab_] = (settings_sel_[settings_tab_] + 1) % cnt;
        render();
    } else if (key == TMUX_KEY_HOME && cnt > 0) {
        settings_sel_[settings_tab_] = 0;
        render();
    } else if (key == TMUX_KEY_END && cnt > 0) {
        settings_sel_[settings_tab_] = cnt - 1;
        render();
    } else if (key == TMUX_KEY_F2 || key == 's' || key == 'S') {
        save_and_apply_settings();
    } else if (key == '\r' || key == '\n' || key == 'e' || key == 'E') {
        settings_iface_field_ = 0;
        edit_current_settings_field();
    } else if (settings_tab_ == 3 && (key == 'h' || key == 'H')) {
        if (cnt > 0) {
            int idx = settings_sel_[3];
            settings_iface_field_ = 1;
            const char* cur = draft_.ifaces[idx].device[0] ? draft_.ifaces[idx].device : draft_.ifaces[idx].host;
            begin_prompt(PROMPT_SETTINGS_VALUE, "Interface Device (ser0/usbh0) or Host IP:", cur);
        }
    } else if (settings_tab_ == 3 && (key == 'n' || key == 'N' || key == 'a' || key == 'A')) {
        open_add_iface_wizard();
    } else if (settings_tab_ == 3 && (key == 'd' || key == 'D' || key == 'x' || key == 'X')) {
        if (draft_.iface_count > 0) {
            int idx = settings_sel_[3];
            for (int k = idx + 1; k < draft_.iface_count; ++k) {
                draft_.ifaces[k - 1] = draft_.ifaces[k];
            }
            draft_.iface_count--;
            if (settings_sel_[3] >= draft_.iface_count && draft_.iface_count > 0) {
                settings_sel_[3] = draft_.iface_count - 1;
            }
            strcpy(dialog_fb_, "Interface removed from draft.");
            render();
        }
    } else if (settings_tab_ == 4 && (key == 'n' || key == 'N' || key == 'a' || key == 'A')) {
        open_edit_peer_dialog(-1);
    } else if (settings_tab_ == 4 && (key == 'd' || key == 'D' || key == 'x' || key == 'X')) {
        if (draft_.peer_count > 0) {
            int idx = settings_sel_[4];
            for (int k = idx + 1; k < draft_.peer_count; ++k) {
                draft_.peers[k - 1] = draft_.peers[k];
            }
            draft_.peer_count--;
            if (settings_sel_[4] >= draft_.peer_count && draft_.peer_count > 0) {
                settings_sel_[4] = draft_.peer_count - 1;
            }
            strcpy(dialog_fb_, "Peer removed from draft.");
            render();
        }
    } else if (settings_tab_ == 4 && (key == 'i' || key == 'I') && cnt > 0) {
        OykPeerConfig& pc = draft_.peers[settings_sel_[4]];
        pc.allow_inbound = pc.allow_inbound ? 0 : 1;
        pc.auto_learned = 0;
        sprintf(dialog_fb_, "Peer %s allow_inbound=%s", pc.id, pc.allow_inbound ? "true" : "false");
        render();
    } else if (settings_tab_ == 4 && (key == 'o' || key == 'O') && cnt > 0) {
        OykPeerConfig& pc = draft_.peers[settings_sel_[4]];
        pc.allow_outbound = pc.allow_outbound ? 0 : 1;
        pc.auto_learned = 0;
        sprintf(dialog_fb_, "Peer %s allow_outbound=%s", pc.id, pc.allow_outbound ? "true" : "false");
        render();
    } else if (settings_tab_ == 4 && (key == 'k' || key == 'K')) {
        int kept = 0;
        for (int i = 0; i < draft_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
            if (draft_.peers[i].auto_learned) {
                draft_.peers[i].auto_learned = 0;
                ++kept;
            }
        }
        sprintf(dialog_fb_, "Marked %d auto-learned peer(s) persistent.", kept);
        render();
    }
}

void TinyTmuxConsole::handle_psram_key(int key) {
    if (key == 27) {
        if (tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING) {
            tinytmux_psram_marchc_cancel();
            render();
        } else {
            view_ = TMUX_VIEW_CONSOLE;
            render();
        }
    } else if (key == 't' || key == 'T' || key == 'r' || key == 'R') {
        if (tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING) {
            tinytmux_psram_marchc_cancel();
        }
        tinytmux_psram_retest();
        render();
    } else if (key == 'm' || key == 'M') {
        if (tinytmux_psram_marchc_status()->state != TMUX_MARCHC_RUNNING) {
            tinytmux_psram_marchc_start(now_ms_);
            last_marchc_redraw_ms_ = now_ms_;
            render();
        }
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_bootloader_key(int key) {
    if (bootloader_pending_) return;
    int auto_cnt = node_ ? node_->auto_learned_peer_count() : 0;
    if (key == 27 || key == 'n' || key == 'N') {
        view_ = TMUX_VIEW_CONSOLE;
        render();
        return;
    }
    if (auto_cnt > 0) {
        if (key == 'r' || key == 'R') {
            open_settings(4);
            return;
        }
        if (key == 's' || key == 'S') {
            char err[64];
            node_->persist_auto_learned_peers(err, sizeof(err));
        } else if (key == 'd' || key == 'D') {
            node_->discard_auto_learned_peers();
        } else {
            return;
        }
    } else if (key != 'y' && key != 'Y') {
        return;
    }
    char call_s[16], notice[128];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    sprintf(notice, "\r*** Node %s going offline for firmware upgrade. Expect a delay before reconnect.\r", call_s);
    node_->begin_shutdown_with_notice(notice);
    bootloader_pending_ = 1;
    bootloader_start_ms_ = now_ms_;
    render();
}

void TinyTmuxConsole::handle_lterm_key(int key) {
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        strcpy(status_line_, lterm_sid_
                                 ? "Console (Local Terminal active; press Tab to return)"
                                 : "Returned to Operator Console.");
        render();
        return;
    }
    if (key == '\r' || key == '\n') {
        lterm_input_[lterm_input_len_] = '\0';
        char line[128];
        strcpy(line, lterm_input_);
        lterm_input_len_ = 0;
        lterm_input_[0] = '\0';

        if (strcmp(line, "/console") == 0 || strcmp(line, "/bg") == 0) {
            handle_lterm_key(27);
            return;
        }
        if (strcmp(line, "/local") == 0 || strcmp(line, "/bbs") == 0 || strcmp(line, "/chat") == 0) {
            const char* target = (line[1] == 'b') ? "BBS" : ((line[1] == 'c') ? "CHAT" : "LOCAL");
            if (lterm_sid_ != 0) node_->close_local_terminal(lterm_sid_);
            char err[64];
            if (!node_->open_local_terminal(-1, target, &lterm_sid_, NULL, &lterm_is_local_, err, sizeof(err))) {
                push_lterm_line(err);
            }
            render();
            return;
        }
        if (strcmp(line, "/disc") == 0) {
            if (lterm_sid_ != 0) {
                node_->close_local_terminal(lterm_sid_);
                lterm_sid_ = 0;
            }
            render();
            return;
        }

        char echo[136];
        sprintf(echo, "> %.124s", line);
        push_lterm_line(echo);
        if (lterm_sid_ != 0) {
            size_t n = strlen(line);
            line[n++] = '\r';
            line[n] = '\0';
            node_->send_local_bytes(lterm_sid_, reinterpret_cast<const oyk_u8*>(line), n);
        } else {
            push_lterm_line("*** Not connected. Type /local, /bbs, or /chat.");
        }
        render();
        return;
    }
    if ((key == 8 || key == 127) && lterm_input_len_ > 0) {
        lterm_input_[--lterm_input_len_] = '\0';
        render();
    } else if (key >= 0x20 && key < 0x7F && lterm_input_len_ < 120) {
        lterm_input_[lterm_input_len_++] = static_cast<char>(key);
        lterm_input_[lterm_input_len_] = '\0';
    }
}

void TinyTmuxConsole::reset_escape() {
    esc_state_ = ESC_IDLE;
    esc_len_ = 0;
    esc_buf_[0] = '\0';
}

void TinyTmuxConsole::dispatch_key(int key) {
    if (!node_) return;
    if (prompt_kind_ != PROMPT_NONE) {
        handle_prompt_key(key);
        return;
    }

    switch (view_) {
        case TMUX_VIEW_CONSOLE:    handle_console_key(key); break;
        case TMUX_VIEW_MENU:       handle_menu_key(key); break;
        case TMUX_VIEW_LOCAL_TERM: handle_lterm_key(key); break;
        case TMUX_VIEW_FILES:      handle_files_key(key); break;
        case TMUX_VIEW_RIG:        handle_rig_key(key); break;
        case TMUX_VIEW_SETTINGS:   handle_settings_key(key); break;
        case TMUX_VIEW_PSRAM:      handle_psram_key(key); break;
        case TMUX_VIEW_BOOTLOADER: handle_bootloader_key(key); break;
        case TMUX_VIEW_STORAGE:    handle_storage_key(key); break;
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_da_csi() {
    TinyTmuxTermType cand = term_type_;
    if (esc_buf_[0] == '?') {
        const char* p = esc_buf_ + 1;
        int idx = 0;
        while (*p) {
            int v = atoi(p);
            if (idx == 0) {
                if (v == 62 && cand < TMUX_TERM_VT220) cand = TMUX_TERM_VT220;
                else if (v >= 63 && v <= 65 && cand < TMUX_TERM_VT340) cand = TMUX_TERM_VT340;
            } else if (v == 22 && cand < TMUX_TERM_VT340) {
                cand = TMUX_TERM_VT340;
            }
            const char* semi = strchr(p, ';');
            if (!semi) break;
            p = semi + 1;
            ++idx;
        }
    } else if (esc_buf_[0] == '>') {
        int pp = -1, pv = 0;
        const char* p = esc_buf_ + 1;
        if (*p) pp = atoi(p);
        const char* semi = strchr(p, ';');
        if (semi) pv = atoi(semi + 1);
        if ((pp == 0 && pv >= 95) ||
            (pp == 1 && pv >= 200) ||
            (pp == 41 && pv >= 200) ||
            (pp == 65 && pv >= 5000) ||
            pp == 83 || pp == 84 || pp == 85) {
            cand = TMUX_TERM_XTERM256;
        } else if (pp == 18 || pp == 19 || pp == 24 || pp == 41 ||
                   pp == 61 || pp == 64 || pp == 65) {
            if (cand < TMUX_TERM_VT340) cand = TMUX_TERM_VT340;
        } else if (pp == 1) {
            if (cand < TMUX_TERM_VT220) cand = TMUX_TERM_VT220;
        }
    }
    if (cand > term_type_) {
        term_type_ = cand;
        render();
    }
}

void TinyTmuxConsole::process_csi_final(char final_ch) {
    switch (final_ch) {
        case 'A': dispatch_key(TMUX_KEY_UP); break;
        case 'B': dispatch_key(TMUX_KEY_DOWN); break;
        case 'C': dispatch_key(TMUX_KEY_RIGHT); break;
        case 'D': dispatch_key(TMUX_KEY_LEFT); break;
        case 'Z': dispatch_key(TMUX_KEY_LEFT); break;
        case 'H': dispatch_key(TMUX_KEY_HOME); break;
        case 'F': dispatch_key(TMUX_KEY_END); break;
        case 'c':
            if (esc_buf_[0] == '?' || esc_buf_[0] == '>') {
                handle_da_csi();
            }
            break;
        case '~': {
            int num = atoi(esc_buf_);
            if (num == 1 || num == 7) dispatch_key(TMUX_KEY_HOME);
            else if (num == 4 || num == 8) dispatch_key(TMUX_KEY_END);
            else if (num == 5) dispatch_key(TMUX_KEY_UP);
            else if (num == 6) dispatch_key(TMUX_KEY_DOWN);
            else if (num == 12) dispatch_key(TMUX_KEY_F2);
            else if (num == 21) dispatch_key(TMUX_KEY_F10);
            break;
        }
        default:
            break;
    }
}

void TinyTmuxConsole::process_ss3_final(char ch) {
    switch (ch) {
        case 'A': dispatch_key(TMUX_KEY_UP); break;
        case 'B': dispatch_key(TMUX_KEY_DOWN); break;
        case 'C': dispatch_key(TMUX_KEY_RIGHT); break;
        case 'D': dispatch_key(TMUX_KEY_LEFT); break;
        case 'H': dispatch_key(TMUX_KEY_HOME); break;
        case 'F': dispatch_key(TMUX_KEY_END); break;
        case 'Q': dispatch_key(TMUX_KEY_F2); break;
        case 'M': dispatch_key('\r'); break;
        default:
            break;
    }
}

void TinyTmuxConsole::tick(oyk_u32 now_ms) {
    now_ms_ = now_ms;
    if (!node_) return;
    if (esc_state_ == ESC_GOT_ESC) {
        if ((now_ms_ - esc_start_ms_) >= 40UL) {
            reset_escape();
            dispatch_key(27);
        }
    } else if (esc_state_ != ESC_IDLE) {
        if ((now_ms_ - esc_start_ms_) >= 100UL) {
            reset_escape();
        }
    }

    if (tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING) {
        int still_running = tinytmux_psram_marchc_step(now_ms_, 16384UL);
        if (view_ == TMUX_VIEW_PSRAM &&
            (!still_running || (now_ms_ - last_marchc_redraw_ms_) >= 120UL)) {
            last_marchc_redraw_ms_ = now_ms_;
            render();
        }
    }

    if (bootloader_pending_) {
        if (node_->disconnecting_count() == 0 || (now_ms_ - bootloader_start_ms_) >= 3000UL) {
            bootloader_pending_ = 0;
            node_->finish_shutdown();
            tinytmux_fs_unmount();
            char signoff_call[24], signoff_line[48];
            oyk_format_callsign(&node_->config().callsign, signoff_call, sizeof(signoff_call));
            sprintf(signoff_line, "\r\n73 de %s\r\n", signoff_call);
            write_str(signoff_line);
            write_str("\r\n\033[7m Node halted in HalfKay bootloader. Flash firmware or power-cycle board. \033[0m\r\n");
            tinytmux_board_enter_bootloader();
        }
    }
}

void TinyTmuxConsole::feed_input(const oyk_u8* data, size_t len) {
    if (!node_ || !data) return;
    for (size_t i = 0; i < len; ++i) {
        oyk_u8 b = data[i];
        switch (esc_state_) {
            case ESC_IDLE:
                if (b == 0x1BU) {
                    esc_state_ = ESC_GOT_ESC;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else if (b == 0x9BU) {
                    esc_state_ = ESC_CSI;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else {
                    dispatch_key(static_cast<int>(b));
                }
                break;

            case ESC_GOT_ESC:
                if (b == '[') {
                    esc_state_ = ESC_CSI;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                } else if (b == 'O') {
                    esc_state_ = ESC_SS3;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                } else if (b == 0x1BU) {
                    reset_escape();
                    dispatch_key(27);
                } else {
                    reset_escape();
                    dispatch_key(27);
                    dispatch_key(static_cast<int>(b));
                }
                break;

            case ESC_CSI:
                if (b == 0x1BU) {
                    esc_state_ = ESC_GOT_ESC;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else if (esc_len_ == 0 && b == '[') {
                    esc_state_ = ESC_CSI_LINUX;
                } else if (b >= 0x20U && b <= 0x3FU) {
                    if (esc_len_ < static_cast<int>(sizeof(esc_buf_) - 1)) {
                        esc_buf_[esc_len_++] = static_cast<char>(b);
                        esc_buf_[esc_len_] = '\0';
                    }
                } else if (b >= 0x40U && b <= 0x7EU) {
                    char final_ch = static_cast<char>(b);
                    esc_buf_[esc_len_] = '\0';
                    process_csi_final(final_ch);
                    reset_escape();
                } else {
                    reset_escape();
                }
                break;

            case ESC_CSI_LINUX:
                reset_escape();
                if (b == 'B') {
                    dispatch_key(TMUX_KEY_F2);
                }
                break;

            case ESC_SS3:
                if (b == 0x1BU) {
                    esc_state_ = ESC_GOT_ESC;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else {
                    char ch = static_cast<char>(b);
                    reset_escape();
                    process_ss3_final(ch);
                }
                break;
        }
    }
}
