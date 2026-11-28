/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_CONSOLE_HPP
#define OYK_LYRA_CONSOLE_HPP

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"
#include "oyk_term.hpp"
#include "oyk_tui.hpp"
#include "oyk_gui.hpp"
#include "board_lyra.hpp"
#include "lyra_net_bridge.hpp"

typedef size_t (*LyraConsoleTxFn)(const char* data, size_t len, void* ctx);

enum LyraTermType {
    LYRA_TERM_VT100 = 0,
    LYRA_TERM_VT220,
    LYRA_TERM_VT340,
    LYRA_TERM_XTERM256
};

enum LyraConsoleView {
    LYRA_VIEW_CONSOLE = 0,
    LYRA_VIEW_MENU,
    LYRA_VIEW_SETTINGS,
    LYRA_VIEW_LOCAL_TERM,
    LYRA_VIEW_FILES,
    LYRA_VIEW_RIG,
    LYRA_VIEW_HW,
    LYRA_VIEW_STORAGE,
    LYRA_VIEW_REBOOT,
    LYRA_VIEW_ABOUT
};

enum LyraPromptKind {
    LYRA_PROMPT_NONE = 0,
    LYRA_PROMPT_SET_TEXT,
    LYRA_PROMPT_SET_BOOL,
    LYRA_PROMPT_TERM_SID,
    LYRA_PROMPT_BAN_CALL,
    LYRA_PROMPT_ADD_WIZARD,
    LYRA_PROMPT_EDIT_IFACE,
    LYRA_PROMPT_EDIT_PEER,
    LYRA_PROMPT_CONFIRM_FORMAT,
    LYRA_PROMPT_LTERM_CONNECT,
    LYRA_PROMPT_FM_UPLOAD,
    LYRA_PROMPT_FM_MKDIR,
    LYRA_PROMPT_FM_RENAME
};

class LyraConsole {
public:
    LyraConsole();

    void init(OykPortableNode* node, LyraNetBridge* bridge,
              LyraConsoleTxFn tx_fn, void* tx_ctx);
    void on_usb_connect();
    void feed_input(const oyk_u8* data, size_t len);
    void tick(oyk_u32 now_ms);
    void render();

    void on_local_term_event(oyk_u32 sid, int event_type,
                             const oyk_u8* data, size_t len, const char* reason);

    LyraTermType term_type() const { return term_type_; }
    int term_rows() const { return term_rows_; }
    int term_cols() const { return term_cols_; }
    const char* term_type_label() const;
    LyraConsoleView view_mode() const { return view_; }
    LyraPromptKind prompt_kind() const { return prompt_kind_; }
    int settings_tab() const { return settings_tab_; }
    int settings_sel() const { return settings_sel_[settings_tab_]; }
    int files_tab() const { return file_mgr_.active_pane; }
    int has_active_terminal() const { return (lterm_sid_ != 0) ? 1 : 0; }
    int reboot_pending() const { return reboot_pending_; }
    const char* status_line() const { return status_fb_; }
    void set_battery_percent(int pct) { battery_pct_ = pct; }
    int battery_percent() const { return battery_pct_; }

private:
    void write_str(const char* s);
    void send_size_probe();
    void draw_box(int row, int col, int h, int w, const char* title, int clear_interior, int with_close = 0);
    void draw_shadow(int row, int col, int h, int w);
    void draw_hdiv(int row, int col, int w);
    static void tui_get_size_cb(void* ctx, int* out_rows, int* out_cols);
    static void tui_put_text_cb(void* ctx, int row, int col, int max_w, int style, const char* text);
    static void tui_put_box_cb(void* ctx, int top, int left, int h, int w, int bdr, int fill, int clr);
    static void tui_put_hdiv_cb(void* ctx, int row, int left, int w, int style);
    static void tui_put_shadow_cb(void* ctx, int top, int left, int h, int w);
    OykTuiBackend make_tui_backend();
    const OykConfig* active_theme_cfg() const;
    const char* sgr_border() const;
    const char* sgr_title() const;
    const char* sgr_header() const;
    const char* sgr_menubar() const;
    const char* sgr_menusel() const;
    const char* sgr_list_hdr() const;
    const char* sgr_sel() const;
    const char* sgr_warn() const;

    void render_console();
    void render_menu();
    void render_settings();
    void render_files();
    void render_rig();
    void render_storage();
    void render_hw_diag();
    void render_reboot();
    void render_about();
    void render_local_term();
    void render_local_term_help_modal();
    void render_wizard_modal();
    void render_edit_iface_modal();
    void render_edit_peer_modal();
    void render_terminate_modal();
    void render_ban_modal();
    void render_confirm_format_modal();
    void render_text_prompt();
    int  modal_close_btn() const;

    void handle_char(oyk_u8 ch, oyk_u32 now_ms);
    void handle_key_code(int key);
    void handle_mouse_event(int row1, int col1, int kind);
    void disable_mouse();
    void handle_console_key(int key);
    void handle_menu_key(int key);
    void exec_menu_hotkey(int key);
    void handle_settings_key(int key);
    void handle_files_key(int key);
    void handle_rig_key(int key);
    void handle_storage_key(int key);
    void handle_reboot_key(int key);
    void handle_local_term_key(int key);
    void handle_modal_key(int key);
    void handle_wizard_key(int key);
    void handle_edit_iface_key(int key);
    void handle_edit_peer_key(int key);
    void handle_terminate_key(int key);
    void handle_ban_key(int key);

    void open_settings(int tab);
    void commit_settings();
    int settings_tab_item_count(int tab) const;
    void edit_current_settings_field();
    void commit_prompt();
    void open_local_term_target(const char* target);
    int  exec_local_term_cmd(int cmd);
    void refresh_files_lists();

    void open_add_iface_wizard();
    void open_edit_iface_dialog(int idx);
    void open_edit_peer_dialog(int idx);
    void open_terminate_dialog();
    void open_ban_dialog();
    void wizard_apply_driver_defaults();
    int wizard_step_field_count() const;
    int edit_iface_field_count() const;
    int wizard_try_advance();
    int edit_iface_try_save();
    int edit_peer_try_save();
    void commit_iface_fields(int slot);
    void commit_peer_fields(int slot);

    OykPortableNode* node_;
    LyraNetBridge* bridge_;
    LyraConsoleTxFn tx_fn_;
    void* tx_ctx_;

    LyraTermType term_type_;
    int term_rows_;
    int term_cols_;
    LyraConsoleView view_;
    LyraPromptKind prompt_kind_;
    int menu_sel_;
    int settings_tab_;
    int settings_sel_[8];
    int theme_scroll_;
    int theme_preview_dirty_;
    OykGuiFileMgr file_mgr_;
    int files_focus_btn_;
    int fm_preview_active_;
    char fm_preview_buf_[512];
    int storage_sel_;
    int reboot_pending_;
    int poweroff_pending_;
    oyk_u32 reboot_ms_;

    OykConfig draft_;
    char prompt_label_[48];
    char prompt_buf_[96];
    size_t prompt_len_;
    char status_fb_[96];
    char dialog_fb_[96];

    char ban_call_tmp_[16];
    char ban_iface_tmp_[16];
    int wiz_edit_idx_;
    int wiz_step_;
    int wiz_drv_;
    int wiz_last_drv_;
    int wiz_focus_;
    int wiz_btn_;
    char wiz_id_[24];
    char wiz_desc_[32];
    char wiz_ep_[40];
    char wiz_port_[12];
    char wiz_mask_[20];
    char wiz_peer_[32];
    oyk_u8 wiz_flags_;
    char wiz_err_[64];

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
    size_t lterm_input_len_;

    int esc_state_;
    char esc_buf_[24];
    size_t esc_len_;
    oyk_u8 x10_buf_[3];
    oyk_u8 x10_len_;
    oyk_u8 mouse_enabled_;
    oyk_u32 esc_ms_;
    oyk_u32 last_ms_;
    int battery_pct_;
    char render_scratch_[320];
};

#endif /* OYK_LYRA_CONSOLE_HPP */
