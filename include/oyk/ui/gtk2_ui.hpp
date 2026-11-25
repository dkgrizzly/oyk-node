// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/node/node_engine.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <string>

namespace oyk::ui {

struct WindowGeometry {
    bool has_size = false;
    int width = 0;
    int height = 0;
    bool has_pos = false;
    int x = 0;
    int y = 0;
    bool x_from_right = false;
    bool y_from_bottom = false;
};

// Parses standard X11 geometry strings: WxH, WxH+X+Y, WxH-X-Y, +X+Y, etc.
inline bool parse_x11_geometry(const std::string& spec, WindowGeometry& out) {
    out = WindowGeometry{};
    if (spec.empty()) return false;
    std::size_t pos = 0;
    const std::size_t n = spec.size();

    auto parse_uint = [&](int& val) -> bool {
        if (pos >= n || !std::isdigit(static_cast<unsigned char>(spec[pos]))) return false;
        long acc = 0;
        while (pos < n && std::isdigit(static_cast<unsigned char>(spec[pos]))) {
            acc = acc * 10 + (spec[pos] - '0');
            if (acc > 32767) return false;
            ++pos;
        }
        val = static_cast<int>(acc);
        return true;
    };

    if (pos < n && std::isdigit(static_cast<unsigned char>(spec[pos]))) {
        int w = 0;
        int h = 0;
        if (!parse_uint(w)) return false;
        if (pos >= n || (spec[pos] != 'x' && spec[pos] != 'X')) return false;
        ++pos;
        if (!parse_uint(h)) return false;
        if (w <= 0 || h <= 0) return false;
        out.has_size = true;
        out.width = w;
        out.height = h;
    }

    if (pos < n && (spec[pos] == '+' || spec[pos] == '-')) {
        bool xr = (spec[pos] == '-');
        ++pos;
        int xv = 0;
        if (!parse_uint(xv)) return false;
        if (pos >= n || (spec[pos] != '+' && spec[pos] != '-')) return false;
        bool yr = (spec[pos] == '-');
        ++pos;
        int yv = 0;
        if (!parse_uint(yv)) return false;
        out.has_pos = true;
        out.x = xv;
        out.y = yv;
        out.x_from_right = xr;
        out.y_from_bottom = yr;
    }

    return pos == n && (out.has_size || out.has_pos);
}

// Native X11 GUI using libgtk-2.0 matching the layout and controls of the ncurses UI.
class Gtk2UI {
public:
    Gtk2UI(node::NodeEngine& engine,
           std::atomic<bool>* external_stop_flag = nullptr,
           std::string initial_geometry = {});

    // Initializes GTK+ 2.0, builds the main window, and runs gtk_main() until clean exit.
    // Returns false if GTK cannot open an X11 display.
    bool run(int& argc, char**& argv);

private:
    struct Impl;
    node::NodeEngine& engine_;
    std::atomic<bool>* external_stop_flag_ = nullptr;
    std::string initial_geometry_;
};

} // namespace oyk::ui
