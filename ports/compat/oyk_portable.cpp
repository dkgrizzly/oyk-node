/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_portable.hpp"
#include "oyk_yapp.hpp"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#define OYK_MKDIR(p) _mkdir(p)
#elif defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
#include <direct.h>
#include <dos.h>
#define OYK_MKDIR(p) mkdir(p)
#elif defined(OYK_TARGET_IMXRT1060) || defined(TEENSYDUINO) || (defined(__arm__) && !defined(__linux__) && !defined(__unix__))
#include <sys/stat.h>
#if (defined(OYK_HAS_FATFS) && OYK_HAS_FATFS) || defined(TEENSYDUINO)
extern "C" {
int oyk_imxrt_mkdir(const char* path);
int oyk_imxrt_is_regular_file(const char* path);
int oyk_imxrt_is_directory(const char* path);
void* oyk_imxrt_opendir(const char* dir_path);
int oyk_imxrt_readdir(void* dir_handle, char* name_out, size_t name_sz,
                      int* is_dir_out, oyk_u32* size_out);
void oyk_imxrt_closedir(void* dir_handle);
}
#define OYK_MKDIR(p) oyk_imxrt_mkdir(p)
#else
#define OYK_MKDIR(p) ((void)(p), -1)
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#define OYK_MKDIR(p) mkdir((p), 0755)
#endif

namespace {

void safe_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_sz && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

int stricmp_ascii(const char* a, const char* b) {
    while (*a && *b) {
        int ca = toupper(static_cast<unsigned char>(*a));
        int cb = toupper(static_cast<unsigned char>(*b));
        if (ca != cb) return ca - cb;
        ++a;
        ++b;
    }
    return toupper(static_cast<unsigned char>(*a)) - toupper(static_cast<unsigned char>(*b));
}

void trim_inplace(char* s) {
    if (!s) return;
    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = len;
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    if (start > 0) {
        memmove(s, s + start, end - start);
    }
    s[end - start] = '\0';
}

void format_flags_bin(oyk_u32 flags, char* out, size_t out_sz) {
    int width = (flags > 0x0FU) ? 8 : 4;
    if (out_sz < static_cast<size_t>(width + 3)) {
        if (out_sz > 0) out[0] = '\0';
        return;
    }
    out[0] = '0';
    out[1] = 'b';
    int pos = 2;
    for (int i = width - 1; i >= 0; --i) {
        out[pos++] = ((flags >> i) & 1U) ? '1' : '0';
    }
    out[pos] = '\0';
}

const char* level_name_str(oyk_u32 level) {
    switch (level) {
        case OYK_LOG_ERROR:   return "ERROR";
        case OYK_LOG_WARNING: return "WARN";
        case OYK_LOG_INFO:    return "INFO";
        case OYK_LOG_KICK:    return "KICK";
        case OYK_LOG_BAN:     return "BAN";
        case OYK_LOG_APPEAL:  return "APPEAL";
        case OYK_LOG_DEBUG:   return "DEBUG";
        default:              return "LOG";
    }
}

void make_dir_simple(const char* path) {
    if (!path || !path[0]) return;
    char tmp[128];
    safe_copy(tmp, sizeof(tmp), path);
    size_t len = strlen(tmp);
    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
        tmp[--len] = '\0';
    }
    OYK_MKDIR(tmp);
}

void join_path(const char* dir, const char* file, char* out, size_t out_sz) {
    if (!dir || !dir[0]) {
        safe_copy(out, out_sz, file);
        return;
    }
    safe_copy(out, out_sz, dir);
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] != '/' && out[len - 1] != '\\' && len + 1 < out_sz) {
#if defined(_WIN32) || defined(_WIN64) || defined(__DOS__) || defined(__MSDOS__) || defined(__WATCOMC__)
        out[len++] = '\\';
#else
        out[len++] = '/';
#endif
        out[len] = '\0';
    }
    size_t rem = (len < out_sz) ? (out_sz - len - 1) : 0;
    if (rem > 0) {
        strncat(out, file, rem);
    }
}

/* Reads entire file into heap buffer (caller frees). */
char* read_all_binary(const char* path, size_t* len_out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz < 0 || sz > 32760L) {
        fclose(fp);
        return NULL;
    }
    fseek(fp, 0, SEEK_SET);
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = (sz > 0) ? fread(buf, 1, static_cast<size_t>(sz), fp) : 0;
    buf[n] = '\0';
    fclose(fp);
    if (len_out) *len_out = n;
    return buf;
}

/* Extracts next line from raw buffer accepting CRLF, CR, or LF. */
int next_line_any_eol(const char* buf, size_t len, size_t* pos, char* line_out, size_t cap) {
    if (*pos >= len) return 0;
    size_t start = *pos;
    size_t i = start;
    while (i < len && buf[i] != '\r' && buf[i] != '\n') {
        ++i;
    }
    size_t copy_len = i - start;
    if (copy_len >= cap) copy_len = cap - 1;
    memcpy(line_out, buf + start, copy_len);
    line_out[copy_len] = '\0';

    if (i < len) {
        if (buf[i] == '\r') {
            ++i;
            if (i < len && buf[i] == '\n') ++i;
        } else if (buf[i] == '\n') {
            ++i;
        }
    }
    *pos = i;
    return 1;
}

int normalize_ban_key(const char* spec, char* out, size_t out_sz) {
    char tmp[32];
    safe_copy(tmp, sizeof(tmp), spec);
    trim_inplace(tmp);
    if (!tmp[0]) return 0;

    int has_dash = (strchr(tmp, '-') != NULL);
    OykCallsign c;
    if (!oyk_parse_callsign(tmp, &c)) return 0;
    if (has_dash && c.ssid > 0) {
        oyk_format_callsign(&c, out, out_sz);
    } else {
        safe_copy(out, out_sz, c.base);
    }
    return 1;
}

int valid_timestamp_15(const char* s) {
    if (!s || strlen(s) != 15 || s[8] != '-') return 0;
    for (int i = 0; i < 15; ++i) {
        if (i == 8) continue;
        if (!isdigit(static_cast<unsigned char>(s[i]))) return 0;
    }
    return 1;
}

} // namespace

OykLineEnding oyk_platform_default_eol() {
#if defined(_WIN32) || defined(_WIN64) || defined(_WIN16) || defined(__WINDOWS__) || \
    defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
    return OYK_EOL_CRLF;
#else
    return OYK_EOL_LF;
#endif
}

const char* oyk_eol_bytes(OykLineEnding eol) {
    if (eol == OYK_EOL_CRLF) return "\r\n";
    if (eol == OYK_EOL_CR)   return "\r";
    return "\n";
}

OykLineEnding oyk_detect_first_eol(const char* buf, size_t len,
                                   OykLineEnding fallback,
                                   int* found_any_out, int* mixed_out) {
    OykLineEnding first = fallback;
    int found = 0;
    int mixed = 0;
    for (size_t i = 0; i < len; ++i) {
        OykLineEnding cur;
        if (buf[i] == '\r') {
            if (i + 1 < len && buf[i + 1] == '\n') {
                cur = OYK_EOL_CRLF;
                ++i;
            } else {
                cur = OYK_EOL_CR;
            }
        } else if (buf[i] == '\n') {
            cur = OYK_EOL_LF;
        } else {
            continue;
        }
        if (!found) {
            first = cur;
            found = 1;
        } else if (cur != first) {
            mixed = 1;
        }
    }
    if (found_any_out) *found_any_out = found;
    if (mixed_out) *mixed_out = mixed;
    return first;
}

int oyk_parse_callsign(const char* text, OykCallsign* out) {
    if (!text || !out) return 0;
    char tmp[32];
    safe_copy(tmp, sizeof(tmp), text);
    trim_inplace(tmp);
    if (!tmp[0]) return 0;

    char* dash = strchr(tmp, '-');
    oyk_u8 ssid = 0;
    if (dash) {
        *dash = '\0';
        const char* sp = dash + 1;
        if (!sp[0] || strlen(sp) > 2) return 0;
        int val = 0;
        while (*sp) {
            if (!isdigit(static_cast<unsigned char>(*sp))) return 0;
            val = val * 10 + (*sp - '0');
            ++sp;
        }
        if (val < 0 || val > 15) return 0;
        ssid = static_cast<oyk_u8>(val);
    }

    size_t blen = strlen(tmp);
    if (blen == 0 || blen > 6) return 0;
    for (size_t i = 0; i < blen; ++i) {
        if (!isalnum(static_cast<unsigned char>(tmp[i]))) return 0;
        out->base[i] = static_cast<char>(toupper(static_cast<unsigned char>(tmp[i])));
    }
    out->base[blen] = '\0';
    out->ssid = ssid;
    return 1;
}

int oyk_parse_callsign_with_default_ssid(const char* text, oyk_u8 default_ssid, OykCallsign* out) {
    if (!text || !out) return 0;
    OykCallsign parsed;
    if (!oyk_parse_callsign(text, &parsed)) return 0;
    if (strchr(text, '-') == NULL) {
        parsed.ssid = static_cast<oyk_u8>(default_ssid & 0x0FU);
    }
    *out = parsed;
    return 1;
}

void oyk_format_callsign(const OykCallsign* call, char* out, size_t out_sz) {
    if (!call || !out || out_sz == 0) return;
    if (call->ssid == 0) {
        safe_copy(out, out_sz, call->base);
    } else {
        char buf[16];
        sprintf(buf, "%s-%u", call->base, static_cast<unsigned int>(call->ssid));
        safe_copy(out, out_sz, buf);
    }
}

int oyk_callsign_eq(const OykCallsign* a, const OykCallsign* b) {
    return (a->ssid == b->ssid) && (strcmp(a->base, b->base) == 0);
}

int oyk_parse_ipv4(const char* text, oyk_u32* net_ip_out) {
    if (!text || !text[0] || !net_ip_out) return 0;
    oyk_u8 oct[4];
    const char* p = text;
    for (int i = 0; i < 4; ++i) {
        if (!isdigit(static_cast<unsigned char>(*p))) return 0;
        unsigned long v = 0;
        while (isdigit(static_cast<unsigned char>(*p))) {
            v = v * 10UL + static_cast<unsigned long>(*p - '0');
            if (v > 255UL) return 0;
            ++p;
        }
        oct[i] = static_cast<oyk_u8>(v);
        if (i < 3) {
            if (*p != '.') return 0;
            ++p;
        }
    }
    if (*p != '\0') return 0;
    oyk_u32 ip = 0;
    memcpy(&ip, oct, 4);
    *net_ip_out = ip;
    return 1;
}

int oyk_is_all_digits(const char* text) {
    if (!text || !text[0]) return 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
    }
    return 1;
}

int oyk_parse_dmr_id(const char* text, oyk_u32* dmr_id_out) {
    if (!oyk_is_all_digits(text)) return 0;
    unsigned long v = 0UL;
    for (const char* p = text; *p; ++p) {
        unsigned long d = static_cast<unsigned long>(*p - '0');
        if (v > (16777214UL - d) / 10UL) return 0;
        v = v * 10UL + d;
    }
    if (v == 0UL || v > 16777214UL) return 0;
    if (dmr_id_out) *dmr_id_out = static_cast<oyk_u32>(v);
    return 1;
}

void oyk_dmr_id_to_ipv4(oyk_u32 dmr_id, oyk_u8 prefix_octet, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    char buf[20];
    sprintf(buf, "%u.%u.%u.%u",
            static_cast<unsigned int>(prefix_octet),
            static_cast<unsigned int>((dmr_id >> 16) & 0xFFUL),
            static_cast<unsigned int>((dmr_id >> 8) & 0xFFUL),
            static_cast<unsigned int>(dmr_id & 0xFFUL));
    safe_copy(out, out_sz, buf);
}

int oyk_resolve_host_or_dmr_id(const char* spec, oyk_u8 prefix_octet,
                               char* out, size_t out_sz, oyk_u32* net_ip_out) {
    if (!spec || !spec[0]) return 0;
    if (oyk_is_all_digits(spec)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(spec, &did)) return 0;
        char ip_buf[20];
        oyk_dmr_id_to_ipv4(did, prefix_octet, ip_buf, sizeof(ip_buf));
        if (out && out_sz > 0) safe_copy(out, out_sz, ip_buf);
        if (net_ip_out) (void)oyk_parse_ipv4(ip_buf, net_ip_out);
        return 1;
    }
    if (out && out_sz > 0) safe_copy(out, out_sz, spec);
    if (net_ip_out) (void)oyk_parse_ipv4(spec, net_ip_out);
    return 1;
}

oyk_u8 oyk_dmr_prefix_for_iface(const OykIfaceConfig* ic) {
    if (!ic) return 12U;
    if (stricmp_ascii(ic->type, "axudp") != 0 && stricmp_ascii(ic->type, "axip") != 0) return 12U;
    if (strcmp(ic->netmask, "255.0.0.0") != 0) return 12U;
    if (!ic->host[0]) return 12U;
    oyk_u32 bind_ip = 0;
    if (!oyk_resolve_host_or_dmr_id(ic->host, 12U, NULL, 0, &bind_ip)) return 12U;
    oyk_u8 top = reinterpret_cast<const oyk_u8*>(&bind_ip)[0];
    return (top != 0) ? top : static_cast<oyk_u8>(12U);
}

int oyk_ip_matches_netmask(oyk_u32 ip_net, oyk_u32 bind_net, oyk_u32 mask_net) {
    if (mask_net == 0 || bind_net == 0) return 1;
    return ((ip_net & mask_net) == (bind_net & mask_net)) ? 1 : 0;
}

int oyk_parse_peer_spec(const char* spec, char* call_out, size_t call_sz,
                        oyk_u32* net_ip_out, oyk_u16* port_out, oyk_u16 default_port) {
    if (!spec || !net_ip_out || !port_out) return 0;
    char buf[96];
    safe_copy(buf, sizeof(buf), spec);
    trim_inplace(buf);
    if (!buf[0]) return 0;

    if (call_out && call_sz > 0) call_out[0] = '\0';

    char* host_part = buf;
    char* at = strchr(buf, '@');
    char* sp = strchr(buf, ' ');
    if (at) {
        *at = '\0';
        trim_inplace(buf);
        OykCallsign c;
        if (!oyk_parse_callsign(buf, &c)) return 0;
        if (call_out && call_sz > 0) oyk_format_callsign(&c, call_out, call_sz);
        host_part = at + 1;
    } else if (sp) {
        *sp = '\0';
        trim_inplace(buf);
        OykCallsign c;
        if (!oyk_parse_callsign(buf, &c)) return 0;
        if (call_out && call_sz > 0) oyk_format_callsign(&c, call_out, call_sz);
        host_part = sp + 1;
    }
    trim_inplace(host_part);

    oyk_u16 port = default_port ? default_port : static_cast<oyk_u16>(10093);
    char* colon = strchr(host_part, ':');
    if (colon) {
        *colon = '\0';
        long pv = atol(colon + 1);
        if (pv <= 0L || pv > 65535L) return 0;
        port = static_cast<oyk_u16>(pv);
    }
    trim_inplace(host_part);

    oyk_u32 ip = 0;
    if (!oyk_parse_ipv4(host_part, &ip)) return 0;
    *net_ip_out = ip;
    *port_out = port;
    return 1;
}

void oyk_format_peer_spec(const OykPeerEntry* peer, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!peer) return;
    oyk_u8 oct[4];
    memcpy(oct, &peer->ip, 4);
    char buf[64];
    if (peer->callsign[0] && peer->port > 0) {
        sprintf(buf, "%s@%u.%u.%u.%u:%u", peer->callsign,
                static_cast<unsigned int>(oct[0]), static_cast<unsigned int>(oct[1]),
                static_cast<unsigned int>(oct[2]), static_cast<unsigned int>(oct[3]),
                static_cast<unsigned int>(peer->port));
    } else if (peer->callsign[0]) {
        sprintf(buf, "%s@%u.%u.%u.%u", peer->callsign,
                static_cast<unsigned int>(oct[0]), static_cast<unsigned int>(oct[1]),
                static_cast<unsigned int>(oct[2]), static_cast<unsigned int>(oct[3]));
    } else if (peer->port > 0) {
        sprintf(buf, "%u.%u.%u.%u:%u",
                static_cast<unsigned int>(oct[0]), static_cast<unsigned int>(oct[1]),
                static_cast<unsigned int>(oct[2]), static_cast<unsigned int>(oct[3]),
                static_cast<unsigned int>(peer->port));
    } else {
        sprintf(buf, "%u.%u.%u.%u",
                static_cast<unsigned int>(oct[0]), static_cast<unsigned int>(oct[1]),
                static_cast<unsigned int>(oct[2]), static_cast<unsigned int>(oct[3]));
    }
    safe_copy(out, out_sz, buf);
}

oyk_u16 oyk_crc16_fcs(const oyk_u8* data, size_t len) {
    oyk_u16 crc = 0xFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<oyk_u16>(crc ^ data[i]);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x0001U) {
                crc = static_cast<oyk_u16>((crc >> 1) ^ 0x8408U);
            } else {
                crc = static_cast<oyk_u16>(crc >> 1);
            }
        }
    }
    return static_cast<oyk_u16>(crc ^ 0xFFFFU);
}

int oyk_verify_fcs(const oyk_u8* data, size_t total_len) {
    if (!data || total_len < 3) return 0;
    size_t plen = total_len - 2;
    oyk_u16 calc = oyk_crc16_fcs(data, plen);
    oyk_u16 recv = static_cast<oyk_u16>(
        static_cast<oyk_u16>(data[plen]) | (static_cast<oyk_u16>(data[plen + 1]) << 8));
    return calc == recv;
}

OykFrameType oyk_frame_type(const OykFrame* f) {
    oyk_u8 c = f->control;
    if ((c & 0x01U) == 0) return OYK_FT_I;
    if ((c & 0x03U) == 0x01U) {
        switch (c & 0x0FU) {
            case 0x01U: return OYK_FT_RR;
            case 0x05U: return OYK_FT_RNR;
            case 0x09U: return OYK_FT_REJ;
            default:    return OYK_FT_UNKNOWN;
        }
    }
    switch (c & 0xEFU) {
        case 0x2FU: return OYK_FT_SABM;
        case 0x43U: return OYK_FT_DISC;
        case 0x0FU: return OYK_FT_DM;
        case 0x63U: return OYK_FT_UA;
        case 0x03U: return OYK_FT_UI;
        default:    return OYK_FT_UNKNOWN;
    }
}

static int decode_ax25_addr(const oyk_u8 b[7], OykCallsign* call, int* c_bit, int* is_last) {
    char tmp[7];
    int len = 0;
    for (int i = 0; i < 6; ++i) {
        char ch = static_cast<char>((b[i] >> 1) & 0x7FU);
        tmp[i] = ch;
    }
    len = 6;
    while (len > 0 && tmp[len - 1] == ' ') --len;
    tmp[len] = '\0';
    if (len == 0) return 0;
    for (int i = 0; i < len; ++i) {
        if (!isalnum(static_cast<unsigned char>(tmp[i]))) return 0;
        call->base[i] = static_cast<char>(toupper(static_cast<unsigned char>(tmp[i])));
    }
    call->base[len] = '\0';
    call->ssid = static_cast<oyk_u8>((b[6] >> 1) & 0x0FU);
    if (c_bit) *c_bit = (b[6] & 0x80U) ? 1 : 0;
    if (is_last) *is_last = (b[6] & 0x01U) ? 1 : 0;
    return 1;
}

static void encode_ax25_addr(const OykCallsign* call, int c_bit, int is_last, oyk_u8 out[7]) {
    size_t len = strlen(call->base);
    for (size_t i = 0; i < 6; ++i) {
        char ch = (i < len) ? call->base[i] : ' ';
        out[i] = static_cast<oyk_u8>(static_cast<oyk_u8>(ch) << 1);
    }
    oyk_u8 sb = 0x60U;
    if (c_bit) sb = static_cast<oyk_u8>(sb | 0x80U);
    sb = static_cast<oyk_u8>(sb | ((call->ssid & 0x0FU) << 1));
    if (is_last) sb = static_cast<oyk_u8>(sb | 0x01U);
    out[6] = sb;
}

int oyk_ax25_decode(const oyk_u8* raw, size_t len, OykFrame* out) {
    if (!raw || !out || len < 15) return 0;
    memset(out, 0, sizeof(*out));
    int dst_c = 0, dst_last = 0, src_c = 0, last = 0;
    if (!decode_ax25_addr(raw, &out->dst, &dst_c, &dst_last) || dst_last) return 0;
    if (!decode_ax25_addr(raw + 7, &out->src, &src_c, &last)) return 0;
    out->is_command = (dst_c && !src_c) ? 1 : 0;
    out->is_response = (!dst_c && src_c) ? 1 : 0;

    size_t off = 14;
    int digis = 0;
    while (!last) {
        if (digis >= 8 || off + 7 > len) return 0;
        last = (raw[off + 6] & 0x01U) ? 1 : 0;
        off += 7;
        ++digis;
    }
    if (off >= len) return 0;
    out->control = raw[off++];

    OykFrameType ft = oyk_frame_type(out);
    if ((ft == OYK_FT_I || ft == OYK_FT_UI) && off < len) {
        out->has_pid = 1;
        out->pid = raw[off++];
    }
    if (off < len) {
        size_t ilen = len - off;
        if (ilen > OYK_MAX_FRAME_INFO) ilen = OYK_MAX_FRAME_INFO;
        memcpy(out->info, raw + off, ilen);
        out->info_len = static_cast<oyk_u16>(ilen);
    }
    return 1;
}

size_t oyk_ax25_encode(const OykFrame* f, oyk_u8* out, size_t cap) {
    if (!f || !out || cap < 16) return 0;
    size_t need = 15 + (f->has_pid ? 1 : 0) + f->info_len;
    if (need > cap) return 0;

    encode_ax25_addr(&f->dst, f->is_command && !f->is_response, 0, out);
    encode_ax25_addr(&f->src, f->is_response && !f->is_command, 1, out + 7);
    size_t off = 14;
    out[off++] = f->control;
    if (f->has_pid) {
        out[off++] = f->pid;
    }
    if (f->info_len > 0) {
        memcpy(out + off, f->info, f->info_len);
        off += f->info_len;
    }
    return off;
}

size_t oyk_kiss_encode(oyk_u8 kiss_port, const oyk_u8* payload, size_t len,
                       oyk_u8* out, size_t cap) {
    if (!out || cap < 4) return 0;
    size_t pos = 0;
    out[pos++] = 0xC0U;
    oyk_u8 cmd = static_cast<oyk_u8>((kiss_port & 0x0FU) << 4);
    out[pos++] = cmd;
    for (size_t i = 0; i < len; ++i) {
        if (pos + 3 >= cap) return 0;
        if (payload[i] == 0xC0U) {
            out[pos++] = 0xDBU;
            out[pos++] = 0xDCU;
        } else if (payload[i] == 0xDBU) {
            out[pos++] = 0xDBU;
            out[pos++] = 0xDDU;
        } else {
            out[pos++] = payload[i];
        }
    }
    if (pos >= cap) return 0;
    out[pos++] = 0xC0U;
    return pos;
}

void oyk_kiss_decoder_init(OykKissDecoder* dec) {
    memset(dec, 0, sizeof(*dec));
}

int oyk_kiss_decoder_feed_byte(OykKissDecoder* dec, oyk_u8 b,
                               oyk_u8* out_port, oyk_u8* out_cmd) {
    if (b == 0xC0U) {
        int ready = 0;
        if (dec->in_frame && !dec->overflowed && dec->len >= 1) {
            oyk_u8 c = dec->buf[0];
            if (out_port) *out_port = static_cast<oyk_u8>((c >> 4) & 0x0FU);
            if (out_cmd) *out_cmd = static_cast<oyk_u8>(c & 0x0FU);
            memmove(dec->buf, dec->buf + 1, dec->len - 1);
            dec->len = static_cast<oyk_u16>(dec->len - 1);
            ready = 1;
        } else {
            dec->len = 0;
        }
        dec->in_frame = 1;
        dec->escaped = 0;
        dec->overflowed = 0;
        return ready;
    }
    if (!dec->in_frame || dec->overflowed) return 0;
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
    } else {
        dec->overflowed = 1;
        dec->len = 0;
    }
    return 0;
}

int oyk_parse_flags_u32(const char* text, oyk_u32* out) {
    if (!text || !out) return 0;
    char s[32];
    safe_copy(s, sizeof(s), text);
    trim_inplace(s);
    if (!s[0]) return 0;

    if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B') && s[2] != '\0') {
        oyk_u32 v = 0;
        for (size_t i = 2; s[i]; ++i) {
            if (s[i] != '0' && s[i] != '1') return 0;
            v = (v << 1) | static_cast<oyk_u32>(s[i] - '0');
        }
        *out = v;
        return 1;
    }
    char* endp = NULL;
    unsigned long val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && s[2] != '\0') {
        val = strtoul(s + 2, &endp, 16);
    } else {
        val = strtoul(s, &endp, 10);
    }
    if (!endp || *endp != '\0') return 0;
    *out = static_cast<oyk_u32>(val);
    return 1;
}

int oyk_parse_log_mask(const char* text, oyk_u32* out) {
    if (!text || !out) return 0;
    oyk_u32 num = 0;
    if (oyk_parse_flags_u32(text, &num)) {
        *out = num;
        return 1;
    }
    char buf[128];
    safe_copy(buf, sizeof(buf), text);
    oyk_u32 mask = 0;
    int any = 0;
    char* p = buf;
    while (*p) {
        char* sep = p;
        while (*sep && *sep != '|' && *sep != ',' && *sep != '+') ++sep;
        char saved = *sep;
        *sep = '\0';
        trim_inplace(p);
        if (*p) {
            oyk_u32 v = 0;
            if (oyk_parse_flags_u32(p, &v)) {
                mask |= v;
                any = 1;
            } else if (stricmp_ascii(p, "SILENT") == 0) {
                any = 1;
            } else if (stricmp_ascii(p, "ERROR") == 0) {
                mask |= OYK_LOG_ERROR; any = 1;
            } else if (stricmp_ascii(p, "WARNING") == 0 || stricmp_ascii(p, "WARN") == 0) {
                mask |= OYK_LOG_WARNING; any = 1;
            } else if (stricmp_ascii(p, "INFO") == 0) {
                mask |= OYK_LOG_INFO; any = 1;
            } else if (stricmp_ascii(p, "KICK") == 0) {
                mask |= OYK_LOG_KICK; any = 1;
            } else if (stricmp_ascii(p, "BAN") == 0) {
                mask |= OYK_LOG_BAN; any = 1;
            } else if (stricmp_ascii(p, "APPEAL") == 0) {
                mask |= OYK_LOG_APPEAL; any = 1;
            } else if (stricmp_ascii(p, "DEBUG") == 0) {
                mask |= OYK_LOG_DEBUG; any = 1;
            } else if (stricmp_ascii(p, "ALL") == 0) {
                mask |= 0xF7U; any = 1;
            } else {
                return 0;
            }
        }
        if (saved == '\0') break;
        p = sep + 1;
    }
    if (!any) return 0;
    *out = mask;
    return 1;
}

void oyk_sanitize_appeal(const char* raw, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!raw) return;

    char tmp[OYK_MAX_APPEAL_LEN + 8];
    size_t pos = 0;
    for (size_t i = 0; raw[i] != '\0' && pos < sizeof(tmp) - 1; ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c >= 0x20U && c <= 0x7EU) {
            tmp[pos++] = static_cast<char>(c);
        }
    }
    tmp[pos] = '\0';
    trim_inplace(tmp);

    size_t limit = OYK_MAX_APPEAL_LEN;
    if (out_sz - 1 < limit) limit = out_sz - 1;
    size_t len = strlen(tmp);
    if (len > limit) len = limit;
    memcpy(out, tmp, len);
    out[len] = '\0';
}

void oyk_format_now_compact(time_t t, char* out, size_t out_sz) {
    struct tm* lt = localtime(&t);
    if (!lt || out_sz < 16) {
        if (out_sz > 0) out[0] = '\0';
        return;
    }
    sprintf(out, "%04d%02d%02d-%02d%02d%02d",
            lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
            lt->tm_hour, lt->tm_min, lt->tm_sec);
}

void oyk_format_now_yyyymmdd(time_t t, char* out, size_t out_sz) {
    struct tm* lt = localtime(&t);
    if (!lt || out_sz < 9) {
        if (out_sz > 0) out[0] = '\0';
        return;
    }
    sprintf(out, "%04d%02d%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

void oyk_format_now_hms(time_t t, char* out, size_t out_sz) {
    struct tm* lt = localtime(&t);
    if (!lt || out_sz < 9) {
        if (out_sz > 0) out[0] = '\0';
        return;
    }
    sprintf(out, "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
}

int oyk_parse_lift_spec(const char* text, time_t now, char* out, size_t out_sz) {
    if (!out || out_sz < 16) return 0;
    char s[64];
    safe_copy(s, sizeof(s), text ? text : "");
    trim_inplace(s);
    if (!s[0] || stricmp_ascii(s, "permanent") == 0 ||
        stricmp_ascii(s, "perm") == 0 || stricmp_ascii(s, "never") == 0) {
        safe_copy(out, out_sz, "permanent");
        return 1;
    }

    /* 1. Canonical YYYYMMDD-HHMMSS */
    if (valid_timestamp_15(s)) {
        safe_copy(out, out_sz, s);
        return 1;
    }

    /* 2. Compact 8-digit YYYYMMDD */
    size_t slen = strlen(s);
    if (slen == 8) {
        int all_d = 1;
        for (size_t i = 0; i < 8; ++i) {
            if (!isdigit(static_cast<unsigned char>(s[i]))) { all_d = 0; break; }
        }
        if (all_d) {
            sprintf(out, "%s-235959", s);
            return 1;
        }
    }

    /* 3. ISO date YYYY-MM-DD [HH:MM[:SS]] */
    if (slen >= 10 &&
        isdigit(static_cast<unsigned char>(s[0])) && isdigit(static_cast<unsigned char>(s[1])) &&
        isdigit(static_cast<unsigned char>(s[2])) && isdigit(static_cast<unsigned char>(s[3])) &&
        (s[4] == '-' || s[4] == '/') &&
        isdigit(static_cast<unsigned char>(s[5])) && isdigit(static_cast<unsigned char>(s[6])) &&
        (s[7] == '-' || s[7] == '/') &&
        isdigit(static_cast<unsigned char>(s[8])) && isdigit(static_cast<unsigned char>(s[9]))) {
        int mo = (s[5] - '0') * 10 + (s[6] - '0');
        int dy = (s[8] - '0') * 10 + (s[9] - '0');
        if (mo < 1 || mo > 12 || dy < 1 || dy > 31) return 0;

        char ymd[9];
        ymd[0] = s[0]; ymd[1] = s[1]; ymd[2] = s[2]; ymd[3] = s[3];
        ymd[4] = s[5]; ymd[5] = s[6]; ymd[6] = s[8]; ymd[7] = s[9];
        ymd[8] = '\0';

        char rest[24];
        safe_copy(rest, sizeof(rest), s + 10);
        trim_inplace(rest);
        if (rest[0] == 'T' || rest[0] == 't') {
            memmove(rest, rest + 1, strlen(rest));
            trim_inplace(rest);
        }
        char hms[7];
        strcpy(hms, "235959");
        if (rest[0]) {
            size_t rlen = strlen(rest);
            if (rlen == 5 && rest[2] == ':' &&
                isdigit(static_cast<unsigned char>(rest[0])) && isdigit(static_cast<unsigned char>(rest[1])) &&
                isdigit(static_cast<unsigned char>(rest[3])) && isdigit(static_cast<unsigned char>(rest[4]))) {
                hms[0] = rest[0]; hms[1] = rest[1]; hms[2] = rest[3]; hms[3] = rest[4];
                hms[4] = '0'; hms[5] = '0'; hms[6] = '\0';
            } else if (rlen == 8 && rest[2] == ':' && rest[5] == ':' &&
                       isdigit(static_cast<unsigned char>(rest[0])) && isdigit(static_cast<unsigned char>(rest[1])) &&
                       isdigit(static_cast<unsigned char>(rest[3])) && isdigit(static_cast<unsigned char>(rest[4])) &&
                       isdigit(static_cast<unsigned char>(rest[6])) && isdigit(static_cast<unsigned char>(rest[7]))) {
                hms[0] = rest[0]; hms[1] = rest[1]; hms[2] = rest[3]; hms[3] = rest[4];
                hms[4] = rest[6]; hms[5] = rest[7]; hms[6] = '\0';
            } else {
                return 0;
            }
        }
        sprintf(out, "%s-%s", ymd, hms);
        return valid_timestamp_15(out);
    }

    /* 4. Relative duration: 30m, 12h, 7d, 2w, 1d12h, 45 minutes, etc. */
    long total_secs = 0L;
    int parts = 0;
    const char* p = s;
    while (*p) {
        while (*p && (isspace(static_cast<unsigned char>(*p)) || *p == ',' || *p == '+')) ++p;
        if (!*p) break;
        if (!isdigit(static_cast<unsigned char>(*p))) return 0;
        long num = 0L;
        while (*p && isdigit(static_cast<unsigned char>(*p))) {
            num = num * 10L + static_cast<long>(*p - '0');
            if (num > 1000000L) return 0;
            ++p;
        }
        while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
        char unit[16];
        size_t ulen = 0;
        while (*p && isalpha(static_cast<unsigned char>(*p)) && ulen < sizeof(unit) - 1) {
            unit[ulen++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
            ++p;
        }
        unit[ulen] = '\0';
        long mult = 0L;
        if (strcmp(unit, "m") == 0 || strcmp(unit, "min") == 0 || strcmp(unit, "mins") == 0 ||
            strcmp(unit, "minute") == 0 || strcmp(unit, "minutes") == 0) {
            mult = 60L;
        } else if (strcmp(unit, "h") == 0 || strcmp(unit, "hr") == 0 || strcmp(unit, "hrs") == 0 ||
                   strcmp(unit, "hour") == 0 || strcmp(unit, "hours") == 0) {
            mult = 3600L;
        } else if (strcmp(unit, "d") == 0 || strcmp(unit, "day") == 0 || strcmp(unit, "days") == 0) {
            mult = 86400L;
        } else if (strcmp(unit, "w") == 0 || strcmp(unit, "wk") == 0 || strcmp(unit, "wks") == 0 ||
                   strcmp(unit, "week") == 0 || strcmp(unit, "weeks") == 0) {
            mult = 7L * 86400L;
        } else {
            return 0;
        }
        total_secs += num * mult;
        ++parts;
    }

    if (parts == 0 || total_secs <= 0L) return 0;
    time_t target = now + static_cast<time_t>(total_secs);
    oyk_format_now_compact(target, out, out_sz);
    return valid_timestamp_15(out);
}

void oyk_config_init_default(OykConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    oyk_parse_callsign("N0CALL-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    safe_copy(cfg->welcome_text, sizeof(cfg->welcome_text), "Type HELP or ? for commands.");
    safe_copy(cfg->info_text, sizeof(cfg->info_text), "OYK Portable Amateur Radio Packet Node");
    safe_copy(cfg->log_path, sizeof(cfg->log_path), "logs");
    cfg->console_level = OYK_DEFAULT_CONSOLE_MASK;
    cfg->rotate_daily = 1;
    cfg->accept_incoming = 1;
    cfg->allow_flash_logging = 0;
    cfg->learned_peer_policy = OYK_LEARN_INBOUND;
    cfg->new_user_welcome[0] = '\0';
    cfg->line_ending = oyk_platform_default_eol();
    cfg->has_detected_eol = 0;

    cfg->bbs_enabled = 1;
    cfg->bbs_files_enabled = 1;
    cfg->bbs_files_case_insensitive = 1;
    cfg->bbs_ssid = 11;
    memset(&cfg->bbs_callsign, 0, sizeof(cfg->bbs_callsign));
    oyk_parse_callsign("OYKBBS", &cfg->bbs_alias);
    safe_copy(cfg->bbs_mail_path, sizeof(cfg->bbs_mail_path), "mail");
    safe_copy(cfg->bbs_files_path, sizeof(cfg->bbs_files_path), "files");
    safe_copy(cfg->bbs_welcome, sizeof(cfg->bbs_welcome),
              "Welcome to OYK Packet BBS! Type H for help, B to exit.");
    cfg->bbs_max_msg_bytes = static_cast<oyk_u16>(OYK_MAX_BBS_BODY - 1);

    cfg->chat_enabled = 1;
    cfg->chat_ssid = 12;
    memset(&cfg->chat_callsign, 0, sizeof(cfg->chat_callsign));
    oyk_parse_callsign("OYKCHT", &cfg->chat_alias);
    safe_copy(cfg->chat_topic, sizeof(cfg->chat_topic), "Lobby");
    safe_copy(cfg->chat_welcome, sizeof(cfg->chat_welcome),
              "Welcome to OYK Packet Chat! Type /h for help, /q to leave.");

    cfg->rig_enabled = 0;
    safe_copy(cfg->rig_protocol, sizeof(cfg->rig_protocol), "rigctl");
    safe_copy(cfg->rig_host, sizeof(cfg->rig_host), "127.0.0.1");
    cfg->rig_port = 4532;
    cfg->rig_poll_sec = 30;
    cfg->rig_notify = 1;

    cfg->printer_enabled = 0;
    cfg->printer_device[0] = '\0';
    safe_copy(cfg->printer_protocol, sizeof(cfg->printer_protocol), "generic");
    cfg->printer_baud = 9600;
    cfg->printer_level = OYK_DEFAULT_CONSOLE_MASK;
    cfg->printer_condensed = 0;

    cfg->iface_count = 1;
    safe_copy(cfg->ifaces[0].id, sizeof(cfg->ifaces[0].id), "axudp0");
    safe_copy(cfg->ifaces[0].type, sizeof(cfg->ifaces[0].type), "axudp");
    safe_copy(cfg->ifaces[0].description, sizeof(cfg->ifaces[0].description), "Primary AXUDP Link");
    safe_copy(cfg->ifaces[0].host, sizeof(cfg->ifaces[0].host), "0.0.0.0");
    cfg->ifaces[0].port = 10093;
    cfg->ifaces[0].block_inbound = 0;
    cfg->ifaces[0].block_outbound = 0;
    cfg->ifaces[0].allow_same_interface = 0;
}

static void resolve_cfg_subpath(const char* base_cfg_path, const char* raw, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!raw || !raw[0]) return;
    int is_abs = 0;
    if (raw[0] == '/' || raw[0] == '\\') is_abs = 1;
    else if (isalpha(static_cast<unsigned char>(raw[0])) && raw[1] == ':') is_abs = 1;
    if (is_abs || !base_cfg_path || !base_cfg_path[0]) {
        safe_copy(out, out_sz, raw);
        return;
    }
    char dir[128];
    safe_copy(dir, sizeof(dir), base_cfg_path);
    char* slash1 = strrchr(dir, '/');
    char* slash2 = strrchr(dir, '\\');
    char* last = (slash1 > slash2) ? slash1 : slash2;
    if (last) {
        *(last + 1) = '\0';
        join_path(dir, raw, out, out_sz);
        return;
    }
    safe_copy(out, out_sz, raw);
}

static void normalize_cmp_path(const char* in, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!in) return;
    size_t i = 0;
    if (in[0] == '.' && (in[1] == '/' || in[1] == '\\')) in += 2;
    while (*in && i + 1 < out_sz) {
        char c = (*in == '\\') ? '/' : *in;
        if (c == '/' && i > 0 && out[i - 1] == '/') {
            ++in;
            continue;
        }
        out[i++] = c;
        ++in;
    }
    out[i] = '\0';
}

static int portable_same_path(const char* a, const char* b) {
    if (!a || !b || !a[0] || !b[0]) return 0;
    char na[128], nb[128];
    normalize_cmp_path(a, na, sizeof(na));
    normalize_cmp_path(b, nb, sizeof(nb));
#if defined(__DOS__) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
    return (stricmp_ascii(na, nb) == 0) ? 1 : 0;
#else
    return (strcmp(na, nb) == 0) ? 1 : 0;
#endif
}

void oyk_config_resolve_banlist(const OykConfig* cfg, char* out, size_t out_sz) {
    if (cfg->banlist_path[0] != '\0') {
        resolve_cfg_subpath(cfg->source_path, cfg->banlist_path, out, out_sz);
        return;
    }
    if (cfg->source_path[0] != '\0') {
        resolve_cfg_subpath(cfg->source_path, "banlist.cfg", out, out_sz);
        return;
    }
    safe_copy(out, out_sz, "banlist.cfg");
}

void oyk_config_resolve_interfaces(const OykConfig* cfg, char* out, size_t out_sz) {
    if (!cfg || !out || out_sz == 0) return;
    resolve_cfg_subpath(cfg->source_path, cfg->interfaces_file, out, out_sz);
}

void oyk_config_resolve_peers(const OykConfig* cfg, char* out, size_t out_sz) {
    if (!cfg || !out || out_sz == 0) return;
    resolve_cfg_subpath(cfg->source_path, cfg->peers_file, out, out_sz);
}

void oyk_config_resolved_bbs_callsign(const OykConfig* cfg, OykCallsign* out) {
    if (!cfg || !out) return;
    if (cfg->bbs_callsign.base[0] != '\0') {
        *out = cfg->bbs_callsign;
        return;
    }
    *out = cfg->callsign;
    out->ssid = static_cast<oyk_u8>(cfg->bbs_ssid & 0x0FU);
}

void oyk_config_resolved_chat_callsign(const OykConfig* cfg, OykCallsign* out) {
    if (!cfg || !out) return;
    if (cfg->chat_callsign.base[0] != '\0') {
        *out = cfg->chat_callsign;
        return;
    }
    *out = cfg->callsign;
    out->ssid = static_cast<oyk_u8>(cfg->chat_ssid & 0x0FU);
}

void oyk_derive_default_iface_id(const OykConfig* cfg, const char* iface_type,
                                 char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    const char* prefix = "iface";
    if (iface_type && iface_type[0]) {
        if (stricmp_ascii(iface_type, "axudp") == 0) prefix = "axudp";
        else if (stricmp_ascii(iface_type, "axip") == 0) prefix = "axip";
        else if (stricmp_ascii(iface_type, "kiss-serial") == 0) prefix = "kiss";
        else if (stricmp_ascii(iface_type, "kiss-tcp") == 0) prefix = "kisstcp";
        else if (stricmp_ascii(iface_type, "telnet") == 0) prefix = "telnet";
        else if (stricmp_ascii(iface_type, "ppp") == 0) prefix = "ppp";
        else if (stricmp_ascii(iface_type, "usb-cdc-net") == 0) prefix = "usbnet";
        else if (stricmp_ascii(iface_type, "usb-cdc-serial") == 0) prefix = "usbser";
    }
    for (int n = 0; n < 100; ++n) {
        char cand[24];
        sprintf(cand, "%s%d", prefix, n);
        int used = 0;
        if (cfg) {
            for (int i = 0; i < cfg->iface_count && i < OYK_MAX_INTERFACES; ++i) {
                if (stricmp_ascii(cfg->ifaces[i].id, cand) == 0) {
                    used = 1;
                    break;
                }
            }
        }
        if (!used) {
            safe_copy(out, out_sz, cand);
            return;
        }
    }
    safe_copy(out, out_sz, prefix);
}

void oyk_derive_default_peer_id(const OykConfig* cfg, const char* callsign,
                                char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    char base[16];
    size_t bl = 0;
    if (callsign) {
        for (size_t i = 0; callsign[i] && bl < 12; ++i) {
            unsigned char ch = static_cast<unsigned char>(callsign[i]);
            if (isalnum(ch) || ch == '-' || ch == '_') {
                base[bl++] = static_cast<char>(tolower(ch));
            }
        }
    }
    if (bl == 0) {
        strcpy(base, "peer");
    } else {
        base[bl] = '\0';
    }
    for (int n = 0; n < 1000; ++n) {
        char cand[20];
        if (n == 0) {
            safe_copy(cand, sizeof(cand), base);
        } else {
            char trunc[12];
            safe_copy(trunc, sizeof(trunc), base);
            sprintf(cand, "%.10s-%d", trunc, n);
        }
        int used = 0;
        if (cfg) {
            for (int i = 0; i < cfg->peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
                if (stricmp_ascii(cfg->peers[i].id, cand) == 0) {
                    used = 1;
                    break;
                }
            }
        }
        if (!used) {
            safe_copy(out, out_sz, cand);
            return;
        }
    }
    safe_copy(out, out_sz, base);
}

oyk_u8 oyk_parse_learned_policy(const char* text, oyk_u8 fallback) {
    if (!text || !text[0]) return OYK_LEARN_INHERIT;
    char buf[24];
    safe_copy(buf, sizeof(buf), text);
    trim_inplace(buf);
    if (!buf[0] || stricmp_ascii(buf, "inherit") == 0 || stricmp_ascii(buf, "default") == 0) {
        return OYK_LEARN_INHERIT;
    }
    if (stricmp_ascii(buf, "none") == 0 || stricmp_ascii(buf, "deny") == 0 || stricmp_ascii(buf, "off") == 0) {
        return OYK_LEARN_NONE;
    }
    if (stricmp_ascii(buf, "inbound") == 0 || stricmp_ascii(buf, "in") == 0) {
        return OYK_LEARN_INBOUND;
    }
    if (stricmp_ascii(buf, "outbound") == 0 || stricmp_ascii(buf, "out") == 0) {
        return OYK_LEARN_OUTBOUND;
    }
    if (stricmp_ascii(buf, "both") == 0 || stricmp_ascii(buf, "all") == 0 || stricmp_ascii(buf, "in+out") == 0) {
        return OYK_LEARN_BOTH;
    }
    return fallback;
}

const char* oyk_format_learned_policy(oyk_u8 policy) {
    switch (policy) {
        case OYK_LEARN_NONE:     return "none";
        case OYK_LEARN_INBOUND:  return "inbound";
        case OYK_LEARN_OUTBOUND: return "outbound";
        case OYK_LEARN_BOTH:     return "both";
        default:                 return "inherit";
    }
}

int oyk_first_peer_for_iface(const OykConfig* cfg, const char* iface_id,
                             oyk_u32* ip_out, oyk_u16* port_out) {
    if (!cfg || !iface_id) return 0;
    for (int i = 0; i < cfg->peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        const OykPeerConfig* p = &cfg->peers[i];
        if (p->interface_id[0] && stricmp_ascii(p->interface_id, iface_id) != 0) continue;
        oyk_u32 ip = p->resolved_ip;
        if (ip == 0 && p->host[0]) {
            (void)oyk_parse_ipv4(p->host, &ip);
        }
        if (ip != 0) {
            if (ip_out) *ip_out = ip;
            if (port_out) *port_out = p->port ? p->port : static_cast<oyk_u16>(10093);
            return 1;
        }
    }
    for (int i = 0; i < cfg->iface_count && i < OYK_MAX_INTERFACES; ++i) {
        if (stricmp_ascii(cfg->ifaces[i].id, iface_id) == 0 && cfg->ifaces[i].peer_count > 0) {
            if (ip_out) *ip_out = cfg->ifaces[i].peers[0].ip;
            if (port_out) *port_out = cfg->ifaces[i].peers[0].port;
            return (cfg->ifaces[i].peers[0].ip != 0) ? 1 : 0;
        }
    }
    return 0;
}

static void format_ipv4_dotted(oyk_u32 net_ip, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (net_ip == 0) {
        out[0] = '\0';
        return;
    }
    const oyk_u8* b = reinterpret_cast<const oyk_u8*>(&net_ip);
    char buf[20];
    sprintf(buf, "%u.%u.%u.%u",
            static_cast<unsigned int>(b[0]), static_cast<unsigned int>(b[1]),
            static_cast<unsigned int>(b[2]), static_cast<unsigned int>(b[3]));
    safe_copy(out, out_sz, buf);
}

static void append_migrated_iface_peers(OykConfig* cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->iface_count && i < OYK_MAX_INTERFACES; ++i) {
        OykIfaceConfig* ic = &cfg->ifaces[i];
        for (int p = 0; p < ic->peer_count && p < OYK_MAX_PEERS; ++p) {
            if (cfg->peer_count >= OYK_MAX_CONFIG_PEERS) break;
            const OykPeerEntry* pe = &ic->peers[p];
            char ip_s[20];
            format_ipv4_dotted(pe->ip, ip_s, sizeof(ip_s));
            int dup = 0;
            for (int k = 0; k < cfg->peer_count; ++k) {
                if (stricmp_ascii(cfg->peers[k].interface_id, ic->id) == 0 &&
                    stricmp_ascii(cfg->peers[k].callsign, pe->callsign) == 0 &&
                    cfg->peers[k].resolved_ip == pe->ip &&
                    cfg->peers[k].port == pe->port) {
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;
            OykPeerConfig* dst = &cfg->peers[cfg->peer_count++];
            memset(dst, 0, sizeof(*dst));
            oyk_derive_default_peer_id(cfg, pe->callsign[0] ? pe->callsign : ic->id,
                                       dst->id, sizeof(dst->id));
            safe_copy(dst->callsign, sizeof(dst->callsign), pe->callsign);
            safe_copy(dst->interface_id, sizeof(dst->interface_id), ic->id);
            safe_copy(dst->host, sizeof(dst->host), ip_s);
            dst->resolved_ip = pe->ip;
            dst->port = pe->port;
            dst->allow_inbound = 1;
            dst->allow_outbound = 1;
            dst->auto_learned = 0;
        }
        ic->peer_count = 0;
    }
}

struct PortableParseState {
    int node_call_explicit_ssid;
    int node_ssid_set;
    oyk_u8 node_ssid_val;
    int chat_call_explicit_ssid;
    int bbs_call_explicit_ssid;
};

static void parse_config_bytes(const char* raw, size_t raw_len,
                               OykConfig* cfg, PortableParseState* st,
                               int allow_node, int allow_iface, int allow_peer) {
    int section = 0; /* 0=node, 1=logfiles, 2=interface, 3=chat, 4=bbs, 5=rig, 6=printer, 7=peer, -1=skip */
    OykIfaceConfig* cur_if = NULL;
    OykPeerConfig* cur_peer = NULL;

    size_t pos = 0;
    char line[256];
    while (next_line_any_eol(raw, raw_len, &pos, line, sizeof(line))) {
        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char* semi = strchr(line, ';');
        if (semi) *semi = '\0';
        trim_inplace(line);
        if (!line[0]) continue;

        size_t llen = strlen(line);
        if (line[0] == '[' && line[llen - 1] == ']') {
            line[llen - 1] = '\0';
            char* sec = line + 1;
            trim_inplace(sec);
            if (stricmp_ascii(sec, "node") == 0) {
                section = allow_node ? 0 : -1;
                cur_if = NULL;
                cur_peer = NULL;
            } else if (stricmp_ascii(sec, "logfiles") == 0) {
                section = allow_node ? 1 : -1;
                cur_if = NULL;
                cur_peer = NULL;
            } else if (stricmp_ascii(sec, "bbs") == 0) {
                section = allow_node ? 4 : -1;
                cur_if = NULL;
                cur_peer = NULL;
            } else if (stricmp_ascii(sec, "chat") == 0) {
                section = allow_node ? 3 : -1;
                cur_if = NULL;
                cur_peer = NULL;
            } else if (stricmp_ascii(sec, "rig") == 0 || stricmp_ascii(sec, "radio") == 0) {
                section = allow_node ? 5 : -1;
                cur_if = NULL;
                cur_peer = NULL;
            } else if (stricmp_ascii(sec, "printer") == 0) {
                section = allow_node ? 6 : -1;
                cur_if = NULL;
                cur_peer = NULL;
            } else if (strlen(sec) > 9 &&
                       (sec[0] == 'i' || sec[0] == 'I') &&
                       (sec[8] == 'e' || sec[8] == 'E')) {
                char* idp = sec + 9;
                trim_inplace(idp);
                if (*idp == ':') { ++idp; trim_inplace(idp); }
                if (!allow_iface) {
                    section = -1; cur_if = NULL; cur_peer = NULL;
                    continue;
                }
                cur_if = NULL;
                for (int ei = 0; ei < cfg->iface_count; ++ei) {
                    if (stricmp_ascii(cfg->ifaces[ei].id, idp) == 0) {
                        cur_if = &cfg->ifaces[ei];
                        break;
                    }
                }
                if (!cur_if && cfg->iface_count < OYK_MAX_INTERFACES) {
                    cur_if = &cfg->ifaces[cfg->iface_count++];
                }
                if (cur_if) {
                    memset(cur_if, 0, sizeof(*cur_if));
                    safe_copy(cur_if->id, sizeof(cur_if->id), idp);
                    safe_copy(cur_if->host, sizeof(cur_if->host), "0.0.0.0");
                    cur_if->port = 0;
                    cur_if->baud = 9600;
                    cur_if->block_inbound = 0;
                    cur_if->block_outbound = 0;
                    cur_if->allow_same_interface = 0;
                    section = 2;
                    cur_peer = NULL;
                } else {
                    section = -1;
                }
            } else if (strlen(sec) > 4 &&
                       (sec[0] == 'p' || sec[0] == 'P') &&
                       (sec[1] == 'e' || sec[1] == 'E') &&
                       (sec[2] == 'e' || sec[2] == 'E') &&
                       (sec[3] == 'r' || sec[3] == 'R') &&
                       (sec[4] == ' ' || sec[4] == '\t' || sec[4] == ':')) {
                char* idp = sec + 4;
                trim_inplace(idp);
                if (*idp == ':') { ++idp; trim_inplace(idp); }
                if (!allow_peer || !*idp) {
                    section = -1; cur_if = NULL; cur_peer = NULL;
                    continue;
                }
                cur_peer = NULL;
                for (int ep = 0; ep < cfg->peer_count && ep < OYK_MAX_CONFIG_PEERS; ++ep) {
                    if (stricmp_ascii(cfg->peers[ep].id, idp) == 0) {
                        cur_peer = &cfg->peers[ep];
                        break;
                    }
                }
                if (!cur_peer && cfg->peer_count < OYK_MAX_CONFIG_PEERS) {
                    cur_peer = &cfg->peers[cfg->peer_count++];
                }
                if (cur_peer) {
                    memset(cur_peer, 0, sizeof(*cur_peer));
                    safe_copy(cur_peer->id, sizeof(cur_peer->id), idp);
                    cur_peer->allow_inbound = 1;
                    cur_peer->allow_outbound = 1;
                    cur_peer->auto_learned = 0;
                    section = 7;
                    cur_if = NULL;
                } else {
                    section = -1;
                }
            }
            continue;
        }

        if (section < 0) continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = line;
        char* v = eq + 1;
        trim_inplace(k);
        trim_inplace(v);

        if (section == 0) {
            if (stricmp_ascii(k, "callsign") == 0) {
                oyk_u8 def_s = st->node_ssid_set ? st->node_ssid_val : static_cast<oyk_u8>(OYK_DEFAULT_NODE_SSID);
                if (oyk_parse_callsign_with_default_ssid(v, def_s, &cfg->callsign)) {
                    st->node_call_explicit_ssid = (strchr(v, '-') != NULL) ? 1 : 0;
                }
            } else if (stricmp_ascii(k, "ssid") == 0) {
                int sv = atoi(v);
                if (sv >= 0 && sv <= 15) {
                    st->node_ssid_val = static_cast<oyk_u8>(sv);
                    st->node_ssid_set = 1;
                    if (!st->node_call_explicit_ssid) {
                        cfg->callsign.ssid = st->node_ssid_val;
                    }
                }
            } else if (stricmp_ascii(k, "alias") == 0) {
                oyk_parse_callsign(v, &cfg->alias);
            } else if (stricmp_ascii(k, "welcome") == 0 || stricmp_ascii(k, "welcome_text") == 0) {
                safe_copy(cfg->welcome_text, sizeof(cfg->welcome_text), v);
            } else if (stricmp_ascii(k, "new_user_welcome") == 0 || stricmp_ascii(k, "newuserwelcome") == 0) {
                safe_copy(cfg->new_user_welcome, sizeof(cfg->new_user_welcome), v);
            } else if (stricmp_ascii(k, "learned_peer_policy") == 0) {
                cfg->learned_peer_policy = oyk_parse_learned_policy(v, OYK_LEARN_INBOUND);
            } else if (stricmp_ascii(k, "info") == 0 || stricmp_ascii(k, "info_text") == 0) {
                safe_copy(cfg->info_text, sizeof(cfg->info_text), v);
            } else if (stricmp_ascii(k, "logpath") == 0) {
                safe_copy(cfg->log_path, sizeof(cfg->log_path), v);
            } else if (stricmp_ascii(k, "consolelevel") == 0) {
                oyk_parse_log_mask(v, &cfg->console_level);
            } else if (stricmp_ascii(k, "log_rotate_daily") == 0) {
                cfg->rotate_daily = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "banlist") == 0) {
                safe_copy(cfg->banlist_path, sizeof(cfg->banlist_path), v);
            } else if (stricmp_ascii(k, "interfaces") == 0 || stricmp_ascii(k, "interfaces_file") == 0) {
                safe_copy(cfg->interfaces_file, sizeof(cfg->interfaces_file), v);
            } else if (stricmp_ascii(k, "peers") == 0 || stricmp_ascii(k, "peers_file") == 0) {
                safe_copy(cfg->peers_file, sizeof(cfg->peers_file), v);
            } else if (stricmp_ascii(k, "accept_incoming") == 0) {
                cfg->accept_incoming = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "allow_flash_logging") == 0 ||
                       stricmp_ascii(k, "allow_logging_to_flash") == 0) {
                cfg->allow_flash_logging = (stricmp_ascii(v, "true") == 0 || strcmp(v, "1") == 0) ? 1 : 0;
            }
        } else if (section == 1) {
            oyk_u32 mask = 0;
            if (oyk_parse_log_mask(v, &mask) && cfg->log_file_count < OYK_MAX_LOG_FILES) {
                OykLogFileEntry* lf = &cfg->log_files[cfg->log_file_count++];
                memset(lf, 0, sizeof(*lf));
                safe_copy(lf->filename, sizeof(lf->filename), k);
                lf->mask = mask;
            }
        } else if (section == 2 && cur_if) {
            if (stricmp_ascii(k, "type") == 0) {
                safe_copy(cur_if->type, sizeof(cur_if->type), v);
                if (stricmp_ascii(cur_if->type, "telnet") == 0) {
                    cur_if->block_outbound = 1;
                    cur_if->allow_same_interface = 0;
                }
            } else if (stricmp_ascii(k, "description") == 0) {
                safe_copy(cur_if->description, sizeof(cur_if->description), v);
            } else if (stricmp_ascii(k, "host") == 0 || stricmp_ascii(k, "bind_host") == 0) {
                safe_copy(cur_if->host, sizeof(cur_if->host), v);
            } else if (stricmp_ascii(k, "netmask") == 0) {
                safe_copy(cur_if->netmask, sizeof(cur_if->netmask), v);
            } else if (stricmp_ascii(k, "port") == 0 || stricmp_ascii(k, "bind_port") == 0) {
                cur_if->port = static_cast<oyk_u16>(atoi(v));
            } else if (stricmp_ascii(k, "device") == 0) {
                safe_copy(cur_if->device, sizeof(cur_if->device), v);
            } else if (stricmp_ascii(k, "baud") == 0) {
                cur_if->baud = static_cast<oyk_u32>(atol(v));
            } else if (stricmp_ascii(k, "kiss_port") == 0) {
                cur_if->kiss_port = static_cast<oyk_u8>(atoi(v) & 0x0F);
            } else if (stricmp_ascii(k, "mode") == 0) {
                cur_if->server_mode = (stricmp_ascii(v, "server") == 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "allow_inbound") == 0 || stricmp_ascii(k, "inbound") == 0) {
                cur_if->block_inbound = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 0 : 1;
            } else if (stricmp_ascii(k, "allow_outbound") == 0 || stricmp_ascii(k, "outbound") == 0) {
                cur_if->block_outbound = static_cast<oyk_u8>(
                    (stricmp_ascii(cur_if->type, "telnet") == 0)
                        ? 1
                        : ((stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 0 : 1));
            } else if (stricmp_ascii(k, "allow_same_interface") == 0 || stricmp_ascii(k, "same_interface") == 0) {
                cur_if->allow_same_interface = static_cast<oyk_u8>(
                    (stricmp_ascii(cur_if->type, "telnet") == 0)
                        ? 0
                        : ((stricmp_ascii(v, "true") == 0 || strcmp(v, "1") == 0 || stricmp_ascii(v, "yes") == 0) ? 1 : 0));
            } else if (stricmp_ascii(k, "auto_learn") == 0) {
                cur_if->no_auto_learn = (stricmp_ascii(v, "false") == 0 || strcmp(v, "0") == 0 ||
                                         stricmp_ascii(v, "no") == 0 || stricmp_ascii(v, "off") == 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "learned_peer_policy") == 0) {
                cur_if->learned_peer_policy = oyk_parse_learned_policy(v, OYK_LEARN_INHERIT);
            } else if (stricmp_ascii(k, "peer") == 0) {
                if (cur_if->peer_count < OYK_MAX_PEERS) {
                    OykPeerEntry pe;
                    memset(&pe, 0, sizeof(pe));
                    if (oyk_parse_peer_spec(v, pe.callsign, sizeof(pe.callsign),
                                            &pe.ip, &pe.port,
                                            cur_if->port ? cur_if->port : static_cast<oyk_u16>(10093))) {
                        cur_if->peers[cur_if->peer_count++] = pe;
                    }
                }
            }
        } else if (section == 3) {
            if (stricmp_ascii(k, "enabled") == 0) {
                cfg->chat_enabled = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "ssid") == 0) {
                int sv = atoi(v);
                if (sv >= 0 && sv <= 15) {
                    cfg->chat_ssid = static_cast<oyk_u8>(sv);
                    if (cfg->chat_callsign.base[0] && !st->chat_call_explicit_ssid) {
                        cfg->chat_callsign.ssid = cfg->chat_ssid;
                    }
                }
            } else if (stricmp_ascii(k, "callsign") == 0) {
                if (!v[0]) {
                    memset(&cfg->chat_callsign, 0, sizeof(cfg->chat_callsign));
                    st->chat_call_explicit_ssid = 0;
                } else if (oyk_parse_callsign_with_default_ssid(v, cfg->chat_ssid, &cfg->chat_callsign)) {
                    st->chat_call_explicit_ssid = (strchr(v, '-') != NULL) ? 1 : 0;
                }
            } else if (stricmp_ascii(k, "alias") == 0) {
                if (!v[0]) memset(&cfg->chat_alias, 0, sizeof(cfg->chat_alias));
                else oyk_parse_callsign(v, &cfg->chat_alias);
            } else if (stricmp_ascii(k, "topic") == 0 || stricmp_ascii(k, "default_topic") == 0) {
                if (v[0]) safe_copy(cfg->chat_topic, sizeof(cfg->chat_topic), v);
            } else if (stricmp_ascii(k, "welcome") == 0) {
                safe_copy(cfg->chat_welcome, sizeof(cfg->chat_welcome), v);
            }
        } else if (section == 4) {
            if (stricmp_ascii(k, "enabled") == 0) {
                cfg->bbs_enabled = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "ssid") == 0) {
                int sv = atoi(v);
                if (sv >= 0 && sv <= 15) {
                    cfg->bbs_ssid = static_cast<oyk_u8>(sv);
                    if (cfg->bbs_callsign.base[0] && !st->bbs_call_explicit_ssid) {
                        cfg->bbs_callsign.ssid = cfg->bbs_ssid;
                    }
                }
            } else if (stricmp_ascii(k, "callsign") == 0) {
                if (!v[0]) {
                    memset(&cfg->bbs_callsign, 0, sizeof(cfg->bbs_callsign));
                    st->bbs_call_explicit_ssid = 0;
                } else if (oyk_parse_callsign_with_default_ssid(v, cfg->bbs_ssid, &cfg->bbs_callsign)) {
                    st->bbs_call_explicit_ssid = (strchr(v, '-') != NULL) ? 1 : 0;
                }
            } else if (stricmp_ascii(k, "alias") == 0) {
                if (!v[0]) memset(&cfg->bbs_alias, 0, sizeof(cfg->bbs_alias));
                else oyk_parse_callsign(v, &cfg->bbs_alias);
            } else if (stricmp_ascii(k, "mailpath") == 0 || stricmp_ascii(k, "mail_path") == 0) {
                if (v[0]) safe_copy(cfg->bbs_mail_path, sizeof(cfg->bbs_mail_path), v);
            } else if (stricmp_ascii(k, "files_enabled") == 0 || stricmp_ascii(k, "filesenabled") == 0) {
                cfg->bbs_files_enabled = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "files_case_insensitive") == 0 || stricmp_ascii(k, "filescaseinsensitive") == 0) {
                cfg->bbs_files_case_insensitive = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "filespath") == 0 || stricmp_ascii(k, "files_path") == 0) {
                if (v[0]) safe_copy(cfg->bbs_files_path, sizeof(cfg->bbs_files_path), v);
            } else if (stricmp_ascii(k, "welcome") == 0) {
                safe_copy(cfg->bbs_welcome, sizeof(cfg->bbs_welcome), v);
            } else if (stricmp_ascii(k, "max_msg_bytes") == 0) {
                long mv = atol(v);
                if (mv < 128L) mv = 128L;
                if (mv > static_cast<long>(OYK_MAX_BBS_BODY - 1)) mv = static_cast<long>(OYK_MAX_BBS_BODY - 1);
                cfg->bbs_max_msg_bytes = static_cast<oyk_u16>(mv);
            }
        } else if (section == 5) {
            if (stricmp_ascii(k, "enabled") == 0) {
                cfg->rig_enabled = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "protocol") == 0 || stricmp_ascii(k, "type") == 0) {
                safe_copy(cfg->rig_protocol, sizeof(cfg->rig_protocol), v);
            } else if (stricmp_ascii(k, "host") == 0 || stricmp_ascii(k, "address") == 0) {
                if (v[0]) safe_copy(cfg->rig_host, sizeof(cfg->rig_host), v);
            } else if (stricmp_ascii(k, "port") == 0) {
                long pv = atol(v);
                if (pv >= 0L && pv <= 65535L) cfg->rig_port = static_cast<oyk_u16>(pv);
            } else if (stricmp_ascii(k, "poll_interval_sec") == 0 || stricmp_ascii(k, "keepalive_sec") == 0) {
                long iv = atol(v);
                if (iv < 0L) iv = 0L;
                if (iv > 3600L) iv = 3600L;
                cfg->rig_poll_sec = static_cast<oyk_u16>(iv);
            } else if (stricmp_ascii(k, "notify") == 0) {
                cfg->rig_notify = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            }
        } else if (section == 6) {
            if (stricmp_ascii(k, "enabled") == 0) {
                cfg->printer_enabled = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "device") == 0 || stricmp_ascii(k, "port") == 0) {
                safe_copy(cfg->printer_device, sizeof(cfg->printer_device), v);
            } else if (stricmp_ascii(k, "protocol") == 0 || stricmp_ascii(k, "type") == 0) {
                if (v[0]) safe_copy(cfg->printer_protocol, sizeof(cfg->printer_protocol), v);
            } else if (stricmp_ascii(k, "baud") == 0) {
                long bv = atol(v);
                if (bv >= 300L) cfg->printer_baud = static_cast<oyk_u32>(bv);
            } else if (stricmp_ascii(k, "level") == 0 || stricmp_ascii(k, "mask") == 0) {
                oyk_parse_log_mask(v, &cfg->printer_level);
            } else if (stricmp_ascii(k, "condensed") == 0) {
                cfg->printer_condensed = (stricmp_ascii(v, "true") == 0 || strcmp(v, "1") == 0) ? 1 : 0;
            }
        } else if (section == 7 && cur_peer) {
            if (stricmp_ascii(k, "callsign") == 0 || stricmp_ascii(k, "call") == 0) {
                if (!v[0] || strcmp(v, "*") == 0) {
                    cur_peer->callsign[0] = '\0';
                } else {
                    OykCallsign pc;
                    if (oyk_parse_callsign(v, &pc)) {
                        oyk_format_callsign(&pc, cur_peer->callsign, sizeof(cur_peer->callsign));
                    } else {
                        safe_copy(cur_peer->callsign, sizeof(cur_peer->callsign), v);
                    }
                }
            } else if (stricmp_ascii(k, "description") == 0 || stricmp_ascii(k, "desc") == 0) {
                safe_copy(cur_peer->description, sizeof(cur_peer->description), v);
            } else if (stricmp_ascii(k, "interface") == 0 || stricmp_ascii(k, "interface_id") == 0 ||
                       stricmp_ascii(k, "iface") == 0) {
                safe_copy(cur_peer->interface_id, sizeof(cur_peer->interface_id), v);
            } else if (stricmp_ascii(k, "host") == 0 || stricmp_ascii(k, "address") == 0) {
                char hb[40];
                safe_copy(hb, sizeof(hb), v);
                char* colon = strrchr(hb, ':');
                if (colon && strchr(hb, ':') == colon && colon > hb && colon[1]) {
                    *colon = '\0';
                    if (cur_peer->port == 0) {
                        long pv = atol(colon + 1);
                        if (pv > 0L && pv <= 65535L) cur_peer->port = static_cast<oyk_u16>(pv);
                    }
                }
                safe_copy(cur_peer->host, sizeof(cur_peer->host), hb);
                oyk_parse_ipv4(cur_peer->host, &cur_peer->resolved_ip);
            } else if (stricmp_ascii(k, "port") == 0) {
                long pv = atol(v);
                if (pv >= 0L && pv <= 65535L) cur_peer->port = static_cast<oyk_u16>(pv);
            } else if (stricmp_ascii(k, "allow_inbound") == 0 || stricmp_ascii(k, "inbound") == 0) {
                cur_peer->allow_inbound = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            } else if (stricmp_ascii(k, "allow_outbound") == 0 || stricmp_ascii(k, "outbound") == 0) {
                cur_peer->allow_outbound = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
            }
        }
    }
}

int oyk_config_load(const char* path, OykConfig* cfg, char* err_out, size_t err_sz) {
    size_t raw_len = 0;
    char* raw = read_all_binary(path, &raw_len);
    if (!raw) {
        safe_copy(err_out, err_sz, "Unable to open config file");
        return 0;
    }

    oyk_config_init_default(cfg);
    cfg->iface_count = 0;
    safe_copy(cfg->source_path, sizeof(cfg->source_path), path);

    int found_eol = 0, mixed_eol = 0;
    cfg->line_ending = oyk_detect_first_eol(raw, raw_len, oyk_platform_default_eol(),
                                            &found_eol, &mixed_eol);
    cfg->has_detected_eol = found_eol;

    PortableParseState st;
    memset(&st, 0, sizeof(st));
    st.node_ssid_val = OYK_DEFAULT_NODE_SSID;
    parse_config_bytes(raw, raw_len, cfg, &st, 1, 1, 1);
    free(raw);

    char if_path[128], pr_path[128];
    oyk_config_resolve_interfaces(cfg, if_path, sizeof(if_path));
    oyk_config_resolve_peers(cfg, pr_path, sizeof(pr_path));
    int same_inc = (if_path[0] && pr_path[0] && portable_same_path(if_path, pr_path)) ? 1 : 0;
    if (same_inc) {
        if (!portable_same_path(if_path, path)) {
            size_t ilen = 0;
            char* iraw = read_all_binary(if_path, &ilen);
            if (iraw) {
                parse_config_bytes(iraw, ilen, cfg, &st, 0, 1, 1);
                free(iraw);
            }
        }
    } else {
        if (if_path[0] && !portable_same_path(if_path, path)) {
            size_t ilen = 0;
            char* iraw = read_all_binary(if_path, &ilen);
            if (iraw) {
                parse_config_bytes(iraw, ilen, cfg, &st, 0, 1, 0);
                free(iraw);
            }
        }
        if (pr_path[0] && !portable_same_path(pr_path, path)) {
            size_t plen = 0;
            char* praw = read_all_binary(pr_path, &plen);
            if (praw) {
                parse_config_bytes(praw, plen, cfg, &st, 0, 0, 1);
                free(praw);
            }
        }
    }

    append_migrated_iface_peers(cfg);

    if (cfg->rig_port == 0) {
        if (stricmp_ascii(cfg->rig_protocol, "radiofoundry") == 0 ||
            stricmp_ascii(cfg->rig_protocol, "takrf") == 0) {
            cfg->rig_port = 4191;
        } else if (stricmp_ascii(cfg->rig_protocol, "rigctl") == 0 ||
                   stricmp_ascii(cfg->rig_protocol, "rigctld") == 0) {
            cfg->rig_port = 4532;
        }
    }

    for (int i = 0; i < cfg->iface_count; ++i) {
        if (cfg->ifaces[i].port == 0) {
            if (stricmp_ascii(cfg->ifaces[i].type, "telnet") == 0) {
                cfg->ifaces[i].port = 8010;
            } else if (stricmp_ascii(cfg->ifaces[i].type, "kiss-tcp") == 0) {
                cfg->ifaces[i].port = 8001;
            } else if (stricmp_ascii(cfg->ifaces[i].type, "axudp") == 0) {
                cfg->ifaces[i].port = 10093;
            }
        }
    }

    if (cfg->iface_count == 0 && !cfg->interfaces_file[0]) {
        cfg->iface_count = 1;
        safe_copy(cfg->ifaces[0].id, sizeof(cfg->ifaces[0].id), "axudp0");
        safe_copy(cfg->ifaces[0].type, sizeof(cfg->ifaces[0].type), "axudp");
        safe_copy(cfg->ifaces[0].description, sizeof(cfg->ifaces[0].description), "Primary AXUDP Link");
        safe_copy(cfg->ifaces[0].host, sizeof(cfg->ifaces[0].host), "0.0.0.0");
        cfg->ifaces[0].port = 10093;
        cfg->ifaces[0].block_inbound = 0;
        cfg->ifaces[0].block_outbound = 0;
        cfg->ifaces[0].allow_same_interface = 0;
    }

    for (int i = 0; i < cfg->iface_count; ++i) {
        if (stricmp_ascii(cfg->ifaces[i].type, "telnet") == 0) {
            cfg->ifaces[i].block_outbound = 1;
            cfg->ifaces[i].allow_same_interface = 0;
        }
        if (stricmp_ascii(cfg->ifaces[i].type, "axudp") == 0 ||
            stricmp_ascii(cfg->ifaces[i].type, "axip") == 0) {
            if (cfg->ifaces[i].host[0] && oyk_is_all_digits(cfg->ifaces[i].host)) {
                oyk_u32 did = 0;
                if (!oyk_parse_dmr_id(cfg->ifaces[i].host, &did)) {
                    safe_copy(err_out, err_sz, "Invalid interface DMR ID (allowed 1..16777214)");
                    return 0;
                }
            }
            if (cfg->ifaces[i].netmask[0]) {
                oyk_u32 nm = 0;
                if (!oyk_parse_ipv4(cfg->ifaces[i].netmask, &nm)) {
                    safe_copy(err_out, err_sz, "Invalid interface IPv4 netmask");
                    return 0;
                }
            }
        }
    }
    for (int p = 0; p < cfg->peer_count && p < OYK_MAX_CONFIG_PEERS; ++p) {
        OykPeerConfig* pc = &cfg->peers[p];
        if (!pc->host[0]) continue;
        if (oyk_is_all_digits(pc->host)) {
            oyk_u32 did = 0;
            if (!oyk_parse_dmr_id(pc->host, &did)) {
                safe_copy(err_out, err_sz, "Invalid peer DMR ID (allowed 1..16777214)");
                return 0;
            }
        }
        oyk_u8 pref = 12U;
        for (int i = 0; i < cfg->iface_count; ++i) {
            if (stricmp_ascii(cfg->ifaces[i].id, pc->interface_id) == 0) {
                pref = oyk_dmr_prefix_for_iface(&cfg->ifaces[i]);
                break;
            }
        }
        (void)oyk_resolve_host_or_dmr_id(pc->host, pref, NULL, 0, &pc->resolved_ip);
    }
    return 1;
}

static void write_portable_iface_section(FILE* fp, const OykIfaceConfig* ic, const char* nl) {
    fprintf(fp, "%s[interface %s]%s", nl, ic->id, nl);
    fprintf(fp, "type = %s%s", ic->type, nl);
    if (ic->description[0]) fprintf(fp, "description = %s%s", ic->description, nl);
    if (stricmp_ascii(ic->type, "kiss-tcp") == 0) {
        fprintf(fp, "mode = %s%s", ic->server_mode ? "server" : "client", nl);
        if (ic->host[0]) fprintf(fp, "host = %s%s", ic->host, nl);
        if (ic->port > 0) fprintf(fp, "port = %u%s", static_cast<unsigned int>(ic->port), nl);
        fprintf(fp, "kiss_port = %u%s", static_cast<unsigned int>(ic->kiss_port), nl);
    } else if (stricmp_ascii(ic->type, "kiss-serial") == 0) {
        if (ic->device[0]) fprintf(fp, "device = %s%s", ic->device, nl);
        if (ic->baud > 0) fprintf(fp, "baud = %lu%s", static_cast<unsigned long>(ic->baud), nl);
        fprintf(fp, "kiss_port = %u%s", static_cast<unsigned int>(ic->kiss_port), nl);
    } else {
        if (ic->host[0]) fprintf(fp, "bind_host = %s%s", ic->host, nl);
        if (ic->port > 0) fprintf(fp, "bind_port = %u%s", static_cast<unsigned int>(ic->port), nl);
        if (ic->netmask[0]) fprintf(fp, "netmask = %s%s", ic->netmask, nl);
        if (ic->device[0]) fprintf(fp, "device = %s%s", ic->device, nl);
    }
    fprintf(fp, "allow_inbound = %s%s", ic->block_inbound ? "false" : "true", nl);
    fprintf(fp, "allow_outbound = %s%s",
            (stricmp_ascii(ic->type, "telnet") == 0 || ic->block_outbound) ? "false" : "true", nl);
    fprintf(fp, "allow_same_interface = %s%s",
            (stricmp_ascii(ic->type, "telnet") != 0 && ic->allow_same_interface) ? "true" : "false", nl);
    if (ic->no_auto_learn) {
        fprintf(fp, "auto_learn = false%s", nl);
    }
    if (ic->learned_peer_policy != OYK_LEARN_INHERIT) {
        fprintf(fp, "learned_peer_policy = %s%s",
                oyk_format_learned_policy(ic->learned_peer_policy), nl);
    }
}

static void write_portable_peer_section(FILE* fp, const OykPeerConfig* pc, const char* nl) {
    if (pc->auto_learned) return;
    fprintf(fp, "%s[peer %s]%s", nl, pc->id, nl);
    if (pc->callsign[0]) fprintf(fp, "callsign = %s%s", pc->callsign, nl);
    if (pc->description[0]) fprintf(fp, "description = %s%s", pc->description, nl);
    if (pc->interface_id[0]) fprintf(fp, "interface = %s%s", pc->interface_id, nl);
    if (pc->host[0]) fprintf(fp, "host = %s%s", pc->host, nl);
    if (pc->port > 0) fprintf(fp, "port = %u%s", static_cast<unsigned int>(pc->port), nl);
    fprintf(fp, "allow_inbound = %s%s", pc->allow_inbound ? "true" : "false", nl);
    fprintf(fp, "allow_outbound = %s%s", pc->allow_outbound ? "true" : "false", nl);
}

int oyk_config_save(const OykConfig* cfg, const char* path) {
    const char* target = (path && path[0]) ? path : cfg->source_path;
    if (!target || !target[0]) return 0;
    FILE* fp = fopen(target, "wb");
    if (!fp) return 0;

    const char* nl = oyk_eol_bytes(cfg->line_ending);
    char if_target[128], pr_target[128];
    resolve_cfg_subpath(target, cfg->interfaces_file, if_target, sizeof(if_target));
    resolve_cfg_subpath(target, cfg->peers_file, pr_target, sizeof(pr_target));
    int same_inc = (if_target[0] && pr_target[0] && portable_same_path(if_target, pr_target)) ? 1 : 0;
    int if_inline = (!if_target[0] || portable_same_path(if_target, target)) ? 1 : 0;
    int pr_inline = (!pr_target[0] || portable_same_path(pr_target, target)) ? 1 : 0;

    char call_s[16], alias_s[16];
    oyk_format_callsign(&cfg->callsign, call_s, sizeof(call_s));
    oyk_format_callsign(&cfg->alias, alias_s, sizeof(alias_s));

    fprintf(fp, "[node]%s", nl);
    fprintf(fp, "callsign = %s%s", call_s, nl);
    if (alias_s[0]) fprintf(fp, "alias = %s%s", alias_s, nl);
    fprintf(fp, "welcome = %s%s", cfg->welcome_text, nl);
    fprintf(fp, "info = %s%s", cfg->info_text, nl);
    fprintf(fp, "accept_incoming = %s%s", cfg->accept_incoming ? "true" : "false", nl);
    fprintf(fp, "allow_flash_logging = %s%s", cfg->allow_flash_logging ? "true" : "false", nl);
    fprintf(fp, "learned_peer_policy = %s%s",
            oyk_format_learned_policy(cfg->learned_peer_policy ? cfg->learned_peer_policy : static_cast<oyk_u8>(OYK_LEARN_INBOUND)), nl);
    if (cfg->new_user_welcome[0]) fprintf(fp, "new_user_welcome = %s%s", cfg->new_user_welcome, nl);
    fprintf(fp, "logpath = %s%s", cfg->log_path, nl);
    fprintf(fp, "consolelevel = 0x%lx%s", static_cast<unsigned long>(cfg->console_level), nl);
    fprintf(fp, "log_rotate_daily = %s%s", cfg->rotate_daily ? "true" : "false", nl);
    if (cfg->banlist_path[0]) fprintf(fp, "banlist = %s%s", cfg->banlist_path, nl);
    if (cfg->interfaces_file[0]) fprintf(fp, "interfaces = %s%s", cfg->interfaces_file, nl);
    if (cfg->peers_file[0]) fprintf(fp, "peers = %s%s", cfg->peers_file, nl);

    if (cfg->log_file_count > 0) {
        fprintf(fp, "%s[logfiles]%s", nl, nl);
        for (int i = 0; i < cfg->log_file_count; ++i) {
            fprintf(fp, "%s = 0x%lx%s", cfg->log_files[i].filename,
                    static_cast<unsigned long>(cfg->log_files[i].mask), nl);
        }
    }

    fprintf(fp, "%s[bbs]%s", nl, nl);
    fprintf(fp, "enabled = %s%s", cfg->bbs_enabled ? "true" : "false", nl);
    fprintf(fp, "ssid = %u%s", static_cast<unsigned int>(cfg->bbs_ssid), nl);
    if (cfg->bbs_callsign.base[0]) {
        char bc[16];
        oyk_format_callsign(&cfg->bbs_callsign, bc, sizeof(bc));
        fprintf(fp, "callsign = %s%s", bc, nl);
    }
    if (cfg->bbs_alias.base[0]) {
        char ba[16];
        oyk_format_callsign(&cfg->bbs_alias, ba, sizeof(ba));
        fprintf(fp, "alias = %s%s", ba, nl);
    }
    fprintf(fp, "mailpath = %s%s", cfg->bbs_mail_path, nl);
    fprintf(fp, "files_enabled = %s%s", cfg->bbs_files_enabled ? "true" : "false", nl);
    fprintf(fp, "files_case_insensitive = %s%s", cfg->bbs_files_case_insensitive ? "true" : "false", nl);
    fprintf(fp, "filespath = %s%s", cfg->bbs_files_path, nl);
    fprintf(fp, "welcome = %s%s", cfg->bbs_welcome, nl);
    fprintf(fp, "max_msg_bytes = %u%s", static_cast<unsigned int>(cfg->bbs_max_msg_bytes), nl);

    fprintf(fp, "%s[chat]%s", nl, nl);
    fprintf(fp, "enabled = %s%s", cfg->chat_enabled ? "true" : "false", nl);
    fprintf(fp, "ssid = %u%s", static_cast<unsigned int>(cfg->chat_ssid), nl);
    if (cfg->chat_callsign.base[0]) {
        char cc[16];
        oyk_format_callsign(&cfg->chat_callsign, cc, sizeof(cc));
        fprintf(fp, "callsign = %s%s", cc, nl);
    }
    if (cfg->chat_alias.base[0]) {
        char ca[16];
        oyk_format_callsign(&cfg->chat_alias, ca, sizeof(ca));
        fprintf(fp, "alias = %s%s", ca, nl);
    }
    fprintf(fp, "topic = %s%s", cfg->chat_topic, nl);
    fprintf(fp, "welcome = %s%s", cfg->chat_welcome, nl);

    fprintf(fp, "%s[rig]%s", nl, nl);
    fprintf(fp, "enabled = %s%s", cfg->rig_enabled ? "true" : "false", nl);
    fprintf(fp, "protocol = %s%s", cfg->rig_protocol[0] ? cfg->rig_protocol : "rigctl", nl);
    fprintf(fp, "host = %s%s", cfg->rig_host[0] ? cfg->rig_host : "127.0.0.1", nl);
    fprintf(fp, "port = %u%s", static_cast<unsigned int>(cfg->rig_port), nl);
    fprintf(fp, "poll_interval_sec = %u%s", static_cast<unsigned int>(cfg->rig_poll_sec), nl);
    fprintf(fp, "notify = %s%s", cfg->rig_notify ? "true" : "false", nl);

    fprintf(fp, "%s[printer]%s", nl, nl);
    fprintf(fp, "enabled = %s%s", cfg->printer_enabled ? "true" : "false", nl);
    fprintf(fp, "device = %s%s", cfg->printer_device, nl);
    fprintf(fp, "protocol = %s%s", cfg->printer_protocol[0] ? cfg->printer_protocol : "generic", nl);
    fprintf(fp, "baud = %lu%s", static_cast<unsigned long>(cfg->printer_baud ? cfg->printer_baud : 9600UL), nl);
    fprintf(fp, "level = 0x%lx%s", static_cast<unsigned long>(cfg->printer_level), nl);
    fprintf(fp, "condensed = %s%s", cfg->printer_condensed ? "true" : "false", nl);

    if (if_inline) {
        for (int i = 0; i < cfg->iface_count; ++i) {
            write_portable_iface_section(fp, &cfg->ifaces[i], nl);
        }
    }
    if (pr_inline) {
        for (int i = 0; i < cfg->peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
            write_portable_peer_section(fp, &cfg->peers[i], nl);
        }
    }

    fclose(fp);

    if (same_inc && !if_inline) {
        FILE* cfp = fopen(if_target, "wb");
        if (!cfp) return 0;
        fprintf(cfp, "# OYK Packet Node Interfaces and Peers%s", nl);
        for (int i = 0; i < cfg->iface_count; ++i) {
            write_portable_iface_section(cfp, &cfg->ifaces[i], nl);
        }
        for (int i = 0; i < cfg->peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
            write_portable_peer_section(cfp, &cfg->peers[i], nl);
        }
        fclose(cfp);
    } else {
        if (!if_inline) {
            FILE* ifp = fopen(if_target, "wb");
            if (!ifp) return 0;
            fprintf(ifp, "# OYK Packet Node Interfaces%s", nl);
            for (int i = 0; i < cfg->iface_count; ++i) {
                write_portable_iface_section(ifp, &cfg->ifaces[i], nl);
            }
            fclose(ifp);
        }
        if (!pr_inline) {
            FILE* pfp = fopen(pr_target, "wb");
            if (!pfp) return 0;
            fprintf(pfp, "# OYK Packet Node Peers%s", nl);
            for (int i = 0; i < cfg->peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
                write_portable_peer_section(pfp, &cfg->peers[i], nl);
            }
            fclose(pfp);
        }
    }
    return 1;
}

/* ==================== OykPortableNode ==================== */

OykPortableNode::OykPortableNode()
    : tx_cb_(NULL), tx_ctx_(NULL), local_cb_(NULL), local_ctx_(NULL),
      printer_cb_(NULL), printer_ctx_(NULL),
      started_at_(0), fixed_now_(0),
      accept_incoming_(1), manual_deny_incoming_(0), storage_is_flash_(0), next_session_id_(1), iface_count_(0),
      mheard_count_(0), new_heard_count_(0),
      banlist_eol_(oyk_platform_default_eol()),
      banlist_has_eol_(0), banlist_mixed_eol_(0),
      ban_count_(0), welcomed_count_(0), console_log_count_(0),
      bbs_next_id_(1), bbs_msg_count_(0),
      bbs_yapp_(NULL), bbs_yapp_sid_(0), bbs_yapp_tx_sess_(NULL) {
    oyk_config_init_default(&cfg_);
    memset(ifaces_, 0, sizeof(ifaces_));
    memset(sessions_, 0, sizeof(sessions_));
    memset(mheard_, 0, sizeof(mheard_));
    memset(new_heard_, 0, sizeof(new_heard_));
    memset(bans_, 0, sizeof(bans_));
    memset(welcomed_calls_, 0, sizeof(welcomed_calls_));
    memset(console_logs_, 0, sizeof(console_logs_));
    memset(bbs_msgs_, 0, sizeof(bbs_msgs_));
    banlist_path_[0] = '\0';
}

OykPortableNode::~OykPortableNode() {
    if (bbs_yapp_) {
        oyk_yapp_reset(bbs_yapp_);
        delete bbs_yapp_;
        bbs_yapp_ = NULL;
    }
}

void OykPortableNode::set_now_override(time_t fixed_now) {
    fixed_now_ = fixed_now;
}

time_t OykPortableNode::now_time() const {
    return (fixed_now_ != 0) ? fixed_now_ : time(NULL);
}

int OykPortableNode::init(const OykConfig* cfg, OykTxCallback tx_cb, void* tx_ctx,
                          char* err_out, size_t err_sz) {
    cfg_ = *cfg;
    migrate_iface_peers(&cfg_);
    welcomed_count_ = 0;
    for (int i = 0; i < cfg_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        if (!cfg_.peers[i].auto_learned && cfg_.peers[i].callsign[0] &&
            strcmp(cfg_.peers[i].callsign, "*") != 0) {
            mark_welcomed_call(cfg_.peers[i].callsign);
        }
    }
    tx_cb_ = tx_cb;
    tx_ctx_ = tx_ctx;
    started_at_ = now_time();
    accept_incoming_ = cfg_.accept_incoming;
    manual_deny_incoming_ = 0;

    /* Ensure default node.log and appeals.log exist in log_files */
    int has_node_log = 0, has_appeals_log = 0;
    for (int i = 0; i < cfg_.log_file_count; ++i) {
        if (stricmp_ascii(cfg_.log_files[i].filename, "node.log") == 0) has_node_log = 1;
        if (stricmp_ascii(cfg_.log_files[i].filename, "appeals.log") == 0) has_appeals_log = 1;
    }
    if (!has_node_log && cfg_.log_file_count < OYK_MAX_LOG_FILES) {
        OykLogFileEntry* lf = &cfg_.log_files[cfg_.log_file_count++];
        memset(lf, 0, sizeof(*lf));
        safe_copy(lf->filename, sizeof(lf->filename), "node.log");
        lf->mask = OYK_DEFAULT_NODELOG_MASK;
    }
    if (!has_appeals_log && cfg_.log_file_count < OYK_MAX_LOG_FILES) {
        OykLogFileEntry* lf = &cfg_.log_files[cfg_.log_file_count++];
        memset(lf, 0, sizeof(*lf));
        safe_copy(lf->filename, sizeof(lf->filename), "appeals.log");
        lf->mask = OYK_DEFAULT_APPEALS_MASK;
    }

    if (file_logging_allowed()) {
        make_dir_simple(cfg_.log_path);
    }

    iface_count_ = cfg_.iface_count;
    for (int i = 0; i < iface_count_; ++i) {
        if (stricmp_ascii(cfg_.ifaces[i].type, "telnet") == 0) {
            cfg_.ifaces[i].block_outbound = 1;
            cfg_.ifaces[i].allow_same_interface = 0;
        }
        ifaces_[i].cfg = cfg_.ifaces[i];
        ifaces_[i].running = 0;
        safe_copy(ifaces_[i].status, sizeof(ifaces_[i].status), "INIT");
    }

    oyk_config_resolve_banlist(&cfg_, banlist_path_, sizeof(banlist_path_));
    banlist_eol_ = cfg_.has_detected_eol ? cfg_.line_ending : oyk_platform_default_eol();
    if (!load_banlist(banlist_path_, err_out, err_sz)) {
        return 0;
    }
    if (cfg_.has_detected_eol && (!banlist_has_eol_ || banlist_mixed_eol_)) {
        banlist_eol_ = cfg_.line_ending;
    }

    if (cfg_.bbs_enabled) {
        bbs_load_index();
    }

    char call_s[16];
    oyk_format_callsign(&cfg_.callsign, call_s, sizeof(call_s));
    char msg[96];
    sprintf(msg, "Node %s online (%d bans loaded)", call_s, ban_count_);
    log_message(OYK_LOG_INFO, msg);
    return 1;
}

int OykPortableNode::begin_shutdown_with_notice(const char* notice_line) {
    accept_incoming_ = 0;
    char node_s[16], signoff[128];
    if (notice_line && notice_line[0]) {
        safe_copy(signoff, sizeof(signoff), notice_line);
    } else {
        oyk_format_callsign(&cfg_.callsign, node_s, sizeof(node_s));
        sprintf(signoff, "\r*** Node shutting down. 73 de %s\r", node_s);
    }

    int count = 0;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED) {
            send_text(&sessions_[i], signoff);
            send_disc(&sessions_[i]);
            sessions_[i].state = OYK_SESS_DISCONNECTING;
            ++count;
        }
    }
    return count;
}

int OykPortableNode::begin_shutdown() {
    return begin_shutdown_with_notice(NULL);
}

int OykPortableNode::disconnecting_count() const {
    int count = 0;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_DISCONNECTING) ++count;
    }
    return count;
}

void OykPortableNode::finish_shutdown() {
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state != OYK_SESS_FREE) {
            sessions_[i].state = OYK_SESS_FREE;
        }
    }
    log_message(OYK_LOG_INFO, "Node shutting down cleanly");
}

void OykPortableNode::shutdown() {
    begin_shutdown();
    finish_shutdown();
}

void OykPortableNode::set_local_event_callback(OykLocalEventCallback cb, void* ctx) {
    local_cb_ = cb;
    local_ctx_ = ctx;
}

void OykPortableNode::set_printer_callback(OykPrinterWriteCallback cb, void* ctx) {
    printer_cb_ = cb;
    printer_ctx_ = ctx;
}

oyk_u8 OykPortableNode::resolve_learned_policy(int iface_idx) const {
    if (iface_idx >= 0 && iface_idx < iface_count_) {
        oyk_u8 pol = ifaces_[iface_idx].cfg.learned_peer_policy;
        if (pol != OYK_LEARN_INHERIT) return pol;
    }
    return cfg_.learned_peer_policy ? cfg_.learned_peer_policy : static_cast<oyk_u8>(OYK_LEARN_INBOUND);
}

const OykPeerConfig* OykPortableNode::find_config_peer(int iface_idx, const OykCallsign* call) const {
    if (iface_idx < 0 || iface_idx >= iface_count_ || !call) return NULL;
    const char* if_id = ifaces_[iface_idx].cfg.id;
    char full[12];
    oyk_format_callsign(call, full, sizeof(full));
    for (int i = 0; i < cfg_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        const OykPeerConfig* p = &cfg_.peers[i];
        if (!p->callsign[0] || strcmp(p->callsign, "*") == 0) continue;
        if (p->interface_id[0] && stricmp_ascii(p->interface_id, if_id) != 0) continue;
        if (stricmp_ascii(p->callsign, full) == 0) return p;
        if (call->ssid == 0 && strchr(p->callsign, '-') == NULL &&
            stricmp_ascii(p->callsign, call->base) == 0) {
            return p;
        }
    }
    return NULL;
}

void OykPortableNode::migrate_iface_peers(OykConfig* cfg) const {
    append_migrated_iface_peers(cfg);
}

int OykPortableNode::is_welcomed_call(const char* call_str) const {
    if (!call_str || !call_str[0]) return 0;
    for (int i = 0; i < welcomed_count_; ++i) {
        if (stricmp_ascii(welcomed_calls_[i], call_str) == 0) return 1;
    }
    return 0;
}

void OykPortableNode::mark_welcomed_call(const char* call_str) {
    if (!call_str || !call_str[0] || is_welcomed_call(call_str)) return;
    if (welcomed_count_ < OYK_MAX_CONFIG_PEERS) {
        safe_copy(welcomed_calls_[welcomed_count_++], sizeof(welcomed_calls_[0]), call_str);
    }
}

int OykPortableNode::find_peer_for_call(int iface_idx, const OykCallsign* call,
                                        oyk_u32* ip_out, oyk_u16* port_out) const {
    if (iface_idx < 0 || iface_idx >= iface_count_ || !call || !ip_out || !port_out) return 0;
    const OykIfaceConfig& ic = ifaces_[iface_idx].cfg;
    char full[12];
    oyk_format_callsign(call, full, sizeof(full));

    int wildcard = -1;
    const oyk_u8 pref = oyk_dmr_prefix_for_iface(&ic);
    for (int i = 0; i < cfg_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        const OykPeerConfig* p = &cfg_.peers[i];
        if (p->interface_id[0] && stricmp_ascii(p->interface_id, ic.id) != 0) continue;
        oyk_u32 ip = p->resolved_ip;
        if (ip == 0 && p->host[0]) (void)oyk_resolve_host_or_dmr_id(p->host, pref, NULL, 0, &ip);
        if (ip == 0) continue;
        if (!p->callsign[0] || strcmp(p->callsign, "*") == 0) {
            if (wildcard < 0) wildcard = i;
        } else if (stricmp_ascii(p->callsign, full) == 0 ||
                   (call->ssid == 0 && strchr(p->callsign, '-') == NULL &&
                    stricmp_ascii(p->callsign, call->base) == 0)) {
            *ip_out = ip;
            *port_out = p->port ? p->port : (ic.port ? ic.port : static_cast<oyk_u16>(10093));
            return 1;
        }
    }
    if (wildcard >= 0) {
        oyk_u32 ip = cfg_.peers[wildcard].resolved_ip;
        if (ip == 0 && cfg_.peers[wildcard].host[0]) {
            (void)oyk_resolve_host_or_dmr_id(cfg_.peers[wildcard].host, pref, NULL, 0, &ip);
        }
        *ip_out = ip;
        *port_out = cfg_.peers[wildcard].port ? cfg_.peers[wildcard].port : (ic.port ? ic.port : static_cast<oyk_u16>(10093));
        return (ip != 0) ? 1 : 0;
    }

    int ic_wild = -1;
    for (int i = 0; i < ic.peer_count; ++i) {
        if (ic.peers[i].callsign[0] == '\0') {
            if (ic_wild < 0) ic_wild = i;
        } else if (stricmp_ascii(ic.peers[i].callsign, full) == 0) {
            *ip_out = ic.peers[i].ip;
            *port_out = ic.peers[i].port;
            return 1;
        }
    }
    if (ic_wild >= 0) {
        *ip_out = ic.peers[ic_wild].ip;
        *port_out = ic.peers[ic_wild].port;
        return 1;
    }
    return 0;
}

int OykPortableNode::open_local_terminal(int iface_idx, const char* target_spec,
                                         oyk_u32* sess_id_out, OykCallsign* resolved_call_out,
                                         int* is_local_out, char* err_out, size_t err_sz) {
    char spec_copy[96];
    safe_copy(spec_copy, sizeof(spec_copy), target_spec ? target_spec : "");
    trim_inplace(spec_copy);

    OykCallsign target;
    memset(&target, 0, sizeof(target));
    int want_local = 0;
    int direct_chat = 0;
    int direct_bbs = 0;

    if (stricmp_ascii(spec_copy, "BBS") == 0) {
        want_local = 1;
        direct_bbs = 1;
        oyk_config_resolved_bbs_callsign(&cfg_, &target);
    } else if (strchr(spec_copy, '@') == NULL && strchr(spec_copy, ' ') == NULL &&
               spec_copy[0] && oyk_parse_callsign(spec_copy, &target) &&
               is_for_bbs(&target)) {
        want_local = 1;
        direct_bbs = 1;
    } else if (stricmp_ascii(spec_copy, "CHAT") == 0) {
        want_local = 1;
        direct_chat = 1;
        oyk_config_resolved_chat_callsign(&cfg_, &target);
    } else if (strchr(spec_copy, '@') == NULL && strchr(spec_copy, ' ') == NULL &&
               spec_copy[0] && oyk_parse_callsign(spec_copy, &target) &&
               is_for_chat(&target)) {
        want_local = 1;
        direct_chat = 1;
    } else if (iface_idx < 0 || !spec_copy[0] ||
               stricmp_ascii(spec_copy, "LOCAL") == 0 ||
               stricmp_ascii(spec_copy, "NODE") == 0) {
        want_local = 1;
        target = cfg_.callsign;
    } else if (strchr(spec_copy, '@') == NULL && strchr(spec_copy, ' ') == NULL &&
               oyk_parse_callsign(spec_copy, &target) &&
               is_for_node(&target)) {
        want_local = 1;
    }

    if (want_local) {
        if (direct_bbs && !cfg_.bbs_enabled) {
            safe_copy(err_out, err_sz, "Local BBS is disabled");
            return 0;
        }
        if (direct_chat && !cfg_.chat_enabled) {
            safe_copy(err_out, err_sz, "Local CHAT is disabled");
            return 0;
        }
        OykSession* sess = NULL;
        for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
            if (sessions_[i].state == OYK_SESS_FREE) {
                sess = &sessions_[i];
                break;
            }
        }
        if (!sess) {
            safe_copy(err_out, err_sz, "No free session slots");
            return 0;
        }
        time_t now_t = now_time();
        memset(sess, 0, sizeof(*sess));
        sess->id = next_session_id_++;
        sess->iface_idx = -1;
        sess->local_call = target;
        sess->remote_call = cfg_.callsign;
        sess->state = OYK_SESS_CONNECTED;
        sess->is_local_term = 1;
        sess->is_local_loopback = 1;
        oyk_format_now_hms(now_t, sess->since_hms, sizeof(sess->since_hms));
        sess->last_activity = now_t;

        if (sess_id_out) *sess_id_out = sess->id;
        if (resolved_call_out) *resolved_call_out = target;
        if (is_local_out) *is_local_out = 1;

        if (local_cb_) {
            local_cb_(sess->id, 1, NULL, 0, "Connected locally", local_ctx_);
        }
        if (direct_bbs) {
            bbs_enter(sess, 1);
        } else if (direct_chat) {
            chat_join(sess, 1);
        } else {
            char node_s[16], banner[200];
            oyk_format_callsign(&cfg_.callsign, node_s, sizeof(node_s));
            if (cfg_.welcome_text[0]) {
                sprintf(banner, "*** Connected to %s Packet Node\r%s\r%s> ",
                        node_s, cfg_.welcome_text, node_s);
            } else {
                sprintf(banner, "*** Connected to %s Packet Node\r%s> ",
                        node_s, node_s);
            }
            send_text(sess, banner);
        }
        char cs[12], msg[80];
        oyk_format_callsign(&target, cs, sizeof(cs));
        sprintf(msg, "Local terminal #%lu connected locally to %s",
                static_cast<unsigned long>(sess->id), cs);
        log_message(OYK_LOG_INFO, msg);
        return 1;
    }

    if (iface_idx < 0 || iface_idx >= iface_count_) {
        safe_copy(err_out, err_sz, "Invalid interface");
        return 0;
    }
    if (ifaces_[iface_idx].cfg.block_outbound ||
        stricmp_ascii(ifaces_[iface_idx].cfg.type, "telnet") == 0) {
        safe_copy(err_out, err_sz, "Outbound connections on this interface are disabled");
        return 0;
    }

    oyk_u32 peer_ip = 0;
    oyk_u16 peer_port = 0;

    if (strchr(spec_copy, '@') != NULL || strchr(spec_copy, ' ') != NULL) {
        char call_s[16];
        oyk_u16 def_p = ifaces_[iface_idx].cfg.port ? ifaces_[iface_idx].cfg.port : static_cast<oyk_u16>(10093);
        if (!oyk_parse_peer_spec(spec_copy, call_s, sizeof(call_s), &peer_ip, &peer_port, def_p) ||
            !call_s[0] || !oyk_parse_callsign(call_s, &target)) {
            safe_copy(err_out, err_sz, "Use CALLSIGN or CALLSIGN@ip:port");
            return 0;
        }
    } else {
        if (!oyk_parse_callsign(spec_copy, &target)) {
            safe_copy(err_out, err_sz, "Invalid target callsign");
            return 0;
        }
        const OykPeerConfig* cpeer = find_config_peer(iface_idx, &target);
        if (cpeer && !cpeer->allow_outbound) {
            safe_copy(err_out, err_sz, "Outbound connections to this peer are disabled");
            return 0;
        }
        if (!find_peer_for_call(iface_idx, &target, &peer_ip, &peer_port)) {
            if (stricmp_ascii(ifaces_[iface_idx].cfg.type, "axudp") == 0 ||
                stricmp_ascii(ifaces_[iface_idx].cfg.type, "axip") == 0 ||
                stricmp_ascii(ifaces_[iface_idx].cfg.type, "usb-cdc-net") == 0) {
                safe_copy(err_out, err_sz, "No peer IP for callsign (use CALL@ip:port)");
                return 0;
            }
        }
    }
    if (peer_ip != 0 && ifaces_[iface_idx].cfg.netmask[0]) {
        oyk_u32 bind_ip = 0, mask_ip = 0;
        (void)oyk_resolve_host_or_dmr_id(ifaces_[iface_idx].cfg.host, 12U, NULL, 0, &bind_ip);
        if (oyk_parse_ipv4(ifaces_[iface_idx].cfg.netmask, &mask_ip) &&
            !oyk_ip_matches_netmask(peer_ip, bind_ip, mask_ip)) {
            ifaces_[iface_idx].tx_errors++;
            safe_copy(err_out, err_sz, "Peer IP outside interface netmask");
            return 0;
        }
    }

    OykSession* sess = NULL;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_FREE) {
            sess = &sessions_[i];
            break;
        }
    }
    if (!sess) {
        safe_copy(err_out, err_sz, "No free session slots");
        return 0;
    }

    time_t now_t = now_time();
    memset(sess, 0, sizeof(*sess));
    sess->id = next_session_id_++;
    sess->iface_idx = iface_idx;
    sess->local_call = cfg_.callsign;
    sess->remote_call = target;
    sess->peer_ip = peer_ip;
    sess->peer_port = peer_port;
    sess->state = OYK_SESS_CONNECTING;
    sess->is_local_term = 1;
    sess->is_local_loopback = 0;
    oyk_format_now_hms(now_t, sess->since_hms, sizeof(sess->since_hms));
    sess->last_activity = now_t;

    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    sabm.dst = target;
    sabm.src = cfg_.callsign;
    sabm.is_command = 1;
    sabm.control = 0x3FU; /* SABM P=1 */
    if (tx_cb_ && tx_cb_(iface_idx, &sabm, peer_ip, peer_port, 0, tx_ctx_)) {
        ifaces_[iface_idx].tx_frames++;
    }

    if (sess_id_out) *sess_id_out = sess->id;
    if (resolved_call_out) *resolved_call_out = target;
    if (is_local_out) *is_local_out = 0;

    char cs[12], msg[80];
    oyk_format_callsign(&target, cs, sizeof(cs));
    sprintf(msg, "Local terminal #%lu connecting to %s on %s",
            static_cast<unsigned long>(sess->id), cs, ifaces_[iface_idx].cfg.id);
    log_message(OYK_LOG_INFO, msg);
    return 1;
}

int OykPortableNode::send_local_bytes(oyk_u32 session_id, const oyk_u8* data, size_t len) {
    if (!data || len == 0) return 0;
    OykSession* sess = NULL;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].id == session_id && sessions_[i].is_local_term &&
            sessions_[i].state == OYK_SESS_CONNECTED) {
            sess = &sessions_[i];
            break;
        }
    }
    if (!sess) return 0;

    if (sess->is_local_loopback) {
        sess->last_activity = now_time();
        for (size_t i = 0; i < len && sess->state == OYK_SESS_CONNECTED; ++i) {
            oyk_u8 ch = data[i];
            if (ch == '\r' || ch == '\n') {
                sess->rx_line[sess->rx_line_len] = '\0';
                if (sess->rx_line_len > 0 || ch == '\r') {
                    if (sess->in_bbs) {
                        handle_bbs_line(sess, sess->rx_line);
                    } else if (sess->in_chat) {
                        handle_chat_line(sess, sess->rx_line);
                    } else {
                        handle_normal_command(sess, sess->rx_line);
                    }
                }
                sess->rx_line_len = 0;
            } else if (ch == 0x1AU) {
                if (sess->rx_line_len > 0) {
                    sess->rx_line[sess->rx_line_len] = '\0';
                    if (sess->in_bbs) handle_bbs_line(sess, sess->rx_line);
                    sess->rx_line_len = 0;
                }
                if (sess->in_bbs && sess->state == OYK_SESS_CONNECTED) {
                    handle_bbs_line(sess, "/EX");
                }
            } else if (ch >= 0x20U && ch <= 0x7EU) {
                if (sess->rx_line_len < static_cast<int>(sizeof(sess->rx_line) - 1)) {
                    sess->rx_line[sess->rx_line_len++] = static_cast<char>(ch);
                }
            }
        }
        return 1;
    }

    if (!tx_cb_) return 0;
    size_t pos = 0;
    while (pos < len) {
        size_t chunk = len - pos;
        if (chunk > 220) chunk = 220;
        OykFrame f;
        memset(&f, 0, sizeof(f));
        f.dst = sess->remote_call;
        f.src = sess->local_call;
        f.is_command = 1;
        f.control = static_cast<oyk_u8>(((sess->vs & 0x07U) << 1) | ((sess->vr & 0x07U) << 5));
        f.has_pid = 1;
        f.pid = 0xF0U;
        f.info_len = static_cast<oyk_u16>(chunk);
        memcpy(f.info, data + pos, chunk);
        sess->vs = static_cast<oyk_u8>((sess->vs + 1) & 0x07U);
        if (tx_cb_(sess->iface_idx, &f, sess->peer_ip, sess->peer_port, sess->peer_handle, tx_ctx_)) {
            ifaces_[sess->iface_idx].tx_frames++;
        }
        pos += chunk;
    }
    sess->last_activity = now_time();
    return 1;
}

int OykPortableNode::close_local_terminal(oyk_u32 session_id) {
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].id == session_id && sessions_[i].is_local_term &&
            sessions_[i].state != OYK_SESS_FREE) {
            if (sessions_[i].in_chat) chat_leave(&sessions_[i], "Disconnected");
            if (!sessions_[i].is_local_loopback) {
                send_disc(&sessions_[i]);
            }
            sessions_[i].state = OYK_SESS_FREE;
            if (local_cb_) {
                local_cb_(session_id, 3, NULL, 0, "Disconnected", local_ctx_);
            }
            return 1;
        }
    }
    return 0;
}

void OykPortableNode::set_iface_status(int iface_idx, int running, const char* status_text) {
    if (iface_idx < 0 || iface_idx >= iface_count_) return;
    ifaces_[iface_idx].running = running;
    safe_copy(ifaces_[iface_idx].status, sizeof(ifaces_[iface_idx].status), status_text);
}

int OykPortableNode::load_banlist(const char* path, char* err_out, size_t err_sz) {
    ban_count_ = 0;
    banlist_has_eol_ = 0;
    banlist_mixed_eol_ = 0;

    size_t raw_len = 0;
    char* raw = read_all_binary(path, &raw_len);
    if (!raw) {
        return 1; /* Missing banlist is fine */
    }

    banlist_eol_ = oyk_detect_first_eol(raw, raw_len, banlist_eol_,
                                        &banlist_has_eol_, &banlist_mixed_eol_);

    size_t pos = 0;
    char line[256];
    while (next_line_any_eol(raw, raw_len, &pos, line, sizeof(line))) {
        trim_inplace(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        char* eq = strchr(line, '=');
        if (!eq) {
            free(raw);
            safe_copy(err_out, err_sz, "Malformed line in banlist.cfg");
            return 0;
        }
        *eq = '\0';
        char key[12];
        if (!normalize_ban_key(line, key, sizeof(key))) {
            free(raw);
            safe_copy(err_out, err_sz, "Invalid callsign in banlist.cfg");
            return 0;
        }

        char* rhs = eq + 1;
        char* c1 = strchr(rhs, ',');
        char* c2 = c1 ? strchr(c1 + 1, ',') : NULL;
        char* c3 = c2 ? strchr(c2 + 1, ',') : NULL;
        if (!c1 || !c2 || !c3) {
            free(raw);
            safe_copy(err_out, err_sz, "Expected 4 comma-separated fields in banlist.cfg");
            return 0;
        }
        *c1 = *c2 = *c3 = '\0';
        char* f_ban = rhs;
        char* f_lift = c1 + 1;
        char* f_flags = c2 + 1;
        char* f_reason = c3 + 1;
        trim_inplace(f_ban);
        trim_inplace(f_lift);
        trim_inplace(f_flags);
        trim_inplace(f_reason);

        oyk_u32 flags = 0;
        if (!valid_timestamp_15(f_ban) ||
            (stricmp_ascii(f_lift, "permanent") != 0 && !valid_timestamp_15(f_lift)) ||
            !oyk_parse_flags_u32(f_flags, &flags)) {
            free(raw);
            safe_copy(err_out, err_sz, "Invalid ban timestamp or flags in banlist.cfg");
            return 0;
        }

        if (ban_count_ < OYK_MAX_BANS) {
            OykBanEntry* e = &bans_[ban_count_++];
            memset(e, 0, sizeof(*e));
            safe_copy(e->key, sizeof(e->key), key);
            safe_copy(e->banned_at, sizeof(e->banned_at), f_ban);
            safe_copy(e->lift_at, sizeof(e->lift_at),
                      (stricmp_ascii(f_lift, "permanent") == 0) ? "permanent" : f_lift);
            e->flags = flags;
            safe_copy(e->reason, sizeof(e->reason), f_reason[0] ? f_reason : "Banned by sysop.");
        }
    }

    free(raw);
    return 1;
}

int OykPortableNode::save_banlist() {
    if (!banlist_path_[0]) return 0;
    FILE* fp = fopen(banlist_path_, "wb");
    if (!fp) return 0;

    const char* nl = oyk_eol_bytes(banlist_eol_);
    fprintf(fp, "# OYK Packet Node Ban List%s", nl);
    fprintf(fp, "# Format: CALLSIGN[-SSID]=YYYYMMDD-HHMMSS,YYYYMMDD-HHMMSS|permanent,flags,reason%s", nl);
    fprintf(fp, "# Flags: bit0=appeal_allowed bit1=notified bit2=appeal_used bit3=hard_ban%s%s", nl, nl);

    for (int i = 0; i < ban_count_; ++i) {
        char fbin[12];
        format_flags_bin(bans_[i].flags, fbin, sizeof(fbin));
        fprintf(fp, "%s=%s,%s,%s,%s%s",
                bans_[i].key, bans_[i].banned_at, bans_[i].lift_at,
                fbin, bans_[i].reason, nl);
    }
    fclose(fp);
    return 1;
}

int OykPortableNode::find_ban_index(const OykCallsign* call, const char* now_compact) const {
    char full[12];
    oyk_format_callsign(call, full, sizeof(full));
    int base_match = -1;

    for (int i = 0; i < ban_count_; ++i) {
        /* Skip expired bans */
        if (stricmp_ascii(bans_[i].lift_at, "permanent") != 0 &&
            strcmp(now_compact, bans_[i].lift_at) >= 0) {
            continue;
        }
        if (strcmp(bans_[i].key, full) == 0) {
            return i;
        }
        if (strcmp(bans_[i].key, call->base) == 0) {
            base_match = i;
        }
    }
    return base_match;
}

int OykPortableNode::is_for_node(const OykCallsign* dst) const {
    if (oyk_callsign_eq(dst, &cfg_.callsign)) return 1;
    if (cfg_.alias.base[0] && oyk_callsign_eq(dst, &cfg_.alias)) return 1;
    return 0;
}

int OykPortableNode::is_for_bbs(const OykCallsign* dst) const {
    if (!cfg_.bbs_enabled || !dst) return 0;
    OykCallsign bbs_call;
    oyk_config_resolved_bbs_callsign(&cfg_, &bbs_call);
    if (oyk_callsign_eq(dst, &bbs_call)) return 1;
    if (cfg_.bbs_alias.base[0] && oyk_callsign_eq(dst, &cfg_.bbs_alias)) return 1;
    return 0;
}

int OykPortableNode::is_for_chat(const OykCallsign* dst) const {
    if (!cfg_.chat_enabled || !dst) return 0;
    OykCallsign chat_call;
    oyk_config_resolved_chat_callsign(&cfg_, &chat_call);
    if (oyk_callsign_eq(dst, &chat_call)) return 1;
    if (cfg_.chat_alias.base[0] && oyk_callsign_eq(dst, &cfg_.chat_alias)) return 1;
    return 0;
}

void OykPortableNode::push_console_log(const char* line) {
    if (console_log_count_ < OYK_MAX_CONSOLE_LOGS) {
        safe_copy(console_logs_[console_log_count_++], sizeof(console_logs_[0]), line);
    } else {
        for (int i = 1; i < OYK_MAX_CONSOLE_LOGS; ++i) {
            safe_copy(console_logs_[i - 1], sizeof(console_logs_[0]), console_logs_[i]);
        }
        safe_copy(console_logs_[OYK_MAX_CONSOLE_LOGS - 1], sizeof(console_logs_[0]), line);
    }
}

void OykPortableNode::write_log_file_line(OykLogFileEntry* lf, const char* line, const char* today) {
    char full_path[160];
    join_path(cfg_.log_path, lf->filename, full_path, sizeof(full_path));

    /* Daily log rotation when calendar date rolls over */
    if (cfg_.rotate_daily && lf->active_date[0] != '\0' && strcmp(lf->active_date, today) != 0) {
        char rotated[180];
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
        /* 8.3-safe rotated filename on DOS: MMDD_<first3>.LOG */
        char dos_rot[16];
        sprintf(dos_rot, "%c%c%c%c%.3s.LOG",
                lf->active_date[4], lf->active_date[5],
                lf->active_date[6], lf->active_date[7], lf->filename);
        join_path(cfg_.log_path, dos_rot, rotated, sizeof(rotated));
#else
        sprintf(rotated, "%s.%s", full_path, lf->active_date);
#endif
        rename(full_path, rotated);
    }
    safe_copy(lf->active_date, sizeof(lf->active_date), today);

    FILE* fp = fopen(full_path, "ab");
    if (fp) {
        const char* nl = oyk_eol_bytes(oyk_platform_default_eol());
        fprintf(fp, "%s%s", line, nl);
        fclose(fp);
    }
}

void OykPortableNode::set_storage_is_flash(int is_flash) {
    storage_is_flash_ = is_flash ? 1 : 0;
    if (file_logging_allowed()) {
        make_dir_simple(cfg_.log_path);
    }
}

int OykPortableNode::file_logging_allowed() const {
    return (!storage_is_flash_ || cfg_.allow_flash_logging) ? 1 : 0;
}

void OykPortableNode::log_message(oyk_u32 level, const char* msg) {
    if (level == OYK_LOG_SILENT || !msg) return;
    time_t t = now_time();
    char hms[10], compact[16], today[10];
    oyk_format_now_hms(t, hms, sizeof(hms));
    oyk_format_now_compact(t, compact, sizeof(compact));
    oyk_format_now_yyyymmdd(t, today, sizeof(today));

    if ((level & cfg_.console_level) != 0) {
        char cline[112];
        sprintf(cline, "[%s] [%s] %.86s", hms, level_name_str(level), msg);
        push_console_log(cline);
    }

    char fline[160];
    sprintf(fline, "[%s] [%s] %.120s", compact, level_name_str(level), msg);
    if (file_logging_allowed()) {
        for (int i = 0; i < cfg_.log_file_count; ++i) {
            if ((level & cfg_.log_files[i].mask) != 0) {
                write_log_file_line(&cfg_.log_files[i], fline, today);
            }
        }
    }
    if (printer_cb_ && cfg_.printer_enabled && (level & cfg_.printer_level) != 0) {
        printer_cb_(level, fline, printer_ctx_);
    }
}

void OykPortableNode::log_appeal(const char* callsign, const char* clean_appeal) {
    time_t t = now_time();
    char compact[16], today[10];
    oyk_format_now_compact(t, compact, sizeof(compact));
    oyk_format_now_yyyymmdd(t, today, sizeof(today));

    /* Strictly callsign, time of appeal, and <=140-char base ASCII appeal message */
    char aline[180];
    sprintf(aline, "%s %s %.140s", callsign, compact, clean_appeal);

    if ((OYK_LOG_APPEAL & cfg_.console_level) != 0) {
        char cline[112];
        sprintf(cline, "[APPEAL] %.100s", aline);
        push_console_log(cline);
    }

    if (file_logging_allowed()) {
        for (int i = 0; i < cfg_.log_file_count; ++i) {
            if ((OYK_LOG_APPEAL & cfg_.log_files[i].mask) != 0) {
                write_log_file_line(&cfg_.log_files[i], aline, today);
            }
        }
    }
    if (printer_cb_ && cfg_.printer_enabled && (OYK_LOG_APPEAL & cfg_.printer_level) != 0) {
        printer_cb_(OYK_LOG_APPEAL, aline, printer_ctx_);
    }
}

void OykPortableNode::record_heard(const OykCallsign* src, int iface_idx) {
    if (!src || !src->base[0] || is_for_node(src)) return;
    char call_s[12], hms[10];
    oyk_format_callsign(src, call_s, sizeof(call_s));
    oyk_format_now_hms(now_time(), hms, sizeof(hms));

    int found = -1;
    for (int i = 0; i < mheard_count_; ++i) {
        if (strcmp(mheard_[i].callsign, call_s) == 0) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        mheard_[found].packets++;
        safe_copy(mheard_[found].time_hms, sizeof(mheard_[found].time_hms), hms);
        safe_copy(mheard_[found].iface_id, sizeof(mheard_[found].iface_id), ifaces_[iface_idx].cfg.id);
        return;
    }

    /* Brand new callsign heard! */
    if (mheard_count_ < OYK_MAX_MHEARD) {
        OykMHeard* mh = &mheard_[mheard_count_++];
        safe_copy(mh->callsign, sizeof(mh->callsign), call_s);
        safe_copy(mh->iface_id, sizeof(mh->iface_id), ifaces_[iface_idx].cfg.id);
        safe_copy(mh->time_hms, sizeof(mh->time_hms), hms);
        mh->packets = 1;
    }

    /* Shift last 5 new callsigns heard (newest at index 0) */
    int limit = (new_heard_count_ < OYK_MAX_NEW_HEARD) ? new_heard_count_ : (OYK_MAX_NEW_HEARD - 1);
    for (int i = limit; i > 0; --i) {
        new_heard_[i] = new_heard_[i - 1];
    }
    if (new_heard_count_ < OYK_MAX_NEW_HEARD) new_heard_count_++;
    safe_copy(new_heard_[0].callsign, sizeof(new_heard_[0].callsign), call_s);
    safe_copy(new_heard_[0].iface_id, sizeof(new_heard_[0].iface_id), ifaces_[iface_idx].cfg.id);
    safe_copy(new_heard_[0].time_hms, sizeof(new_heard_[0].time_hms), hms);

    char msg[64];
    sprintf(msg, "New callsign heard: %s on %s", call_s, ifaces_[iface_idx].cfg.id);
    log_message(OYK_LOG_INFO, msg);
}

void OykPortableNode::send_raw_bytes(OykSession* sess, const oyk_u8* data, size_t len) {
    if (!sess || !data || len == 0 || sess->is_local_loopback || !tx_cb_) return;
    size_t pos = 0;
    while (pos < len) {
        size_t chunk = len - pos;
        if (chunk > 220) chunk = 220;
        OykFrame f;
        memset(&f, 0, sizeof(f));
        f.dst = sess->remote_call;
        f.src = sess->local_call;
        f.is_command = 1;
        f.control = static_cast<oyk_u8>(((sess->vs & 0x07U) << 1) | ((sess->vr & 0x07U) << 5));
        f.has_pid = 1;
        f.pid = 0xF0U;
        f.info_len = static_cast<oyk_u16>(chunk);
        memcpy(f.info, data + pos, chunk);
        sess->vs = static_cast<oyk_u8>((sess->vs + 1) & 0x07U);
        if (tx_cb_(sess->iface_idx, &f, sess->peer_ip, sess->peer_port, sess->peer_handle, tx_ctx_)) {
            ifaces_[sess->iface_idx].tx_frames++;
        }
        pos += chunk;
    }
}

void OykPortableNode::send_text(OykSession* sess, const char* text) {
    if (!sess || !text) return;
    if (sess->is_local_loopback) {
        if (local_cb_) {
            local_cb_(sess->id, 2, reinterpret_cast<const oyk_u8*>(text), strlen(text), NULL, local_ctx_);
        }
        return;
    }
    if (!tx_cb_) return;
    size_t total = strlen(text);
    size_t pos = 0;
    while (pos < total) {
        size_t chunk = total - pos;
        if (chunk > 220) chunk = 220;
        OykFrame f;
        memset(&f, 0, sizeof(f));
        f.dst = sess->remote_call;
        f.src = sess->local_call;
        f.is_command = 1;
        f.control = static_cast<oyk_u8>(((sess->vs & 0x07U) << 1) | ((sess->vr & 0x07U) << 5));
        f.has_pid = 1;
        f.pid = 0xF0U;
        f.info_len = static_cast<oyk_u16>(chunk);
        memcpy(f.info, text + pos, chunk);
        sess->vs = static_cast<oyk_u8>((sess->vs + 1) & 0x07U);
        if (tx_cb_(sess->iface_idx, &f, sess->peer_ip, sess->peer_port, sess->peer_handle, tx_ctx_)) {
            ifaces_[sess->iface_idx].tx_frames++;
        }
        pos += chunk;
    }
}

void OykPortableNode::send_disc(OykSession* sess) {
    if (!sess) return;
    if (sess->is_local_loopback) {
        if (local_cb_) {
            local_cb_(sess->id, 3, NULL, 0, "Disconnected", local_ctx_);
        }
        return;
    }
    if (!tx_cb_) return;
    OykFrame f;
    memset(&f, 0, sizeof(f));
    f.dst = sess->remote_call;
    f.src = sess->local_call;
    f.is_command = 1;
    f.control = 0x53U; /* DISC with P=1 */
    if (tx_cb_(sess->iface_idx, &f, sess->peer_ip, sess->peer_port, sess->peer_handle, tx_ctx_)) {
        ifaces_[sess->iface_idx].tx_frames++;
    }
}

/* ==================== Portable BBS Files Area & Sysop Approval ==================== */

struct OykDirEnt {
    char name[32];
    int is_dir;
    oyk_u32 size;
};

static int oyk_list_dir_entries(const char* dir_path, OykDirEnt* out, int max_out) {
    if (!dir_path || !dir_path[0] || !out || max_out <= 0) return 0;
    int count = 0;
#if defined(_WIN32) || defined(_WIN64)
    char pat[192];
    join_path(dir_path, "*", pat, sizeof(pat));
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!oyk_yapp_is_safe_filename(fd.cFileName)) continue;
        if (count < max_out) {
            safe_copy(out[count].name, sizeof(out[count].name), fd.cFileName);
            out[count].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
            out[count].size = static_cast<oyk_u32>(fd.nFileSizeLow);
            ++count;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#elif defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
    char pat[192];
    join_path(dir_path, "*.*", pat, sizeof(pat));
    struct find_t fi;
    if (_dos_findfirst(pat, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_SUBDIR, &fi) == 0) {
        do {
            if (!oyk_yapp_is_safe_filename(fi.name)) continue;
            if (count < max_out) {
                safe_copy(out[count].name, sizeof(out[count].name), fi.name);
                out[count].is_dir = (fi.attrib & _A_SUBDIR) ? 1 : 0;
                out[count].size = static_cast<oyk_u32>(fi.size);
                ++count;
            }
        } while (_dos_findnext(&fi) == 0);
    }
#elif defined(OYK_TARGET_IMXRT1060) || defined(TEENSYDUINO) || (defined(__arm__) && !defined(__linux__) && !defined(__unix__))
#if (defined(OYK_HAS_FATFS) && OYK_HAS_FATFS) || defined(TEENSYDUINO)
    void* dh = oyk_imxrt_opendir(dir_path);
    if (!dh) return 0;
    char dname[64];
    int dis_dir = 0;
    oyk_u32 dsz = 0;
    while (count < max_out && oyk_imxrt_readdir(dh, dname, sizeof(dname), &dis_dir, &dsz)) {
        if (!oyk_yapp_is_safe_filename(dname)) continue;
        safe_copy(out[count].name, sizeof(out[count].name), dname);
        out[count].is_dir = dis_dir ? 1 : 0;
        out[count].size = dis_dir ? 0UL : dsz;
        ++count;
    }
    oyk_imxrt_closedir(dh);
#else
    (void)dir_path;
#endif
#else
    DIR* d = opendir(dir_path);
    if (!d) return 0;
    struct dirent* de = NULL;
    while ((de = readdir(d)) != NULL && count < max_out) {
        if (!oyk_yapp_is_safe_filename(de->d_name)) continue;
        char full[240];
        join_path(dir_path, de->d_name, full, sizeof(full));
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) continue; /* Reject symlinks to prevent root escape */
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;
        safe_copy(out[count].name, sizeof(out[count].name), de->d_name);
        out[count].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        out[count].size = S_ISREG(st.st_mode) ? static_cast<oyk_u32>(st.st_size) : 0UL;
        ++count;
    }
    closedir(d);
#endif
    /* Sort: directories first, then alphabetical */
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            int swap = 0;
            if (out[j].is_dir != out[i].is_dir) {
                swap = out[j].is_dir > out[i].is_dir;
            } else if (stricmp_ascii(out[j].name, out[i].name) < 0) {
                swap = 1;
            }
            if (swap) {
                OykDirEnt tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
    return count;
}

static int oyk_is_regular_file(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32) || defined(_WIN64)
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#elif defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
    struct find_t fi;
    if (_dos_findfirst(path, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_SUBDIR, &fi) != 0) return 0;
    return (fi.attrib & _A_SUBDIR) ? 0 : 1;
#elif defined(OYK_TARGET_IMXRT1060) || defined(TEENSYDUINO) || (defined(__arm__) && !defined(__linux__) && !defined(__unix__))
#if (defined(OYK_HAS_FATFS) && OYK_HAS_FATFS) || defined(TEENSYDUINO)
    return oyk_imxrt_is_regular_file(path);
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
#endif
#else
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

static int oyk_is_directory(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32) || defined(_WIN64)
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#elif defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
    struct find_t fi;
    if (_dos_findfirst(path, _A_SUBDIR, &fi) == 0 && (fi.attrib & _A_SUBDIR)) return 1;
    /* Root or existing dir with probe */
    char pat[192];
    join_path(path, "*.*", pat, sizeof(pat));
    return (_dos_findfirst(pat, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_SUBDIR, &fi) == 0) ? 1 : 0;
#elif defined(OYK_TARGET_IMXRT1060) || defined(TEENSYDUINO) || (defined(__arm__) && !defined(__linux__) && !defined(__unix__))
#if (defined(OYK_HAS_FATFS) && OYK_HAS_FATFS) || defined(TEENSYDUINO)
    return oyk_imxrt_is_directory(path);
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
#else
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

static void make_dirs_recursive(const char* path) {
    if (!path || !path[0]) return;
    char tmp[192];
    safe_copy(tmp, sizeof(tmp), path);
    size_t len = strlen(tmp);
    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            if (i == 2 && tmp[1] == ':') continue;
            char saved = tmp[i];
            tmp[i] = '\0';
            make_dir_simple(tmp);
            tmp[i] = saved;
        }
    }
    make_dir_simple(tmp);
}

int oyk_bbs_resolve_safe_path(const char* files_root,
                              const char* cur_rel,
                              const char* user_arg,
                              int allow_parent_in_root,
                              char* abs_out, size_t abs_sz,
                              char* rel_out, size_t rel_sz,
                              char* err_out, size_t err_sz) {
    if (abs_out && abs_sz > 0) abs_out[0] = '\0';
    if (rel_out && rel_sz > 0) rel_out[0] = '\0';
    const char* root = (files_root && files_root[0]) ? files_root : "files";
    const char* arg = user_arg ? user_arg : "";

    for (size_t k = 0; arg[k]; ++k) {
        unsigned char c = static_cast<unsigned char>(arg[k]);
        if (c < 0x20U || c >= 0x7FU) {
            safe_copy(err_out, err_sz, "Access denied: invalid characters in path.");
            return 0;
        }
        if (c == ':') {
            safe_copy(err_out, err_sz, "Access denied: drive specifiers not permitted.");
            return 0;
        }
        if (c == '\\') {
            safe_copy(err_out, err_sz, "Access denied: backslashes not permitted.");
            return 0;
        }
    }

    char stack[8][32];
    int depth = 0;

    char arg_copy[96];
    safe_copy(arg_copy, sizeof(arg_copy), arg);
    trim_inplace(arg_copy);

    if (arg_copy[0] != '/') {
        /* Seed stack from cur_rel */
        const char* p = cur_rel ? cur_rel : "";
        while (*p && depth < 8) {
            while (*p == '/') ++p;
            char seg[32];
            size_t sl = 0;
            while (*p && *p != '/' && sl < sizeof(seg) - 1) seg[sl++] = *p++;
            seg[sl] = '\0';
            if (sl > 0 && strcmp(seg, ".") != 0 && strcmp(seg, "..") != 0) {
                safe_copy(stack[depth++], sizeof(stack[0]), seg);
            }
        }
    }

    const char* p = arg_copy;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        char seg[32];
        size_t sl = 0;
        while (*p && *p != '/' && sl < sizeof(seg) - 1) seg[sl++] = *p++;
        seg[sl] = '\0';
        if (*p && *p != '/') {
            safe_copy(err_out, err_sz, "Access denied: path segment too long.");
            return 0;
        }
        if (strcmp(seg, ".") == 0) {
            continue;
        } else if (strcmp(seg, "..") == 0) {
            if (!allow_parent_in_root) {
                safe_copy(err_out, err_sz, "Access denied: directory traversal is not permitted.");
                return 0;
            }
            if (depth <= 0) {
                safe_copy(err_out, err_sz, "Access denied: cannot navigate above files area root.");
                return 0;
            }
            --depth;
        } else {
            if (!oyk_yapp_is_safe_filename(seg)) {
                safe_copy(err_out, err_sz, "Access denied: invalid path component.");
                return 0;
            }
            if (depth >= 8) {
                safe_copy(err_out, err_sz, "Access denied: path depth limit exceeded.");
                return 0;
            }
            safe_copy(stack[depth++], sizeof(stack[0]), seg);
        }
    }

    char abs_buf[192];
    char rel_buf[96];
    safe_copy(abs_buf, sizeof(abs_buf), root);
    /* Strip trailing slash from root copy */
    size_t rlen = strlen(abs_buf);
    while (rlen > 1 && (abs_buf[rlen - 1] == '/' || abs_buf[rlen - 1] == '\\')) {
        abs_buf[--rlen] = '\0';
    }
    rel_buf[0] = '\0';
    for (int i = 0; i < depth; ++i) {
        char next_abs[192];
        join_path(abs_buf, stack[i], next_abs, sizeof(next_abs));
        safe_copy(abs_buf, sizeof(abs_buf), next_abs);
        if (rel_buf[0] && strlen(rel_buf) + 1 < sizeof(rel_buf)) strcat(rel_buf, "/");
        if (strlen(rel_buf) + strlen(stack[i]) < sizeof(rel_buf) - 1) strcat(rel_buf, stack[i]);
    }

    if (abs_out) safe_copy(abs_out, abs_sz, abs_buf);
    if (rel_out) safe_copy(rel_out, rel_sz, rel_buf);
    return 1;
}

int oyk_bbs_list_incoming(const char* incoming_root,
                          OykPendingIncoming* out, int max_out) {
    if (!out || max_out <= 0) return 0;
    const char* root = (incoming_root && incoming_root[0]) ? incoming_root : "incoming";
    OykDirEnt* dirs = new OykDirEnt[24];
    OykDirEnt* files = new OykDirEnt[24];
    int ndirs = oyk_list_dir_entries(root, dirs, 24);
    int total = 0;
    for (int i = 0; i < ndirs && total < max_out; ++i) {
        if (!dirs[i].is_dir) continue;
        char sub[192];
        join_path(root, dirs[i].name, sub, sizeof(sub));
        int nfiles = oyk_list_dir_entries(sub, files, 24);
        for (int j = 0; j < nfiles && total < max_out; ++j) {
            if (files[j].is_dir) continue;
            safe_copy(out[total].uploader, sizeof(out[total].uploader), dirs[i].name);
            safe_copy(out[total].filename, sizeof(out[total].filename), files[j].name);
            out[total].size = files[j].size;
            ++total;
        }
    }
    delete[] files;
    delete[] dirs;
    return total;
}

int oyk_bbs_approve_incoming_file(const char* incoming_root,
                                  const char* files_root,
                                  const char* uploader,
                                  const char* filename,
                                  const char* dest_subdir,
                                  int allow_overwrite,
                                  char* dest_out, size_t dest_sz,
                                  int* already_exists_out,
                                  char* err_out, size_t err_sz) {
    if (already_exists_out) *already_exists_out = 0;
    if (dest_out && dest_sz > 0) dest_out[0] = '\0';

    if (!oyk_yapp_is_safe_filename(uploader) || !oyk_yapp_is_safe_filename(filename)) {
        safe_copy(err_out, err_sz, "Invalid uploader or filename.");
        return 0;
    }

    const char* inc = (incoming_root && incoming_root[0]) ? incoming_root : "incoming";
    const char* froot = (files_root && files_root[0]) ? files_root : "files";

    char src_dir[160], src_path[192];
    join_path(inc, uploader, src_dir, sizeof(src_dir));
    join_path(src_dir, filename, src_path, sizeof(src_path));
    if (!oyk_is_regular_file(src_path)) {
        safe_copy(err_out, err_sz, "Incoming file not found.");
        return 0;
    }

    char dst_dir[192], rel_dir[96];
    if (!oyk_bbs_resolve_safe_path(froot, "", dest_subdir ? dest_subdir : "", 0,
                                   dst_dir, sizeof(dst_dir), rel_dir, sizeof(rel_dir),
                                   err_out, err_sz)) {
        return 0;
    }
    make_dirs_recursive(dst_dir);

    char dst_path[220];
    join_path(dst_dir, filename, dst_path, sizeof(dst_path));
    if (oyk_is_regular_file(dst_path)) {
        if (!allow_overwrite) {
            if (already_exists_out) *already_exists_out = 1;
            if (dest_out) safe_copy(dest_out, dest_sz, dst_path);
            safe_copy(err_out, err_sz, "File already exists in BBS files area.");
            return 0;
        }
        remove(dst_path);
    }

    if (rename(src_path, dst_path) != 0) {
        FILE* in_fp = fopen(src_path, "rb");
        if (!in_fp) {
            safe_copy(err_out, err_sz, "Failed to open incoming file.");
            return 0;
        }
        FILE* out_fp = fopen(dst_path, "wb");
        if (!out_fp) {
            fclose(in_fp);
            safe_copy(err_out, err_sz, "Failed to create destination file.");
            return 0;
        }
        char buf[512];
        size_t n = 0;
        int ok = 1;
        while ((n = fread(buf, 1, sizeof(buf), in_fp)) > 0) {
            if (fwrite(buf, 1, n, out_fp) != n) { ok = 0; break; }
        }
        fclose(out_fp);
        fclose(in_fp);
        if (!ok) {
            remove(dst_path);
            safe_copy(err_out, err_sz, "Disk write error while moving file.");
            return 0;
        }
        remove(src_path);
    }

    if (dest_out) safe_copy(dest_out, dest_sz, dst_path);
    return 1;
}

int oyk_bbs_delete_incoming_file(const char* incoming_root,
                                 const char* uploader,
                                 const char* filename,
                                 char* err_out, size_t err_sz) {
    if (!oyk_yapp_is_safe_filename(uploader) || !oyk_yapp_is_safe_filename(filename)) {
        safe_copy(err_out, err_sz, "Invalid uploader or filename.");
        return 0;
    }
    const char* inc = (incoming_root && incoming_root[0]) ? incoming_root : "incoming";
    char src_dir[160], src_path[192];
    join_path(inc, uploader, src_dir, sizeof(src_dir));
    join_path(src_dir, filename, src_path, sizeof(src_path));
    if (!oyk_is_regular_file(src_path)) {
        safe_copy(err_out, err_sz, "Incoming file not found.");
        return 0;
    }
    if (remove(src_path) != 0) {
        safe_copy(err_out, err_sz, "Failed to delete incoming file.");
        return 0;
    }
    return 1;
}

static void walk_approved_dir(const char* dir_path, const char* rel_prefix,
                              int depth, OykApprovedFile* out, int max_out, int* total) {
    if (depth > 4 || *total >= max_out) return;
    OykDirEnt* ents = new OykDirEnt[24];
    int n = oyk_list_dir_entries(dir_path, ents, 24);
    for (int i = 0; i < n && *total < max_out; ++i) {
        if (!ents[i].name[0] || strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        char child_abs[192], child_rel[96];
        join_path(dir_path, ents[i].name, child_abs, sizeof(child_abs));
        if (rel_prefix && rel_prefix[0]) {
            if (strlen(rel_prefix) + 1 + strlen(ents[i].name) >= sizeof(child_rel)) continue;
            sprintf(child_rel, "%s/%s", rel_prefix, ents[i].name);
        } else {
            safe_copy(child_rel, sizeof(child_rel), ents[i].name);
        }
        if (ents[i].is_dir) {
            walk_approved_dir(child_abs, child_rel, depth + 1, out, max_out, total);
        } else {
            safe_copy(out[*total].rel_path, sizeof(out[*total].rel_path), child_rel);
            out[*total].size = ents[i].size;
            ++(*total);
        }
    }
    delete[] ents;
}

int oyk_bbs_list_approved(const char* files_root,
                          OykApprovedFile* out, int max_out) {
    if (!out || max_out <= 0) return 0;
    const char* root = (files_root && files_root[0]) ? files_root : "files";
    int total = 0;
    walk_approved_dir(root, "", 0, out, max_out, &total);
    return total;
}

int oyk_bbs_delete_approved_file(const char* files_root,
                                 const char* rel_path,
                                 char* err_out, size_t err_sz) {
    if (!rel_path || !rel_path[0] ||
        strstr(rel_path, "..") != NULL ||
        strchr(rel_path, '\\') != NULL ||
        strchr(rel_path, ':') != NULL) {
        safe_copy(err_out, err_sz, "Access denied: invalid file path.");
        return 0;
    }
    const char* root = (files_root && files_root[0]) ? files_root : "files";
    char cur[192];
    safe_copy(cur, sizeof(cur), root);
    size_t rlen = strlen(cur);
    while (rlen > 1 && (cur[rlen - 1] == '/' || cur[rlen - 1] == '\\')) {
        cur[--rlen] = '\0';
    }

    int segs = 0;
    const char* p = rel_path;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        char seg[40];
        size_t sl = 0;
        while (*p && *p != '/' && sl < sizeof(seg) - 1) seg[sl++] = *p++;
        seg[sl] = '\0';
        if (sl == 0 || strcmp(seg, ".") == 0 || strcmp(seg, "..") == 0) {
            safe_copy(err_out, err_sz, "Access denied: invalid path component.");
            return 0;
        }
        char next_p[192];
        join_path(cur, seg, next_p, sizeof(next_p));
        safe_copy(cur, sizeof(cur), next_p);
        ++segs;
    }
    if (segs == 0 || !oyk_is_regular_file(cur)) {
        safe_copy(err_out, err_sz, "Approved file not found.");
        return 0;
    }
    if (remove(cur) != 0) {
        safe_copy(err_out, err_sz, "Failed to remove approved file.");
        return 0;
    }
    return 1;
}

int OykPortableNode::bbs_yapp_send_trampoline(const oyk_u8* data, size_t len, void* ctx) {
    OykPortableNode* self = static_cast<OykPortableNode*>(ctx);
    if (!self || !self->bbs_yapp_tx_sess_ || !data || len == 0) return 0;
    self->send_raw_bytes(self->bbs_yapp_tx_sess_, data, len);
    return 1;
}

static int portable_bbs_yapp_no_overwrite_cb(const char* /*target*/, const char* /*fname*/, void* /*ctx*/) {
    return 0;
}

static void bbs_base_call(const char* in, char* out, size_t out_sz) {
    size_t j = 0;
    if (!in || !out || out_sz == 0) return;
    while (*in && isspace(static_cast<unsigned char>(*in))) ++in;
    while (*in && *in != '-' && !isspace(static_cast<unsigned char>(*in)) && j < out_sz - 1) {
        out[j++] = static_cast<char>(toupper(static_cast<unsigned char>(*in)));
        ++in;
    }
    out[j] = '\0';
}

static void bbs_sanitize_tok(const char* in, char* out, size_t out_sz) {
    size_t j = 0;
    if (!in || !out || out_sz == 0) return;
    while (*in && j < out_sz - 1) {
        unsigned char c = static_cast<unsigned char>(*in);
        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            out[j++] = static_cast<char>(toupper(c));
        }
        ++in;
    }
    out[j] = '\0';
}

static void bbs_sanitize_text(const char* in, char* out, size_t out_sz) {
    size_t j = 0;
    if (!in || !out || out_sz == 0) return;
    while (*in && j < out_sz - 1) {
        unsigned char c = static_cast<unsigned char>(*in);
        if (c >= 0x20U && c <= 0x7EU && c != '|') {
            out[j++] = static_cast<char>(c);
        }
        ++in;
    }
    out[j] = '\0';
}

static int bbs_call_matches(const char* target, const OykSession* sess) {
    if (!target || !target[0] || !sess) return 0;
    char full_s[16], base_s[12], t_base[12];
    oyk_format_callsign(&sess->remote_call, full_s, sizeof(full_s));
    bbs_base_call(full_s, base_s, sizeof(base_s));
    if (stricmp_ascii(target, full_s) == 0 || stricmp_ascii(target, base_s) == 0) return 1;
    if (strchr(target, '-') == NULL) {
        bbs_base_call(target, t_base, sizeof(t_base));
        if (stricmp_ascii(t_base, base_s) == 0) return 1;
    }
    return 0;
}

static int bbs_contains_ci(const char* hay, const char* needle) {
    if (!hay || !needle || !needle[0]) return 0;
    size_t nlen = strlen(needle);
    for (size_t i = 0; hay[i]; ++i) {
        size_t j = 0;
        while (j < nlen && hay[i + j] &&
               toupper(static_cast<unsigned char>(hay[i + j])) ==
               toupper(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

int OykPortableNode::bbs_is_sysop(const OykSession* sess) const {
    if (!sess) return 0;
    if (sess->is_local_loopback) return 1;
    return (stricmp_ascii(sess->remote_call.base, cfg_.callsign.base) == 0) ? 1 : 0;
}

int OykPortableNode::bbs_can_read(const OykBbsMsgMeta* m, const OykSession* sess) const {
    if (!m || !sess || m->status == 'K') return 0;
    if (m->type != 'P') return 1;
    if (bbs_is_sysop(sess)) return 1;
    return (bbs_call_matches(m->to, sess) || bbs_call_matches(m->from, sess)) ? 1 : 0;
}

int OykPortableNode::bbs_can_kill(const OykBbsMsgMeta* m, const OykSession* sess) const {
    if (!m || !sess || m->status == 'K') return 0;
    if (bbs_is_sysop(sess)) return 1;
    if (bbs_call_matches(m->from, sess)) return 1;
    if (m->type == 'P' && bbs_call_matches(m->to, sess)) return 1;
    return 0;
}

int OykPortableNode::bbs_unread_for(const char* base_call) const {
    if (!base_call || !base_call[0]) return 0;
    int n = 0;
    for (int i = 0; i < bbs_msg_count_; ++i) {
        const OykBbsMsgMeta& m = bbs_msgs_[i];
        if (m.type == 'P' && m.status == 'N') {
            char tb[12];
            bbs_base_call(m.to, tb, sizeof(tb));
            if (stricmp_ascii(tb, base_call) == 0) ++n;
        }
    }
    return n;
}

void OykPortableNode::bbs_load_index() {
    bbs_msg_count_ = 0;
    bbs_next_id_ = 1;
    char idx_path[160];
    join_path(cfg_.bbs_mail_path[0] ? cfg_.bbs_mail_path : "mail", "BBS.IDX",
              idx_path, sizeof(idx_path));
    FILE* fp = fopen(idx_path, "rb");
    if (!fp) return;

    char line[220];
    while (fgets(line, static_cast<int>(sizeof(line)), fp) != NULL) {
        trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;
        /* Format: id|type|status|from|to|at|date|size|subject */
        char* parts[9];
        int pc = 0;
        char* p = line;
        parts[pc++] = p;
        while (*p && pc < 9) {
            if (*p == '|') {
                *p = '\0';
                parts[pc++] = p + 1;
            }
            ++p;
        }
        if (pc < 9) continue;
        oyk_u32 id = static_cast<oyk_u32>(atol(parts[0]));
        if (id == 0) continue;
        if (id >= bbs_next_id_) bbs_next_id_ = id + 1;
        char st = parts[2][0] ? parts[2][0] : 'N';
        if (st == 'K') continue;
        if (bbs_msg_count_ < OYK_MAX_BBS_MSGS) {
            OykBbsMsgMeta* m = &bbs_msgs_[bbs_msg_count_++];
            memset(m, 0, sizeof(*m));
            m->id = id;
            m->type = parts[1][0] ? parts[1][0] : 'P';
            m->status = st;
            safe_copy(m->from, sizeof(m->from), parts[3]);
            safe_copy(m->to, sizeof(m->to), parts[4]);
            safe_copy(m->at_bbs, sizeof(m->at_bbs), parts[5]);
            safe_copy(m->date, sizeof(m->date), parts[6]);
            m->size = static_cast<oyk_u16>(atol(parts[7]));
            safe_copy(m->subject, sizeof(m->subject), parts[8]);
        }
    }
    fclose(fp);
}

int OykPortableNode::bbs_save_index() const {
    const char* dir = cfg_.bbs_mail_path[0] ? cfg_.bbs_mail_path : "mail";
    make_dir_simple(dir);
    char idx_path[160];
    join_path(dir, "BBS.IDX", idx_path, sizeof(idx_path));
    FILE* fp = fopen(idx_path, "wb");
    if (!fp) return 0;
    for (int i = 0; i < bbs_msg_count_; ++i) {
        const OykBbsMsgMeta& m = bbs_msgs_[i];
        fprintf(fp, "%lu|%c|%c|%s|%s|%s|%s|%u|%s\n",
                static_cast<unsigned long>(m.id),
                m.type, m.status, m.from, m.to, m.at_bbs, m.date,
                static_cast<unsigned int>(m.size), m.subject);
    }
    fclose(fp);
    return 1;
}

int OykPortableNode::bbs_save_msg_file(const OykBbsMsgMeta* meta, const char* body) const {
    if (!meta) return 0;
    const char* dir = cfg_.bbs_mail_path[0] ? cfg_.bbs_mail_path : "mail";
    make_dir_simple(dir);
    char fname[24], full[160];
    sprintf(fname, "M%lu.MSG", static_cast<unsigned long>(meta->id));
    join_path(dir, fname, full, sizeof(full));
    FILE* fp = fopen(full, "wb");
    if (!fp) return 0;
    fprintf(fp, "ID: %lu\nTYPE: %c\nSTATUS: %c\nFROM: %s\nTO: %s\nAT: %s\nDATE: %s\nSUBJECT: %s\n---\n",
            static_cast<unsigned long>(meta->id),
            meta->type, meta->status, meta->from, meta->to, meta->at_bbs,
            meta->date, meta->subject);
    if (body && body[0]) {
        fputs(body, fp);
    }
    fclose(fp);
    return 1;
}

void OykPortableNode::bbs_send_prompt(OykSession* sess) {
    OykCallsign bbs_c;
    oyk_config_resolved_bbs_callsign(&cfg_, &bbs_c);
    char bs[16], p[32];
    oyk_format_callsign(&bbs_c, bs, sizeof(bs));
    sprintf(p, "de %s> ", bs);
    send_text(sess, p);
}

void OykPortableNode::bbs_send_msg(OykSession* sess, OykBbsMsgMeta* m) {
    if (!sess || !m) return;
    char hdr[320];
    sprintf(hdr,
            "------------------------------------------------------------------\r"
            "Msg #%lu [%c%c] Date: %s\r"
            "From: %s  To: %s%s%s\r"
            "Subj: %s\r"
            "------------------------------------------------------------------\r",
            static_cast<unsigned long>(m->id), m->type, m->status, m->date,
            m->from, m->to, m->at_bbs[0] ? " @ " : "", m->at_bbs, m->subject);
    send_text(sess, hdr);

    const char* dir = cfg_.bbs_mail_path[0] ? cfg_.bbs_mail_path : "mail";
    char fname[24], full[160];
    sprintf(fname, "M%lu.MSG", static_cast<unsigned long>(m->id));
    join_path(dir, fname, full, sizeof(full));
    FILE* fp = fopen(full, "rb");
    if (fp) {
        char line[180];
        int in_body = 0;
        while (fgets(line, static_cast<int>(sizeof(line)), fp) != NULL) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            if (!in_body) {
                if (strcmp(line, "---") == 0) in_body = 1;
                continue;
            }
            if (l + 2 < sizeof(line)) {
                line[l] = '\r';
                line[l + 1] = '\0';
            }
            send_text(sess, line);
        }
        fclose(fp);
    }
    send_text(sess, "------------------------------------------------------------------\r");

    if (m->type == 'P' && m->status == 'N' && bbs_call_matches(m->to, sess)) {
        m->status = 'Y';
        bbs_save_index();
    }
}

void OykPortableNode::bbs_enter(OykSession* sess, int direct_connect) {
    if (!sess) return;
    sess->in_bbs = 1;
    sess->bbs_direct = direct_connect ? 1 : 0;
    sess->bbs_stage = 0;
    sess->bbs_draft_len = 0;
    sess->bbs_draft_body[0] = '\0';
    sess->bbs_files_rel[0] = '\0';
    sess->bbs_yapp_active = 0;
    sess->bbs_yapp_upload = 0;

    OykCallsign bbs_c;
    oyk_config_resolved_bbs_callsign(&cfg_, &bbs_c);
    char bs[16], us[16], ub[12];
    oyk_format_callsign(&bbs_c, bs, sizeof(bs));
    oyk_format_callsign(&sess->remote_call, us, sizeof(us));
    bbs_base_call(us, ub, sizeof(ub));

    int active = 0;
    for (int i = 0; i < bbs_msg_count_; ++i) {
        if (bbs_msgs_[i].status != 'K') ++active;
    }
    int unread = bbs_unread_for(ub);

    char msg[260];
    sprintf(msg,
            "[OYK-0.1-HMR$]\r*** %s Packet BBS ***\r%s\r"
            "Hello %s. Active msgs: %d | Unread personal: %d\r",
            bs, cfg_.bbs_welcome, ub, active, unread);
    send_text(sess, msg);
    if (unread > 0) {
        send_text(sess, "*** You have unread personal mail. Type LN or RM to read.\r");
    }
    bbs_send_prompt(sess);
}

void OykPortableNode::handle_bbs_line(OykSession* sess, const char* raw_line) {
    if (!sess || !sess->in_bbs) return;

    char line[160];
    safe_copy(line, sizeof(line), raw_line);

    if (sess->bbs_stage == 1) {
        trim_inplace(line);
        if (stricmp_ascii(line, "/AB") == 0 || stricmp_ascii(line, "/ABORT") == 0) {
            sess->bbs_stage = 0;
            send_text(sess, "Message aborted.\r");
            bbs_send_prompt(sess);
            return;
        }
        bbs_sanitize_text(line, sess->bbs_draft_subj, sizeof(sess->bbs_draft_subj));
        if (!sess->bbs_draft_subj[0]) safe_copy(sess->bbs_draft_subj, sizeof(sess->bbs_draft_subj), "(no subject)");
        sess->bbs_draft_len = 0;
        sess->bbs_draft_body[0] = '\0';
        sess->bbs_stage = 2;
        send_text(sess, "Enter message text. End with /EX or Ctrl-Z on a new line (/AB to abort):\r");
        return;
    }

    if (sess->bbs_stage == 2) {
        char trimmed[160];
        safe_copy(trimmed, sizeof(trimmed), line);
        trim_inplace(trimmed);
        if (stricmp_ascii(trimmed, "/AB") == 0 || stricmp_ascii(trimmed, "/ABORT") == 0) {
            sess->bbs_stage = 0;
            sess->bbs_draft_len = 0;
            sess->bbs_draft_body[0] = '\0';
            send_text(sess, "Message aborted.\r");
            bbs_send_prompt(sess);
            return;
        }
        if (stricmp_ascii(trimmed, "/EX") == 0 || strcmp(trimmed, ".") == 0) {
            OykBbsMsgMeta meta;
            memset(&meta, 0, sizeof(meta));
            meta.id = bbs_next_id_++;
            meta.type = sess->bbs_draft_type;
            meta.status = (meta.type == 'B') ? '$' : 'N';
            char us[16];
            oyk_format_callsign(&sess->remote_call, us, sizeof(us));
            bbs_base_call(us, meta.from, sizeof(meta.from));
            safe_copy(meta.to, sizeof(meta.to), sess->bbs_draft_to);
            safe_copy(meta.at_bbs, sizeof(meta.at_bbs), sess->bbs_draft_at);
            oyk_format_now_compact(now_time(), meta.date, sizeof(meta.date));
            safe_copy(meta.subject, sizeof(meta.subject), sess->bbs_draft_subj);
            meta.size = sess->bbs_draft_len;

            int slot = -1;
            if (bbs_msg_count_ < OYK_MAX_BBS_MSGS) {
                slot = bbs_msg_count_++;
            } else {
                for (int i = 0; i < bbs_msg_count_; ++i) {
                    if (bbs_msgs_[i].status == 'K') { slot = i; break; }
                }
                if (slot < 0) {
                    for (int i = 1; i < OYK_MAX_BBS_MSGS; ++i) bbs_msgs_[i - 1] = bbs_msgs_[i];
                    slot = OYK_MAX_BBS_MSGS - 1;
                }
            }
            bbs_msgs_[slot] = meta;
            bbs_save_msg_file(&meta, sess->bbs_draft_body);
            bbs_save_index();

            sess->bbs_stage = 0;
            sess->bbs_draft_len = 0;
            sess->bbs_draft_body[0] = '\0';

            char resp[96];
            sprintf(resp, "Message #%lu saved (%u bytes) for %s.\r",
                    static_cast<unsigned long>(meta.id),
                    static_cast<unsigned int>(meta.size), meta.to);
            send_text(sess, resp);
            bbs_send_prompt(sess);
            return;
        }
        oyk_u16 max_b = cfg_.bbs_max_msg_bytes;
        if (max_b == 0 || max_b >= OYK_MAX_BBS_BODY) max_b = static_cast<oyk_u16>(OYK_MAX_BBS_BODY - 1);
        size_t llen = strlen(line);
        if (static_cast<size_t>(sess->bbs_draft_len) + llen + 1 > static_cast<size_t>(max_b)) {
            send_text(sess, "*** Max message size reached. Type /EX to send or /AB to abort.\r");
            return;
        }
        memcpy(sess->bbs_draft_body + sess->bbs_draft_len, line, llen);
        sess->bbs_draft_len = static_cast<oyk_u16>(sess->bbs_draft_len + llen);
        sess->bbs_draft_body[sess->bbs_draft_len++] = '\n';
        sess->bbs_draft_body[sess->bbs_draft_len] = '\0';
        return;
    }

    trim_inplace(line);
    if (!line[0]) {
        bbs_send_prompt(sess);
        return;
    }

    char cmd[16];
    size_t ci = 0;
    while (line[ci] && !isspace(static_cast<unsigned char>(line[ci])) && ci < sizeof(cmd) - 1) {
        cmd[ci] = static_cast<char>(toupper(static_cast<unsigned char>(line[ci])));
        ++ci;
    }
    cmd[ci] = '\0';
    const char* arg = line + ci;
    while (*arg && isspace(static_cast<unsigned char>(*arg))) ++arg;

    if (strcmp(cmd, "H") == 0 || strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        if (cfg_.bbs_files_enabled) {
            send_text(sess,
                      "BBS Commands: L/LL, LM/LN, LB/LP, L>/L</L@/LC/LS, R <n>, RM, "
                      "SP/SB/ST <to>, SR <n>, K <n>, KM, FILES/DIR, CD, PWD, TYPE, GET, UPLOAD, I, B\r");
        } else {
            send_text(sess,
                      "BBS Commands: L/LL, LM/LN, LB/LP, L>/L</L@/LC/LS, R <n>, RM, "
                      "SP/SB/ST <to>, SR <n>, K <n>, KM, I, B (Files Area disabled)\r");
        }
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "I") == 0 || strcmp(cmd, "INFO") == 0 || strcmp(cmd, "STAT") == 0) {
        int active = 0, pers = 0, bull = 0;
        for (int i = 0; i < bbs_msg_count_; ++i) {
            if (bbs_msgs_[i].status == 'K') continue;
            ++active;
            if (bbs_msgs_[i].type == 'P') ++pers;
            else if (bbs_msgs_[i].type == 'B') ++bull;
        }
        char us[16], ub[12], resp[160];
        oyk_format_callsign(&sess->remote_call, us, sizeof(us));
        bbs_base_call(us, ub, sizeof(ub));
        sprintf(resp, "OYK Packet BBS [OYK-0.1-HMR$] Active:%d (P:%d B:%d) Unread:%d Files:%s\r",
                active, pers, bull, bbs_unread_for(ub),
                cfg_.bbs_files_enabled ? "ON" : "OFF");
        send_text(sess, resp);
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "B") == 0 || strcmp(cmd, "BYE") == 0 ||
        strcmp(cmd, "Q") == 0 || strcmp(cmd, "QUIT") == 0) {
        int direct = sess->bbs_direct;
        sess->in_bbs = 0;
        sess->bbs_stage = 0;
        if (direct) {
            OykCallsign bbs_c;
            oyk_config_resolved_bbs_callsign(&cfg_, &bbs_c);
            char bs[16], bye_s[40];
            oyk_format_callsign(&bbs_c, bs, sizeof(bs));
            sprintf(bye_s, "73 de %s\r", bs);
            send_text(sess, bye_s);
            send_disc(sess);
            sess->state = OYK_SESS_FREE;
        } else {
            char ns[16], ret_s[64];
            oyk_format_callsign(&cfg_.callsign, ns, sizeof(ns));
            sprintf(ret_s, "*** Leaving BBS. Returning to node.\r%s> ", ns);
            send_text(sess, ret_s);
        }
        return;
    }

    if (strcmp(cmd, "L") == 0 || strcmp(cmd, "LL") == 0 || strcmp(cmd, "LM") == 0 ||
        strcmp(cmd, "LN") == 0 || strcmp(cmd, "LB") == 0 || strcmp(cmd, "LP") == 0 ||
        strcmp(cmd, "L>") == 0 || strcmp(cmd, "L<") == 0 || strcmp(cmd, "L@") == 0 ||
        strcmp(cmd, "LS") == 0 || strcmp(cmd, "LC") == 0) {
        if (strcmp(cmd, "LC") == 0) {
            int any = 0;
            for (int i = 0; i < bbs_msg_count_; ++i) {
                if (bbs_msgs_[i].status == 'K' || bbs_msgs_[i].type != 'B') continue;
                int dup = 0;
                for (int p = 0; p < i; ++p) {
                    if (bbs_msgs_[p].status != 'K' && bbs_msgs_[p].type == 'B' &&
                        stricmp_ascii(bbs_msgs_[p].to, bbs_msgs_[i].to) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (dup) continue;
                int cnt = 0;
                for (int q = i; q < bbs_msg_count_; ++q) {
                    if (bbs_msgs_[q].status != 'K' && bbs_msgs_[q].type == 'B' &&
                        stricmp_ascii(bbs_msgs_[q].to, bbs_msgs_[i].to) == 0) ++cnt;
                }
                char row[48];
                sprintf(row, "  %-10s (%d)\r", bbs_msgs_[i].to, cnt);
                send_text(sess, row);
                any = 1;
            }
            if (!any) send_text(sess, "No bulletin categories.\r");
            bbs_send_prompt(sess);
            return;
        }
        int limit = 20;
        if (strcmp(cmd, "LL") == 0 && arg[0]) {
            int lv = atoi(arg);
            if (lv > 0) limit = lv;
        }
        int shown = 0;
        for (int i = bbs_msg_count_ - 1; i >= 0 && shown < limit; --i) {
            const OykBbsMsgMeta& m = bbs_msgs_[i];
            if (!bbs_can_read(&m, sess)) continue;
            int keep = 0;
            if (strcmp(cmd, "L") == 0 || strcmp(cmd, "LL") == 0) keep = 1;
            else if (strcmp(cmd, "LM") == 0) keep = bbs_call_matches(m.to, sess);
            else if (strcmp(cmd, "LN") == 0) keep = (m.type == 'P' && m.status == 'N' && bbs_call_matches(m.to, sess));
            else if (strcmp(cmd, "LB") == 0) keep = (m.type == 'B');
            else if (strcmp(cmd, "LP") == 0) keep = (m.type == 'P');
            else if (strcmp(cmd, "L>") == 0) keep = (arg[0] && stricmp_ascii(m.to, arg) == 0);
            else if (strcmp(cmd, "L<") == 0) keep = (arg[0] && stricmp_ascii(m.from, arg) == 0);
            else if (strcmp(cmd, "L@") == 0) keep = (arg[0] && stricmp_ascii(m.at_bbs, arg) == 0);
            else if (strcmp(cmd, "LS") == 0) keep = (arg[0] && (bbs_contains_ci(m.subject, arg) ||
                                                                 bbs_contains_ci(m.to, arg) ||
                                                                 bbs_contains_ci(m.from, arg)));
            if (keep) {
                if (shown == 0) {
                    send_text(sess, "Msg#  TS Size  To       From     Subject\r");
                }
                char row[96];
                sprintf(row, "%-5lu %c%c %5u %-8.8s %-8.8s %.32s\r",
                        static_cast<unsigned long>(m.id), m.type, m.status,
                        static_cast<unsigned int>(m.size), m.to, m.from, m.subject);
                send_text(sess, row);
                ++shown;
            }
        }
        if (shown == 0) send_text(sess, "No matching messages.\r");
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "RM") == 0 || strcmp(cmd, "RN") == 0) {
        int cnt = 0;
        for (int i = 0; i < bbs_msg_count_; ++i) {
            if (bbs_msgs_[i].type == 'P' && bbs_msgs_[i].status == 'N' &&
                bbs_call_matches(bbs_msgs_[i].to, sess)) {
                bbs_send_msg(sess, &bbs_msgs_[i]);
                ++cnt;
            }
        }
        if (cnt == 0) send_text(sess, "No unread personal messages.\r");
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "R") == 0 || strcmp(cmd, "READ") == 0) {
        oyk_u32 mid = static_cast<oyk_u32>(atol(arg));
        int found = 0;
        for (int i = 0; i < bbs_msg_count_; ++i) {
            if (bbs_msgs_[i].id == mid && bbs_can_read(&bbs_msgs_[i], sess)) {
                bbs_send_msg(sess, &bbs_msgs_[i]);
                found = 1;
                break;
            }
        }
        if (!found) send_text(sess, "Message not found or access denied.\r");
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "SP") == 0 || strcmp(cmd, "S") == 0 ||
        strcmp(cmd, "SB") == 0 || strcmp(cmd, "ST") == 0) {
        if (!arg[0]) {
            send_text(sess, "Usage: SP/SB/ST <to> [@ <bbs>]\r");
            bbs_send_prompt(sess);
            return;
        }
        char to_tok[24], at_tok[24];
        to_tok[0] = '\0';
        at_tok[0] = '\0';
        const char* p = arg;
        size_t ti = 0;
        while (*p && !isspace(static_cast<unsigned char>(*p)) && *p != '@' && ti < sizeof(to_tok) - 1) {
            to_tok[ti++] = *p++;
        }
        to_tok[ti] = '\0';
        while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
        if (*p == '@') {
            ++p;
            while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
            size_t ai = 0;
            while (*p && !isspace(static_cast<unsigned char>(*p)) && ai < sizeof(at_tok) - 1) {
                at_tok[ai++] = *p++;
            }
            at_tok[ai] = '\0';
        }
        bbs_sanitize_tok(to_tok, sess->bbs_draft_to, sizeof(sess->bbs_draft_to));
        bbs_sanitize_tok(at_tok, sess->bbs_draft_at, sizeof(sess->bbs_draft_at));
        if (!sess->bbs_draft_to[0]) {
            send_text(sess, "Invalid recipient.\r");
            bbs_send_prompt(sess);
            return;
        }
        sess->bbs_draft_type = (cmd[1] == 'B') ? 'B' : ((cmd[1] == 'T') ? 'T' : 'P');
        sess->bbs_stage = 1;
        send_text(sess, "Enter title (subject) (/AB to abort): ");
        return;
    }

    if (strcmp(cmd, "SR") == 0) {
        oyk_u32 mid = static_cast<oyk_u32>(atol(arg));
        int idx = -1;
        for (int i = 0; i < bbs_msg_count_; ++i) {
            if (bbs_msgs_[i].id == mid && bbs_can_read(&bbs_msgs_[i], sess)) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            send_text(sess, "Message not found.\r");
            bbs_send_prompt(sess);
            return;
        }
        sess->bbs_draft_type = 'P';
        safe_copy(sess->bbs_draft_to, sizeof(sess->bbs_draft_to), bbs_msgs_[idx].from);
        sess->bbs_draft_at[0] = '\0';
        if (bbs_msgs_[idx].subject[0] &&
            toupper(static_cast<unsigned char>(bbs_msgs_[idx].subject[0])) == 'R' &&
            toupper(static_cast<unsigned char>(bbs_msgs_[idx].subject[1])) == 'E' &&
            bbs_msgs_[idx].subject[2] == ':') {
            safe_copy(sess->bbs_draft_subj, sizeof(sess->bbs_draft_subj), bbs_msgs_[idx].subject);
        } else {
            sprintf(sess->bbs_draft_subj, "Re: %.43s", bbs_msgs_[idx].subject);
        }
        sess->bbs_draft_len = 0;
        sess->bbs_draft_body[0] = '\0';
        sess->bbs_stage = 2;
        send_text(sess, "Enter reply text. End with /EX or Ctrl-Z (/AB to abort):\r");
        return;
    }

    if (strcmp(cmd, "KM") == 0) {
        int killed = 0;
        for (int i = 0; i < bbs_msg_count_; ++i) {
            if (bbs_msgs_[i].type == 'P' && bbs_msgs_[i].status == 'Y' &&
                bbs_call_matches(bbs_msgs_[i].to, sess)) {
                bbs_msgs_[i].status = 'K';
                ++killed;
            }
        }
        if (killed > 0) bbs_save_index();
        char resp[64];
        sprintf(resp, "Killed %d read personal message(s).\r", killed);
        send_text(sess, resp);
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "K") == 0 || strcmp(cmd, "KILL") == 0) {
        oyk_u32 mid = static_cast<oyk_u32>(atol(arg));
        int found = 0;
        for (int i = 0; i < bbs_msg_count_; ++i) {
            if (bbs_msgs_[i].id == mid && bbs_msgs_[i].status != 'K') {
                found = 1;
                if (!bbs_can_kill(&bbs_msgs_[i], sess)) {
                    send_text(sess, "Not authorized to kill message.\r");
                } else {
                    bbs_msgs_[i].status = 'K';
                    bbs_save_index();
                    send_text(sess, "Message killed.\r");
                }
                break;
            }
        }
        if (!found) send_text(sess, "Message not found.\r");
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "F") == 0 || strcmp(cmd, "FILES") == 0 || strcmp(cmd, "DIR") == 0 ||
        strcmp(cmd, "PWD") == 0 || strcmp(cmd, "CD") == 0 ||
        strcmp(cmd, "TYPE") == 0 || strcmp(cmd, "CAT") == 0 ||
        strcmp(cmd, "D") == 0 || strcmp(cmd, "GET") == 0 ||
        strcmp(cmd, "DOWNLOAD") == 0 || strcmp(cmd, "YAPP") == 0 ||
        strcmp(cmd, "U") == 0 || strcmp(cmd, "UPLOAD") == 0 || strcmp(cmd, "YU") == 0) {
        if (!cfg_.bbs_files_enabled) {
            send_text(sess, "*** BBS Files Area is currently disabled.\r");
            bbs_send_prompt(sess);
            return;
        }
    }

    if (strcmp(cmd, "F") == 0 || strcmp(cmd, "FILES") == 0 || strcmp(cmd, "DIR") == 0) {
        char abs_dir[192], rel_dir[96], perr[80];
        if (!oyk_bbs_resolve_safe_path(cfg_.bbs_files_path, sess->bbs_files_rel, arg, 0,
                                       abs_dir, sizeof(abs_dir), rel_dir, sizeof(rel_dir),
                                       perr, sizeof(perr))) {
            send_text(sess, perr);
            send_text(sess, "\r");
            bbs_send_prompt(sess);
            return;
        }
        char hdr[120];
        sprintf(hdr, "Files Area: /%s\r", rel_dir);
        send_text(sess, hdr);
        OykDirEnt* ents = new OykDirEnt[32];
        int cnt = oyk_list_dir_entries(abs_dir, ents, 32);
        if (cnt == 0) {
            send_text(sess, "(no files in this directory)\r");
        } else {
            send_text(sess, "Name                     Size\r");
            for (int i = 0; i < cnt; ++i) {
                char row[64];
                if (ents[i].is_dir) {
                    sprintf(row, "%-24.24s <DIR>\r", ents[i].name);
                } else {
                    sprintf(row, "%-24.24s %lu\r", ents[i].name, static_cast<unsigned long>(ents[i].size));
                }
                send_text(sess, row);
            }
        }
        delete[] ents;
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "PWD") == 0) {
        char msg[96];
        sprintf(msg, "Files Area: /%s\r", sess->bbs_files_rel);
        send_text(sess, msg);
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "CD") == 0) {
        if (!arg[0] || strcmp(arg, "/") == 0) {
            sess->bbs_files_rel[0] = '\0';
            send_text(sess, "Files Area: /\r");
            bbs_send_prompt(sess);
            return;
        }
        char abs_dir[192], rel_dir[64], perr[80];
        if (!oyk_bbs_resolve_safe_path(cfg_.bbs_files_path, sess->bbs_files_rel, arg, 1,
                                       abs_dir, sizeof(abs_dir), rel_dir, sizeof(rel_dir),
                                       perr, sizeof(perr))) {
            send_text(sess, perr);
            send_text(sess, "\r");
            bbs_send_prompt(sess);
            return;
        }
        if (!oyk_is_directory(abs_dir)) {
            send_text(sess, "Directory not found.\r");
            bbs_send_prompt(sess);
            return;
        }
        safe_copy(sess->bbs_files_rel, sizeof(sess->bbs_files_rel), rel_dir);
        char msg[96];
        sprintf(msg, "Files Area: /%s\r", sess->bbs_files_rel);
        send_text(sess, msg);
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "TYPE") == 0 || strcmp(cmd, "CAT") == 0) {
        if (!arg[0]) {
            send_text(sess, "Usage: TYPE <filename>\r");
            bbs_send_prompt(sess);
            return;
        }
        char abs_f[192], rel_f[96], perr[80];
        if (!oyk_bbs_resolve_safe_path(cfg_.bbs_files_path, sess->bbs_files_rel, arg, 0,
                                       abs_f, sizeof(abs_f), rel_f, sizeof(rel_f),
                                       perr, sizeof(perr))) {
            send_text(sess, perr);
            send_text(sess, "\r");
            bbs_send_prompt(sess);
            return;
        }
        if (!oyk_is_regular_file(abs_f)) {
            send_text(sess, "File not found.\r");
            bbs_send_prompt(sess);
            return;
        }
        FILE* fp = fopen(abs_f, "rb");
        if (!fp) {
            send_text(sess, "Unable to open file.\r");
            bbs_send_prompt(sess);
            return;
        }
        char out_buf[160];
        size_t out_len = 0;
        oyk_u32 shown = 0;
        int c = 0;
        while ((c = fgetc(fp)) != EOF && shown < 4096UL) {
            if (c == '\n') {
                out_buf[out_len++] = '\r';
                ++shown;
            } else if (c != '\r' && (c == '\t' || (c >= 0x20 && c <= 0x7E))) {
                out_buf[out_len++] = static_cast<char>(c);
                ++shown;
            }
            if (out_len >= sizeof(out_buf) - 2) {
                out_buf[out_len] = '\0';
                send_text(sess, out_buf);
                out_len = 0;
            }
        }
        if (out_len > 0) {
            out_buf[out_len] = '\0';
            send_text(sess, out_buf);
        }
        fclose(fp);
        send_text(sess, "\r--- EOF ---\r");
        bbs_send_prompt(sess);
        return;
    }

    if (strcmp(cmd, "D") == 0 || strcmp(cmd, "GET") == 0 ||
        strcmp(cmd, "DOWNLOAD") == 0 || strcmp(cmd, "YAPP") == 0) {
        if (!arg[0]) {
            send_text(sess, "Usage: GET <filename>\r");
            bbs_send_prompt(sess);
            return;
        }
        if (sess->is_local_loopback) {
            send_text(sess, "*** YAPP file transfer is disallowed when both sides are local.\r");
            bbs_send_prompt(sess);
            return;
        }
        if (bbs_yapp_sid_ != 0 && bbs_yapp_sid_ != sess->id) {
            send_text(sess, "*** BBS YAPP channel busy; try again shortly.\r");
            bbs_send_prompt(sess);
            return;
        }
        char abs_f[192], rel_f[96], perr[80];
        if (!oyk_bbs_resolve_safe_path(cfg_.bbs_files_path, sess->bbs_files_rel, arg, 0,
                                       abs_f, sizeof(abs_f), rel_f, sizeof(rel_f),
                                       perr, sizeof(perr))) {
            send_text(sess, perr);
            send_text(sess, "\r");
            bbs_send_prompt(sess);
            return;
        }
        if (!oyk_is_regular_file(abs_f)) {
            send_text(sess, "File not found.\r");
            bbs_send_prompt(sess);
            return;
        }
        if (!bbs_yapp_) bbs_yapp_ = new OykYappSession();
        bbs_yapp_tx_sess_ = sess;
        oyk_yapp_init(bbs_yapp_, "incoming",
                      OykPortableNode::bbs_yapp_send_trampoline, this,
                      portable_bbs_yapp_no_overwrite_cb, this);
        oyk_yapp_set_remote(bbs_yapp_, &sess->remote_call);
        char yerr[80];
        if (!oyk_yapp_start_send(bbs_yapp_, abs_f, yerr, sizeof(yerr))) {
            bbs_yapp_tx_sess_ = NULL;
            send_text(sess, "*** YAPP error: ");
            send_text(sess, yerr);
            send_text(sess, "\r");
            bbs_send_prompt(sess);
            return;
        }
        bbs_yapp_sid_ = sess->id;
        sess->bbs_yapp_active = 1;
        sess->bbs_yapp_upload = 0;
        return;
    }

    if (strcmp(cmd, "U") == 0 || strcmp(cmd, "UPLOAD") == 0 || strcmp(cmd, "YU") == 0) {
        if (sess->is_local_loopback) {
            send_text(sess, "*** YAPP file transfer is disallowed when both sides are local.\r");
            bbs_send_prompt(sess);
            return;
        }
        if (bbs_yapp_sid_ != 0 && bbs_yapp_sid_ != sess->id) {
            send_text(sess, "*** BBS YAPP channel busy; try again shortly.\r");
            bbs_send_prompt(sess);
            return;
        }
        if (!bbs_yapp_) bbs_yapp_ = new OykYappSession();
        bbs_yapp_tx_sess_ = sess;
        oyk_yapp_init(bbs_yapp_, "incoming",
                      OykPortableNode::bbs_yapp_send_trampoline, this,
                      portable_bbs_yapp_no_overwrite_cb, this);
        oyk_yapp_set_remote(bbs_yapp_, &sess->remote_call);
        bbs_yapp_sid_ = sess->id;
        sess->bbs_yapp_active = 1;
        sess->bbs_yapp_upload = 1;
        send_text(sess, "*** Ready for YAPP upload into incoming/. Start YAPP send now (Ctrl-X to cancel).\r");
        return;
    }

    send_text(sess, "Unknown BBS command. Type H for help.\r");
    bbs_send_prompt(sess);
}

void OykPortableNode::bbs_cleanup_yapp(OykSession* sess) {
    if (!sess) return;
    if (bbs_yapp_sid_ == sess->id) {
        if (bbs_yapp_) oyk_yapp_reset(bbs_yapp_);
        bbs_yapp_sid_ = 0;
        bbs_yapp_tx_sess_ = NULL;
    }
    sess->bbs_yapp_active = 0;
    sess->bbs_yapp_upload = 0;
}

void OykPortableNode::bbs_handle_raw_bytes(OykSession* sess, const oyk_u8* data, size_t len) {
    if (!sess || !sess->bbs_yapp_active || !bbs_yapp_ || bbs_yapp_sid_ != sess->id) return;
    bbs_yapp_tx_sess_ = sess;
    oyk_u8 plain[64];
    size_t pn = oyk_yapp_feed_bytes(bbs_yapp_, data, len, plain, sizeof(plain));
    if (bbs_yapp_->state == OYK_YAPP_COMPLETE ||
        bbs_yapp_->state == OYK_YAPP_ABORTED ||
        bbs_yapp_->state == OYK_YAPP_ERROR) {
        int was_up = sess->bbs_yapp_upload;
        int ok = (bbs_yapp_->state == OYK_YAPP_COMPLETE) ? 1 : 0;
        char st_msg[128];
        safe_copy(st_msg, sizeof(st_msg), bbs_yapp_->status);
        char target_copy[128];
        safe_copy(target_copy, sizeof(target_copy), bbs_yapp_->target_path);
        bbs_cleanup_yapp(sess);
        send_text(sess, "\r*** ");
        send_text(sess, st_msg);
        send_text(sess, "\r");
        if (was_up && ok) {
            send_text(sess, "*** File stored in incoming queue pending sysop approval.\r");
            char lmsg[128];
            sprintf(lmsg, "BBS incoming upload: %.80s (pending sysop approval)", target_copy);
            log_message(OYK_LOG_INFO, lmsg);
        }
        bbs_send_prompt(sess);
    } else if (!oyk_yapp_is_active(bbs_yapp_) && pn > 0) {
        for (size_t i = 0; i < pn; ++i) {
            if (plain[i] == OYK_YAPP_CAN || plain[i] == 0x03U) {
                bbs_cleanup_yapp(sess);
                send_text(sess, "\r*** YAPP upload cancelled.\r");
                bbs_send_prompt(sess);
                break;
            }
        }
    }
}

void OykPortableNode::chat_broadcast_topic(const char* topic, oyk_u32 exclude_sid, const char* text) {
    if (!topic || !text) return;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED &&
            sessions_[i].in_chat &&
            sessions_[i].id != exclude_sid &&
            stricmp_ascii(sessions_[i].chat_topic, topic) == 0) {
            send_text(&sessions_[i], text);
        }
    }
}

void OykPortableNode::chat_join(OykSession* sess, int direct_connect) {
    if (!sess) return;
    sess->in_chat = 1;
    sess->chat_direct = direct_connect ? 1 : 0;
    safe_copy(sess->chat_topic, sizeof(sess->chat_topic),
              cfg_.chat_topic[0] ? cfg_.chat_topic : "Lobby");
    sess->chat_name[0] = '\0';

    int count = 0;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED && sessions_[i].in_chat) ++count;
    }

    OykCallsign chat_c;
    oyk_config_resolved_chat_callsign(&cfg_, &chat_c);
    char c_str[16], u_str[16], msg[220];
    oyk_format_callsign(&chat_c, c_str, sizeof(c_str));
    oyk_format_callsign(&sess->remote_call, u_str, sizeof(u_str));

    sprintf(msg, "*** %s Packet Chat ***\r%s\rTopic: %s | Users online: %d\r",
            c_str, cfg_.chat_welcome, sess->chat_topic, count);
    send_text(sess, msg);

    char note[64];
    sprintf(note, "*** %s joined %s\r", u_str, sess->chat_topic);
    chat_broadcast_topic(sess->chat_topic, sess->id, note);
}

void OykPortableNode::chat_leave(OykSession* sess, const char* reason) {
    if (!sess || !sess->in_chat) return;
    char topic[16], u_str[16], note[96];
    safe_copy(topic, sizeof(topic), sess->chat_topic);
    oyk_format_callsign(&sess->remote_call, u_str, sizeof(u_str));
    sess->in_chat = 0;
    if (reason && reason[0]) {
        sprintf(note, "*** %s left chat (%s)\r", u_str, reason);
    } else {
        sprintf(note, "*** %s left chat\r", u_str);
    }
    chat_broadcast_topic(topic, sess->id, note);
}

void OykPortableNode::handle_chat_line(OykSession* sess, const char* raw_line) {
    char line[160];
    safe_copy(line, sizeof(line), raw_line);
    trim_inplace(line);
    if (!line[0]) return;

    char u_str[16];
    oyk_format_callsign(&sess->remote_call, u_str, sizeof(u_str));

    if (line[0] == '/') {
        char cmd[16];
        size_t i = 0;
        while (line[i] && !isspace(static_cast<unsigned char>(line[i])) && i < sizeof(cmd) - 1) {
            cmd[i] = line[i];
            ++i;
        }
        cmd[i] = '\0';
        const char* arg = line + i;
        while (*arg && isspace(static_cast<unsigned char>(*arg))) ++arg;

        if (stricmp_ascii(cmd, "/h") == 0 || stricmp_ascii(cmd, "/help") == 0 || strcmp(cmd, "/?") == 0) {
            send_text(sess,
                      "Chat Commands: /u (users), /t (topics), /j <topic>, /m <call> <msg>, /n <name>, /q (leave)\r");
            return;
        }
        if (stricmp_ascii(cmd, "/u") == 0 || stricmp_ascii(cmd, "/who") == 0) {
            char resp[260];
            strcpy(resp, "Chat Users:\r");
            for (int k = 0; k < OYK_MAX_SESSIONS; ++k) {
                if (sessions_[k].state == OYK_SESS_CONNECTED && sessions_[k].in_chat) {
                    char cs[12], row[80];
                    oyk_format_callsign(&sessions_[k].remote_call, cs, sizeof(cs));
                    const char* ifid = (sessions_[k].iface_idx >= 0) ? ifaces_[sessions_[k].iface_idx].cfg.id : "local";
                    if (sessions_[k].chat_name[0]) {
                        sprintf(row, "  %s (%s) [%s] on %s\r", cs, sessions_[k].chat_name,
                                sessions_[k].chat_topic, ifid);
                    } else {
                        sprintf(row, "  %s [%s] on %s\r", cs, sessions_[k].chat_topic, ifid);
                    }
                    if (strlen(resp) + strlen(row) < sizeof(resp) - 1) strcat(resp, row);
                }
            }
            send_text(sess, resp);
            return;
        }
        if (stricmp_ascii(cmd, "/t") == 0 || stricmp_ascii(cmd, "/topics") == 0) {
            char resp[200];
            strcpy(resp, "Active Topics:\r");
            for (int k = 0; k < OYK_MAX_SESSIONS; ++k) {
                if (sessions_[k].state == OYK_SESS_CONNECTED && sessions_[k].in_chat) {
                    int dup = 0;
                    for (int p = 0; p < k; ++p) {
                        if (sessions_[p].state == OYK_SESS_CONNECTED && sessions_[p].in_chat &&
                            stricmp_ascii(sessions_[p].chat_topic, sessions_[k].chat_topic) == 0) {
                            dup = 1;
                            break;
                        }
                    }
                    if (!dup) {
                        int cnt = 0;
                        for (int q = k; q < OYK_MAX_SESSIONS; ++q) {
                            if (sessions_[q].state == OYK_SESS_CONNECTED && sessions_[q].in_chat &&
                                stricmp_ascii(sessions_[q].chat_topic, sessions_[k].chat_topic) == 0) ++cnt;
                        }
                        char row[32];
                        sprintf(row, "  %s (%d)\r", sessions_[k].chat_topic, cnt);
                        if (strlen(resp) + strlen(row) < sizeof(resp) - 1) strcat(resp, row);
                    }
                }
            }
            send_text(sess, resp);
            return;
        }
        if (stricmp_ascii(cmd, "/j") == 0 || stricmp_ascii(cmd, "/join") == 0 || stricmp_ascii(cmd, "/topic") == 0) {
            if (!arg[0]) {
                send_text(sess, "Usage: /j <topic>\r");
                return;
            }
            char nt[16];
            size_t p = 0;
            while (arg[p] && !isspace(static_cast<unsigned char>(arg[p])) && p < sizeof(nt) - 1) {
                nt[p] = arg[p];
                ++p;
            }
            nt[p] = '\0';
            char note[64];
            sprintf(note, "*** %s moved to %s\r", u_str, nt);
            chat_broadcast_topic(sess->chat_topic, sess->id, note);
            safe_copy(sess->chat_topic, sizeof(sess->chat_topic), nt);
            sprintf(note, "*** Joined topic %s\r", sess->chat_topic);
            send_text(sess, note);
            sprintf(note, "*** %s joined %s\r", u_str, sess->chat_topic);
            chat_broadcast_topic(sess->chat_topic, sess->id, note);
            return;
        }
        if (stricmp_ascii(cmd, "/m") == 0 || stricmp_ascii(cmd, "/msg") == 0) {
            char dst_c[16];
            size_t p = 0;
            while (arg[p] && !isspace(static_cast<unsigned char>(arg[p])) && p < sizeof(dst_c) - 1) {
                dst_c[p] = static_cast<char>(toupper(static_cast<unsigned char>(arg[p])));
                ++p;
            }
            dst_c[p] = '\0';
            const char* mtxt = arg + p;
            while (*mtxt && isspace(static_cast<unsigned char>(*mtxt))) ++mtxt;
            if (!dst_c[0] || !mtxt[0]) {
                send_text(sess, "Usage: /m <callsign> <message>\r");
                return;
            }
            OykSession* target = NULL;
            for (int k = 0; k < OYK_MAX_SESSIONS; ++k) {
                if (sessions_[k].state == OYK_SESS_CONNECTED && sessions_[k].in_chat) {
                    char cs[12];
                    oyk_format_callsign(&sessions_[k].remote_call, cs, sizeof(cs));
                    if (stricmp_ascii(cs, dst_c) == 0) {
                        target = &sessions_[k];
                        break;
                    }
                }
            }
            if (!target) {
                send_text(sess, "*** User not in chat.\r");
                return;
            }
            char pm[180];
            sprintf(pm, "[PM from %s] %.140s\r", u_str, mtxt);
            send_text(target, pm);
            sprintf(pm, "[PM to %s] %.140s\r", dst_c, mtxt);
            send_text(sess, pm);
            return;
        }
        if (stricmp_ascii(cmd, "/n") == 0 || stricmp_ascii(cmd, "/name") == 0) {
            safe_copy(sess->chat_name, sizeof(sess->chat_name), arg);
            send_text(sess, sess->chat_name[0] ? "*** Display name updated.\r" : "*** Display name cleared.\r");
            return;
        }
        if (stricmp_ascii(cmd, "/q") == 0 || stricmp_ascii(cmd, "/quit") == 0 || stricmp_ascii(cmd, "/bye") == 0) {
            int direct = sess->chat_direct;
            chat_leave(sess, NULL);
            if (direct) {
                OykCallsign chat_c;
                oyk_config_resolved_chat_callsign(&cfg_, &chat_c);
                char cc[16], bye[40];
                oyk_format_callsign(&chat_c, cc, sizeof(cc));
                sprintf(bye, "73 de %s\r", cc);
                send_text(sess, bye);
                send_disc(sess);
                sess->state = OYK_SESS_FREE;
            } else {
                char node_s[16], ret[64];
                oyk_format_callsign(&cfg_.callsign, node_s, sizeof(node_s));
                sprintf(ret, "*** Left chat. Returning to node.\r%s> ", node_s);
                send_text(sess, ret);
            }
            return;
        }
        send_text(sess, "Unknown chat command. Type /h for help.\r");
        return;
    }

    char out[200];
    if (sess->chat_name[0]) {
        sprintf(out, "<%s %s> %.150s\r", u_str, sess->chat_name, line);
    } else {
        sprintf(out, "<%s> %.160s\r", u_str, line);
    }
    chat_broadcast_topic(sess->chat_topic, sess->id, out);
}

void OykPortableNode::on_rx_frame(int iface_idx, const OykFrame* frame,
                                  oyk_u32 peer_ip, oyk_u16 peer_port, oyk_u16 peer_handle) {
    if (iface_idx < 0 || iface_idx >= iface_count_ || !frame) return;
    if (peer_ip != 0 && ifaces_[iface_idx].cfg.netmask[0]) {
        oyk_u32 bind_ip = 0, mask_ip = 0;
        (void)oyk_resolve_host_or_dmr_id(ifaces_[iface_idx].cfg.host, 12U, NULL, 0, &bind_ip);
        if (oyk_parse_ipv4(ifaces_[iface_idx].cfg.netmask, &mask_ip) &&
            !oyk_ip_matches_netmask(peer_ip, bind_ip, mask_ip)) {
            ifaces_[iface_idx].rx_errors++;
            return;
        }
    }
    ifaces_[iface_idx].rx_frames++;
    record_heard(&frame->src, iface_idx);

    OykFrameType ft = oyk_frame_type(frame);
    if (ft == OYK_FT_UI ||
        (!is_for_node(&frame->dst) && !is_for_bbs(&frame->dst) && !is_for_chat(&frame->dst))) {
        return;
    }

    /* Locate existing session */
    OykSession* sess = NULL;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state != OYK_SESS_FREE &&
            sessions_[i].iface_idx == iface_idx &&
            oyk_callsign_eq(&sessions_[i].remote_call, &frame->src)) {
            sess = &sessions_[i];
            break;
        }
    }

    time_t now_t = now_time();
    char now_compact[16], today[10], src_s[12];
    oyk_format_now_compact(now_t, now_compact, sizeof(now_compact));
    oyk_format_now_yyyymmdd(now_t, today, sizeof(today));
    oyk_format_callsign(&frame->src, src_s, sizeof(src_s));

    if (ft == OYK_FT_SABM) {
        if (!sess && !accept_incoming_) {
            OykFrame dm;
            memset(&dm, 0, sizeof(dm));
            dm.dst = frame->src;
            dm.src = frame->dst;
            dm.is_response = 1;
            dm.control = 0x1FU;
            if (tx_cb_) tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
            log_message(OYK_LOG_WARNING, "Denied incoming SABM (incoming disabled)");
            return;
        }

        if (!sess) {
            if (ifaces_[iface_idx].cfg.block_inbound) {
                OykFrame dm;
                memset(&dm, 0, sizeof(dm));
                dm.dst = frame->src;
                dm.src = frame->dst;
                dm.is_response = 1;
                dm.control = 0x1FU;
                if (tx_cb_) tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
                char msg[72];
                sprintf(msg, "Denied incoming %s on %s (interface disabled)", src_s, ifaces_[iface_idx].cfg.id);
                log_message(OYK_LOG_WARNING, msg);
                return;
            }
            const OykPeerConfig* existing_peer = find_config_peer(iface_idx, &frame->src);
            if (existing_peer && !existing_peer->allow_inbound) {
                OykFrame dm;
                memset(&dm, 0, sizeof(dm));
                dm.dst = frame->src;
                dm.src = frame->dst;
                dm.is_response = 1;
                dm.control = 0x1FU;
                if (tx_cb_) tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
                char msg[72];
                sprintf(msg, "Denied incoming %s on %s (peer disabled)", src_s, ifaces_[iface_idx].cfg.id);
                log_message(OYK_LOG_WARNING, msg);
                return;
            }

            int ban_idx = find_ban_index(&frame->src, now_compact);
            int restricted = 0;
            int appeal_ok = 0;
            char ban_key[12];
            ban_key[0] = '\0';

            if (ban_idx >= 0) {
                OykBanEntry* be = &bans_[ban_idx];
                safe_copy(ban_key, sizeof(ban_key), be->key);
                int notified = (be->flags & OYK_BAN_NOTIFIED) ? 1 : 0;
                int hard = (be->flags & OYK_BAN_HARD_BAN) ? 1 : 0;
                int app_allowed = (be->flags & OYK_BAN_APPEAL_ALLOWED) ? 1 : 0;
                int app_used = (be->flags & OYK_BAN_APPEAL_USED) ? 1 : 0;

                if (notified && hard) {
                    OykFrame dm;
                    memset(&dm, 0, sizeof(dm));
                    dm.dst = frame->src;
                    dm.src = frame->dst;
                    dm.is_response = 1;
                    dm.control = 0x1FU;
                    if (tx_cb_) tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
                    char msg[72];
                    sprintf(msg, "Rejected hard-banned %s", src_s);
                    log_message(OYK_LOG_BAN, msg);
                    return;
                }
                if (notified && !hard && strcmp(be->last_check_date, today) == 0) {
                    OykFrame dm;
                    memset(&dm, 0, sizeof(dm));
                    dm.dst = frame->src;
                    dm.src = frame->dst;
                    dm.is_response = 1;
                    dm.control = 0x1FU;
                    if (tx_cb_) tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
                    char msg[80];
                    sprintf(msg, "Rejected soft-banned %s (daily check used)", src_s);
                    log_message(OYK_LOG_BAN, msg);
                    return;
                }
                restricted = 1;
                appeal_ok = (app_allowed && !app_used) ? 1 : 0;
                if (!notified) {
                    be->flags |= OYK_BAN_NOTIFIED;
                    safe_copy(be->last_check_date, sizeof(be->last_check_date), today);
                    save_banlist();
                } else {
                    safe_copy(be->last_check_date, sizeof(be->last_check_date), today);
                }
            }

            /* Allocate free session slot */
            for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
                if (sessions_[i].state == OYK_SESS_FREE) {
                    sess = &sessions_[i];
                    break;
                }
            }
            if (!sess) {
                OykFrame dm;
                memset(&dm, 0, sizeof(dm));
                dm.dst = frame->src;
                dm.src = frame->dst;
                dm.is_response = 1;
                dm.control = 0x1FU;
                if (tx_cb_) tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
                return;
            }

            const OykIfaceConfig& ifc = ifaces_[iface_idx].cfg;
            int is_telnet = (stricmp_ascii(ifc.type, "telnet") == 0) ? 1 : 0;
            if (!existing_peer && !is_telnet && !ifc.no_auto_learn &&
                !is_for_node(&frame->src) && !is_for_bbs(&frame->src) && !is_for_chat(&frame->src)) {
                oyk_u8 pol = resolve_learned_policy(iface_idx);
                oyk_u8 allow_in = (pol == OYK_LEARN_INBOUND || pol == OYK_LEARN_BOTH) ? 1 : 0;
                oyk_u8 allow_out = (pol == OYK_LEARN_OUTBOUND || pol == OYK_LEARN_BOTH) ? 1 : 0;
                if (cfg_.peer_count < OYK_MAX_CONFIG_PEERS) {
                    OykPeerConfig* lp = &cfg_.peers[cfg_.peer_count++];
                    memset(lp, 0, sizeof(*lp));
                    oyk_derive_default_peer_id(&cfg_, src_s, lp->id, sizeof(lp->id));
                    safe_copy(lp->callsign, sizeof(lp->callsign), src_s);
                    safe_copy(lp->interface_id, sizeof(lp->interface_id), ifc.id);
                    format_ipv4_dotted(peer_ip, lp->host, sizeof(lp->host));
                    lp->resolved_ip = peer_ip;
                    lp->port = peer_port;
                    lp->allow_inbound = allow_in;
                    lp->allow_outbound = allow_out;
                    lp->auto_learned = 1;
                    char lmsg[96];
                    sprintf(lmsg, "Auto-learned peer %s (%s) on %s", lp->id, lp->callsign, lp->interface_id);
                    log_message(OYK_LOG_INFO, lmsg);
                }
                if (!allow_in) {
                    OykFrame dm;
                    memset(&dm, 0, sizeof(dm));
                    dm.dst = frame->src;
                    dm.src = frame->dst;
                    dm.is_response = 1;
                    dm.control = 0x1FU;
                    if (tx_cb_) tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
                    log_message(OYK_LOG_WARNING, "Denied auto-learned peer inbound (policy)");
                    return;
                }
            }
            int is_first_time = !is_welcomed_call(src_s);
            mark_welcomed_call(src_s);

            memset(sess, 0, sizeof(*sess));
            sess->id = next_session_id_++;
            sess->iface_idx = iface_idx;
            sess->local_call = frame->dst;
            sess->remote_call = frame->src;
            sess->peer_ip = peer_ip;
            sess->peer_port = peer_port;
            sess->peer_handle = peer_handle;
            sess->state = OYK_SESS_CONNECTED;
            sess->restricted_ban = restricted;
            sess->appeal_eligible = appeal_ok;
            safe_copy(sess->ban_key, sizeof(sess->ban_key), ban_key);
            oyk_format_now_hms(now_t, sess->since_hms, sizeof(sess->since_hms));
            sess->last_activity = now_t;

            /* Send UA */
            OykFrame ua;
            memset(&ua, 0, sizeof(ua));
            ua.dst = frame->src;
            ua.src = frame->dst;
            ua.is_response = 1;
            ua.control = 0x73U;
            if (tx_cb_) {
                tx_cb_(iface_idx, &ua, peer_ip, peer_port, peer_handle, tx_ctx_);
                ifaces_[iface_idx].tx_frames++;
            }

            if (restricted) {
                OykBanEntry* be = &bans_[ban_idx];
                char notice[360];
                sprintf(notice,
                        "*** RESTRICTED SESSION - CALLSIGN BANNED ***\r"
                        "Type: %s | Banned: %s | Lifts: %s\r"
                        "Reason: %s\r%s",
                        (be->flags & OYK_BAN_HARD_BAN) ? "HARD BAN" : "SOFT BAN",
                        be->banned_at, be->lift_at, be->reason,
                        appeal_ok
                            ? "Submit ONE appeal (max 140 ASCII chars) or QUIT:\rAppeal> "
                            : ((be->flags & OYK_BAN_APPEAL_USED)
                                   ? "Appeal already submitted.\rDisconnecting.\r"
                                   : "Appeals not permitted.\rDisconnecting.\r"));
                send_text(sess, notice);
                if (!appeal_ok) {
                    send_disc(sess);
                    sess->state = OYK_SESS_FREE;
                }
            } else if (is_for_bbs(&frame->dst)) {
                bbs_enter(sess, 1);
                char msg[72];
                sprintf(msg, "BBS session #%lu connected: %s on %s",
                        static_cast<unsigned long>(sess->id), src_s, ifaces_[iface_idx].cfg.id);
                log_message(OYK_LOG_INFO, msg);
            } else if (is_for_chat(&frame->dst)) {
                chat_join(sess, 1);
                char msg[72];
                sprintf(msg, "CHAT session #%lu connected: %s on %s",
                        static_cast<unsigned long>(sess->id), src_s, ifaces_[iface_idx].cfg.id);
                log_message(OYK_LOG_INFO, msg);
            } else {
                char node_s[16], banner[220];
                oyk_format_callsign(&cfg_.callsign, node_s, sizeof(node_s));
                const char* wmsg = (is_first_time && cfg_.new_user_welcome[0])
                                       ? cfg_.new_user_welcome
                                       : cfg_.welcome_text;
                if (wmsg[0]) {
                    sprintf(banner, "*** Connected to %s Packet Node\r%s\r%s> ",
                            node_s, wmsg, node_s);
                } else {
                    sprintf(banner, "*** Connected to %s Packet Node\r%s> ",
                            node_s, node_s);
                }
                send_text(sess, banner);
                char msg[72];
                sprintf(msg, "Session #%lu connected: %s on %s",
                        static_cast<unsigned long>(sess->id), src_s, ifaces_[iface_idx].cfg.id);
                log_message(OYK_LOG_INFO, msg);
            }
        }
        return;
    }

    if (!sess) {
        if (ft != OYK_FT_DM && tx_cb_) {
            OykFrame dm;
            memset(&dm, 0, sizeof(dm));
            dm.dst = frame->src;
            dm.src = frame->dst;
            dm.is_response = 1;
            dm.control = 0x1FU;
            tx_cb_(iface_idx, &dm, peer_ip, peer_port, peer_handle, tx_ctx_);
        }
        return;
    }

    sess->last_activity = now_t;
    sess->peer_ip = peer_ip;
    sess->peer_port = peer_port;

    if (sess->state == OYK_SESS_CONNECTING) {
        if (ft == OYK_FT_UA) {
            sess->state = OYK_SESS_CONNECTED;
            sess->vs = 0;
            sess->vr = 0;
            if (sess->is_local_term && local_cb_) {
                local_cb_(sess->id, 1, NULL, 0, "Connected", local_ctx_);
            }
        } else if (ft == OYK_FT_DM || ft == OYK_FT_DISC) {
            oyk_u32 sid = sess->id;
            int was_local = sess->is_local_term;
            sess->state = OYK_SESS_FREE;
            if (was_local && local_cb_) {
                local_cb_(sid, 3, NULL, 0, (ft == OYK_FT_DM) ? "Connection refused (DM)" : "Disconnected", local_ctx_);
            }
        }
        return;
    }

    if (ft == OYK_FT_DISC) {
        OykFrame ua;
        memset(&ua, 0, sizeof(ua));
        ua.dst = frame->src;
        ua.src = frame->dst;
        ua.is_response = 1;
        ua.control = 0x73U;
        if (tx_cb_) {
            tx_cb_(iface_idx, &ua, peer_ip, peer_port, peer_handle, tx_ctx_);
            ifaces_[iface_idx].tx_frames++;
        }
        oyk_u32 sid = sess->id;
        int was_local = sess->is_local_term;
        if (sess->in_bbs) bbs_cleanup_yapp(sess);
        if (sess->in_chat) chat_leave(sess, "Disconnected");
        sess->state = OYK_SESS_FREE;
        if (was_local && local_cb_) {
            local_cb_(sid, 3, NULL, 0, "Disconnected by remote", local_ctx_);
        }
        return;
    }

    if (ft == OYK_FT_DM || (ft == OYK_FT_UA && sess->state == OYK_SESS_DISCONNECTING)) {
        oyk_u32 sid = sess->id;
        int was_local = sess->is_local_term;
        if (sess->in_bbs) bbs_cleanup_yapp(sess);
        if (sess->in_chat) chat_leave(sess, "Disconnected");
        sess->state = OYK_SESS_FREE;
        if (was_local && local_cb_) {
            local_cb_(sid, 3, NULL, 0, "Disconnected", local_ctx_);
        }
        return;
    }

    if (ft == OYK_FT_I && sess->state == OYK_SESS_CONNECTED) {
        oyk_u8 ns = static_cast<oyk_u8>((frame->control >> 1) & 0x07U);
        if (ns == sess->vr) {
            sess->vr = static_cast<oyk_u8>((sess->vr + 1) & 0x07U);
            OykFrame rr;
            memset(&rr, 0, sizeof(rr));
            rr.dst = sess->remote_call;
            rr.src = sess->local_call;
            rr.is_response = 1;
            rr.control = static_cast<oyk_u8>(0x01U | ((sess->vr & 0x07U) << 5));
            if (tx_cb_) {
                tx_cb_(iface_idx, &rr, peer_ip, peer_port, peer_handle, tx_ctx_);
                ifaces_[iface_idx].tx_frames++;
            }

            if (sess->is_local_term) {
                if (local_cb_ && frame->info_len > 0) {
                    local_cb_(sess->id, 2, frame->info, frame->info_len, NULL, local_ctx_);
                }
                return;
            }

            if (sess->in_bbs && sess->bbs_yapp_active) {
                if (frame->info_len > 0) {
                    bbs_handle_raw_bytes(sess, frame->info, frame->info_len);
                }
                return;
            }

            for (oyk_u16 i = 0; i < frame->info_len; ++i) {
                oyk_u8 ch = frame->info[i];
                if (ch == '\r' || ch == '\n') {
                    sess->rx_line[sess->rx_line_len] = '\0';
                    if (sess->rx_line_len > 0 || ch == '\r') {
                        if (sess->restricted_ban) {
                            handle_restricted_line(sess, sess->rx_line);
                        } else if (sess->in_bbs) {
                            handle_bbs_line(sess, sess->rx_line);
                        } else if (sess->in_chat) {
                            handle_chat_line(sess, sess->rx_line);
                        } else {
                            handle_normal_command(sess, sess->rx_line);
                        }
                    }
                    sess->rx_line_len = 0;
                    if (sess->state != OYK_SESS_CONNECTED) break;
                } else if (ch == 0x1AU) {
                    if (sess->rx_line_len > 0) {
                        sess->rx_line[sess->rx_line_len] = '\0';
                        if (sess->in_bbs) handle_bbs_line(sess, sess->rx_line);
                        sess->rx_line_len = 0;
                    }
                    if (sess->in_bbs && sess->state == OYK_SESS_CONNECTED) {
                        handle_bbs_line(sess, "/EX");
                    }
                    if (sess->state != OYK_SESS_CONNECTED) break;
                } else if (ch >= 0x20U && ch <= 0x7EU) {
                    if (sess->rx_line_len < static_cast<int>(sizeof(sess->rx_line) - 1)) {
                        sess->rx_line[sess->rx_line_len++] = static_cast<char>(ch);
                    }
                }
            }
        }
    }
}

void OykPortableNode::handle_restricted_line(OykSession* sess, const char* line) {
    char clean[OYK_MAX_APPEAL_LEN + 1];
    oyk_sanitize_appeal(line, clean, sizeof(clean));

    if (!sess->appeal_eligible || !clean[0] ||
        stricmp_ascii(clean, "QUIT") == 0 || stricmp_ascii(clean, "BYE") == 0) {
        send_text(sess, "No appeal submitted. Disconnecting.\r");
        send_disc(sess);
        sess->state = OYK_SESS_FREE;
        return;
    }

    sess->appeal_eligible = 0;
    for (int i = 0; i < ban_count_; ++i) {
        if (strcmp(bans_[i].key, sess->ban_key) == 0) {
            bans_[i].flags |= OYK_BAN_APPEAL_USED;
            save_banlist();
            break;
        }
    }

    char call_s[12];
    oyk_format_callsign(&sess->remote_call, call_s, sizeof(call_s));
    log_appeal(call_s, clean);

    send_text(sess, "Appeal recorded. Disconnecting.\r");
    send_disc(sess);
    sess->state = OYK_SESS_FREE;
}

void OykPortableNode::handle_normal_command(OykSession* sess, const char* raw_line) {
    char cmd[64];
    safe_copy(cmd, sizeof(cmd), raw_line);
    trim_inplace(cmd);

    char node_s[16];
    oyk_format_callsign(&cfg_.callsign, node_s, sizeof(node_s));

    if (!cmd[0]) {
        char p[24];
        sprintf(p, "%s> ", node_s);
        send_text(sess, p);
        return;
    }

    if (stricmp_ascii(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        send_text(sess, "Commands: INFO, PORTS, USERS, MHEARD, BBS, CHAT, BYE, HELP\r");
    } else if (stricmp_ascii(cmd, "BBS") == 0) {
        if (!cfg_.bbs_enabled) {
            send_text(sess, "BBS is disabled on this node.\r");
        } else {
            bbs_enter(sess, 0);
            return;
        }
    } else if (stricmp_ascii(cmd, "CHAT") == 0 || stricmp_ascii(cmd, "CH") == 0) {
        if (!cfg_.chat_enabled) {
            send_text(sess, "CHAT is disabled on this node.\r");
        } else {
            chat_join(sess, 0);
            return;
        }
    } else if (stricmp_ascii(cmd, "INFO") == 0 || stricmp_ascii(cmd, "I") == 0) {
        char resp[160];
        sprintf(resp, "%s - %s | Active: %d\r", node_s, cfg_.info_text, total_active_connections());
        send_text(sess, resp);
    } else if (stricmp_ascii(cmd, "PORTS") == 0 || stricmp_ascii(cmd, "P") == 0) {
        char resp[240];
        resp[0] = '\0';
        for (int i = 0; i < iface_count_; ++i) {
            char row[64];
            sprintf(row, "%d) %s [%s] Conn:%d\r",
                    i + 1, ifaces_[i].cfg.id, ifaces_[i].cfg.type, active_connections_on(i));
            if (strlen(resp) + strlen(row) < sizeof(resp) - 1) strcat(resp, row);
        }
        send_text(sess, resp);
    } else if (stricmp_ascii(cmd, "USERS") == 0 || stricmp_ascii(cmd, "U") == 0) {
        char resp[240];
        sprintf(resp, "Active (%d):\r", total_active_connections());
        for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
            if (sessions_[i].state == OYK_SESS_CONNECTED) {
                char cs[12], row[48];
                oyk_format_callsign(&sessions_[i].remote_call, cs, sizeof(cs));
                sprintf(row, " #%lu %s on %s\r",
                        static_cast<unsigned long>(sessions_[i].id),
                        cs, (sessions_[i].iface_idx >= 0) ? ifaces_[sessions_[i].iface_idx].cfg.id : "local");
                if (strlen(resp) + strlen(row) < sizeof(resp) - 1) strcat(resp, row);
            }
        }
        send_text(sess, resp);
    } else if (stricmp_ascii(cmd, "MHEARD") == 0 || stricmp_ascii(cmd, "MH") == 0) {
        char resp[240];
        resp[0] = '\0';
        for (int i = 0; i < new_heard_count_; ++i) {
            char row[48];
            sprintf(row, "%s %s %s\r", new_heard_[i].callsign,
                    new_heard_[i].iface_id, new_heard_[i].time_hms);
            if (strlen(resp) + strlen(row) < sizeof(resp) - 1) strcat(resp, row);
        }
        send_text(sess, resp[0] ? resp : "No stations heard.\r");
    } else if (stricmp_ascii(cmd, "BYE") == 0 || stricmp_ascii(cmd, "QUIT") == 0 ||
               stricmp_ascii(cmd, "B") == 0) {
        char resp[32];
        sprintf(resp, "73 de %s\r", node_s);
        send_text(sess, resp);
        send_disc(sess);
        sess->state = OYK_SESS_FREE;
        return;
    } else if (((cmd[0] == 'C' || cmd[0] == 'c') && (cmd[1] == ' ' || cmd[1] == '\t')) ||
               ((strlen(cmd) > 8) && (cmd[7] == ' ' || cmd[7] == '\t') &&
                (cmd[0] == 'C' || cmd[0] == 'c') && (cmd[1] == 'O' || cmd[1] == 'o') &&
                (cmd[2] == 'N' || cmd[2] == 'n') && (cmd[3] == 'N' || cmd[3] == 'n') &&
                (cmd[4] == 'E' || cmd[4] == 'e') && (cmd[5] == 'C' || cmd[5] == 'c') &&
                (cmd[6] == 'T' || cmd[6] == 't'))) {
        const char* rest = (cmd[1] == ' ' || cmd[1] == '\t') ? (cmd + 2) : (cmd + 8);
        while (*rest == ' ' || *rest == '\t') ++rest;
        char port_tok[24], call_tok[24];
        port_tok[0] = '\0';
        call_tok[0] = '\0';
        int pi = 0;
        while (*rest && *rest != ' ' && *rest != '\t' && pi + 1 < static_cast<int>(sizeof(port_tok))) {
            port_tok[pi++] = *rest++;
        }
        port_tok[pi] = '\0';
        while (*rest == ' ' || *rest == '\t') ++rest;
        int ci = 0;
        while (*rest && *rest != ' ' && *rest != '\t' && ci + 1 < static_cast<int>(sizeof(call_tok))) {
            call_tok[ci++] = *rest++;
        }
        call_tok[ci] = '\0';
        if (!port_tok[0] || !call_tok[0]) {
            send_text(sess, "Usage: CONNECT <port> <callsign>\r");
        } else {
            int tidx = -1;
            for (int ii = 0; ii < iface_count_; ++ii) {
                if (stricmp_ascii(ifaces_[ii].cfg.id, port_tok) == 0) {
                    tidx = ii;
                    break;
                }
            }
            if (tidx < 0) {
                int pnum = atoi(port_tok);
                if (pnum >= 1 && pnum <= iface_count_) tidx = pnum - 1;
            }
            if (tidx < 0) {
                char msg[64];
                sprintf(msg, "Unknown port '%s'.\r", port_tok);
                send_text(sess, msg);
            } else if (ifaces_[tidx].cfg.block_outbound ||
                       stricmp_ascii(ifaces_[tidx].cfg.type, "telnet") == 0) {
                char msg[112];
                sprintf(msg, "Connection refused: outbound connections on interface '%s' are disabled.\r",
                        ifaces_[tidx].cfg.id);
                send_text(sess, msg);
                char wmsg[88];
                sprintf(wmsg, "Blocked outbound connect on %s (allow_outbound=false)", ifaces_[tidx].cfg.id);
                log_message(OYK_LOG_WARNING, wmsg);
            } else if (!can_forward_between_ifaces(sess->iface_idx, tidx)) {
                char msg[112];
                sprintf(msg, "Connection refused: same-interface forwarding on interface '%s' is disabled.\r",
                        ifaces_[tidx].cfg.id);
                send_text(sess, msg);
                char wmsg[96];
                sprintf(wmsg, "Blocked same-interface connect on %s (allow_same_interface=false)",
                        ifaces_[tidx].cfg.id);
                log_message(OYK_LOG_WARNING, wmsg);
            } else {
                OykCallsign tc;
                if (!oyk_parse_callsign(call_tok, &tc)) {
                    char msg[64];
                    sprintf(msg, "Invalid callsign '%s'.\r", call_tok);
                    send_text(sess, msg);
                } else {
                    char tcs[16], msg[80];
                    oyk_format_callsign(&tc, tcs, sizeof(tcs));
                    sprintf(msg, "*** Connecting to %s via %s...\r", tcs, ifaces_[tidx].cfg.id);
                    send_text(sess, msg);
                }
            }
        }
    } else {
        send_text(sess, "Unknown command. Type HELP.\r");
    }

    char p[24];
    sprintf(p, "%s> ", node_s);
    send_text(sess, p);
}

void OykPortableNode::tick() {
    time_t now_t = now_time();
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED) {
            long idle = static_cast<long>(now_t - sessions_[i].last_activity);
            long limit = sessions_[i].restricted_ban ? 120L : 600L;
            if (idle >= limit) {
                send_disc(&sessions_[i]);
                sessions_[i].state = OYK_SESS_FREE;
            }
        }
    }
}

void OykPortableNode::set_accept_incoming(int allow) {
    accept_incoming_ = allow ? 1 : 0;
    manual_deny_incoming_ = allow ? 0 : 1;
    log_message(allow ? OYK_LOG_INFO : OYK_LOG_WARNING,
                allow ? "Incoming connections ALLOWED" : "Incoming connections DENIED");
}

int OykPortableNode::can_forward_between_ifaces(int src_iface_idx, int dst_iface_idx) const {
    if (dst_iface_idx < 0 || dst_iface_idx >= iface_count_) return 0;
    if (src_iface_idx >= 0 && src_iface_idx == dst_iface_idx &&
        (!ifaces_[dst_iface_idx].cfg.allow_same_interface ||
         stricmp_ascii(ifaces_[dst_iface_idx].cfg.type, "telnet") == 0)) {
        return 0;
    }
    return 1;
}

static int portable_iface_eq(const OykIfaceConfig* a, const OykIfaceConfig* b) {
    if (strcmp(a->id, b->id) != 0) return 0;
    if (stricmp_ascii(a->type, b->type) != 0) return 0;
    if (strcmp(a->description, b->description) != 0) return 0;
    if (strcmp(a->host, b->host) != 0) return 0;
    if (strcmp(a->netmask, b->netmask) != 0) return 0;
    if (a->port != b->port) return 0;
    if (strcmp(a->device, b->device) != 0) return 0;
    if (a->baud != b->baud) return 0;
    if (a->kiss_port != b->kiss_port) return 0;
    if (a->server_mode != b->server_mode) return 0;
    if (a->block_inbound != b->block_inbound) return 0;
    if (a->block_outbound != b->block_outbound) return 0;
    if (a->allow_same_interface != b->allow_same_interface) return 0;
    if (a->peer_count != b->peer_count) return 0;
    for (int i = 0; i < a->peer_count; ++i) {
        if (strcmp(a->peers[i].callsign, b->peers[i].callsign) != 0 ||
            a->peers[i].ip != b->peers[i].ip ||
            a->peers[i].port != b->peers[i].port) {
            return 0;
        }
    }
    return 1;
}

int OykPortableNode::apply_config(const OykConfig* new_cfg, int save_to_disk,
                                  OykConfigApplyResult* res_out) {
    OykConfigApplyResult local_res;
    memset(&local_res, 0, sizeof(local_res));
    OykConfigApplyResult* res = res_out ? res_out : &local_res;
    memset(res, 0, sizeof(*res));

    if (!new_cfg || !new_cfg->callsign.base[0]) {
        safe_copy(res->error, sizeof(res->error), "Invalid node callsign.");
        return 0;
    }
    if (new_cfg->iface_count < 0 || new_cfg->iface_count > OYK_MAX_INTERFACES) {
        safe_copy(res->error, sizeof(res->error), "Invalid interface count.");
        return 0;
    }
    for (int i = 0; i < new_cfg->iface_count; ++i) {
        if (!new_cfg->ifaces[i].id[0] || !new_cfg->ifaces[i].type[0]) {
            safe_copy(res->error, sizeof(res->error), "Interface ID and type are required.");
            return 0;
        }
        for (int j = i + 1; j < new_cfg->iface_count; ++j) {
            if (stricmp_ascii(new_cfg->ifaces[i].id, new_cfg->ifaces[j].id) == 0) {
                safe_copy(res->error, sizeof(res->error), "Duplicate interface ID.");
                return 0;
            }
        }
    }

    struct ApplyCfgGuard {
        OykConfig* p;
        explicit ApplyCfgGuard(const OykConfig* src) : p(new OykConfig(*src)) {}
        ~ApplyCfgGuard() { delete p; }
    };
    struct ApplyOldIfacesGuard {
        OykIfaceRuntime* p;
        ApplyOldIfacesGuard() : p(new OykIfaceRuntime[OYK_MAX_INTERFACES]) {}
        ~ApplyOldIfacesGuard() { delete[] p; }
    };
    ApplyCfgGuard next_guard(new_cfg);
    if (!next_guard.p) {
        safe_copy(res->error, sizeof(res->error), "Out of memory.");
        return 0;
    }
    OykConfig& next_cfg = *next_guard.p;
    for (int i = 0; i < next_cfg.iface_count; ++i) {
        if (stricmp_ascii(next_cfg.ifaces[i].type, "telnet") == 0) {
            next_cfg.ifaces[i].block_outbound = 1;
            next_cfg.ifaces[i].allow_same_interface = 0;
        }
        if (stricmp_ascii(next_cfg.ifaces[i].type, "axudp") == 0 ||
            stricmp_ascii(next_cfg.ifaces[i].type, "axip") == 0) {
            if (next_cfg.ifaces[i].host[0] && oyk_is_all_digits(next_cfg.ifaces[i].host)) {
                oyk_u32 did = 0;
                if (!oyk_parse_dmr_id(next_cfg.ifaces[i].host, &did)) {
                    safe_copy(res->error, sizeof(res->error), "Invalid interface DMR ID (allowed: 1..16777214).");
                    return 0;
                }
            }
            if (next_cfg.ifaces[i].netmask[0]) {
                oyk_u32 nm = 0;
                if (!oyk_parse_ipv4(next_cfg.ifaces[i].netmask, &nm)) {
                    safe_copy(res->error, sizeof(res->error), "Invalid interface IPv4 netmask.");
                    return 0;
                }
            }
        }
    }
    for (int p = 0; p < next_cfg.peer_count && p < OYK_MAX_CONFIG_PEERS; ++p) {
        OykPeerConfig* pc = &next_cfg.peers[p];
        if (!pc->host[0]) continue;
        if (oyk_is_all_digits(pc->host)) {
            oyk_u32 did = 0;
            if (!oyk_parse_dmr_id(pc->host, &did)) {
                safe_copy(res->error, sizeof(res->error), "Invalid peer DMR ID (allowed: 1..16777214).");
                return 0;
            }
        }
        oyk_u8 pref = 12U;
        for (int i = 0; i < next_cfg.iface_count; ++i) {
            if (stricmp_ascii(next_cfg.ifaces[i].id, pc->interface_id) == 0) {
                pref = oyk_dmr_prefix_for_iface(&next_cfg.ifaces[i]);
                break;
            }
        }
        (void)oyk_resolve_host_or_dmr_id(pc->host, pref, NULL, 0, &pc->resolved_ip);
    }
    migrate_iface_peers(&next_cfg);
    next_cfg.bbs_ssid = static_cast<oyk_u8>(next_cfg.bbs_ssid & 0x0FU);
    next_cfg.chat_ssid = static_cast<oyk_u8>(next_cfg.chat_ssid & 0x0FU);
    if (!next_cfg.source_path[0] && cfg_.source_path[0]) {
        safe_copy(next_cfg.source_path, sizeof(next_cfg.source_path), cfg_.source_path);
    }
    if (!next_cfg.has_detected_eol && cfg_.has_detected_eol) {
        next_cfg.line_ending = cfg_.line_ending;
        next_cfg.has_detected_eol = 1;
    }

    OykCallsign old_bbs_c, new_bbs_c, old_chat_c, new_chat_c;
    oyk_config_resolved_bbs_callsign(&cfg_, &old_bbs_c);
    oyk_config_resolved_bbs_callsign(&next_cfg, &new_bbs_c);
    oyk_config_resolved_chat_callsign(&cfg_, &old_chat_c);
    oyk_config_resolved_chat_callsign(&next_cfg, &new_chat_c);

    int node_id_changed = (!oyk_callsign_eq(&cfg_.callsign, &next_cfg.callsign) ||
                           !oyk_callsign_eq(&cfg_.alias, &next_cfg.alias)) ? 1 : 0;
    int bbs_id_changed = (!oyk_callsign_eq(&old_bbs_c, &new_bbs_c) ||
                          !oyk_callsign_eq(&cfg_.bbs_alias, &next_cfg.bbs_alias)) ? 1 : 0;
    int chat_id_changed = (!oyk_callsign_eq(&old_chat_c, &new_chat_c) ||
                           !oyk_callsign_eq(&cfg_.chat_alias, &next_cfg.chat_alias)) ? 1 : 0;
    int bbs_disabled = (cfg_.bbs_enabled && !next_cfg.bbs_enabled) ? 1 : 0;
    int chat_disabled = (cfg_.chat_enabled && !next_cfg.chat_enabled) ? 1 : 0;
    int identity_restart = (node_id_changed || bbs_id_changed || chat_id_changed) ? 1 : 0;

    int ifaces_changed = (cfg_.iface_count != next_cfg.iface_count) ? 1 : 0;
    if (!ifaces_changed) {
        for (int i = 0; i < cfg_.iface_count; ++i) {
            if (!portable_iface_eq(&cfg_.ifaces[i], &next_cfg.ifaces[i])) {
                ifaces_changed = 1;
                break;
            }
        }
    }

    res->identity_restarted = identity_restart;
    res->interfaces_changed = ifaces_changed;

    char new_node_s[16], new_bbs_s[16], new_chat_s[16];
    oyk_format_callsign(&next_cfg.callsign, new_node_s, sizeof(new_node_s));
    oyk_format_callsign(&new_bbs_c, new_bbs_s, sizeof(new_bbs_s));
    oyk_format_callsign(&new_chat_c, new_chat_s, sizeof(new_chat_s));

    /* Phase 1: Temporarily disable new incoming connections & sign off affected sessions */
    if (identity_restart) {
        accept_incoming_ = 0;
    }

    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        OykSession* s = &sessions_[i];
        if (s->state != OYK_SESS_CONNECTED && s->state != OYK_SESS_CONNECTING) continue;

        int iface_removed_or_changed = 0;
        if (s->iface_idx >= 0 && s->iface_idx < iface_count_) {
            int found_same = 0;
            for (int k = 0; k < next_cfg.iface_count; ++k) {
                if (portable_iface_eq(&ifaces_[s->iface_idx].cfg, &next_cfg.ifaces[k])) {
                    found_same = 1;
                    break;
                }
            }
            if (!found_same) iface_removed_or_changed = 1;
        }

        int must_signoff = identity_restart ||
                           (bbs_disabled && s->in_bbs) ||
                           (chat_disabled && s->in_chat) ||
                           iface_removed_or_changed;
        if (!must_signoff) continue;

        char signoff[176];
        if (s->in_bbs) {
            if (next_cfg.bbs_enabled) {
                sprintf(signoff,
                        "\r*** Node is restarting for configuration changes. "
                        "BBS will be accessible on %s after restart. 73!\r",
                        new_bbs_s);
            } else {
                strcpy(signoff,
                       "\r*** Node is restarting for configuration changes. "
                       "BBS will not be available after this change. 73!\r");
            }
        } else if (s->in_chat) {
            if (next_cfg.chat_enabled) {
                sprintf(signoff,
                        "\r*** Node is restarting for configuration changes. "
                        "CHAT will be accessible on %s after restart. 73!\r",
                        new_chat_s);
            } else {
                strcpy(signoff,
                       "\r*** Node is restarting for configuration changes. "
                       "CHAT will not be available after this change. 73!\r");
            }
        } else {
            sprintf(signoff,
                    "\r*** Node is restarting for configuration changes. "
                    "Node will be accessible on %s after restart. 73!\r",
                    new_node_s);
        }

        if (s->in_bbs) bbs_cleanup_yapp(s);
        if (s->in_chat) chat_leave(s, "Node restarting for configuration changes");

        if (s->state == OYK_SESS_CONNECTED) {
            send_text(s, signoff);
        }
        oyk_u32 sid = s->id;
        int was_local = (s->is_local_loopback || s->is_local_term) ? 1 : 0;
        if (!s->is_local_loopback) {
            send_disc(s);
        }
        s->state = OYK_SESS_FREE;
        if (was_local && local_cb_) {
            local_cb_(sid, 3, NULL, 0, "Configuration change restart", local_ctx_);
        }
        res->signed_off_sessions++;
    }

    /* Phase 2: Attempt to save config file (apply in memory even if read-only) */
    const char* target_path = next_cfg.source_path[0] ? next_cfg.source_path : "node.cfg";
    safe_copy(res->save_path, sizeof(res->save_path), target_path);
    if (save_to_disk) {
        if (oyk_config_save(&next_cfg, target_path)) {
            res->saved_to_disk = 1;
            safe_copy(next_cfg.source_path, sizeof(next_cfg.source_path), target_path);
        } else {
            res->saved_to_disk = 0;
            safe_copy(res->warning, sizeof(res->warning),
                      "Config file not writable; changes remain in memory only and will be lost on exit.");
        }
    }

    /* Phase 3: Apply running configuration */
    int mail_path_changed = (strcmp(cfg_.bbs_mail_path, next_cfg.bbs_mail_path) != 0) ? 1 : 0;
    ApplyOldIfacesGuard old_guard;
    OykIfaceRuntime* old_ifaces = old_guard.p;
    int old_iface_count = iface_count_;
    if (!old_ifaces) {
        safe_copy(res->error, sizeof(res->error), "Out of memory.");
        return 0;
    }
    memcpy(old_ifaces, ifaces_, sizeof(ifaces_));

    cfg_ = next_cfg;

    iface_count_ = cfg_.iface_count;
    for (int i = 0; i < iface_count_; ++i) {
        int matched = -1;
        for (int j = 0; j < old_iface_count; ++j) {
            if (portable_iface_eq(&old_ifaces[j].cfg, &cfg_.ifaces[i])) {
                matched = j;
                break;
            }
        }
        if (matched >= 0) {
            ifaces_[i] = old_ifaces[matched];
            ifaces_[i].cfg = cfg_.ifaces[i];
        } else {
            memset(&ifaces_[i], 0, sizeof(ifaces_[i]));
            ifaces_[i].cfg = cfg_.ifaces[i];
            ifaces_[i].running = 0;
            safe_copy(ifaces_[i].status, sizeof(ifaces_[i].status), "INIT");
        }
    }

    if (cfg_.bbs_enabled && mail_path_changed) {
        bbs_load_index();
    }

    /* Restore incoming state respecting manual operator disable */
    if (manual_deny_incoming_) {
        accept_incoming_ = 0;
    } else {
        accept_incoming_ = cfg_.accept_incoming ? 1 : 0;
    }

    if (res->warning[0]) {
        log_message(OYK_LOG_WARNING, res->warning);
    }
    char lmsg[104];
    sprintf(lmsg, "Config applied (%s BBS:%s CHAT:%s Files:%s)",
            new_node_s,
            cfg_.bbs_enabled ? new_bbs_s : "OFF",
            cfg_.chat_enabled ? new_chat_s : "OFF",
            cfg_.bbs_files_enabled ? "ON" : "OFF");
    log_message(OYK_LOG_INFO, lmsg);

    res->applied = 1;
    return 1;
}

int OykPortableNode::auto_learned_peer_count() const {
    int cnt = 0;
    for (int i = 0; i < cfg_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        if (cfg_.peers[i].auto_learned) ++cnt;
    }
    return cnt;
}

int OykPortableNode::persist_auto_learned_peers(char* err_out, size_t err_sz) {
    for (int i = 0; i < cfg_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        cfg_.peers[i].auto_learned = 0;
    }
    const char* target = cfg_.source_path[0] ? cfg_.source_path : "node.cfg";
    if (!oyk_config_save(&cfg_, target)) {
        if (err_out && err_sz > 0) safe_copy(err_out, err_sz, "Unable to save config");
        return 0;
    }
    return 1;
}

void OykPortableNode::discard_auto_learned_peers() {
    int keep = 0;
    for (int i = 0; i < cfg_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        if (!cfg_.peers[i].auto_learned) {
            if (keep != i) cfg_.peers[keep] = cfg_.peers[i];
            ++keep;
        }
    }
    cfg_.peer_count = keep;
}

int OykPortableNode::terminate_session(oyk_u32 session_id, char* msg_out, size_t msg_sz) {
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED && sessions_[i].id == session_id) {
            char cs[12];
            oyk_format_callsign(&sessions_[i].remote_call, cs, sizeof(cs));
            send_disc(&sessions_[i]);
            sessions_[i].state = OYK_SESS_FREE;
            char msg[72];
            sprintf(msg, "Terminated session #%lu (%s)",
                    static_cast<unsigned long>(session_id), cs);
            log_message(OYK_LOG_KICK, msg);
            if (msg_out) safe_copy(msg_out, msg_sz, msg);
            return 1;
        }
    }
    if (msg_out) safe_copy(msg_out, msg_sz, "Session not found");
    return 0;
}

int OykPortableNode::ban_callsign(const char* call_spec, oyk_u32 flags, const char* reason,
                                  const char* lift_spec, char* msg_out, size_t msg_sz) {
    char key[12];
    if (!normalize_ban_key(call_spec, key, sizeof(key))) {
        if (msg_out) safe_copy(msg_out, msg_sz, "Invalid callsign");
        return 0;
    }

    char parsed_lift[20];
    if (!oyk_parse_lift_spec(lift_spec, now_time(), parsed_lift, sizeof(parsed_lift))) {
        if (msg_out) safe_copy(msg_out, msg_sz, "Invalid lift (use permanent, 30m, 12h, 7d, YYYY-MM-DD)");
        return 0;
    }

    int idx = -1;
    for (int i = 0; i < ban_count_; ++i) {
        if (strcmp(bans_[i].key, key) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (ban_count_ >= OYK_MAX_BANS) {
            if (msg_out) safe_copy(msg_out, msg_sz, "Ban list full");
            return 0;
        }
        idx = ban_count_++;
    }

    OykBanEntry* e = &bans_[idx];
    memset(e, 0, sizeof(*e));
    safe_copy(e->key, sizeof(e->key), key);
    oyk_format_now_compact(now_time(), e->banned_at, sizeof(e->banned_at));
    safe_copy(e->lift_at, sizeof(e->lift_at), parsed_lift);
    e->flags = flags;
    safe_copy(e->reason, sizeof(e->reason),
              (reason && reason[0]) ? reason : "Banned by sysop.");
    int ban_saved = save_banlist();

    /* Disconnect any matching active sessions */
    char now_c[16];
    oyk_format_now_compact(now_time(), now_c, sizeof(now_c));
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED &&
            find_ban_index(&sessions_[i].remote_call, now_c) >= 0) {
            send_disc(&sessions_[i]);
            sessions_[i].state = OYK_SESS_FREE;
        }
    }

    char msg[96];
    if (ban_saved) {
        sprintf(msg, "Banned %s: %.48s", key, e->reason);
    } else {
        sprintf(msg, "Banned %s (in memory only; banlist not writable)", key);
    }
    log_message(OYK_LOG_BAN, msg);
    if (msg_out) safe_copy(msg_out, msg_sz, msg);
    return 1;
}

int OykPortableNode::unban_callsign(const char* call_spec, char* msg_out, size_t msg_sz) {
    char key[12];
    if (!normalize_ban_key(call_spec, key, sizeof(key))) {
        if (msg_out) safe_copy(msg_out, msg_sz, "Invalid callsign");
        return 0;
    }
    for (int i = 0; i < ban_count_; ++i) {
        if (strcmp(bans_[i].key, key) == 0) {
            for (int j = i + 1; j < ban_count_; ++j) {
                bans_[j - 1] = bans_[j];
            }
            --ban_count_;
            int ban_saved = save_banlist();
            char msg[80];
            if (ban_saved) {
                sprintf(msg, "Unbanned %s", key);
            } else {
                sprintf(msg, "Unbanned %s (in memory only; banlist not writable)", key);
            }
            log_message(OYK_LOG_BAN, msg);
            if (msg_out) safe_copy(msg_out, msg_sz, msg);
            return 1;
        }
    }
    if (msg_out) safe_copy(msg_out, msg_sz, "Callsign not banned");
    return 0;
}

int OykPortableNode::active_connections_on(int iface_idx) const {
    int n = 0;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED && sessions_[i].iface_idx == iface_idx) {
            ++n;
        }
    }
    return n;
}

int OykPortableNode::total_active_connections() const {
    int n = 0;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (sessions_[i].state == OYK_SESS_CONNECTED) ++n;
    }
    return n;
}

oyk_u32 OykPortableNode::uptime_seconds() const {
    time_t cur = now_time();
    return (cur >= started_at_) ? static_cast<oyk_u32>(cur - started_at_) : 0;
}
