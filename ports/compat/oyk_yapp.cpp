/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_yapp.hpp"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define OYK_YAPP_MKDIR(p) _mkdir(p)
#define OYK_YAPP_SEP      '\\'
#elif defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
#include <direct.h>
#define OYK_YAPP_MKDIR(p) mkdir(p)
#define OYK_YAPP_SEP      '\\'
#else
#include <sys/stat.h>
#define OYK_YAPP_MKDIR(p) mkdir((p), 0755)
#define OYK_YAPP_SEP      '/'
#endif

namespace {

void ycopy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

oyk_u8 calc_csum(const oyk_u8* buf, size_t len) {
    oyk_u32 s = 0;
    for (size_t i = 0; i < len; ++i) s += buf[i];
    return static_cast<oyk_u8>(s & 0xFFU);
}

int is_reserved_name(const char* name) {
    char stem[16];
    size_t n = 0;
    while (name[n] && name[n] != '.' && n < sizeof(stem) - 1) {
        stem[n] = static_cast<char>(toupper(static_cast<unsigned char>(name[n])));
        ++n;
    }
    stem[n] = '\0';
    if (strcmp(stem, "CON") == 0 || strcmp(stem, "PRN") == 0 ||
        strcmp(stem, "AUX") == 0 || strcmp(stem, "NUL") == 0) {
        return 1;
    }
    if (n == 4 &&
        (strncmp(stem, "COM", 3) == 0 || strncmp(stem, "LPT", 3) == 0) &&
        stem[3] >= '1' && stem[3] <= '9') {
        return 1;
    }
    return 0;
}

const char* path_basename(const char* p) {
    if (!p) return "";
    const char* s1 = strrchr(p, '/');
    const char* s2 = strrchr(p, '\\');
    const char* last = (s1 > s2) ? s1 : s2;
    return last ? (last + 1) : p;
}

int file_exists_ro(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

void yapp_close_fp(OykYappSession* y) {
    if (y->fp) {
        fclose(y->fp);
        y->fp = NULL;
    }
}

void yapp_send_raw(OykYappSession* y, const oyk_u8* data, size_t len) {
    if (y->send_fn && data && len > 0) {
        y->send_fn(data, len, y->send_ctx);
    }
}

void yapp_send_2(OykYappSession* y, oyk_u8 b0, oyk_u8 b1) {
    oyk_u8 p[2];
    p[0] = b0;
    p[1] = b1;
    yapp_send_raw(y, p, 2);
}

void yapp_send_cancel(OykYappSession* y, const char* reason) {
    oyk_u8 p[140];
    size_t n = reason ? strlen(reason) : 0;
    if (n > 136) n = 136;
    p[0] = OYK_YAPP_CAN;
    p[1] = static_cast<oyk_u8>(n);
    if (n > 0) memcpy(p + 2, reason, n);
    yapp_send_raw(y, p, 2 + n);
}

void yapp_send_header(OykYappSession* y) {
    oyk_u8 p[180];
    char sz_s[24];
    sprintf(sz_s, "%lu", static_cast<unsigned long>(y->total_bytes));
    const char* date_s = "00000000";

    size_t fn_len = strlen(y->filename);
    size_t sz_len = strlen(sz_s);
    size_t dt_len = strlen(date_s);
    size_t pay_len = fn_len + 1 + sz_len + 1 + dt_len + 1;
    if (pay_len + 2 > sizeof(p)) return;

    p[0] = OYK_YAPP_SOH;
    p[1] = static_cast<oyk_u8>(pay_len & 0xFFU);
    size_t pos = 2;
    memcpy(p + pos, y->filename, fn_len); pos += fn_len; p[pos++] = 0;
    memcpy(p + pos, sz_s, sz_len);        pos += sz_len; p[pos++] = 0;
    memcpy(p + pos, date_s, dt_len);      pos += dt_len; p[pos++] = 0;
    yapp_send_raw(y, p, pos);
}

void yapp_send_data_chunks(OykYappSession* y) {
    if (!y->fp || y->state != OYK_YAPP_SENDING) return;
    oyk_u8 pkt[134];
    while (1) {
        size_t n = fread(pkt + 2, 1, 128, y->fp);
        if (n == 0) break;
        pkt[0] = OYK_YAPP_STX;
        pkt[1] = static_cast<oyk_u8>(n & 0xFFU);
        size_t total = 2 + n;
        if (y->yappc) {
            pkt[total++] = calc_csum(pkt + 2, n);
        }
        yapp_send_raw(y, pkt, total);
        y->bytes_transferred += static_cast<oyk_u32>(n);
    }
    yapp_close_fp(y);
    y->state = OYK_YAPP_SEND_WAIT_EOF;
    sprintf(y->status, "YAPP: Sent %lu bytes; awaiting EOF ack...",
            static_cast<unsigned long>(y->bytes_transferred));
    yapp_send_2(y, OYK_YAPP_ETX, 0x01U);
}

void yapp_open_recv_file(OykYappSession* y) {
    OYK_YAPP_MKDIR(y->incoming_root);
    OYK_YAPP_MKDIR(y->target_dir);
    y->fp = fopen(y->target_path, "wb");
    if (!y->fp) {
        yapp_send_cancel(y, "Unable to create destination file");
        y->state = OYK_YAPP_ERROR;
        ycopy(y->status, sizeof(y->status), "YAPP error: cannot open file for writing");
        return;
    }
    y->bytes_transferred = 0;
    y->state = OYK_YAPP_RECV_DATA;
    sprintf(y->status, "YAPP: Receiving %.60s...", y->filename);
    yapp_send_2(y, OYK_YAPP_ACK, y->yappc ? 0x06U : 0x02U);
}

void yapp_handle_packet(OykYappSession* y) {
    oyk_u8 type = y->pkt_type;
    const oyk_u8* pay = y->pkt_buf;
    size_t plen = y->pkt_len;

    if (type == OYK_YAPP_CAN) {
        yapp_close_fp(y);
        yapp_send_2(y, OYK_YAPP_ACK, 0x05U);
        y->state = OYK_YAPP_ABORTED;
        ycopy(y->status, sizeof(y->status), "YAPP cancelled by peer");
        return;
    }
    if (type == OYK_YAPP_NAK) {
        yapp_close_fp(y);
        y->state = OYK_YAPP_ERROR;
        ycopy(y->status, sizeof(y->status), "YAPP rejected by peer");
        return;
    }
    if (type == OYK_YAPP_DLE) {
        return;
    }

    switch (y->state) {
        case OYK_YAPP_SEND_WAIT_INIT:
            if (type == OYK_YAPP_ACK && plen >= 1 && pay[0] == 0x01U) {
                y->state = OYK_YAPP_SEND_WAIT_HDR;
                sprintf(y->status, "YAPP: Sending header for %.60s...", y->filename);
                yapp_send_header(y);
            }
            break;

        case OYK_YAPP_SEND_WAIT_HDR:
            if (type == OYK_YAPP_ACK && plen >= 1 && (pay[0] == 0x02U || pay[0] == 0x06U)) {
                y->yappc = (pay[0] == 0x06U) ? 1 : 0;
                y->state = OYK_YAPP_SENDING;
                sprintf(y->status, "YAPP%s: Sending %.60s...",
                        y->yappc ? "-C" : "", y->filename);
                yapp_send_data_chunks(y);
            }
            break;

        case OYK_YAPP_SEND_WAIT_EOF:
            if (type == OYK_YAPP_ACK && plen >= 1 && pay[0] == 0x03U) {
                y->state = OYK_YAPP_SEND_WAIT_EOT;
                yapp_send_2(y, OYK_YAPP_EOT, 0x01U);
            }
            break;

        case OYK_YAPP_SEND_WAIT_EOT:
            if (type == OYK_YAPP_ACK && plen >= 1 && pay[0] == 0x04U) {
                y->state = OYK_YAPP_COMPLETE;
                sprintf(y->status, "YAPP upload complete: %.48s (%lu B)",
                        y->filename, static_cast<unsigned long>(y->bytes_transferred));
            }
            break;

        case OYK_YAPP_RECV_WAIT_HDR:
            if (type == OYK_YAPP_ENQ && plen >= 1 && pay[0] == 0x01U) {
                yapp_send_2(y, OYK_YAPP_ACK, 0x01U);
            } else if (type == OYK_YAPP_EOT && plen >= 1 && pay[0] == 0x01U) {
                yapp_send_2(y, OYK_YAPP_ACK, 0x04U);
                y->state = OYK_YAPP_COMPLETE;
                ycopy(y->status, sizeof(y->status), "YAPP transfer complete");
            } else if (type == OYK_YAPP_SOH) {
                char fname[128], fsize[24], fdate[24];
                fname[0] = fsize[0] = fdate[0] = '\0';
                size_t pos = 0;
                int field = 0;
                while (pos < plen && field < 3) {
                    size_t nul = pos;
                    while (nul < plen && pay[nul] != 0) ++nul;
                    size_t flen = nul - pos;
                    char* dst = (field == 0) ? fname : ((field == 1) ? fsize : fdate);
                    size_t dcap = (field == 0) ? sizeof(fname) : sizeof(fsize);
                    if (flen >= dcap) flen = dcap - 1;
                    memcpy(dst, pay + pos, flen);
                    dst[flen] = '\0';
                    pos = (nul < plen) ? (nul + 1) : plen;
                    ++field;
                }

                /* Strip trailing periods from legacy DOS/Win16 clients (e.g. "LICENSE." -> "LICENSE")
                   only when no leading period or path traversal is present. */
                if (fname[0] != '\0' && fname[0] != '.' &&
                    strstr(fname, "..") == NULL &&
                    strchr(fname, '/') == NULL &&
                    strchr(fname, '\\') == NULL) {
                    size_t fl = strlen(fname);
                    while (fl > 0 && fname[fl - 1] == '.') {
                        fname[--fl] = '\0';
                    }
                }

                if (!oyk_yapp_is_safe_filename(fname)) {
                    yapp_send_cancel(y, "Invalid filename (path traversal rejected)");
                    y->state = OYK_YAPP_ERROR;
                    ycopy(y->status, sizeof(y->status), "YAPP rejected unsafe filename");
                    return;
                }

                ycopy(y->filename, sizeof(y->filename), fname);
                y->total_bytes = fsize[0] ? static_cast<oyk_u32>(strtoul(fsize, NULL, 10)) : 0UL;
                y->yappc = (fdate[0] != '\0') ? 1 : 0;

                if (!oyk_yapp_resolve_incoming_path(y->incoming_root, &y->remote_call,
                                                    y->filename,
                                                    y->target_path, sizeof(y->target_path),
                                                    y->target_dir, sizeof(y->target_dir))) {
                    yapp_send_cancel(y, "Invalid destination path");
                    y->state = OYK_YAPP_ERROR;
                    ycopy(y->status, sizeof(y->status), "YAPP destination path error");
                    return;
                }

                if (file_exists_ro(y->target_path)) {
                    if (y->overwrite_fn) {
                        if (!y->overwrite_fn(y->target_path, y->filename, y->overwrite_ctx)) {
                            yapp_send_cancel(y, "File exists; overwrite declined");
                            y->state = OYK_YAPP_ABORTED;
                            ycopy(y->status, sizeof(y->status), "YAPP: Skipped existing file");
                            return;
                        }
                        yapp_open_recv_file(y);
                    } else {
                        y->state = OYK_YAPP_RECV_OVERWRITE;
                        sprintf(y->status, "YAPP: File exists (%.60s); confirm overwrite", y->filename);
                    }
                    return;
                }

                yapp_open_recv_file(y);
            }
            break;

        case OYK_YAPP_RECV_DATA:
            if (type == OYK_YAPP_STX) {
                if (y->fp && plen > 0) {
                    size_t w = fwrite(pay, 1, plen, y->fp);
                    if (w != plen) {
                        yapp_close_fp(y);
                        yapp_send_cancel(y, "Disk write error");
                        y->state = OYK_YAPP_ERROR;
                        ycopy(y->status, sizeof(y->status), "YAPP error: disk write failed");
                        return;
                    }
                    y->bytes_transferred += static_cast<oyk_u32>(w);
                }
            } else if (type == OYK_YAPP_ETX && plen >= 1 && pay[0] == 0x01U) {
                yapp_close_fp(y);
                y->state = OYK_YAPP_RECV_WAIT_EOT;
                sprintf(y->status, "YAPP: Saved %.52s (%lu B)",
                        y->target_path, static_cast<unsigned long>(y->bytes_transferred));
                yapp_send_2(y, OYK_YAPP_ACK, 0x03U);
            }
            break;

        case OYK_YAPP_RECV_WAIT_EOT:
            if (type == OYK_YAPP_EOT && plen >= 1 && pay[0] == 0x01U) {
                yapp_send_2(y, OYK_YAPP_ACK, 0x04U);
                y->state = OYK_YAPP_COMPLETE;
                sprintf(y->status, "YAPP saved: %.56s (%lu B)",
                        y->target_path, static_cast<unsigned long>(y->bytes_transferred));
            } else if (type == OYK_YAPP_SOH) {
                y->state = OYK_YAPP_RECV_WAIT_HDR;
                yapp_handle_packet(y);
            }
            break;

        default:
            break;
    }
}

void yapp_handle_byte(OykYappSession* y, oyk_u8 b) {
    if (y->parse_stage == 0) {
        y->pkt_type = b;
        y->pkt_len = 0;
        y->pkt_expected = 0;
        y->pkt_expect_csum = 0;
        if (b == OYK_YAPP_ENQ || b == OYK_YAPP_ACK || b == OYK_YAPP_ETX || b == OYK_YAPP_EOT ||
            b == OYK_YAPP_SOH || b == OYK_YAPP_STX || b == OYK_YAPP_NAK || b == OYK_YAPP_CAN ||
            b == OYK_YAPP_DLE) {
            y->parse_stage = 1;
        }
    } else if (y->parse_stage == 1) {
        if (y->pkt_type == OYK_YAPP_ENQ || y->pkt_type == OYK_YAPP_ACK ||
            y->pkt_type == OYK_YAPP_ETX || y->pkt_type == OYK_YAPP_EOT) {
            y->pkt_buf[0] = b;
            y->pkt_len = 1;
            y->parse_stage = 0;
            yapp_handle_packet(y);
        } else {
            y->pkt_expected = (b == 0 && (y->pkt_type == OYK_YAPP_STX || y->pkt_type == OYK_YAPP_SOH))
                                  ? static_cast<oyk_u16>(256U) : static_cast<oyk_u16>(b);
            y->pkt_expect_csum = (y->pkt_type == OYK_YAPP_STX && y->yappc) ? 1 : 0;
            if (y->pkt_expected == 0) {
                if (y->pkt_expect_csum) {
                    y->parse_stage = 3;
                } else {
                    y->parse_stage = 0;
                    yapp_handle_packet(y);
                }
            } else {
                y->parse_stage = 2;
            }
        }
    } else if (y->parse_stage == 2) {
        if (y->pkt_len < sizeof(y->pkt_buf)) {
            y->pkt_buf[y->pkt_len++] = b;
        }
        if (y->pkt_len >= y->pkt_expected) {
            if (y->pkt_expect_csum) {
                y->parse_stage = 3;
            } else {
                y->parse_stage = 0;
                yapp_handle_packet(y);
            }
        }
    } else if (y->parse_stage == 3) {
        oyk_u8 calc = calc_csum(y->pkt_buf, y->pkt_len);
        y->parse_stage = 0;
        if (calc != b) {
            yapp_close_fp(y);
            yapp_send_cancel(y, "YAPP-C checksum mismatch");
            y->state = OYK_YAPP_ERROR;
            ycopy(y->status, sizeof(y->status), "YAPP error: checksum mismatch");
        } else {
            yapp_handle_packet(y);
        }
    }
}

} // namespace

int oyk_yapp_is_safe_filename(const char* name) {
    if (!name || !name[0]) return 0;
    size_t len = strlen(name);
    if (len > 128) return 0;
    if (name[0] == '.' || name[len - 1] == '.' ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    if (strstr(name, "..") != NULL) return 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20U || c >= 0x7FU) return 0;
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return 0;
        }
    }
    if (is_reserved_name(name)) return 0;
    return 1;
}

void oyk_yapp_format_peer_dir(const OykCallsign* remote_call, char* out, size_t out_sz) {
    char base[8];
    size_t pos = 0;
    if (remote_call) {
        for (size_t i = 0; remote_call->base[i] && pos < 6; ++i) {
            unsigned char c = static_cast<unsigned char>(remote_call->base[i]);
            if (isalnum(c)) {
                base[pos++] = static_cast<char>(toupper(c));
            }
        }
    }
    base[pos] = '\0';
    if (pos == 0) strcpy(base, "UNKNOWN");

    unsigned int ssid = remote_call ? static_cast<unsigned int>(remote_call->ssid & 0x0FU) : 0U;
    char tmp[16];
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || \
    (!defined(_WIN32) && !defined(_WIN64) && defined(__WATCOMC__))
    /* FAT 8.3 safety: use CALL-SSID if <=8 chars, otherwise CALL.SSID (always <=6.2) */
    sprintf(tmp, "%s-%u", base, ssid);
    if (strlen(tmp) > 8) {
        sprintf(tmp, "%s.%u", base, ssid);
    }
#else
    sprintf(tmp, "%s-%u", base, ssid);
#endif
    ycopy(out, out_sz, tmp);
}

int oyk_yapp_resolve_incoming_path(const char* incoming_root,
                                   const OykCallsign* remote_call,
                                   const char* filename,
                                   char* out_path, size_t out_sz,
                                   char* out_dir, size_t out_dir_sz) {
    if (!oyk_yapp_is_safe_filename(filename)) return 0;
    const char* root = (incoming_root && incoming_root[0]) ? incoming_root : "incoming";
    char peer[16];
    oyk_yapp_format_peer_dir(remote_call, peer, sizeof(peer));

    char dir_buf[160];
    if (strlen(root) + 1 + strlen(peer) + 1 > sizeof(dir_buf)) return 0;
    sprintf(dir_buf, "%s%c%s", root, OYK_YAPP_SEP, peer);

    if (out_dir && out_dir_sz > 0) {
        ycopy(out_dir, out_dir_sz, dir_buf);
    }
    if (strlen(dir_buf) + 1 + strlen(filename) + 1 > out_sz) return 0;
    sprintf(out_path, "%s%c%s", dir_buf, OYK_YAPP_SEP, filename);
    return 1;
}

void oyk_yapp_init(OykYappSession* y, const char* incoming_root,
                   OykYappSendFn send_fn, void* send_ctx,
                   OykYappOverwriteFn overwrite_fn, void* overwrite_ctx) {
    memset(y, 0, sizeof(*y));
    ycopy(y->incoming_root, sizeof(y->incoming_root),
          (incoming_root && incoming_root[0]) ? incoming_root : "incoming");
    oyk_parse_callsign("UNKNOWN", &y->remote_call);
    y->send_fn = send_fn;
    y->send_ctx = send_ctx;
    y->overwrite_fn = overwrite_fn;
    y->overwrite_ctx = overwrite_ctx;
    y->state = OYK_YAPP_IDLE;
}

void oyk_yapp_reset(OykYappSession* y) {
    yapp_close_fp(y);
    y->state = OYK_YAPP_IDLE;
    y->sending = 0;
    y->yappc = 0;
    y->pending_enq = 0;
    y->filename[0] = '\0';
    y->target_dir[0] = '\0';
    y->target_path[0] = '\0';
    y->total_bytes = 0;
    y->bytes_transferred = 0;
    y->status[0] = '\0';
    y->parse_stage = 0;
    y->pkt_type = 0;
    y->pkt_expected = 0;
    y->pkt_len = 0;
    y->pkt_expect_csum = 0;
}

void oyk_yapp_set_remote(OykYappSession* y, const OykCallsign* remote_call) {
    if (remote_call) y->remote_call = *remote_call;
}

int oyk_yapp_is_active(const OykYappSession* y) {
    return (y->state != OYK_YAPP_IDLE &&
            y->state != OYK_YAPP_COMPLETE &&
            y->state != OYK_YAPP_ABORTED &&
            y->state != OYK_YAPP_ERROR) ? 1 : 0;
}

int oyk_yapp_start_send(OykYappSession* y, const char* local_path,
                        char* err_out, size_t err_sz) {
    if (oyk_yapp_is_active(y)) {
        ycopy(err_out, err_sz, "YAPP transfer already in progress");
        return 0;
    }
    oyk_yapp_reset(y);

    char clean_base[128];
    ycopy(clean_base, sizeof(clean_base), path_basename(local_path));
    {
        size_t bl = strlen(clean_base);
        while (bl > 0 && clean_base[bl - 1] == '.') {
            clean_base[--bl] = '\0';
        }
    }
    if (!oyk_yapp_is_safe_filename(clean_base)) {
        ycopy(err_out, err_sz, "Invalid filename for YAPP");
        return 0;
    }
    FILE* f = fopen(local_path, "rb");
    if (!f) {
        ycopy(err_out, err_sz, "Unable to open file for reading");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;

    y->fp = f;
    y->total_bytes = static_cast<oyk_u32>(sz);
    ycopy(y->filename, sizeof(y->filename), clean_base);
    ycopy(y->target_path, sizeof(y->target_path), local_path);
    y->sending = 1;
    y->state = OYK_YAPP_SEND_WAIT_INIT;
    sprintf(y->status, "YAPP: Sending init for %.60s...", y->filename);
    yapp_send_2(y, OYK_YAPP_ENQ, 0x01U);
    return 1;
}

void oyk_yapp_cancel(OykYappSession* y, const char* reason) {
    if (!oyk_yapp_is_active(y)) return;
    yapp_close_fp(y);
    yapp_send_cancel(y, reason ? reason : "Cancelled by user");
    y->state = OYK_YAPP_ABORTED;
    ycopy(y->status, sizeof(y->status), "YAPP transfer cancelled");
}

void oyk_yapp_resolve_overwrite(OykYappSession* y, int allow_overwrite) {
    if (y->state != OYK_YAPP_RECV_OVERWRITE) return;
    if (!allow_overwrite) {
        yapp_send_cancel(y, "File exists; overwrite declined");
        y->state = OYK_YAPP_ABORTED;
        ycopy(y->status, sizeof(y->status), "YAPP: Skipped existing file");
        return;
    }
    yapp_open_recv_file(y);
}

size_t oyk_yapp_feed_bytes(OykYappSession* y, const oyk_u8* data, size_t len,
                           oyk_u8* plain_out, size_t plain_cap) {
    size_t out_n = 0;
    for (size_t i = 0; i < len; ++i) {
        oyk_u8 b = data[i];
        if (!oyk_yapp_is_active(y)) {
            if (y->pending_enq) {
                if (b == 0x01U) {
                    y->pending_enq = 0;
                    oyk_yapp_reset(y);
                    y->sending = 0;
                    y->state = OYK_YAPP_RECV_WAIT_HDR;
                    ycopy(y->status, sizeof(y->status), "YAPP: Incoming transfer; waiting for header...");
                    yapp_send_2(y, OYK_YAPP_ACK, 0x01U);
                    continue;
                } else if (b == OYK_YAPP_ENQ) {
                    if (plain_out && out_n < plain_cap) plain_out[out_n++] = OYK_YAPP_ENQ;
                    continue;
                } else {
                    y->pending_enq = 0;
                    if (plain_out && out_n < plain_cap) plain_out[out_n++] = OYK_YAPP_ENQ;
                    if (plain_out && out_n < plain_cap) plain_out[out_n++] = b;
                    continue;
                }
            }
            if (b == OYK_YAPP_ENQ) {
                y->pending_enq = 1;
                continue;
            }
            if (plain_out && out_n < plain_cap) plain_out[out_n++] = b;
            continue;
        }
        yapp_handle_byte(y, b);
    }
    return out_n;
}
