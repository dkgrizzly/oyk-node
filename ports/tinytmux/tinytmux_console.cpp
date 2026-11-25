/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_console.hpp"

#include <stdio.h>
#include <string.h>

TinyTmuxConsole::TinyTmuxConsole()
    : node_(NULL), tx_fn_(NULL), tx_ctx_(NULL),
      view_(TMUX_VIEW_CONSOLE), menu_sel_(0),
      lterm_sid_(0), lterm_is_local_(0),
      lterm_line_count_(0), lterm_curline_len_(0), lterm_input_len_(0) {
    strcpy(status_line_, "TinyTMUX Ready. M:Menu  Tab:Terminal  A/D:Incoming  F:Files");
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
    lterm_sid_ = 0;
    lterm_is_local_ = 0;
    lterm_line_count_ = 0;
    lterm_curline_len_ = 0;
    lterm_input_len_ = 0;
    push_lterm_line("*** OYK TinyTMUX Local Terminal (Esc:Console, /local, /bbs, /chat, /disc)");
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
    if (view_ == TMUX_VIEW_LOCAL_TERM) {
        render_local_terminal();
    } else if (view_ == TMUX_VIEW_MENU) {
        render_console();
        render_menu();
    } else {
        render_console();
    }
}

void TinyTmuxConsole::render_console() {
    char buf[160];
    char call_s[16];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));

    /* Clear screen & home cursor (VT100) */
    write_str("\033[H\033[2J");

    /* Row 1: Top Menu Bar */
    sprintf(buf, "\033[7m Node (M) %-34s %s \033[0m\r\n",
            "",
            lterm_sid_ ? "Tab:Term(ACTIVE) | M:Menu" : "Tab:Term | M:Menu");
    write_str(buf);

    /* Row 2: Header Bar */
    sprintf(buf, "\033[1;36mOYK PACKET NODE (TinyTMUX IMXRT1060): %-9s | Up:%lus | Conn:%d | [%s]\033[0m\r\n",
            call_s,
            static_cast<unsigned long>(node_->uptime_seconds()),
            node_->total_active_connections(),
            node_->accept_incoming() ? "INCOMING: ALLOW" : "INCOMING: DENY");
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
    static const char* ITEMS[5] = {
        "[A] Allow Incoming Connections",
        "[D] Deny Incoming Connections ",
        "[L] Local Terminal...         ",
        "[F] Approve Incoming Files... ",
        "[Esc] Close Menu              "
    };
    write_str("\033[3;3H+----------------------------------+");
    for (int i = 0; i < 5; ++i) {
        char row[64];
        sprintf(row, "\033[%d;3H| %s%s\033[0m |",
                4 + i, (i == menu_sel_) ? "\033[7m" : "", ITEMS[i]);
        write_str(row);
    }
    write_str("\033[9;3H+----------------------------------+");
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

void TinyTmuxConsole::handle_console_key(int ch) {
    if (ch == 'm' || ch == 'M') {
        view_ = TMUX_VIEW_MENU;
        menu_sel_ = 0;
        render();
    } else if (ch == '\t' || ch == 'l' || ch == 'L') {
        view_ = TMUX_VIEW_LOCAL_TERM;
        render();
    } else if (ch == 'a' || ch == 'A') {
        node_->set_accept_incoming(1);
        strcpy(status_line_, "New incoming connections are now ALLOWED.");
        render();
    } else if (ch == 'd' || ch == 'D') {
        node_->set_accept_incoming(0);
        strcpy(status_line_, "New incoming connections are now DENIED.");
        render();
    } else if (ch == 'f' || ch == 'F') {
        OykPendingIncoming items[8];
        int n = oyk_bbs_list_incoming("incoming", items, 8);
        if (n == 0) {
            strcpy(status_line_, "No pending files in incoming/ to approve.");
        } else {
            char dest[128], err[64];
            int exists = 0;
            if (oyk_bbs_approve_incoming_file("incoming", node_->config().bbs_files_path,
                                              items[0].uploader, items[0].filename,
                                              "", 0, dest, sizeof(dest),
                                              &exists, err, sizeof(err))) {
                sprintf(status_line_, "Approved %s/%s -> BBS files area",
                        items[0].uploader, items[0].filename);
                node_->log_message(OYK_LOG_INFO, status_line_);
            } else {
                sprintf(status_line_, "Approval: %.64s", err);
            }
        }
        render();
    } else if (ch == 0x0C) { /* Ctrl-L */
        render();
    }
}

void TinyTmuxConsole::handle_menu_key(int ch) {
    if (ch == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (ch == 'a' || ch == 'A') {
        view_ = TMUX_VIEW_CONSOLE;
        handle_console_key('a');
    } else if (ch == 'd' || ch == 'D') {
        view_ = TMUX_VIEW_CONSOLE;
        handle_console_key('d');
    } else if (ch == 'l' || ch == 'L') {
        view_ = TMUX_VIEW_LOCAL_TERM;
        render();
    } else if (ch == 'f' || ch == 'F') {
        view_ = TMUX_VIEW_CONSOLE;
        handle_console_key('f');
    }
}

void TinyTmuxConsole::handle_lterm_key(int ch) {
    if (ch == 27) {
        /* Switch back to Operator Console WITHOUT closing active session */
        view_ = TMUX_VIEW_CONSOLE;
        strcpy(status_line_, lterm_sid_
                                 ? "Console (Local Terminal session still active; press Tab to return)"
                                 : "Returned to Operator Console.");
        render();
        return;
    }
    if (ch == '\r' || ch == '\n') {
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
    if ((ch == 8 || ch == 127) && lterm_input_len_ > 0) {
        lterm_input_[--lterm_input_len_] = '\0';
        render();
    } else if (ch >= 0x20 && ch < 0x7F && lterm_input_len_ < 120) {
        lterm_input_[lterm_input_len_++] = static_cast<char>(ch);
        lterm_input_[lterm_input_len_] = '\0';
    }
}

void TinyTmuxConsole::feed_input(const oyk_u8* data, size_t len) {
    if (!node_ || !data) return;
    for (size_t i = 0; i < len; ++i) {
        int ch = data[i];
        if (view_ == TMUX_VIEW_CONSOLE) {
            handle_console_key(ch);
        } else if (view_ == TMUX_VIEW_MENU) {
            handle_menu_key(ch);
        } else if (view_ == TMUX_VIEW_LOCAL_TERM) {
            handle_lterm_key(ch);
        }
    }
}
