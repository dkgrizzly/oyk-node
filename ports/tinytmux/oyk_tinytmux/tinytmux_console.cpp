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

static const char* cycle_iface_type(const char* cur) {
    static const char* TYPES[7] = {
        "axudp", "axip", "ppp", "kiss-serial", "usb-cdc-net", "usb-cdc-serial", "telnet"
    };
    for (int i = 0; i < 7; ++i) {
        if (strieq_ci(cur, TYPES[i])) return TYPES[(i + 1) % 7];
    }
    return TYPES[0];
}

TinyTmuxConsole::TinyTmuxConsole()
    : node_(NULL), bridge_(NULL), tx_fn_(NULL), tx_ctx_(NULL), now_ms_(0),
      view_(TMUX_VIEW_CONSOLE), menu_sel_(0),
      files_tab_(0), pending_count_(0), approved_count_(0),
      settings_tab_(0), settings_iface_field_(0),
      prompt_kind_(PROMPT_NONE), prompt_len_(0),
      lterm_sid_(0), lterm_is_local_(0),
      lterm_line_count_(0), lterm_curline_len_(0), lterm_input_len_(0),
      esc_state_(ESC_IDLE), esc_len_(0), esc_start_ms_(0),
      last_marchc_redraw_ms_(0),
      bootloader_pending_(0), bootloader_start_ms_(0) {
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
    new_iface_id_tmp_[0] = '\0';
    memset(lterm_lines_, 0, sizeof(lterm_lines_));
    lterm_curline_[0] = '\0';
    lterm_input_[0] = '\0';
}

void TinyTmuxConsole::init(OykPortableNode* node, TinyTmuxConsoleTxFn tx_fn, void* tx_ctx) {
    node_ = node;
    tx_fn_ = tx_fn;
    tx_ctx_ = tx_ctx;
    view_ = TMUX_VIEW_CONSOLE;
    menu_sel_ = 0;
    files_tab_ = 0;
    files_sel_[0] = 0;
    files_sel_[1] = 0;
    prompt_kind_ = PROMPT_NONE;
    prompt_len_ = 0;
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
        case TMUX_VIEW_MENU:
            render_console();
            render_menu();
            break;
        default:
            render_console();
            break;
    }
    if (prompt_kind_ != PROMPT_NONE) {
        render_prompt_overlay();
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
    sprintf(buf, "\033[7m Node (M) | Settings (S) | Files (F) | Radio (R) | %-26s \033[0m\r\n",
            lterm_sid_ ? "Tab:Term(ACTIVE)" : "Tab:Term");
    write_str(buf);

    /* Row 2: Header Bar with PSRAM and USBH hardware indicators */
    sprintf(buf, "\033[1;36mOYK TMUX: %-9s Up:%lus C:%d [IN:%s] PSRAM:%-3s USBH:%-3s\033[0m\r\n",
            call_s,
            static_cast<unsigned long>(node_->uptime_seconds()),
            node_->total_active_connections(),
            node_->accept_incoming() ? "ALLOW" : "DENY",
            psram_s,
            usbh_s);
    write_str(buf);

    /* Interfaces & Last 5 New Callsigns Heard */
    write_str("Port     Type    Conn Status             RX/TX  | Last 5 New Callsigns\r\n");
    for (int i = 0; i < 5; ++i) {
        char left[80], right[40];
        if (i < node_->iface_count()) {
            const OykIfaceRuntime& r = node_->iface(i);
            sprintf(left, "%-8.8s %-7.7s %-4d %-18.18s %lu/%lu",
                    r.cfg.id, r.cfg.type, node_->active_connections_on(i), r.status,
                    static_cast<unsigned long>(r.rx_frames),
                    static_cast<unsigned long>(r.tx_frames));
        } else {
            left[0] = '\0';
        }
        if (i < node_->new_heard_count()) {
            const OykNewHeard& nh = node_->new_heard(i);
            sprintf(right, "%d.%-9.9s %-7.7s %s", i + 1, nh.callsign, nh.iface_id, nh.time_hms);
        } else {
            right[0] = '\0';
        }
        sprintf(buf, "%-47.47s | %-28.28s\r\n", left, right);
        write_str(buf);
    }

    /* Active Connections */
    sprintf(buf, "\033[1mActive Connections (%d) | Bans: %d\033[0m\r\n",
            node_->total_active_connections(), node_->ban_count());
    write_str(buf);
    int shown = 0;
    for (int i = 0; i < node_->max_sessions() && shown < 5; ++i) {
        const OykSession& s = node_->session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            const char* role = s.restricted_ban ? "BAN NOTICE"
                                                : (s.in_bbs ? "BBS"
                                                            : (s.in_chat ? "CHAT"
                                                                         : (s.is_local_term ? "LOCAL TERM" : "NODE SHELL")));
            sprintf(buf, "#%-3lu %-10.10s %-8.8s %-8.8s %s\r\n",
                    static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local",
                    s.since_hms, role);
            write_str(buf);
            ++shown;
        }
    }
    if (shown == 0) {
        write_str("(no active connections)\r\n");
    }

    /* Recent Activity Log */
    write_str("\033[1mNode Activity Log:\033[0m\r\n");
    int total_logs = node_->console_log_count();
    int start = (total_logs > 5) ? (total_logs - 5) : 0;
    for (int i = start; i < total_logs; ++i) {
        sprintf(buf, "%.78s\r\n", node_->console_log_line(i));
        write_str(buf);
    }

    /* Bottom Status Line */
    sprintf(buf, "\033[7m %-78.78s \033[0m\r\n", status_line_);
    write_str(buf);
}

void TinyTmuxConsole::render_menu() {
    static const char* ITEMS[11] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [L] Local Terminal...          ",
        " [F] Manage Files Area...       ",
        " [R] Radio Control...           ",
        " [S] Settings...                ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [P] PSRAM Diagnostics...       ",
        " [U] Enter HalfKay Bootloader   ",
        " [X] Close Menu                 "
    };
    static const int ITEM_ROWS[11] = { 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 15 };

    write_str("\033[2;3H+--------------------------------+");
    write_str("\033[5;3H+--------------------------------+");
    write_str("\033[14;3H+--------------------------------+");
    write_str("\033[16;3H+--------------------------------+");
    for (int i = 0; i < 11; ++i) {
        char row[80];
        sprintf(row, "\033[%d;3H|%s%.32s\033[0m|",
                ITEM_ROWS[i], (i == menu_sel_) ? "\033[7m" : "", ITEMS[i]);
        write_str(row);
    }
}

void TinyTmuxConsole::render_local_terminal() {
    char buf[160];
    write_str("\033[H\033[2J");
    sprintf(buf, "\033[7m OYK LOCAL TERMINAL (%s) | Esc:Console  /local /bbs /chat /disc \033[0m\r\n",
            lterm_sid_ ? "CONNECTED" : "DISCONNECTED");
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
    char buf[160];
    write_str("\033[H\033[2J");
    write_str("\033[7m FILES AREA MANAGER (TinyTMUX)                       Esc:Back  R:Refresh \033[0m\r\n");
    sprintf(buf, " %s 1:Pending Incoming (%d) \033[0m  %s 2:Approved Files (%d) \033[0m\r\n",
            (files_tab_ == 0) ? "\033[7m" : "", pending_count_,
            (files_tab_ == 1) ? "\033[7m" : "", approved_count_);
    write_str(buf);
    write_str("------------------------------------------------------------------------------\r\n");

    if (files_tab_ == 0) {
        write_str(" Uploader        Filename                         Size\r\n");
        if (pending_count_ == 0) {
            write_str(" (no pending uploads in incoming/)\r\n");
        } else {
            int max_show = (pending_count_ < 12) ? pending_count_ : 12;
            for (int i = 0; i < max_show; ++i) {
                sprintf(buf, "%s %-15.15s %-32.32s %lu \033[0m\r\n",
                        (i == files_sel_[0]) ? "\033[7m" : "",
                        pending_files_[i].uploader,
                        pending_files_[i].filename,
                        static_cast<unsigned long>(pending_files_[i].size));
                write_str(buf);
            }
        }
        write_str("\r\n\033[7m Tab/1-2:Tabs  Enter/A:Approve  D:Delete  R:Refresh  Esc:Close \033[0m\r\n");
    } else {
        write_str(" Approved File Path                                           Size\r\n");
        if (approved_count_ == 0) {
            write_str(" (no approved files in BBS files area)\r\n");
        } else {
            int max_show = (approved_count_ < 12) ? approved_count_ : 12;
            for (int i = 0; i < max_show; ++i) {
                sprintf(buf, "%s %-60.60s %lu \033[0m\r\n",
                        (i == files_sel_[1]) ? "\033[7m" : "",
                        approved_files_[i].rel_path,
                        static_cast<unsigned long>(approved_files_[i].size));
                write_str(buf);
            }
        }
        write_str("\r\n\033[7m Tab/1-2:Tabs  D:Remove Approved  R:Refresh  Esc:Close \033[0m\r\n");
    }
    sprintf(buf, " Status: %.70s\r\n", dialog_fb_[0] ? dialog_fb_ : "Ready.");
    write_str(buf);
}

void TinyTmuxConsole::render_rig() {
    char buf[160];
    write_str("\033[H\033[2J");
    write_str("\033[7m RADIO CONTROL (Hamlib rigctl / RadioFoundry UDP 4191)      Esc:Close \033[0m\r\n");

    const OykConfig& cfg = node_->config();
    if (!cfg.rig_enabled) {
        write_str("\r\n Rig control is DISABLED. Configure in Settings (S) -> Tab 5:Rig.\r\n");
    } else {
        OykRigStatus st;
        oyk_rig_status_init(&st, oyk_rig_parse_protocol(cfg.rig_protocol));
        if (bridge_) st = *bridge_->rig_status();

        sprintf(buf, " Protocol : %-14s  Target: %s:%u  Notify: %s\r\n",
                oyk_rig_protocol_name(st.protocol),
                cfg.rig_host[0] ? cfg.rig_host : "(peer)",
                static_cast<unsigned int>(cfg.rig_port ? cfg.rig_port : oyk_rig_default_port(st.protocol)),
                cfg.rig_notify ? "ON" : "OFF");
        write_str(buf);
        sprintf(buf, " Status   : %s\r\n", st.reachable ? "REACHABLE" : "NO RESPONSE / IDLE");
        write_str(buf);

        char summary[128];
        oyk_rig_format_summary(&st, summary, sizeof(summary));
        sprintf(buf, " Radio    : %.66s\r\n\r\n", summary);
        write_str(buf);

        if (st.protocol == OYK_RIG_PROTO_RADIOFOUNDRY) {
            write_str(" Commands (RadioFoundry / takRF UDP 4191):\r\n");
            write_str("   [J/K] Step Channel -/+1    [H/L] Step Zone -/+1\r\n");
            write_str("   [S]   Select Zone:Pos      [N]   Stop Notify     [R] Poll Now\r\n");
        } else {
            write_str(" Commands (Hamlib rigctld TCP 4532):\r\n");
            write_str("   [F] Set Frequency (Hz)     [M] Set Mode          [P] Toggle PTT\r\n");
            write_str("   [V] Set VFO (VFOA/VFOB)    [R] Poll Status\r\n");
        }
    }
    sprintf(buf, "\r\n Feedback: %.68s\r\n", dialog_fb_[0] ? dialog_fb_ : "Ready.");
    write_str(buf);
}

int TinyTmuxConsole::settings_tab_item_count(int tab) const {
    if (tab == 0) return 6;
    if (tab == 1) return 8;
    if (tab == 2) return 6;
    if (tab == 3) return draft_.iface_count;
    return 6;
}

OYK_FLASHMEM void TinyTmuxConsole::render_settings() {
    static const char* TAB_NAMES[5] = {
        "1:Node", "2:BBS", "3:CHAT", "4:Interfaces", "5:Rig"
    };
    static const char* NODE_LABELS[6] = {
        "Node Callsign", "Node Alias", "Welcome Message", "Info Message",
        "Allow Incoming (Default)", "Allow Logging to Flash"
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

    char buf[160];
    write_str("\033[H\033[2J");
    write_str("\033[7m OYK NODE SETTINGS (TinyTMUX)            F2/S:Save & Apply  Esc:Cancel \033[0m\r\n ");
    for (int t = 0; t < 5; ++t) {
        sprintf(buf, "%s %s \033[0m ", (t == settings_tab_) ? "\033[7m" : "", TAB_NAMES[t]);
        write_str(buf);
    }
    write_str("\r\n------------------------------------------------------------------------------\r\n");

    int cnt = settings_tab_item_count(settings_tab_);
    if (settings_tab_ != 3) {
        const char** labels = (settings_tab_ == 0) ? NODE_LABELS
                            : ((settings_tab_ == 1) ? BBS_LABELS
                                                    : ((settings_tab_ == 2) ? CHAT_LABELS : RIG_LABELS));
        for (int i = 0; i < cnt; ++i) {
            char val[56], cs[16];
            val[0] = '\0';
            if (settings_tab_ == 0) {
                if (i == 0) oyk_format_callsign(&draft_.callsign, val, sizeof(val));
                else if (i == 1) { if (draft_.alias.base[0]) oyk_format_callsign(&draft_.alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 2) { strncpy(val, draft_.welcome_text, 48); val[48] = '\0'; }
                else if (i == 3) { strncpy(val, draft_.info_text, 48); val[48] = '\0'; }
                else if (i == 4) strcpy(val, draft_.accept_incoming ? "[ON]" : "[OFF]");
                else strcpy(val, draft_.allow_flash_logging ? "[ON]" : "[OFF]");
            } else if (settings_tab_ == 1) {
                if (i == 0) strcpy(val, draft_.bbs_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign bc; oyk_config_resolved_bbs_callsign(&draft_, &bc); oyk_format_callsign(&bc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.bbs_ssid), cs); }
                else if (i == 2) { if (draft_.bbs_callsign.base[0]) oyk_format_callsign(&draft_.bbs_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.bbs_alias.base[0]) oyk_format_callsign(&draft_.bbs_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) strcpy(val, draft_.bbs_files_enabled ? "[ON]" : "[OFF]");
                else if (i == 5) { strncpy(val, draft_.bbs_files_path, 48); val[48] = '\0'; }
                else if (i == 6) { strncpy(val, draft_.bbs_mail_path, 48); val[48] = '\0'; }
                else { strncpy(val, draft_.bbs_welcome, 48); val[48] = '\0'; }
            } else if (settings_tab_ == 2) {
                if (i == 0) strcpy(val, draft_.chat_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign cc; oyk_config_resolved_chat_callsign(&draft_, &cc); oyk_format_callsign(&cc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.chat_ssid), cs); }
                else if (i == 2) { if (draft_.chat_callsign.base[0]) oyk_format_callsign(&draft_.chat_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.chat_alias.base[0]) oyk_format_callsign(&draft_.chat_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) { strncpy(val, draft_.chat_topic, 48); val[48] = '\0'; }
                else { strncpy(val, draft_.chat_welcome, 48); val[48] = '\0'; }
            } else {
                if (i == 0) strcpy(val, draft_.rig_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { strncpy(val, draft_.rig_protocol[0] ? draft_.rig_protocol : "none", 48); val[48] = '\0'; }
                else if (i == 2) { strncpy(val, draft_.rig_host, 48); val[48] = '\0'; }
                else if (i == 3) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_port));
                else if (i == 4) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_poll_sec));
                else strcpy(val, draft_.rig_notify ? "[ON]" : "[OFF]");
            }
            sprintf(buf, "%s %-29.29s : %-44.44s \033[0m\r\n",
                    (i == settings_sel_[settings_tab_]) ? "\033[7m" : "", labels[i], val);
            write_str(buf);
        }
        write_str("\r\n\033[7m Tab/1-5:Tabs  Up/Dn:Select  Enter/E:Edit  S/F2:Save & Apply  Esc:Cancel \033[0m\r\n");
    } else {
        write_str(" ID       Type            Dev/Host             Port/Baud\r\n");
        if (draft_.iface_count == 0) {
            write_str(" (no interfaces configured; press N to add)\r\n");
        } else {
            for (int i = 0; i < draft_.iface_count && i < 8; ++i) {
                const OykIfaceConfig& ic = draft_.ifaces[i];
                const char* loc = ic.device[0] ? ic.device : ic.host;
                sprintf(buf, "%s %-8.8s %-15.15s %-20.20s %u/%lu \033[0m\r\n",
                        (i == settings_sel_[3]) ? "\033[7m" : "",
                        ic.id, ic.type, loc,
                        static_cast<unsigned int>(ic.port),
                        static_cast<unsigned long>(ic.baud));
                write_str(buf);
            }
        }
        write_str("\r\n\033[7m N:New  D:Delete  Enter:Cycle Type  H:Edit Host/Dev  S/F2:Save  Esc:Cancel \033[0m\r\n");
    }
    sprintf(buf, " %.78s\r\n", dialog_fb_[0] ? dialog_fb_ : "Edit settings, then press S or F2 to Save & Apply.");
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
    write_str("\033[7m TEENSY 4.1 FLEXSPI2 PSRAM DIAGNOSTICS   T:Quick  M:MarchC-  Esc:Back \033[0m\r\n");
    sprintf(buf, " Status      : %-6s  Base: 0x%08lX  Quick test: %lu ms\r\n",
            tag,
            static_cast<unsigned long>(info->base_addr),
            static_cast<unsigned long>(info->test_time_ms));
    write_str(buf);
    sprintf(buf, " Capacity    : %lu MB usable / %lu MB detected (%d/%d chips passed)\r\n\r\n",
            static_cast<unsigned long>(info->usable_bytes / (1024UL * 1024UL)),
            static_cast<unsigned long>(info->detected_bytes / (1024UL * 1024UL)),
            info->usable_chip_count, info->chip_count);
    write_str(buf);

    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        const TinyTmuxPsramChipInfo& c = info->chips[i];
        const char* kind_str = "EMPTY";
        if (c.chip_kind == TMUX_FLEXSPI_PSRAM) kind_str = "PSRAM";
        else if (c.chip_kind == TMUX_FLEXSPI_FLASH_NOR) kind_str = "QSPI NOR";
        else if (c.chip_kind == TMUX_FLEXSPI_FLASH_NAND) kind_str = "QSPI NAND";
        else if (c.detected) kind_str = "DETECTED";
        sprintf(buf, " Chip %d (CS%d @ 0x%08lX): %-9s  %s  %lu MB  JEDEC=%02X %02X %02X\r\n",
                i, i,
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
            sprintf(buf, "   March C-: %s %3u%%  %-16s\r\n",
                    bar, static_cast<unsigned int>(mc->chip_percent[i]), res_s);
            write_str(buf);
        }
        if (c.detected && c.chip_kind == TMUX_FLEXSPI_PSRAM && !c.passed) {
            sprintf(buf, "   Fault @ 0x%08lX  Check footprint: %s\r\n",
                    static_cast<unsigned long>(c.fail_addr),
                    tinytmux_psram_footprint_label(i));
            write_str(buf);
        }
    }

    if (info->misplaced_flash_cs0) {
        write_str("\r\n \033[7m WARNING: QSPI FLASH ON WRONG PADS (CS0 / Pin 48 small footprint)! \033[0m\r\n");
        write_str(" Teensy 4.1 LittleFS_QSPI only supports CS1 (large pads / Pin 51).\r\n");
        write_str(" Startup CS0 probes PSRAM first; move flash to CS1 to use QSPI storage.\r\n");
    }

    write_str("\r\n");
    if (mc->state == TMUX_MARCHC_RUNNING) {
        sprintf(buf, " March C- RUNNING: Chip %d (%s)  Elapsed: %lu ms  (Esc: Cancel)\r\n",
                mc->active_chip, mc->pass_label, static_cast<unsigned long>(mc->elapsed_ms));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_PASSED) {
        sprintf(buf, " March C- PASSED cleanly on all detected chips in %lu ms.\r\n",
                static_cast<unsigned long>(mc->elapsed_ms));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_FAILED) {
        sprintf(buf, " \033[7m MARCH C- FAIL: Chip %d @ 0x%08lX (exp %08lX got %08lX) \033[0m\r\n",
                mc->fail_chip,
                static_cast<unsigned long>(mc->fail_addr),
                static_cast<unsigned long>(mc->fail_expected),
                static_cast<unsigned long>(mc->fail_actual));
        write_str(buf);
        sprintf(buf, " Check/resolder footprint: %s\r\n", tinytmux_psram_footprint_label(mc->fail_chip));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_CANCELLED) {
        write_str(" March C- test cancelled by operator.\r\n");
    } else {
        write_str(" Press T for quick self-test, M for full March C- test, Esc to return.\r\n");
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_bootloader() {
    char buf[160];
    int active = node_ ? node_->total_active_connections() : 0;
    int disc_left = node_ ? node_->disconnecting_count() : 0;
    write_str("\033[H\033[2J");
    write_str("\033[7m ENTER TEENSY 4.1 HALFKAY BOOTLOADER (FIRMWARE UPGRADE)       Esc:Cancel \033[0m\r\n\r\n");
    if (!bootloader_pending_) {
        if (active > 0) {
            sprintf(buf, " Enter HalfKay bootloader? (%d active connection(s))\r\n\r\n", active);
            write_str(buf);
            write_str(" Remote users will be notified that the node is going offline for a\r\n");
            write_str(" firmware upgrade and gracefully disconnected before rebooting.\r\n\r\n");
        } else {
            write_str(" Enter HalfKay bootloader? (no active connections)\r\n\r\n");
        }
        write_str(" \033[7m WARNING: Node will stay OFFLINE in HalfKay bootloader mode \033[0m\r\n");
        write_str(" until you flash new firmware or power-cycle the Teensy 4.1 board.\r\n\r\n");
        write_str("   [Y] Enter HalfKay Bootloader     [N / Esc] Cancel\r\n");
    } else {
        oyk_u32 elapsed = (now_ms_ >= bootloader_start_ms_) ? (now_ms_ - bootloader_start_ms_) : 0UL;
        sprintf(buf, " Notifying remote users & draining AX.25 disconnects (%d remaining, %lu/3000 ms)...\r\n",
                disc_left, static_cast<unsigned long>(elapsed));
        write_str(buf);
    }
}

void TinyTmuxConsole::render_prompt_overlay() {
    char buf[192];
    sprintf(buf, "\033[22;1H\033[7m %-78.78s \033[0m\r\n\033[7m > %-75.75s \033[0m",
            prompt_label_, prompt_buf_);
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::open_settings() {
    if (node_) draft_ = node_->config();
    settings_tab_ = 0;
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
        } else {
            draft_.allow_flash_logging = draft_.allow_flash_logging ? 0 : 1;
            render();
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
            con_str_copy(draft_.ifaces[idx].type, sizeof(draft_.ifaces[idx].type),
                         cycle_iface_type(draft_.ifaces[idx].type));
            render();
        }
    } else {
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

    if (pk == PROMPT_SETTINGS_VALUE) {
        int idx = settings_sel_[settings_tab_];
        if (settings_tab_ == 0) {
            if (idx == 0) oyk_parse_callsign(val, &draft_.callsign);
            else if (idx == 1) {
                if (!val[0]) memset(&draft_.alias, 0, sizeof(draft_.alias));
                else oyk_parse_callsign(val, &draft_.alias);
            } else if (idx == 2) con_str_copy(draft_.welcome_text, sizeof(draft_.welcome_text), val);
            else if (idx == 3) con_str_copy(draft_.info_text, sizeof(draft_.info_text), val);
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
        } else if (settings_tab_ == 4) {
            if (idx == 2) con_str_copy(draft_.rig_host, sizeof(draft_.rig_host), val);
            else if (idx == 3) { long p = strtol(val, NULL, 10); if (p > 0 && p <= 65535L) draft_.rig_port = static_cast<oyk_u16>(p); }
            else if (idx == 4) { long p = strtol(val, NULL, 10); if (p >= 0 && p <= 3600L) draft_.rig_poll_sec = static_cast<oyk_u16>(p); }
        }
        render();
        return;
    }

    if (pk == PROMPT_SETTINGS_NEW_IFACE_ID) {
        if (!val[0]) { render(); return; }
        con_str_copy(new_iface_id_tmp_, sizeof(new_iface_id_tmp_), val);
        begin_prompt(PROMPT_SETTINGS_NEW_IFACE_TYPE,
                     "Type (axudp/axip/ppp/kiss-serial/usb-cdc-net/usb-cdc-serial):",
                     "usb-cdc-net");
        return;
    }

    if (pk == PROMPT_SETTINGS_NEW_IFACE_TYPE) {
        if (draft_.iface_count < OYK_MAX_INTERFACES) {
            OykIfaceConfig& ic = draft_.ifaces[draft_.iface_count];
            memset(&ic, 0, sizeof(ic));
            con_str_copy(ic.id, sizeof(ic.id), new_iface_id_tmp_);
            con_str_copy(ic.type, sizeof(ic.type), val[0] ? val : "axudp");
            if (strieq_ci(ic.type, "usb-cdc-net") || strieq_ci(ic.type, "usb-cdc-serial")) {
                strcpy(ic.device, "usbh0");
                strcpy(ic.host, "192.168.7.2");
                ic.port = 10093;
                ic.baud = 115200UL;
            } else if (strieq_ci(ic.type, "ppp") || strieq_ci(ic.type, "kiss-serial")) {
                strcpy(ic.device, "ser0");
                ic.baud = strieq_ci(ic.type, "ppp") ? 115200UL : 9600UL;
                if (strieq_ci(ic.type, "ppp")) ic.port = 10093;
            } else {
                strcpy(ic.host, "0.0.0.0");
                ic.port = strieq_ci(ic.type, "telnet") ? 8023U : 10093U;
            }
            settings_sel_[3] = draft_.iface_count;
            draft_.iface_count++;
            strcpy(dialog_fb_, "Interface added to draft.");
        } else {
            strcpy(dialog_fb_, "Maximum interface slots reached.");
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

void TinyTmuxConsole::handle_prompt_key(int key) {
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
        open_settings();
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
    } else if (key == 0x0C) {
        render();
    }
}

void TinyTmuxConsole::handle_menu_key(int key) {
    if (key == 27 || key == TMUX_KEY_F10 || key == 'x' || key == 'X') {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == TMUX_KEY_UP) {
        menu_sel_ = (menu_sel_ + 10) % 11;
        render();
    } else if (key == TMUX_KEY_DOWN) {
        menu_sel_ = (menu_sel_ + 1) % 11;
        render();
    } else if (key == TMUX_KEY_HOME) {
        menu_sel_ = 0;
        render();
    } else if (key == TMUX_KEY_END) {
        menu_sel_ = 10;
        render();
    } else if (key == '\r' || key == '\n') {
        static const int KEYS[11] = { 'a', 'd', 'l', 'f', 'r', 's', 't', 'b', 'p', 'u', 'x' };
        int k = KEYS[menu_sel_];
        view_ = TMUX_VIEW_CONSOLE;
        if (k == 'x') render();
        else handle_console_key(k);
    } else if (key == 'a' || key == 'A' || key == 'd' || key == 'D' ||
               key == 'l' || key == 'L' || key == 'f' || key == 'F' ||
               key == 'r' || key == 'R' || key == 's' || key == 'S' ||
               key == 't' || key == 'T' || key == 'b' || key == 'B' ||
               key == 'p' || key == 'P' || key == 'u' || key == 'U') {
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
        settings_tab_ = (settings_tab_ + 1) % 5;
        render();
    } else if (key == TMUX_KEY_LEFT) {
        settings_tab_ = (settings_tab_ + 4) % 5;
        render();
    } else if (key >= '1' && key <= '5') {
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
    } else if (settings_tab_ == 3 && (key == 'n' || key == 'N')) {
        begin_prompt(PROMPT_SETTINGS_NEW_IFACE_ID, "New Interface ID (e.g. rfusb0):", "");
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
    if (key == 27 || key == 'n' || key == 'N') {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == 'y' || key == 'Y') {
        char call_s[16], notice[128];
        oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
        sprintf(notice, "\r*** Node %s going offline for firmware upgrade. Expect a delay before reconnect.\r", call_s);
        node_->begin_shutdown_with_notice(notice);
        bootloader_pending_ = 1;
        bootloader_start_ms_ = now_ms_;
        render();
    }
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
                    if (esc_len_ < 15) {
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
