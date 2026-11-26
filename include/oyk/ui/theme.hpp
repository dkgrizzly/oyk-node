// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oyk::ui {

enum class ThemeRole : std::size_t {
    Bg = 0,
    Fg,
    PanelBg,
    PanelFg,
    Border,
    HeaderBg,
    HeaderFg,
    Accent,
    StatusBg,
    StatusFg,
    SelectionBg,
    SelectionFg,
    InputBg,
    InputFg,
    ButtonBg,
    ButtonFg,
    Ok,
    Warn,
    Error,
    Count
};

inline constexpr std::size_t kThemeRoleCount = static_cast<std::size_t>(ThemeRole::Count);

inline constexpr std::array<const char*, kThemeRoleCount> kThemeRoleKeys = {
    "bg",           "fg",           "panel_bg",   "panel_fg",    "border",
    "header_bg",    "header_fg",    "accent",     "status_bg",   "status_fg",
    "selection_bg", "selection_fg", "input_bg",   "input_fg",    "button_bg",
    "button_fg",    "ok",           "warn",       "error"
};

inline constexpr std::array<const char*, kThemeRoleCount> kThemeRoleLabels = {
    "Background",    "Foreground",   "Panel Bg",     "Panel Fg",     "Border",
    "Header Bg",     "Header Fg",    "Accent",       "Status Bg",    "Status Fg",
    "Selection Bg",  "Selection Fg", "Input Bg",     "Input Fg",     "Button Bg",
    "Button Fg",     "OK Status",    "Warning",      "Error"
};

struct ThemeConfig {
    std::string name;
    bool is_dark_base = true;
    std::array<uint32_t, kThemeRoleCount> colors{};
};

inline std::string theme_to_lower(std::string_view s) {
    std::string out(s);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

inline bool is_builtin_theme_name(std::string_view name) {
    const std::string l = theme_to_lower(name);
    return l.empty() || l == "dark" || l == "light" || l == "native" || l == "auto";
}

inline bool parse_hex_rgb(std::string_view text, uint32_t& out) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) return false;
    uint32_t v = 0;
    for (char c : text) {
        int nib = -1;
        if (c >= '0' && c <= '9') nib = c - '0';
        else if (c >= 'a' && c <= 'f') nib = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') nib = 10 + (c - 'A');
        else return false;
        v = (v << 4) | static_cast<uint32_t>(nib);
    }
    out = v & 0x00FFFFFFu;
    return true;
}

inline std::string format_hex_rgb(uint32_t rgb) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned int>(rgb & 0x00FFFFFFu));
    return std::string(buf);
}

inline std::optional<std::size_t> theme_role_index(std::string_view key) {
    const std::string l = theme_to_lower(key);
    for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
        if (l == kThemeRoleKeys[i]) return i;
    }
    return std::nullopt;
}

inline ThemeConfig builtin_dark_theme() {
    ThemeConfig t;
    t.name = "dark";
    t.is_dark_base = true;
    t.colors = {
        0x10141Au, // bg
        0xE2E8F0u, // fg
        0x18202Au, // panel_bg
        0xE2E8F0u, // panel_fg
        0x38BDF8u, // border
        0x1E3A5Fu, // header_bg
        0xFFFFFFu, // header_fg
        0x38BDF8u, // accent
        0x18202Au, // status_bg
        0xFACC15u, // status_fg
        0x0EA5E9u, // selection_bg
        0xFFFFFFu, // selection_fg
        0x0F172Au, // input_bg
        0xF8FAFCu, // input_fg
        0x263445u, // button_bg
        0xF8FAFCu, // button_fg
        0x4ADE80u, // ok
        0xFACC15u, // warn
        0xF87171u  // error
    };
    return t;
}

inline ThemeConfig builtin_light_theme() {
    ThemeConfig t;
    t.name = "light";
    t.is_dark_base = false;
    t.colors = {
        0xF4F6F9u, // bg
        0x111827u, // fg
        0xFFFFFFu, // panel_bg
        0x111827u, // panel_fg
        0x1D4ED8u, // border
        0x1E40AFu, // header_bg
        0xFFFFFFu, // header_fg
        0x0284C7u, // accent
        0xE2E8F0u, // status_bg
        0x1E293Bu, // status_fg
        0x2563EBu, // selection_bg
        0xFFFFFFu, // selection_fg
        0xFFFFFFu, // input_bg
        0x0F172Au, // input_fg
        0xE5E7EBu, // button_bg
        0x111827u, // button_fg
        0x15803Du, // ok
        0xB45309u, // warn
        0xB91C1Cu  // error
    };
    return t;
}

inline std::string make_custom_clone_name(const std::vector<ThemeConfig>& existing,
                                          bool base_is_dark) {
    const std::string prefix = base_is_dark ? "custom-dark" : "custom-light";
    auto in_use = [&](const std::string& cand) {
        const std::string lc = theme_to_lower(cand);
        for (const auto& t : existing) {
            if (theme_to_lower(t.name) == lc) return true;
        }
        return false;
    };
    if (!in_use(prefix)) return prefix;
    for (int idx = 2; idx < 1000; ++idx) {
        std::string cand = prefix + "-" + std::to_string(idx);
        if (!in_use(cand)) return cand;
    }
    return prefix + "-custom";
}

inline std::string normalized_port_theme_name(std::string_view configured_name,
                                              bool default_to_native) {
    const std::string l = theme_to_lower(configured_name);
    if (l.empty()) return default_to_native ? "native" : "dark";
    if (l == "auto") return default_to_native ? "native" : "dark";
    return std::string(configured_name);
}

inline ThemeConfig resolve_effective_theme(std::string_view configured_name,
                                           const std::vector<ThemeConfig>& custom_themes,
                                           bool default_to_native,
                                           bool os_prefers_dark) {
    const std::string eff = normalized_port_theme_name(configured_name, default_to_native);
    const std::string le = theme_to_lower(eff);
    if (le == "native") {
        ThemeConfig base = os_prefers_dark ? builtin_dark_theme() : builtin_light_theme();
        base.name = "native";
        return base;
    }
    if (le == "light") return builtin_light_theme();
    if (le == "dark") return builtin_dark_theme();
    for (const auto& t : custom_themes) {
        if (theme_to_lower(t.name) == le) return t;
    }
    return os_prefers_dark ? builtin_dark_theme() : builtin_light_theme();
}

inline int rgb_to_ansi16(uint32_t rgb) {
    static constexpr std::array<uint32_t, 16> kAnsiRgb = {
        0x000000u, 0xAA0000u, 0x00AA00u, 0xAA5500u,
        0x0000AAu, 0xAA00AAu, 0x00AAAAu, 0xAAAAAAu,
        0x555555u, 0xFF5555u, 0x55FF55u, 0xFFFF55u,
        0x5555FFu, 0xFF55FFu, 0x55FFFFu, 0xFFFFFFu
    };
    const int r = static_cast<int>((rgb >> 16) & 0xFFu);
    const int g = static_cast<int>((rgb >> 8) & 0xFFu);
    const int b = static_cast<int>(rgb & 0xFFu);
    int best_idx = 7;
    long best_dist = 0x7FFFFFFFL;
    for (int i = 0; i < 16; ++i) {
        const uint32_t c = kAnsiRgb[static_cast<std::size_t>(i)];
        const long dr = r - static_cast<long>((c >> 16) & 0xFFu);
        const long dg = g - static_cast<long>((c >> 8) & 0xFFu);
        const long db = b - static_cast<long>(c & 0xFFu);
        const long d = dr * dr + dg * dg + db * db;
        if (d < best_dist) {
            best_dist = d;
            best_idx = i;
        }
    }
    return best_idx;
}

inline int rgb_to_xterm256(uint32_t rgb) {
    static constexpr std::array<int, 6> kCubeLevels = {0, 95, 135, 175, 215, 255};
    const long r = static_cast<long>((rgb >> 16) & 0xFFu);
    const long g = static_cast<long>((rgb >> 8) & 0xFFu);
    const long b = static_cast<long>(rgb & 0xFFu);

    auto nearest_cube = [](long v) -> int {
        int best = 0;
        long best_d = 0x7FFFFFFFL;
        for (int i = 0; i < 6; ++i) {
            const long d = (v - kCubeLevels[static_cast<std::size_t>(i)]) *
                           (v - kCubeLevels[static_cast<std::size_t>(i)]);
            if (d < best_d) { best_d = d; best = i; }
        }
        return best;
    };

    const int ci_r = nearest_cube(r);
    const int ci_g = nearest_cube(g);
    const int ci_b = nearest_cube(b);
    const long cr = kCubeLevels[static_cast<std::size_t>(ci_r)];
    const long cg = kCubeLevels[static_cast<std::size_t>(ci_g)];
    const long cb = kCubeLevels[static_cast<std::size_t>(ci_b)];
    long best_dist = (r - cr) * (r - cr) + (g - cg) * (g - cg) + (b - cb) * (b - cb);
    int best_idx = 16 + 36 * ci_r + 6 * ci_g + ci_b;

    const long gray_avg = (r + g + b) / 3;
    int gi = static_cast<int>((gray_avg - 8 + 5) / 10);
    gi = std::clamp(gi, 0, 23);
    const long gv = 8 + gi * 10;
    const long gd = (r - gv) * (r - gv) + (g - gv) * (g - gv) + (b - gv) * (b - gv);
    if (gd < best_dist) {
        best_idx = 232 + gi;
    }
    return best_idx;
}

} // namespace oyk::ui
