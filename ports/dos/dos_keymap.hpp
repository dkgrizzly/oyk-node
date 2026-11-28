/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_DOS_KEYMAP_HPP
#define OYK_DOS_KEYMAP_HPP

/* Shared strict key-dispatch helper for MS-DOS & MS-DOS Graphical consoles.
 * Enforces closed-menu single-letter suppression, Ctrl-N/F10 Node Menu,
 * Tab/F9 Local Terminal toggle, strict modal preemption, and Esc dismissal. */

enum OykDosKeyAction {
    OYK_DACT_NONE      = -1,
    OYK_DACT_ALLOW_IN  = 0,
    OYK_DACT_DENY_IN   = 1,
    OYK_DACT_TERM      = 2,
    OYK_DACT_FILES     = 3,
    OYK_DACT_RIG       = 4,
    OYK_DACT_SETTINGS  = 5,
    OYK_DACT_TERMINATE = 6,
    OYK_DACT_BAN       = 7,
    OYK_DACT_ABOUT     = 8,
    OYK_DACT_EXIT      = 9,
    OYK_DACT_OPEN_MENU = 10,
    OYK_DACT_CLOSE     = 11
};

inline int oyk_dos_map_main_key(int ch, int ext, int modal_open) {
    if (modal_open) {
        return (ch == 27) ? OYK_DACT_CLOSE : OYK_DACT_NONE;
    }
    if (ch == 0 || ch == 0xE0) {
        if (ext == 59) return OYK_DACT_ABOUT;     /* F1 */
        if (ext == 67) return OYK_DACT_TERM;      /* F9 */
        if (ext == 68) return OYK_DACT_OPEN_MENU; /* F10 */
        return OYK_DACT_NONE;
    }
    if (ch == 0x0E) return OYK_DACT_OPEN_MENU;    /* Ctrl-N */
    if (ch == '\t') return OYK_DACT_TERM;         /* Tab */
    /* Single-letter hotkeys are intentionally ignored when Node Menu is closed */
    return OYK_DACT_NONE;
}

inline int oyk_dos_map_menu_key(int ch, int ext) {
    if (ch == 0 || ch == 0xE0) {
        if (ext == 68) return OYK_DACT_CLOSE;     /* F10 toggles menu closed */
        return OYK_DACT_NONE;
    }
    if (ch == 27 || ch == 0x0E) return OYK_DACT_CLOSE;
    if (ch == 'a' || ch == 'A') return OYK_DACT_ALLOW_IN;
    if (ch == 'd' || ch == 'D') return OYK_DACT_DENY_IN;
    if (ch == 'l' || ch == 'L') return OYK_DACT_TERM;
    if (ch == 'f' || ch == 'F') return OYK_DACT_FILES;
    if (ch == 'r' || ch == 'R') return OYK_DACT_RIG;
    if (ch == 's' || ch == 'S') return OYK_DACT_SETTINGS;
    if (ch == 't' || ch == 'T') return OYK_DACT_TERMINATE;
    if (ch == 'b' || ch == 'B') return OYK_DACT_BAN;
    if (ch == '?')              return OYK_DACT_ABOUT;
    if (ch == 'x' || ch == 'X' || ch == 'q' || ch == 'Q') return OYK_DACT_EXIT;
    return OYK_DACT_NONE;
}

#endif /* OYK_DOS_KEYMAP_HPP */
