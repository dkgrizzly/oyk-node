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

static void gt_notify_preview(OykGuiThemeCtrl* c) {
    if (!c) return;
    ++c->preview_generation;
    if (c->preview_cb) {
        OykThemeConfig pal;
        oyk_gui_theme_resolve(c, &pal);
        c->preview_cb(&pal, c->active_name, c->preview_ctx);
    }
}

void oyk_gui_theme_init(OykGuiThemeCtrl* c, OykConfig* draft,
                        int default_native, int os_dark) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->draft = draft;
    c->default_native = default_native ? 1 : 0;
    c->os_dark = os_dark ? 1 : 0;
    if (draft && draft->theme_name[0] && !gt_strieq(draft->theme_name, "auto")) {
        gt_copy(c->active_name, sizeof(c->active_name), draft->theme_name);
    } else {
        gt_copy(c->active_name, sizeof(c->active_name), default_native ? "native" : "dark");
    }
    gt_copy(c->saved_name, sizeof(c->saved_name), c->active_name);
    c->preview_active = 1;
}

void oyk_gui_theme_set_os_native(OykGuiThemeCtrl* c, const OykThemeConfig* os_pal) {
    if (!c || !os_pal) return;
    c->os_native_pal = *os_pal;
    gt_copy(c->os_native_pal.name, sizeof(c->os_native_pal.name), "native");
    c->has_os_native = 1;
    if (gt_strieq(c->active_name, "native")) {
        gt_notify_preview(c);
    }
}

void oyk_gui_theme_set_preview_cb(OykGuiThemeCtrl* c, OykGuiThemePreviewFn cb, void* ctx) {
    if (!c) return;
    c->preview_cb = cb;
    c->preview_ctx = ctx;
}

int oyk_gui_theme_list_names(const OykGuiThemeCtrl* c, const char** out_names, int max_names) {
    if (!out_names || max_names <= 0) return 0;
    int count = 0;
    if (!c || c->default_native) {
        if (count < max_names) out_names[count++] = "native";
    }
    if (count < max_names) out_names[count++] = "dark";
    if (count < max_names) out_names[count++] = "light";
    if (c && c->draft) {
        for (int i = 0; i < c->draft->theme_count && i < OYK_MAX_THEMES && count < max_names; ++i) {
            if (!oyk_theme_is_builtin(c->draft->themes[i].name) && c->draft->themes[i].name[0]) {
                out_names[count++] = c->draft->themes[i].name;
            }
        }
    }
    return count;
}

void oyk_gui_theme_select(OykGuiThemeCtrl* c, const char* name) {
    if (!c || !name || !name[0]) return;
    gt_copy(c->active_name, sizeof(c->active_name), name);
    c->preview_active = 1;
    gt_notify_preview(c);
}

void oyk_gui_theme_cycle(OykGuiThemeCtrl* c, int delta) {
    if (!c) return;
    const char* names[OYK_MAX_THEMES + 4];
    int n = oyk_gui_theme_list_names(c, names, OYK_MAX_THEMES + 4);
    if (n <= 0) return;
    int cur = 0;
    for (int i = 0; i < n; ++i) {
        if (gt_strieq(names[i], c->active_name)) {
            cur = i;
            break;
        }
    }
    int step = (delta >= 0) ? 1 : -1;
    int next = (cur + step + n) % n;
    oyk_gui_theme_select(c, names[next]);
}

void oyk_gui_theme_revert(OykGuiThemeCtrl* c) {
    if (!c) return;
    const char* target = c->saved_name[0]
                             ? c->saved_name
                             : (c->default_native ? "native" : "dark");
    gt_copy(c->active_name, sizeof(c->active_name), target);
    c->preview_active = 0;
    gt_notify_preview(c);
}

void oyk_gui_theme_resolve(const OykGuiThemeCtrl* c, OykThemeConfig* out_pal) {
    if (!out_pal) return;
    if (!c) {
        oyk_theme_init_builtin(out_pal, 0);
        return;
    }
    const char* eff = c->active_name;
    if (!eff[0] || gt_strieq(eff, "auto")) {
        eff = c->default_native ? "native" : "dark";
    }
    if (gt_strieq(eff, "native") && c->has_os_native) {
        *out_pal = c->os_native_pal;
        gt_copy(out_pal->name, sizeof(out_pal->name), "native");
        return;
    }
    if (!c->draft) {
        int light = gt_strieq(eff, "light") ? 1 : (gt_strieq(eff, "native") ? !c->os_dark : 0);
        oyk_theme_init_builtin(out_pal, light);
        if (gt_strieq(eff, "native")) {
            gt_copy(out_pal->name, sizeof(out_pal->name), "native");
        }
        return;
    }
    OykConfig tmp = *c->draft;
    gt_copy(tmp.theme_name, sizeof(tmp.theme_name), eff);
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
        int nat_dark = c->has_os_native ? c->os_native_pal.is_dark_base : c->os_dark;
        int dark_b = gt_strieq(c->active_name, "light")
                         ? 0
                         : (gt_strieq(c->active_name, "native") ? nat_dark : 1);
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
            gt_notify_preview(c);
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
        gt_notify_preview(c);
    }
    return removed;
}