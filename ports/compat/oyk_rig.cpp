/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_rig.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rig_stricmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower(static_cast<unsigned char>(*a));
        int cb = tolower(static_cast<unsigned char>(*b));
        if (ca != cb) return ca - cb;
        ++a;
        ++b;
    }
    return tolower(static_cast<unsigned char>(*a)) - tolower(static_cast<unsigned char>(*b));
}

static void rig_safe_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void rig_trim(char* s) {
    if (!s) return;
    char* p = s;
    while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace(static_cast<unsigned char>(s[len - 1]))) {
        s[--len] = '\0';
    }
}

static int rig_parse_long_strict(const char* text, long* out) {
    if (!text || !out) return 0;
    char buf[32];
    rig_safe_copy(buf, sizeof(buf), text);
    rig_trim(buf);
    if (!buf[0]) return 0;
    char* endp = NULL;
    long v = strtol(buf, &endp, 10);
    if (!endp || *endp != '\0') return 0;
    *out = v;
    return 1;
}

/* Parse Hz, kHz, or MHz decimal strings without floating-point rounding bugs on 16-bit DOS */
static oyk_u32 rig_parse_freq_hz(const char* raw) {
    if (!raw) return 0UL;
    char buf[40];
    rig_safe_copy(buf, sizeof(buf), raw);
    rig_trim(buf);
    size_t len = strlen(buf);
    if (len == 0) return 0UL;

    int mult_kind = 0; /* 0=auto, 1=Hz, 2=kHz, 3=MHz */
    if (len >= 3 && rig_stricmp(buf + len - 3, "mhz") == 0) {
        mult_kind = 3;
        buf[len - 3] = '\0';
        rig_trim(buf);
    } else if (len >= 3 && rig_stricmp(buf + len - 3, "khz") == 0) {
        mult_kind = 2;
        buf[len - 3] = '\0';
        rig_trim(buf);
    } else if (len >= 2 && rig_stricmp(buf + len - 2, "hz") == 0) {
        mult_kind = 1;
        buf[len - 2] = '\0';
        rig_trim(buf);
    }
    if (!buf[0]) return 0UL;

    const char* dot = strchr(buf, '.');
    if (!dot) {
        char* endp = NULL;
        unsigned long whole = strtoul(buf, &endp, 10);
        if (!endp || *endp != '\0') return 0UL;
        if (mult_kind == 3) return static_cast<oyk_u32>(whole * 1000000UL);
        if (mult_kind == 2) return static_cast<oyk_u32>(whole * 1000UL);
        return static_cast<oyk_u32>(whole);
    }

    char int_part[16];
    size_t ilen = static_cast<size_t>(dot - buf);
    if (ilen >= sizeof(int_part)) return 0UL;
    memcpy(int_part, buf, ilen);
    int_part[ilen] = '\0';

    unsigned long whole = ilen > 0 ? strtoul(int_part, NULL, 10) : 0UL;
    const char* frac = dot + 1;
    unsigned long scale = 1000000UL; /* Default dotted frequency like 446.000 is MHz */
    if (mult_kind == 2) scale = 1000UL;
    else if (mult_kind == 1) scale = 1UL;
    else if (mult_kind == 0 && whole >= 100000UL) scale = 1UL;

    unsigned long frac_val = 0UL;
    unsigned long place = scale;
    while (*frac && isdigit(static_cast<unsigned char>(*frac)) && place >= 10UL) {
        place /= 10UL;
        frac_val += static_cast<unsigned long>(*frac - '0') * place;
        ++frac;
    }
    return static_cast<oyk_u32>(whole * scale + frac_val);
}

int oyk_rig_parse_protocol(const char* text) {
    if (!text) return OYK_RIG_PROTO_NONE;
    char buf[24];
    rig_safe_copy(buf, sizeof(buf), text);
    rig_trim(buf);
    if (rig_stricmp(buf, "rigctl") == 0 || rig_stricmp(buf, "rigctld") == 0 ||
        rig_stricmp(buf, "hamlib") == 0) {
        return OYK_RIG_PROTO_RIGCTL;
    }
    if (rig_stricmp(buf, "radiofoundry") == 0 || rig_stricmp(buf, "takrf") == 0 ||
        rig_stricmp(buf, "rf-udp") == 0 || rig_stricmp(buf, "udp4191") == 0) {
        return OYK_RIG_PROTO_RADIOFOUNDRY;
    }
    return OYK_RIG_PROTO_NONE;
}

const char* oyk_rig_protocol_name(int proto) {
    if (proto == OYK_RIG_PROTO_RIGCTL) return "rigctl";
    if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) return "radiofoundry";
    return "none";
}

oyk_u16 oyk_rig_default_port(int proto) {
    if (proto == OYK_RIG_PROTO_RIGCTL) return static_cast<oyk_u16>(OYK_RIGCTL_DEFAULT_PORT);
    if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) return static_cast<oyk_u16>(OYK_RF_DEFAULT_PORT);
    return 0;
}

void oyk_rig_status_init(OykRigStatus* st, int proto) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->protocol = proto;
    st->ptt = -1;
    st->zone = -1;
    st->pos = -1;
    st->color_code = -1;
    st->timeslot = -1;
    st->talkgroup = -1L;
}

size_t oyk_rigctl_build_get_freq(char* out, size_t cap) {
    if (!out || cap < 3) return 0;
    strcpy(out, "f\n");
    return 2;
}

size_t oyk_rigctl_build_set_freq(oyk_u32 hz, char* out, size_t cap) {
    if (!out || cap < 16) return 0;
    sprintf(out, "F %lu\n", static_cast<unsigned long>(hz));
    return strlen(out);
}

size_t oyk_rigctl_build_get_mode(char* out, size_t cap) {
    if (!out || cap < 3) return 0;
    strcpy(out, "m\n");
    return 2;
}

size_t oyk_rigctl_build_set_mode(const char* mode, oyk_u32 passband_hz, char* out, size_t cap) {
    if (!out || !mode || !mode[0] || cap < 24) return 0;
    char m[16];
    rig_safe_copy(m, sizeof(m), mode);
    rig_trim(m);
    if (!m[0]) return 0;
    sprintf(out, "M %s %lu\n", m, static_cast<unsigned long>(passband_hz));
    return strlen(out);
}

size_t oyk_rigctl_build_get_ptt(char* out, size_t cap) {
    if (!out || cap < 3) return 0;
    strcpy(out, "t\n");
    return 2;
}

size_t oyk_rigctl_build_set_ptt(int tx, char* out, size_t cap) {
    if (!out || cap < 5) return 0;
    strcpy(out, tx ? "T 1\n" : "T 0\n");
    return 4;
}

size_t oyk_rigctl_build_get_vfo(char* out, size_t cap) {
    if (!out || cap < 3) return 0;
    strcpy(out, "v\n");
    return 2;
}

size_t oyk_rigctl_build_set_vfo(const char* vfo, char* out, size_t cap) {
    if (!out || !vfo || !vfo[0] || cap < 20) return 0;
    char v[16];
    rig_safe_copy(v, sizeof(v), vfo);
    rig_trim(v);
    if (!v[0]) return 0;
    sprintf(out, "V %s\n", v);
    return strlen(out);
}

static int next_reply_line(const char** cur, char* line_out, size_t line_sz) {
    if (!cur || !*cur || !**cur) return 0;
    const char* p = *cur;
    while (*p == '\r' || *p == '\n') ++p;
    if (!*p) { *cur = p; return 0; }
    size_t n = 0;
    while (*p && *p != '\r' && *p != '\n') {
        if (n + 1 < line_sz) line_out[n++] = *p;
        ++p;
    }
    line_out[n] = '\0';
    *cur = p;
    rig_trim(line_out);
    return 1;
}

int oyk_rigctl_parse_rprt(const char* line, int* code_out) {
    if (!line) return 0;
    char buf[48];
    rig_safe_copy(buf, sizeof(buf), line);
    rig_trim(buf);
    if (strncmp(buf, "RPRT ", 5) != 0) return 0;
    long code = 0L;
    if (!rig_parse_long_strict(buf + 5, &code)) return 0;
    if (code_out) *code_out = static_cast<int>(code);
    return 1;
}

int oyk_rigctl_parse_freq_reply(const char* reply, oyk_u32* hz_out, int* rprt_out) {
    if (rprt_out) *rprt_out = 0;
    if (!reply || !hz_out) return 0;
    const char* p = reply;
    char line[64];
    if (!next_reply_line(&p, line, sizeof(line))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(line, &rprt)) {
        if (rprt_out) *rprt_out = rprt;
        return 0;
    }
    oyk_u32 hz = rig_parse_freq_hz(line);
    if (hz == 0UL && strcmp(line, "0") != 0) return 0;
    *hz_out = hz;
    return 1;
}

int oyk_rigctl_parse_mode_reply(const char* reply, char* mode_out, size_t mode_sz,
                                oyk_u32* passband_out, int* rprt_out) {
    if (rprt_out) *rprt_out = 0;
    if (!reply || !mode_out || mode_sz == 0) return 0;
    const char* p = reply;
    char l1[64], l2[64];
    if (!next_reply_line(&p, l1, sizeof(l1))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(l1, &rprt)) {
        if (rprt_out) *rprt_out = rprt;
        return 0;
    }
    rig_safe_copy(mode_out, mode_sz, l1);
    if (passband_out) {
        *passband_out = 0UL;
        if (next_reply_line(&p, l2, sizeof(l2)) && !oyk_rigctl_parse_rprt(l2, &rprt)) {
            long pb = 0L;
            if (rig_parse_long_strict(l2, &pb) && pb >= 0L) {
                *passband_out = static_cast<oyk_u32>(pb);
            }
        }
    }
    return 1;
}

int oyk_rigctl_parse_ptt_reply(const char* reply, int* ptt_out, int* rprt_out) {
    if (rprt_out) *rprt_out = 0;
    if (!reply || !ptt_out) return 0;
    const char* p = reply;
    char line[64];
    if (!next_reply_line(&p, line, sizeof(line))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(line, &rprt)) {
        if (rprt_out) *rprt_out = rprt;
        return 0;
    }
    long v = 0L;
    if (!rig_parse_long_strict(line, &v)) return 0;
    *ptt_out = (v != 0L) ? 1 : 0;
    return 1;
}

int oyk_rigctl_parse_vfo_reply(const char* reply, char* vfo_out, size_t vfo_sz, int* rprt_out) {
    if (rprt_out) *rprt_out = 0;
    if (!reply || !vfo_out || vfo_sz == 0) return 0;
    const char* p = reply;
    char line[64];
    if (!next_reply_line(&p, line, sizeof(line))) return 0;
    int rprt = 0;
    if (oyk_rigctl_parse_rprt(line, &rprt)) {
        if (rprt_out) *rprt_out = rprt;
        return 0;
    }
    rig_safe_copy(vfo_out, vfo_sz, line);
    return 1;
}

size_t oyk_rf_build_select(int zone, int pos, char* out, size_t cap) {
    if (!out || cap < 8) return 0;
    if (zone < 0) {
        strcpy(out, "select");
        return 6;
    }
    if (pos < 0) {
        if (cap < 20) return 0;
        sprintf(out, "select?zone=%d", zone);
        return strlen(out);
    }
    if (cap < 32) return 0;
    sprintf(out, "select?zone=%d&pos=%d", zone, pos);
    return strlen(out);
}

size_t oyk_rf_build_stop_notify(char* out, size_t cap) {
    if (!out || cap < 16) return 0;
    strcpy(out, "select?notify=0");
    return 15;
}

size_t oyk_rf_build_step_channel(int delta, char* out, size_t cap) {
    if (!out || cap < 24) return 0;
    sprintf(out, "step?channel=%d", delta);
    return strlen(out);
}

size_t oyk_rf_build_step_zone(int delta, char* out, size_t cap) {
    if (!out || cap < 20) return 0;
    sprintf(out, "step?zone=%d", delta);
    return strlen(out);
}

static void strip_quotes(char* s) {
    rig_trim(s);
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static int apply_rf_kv(const char* raw_k, const char* raw_v, OykRigStatus* st) {
    char k[24], v[48];
    rig_safe_copy(k, sizeof(k), raw_k);
    rig_safe_copy(v, sizeof(v), raw_v);
    strip_quotes(k);
    strip_quotes(v);
    if (!k[0] || !v[0]) return 0;

    long lv = 0L;
    if (rig_stricmp(k, "zone") == 0 || rig_stricmp(k, "z") == 0) {
        if (rig_parse_long_strict(v, &lv)) { st->zone = static_cast<int>(lv); return 1; }
    } else if (rig_stricmp(k, "pos") == 0 || rig_stricmp(k, "ch") == 0 ||
               rig_stricmp(k, "channel") == 0 || rig_stricmp(k, "index") == 0) {
        if (rig_parse_long_strict(v, &lv)) { st->pos = static_cast<int>(lv); return 1; }
    } else if (rig_stricmp(k, "name") == 0 || rig_stricmp(k, "ch_name") == 0 ||
               rig_stricmp(k, "channel_name") == 0 || rig_stricmp(k, "label") == 0) {
        rig_safe_copy(st->channel_name, sizeof(st->channel_name), v);
        return 1;
    } else if (rig_stricmp(k, "freq") == 0 || rig_stricmp(k, "frequency") == 0 ||
               rig_stricmp(k, "rx_freq") == 0 || rig_stricmp(k, "f") == 0) {
        oyk_u32 hz = rig_parse_freq_hz(v);
        if (hz > 0UL) { st->freq_hz = hz; return 1; }
    } else if (rig_stricmp(k, "mode") == 0) {
        rig_safe_copy(st->mode, sizeof(st->mode), v);
        if (rig_stricmp(v, "dmr") == 0) st->is_dmr = 1;
        return 1;
    } else if (rig_stricmp(k, "cc") == 0 || rig_stricmp(k, "color_code") == 0 ||
               rig_stricmp(k, "colorcode") == 0) {
        if (rig_parse_long_strict(v, &lv)) {
            st->color_code = static_cast<int>(lv);
            st->is_dmr = 1;
            return 1;
        }
    } else if (rig_stricmp(k, "ts") == 0 || rig_stricmp(k, "timeslot") == 0 ||
               rig_stricmp(k, "slot") == 0) {
        if (rig_parse_long_strict(v, &lv)) {
            st->timeslot = static_cast<int>(lv);
            st->is_dmr = 1;
            return 1;
        }
    } else if (rig_stricmp(k, "tg") == 0 || rig_stricmp(k, "talkgroup") == 0) {
        if (rig_parse_long_strict(v, &lv)) {
            st->talkgroup = lv;
            st->is_dmr = 1;
            return 1;
        }
    }
    return 0;
}

int oyk_rf_parse_status(const char* text, OykRigStatus* st) {
    if (!text || !st) return 0;
    char work[256];
    rig_safe_copy(work, sizeof(work), text);
    rig_trim(work);
    if (!work[0]) return 0;

    rig_safe_copy(st->raw_status, sizeof(st->raw_status), work);
    st->protocol = OYK_RIG_PROTO_RADIOFOUNDRY;

    size_t len = strlen(work);
    if (len >= 2 && work[0] == '{' && work[len - 1] == '}') {
        work[len - 1] = '\0';
        memmove(work, work + 1, len - 1);
    }

    int parsed_any = 0;

    if (strchr(work, '\n') != NULL) {
        const char* p = work;
        char line[96];
        while (next_reply_line(&p, line, sizeof(line))) {
            char* sep = strchr(line, '=');
            if (!sep) sep = strchr(line, ':');
            if (sep) {
                *sep = '\0';
                if (apply_rf_kv(line, sep + 1, st)) parsed_any = 1;
            }
        }
    }

    if (!parsed_any) {
        const char* p = work;
        while (*p) {
            while (*p && (isspace(static_cast<unsigned char>(*p)) ||
                          *p == '&' || *p == ',' || *p == ';')) {
                ++p;
            }
            if (!*p) break;

            char k[24];
            size_t kn = 0;
            while (*p && *p != '=' && *p != ':' && *p != '&' && *p != ',' && *p != ';' &&
                   !isspace(static_cast<unsigned char>(*p))) {
                if (kn + 1 < sizeof(k)) k[kn++] = *p;
                ++p;
            }
            k[kn] = '\0';
            while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
            if (*p != '=' && *p != ':') {
                while (*p && !isspace(static_cast<unsigned char>(*p)) &&
                       *p != '&' && *p != ',' && *p != ';') {
                    ++p;
                }
                continue;
            }
            ++p;
            while (*p && isspace(static_cast<unsigned char>(*p))) ++p;

            char v[48];
            size_t vn = 0;
            if (*p == '"') {
                ++p;
                while (*p && *p != '"') {
                    if (vn + 1 < sizeof(v)) v[vn++] = *p;
                    ++p;
                }
                if (*p == '"') ++p;
            } else {
                while (*p && *p != '&' && *p != ',' && *p != ';' &&
                       !isspace(static_cast<unsigned char>(*p))) {
                    if (vn + 1 < sizeof(v)) v[vn++] = *p;
                    ++p;
                }
            }
            v[vn] = '\0';
            if (apply_rf_kv(k, v, st)) parsed_any = 1;
        }
    }

    return (parsed_any || work[0] != '\0') ? 1 : 0;
}

void oyk_rig_format_summary(const OykRigStatus* st, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!st) return;

    char buf[140];
    if (st->protocol == OYK_RIG_PROTO_RADIOFOUNDRY) {
        if (st->zone >= 0 || st->pos >= 0 || st->channel_name[0] || st->freq_hz > 0UL) {
            if (st->is_dmr) {
                sprintf(buf, "Z%d/Ch%d %.18s %luHz CC%d TS%d TG%ld",
                        st->zone, st->pos,
                        st->channel_name[0] ? st->channel_name : "-",
                        static_cast<unsigned long>(st->freq_hz),
                        st->color_code, st->timeslot, st->talkgroup);
            } else {
                sprintf(buf, "Z%d/Ch%d %.20s %lu Hz",
                        st->zone, st->pos,
                        st->channel_name[0] ? st->channel_name : "-",
                        static_cast<unsigned long>(st->freq_hz));
            }
        } else if (st->raw_status[0]) {
            rig_safe_copy(buf, sizeof(buf), st->raw_status);
        } else {
            strcpy(buf, "No status received");
        }
    } else if (st->protocol == OYK_RIG_PROTO_RIGCTL) {
        sprintf(buf, "%lu Hz %s (%lu Hz) %s [%s]",
                static_cast<unsigned long>(st->freq_hz),
                st->mode[0] ? st->mode : "-",
                static_cast<unsigned long>(st->passband_hz),
                st->vfo[0] ? st->vfo : "",
                st->ptt > 0 ? "TX" : (st->ptt == 0 ? "RX" : "?"));
    } else {
        strcpy(buf, "Disabled");
    }
    rig_safe_copy(out, out_sz, buf);
}
