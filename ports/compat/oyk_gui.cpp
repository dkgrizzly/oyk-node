/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_gui.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gui_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1U;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int gui_strieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) !=
            tolower(static_cast<unsigned char>(*b))) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static int gui_is_all_digits(const char* text) {
    if (!text || !text[0]) return 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
    }
    return 1;
}

static int gui_parse_dmr_id(const char* text, oyk_u32* dmr_id_out) {
    if (!gui_is_all_digits(text)) return 0;
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

static int gui_parse_ipv4(const char* text, oyk_u32* net_ip_out) {
    if (!text || !text[0]) return 0;
    oyk_u8 oct[4];
    const char* p = text;
    for (int i = 0; i < 4; ++i) {
        if (!isdigit(static_cast<unsigned char>(*p))) return 0;
        unsigned long v = 0UL;
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
    if (net_ip_out) {
        oyk_u32 ip = 0;
        memcpy(&ip, oct, 4);
        *net_ip_out = ip;
    }
    return 1;
}

static int gui_parse_callsign(const char* text) {
    if (!text) return 0;
    char tmp[32];
    gui_copy(tmp, sizeof(tmp), text);
    size_t len = strlen(tmp);
    while (len > 0 && isspace(static_cast<unsigned char>(tmp[len - 1]))) {
        tmp[--len] = '\0';
    }
    char* p = tmp;
    while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
    if (!*p) return 0;
    char* dash = strchr(p, '-');
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
    }
    size_t blen = strlen(p);
    if (blen == 0 || blen > 6) return 0;
    for (size_t i = 0; i < blen; ++i) {
        if (!isalnum(static_cast<unsigned char>(p[i]))) return 0;
    }
    return 1;
}

static void gui_derive_default_iface_id(const OykConfig* cfg, const char* iface_type,
                                        char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    const char* prefix = "iface";
    if (iface_type && iface_type[0]) {
        if (gui_strieq(iface_type, "axudp")) prefix = "axudp";
        else if (gui_strieq(iface_type, "axip")) prefix = "axip";
        else if (gui_strieq(iface_type, "kiss-serial")) prefix = "kiss";
        else if (gui_strieq(iface_type, "kiss-tcp")) prefix = "kisstcp";
        else if (gui_strieq(iface_type, "telnet")) prefix = "telnet";
        else if (gui_strieq(iface_type, "ppp")) prefix = "ppp";
        else if (gui_strieq(iface_type, "usb-cdc-net")) prefix = "usbnet";
        else if (gui_strieq(iface_type, "usb-cdc-serial")) prefix = "usbser";
    }
    for (int n = 0; n < 100; ++n) {
        char cand[24];
        sprintf(cand, "%s%d", prefix, n);
        int used = 0;
        if (cfg) {
            for (int i = 0; i < cfg->iface_count && i < OYK_MAX_INTERFACES; ++i) {
                if (gui_strieq(cfg->ifaces[i].id, cand)) {
                    used = 1;
                    break;
                }
            }
        }
        if (!used) {
            gui_copy(out, out_sz, cand);
            return;
        }
    }
    gui_copy(out, out_sz, prefix);
}

static const char* gui_format_learned_policy(oyk_u8 policy) {
    switch (policy) {
        case OYK_LEARN_NONE:     return "none";
        case OYK_LEARN_INBOUND:  return "inbound";
        case OYK_LEARN_OUTBOUND: return "outbound";
        case OYK_LEARN_BOTH:     return "both";
        default:                 return "inherit";
    }
}

static const OykGuiMenuItem kNodeMenuItems[13] = {
    { OYK_ACT_ALLOW_INCOMING, "Allow Incoming Connections",  "_Allow Incoming Connections",  "Allow Incoming Connections",  'A', 0 },
    { OYK_ACT_DENY_INCOMING,  "Deny Incoming Connections",   "_Deny Incoming Connections",   "Deny Incoming Connections",   'D', 0 },
    { OYK_ACT_NONE,           "-",                           "-",                            "-",                           0,   1 },
    { OYK_ACT_LOCAL_TERM,     "Local Terminal...",           "_Local Terminal...",           "Local Terminal...",           'T', 0 },
    { OYK_ACT_FILES,          "Manage Files Area...",        "Manage _Files Area...",        "Manage Files Area...",        0,   0 },
    { OYK_ACT_RIG,            "Radio Control...",            "_Radio Control...",            "Radio Control...",            0,   0 },
    { OYK_ACT_SETTINGS,       "Settings...",                 "_Settings...",                 "Settings...",                 ',', 0 },
    { OYK_ACT_PEERS,          "Peers...",                    "_Peers...",                    "Peers...",                    0,   0 },
    { OYK_ACT_THEME,          "Theme...",                    "T_heme...",                    "Theme...",                    0,   0 },
    { OYK_ACT_TERMINATE,      "Terminate Connection...",     "_Terminate Connection...",     "Terminate Connection...",     0,   0 },
    { OYK_ACT_BAN,            "Ban / Unban Callsign...",     "_Ban / Unban Callsign...",     "Ban / Unban Callsign...",     0,   0 },
    { OYK_ACT_NONE,           "-",                           "-",                            "-",                           0,   1 },
    { OYK_ACT_EXIT,           "Exit",                        "E_xit",                        "Quit",                        'Q', 0 }
};

int oyk_gui_node_menu_count() {
    return 13;
}

const OykGuiMenuItem* oyk_gui_node_menu_item(int idx) {
    if (idx < 0 || idx >= 13) return NULL;
    return &kNodeMenuItems[idx];
}

const char* oyk_gui_screen_title(int screen_id) {
    switch (screen_id) {
        case OYK_SCR_MAIN:       return "OYK Amateur Radio Packet Node";
        case OYK_SCR_SETTINGS:   return "Node Settings";
        case OYK_SCR_ADD_IFACE:  return "Add Interface";
        case OYK_SCR_EDIT_IFACE: return "Edit Interface";
        case OYK_SCR_PEERS:      return "Peers";
        case OYK_SCR_EDIT_PEER:  return "Edit Peer";
        case OYK_SCR_TERMINATE:  return "Terminate Active Connection";
        case OYK_SCR_BAN:        return "Ban / Unban Callsign";
        case OYK_SCR_EXIT_PEERS: return "Auto-Learned Peers";
        case OYK_SCR_THEME:      return "Theme Settings";
        case OYK_SCR_TERMINAL:   return "OYK Local Packet Terminal (YAPP)";
        case OYK_SCR_FILES:      return "Manage BBS Files Area";
        case OYK_SCR_RIG:        return "Radio Control";
        case OYK_SCR_ABOUT:      return "About OYK Amateur Radio Packet Node";
        case OYK_SCR_MENU:       return "Node Menu";
        case OYK_SCR_TERM_HELP:  return "Terminal Hotkey Help";
        default:                 return "OYK Packet Node";
    }
}

static const char* const kSettingsTabTitles[7] = {
    "Node", "BBS", "CHAT", "Interfaces", "Peers", "Rig", "Printer"
};

int oyk_gui_settings_tab_count() {
    return 7;
}

const char* oyk_gui_settings_tab_title(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= 7) return "";
    return kSettingsTabTitles[tab_idx];
}

struct GuiTableSchema {
    int count;
    const char* cols[7];
};

static const GuiTableSchema kTableSchemas[9] = {
    /* OYK_TBL_IFACES */
    { 5, { "Port", "Type", "Conn", "Status", "RX/TX", NULL, NULL } },
    /* OYK_TBL_HEARD */
    { 4, { "#", "Callsign", "Port", "Heard", NULL, NULL, NULL } },
    /* OYK_TBL_CONNECTIONS */
    { 7, { "ID", "Callsign", "Port", "Dir", "State", "Since", "Role" } },
    /* OYK_TBL_TERMINATE */
    { 4, { "ID", "Callsign", "Port", "State", NULL, NULL, NULL } },
    /* OYK_TBL_PEERS */
    { 5, { "ID", "Callsign", "Interface", "Endpoint", "Policy", NULL, NULL } },
    /* OYK_TBL_BANS */
    { 5, { "Callsign", "Banned At", "Lift At", "Flags", "Reason", NULL, NULL } },
    /* OYK_TBL_FILES_INCOMING */
    { 3, { "Uploader", "Filename", "Size", NULL, NULL, NULL, NULL } },
    /* OYK_TBL_FILES_APPROVED */
    { 2, { "Filename", "Size", NULL, NULL, NULL, NULL, NULL } },
    /* OYK_TBL_THEME */
    { 4, { "Role", "Key", "Color", "Preview", NULL, NULL, NULL } }
};

int oyk_gui_table_column_count(int table_id) {
    if (table_id < 0 || table_id >= 9) return 0;
    return kTableSchemas[table_id].count;
}

const char* oyk_gui_table_column(int table_id, int col_idx) {
    if (table_id < 0 || table_id >= 9) return "";
    if (col_idx < 0 || col_idx >= kTableSchemas[table_id].count) return "";
    return kTableSchemas[table_id].cols[col_idx];
}

int oyk_gui_terminate_columns(const char** out_cols, int max_cols) {
    int n = oyk_gui_table_column_count(OYK_TBL_TERMINATE);
    int c = (max_cols < n) ? max_cols : n;
    for (int i = 0; i < c; ++i) {
        out_cols[i] = kTableSchemas[OYK_TBL_TERMINATE].cols[i];
    }
    return c;
}

int oyk_gui_control_box_size(int char_h) {
    if (char_h <= 10) return 10;
    if (char_h <= 14) return 12;
    return 13;
}

int oyk_gui_control_label_offset(int char_h) {
    return oyk_gui_control_box_size(char_h) + 6;
}

int oyk_gui_vertical_text_offset(int box_h, int char_h) {
    if (box_h <= 0 || char_h <= 0 || box_h <= char_h) return 0;
    return (box_h - char_h) / 2;
}

int oyk_gui_control_height(int char_h, int pad_y) {
    int ch = (char_h > 0) ? char_h : 8;
    int py = (pad_y > 0) ? pad_y : 0;
    return ch + (py * 2);
}

const char* oyk_gui_format_learned_policy(int policy) {
    return gui_format_learned_policy(static_cast<oyk_u8>(policy));
}

int oyk_gui_cycle_learned_policy(int policy, int delta) {
    int cur = policy;
    if (cur < 0 || cur > static_cast<int>(OYK_LEARN_BOTH)) cur = 0;
    int step = (delta < 0) ? -1 : 1;
    int next = (cur + 5 + step) % 5;
    return next;
}

const char* oyk_gui_about_summary() {
    return "OYK Amateur Radio Packet Node (GPL-3.0-or-later)\n"
           "AX.25 / KISS / AXUDP / AXIP / BBS / CHAT / YAPP / Rig CAT\n"
           "Hotkeys: M/F10 Node Menu | Tab/F9 Terminal | 1..7 Tabs | Esc Close";
}

void oyk_gui_format_duration(oyk_u32 secs, char* dst, size_t cap) {
    if (!dst || cap == 0) return;
    oyk_u32 h = secs / 3600UL;
    oyk_u32 m = (secs % 3600UL) / 60UL;
    oyk_u32 s = secs % 60UL;
    char buf[32];
    if (h > 0) {
        sprintf(buf, "%luh %lum %lus",
                static_cast<unsigned long>(h),
                static_cast<unsigned long>(m),
                static_cast<unsigned long>(s));
    } else if (m > 0) {
        sprintf(buf, "%lum %lus",
                static_cast<unsigned long>(m),
                static_cast<unsigned long>(s));
    } else {
        sprintf(buf, "%lus", static_cast<unsigned long>(s));
    }
    gui_copy(dst, cap, buf);
}

const char* oyk_gui_format_incoming_pill(int accept_in, int compact) {
    if (compact) {
        return accept_in ? "[IN:ALLOW]" : "[IN:DENY]";
    }
    return accept_in ? "[INCOMING: ALLOW]" : "[INCOMING: DENY]";
}

void oyk_gui_format_console_banner(const char* callsign, const char* alias,
                                   int accept_in, int conn_count, int iface_count,
                                   char* dst, size_t cap) {
    if (!dst || cap == 0) return;
    const char* cs = (callsign && callsign[0]) ? callsign : "N0CALL-7";
    char id_part[48];
    if (alias && alias[0]) {
        sprintf(id_part, "%.16s (%.16s)", cs, alias);
    } else {
        gui_copy(id_part, sizeof(id_part), cs);
    }
    char full[140];
    sprintf(full, "OYK NODE: %s | Ifaces: %d | Conn: %d | %s",
            id_part, iface_count, conn_count,
            oyk_gui_format_incoming_pill(accept_in, 0));
    gui_copy(dst, cap, full);
}

void oyk_gui_format_console_header(const char* callsign, const char* alias,
                                   oyk_u32 uptime_sec, int conn_count,
                                   int accept_in, int compact,
                                   char* dst, size_t cap) {
    if (!dst || cap == 0) return;
    const char* cs = (callsign && callsign[0]) ? callsign : "N0CALL-7";
    char dur[32];
    oyk_gui_format_duration(uptime_sec, dur, sizeof(dur));
    char id_part[48];
    if (alias && alias[0]) {
        sprintf(id_part, "%.16s (%.16s)", cs, alias);
    } else {
        gui_copy(id_part, sizeof(id_part), cs);
    }
    char full[140];
    if (compact) {
        sprintf(full, "OYK: %s | Up:%s | C:%d | %s",
                id_part, dur, conn_count,
                oyk_gui_format_incoming_pill(accept_in, 1));
    } else {
        sprintf(full, "OYK PACKET NODE: %s  |  Uptime: %s  |  Active Connections: %d",
                id_part, dur, conn_count);
    }
    gui_copy(dst, cap, full);
}

int oyk_gui_parse_port_str(const char* text, oyk_u16* out_port) {
    if (!text || !text[0]) return 0;
    const char* p = text;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return 0;
    for (const char* q = p; *q; ++q) {
        if (!isdigit(static_cast<unsigned char>(*q))) return 0;
    }
    long v = strtol(p, NULL, 10);
    if (v <= 0L || v > 65535L) return 0;
    if (out_port) *out_port = static_cast<oyk_u16>(v);
    return 1;
}

int oyk_gui_validate_iface_draft(const OykConfig* cfg, int edit_idx,
                                 const OykIfaceConfig* draft,
                                 char* err, size_t err_cap) {
    if (!draft) {
        gui_copy(err, err_cap, "Invalid interface configuration.");
        return 0;
    }
    if (!draft->id[0]) {
        gui_copy(err, err_cap, "Interface ID cannot be empty.");
        return 0;
    }
    if (cfg) {
        for (int i = 0; i < cfg->iface_count; ++i) {
            if (i != edit_idx && gui_strieq(cfg->ifaces[i].id, draft->id)) {
                gui_copy(err, err_cap, "An interface with that ID already exists.");
                return 0;
            }
        }
    }
    if (gui_strieq(draft->type, "kiss-serial")) {
        if (!draft->device[0]) {
            gui_copy(err, err_cap, "Serial device cannot be empty.");
            return 0;
        }
        if (draft->baud < 300UL) {
            gui_copy(err, err_cap, "Baud rate must be at least 300.");
            return 0;
        }
    } else if (gui_strieq(draft->type, "axudp") || gui_strieq(draft->type, "axip")) {
        if (!draft->host[0]) {
            gui_copy(err, err_cap, "Bind host or DMR ID cannot be empty.");
            return 0;
        }
        if (gui_is_all_digits(draft->host)) {
            oyk_u32 did = 0;
            if (!gui_parse_dmr_id(draft->host, &did)) {
                gui_copy(err, err_cap, "Invalid DMR ID (allowed 1..16777214).");
                return 0;
            }
        }
        if (gui_strieq(draft->type, "axudp") && draft->port == 0U) {
            gui_copy(err, err_cap, "UDP port must be between 1 and 65535.");
            return 0;
        }
        if (draft->netmask[0]) {
            oyk_u32 mip = 0;
            if (!gui_parse_ipv4(draft->netmask, &mip)) {
                gui_copy(err, err_cap, "Invalid IPv4 netmask (e.g. 255.0.0.0).");
                return 0;
            }
        }
    } else if (gui_strieq(draft->type, "kiss-tcp") || gui_strieq(draft->type, "telnet")) {
        if (!draft->host[0]) {
            gui_copy(err, err_cap, "Host / IP cannot be empty.");
            return 0;
        }
        if (draft->port == 0U) {
            gui_copy(err, err_cap, "TCP port must be between 1 and 65535.");
            return 0;
        }
    } else {
        gui_copy(err, err_cap, "Unsupported interface driver type.");
        return 0;
    }
    if (err && err_cap > 0) err[0] = '\0';
    return 1;
}

int oyk_gui_validate_peer_draft(const OykPeerConfig* draft, char* err, size_t err_cap) {
    if (!draft) {
        gui_copy(err, err_cap, "Invalid peer configuration.");
        return 0;
    }
    if (!draft->id[0]) {
        gui_copy(err, err_cap, "Peer ID cannot be empty.");
        return 0;
    }
    if (!draft->callsign[0]) {
        gui_copy(err, err_cap, "Peer callsign cannot be empty.");
        return 0;
    }
    if (!gui_parse_callsign(draft->callsign)) {
        gui_copy(err, err_cap, "Invalid AX.25 callsign.");
        return 0;
    }
    if (err && err_cap > 0) err[0] = '\0';
    return 1;
}

void oyk_gui_format_exit_prompt(int auto_learned_count,
                                char* title, size_t tcap,
                                char* body, size_t bcap) {
    gui_copy(title, tcap, oyk_gui_screen_title(OYK_SCR_EXIT_PEERS));
    if (body && bcap > 0) {
        char buf[192];
        sprintf(buf,
                "%d auto-learned peer(s) were added during this session.\n"
                "Save them to configuration, discard them, or review in Peers?",
                auto_learned_count);
        gui_copy(body, bcap, buf);
    }
}

void oyk_gui_build_exit_prompt(int auto_learned_count, int active_conns,
                               OykGuiExitPrompt* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->auto_learned_count = auto_learned_count;
    if (auto_learned_count > 0) {
        out->has_auto_learned = 1;
        oyk_gui_format_exit_prompt(auto_learned_count,
                                   out->title, sizeof(out->title),
                                   out->body, sizeof(out->body));
    } else {
        out->has_auto_learned = 0;
        gui_copy(out->title, sizeof(out->title), "Exit");
        if (active_conns > 0) {
            sprintf(out->body, "Exit node? (%d active)", active_conns);
        } else {
            gui_copy(out->body, sizeof(out->body), "Exit node?");
        }
    }
}

/* ==================== Shared Add Interface Wizard ==================== */

struct GuiDriverSpec {
    oyk_u32 bit;
    const char* type;
    const char* desc;
    const char* def_host;
    const char* def_port;
};

static const GuiDriverSpec kDrivers[5] = {
    { OYK_DRV_AXUDP,       "axudp",       "AX.25 over UDP/IPv4 (Port 10093)",          "0.0.0.0",   "10093" },
    { OYK_DRV_AXIP,        "axip",        "AX.25 over raw IP (Protocol 93)",           "0.0.0.0",   "0"     },
    { OYK_DRV_KISS_SERIAL, "kiss-serial", "Hardware serial KISS TNC (COM / ser / tty)", "Modem",    "9600"  },
    { OYK_DRV_KISS_TCP,    "kiss-tcp",    "KISS TNC over TCP/IP (Direwolf / remote)",  "127.0.0.1", "8001"  },
    { OYK_DRV_TELNET,      "telnet",      "Plaintext Telnet terminal access port",     "0.0.0.0",   "8010"  }
};

static const GuiDriverSpec* find_vis_driver(oyk_u32 mask, int vis_idx) {
    if (mask == 0U) mask = OYK_DRV_ALL;
    int seen = 0;
    for (int i = 0; i < 5; ++i) {
        if ((mask & kDrivers[i].bit) != 0U) {
            if (seen == vis_idx) return &kDrivers[i];
            ++seen;
        }
    }
    return &kDrivers[0];
}

int oyk_gui_wiz_driver_count(oyk_u32 driver_mask) {
    if (driver_mask == 0U) driver_mask = OYK_DRV_ALL;
    int cnt = 0;
    for (int i = 0; i < 5; ++i) {
        if ((driver_mask & kDrivers[i].bit) != 0U) ++cnt;
    }
    return cnt;
}

const char* oyk_gui_wiz_driver_type(oyk_u32 driver_mask, int vis_idx) {
    return find_vis_driver(driver_mask, vis_idx)->type;
}

const char* oyk_gui_wiz_driver_desc(oyk_u32 driver_mask, int vis_idx) {
    return find_vis_driver(driver_mask, vis_idx)->desc;
}

const char* oyk_gui_wiz_driver_def_host(oyk_u32 driver_mask, int vis_idx) {
    return find_vis_driver(driver_mask, vis_idx)->def_host;
}

const char* oyk_gui_wiz_driver_def_port(oyk_u32 driver_mask, int vis_idx) {
    return find_vis_driver(driver_mask, vis_idx)->def_port;
}

void oyk_gui_wiz_init(OykGuiAddIfaceWizard* w, const OykConfig* cfg, oyk_u32 driver_mask) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->step = 0;
    w->driver_mask = driver_mask ? driver_mask : static_cast<oyk_u32>(OYK_DRV_ALL);
    oyk_gui_wiz_select_driver(w, cfg, 0);
}

void oyk_gui_wiz_select_driver(OykGuiAddIfaceWizard* w, const OykConfig* cfg, int vis_idx) {
    if (!w) return;
    const GuiDriverSpec* d = find_vis_driver(w->driver_mask, vis_idx);
    w->selected_driver_idx = vis_idx;
    memset(&w->draft, 0, sizeof(w->draft));
    gui_copy(w->draft.type, sizeof(w->draft.type), d->type);
    if (cfg) {
        gui_derive_default_iface_id(cfg, d->type, w->draft.id, sizeof(w->draft.id));
    } else {
        gui_copy(w->draft.id, sizeof(w->draft.id), d->type);
    }
    if (gui_strieq(d->type, "kiss-serial")) {
        gui_copy(w->draft.device, sizeof(w->draft.device), d->def_host);
        w->draft.baud = static_cast<oyk_u32>(strtoul(d->def_port, NULL, 10));
    } else {
        gui_copy(w->draft.host, sizeof(w->draft.host), d->def_host);
        w->draft.port = static_cast<oyk_u16>(atoi(d->def_port));
    }
    w->draft.block_inbound = 0;
    w->draft.block_outbound = gui_strieq(d->type, "telnet") ? 1 : 0;
    w->draft.allow_same_interface = 0;
    w->draft.no_auto_learn = gui_strieq(d->type, "telnet") ? 1 : 0;
    w->draft.learned_peer_policy = OYK_LEARN_INHERIT;
    w->static_peer_spec[0] = '\0';
    w->error[0] = '\0';
}

int oyk_gui_wiz_advance(OykGuiAddIfaceWizard* w, const OykConfig* cfg) {
    if (!w) return 0;
    w->error[0] = '\0';
    if (w->step == 0) {
        w->step = 1;
        return 1;
    }
    if (w->step == 1) {
        if (!w->draft.id[0]) {
            gui_copy(w->error, sizeof(w->error), "Interface ID cannot be empty.");
            return 0;
        }
        if (cfg) {
            for (int i = 0; i < cfg->iface_count; ++i) {
                if (gui_strieq(cfg->ifaces[i].id, w->draft.id)) {
                    gui_copy(w->error, sizeof(w->error), "An interface with that ID already exists.");
                    return 0;
                }
            }
        }
        w->step = 2;
        return 1;
    }
    if (w->step == 2) {
        if (!oyk_gui_validate_iface_draft(cfg, -1, &w->draft, w->error, sizeof(w->error))) {
            return 0;
        }
        w->step = 3;
        return 1;
    }
    return 1;
}

void oyk_gui_wiz_back(OykGuiAddIfaceWizard* w) {
    if (!w) return;
    w->error[0] = '\0';
    if (w->step > 0) --w->step;
}

const char* oyk_gui_wiz_step_prompt(const OykGuiAddIfaceWizard* w) {
    if (!w) return "";
    if (w->step == 0) return "Select the driver for the new interface:";
    if (w->step == 1) return "Enter a unique interface ID and optional description:";
    if (w->step == 2) {
        if (gui_strieq(w->draft.type, "axudp")) {
            return "Configure AXUDP bind host/DMR, port, netmask, and policy:";
        }
        if (gui_strieq(w->draft.type, "axip")) {
            return "Configure AXIP bind host/DMR, netmask, and policy:";
        }
        if (gui_strieq(w->draft.type, "kiss-serial")) {
            return "Configure KISS serial device, baud rate, and policy:";
        }
        if (gui_strieq(w->draft.type, "kiss-tcp")) {
            return "Configure KISS-TCP host, TCP port, and policy:";
        }
        return "Configure Telnet bind host, TCP port, and inbound policy:";
    }
    return "Review interface settings before committing:";
}

void oyk_gui_wiz_format_summary(const OykGuiAddIfaceWizard* w,
                                char* dst, size_t cap, int use_crlf) {
    if (!w || !dst || cap == 0) return;
    const char* nl = use_crlf ? "\r\n" : "\n";
    const OykIfaceConfig& d = w->draft;
    const char* desc = d.description[0] ? d.description : "(none)";
    const char* ain = d.block_inbound ? "false" : "true";
    const char* aout = d.block_outbound ? "false" : "true";
    const char* asame = d.allow_same_interface ? "true" : "false";
    const char* lp_s = gui_format_learned_policy(d.learned_peer_policy);

    char buf[512];
    if (gui_strieq(d.type, "kiss-serial")) {
        sprintf(buf,
                "Driver:        %s%sInterface ID:  %s%sDescription:   %s%s"
                "Serial Device: %s%sBaud Rate:     %lu%sAllow In/Out:  %s / %s%s"
                "Same-Iface:    %s%sLearned Pol:   %s",
                d.type, nl, d.id, nl, desc, nl, d.device, nl,
                static_cast<unsigned long>(d.baud), nl, ain, aout, nl, asame, nl, lp_s);
    } else if (gui_strieq(d.type, "axudp")) {
        sprintf(buf,
                "Driver:        %s%sInterface ID:  %s%sDescription:   %s%s"
                "Bind Host:     %s%sUDP Port:      %u%sNetmask:       %s%s"
                "Static Peer:   %s%sAllow In/Out:  %s / %s%sSame-Iface:    %s%s"
                "Learned Pol:   %s",
                d.type, nl, d.id, nl, desc, nl, d.host, nl,
                static_cast<unsigned int>(d.port), nl,
                d.netmask[0] ? d.netmask : "(any)", nl,
                w->static_peer_spec[0] ? w->static_peer_spec : "(auto-learn)", nl,
                ain, aout, nl, asame, nl, lp_s);
    } else if (gui_strieq(d.type, "axip")) {
        sprintf(buf,
                "Driver:        %s%sInterface ID:  %s%sDescription:   %s%s"
                "Bind Host:     %s%sNetmask:       %s%s"
                "Static Peer:   %s%sAllow In/Out:  %s / %s%sSame-Iface:    %s%s"
                "Learned Pol:   %s",
                d.type, nl, d.id, nl, desc, nl, d.host, nl,
                d.netmask[0] ? d.netmask : "(any)", nl,
                w->static_peer_spec[0] ? w->static_peer_spec : "(auto-learn)", nl,
                ain, aout, nl, asame, nl, lp_s);
    } else {
        int is_telnet = gui_strieq(d.type, "telnet");
        sprintf(buf,
                "Driver:        %s%sInterface ID:  %s%sDescription:   %s%s"
                "Host / IP:     %s%sTCP Port:      %u%sAllow In/Out:  %s / %s%s"
                "Same-Iface:    %s",
                d.type, nl, d.id, nl, desc, nl, d.host, nl,
                static_cast<unsigned int>(d.port), nl, ain,
                is_telnet ? "false (disabled)" : aout, nl,
                is_telnet ? "false (disabled)" : asame);
    }
    gui_copy(dst, cap, buf);
}

void oyk_gui_filemgr_pane_title(const OykGuiFilePane* p, const char* label,
                                char* out, size_t cap) {
    if (!out || cap == 0) return;
    const char* lbl = label ? label : "Folder";
    const char* rel = (p && p->rel[0]) ? p->rel : "";
    char buf[128];
    sprintf(buf, "%s: /%s", lbl, rel);
    gui_copy(out, cap, buf);
}

