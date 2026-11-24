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

/* Tunable memory bounds (defaults fit comfortably within 640KB IBM XT conventional RAM) */
#ifndef OYK_MAX_INTERFACES
#define OYK_MAX_INTERFACES   4
#endif
#ifndef OYK_MAX_SESSIONS
#define OYK_MAX_SESSIONS     8
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
#ifndef OYK_MAX_FRAME_INFO
#define OYK_MAX_FRAME_INFO   256
#endif
#ifndef OYK_MAX_APPEAL_LEN
#define OYK_MAX_APPEAL_LEN   140
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

struct OykIfaceConfig {
    char id[16];
    char type[16];       /* "axudp", "axip", "kiss-serial", "kiss-tcp", "telnet" */
    char description[40];
    char host[32];
    oyk_u16 port;
    char device[24];     /* e.g. "COM1" or "/dev/ttyS0" */
    oyk_u32 baud;
    oyk_u8 kiss_port;
    int server_mode;
    int peer_count;
    OykPeerEntry peers[OYK_MAX_PEERS];
};

struct OykLogFileEntry {
    char filename[32];
    oyk_u32 mask;
    char active_date[10]; /* YYYYMMDD */
};

struct OykConfig {
    OykCallsign callsign;
    OykCallsign alias;
    char info_text[80];
    char source_path[128];
    char banlist_path[128];
    char log_path[96];
    oyk_u32 console_level;
    int rotate_daily;
    int accept_incoming;
    OykLineEnding line_ending;
    int has_detected_eol;

    int log_file_count;
    OykLogFileEntry log_files[OYK_MAX_LOG_FILES];

    int iface_count;
    OykIfaceConfig ifaces[OYK_MAX_INTERFACES];
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

struct OykSession {
    oyk_u32 id;
    int iface_idx;
    OykCallsign local_call;
    OykCallsign remote_call;
    oyk_u32 peer_ip;      /* IPv4 in network byte order */
    oyk_u16 peer_port;
    oyk_u16 peer_handle;  /* e.g. socket index for Telnet/KISS */
    OykSessState state;
    int is_local_term;    /* 1 = local operator outbound terminal (raw binary pass-through) */
    int restricted_ban;
    int appeal_eligible;
    char ban_key[12];
    oyk_u8 vs;
    oyk_u8 vr;
    char since_hms[10];
    time_t last_activity;
    char rx_line[160];
    int rx_line_len;
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

int oyk_parse_callsign(const char* text, OykCallsign* out);
void oyk_format_callsign(const OykCallsign* call, char* out, size_t out_sz);
int oyk_callsign_eq(const OykCallsign* a, const OykCallsign* b);
/* Parse dotted-quad IPv4 into network-byte-order oyk_u32; returns 1 on success. */
int oyk_parse_ipv4(const char* text, oyk_u32* net_ip_out);
/* Parse "CALLSIGN@ip:port", "CALLSIGN ip:port", or "ip:port" peer spec. */
int oyk_parse_peer_spec(const char* spec, char* call_out, size_t call_sz,
                        oyk_u32* net_ip_out, oyk_u16* port_out, oyk_u16 default_port);

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
void oyk_sanitize_appeal(const char* raw, char* out, size_t out_sz);
void oyk_format_now_compact(time_t t, char* out, size_t out_sz);
void oyk_format_now_yyyymmdd(time_t t, char* out, size_t out_sz);
void oyk_format_now_hms(time_t t, char* out, size_t out_sz);

/* Config & Banlist I/O (accepts CRLF, CR, LF; preserves EOL on save) */
void oyk_config_init_default(OykConfig* cfg);
int oyk_config_load(const char* path, OykConfig* cfg, char* err_out, size_t err_sz);
int oyk_config_save(const OykConfig* cfg, const char* path);
void oyk_config_resolve_banlist(const OykConfig* cfg, char* out, size_t out_sz);

/* Cooperative portable packet node engine */
class OykPortableNode {
public:
    OykPortableNode();

    int init(const OykConfig* cfg, OykTxCallback tx_cb, void* tx_ctx,
             char* err_out, size_t err_sz);
    int begin_shutdown();
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
    int terminate_session(oyk_u32 session_id, char* msg_out, size_t msg_sz);
    int ban_callsign(const char* call_spec, oyk_u32 flags, const char* reason,
                     const char* lift_spec, char* msg_out, size_t msg_sz);
    int unban_callsign(const char* call_spec, char* msg_out, size_t msg_sz);

    /* Local operator outbound terminal (supports raw binary YAPP + interactive text) */
    void set_local_event_callback(OykLocalEventCallback cb, void* ctx);
    int find_peer_for_call(int iface_idx, const OykCallsign* call,
                           oyk_u32* ip_out, oyk_u16* port_out) const;
    int open_local_terminal(int iface_idx, const char* target_spec,
                            oyk_u32* sess_id_out, OykCallsign* resolved_call_out,
                            char* err_out, size_t err_sz);
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

    void log_message(oyk_u32 level, const char* msg);
    void log_appeal(const char* callsign, const char* clean_appeal);

private:
    int load_banlist(const char* path, char* err_out, size_t err_sz);
    int save_banlist();
    int find_ban_index(const OykCallsign* call, const char* now_compact) const;
    int is_for_node(const OykCallsign* dst) const;
    void record_heard(const OykCallsign* src, int iface_idx);
    void send_text(OykSession* sess, const char* text);
    void send_disc(OykSession* sess);
    void handle_normal_command(OykSession* sess, const char* line);
    void handle_restricted_line(OykSession* sess, const char* line);
    void write_log_file_line(OykLogFileEntry* lf, const char* line, const char* today);
    void push_console_log(const char* line);

    OykConfig cfg_;
    OykTxCallback tx_cb_;
    void* tx_ctx_;
    OykLocalEventCallback local_cb_;
    void* local_ctx_;
    time_t started_at_;
    time_t fixed_now_;
    int accept_incoming_;
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

    int console_log_count_;
    char console_logs_[OYK_MAX_CONSOLE_LOGS][112];
};

#endif /* OYK_PORTABLE_HPP */
