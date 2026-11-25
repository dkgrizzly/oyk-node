/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_printer.hpp"

#include <string.h>

#define PRN_ESC 0x1BU
#define PRN_SI  0x0FU
#define PRN_DC2 0x12U

static int prn_ieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
        if (ca != cb) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

int oyk_printer_parse_protocol(const char* name) {
    if (!name || !name[0]) return OYK_PRN_NONE;
    if (prn_ieq(name, "generic") || prn_ieq(name, "text") || prn_ieq(name, "ascii")) return OYK_PRN_GENERIC;
    if (prn_ieq(name, "mx80") || prn_ieq(name, "mx-80") || prn_ieq(name, "epson")) return OYK_PRN_MX80;
    if (prn_ieq(name, "escp2") || prn_ieq(name, "esc/p2") || prn_ieq(name, "escp")) return OYK_PRN_ESCP2;
    return OYK_PRN_NONE;
}

const char* oyk_printer_protocol_name(int proto) {
    switch (proto) {
        case OYK_PRN_GENERIC: return "generic";
        case OYK_PRN_MX80:    return "mx80";
        case OYK_PRN_ESCP2:   return "escp2";
        default:              return "none";
    }
}

static size_t prn_put(unsigned char* out, size_t cap, size_t pos, const unsigned char* data, size_t n) {
    if (pos + n > cap) return cap + 1;
    memcpy(out + pos, data, n);
    return pos + n;
}

size_t oyk_printer_build_init(int proto, int condensed, unsigned char* out, size_t cap) {
    if (!out) return 0;
    size_t pos = 0;
    if (proto == OYK_PRN_MX80) {
        const unsigned char reset[2] = { PRN_ESC, '@' };
        pos = prn_put(out, cap, pos, reset, 2);
        const unsigned char pitch[1] = { static_cast<unsigned char>(condensed ? PRN_SI : PRN_DC2) };
        pos = prn_put(out, cap, pos, pitch, 1);
    } else if (proto == OYK_PRN_ESCP2) {
        const unsigned char seq[6] = { PRN_ESC, '@', PRN_ESC, 't', 0, 0 };
        pos = prn_put(out, cap, pos, seq, 5); /* ESC @, ESC t 0 (PC437) */
        const unsigned char pitch[3] = { PRN_ESC,
                                         static_cast<unsigned char>(condensed ? 'P' : 'M'),
                                         0 };
        pos = prn_put(out, cap, pos, pitch, 2); /* ESC P (10 CPI) / ESC M (12 CPI) */
        if (condensed) {
            const unsigned char si[1] = { PRN_SI };
            pos = prn_put(out, cap, pos, si, 1);
        }
    } else {
        return 0;
    }
    return (pos > cap) ? 0 : pos;
}

size_t oyk_printer_format_line(int proto, unsigned long level, const char* text,
                               unsigned char* out, size_t cap) {
    if (!out || !text || proto == OYK_PRN_NONE) return 0;
    size_t pos = 0;
    int strong = ((level & 0x61UL) != 0UL) ? 1 : 0;  /* ERROR | BAN | APPEAL */
    int warn   = ((level & 0x02UL) != 0UL) ? 1 : 0;  /* WARNING */

    if (proto == OYK_PRN_MX80 && (strong || warn)) {
        const unsigned char on[2] = { PRN_ESC, 'E' };
        pos = prn_put(out, cap, pos, on, 2);
    } else if (proto == OYK_PRN_ESCP2 && (strong || warn)) {
        const unsigned char on[2] = { PRN_ESC, 'E' };
        pos = prn_put(out, cap, pos, on, 2);
        if (strong) {
            const unsigned char ul[3] = { PRN_ESC, '-', 1 };
            pos = prn_put(out, cap, pos, ul, 3);
        }
    }

    for (const char* p = text; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20U || c > 0x7EU) c = (c == '\t') ? ' ' : '?';
        if (*p == '\r' || *p == '\n') break;
        if (pos >= cap) return 0;
        out[pos++] = c;
    }

    if ((proto == OYK_PRN_MX80 || proto == OYK_PRN_ESCP2) && (strong || warn)) {
        if (proto == OYK_PRN_ESCP2 && strong) {
            const unsigned char ul[3] = { PRN_ESC, '-', 0 };
            pos = prn_put(out, cap, pos, ul, 3);
        }
        const unsigned char off[2] = { PRN_ESC, 'F' };
        pos = prn_put(out, cap, pos, off, 2);
    }

    const unsigned char crlf[2] = { '\r', '\n' };
    pos = prn_put(out, cap, pos, crlf, 2);
    return (pos > cap) ? 0 : pos;
}
