/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_term.hpp"

#include <string.h>

static int oyk_term_normalize_default(int default_eol) {
    if (default_eol == OYK_TERM_EOL_LF || default_eol == OYK_TERM_EOL_CRLF) {
        return default_eol;
    }
    return OYK_TERM_EOL_CR;
}

static int oyk_term_ascii_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A');
    return ch;
}

static int oyk_term_stricmp(const char* a, const char* b) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        int da = oyk_term_ascii_tolower(static_cast<unsigned char>(*a));
        int db = oyk_term_ascii_tolower(static_cast<unsigned char>(*b));
        if (da != db) return da - db;
        ++a;
        ++b;
    }
    return oyk_term_ascii_tolower(static_cast<unsigned char>(*a)) -
           oyk_term_ascii_tolower(static_cast<unsigned char>(*b));
}

void oyk_term_eol_init(OykTermEolParser* p, int default_eol, int auto_detect) {
    if (!p) return;
    p->default_eol = oyk_term_normalize_default(default_eol);
    p->override_mode = OYK_TERM_EOL_AUTO;
    p->detected_eol = 0;
    p->auto_detect = auto_detect ? 1 : 0;
    p->pending_cr = 0;
    p->pending_lf = 0;
}

void oyk_term_eol_reset(OykTermEolParser* p, int default_eol, int auto_detect) {
    oyk_term_eol_init(p, default_eol, auto_detect);
}

int oyk_term_eol_feed_byte(OykTermEolParser* p, unsigned char ch) {
    if (!p) return OYK_TERM_ACT_IGNORE;

    if (p->pending_cr) {
        p->pending_cr = 0;
        if (ch == '\n') {
            if (p->auto_detect) p->detected_eol = OYK_TERM_EOL_CRLF;
            return OYK_TERM_ACT_IGNORE;
        }
        if (ch == 0x00U) {
            if (p->auto_detect) p->detected_eol = OYK_TERM_EOL_CR;
            return OYK_TERM_ACT_IGNORE;
        }
        if (p->auto_detect) p->detected_eol = OYK_TERM_EOL_CR;
    } else if (p->pending_lf) {
        p->pending_lf = 0;
        if (ch == '\r') {
            if (p->auto_detect) p->detected_eol = OYK_TERM_EOL_CRLF;
            return OYK_TERM_ACT_IGNORE;
        }
        if (p->auto_detect) p->detected_eol = OYK_TERM_EOL_LF;
    }

    if (ch == '\r') {
        p->pending_cr = 1;
        return OYK_TERM_ACT_NEWLINE;
    }
    if (ch == '\n') {
        p->pending_lf = 1;
        return OYK_TERM_ACT_NEWLINE;
    }
    if (ch == 0x08U || ch == 0x7FU) {
        return OYK_TERM_ACT_BS;
    }
    if (ch == 0x1AU) {
        return OYK_TERM_ACT_CTRL_Z;
    }
    if (ch == '\t' || (ch >= 0x20U && ch <= 0x7EU)) {
        return OYK_TERM_ACT_CHAR;
    }
    return OYK_TERM_ACT_IGNORE;
}

int oyk_term_eol_effective_tx(const OykTermEolParser* p) {
    if (!p) return OYK_TERM_EOL_CR;
    if (p->override_mode == OYK_TERM_EOL_CR ||
        p->override_mode == OYK_TERM_EOL_LF ||
        p->override_mode == OYK_TERM_EOL_CRLF) {
        return p->override_mode;
    }
    if (p->auto_detect) {
        if (p->detected_eol == OYK_TERM_EOL_CR ||
            p->detected_eol == OYK_TERM_EOL_LF ||
            p->detected_eol == OYK_TERM_EOL_CRLF) {
            return p->detected_eol;
        }
        if (p->pending_cr) return OYK_TERM_EOL_CR;
        if (p->pending_lf) return OYK_TERM_EOL_LF;
    }
    return oyk_term_normalize_default(p->default_eol);
}

const char* oyk_term_eol_mode_bytes(int eol_mode, size_t* len_out) {
    if (eol_mode == OYK_TERM_EOL_LF) {
        if (len_out) *len_out = 1U;
        return "\n";
    }
    if (eol_mode == OYK_TERM_EOL_CRLF) {
        if (len_out) *len_out = 2U;
        return "\r\n";
    }
    if (len_out) *len_out = 1U;
    return "\r";
}

const char* oyk_term_eol_tx_bytes(const OykTermEolParser* p, size_t* len_out) {
    return oyk_term_eol_mode_bytes(oyk_term_eol_effective_tx(p), len_out);
}

const char* oyk_term_eol_label(const OykTermEolParser* p) {
    int eff = oyk_term_eol_effective_tx(p);
    int manual = (p && p->override_mode != OYK_TERM_EOL_AUTO) ? 1 : 0;
    if (eff == OYK_TERM_EOL_LF) return manual ? "LF*" : "LF";
    if (eff == OYK_TERM_EOL_CRLF) return manual ? "CRLF*" : "CRLF";
    return manual ? "CR*" : "CR";
}

int oyk_term_eol_parse_mode(const char* text, int* mode_out) {
    if (!text) return 0;
    while (*text == ' ' || *text == '\t') ++text;
    char buf[16];
    size_t n = 0;
    while (text[n] && text[n] != ' ' && text[n] != '\t' && n < sizeof(buf) - 1) {
        buf[n] = text[n];
        ++n;
    }
    buf[n] = '\0';
    if (!buf[0]) return 0;
    if (oyk_term_stricmp(buf, "auto") == 0) {
        if (mode_out) *mode_out = OYK_TERM_EOL_AUTO;
        return 1;
    }
    if (oyk_term_stricmp(buf, "cr") == 0) {
        if (mode_out) *mode_out = OYK_TERM_EOL_CR;
        return 1;
    }
    if (oyk_term_stricmp(buf, "lf") == 0) {
        if (mode_out) *mode_out = OYK_TERM_EOL_LF;
        return 1;
    }
    if (oyk_term_stricmp(buf, "crlf") == 0 || oyk_term_stricmp(buf, "cr+lf") == 0) {
        if (mode_out) *mode_out = OYK_TERM_EOL_CRLF;
        return 1;
    }
    return 0;
}

size_t oyk_term_format_tx_text(const char* src, int eol_mode, char* dst, size_t dst_cap) {
    if (!src || !dst || dst_cap == 0) return 0;
    size_t nl_len = 0;
    const char* nl = oyk_term_eol_mode_bytes(eol_mode, &nl_len);
    size_t out_pos = 0;
    size_t cap_lim = dst_cap - 1U;
    for (size_t i = 0; src[i] != '\0'; ++i) {
        char ch = src[i];
        if (ch == '\r') {
            if (src[i + 1] == '\n') ++i;
            for (size_t k = 0; k < nl_len && out_pos < cap_lim; ++k) {
                dst[out_pos++] = nl[k];
            }
        } else if (ch == '\n') {
            if (src[i + 1] == '\r') ++i;
            for (size_t k = 0; k < nl_len && out_pos < cap_lim; ++k) {
                dst[out_pos++] = nl[k];
            }
        } else {
            if (out_pos < cap_lim) {
                dst[out_pos++] = ch;
            }
        }
    }
    dst[out_pos] = '\0';
    return out_pos;
}

static int oyk_term_wrap_impl(const char* line, size_t line_len, int width,
                              OykTermWrapSpan* out_spans, int max_spans) {
    if (width < 1) width = 1;
    if (!line || line_len == 0U) {
        if (out_spans && max_spans > 0) {
            out_spans[0].start = 0U;
            out_spans[0].len = 0U;
        }
        return 1;
    }

    const size_t w = static_cast<size_t>(width);
    size_t pos = 0U;
    int count = 0;
    while (pos < line_len) {
        size_t rem = line_len - pos;
        if (rem <= w) {
            if (out_spans && count < max_spans) {
                out_spans[count].start = pos;
                out_spans[count].len = rem;
            }
            ++count;
            break;
        }

        size_t brk = 0U;
        int found_ws = 0;
        for (size_t k = pos + w; k > pos; --k) {
            char c = line[k];
            if (c == ' ' || c == '\t') {
                brk = k;
                found_ws = 1;
                break;
            }
        }

        if (found_ws) {
            if (out_spans && count < max_spans) {
                out_spans[count].start = pos;
                out_spans[count].len = brk - pos;
            }
            ++count;
            pos = brk + 1U;
        } else {
            if (out_spans && count < max_spans) {
                out_spans[count].start = pos;
                out_spans[count].len = w;
            }
            ++count;
            pos += w;
        }
    }
    if (count == 0) {
        if (out_spans && max_spans > 0) {
            out_spans[0].start = 0U;
            out_spans[0].len = 0U;
        }
        return 1;
    }
    return count;
}

int oyk_term_wrap_line(const char* line, size_t line_len, int width,
                       OykTermWrapSpan* out_spans, int max_spans) {
    int total = oyk_term_wrap_impl(line, line_len, width, out_spans, max_spans);
    return (out_spans && total > max_spans) ? max_spans : total;
}

int oyk_term_count_wrapped_rows(const char* line, size_t line_len, int width) {
    return oyk_term_wrap_impl(line, line_len, width, NULL, 0);
}

void oyk_term_input_slice(size_t input_len, int avail_w,
                          size_t* out_start, int* out_cursor_col) {
    if (avail_w < 1) avail_w = 1;
    const size_t aw = static_cast<size_t>(avail_w);
    if (input_len < aw) {
        if (out_start) *out_start = 0U;
        if (out_cursor_col) *out_cursor_col = static_cast<int>(input_len);
    } else {
        if (out_start) *out_start = input_len - (aw - 1U);
        if (out_cursor_col) *out_cursor_col = avail_w - 1;
    }
}

int oyk_term_decode_ctrl_a_cmd(int key) {
    if (key == 27) return OYK_TCMD_CANCEL;
    if (key == 0x01 || key == 'a' || key == 'A') return OYK_TCMD_SEND_CTRL_A;
    if (key == 'z' || key == 'Z' || key == '?' || key == 'h' || key == 'H') return OYK_TCMD_HELP;
    if (key == 'c' || key == 'C') return OYK_TCMD_CONNECT;
    if (key == 'l' || key == 'L') return OYK_TCMD_LOCAL;
    if (key == 'b' || key == 'B') return OYK_TCMD_BBS;
    if (key == 't' || key == 'T') return OYK_TCMD_CHAT;
    if (key == 'd' || key == 'D') return OYK_TCMD_DISCONNECT;
    if (key == 's' || key == 'S') return OYK_TCMD_YAPP_SEND;
    if (key == 'k' || key == 'K') return OYK_TCMD_YAPP_ABORT;
    if (key == 'e' || key == 'E') return OYK_TCMD_CYCLE_EOL;
    if (key == 'p' || key == 'P') return OYK_TCMD_PORTS;
    if (key == 'w' || key == 'W') return OYK_TCMD_CLEAR;
    if (key == 'r' || key == 'R') return OYK_TCMD_REDRAW;
    if (key == 'x' || key == 'X' || key == 'q' || key == 'Q') return OYK_TCMD_CONSOLE;
    return OYK_TCMD_NONE;
}

int oyk_term_eol_cycle_mode(OykTermEolParser* p) {
    if (!p) return OYK_TERM_EOL_AUTO;
    if (p->override_mode == OYK_TERM_EOL_AUTO) p->override_mode = OYK_TERM_EOL_CR;
    else if (p->override_mode == OYK_TERM_EOL_CR) p->override_mode = OYK_TERM_EOL_LF;
    else if (p->override_mode == OYK_TERM_EOL_LF) p->override_mode = OYK_TERM_EOL_CRLF;
    else p->override_mode = OYK_TERM_EOL_AUTO;
    return p->override_mode;
}

static const OykTermHelpEntry kOykTermHelpEntries[12] = {
    { "Z / ?", "Show this hotkey help screen",      "" },
    { "C",     "Connect to port / target...",       "F2" },
    { "L",     "Quick-connect Local Node",          "" },
    { "B",     "Quick-connect Local BBS",           "" },
    { "T",     "Quick-connect Local CHAT",          "" },
    { "D",     "Disconnect active session",         "F4" },
    { "S",     "Send file via YAPP",                "F3" },
    { "K",     "Abort YAPP file transfer",          "F6" },
    { "E",     "Cycle wire EOL (AUTO/CR/LF/CRLF)",  "" },
    { "P",     "List configured ports / interfaces", "" },
    { "W / R", "Clear screen / Redraw & size",      "Ctrl+L" },
    { "X / Q", "Return to Operator Console",        "Tab/F9" }
};

const char* oyk_term_hotkey_bar_text(int is_tui, int ctrl_a_pending) {
    if (is_tui) {
        if (ctrl_a_pending) {
            return "CTRL-A: Z:Help C:Conn L:Loc B:BBS T:Chat D:Disc S:YAPP K:Abrt E:EOL P:Prt W:Clr X:Exit";
        }
        return "Ctrl-A Z:Help  F2:Connect  F3:YAPP  F4:Disc  Ctrl-A E:EOL  Tab/F9:Console";
    }
    return "Ctrl-A Z:Help  |  Ctrl-A E:EOL  |  Ctrl-A S:YAPP  |  Ctrl-A D:Disc";
}

int oyk_term_help_count() {
    return 12;
}

const OykTermHelpEntry* oyk_term_help_entry(int idx) {
    if (idx < 0 || idx >= 12) return NULL;
    return &kOykTermHelpEntries[idx];
}
