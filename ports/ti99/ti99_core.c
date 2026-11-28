/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#include "ti99_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ti99_strcpy_safe(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1 < cap && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int ti99_stricmp(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;
    while (*a && *b) {
        ca = (unsigned char)toupper((unsigned char)*a);
        cb = (unsigned char)toupper((unsigned char)*b);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        ++a;
        ++b;
    }
    ca = (unsigned char)toupper((unsigned char)*a);
    cb = (unsigned char)toupper((unsigned char)*b);
    return (int)ca - (int)cb;
}

void ti99_be16_write(uint8_t *dst, uint16_t val)
{
    dst[0] = (uint8_t)((val >> 8) & 0xFFU);
    dst[1] = (uint8_t)(val & 0xFFU);
}

uint16_t ti99_be16_read(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

void ti99_be32_write(uint8_t *dst, uint32_t val)
{
    dst[0] = (uint8_t)((val >> 24) & 0xFFUL);
    dst[1] = (uint8_t)((val >> 16) & 0xFFUL);
    dst[2] = (uint8_t)((val >> 8) & 0xFFUL);
    dst[3] = (uint8_t)(val & 0xFFUL);
}

uint32_t ti99_be32_read(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

uint16_t ti99_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    int bit;
    for (i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001U) {
                crc = (uint16_t)((crc >> 1) ^ 0x8408U);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return (uint16_t)(crc ^ 0xFFFFU);
}

int ti99_parse_callsign(const char *str, Ti99Callsign *out)
{
    size_t i = 0;
    size_t j = 0;
    int ssid = 0;
    if (!str || !out || !str[0]) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    for (i = 0; i < 6; ++i) {
        out->base[i] = ' ';
    }
    out->base[6] = '\0';
    i = 0;
    while (str[i] && str[i] != '-' && str[i] != ' ') {
        unsigned char c = (unsigned char)toupper((unsigned char)str[i]);
        if (!isalnum(c) || j >= 6) {
            return -1;
        }
        out->base[j++] = (char)c;
        ++i;
    }
    if (j == 0) {
        return -1;
    }
    if (str[i] == '-') {
        ++i;
        if (!isdigit((unsigned char)str[i])) {
            return -1;
        }
        while (isdigit((unsigned char)str[i])) {
            ssid = ssid * 10 + (str[i] - '0');
            ++i;
        }
        if (ssid < 0 || ssid > 15) {
            return -1;
        }
    }
    while (str[i] == ' ') {
        ++i;
    }
    if (str[i] != '\0') {
        return -1;
    }
    out->ssid = (uint8_t)ssid;
    return 0;
}

void ti99_format_callsign(const Ti99Callsign *cs, char *out, size_t cap)
{
    char tmp[12];
    size_t i;
    size_t n = 0;
    if (!cs || !out || cap == 0) {
        return;
    }
    for (i = 0; i < 6 && cs->base[i] != '\0' && cs->base[i] != ' '; ++i) {
        tmp[n++] = cs->base[i];
    }
    if (cs->ssid > 0) {
        tmp[n++] = '-';
        if (cs->ssid >= 10) {
            tmp[n++] = (char)('0' + (cs->ssid / 10));
        }
        tmp[n++] = (char)('0' + (cs->ssid % 10));
    }
    tmp[n] = '\0';
    ti99_strcpy_safe(out, cap, tmp);
}

static void encode_ax25_addr(const Ti99Callsign *cs, int is_last, int cr_bit, uint8_t *out)
{
    size_t i;
    for (i = 0; i < 6; ++i) {
        out[i] = (uint8_t)((uint8_t)cs->base[i] << 1);
    }
    out[6] = (uint8_t)(0x60U | (cr_bit ? 0x80U : 0x00U) |
                       ((uint8_t)(cs->ssid & 0x0FU) << 1) |
                       (is_last ? 0x01U : 0x00U));
}

size_t ti99_ax25_encode_ui(const char *src, const char *dst, const uint8_t *payload,
                           size_t payload_len, uint8_t *out, size_t cap)
{
    Ti99Callsign s;
    Ti99Callsign d;
    size_t total;
    if (!out || ti99_parse_callsign(src, &s) != 0 || ti99_parse_callsign(dst, &d) != 0) {
        return 0;
    }
    total = 16U + payload_len;
    if (total > cap) {
        return 0;
    }
    encode_ax25_addr(&d, 0, 1, out);
    encode_ax25_addr(&s, 1, 0, out + 7);
    out[14] = 0x03U; /* UI frame */
    out[15] = 0xF0U; /* No L3 */
    if (payload_len > 0 && payload) {
        memcpy(out + 16, payload, payload_len);
    }
    return total;
}

int ti99_ax25_decode_header(const uint8_t *frame, size_t len,
                            char *src_out, size_t src_cap,
                            char *dst_out, size_t dst_cap,
                            uint8_t *ctrl_out, uint8_t *pid_out)
{
    Ti99Callsign d;
    Ti99Callsign s;
    size_t i;
    if (!frame || len < 16) {
        return -1;
    }
    for (i = 0; i < 6; ++i) {
        d.base[i] = (char)(frame[i] >> 1);
        s.base[i] = (char)(frame[7 + i] >> 1);
    }
    d.base[6] = '\0';
    s.base[6] = '\0';
    d.ssid = (uint8_t)((frame[6] >> 1) & 0x0FU);
    s.ssid = (uint8_t)((frame[13] >> 1) & 0x0FU);
    if (dst_out) {
        ti99_format_callsign(&d, dst_out, dst_cap);
    }
    if (src_out) {
        ti99_format_callsign(&s, src_out, src_cap);
    }
    if (ctrl_out) {
        *ctrl_out = frame[14];
    }
    if (pid_out) {
        *pid_out = frame[15];
    }
    return 0;
}

size_t ti99_kiss_encode(const uint8_t *frame, size_t len, uint8_t port,
                        uint8_t *out, size_t cap)
{
    size_t pos = 0;
    size_t i;
    if (!out || cap < 4 || !frame || len == 0) {
        return 0;
    }
    out[pos++] = 0xC0U;
    out[pos++] = (uint8_t)((port & 0x0FU) << 4);
    for (i = 0; i < len; ++i) {
        uint8_t b = frame[i];
        if (b == 0xC0U) {
            if (pos + 2 >= cap) return 0;
            out[pos++] = 0xDBU;
            out[pos++] = 0xDCU;
        } else if (b == 0xDBU) {
            if (pos + 2 >= cap) return 0;
            out[pos++] = 0xDBU;
            out[pos++] = 0xDDU;
        } else {
            if (pos + 1 >= cap) return 0;
            out[pos++] = b;
        }
    }
    if (pos >= cap) {
        return 0;
    }
    out[pos++] = 0xC0U;
    return pos;
}

void ti99_kiss_init(Ti99KissDecoder *dec)
{
    if (dec) {
        memset(dec, 0, sizeof(*dec));
    }
}

size_t ti99_kiss_feed(Ti99KissDecoder *dec, uint8_t byte, uint8_t *out, size_t cap)
{
    if (!dec) {
        return 0;
    }
    if (byte == 0xC0U) {
        size_t done = 0;
        if (dec->in_frame && dec->len > 1 && (dec->buf[0] & 0x0FU) == 0U && out) {
            done = dec->len - 1U;
            if (done > cap) {
                done = 0;
            } else {
                memcpy(out, dec->buf + 1U, done);
            }
        }
        dec->in_frame = 1;
        dec->escaped = 0;
        dec->len = 0;
        return done;
    }
    if (!dec->in_frame) {
        return 0;
    }
    if (dec->escaped) {
        dec->escaped = 0;
        if (byte == 0xDCU) {
            byte = 0xC0U;
        } else if (byte == 0xDDU) {
            byte = 0xDBU;
        } else {
            dec->len = 0;
            dec->in_frame = 0;
            return 0;
        }
    } else if (byte == 0xDBU) {
        dec->escaped = 1;
        return 0;
    }
    if (dec->len < sizeof(dec->buf)) {
        dec->buf[dec->len++] = byte;
    }
    return 0;
}

void ti99_config_init_default(Ti99Config *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    ti99_strcpy_safe(cfg->callsign, sizeof(cfg->callsign), "N0CALL-7");
    ti99_strcpy_safe(cfg->alias, sizeof(cfg->alias), "TI99ND");
    ti99_strcpy_safe(cfg->bbs_callsign, sizeof(cfg->bbs_callsign), "N0CALL-11");
    ti99_strcpy_safe(cfg->chat_callsign, sizeof(cfg->chat_callsign), "N0CALL-12");
    cfg->bbs_enabled = 1;
    cfg->chat_enabled = 1;
    cfg->allow_incoming = 1;
    cfg->eol_mode = TI99_EOL_LF;
    cfg->t1_sec = 3;
    cfg->n2_retries = 10;
    ti99_strcpy_safe(cfg->rig_host, sizeof(cfg->rig_host), "127.0.0.1");
    cfg->rig_port = 4532U;
    ti99_strcpy_safe(cfg->printer_driver, sizeof(cfg->printer_driver), "generic");
    ti99_strcpy_safe(cfg->printer_dev, sizeof(cfg->printer_dev), "PIO");
    ti99_strcpy_safe(cfg->theme_name, sizeof(cfg->theme_name), "native");

    cfg->iface_count = 2;
    ti99_strcpy_safe(cfg->ifaces[0].name, sizeof(cfg->ifaces[0].name), "tipi0");
    ti99_strcpy_safe(cfg->ifaces[0].driver, sizeof(cfg->ifaces[0].driver), "axudp");
    ti99_strcpy_safe(cfg->ifaces[0].endpoint, sizeof(cfg->ifaces[0].endpoint), "0.0.0.0");
    cfg->ifaces[0].port = 10093U;
    cfg->ifaces[0].enabled = 1;

    ti99_strcpy_safe(cfg->ifaces[1].name, sizeof(cfg->ifaces[1].name), "rs232a");
    ti99_strcpy_safe(cfg->ifaces[1].driver, sizeof(cfg->ifaces[1].driver), "kiss-serial");
    ti99_strcpy_safe(cfg->ifaces[1].endpoint, sizeof(cfg->ifaces[1].endpoint), "RS232/1");
    cfg->ifaces[1].baud = 9600U;
    cfg->ifaces[1].enabled = 1;
}

int ti99_config_parse(const char *text, size_t len, Ti99Config *cfg)
{
    size_t i = 0;
    int eol_detected = 0;
    char line[128];
    if (!text || !cfg) {
        return -1;
    }
    ti99_config_init_default(cfg);
    while (i < len) {
        size_t n = 0;
        char *eq;
        while (i < len && text[i] != '\r' && text[i] != '\n') {
            if (n + 1 < sizeof(line)) {
                line[n++] = text[i];
            }
            ++i;
        }
        line[n] = '\0';
        if (i < len) {
            if (text[i] == '\r') {
                if (i + 1 < len && text[i + 1] == '\n') {
                    if (!eol_detected) {
                        cfg->eol_mode = TI99_EOL_CRLF;
                        eol_detected = 1;
                    }
                    i += 2;
                } else {
                    if (!eol_detected) {
                        cfg->eol_mode = TI99_EOL_CR;
                        eol_detected = 1;
                    }
                    ++i;
                }
            } else {
                if (!eol_detected) {
                    cfg->eol_mode = TI99_EOL_LF;
                    eol_detected = 1;
                }
                ++i;
            }
        }
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        {
            const char *k = line;
            const char *v = eq + 1;
            while (*v == ' ' || *v == '\t') {
                ++v;
            }
            if (ti99_stricmp(k, "callsign") == 0) {
                ti99_strcpy_safe(cfg->callsign, sizeof(cfg->callsign), v);
            } else if (ti99_stricmp(k, "alias") == 0) {
                ti99_strcpy_safe(cfg->alias, sizeof(cfg->alias), v);
            } else if (ti99_stricmp(k, "bbs_callsign") == 0) {
                ti99_strcpy_safe(cfg->bbs_callsign, sizeof(cfg->bbs_callsign), v);
            } else if (ti99_stricmp(k, "chat_callsign") == 0) {
                ti99_strcpy_safe(cfg->chat_callsign, sizeof(cfg->chat_callsign), v);
            } else if (ti99_stricmp(k, "allow_incoming") == 0) {
                cfg->allow_incoming = (uint8_t)(atoi(v) ? 1 : 0);
            } else if (ti99_stricmp(k, "theme") == 0) {
                ti99_strcpy_safe(cfg->theme_name, sizeof(cfg->theme_name), v);
            } else if (ti99_stricmp(k, "t1") == 0) {
                cfg->t1_sec = (uint8_t)atoi(v);
            } else if (ti99_stricmp(k, "n2") == 0) {
                cfg->n2_retries = (uint8_t)atoi(v);
            }
        }
    }
    return 0;
}

size_t ti99_config_serialize(const Ti99Config *cfg, char *out, size_t cap)
{
    const char *eol = "\n";
    int n;
    if (!cfg || !out || cap == 0) {
        return 0;
    }
    if (cfg->eol_mode == TI99_EOL_CRLF) {
        eol = "\r\n";
    } else if (cfg->eol_mode == TI99_EOL_CR) {
        eol = "\r";
    }
    n = snprintf(out, cap,
                 "callsign=%s%salias=%s%sbbs_callsign=%s%schat_callsign=%s%s"
                 "allow_incoming=%u%stheme=%s%st1=%u%sn2=%u%s",
                 cfg->callsign, eol, cfg->alias, eol,
                 cfg->bbs_callsign, eol, cfg->chat_callsign, eol,
                 (unsigned)cfg->allow_incoming, eol, cfg->theme_name, eol,
                 (unsigned)cfg->t1_sec, eol, (unsigned)cfg->n2_retries, eol);
    if (n < 0 || (size_t)n >= cap) {
        return 0;
    }
    return (size_t)n;
}

void ti99_node_init(Ti99Node *node, int seed_demo)
{
    if (!node) {
        return;
    }
    memset(node, 0, sizeof(*node));
    ti99_config_init_default(&node->cfg);
    if (seed_demo) {
        ti99_strcpy_safe(node->cfg.callsign, sizeof(node->cfg.callsign), "KR4OYK-7");
        ti99_strcpy_safe(node->cfg.bbs_callsign, sizeof(node->cfg.bbs_callsign), "KR4OYK-11");
        ti99_strcpy_safe(node->cfg.chat_callsign, sizeof(node->cfg.chat_callsign), "KR4OYK-12");

        node->session_count = 2;
        node->sessions[0].id = 1;
        ti99_strcpy_safe(node->sessions[0].callsign, sizeof(node->sessions[0].callsign), "W1AW-4");
        ti99_strcpy_safe(node->sessions[0].port_name, sizeof(node->sessions[0].port_name), "tipi0");
        ti99_strcpy_safe(node->sessions[0].state, sizeof(node->sessions[0].state), "CONNECTED");

        node->sessions[1].id = 2;
        ti99_strcpy_safe(node->sessions[1].callsign, sizeof(node->sessions[1].callsign), "K4BBS-1");
        ti99_strcpy_safe(node->sessions[1].port_name, sizeof(node->sessions[1].port_name), "rs232a");
        ti99_strcpy_safe(node->sessions[1].state, sizeof(node->sessions[1].state), "I-FRAME");

        ti99_node_record_mheard(node, "W1AW-4");
        ti99_node_record_mheard(node, "K4BBS-1");
        ti99_node_record_mheard(node, "N2NET-9");
        ti99_node_log(node, "TI-99/4A packet node ready (SAMS + TIPI + RS232)");
    }
}

void ti99_node_log(Ti99Node *node, const char *msg)
{
    uint8_t i;
    if (!node || !msg) {
        return;
    }
    if (node->log_count >= TI99_MAX_LOG_LINES) {
        for (i = 1; i < TI99_MAX_LOG_LINES; ++i) {
            ti99_strcpy_safe(node->logs[i - 1], sizeof(node->logs[i - 1]), node->logs[i]);
        }
        node->log_count = TI99_MAX_LOG_LINES - 1;
    }
    ti99_strcpy_safe(node->logs[node->log_count], sizeof(node->logs[0]), msg);
    ++node->log_count;
}

void ti99_node_record_mheard(Ti99Node *node, const char *call)
{
    uint8_t i;
    if (!node || !call || !call[0]) {
        return;
    }
    for (i = 0; i < node->mheard_count; ++i) {
        if (ti99_stricmp(node->mheard[i], call) == 0) {
            return;
        }
    }
    if (node->mheard_count >= TI99_MAX_MHEARD) {
        for (i = 1; i < TI99_MAX_MHEARD; ++i) {
            ti99_strcpy_safe(node->mheard[i - 1], sizeof(node->mheard[i - 1]), node->mheard[i]);
        }
        node->mheard_count = TI99_MAX_MHEARD - 1;
    }
    ti99_strcpy_safe(node->mheard[node->mheard_count], sizeof(node->mheard[0]), call);
    ++node->mheard_count;
}

void ti99_node_learn_peer(Ti99Node *node, const char *call, const char *host,
                          uint16_t port, const char *iface)
{
    uint8_t i;
    if (!node || !call || !call[0]) {
        return;
    }
    for (i = 0; i < node->cfg.peer_count; ++i) {
        if (ti99_stricmp(node->cfg.peers[i].callsign, call) == 0) {
            ti99_strcpy_safe(node->cfg.peers[i].host, sizeof(node->cfg.peers[i].host), host);
            node->cfg.peers[i].port = port;
            return;
        }
    }
    if (node->cfg.peer_count >= TI99_MAX_PEERS) {
        return;
    }
    i = node->cfg.peer_count++;
    ti99_strcpy_safe(node->cfg.peers[i].callsign, sizeof(node->cfg.peers[i].callsign), call);
    ti99_strcpy_safe(node->cfg.peers[i].host, sizeof(node->cfg.peers[i].host), host);
    node->cfg.peers[i].port = port;
    ti99_strcpy_safe(node->cfg.peers[i].iface, sizeof(node->cfg.peers[i].iface), iface);
    node->cfg.peers[i].auto_learned = 1;
    node->dirty_peers = 1;
}

int ti99_node_is_banned(const Ti99Node *node, const char *call, uint8_t *hard_out)
{
    uint8_t i;
    if (!node || !call) {
        return 0;
    }
    for (i = 0; i < node->ban_count; ++i) {
        if (ti99_stricmp(node->bans[i].callsign, call) == 0) {
            if (hard_out) {
                *hard_out = node->bans[i].hard;
            }
            return 1;
        }
    }
    return 0;
}

int ti99_node_add_ban(Ti99Node *node, const char *call, uint8_t hard, const char *reason)
{
    uint8_t i;
    if (!node || !call || !call[0]) {
        return -1;
    }
    for (i = 0; i < node->ban_count; ++i) {
        if (ti99_stricmp(node->bans[i].callsign, call) == 0) {
            node->bans[i].hard = hard ? 1U : 0U;
            ti99_strcpy_safe(node->bans[i].reason, sizeof(node->bans[i].reason), reason);
            return 0;
        }
    }
    if (node->ban_count >= TI99_MAX_BANS) {
        return -1;
    }
    i = node->ban_count++;
    ti99_strcpy_safe(node->bans[i].callsign, sizeof(node->bans[i].callsign), call);
    node->bans[i].hard = hard ? 1U : 0U;
    ti99_strcpy_safe(node->bans[i].reason, sizeof(node->bans[i].reason), reason);
    return 0;
}

int ti99_node_remove_ban(Ti99Node *node, const char *call)
{
    uint8_t i;
    if (!node || !call) {
        return -1;
    }
    for (i = 0; i < node->ban_count; ++i) {
        if (ti99_stricmp(node->bans[i].callsign, call) == 0) {
            uint8_t j;
            for (j = i + 1U; j < node->ban_count; ++j) {
                node->bans[j - 1U] = node->bans[j];
            }
            --node->ban_count;
            return 0;
        }
    }
    return -1;
}

int ti99_node_terminate_session(Ti99Node *node, uint16_t session_id)
{
    uint8_t i;
    if (!node) {
        return -1;
    }
    for (i = 0; i < node->session_count; ++i) {
        if (node->sessions[i].id == session_id) {
            uint8_t j;
            for (j = i + 1U; j < node->session_count; ++j) {
                node->sessions[j - 1U] = node->sessions[j];
            }
            --node->session_count;
            return 0;
        }
    }
    return -1;
}

void ti99_node_send_bbs_prompt(Ti99Node *node, const char *call, char *out, size_t cap)
{
    uint8_t hard = 0;
    if (!node || !out || cap == 0) {
        return;
    }
    if (ti99_node_is_banned(node, call, &hard)) {
        if (hard) {
            snprintf(out, cap, "Access denied (%s hard banned).\r", call ? call : "UNKNOWN");
        } else {
            snprintf(out, cap, "Restricted APPEAL session for %s (L/A/B)>\r",
                     call ? call : "UNKNOWN");
        }
        return;
    }
    snprintf(out, cap, "OYK BBS (%s) [TI-99/4A] L/R/S/F/B>\r", node->cfg.bbs_callsign);
}

void ti99_node_send_chat_banner(Ti99Node *node, const char *call, char *out, size_t cap)
{
    if (!node || !out || cap == 0) {
        return;
    }
    snprintf(out, cap, "*** OYK CHAT (%s) Welcome %s (/who /q) ***\r",
             node->cfg.chat_callsign, call ? call : "GUEST");
}

void ti99_app_init(Ti99App *app, int seed_demo)
{
    if (!app) {
        return;
    }
    memset(app, 0, sizeof(*app));
    ti99_node_init(&app->node, seed_demo);
    app->screen = TI99_SCR_MAIN;
    app->term_eol = TI99_EOL_LF;
    app->term_echo = 1;
    app->sams_banks = 255;
    app->f18a_detected = 1;
    app->tipi_present = 1;
    app->rs232_present = 1;

    app->file_count = 4;
    ti99_strcpy_safe(app->files[0].name, sizeof(app->files[0].name), "BULLETINS");
    app->files[0].is_dir = 1;
    app->files[0].is_incoming = 0;

    ti99_strcpy_safe(app->files[1].name, sizeof(app->files[1].name), "NODEINFO.TXT");
    app->files[1].size_bytes = 512UL;
    app->files[1].is_incoming = 0;

    ti99_strcpy_safe(app->files[2].name, sizeof(app->files[2].name), "UPLOADS");
    app->files[2].is_dir = 1;
    app->files[2].is_incoming = 1;

    ti99_strcpy_safe(app->files[3].name, sizeof(app->files[3].name), "PACKET.YAP");
    app->files[3].size_bytes = 256UL;
    app->files[3].is_incoming = 1;

    app->term_count = 2;
    ti99_strcpy_safe(app->term_lines[0], sizeof(app->term_lines[0]),
                     "TI-99/4A Local Terminal Ready (F18A/40-col)");
    ti99_strcpy_safe(app->term_lines[1], sizeof(app->term_lines[1]),
                     "Ctrl-A Z:Help  Ctrl-A X:Exit");
    ti99_strcpy_safe(app->feedback, sizeof(app->feedback), "Ready");
}

int ti99_app_exec_action(Ti99App *app, int action)
{
    if (!app) {
        return -1;
    }
    switch (action) {
    case TI99_ACT_SETTINGS:
        app->screen = TI99_SCR_SETTINGS;
        app->settings_tab = 0;
        return 0;
    case TI99_ACT_ADD_IFACE:
        app->screen = TI99_SCR_ADD_IFACE_1;
        app->wizard_step = 1;
        return 0;
    case TI99_ACT_BAN:
        app->screen = TI99_SCR_BAN;
        return 0;
    case TI99_ACT_TERM:
        app->screen = TI99_SCR_TERMINAL;
        return 0;
    case TI99_ACT_TERMINATE:
        app->screen = TI99_SCR_TERMINATE;
        return 0;
    case TI99_ACT_FILES:
        app->screen = TI99_SCR_FILES;
        return 0;
    case TI99_ACT_THEME:
        app->screen = TI99_SCR_THEME;
        return 0;
    case TI99_ACT_ABOUT:
        app->screen = TI99_SCR_ABOUT;
        return 0;
    case TI99_ACT_EXIT:
        app->screen = TI99_SCR_EXIT;
        return 0;
    case TI99_ACT_TOGGLE_IN:
        app->node.cfg.allow_incoming = app->node.cfg.allow_incoming ? 0U : 1U;
        return 0;
    default:
        return -1;
    }
}

int ti99_app_set_screen_by_name(Ti99App *app, const char *name)
{
    if (!app || !name) {
        return -1;
    }
    if (strcmp(name, "main") == 0) { app->screen = TI99_SCR_MAIN; return 0; }
    if (strcmp(name, "menu") == 0) { app->screen = TI99_SCR_MENU; return 0; }
    if (strcmp(name, "settings") == 0) { app->screen = TI99_SCR_SETTINGS; app->settings_tab = 0; return 0; }
    if (strcmp(name, "settings_bbs") == 0) { app->screen = TI99_SCR_SETTINGS_BBS; app->settings_tab = 1; return 0; }
    if (strcmp(name, "settings_chat") == 0) { app->screen = TI99_SCR_SETTINGS_CHAT; app->settings_tab = 2; return 0; }
    if (strcmp(name, "settings_ifaces") == 0) { app->screen = TI99_SCR_SETTINGS_IFACES; app->settings_tab = 3; return 0; }
    if (strcmp(name, "peers") == 0) { app->screen = TI99_SCR_PEERS; app->settings_tab = 4; return 0; }
    if (strcmp(name, "settings_rig") == 0) { app->screen = TI99_SCR_SETTINGS_RIG; app->settings_tab = 5; return 0; }
    if (strcmp(name, "settings_printer") == 0) { app->screen = TI99_SCR_SETTINGS_PRINTER; app->settings_tab = 6; return 0; }
    if (strcmp(name, "add_iface_1") == 0) { app->screen = TI99_SCR_ADD_IFACE_1; app->wizard_step = 1; return 0; }
    if (strcmp(name, "add_iface_2") == 0) { app->screen = TI99_SCR_ADD_IFACE_2; app->wizard_step = 2; return 0; }
    if (strcmp(name, "add_iface_3") == 0) { app->screen = TI99_SCR_ADD_IFACE_3; app->wizard_step = 3; return 0; }
    if (strcmp(name, "add_iface_4") == 0) { app->screen = TI99_SCR_ADD_IFACE_4; app->wizard_step = 4; return 0; }
    if (strcmp(name, "edit_iface") == 0) { app->screen = TI99_SCR_EDIT_IFACE; return 0; }
    if (strcmp(name, "edit_peer") == 0) { app->screen = TI99_SCR_EDIT_PEER; return 0; }
    if (strcmp(name, "terminate") == 0) { app->screen = TI99_SCR_TERMINATE; return 0; }
    if (strcmp(name, "ban") == 0) { app->screen = TI99_SCR_BAN; return 0; }
    if (strcmp(name, "exit") == 0) { app->screen = TI99_SCR_EXIT; return 0; }
    if (strcmp(name, "theme") == 0) { app->screen = TI99_SCR_THEME; return 0; }
    if (strcmp(name, "terminal") == 0) { app->screen = TI99_SCR_TERMINAL; return 0; }
    if (strcmp(name, "terminal_help") == 0) { app->screen = TI99_SCR_TERMINAL_HELP; return 0; }
    if (strcmp(name, "files") == 0) { app->screen = TI99_SCR_FILES; return 0; }
    if (strcmp(name, "about") == 0) { app->screen = TI99_SCR_ABOUT; return 0; }
    return -1;
}

const char *ti99_app_screen_title(Ti99ScreenId scr)
{
    switch (scr) {
    case TI99_SCR_MAIN:             return "OYK PACKET NODE";
    case TI99_SCR_MENU:             return "Node Menu";
    case TI99_SCR_SETTINGS:
    case TI99_SCR_SETTINGS_BBS:
    case TI99_SCR_SETTINGS_CHAT:
    case TI99_SCR_SETTINGS_IFACES:
    case TI99_SCR_PEERS:
    case TI99_SCR_SETTINGS_RIG:
    case TI99_SCR_SETTINGS_PRINTER: return "Node Settings";
    case TI99_SCR_ADD_IFACE_1:
    case TI99_SCR_ADD_IFACE_2:
    case TI99_SCR_ADD_IFACE_3:
    case TI99_SCR_ADD_IFACE_4:      return "Add Interface";
    case TI99_SCR_EDIT_IFACE:       return "Edit Interface";
    case TI99_SCR_EDIT_PEER:        return "Edit Peer";
    case TI99_SCR_TERMINATE:        return "Terminate Active Connection";
    case TI99_SCR_BAN:              return "Ban / Unban Callsign";
    case TI99_SCR_EXIT:             return "Auto-Learned Peers";
    case TI99_SCR_THEME:            return "Theme Settings";
    case TI99_SCR_TERMINAL:         return "Local Terminal";
    case TI99_SCR_TERMINAL_HELP:    return "Terminal Hotkey Help";
    case TI99_SCR_FILES:            return "File Manager";
    case TI99_SCR_ABOUT:            return "About OYK Packet Node";
    default:                        return "OYK";
    }
}

uint8_t ti99_app_list_files(const Ti99App *app, uint8_t pane,
                            Ti99FileEntry *out, uint8_t max_items)
{
    uint8_t count = 0;
    uint8_t i;
    const char *sub;
    if (!app || !out || max_items == 0) {
        return 0;
    }
    sub = pane ? app->files_subdir : app->incoming_subdir;
    if (sub[0] != '\0') {
        memset(&out[count], 0, sizeof(out[count]));
        ti99_strcpy_safe(out[count].name, sizeof(out[count].name), "..");
        out[count].is_dir = 1;
        out[count].is_incoming = pane ? 0U : 1U;
        ++count;
    }
    for (i = 0; i < app->file_count && count < max_items; ++i) {
        uint8_t match_pane = pane ? (app->files[i].is_incoming == 0U)
                                  : (app->files[i].is_incoming != 0U);
        if (match_pane && sub[0] == '\0') {
            out[count++] = app->files[i];
        }
    }
    if (sub[0] != '\0' && count < max_items) {
        memset(&out[count], 0, sizeof(out[count]));
        ti99_strcpy_safe(out[count].name, sizeof(out[count].name), "SAMPLE.TXT");
        out[count].size_bytes = 128UL;
        out[count].is_incoming = pane ? 0U : 1U;
        ++count;
    }
    return count;
}

int ti99_app_cd(Ti99App *app, uint8_t pane, const char *dir)
{
    char *sub;
    size_t cap;
    if (!app || !dir) {
        return -1;
    }
    sub = pane ? app->files_subdir : app->incoming_subdir;
    cap = pane ? sizeof(app->files_subdir) : sizeof(app->incoming_subdir);
    if (strcmp(dir, "..") == 0) {
        if (sub[0] == '\0') {
            return -1; /* Already jailed at root */
        }
        sub[0] = '\0';
        return 0;
    }
    ti99_strcpy_safe(sub, cap, dir);
    return 0;
}
