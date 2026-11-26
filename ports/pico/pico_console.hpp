/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_CONSOLE_HPP
#define OYK_PICO_CONSOLE_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"
#include "board_pico.hpp"
#include "pico_net_bridge.hpp"

typedef size_t (*PicoConsoleTxFn)(const char* data, size_t len, void* ctx);

enum PicoTermType {
    PICO_TERM_VT100 = 0,
    PICO_TERM_VT220,
    PICO_TERM_VT340,
    PICO_TERM_XTERM256
};

enum PicoConsoleView {
    PICO_VIEW_CONSOLE = 0,
    PICO_VIEW_MENU,
    PICO_VIEW_SETTINGS,
    PICO_VIEW_LOCAL_TERM,
    PICO_VIEW_FILES,
    PICO_VIEW_RIG,
    PICO_VIEW_PSRAM,
    PICO_VIEW_STORAGE,
    PICO_VIEW_BOOTLOADER
};

enum PicoPromptKind {
    PICO_PROMPT_NONE = 0,
    PICO_PROMPT_SET_TEXT,
    PICO_PROMPT_SET_BOOL,
    PICO_PROMPT_TERM_SID,
    PICO_PROMPT_BAN_CALL,
    PICO_PROMPT_ADD_WIZARD,
    PICO_PROMPT_EDIT_IFACE,
    PICO_PROMPT_EDIT_PEER,
    PICO_PROMPT_THEME,
    PICO_PROMPT_CONFIRM_FORMAT
};

class PicoConsole {
public:
    PicoConsole();

    void init(OykPortableNode* node, PicoNetBridge* bridge,
              PicoConsoleTxFn tx_fn, void* tx_ctx);
    void on_usb_connect();
    void feed_input(const oyk_u8* data, size_t len);
    void tick(oyk_u32 now_ms);
    void render();

    void on_local_term_event(oyk_u32 sid, int event_type,
                             const oyk_u8* data, size_t len, const char* reason);

    PicoTermType term_type() const { return term_type_; }
    const char* term_type_label() const;
    PicoConsoleView view_mode() const { return view_; }
    PicoPromptKind prompt_kind() const { return prompt_kind_; }
    int settings_tab() const { return settings_tab_; }
    int settings_sel() const { return settings_sel_; }
    int bootloader_pending() const { return bootloader_pending_; }

private:
    void write_str(const char* s);
    void draw_box(int row, int col, int h, int w, const char* title, int clear_interior);
    void draw_shadow(int row, int col, int h, int w);
    void draw_hdiv(int row, int col, int w);
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
    void render_storage();
    void render_psram();
    void render_bootloader();
    void render_local_term();
    void render_wizard_modal();
    void render_edit_iface_modal();
    void render_edit_peer_modal();
    void render_terminate_modal();
    void render_ban_modal();
    void render_theme_modal();
    void render_confirm_format_modal();
    void render_text_prompt();

    void handle_char(oyk_u8 ch, oyk_u32 now_ms);
    void handle_key_code(int key);
    void handle_console_key(int key);
    void handle_menu_key(int key);
    void handle_settings_key(int key);
    void handle_storage_key(int key);
    void handle_bootloader_key(int key);
    void handle_modal_key(int key);
    void open_settings(int tab);
    void commit_settings();

    OykPortableNode* node_;
    PicoNetBridge* bridge_;
    PicoConsoleTxFn tx_fn_;
    void* tx_ctx_;

    PicoTermType term_type_;
    PicoConsoleView view_;
    PicoPromptKind prompt_kind_;
    int menu_sel_;
    int settings_tab_;
    int settings_sel_;
    int storage_sel_;
    int modal_sel_;
    int wiz_step_;
    int wiz_drv_;
    int bootloader_pending_;
    oyk_u32 bootloader_ms_;

    OykConfig draft_;
    char prompt_buf_[96];
    size_t prompt_len_;
    char status_fb_[96];

    oyk_u32 lterm_sid_;
    char lterm_lines_[12][80];
    int lterm_line_count_;
    char lterm_input_[80];
    size_t lterm_input_len_;

    int esc_state_;
    char esc_buf_[24];
    size_t esc_len_;
    oyk_u32 esc_ms_;
    oyk_u32 last_ms_;
};

#endif /* OYK_PICO_CONSOLE_HPP */
