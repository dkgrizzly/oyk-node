/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TERM_HPP
#define OYK_TERM_HPP

/*
 * Shared C++98 / 16-bit-safe Terminal Line-Ending & Word-Wrap Engine
 *
 * - Normalizes CRLF, LFCR, bare CR, bare LF, and Telnet CR-NUL across chunk
 *   boundaries without dropping blank lines or double-spacing CRLF/LFCR.
 * - Auto-detects the peer's active wire line ending (CR, LF, or CRLF) with
 *   optional transport lock (e.g. RFC 854 CRLF for Telnet) and manual /eol override.
 * - Provides dynamic flush-left word-wrapping and horizontal input panning for
 *   text-mode and framebuffer TUI terminals.
 */

#include <stddef.h>

enum OykTermEolMode {
    OYK_TERM_EOL_AUTO = 0,
    OYK_TERM_EOL_CR   = 1,
    OYK_TERM_EOL_LF   = 2,
    OYK_TERM_EOL_CRLF = 3
};

enum OykTermFeedAction {
    OYK_TERM_ACT_IGNORE  = 0,
    OYK_TERM_ACT_NEWLINE = 1,
    OYK_TERM_ACT_CHAR    = 2,
    OYK_TERM_ACT_BS      = 3,
    OYK_TERM_ACT_CTRL_Z  = 4
};

struct OykTermEolParser {
    int default_eol;   /* OYK_TERM_EOL_CR / LF / CRLF fallback before detection */
    int override_mode; /* OYK_TERM_EOL_AUTO (0) or forced CR / LF / CRLF */
    int detected_eol;  /* 0 = none yet, else OYK_TERM_EOL_CR / LF / CRLF */
    int auto_detect;   /* 1 = adapt detected_eol from RX, 0 = fixed transport (e.g. Telnet) */
    int pending_cr;
    int pending_lf;
};

struct OykTermWrapSpan {
    size_t start;
    size_t len;
};

void oyk_term_eol_init(OykTermEolParser* p, int default_eol, int auto_detect);
void oyk_term_eol_reset(OykTermEolParser* p, int default_eol, int auto_detect);
int  oyk_term_eol_feed_byte(OykTermEolParser* p, unsigned char ch);
int  oyk_term_eol_effective_tx(const OykTermEolParser* p);
const char* oyk_term_eol_tx_bytes(const OykTermEolParser* p, size_t* len_out);
const char* oyk_term_eol_mode_bytes(int eol_mode, size_t* len_out);
const char* oyk_term_eol_label(const OykTermEolParser* p);
int  oyk_term_eol_parse_mode(const char* text, int* mode_out);

/* Normalizes CRLF/LFCR/CR/LF inside src to the specified eol_mode bytes. */
size_t oyk_term_format_tx_text(const char* src, int eol_mode, char* dst, size_t dst_cap);

/* Flush-left whitespace word-wrap with hard-break fallback; stores up to max_spans spans. */
int  oyk_term_wrap_line(const char* line, size_t line_len, int width,
                        OykTermWrapSpan* out_spans, int max_spans);
int  oyk_term_count_wrapped_rows(const char* line, size_t line_len, int width);

/* Computes horizontally panned start offset and 0-based cursor column inside [0..avail_w-1]. */
void oyk_term_input_slice(size_t input_len, int avail_w,
                          size_t* out_start, int* out_cursor_col);

#endif /* OYK_TERM_HPP */
