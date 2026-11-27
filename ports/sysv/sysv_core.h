/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Shared Strict ANSI C89 Core, Configuration, Protocol, and UI Controller
 * for System V UNIX (Motorola 88K SVR4, AT&T 3B2 SVR3.2) and OpenVMS
 * (VAX, Alpha, Itanium, x86-64).
 */

#ifndef OYK_SYSV_CORE_H
#define OYK_SYSV_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char  sysv_u8;
typedef unsigned short sysv_u16;
typedef unsigned long  sysv_u32;

#define OYK_SYSV_MAX_IFACES     8
#define OYK_SYSV_MAX_PEERS      16
#define OYK_SYSV_MAX_BANS       16
#define OYK_SYSV_MAX_HEARD      5
#define OYK_SYSV_MAX_SESSIONS   12
#define OYK_SYSV_MAX_LOGS       24
#define OYK_SYSV_MAX_THEMES     8
#define OYK_SYSV_MAX_BBS_MSGS   24
#define OYK_SYSV_MAX_DIR_ENTS   24
#define OYK_SYSV_MAX_TERM_LINES 32

typedef enum {
    OYK_SYSV_EOL_LF = 0,
    OYK_SYSV_EOL_CR = 1,
    OYK_SYSV_EOL_CRLF = 2
} OykSysvLineEnding;

typedef enum {
    OYK_SYSV_ACT_NONE = 0,
    OYK_SYSV_ACT_ABOUT,
    OYK_SYSV_ACT_ALLOW_INCOMING,
    OYK_SYSV_ACT_DENY_INCOMING,
    OYK_SYSV_ACT_LOCAL_TERM,
    OYK_SYSV_ACT_FILES,
    OYK_SYSV_ACT_RIG,
    OYK_SYSV_ACT_SETTINGS,
    OYK_SYSV_ACT_PEERS,
    OYK_SYSV_ACT_THEME,
    OYK_SYSV_ACT_TERMINATE,
    OYK_SYSV_ACT_BAN,
    OYK_SYSV_ACT_EXIT,
    OYK_SYSV_ACT_HELP_HOTKEYS
} OykSysvGuiAction;

typedef enum {
    OYK_SYSV_SCR_MAIN = 0,
    OYK_SYSV_SCR_MENU,
    OYK_SYSV_SCR_SETTINGS,
    OYK_SYSV_SCR_ADD_IFACE,
    OYK_SYSV_SCR_EDIT_IFACE,
    OYK_SYSV_SCR_PEERS,
    OYK_SYSV_SCR_EDIT_PEER,
    OYK_SYSV_SCR_TERMINATE,
    OYK_SYSV_SCR_BAN,
    OYK_SYSV_SCR_EXIT_PEERS,
    OYK_SYSV_SCR_THEME,
    OYK_SYSV_SCR_TERMINAL,
    OYK_SYSV_SCR_TERM_HELP,
    OYK_SYSV_SCR_FILES,
    OYK_SYSV_SCR_RIG,
    OYK_SYSV_SCR_ABOUT
} OykSysvScreenId;

typedef enum {
    OYK_SYSV_TBL_IFACES = 0,
    OYK_SYSV_TBL_HEARD,
    OYK_SYSV_TBL_CONNECTIONS,
    OYK_SYSV_TBL_TERMINATE,
    OYK_SYSV_TBL_PEERS,
    OYK_SYSV_TBL_BANS,
    OYK_SYSV_TBL_FILES_INCOMING,
    OYK_SYSV_TBL_FILES_APPROVED,
    OYK_SYSV_TBL_THEME
} OykSysvTableId;

typedef enum {
    OYK_SYSV_TR_BG = 0,
    OYK_SYSV_TR_FG,
    OYK_SYSV_TR_BORDER,
    OYK_SYSV_TR_HEADER_BG,
    OYK_SYSV_TR_HEADER_FG,
    OYK_SYSV_TR_STATUS_BG,
    OYK_SYSV_TR_STATUS_FG,
    OYK_SYSV_TR_MENU_BG,
    OYK_SYSV_TR_MENU_FG,
    OYK_SYSV_TR_SELECTION_BG,
    OYK_SYSV_TR_SELECTION_FG,
    OYK_SYSV_TR_PANEL_BG,
    OYK_SYSV_TR_PANEL_FG,
    OYK_SYSV_TR_INPUT_BG,
    OYK_SYSV_TR_INPUT_FG,
    OYK_SYSV_TR_ACCENT,
    OYK_SYSV_TR_OK,
    OYK_SYSV_TR_WARN,
    OYK_SYSV_TR_ERROR,
    OYK_SYSV_THEME_COLOR_COUNT
} OykSysvThemeRole;

typedef enum {
    OYK_SYSV_TCMD_NONE = 0,
    OYK_SYSV_TCMD_HELP,
    OYK_SYSV_TCMD_CLOSE,
    OYK_SYSV_TCMD_CLEAR,
    OYK_SYSV_TCMD_WRAP,
    OYK_SYSV_TCMD_EOL,
    OYK_SYSV_TCMD_ECHO,
    OYK_SYSV_TCMD_UPLOAD,
    OYK_SYSV_TCMD_LITERAL_CTRL_A
} OykSysvTermCmd;

typedef struct {
    int         action;
    const char *label;
    const char *alt_label;
    char        shortcut_key;
    int         is_separator;
} OykSysvMenuItem;

typedef struct {
    char    base[8];
    sysv_u8 ssid;
} OykSysvCallsign;

typedef struct {
    OykSysvCallsign dst;
    OykSysvCallsign src;
    int             is_command;
    int             is_response;
    sysv_u8         control;
    sysv_u8         pid;
    sysv_u8         info[256];
    size_t          info_len;
} OykSysvFrame;

typedef struct {
    sysv_u8 buf[512];
    size_t  len;
    int     in_frame;
    int     escaped;
} OykSysvKissDecoder;

typedef struct {
    char     id[16];
    char     type[16];
    char     description[48];
    char     host[48];
    sysv_u16 port;
    char     netmask[20];
    char     device[48];
    sysv_u32 baud;
    int      allow_inbound;
    int      allow_outbound;
    int      allow_same_interface;
    int      learned_peer_policy; /* 0=inherit, 1=none, 2=inbound, 3=outbound, 4=both */
} OykSysvIfaceConfig;

typedef struct {
    char     id[16];
    char     callsign[12];
    char     description[48];
    char     interface_id[16];
    char     host[48];
    sysv_u16 port;
    int      allow_inbound;
    int      allow_outbound;
    int      auto_learned;
} OykSysvPeerConfig;

typedef struct {
    char     name[24];
    int      is_dark_base;
    sysv_u32 colors[OYK_SYSV_THEME_COLOR_COUNT];
} OykSysvThemeConfig;

typedef struct {
    OykSysvCallsign    callsign;
    OykSysvCallsign    alias;
    int                accept_incoming;
    OykSysvLineEnding  line_ending;
    int                has_detected_eol;
    int                bbs_enabled;
    sysv_u8            bbs_ssid;
    int                bbs_files_enabled;
    char               bbs_mail_path[64];
    char               bbs_files_path[64];
    char               incoming_path[64];
    int                chat_enabled;
    sysv_u8            chat_ssid;
    char               chat_topic[32];
    int                rig_enabled;
    char               rig_protocol[16];
    char               rig_host[48];
    sysv_u16           rig_port;
    int                rig_poll_sec;
    int                printer_enabled;
    char               printer_device[48];
    char               printer_protocol[16];
    sysv_u32           printer_baud;
    int                printer_condensed;
    int                iface_count;
    OykSysvIfaceConfig ifaces[OYK_SYSV_MAX_IFACES];
    int                peer_count;
    OykSysvPeerConfig  peers[OYK_SYSV_MAX_PEERS];
    int                theme_count;
    OykSysvThemeConfig themes[OYK_SYSV_MAX_THEMES];
    char               active_theme[24];
} OykSysvConfig;

typedef struct {
    int      running;
    sysv_u32 rx_frames;
    sysv_u32 tx_frames;
    char     status_text[32];
} OykSysvIfaceRuntime;

typedef struct {
    char callsign[12];
    char iface_id[16];
    char time_hms[10];
} OykSysvNewHeard;

typedef struct {
    sysv_u32 id;
    int      state; /* 0=free, 1=opening, 2=connected */
    int      iface_idx;
    int      is_local_term;
    char     callsign[12];
    char     port_id[16];
    char     dir[8];
    char     since_hms[10];
    char     role[12];
} OykSysvSession;

typedef struct {
    char     key[12];
    char     banned_at[18];
    char     lift_at[18];
    sysv_u32 flags;
    char     reason[64];
} OykSysvBanEntry;

typedef struct {
    char               platform_label[48];
    OykSysvConfig      cfg;
    sysv_u32           uptime_sec;
    OykSysvIfaceRuntime iface_rt[OYK_SYSV_MAX_IFACES];
    int                heard_count;
    OykSysvNewHeard    heard[OYK_SYSV_MAX_HEARD];
    OykSysvSession     sessions[OYK_SYSV_MAX_SESSIONS];
    int                ban_count;
    OykSysvBanEntry    bans[OYK_SYSV_MAX_BANS];
    int                log_count;
    char               logs[OYK_SYSV_MAX_LOGS][96];
} OykSysvNode;

typedef struct {
    int                step; /* 0:Driver, 1:Identity, 2:Endpoint/Policy, 3:Review */
    int                selected_driver_idx;
    OykSysvIfaceConfig draft;
    char               error[96];
} OykSysvAddIfaceWiz;

typedef void (*OykSysvThemePreviewFn)(const OykSysvThemeConfig *pal,
                                      const char *active_name,
                                      void *ctx);

typedef struct {
    OykSysvConfig        *draft;
    char                  active_name[24];
    char                  saved_name[24];
    int                   selected_role;
    int                   dirty;
    int                   preview_active;
    int                   default_native;
    int                   os_dark;
    OykSysvThemeConfig    os_native_pal;
    sysv_u32              preview_generation;
    OykSysvThemePreviewFn preview_cb;
    void                 *preview_ctx;
} OykSysvThemeCtrl;

typedef struct {
    int               seen_cr;
    OykSysvLineEnding eol;
    int               echo;
    int               word_wrap;
} OykSysvTermEolParser;

typedef struct {
    int offset;
    int len;
} OykSysvTermSpan;

typedef struct {
    char          name[48];
    unsigned long size;
    int           is_dir;
    int           is_parent; /* 1 ONLY inside subdirectories */
} OykSysvFileEntry;

typedef struct {
    char             root[64];
    char             rel[64];
    OykSysvFileEntry entries[OYK_SYSV_MAX_DIR_ENTS];
    int              count;
    int              sel;
} OykSysvFilePane;

typedef struct {
    OykSysvFilePane inc;
    OykSysvFilePane files;
    int             active_pane; /* 0 = Incoming, 1 = Files */
    char            feedback[96];
    int             preview_active;
    char            preview_text[256];
} OykSysvFileMgr;

typedef struct {
    OykSysvNode          node;
    int                  screen;
    int                  settings_tab;
    int                  menu_sel;
    int                  iface_sel;
    int                  peer_sel;
    int                  term_sess_sel;
    int                  ban_sel;
    int                  exit_btn_sel;
    int                  term_help_from_term;
    OykSysvAddIfaceWiz   wiz;
    OykSysvThemeCtrl     theme_ctrl;
    OykSysvFileMgr       fm;
    OykSysvTermEolParser term_eol;
    int                  term_ctrl_a_pending;
    int                  term_line_count;
    char                 term_lines[OYK_SYSV_MAX_TERM_LINES][84];
    char                 term_input[84];
    char                 term_status[40];
    char                 feedback[96];
    int                  running;
} OykSysvApp;

/* String & byte helpers (strict C89, endian-neutral) */
void oyk_sysv_copy_str(char *dst, size_t cap, const char *src);
int  oyk_sysv_strieq(const char *a, const char *b);
void oyk_sysv_be16_write(sysv_u8 *out, sysv_u16 v);
sysv_u16 oyk_sysv_be16_read(const sysv_u8 *in);
void oyk_sysv_be32_write(sysv_u8 *out, sysv_u32 v);
sysv_u32 oyk_sysv_be32_read(const sysv_u8 *in);

/* Metadata contracts */
int                     oyk_sysv_node_menu_count(void);
const OykSysvMenuItem  *oyk_sysv_node_menu_item(int idx);
const char             *oyk_sysv_screen_title(int screen_id);
int                     oyk_sysv_settings_tab_count(void);
const char             *oyk_sysv_settings_tab_title(int tab_idx);
int                     oyk_sysv_table_column_count(int table_id);
const char             *oyk_sysv_table_column(int table_id, int col_idx);
const char             *oyk_sysv_about_summary(void);

/* Callsign, AX.25, CRC-16, KISS & appeal hygiene */
int      oyk_sysv_parse_callsign(const char *text, OykSysvCallsign *out);
void     oyk_sysv_format_callsign(const OykSysvCallsign *cs, char *dst, size_t cap);
int      oyk_sysv_callsign_eq(const OykSysvCallsign *a, const OykSysvCallsign *b);
sysv_u16 oyk_sysv_crc16_fcs(const sysv_u8 *data, size_t len);
size_t   oyk_sysv_ax25_encode(const OykSysvFrame *f, sysv_u8 *out, size_t cap);
int      oyk_sysv_ax25_decode(const sysv_u8 *raw, size_t len, OykSysvFrame *out);
size_t   oyk_sysv_kiss_encode(sysv_u8 kiss_port, const sysv_u8 *payload, size_t len,
                              sysv_u8 *out, size_t cap);
void     oyk_sysv_kiss_init(OykSysvKissDecoder *dec);
int      oyk_sysv_kiss_feed(OykSysvKissDecoder *dec, sysv_u8 b,
                            sysv_u8 *out_port, sysv_u8 *out_cmd);
void     oyk_sysv_sanitize_appeal(const char *raw, char *out, size_t cap);
int      oyk_sysv_is_safe_filename(const char *name);

/* Formatting & validation */
void        oyk_sysv_format_duration(sysv_u32 secs, char *dst, size_t cap);
void        oyk_sysv_format_console_header(const char *callsign, const char *alias,
                                           sysv_u32 uptime_sec, int conn_count,
                                           int accept_in, int compact,
                                           char *dst, size_t cap);
const char *oyk_sysv_format_incoming_pill(int accept_in, int compact);
const char *oyk_sysv_format_learned_policy(int policy);
int         oyk_sysv_cycle_learned_policy(int policy, int delta);

/* Config & EOL preservation */
void              oyk_sysv_config_init_default(OykSysvConfig *cfg);
OykSysvLineEnding oyk_sysv_detect_text_eol(const char *buf, size_t len);
int               oyk_sysv_config_parse(const char *text, size_t len, OykSysvConfig *cfg);
size_t            oyk_sysv_config_serialize(const OykSysvConfig *cfg, char *dst, size_t cap);
int               oyk_sysv_config_load_file(const char *path, OykSysvConfig *cfg);
int               oyk_sysv_config_save_file(const OykSysvConfig *cfg, const char *path);

/* Node lifecycle & demo seed */
void oyk_sysv_node_init(OykSysvNode *node, const OykSysvConfig *cfg, const char *platform_label);
void oyk_sysv_node_seed_demo(OykSysvNode *node);
void oyk_sysv_node_log(OykSysvNode *node, const char *line);
int  oyk_sysv_node_active_conn_count(const OykSysvNode *node);
int  oyk_sysv_node_auto_peer_count(const OykSysvNode *node);
int  oyk_sysv_node_terminate_session(OykSysvNode *node, sysv_u32 sess_id);
int  oyk_sysv_node_ban_callsign(OykSysvNode *node, const char *call, const char *reason);

/* Add-interface wizard */
void        oyk_sysv_wiz_init(OykSysvAddIfaceWiz *wiz, const OykSysvConfig *cfg);
int         oyk_sysv_wiz_driver_count(void);
const char *oyk_sysv_wiz_driver_name(int idx);
const char *oyk_sysv_wiz_driver_desc(int idx);
void        oyk_sysv_wiz_select_driver(OykSysvAddIfaceWiz *wiz, const OykSysvConfig *cfg, int idx);
int         oyk_sysv_wiz_advance(OykSysvAddIfaceWiz *wiz, const OykSysvConfig *cfg);
void        oyk_sysv_wiz_back(OykSysvAddIfaceWiz *wiz);
const char *oyk_sysv_wiz_step_prompt(const OykSysvAddIfaceWiz *wiz);
void        oyk_sysv_wiz_format_summary(const OykSysvAddIfaceWiz *wiz, char *dst, size_t cap);

/* Theme controller */
void        oyk_sysv_theme_preset(const char *name, OykSysvThemeConfig *out);
const char *oyk_sysv_theme_role_name(int role);
const char *oyk_sysv_theme_role_key(int role);
void        oyk_sysv_theme_init(OykSysvThemeCtrl *tc, OykSysvConfig *draft,
                                int default_native, int os_dark);
void        oyk_sysv_theme_set_preview_cb(OykSysvThemeCtrl *tc,
                                          OykSysvThemePreviewFn cb, void *ctx);
void        oyk_sysv_theme_resolve(const OykSysvThemeCtrl *tc, OykSysvThemeConfig *out);
void        oyk_sysv_theme_select(OykSysvThemeCtrl *tc, const char *name);
void        oyk_sysv_theme_cycle(OykSysvThemeCtrl *tc, int delta);
void        oyk_sysv_theme_revert(OykSysvThemeCtrl *tc);

/* Terminal helpers */
int         oyk_sysv_term_decode_ctrl_a(int ch);
const char *oyk_sysv_term_hotkey_bar_text(int ctrl_a_pending);
int         oyk_sysv_term_help_count(void);
const char *oyk_sysv_term_help_key(int idx);
const char *oyk_sysv_term_help_action(int idx);
const char *oyk_sysv_term_help_alt(int idx);
int         oyk_sysv_term_wrap_line(const char *text, int len, int width,
                                    OykSysvTermSpan *spans, int max_spans);

/* Jailed dual-pane File Manager */
int  oyk_sysv_resolve_safe_path(const char *root, const char *cur_rel,
                                const char *user_arg, int allow_parent_in_root,
                                char *abs_out, size_t abs_sz,
                                char *rel_out, size_t rel_sz,
                                char *err_out, size_t err_sz);
void oyk_sysv_filemgr_init(OykSysvFileMgr *fm, const char *inc_root, const char *files_root);
void oyk_sysv_filemgr_pane_title(const OykSysvFilePane *p, const char *label,
                                 char *out, size_t cap);
int  oyk_sysv_filemgr_enter_dir(OykSysvFileMgr *fm, int pane_idx, const char *child);
int  oyk_sysv_filemgr_up(OykSysvFileMgr *fm, int pane_idx);

/* Unified operator application state */
void oyk_sysv_app_init(OykSysvApp *app, const char *platform_label, int seed_demo);
void oyk_sysv_app_exec_menu_action(OykSysvApp *app, int action);
void oyk_sysv_app_term_append(OykSysvApp *app, const char *text);

/* Rig CAT & Printer line helpers */
int oyk_sysv_rigctl_build_set_freq(sysv_u32 hz, char *dst, size_t cap);
int oyk_sysv_printer_format_line(const char *proto, const char *msg,
                                 unsigned char *dst, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OYK_SYSV_CORE_H */
