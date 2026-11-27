/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ESP32C6_CONSOLE_HPP
#define OYK_ESP32C6_CONSOLE_HPP

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"
#include "oyk_term.hpp"
#include "oyk_tui.hpp"
#include "oyk_gui.hpp"
#include "board_esp32c6.hpp"
#include "esp32c6_lora.hpp"
#include "esp32c6_oled.hpp"
#include "esp32c6_fs.hpp"
#include "esp32c6_net_bridge.hpp"

typedef size_t (*Esp32C6ConsoleTxFn)(const char* data, size_t len, void* ctx);

enum Esp32C6TermType {
    C6L_TERM_VT100 = 0,
    C6L_TERM_VT220,
    C6L_TERM_VT340,
    C6L_TERM_XTERM256
};

enum Esp32C6ConsoleView {
    C6L_VIEW_CONSOLE = 0,
    C6L_VIEW_MENU,
    C6L_VIEW_SETTINGS,
    C6L_VIEW_LOCAL_TERM,
    C6L_VIEW_FILES,
    C6L_VIEW_RIG,
    C6L_VIEW_LORA,
    C6L_VIEW_STORAGE,
    C6L_VIEW_HW,
    C6L_VIEW_BOOTLOADER,
    C6L_VIEW_ABOUT
};

enum Esp32C6PromptKind {
    C6L_PROMPT_NONE = 0,
    C6L_PROMPT_TERM_SID,
    C6L_PROMPT_BAN_CALL,
    C6L_PROMPT_ADD_WIZARD,
    C6L_PROMPT_EDIT_IFACE,
    C6L_PROMPT_EDIT_PEER,
    C6L_PROMPT_THEME,
    C6L_PROMPT_CONFIRM_FORMAT,
    C6L_PROMPT_SET_TEXT,
    C6L_PROMPT_SET_BOOL,
    C6L_PROMPT_LTERM_CONNECT,
    C6L_PROMPT_FM_UPLOAD,
    C6L_PROMPT_FM_MKDIR,
    C6L_PROMPT_FM_RENAME
};

#define C6L_LTERM_HIST_ROWS 24

class Esp32C6Console {
public:
    Esp32C6Console();

    void init(OykPortableNode* node, Esp32C6NetBridge* bridge, Esp32C6ConsoleTxFn tx_fn, void* tx_ctx);
    void on_usb_connect();
    void feed_input(const oyk_u8* data, size_t len);
    void tick(oyk_u32 now_ms);
    void render();
    void render_oled(Esp32C6OledSnapshot* out);

    void on_local_term_event(oyk_u32 sid, int event_type,
                             const oyk_u8* data, size_t len, const char* reason);

    Esp32C6TermType term_type() const { return term_type_; }
    Esp32C6ConsoleView view_mode() const { return view_; }
    Esp32C6PromptKind prompt_kind() const { return prompt_kind_; }
    int settings_tab() const { return settings_tab_; }
    int term_rows() const { return term_rows_; }
    int term_cols() const { return term_cols_; }
    int mouse_enabled() const { return mouse_enabled_; }
    int bootloader_pending() const { return bootloader_pending_; }
    oyk_u32 local_term_sid() const { return lterm_sid_; }

private:
    const OykConfig* active_theme_cfg() const;
    const char* term_type_label() const;
    const char* sgr_border() const;
    const char* sgr_title() const;
    const char* sgr_header() const;
    const char* sgr_menubar() const;
    const char* sgr_menusel() const;
    const char* sgr_list_hdr() const;
    const char* sgr_sel() const;
    const char* sgr_warn() const;
    const char* sgr_ok() const;

    void write_str(const char* s);
    void send_probes();
    void draw_box(int row, int col, int h, int w, const char* title, int clear_interior, int with_close);
    void draw_hdiv(int row, int col, int w);
    void draw_shadow(int row, int col, int h, int w);
    static void tui_get_size_cb(void* ctx, int* out_rows, int* out_cols);
    static void tui_put_text_cb(void* ctx, int row, int col, int max_w, int style, const char* text);
    static void tui_put_box_cb(void* ctx, int top, int left, int h, int w, int bdr, int fill, int clr);
    static void tui_put_hdiv_cb(void* ctx, int row, int left, int w, int style);
    static void tui_put_shadow_cb(void* ctx, int top, int left, int h, int w);
    OykTuiBackend make_tui_backend();

    void render_console();
    void render_menu();
    void render_settings();
    void render_files();
    void render_rig();
    void render_lora();
    void render_storage();
    void render_hw();
    void render_bootloader();
    void render_about();
    void render_local_term();
    void render_local_term_help();
    void render_wizard_modal();
    void render_edit_iface_modal();
    void render_edit_peer_modal();
    void render_terminate_modal();
    void render_ban_modal();
    void render_theme_modal();
    void render_confirm_format_modal();
    void render_text_prompt();

    void handle_char(oyk_u8 ch);
    void handle_key_code(int key);
    void handle_da_csi();
    void handle_cpr_csi();
    void handle_mouse_event(int row1, int col1, int kind);
    void handle_console_key(int key);
    void handle_menu_key(int key);
    void handle_settings_key(int key);
    void handle_files_key(int key);
    void handle_rig_key(int key);
    void handle_lora_key(int key);
    void handle_storage_key(int key);
    void handle_bootloader_key(int key);
    void handle_local_term_key(int key);
    void handle_prompt_key(int key);

    void open_settings(int tab);
    void open_wizard();
    void open_edit_iface(int idx);
    void open_edit_peer(int idx);
    void open_terminate_dialog();
    void open_ban_dialog();
    void open_theme_dialog();
    void refresh_files_lists();
    void append_lterm_line(const char* line);

    OykPortableNode* node_;
    Esp32C6NetBridge* bridge_;
    Esp32C6ConsoleTxFn tx_fn_;
    void* tx_ctx_;

    Esp32C6TermType term_type_;
    Esp32C6ConsoleView view_;
    Esp32C6PromptKind prompt_kind_;
    int term_rows_;
    int term_cols_;
    int mouse_enabled_;
    oyk_u32 now_ms_;
    oyk_u32 last_esc_ms_;
    int esc_state_;
    char esc_buf_[48];
    size_t esc_len_;
    oyk_u8 x10_buf_[4];
    int x10_len_;

    int menu_sel_;
    int settings_tab_;
    int settings_sel_;
    OykConfig draft_;
    char status_fb_[96];
    char dialog_fb_[96];

    OykGuiAddIfaceWizard wiz_;
    char wiz_id_[24];
    char wiz_desc_[64];
    char wiz_host_[48];
    char wiz_port_[12];
    char wiz_mask_[24];
    char wiz_peer_[48];
    int wiz_same_iface_;
    int wiz_lpol_;
    int wiz_field_;
    int wiz_btn_;

    int edit_iface_idx_;
    OykIfaceConfig edit_iface_buf_;
    int edit_iface_field_;

    int edit_peer_idx_;
    OykPeerConfig edit_peer_buf_;
    int edit_peer_field_;
    int edit_btn_;

    int term_sel_;
    int ban_sel_;
    char ban_call_[16];
    char ban_reason_[64];
    int ban_hard_;

    int theme_sel_;
    int theme_role_sel_;
    char prompt_label_[48];
    char prompt_buf_[96];
    int prompt_cursor_;
    OykGuiFileMgr file_mgr_;
    int files_focus_btn_;
    int fm_preview_active_;
    char fm_preview_buf_[512];

    int bootloader_pending_;
    oyk_u32 bootloader_start_ms_;

    oyk_u32 lterm_sid_;
    OykTermEolParser lterm_eol_;
    int lterm_ctrl_a_;
    int lterm_help_open_;
    char lterm_lines_[C6L_LTERM_HIST_ROWS][128];
    int lterm_line_count_;
    char lterm_input_[96];
    int lterm_input_len_;
};

#endif /* OYK_ESP32C6_CONSOLE_HPP */
