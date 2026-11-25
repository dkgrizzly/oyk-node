/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PRINTER_HPP
#define OYK_PRINTER_HPP

/*
 * Line-by-line printer log formatting shared by every port (POSIX, MS-DOS, Windows,
 * Teensy 4.1 / TinyTMUX). Strict C++98, zero STL. Output goes directly to the
 * device (parallel/serial/USB printer class); no spooler (lpr/CUPS/WinSpool) is used.
 *
 * Protocols:
 *   generic : 7-bit printable ASCII, CR+LF, no escape codes.
 *   mx80    : Epson MX-80 (ESC @, SI/DC2 condensed, ESC E/ESC F emphasized bold).
 *   escp2   : Epson ESC/P2 (ESC @, ESC t 0 PC437, ESC M/P pitch, SI condensed,
 *             ESC E bold, ESC - underline).
 */

#include <stddef.h>

enum OykPrinterProtocol {
    OYK_PRN_NONE = 0,
    OYK_PRN_GENERIC,
    OYK_PRN_MX80,
    OYK_PRN_ESCP2
};

/* Severity bits mirror OykLogLevel (ERROR=0x01 WARNING=0x02 INFO=0x04 KICK=0x10 BAN=0x20 APPEAL=0x40). */

int oyk_printer_parse_protocol(const char* name);
const char* oyk_printer_protocol_name(int proto);

/* Builds the one-time initialization byte sequence. Returns bytes written (0 if none). */
size_t oyk_printer_build_init(int proto, int condensed, unsigned char* out, size_t cap);

/*
 * Formats one log line (without trailing newline in `text`) into printer bytes,
 * including styling for high-severity levels and terminating CR+LF.
 * Returns bytes written (0 on overflow or proto == OYK_PRN_NONE).
 */
size_t oyk_printer_format_line(int proto, unsigned long level, const char* text,
                               unsigned char* out, size_t cap);

#endif /* OYK_PRINTER_HPP */
