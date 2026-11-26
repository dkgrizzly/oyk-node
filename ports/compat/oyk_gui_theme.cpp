/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_gui.hpp"

#include <ctype.h>
#include <string.h>

static void gt_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1U;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int gt_strieq(const char* a, const char* b) {
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

void oyk_gui_theme_init(OykGuiThemeCtrl* c, OykConfig* draft,
                        int default_native, int os_dark) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->draft = draft;
    c->default_native = default_native ? 1 : 0;
    c->os_dark = os_dark ? 1 : 0;
    if (draft && draft->theme_name[0]) {
        gt_copy(c->active_name, sizeof(c->active_name), draft->theme_name);
    } else {
        gt_copy(c->active_name, sizeof(c->active_name), default_native ? "native" : "dark");
    }
    c->preview_active = 1;
}

void oyk_gui_theme_select(OykGuiThemeCtrl* c, const char* name) {
    if (!c || !name || !name[0]) return;
    gt_copy(c->active_name, sizeof(c->active_name), name);
    c->preview_active = 1;
}

void oyk_gui_theme_resolve(const OykGuiThemeCtrl* c, OykThemeConfig* out_pal) {
    if (!out_pal) return;
    if (!c || !c->draft) {
        oyk_theme_init_builtin(out_pal, c && !c->os_dark ? 1 : 0);
        return;
    }
    OykConfig tmp = *c->draft;
    gt_copy(tmp.theme_name, sizeof(tmp.theme_name), c->active_name);
    oyk_theme_resolve_effective(&tmp, c->default_native, c->os_dark, out_pal);
}

int oyk_gui_theme_set_role_rgb(OykGuiThemeCtrl* c, int role_idx, oyk_u32 rgb,
                               char* err, size_t err_cap) {
    if (!c || !c->draft || role_idx < 0 || role_idx >= OYK_THEME_COLOR_COUNT) {
        gt_copy(err, err_cap, "Invalid theme role index.");
        return 0;
    }
    if (oyk_theme_is_builtin(c->active_name)) {
        if (c->draft->theme_count >= OYK_MAX_THEMES) {
            gt_copy(err, err_cap, "Maximum custom theme count reached.");
            return 0;
        }
        OykThemeConfig pal;
        oyk_gui_theme_resolve(c, &pal);
        int dark_b = gt_strieq(c->active_name, "light")
                         ? 0
                         : (gt_strieq(c->active_name, "native") ? c->os_dark : 1);
        char cname[24];
        oyk_theme_make_clone_name(c->draft, dark_b, cname, sizeof(cname));
        OykThemeConfig* nt = &c->draft->themes[c->draft->theme_count++];
        *nt = pal;
        nt->is_dark_base = dark_b ? 1 : 0;
        gt_copy(nt->name, sizeof(nt->name), cname);
        gt_copy(c->active_name, sizeof(c->active_name), cname);
    }
    for (int i = 0; i < c->draft->theme_count; ++i) {
        if (gt_strieq(c->draft->themes[i].name, c->active_name)) {
            c->draft->themes[i].colors[role_idx] = (rgb & 0x00FFFFFFUL);
            c->dirty = 1;
            c->preview_active = 1;
            if (err && err_cap > 0) err[0] = '\0';
            return 1;
        }
    }
    gt_copy(err, err_cap, "Active custom theme not found.");
    return 0;
}

int oyk_gui_theme_set_role_hex(OykGuiThemeCtrl* c, int role_idx, const char* hex_str,
                               char* err, size_t err_cap) {
    oyk_u32 rgb = 0;
    if (!oyk_theme_parse_hex(hex_str, &rgb)) {
        gt_copy(err, err_cap, "Enter a valid #RRGGBB hex color.");
        return 0;
    }
    return oyk_gui_theme_set_role_rgb(c, role_idx, rgb, err, err_cap);
}

int oyk_gui_theme_delete_custom(OykGuiThemeCtrl* c) {
    if (!c || !c->draft || oyk_theme_is_builtin(c->active_name)) return 0;
    int removed = 0;
    for (int i = 0; i < c->draft->theme_count; ++i) {
        if (gt_strieq(c->draft->themes[i].name, c->active_name)) {
            for (int k = i; k + 1 < c->draft->theme_count; ++k) {
                c->draft->themes[k] = c->draft->themes[k + 1];
            }
            --c->draft->theme_count;
            removed = 1;
            break;
        }
    }
    if (removed) {
        gt_copy(c->active_name, sizeof(c->active_name),
                c->default_native ? "native" : "dark");
        c->dirty = 1;
        c->preview_active = 1;
    }
    return removed;
}
