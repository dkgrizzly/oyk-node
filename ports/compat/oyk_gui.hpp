/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_GUI_HPP
#define OYK_GUI_HPP

/*
 * Shared C++98 / 16-bit-clean GUI Controller & Schema Layer for:
 *   - Windows 3.1 (Win16) and Windows 95..11 (Win32 / Win64)
 *   - X11 GTK+ 2.0
 *   - Classic Macintosh System 6.0.8 - Mac OS 9.2.2 (68k + PPC Fat)
 *   - Mac OS X / modern macOS (PowerPC, Intel, Apple Silicon)
 *
 * Centralizes canonical menu definitions, window/dialog titles, the 7 Settings
 * tabs, table column schemas, validation rules, Add Interface wizard transitions,
 * Theme cloning/preview logic, console header formatting, and exit prompts.
 */

#include <stddef.h>
#include "oyk_portable.hpp"

enum OykGuiAction {
    OYK_ACT_NONE = 0,
    OYK_ACT_ABOUT,
    OYK_ACT_ALLOW_INCOMING,
    OYK_ACT_DENY_INCOMING,
    OYK_ACT_LOCAL_TERM,
    OYK_ACT_FILES,
    OYK_ACT_RIG,
    OYK_ACT_SETTINGS,
    OYK_ACT_PEERS,
    OYK_ACT_THEME,
    OYK_ACT_TERMINATE,
    OYK_ACT_BAN,
    OYK_ACT_EXIT,
    OYK_ACT_HELP_HOTKEYS
};

struct OykGuiMenuItem {
    int action;
    const char* label;
    const char* gtk_label;
    const char* mac_label;
    char shortcut_key;
    int is_separator;
};

enum OykGuiScreenId {
    OYK_SCR_MAIN = 0,
    OYK_SCR_SETTINGS,
    OYK_SCR_ADD_IFACE,
    OYK_SCR_EDIT_IFACE,
    OYK_SCR_PEERS,
    OYK_SCR_EDIT_PEER,
    OYK_SCR_TERMINATE,
    OYK_SCR_BAN,
    OYK_SCR_EXIT_PEERS,
    OYK_SCR_THEME,
    OYK_SCR_TERMINAL,
    OYK_SCR_FILES,
    OYK_SCR_RIG,
    OYK_SCR_ABOUT,
    OYK_SCR_MENU
};

enum OykGuiTableId {
    OYK_TBL_IFACES = 0,
    OYK_TBL_HEARD,
    OYK_TBL_CONNECTIONS,
    OYK_TBL_TERMINATE,
    OYK_TBL_PEERS,
    OYK_TBL_BANS,
    OYK_TBL_FILES_INCOMING,
    OYK_TBL_FILES_APPROVED,
    OYK_TBL_THEME
};

enum OykGuiDriverMask {
    OYK_DRV_AXUDP       = 0x01U,
    OYK_DRV_AXIP        = 0x02U,
    OYK_DRV_KISS_SERIAL = 0x04U,
    OYK_DRV_KISS_TCP    = 0x08U,
    OYK_DRV_TELNET      = 0x10U,
    OYK_DRV_ALL         = 0x1FU
};

struct OykGuiAddIfaceWizard {
    int step; /* 0:Driver, 1:Identity, 2:Params/Policy, 3:Review */
    oyk_u32 driver_mask;
    int selected_driver_idx;
    char static_peer_spec[48];
    OykIfaceConfig draft;
    char error[96];
};

typedef void (*OykGuiThemePreviewFn)(const OykThemeConfig* pal,
                                     const char* active_name,
                                     void* ctx);

struct OykGuiThemeCtrl {
    OykConfig* draft;
    char active_name[32];
    char saved_name[32];
    int selected_role;
    int dirty;
    int preview_active;
    int default_native;
    int os_dark;
    int has_os_native;
    OykThemeConfig os_native_pal;
    oyk_u32 preview_generation;
    OykGuiThemePreviewFn preview_cb;
    void* preview_ctx;
};

struct OykGuiExitPrompt {
    int has_auto_learned;
    int auto_learned_count;
    char title[48];
    char body[192];
};

/* Canonical Node menu & screen/tab/table metadata */
int                  oyk_gui_node_menu_count();
const OykGuiMenuItem* oyk_gui_node_menu_item(int idx);
const char*          oyk_gui_screen_title(int screen_id);
int                  oyk_gui_settings_tab_count();
const char*          oyk_gui_settings_tab_title(int tab_idx);
int                  oyk_gui_table_column_count(int table_id);
const char*          oyk_gui_table_column(int table_id, int col_idx);
int                  oyk_gui_terminate_columns(const char** out_cols, int max_cols);

enum OykGuiControlKind {
    OYK_CTRL_CHECKBOX = 0,
    OYK_CTRL_RADIO    = 1
};

int         oyk_gui_control_box_size(int char_h);
int         oyk_gui_control_label_offset(int char_h);
int         oyk_gui_vertical_text_offset(int box_h, int char_h);
int         oyk_gui_control_height(int char_h, int pad_y);
const char* oyk_gui_format_learned_policy(int policy);
int         oyk_gui_cycle_learned_policy(int policy, int delta);
const char* oyk_gui_about_summary();

/* Formatting & validation helpers */
void        oyk_gui_format_duration(oyk_u32 secs, char* dst, size_t cap);
void        oyk_gui_format_console_banner(const char* callsign, const char* alias,
                                          int accept_in, int conn_count, int iface_count,
                                          char* dst, size_t cap);
void        oyk_gui_format_console_header(const char* callsign, const char* alias,
                                          oyk_u32 uptime_sec, int conn_count,
                                          int accept_in, int compact,
                                          char* dst, size_t cap);
const char* oyk_gui_format_incoming_pill(int accept_in, int compact);
int         oyk_gui_parse_port_str(const char* text, oyk_u16* out_port);
int         oyk_gui_validate_iface_draft(const OykConfig* cfg, int edit_idx,
                                         const OykIfaceConfig* draft,
                                         char* err, size_t err_cap);
int         oyk_gui_validate_peer_draft(const OykPeerConfig* draft,
                                        char* err, size_t err_cap);
void        oyk_gui_format_exit_prompt(int auto_learned_count,
                                       char* title, size_t tcap,
                                       char* body, size_t bcap);
void        oyk_gui_build_exit_prompt(int auto_learned_count, int active_conns,
                                      OykGuiExitPrompt* out);

/* Shared Add Interface Wizard state machine */
void        oyk_gui_wiz_init(OykGuiAddIfaceWizard* w, const OykConfig* cfg, oyk_u32 driver_mask);
int         oyk_gui_wiz_driver_count(oyk_u32 driver_mask);
const char* oyk_gui_wiz_driver_type(oyk_u32 driver_mask, int vis_idx);
const char* oyk_gui_wiz_driver_desc(oyk_u32 driver_mask, int vis_idx);
const char* oyk_gui_wiz_driver_def_host(oyk_u32 driver_mask, int vis_idx);
const char* oyk_gui_wiz_driver_def_port(oyk_u32 driver_mask, int vis_idx);
void        oyk_gui_wiz_select_driver(OykGuiAddIfaceWizard* w, const OykConfig* cfg, int vis_idx);
int         oyk_gui_wiz_advance(OykGuiAddIfaceWizard* w, const OykConfig* cfg);
void        oyk_gui_wiz_back(OykGuiAddIfaceWizard* w);
const char* oyk_gui_wiz_step_prompt(const OykGuiAddIfaceWizard* w);
void        oyk_gui_wiz_format_summary(const OykGuiAddIfaceWizard* w,
                                       char* dst, size_t cap, int use_crlf);

/* Shared Theme Settings Controller */
void        oyk_gui_theme_init(OykGuiThemeCtrl* c, OykConfig* draft,
                               int default_native, int os_dark);
void        oyk_gui_theme_set_os_native(OykGuiThemeCtrl* c, const OykThemeConfig* os_pal);
void        oyk_gui_theme_set_preview_cb(OykGuiThemeCtrl* c, OykGuiThemePreviewFn cb, void* ctx);
int         oyk_gui_theme_list_names(const OykGuiThemeCtrl* c, const char** out_names, int max_names);
void        oyk_gui_theme_select(OykGuiThemeCtrl* c, const char* name);
void        oyk_gui_theme_cycle(OykGuiThemeCtrl* c, int delta);
void        oyk_gui_theme_revert(OykGuiThemeCtrl* c);
int         oyk_gui_theme_set_role_rgb(OykGuiThemeCtrl* c, int role_idx, oyk_u32 rgb,
                                       char* err, size_t err_cap);
int         oyk_gui_theme_set_role_hex(OykGuiThemeCtrl* c, int role_idx, const char* hex_str,
                                       char* err, size_t err_cap);
int         oyk_gui_theme_delete_custom(OykGuiThemeCtrl* c);
void        oyk_gui_theme_resolve(const OykGuiThemeCtrl* c, OykThemeConfig* out_pal);

#endif /* OYK_GUI_HPP */
