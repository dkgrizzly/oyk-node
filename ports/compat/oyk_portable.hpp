/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PORTABLE_HPP
#define OYK_PORTABLE_HPP

/*
 * Portable C++98 / 16-bit-clean Packet Node Core for:
 *   - MS-DOS (OpenWatcom 1.9, 8088/NEC V20, 640KB RAM + optional EMS 4.0, mTCP)
 *   - Windows 3.1 (16-bit OpenWatcom 1.9, Winsock 1.1)
 *   - Windows 95..ME & Windows NT 3.51..Windows 11 (32-bit & 64-bit, Winsock 1.1)
 *
 * Uses zero STL, zero exceptions, zero RTTI, and bounded memory tables.
 */

#include <stddef.h>
#include <stdio.h>
#include <time.h>

/* Fixed-width integer types safe across 16-bit (int=16, long=32) and 32/64-bit targets */
typedef unsigned char  oyk_u8;
typedef unsigned short oyk_u16;
#if defined(__LP64__)
typedef unsigned int   oyk_u32;
#else
typedef unsigned long  oyk_u32;
#endif

#include "oyk_term.hpp"

/* Tunable memory bounds (defaults fit comfortably within 640KB IBM XT conventional RAM) */
#ifndef OYK_MAX_INTERFACES
#if defined(TEENSYDUINO)
#define OYK_MAX_INTERFACES   10
#else
#define OYK_MAX_INTERFACES   4
#endif
#endif
#ifndef OYK_MAX_SESSIONS
#if defined(TEENSYDUINO)
#define OYK_MAX_SESSIONS     16
#else
#define OYK_MAX_SESSIONS     8
#endif
#endif
#ifndef OYK_MAX_MHEARD
#define OYK_MAX_MHEARD       16
#endif
#ifndef OYK_MAX_NEW_HEARD
#define OYK_MAX_NEW_HEARD    5
#endif
#ifndef OYK_MAX_BANS
#define OYK_MAX_BANS         32
#endif
#ifndef OYK_MAX_LOG_FILES
#define OYK_MAX_LOG_FILES    4
#endif
#ifndef OYK_MAX_CONSOLE_LOGS
#define OYK_MAX_CONSOLE_LOGS 48
#endif
#ifndef OYK_MAX_PEERS
#define OYK_MAX_PEERS        8
#endif
#ifndef OYK_MAX_CONFIG_PEERS
#if defined(__DOS__) || (defined(__WINDOWS__) && !defined(__NT__))
#define OYK_MAX_CONFIG_PEERS 12
#else
#define OYK_MAX_CONFIG_PEERS 32
#endif
#endif
#ifndef OYK_MAX_THEMES
#define OYK_MAX_THEMES       8
#endif

/* Learned peer admission policies */
#define OYK_LEARN_INHERIT    0x00U
#define OYK_LEARN_NONE       0x01U
#define OYK_LEARN_INBOUND    0x02U
#define OYK_LEARN_OUTBOUND   0x03U
#define OYK_LEARN_BOTH       0x04U
#ifndef OYK_MAX_FRAME_INFO
#define OYK_MAX_FRAME_INFO   256
#endif
#ifndef OYK_MAX_APPEAL_LEN
#define OYK_MAX_APPEAL_LEN   140
#endif
#ifndef OYK_MAX_BBS_MSGS
#define OYK_MAX_BBS_MSGS     32
#endif
#ifndef OYK_MAX_BBS_BODY
#if defined(__DOS__) || (defined(__WINDOWS__) && !defined(__NT__))
#define OYK_MAX_BBS_BODY     512
#else
#define OYK_MAX_BBS_BODY     2048
#endif
#endif

/* Line ending conventions for config files ONLY (never overrides wire protocols) */
enum OykLineEnding {
    OYK_EOL_LF   = 0,
    OYK_EOL_CRLF = 1,
    OYK_EOL_CR   = 2
};

/* Severity bitmasks */
enum OykLogLevel {
    OYK_LOG_SILENT  = 0x00,
    OYK_LOG_ERROR   = 0x01,
    OYK_LOG_WARNING = 0x02,
    OYK_LOG_INFO    = 0x04,
    OYK_LOG_KICK    = 0x10,
    OYK_LOG_BAN     = 0x20,
    OYK_LOG_APPEAL  = 0x40,
    OYK_LOG_DEBUG   = 0x80
};

#define OYK_DEFAULT_CONSOLE_MASK  0x77U
#define OYK_DEFAULT_NODELOG_MASK  0x37U
#define OYK_DEFAULT_APPEALS_MASK  0x40U

/* Ban entry flags (4-bit bitfield: 0b0000) */
#define OYK_BAN_APPEAL_ALLOWED  0x01U
#define OYK_BAN_NOTIFIED        0x02U
#define OYK_BAN_APPEAL_USED     0x04U
#define OYK_BAN_HARD_BAN        0x08U

/* AX.25 Frame Types */
enum OykFrameType {
    OYK_FT_I = 0,
    OYK_FT_RR,
    OYK_FT_RNR,
    OYK_FT_REJ,
    OYK_FT_SABM,
    OYK_FT_DISC,
    OYK_FT_DM,
    OYK_FT_UA,
    OYK_FT_UI,
    OYK_FT_UNKNOWN
};

struct OykCallsign {
    char base[7]; /* 1..6 uppercase ASCII + NUL */
    oyk_u8 ssid;  /* 0..15 */
};

struct OykFrame {
    OykCallsign dst;
    OykCallsign src;
    int is_command;
    int is_response;
    oyk_u8 control;
    int has_pid;
    oyk_u8 pid;
    oyk_u16 info_len;
    oyk_u8 info[OYK_MAX_FRAME_INFO];
};

struct OykPeerEntry {
    char callsign[12]; /* Empty string = wildcard/default peer */
    oyk_u32 ip;        /* Network byte order IPv4 */
    oyk_u16 port;
};

struct OykPeerConfig {
    char id[16];
    char callsign[12];
    char description[40];
    char interface_id[16];
    char host[32];
    oyk_u32 resolved_ip; /* Network byte order IPv4 (0 if unresolved/serial) */
    oyk_u16 port;
    oyk_u8 allow_inbound;
    oyk_u8 allow_outbound;
    oyk_u8 auto_learned;
};

struct OykIfaceConfig {
    char id[16];
    char type[16];       /* "axudp", "axip", "kiss-serial", "kiss-tcp", "telnet" */
    char description[40];
    char host[32];
    char netmask[16];    /* Optional IPv4 netmask for axudp/axip (e.g. "255.0.0.0") */
    oyk_u16 port;
    char device[24];     /* e.g. "COM1" or "/dev/ttyS0" */
    oyk_u32 baud;
    oyk_u8 kiss_port;
    int server_mode;
    oyk_u8 block_inbound;        /* 0 = allow (default), 1 = deny */
    oyk_u8 block_outbound;       /* 0 = allow (default for non-telnet), 1 = deny (forced 1 for telnet) */
    oyk_u8 allow_same_interface; /* 0 = block same-iface forwarding (default), 1 = allow */
    oyk_u8 no_auto_learn;        /* 0 = auto-learn enabled on non-telnet, 1 = disabled */
    oyk_u8 learned_peer_policy; /* OYK_LEARN_INHERIT .. OYK_LEARN_BOTH */
    int peer_count;
    OykPeerEntry peers[OYK_MAX_PEERS];
};

struct OykLogFileEntry {
    char filename[32];
    oyk_u32 mask;
    char active_date[10]; /* YYYYMMDD */
};

enum OykThemeRole {
    OYK_TR_BG = 0,
    OYK_TR_FG,
    OYK_TR_PANEL_BG,
    OYK_TR_PANEL_FG,
    OYK_TR_BORDER,
    OYK_TR_HEADER_BG,
    OYK_TR_HEADER_FG,
    OYK_TR_ACCENT,
    OYK_TR_STATUS_BG,
    OYK_TR_STATUS_FG,
    OYK_TR_SELECTION_BG,
    OYK_TR_SELECTION_FG,
    OYK_TR_INPUT_BG,
    OYK_TR_INPUT_FG,
    OYK_TR_BUTTON_BG,
    OYK_TR_BUTTON_FG,
    OYK_TR_OK,
    OYK_TR_WARN,
    OYK_TR_ERROR,
    OYK_THEME_COLOR_COUNT
};

struct OykThemeConfig {
    char   name[24];
    oyk_u8 is_dark_base; /* 1 = dark, 0 = light */
    oyk_u32 colors[OYK_THEME_COLOR_COUNT];
};

struct OykConfig {
    OykCallsign callsign;
    OykCallsign alias;
    char welcome_text[96];
    char info_text[80];
    char source_path[128];
    char banlist_path[128];
    char interfaces_file[64];
    char peers_file[64];
    char theme_name[24];
    char themes_file[64];
    char log_path[96];
    oyk_u32 console_level;
    int rotate_daily;
    int accept_incoming;
    int allow_flash_logging;
    int amiga_private_screen;
    oyk_u8 learned_peer_policy;
    char new_user_welcome[96];
    OykLineEnding line_ending;
    int has_detected_eol;

    int bbs_enabled;
    int bbs_files_enabled;
    int bbs_files_case_insensitive;
    oyk_u8 bbs_ssid;  /* Default 11 -> <callsign.base>-11 */
    OykCallsign bbs_callsign;
    OykCallsign bbs_alias;
    char bbs_mail_path[96];
    char bbs_files_path[96];
    char bbs_welcome[96];
    oyk_u16 bbs_max_msg_bytes;

    int chat_enabled;
    oyk_u8 chat_ssid; /* Default 12 -> <callsign.base>-12 */
    OykCallsign chat_callsign;
    OykCallsign chat_alias;
    char chat_topic[16];
    char chat_welcome[96];

    int rig_enabled;
    char rig_protocol[16]; /* "rigctl", "radiofoundry", or "none" */
    char rig_host[32];
    oyk_u16 rig_port;      /* 4532 for rigctl, 4191 for radiofoundry */
    oyk_u16 rig_poll_sec;  /* Default 30s keepalive */
    int rig_notify;        /* Receive RadioFoundry UDP 4191 notifications */

    int printer_enabled;
    char printer_device[48];   /* e.g. "/dev/lp0", "LPT1:", "usb-printer", "ser1" */
    char printer_protocol[16]; /* "generic", "mx80", "escp2" */
    oyk_u32 printer_baud;      /* Serial baud rate for serial printers (e.g. 9600) */
    oyk_u32 printer_level;     /* Severity bitmask (default 0x77) */
    int printer_condensed;     /* 1 = 132-col condensed mode on MX-80 / ESC/P2 */

    int log_file_count;
    OykLogFileEntry log_files[OYK_MAX_LOG_FILES];

    int iface_count;
    OykIfaceConfig ifaces[OYK_MAX_INTERFACES];

    int peer_count;
    OykPeerConfig peers[OYK_MAX_CONFIG_PEERS];

    int theme_count;
    OykThemeConfig themes[OYK_MAX_THEMES];
};

struct OykBanEntry {
    char key[12];
    char banned_at[16]; /* YYYYMMDD-HHMMSS */
    char lift_at[16];   /* YYYYMMDD-HHMMSS or "permanent" */
    oyk_u32 flags;
    char reason[96];
    char last_check_date[10]; /* YYYYMMDD (runtime daily check for soft bans) */
};

struct OykNewHeard {
    char callsign[12];
    char iface_id[16];
    char time_hms[10];
};

struct OykMHeard {
    char callsign[12];
    char iface_id[16];
    char time_hms[10];
    oyk_u32 packets;
};

enum OykSessState {
    OYK_SESS_FREE = 0,
    OYK_SESS_CONNECTING,
    OYK_SESS_CONNECTED,
    OYK_SESS_DISCONNECTING
};

struct OykBbsMsgMeta {
    oyk_u32 id;
    char type;       /* 'P', 'B', 'T' */
    char status;     /* 'N', 'Y', '$', 'K' */
    char from[12];
    char to[12];
    char at_bbs[12];
    char date[16];
    char subject[48];
    oyk_u16 size;
};

struct OykPendingIncoming {
    char uploader[16];
    char filename[32];
    oyk_u32 size;
};

struct OykApprovedFile {
    char rel_path[96];
    oyk_u32 size;
};

struct OykConfigApplyResult {
    int applied;
    int saved_to_disk;
    int identity_restarted;
    int interfaces_changed;
    int signed_off_sessions;
    char save_path[128];
    char warning[160];
    char error[96];
};

struct OykSession {
    oyk_u32 id;
    int iface_idx;
    OykCallsign local_call;
    OykCallsign remote_call;
    oyk_u32 peer_ip;      /* IPv4 in network byte order */
    oyk_u16 peer_port;
    oyk_u16 peer_handle;  /* e.g. socket index for Telnet/KISS */
    OykSessState state;
    int is_local_term;     /* 1 = local operator terminal */
    int is_local_loopback; /* 1 = connected locally to this node (YAPP disallowed) */
    int in_chat;           /* 1 = participant in CHAT conference */
    int chat_direct;       /* 1 = connected directly to CHAT SSID */
    char chat_topic[16];
    char chat_name[16];
    int in_bbs;            /* 1 = active in Packet BBS */
    int bbs_direct;        /* 1 = connected directly to BBS SSID */
    int bbs_stage;         /* 0=cmd, 1=subject, 2=body */
    char bbs_draft_type;
    char bbs_draft_to[12];
    char bbs_draft_at[12];
    char bbs_draft_subj[48];
    oyk_u16 bbs_draft_len;
    char bbs_draft_body[OYK_MAX_BBS_BODY];
    char bbs_files_rel[64];
    int bbs_yapp_active;
    int bbs_yapp_upload;
    int restricted_ban;
    int appeal_eligible;
    char ban_key[12];
    oyk_u8 vs;
    oyk_u8 vr;
    char since_hms[10];
    time_t last_activity;
    char rx_line[160];
    int rx_line_len;
    OykTermEolParser rx_eol;
};

struct OykIfaceRuntime {
    OykIfaceConfig cfg;
    int running;
    char status[36];
    oyk_u32 rx_frames;
    oyk_u32 tx_frames;
    oyk_u32 rx_errors;
    oyk_u32 tx_errors;
};

/* Transport transmit callback implemented by DOS / Win16 / Win32 host */
typedef int (*OykTxCallback)(int iface_idx,
                             const OykFrame* frame,
                             oyk_u32 peer_ip,
                             oyk_u16 peer_port,
                             oyk_u16 peer_handle,
                             void* user_ctx);

/* Local terminal event callback: event_type 1=connected, 2=rx_bytes, 3=closed */
typedef void (*OykLocalEventCallback)(oyk_u32 session_id,
                                      int event_type,
                                      const oyk_u8* data,
                                      size_t len,
                                      const char* reason,
                                      void* user_ctx);

/* Direct line-by-line printer log callback implemented by platform ports */
typedef void (*OykPrinterWriteCallback)(oyk_u32 level,
                                        const char* formatted_line,
                                        void* user_ctx);

/* KISS framing helpers */
struct OykKissDecoder {
    int in_frame;
    int escaped;
    int overflowed;
    oyk_u16 len;
    oyk_u8 buf[384];
};

/* Core utility API */
OykLineEnding oyk_platform_default_eol();
const char* oyk_eol_bytes(OykLineEnding eol);
OykLineEnding oyk_detect_first_eol(const char* buf, size_t len,
                                   OykLineEnding fallback,
                                   int* found_any_out, int* mixed_out);

#define OYK_DEFAULT_NODE_SSID     7U

int oyk_parse_callsign(const char* text, OykCallsign* out);
/* Parses callsign; if no explicit '-SSID' is given, takes base callsign and uses default_ssid. */
int oyk_parse_callsign_with_default_ssid(const char* text, oyk_u8 default_ssid, OykCallsign* out);
void oyk_format_callsign(const OykCallsign* call, char* out, size_t out_sz);
int oyk_callsign_eq(const OykCallsign* a, const OykCallsign* b);
/* Parse dotted-quad IPv4 into network-byte-order oyk_u32; returns 1 on success. */
int oyk_parse_ipv4(const char* text, oyk_u32* net_ip_out);
/* RadioFoundry DMR ID (1..16777214) <-> IPv4 helpers. */
int oyk_is_all_digits(const char* text);
int oyk_parse_dmr_id(const char* text, oyk_u32* dmr_id_out);
void oyk_dmr_id_to_ipv4(oyk_u32 dmr_id, oyk_u8 prefix_octet, char* out, size_t out_sz);
oyk_u8 oyk_dmr_prefix_for_iface(const OykIfaceConfig* ic);
int oyk_resolve_host_or_dmr_id(const char* spec, oyk_u8 prefix_octet,
                               char* out, size_t out_sz, oyk_u32* net_ip_out);
int oyk_ip_matches_netmask(oyk_u32 ip_net, oyk_u32 bind_net, oyk_u32 mask_net);
/* Parse "CALLSIGN@ip:port", "CALLSIGN ip:port", or "ip:port" peer spec. */
int oyk_parse_peer_spec(const char* spec, char* call_out, size_t call_sz,
                        oyk_u32* net_ip_out, oyk_u16* port_out, oyk_u16 default_port);
void oyk_format_peer_spec(const OykPeerEntry* peer, char* out, size_t out_sz);

oyk_u16 oyk_crc16_fcs(const oyk_u8* data, size_t len);
int oyk_verify_fcs(const oyk_u8* data, size_t total_len);
OykFrameType oyk_frame_type(const OykFrame* f);
int oyk_ax25_decode(const oyk_u8* raw, size_t len, OykFrame* out);
size_t oyk_ax25_encode(const OykFrame* f, oyk_u8* out, size_t cap);

size_t oyk_kiss_encode(oyk_u8 kiss_port, const oyk_u8* payload, size_t len,
                       oyk_u8* out, size_t cap);
void oyk_kiss_decoder_init(OykKissDecoder* dec);
/* Feed one byte; returns 1 when a complete frame is ready in dec->buf[0..dec->len-1] */
int oyk_kiss_decoder_feed_byte(OykKissDecoder* dec, oyk_u8 b,
                               oyk_u8* out_port, oyk_u8* out_cmd);

int oyk_parse_flags_u32(const char* text, oyk_u32* out);
int oyk_parse_log_mask(const char* text, oyk_u32* out);
/* Parse user-friendly ban lift spec (permanent, 30m, 12h, 7d, YYYY-MM-DD, YYYYMMDD-HHMMSS) */
int oyk_parse_lift_spec(const char* text, time_t now, char* out, size_t out_sz);
void oyk_sanitize_appeal(const char* raw, char* out, size_t out_sz);
void oyk_format_now_compact(time_t t, char* out, size_t out_sz);
void oyk_format_now_yyyymmdd(time_t t, char* out, size_t out_sz);
void oyk_format_now_hms(time_t t, char* out, size_t out_sz);

/* Config & Banlist I/O (accepts CRLF, CR, LF; preserves EOL on save) */
void oyk_config_init_default(OykConfig* cfg);
int oyk_config_load(const char* path, OykConfig* cfg, char* err_out, size_t err_sz);
int oyk_config_save(const OykConfig* cfg, const char* path);
void oyk_config_resolve_banlist(const OykConfig* cfg, char* out, size_t out_sz);
void oyk_config_resolve_interfaces(const OykConfig* cfg, char* out, size_t out_sz);
void oyk_config_resolve_peers(const OykConfig* cfg, char* out, size_t out_sz);
void oyk_config_resolve_themes(const OykConfig* cfg, char* out, size_t out_sz);

/* Theme helpers (strict C++98, 16-bit safe) */
const char* oyk_theme_role_key(int role);
const char* oyk_theme_role_label(int role);
int         oyk_theme_role_index(const char* key);
int         oyk_theme_is_builtin(const char* name);
int         oyk_theme_parse_hex(const char* text, oyk_u32* rgb_out);
void        oyk_theme_format_hex(oyk_u32 rgb, char* out, size_t out_sz);
void        oyk_theme_init_builtin(OykThemeConfig* out, int light_mode);
void        oyk_theme_init_dos_native(OykThemeConfig* out);
void        oyk_theme_make_clone_name(const OykConfig* cfg, int is_dark_base,
                                      char* out, size_t out_sz);
void        oyk_theme_resolve_effective(const OykConfig* cfg, int default_to_native,
                                        int os_prefers_dark, OykThemeConfig* out);
oyk_u8      oyk_rgb_to_vga16(oyk_u32 rgb);
int         oyk_rgb_to_ansi16(oyk_u32 rgb);
int         oyk_rgb_to_xterm256(oyk_u32 rgb);
void oyk_config_resolved_bbs_callsign(const OykConfig* cfg, OykCallsign* out);
void oyk_config_resolved_chat_callsign(const OykConfig* cfg, OykCallsign* out);
void oyk_derive_default_iface_id(const OykConfig* cfg, const char* iface_type,
                                 char* out, size_t out_sz);
void oyk_derive_default_peer_id(const OykConfig* cfg, const char* callsign,
                                char* out, size_t out_sz);
oyk_u8 oyk_parse_learned_policy(const char* text, oyk_u8 fallback);
const char* oyk_format_learned_policy(oyk_u8 policy);
int oyk_first_peer_for_iface(const OykConfig* cfg, const char* iface_id,
                             oyk_u32* ip_out, oyk_u16* port_out);

/* BBS Files Area path validation & Sysop incoming file approval helpers */
int oyk_bbs_resolve_safe_path(const char* files_root,
                              const char* cur_rel,
                              const char* user_arg,
                              int allow_parent_in_root,
                              char* abs_out, size_t abs_sz,
                              char* rel_out, size_t rel_sz,
                              char* err_out, size_t err_sz);
int oyk_bbs_list_incoming(const char* incoming_root,
                          OykPendingIncoming* out, int max_out);
int oyk_bbs_approve_incoming_file(const char* incoming_root,
                                  const char* files_root,
                                  const char* uploader,
                                  const char* filename,
                                  const char* dest_subdir,
                                  int allow_overwrite,
                                  char* dest_out, size_t dest_sz,
                                  int* already_exists_out,
                                  char* err_out, size_t err_sz);
int oyk_bbs_delete_incoming_file(const char* incoming_root,
                                 const char* uploader,
                                 const char* filename,
                                 char* err_out, size_t err_sz);
int oyk_bbs_list_approved(const char* files_root,
                          OykApprovedFile* out, int max_out);
int oyk_bbs_delete_approved_file(const char* files_root,
                                 const char* rel_path,
                                 char* err_out, size_t err_sz);

struct OykYappSession;

/* Cooperative portable packet node engine */
class OykPortableNode {
public:
    OykPortableNode();
    ~OykPortableNode();

    int init(const OykConfig* cfg, OykTxCallback tx_cb, void* tx_ctx,
             char* err_out, size_t err_sz);
    int begin_shutdown();
    int begin_shutdown_with_notice(const char* notice_line);
    int disconnecting_count() const;
    void finish_shutdown();
    void shutdown();

    void set_now_override(time_t fixed_now); /* 0 = use real time() */
    time_t now_time() const;

    void set_iface_status(int iface_idx, int running, const char* status_text);
    void on_rx_frame(int iface_idx, const OykFrame* frame,
                     oyk_u32 peer_ip, oyk_u16 peer_port, oyk_u16 peer_handle);
    void tick();

    /* Local operator controls */
    void set_accept_incoming(int allow);
    int accept_incoming() const { return accept_incoming_; }
    int manual_deny_incoming() const { return manual_deny_incoming_; }
    int can_forward_between_ifaces(int src_iface_idx, int dst_iface_idx) const;
    int apply_config(const OykConfig* new_cfg, int save_to_disk, OykConfigApplyResult* res_out);
    int auto_learned_peer_count() const;
    int persist_auto_learned_peers(char* err_out, size_t err_sz);
    void discard_auto_learned_peers();
    int terminate_session(oyk_u32 session_id, char* msg_out, size_t msg_sz);
    int ban_callsign(const char* call_spec, oyk_u32 flags, const char* reason,
                     const char* lift_spec, char* msg_out, size_t msg_sz);
    int unban_callsign(const char* call_spec, char* msg_out, size_t msg_sz);

    /* Local operator outbound terminal (supports raw binary YAPP + interactive text) */
    void set_local_event_callback(OykLocalEventCallback cb, void* ctx);
    void set_printer_callback(OykPrinterWriteCallback cb, void* ctx);
    int find_peer_for_call(int iface_idx, const OykCallsign* call,
                           oyk_u32* ip_out, oyk_u16* port_out) const;
    int open_local_terminal(int iface_idx, const char* target_spec,
                            oyk_u32* sess_id_out, OykCallsign* resolved_call_out,
                            int* is_local_out, char* err_out, size_t err_sz);
    int send_local_bytes(oyk_u32 session_id, const oyk_u8* data, size_t len);
    int close_local_terminal(oyk_u32 session_id);

    /* Query helpers for DOS TUI & Win16/Win32/Win64 GUIs */
    const OykConfig& config() const { return cfg_; }
    int iface_count() const { return iface_count_; }
    const OykIfaceRuntime& iface(int i) const { return ifaces_[i]; }
    int active_connections_on(int iface_idx) const;
    int total_active_connections() const;
    oyk_u32 uptime_seconds() const;

    int new_heard_count() const { return new_heard_count_; }
    const OykNewHeard& new_heard(int i) const { return new_heard_[i]; }

    int max_sessions() const { return OYK_MAX_SESSIONS; }
    const OykSession& session_slot(int i) const { return sessions_[i]; }

    int ban_count() const { return ban_count_; }
    const OykBanEntry& ban_entry(int i) const { return bans_[i]; }

    int console_log_count() const { return console_log_count_; }
    const char* console_log_line(int i) const { return console_logs_[i]; }

    OykLineEnding banlist_line_ending() const { return banlist_eol_; }

    void set_storage_is_flash(int is_flash);
    int storage_is_flash() const { return storage_is_flash_; }
    int file_logging_allowed() const;

    void log_message(oyk_u32 level, const char* msg);
    void log_appeal(const char* callsign, const char* clean_appeal);

private:
    int load_banlist(const char* path, char* err_out, size_t err_sz);
    int save_banlist();
    int find_ban_index(const OykCallsign* call, const char* now_compact) const;
    int is_for_node(const OykCallsign* dst) const;
    int is_for_bbs(const OykCallsign* dst) const;
    int is_for_chat(const OykCallsign* dst) const;
    oyk_u8 resolve_learned_policy(int iface_idx) const;
    const OykPeerConfig* find_config_peer(int iface_idx, const OykCallsign* call) const;
    void migrate_iface_peers(OykConfig* cfg) const;
    int is_welcomed_call(const char* call_str) const;
    void mark_welcomed_call(const char* call_str);
    void record_heard(const OykCallsign* src, int iface_idx);
    void send_text(OykSession* sess, const char* text);
    void send_raw_bytes(OykSession* sess, const oyk_u8* data, size_t len);
    void send_disc(OykSession* sess);
    void bbs_load_index();
    int bbs_save_index() const;
    int bbs_save_msg_file(const OykBbsMsgMeta* meta, const char* body) const;
    int bbs_is_sysop(const OykSession* sess) const;
    int bbs_can_read(const OykBbsMsgMeta* m, const OykSession* sess) const;
    int bbs_can_kill(const OykBbsMsgMeta* m, const OykSession* sess) const;
    int bbs_unread_for(const char* base_call) const;
    void bbs_send_prompt(OykSession* sess);
    void bbs_send_msg(OykSession* sess, OykBbsMsgMeta* m);
    void bbs_enter(OykSession* sess, int direct_connect);
    void bbs_cleanup_yapp(OykSession* sess);
    static int bbs_yapp_send_trampoline(const oyk_u8* data, size_t len, void* ctx);
    void bbs_handle_raw_bytes(OykSession* sess, const oyk_u8* data, size_t len);
    void handle_bbs_line(OykSession* sess, const char* line);
    void chat_broadcast_topic(const char* topic, oyk_u32 exclude_sid, const char* text);
    void chat_join(OykSession* sess, int direct_connect);
    void chat_leave(OykSession* sess, const char* reason);
    void handle_chat_line(OykSession* sess, const char* line);
    void handle_normal_command(OykSession* sess, const char* line);
    void handle_restricted_line(OykSession* sess, const char* line);
    void write_log_file_line(OykLogFileEntry* lf, const char* line, const char* today);
    void push_console_log(const char* line);

    OykConfig cfg_;
    OykTxCallback tx_cb_;
    void* tx_ctx_;
    OykLocalEventCallback local_cb_;
    void* local_ctx_;
    OykPrinterWriteCallback printer_cb_;
    void* printer_ctx_;
    time_t started_at_;
    time_t fixed_now_;
    int accept_incoming_;
    int manual_deny_incoming_;
    int storage_is_flash_;
    oyk_u32 next_session_id_;

    int iface_count_;
    OykIfaceRuntime ifaces_[OYK_MAX_INTERFACES];

    OykSession sessions_[OYK_MAX_SESSIONS];

    int mheard_count_;
    OykMHeard mheard_[OYK_MAX_MHEARD];
    int new_heard_count_;
    OykNewHeard new_heard_[OYK_MAX_NEW_HEARD];

    char banlist_path_[128];
    OykLineEnding banlist_eol_;
    int banlist_has_eol_;
    int banlist_mixed_eol_;
    int ban_count_;
    OykBanEntry bans_[OYK_MAX_BANS];

    int welcomed_count_;
    char welcomed_calls_[OYK_MAX_CONFIG_PEERS][12];

    int console_log_count_;
    char console_logs_[OYK_MAX_CONSOLE_LOGS][112];

    oyk_u32 bbs_next_id_;
    int bbs_msg_count_;
    OykBbsMsgMeta bbs_msgs_[OYK_MAX_BBS_MSGS];
    OykYappSession* bbs_yapp_;
    oyk_u32 bbs_yapp_sid_;
    OykSession* bbs_yapp_tx_sess_;
};

#endif /* OYK_PORTABLE_HPP */
