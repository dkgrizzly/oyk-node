/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_core.h"

#include <stdio.h>
#ifdef OYK_SYSV_SVR32
extern void *malloc(size_t);
extern void  free(void *);
extern int   atoi(const char *);
#else
#include <stdlib.h>
#endif
#include <string.h>
#if !defined(__VMS) && !defined(_WIN32)
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#endif

#ifdef OYK_SYSV_SVR32
static void s_svr32_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d;
    const unsigned char *s;
    size_t i;
    d = (unsigned char *)dst;
    s = (const unsigned char *)src;
    if (d == s || n == 0U) return;
    if (d < s) {
        for (i = 0U; i < n; ++i) d[i] = s[i];
    } else {
        for (i = n; i > 0U; --i) d[i - 1U] = s[i - 1U];
    }
}
#define memmove s_svr32_memmove
#endif

static int s_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A');
    return ch;
}

static int s_toupper(int ch) {
    if (ch >= 'a' && ch <= 'z') return ch - ('a' - 'A');
    return ch;
}

static int s_isalnum(int ch) {
    return ((ch >= '0' && ch <= '9') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z')) ? 1 : 0;
}

static int s_isspace(int ch) {
    return (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') ? 1 : 0;
}

void oyk_sysv_copy_str(char *dst, size_t cap, const char *src) {
    size_t n;
    if (!dst || cap == 0U) return;
    if (!src) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= cap) n = cap - 1U;
    if (n > 0U) memcpy(dst, src, n);
    dst[n] = '\0';
}

int oyk_sysv_strieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (s_tolower((unsigned char)*a) != s_tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

void oyk_sysv_be16_write(sysv_u8 *out, sysv_u16 v) {
    if (!out) return;
    out[0] = (sysv_u8)((v >> 8) & 0xFFU);
    out[1] = (sysv_u8)(v & 0xFFU);
}

sysv_u16 oyk_sysv_be16_read(const sysv_u8 *in) {
    if (!in) return 0U;
    return (sysv_u16)(((sysv_u16)in[0] << 8) | (sysv_u16)in[1]);
}

void oyk_sysv_be32_write(sysv_u8 *out, sysv_u32 v) {
    if (!out) return;
    out[0] = (sysv_u8)((v >> 24) & 0xFFUL);
    out[1] = (sysv_u8)((v >> 16) & 0xFFUL);
    out[2] = (sysv_u8)((v >> 8) & 0xFFUL);
    out[3] = (sysv_u8)(v & 0xFFUL);
}

sysv_u32 oyk_sysv_be32_read(const sysv_u8 *in) {
    if (!in) return 0UL;
    return ((sysv_u32)in[0] << 24) |
           ((sysv_u32)in[1] << 16) |
           ((sysv_u32)in[2] << 8) |
           (sysv_u32)in[3];
}

static void s_trim_inplace(char *s) {
    size_t len;
    size_t start;
    size_t end;
    if (!s) return;
    len = strlen(s);
    start = 0U;
    while (start < len && s_isspace((unsigned char)s[start])) ++start;
    end = len;
    while (end > start && s_isspace((unsigned char)s[end - 1U])) --end;
    if (start > 0U && end > start) {
        memmove(s, s + start, end - start);
    }
    s[end - start] = '\0';
}

int oyk_sysv_node_menu_count(void) {
    return 11;
}

const OykSysvMenuItem *oyk_sysv_node_menu_item(int idx) {
    static const OykSysvMenuItem items[11] = {
        { OYK_SYSV_ACT_ALLOW_INCOMING, "Allow Incoming Connections", "Allow Incoming Connections", 'A', 0 },
        { OYK_SYSV_ACT_DENY_INCOMING,  "Deny Incoming Connections",  "Deny Incoming Connections",  'D', 0 },
        { OYK_SYSV_ACT_NONE,           "-",                          "-",                          0,   1 },
        { OYK_SYSV_ACT_LOCAL_TERM,     "Local Terminal...",          "Local Terminal...",          'T', 0 },
        { OYK_SYSV_ACT_FILES,          "Manage Files Area...",       "Manage Files Area...",       'F', 0 },
        { OYK_SYSV_ACT_RIG,            "Radio Control...",           "Radio Control...",           'R', 0 },
        { OYK_SYSV_ACT_SETTINGS,       "Settings...",                "Settings...",                'S', 0 },
        { OYK_SYSV_ACT_TERMINATE,      "Terminate Connection...",    "Terminate Connection...",    'K', 0 },
        { OYK_SYSV_ACT_BAN,            "Ban / Unban Callsign...",    "Ban / Unban Callsign...",    'B', 0 },
        { OYK_SYSV_ACT_NONE,           "-",                          "-",                          0,   1 },
        { OYK_SYSV_ACT_EXIT,           "Exit",                       "Exit",                       'Q', 0 }
    };
    if (idx < 0 || idx >= 11) return NULL;
    return &items[idx];
}

const char *oyk_sysv_screen_title(int screen_id) {
    switch (screen_id) {
        case OYK_SYSV_SCR_MAIN:       return "OYK Amateur Radio Packet Node";
        case OYK_SYSV_SCR_MENU:       return "Node Menu";
        case OYK_SYSV_SCR_SETTINGS:   return "Node Settings";
        case OYK_SYSV_SCR_ADD_IFACE:  return "Add Interface";
        case OYK_SYSV_SCR_EDIT_IFACE: return "Edit Interface";
        case OYK_SYSV_SCR_PEERS:      return "Peers";
        case OYK_SYSV_SCR_EDIT_PEER:  return "Edit Peer";
        case OYK_SYSV_SCR_TERMINATE:  return "Terminate Active Connection";
        case OYK_SYSV_SCR_BAN:        return "Ban / Unban Callsign";
        case OYK_SYSV_SCR_EXIT_PEERS: return "Auto-Learned Peers";
        case OYK_SYSV_SCR_THEME:      return "Theme Settings";
        case OYK_SYSV_SCR_TERMINAL:   return "OYK Local Packet Terminal (YAPP)";
        case OYK_SYSV_SCR_TERM_HELP:  return "Terminal Hotkey Help";
        case OYK_SYSV_SCR_FILES:      return "Manage BBS Files Area";
        case OYK_SYSV_SCR_RIG:        return "Radio Control";
        case OYK_SYSV_SCR_ABOUT:      return "About OYK Amateur Radio Packet Node";
        default:                      return "OYK Packet Node";
    }
}

int oyk_sysv_settings_tab_count(void) {
    return 8;
}

const char *oyk_sysv_settings_tab_title(int tab_idx) {
    static const char * const titles[8] = {
        "1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn", "8:Theme"
    };
    if (tab_idx < 0 || tab_idx >= 8) return "";
    return titles[tab_idx];
}

int oyk_sysv_table_column_count(int table_id) {
    switch (table_id) {
        case OYK_SYSV_TBL_IFACES:         return 5;
        case OYK_SYSV_TBL_HEARD:          return 4;
        case OYK_SYSV_TBL_CONNECTIONS:    return 7;
        case OYK_SYSV_TBL_TERMINATE:      return 4;
        case OYK_SYSV_TBL_PEERS:          return 5;
        case OYK_SYSV_TBL_BANS:           return 5;
        case OYK_SYSV_TBL_FILES_INCOMING: return 3;
        case OYK_SYSV_TBL_FILES_APPROVED: return 2;
        case OYK_SYSV_TBL_THEME:          return 4;
        default:                          return 0;
    }
}

const char *oyk_sysv_table_column(int table_id, int col_idx) {
    static const char * const c_ifaces[5] = { "Port", "Type", "Conn", "Status", "RX/TX" };
    static const char * const c_heard[4]  = { "#", "Callsign", "Port", "Heard" };
    static const char * const c_conns[7]  = { "ID", "Callsign", "Port", "Dir", "State", "Since", "Role" };
    static const char * const c_term[4]   = { "ID", "Callsign", "Port", "State" };
    static const char * const c_peers[5]  = { "ID", "Callsign", "Interface", "Endpoint", "Policy" };
    static const char * const c_bans[5]   = { "Callsign", "Banned At", "Lift At", "Flags", "Reason" };
    static const char * const c_finc[3]   = { "Uploader", "Filename", "Size" };
    static const char * const c_fapp[2]   = { "Filename", "Size" };
    static const char * const c_thm[4]    = { "Role", "Key", "Color", "Preview" };

    switch (table_id) {
        case OYK_SYSV_TBL_IFACES:         return (col_idx >= 0 && col_idx < 5) ? c_ifaces[col_idx] : "";
        case OYK_SYSV_TBL_HEARD:          return (col_idx >= 0 && col_idx < 4) ? c_heard[col_idx]  : "";
        case OYK_SYSV_TBL_CONNECTIONS:    return (col_idx >= 0 && col_idx < 7) ? c_conns[col_idx]  : "";
        case OYK_SYSV_TBL_TERMINATE:      return (col_idx >= 0 && col_idx < 4) ? c_term[col_idx]   : "";
        case OYK_SYSV_TBL_PEERS:          return (col_idx >= 0 && col_idx < 5) ? c_peers[col_idx]  : "";
        case OYK_SYSV_TBL_BANS:           return (col_idx >= 0 && col_idx < 5) ? c_bans[col_idx]   : "";
        case OYK_SYSV_TBL_FILES_INCOMING: return (col_idx >= 0 && col_idx < 3) ? c_finc[col_idx]   : "";
        case OYK_SYSV_TBL_FILES_APPROVED: return (col_idx >= 0 && col_idx < 2) ? c_fapp[col_idx]   : "";
        case OYK_SYSV_TBL_THEME:          return (col_idx >= 0 && col_idx < 4) ? c_thm[col_idx]    : "";
        default: return "";
    }
}

const char *oyk_sysv_about_summary(void) {
    return "OYK Amateur Radio Packet Node\n"
           "AX.25 / KISS / AXUDP / AXIP / Telnet\n"
           "Portable BBS, Chat & YAPP File Area\n"
           "Copyright (C) 2026 KR4OYK - David Kuder\n"
           "License: GPL-3.0-or-later";
}

int oyk_sysv_parse_callsign(const char *text, OykSysvCallsign *out) {
    char tmp[24];
    char *dash;
    int ssid;
    size_t blen;
    size_t i;
    if (!text || !out) return 0;
    oyk_sysv_copy_str(tmp, sizeof(tmp), text);
    s_trim_inplace(tmp);
    for (i = 0U; tmp[i]; ++i) {
        tmp[i] = (char)s_toupper((unsigned char)tmp[i]);
    }
    ssid = 0;
    dash = strchr(tmp, '-');
    if (dash) {
        const char *sp;
        *dash = '\0';
        sp = dash + 1;
        if (!sp[0] || strlen(sp) > 2U) return 0;
        while (*sp) {
            if (*sp < '0' || *sp > '9') return 0;
            ssid = ssid * 10 + (*sp - '0');
            ++sp;
        }
        if (ssid < 0 || ssid > 15) return 0;
    }
    blen = strlen(tmp);
    if (blen == 0U || blen > 6U) return 0;
    for (i = 0U; i < blen; ++i) {
        if (!s_isalnum((unsigned char)tmp[i])) return 0;
    }
    memset(out, 0, sizeof(*out));
    oyk_sysv_copy_str(out->base, sizeof(out->base), tmp);
    out->ssid = (sysv_u8)ssid;
    return 1;
}

void oyk_sysv_format_callsign(const OykSysvCallsign *cs, char *dst, size_t cap) {
    char buf[16];
    if (!dst || cap == 0U) return;
    if (!cs || !cs->base[0]) { dst[0] = '\0'; return; }
    if (cs->ssid > 0U) {
        sprintf(buf, "%s-%u", cs->base, (unsigned int)cs->ssid);
    } else {
        sprintf(buf, "%s", cs->base);
    }
    oyk_sysv_copy_str(dst, cap, buf);
}

int oyk_sysv_callsign_eq(const OykSysvCallsign *a, const OykSysvCallsign *b) {
    if (!a || !b) return 0;
    return (oyk_sysv_strieq(a->base, b->base) && a->ssid == b->ssid) ? 1 : 0;
}

sysv_u16 oyk_sysv_crc16_fcs(const sysv_u8 *data, size_t len) {
    sysv_u16 crc;
    size_t i;
    int bit;
    crc = 0xFFFFU;
    if (!data) return 0U;
    for (i = 0U; i < len; ++i) {
        crc ^= (sysv_u16)data[i];
        for (bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001U) {
                crc = (sysv_u16)((crc >> 1) ^ 0x8408U);
            } else {
                crc >>= 1;
            }
        }
    }
    return (sysv_u16)(crc ^ 0xFFFFU);
}

static void s_encode_ax25_addr(const OykSysvCallsign *cs, int cmd_bit, int last, sysv_u8 *out) {
    size_t i;
    size_t blen;
    blen = strlen(cs->base);
    for (i = 0U; i < 6U; ++i) {
        sysv_u8 c;
        c = (sysv_u8)((i < blen) ? s_toupper((unsigned char)cs->base[i]) : ' ');
        out[i] = (sysv_u8)(c << 1);
    }
    out[6] = (sysv_u8)(0x60U | ((cs->ssid & 0x0FU) << 1) |
                       (cmd_bit ? 0x80U : 0x00U) | (last ? 0x01U : 0x00U));
}

static void s_decode_ax25_addr(const sysv_u8 *in, OykSysvCallsign *out, int *cmd_bit, int *last) {
    size_t i;
    size_t n;
    memset(out, 0, sizeof(*out));
    n = 0U;
    for (i = 0U; i < 6U; ++i) {
        char ch;
        ch = (char)((in[i] >> 1) & 0x7FU);
        if (ch != ' ' && n < 6U) {
            out->base[n++] = ch;
        }
    }
    out->base[n] = '\0';
    out->ssid = (sysv_u8)((in[6] >> 1) & 0x0FU);
    if (cmd_bit) *cmd_bit = (in[6] & 0x80U) ? 1 : 0;
    if (last) *last = (in[6] & 0x01U) ? 1 : 0;
}

size_t oyk_sysv_ax25_encode(const OykSysvFrame *f, sysv_u8 *out, size_t cap) {
    size_t pos;
    sysv_u16 fcs;
    if (!f || !out || cap < 18U) return 0U;
    s_encode_ax25_addr(&f->dst, f->is_command, 0, out);
    s_encode_ax25_addr(&f->src, f->is_response, 1, out + 7);
    pos = 14U;
    out[pos++] = f->control;
    if ((f->control & 0x01U) == 0U || f->control == 0x03U || f->control == 0x13U) {
        if (pos + f->info_len + 3U > cap) return 0U;
        out[pos++] = f->pid ? f->pid : 0xF0U;
        if (f->info_len > 0U) {
            memcpy(out + pos, f->info, f->info_len);
            pos += f->info_len;
        }
    } else if (pos + 2U > cap) {
        return 0U;
    }
    fcs = oyk_sysv_crc16_fcs(out, pos);
    out[pos++] = (sysv_u8)(fcs & 0xFFU);
    out[pos++] = (sysv_u8)((fcs >> 8) & 0xFFU);
    return pos;
}

int oyk_sysv_ax25_decode(const sysv_u8 *raw, size_t len, OykSysvFrame *out) {
    size_t pos;
    int last;
    int dst_cmd;
    int src_cmd;
    sysv_u16 calc_fcs;
    sysv_u16 pkt_fcs;
    if (!raw || !out || len < 17U) return 0;
    calc_fcs = oyk_sysv_crc16_fcs(raw, len - 2U);
    pkt_fcs = (sysv_u16)((sysv_u16)raw[len - 2U] | ((sysv_u16)raw[len - 1U] << 8));
    if (calc_fcs != pkt_fcs) return 0;
    memset(out, 0, sizeof(*out));
    dst_cmd = 0;
    src_cmd = 0;
    last = 0;
    s_decode_ax25_addr(raw, &out->dst, &dst_cmd, &last);
    pos = 7U;
    if (last || pos + 7U >= len - 2U) return 0;
    s_decode_ax25_addr(raw + pos, &out->src, &src_cmd, &last);
    pos += 7U;
    while (!last) {
        if (pos + 7U > len - 2U) return 0;
        last = (raw[pos + 6U] & 0x01U) ? 1 : 0;
        pos += 7U;
    }
    if (pos >= len - 2U) return 0;
    out->is_command = dst_cmd && !src_cmd;
    out->is_response = !dst_cmd && src_cmd;
    out->control = raw[pos++];
    if ((out->control & 0x01U) == 0U || out->control == 0x03U || out->control == 0x13U) {
        if (pos < len - 2U) {
            out->pid = raw[pos++];
            if (pos < len - 2U) {
                size_t ilen;
                ilen = (len - 2U) - pos;
                if (ilen > sizeof(out->info)) ilen = sizeof(out->info);
                memcpy(out->info, raw + pos, ilen);
                out->info_len = ilen;
            }
        }
    }
    return 1;
}

size_t oyk_sysv_kiss_encode(sysv_u8 kiss_port, const sysv_u8 *payload, size_t len,
                            sysv_u8 *out, size_t cap) {
    size_t pos;
    size_t i;
    if (!payload || !out || cap < 4U) return 0U;
    pos = 0U;
    out[pos++] = 0xC0U;
    out[pos++] = (sysv_u8)((kiss_port & 0x0FU) << 4);
    for (i = 0U; i < len; ++i) {
        sysv_u8 b;
        b = payload[i];
        if (b == 0xC0U) {
            if (pos + 2U >= cap) return 0U;
            out[pos++] = 0xDBU;
            out[pos++] = 0xDCU;
        } else if (b == 0xDBU) {
            if (pos + 2U >= cap) return 0U;
            out[pos++] = 0xDBU;
            out[pos++] = 0xDDU;
        } else {
            if (pos + 1U >= cap) return 0U;
            out[pos++] = b;
        }
    }
    if (pos >= cap) return 0U;
    out[pos++] = 0xC0U;
    return pos;
}

void oyk_sysv_kiss_init(OykSysvKissDecoder *dec) {
    if (!dec) return;
    memset(dec, 0, sizeof(*dec));
}

int oyk_sysv_kiss_feed(OykSysvKissDecoder *dec, sysv_u8 b,
                       sysv_u8 *out_port, sysv_u8 *out_cmd) {
    if (!dec) return 0;
    if (b == 0xC0U) {
        if (dec->in_frame && dec->len > 1U) {
            sysv_u8 hdr;
            hdr = dec->buf[0];
            if (out_port) *out_port = (sysv_u8)((hdr >> 4) & 0x0FU);
            if (out_cmd) *out_cmd = (sysv_u8)(hdr & 0x0FU);
            memmove(dec->buf, dec->buf + 1, dec->len - 1U);
            dec->len -= 1U;
            dec->in_frame = 0;
            dec->escaped = 0;
            return 1;
        }
        dec->in_frame = 1;
        dec->escaped = 0;
        dec->len = 0U;
        return 0;
    }
    if (!dec->in_frame) return 0;
    if (dec->escaped) {
        dec->escaped = 0;
        if (b == 0xDCU) b = 0xC0U;
        else if (b == 0xDDU) b = 0xDBU;
    } else if (b == 0xDBU) {
        dec->escaped = 1;
        return 0;
    }
    if (dec->len < sizeof(dec->buf)) {
        dec->buf[dec->len++] = b;
    }
    return 0;
}

void oyk_sysv_sanitize_appeal(const char *raw, char *out, size_t cap) {
    size_t oi;
    size_t i;
    if (!out || cap == 0U) return;
    if (!raw) { out[0] = '\0'; return; }
    oi = 0U;
    for (i = 0U; raw[i] && oi < 140U && oi + 1U < cap; ++i) {
        unsigned char c;
        c = (unsigned char)raw[i];
        if (c >= 0x20U && c <= 0x7EU) {
            out[oi++] = (char)c;
        }
    }
    out[oi] = '\0';
    s_trim_inplace(out);
}

int oyk_sysv_is_safe_filename(const char *name) {
    size_t len;
    size_t i;
    if (!name || !name[0]) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    len = strlen(name);
    if (len > 44U) return 0;
    for (i = 0U; i < len; ++i) {
        unsigned char c;
        c = (unsigned char)name[i];
        if (c < 0x21U || c >= 0x7FU || c == '/' || c == '\\' || c == ':') return 0;
    }
    return 1;
}

void oyk_sysv_format_duration(sysv_u32 secs, char *dst, size_t cap) {
    char buf[32];
    sysv_u32 h;
    sysv_u32 m;
    sysv_u32 s;
    if (!dst || cap == 0U) return;
    h = secs / 3600UL;
    m = (secs % 3600UL) / 60UL;
    s = secs % 60UL;
    if (h > 0UL) {
        sprintf(buf, "%luh%02lum%02lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    } else if (m > 0UL) {
        sprintf(buf, "%lum%02lus", (unsigned long)m, (unsigned long)s);
    } else {
        sprintf(buf, "%lus", (unsigned long)s);
    }
    oyk_sysv_copy_str(dst, cap, buf);
}

const char *oyk_sysv_format_incoming_pill(int accept_in, int compact) {
    if (compact) return accept_in ? "[IN:ALLOW]" : "[IN:DENY]";
    return accept_in ? "[INCOMING: ALLOW]" : "[INCOMING: DENY]";
}

void oyk_sysv_format_console_header(const char *callsign, const char *alias,
                                    sysv_u32 uptime_sec, int conn_count,
                                    int accept_in, int compact,
                                    char *dst, size_t cap) {
    char up[32];
    char buf[140];
    if (!dst || cap == 0U) return;
    oyk_sysv_format_duration(uptime_sec, up, sizeof(up));
    if (compact) {
        sprintf(buf, "%s (%s) | Up:%s | Conn:%d",
                callsign ? callsign : "N0CALL",
                alias ? alias : "OYKNOD",
                up, conn_count);
    } else {
        sprintf(buf, "OYK PACKET NODE: %s (%s) | Uptime: %s | Active Connections: %d | %s",
                callsign ? callsign : "N0CALL",
                alias ? alias : "OYKNOD",
                up, conn_count,
                oyk_sysv_format_incoming_pill(accept_in, 0));
    }
    oyk_sysv_copy_str(dst, cap, buf);
}

const char *oyk_sysv_format_learned_policy(int policy) {
    switch (policy) {
        case 1: return "none";
        case 2: return "inbound";
        case 3: return "outbound";
        case 4: return "both";
        default: return "inherit";
    }
}

int oyk_sysv_cycle_learned_policy(int policy, int delta) {
    int p;
    p = (policy + delta) % 5;
    if (p < 0) p += 5;
    return p;
}

void oyk_sysv_theme_preset(const char *name, OykSysvThemeConfig *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (name && oyk_sysv_strieq(name, "dark")) {
        oyk_sysv_copy_str(out->name, sizeof(out->name), "dark");
        out->is_dark_base = 1;
        out->colors[OYK_SYSV_TR_BG]           = 0x111520UL;
        out->colors[OYK_SYSV_TR_FG]           = 0xE6ECF5UL;
        out->colors[OYK_SYSV_TR_BORDER]       = 0x4A6388UL;
        out->colors[OYK_SYSV_TR_HEADER_BG]    = 0x1A2842UL;
        out->colors[OYK_SYSV_TR_HEADER_FG]    = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_STATUS_BG]    = 0x1A2233UL;
        out->colors[OYK_SYSV_TR_STATUS_FG]    = 0xC8D8ECUL;
        out->colors[OYK_SYSV_TR_MENU_BG]      = 0x1D283AUL;
        out->colors[OYK_SYSV_TR_MENU_FG]      = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_SELECTION_BG] = 0x2B5B84UL;
        out->colors[OYK_SYSV_TR_SELECTION_FG] = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_PANEL_BG]     = 0x171E2EUL;
        out->colors[OYK_SYSV_TR_PANEL_FG]     = 0xE6ECF5UL;
        out->colors[OYK_SYSV_TR_INPUT_BG]     = 0x0E1422UL;
        out->colors[OYK_SYSV_TR_INPUT_FG]     = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_ACCENT]       = 0x5CC8FFUL;
        out->colors[OYK_SYSV_TR_OK]           = 0x35C975UL;
        out->colors[OYK_SYSV_TR_WARN]         = 0xF5B841UL;
        out->colors[OYK_SYSV_TR_ERROR]        = 0xE85858UL;
    } else if (name && oyk_sysv_strieq(name, "light")) {
        oyk_sysv_copy_str(out->name, sizeof(out->name), "light");
        out->is_dark_base = 0;
        out->colors[OYK_SYSV_TR_BG]           = 0xDCE3ECUL;
        out->colors[OYK_SYSV_TR_FG]           = 0x14181FUL;
        out->colors[OYK_SYSV_TR_BORDER]       = 0x4A5E78UL;
        out->colors[OYK_SYSV_TR_HEADER_BG]    = 0x1E3A63UL;
        out->colors[OYK_SYSV_TR_HEADER_FG]    = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_STATUS_BG]    = 0xCFD8E4UL;
        out->colors[OYK_SYSV_TR_STATUS_FG]    = 0x14181FUL;
        out->colors[OYK_SYSV_TR_MENU_BG]      = 0xE6ECF4UL;
        out->colors[OYK_SYSV_TR_MENU_FG]      = 0x14181FUL;
        out->colors[OYK_SYSV_TR_SELECTION_BG] = 0x2A5B8CUL;
        out->colors[OYK_SYSV_TR_SELECTION_FG] = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_PANEL_BG]     = 0xF5F7FAUL;
        out->colors[OYK_SYSV_TR_PANEL_FG]     = 0x14181FUL;
        out->colors[OYK_SYSV_TR_INPUT_BG]     = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_INPUT_FG]     = 0x000000UL;
        out->colors[OYK_SYSV_TR_ACCENT]       = 0x1E5699UL;
        out->colors[OYK_SYSV_TR_OK]           = 0x1A7A3AUL;
        out->colors[OYK_SYSV_TR_WARN]         = 0xA66200UL;
        out->colors[OYK_SYSV_TR_ERROR]        = 0xB82020UL;
    } else {
        /* OS-native default: Motif/System V slate & navy */
        oyk_sysv_copy_str(out->name, sizeof(out->name), "native");
        out->is_dark_base = 0;
        out->colors[OYK_SYSV_TR_BG]           = 0xC4C4C4UL;
        out->colors[OYK_SYSV_TR_FG]           = 0x101010UL;
        out->colors[OYK_SYSV_TR_BORDER]       = 0x484848UL;
        out->colors[OYK_SYSV_TR_HEADER_BG]    = 0x1E354DUL;
        out->colors[OYK_SYSV_TR_HEADER_FG]    = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_STATUS_BG]    = 0xB2B2B2UL;
        out->colors[OYK_SYSV_TR_STATUS_FG]    = 0x101010UL;
        out->colors[OYK_SYSV_TR_MENU_BG]      = 0xD0D0D0UL;
        out->colors[OYK_SYSV_TR_MENU_FG]      = 0x101010UL;
        out->colors[OYK_SYSV_TR_SELECTION_BG] = 0x1E354DUL;
        out->colors[OYK_SYSV_TR_SELECTION_FG] = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_PANEL_BG]     = 0xD8D8D8UL;
        out->colors[OYK_SYSV_TR_PANEL_FG]     = 0x101010UL;
        out->colors[OYK_SYSV_TR_INPUT_BG]     = 0xFFFFFFUL;
        out->colors[OYK_SYSV_TR_INPUT_FG]     = 0x000000UL;
        out->colors[OYK_SYSV_TR_ACCENT]       = 0x1E354DUL;
        out->colors[OYK_SYSV_TR_OK]           = 0x1A6B33UL;
        out->colors[OYK_SYSV_TR_WARN]         = 0x995A00UL;
        out->colors[OYK_SYSV_TR_ERROR]        = 0xB02020UL;
    }
}

const char *oyk_sysv_theme_role_name(int role) {
    static const char * const names[OYK_SYSV_THEME_COLOR_COUNT] = {
        "Background", "Foreground", "Border", "Header Bg", "Header Fg",
        "Status Bg", "Status Fg", "Menu Bg", "Menu Fg", "Select Bg",
        "Select Fg", "Panel Bg", "Panel Fg", "Input Bg", "Input Fg",
        "Accent", "OK / Allow", "Warning", "Error / Deny"
    };
    if (role < 0 || role >= OYK_SYSV_THEME_COLOR_COUNT) return "";
    return names[role];
}

const char *oyk_sysv_theme_role_key(int role) {
    static const char * const keys[OYK_SYSV_THEME_COLOR_COUNT] = {
        "bg", "fg", "border", "header_bg", "header_fg",
        "status_bg", "status_fg", "menu_bg", "menu_fg", "sel_bg",
        "sel_fg", "panel_bg", "panel_fg", "input_bg", "input_fg",
        "accent", "ok", "warn", "error"
    };
    if (role < 0 || role >= OYK_SYSV_THEME_COLOR_COUNT) return "";
    return keys[role];
}

void oyk_sysv_config_init_default(OykSysvConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    oyk_sysv_parse_callsign("KR4OYK-7", &cfg->callsign);
    oyk_sysv_parse_callsign("OYKNOD", &cfg->alias);
    cfg->accept_incoming = 1;
    cfg->line_ending = OYK_SYSV_EOL_LF;
    cfg->has_detected_eol = 0;
    cfg->bbs_enabled = 1;
    cfg->bbs_ssid = 1;
    cfg->bbs_files_enabled = 1;
    oyk_sysv_copy_str(cfg->bbs_mail_path, sizeof(cfg->bbs_mail_path), "bbs");
    oyk_sysv_copy_str(cfg->bbs_files_path, sizeof(cfg->bbs_files_path), "files");
    oyk_sysv_copy_str(cfg->incoming_path, sizeof(cfg->incoming_path), "incoming");
    cfg->chat_enabled = 1;
    cfg->chat_ssid = 2;
    oyk_sysv_copy_str(cfg->chat_topic, sizeof(cfg->chat_topic), "Lobby");
    cfg->rig_enabled = 1;
    oyk_sysv_copy_str(cfg->rig_protocol, sizeof(cfg->rig_protocol), "rigctld");
    oyk_sysv_copy_str(cfg->rig_host, sizeof(cfg->rig_host), "127.0.0.1");
    cfg->rig_port = 4532U;
    cfg->rig_poll_sec = 5;
    cfg->printer_enabled = 0;
    oyk_sysv_copy_str(cfg->printer_device, sizeof(cfg->printer_device), "/dev/lp0");
    oyk_sysv_copy_str(cfg->printer_protocol, sizeof(cfg->printer_protocol), "generic");
    cfg->printer_baud = 9600UL;
    cfg->printer_condensed = 0;
    oyk_sysv_copy_str(cfg->active_theme, sizeof(cfg->active_theme), "native");
    cfg->theme_count = 3;
    oyk_sysv_theme_preset("native", &cfg->themes[0]);
    oyk_sysv_theme_preset("dark", &cfg->themes[1]);
    oyk_sysv_theme_preset("light", &cfg->themes[2]);

    cfg->iface_count = 1;
    oyk_sysv_copy_str(cfg->ifaces[0].id, sizeof(cfg->ifaces[0].id), "axudp0");
    oyk_sysv_copy_str(cfg->ifaces[0].type, sizeof(cfg->ifaces[0].type), "axudp");
    oyk_sysv_copy_str(cfg->ifaces[0].description, sizeof(cfg->ifaces[0].description), "Primary AXUDP");
    oyk_sysv_copy_str(cfg->ifaces[0].host, sizeof(cfg->ifaces[0].host), "0.0.0.0");
    cfg->ifaces[0].port = 10093U;
    oyk_sysv_copy_str(cfg->ifaces[0].netmask, sizeof(cfg->ifaces[0].netmask), "255.0.0.0");
    cfg->ifaces[0].allow_inbound = 1;
    cfg->ifaces[0].allow_outbound = 1;
}

OykSysvLineEnding oyk_sysv_detect_text_eol(const char *buf, size_t len) {
    size_t i;
    if (!buf) return OYK_SYSV_EOL_LF;
    for (i = 0U; i < len; ++i) {
        if (buf[i] == '\r') {
            if (i + 1U < len && buf[i + 1U] == '\n') return OYK_SYSV_EOL_CRLF;
            return OYK_SYSV_EOL_CR;
        }
        if (buf[i] == '\n') return OYK_SYSV_EOL_LF;
    }
    return OYK_SYSV_EOL_LF;
}

static int s_parse_bool(const char *val) {
    if (!val) return 0;
    if (oyk_sysv_strieq(val, "true") || oyk_sysv_strieq(val, "yes") ||
        oyk_sysv_strieq(val, "on") || strcmp(val, "1") == 0) {
        return 1;
    }
    return 0;
}

int oyk_sysv_config_parse(const char *text, size_t len, OykSysvConfig *cfg) {
    size_t pos;
    char sec[48];
    int cur_iface;
    int cur_peer;
    if (!text || !cfg) return 0;
    oyk_sysv_config_init_default(cfg);
    cfg->line_ending = oyk_sysv_detect_text_eol(text, len);
    cfg->has_detected_eol = 1;
    sec[0] = '\0';
    cur_iface = -1;
    cur_peer = -1;
    pos = 0U;

    while (pos < len) {
        char line[192];
        size_t li;
        char *eq;
        li = 0U;
        while (pos < len && text[pos] != '\r' && text[pos] != '\n') {
            if (li + 1U < sizeof(line)) line[li++] = text[pos];
            ++pos;
        }
        line[li] = '\0';
        if (pos < len && text[pos] == '\r') {
            ++pos;
            if (pos < len && text[pos] == '\n') ++pos;
        } else if (pos < len && text[pos] == '\n') {
            ++pos;
        }
        s_trim_inplace(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            char *rb;
            rb = strchr(line, ']');
            if (rb) *rb = '\0';
            oyk_sysv_copy_str(sec, sizeof(sec), line + 1);
            s_trim_inplace(sec);
            cur_iface = -1;
            cur_peer = -1;
            if (strncmp(sec, "interface ", 10) == 0 && cfg->iface_count < OYK_SYSV_MAX_IFACES) {
                if (cfg->iface_count == 1 && strcmp(cfg->ifaces[0].id, "axudp0") == 0 &&
                    strncmp(sec + 10, "axudp0", 6) != 0) {
                    cfg->iface_count = 0;
                }
                cur_iface = cfg->iface_count++;
                memset(&cfg->ifaces[cur_iface], 0, sizeof(cfg->ifaces[cur_iface]));
                oyk_sysv_copy_str(cfg->ifaces[cur_iface].id,
                                  sizeof(cfg->ifaces[cur_iface].id), sec + 10);
                cfg->ifaces[cur_iface].allow_inbound = 1;
                cfg->ifaces[cur_iface].allow_outbound = 1;
            } else if (strncmp(sec, "peer ", 5) == 0 && cfg->peer_count < OYK_SYSV_MAX_PEERS) {
                cur_peer = cfg->peer_count++;
                memset(&cfg->peers[cur_peer], 0, sizeof(cfg->peers[cur_peer]));
                oyk_sysv_copy_str(cfg->peers[cur_peer].id,
                                  sizeof(cfg->peers[cur_peer].id), sec + 5);
                cfg->peers[cur_peer].allow_inbound = 1;
                cfg->peers[cur_peer].allow_outbound = 1;
            }
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        s_trim_inplace(line);
        s_trim_inplace(eq + 1);
        if (cur_iface >= 0) {
            OykSysvIfaceConfig *ifc;
            ifc = &cfg->ifaces[cur_iface];
            if (oyk_sysv_strieq(line, "type")) oyk_sysv_copy_str(ifc->type, sizeof(ifc->type), eq + 1);
            else if (oyk_sysv_strieq(line, "host") || oyk_sysv_strieq(line, "bind_host")) {
                oyk_sysv_copy_str(ifc->host, sizeof(ifc->host), eq + 1);
            } else if (oyk_sysv_strieq(line, "port") || oyk_sysv_strieq(line, "bind_port")) {
                ifc->port = (sysv_u16)atoi(eq + 1);
            } else if (oyk_sysv_strieq(line, "allow_inbound")) {
                ifc->allow_inbound = s_parse_bool(eq + 1);
            } else if (oyk_sysv_strieq(line, "allow_outbound")) {
                ifc->allow_outbound = s_parse_bool(eq + 1);
            } else if (oyk_sysv_strieq(line, "allow_same_interface")) {
                ifc->allow_same_interface = s_parse_bool(eq + 1);
            }
        } else if (cur_peer >= 0) {
            OykSysvPeerConfig *pc;
            pc = &cfg->peers[cur_peer];
            if (oyk_sysv_strieq(line, "callsign")) oyk_sysv_copy_str(pc->callsign, sizeof(pc->callsign), eq + 1);
            else if (oyk_sysv_strieq(line, "interface")) oyk_sysv_copy_str(pc->interface_id, sizeof(pc->interface_id), eq + 1);
            else if (oyk_sysv_strieq(line, "host")) oyk_sysv_copy_str(pc->host, sizeof(pc->host), eq + 1);
            else if (oyk_sysv_strieq(line, "port")) pc->port = (sysv_u16)atoi(eq + 1);
            else if (oyk_sysv_strieq(line, "auto_learned")) pc->auto_learned = s_parse_bool(eq + 1);
        } else if (oyk_sysv_strieq(sec, "node") || !sec[0]) {
            if (oyk_sysv_strieq(line, "callsign")) oyk_sysv_parse_callsign(eq + 1, &cfg->callsign);
            else if (oyk_sysv_strieq(line, "alias")) oyk_sysv_parse_callsign(eq + 1, &cfg->alias);
            else if (oyk_sysv_strieq(line, "accept_incoming")) cfg->accept_incoming = s_parse_bool(eq + 1);
            else if (oyk_sysv_strieq(line, "theme")) oyk_sysv_copy_str(cfg->active_theme, sizeof(cfg->active_theme), eq + 1);
            else if (oyk_sysv_strieq(line, "bbs_files_path")) oyk_sysv_copy_str(cfg->bbs_files_path, sizeof(cfg->bbs_files_path), eq + 1);
            else if (oyk_sysv_strieq(line, "bbs_files_enabled")) cfg->bbs_files_enabled = s_parse_bool(eq + 1);
        }
    }
    return 1;
}

static size_t s_append_line(char *dst, size_t pos, size_t cap, const char *line, const char *eol) {
    size_t ll;
    size_t el;
    ll = strlen(line);
    el = strlen(eol);
    if (pos + ll + el + 1U >= cap) return pos;
    memcpy(dst + pos, line, ll);
    pos += ll;
    memcpy(dst + pos, eol, el);
    pos += el;
    dst[pos] = '\0';
    return pos;
}

size_t oyk_sysv_config_serialize(const OykSysvConfig *cfg, char *dst, size_t cap) {
    const char *eol;
    char call_s[16];
    char alias_s[16];
    char line[160];
    size_t pos;
    int i;
    if (!cfg || !dst || cap == 0U) return 0U;
    dst[0] = '\0';
    eol = (cfg->line_ending == OYK_SYSV_EOL_CRLF) ? "\r\n"
        : ((cfg->line_ending == OYK_SYSV_EOL_CR) ? "\r" : "\n");
    oyk_sysv_format_callsign(&cfg->callsign, call_s, sizeof(call_s));
    oyk_sysv_format_callsign(&cfg->alias, alias_s, sizeof(alias_s));
    pos = 0U;
    pos = s_append_line(dst, pos, cap, "[node]", eol);
    sprintf(line, "callsign = %s", call_s);
    pos = s_append_line(dst, pos, cap, line, eol);
    sprintf(line, "alias = %s", alias_s);
    pos = s_append_line(dst, pos, cap, line, eol);
    sprintf(line, "accept_incoming = %s", cfg->accept_incoming ? "true" : "false");
    pos = s_append_line(dst, pos, cap, line, eol);
    sprintf(line, "theme = %s", cfg->active_theme[0] ? cfg->active_theme : "native");
    pos = s_append_line(dst, pos, cap, line, eol);
    for (i = 0; i < cfg->iface_count; ++i) {
        const OykSysvIfaceConfig *ifc;
        ifc = &cfg->ifaces[i];
        sprintf(line, "[interface %s]", ifc->id);
        pos = s_append_line(dst, pos, cap, line, eol);
        sprintf(line, "type = %s", ifc->type);
        pos = s_append_line(dst, pos, cap, line, eol);
        sprintf(line, "host = %s", ifc->host);
        pos = s_append_line(dst, pos, cap, line, eol);
        sprintf(line, "port = %u", (unsigned int)ifc->port);
        pos = s_append_line(dst, pos, cap, line, eol);
        sprintf(line, "allow_same_interface = %s", ifc->allow_same_interface ? "true" : "false");
        pos = s_append_line(dst, pos, cap, line, eol);
    }
    return pos;
}

int oyk_sysv_config_load_file(const char *path, OykSysvConfig *cfg) {
    FILE *fp;
    char *buf;
    size_t n;
    int ok;
    if (!path || !cfg) return 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    buf = (char *)malloc(8192U);
    if (!buf) { fclose(fp); return 0; }
    n = fread(buf, 1U, 8191U, fp);
    buf[n] = '\0';
    fclose(fp);
    ok = oyk_sysv_config_parse(buf, n, cfg);
    free(buf);
    return ok;
}

int oyk_sysv_config_save_file(const OykSysvConfig *cfg, const char *path) {
    FILE *fp;
    char *buf;
    size_t n;
    size_t w;
    if (!cfg || !path) return 0;
    buf = (char *)malloc(8192U);
    if (!buf) return 0;
    n = oyk_sysv_config_serialize(cfg, buf, 8192U);
    fp = fopen(path, "wb");
    if (!fp) { free(buf); return 0; }
    w = (n > 0U) ? fwrite(buf, 1U, n, fp) : 0U;
    fclose(fp);
    free(buf);
    return (w == n) ? 1 : 0;
}

void oyk_sysv_node_init(OykSysvNode *node, const OykSysvConfig *cfg, const char *platform_label) {
    int i;
    if (!node) return;
    memset(node, 0, sizeof(*node));
    oyk_sysv_copy_str(node->platform_label, sizeof(node->platform_label),
                      platform_label ? platform_label : "System V UNIX");
    if (cfg) {
        node->cfg = *cfg;
    } else {
        oyk_sysv_config_init_default(&node->cfg);
    }
    for (i = 0; i < node->cfg.iface_count && i < OYK_SYSV_MAX_IFACES; ++i) {
        node->iface_rt[i].running = 1;
        oyk_sysv_copy_str(node->iface_rt[i].status_text,
                          sizeof(node->iface_rt[i].status_text), "UP");
    }
    oyk_sysv_node_log(node, "Node initialized (SysV C89 Core)");
}

void oyk_sysv_node_seed_demo(OykSysvNode *node) {
    if (!node) return;
    node->uptime_sec = 3725UL;
    if (node->cfg.iface_count < 2) {
        node->cfg.iface_count = 2;
        oyk_sysv_copy_str(node->cfg.ifaces[1].id, sizeof(node->cfg.ifaces[1].id), "kiss0");
        oyk_sysv_copy_str(node->cfg.ifaces[1].type, sizeof(node->cfg.ifaces[1].type), "kiss-serial");
        oyk_sysv_copy_str(node->cfg.ifaces[1].description, sizeof(node->cfg.ifaces[1].description), "Serial TNC");
        oyk_sysv_copy_str(node->cfg.ifaces[1].device, sizeof(node->cfg.ifaces[1].device), "/dev/ttya0");
        node->cfg.ifaces[1].baud = 9600UL;
        node->cfg.ifaces[1].allow_inbound = 1;
        node->cfg.ifaces[1].allow_outbound = 1;
        node->iface_rt[1].running = 1;
        oyk_sysv_copy_str(node->iface_rt[1].status_text,
                          sizeof(node->iface_rt[1].status_text), "UP");
    }
    node->iface_rt[0].rx_frames = 42UL;
    node->iface_rt[0].tx_frames = 38UL;
    node->iface_rt[1].rx_frames = 17UL;
    node->iface_rt[1].tx_frames = 14UL;

    if (node->cfg.peer_count == 0) {
        node->cfg.peer_count = 2;
        oyk_sysv_copy_str(node->cfg.peers[0].id, sizeof(node->cfg.peers[0].id), "peer0");
        oyk_sysv_copy_str(node->cfg.peers[0].callsign, sizeof(node->cfg.peers[0].callsign), "W3TRM-7");
        oyk_sysv_copy_str(node->cfg.peers[0].interface_id, sizeof(node->cfg.peers[0].interface_id), "axudp0");
        oyk_sysv_copy_str(node->cfg.peers[0].host, sizeof(node->cfg.peers[0].host), "192.0.2.10");
        node->cfg.peers[0].port = 10093U;
        node->cfg.peers[0].allow_inbound = 1;
        node->cfg.peers[0].allow_outbound = 1;

        oyk_sysv_copy_str(node->cfg.peers[1].id, sizeof(node->cfg.peers[1].id), "auto0");
        oyk_sysv_copy_str(node->cfg.peers[1].callsign, sizeof(node->cfg.peers[1].callsign), "K4PKT-4");
        oyk_sysv_copy_str(node->cfg.peers[1].interface_id, sizeof(node->cfg.peers[1].interface_id), "axudp0");
        oyk_sysv_copy_str(node->cfg.peers[1].host, sizeof(node->cfg.peers[1].host), "192.0.2.25");
        node->cfg.peers[1].port = 10093U;
        node->cfg.peers[1].allow_inbound = 1;
        node->cfg.peers[1].allow_outbound = 1;
        node->cfg.peers[1].auto_learned = 1;
    }

    node->heard_count = 3;
    oyk_sysv_copy_str(node->heard[0].callsign, 12U, "W3TRM-1");
    oyk_sysv_copy_str(node->heard[0].iface_id, 16U, "axudp0");
    oyk_sysv_copy_str(node->heard[0].time_hms, 10U, "17:20:11");
    oyk_sysv_copy_str(node->heard[1].callsign, 12U, "N4XYZ-7");
    oyk_sysv_copy_str(node->heard[1].iface_id, 16U, "kiss0");
    oyk_sysv_copy_str(node->heard[1].time_hms, 10U, "17:18:45");
    oyk_sysv_copy_str(node->heard[2].callsign, 12U, "K4PKT-4");
    oyk_sysv_copy_str(node->heard[2].iface_id, 16U, "axudp0");
    oyk_sysv_copy_str(node->heard[2].time_hms, 10U, "17:15:02");

    node->sessions[0].id = 1UL;
    node->sessions[0].state = 2;
    node->sessions[0].iface_idx = 0;
    oyk_sysv_copy_str(node->sessions[0].callsign, 12U, "W3TRM-1");
    oyk_sysv_copy_str(node->sessions[0].port_id, 16U, "axudp0");
    oyk_sysv_copy_str(node->sessions[0].dir, 8U, "IN");
    oyk_sysv_copy_str(node->sessions[0].since_hms, 10U, "17:19:00");
    oyk_sysv_copy_str(node->sessions[0].role, 12U, "BBS");

    node->sessions[1].id = 2UL;
    node->sessions[1].state = 2;
    node->sessions[1].iface_idx = 1;
    oyk_sysv_copy_str(node->sessions[1].callsign, 12U, "N4XYZ-7");
    oyk_sysv_copy_str(node->sessions[1].port_id, 16U, "kiss0");
    oyk_sysv_copy_str(node->sessions[1].dir, 8U, "IN");
    oyk_sysv_copy_str(node->sessions[1].since_hms, 10U, "17:21:05");
    oyk_sysv_copy_str(node->sessions[1].role, 12U, "CHAT");

    node->ban_count = 1;
    oyk_sysv_copy_str(node->bans[0].key, 12U, "N0BAD");
    oyk_sysv_copy_str(node->bans[0].banned_at, 18U, "20261127-1200");
    oyk_sysv_copy_str(node->bans[0].lift_at, 18U, "PERMANENT");
    node->bans[0].flags = 0x05UL;
    oyk_sysv_copy_str(node->bans[0].reason, 64U, "Excessive beacon floods");

    oyk_sysv_node_log(node, "W3TRM-1 connected to BBS on axudp0");
    oyk_sysv_node_log(node, "N4XYZ-7 joined CHAT Lobby on kiss0");
}

void oyk_sysv_node_log(OykSysvNode *node, const char *line) {
    int i;
    if (!node || !line) return;
    if (node->log_count >= OYK_SYSV_MAX_LOGS) {
        for (i = 1; i < OYK_SYSV_MAX_LOGS; ++i) {
            oyk_sysv_copy_str(node->logs[i - 1], 96U, node->logs[i]);
        }
        node->log_count = OYK_SYSV_MAX_LOGS - 1;
    }
    oyk_sysv_copy_str(node->logs[node->log_count++], 96U, line);
}

int oyk_sysv_node_active_conn_count(const OykSysvNode *node) {
    int c;
    int i;
    c = 0;
    if (!node) return 0;
    for (i = 0; i < OYK_SYSV_MAX_SESSIONS; ++i) {
        if (node->sessions[i].state == 2) ++c;
    }
    return c;
}

int oyk_sysv_node_auto_peer_count(const OykSysvNode *node) {
    int c;
    int i;
    c = 0;
    if (!node) return 0;
    for (i = 0; i < node->cfg.peer_count; ++i) {
        if (node->cfg.peers[i].auto_learned) ++c;
    }
    return c;
}

int oyk_sysv_node_terminate_session(OykSysvNode *node, sysv_u32 sess_id) {
    int i;
    if (!node) return 0;
    for (i = 0; i < OYK_SYSV_MAX_SESSIONS; ++i) {
        if (node->sessions[i].state != 0 && node->sessions[i].id == sess_id) {
            node->sessions[i].state = 0;
            return 1;
        }
    }
    return 0;
}

int oyk_sysv_node_ban_callsign(OykSysvNode *node, const char *call, const char *reason) {
    OykSysvCallsign cs;
    char key[16];
    int i;
    if (!node || !oyk_sysv_parse_callsign(call, &cs)) return 0;
    oyk_sysv_format_callsign(&cs, key, sizeof(key));
    for (i = 0; i < node->ban_count; ++i) {
        if (oyk_sysv_strieq(node->bans[i].key, key)) return 1;
    }
    if (node->ban_count >= OYK_SYSV_MAX_BANS) return 0;
    i = node->ban_count++;
    memset(&node->bans[i], 0, sizeof(node->bans[i]));
    oyk_sysv_copy_str(node->bans[i].key, sizeof(node->bans[i].key), key);
    oyk_sysv_copy_str(node->bans[i].banned_at, sizeof(node->bans[i].banned_at), "NOW");
    oyk_sysv_copy_str(node->bans[i].lift_at, sizeof(node->bans[i].lift_at), "PERMANENT");
    oyk_sysv_copy_str(node->bans[i].reason, sizeof(node->bans[i].reason), reason ? reason : "Sysop ban");
    return 1;
}

void oyk_sysv_wiz_init(OykSysvAddIfaceWiz *wiz, const OykSysvConfig *cfg) {
    if (!wiz) return;
    memset(wiz, 0, sizeof(*wiz));
    oyk_sysv_wiz_select_driver(wiz, cfg, 0);
}

int oyk_sysv_wiz_driver_count(void) {
    return 5;
}

const char *oyk_sysv_wiz_driver_name(int idx) {
    static const char * const names[5] = {
        "axudp", "axip", "kiss-serial", "kiss-tcp", "telnet"
    };
    if (idx < 0 || idx >= 5) return "";
    return names[idx];
}

const char *oyk_sysv_wiz_driver_desc(int idx) {
    static const char * const descs[5] = {
        "AXUDP over UDP/IP (RFC 1226 / PoPT)",
        "AXIP raw IP protocol 93 link",
        "Serial KISS TNC (/dev/ttya0)",
        "TCP KISS modem (Dire Wolf / AGW)",
        "Plain Telnet terminal listener"
    };
    if (idx < 0 || idx >= 5) return "";
    return descs[idx];
}

void oyk_sysv_wiz_select_driver(OykSysvAddIfaceWiz *wiz, const OykSysvConfig *cfg, int idx) {
    int count;
    if (!wiz) return;
    if (idx < 0 || idx >= 5) idx = 0;
    wiz->selected_driver_idx = idx;
    memset(&wiz->draft, 0, sizeof(wiz->draft));
    count = cfg ? cfg->iface_count : 0;
    sprintf(wiz->draft.id, "if%d", count);
    oyk_sysv_copy_str(wiz->draft.type, sizeof(wiz->draft.type), oyk_sysv_wiz_driver_name(idx));
    oyk_sysv_copy_str(wiz->draft.description, sizeof(wiz->draft.description), oyk_sysv_wiz_driver_desc(idx));
    oyk_sysv_copy_str(wiz->draft.host, sizeof(wiz->draft.host), "0.0.0.0");
    wiz->draft.port = (idx == 0) ? 10093U : ((idx == 3) ? 8001U : ((idx == 4) ? 2323U : 0U));
    oyk_sysv_copy_str(wiz->draft.device, sizeof(wiz->draft.device), "/dev/ttya0");
    wiz->draft.baud = 9600UL;
    wiz->draft.allow_inbound = 1;
    wiz->draft.allow_outbound = 1;
    wiz->error[0] = '\0';
}

int oyk_sysv_wiz_advance(OykSysvAddIfaceWiz *wiz, const OykSysvConfig *cfg) {
    (void)cfg;
    if (!wiz) return 0;
    if (wiz->step == 1 && !wiz->draft.id[0]) {
        oyk_sysv_copy_str(wiz->error, sizeof(wiz->error), "Interface ID is required.");
        return 0;
    }
    if (wiz->step < 3) {
        ++wiz->step;
        wiz->error[0] = '\0';
        return 1;
    }
    return 1;
}

void oyk_sysv_wiz_back(OykSysvAddIfaceWiz *wiz) {
    if (!wiz) return;
    if (wiz->step > 0) --wiz->step;
    wiz->error[0] = '\0';
}

const char *oyk_sysv_wiz_step_prompt(const OykSysvAddIfaceWiz *wiz) {
    if (!wiz) return "";
    switch (wiz->step) {
        case 0: return "Step 1: Driver";
        case 1: return "Step 2: Identity";
        case 2: return "Step 3: Endpoint & Policy";
        default: return "Step 4: Review Summary";
    }
}

void oyk_sysv_wiz_format_summary(const OykSysvAddIfaceWiz *wiz, char *dst, size_t cap) {
    char buf[240];
    if (!wiz || !dst || cap == 0U) return;
    sprintf(buf,
            "ID: %s | Driver: %s | Host/Dev: %s:%u | In:%s Out:%s | Same-If:%s | Learned:%s",
            wiz->draft.id,
            wiz->draft.type,
            (strcmp(wiz->draft.type, "kiss-serial") == 0) ? wiz->draft.device : wiz->draft.host,
            (unsigned int)wiz->draft.port,
            wiz->draft.allow_inbound ? "Y" : "N",
            wiz->draft.allow_outbound ? "Y" : "N",
            wiz->draft.allow_same_interface ? "Y" : "N",
            oyk_sysv_format_learned_policy(wiz->draft.learned_peer_policy));
    oyk_sysv_copy_str(dst, cap, buf);
}

void oyk_sysv_theme_init(OykSysvThemeCtrl *tc, OykSysvConfig *draft,
                         int default_native, int os_dark) {
    if (!tc) return;
    memset(tc, 0, sizeof(*tc));
    tc->draft = draft;
    tc->default_native = default_native ? 1 : 0;
    tc->os_dark = os_dark ? 1 : 0;
    oyk_sysv_theme_preset("native", &tc->os_native_pal);
    oyk_sysv_copy_str(tc->active_name, sizeof(tc->active_name),
                      (draft && draft->active_theme[0]) ? draft->active_theme : "native");
    oyk_sysv_copy_str(tc->saved_name, sizeof(tc->saved_name), tc->active_name);
}

void oyk_sysv_theme_set_preview_cb(OykSysvThemeCtrl *tc,
                                   OykSysvThemePreviewFn cb, void *ctx) {
    if (!tc) return;
    tc->preview_cb = cb;
    tc->preview_ctx = ctx;
}

void oyk_sysv_theme_resolve(const OykSysvThemeCtrl *tc, OykSysvThemeConfig *out) {
    const char *name;
    int i;
    if (!out) return;
    name = (tc && tc->active_name[0]) ? tc->active_name : "native";
    if (tc && tc->draft) {
        for (i = 0; i < tc->draft->theme_count; ++i) {
            if (oyk_sysv_strieq(tc->draft->themes[i].name, name)) {
                *out = tc->draft->themes[i];
                return;
            }
        }
    }
    oyk_sysv_theme_preset(name, out);
}

void oyk_sysv_theme_select(OykSysvThemeCtrl *tc, const char *name) {
    OykSysvThemeConfig pal;
    if (!tc || !name) return;
    oyk_sysv_copy_str(tc->active_name, sizeof(tc->active_name), name);
    if (tc->draft) oyk_sysv_copy_str(tc->draft->active_theme, sizeof(tc->draft->active_theme), name);
    tc->preview_active = 1;
    tc->preview_generation++;
    if (tc->preview_cb) {
        oyk_sysv_theme_resolve(tc, &pal);
        tc->preview_cb(&pal, tc->active_name, tc->preview_ctx);
    }
}

void oyk_sysv_theme_cycle(OykSysvThemeCtrl *tc, int delta) {
    static const char * const names[3] = { "native", "dark", "light" };
    int cur;
    int i;
    int next;
    if (!tc) return;
    cur = 0;
    for (i = 0; i < 3; ++i) {
        if (oyk_sysv_strieq(tc->active_name, names[i])) { cur = i; break; }
    }
    next = (cur + delta) % 3;
    if (next < 0) next += 3;
    oyk_sysv_theme_select(tc, names[next]);
}

void oyk_sysv_theme_revert(OykSysvThemeCtrl *tc) {
    if (!tc) return;
    oyk_sysv_theme_select(tc, tc->saved_name);
    tc->dirty = 0;
    tc->preview_active = 0;
}

int oyk_sysv_term_decode_ctrl_a(int ch) {
    int u;
    if (ch == 'z' || ch == 'Z' || ch == '?' || ch == 'h' || ch == 'H') return OYK_SYSV_TCMD_HELP;
    u = s_toupper(ch);
    if (u == 'X' || u == 'Q') return OYK_SYSV_TCMD_CLOSE;
    if (u == 'L') return OYK_SYSV_TCMD_CLEAR;
    if (u == 'W') return OYK_SYSV_TCMD_WRAP;
    if (u == 'E') return OYK_SYSV_TCMD_EOL;
    if (u == 'O') return OYK_SYSV_TCMD_ECHO;
    if (u == 'S') return OYK_SYSV_TCMD_UPLOAD;
    if (u == 'A') return OYK_SYSV_TCMD_LITERAL_CTRL_A;
    return OYK_SYSV_TCMD_NONE;
}

const char *oyk_sysv_term_hotkey_bar_text(int ctrl_a_pending) {
    if (ctrl_a_pending) {
        return "Ctrl-A: Z/Help  X:Close  L:Clear  W:Wrap  E:EOL  O:Echo  S:Upload";
    }
    return "Ctrl-A Z:Help  Tab/F9:Console  [EOL:CRLF]  [Echo:ON]";
}

int oyk_sysv_term_help_count(void) {
    return 8;
}

const char *oyk_sysv_term_help_key(int idx) {
    static const char * const k[8] = {
        "Ctrl-A Z", "Ctrl-A X", "Ctrl-A L", "Ctrl-A W",
        "Ctrl-A E", "Ctrl-A O", "Ctrl-A S", "Ctrl-A A"
    };
    return (idx >= 0 && idx < 8) ? k[idx] : "";
}

const char *oyk_sysv_term_help_action(int idx) {
    static const char * const a[8] = {
        "Terminal Hotkey Help", "Close Terminal / Back to Console", "Clear Output",
        "Toggle Word Wrap", "Cycle Line Ending", "Toggle Local Echo",
        "Send YAPP Upload", "Send Literal Ctrl-A"
    };
    return (idx >= 0 && idx < 8) ? a[idx] : "";
}

const char *oyk_sysv_term_help_alt(int idx) {
    static const char * const alt[8] = {
        "?", "Esc / F9", "-", "-", "-", "-", "-", "-"
    };
    return (idx >= 0 && idx < 8) ? alt[idx] : "";
}

int oyk_sysv_term_wrap_line(const char *text, int len, int width,
                            OykSysvTermSpan *spans, int max_spans) {
    int count;
    int pos;
    if (!text || width <= 0 || !spans || max_spans <= 0) return 0;
    if (len <= 0) {
        spans[0].offset = 0;
        spans[0].len = 0;
        return 1;
    }
    count = 0;
    pos = 0;
    while (pos < len && count < max_spans) {
        int end;
        int brk;
        end = pos + width;
        if (end >= len) {
            spans[count].offset = pos;
            spans[count].len = len - pos;
            ++count;
            break;
        }
        brk = end;
        while (brk > pos && text[brk] != ' ') --brk;
        if (brk <= pos) brk = end;
        spans[count].offset = pos;
        spans[count].len = brk - pos;
        ++count;
        pos = brk;
        while (pos < len && text[pos] == ' ') ++pos;
    }
    return count;
}

int oyk_sysv_resolve_safe_path(const char *root, const char *cur_rel,
                               const char *user_arg, int allow_parent_in_root,
                               char *abs_out, size_t abs_sz,
                               char *rel_out, size_t rel_sz,
                               char *err_out, size_t err_sz) {
    char stack[8][32];
    char seg[32];
    int depth;
    const char *p;
    const char *base;
    const char *arg;
    char abs_buf[192];
    char rel_buf[96];
    int i;
    if (abs_out && abs_sz > 0U) abs_out[0] = '\0';
    if (rel_out && rel_sz > 0U) rel_out[0] = '\0';
    base = (root && root[0]) ? root : "files";
    arg = user_arg ? user_arg : "";
    for (i = 0; arg[i]; ++i) {
        unsigned char c;
        c = (unsigned char)arg[i];
        if (c < 0x20U || c >= 0x7FU || c == ':' || c == '\\') {
            oyk_sysv_copy_str(err_out, err_sz, "Access denied: invalid path character.");
            return 0;
        }
    }
    depth = 0;
    if (arg[0] != '/' && cur_rel) {
        p = cur_rel;
        while (*p && depth < 8) {
            size_t sl;
            while (*p == '/') ++p;
            sl = 0U;
            while (*p && *p != '/' && sl + 1U < sizeof(seg)) seg[sl++] = *p++;
            seg[sl] = '\0';
            if (sl > 0U && strcmp(seg, ".") != 0 && strcmp(seg, "..") != 0) {
                oyk_sysv_copy_str(stack[depth++], sizeof(stack[0]), seg);
            }
        }
    }
    p = arg;
    while (*p) {
        size_t sl;
        while (*p == '/') ++p;
        if (!*p) break;
        sl = 0U;
        while (*p && *p != '/' && sl + 1U < sizeof(seg)) seg[sl++] = *p++;
        seg[sl] = '\0';
        if (*p && *p != '/') {
            oyk_sysv_copy_str(err_out, err_sz, "Access denied: component too long.");
            return 0;
        }
        if (strcmp(seg, ".") == 0) {
            continue;
        } else if (strcmp(seg, "..") == 0) {
            if (!allow_parent_in_root || depth <= 0) {
                oyk_sysv_copy_str(err_out, err_sz, "Access denied: cannot escape root.");
                return 0;
            }
            --depth;
        } else {
            if (!oyk_sysv_is_safe_filename(seg) || depth >= 8) {
                oyk_sysv_copy_str(err_out, err_sz, "Access denied: invalid component.");
                return 0;
            }
            oyk_sysv_copy_str(stack[depth++], sizeof(stack[0]), seg);
        }
    }
    oyk_sysv_copy_str(abs_buf, sizeof(abs_buf), base);
    rel_buf[0] = '\0';
    for (i = 0; i < depth; ++i) {
        size_t al;
        size_t rl;
        al = strlen(abs_buf);
        if (al > 0U && abs_buf[al - 1U] != '/' && al + 1U < sizeof(abs_buf)) {
            abs_buf[al++] = '/';
            abs_buf[al] = '\0';
        }
        if (al + strlen(stack[i]) + 1U < sizeof(abs_buf)) strcat(abs_buf, stack[i]);
        rl = strlen(rel_buf);
        if (rl > 0U && rl + 1U < sizeof(rel_buf)) strcat(rel_buf, "/");
        if (strlen(rel_buf) + strlen(stack[i]) + 1U < sizeof(rel_buf)) strcat(rel_buf, stack[i]);
    }
    if (abs_out) oyk_sysv_copy_str(abs_out, abs_sz, abs_buf);
    if (rel_out) oyk_sysv_copy_str(rel_out, rel_sz, rel_buf);
    return 1;
}

static void s_scan_real_pane_entries(OykSysvFilePane *p) {
#if !defined(__VMS) && !defined(_WIN32)
    char dir_path[192];
    DIR *d;
    struct dirent *de;
#endif
    memset(p->entries, 0, sizeof(p->entries));
    p->count = 0;
    p->sel = 0;
    if (p->rel[0]) {
        oyk_sysv_copy_str(p->entries[p->count].name, 48U, "..");
        p->entries[p->count].is_dir = 1;
        p->entries[p->count].is_parent = 1;
        p->count++;
    }
#if !defined(__VMS) && !defined(_WIN32)
    {
        char nrel[64];
        char err[64];
        if (!oyk_sysv_resolve_safe_path(p->root, p->rel, ".", 1,
                                        dir_path, sizeof(dir_path),
                                        nrel, sizeof(nrel), err, sizeof(err))) {
            return;
        }
        d = opendir(dir_path);
        if (!d) return;
        while ((de = readdir(d)) != NULL && p->count < OYK_SYSV_MAX_DIR_ENTS) {
            char full[512];
            struct stat st;
            if (!oyk_sysv_is_safe_filename(de->d_name)) continue;
            oyk_sysv_copy_str(p->entries[p->count].name, 48U, de->d_name);
            p->entries[p->count].is_dir = 0;
            p->entries[p->count].is_parent = 0;
            p->entries[p->count].size = 0UL;
            if (strlen(dir_path) + 1U + strlen(de->d_name) + 1U < sizeof(full)) {
                sprintf(full, "%s/%s", dir_path, de->d_name);
                if (stat(full, &st) == 0) {
                    int is_d;
                    is_d = 0;
#ifdef S_ISDIR
                    is_d = S_ISDIR(st.st_mode) ? 1 : 0;
#elif defined(S_IFMT) && defined(S_IFDIR)
                    is_d = ((st.st_mode & S_IFMT) == S_IFDIR) ? 1 : 0;
#endif
                    if (is_d) {
                        p->entries[p->count].is_dir = 1;
                    } else {
                        p->entries[p->count].size = (unsigned long)st.st_size;
                    }
                }
            }
            p->count++;
        }
        closedir(d);
    }
#endif
}

static void s_seed_pane_entries(OykSysvFilePane *p, int is_inc) {
    if (!p->seed_demo) {
        s_scan_real_pane_entries(p);
        return;
    }
    memset(p->entries, 0, sizeof(p->entries));
    p->count = 0;
    p->sel = 0;
    if (p->rel[0]) {
        oyk_sysv_copy_str(p->entries[p->count].name, 48U, "..");
        p->entries[p->count].is_dir = 1;
        p->entries[p->count].is_parent = 1;
        p->count++;
        oyk_sysv_copy_str(p->entries[p->count].name, 48U,
                          is_inc ? "fieldday.txt" : "welcome.txt");
        p->entries[p->count].size = is_inc ? 512UL : 256UL;
        p->count++;
    } else if (is_inc) {
        oyk_sysv_copy_str(p->entries[p->count].name, 48U, "W3TRM-1");
        p->entries[p->count].is_dir = 1;
        p->count++;
        oyk_sysv_copy_str(p->entries[p->count].name, 48U, "packet.txt");
        p->entries[p->count].size = 1024UL;
        p->count++;
    } else {
        oyk_sysv_copy_str(p->entries[p->count].name, 48U, "bulletins");
        p->entries[p->count].is_dir = 1;
        p->count++;
        oyk_sysv_copy_str(p->entries[p->count].name, 48U, "readme.txt");
        p->entries[p->count].size = 384UL;
        p->count++;
    }
}

void oyk_sysv_filemgr_init_ex(OykSysvFileMgr *fm, const char *inc_root,
                              const char *files_root, int seed_demo) {
    if (!fm) return;
    memset(fm, 0, sizeof(*fm));
    fm->inc.seed_demo = seed_demo ? 1 : 0;
    fm->files.seed_demo = seed_demo ? 1 : 0;
    oyk_sysv_copy_str(fm->inc.root, sizeof(fm->inc.root), inc_root ? inc_root : "incoming");
    oyk_sysv_copy_str(fm->files.root, sizeof(fm->files.root), files_root ? files_root : "files");
    s_seed_pane_entries(&fm->inc, 1);
    s_seed_pane_entries(&fm->files, 0);
}

void oyk_sysv_filemgr_init(OykSysvFileMgr *fm, const char *inc_root, const char *files_root) {
    oyk_sysv_filemgr_init_ex(fm, inc_root, files_root, 1);
}

void oyk_sysv_filemgr_refresh(OykSysvFileMgr *fm, int pane_idx) {
    if (!fm) return;
    if (pane_idx == 0) s_seed_pane_entries(&fm->inc, 1);
    else s_seed_pane_entries(&fm->files, 0);
}

void oyk_sysv_filemgr_pane_title(const OykSysvFilePane *p, const char *label,
                                 char *out, size_t cap) {
    char buf[96];
    if (!out || cap == 0U) return;
    if (!p || !p->rel[0]) {
        sprintf(buf, "%s /", label ? label : "Files:");
    } else {
        sprintf(buf, "%s /%s", label ? label : "Files:", p->rel);
    }
    oyk_sysv_copy_str(out, cap, buf);
}

int oyk_sysv_filemgr_enter_dir(OykSysvFileMgr *fm, int pane_idx, const char *child) {
    OykSysvFilePane *p;
    char abs_d[192];
    char nrel[64];
    char err[64];
    if (!fm || !child) return 0;
    p = (pane_idx == 0) ? &fm->inc : &fm->files;
    if (!oyk_sysv_resolve_safe_path(p->root, p->rel, child, 1,
                                    abs_d, sizeof(abs_d), nrel, sizeof(nrel),
                                    err, sizeof(err))) {
        oyk_sysv_copy_str(fm->feedback, sizeof(fm->feedback), err);
        return 0;
    }
    oyk_sysv_copy_str(p->rel, sizeof(p->rel), nrel);
    s_seed_pane_entries(p, pane_idx == 0);
    return 1;
}

int oyk_sysv_filemgr_up(OykSysvFileMgr *fm, int pane_idx) {
    return oyk_sysv_filemgr_enter_dir(fm, pane_idx, "..");
}

void oyk_sysv_app_init(OykSysvApp *app, const char *platform_label, int seed_demo) {
    if (!app) return;
    memset(app, 0, sizeof(*app));
    oyk_sysv_node_init(&app->node, NULL, platform_label);
    if (seed_demo) {
        oyk_sysv_node_seed_demo(&app->node);
    }
    oyk_sysv_wiz_init(&app->wiz, &app->node.cfg);
    oyk_sysv_theme_init(&app->theme_ctrl, &app->node.cfg, 1, 0);
    oyk_sysv_filemgr_init_ex(&app->fm, app->node.cfg.incoming_path,
                             app->node.cfg.bbs_files_path, seed_demo);
    app->term_eol.eol = OYK_SYSV_EOL_CRLF;
    app->term_eol.echo = 1;
    app->term_eol.word_wrap = 1;
    if (seed_demo) {
        app->term_connected = 1;
        oyk_sysv_copy_str(app->term_status, sizeof(app->term_status), "Connected to KR4OYK-1 (BBS)");
        oyk_sysv_app_term_append(app, "*** Connected to KR4OYK-1 Packet BBS");
        oyk_sysv_app_term_append(app, "Type H for BBS commands, Ctrl-A Z for Help.");
    } else {
        app->term_connected = 0;
        oyk_sysv_copy_str(app->term_status, sizeof(app->term_status), "Disconnected");
        oyk_sysv_app_term_append(app, "*** OYK Local Packet Terminal (Disconnected)");
        oyk_sysv_app_term_append(app, "Ctrl-A Z:Help | Tab/F9:Console");
    }
    app->running = 1;
}

void oyk_sysv_app_exec_menu_action(OykSysvApp *app, int action) {
    if (!app) return;
    switch (action) {
        case OYK_SYSV_ACT_ALLOW_INCOMING:
            app->node.cfg.accept_incoming = 1;
            app->screen = OYK_SYSV_SCR_MAIN;
            oyk_sysv_copy_str(app->feedback, sizeof(app->feedback), "Incoming connections allowed.");
            break;
        case OYK_SYSV_ACT_DENY_INCOMING:
            app->node.cfg.accept_incoming = 0;
            app->screen = OYK_SYSV_SCR_MAIN;
            oyk_sysv_copy_str(app->feedback, sizeof(app->feedback), "Incoming connections denied.");
            break;
        case OYK_SYSV_ACT_LOCAL_TERM:
            app->screen = OYK_SYSV_SCR_TERMINAL;
            break;
        case OYK_SYSV_ACT_FILES:
            oyk_sysv_filemgr_refresh(&app->fm, 0);
            oyk_sysv_filemgr_refresh(&app->fm, 1);
            app->screen = OYK_SYSV_SCR_FILES;
            break;
        case OYK_SYSV_ACT_RIG:
            app->screen = OYK_SYSV_SCR_SETTINGS;
            app->settings_tab = 5;
            app->settings_row = 0;
            app->modal_btn = 0;
            break;
        case OYK_SYSV_ACT_SETTINGS:
            app->screen = OYK_SYSV_SCR_SETTINGS;
            app->settings_tab = 0;
            app->settings_row = 0;
            app->modal_btn = 0;
            break;
        case OYK_SYSV_ACT_PEERS:
            app->screen = OYK_SYSV_SCR_SETTINGS;
            app->settings_tab = 4;
            app->settings_row = 0;
            app->modal_btn = 0;
            break;
        case OYK_SYSV_ACT_THEME:
            app->screen = OYK_SYSV_SCR_SETTINGS;
            app->settings_tab = 7;
            app->settings_row = 0;
            app->modal_btn = 0;
            break;
        case OYK_SYSV_ACT_TERMINATE:
            app->modal_btn = 0;
            app->screen = OYK_SYSV_SCR_TERMINATE;
            break;
        case OYK_SYSV_ACT_BAN:
            app->modal_btn = 0;
            app->screen = OYK_SYSV_SCR_BAN;
            break;
        case OYK_SYSV_ACT_ABOUT:
            app->modal_btn = 0;
            app->screen = OYK_SYSV_SCR_ABOUT;
            break;
        case OYK_SYSV_ACT_EXIT:
            if (oyk_sysv_node_auto_peer_count(&app->node) > 0) {
                app->exit_btn_sel = 0;
                app->screen = OYK_SYSV_SCR_EXIT_PEERS;
            } else {
                app->running = 0;
            }
            break;
        case OYK_SYSV_ACT_HELP_HOTKEYS:
            app->term_help_from_term = (app->screen == OYK_SYSV_SCR_TERMINAL) ? 1 : 0;
            app->screen = OYK_SYSV_SCR_TERM_HELP;
            break;
        default:
            break;
    }
}

void oyk_sysv_app_term_append(OykSysvApp *app, const char *text) {
    int i;
    if (!app || !text) return;
    if (app->term_line_count >= OYK_SYSV_MAX_TERM_LINES) {
        for (i = 1; i < OYK_SYSV_MAX_TERM_LINES; ++i) {
            oyk_sysv_copy_str(app->term_lines[i - 1], 84U, app->term_lines[i]);
        }
        app->term_line_count = OYK_SYSV_MAX_TERM_LINES - 1;
    }
    oyk_sysv_copy_str(app->term_lines[app->term_line_count++], 84U, text);
}

int oyk_sysv_rigctl_build_set_freq(sysv_u32 hz, char *dst, size_t cap) {
    char buf[32];
    if (!dst || cap == 0U || hz == 0UL) return 0;
    sprintf(buf, "F %lu\n", (unsigned long)hz);
    oyk_sysv_copy_str(dst, cap, buf);
    return (int)strlen(dst);
}

int oyk_sysv_printer_format_line(const char *proto, const char *msg,
                                 unsigned char *dst, size_t cap) {
    char buf[128];
    size_t n;
    (void)proto;
    if (!dst || cap == 0U) return 0;
    sprintf(buf, "%s\r\n", msg ? msg : "");
    n = strlen(buf);
    if (n >= cap) n = cap - 1U;
    memcpy(dst, buf, n);
    dst[n] = '\0';
    return (int)n;
}
