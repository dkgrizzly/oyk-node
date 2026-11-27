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
#include "oyk_term.hpp"
#include "oyk_tui.hpp"
#include "oyk_gui.hpp"
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
    TMUX_VIEW_STORAGE,
    TMUX_VIEW_ABOUT
};

enum TinyTmuxTermType {
    TMUX_TERM_VT100 = 0,
    TMUX_TERM_VT220,
    TMUX_TERM_VT340,
    TMUX_TERM_XTERM256
};

class TinyTmuxConsole {
public:
    TinyTmuxConsole();

    void init(OykPortableNode* node, TinyTmuxConsoleTxFn tx_fn, void* tx_ctx);
    void set_net_bridge(TinyTmuxNetBridge* bridge);
    void set_now_ms(oyk_u32 now_ms) { now_ms_ = now_ms; }
    void tick(oyk_u32 now_ms);

    /* Called when USB-CDC0 operator terminal connects: sends DA1/DA2 inquiry & renders */
    void on_usb_connect();
    TinyTmuxTermType term_type() const { return term_type_; }
    const char* term_type_label() const;

    /* Render current screen as ANSI/VT escape sequences */
    void render();

    /* Process incoming keystrokes / ANSI escapes from USB-CDC0 or UART terminal */
    void feed_input(const oyk_u8* data, size_t len);

    /* Local terminal RX delivery hook from OykPortableNode */
    void on_local_term_event(oyk_u32 sid, int event_type,
                             const oyk_u8* data, size_t len,
                             const char* reason);

    int term_rows() const { return term_rows_; }
    int term_cols() const { return term_cols_; }
    TinyTmuxViewMode view_mode() const { return view_; }
    int settings_tab() const { return settings_tab_; }
    int current_settings_sel() const { return settings_sel_[settings_tab_]; }
    int files_tab() const { return file_mgr_.active_pane; }
    int has_active_terminal() const { return (lterm_sid_ != 0) ? 1 : 0; }
    const char* status_line() const { return status_line_; }

private:
    enum PromptKind {
        PROMPT_NONE = 0,
        PROMPT_TERMINATE_SID,
        PROMPT_BAN_CALLSIGN,
        PROMPT_THEME_SETTINGS,
        PROMPT_SETTINGS_VALUE,
        PROMPT_SETTINGS_ADD_WIZARD,
        PROMPT_SETTINGS_EDIT_IFACE,
        PROMPT_SETTINGS_EDIT_PEER,
        PROMPT_RIG_FREQ,
        PROMPT_RIG_MODE,
        PROMPT_RIG_VFO,
        PROMPT_RIG_RF_SELECT,
        PROMPT_CONFIRM_FORMAT,
        PROMPT_LTERM_CONNECT,
        PROMPT_FM_UPLOAD,
        PROMPT_FM_MKDIR,
        PROMPT_FM_RENAME
    };

    enum EscState {
        ESC_IDLE = 0,
        ESC_GOT_ESC,
        ESC_CSI,
        ESC_CSI_LINUX,
        ESC_SS3,
        ESC_CSI_MOUSE_X10
    };

    void write_str(const char* s);
    void reset_escape();
    void dispatch_key(int key);
    void process_csi_final(char final_ch);
    void process_ss3_final(char ch);
    void handle_da_csi();
    void handle_mouse_event(int row1, int col1, int kind);
    void disable_mouse();

    const OykConfig* active_theme_cfg() const;
    const char* sgr_border() const;
    const char* sgr_title() const;
    const char* sgr_header() const;
    const char* sgr_menubar() const;
    const char* sgr_menusel() const;
    const char* sgr_list_hdr() const;
    const char* sgr_sel() const;
    const char* sgr_warn() const;
    void draw_dec_box(int row, int col, int h, int w, const char* title, int clear_interior, int with_close = 0);
    void draw_dec_shadow(int row, int col, int h, int w);
    void draw_dec_hdiv(int row, int col, int w);
    static void tui_get_size_cb(void* ctx, int* out_rows, int* out_cols);
    static void tui_put_text_cb(void* ctx, int row, int col, int max_w, int style, const char* text);
    static void tui_put_box_cb(void* ctx, int top, int left, int h, int w, int bdr, int fill, int clr);
    static void tui_put_hdiv_cb(void* ctx, int row, int left, int w, int style);
    static void tui_put_shadow_cb(void* ctx, int top, int left, int h, int w);
    OykTuiBackend make_tui_backend();
    void send_size_probe();

    void render_console();
    void render_menu();
    void render_local_terminal();
    void render_lterm_help_modal();
    void render_files();
    void render_rig();
    void render_settings();
    void render_psram();
    void render_bootloader();
    void render_storage();
    void render_about();
    void render_prompt_overlay();
    void render_add_iface_wizard();
    void render_edit_iface_dialog();
    void render_edit_peer_dialog();
    void render_confirm_format_modal();

    void open_add_iface_wizard();
    void open_edit_iface_dialog(int edit_idx);
    void open_edit_peer_dialog(int edit_idx);
    void handle_add_wizard_key(int key);
    void handle_edit_iface_key(int key);
    void handle_edit_peer_key(int key);
    void open_terminate_dialog();
    void render_terminate_dialog();
    void handle_terminate_key(int key);
    void open_ban_dialog();
    void render_ban_dialog();
    void handle_ban_key(int key);
    void open_theme_dialog();
    void render_theme_dialog();
    void handle_theme_key(int key);
    void wizard_apply_driver_defaults();
    int wizard_try_advance();
    int edit_iface_try_save();
    int edit_peer_try_save();
    void commit_iface_fields(int slot);
    void commit_peer_fields(int slot);
    int wizard_step_field_count() const;
    int edit_iface_field_count() const;

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
    void open_lterm_target(const char* target);
    int  exec_lterm_cmd(int cmd);

    void begin_prompt(PromptKind kind, const char* label, const char* prefill);
    void commit_prompt();
    void refresh_files_lists();
    void open_settings(int initial_tab = 0);
    void save_and_apply_settings();
    void edit_current_settings_field();
    int settings_tab_item_count(int tab) const;
    void push_lterm_line(const char* line);

    OykPortableNode* node_;
    TinyTmuxNetBridge* bridge_;
    TinyTmuxConsoleTxFn tx_fn_;
    void* tx_ctx_;
    oyk_u32 now_ms_;

    TinyTmuxTermType term_type_;
    int term_rows_;
    int term_cols_;
    TinyTmuxViewMode view_;
    int menu_sel_;
    char status_line_[96];
    char dialog_fb_[96];

    /* Files area state (dual-pane jailed manager) */
    OykGuiFileMgr file_mgr_;
    int files_focus_btn_;
    int fm_preview_active_;
    char fm_preview_buf_[512];

    /* Settings draft & navigation (7 tabs: Node, BBS, CHAT, Ifaces, Peers, Rig, Printer) */
    OykConfig draft_;
    int settings_tab_;
    int settings_sel_[7];
    int settings_iface_field_;

    /* Line & modal prompt state */
    PromptKind prompt_kind_;
    char prompt_label_[48];
    char prompt_buf_[128];
    int prompt_len_;
    char ban_call_tmp_[16];
    char ban_action_tmp_[16];
    int wiz_edit_idx_;
    int wiz_step_;
    int wiz_drv_;
    int wiz_last_drv_;
    int wiz_focus_;
    int wiz_btn_;
    char wiz_id_[16];
    char wiz_desc_[32];
    char wiz_ep_[32];
    char wiz_port_[12];
    char wiz_mask_[16];
    char wiz_peer_[32];
    oyk_u8 wiz_flags_; /* bit0:allow_in, bit1:allow_out, bits2..4:learned_policy */
    char wiz_err_[56];

    /* Persistent switchable local terminal state */
    oyk_u32 lterm_sid_;
    int lterm_is_local_;
    int lterm_opening_;
    int lterm_scroll_;
    int lterm_ctrl_a_;
    int lterm_help_open_;
    OykTermEolParser lterm_eol_;
    char lterm_lines_[32][200];
    int lterm_line_count_;
    char lterm_curline_[200];
    int lterm_curline_len_;
    char lterm_input_[160];
    int lterm_input_len_;

    EscState esc_state_;
    char esc_buf_[48];
    int esc_len_;
    oyk_u8 x10_buf_[3];
    int x10_len_;
    oyk_u8 mouse_enabled_;
    oyk_u32 esc_start_ms_;
    oyk_u32 last_marchc_redraw_ms_;
    int bootloader_pending_;
    oyk_u32 bootloader_start_ms_;
    int storage_sel_;
};

#endif /* OYK_TINYTMUX_CONSOLE_HPP */
