/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "gsos_core.h"

#include <stdio.h>
#include <string.h>

static int g_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A');
    return ch;
}

static int g_toupper(int ch) {
    if (ch >= 'a' && ch <= 'z') return ch - ('a' - 'A');
    return ch;
}

static int g_isalnum(int ch) {
    return ((ch >= '0' && ch <= '9') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z')) ? 1 : 0;
}

static void g_copy(char *dst, size_t cap, const char *src) {
    size_t n;
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= cap) n = cap - 1U;
    if (n > 0U) memcpy(dst, src, n);
    dst[n] = '\0';
}

static int g_strieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (g_tolower((unsigned char)*a) != g_tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static OykGsosMenuItem g_menu_tmp;

int oyk_gsos_node_menu_count(void) { return 13; }

const OykGsosMenuItem *oyk_gsos_node_menu_item(int idx) {
    memset(&g_menu_tmp, 0, sizeof(g_menu_tmp));
    switch (idx) {
        case 0:  g_menu_tmp.action = OYK_GSOS_ACT_ALLOW_INCOMING; g_menu_tmp.label = "Allow Incoming Connections"; g_menu_tmp.mac_label = g_menu_tmp.label; g_menu_tmp.shortcut_key = 'A'; return &g_menu_tmp;
        case 1:  g_menu_tmp.action = OYK_GSOS_ACT_DENY_INCOMING;  g_menu_tmp.label = "Deny Incoming Connections";  g_menu_tmp.mac_label = g_menu_tmp.label; g_menu_tmp.shortcut_key = 'D'; return &g_menu_tmp;
        case 2:  g_menu_tmp.action = OYK_GSOS_ACT_NONE;           g_menu_tmp.label = "-"; g_menu_tmp.mac_label = "-"; g_menu_tmp.is_separator = 1; return &g_menu_tmp;
        case 3:  g_menu_tmp.action = OYK_GSOS_ACT_LOCAL_TERM;     g_menu_tmp.label = "Local Terminal...";          g_menu_tmp.mac_label = g_menu_tmp.label; g_menu_tmp.shortcut_key = 'T'; return &g_menu_tmp;
        case 4:  g_menu_tmp.action = OYK_GSOS_ACT_FILES;          g_menu_tmp.label = "Manage Files Area...";       g_menu_tmp.mac_label = g_menu_tmp.label; return &g_menu_tmp;
        case 5:  g_menu_tmp.action = OYK_GSOS_ACT_RIG;            g_menu_tmp.label = "Radio Control...";           g_menu_tmp.mac_label = g_menu_tmp.label; return &g_menu_tmp;
        case 6:  g_menu_tmp.action = OYK_GSOS_ACT_SETTINGS;       g_menu_tmp.label = "Settings...";                g_menu_tmp.mac_label = g_menu_tmp.label; g_menu_tmp.shortcut_key = ','; return &g_menu_tmp;
        case 7:  g_menu_tmp.action = OYK_GSOS_ACT_PEERS;          g_menu_tmp.label = "Peers...";                   g_menu_tmp.mac_label = g_menu_tmp.label; return &g_menu_tmp;
        case 8:  g_menu_tmp.action = OYK_GSOS_ACT_THEME;          g_menu_tmp.label = "Theme...";                   g_menu_tmp.mac_label = g_menu_tmp.label; return &g_menu_tmp;
        case 9:  g_menu_tmp.action = OYK_GSOS_ACT_TERMINATE;      g_menu_tmp.label = "Terminate Connection...";    g_menu_tmp.mac_label = g_menu_tmp.label; return &g_menu_tmp;
        case 10: g_menu_tmp.action = OYK_GSOS_ACT_BAN;            g_menu_tmp.label = "Ban / Unban Callsign...";    g_menu_tmp.mac_label = g_menu_tmp.label; return &g_menu_tmp;
        case 11: g_menu_tmp.action = OYK_GSOS_ACT_NONE;           g_menu_tmp.label = "-"; g_menu_tmp.mac_label = "-"; g_menu_tmp.is_separator = 1; return &g_menu_tmp;
        case 12: g_menu_tmp.action = OYK_GSOS_ACT_EXIT;           g_menu_tmp.label = "Exit"; g_menu_tmp.mac_label = "Quit"; g_menu_tmp.shortcut_key = 'Q'; return &g_menu_tmp;
        default: return NULL;
    }
}

const char *oyk_gsos_screen_title(int screen_id) {
    switch (screen_id) {
        case OYK_GSOS_SCR_MAIN:       return "OYK Amateur Radio Packet Node";
        case OYK_GSOS_SCR_SETTINGS:   return "Node Settings";
        case OYK_GSOS_SCR_ADD_IFACE:  return "Add Interface";
        case OYK_GSOS_SCR_EDIT_IFACE: return "Edit Interface";
        case OYK_GSOS_SCR_PEERS:      return "Peers";
        case OYK_GSOS_SCR_EDIT_PEER:  return "Edit Peer";
        case OYK_GSOS_SCR_TERMINATE:  return "Terminate Active Connection";
        case OYK_GSOS_SCR_BAN:        return "Ban / Unban Callsign";
        case OYK_GSOS_SCR_EXIT_PEERS: return "Auto-Learned Peers";
        case OYK_GSOS_SCR_THEME:      return "Theme Settings";
        case OYK_GSOS_SCR_TERMINAL:   return "OYK Local Packet Terminal (YAPP)";
        case OYK_GSOS_SCR_FILES:      return "Manage BBS Files Area";
        case OYK_GSOS_SCR_RIG:        return "Radio Control";
        case OYK_GSOS_SCR_ABOUT:      return "About OYK Amateur Radio Packet Node";
        default:                      return "OYK Packet Node";
    }
}

int oyk_gsos_settings_tab_count(void) { return 7; }

const char *oyk_gsos_settings_tab_title(int tab_idx) {
    switch (tab_idx) {
        case 0: return "Node";
        case 1: return "BBS";
        case 2: return "CHAT";
        case 3: return "Interfaces";
        case 4: return "Peers";
        case 5: return "Rig";
        case 6: return "Printer";
        default: return "";
    }
}

int oyk_gsos_table_column_count(int table_id) {
    switch (table_id) {
        case OYK_GSOS_TBL_IFACES:         return 5;
        case OYK_GSOS_TBL_HEARD:          return 4;
        case OYK_GSOS_TBL_CONNECTIONS:    return 7;
        case OYK_GSOS_TBL_TERMINATE:      return 4;
        case OYK_GSOS_TBL_PEERS:          return 5;
        case OYK_GSOS_TBL_BANS:           return 5;
        case OYK_GSOS_TBL_FILES_INCOMING: return 3;
        case OYK_GSOS_TBL_FILES_APPROVED: return 2;
        case OYK_GSOS_TBL_THEME:          return 4;
        default:                          return 0;
    }
}

const char *oyk_gsos_table_column(int table_id, int col_idx) {
    if (table_id == OYK_GSOS_TBL_IFACES) {
        switch (col_idx) {
            case 0: return "Port"; case 1: return "Type"; case 2: return "Conn";
            case 3: return "Status"; case 4: return "RX/TX"; default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_HEARD) {
        switch (col_idx) {
            case 0: return "#"; case 1: return "Callsign"; case 2: return "Port";
            case 3: return "Heard"; default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_CONNECTIONS) {
        switch (col_idx) {
            case 0: return "ID"; case 1: return "Callsign"; case 2: return "Port";
            case 3: return "Dir"; case 4: return "State"; case 5: return "Since";
            case 6: return "Role"; default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_TERMINATE) {
        switch (col_idx) {
            case 0: return "ID"; case 1: return "Callsign"; case 2: return "Port";
            case 3: return "State"; default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_PEERS) {
        switch (col_idx) {
            case 0: return "ID"; case 1: return "Callsign"; case 2: return "Interface";
            case 3: return "Endpoint"; case 4: return "Policy"; default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_BANS) {
        switch (col_idx) {
            case 0: return "Callsign"; case 1: return "Banned At"; case 2: return "Lift At";
            case 3: return "Flags"; case 4: return "Reason"; default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_FILES_INCOMING) {
        switch (col_idx) {
            case 0: return "Uploader"; case 1: return "Filename"; case 2: return "Size";
            default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_FILES_APPROVED) {
        switch (col_idx) {
            case 0: return "Filename"; case 1: return "Size"; default: return "";
        }
    } else if (table_id == OYK_GSOS_TBL_THEME) {
        switch (col_idx) {
            case 0: return "Role"; case 1: return "Key"; case 2: return "Color";
            case 3: return "Preview"; default: return "";
        }
    }
    return "";
}

int oyk_gsos_parse_callsign(const char *text, OykGsosCallsign *out) {
    char tmp[24];
    char *dash;
    int ssid = 0;
    size_t blen, i;
    if (!text || !out) return 0;
    g_copy(tmp, sizeof(tmp), text);
    for (i = 0; tmp[i]; ++i) {
        tmp[i] = (char)g_toupper((unsigned char)tmp[i]);
    }
    dash = strchr(tmp, '-');
    if (dash) {
        const char *sp = dash + 1;
        *dash = '\0';
        if (!sp[0] || strlen(sp) > 2U) return 0;
        while (*sp) {
            if (*sp < '0' || *sp > '9') return 0;
            ssid = ssid * 10 + (*sp - '0');
            ++sp;
        }
        if (ssid < 0 || ssid > 15) return 0;
    }
    blen = strlen(tmp);
    if (blen == 0U || blen > 6U) return 0;
    for (i = 0; i < blen; ++i) {
        if (!g_isalnum((unsigned char)tmp[i])) return 0;
    }
    memset(out, 0, sizeof(*out));
    g_copy(out->base, sizeof(out->base), tmp);
    out->ssid = (gsos_u8)ssid;
    return 1;
}

void oyk_gsos_format_callsign(const OykGsosCallsign *cs, char *dst, size_t cap) {
    char buf[16];
    if (!dst || cap == 0U) return;
    if (!cs || !cs->base[0]) { dst[0] = '\0'; return; }
    if (cs->ssid > 0U) {
        sprintf(buf, "%s-%u", cs->base, (unsigned int)cs->ssid);
    } else {
        sprintf(buf, "%s", cs->base);
    }
    g_copy(dst, cap, buf);
}

void oyk_gsos_format_console_banner(const char *callsign, const char *alias,
                                    int accept_in, int conn_count, int iface_count,
                                    char *dst, size_t cap) {
    char buf[128];
    if (!dst || cap == 0U) return;
    sprintf(buf, "%s (%s) | %s | Conn:%d | Ifaces:%d",
            callsign ? callsign : "N0CALL",
            alias ? alias : "OYKNOD",
            accept_in ? "[INCOMING: ALLOW]" : "[INCOMING: DENY]",
            conn_count, iface_count);
    g_copy(dst, cap, buf);
}

void oyk_gsos_theme_preset(const char *name, OykGsosThemeConfig *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (name && g_strieq(name, "dark")) {
        g_copy(out->name, sizeof(out->name), "dark");
        out->is_dark_base = 1;
        out->colors[OYK_GSOS_TR_BG]           = 0x111520UL;
        out->colors[OYK_GSOS_TR_FG]           = 0xE6ECF5UL;
        out->colors[OYK_GSOS_TR_BORDER]       = 0x3D4C66UL;
        out->colors[OYK_GSOS_TR_HEADER_BG]    = 0x1E2942UL;
        out->colors[OYK_GSOS_TR_HEADER_FG]    = 0x99CCFFUL;
        out->colors[OYK_GSOS_TR_STATUS_BG]    = 0x192338UL;
        out->colors[OYK_GSOS_TR_STATUS_FG]    = 0xFFE680UL;
        out->colors[OYK_GSOS_TR_MENU_BG]      = 0x1B263BUL;
        out->colors[OYK_GSOS_TR_MENU_FG]      = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_SELECTION_BG] = 0x2F66CCUL;
        out->colors[OYK_GSOS_TR_SELECTION_FG] = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_PANEL_BG]     = 0x171E2EUL;
        out->colors[OYK_GSOS_TR_PANEL_FG]     = 0x99CCFFUL;
        out->colors[OYK_GSOS_TR_INPUT_BG]     = 0x0C1018UL;
        out->colors[OYK_GSOS_TR_INPUT_FG]     = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_ACCENT]       = 0x33AAFFUL;
        out->colors[OYK_GSOS_TR_OK]           = 0x33CC66UL;
        out->colors[OYK_GSOS_TR_WARN]         = 0xFFCC33UL;
        out->colors[OYK_GSOS_TR_ERROR]        = 0xFF5555UL;
    } else if (name && g_strieq(name, "green")) {
        g_copy(out->name, sizeof(out->name), "green");
        out->is_dark_base = 1;
        out->colors[OYK_GSOS_TR_BG]           = 0x001A08UL;
        out->colors[OYK_GSOS_TR_FG]           = 0x44FF66UL;
        out->colors[OYK_GSOS_TR_BORDER]       = 0x229933UL;
        out->colors[OYK_GSOS_TR_HEADER_BG]    = 0x002B11UL;
        out->colors[OYK_GSOS_TR_HEADER_FG]    = 0x88FF99UL;
        out->colors[OYK_GSOS_TR_STATUS_BG]    = 0x00220CUL;
        out->colors[OYK_GSOS_TR_STATUS_FG]    = 0xAAFFBBUL;
        out->colors[OYK_GSOS_TR_MENU_BG]      = 0x002B11UL;
        out->colors[OYK_GSOS_TR_MENU_FG]      = 0x44FF66UL;
        out->colors[OYK_GSOS_TR_SELECTION_BG] = 0x22AA44UL;
        out->colors[OYK_GSOS_TR_SELECTION_FG] = 0x001105UL;
        out->colors[OYK_GSOS_TR_PANEL_BG]     = 0x00200AUL;
        out->colors[OYK_GSOS_TR_PANEL_FG]     = 0x66FF88UL;
        out->colors[OYK_GSOS_TR_INPUT_BG]     = 0x001204UL;
        out->colors[OYK_GSOS_TR_INPUT_FG]     = 0x44FF66UL;
        out->colors[OYK_GSOS_TR_ACCENT]       = 0x44FFAAUL;
        out->colors[OYK_GSOS_TR_OK]           = 0x66FF66UL;
        out->colors[OYK_GSOS_TR_WARN]         = 0xCCFF44UL;
        out->colors[OYK_GSOS_TR_ERROR]        = 0xFF6644UL;
    } else if (name && g_strieq(name, "light")) {
        g_copy(out->name, sizeof(out->name), "light");
        out->is_dark_base = 0;
        out->colors[OYK_GSOS_TR_BG]           = 0xF5F5F7UL;
        out->colors[OYK_GSOS_TR_FG]           = 0x111111UL;
        out->colors[OYK_GSOS_TR_BORDER]       = 0x444455UL;
        out->colors[OYK_GSOS_TR_HEADER_BG]    = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_HEADER_FG]    = 0x000000UL;
        out->colors[OYK_GSOS_TR_STATUS_BG]    = 0xE2E5EBUL;
        out->colors[OYK_GSOS_TR_STATUS_FG]    = 0x112244UL;
        out->colors[OYK_GSOS_TR_MENU_BG]      = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_MENU_FG]      = 0x000000UL;
        out->colors[OYK_GSOS_TR_SELECTION_BG] = 0x0055CCUL;
        out->colors[OYK_GSOS_TR_SELECTION_FG] = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_PANEL_BG]     = 0xEAECEFUL;
        out->colors[OYK_GSOS_TR_PANEL_FG]     = 0x003388UL;
        out->colors[OYK_GSOS_TR_INPUT_BG]     = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_INPUT_FG]     = 0x000000UL;
        out->colors[OYK_GSOS_TR_ACCENT]       = 0x0055CCUL;
        out->colors[OYK_GSOS_TR_OK]           = 0x007722UL;
        out->colors[OYK_GSOS_TR_WARN]         = 0xAA6600UL;
        out->colors[OYK_GSOS_TR_ERROR]        = 0xCC0000UL;
    } else {
        /* Apple IIGS GS/OS Finder/Platinum native desktop palette */
        g_copy(out->name, sizeof(out->name), "native");
        out->is_dark_base = 0;
        out->colors[OYK_GSOS_TR_BG]           = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_FG]           = 0x000000UL;
        out->colors[OYK_GSOS_TR_BORDER]       = 0x000000UL;
        out->colors[OYK_GSOS_TR_HEADER_BG]    = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_HEADER_FG]    = 0x000000UL;
        out->colors[OYK_GSOS_TR_STATUS_BG]    = 0x3366FFUL;
        out->colors[OYK_GSOS_TR_STATUS_FG]    = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_MENU_BG]      = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_MENU_FG]      = 0x000000UL;
        out->colors[OYK_GSOS_TR_SELECTION_BG] = 0x3366FFUL;
        out->colors[OYK_GSOS_TR_SELECTION_FG] = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_PANEL_BG]     = 0xDDDDDDUL;
        out->colors[OYK_GSOS_TR_PANEL_FG]     = 0x000000UL;
        out->colors[OYK_GSOS_TR_INPUT_BG]     = 0xFFFFFFUL;
        out->colors[OYK_GSOS_TR_INPUT_FG]     = 0x000000UL;
        out->colors[OYK_GSOS_TR_ACCENT]       = 0x3366FFUL;
        out->colors[OYK_GSOS_TR_OK]           = 0x007722UL;
        out->colors[OYK_GSOS_TR_WARN]         = 0xAA6600UL;
        out->colors[OYK_GSOS_TR_ERROR]        = 0xCC0000UL;
    }
}

const char *oyk_gsos_theme_role_name(int role) {
    switch (role) {
        case OYK_GSOS_TR_BG:           return "Background";
        case OYK_GSOS_TR_FG:           return "Foreground";
        case OYK_GSOS_TR_BORDER:       return "Border";
        case OYK_GSOS_TR_HEADER_BG:    return "Header BG";
        case OYK_GSOS_TR_HEADER_FG:    return "Header FG";
        case OYK_GSOS_TR_STATUS_BG:    return "Status BG";
        case OYK_GSOS_TR_STATUS_FG:    return "Status FG";
        case OYK_GSOS_TR_MENU_BG:      return "Menu BG";
        case OYK_GSOS_TR_MENU_FG:      return "Menu FG";
        case OYK_GSOS_TR_SELECTION_BG: return "Select BG";
        case OYK_GSOS_TR_SELECTION_FG: return "Select FG";
        case OYK_GSOS_TR_PANEL_BG:     return "Panel BG";
        case OYK_GSOS_TR_PANEL_FG:     return "Panel FG";
        case OYK_GSOS_TR_INPUT_BG:     return "Input BG";
        case OYK_GSOS_TR_INPUT_FG:     return "Input FG";
        case OYK_GSOS_TR_ACCENT:       return "Accent";
        case OYK_GSOS_TR_OK:           return "OK";
        case OYK_GSOS_TR_WARN:         return "Warn";
        case OYK_GSOS_TR_ERROR:        return "Error";
        default:                       return "";
    }
}

const char *oyk_gsos_theme_role_key(int role) {
    switch (role) {
        case OYK_GSOS_TR_BG:           return "bg";
        case OYK_GSOS_TR_FG:           return "fg";
        case OYK_GSOS_TR_BORDER:       return "border";
        case OYK_GSOS_TR_HEADER_BG:    return "header_bg";
        case OYK_GSOS_TR_HEADER_FG:    return "header_fg";
        case OYK_GSOS_TR_STATUS_BG:    return "status_bg";
        case OYK_GSOS_TR_STATUS_FG:    return "status_fg";
        case OYK_GSOS_TR_MENU_BG:      return "menu_bg";
        case OYK_GSOS_TR_MENU_FG:      return "menu_fg";
        case OYK_GSOS_TR_SELECTION_BG: return "sel_bg";
        case OYK_GSOS_TR_SELECTION_FG: return "sel_fg";
        case OYK_GSOS_TR_PANEL_BG:     return "panel_bg";
        case OYK_GSOS_TR_PANEL_FG:     return "panel_fg";
        case OYK_GSOS_TR_INPUT_BG:     return "input_bg";
        case OYK_GSOS_TR_INPUT_FG:     return "input_fg";
        case OYK_GSOS_TR_ACCENT:       return "accent";
        case OYK_GSOS_TR_OK:           return "ok";
        case OYK_GSOS_TR_WARN:         return "warn";
        case OYK_GSOS_TR_ERROR:        return "error";
        default:                       return "";
    }
}

void oyk_gsos_config_init_default(OykGsosConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    oyk_gsos_parse_callsign("N0CALL", &cfg->callsign);
    oyk_gsos_parse_callsign("OYKNOD", &cfg->alias);
    cfg->accept_incoming = 1;
    cfg->line_ending = OYK_GSOS_EOL_CR;
    cfg->has_detected_eol = 0;
    cfg->bbs_enabled = 1;
    cfg->bbs_ssid = 11;
    cfg->chat_enabled = 1;
    cfg->chat_ssid = 12;
    cfg->rig_enabled = 1;
    g_copy(cfg->rig_protocol, sizeof(cfg->rig_protocol), "rigctl");
    cfg->rig_port = 4532U;
    cfg->printer_enabled = 0;
    g_copy(cfg->printer_device, sizeof(cfg->printer_device), ".B");
    g_copy(cfg->printer_protocol, sizeof(cfg->printer_protocol), "generic");
    cfg->printer_baud = 9600UL;
    cfg->theme_count = 3;
    oyk_gsos_theme_preset("native", &cfg->themes[0]);
    oyk_gsos_theme_preset("dark", &cfg->themes[1]);
    oyk_gsos_theme_preset("light", &cfg->themes[2]);
    g_copy(cfg->active_theme, sizeof(cfg->active_theme), "native");
}

OykGsosLineEnding oyk_gsos_detect_text_eol(const char *buf, size_t len) {
    size_t i;
    if (!buf || len == 0U) return OYK_GSOS_EOL_CR;
    for (i = 0; i < len; ++i) {
        if (buf[i] == '\r') {
            if (i + 1U < len && buf[i + 1U] == '\n') return OYK_GSOS_EOL_CRLF;
            return OYK_GSOS_EOL_CR;
        }
        if (buf[i] == '\n') return OYK_GSOS_EOL_LF;
    }
    return OYK_GSOS_EOL_CR;
}

int oyk_gsos_config_parse(const char *text, size_t len, OykGsosConfig *cfg) {
    char section[32];
    size_t pos = 0U;
    int cur_iface = -1;
    int cur_peer = -1;
    if (!text || !cfg) return 0;
    oyk_gsos_config_init_default(cfg);
    cfg->line_ending = oyk_gsos_detect_text_eol(text, len);
    cfg->has_detected_eol = 1;
    section[0] = '\0';

    while (pos < len) {
        char line[128];
        size_t lpos = 0U;
        char *eq, *k, *v;
        while (pos < len && text[pos] != '\r' && text[pos] != '\n') {
            if (lpos + 1U < sizeof(line)) line[lpos++] = text[pos];
            ++pos;
        }
        if (pos < len && text[pos] == '\r') ++pos;
        if (pos < len && text[pos] == '\n') ++pos;
        line[lpos] = '\0';

        k = line;
        while (*k == ' ' || *k == '\t') ++k;
        if (*k == '\0' || *k == '#' || *k == ';') continue;

        if (*k == '[') {
            char *rb = strchr(k, ']');
            if (rb) {
                *rb = '\0';
                g_copy(section, sizeof(section), k + 1);
                cur_iface = -1;
                cur_peer = -1;
                if (strncmp(section, "interface ", 10) == 0 &&
                    cfg->iface_count < OYK_GSOS_MAX_IFACES) {
                    cur_iface = cfg->iface_count++;
                    memset(&cfg->ifaces[cur_iface], 0, sizeof(OykGsosIfaceConfig));
                    g_copy(cfg->ifaces[cur_iface].id, sizeof(cfg->ifaces[cur_iface].id),
                           section + 10);
                    cfg->ifaces[cur_iface].allow_inbound = 1;
                    cfg->ifaces[cur_iface].allow_outbound = 1;
                } else if (strncmp(section, "peer ", 5) == 0 &&
                           cfg->peer_count < OYK_GSOS_MAX_PEERS) {
                    cur_peer = cfg->peer_count++;
                    memset(&cfg->peers[cur_peer], 0, sizeof(OykGsosPeerConfig));
                    g_copy(cfg->peers[cur_peer].id, sizeof(cfg->peers[cur_peer].id),
                           section + 5);
                    cfg->peers[cur_peer].allow_inbound = 1;
                    cfg->peers[cur_peer].allow_outbound = 1;
                }
            }
            continue;
        }

        eq = strchr(k, '=');
        if (!eq) continue;
        *eq = '\0';
        v = eq + 1;
        while (*v == ' ' || *v == '\t') ++v;
        {
            size_t kl = strlen(k);
            while (kl > 0U && (k[kl - 1U] == ' ' || k[kl - 1U] == '\t')) k[--kl] = '\0';
        }
        if (g_strieq(section, "node")) {
            if (g_strieq(k, "callsign")) oyk_gsos_parse_callsign(v, &cfg->callsign);
            else if (g_strieq(k, "alias")) oyk_gsos_parse_callsign(v, &cfg->alias);
            else if (g_strieq(k, "accept_incoming")) cfg->accept_incoming = g_strieq(v, "true") || v[0] == '1';
            else if (g_strieq(k, "theme")) g_copy(cfg->active_theme, sizeof(cfg->active_theme), v);
        } else if (cur_iface >= 0) {
            if (g_strieq(k, "type")) g_copy(cfg->ifaces[cur_iface].type, 16U, v);
            else if (g_strieq(k, "bind_host") || g_strieq(k, "host")) g_copy(cfg->ifaces[cur_iface].host, 32U, v);
            else if (g_strieq(k, "device")) g_copy(cfg->ifaces[cur_iface].device, 32U, v);
            else if (g_strieq(k, "bind_port") || g_strieq(k, "port")) {
                unsigned long pv = 0UL;
                sscanf(v, "%lu", &pv);
                cfg->ifaces[cur_iface].port = (gsos_u16)pv;
            }
        } else if (cur_peer >= 0) {
            if (g_strieq(k, "callsign")) g_copy(cfg->peers[cur_peer].callsign, 12U, v);
            else if (g_strieq(k, "interface")) g_copy(cfg->peers[cur_peer].interface_id, 16U, v);
            else if (g_strieq(k, "host")) g_copy(cfg->peers[cur_peer].host, 32U, v);
            else if (g_strieq(k, "description")) g_copy(cfg->peers[cur_peer].description, 32U, v);
            else if (g_strieq(k, "port")) {
                unsigned long pv = 0UL;
                sscanf(v, "%lu", &pv);
                cfg->peers[cur_peer].port = (gsos_u16)pv;
            }
        }
    }
    return 1;
}

size_t oyk_gsos_config_serialize(const OykGsosConfig *cfg, char *dst, size_t cap) {
    const char *eol;
    char cs[16], al[16], tmp[512];
    size_t len = 0U;
    int i;
    if (!cfg || !dst || cap == 0U) return 0U;
    eol = (cfg->line_ending == OYK_GSOS_EOL_CRLF) ? "\r\n"
        : (cfg->line_ending == OYK_GSOS_EOL_LF) ? "\n" : "\r";
    oyk_gsos_format_callsign(&cfg->callsign, cs, sizeof(cs));
    oyk_gsos_format_callsign(&cfg->alias, al, sizeof(al));
    sprintf(tmp, "[node]%scallsign = %s%salias = %s%saccept_incoming = %s%stheme = %s%s",
            eol, cs, eol, al, eol,
            cfg->accept_incoming ? "true" : "false", eol,
            cfg->active_theme[0] ? cfg->active_theme : "native", eol);
    g_copy(dst, cap, tmp);
    len = strlen(dst);
    for (i = 0; i < cfg->iface_count && len + 96U < cap; ++i) {
        sprintf(tmp, "%s[interface %s]%stype = %s%s",
                eol, cfg->ifaces[i].id, eol, cfg->ifaces[i].type, eol);
        g_copy(dst + len, cap - len, tmp);
        len = strlen(dst);
    }
    return len;
}

void oyk_gsos_node_init(OykGsosNode *node, const OykGsosConfig *cfg) {
    int i;
    if (!node) return;
    memset(node, 0, sizeof(*node));
    if (cfg) node->cfg = *cfg;
    else oyk_gsos_config_init_default(&node->cfg);
    for (i = 0; i < node->cfg.iface_count && i < OYK_GSOS_MAX_IFACES; ++i) {
        node->iface_rt[i].running = 1;
    }
    oyk_gsos_node_log(node, "OYK-Node GS/OS core initialized");
}

void oyk_gsos_node_seed_demo(OykGsosNode *node) {
    if (!node) return;
    oyk_gsos_parse_callsign("KR4OYK-7", &node->cfg.callsign);
    oyk_gsos_parse_callsign("OYKNOD", &node->cfg.alias);
    node->cfg.iface_count = 2;
    memset(&node->cfg.ifaces[0], 0, sizeof(OykGsosIfaceConfig));
    g_copy(node->cfg.ifaces[0].id, 16U, "axudp0");
    g_copy(node->cfg.ifaces[0].type, 16U, "axudp");
    g_copy(node->cfg.ifaces[0].host, 32U, "127.0.0.1");
    node->cfg.ifaces[0].port = 10093U;
    g_copy(node->cfg.ifaces[0].netmask, 20U, "255.0.0.0");
    node->iface_rt[0].running = 1;
    node->iface_rt[0].rx_frames = 14UL;
    node->iface_rt[0].tx_frames = 9UL;

    memset(&node->cfg.ifaces[1], 0, sizeof(OykGsosIfaceConfig));
    g_copy(node->cfg.ifaces[1].id, 16U, "kiss0");
    g_copy(node->cfg.ifaces[1].type, 16U, "kiss-serial");
    g_copy(node->cfg.ifaces[1].device, 32U, ".A");
    node->cfg.ifaces[1].baud = 9600UL;
    node->iface_rt[1].running = 1;
    node->iface_rt[1].rx_frames = 5UL;
    node->iface_rt[1].tx_frames = 3UL;

    node->cfg.peer_count = 2;
    memset(&node->cfg.peers[0], 0, sizeof(OykGsosPeerConfig));
    g_copy(node->cfg.peers[0].id, 16U, "remote0");
    g_copy(node->cfg.peers[0].callsign, 12U, "R3MOT-7");
    g_copy(node->cfg.peers[0].interface_id, 16U, "axudp0");
    g_copy(node->cfg.peers[0].host, 32U, "12.34.56.78");
    node->cfg.peers[0].port = 10093U;
    node->cfg.peers[0].auto_learned = 0;

    memset(&node->cfg.peers[1], 0, sizeof(OykGsosPeerConfig));
    g_copy(node->cfg.peers[1].id, 16U, "auto1");
    g_copy(node->cfg.peers[1].callsign, 12U, "W4ABC-7");
    g_copy(node->cfg.peers[1].interface_id, 16U, "kiss0");
    g_copy(node->cfg.peers[1].host, 32U, "145.050");
    node->cfg.peers[1].port = 0U;
    node->cfg.peers[1].auto_learned = 1;

    node->heard_count = 2;
    g_copy(node->heard[0].callsign, 12U, "W4ABC-7");
    g_copy(node->heard[0].iface_id, 16U, "kiss0");
    g_copy(node->heard[0].time_hms, 10U, "12:04:11");
    g_copy(node->heard[1].callsign, 12U, "R3MOT-7");
    g_copy(node->heard[1].iface_id, 16U, "axudp0");
    g_copy(node->heard[1].time_hms, 10U, "12:03:45");

    node->sessions[0].id = 1UL;
    node->sessions[0].state = 2;
    node->sessions[0].iface_idx = 0;
    g_copy(node->sessions[0].callsign, 12U, "R3MOT-7");
    g_copy(node->sessions[0].since_hms, 10U, "12:02:00");

    node->sessions[1].id = 2UL;
    node->sessions[1].state = 2;
    node->sessions[1].iface_idx = 1;
    g_copy(node->sessions[1].callsign, 12U, "W4ABC-7");
    g_copy(node->sessions[1].since_hms, 10U, "12:04:15");

    node->ban_count = 1;
    g_copy(node->bans[0].key, 12U, "N0BAD");
    g_copy(node->bans[0].banned_at, 16U, "20261126-120000");
    g_copy(node->bans[0].lift_at, 16U, "permanent");
    node->bans[0].flags = 0x07UL;
    g_copy(node->bans[0].reason, 48U, "Beacon flood");

    node->log_count = 0;
    oyk_gsos_node_log(node, "12:02:00 [axudp0] Connected R3MOT-7");
    oyk_gsos_node_log(node, "12:04:11 [kiss0] Heard W4ABC-7 via SCC .A");
    oyk_gsos_node_log(node, "12:04:15 [bbs] W4ABC-7 checked pending files");
}

void oyk_gsos_node_log(OykGsosNode *node, const char *line) {
    int i;
    if (!node || !line) return;
    if (node->log_count >= OYK_GSOS_MAX_LOG_LINES) {
        for (i = 1; i < OYK_GSOS_MAX_LOG_LINES; ++i) {
            g_copy(node->logs[i - 1], 80U, node->logs[i]);
        }
        node->log_count = OYK_GSOS_MAX_LOG_LINES - 1;
    }
    g_copy(node->logs[node->log_count++], 80U, line);
}

int oyk_gsos_node_active_conn_count(const OykGsosNode *node) {
    int i, c = 0;
    if (!node) return 0;
    for (i = 0; i < OYK_GSOS_MAX_SESSIONS; ++i) {
        if (node->sessions[i].state != 0) ++c;
    }
    return c;
}

int oyk_gsos_node_auto_peer_count(const OykGsosNode *node) {
    int i, c = 0;
    if (!node) return 0;
    for (i = 0; i < node->cfg.peer_count && i < OYK_GSOS_MAX_PEERS; ++i) {
        if (node->cfg.peers[i].auto_learned) ++c;
    }
    return c;
}

int oyk_gsos_node_terminate_session(OykGsosNode *node, gsos_u32 sess_id) {
    int i;
    if (!node) return 0;
    for (i = 0; i < OYK_GSOS_MAX_SESSIONS; ++i) {
        if (node->sessions[i].state != 0 && node->sessions[i].id == sess_id) {
            node->sessions[i].state = 0;
            return 1;
        }
    }
    return 0;
}

int oyk_gsos_node_ban_callsign(OykGsosNode *node, const char *call, const char *reason) {
    if (!node || !call || !call[0]) return 0;
    if (node->ban_count >= OYK_GSOS_MAX_BANS) return 0;
    g_copy(node->bans[node->ban_count].key, 12U, call);
    g_copy(node->bans[node->ban_count].banned_at, 16U, "20261126-120000");
    g_copy(node->bans[node->ban_count].lift_at, 16U, "permanent");
    node->bans[node->ban_count].flags = 0x07UL;
    g_copy(node->bans[node->ban_count].reason, 48U, reason ? reason : "Operator ban");
    ++node->ban_count;
    return 1;
}

void oyk_gsos_wiz_init(OykGsosAddIfaceWiz *wiz, const OykGsosConfig *cfg) {
    if (!wiz) return;
    memset(wiz, 0, sizeof(*wiz));
    oyk_gsos_wiz_select_driver(wiz, cfg, 0);
}

int oyk_gsos_wiz_driver_count(void) { return 5; }

const char *oyk_gsos_wiz_driver_name(int idx) {
    switch (idx) {
        case 0: return "axudp";
        case 1: return "axip";
        case 2: return "kiss-serial";
        case 3: return "kiss-tcp";
        case 4: return "telnet";
        default: return "";
    }
}

void oyk_gsos_wiz_select_driver(OykGsosAddIfaceWiz *wiz, const OykGsosConfig *cfg, int idx) {
    const char *drv;
    if (!wiz || idx < 0 || idx >= 5) return;
    wiz->selected_driver_idx = idx;
    drv = oyk_gsos_wiz_driver_name(idx);
    memset(&wiz->draft, 0, sizeof(wiz->draft));
    g_copy(wiz->draft.type, sizeof(wiz->draft.type), drv);
    sprintf(wiz->draft.id, "%s%d",
            (idx == 2) ? "kiss" : drv,
            cfg ? cfg->iface_count : 0);
    g_copy(wiz->draft.host, sizeof(wiz->draft.host), "0.0.0.0");
    g_copy(wiz->draft.netmask, sizeof(wiz->draft.netmask), "255.0.0.0");
    g_copy(wiz->draft.device, sizeof(wiz->draft.device), ".A");
    wiz->draft.port = (idx == 4) ? 2300U : 10093U;
    wiz->draft.baud = 9600UL;
    wiz->draft.allow_inbound = 1;
    wiz->draft.allow_outbound = 1;
}

void oyk_gsos_theme_init(OykGsosThemeCtrl *tc, OykGsosConfig *draft,
                         int default_native, int os_dark) {
    if (!tc) return;
    memset(tc, 0, sizeof(*tc));
    tc->draft = draft;
    tc->default_native = default_native;
    tc->os_dark = os_dark;
    oyk_gsos_theme_preset(default_native ? "native" : "dark", &tc->os_native_pal);
    g_copy(tc->active_name, sizeof(tc->active_name),
           (draft && draft->active_theme[0]) ? draft->active_theme
                                            : (default_native ? "native" : "dark"));
    g_copy(tc->saved_name, sizeof(tc->saved_name), tc->active_name);
}

void oyk_gsos_theme_set_os_native(OykGsosThemeCtrl *tc, const OykGsosThemeConfig *pal) {
    if (!tc || !pal) return;
    tc->os_native_pal = *pal;
    tc->has_os_native = 1;
    tc->os_dark = pal->is_dark_base;
}

void oyk_gsos_theme_set_preview_cb(OykGsosThemeCtrl *tc, OykGsosThemePreviewFn cb, void *ctx) {
    if (!tc) return;
    tc->preview_cb = cb;
    tc->preview_ctx = ctx;
}

void oyk_gsos_theme_resolve(const OykGsosThemeCtrl *tc, OykGsosThemeConfig *out) {
    int i;
    if (!out) return;
    if (!tc) {
        oyk_gsos_theme_preset("native", out);
        return;
    }
    if (g_strieq(tc->active_name, "native") && tc->has_os_native) {
        *out = tc->os_native_pal;
        return;
    }
    if (tc->draft) {
        for (i = 0; i < tc->draft->theme_count && i < OYK_GSOS_MAX_THEMES; ++i) {
            if (g_strieq(tc->draft->themes[i].name, tc->active_name)) {
                *out = tc->draft->themes[i];
                return;
            }
        }
    }
    oyk_gsos_theme_preset(tc->active_name, out);
}

static void g_emit_preview(OykGsosThemeCtrl *tc) {
    OykGsosThemeConfig pal;
    if (!tc) return;
    ++tc->preview_generation;
    tc->preview_active = 1;
    oyk_gsos_theme_resolve(tc, &pal);
    if (tc->preview_cb) tc->preview_cb(&pal, tc->active_name, tc->preview_ctx);
}

void oyk_gsos_theme_select(OykGsosThemeCtrl *tc, const char *name) {
    if (!tc || !name || !name[0]) return;
    g_copy(tc->active_name, sizeof(tc->active_name), name);
    tc->dirty = 1;
    g_emit_preview(tc);
}

int oyk_gsos_theme_set_role_rgb(OykGsosThemeCtrl *tc, int role, gsos_u32 rgb) {
    OykGsosThemeConfig custom;
    int i, found = -1;
    if (!tc || role < 0 || role >= OYK_GSOS_THEME_COLOR_COUNT) return 0;
    oyk_gsos_theme_resolve(tc, &custom);
    g_copy(custom.name, sizeof(custom.name), "custom");
    custom.colors[role] = rgb & 0xFFFFFFUL;
    if (tc->draft) {
        for (i = 0; i < tc->draft->theme_count && i < OYK_GSOS_MAX_THEMES; ++i) {
            if (g_strieq(tc->draft->themes[i].name, "custom")) { found = i; break; }
        }
        if (found < 0 && tc->draft->theme_count < OYK_GSOS_MAX_THEMES) {
            found = tc->draft->theme_count++;
        }
        if (found >= 0) tc->draft->themes[found] = custom;
        g_copy(tc->draft->active_theme, sizeof(tc->draft->active_theme), "custom");
    }
    g_copy(tc->active_name, sizeof(tc->active_name), "custom");
    tc->dirty = 1;
    g_emit_preview(tc);
    return 1;
}

void oyk_gsos_theme_revert(OykGsosThemeCtrl *tc) {
    if (!tc) return;
    g_copy(tc->active_name, sizeof(tc->active_name), tc->saved_name);
    if (tc->draft) g_copy(tc->draft->active_theme, sizeof(tc->draft->active_theme), tc->saved_name);
    tc->dirty = 0;
    g_emit_preview(tc);
}

int oyk_gsos_term_decode_ctrl_a(int ch) {
    switch (g_toupper((unsigned char)ch)) {
        case '?':
        case 'H': return OYK_GSOS_TCMD_HELP;
        case 'X':
        case 'Q': return OYK_GSOS_TCMD_CLOSE;
        case 'C':
        case 'L': return OYK_GSOS_TCMD_CLEAR;
        case 'W': return OYK_GSOS_TCMD_WRAP;
        case 'E': return OYK_GSOS_TCMD_EOL;
        case 'K': return OYK_GSOS_TCMD_ECHO;
        case 'S': return OYK_GSOS_TCMD_UPLOAD;
        case 'A':
        case 0x01: return OYK_GSOS_TCMD_LITERAL_CTRL_A;
        default:  return OYK_GSOS_TCMD_NONE;
    }
}

int oyk_gsos_term_wrap_line(const char *text, int len, int width,
                            OykGsosTermSpan *spans, int max_spans) {
    int pos = 0, count = 0;
    if (!text || width <= 0 || !spans || max_spans <= 0) return 0;
    if (len == 0) {
        spans[0].offset = 0;
        spans[0].len = 0;
        return 1;
    }
    while (pos < len && count < max_spans) {
        int take = len - pos;
        if (take > width) {
            int brk = width;
            while (brk > 0 && text[pos + brk] != ' ') --brk;
            take = (brk > 0) ? brk : width;
        }
        spans[count].offset = pos;
        spans[count].len = take;
        ++count;
        pos += take;
        while (pos < len && text[pos] == ' ') ++pos;
    }
    return count;
}

int oyk_gsos_term_eol_feed(OykGsosTermEolParser *p, const char *in_buf, int in_len,
                           char *out_buf, int out_cap) {
    int i, out_len = 0;
    if (!p || !in_buf || !out_buf || out_cap <= 0) return 0;
    for (i = 0; i < in_len && out_len < out_cap; ++i) {
        char ch = in_buf[i];
        if (p->seen_cr) {
            p->seen_cr = 0;
            if (ch == '\n') {
                p->detected = OYK_GSOS_EOL_CRLF;
                p->has_detected = 1;
                continue;
            }
            p->detected = OYK_GSOS_EOL_CR;
            p->has_detected = 1;
        }
        if (ch == '\r') {
            p->seen_cr = 1;
            out_buf[out_len++] = '\n';
        } else if (ch == '\n') {
            if (!p->has_detected) {
                p->detected = OYK_GSOS_EOL_LF;
                p->has_detected = 1;
            }
            out_buf[out_len++] = '\n';
        } else {
            out_buf[out_len++] = ch;
        }
    }
    return out_len;
}

int oyk_gsos_rigctl_build_set_freq(gsos_u32 hz, char *dst, size_t cap) {
    char tmp[32];
    int n;
    if (!dst || cap == 0U) return 0;
    n = sprintf(tmp, "F %lu\n", (unsigned long)hz);
    if (n <= 0 || (size_t)n >= cap) return 0;
    g_copy(dst, cap, tmp);
    return n;
}

int oyk_gsos_printer_format_line(const char *proto, const char *msg,
                                 unsigned char *dst, size_t cap) {
    char tmp[96];
    int n;
    if (!dst || cap == 0U || !msg) return 0;
    n = sprintf(tmp, "[%s] %s\r\n", proto ? proto : "generic", msg);
    if (n <= 0 || (size_t)n > cap) return 0;
    memcpy(dst, tmp, (size_t)n);
    return n;
}
