/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_portable.hpp"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define OYK_MKDIR(p) _mkdir(p)
#elif defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
#include <direct.h>
#define OYK_MKDIR(p) mkdir(p)
#else
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

void oyk_config_init_default(OykConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    oyk_parse_callsign("N0CALL-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    safe_copy(cfg->info_text, sizeof(cfg->info_text), "OYK Portable Amateur Radio Packet Node");
    safe_copy(cfg->log_path, sizeof(cfg->log_path), "logs");
    cfg->console_level = OYK_DEFAULT_CONSOLE_MASK;
    cfg->rotate_daily = 1;
    cfg->accept_incoming = 1;
    cfg->line_ending = oyk_platform_default_eol();
    cfg->has_detected_eol = 0;

    cfg->iface_count = 1;
    safe_copy(cfg->ifaces[0].id, sizeof(cfg->ifaces[0].id), "axudp0");
    safe_copy(cfg->ifaces[0].type, sizeof(cfg->ifaces[0].type), "axudp");
    safe_copy(cfg->ifaces[0].description, sizeof(cfg->ifaces[0].description), "Primary AXUDP Link");
    safe_copy(cfg->ifaces[0].host, sizeof(cfg->ifaces[0].host), "0.0.0.0");
    cfg->ifaces[0].port = 10093;
}

void oyk_config_resolve_banlist(const OykConfig* cfg, char* out, size_t out_sz) {
    if (cfg->banlist_path[0] != '\0') {
        safe_copy(out, out_sz, cfg->banlist_path);
        return;
    }
    if (cfg->source_path[0] != '\0') {
        char dir[128];
        safe_copy(dir, sizeof(dir), cfg->source_path);
        char* slash1 = strrchr(dir, '/');
        char* slash2 = strrchr(dir, '\\');
        char* last = (slash1 > slash2) ? slash1 : slash2;
        if (last) {
            *(last + 1) = '\0';
            join_path(dir, "banlist.cfg", out, out_sz);
            return;
        }
    }
    safe_copy(out, out_sz, "banlist.cfg");
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

    int section = 0; /* 0=node, 1=logfiles, 2=interface */
    OykIfaceConfig* cur_if = NULL;

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
                section = 0;
                cur_if = NULL;
            } else if (stricmp_ascii(sec, "logfiles") == 0) {
                section = 1;
                cur_if = NULL;
            } else if (strlen(sec) > 9 &&
                       (sec[0] == 'i' || sec[0] == 'I') &&
                       (sec[8] == 'e' || sec[8] == 'E')) {
                char* idp = sec + 9;
                trim_inplace(idp);
                if (*idp == ':') { ++idp; trim_inplace(idp); }
                if (cfg->iface_count < OYK_MAX_INTERFACES) {
                    cur_if = &cfg->ifaces[cfg->iface_count++];
                    memset(cur_if, 0, sizeof(*cur_if));
                    safe_copy(cur_if->id, sizeof(cur_if->id), idp);
                    safe_copy(cur_if->host, sizeof(cur_if->host), "0.0.0.0");
                    cur_if->port = 0;
                    cur_if->baud = 9600;
                    section = 2;
                }
            }
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = line;
        char* v = eq + 1;
        trim_inplace(k);
        trim_inplace(v);

        if (section == 0) {
            if (stricmp_ascii(k, "callsign") == 0) {
                oyk_parse_callsign(v, &cfg->callsign);
            } else if (stricmp_ascii(k, "alias") == 0) {
                oyk_parse_callsign(v, &cfg->alias);
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
            } else if (stricmp_ascii(k, "accept_incoming") == 0) {
                cfg->accept_incoming = (stricmp_ascii(v, "false") != 0 && strcmp(v, "0") != 0) ? 1 : 0;
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
            } else if (stricmp_ascii(k, "description") == 0) {
                safe_copy(cur_if->description, sizeof(cur_if->description), v);
            } else if (stricmp_ascii(k, "host") == 0 || stricmp_ascii(k, "bind_host") == 0) {
                safe_copy(cur_if->host, sizeof(cur_if->host), v);
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
            }
        }
    }

    free(raw);

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

    if (cfg->iface_count == 0) {
        cfg->iface_count = 1;
        safe_copy(cfg->ifaces[0].id, sizeof(cfg->ifaces[0].id), "axudp0");
        safe_copy(cfg->ifaces[0].type, sizeof(cfg->ifaces[0].type), "axudp");
        safe_copy(cfg->ifaces[0].description, sizeof(cfg->ifaces[0].description), "Primary AXUDP Link");
        safe_copy(cfg->ifaces[0].host, sizeof(cfg->ifaces[0].host), "0.0.0.0");
        cfg->ifaces[0].port = 10093;
    }
    return 1;
}

int oyk_config_save(const OykConfig* cfg, const char* path) {
    const char* target = (path && path[0]) ? path : cfg->source_path;
    if (!target || !target[0]) return 0;
    FILE* fp = fopen(target, "wb");
    if (!fp) return 0;

    const char* nl = oyk_eol_bytes(cfg->line_ending);
    char call_s[16], alias_s[16];
    oyk_format_callsign(&cfg->callsign, call_s, sizeof(call_s));
    oyk_format_callsign(&cfg->alias, alias_s, sizeof(alias_s));

    fprintf(fp, "[node]%s", nl);
    fprintf(fp, "callsign = %s%s", call_s, nl);
    if (alias_s[0]) fprintf(fp, "alias = %s%s", alias_s, nl);
    fprintf(fp, "info = %s%s", cfg->info_text, nl);
    fprintf(fp, "accept_incoming = %s%s", cfg->accept_incoming ? "true" : "false", nl);
    fprintf(fp, "logpath = %s%s", cfg->log_path, nl);
    fprintf(fp, "consolelevel = 0x%lx%s", static_cast<unsigned long>(cfg->console_level), nl);
    fprintf(fp, "log_rotate_daily = %s%s", cfg->rotate_daily ? "true" : "false", nl);
    if (cfg->banlist_path[0]) fprintf(fp, "banlist = %s%s", cfg->banlist_path, nl);

    if (cfg->log_file_count > 0) {
        fprintf(fp, "%s[logfiles]%s", nl, nl);
        for (int i = 0; i < cfg->log_file_count; ++i) {
            fprintf(fp, "%s = 0x%lx%s", cfg->log_files[i].filename,
                    static_cast<unsigned long>(cfg->log_files[i].mask), nl);
        }
    }

    for (int i = 0; i < cfg->iface_count; ++i) {
        const OykIfaceConfig* ic = &cfg->ifaces[i];
        fprintf(fp, "%s[interface %s]%s", nl, ic->id, nl);
        fprintf(fp, "type = %s%s", ic->type, nl);
        if (ic->description[0]) fprintf(fp, "description = %s%s", ic->description, nl);
        if (ic->host[0]) fprintf(fp, "bind_host = %s%s", ic->host, nl);
        if (ic->port > 0) fprintf(fp, "bind_port = %u%s", static_cast<unsigned int>(ic->port), nl);
        if (ic->device[0]) fprintf(fp, "device = %s%s", ic->device, nl);
    }

    fclose(fp);
    return 1;
}

/* ==================== OykPortableNode ==================== */

OykPortableNode::OykPortableNode()
    : tx_cb_(NULL), tx_ctx_(NULL), started_at_(0), fixed_now_(0),
      accept_incoming_(1), next_session_id_(1), iface_count_(0),
      mheard_count_(0), new_heard_count_(0),
      banlist_eol_(oyk_platform_default_eol()),
      banlist_has_eol_(0), banlist_mixed_eol_(0),
      ban_count_(0), console_log_count_(0) {
    oyk_config_init_default(&cfg_);
    memset(ifaces_, 0, sizeof(ifaces_));
    memset(sessions_, 0, sizeof(sessions_));
    memset(mheard_, 0, sizeof(mheard_));
    memset(new_heard_, 0, sizeof(new_heard_));
    memset(bans_, 0, sizeof(bans_));
    memset(console_logs_, 0, sizeof(console_logs_));
    banlist_path_[0] = '\0';
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
    tx_cb_ = tx_cb;
    tx_ctx_ = tx_ctx;
    started_at_ = now_time();
    accept_incoming_ = cfg_.accept_incoming;

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

    make_dir_simple(cfg_.log_path);

    iface_count_ = cfg_.iface_count;
    for (int i = 0; i < iface_count_; ++i) {
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

    char call_s[16];
    oyk_format_callsign(&cfg_.callsign, call_s, sizeof(call_s));
    char msg[96];
    sprintf(msg, "Node %s online (%d bans loaded)", call_s, ban_count_);
    log_message(OYK_LOG_INFO, msg);
    return 1;
}

int OykPortableNode::begin_shutdown() {
    accept_incoming_ = 0;
    char node_s[16], signoff[80];
    oyk_format_callsign(&cfg_.callsign, node_s, sizeof(node_s));
    sprintf(signoff, "\r*** Node shutting down. 73 de %s\r", node_s);

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
    for (int i = 0; i < cfg_.log_file_count; ++i) {
        if ((level & cfg_.log_files[i].mask) != 0) {
            write_log_file_line(&cfg_.log_files[i], fline, today);
        }
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

    for (int i = 0; i < cfg_.log_file_count; ++i) {
        if ((OYK_LOG_APPEAL & cfg_.log_files[i].mask) != 0) {
            write_log_file_line(&cfg_.log_files[i], aline, today);
        }
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

void OykPortableNode::send_text(OykSession* sess, const char* text) {
    if (!tx_cb_ || !sess || !text) return;
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
    if (!tx_cb_ || !sess) return;
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

void OykPortableNode::on_rx_frame(int iface_idx, const OykFrame* frame,
                                  oyk_u32 peer_ip, oyk_u16 peer_port, oyk_u16 peer_handle) {
    if (iface_idx < 0 || iface_idx >= iface_count_ || !frame) return;
    ifaces_[iface_idx].rx_frames++;
    record_heard(&frame->src, iface_idx);

    OykFrameType ft = oyk_frame_type(frame);
    if (ft == OYK_FT_UI || !is_for_node(&frame->dst)) return;

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
            } else {
                char node_s[16], banner[140];
                oyk_format_callsign(&cfg_.callsign, node_s, sizeof(node_s));
                sprintf(banner, "*** Connected to %s Packet Node\rType HELP or ?\r%s> ",
                        node_s, node_s);
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
        sess->state = OYK_SESS_FREE;
        return;
    }

    if (ft == OYK_FT_DM || (ft == OYK_FT_UA && sess->state == OYK_SESS_DISCONNECTING)) {
        sess->state = OYK_SESS_FREE;
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

            for (oyk_u16 i = 0; i < frame->info_len; ++i) {
                oyk_u8 ch = frame->info[i];
                if (ch == '\r' || ch == '\n') {
                    sess->rx_line[sess->rx_line_len] = '\0';
                    if (sess->rx_line_len > 0 || ch == '\r') {
                        if (sess->restricted_ban) {
                            handle_restricted_line(sess, sess->rx_line);
                        } else {
                            handle_normal_command(sess, sess->rx_line);
                        }
                    }
                    sess->rx_line_len = 0;
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
        send_text(sess, "Commands: INFO, PORTS, USERS, MHEARD, BYE, HELP\r");
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
                        cs, ifaces_[sessions_[i].iface_idx].cfg.id);
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
    log_message(allow ? OYK_LOG_INFO : OYK_LOG_WARNING,
                allow ? "Incoming connections ALLOWED" : "Incoming connections DENIED");
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
    safe_copy(e->lift_at, sizeof(e->lift_at),
              (lift_spec && lift_spec[0]) ? lift_spec : "permanent");
    e->flags = flags;
    safe_copy(e->reason, sizeof(e->reason),
              (reason && reason[0]) ? reason : "Banned by sysop.");
    save_banlist();

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

    char msg[80];
    sprintf(msg, "Banned %s: %.48s", key, e->reason);
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
            save_banlist();
            char msg[48];
            sprintf(msg, "Unbanned %s", key);
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
