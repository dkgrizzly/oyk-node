/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#ifndef OYK_TI99_CORE_H
#define OYK_TI99_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TI99_MAX_IFACES         6
#define TI99_MAX_PEERS          16
#define TI99_MAX_SESSIONS       8
#define TI99_MAX_MHEARD         5
#define TI99_MAX_LOG_LINES      16
#define TI99_MAX_BANS           8
#define TI99_MAX_FILES          12
#define TI99_MAX_TERM_LINES     24
#define TI99_TERM_COLS          80
#define TI99_MAX_FRAME_LEN      330

#define TI99_EOL_LF             0
#define TI99_EOL_CRLF           1
#define TI99_EOL_CR             2

#define TI99_THEME_ROLES        19

#define TI99_ACT_SETTINGS       1
#define TI99_ACT_ADD_IFACE      2
#define TI99_ACT_BAN            3
#define TI99_ACT_TERM           4
#define TI99_ACT_TERMINATE      5
#define TI99_ACT_FILES          6
#define TI99_ACT_THEME          7
#define TI99_ACT_ABOUT          8
#define TI99_ACT_EXIT           9
#define TI99_ACT_TOGGLE_IN      10

#define TI99_KEY_TAB            0x09
#define TI99_KEY_CTRL_N         0x0E
#define TI99_KEY_ESC            0x1B
#define TI99_KEY_F9             0x109
#define TI99_KEY_F10            0x10A

typedef enum Ti99ScreenId {
    TI99_SCR_MAIN = 0,
    TI99_SCR_MENU,
    TI99_SCR_SETTINGS,
    TI99_SCR_SETTINGS_BBS,
    TI99_SCR_SETTINGS_CHAT,
    TI99_SCR_SETTINGS_IFACES,
    TI99_SCR_PEERS,
    TI99_SCR_SETTINGS_RIG,
    TI99_SCR_SETTINGS_PRINTER,
    TI99_SCR_ADD_IFACE_1,
    TI99_SCR_ADD_IFACE_2,
    TI99_SCR_ADD_IFACE_3,
    TI99_SCR_ADD_IFACE_4,
    TI99_SCR_EDIT_IFACE,
    TI99_SCR_EDIT_PEER,
    TI99_SCR_TERMINATE,
    TI99_SCR_BAN,
    TI99_SCR_EXIT,
    TI99_SCR_THEME,
    TI99_SCR_TERMINAL,
    TI99_SCR_TERMINAL_HELP,
    TI99_SCR_FILES,
    TI99_SCR_ABOUT
} Ti99ScreenId;

typedef struct Ti99Callsign {
    char base[7];
    uint8_t ssid;
} Ti99Callsign;

typedef struct Ti99IfaceConfig {
    char name[12];
    char driver[16];
    char endpoint[32];
    uint16_t port;
    uint16_t baud;
    uint8_t enabled;
    uint8_t allow_same_iface;
    uint8_t learned_policy;
    uint32_t rx_packets;
    uint32_t tx_packets;
} Ti99IfaceConfig;

typedef struct Ti99PeerConfig {
    char callsign[12];
    char host[32];
    uint16_t port;
    char iface[12];
    uint8_t auto_learned;
    uint8_t pinned;
} Ti99PeerConfig;

typedef struct Ti99Session {
    uint16_t id;
    char callsign[12];
    char port_name[12];
    char state[16];
    uint8_t appeal_only;
} Ti99Session;

typedef struct Ti99BanEntry {
    char callsign[12];
    uint8_t hard;
    char reason[24];
} Ti99BanEntry;

typedef struct Ti99FileEntry {
    char name[20];
    uint32_t size_bytes;
    uint8_t is_dir;
    uint8_t is_incoming;
} Ti99FileEntry;

typedef struct Ti99Config {
    char callsign[12];
    char alias[10];
    char bbs_callsign[12];
    char chat_callsign[12];
    uint8_t bbs_enabled;
    uint8_t chat_enabled;
    uint8_t allow_incoming;
    uint8_t eol_mode;
    uint8_t t1_sec;
    uint8_t n2_retries;
    char rig_host[24];
    uint16_t rig_port;
    uint8_t rig_enabled;
    char printer_driver[12];
    char printer_dev[16];
    uint8_t printer_enabled;
    char theme_name[16];
    uint8_t iface_count;
    Ti99IfaceConfig ifaces[TI99_MAX_IFACES];
    uint8_t peer_count;
    Ti99PeerConfig peers[TI99_MAX_PEERS];
} Ti99Config;

typedef struct Ti99KissDecoder {
    uint8_t buf[TI99_MAX_FRAME_LEN];
    size_t len;
    uint8_t in_frame;
    uint8_t escaped;
} Ti99KissDecoder;

typedef struct Ti99Node {
    Ti99Config cfg;
    Ti99Session sessions[TI99_MAX_SESSIONS];
    uint8_t session_count;
    char mheard[TI99_MAX_MHEARD][12];
    uint8_t mheard_count;
    char logs[TI99_MAX_LOG_LINES][64];
    uint8_t log_count;
    Ti99BanEntry bans[TI99_MAX_BANS];
    uint8_t ban_count;
    uint8_t dirty_peers;
    uint32_t uptime_sec;
    uint16_t tx_seq;
} Ti99Node;

typedef struct Ti99App {
    Ti99Node node;
    Ti99ScreenId screen;
    uint8_t settings_tab;
    uint8_t menu_cursor;
    uint8_t selected_iface;
    uint8_t selected_peer;
    uint8_t selected_session;
    uint8_t selected_theme;
    uint8_t wizard_step;
    uint8_t wizard_driver_idx;
    char wizard_name[12];
    char wizard_endpoint[32];
    uint16_t wizard_port;
    uint8_t wizard_allow_same;
    uint8_t wizard_policy;
    uint8_t term_eol;
    uint8_t term_echo;
    char term_lines[TI99_MAX_TERM_LINES][TI99_TERM_COLS + 1];
    uint8_t term_count;
    char term_input[64];
    uint8_t file_pane;
    char incoming_subdir[24];
    char files_subdir[24];
    Ti99FileEntry files[TI99_MAX_FILES];
    uint8_t file_count;
    uint8_t file_cursor;
    char feedback[64];
    uint8_t f18a_detected;
    uint8_t sams_banks;
    uint8_t tipi_present;
    uint8_t rs232_present;
    uint8_t quit_requested;
} Ti99App;

void ti99_be16_write(uint8_t *dst, uint16_t val);
uint16_t ti99_be16_read(const uint8_t *src);
void ti99_be32_write(uint8_t *dst, uint32_t val);
uint32_t ti99_be32_read(const uint8_t *src);

uint16_t ti99_crc16_ccitt(const uint8_t *data, size_t len);
int ti99_parse_callsign(const char *str, Ti99Callsign *out);
void ti99_format_callsign(const Ti99Callsign *cs, char *out, size_t cap);

size_t ti99_ax25_encode_ui(const char *src, const char *dst, const uint8_t *payload,
                           size_t payload_len, uint8_t *out, size_t cap);
int ti99_ax25_decode_header(const uint8_t *frame, size_t len,
                            char *src_out, size_t src_cap,
                            char *dst_out, size_t dst_cap,
                            uint8_t *ctrl_out, uint8_t *pid_out);

size_t ti99_kiss_encode(const uint8_t *frame, size_t len, uint8_t port,
                        uint8_t *out, size_t cap);
void ti99_kiss_init(Ti99KissDecoder *dec);
size_t ti99_kiss_feed(Ti99KissDecoder *dec, uint8_t byte, uint8_t *out, size_t cap);

void ti99_config_init_default(Ti99Config *cfg);
int ti99_config_parse(const char *text, size_t len, Ti99Config *cfg);
size_t ti99_config_serialize(const Ti99Config *cfg, char *out, size_t cap);

void ti99_node_init(Ti99Node *node, int seed_demo);
void ti99_node_log(Ti99Node *node, const char *msg);
void ti99_node_record_mheard(Ti99Node *node, const char *call);
void ti99_node_learn_peer(Ti99Node *node, const char *call, const char *host,
                          uint16_t port, const char *iface);
int ti99_node_is_banned(const Ti99Node *node, const char *call, uint8_t *hard_out);
int ti99_node_add_ban(Ti99Node *node, const char *call, uint8_t hard, const char *reason);
int ti99_node_remove_ban(Ti99Node *node, const char *call);
int ti99_node_terminate_session(Ti99Node *node, uint16_t session_id);
void ti99_node_send_bbs_prompt(Ti99Node *node, const char *call, char *out, size_t cap);
void ti99_node_send_chat_banner(Ti99Node *node, const char *call, char *out, size_t cap);

void ti99_app_init(Ti99App *app, int seed_demo);
int ti99_app_exec_action(Ti99App *app, int action);
int ti99_app_handle_key(Ti99App *app, int key);
int ti99_app_set_screen_by_name(Ti99App *app, const char *name);
const char *ti99_app_screen_title(Ti99ScreenId scr);
uint8_t ti99_app_list_files(const Ti99App *app, uint8_t pane,
                            Ti99FileEntry *out, uint8_t max_items);
int ti99_app_cd(Ti99App *app, uint8_t pane, const char *dir);

#ifdef __cplusplus
}
#endif

#endif /* OYK_TI99_CORE_H */
