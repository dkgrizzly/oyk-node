/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_GSOS_CORE_H
#define OYK_GSOS_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char  gsos_u8;
typedef unsigned short gsos_u16;
typedef unsigned long  gsos_u32;

#define OYK_GSOS_MAX_IFACES       4
#define OYK_GSOS_MAX_PEERS        8
#define OYK_GSOS_MAX_BANS         8
#define OYK_GSOS_MAX_HEARD        5
#define OYK_GSOS_MAX_SESSIONS     6
#define OYK_GSOS_MAX_LOG_LINES   16
#define OYK_GSOS_MAX_THEMES       6

typedef enum {
    OYK_GSOS_EOL_LF = 0,
    OYK_GSOS_EOL_CR = 1,
    OYK_GSOS_EOL_CRLF = 2
} OykGsosLineEnding;

typedef enum {
    OYK_GSOS_ACT_NONE = 0,
    OYK_GSOS_ACT_ABOUT,
    OYK_GSOS_ACT_ALLOW_INCOMING,
    OYK_GSOS_ACT_DENY_INCOMING,
    OYK_GSOS_ACT_LOCAL_TERM,
    OYK_GSOS_ACT_FILES,
    OYK_GSOS_ACT_RIG,
    OYK_GSOS_ACT_SETTINGS,
    OYK_GSOS_ACT_PEERS,
    OYK_GSOS_ACT_THEME,
    OYK_GSOS_ACT_TERMINATE,
    OYK_GSOS_ACT_BAN,
    OYK_GSOS_ACT_EXIT,
    OYK_GSOS_ACT_HELP_HOTKEYS
} OykGsosGuiAction;

typedef enum {
    OYK_GSOS_SCR_MAIN = 0,
    OYK_GSOS_SCR_SETTINGS,
    OYK_GSOS_SCR_ADD_IFACE,
    OYK_GSOS_SCR_EDIT_IFACE,
    OYK_GSOS_SCR_PEERS,
    OYK_GSOS_SCR_EDIT_PEER,
    OYK_GSOS_SCR_TERMINATE,
    OYK_GSOS_SCR_BAN,
    OYK_GSOS_SCR_EXIT_PEERS,
    OYK_GSOS_SCR_THEME,
    OYK_GSOS_SCR_TERMINAL,
    OYK_GSOS_SCR_TERM_HELP,
    OYK_GSOS_SCR_FILES,
    OYK_GSOS_SCR_RIG,
    OYK_GSOS_SCR_ABOUT,
    OYK_GSOS_SCR_MENU
} OykGsosScreenId;

typedef enum {
    OYK_GSOS_TBL_IFACES = 0,
    OYK_GSOS_TBL_HEARD,
    OYK_GSOS_TBL_CONNECTIONS,
    OYK_GSOS_TBL_TERMINATE,
    OYK_GSOS_TBL_PEERS,
    OYK_GSOS_TBL_BANS,
    OYK_GSOS_TBL_FILES_INCOMING,
    OYK_GSOS_TBL_FILES_APPROVED,
    OYK_GSOS_TBL_THEME
} OykGsosTableId;

typedef enum {
    OYK_GSOS_TR_BG = 0,
    OYK_GSOS_TR_FG,
    OYK_GSOS_TR_BORDER,
    OYK_GSOS_TR_HEADER_BG,
    OYK_GSOS_TR_HEADER_FG,
    OYK_GSOS_TR_STATUS_BG,
    OYK_GSOS_TR_STATUS_FG,
    OYK_GSOS_TR_MENU_BG,
    OYK_GSOS_TR_MENU_FG,
    OYK_GSOS_TR_SELECTION_BG,
    OYK_GSOS_TR_SELECTION_FG,
    OYK_GSOS_TR_PANEL_BG,
    OYK_GSOS_TR_PANEL_FG,
    OYK_GSOS_TR_INPUT_BG,
    OYK_GSOS_TR_INPUT_FG,
    OYK_GSOS_TR_ACCENT,
    OYK_GSOS_TR_OK,
    OYK_GSOS_TR_WARN,
    OYK_GSOS_TR_ERROR,
    OYK_GSOS_THEME_COLOR_COUNT
} OykGsosThemeRole;

typedef enum {
    OYK_GSOS_TCMD_NONE = 0,
    OYK_GSOS_TCMD_HELP,
    OYK_GSOS_TCMD_CLOSE,
    OYK_GSOS_TCMD_CLEAR,
    OYK_GSOS_TCMD_WRAP,
    OYK_GSOS_TCMD_EOL,
    OYK_GSOS_TCMD_ECHO,
    OYK_GSOS_TCMD_UPLOAD,
    OYK_GSOS_TCMD_LITERAL_CTRL_A
} OykGsosTermCmd;

typedef struct {
    int         action;
    const char *label;
    const char *mac_label;
    char        shortcut_key;
    int         is_separator;
} OykGsosMenuItem;

typedef struct {
    char base[8];
    gsos_u8 ssid;
} OykGsosCallsign;

typedef struct {
    char     id[16];
    char     type[16];
    char     description[32];
    char     host[32];
    gsos_u16 port;
    char     netmask[20];
    char     device[32];
    gsos_u32 baud;
    int      allow_inbound;
    int      allow_outbound;
    int      allow_same_interface;
    int      learned_peer_policy; /* 0=inherit, 1=none, 2=inbound, 3=outbound, 4=both */
} OykGsosIfaceConfig;

typedef struct {
    char     id[16];
    char     callsign[12];
    char     description[32];
    char     interface_id[16];
    char     host[32];
    gsos_u16 port;
    int      allow_inbound;
    int      allow_outbound;
    int      auto_learned;
} OykGsosPeerConfig;

typedef struct {
    char name[24];
    int  is_dark_base;
    gsos_u32 colors[OYK_GSOS_THEME_COLOR_COUNT];
} OykGsosThemeConfig;

typedef struct {
    OykGsosCallsign callsign;
    OykGsosCallsign alias;
    int             accept_incoming;
    OykGsosLineEnding line_ending;
    int             has_detected_eol;
    int             bbs_enabled;
    gsos_u8         bbs_ssid;
    int             bbs_files_enabled;
    char            bbs_mail_path[48];
    char            bbs_files_path[48];
    int             chat_enabled;
    gsos_u8         chat_ssid;
    char            chat_topic[32];
    int             rig_enabled;
    char            rig_protocol[16];
    char            rig_host[32];
    gsos_u16        rig_port;
    int             rig_poll_sec;
    int             printer_enabled;
    char            printer_device[32];
    char            printer_protocol[16];
    gsos_u32        printer_baud;
    int             printer_condensed;
    int             iface_count;
    OykGsosIfaceConfig ifaces[OYK_GSOS_MAX_IFACES];
    int             peer_count;
    OykGsosPeerConfig peers[OYK_GSOS_MAX_PEERS];
    int             theme_count;
    OykGsosThemeConfig themes[OYK_GSOS_MAX_THEMES];
    char            active_theme[24];
} OykGsosConfig;

typedef struct {
    int      running;
    gsos_u32 rx_frames;
    gsos_u32 tx_frames;
} OykGsosIfaceRuntime;

typedef struct {
    char callsign[12];
    char iface_id[16];
    char time_hms[10];
} OykGsosNewHeard;

typedef struct {
    gsos_u32 id;
    int      state; /* 0=free, 1=opening, 2=connected */
    int      iface_idx;
    int      is_local_term;
    char     callsign[12];
    char     since_hms[10];
} OykGsosSession;

typedef struct {
    char     key[12];
    char     banned_at[16];
    char     lift_at[16];
    gsos_u32 flags;
    char     reason[48];
} OykGsosBanEntry;

typedef struct {
    OykGsosConfig       cfg;
    gsos_u32            uptime_sec;
    OykGsosIfaceRuntime iface_rt[OYK_GSOS_MAX_IFACES];
    int                 heard_count;
    OykGsosNewHeard     heard[OYK_GSOS_MAX_HEARD];
    OykGsosSession      sessions[OYK_GSOS_MAX_SESSIONS];
    int                 ban_count;
    OykGsosBanEntry     bans[OYK_GSOS_MAX_BANS];
    int                 log_count;
    char                logs[OYK_GSOS_MAX_LOG_LINES][80];
} OykGsosNode;

typedef struct {
    int step;
    int selected_driver_idx;
    OykGsosIfaceConfig draft;
    char error[80];
} OykGsosAddIfaceWiz;

typedef void (*OykGsosThemePreviewFn)(const OykGsosThemeConfig *pal,
                                      const char *active_name,
                                      void *ctx);

typedef struct {
    OykGsosConfig         *draft;
    char                   active_name[24];
    char                   saved_name[24];
    int                    selected_role;
    int                    dirty;
    int                    preview_active;
    int                    default_native;
    int                    os_dark;
    int                    has_os_native;
    OykGsosThemeConfig     os_native_pal;
    gsos_u32               preview_generation;
    OykGsosThemePreviewFn  preview_cb;
    void                  *preview_ctx;
} OykGsosThemeCtrl;

typedef struct {
    int               seen_cr;
    OykGsosLineEnding detected;
    int               has_detected;
} OykGsosTermEolParser;

typedef struct {
    int offset;
    int len;
} OykGsosTermSpan;

/* Metadata contracts */
int                    oyk_gsos_node_menu_count(void);
const OykGsosMenuItem *oyk_gsos_node_menu_item(int idx);
const char            *oyk_gsos_screen_title(int screen_id);
int                    oyk_gsos_settings_tab_count(void);
const char            *oyk_gsos_settings_tab_title(int tab_idx);
int                    oyk_gsos_table_column_count(int table_id);
const char            *oyk_gsos_table_column(int table_id, int col_idx);
int                    oyk_gsos_is_modal_screen(int screen_id);
int                    oyk_gsos_global_hotkey_allowed(int screen_id, int menu_open);

/* Callsign & formatting */
int  oyk_gsos_parse_callsign(const char *text, OykGsosCallsign *out);
void oyk_gsos_format_callsign(const OykGsosCallsign *cs, char *dst, size_t cap);
void oyk_gsos_format_console_banner(const char *callsign, const char *alias,
                                    int accept_in, int conn_count, int iface_count,
                                    char *dst, size_t cap);
void oyk_gsos_format_console_header(const char *callsign, const char *alias,
                                    int conn_count, char *dst, size_t cap);
const char *oyk_gsos_format_incoming_pill(int accept_in);

/* Config & EOL handling */
void              oyk_gsos_config_init_default(OykGsosConfig *cfg);
OykGsosLineEnding oyk_gsos_detect_text_eol(const char *buf, size_t len);
int               oyk_gsos_config_parse(const char *text, size_t len, OykGsosConfig *cfg);
size_t            oyk_gsos_config_serialize(const OykGsosConfig *cfg, char *dst, size_t cap);

/* Node lifecycle & runtime helpers */
void oyk_gsos_node_init(OykGsosNode *node, const OykGsosConfig *cfg);
void oyk_gsos_node_seed_demo(OykGsosNode *node);
void oyk_gsos_node_log(OykGsosNode *node, const char *line);
int  oyk_gsos_node_active_conn_count(const OykGsosNode *node);
int  oyk_gsos_node_auto_peer_count(const OykGsosNode *node);
int  oyk_gsos_node_terminate_session(OykGsosNode *node, gsos_u32 sess_id);
int  oyk_gsos_node_ban_callsign(OykGsosNode *node, const char *call, const char *reason);

/* Add-interface wizard */
void oyk_gsos_wiz_init(OykGsosAddIfaceWiz *wiz, const OykGsosConfig *cfg);
int  oyk_gsos_wiz_driver_count(void);
const char *oyk_gsos_wiz_driver_name(int idx);
const char *oyk_gsos_wiz_driver_desc(int idx);
void oyk_gsos_wiz_select_driver(OykGsosAddIfaceWiz *wiz, const OykGsosConfig *cfg, int idx);
int  oyk_gsos_wiz_advance(OykGsosAddIfaceWiz *wiz, const OykGsosConfig *cfg);
void oyk_gsos_wiz_back(OykGsosAddIfaceWiz *wiz);
const char *oyk_gsos_wiz_step_prompt(const OykGsosAddIfaceWiz *wiz);
void oyk_gsos_wiz_format_summary(const OykGsosAddIfaceWiz *wiz, char *dst, size_t cap);
const char *oyk_gsos_format_learned_policy(int policy);
int  oyk_gsos_cycle_learned_policy(int policy, int delta);

/* Theme controller */
void        oyk_gsos_theme_preset(const char *name, OykGsosThemeConfig *out);
const char *oyk_gsos_theme_role_name(int role);
const char *oyk_gsos_theme_role_key(int role);
void oyk_gsos_theme_init(OykGsosThemeCtrl *tc, OykGsosConfig *draft, int default_native, int os_dark);
void oyk_gsos_theme_set_os_native(OykGsosThemeCtrl *tc, const OykGsosThemeConfig *pal);
void oyk_gsos_theme_set_preview_cb(OykGsosThemeCtrl *tc, OykGsosThemePreviewFn cb, void *ctx);
void oyk_gsos_theme_resolve(const OykGsosThemeCtrl *tc, OykGsosThemeConfig *out);
void oyk_gsos_theme_select(OykGsosThemeCtrl *tc, const char *name);
void oyk_gsos_theme_cycle(OykGsosThemeCtrl *tc, int delta);
int  oyk_gsos_theme_set_role_rgb(OykGsosThemeCtrl *tc, int role, gsos_u32 rgb);
void oyk_gsos_theme_revert(OykGsosThemeCtrl *tc);

/* Terminal helpers */
int  oyk_gsos_term_decode_ctrl_a(int ch);
const char *oyk_gsos_term_hotkey_bar_text(int ctrl_a_pending);
int  oyk_gsos_term_help_count(void);
const char *oyk_gsos_term_help_key(int idx);
const char *oyk_gsos_term_help_action(int idx);
const char *oyk_gsos_term_help_alt(int idx);
int  oyk_gsos_term_wrap_line(const char *text, int len, int width,
                             OykGsosTermSpan *spans, int max_spans);
int  oyk_gsos_term_eol_feed(OykGsosTermEolParser *p, const char *in_buf, int in_len,
                            char *out_buf, int out_cap);

/* Jailed Dual-Pane File Manager helpers (ProDOS/GSOS) */
typedef struct {
    char          name[32];
    unsigned long size;
    int           is_dir;
    int           is_parent;
} OykGsosFileEntry;

typedef struct {
    char             root[32];
    char             rel[48];
    OykGsosFileEntry entries[8];
    int              count;
    int              sel;
} OykGsosFilePane;

typedef struct {
    OykGsosFilePane inc;
    OykGsosFilePane files;
    int             active_pane;
    char            feedback[64];
} OykGsosFileMgr;

void oyk_gsos_filemgr_init(OykGsosFileMgr *fm, const char *inc_root, const char *files_root);
void oyk_gsos_filemgr_pane_title(const OykGsosFilePane *p, const char *label,
                                 char *out, size_t cap);
int  oyk_gsos_filemgr_enter_dir(OykGsosFileMgr *fm, int pane_idx, const char *child);
int  oyk_gsos_filemgr_up(OykGsosFileMgr *fm, int pane_idx);

/* Rig & Printer helpers */
int oyk_gsos_rigctl_build_set_freq(gsos_u32 hz, char *dst, size_t cap);
int oyk_gsos_printer_format_line(const char *proto, const char *msg,
                                 unsigned char *dst, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OYK_GSOS_CORE_H */
