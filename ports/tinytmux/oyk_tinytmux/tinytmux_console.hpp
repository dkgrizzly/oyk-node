/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_CONSOLE_HPP
#define OYK_TINYTMUX_CONSOLE_HPP

/*
 * ANSI/VT100 80x24 Local Operator Console for Teensy 4.1 / TinyTMUX.
 * Runs over TinyUSB CDC0 (usb0) or any hardware UART terminal (ser0..ser7).
 *
 * Full cross-port parity layout (AGENTS.md Section 5):
 *   - Top Menu Bar & Header Bar with PSRAM:<16M|8M|NO|ERR> and USBH:<NET|SER|N+S|->
 *   - Node Menu (M/F10):
 *       [A] Allow Incoming Connections
 *       [D] Deny Incoming Connections
 *       [L] Local Terminal...
 *       [F] Manage Files Area...        (2 tabs: Pending Incoming / Approved Files)
 *       [R] Radio Control...            (Hamlib rigctl TCP + RadioFoundry UDP 4191)
 *       [S] Settings...                 (5 tabs: Node | BBS | CHAT | Interfaces | Rig)
 *       [T] Terminate Connection...
 *       [B] Ban / Unban Callsign...
 *       [P] PSRAM Diagnostics / Re-Test...
 *       [X] Close Menu
 */

#include "oyk_tinytmux_config.h"
#include "oyk_portable.hpp"
#include "tinytmux_psram.hpp"
#include "lwip_net_bridge.hpp"

typedef size_t (*TinyTmuxConsoleTxFn)(const char* data, size_t len, void* ctx);

enum TinyTmuxViewMode {
    TMUX_VIEW_CONSOLE = 0,
    TMUX_VIEW_MENU,
    TMUX_VIEW_LOCAL_TERM,
    TMUX_VIEW_FILES,
    TMUX_VIEW_RIG,
    TMUX_VIEW_SETTINGS,
    TMUX_VIEW_PSRAM,
    TMUX_VIEW_BOOTLOADER,
    TMUX_VIEW_STORAGE
};

class TinyTmuxConsole {
public:
    TinyTmuxConsole();

    void init(OykPortableNode* node, TinyTmuxConsoleTxFn tx_fn, void* tx_ctx);
    void set_net_bridge(TinyTmuxNetBridge* bridge);
    void set_now_ms(oyk_u32 now_ms) { now_ms_ = now_ms; }
    void tick(oyk_u32 now_ms);

    /* Render current screen as ANSI/VT100 escape sequences */
    void render();

    /* Process incoming keystrokes / ANSI escapes from USB-CDC0 or UART terminal */
    void feed_input(const oyk_u8* data, size_t len);

    /* Local terminal RX delivery hook from OykPortableNode */
    void on_local_term_event(oyk_u32 sid, int event_type,
                             const oyk_u8* data, size_t len,
                             const char* reason);

    TinyTmuxViewMode view_mode() const { return view_; }
    int settings_tab() const { return settings_tab_; }
    int current_settings_sel() const { return settings_sel_[settings_tab_]; }
    int files_tab() const { return files_tab_; }
    int has_active_terminal() const { return (lterm_sid_ != 0) ? 1 : 0; }
    const char* status_line() const { return status_line_; }

private:
    enum PromptKind {
        PROMPT_NONE = 0,
        PROMPT_TERMINATE_SID,
        PROMPT_BAN_CALLSIGN,
        PROMPT_BAN_ACTION,
        PROMPT_BAN_REASON,
        PROMPT_SETTINGS_VALUE,
        PROMPT_SETTINGS_NEW_IFACE_ID,
        PROMPT_SETTINGS_NEW_IFACE_TYPE,
        PROMPT_RIG_FREQ,
        PROMPT_RIG_MODE,
        PROMPT_RIG_VFO,
        PROMPT_RIG_RF_SELECT,
        PROMPT_CONFIRM_FORMAT
    };

    enum EscState {
        ESC_IDLE = 0,
        ESC_GOT_ESC,
        ESC_CSI,
        ESC_CSI_LINUX,
        ESC_SS3
    };

    void write_str(const char* s);
    void reset_escape();
    void dispatch_key(int key);
    void process_csi_final(char final_ch);
    void process_ss3_final(char ch);
    void render_console();
    void render_menu();
    void render_local_terminal();
    void render_files();
    void render_rig();
    void render_settings();
    void render_psram();
    void render_bootloader();
    void render_storage();
    void render_prompt_overlay();

    void handle_console_key(int key);
    void handle_menu_key(int key);
    void handle_lterm_key(int key);
    void handle_files_key(int key);
    void handle_rig_key(int key);
    void handle_settings_key(int key);
    void handle_psram_key(int key);
    void handle_bootloader_key(int key);
    void handle_storage_key(int key);
    void handle_prompt_key(int key);

    void begin_prompt(PromptKind kind, const char* label, const char* prefill);
    void commit_prompt();
    void refresh_files_lists();
    void open_settings();
    void save_and_apply_settings();
    void edit_current_settings_field();
    int settings_tab_item_count(int tab) const;
    void push_lterm_line(const char* line);

    OykPortableNode* node_;
    TinyTmuxNetBridge* bridge_;
    TinyTmuxConsoleTxFn tx_fn_;
    void* tx_ctx_;
    oyk_u32 now_ms_;

    TinyTmuxViewMode view_;
    int menu_sel_;
    char status_line_[96];
    char dialog_fb_[96];

    /* Files area state (2 tabs matching all ports) */
    int files_tab_;
    int files_sel_[2];
    OykPendingIncoming pending_files_[16];
    int pending_count_;
    OykApprovedFile approved_files_[16];
    int approved_count_;

    /* Settings draft & navigation (5 tabs: Node, BBS, CHAT, Interfaces, Rig) */
    OykConfig draft_;
    int settings_tab_;
    int settings_sel_[5];
    int settings_iface_field_;

    /* Line prompt state */
    PromptKind prompt_kind_;
    char prompt_label_[48];
    char prompt_buf_[128];
    int prompt_len_;
    char ban_call_tmp_[16];
    char ban_action_tmp_[16];
    char new_iface_id_tmp_[16];

    /* Persistent switchable local terminal state */
    oyk_u32 lterm_sid_;
    int lterm_is_local_;
    char lterm_lines_[16][80];
    int lterm_line_count_;
    char lterm_curline_[80];
    int lterm_curline_len_;
    char lterm_input_[128];
    int lterm_input_len_;

    EscState esc_state_;
    char esc_buf_[16];
    int esc_len_;
    oyk_u32 esc_start_ms_;
    oyk_u32 last_marchc_redraw_ms_;
    int bootloader_pending_;
    oyk_u32 bootloader_start_ms_;
    int storage_sel_;
};

#endif /* OYK_TINYTMUX_CONSOLE_HPP */
