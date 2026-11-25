/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_CONSOLE_HPP
#define OYK_TINYTMUX_CONSOLE_HPP

/*
 * ANSI/VT100 80x24 Local Operator Console for Teensy 4.1 / TinyTMUX.
 * Runs over TinyUSB CDC0 (usb0) or any hardware UART terminal (ser0..ser7).
 *
 * Matches the layout of the Linux ncurses and MS-DOS consoles:
 *   - Top Menu Bar (F10/M: Node Menu, Tab/F9: Local Terminal)
 *   - Header Bar (Node Callsign, Uptime, Connections, [INCOMING: ALLOW/DENY])
 *   - Interfaces (Ethernet + PPP RadioFoundry + KISS) & Last 5 New Callsigns Heard
 *   - Active Connections (NODE SHELL / BBS / CHAT / LOCAL TERM / BAN NOTICE)
 *   - Node Activity Log & Status Bar
 *   - Switchable Local Terminal (Tab/F9) without closing active session
 *   - Approve Incoming Files (F)
 */

#include "../compat/oyk_portable.hpp"

typedef size_t (*TinyTmuxConsoleTxFn)(const char* data, size_t len, void* ctx);

enum TinyTmuxViewMode {
    TMUX_VIEW_CONSOLE = 0,
    TMUX_VIEW_MENU,
    TMUX_VIEW_LOCAL_TERM
};

class TinyTmuxConsole {
public:
    TinyTmuxConsole();

    void init(OykPortableNode* node, TinyTmuxConsoleTxFn tx_fn, void* tx_ctx);

    /* Render current screen as ANSI/VT100 escape sequences */
    void render();

    /* Process incoming keystrokes from USB-CDC0 or UART terminal */
    void feed_input(const oyk_u8* data, size_t len);

    /* Local terminal RX delivery hook from OykPortableNode */
    void on_local_term_event(oyk_u32 sid, int event_type,
                             const oyk_u8* data, size_t len,
                             const char* reason);

    TinyTmuxViewMode view_mode() const { return view_; }
    int has_active_terminal() const { return (lterm_sid_ != 0) ? 1 : 0; }
    const char* status_line() const { return status_line_; }

private:
    void write_str(const char* s);
    void render_console();
    void render_menu();
    void render_local_terminal();
    void handle_console_key(int ch);
    void handle_menu_key(int ch);
    void handle_lterm_key(int ch);
    void push_lterm_line(const char* line);

    OykPortableNode* node_;
    TinyTmuxConsoleTxFn tx_fn_;
    void* tx_ctx_;

    TinyTmuxViewMode view_;
    int menu_sel_;
    char status_line_[80];

    /* Persistent switchable local terminal state */
    oyk_u32 lterm_sid_;
    int lterm_is_local_;
    char lterm_lines_[16][80];
    int lterm_line_count_;
    char lterm_curline_[80];
    int lterm_curline_len_;
    char lterm_input_[128];
    int lterm_input_len_;
};

#endif /* OYK_TINYTMUX_CONSOLE_HPP */
