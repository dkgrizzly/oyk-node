/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_RIG_HPP
#define OYK_RIG_HPP

/*
 * Strict C++98 / 16-bit-clean Rig Control Protocol Module:
 *   1. Hamlib rigctld TCP protocol (default port 4532)
 *   2. RadioFoundry / takRF UDP zone & channel control protocol (default port 4191)
 *      Ref: https://radiofoundry.org/docs/spec/tcp-services
 *
 * Uses zero STL, zero exceptions, zero RTTI, and bounded fixed buffers so it
 * compiles identically on 16-bit MS-DOS (OpenWatcom), Win16, Win32/64, and Linux.
 */

#include "oyk_portable.hpp"

#define OYK_RIG_PROTO_NONE          0
#define OYK_RIG_PROTO_RIGCTL        1
#define OYK_RIG_PROTO_RADIOFOUNDRY  2

#define OYK_RIGCTL_DEFAULT_PORT     4532U
#define OYK_RF_DEFAULT_PORT         4191U

struct OykRigStatus {
    int reachable;
    int protocol;              /* OYK_RIG_PROTO_* */
    oyk_u32 freq_hz;           /* 32-bit unsigned Hz (up to 4.29 GHz) */
    char mode[16];             /* e.g. FM, USB, LSB, CW, PKTFM, DMR */
    oyk_u32 passband_hz;
    int ptt;                   /* -1=unknown, 0=RX, 1=TX */
    char vfo[16];              /* e.g. VFOA, VFOB */

    /* RadioFoundry / takRF UDP 4191 fields (0-based zone and pos) */
    int zone;                  /* -1 = unknown */
    int pos;                   /* -1 = unknown */
    char channel_name[32];
    int is_dmr;
    int color_code;            /* -1 = unknown */
    int timeslot;              /* -1 = unknown */
    long talkgroup;            /* -1 = unknown */
    int notifications_active;

    char raw_status[128];
    char last_error[96];
};

/* Protocol name / default port helpers */
int oyk_rig_parse_protocol(const char* text);
const char* oyk_rig_protocol_name(int proto);
oyk_u16 oyk_rig_default_port(int proto);

void oyk_rig_status_init(OykRigStatus* st, int proto);

/* Hamlib rigctl / rigctld command builders (newline-terminated ASCII) */
size_t oyk_rigctl_build_get_freq(char* out, size_t cap);
size_t oyk_rigctl_build_set_freq(oyk_u32 hz, char* out, size_t cap);
size_t oyk_rigctl_build_get_mode(char* out, size_t cap);
size_t oyk_rigctl_build_set_mode(const char* mode, oyk_u32 passband_hz, char* out, size_t cap);
size_t oyk_rigctl_build_get_ptt(char* out, size_t cap);
size_t oyk_rigctl_build_set_ptt(int tx, char* out, size_t cap);
size_t oyk_rigctl_build_get_vfo(char* out, size_t cap);
size_t oyk_rigctl_build_set_vfo(const char* vfo, char* out, size_t cap);

/* Hamlib rigctl response parsers */
int oyk_rigctl_parse_rprt(const char* line, int* code_out);
int oyk_rigctl_parse_freq_reply(const char* reply, oyk_u32* hz_out, int* rprt_out);
int oyk_rigctl_parse_mode_reply(const char* reply, char* mode_out, size_t mode_sz,
                                oyk_u32* passband_out, int* rprt_out);
int oyk_rigctl_parse_ptt_reply(const char* reply, int* ptt_out, int* rprt_out);
int oyk_rigctl_parse_vfo_reply(const char* reply, char* vfo_out, size_t vfo_sz, int* rprt_out);

/* RadioFoundry / takRF UDP 4191 command builders */
size_t oyk_rf_build_select(int zone, int pos, char* out, size_t cap);
size_t oyk_rf_build_stop_notify(char* out, size_t cap);
size_t oyk_rf_build_step_channel(int delta, char* out, size_t cap);
size_t oyk_rf_build_step_zone(int delta, char* out, size_t cap);

/* RadioFoundry / takRF UDP 4191 status reply / notification parser */
int oyk_rf_parse_status(const char* text, OykRigStatus* st);

/* Format a concise one-line human summary of OykRigStatus */
void oyk_rig_format_summary(const OykRigStatus* st, char* out, size_t out_sz);

#endif /* OYK_RIG_HPP */
