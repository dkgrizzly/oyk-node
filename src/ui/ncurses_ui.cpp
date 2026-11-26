// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ui/ncurses_ui.hpp"
#include "oyk/ui/theme.hpp"
#include "oyk/protocol/yapp.hpp"

#include <ncurses.h>
#include <arpa/inet.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace oyk::ui {

namespace {

constexpr int CP_HEADER   = 1;
constexpr int CP_OK       = 2;
constexpr int CP_WARN     = 3;
constexpr int CP_DANGER   = 4;
constexpr int CP_MENU     = 5;
constexpr int CP_MENU_SEL = 6;
constexpr int CP_BORDER   = 7;
constexpr int CP_NC_PANEL = 8;
constexpr int CP_NC_DIR   = 9;
constexpr int CP_NC_SEL   = 10;
constexpr int CP_BASE     = 11;
constexpr int CP_PREVIEW  = 12;

struct CursesGuard {
    CursesGuard() {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);
        set_escdelay(25);
        timeout(200);
    }
    ~CursesGuard() {
        curs_set(1);
        endwin();
    }
};

std::string format_hms(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S");
    return oss.str();
}

std::string format_duration(uint64_t secs) {
    uint64_t h = secs / 3600;
    uint64_t m = (secs % 3600) / 60;
    uint64_t s = secs % 60;
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << h << ":"
        << std::setw(2) << std::setfill('0') << m << ":"
        << std::setw(2) << std::setfill('0') << s;
    return oss.str();
}

std::string clip(const std::string& s, int max_len) {
    if (max_len <= 0) return {};
    if (static_cast<int>(s.size()) <= max_len) return s;
    if (max_len <= 2) return s.substr(0, static_cast<std::size_t>(max_len));
    return s.substr(0, static_cast<std::size_t>(max_len - 1)) + "~";
}

void print_clipped(int y, int x, int max_w, const std::string& text) {
    if (max_w <= 0) return;
    mvaddnstr(y, x, clip(text, max_w).c_str(), max_w);
}

} // namespace

struct NcursesUI::TermState {
    std::mutex mtx;
    uint64_t sid = 0;
    bool connecting = false;
    bool connected = false;
    bool is_local = false;
    std::string port_id;
    ax25::Callsign remote_call{"UNKNOWN", 0};
    std::deque<std::string> lines;
    std::string cur_line;
    std::string input;
    protocol::YappSession yapp{"incoming"};
    std::string last_yapp_status;

    void append_line(const std::string& s) {
        lines.push_back(s);
        while (lines.size() > 400) lines.pop_front();
    }

    void append_bytes(const std::vector<uint8_t>& raw) {
        for (uint8_t b : raw) {
            if (b == '\r' || b == '\n') {
                if (!cur_line.empty() || b == '\r') {
                    append_line(cur_line);
                    cur_line.clear();
                }
            } else if (b >= 0x20 && b < 0x7F) {
                cur_line.push_back(static_cast<char>(b));
            }
        }
    }
};

NcursesUI::NcursesUI(node::NodeEngine& engine, std::atomic<bool>* external_stop_flag)
    : engine_(engine),
      external_stop_flag_(external_stop_flag),
      status_message_("Ready. Press F10/M for Node menu or Tab/F9 for Local Terminal.") {}

NcursesUI::~NcursesUI() {
    close_terminal_session();
}

bool NcursesUI::has_active_terminal() const {
    if (!term_state_) return false;
    std::lock_guard<std::mutex> lk(term_state_->mtx);
    return term_state_->connected || term_state_->connecting;
}

void NcursesUI::close_terminal_session() {
    if (!term_state_) return;
    uint64_t sid = 0;
    {
        std::lock_guard<std::mutex> lk(term_state_->mtx);
        sid = term_state_->sid;
        term_state_->sid = 0;
        term_state_->connected = false;
        term_state_->connecting = false;
        term_state_->is_local = false;
        term_state_->yapp.reset();
    }
    if (sid != 0) {
        engine_.close_local_terminal(sid);
    }
}

void NcursesUI::set_status(const std::string& msg) {
    status_message_ = msg;
}

void NcursesUI::run() {
    CursesGuard guard;

    if (has_colors()) {
        start_color();
        use_default_colors();
        has_colors_ = true;
        apply_theme_colors();
    }

    bool should_exit = false;
    while (!should_exit) {
        if (external_stop_flag_ && external_stop_flag_->load()) {
            break;
        }

        if (term_state_) {
            std::lock_guard<std::mutex> lk(term_state_->mtx);
            if (!term_state_->is_local && term_state_->yapp.awaiting_overwrite()) {
                status_message_ = "Background terminal: YAPP overwrite confirmation needed (press Tab)";
            }
        }

        node::NodeSnapshot snap = engine_.snapshot();
        draw_screen(snap);

        int ch = getch();
        if (ch == ERR || ch == KEY_RESIZE) {
            continue;
        }

        switch (ch) {
            case '\t':
            case KEY_F(9):
                modal_local_terminal();
                break;
            case KEY_F(10):
            case 'm':
            case 'M':
            case 'n':
            case 'N':
            case '\n':
            case '\r':
            case KEY_ENTER:
                open_node_menu(should_exit);
                break;
            case 'a':
            case 'A':
                engine_.set_accept_incoming(true);
                set_status("New incoming connections are now ALLOWED.");
                break;
            case 'd':
            case 'D':
                engine_.set_accept_incoming(false);
                set_status("New incoming connections are now DENIED.");
                break;
            case 't':
            case 'T':
                modal_terminate_connection();
                break;
            case 'b':
            case 'B':
                modal_ban_callsign();
                break;
            case 'l':
            case 'L':
                modal_local_terminal();
                break;
            case 'f':
            case 'F':
                modal_approve_incoming_files();
                break;
            case 'r':
            case 'R':
                modal_rig_control();
                break;
            case 'p':
            case 'P':
                modal_settings(4);
                break;
            case 's':
            case 'S':
                modal_settings(0);
                break;
            case 'h':
            case 'H':
                modal_theme();
                break;
            case 'q':
            case 'Q':
            case 'x':
            case 'X':
                if (modal_confirm_exit()) {
                    should_exit = true;
                }
                break;
            default:
                break;
        }
    }
    close_terminal_session();
}

void NcursesUI::draw_box(int y, int x, int h, int w, const std::string& title) {
    if (h < 2 || w < 4) return;
    if (has_colors_) attron(COLOR_PAIR(CP_BORDER));

    mvaddch(y, x, ACS_ULCORNER);
    mvhline(y, x + 1, ACS_HLINE, w - 2);
    mvaddch(y, x + w - 1, ACS_URCORNER);

    for (int row = y + 1; row < y + h - 1; ++row) {
        mvaddch(row, x, ACS_VLINE);
        mvaddch(row, x + w - 1, ACS_VLINE);
    }

    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

    if (!title.empty() && w > 6) {
        std::string label = " " + clip(title, w - 6) + " ";
        attron(A_BOLD);
        mvaddstr(y, x + 2, label.c_str());
        attroff(A_BOLD);
    }
    if (has_colors_) attroff(COLOR_PAIR(CP_BORDER));
}

void NcursesUI::draw_screen(const node::NodeSnapshot& snap) {
    erase();
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    if (rows < 16 || cols < 60) {
        mvprintw(0, 0, "Terminal too small (%dx%d). Minimum recommended: 80x24.", cols, rows);
        mvprintw(2, 0, "Node: %s | Conn: %zu | Incoming: %s",
                 snap.node_callsign.c_str(),
                 snap.total_active_connections,
                 snap.accept_incoming ? "ALLOWED" : "DENIED");
        mvprintw(4, 0, "Press F10 or M for Node menu.");
        refresh();
        return;
    }

    draw_menu_bar(cols);
    draw_header(snap, 1, cols);

    // Vertical layout:
    // Row 0: Top menu bar (Node)
    // Row 1: Header status bar
    // Rows 2..(1+top_h): Interfaces (left) + Last 5 New Callsigns Heard (right)
    // Middle: Active Connections & Banned Summary
    // Lower: Activity Log
    // Row rows-1: Status message
    const int avail_h = rows - 3; // exclude menu bar (1) + header (1) + status (1)
    const int top_h = std::max(8, std::min(10, avail_h / 3 + 1));
    const int mid_h = std::max(5, (avail_h - top_h) / 2);
    const int bot_h = std::max(4, avail_h - top_h - mid_h);

    const int right_w = std::max(34, std::min(44, cols * 42 / 100));
    const int left_w = cols - right_w;

    draw_interfaces_pane(snap, 2, 0, top_h, left_w);
    draw_new_callsigns_pane(snap, 2, left_w, top_h, right_w);
    draw_sessions_pane(snap, 2 + top_h, 0, mid_h, cols);
    draw_log_pane(snap, 2 + top_h + mid_h, 0, bot_h, cols);

    // Status message line at bottom (no bottom buttons)
    if (has_colors_) attron(COLOR_PAIR(CP_WARN));
    print_clipped(rows - 1, 1, cols - 2, status_message_);
    if (has_colors_) attroff(COLOR_PAIR(CP_WARN));

    refresh();
}

void NcursesUI::draw_menu_bar(int cols) {
    if (has_colors_) attron(COLOR_PAIR(CP_MENU));
    else attron(A_REVERSE);

    mvhline(0, 0, ' ', cols);

    if (has_colors_) attron(COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
    else attron(A_BOLD);
    mvaddnstr(0, 1, " Node ", std::max(0, cols - 1));
    if (has_colors_) attroff(COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
    else attroff(A_BOLD);

    if (has_colors_) attron(COLOR_PAIR(CP_MENU));
    const std::string hint = has_active_terminal()
        ? "Tab/F9: Terminal (ACTIVE) | F10/M: Menu "
        : "Tab/F9: Terminal | F10/M: Menu ";
    if (cols > static_cast<int>(hint.size()) + 10) {
        mvaddnstr(0, cols - static_cast<int>(hint.size()), hint.c_str(),
                  static_cast<int>(hint.size()));
    }

    if (has_colors_) attroff(COLOR_PAIR(CP_MENU));
    else attroff(A_REVERSE);
}

void NcursesUI::draw_header(const node::NodeSnapshot& snap, int y, int cols) {
    if (has_colors_) attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
    else attron(A_REVERSE | A_BOLD);

    mvhline(y, 0, ' ', cols);

    std::ostringstream left;
    left << " OYK PACKET NODE: " << snap.node_callsign;
    if (!snap.node_alias.empty()) {
        left << " (" << snap.node_alias << ")";
    }
    left << "  |  Up: " << format_duration(snap.uptime_seconds)
         << "  |  Active: " << snap.total_active_connections;
    print_clipped(y, 0, cols - 24, left.str());

    if (has_colors_) attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
    else attroff(A_REVERSE | A_BOLD);

    // Incoming badge on right side of header
    std::string badge = snap.accept_incoming ? " [INCOMING: ALLOW] " : " [INCOMING: DENY]  ";
    int bx = std::max(0, cols - static_cast<int>(badge.size()));
    if (has_colors_) {
        attron(COLOR_PAIR(snap.accept_incoming ? CP_OK : CP_DANGER) | A_BOLD | A_REVERSE);
    } else {
        attron(A_REVERSE | A_BOLD);
    }
    mvaddnstr(y, bx, badge.c_str(), cols - bx);
    if (has_colors_) {
        attroff(COLOR_PAIR(snap.accept_incoming ? CP_OK : CP_DANGER) | A_BOLD | A_REVERSE);
    } else {
        attroff(A_REVERSE | A_BOLD);
    }
}

void NcursesUI::draw_interfaces_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w) {
    draw_box(y, x, h, w, "Radio Interfaces (Active Connections)");
    int inner_w = w - 4;
    if (inner_w <= 0 || h < 4) return;

    attron(A_BOLD | A_UNDERLINE);
    std::ostringstream hdr;
    hdr << std::left
        << std::setw(9) << "Port"
        << std::setw(7) << "Type"
        << std::setw(6) << "Conn"
        << std::setw(18) << "Status"
        << "RX/TX";
    print_clipped(y + 1, x + 2, inner_w, hdr.str());
    attroff(A_BOLD | A_UNDERLINE);

    int max_rows = h - 3;
    if (snap.interfaces.empty()) {
        print_clipped(y + 2, x + 2, inner_w, "(no radio interfaces configured)");
        return;
    }

    for (int i = 0; i < max_rows && i < static_cast<int>(snap.interfaces.size()); ++i) {
        const auto& iface = snap.interfaces[static_cast<std::size_t>(i)];
        std::ostringstream line;
        line << std::left
             << std::setw(9) << clip(iface.id, 8)
             << std::setw(7) << clip(iface.type, 6)
             << std::setw(6) << iface.active_connections
             << std::setw(18) << clip(iface.status, 17)
             << iface.rx_frames << "/" << iface.tx_frames;
        if (has_colors_) {
            attron(COLOR_PAIR(iface.running ? CP_OK : CP_DANGER));
        }
        print_clipped(y + 2 + i, x + 2, inner_w, line.str());
        if (has_colors_) {
            attroff(COLOR_PAIR(iface.running ? CP_OK : CP_DANGER));
        }
    }
}

void NcursesUI::draw_new_callsigns_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w) {
    draw_box(y, x, h, w, "Last 5 New Callsigns Heard");
    int inner_w = w - 4;
    if (inner_w <= 0 || h < 4) return;

    attron(A_BOLD | A_UNDERLINE);
    std::ostringstream hdr;
    hdr << std::left
        << std::setw(4) << "#"
        << std::setw(11) << "Callsign"
        << std::setw(9) << "Port"
        << "Heard";
    print_clipped(y + 1, x + 2, inner_w, hdr.str());
    attroff(A_BOLD | A_UNDERLINE);

    int max_rows = std::min(5, h - 3);
    if (snap.last_new_callsigns.empty()) {
        print_clipped(y + 2, x + 2, inner_w, "(no callsigns heard yet)");
        return;
    }

    for (int i = 0; i < max_rows && i < static_cast<int>(snap.last_new_callsigns.size()); ++i) {
        const auto& rec = snap.last_new_callsigns[static_cast<std::size_t>(i)];
        std::ostringstream line;
        line << std::left
             << std::setw(4) << (std::to_string(i + 1) + ".")
             << std::setw(11) << clip(rec.callsign, 10)
             << std::setw(9) << clip(rec.interface_id, 8)
             << format_hms(rec.first_heard);
        print_clipped(y + 2 + i, x + 2, inner_w, line.str());
    }
}

void NcursesUI::draw_sessions_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w) {
    std::ostringstream title;
    title << "Active Connections (" << snap.active_sessions.size()
          << ")  |  Banned Callsigns: " << snap.banned_callsigns.size();
    draw_box(y, x, h, w, title.str());

    int inner_w = w - 4;
    if (inner_w <= 0 || h < 4) return;

    attron(A_BOLD | A_UNDERLINE);
    std::ostringstream hdr;
    hdr << std::left
        << std::setw(6) << "ID"
        << std::setw(12) << "Callsign"
        << std::setw(10) << "Port"
        << std::setw(10) << "Dir"
        << std::setw(13) << "State"
        << std::setw(10) << "Since"
        << "Bridge";
    print_clipped(y + 1, x + 2, inner_w, hdr.str());
    attroff(A_BOLD | A_UNDERLINE);

    int max_rows = h - 3;
    if (snap.active_sessions.empty()) {
        print_clipped(y + 2, x + 2, inner_w, "(no active connections)");
        return;
    }

    for (int i = 0; i < max_rows && i < static_cast<int>(snap.active_sessions.size()); ++i) {
        const auto& s = snap.active_sessions[static_cast<std::size_t>(i)];
        std::ostringstream line;
        std::string role = (s.mode == node::SessionMode::RestrictedBan)
                               ? "BAN NOTICE"
                               : (s.mode == node::SessionMode::LocalTerminal)
                                     ? "LOCAL TERM"
                                     : (s.mode == node::SessionMode::Chat)
                                           ? "CHAT"
                                           : (s.mode == node::SessionMode::Bbs)
                                                 ? "BBS"
                                                 : (s.bridged_session_id ? ("<-> #" + std::to_string(s.bridged_session_id))
                                                                         : "NODE SHELL");
        line << std::left
             << std::setw(6) << ("#" + std::to_string(s.id))
             << std::setw(12) << clip(s.remote_call, 11)
             << std::setw(10) << clip(s.interface_id, 9)
             << std::setw(10) << (s.direction == node::SessionDirection::Inbound ? "INBOUND" : "OUTBOUND")
             << std::setw(13) << node::Session::state_name(s.state)
             << std::setw(10) << format_hms(s.connected_at)
             << role;
        print_clipped(y + 2 + i, x + 2, inner_w, line.str());
    }
}

void NcursesUI::draw_log_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w) {
    draw_box(y, x, h, w, "Node Activity Log");
    int inner_w = w - 4;
    int max_rows = h - 2;
    if (inner_w <= 0 || max_rows <= 0) return;

    const int total = static_cast<int>(snap.recent_logs.size());
    const int start = std::max(0, total - max_rows);
    for (int i = start; i < total; ++i) {
        print_clipped(y + 1 + (i - start), x + 2, inner_w,
                      snap.recent_logs[static_cast<std::size_t>(i)]);
    }
}

void NcursesUI::open_node_menu(bool& should_exit) {
    static const char* ITEMS[11] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [L] Local Terminal...          ",
        " [F] Manage Files Area...       ",
        " [R] Radio Control...           ",
        " [S] Settings...                ",
        " [P] Peers...                   ",
        " [H] Theme...                   ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [X] Exit                       "
    };
    // Row offsets inside menu window (with separators after Deny and Ban)
    static const int ITEM_ROWS[11] = { 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 13 };

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    const int w = std::min(36, cols - 2);
    const int h = 15;
    if (rows < h + 2 || w < 24) return;

    WINDOW* win = newwin(h, w, 1, 1);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    int sel = 0;
    int chosen = -1;
    while (chosen < 0) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        werase(win);
        box(win, 0, 0);
        mvwhline(win, 3, 1, ACS_HLINE, w - 2);
        mvwhline(win, 12, 1, ACS_HLINE, w - 2);

        for (int i = 0; i < 11; ++i) {
            if (i == sel) wattron(win, A_REVERSE | A_BOLD);
            mvwaddnstr(win, ITEM_ROWS[i], 1, clip(ITEMS[i], w - 2).c_str(), w - 2);
            if (i == sel) wattroff(win, A_REVERSE | A_BOLD);
        }
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27 || ch == KEY_F(10)) break;
        if (ch == KEY_UP) {
            sel = (sel + 10) % 11;
        } else if (ch == KEY_DOWN) {
            sel = (sel + 1) % 11;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            chosen = sel;
        } else if (ch == 'a' || ch == 'A') {
            chosen = 0;
        } else if (ch == 'd' || ch == 'D') {
            chosen = 1;
        } else if (ch == 'l' || ch == 'L') {
            chosen = 2;
        } else if (ch == 'f' || ch == 'F') {
            chosen = 3;
        } else if (ch == 'r' || ch == 'R') {
            chosen = 4;
        } else if (ch == 's' || ch == 'S') {
            chosen = 5;
        } else if (ch == 'p' || ch == 'P') {
            chosen = 6;
        } else if (ch == 'h' || ch == 'H') {
            chosen = 7;
        } else if (ch == 't' || ch == 'T') {
            chosen = 8;
        } else if (ch == 'b' || ch == 'B') {
            chosen = 9;
        } else if (ch == 'x' || ch == 'X' || ch == 'q' || ch == 'Q') {
            chosen = 10;
        }
    }

    delwin(win);

    switch (chosen) {
        case 0:
            engine_.set_accept_incoming(true);
            set_status("New incoming connections are now ALLOWED.");
            break;
        case 1:
            engine_.set_accept_incoming(false);
            set_status("New incoming connections are now DENIED.");
            break;
        case 2:
            modal_local_terminal();
            break;
        case 3:
            modal_approve_incoming_files();
            break;
        case 4:
            modal_rig_control();
            break;
        case 5:
            modal_settings(0);
            break;
        case 6:
            modal_settings(4);
            break;
        case 7:
            modal_theme();
            break;
        case 8:
            modal_terminate_connection();
            break;
        case 9:
            modal_ban_callsign();
            break;
        case 10:
            if (modal_confirm_exit()) {
                should_exit = true;
            }
            break;
        default:
            break;
    }
}

void NcursesUI::apply_theme_colors() {
    if (!has_colors_) return;
    const node::NodeConfig cfg = engine_.config();
    const ThemeConfig t = resolve_effective_theme(cfg.theme_name, cfg.themes, false, true);
    auto pick = [&](ThemeRole r) -> int {
        const uint32_t rgb = t.colors[static_cast<std::size_t>(r)];
        if (COLORS >= 256) return rgb_to_xterm256(rgb);
        return rgb_to_ansi16(rgb) & 0x07;
    };
    const int c_bg = pick(ThemeRole::Bg);
    const int c_fg = pick(ThemeRole::Fg);
    init_pair(CP_BASE, static_cast<short>(c_fg), static_cast<short>(c_bg));
    init_pair(CP_HEADER, static_cast<short>(pick(ThemeRole::HeaderFg)), static_cast<short>(pick(ThemeRole::HeaderBg)));
    init_pair(CP_OK, static_cast<short>(pick(ThemeRole::Ok)), static_cast<short>(c_bg));
    init_pair(CP_WARN, static_cast<short>(pick(ThemeRole::Warn)), static_cast<short>(c_bg));
    init_pair(CP_DANGER, static_cast<short>(pick(ThemeRole::Error)), static_cast<short>(c_bg));
    init_pair(CP_MENU, static_cast<short>(pick(ThemeRole::ButtonFg)), static_cast<short>(pick(ThemeRole::ButtonBg)));
    init_pair(CP_MENU_SEL, static_cast<short>(pick(ThemeRole::SelectionFg)), static_cast<short>(pick(ThemeRole::SelectionBg)));
    init_pair(CP_BORDER, static_cast<short>(pick(ThemeRole::Border)), static_cast<short>(c_bg));
    init_pair(CP_NC_PANEL, static_cast<short>(pick(ThemeRole::PanelFg)), static_cast<short>(pick(ThemeRole::PanelBg)));
    init_pair(CP_NC_DIR, static_cast<short>(pick(ThemeRole::Accent)), static_cast<short>(pick(ThemeRole::PanelBg)));
    init_pair(CP_NC_SEL, static_cast<short>(pick(ThemeRole::SelectionFg)), static_cast<short>(pick(ThemeRole::SelectionBg)));
    bkgd(COLOR_PAIR(CP_BASE));
}

void NcursesUI::modal_theme() {
    node::NodeConfig draft = engine_.config();
    std::string active_name = normalized_port_theme_name(draft.theme_name, false);

    auto collect_names = [&]() -> std::vector<std::string> {
        std::vector<std::string> names{"dark", "light"};
        for (const auto& ct : draft.themes) {
            if (!is_builtin_theme_name(ct.name)) names.push_back(ct.name);
        }
        return names;
    };

    auto active_palette = [&]() -> ThemeConfig {
        return resolve_effective_theme(active_name, draft.themes, false, true);
    };

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    const int w = std::min(74, cols - 4);
    const int h = std::min(22, rows - 2);
    if (w < 54 || h < 14) return;

    WINDOW* win = newwin(h, w, std::max(1, (rows - h) / 2), std::max(1, (cols - w) / 2));
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    int role_sel = 0;
    int scroll = 0;
    std::string fb = "Left/Right:Switch theme  Enter:Edit #RRGGBB  S/F2:Save";

    auto prompt_hex = [&](const std::string& label, std::string& val_io) -> bool {
        echo();
        curs_set(1);
        mvwhline(win, h - 2, 2, ' ', w - 4);
        mvwaddnstr(win, h - 2, 2, label.c_str(), w - 16);
        wrefresh(win);
        char buf[32]{};
        wgetnstr(win, buf, 16);
        noecho();
        curs_set(0);
        if (!buf[0]) return false;
        val_io = buf;
        return true;
    };

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;
        const std::vector<std::string> names = collect_names();
        const ThemeConfig pal = active_palette();
        const int vis_rows = h - 9;
        if (role_sel < scroll) scroll = role_sel;
        if (role_sel >= scroll + vis_rows) scroll = role_sel - vis_rows + 1;

        werase(win);
        if (has_colors_) wattron(win, COLOR_PAIR(CP_BORDER));
        box(win, 0, 0);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_BORDER));
        wattron(win, A_BOLD);
        mvwprintw(win, 0, 2, " Theme Settings ");
        wattroff(win, A_BOLD);

        mvwprintw(win, 2, 2, "Theme  : < %-20.20s >  (%s)",
                  active_name.c_str(),
                  is_builtin_theme_name(active_name) ? "Built-in (not written)" : "Custom");
        mvwaddnstr(win, 3, 2, "Editing built-in dark/light clones custom-dark or custom-light.", w - 4);
        wattron(win, A_BOLD);
        mvwaddnstr(win, 5, 2, "Role              Key            Color     Preview", w - 4);
        wattroff(win, A_BOLD);

        for (int i = 0; i < vis_rows && (scroll + i) < static_cast<int>(kThemeRoleCount); ++i) {
            const int idx = scroll + i;
            const bool is_sel = (idx == role_sel);
            const std::string hx = format_hex_rgb(pal.colors[static_cast<std::size_t>(idx)]);
            if (is_sel) wattron(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            mvwprintw(win, 6 + i, 2, "%-16.16s  %-13.13s  %-8.8s",
                      kThemeRoleLabels[idx], kThemeRoleKeys[idx], hx.c_str());
            if (is_sel) wattroff(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            if (has_colors_) {
                const uint32_t rgb = pal.colors[static_cast<std::size_t>(idx)];
                int sc = (COLORS >= 256) ? rgb_to_xterm256(rgb) : (rgb_to_ansi16(rgb) & 0x07);
                init_pair(CP_PREVIEW, static_cast<short>(sc), static_cast<short>(sc));
                wattron(win, COLOR_PAIR(CP_PREVIEW));
                mvwaddstr(win, 6 + i, 45, "    ");
                wattroff(win, COLOR_PAIR(CP_PREVIEW));
            }
        }

        if (has_colors_) wattron(win, COLOR_PAIR(CP_BORDER));
        mvwhline(win, h - 4, 1, ACS_HLINE, w - 2);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_BORDER));
        mvwaddnstr(win, h - 3, 2, fb.c_str(), w - 4);
        mvwaddnstr(win, h - 2, 2, "Enter:Edit  Lt/Rt:Theme  X:Del Custom  S/F2:Save & Apply  Esc:Cancel", w - 4);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break;
        if (ch == KEY_UP) {
            role_sel = (role_sel + static_cast<int>(kThemeRoleCount) - 1) % static_cast<int>(kThemeRoleCount);
        } else if (ch == KEY_DOWN) {
            role_sel = (role_sel + 1) % static_cast<int>(kThemeRoleCount);
        } else if (ch == KEY_LEFT || ch == KEY_RIGHT) {
            int cur_i = 0;
            for (int i = 0; i < static_cast<int>(names.size()); ++i) {
                if (theme_to_lower(names[static_cast<std::size_t>(i)]) == theme_to_lower(active_name)) {
                    cur_i = i;
                    break;
                }
            }
            const int n = static_cast<int>(names.size());
            cur_i = (ch == KEY_RIGHT) ? ((cur_i + 1) % n) : ((cur_i + n - 1) % n);
            active_name = names[static_cast<std::size_t>(cur_i)];
            fb = "Selected theme '" + active_name + "'. Press S/F2 to apply.";
        } else if (ch == 'x' || ch == 'X') {
            if (is_builtin_theme_name(active_name)) {
                fb = "Built-in themes cannot be deleted.";
            } else {
                const std::string ln = theme_to_lower(active_name);
                draft.themes.erase(std::remove_if(draft.themes.begin(), draft.themes.end(),
                                                  [&](const ThemeConfig& tc) {
                                                      return theme_to_lower(tc.name) == ln;
                                                  }),
                                   draft.themes.end());
                active_name = "dark";
                fb = "Deleted custom theme; reverted to dark.";
            }
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            std::string hx = format_hex_rgb(pal.colors[static_cast<std::size_t>(role_sel)]);
            if (prompt_hex(std::string(kThemeRoleKeys[role_sel]) + " (#RRGGBB): ", hx)) {
                uint32_t new_rgb = 0;
                if (!parse_hex_rgb(hx, new_rgb)) {
                    fb = "Invalid hex color (use #RRGGBB).";
                } else {
                    if (is_builtin_theme_name(active_name)) {
                        ThemeConfig cloned = pal;
                        cloned.is_dark_base = (theme_to_lower(active_name) != "light");
                        cloned.name = make_custom_clone_name(draft.themes, cloned.is_dark_base);
                        draft.themes.push_back(cloned);
                        active_name = cloned.name;
                    }
                    for (auto& ct : draft.themes) {
                        if (theme_to_lower(ct.name) == theme_to_lower(active_name)) {
                            ct.colors[static_cast<std::size_t>(role_sel)] = new_rgb;
                            break;
                        }
                    }
                    fb = "Updated " + active_name + " (" + kThemeRoleKeys[role_sel] + ").";
                }
            }
        } else if (ch == 's' || ch == 'S' || ch == KEY_F(2)) {
            draft.theme_name = active_name;
            auto res = engine_.apply_config(draft, true);
            if (res.applied) {
                apply_theme_colors();
                set_status(res.saved_to_disk
                               ? ("Theme '" + active_name + "' saved & applied.")
                               : ("Theme '" + active_name + "' applied (in-memory only)."));
                break;
            }
            fb = "Apply failed: " + res.error;
        }
    }
    delwin(win);
}

bool NcursesUI::modal_file_selector(const std::string& title, std::string& selected_path_out) {
    struct Entry {
        std::string name;
        bool is_dir = false;
        uint64_t size = 0;
    };

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur_dir = fs::current_path(ec);
    if (ec || cur_dir.empty()) cur_dir = ".";

    auto load_entries = [](const fs::path& dir) -> std::vector<Entry> {
        std::vector<Entry> dirs, files;
        if (dir.has_parent_path() && dir.parent_path() != dir) {
            dirs.push_back(Entry{"..", true, 0});
        }
        std::error_code lec;
        for (const auto& de : fs::directory_iterator(dir, lec)) {
            std::error_code sec;
            bool is_d = de.is_directory(sec);
            Entry e;
            e.name = de.path().filename().string();
            e.is_dir = is_d;
            if (!is_d) {
                e.size = de.file_size(sec);
                if (sec) e.size = 0;
                files.push_back(std::move(e));
            } else {
                dirs.push_back(std::move(e));
            }
        }
        auto cmp = [](const Entry& a, const Entry& b) {
            if (a.name == "..") return true;
            if (b.name == "..") return false;
            return a.name < b.name;
        };
        std::sort(dirs.begin(), dirs.end(), cmp);
        std::sort(files.begin(), files.end(), cmp);
        dirs.insert(dirs.end(), files.begin(), files.end());
        return dirs;
    };

    auto fmt_size = [](uint64_t sz) -> std::string {
        std::ostringstream oss;
        if (sz >= 1024ULL * 1024ULL) {
            oss << std::fixed << std::setprecision(1) << (static_cast<double>(sz) / (1024.0 * 1024.0)) << "M";
        } else if (sz >= 1024ULL) {
            oss << std::fixed << std::setprecision(1) << (static_cast<double>(sz) / 1024.0) << "K";
        } else {
            oss << sz << "B";
        }
        return oss.str();
    };

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(76, std::max(44, cols - 4));
    int h = std::min(22, std::max(12, rows - 4));
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return false;
    keypad(win, TRUE);
    wtimeout(win, 200);
    if (has_colors_) wbkgd(win, COLOR_PAIR(CP_NC_PANEL));

    std::vector<Entry> entries = load_entries(cur_dir);
    int sel = 0;
    int top = 0;
    bool picked = false;

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;
        int list_h = std::max(1, h - 5);
        if (sel >= static_cast<int>(entries.size())) sel = std::max(0, static_cast<int>(entries.size()) - 1);
        if (sel < top) top = sel;
        if (sel >= top + list_h) top = sel - list_h + 1;

        werase(win);
        if (has_colors_) wattron(win, COLOR_PAIR(CP_NC_PANEL));
        box(win, 0, 0);
        mvwaddnstr(win, 0, 2, (" " + clip(title, w - 6) + " ").c_str(), w - 4);
        wattron(win, A_BOLD);
        mvwaddnstr(win, 1, 2, clip(cur_dir.string(), w - 4).c_str(), w - 4);
        wattroff(win, A_BOLD);
        mvwhline(win, 2, 1, ACS_HLINE, w - 2);

        for (int i = 0; i < list_h && (top + i) < static_cast<int>(entries.size()); ++i) {
            const Entry& e = entries[static_cast<std::size_t>(top + i)];
            int inner_w = w - 4;
            int size_w = 10;
            int name_w = std::max(8, inner_w - size_w - 1);
            std::string right = e.is_dir ? "[DIR]" : fmt_size(e.size);
            std::ostringstream row;
            row << std::left << std::setw(name_w) << clip(e.name, name_w)
                << " " << std::right << std::setw(size_w - 1) << right;
            bool is_sel = (top + i == sel);
            if (is_sel) {
                if (has_colors_) wattron(win, COLOR_PAIR(CP_NC_SEL) | A_BOLD);
                else wattron(win, A_REVERSE | A_BOLD);
            } else if (e.is_dir && has_colors_) {
                wattron(win, COLOR_PAIR(CP_NC_DIR) | A_BOLD);
            }
            mvwaddnstr(win, 3 + i, 2, clip(row.str(), inner_w).c_str(), inner_w);
            if (is_sel) {
                if (has_colors_) wattroff(win, COLOR_PAIR(CP_NC_SEL) | A_BOLD);
                else wattroff(win, A_REVERSE | A_BOLD);
            } else if (e.is_dir && has_colors_) {
                wattroff(win, COLOR_PAIR(CP_NC_DIR) | A_BOLD);
            }
        }

        mvwhline(win, h - 2, 1, ACS_HLINE, w - 2);
        mvwaddnstr(win, h - 1, 2, clip(" Up/Dn:Move  Enter:Open/Select  Bksp:Up  Esc:Cancel ", w - 4).c_str(), w - 4);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break;
        if (ch == KEY_UP && sel > 0) {
            --sel;
        } else if (ch == KEY_DOWN && sel + 1 < static_cast<int>(entries.size())) {
            ++sel;
        } else if (ch == KEY_PPAGE) {
            sel = std::max(0, sel - list_h);
        } else if (ch == KEY_NPAGE) {
            sel = std::min(std::max(0, static_cast<int>(entries.size()) - 1), sel + list_h);
        } else if (ch == KEY_HOME) {
            sel = 0;
        } else if (ch == KEY_END && !entries.empty()) {
            sel = static_cast<int>(entries.size()) - 1;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            if (cur_dir.has_parent_path() && cur_dir.parent_path() != cur_dir) {
                cur_dir = cur_dir.parent_path();
                entries = load_entries(cur_dir);
                sel = 0;
                top = 0;
            }
        } else if ((ch == '\n' || ch == '\r' || ch == KEY_ENTER) && !entries.empty()) {
            const Entry& e = entries[static_cast<std::size_t>(sel)];
            if (e.is_dir) {
                if (e.name == "..") {
                    cur_dir = cur_dir.parent_path();
                } else {
                    cur_dir = cur_dir / e.name;
                }
                entries = load_entries(cur_dir);
                sel = 0;
                top = 0;
            } else {
                selected_path_out = (cur_dir / e.name).string();
                picked = true;
                break;
            }
        }
    }

    delwin(win);
    return picked;
}

void NcursesUI::modal_local_terminal() {
    if (!term_state_) {
        term_state_ = std::make_shared<TermState>();
        auto ts_init = term_state_;
        ts_init->yapp.set_send_callback([this, ts_init](const std::vector<uint8_t>& pkt) {
            if (ts_init->sid != 0) {
                engine_.send_local_terminal_bytes(ts_init->sid, pkt);
            }
        });
        std::lock_guard<std::mutex> lk(ts_init->mtx);
        ts_init->append_line("*** Local Packet Terminal (YAPP / YAPP-C enabled for remote links)");
        ts_init->append_line("*** Downloads save to incoming/<CALLSIGN-SSID>/<file>");
        ts_init->append_line("*** F2:Connect  F3:YAPP Upload  F4:Disconnect  F6:Abort YAPP  F9/Esc:Console");
        ts_init->append_line("*** Commands: /connect <port> <call>, /send, /disc, /ports, /console");
    }
    auto ts = term_state_;

    auto start_connect = [&](const std::string& port, const std::string& target) {
        uint64_t old_sid = 0;
        {
            std::lock_guard<std::mutex> lk(ts->mtx);
            old_sid = ts->sid;
            ts->sid = 0;
            ts->connected = false;
            ts->connecting = false;
            ts->yapp.reset();
        }
        if (old_sid != 0) engine_.close_local_terminal(old_sid);

        node::NodeEngine::LocalTerminalCallbacks cbs;
        cbs.on_connected = [ts](uint64_t sid) {
            std::lock_guard<std::mutex> lk(ts->mtx);
            if (ts->sid != sid) return;
            ts->connecting = false;
            ts->connected = true;
            ts->append_line("*** Connected to " + ts->remote_call.str() + " via " + ts->port_id);
        };
        cbs.on_rx_bytes = [ts](uint64_t sid, const std::vector<uint8_t>& bytes) {
            std::lock_guard<std::mutex> lk(ts->mtx);
            if (ts->sid != sid) return;
            if (ts->is_local) {
                ts->append_bytes(bytes);
                return;
            }
            std::vector<uint8_t> plain;
            ts->yapp.feed_bytes(bytes.data(), bytes.size(), plain);
            if (!plain.empty()) {
                ts->append_bytes(plain);
            }
            auto yp = ts->yapp.progress();
            if (!yp.status_message.empty() && yp.status_message != ts->last_yapp_status) {
                ts->last_yapp_status = yp.status_message;
                ts->append_line("*** " + yp.status_message);
            }
        };
        cbs.on_closed = [ts](uint64_t sid, const std::string& reason) {
            std::lock_guard<std::mutex> lk(ts->mtx);
            if (ts->sid != sid) return;
            ts->sid = 0;
            ts->connecting = false;
            ts->connected = false;
            ts->is_local = false;
            ts->yapp.reset();
            ts->append_line("*** Disconnected (" + reason + ")");
        };

        uint64_t new_sid = 0;
        ax25::Callsign resolved;
        bool is_local = false;
        std::string err;
        if (engine_.open_local_terminal(port, target, std::move(cbs), new_sid, &resolved, err, &is_local)) {
            std::lock_guard<std::mutex> lk(ts->mtx);
            ts->sid = new_sid;
            ts->is_local = is_local;
            ts->connecting = !is_local;
            ts->connected = is_local;
            ts->port_id = is_local ? "local" : port;
            ts->remote_call = resolved;
            ts->yapp.set_remote_callsign(resolved);
            if (is_local) {
                ts->append_line("*** Connected locally to " + resolved.str() + " (YAPP disabled on local link)");
            } else {
                ts->append_line("*** Connecting to " + resolved.str() + " on " + port + "...");
            }
        } else {
            std::lock_guard<std::mutex> lk(ts->mtx);
            ts->append_line("*** Connect error: " + err);
        }
    };

    auto prompt_connect = [&]() {
        auto snap = engine_.snapshot();
        std::string def_port = snap.interfaces.empty() ? "axudp0" : snap.interfaces[0].id;
        for (const auto& iface : snap.interfaces) {
            if (iface.type != "telnet") {
                def_port = iface.id;
                break;
            }
        }

        int rows = 0, cols = 0;
        getmaxyx(stdscr, rows, cols);
        int w = std::min(62, cols - 4);
        int h = 9;
        WINDOW* cw = newwin(h, w, std::max(1, (rows - h) / 2), std::max(1, (cols - w) / 2));
        if (!cw) return;
        keypad(cw, TRUE);
        wtimeout(cw, 200);

        std::string port_in = def_port;
        std::string call_in;
        int focus = 1; // start in Callsign box
        while (true) {
            if (external_stop_flag_ && external_stop_flag_->load()) break;
            curs_set(1);
            werase(cw);
            box(cw, 0, 0);
            mvwprintw(cw, 0, 2, " Connect to Remote Packet Node ");
            mvwaddnstr(cw, 2, 2, "Port    : ", w - 4);
            if (focus == 0) wattron(cw, A_UNDERLINE | A_BOLD);
            mvwaddnstr(cw, 2, 12, clip(port_in, w - 14).c_str(), w - 14);
            if (focus == 0) wattroff(cw, A_UNDERLINE | A_BOLD);

            mvwaddnstr(cw, 4, 2, "Target  : ", w - 4);
            if (focus == 1) wattron(cw, A_UNDERLINE | A_BOLD);
            mvwaddnstr(cw, 4, 12, clip(call_in, w - 14).c_str(), w - 14);
            if (focus == 1) wattroff(cw, A_UNDERLINE | A_BOLD);

            mvwaddnstr(cw, 6, 2, clip("LOCAL, CHAT, CALL, or CALL@ip:port | Tab:Field Enter:Go Esc:Cancel", w - 4).c_str(), w - 4);
            wmove(cw, focus == 0 ? 2 : 4, 12 + static_cast<int>((focus == 0 ? port_in : call_in).size()));
            wrefresh(cw);

            int ch = wgetch(cw);
            if (ch == ERR) continue;
            if (ch == 27) break;
            if (ch == '\t') {
                focus = 1 - focus;
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (!port_in.empty()) {
                    curs_set(0);
                    delwin(cw);
                    start_connect(port_in, call_in);
                    return;
                }
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                std::string& fld = (focus == 0) ? port_in : call_in;
                if (!fld.empty()) fld.pop_back();
            } else if (ch >= 32 && ch < 127) {
                std::string& fld = (focus == 0) ? port_in : call_in;
                if (fld.size() < 48) fld.push_back(static_cast<char>(ch));
            }
        }
        curs_set(0);
        delwin(cw);
    };

    auto prompt_overwrite = [&](const std::string& path_str) -> bool {
        int rows = 0, cols = 0;
        getmaxyx(stdscr, rows, cols);
        int w = std::min(68, cols - 4);
        int h = 7;
        WINDOW* ow = newwin(h, w, std::max(1, (rows - h) / 2), std::max(1, (cols - w) / 2));
        if (!ow) return false;
        keypad(ow, TRUE);
        wtimeout(ow, 200);
        bool allow = false;
        while (true) {
            if (external_stop_flag_ && external_stop_flag_->load()) break;
            werase(ow);
            box(ow, 0, 0);
            mvwprintw(ow, 0, 2, " YAPP File Already Exists ");
            mvwaddnstr(ow, 2, 2, clip("File: " + path_str, w - 4).c_str(), w - 4);
            mvwaddnstr(ow, 4, 2, "Overwrite existing file?  [Y] Yes   [N] No", w - 4);
            wrefresh(ow);
            int ch = wgetch(ow);
            if (ch == ERR) continue;
            if (ch == 'y' || ch == 'Y') { allow = true; break; }
            if (ch == 'n' || ch == 'N' || ch == 27) { allow = false; break; }
        }
        delwin(ow);
        return allow;
    };

    std::string& input = ts->input;
    curs_set(1);

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        // Check if incoming YAPP transfer is waiting for overwrite confirmation
        std::string pending_overwrite_path;
        {
            std::lock_guard<std::mutex> lk(ts->mtx);
            if (!ts->is_local && ts->yapp.awaiting_overwrite()) {
                pending_overwrite_path = ts->yapp.progress().target_path;
            }
        }
        if (!pending_overwrite_path.empty()) {
            curs_set(0);
            bool allow = prompt_overwrite(pending_overwrite_path);
            curs_set(1);
            std::lock_guard<std::mutex> lk(ts->mtx);
            ts->yapp.resolve_overwrite(allow);
            auto yp = ts->yapp.progress();
            if (!yp.status_message.empty() && yp.status_message != ts->last_yapp_status) {
                ts->last_yapp_status = yp.status_message;
                ts->append_line("*** " + yp.status_message);
            }
        }

        int rows = 0, cols = 0;
        getmaxyx(stdscr, rows, cols);
        erase();

        // Top bar
        std::string header;
        protocol::YappProgress yp;
        std::vector<std::string> view_lines;
        std::string live_line;
        {
            std::lock_guard<std::mutex> lk(ts->mtx);
            yp = ts->yapp.progress();
            if (ts->connected) {
                header = " LOCAL TERMINAL | CONNECTED: " + ts->remote_call.str() + " via " + ts->port_id +
                         (ts->is_local ? " (LOCAL - YAPP disabled)" : "");
            } else if (ts->connecting) {
                header = " LOCAL TERMINAL | CONNECTING: " + ts->remote_call.str() + " via " + ts->port_id + "...";
            } else {
                header = " LOCAL TERMINAL | DISCONNECTED (F2:Connect | F9/Esc:Console)";
            }
            int body_h = std::max(1, rows - 4);
            int total = static_cast<int>(ts->lines.size());
            int start = std::max(0, total - body_h + (ts->cur_line.empty() ? 0 : 1));
            for (int i = start; i < total; ++i) {
                view_lines.push_back(ts->lines[static_cast<std::size_t>(i)]);
            }
            live_line = ts->cur_line;
        }

        if (has_colors_) attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        else attron(A_REVERSE | A_BOLD);
        mvhline(0, 0, ' ', cols);
        print_clipped(0, 0, cols, header);
        if (has_colors_) attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
        else attroff(A_REVERSE | A_BOLD);

        int body_h = std::max(1, rows - 4);
        int row = 1;
        for (const auto& l : view_lines) {
            if (row >= 1 + body_h) break;
            print_clipped(row++, 1, cols - 2, l);
        }
        if (!live_line.empty() && row < 1 + body_h) {
            print_clipped(row++, 1, cols - 2, live_line);
        }

        // YAPP / hotkey bar
        std::string bar = "F2:Connect  F3:YAPP Upload  F4:Disconnect  F6:Abort YAPP  F9/Esc:Console";
        if (yp.state != protocol::YappState::Idle && !yp.status_message.empty()) {
            bar = yp.status_message + " (" + std::to_string(yp.bytes_transferred) +
                  (yp.total_bytes > 0 ? ("/" + std::to_string(yp.total_bytes)) : "") + " B)";
        }
        if (has_colors_) attron(COLOR_PAIR(CP_MENU));
        else attron(A_REVERSE);
        mvhline(rows - 2, 0, ' ', cols);
        print_clipped(rows - 2, 1, cols - 2, bar);
        if (has_colors_) attroff(COLOR_PAIR(CP_MENU));
        else attroff(A_REVERSE);

        // Command input row
        mvaddstr(rows - 1, 0, "> ");
        print_clipped(rows - 1, 2, cols - 3, input);
        move(rows - 1, std::min(cols - 1, 2 + static_cast<int>(input.size())));
        refresh();

        int ch = getch();
        if (ch == ERR || ch == KEY_RESIZE) continue;
        if (ch == 27 || ch == KEY_F(9)) break;

        if (ch == KEY_F(2)) {
            prompt_connect();
        } else if (ch == KEY_F(3)) {
            bool is_conn = false;
            bool is_loc = false;
            {
                std::lock_guard<std::mutex> lk(ts->mtx);
                is_conn = ts->connected;
                is_loc = ts->is_local;
            }
            if (!is_conn) {
                std::lock_guard<std::mutex> lk(ts->mtx);
                ts->append_line("*** Connect to a remote node first before YAPP upload (F2).");
            } else if (is_loc) {
                std::lock_guard<std::mutex> lk(ts->mtx);
                ts->append_line("*** YAPP file transfer is disallowed when both sides are local.");
            } else {
                curs_set(0);
                std::string chosen_file;
                bool ok = modal_file_selector("YAPP Upload - Select File", chosen_file);
                curs_set(1);
                if (ok) {
                    std::lock_guard<std::mutex> lk(ts->mtx);
                    std::string err;
                    if (!ts->yapp.start_send(chosen_file, err)) {
                        ts->append_line("*** YAPP upload error: " + err);
                    } else {
                        ts->last_yapp_status = ts->yapp.progress().status_message;
                        ts->append_line("*** " + ts->last_yapp_status);
                    }
                }
            }
        } else if (ch == KEY_F(4)) {
            uint64_t sid = 0;
            {
                std::lock_guard<std::mutex> lk(ts->mtx);
                sid = ts->sid;
                ts->yapp.cancel("Disconnected");
            }
            if (sid != 0) engine_.close_local_terminal(sid);
        } else if (ch == KEY_F(6)) {
            std::lock_guard<std::mutex> lk(ts->mtx);
            ts->yapp.cancel("Cancelled by user");
            ts->append_line("*** YAPP transfer aborted.");
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            std::string line = input;
            input.clear();

            if (!line.empty() && line[0] == '/') {
                std::istringstream iss(line);
                std::string cmd;
                iss >> cmd;
                std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (cmd == "/connect" || cmd == "/c") {
                    std::string p, t;
                    iss >> p >> t;
                    if (p.empty() || t.empty()) {
                        std::lock_guard<std::mutex> lk(ts->mtx);
                        ts->append_line("*** Usage: /connect <port> <callsign[@ip:port]>");
                    } else {
                        start_connect(p, t);
                    }
                } else if (cmd == "/disc" || cmd == "/d") {
                    uint64_t sid = 0;
                    { std::lock_guard<std::mutex> lk(ts->mtx); sid = ts->sid; }
                    if (sid != 0) engine_.close_local_terminal(sid);
                } else if (cmd == "/send") {
                    bool is_conn = false;
                    bool is_loc = false;
                    { std::lock_guard<std::mutex> lk(ts->mtx); is_conn = ts->connected; is_loc = ts->is_local; }
                    if (!is_conn) {
                        std::lock_guard<std::mutex> lk(ts->mtx);
                        ts->append_line("*** Not connected.");
                    } else if (is_loc) {
                        std::lock_guard<std::mutex> lk(ts->mtx);
                        ts->append_line("*** YAPP file transfer is disallowed when both sides are local.");
                    } else {
                        curs_set(0);
                        std::string chosen_file;
                        bool ok = modal_file_selector("YAPP Upload - Select File", chosen_file);
                        curs_set(1);
                        if (ok) {
                            std::lock_guard<std::mutex> lk(ts->mtx);
                            std::string err;
                            if (!ts->yapp.start_send(chosen_file, err)) {
                                ts->append_line("*** YAPP upload error: " + err);
                            } else {
                                ts->append_line("*** " + ts->yapp.progress().status_message);
                            }
                        }
                    }
                } else if (cmd == "/ports") {
                    auto snap = engine_.snapshot();
                    std::lock_guard<std::mutex> lk(ts->mtx);
                    ts->append_line("*** Configured Interfaces:");
                    for (const auto& iface : snap.interfaces) {
                        ts->append_line("  " + iface.id + " [" + iface.type + "] " + iface.status);
                    }
                } else if (cmd == "/abort") {
                    std::lock_guard<std::mutex> lk(ts->mtx);
                    ts->yapp.cancel("Cancelled by user");
                    ts->append_line("*** YAPP transfer aborted.");
                } else if (cmd == "/console" || cmd == "/bg" || cmd == "/quit" || cmd == "/exit") {
                    break;
                } else {
                    std::lock_guard<std::mutex> lk(ts->mtx);
                    ts->append_line("*** Commands: /connect <port> <call>, /send, /disc, /ports, /abort, /console");
                }
            } else {
                uint64_t sid = 0;
                bool is_conn = false;
                {
                    std::lock_guard<std::mutex> lk(ts->mtx);
                    sid = ts->sid;
                    is_conn = ts->connected;
                    ts->append_line("> " + line);
                }
                if (is_conn && sid != 0) {
                    std::string wire = line + "\r";
                    std::vector<uint8_t> bytes(wire.begin(), wire.end());
                    engine_.send_local_terminal_bytes(sid, bytes);
                } else {
                    std::lock_guard<std::mutex> lk(ts->mtx);
                    ts->append_line("*** Not connected. Press F2 or type /connect <port> <callsign>");
                }
            }
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            if (!input.empty()) input.pop_back();
        } else if (ch >= 32 && ch < 127 && input.size() < 220) {
            input.push_back(static_cast<char>(ch));
        }
    }

    curs_set(0);
    set_status(has_active_terminal()
                   ? "Returned to Operator Console (Local Terminal session still active; press Tab/F9 to return)."
                   : "Returned to Operator Console.");
}

void NcursesUI::modal_terminate_connection() {
    auto snap = engine_.snapshot();
    if (snap.active_sessions.empty()) {
        set_status("No active connections to terminate.");
        return;
    }

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(64, cols - 4);
    int h = std::min(16, rows - 4);
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    int sel = 0;
    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        snap = engine_.snapshot();
        if (snap.active_sessions.empty()) {
            set_status("All active connections have closed.");
            break;
        }
        if (sel >= static_cast<int>(snap.active_sessions.size())) {
            sel = static_cast<int>(snap.active_sessions.size()) - 1;
        }

        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Terminate Active Connection ");

        int list_h = h - 5;
        int offset = (sel >= list_h) ? (sel - list_h + 1) : 0;
        for (int i = 0; i < list_h && (offset + i) < static_cast<int>(snap.active_sessions.size()); ++i) {
            const auto& s = snap.active_sessions[static_cast<std::size_t>(offset + i)];
            std::ostringstream line;
            line << " #" << s.id << "  " << std::left << std::setw(10) << s.remote_call
                 << " on " << std::setw(8) << s.interface_id
                 << " [" << node::Session::state_name(s.state) << "] ";
            if (offset + i == sel) wattron(win, A_REVERSE | A_BOLD);
            mvwaddnstr(win, 2 + i, 2, clip(line.str(), w - 4).c_str(), w - 4);
            if (offset + i == sel) wattroff(win, A_REVERSE | A_BOLD);
        }

        mvwaddnstr(win, h - 2, 2, "Up/Down: select | Enter: terminate | Esc: cancel", w - 4);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break;
        if (ch == KEY_UP && sel > 0) {
            --sel;
        } else if (ch == KEY_DOWN && sel + 1 < static_cast<int>(snap.active_sessions.size())) {
            ++sel;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            uint64_t sid = snap.active_sessions[static_cast<std::size_t>(sel)].id;
            std::string msg;
            engine_.terminate_session(sid, &msg);
            set_status(msg);
            break;
        }
    }

    delwin(win);
}

void NcursesUI::modal_ban_callsign() {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(74, cols - 4);
    int h = std::min(21, rows - 4);
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    std::string call_input;
    std::string lift_input = "permanent";
    std::string reason_input = "Banned by sysop.";
    bool hard_ban = false;
    bool appeal_allowed = true;
    std::string feedback;
    int unban_sel = 0;
    int focus = 0; // 0=callsign, 1=lift, 2=reason, 3=flags, 4=unban list

    auto submit_ban = [&]() {
        if (call_input.empty()) {
            feedback = "Enter a callsign first.";
            return;
        }
        uint32_t flags = (appeal_allowed ? node::BAN_FLAG_APPEAL_ALLOWED : 0) |
                         (hard_ban ? node::BAN_FLAG_HARD_BAN : 0);
        std::string msg;
        if (engine_.ban_callsign_with_lift_spec(call_input, lift_input, &msg, true, flags, reason_input)) {
            call_input.clear();
            lift_input = "permanent";
            feedback = msg;
            set_status(msg);
        } else {
            feedback = "Error: " + msg;
        }
    };

    auto confirm_unban = [&](const std::string& call_key) -> bool {
        int cw = std::min(46, cols - 4);
        int ch_h = 6;
        WINDOW* cwin = newwin(ch_h, cw, std::max(1, (rows - ch_h) / 2), std::max(1, (cols - cw) / 2));
        if (!cwin) return false;
        keypad(cwin, TRUE);
        wtimeout(cwin, 200);
        bool yes = false;
        while (true) {
            if (external_stop_flag_ && external_stop_flag_->load()) break;
            werase(cwin);
            box(cwin, 0, 0);
            mvwprintw(cwin, 0, 2, " Confirm Unban ");
            mvwaddnstr(cwin, 2, 2, clip("Unban callsign " + call_key + "?", cw - 4).c_str(), cw - 4);
            mvwaddnstr(cwin, 3, 2, "[Y] Yes   [N] No", cw - 4);
            wrefresh(cwin);
            int k = wgetch(cwin);
            if (k == ERR) continue;
            if (k == 'y' || k == 'Y') { yes = true; break; }
            if (k == 'n' || k == 'N' || k == 27) { yes = false; break; }
        }
        delwin(cwin);
        return yes;
    };

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        auto snap = engine_.snapshot();
        if (snap.ban_entries.empty()) {
            if (focus == 4) focus = 0;
            unban_sel = 0;
        } else if (unban_sel >= static_cast<int>(snap.ban_entries.size())) {
            unban_sel = static_cast<int>(snap.ban_entries.size()) - 1;
        }

        curs_set((focus <= 2) ? 1 : 0);
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Ban / Unban Callsign (banlist.cfg) ");

        mvwaddnstr(win, 2, 2, "Callsign: ", w - 4);
        if (focus == 0) wattron(win, A_UNDERLINE | A_BOLD);
        mvwaddnstr(win, 2, 12,
                   (call_input + std::string(12 - std::min<std::size_t>(12, call_input.size()), ' ')).c_str(),
                   w - 14);
        if (focus == 0) wattroff(win, A_UNDERLINE | A_BOLD);

        mvwaddnstr(win, 3, 2, "Lift At : ", w - 4);
        int field_w = std::max(10, w - 14);
        if (focus == 1) wattron(win, A_UNDERLINE | A_BOLD);
        mvwaddnstr(win, 3, 12, clip(lift_input, field_w).c_str(), field_w);
        if (focus == 1) wattroff(win, A_UNDERLINE | A_BOLD);
        if (w > 48) {
            mvwaddnstr(win, 3, 32, clip("(permanent, 30m, 12h, 7d, YYYY-MM-DD)", w - 34).c_str(), w - 34);
        }

        mvwaddnstr(win, 4, 2, "Reason  : ", w - 4);
        if (focus == 2) wattron(win, A_UNDERLINE | A_BOLD);
        mvwaddnstr(win, 4, 12, clip(reason_input, field_w).c_str(), field_w);
        if (focus == 2) wattroff(win, A_UNDERLINE | A_BOLD);

        std::string flags_line = std::string("Options : [") + (hard_ban ? "X" : " ") +
                                 "] Hard Ban (H)   [" + (appeal_allowed ? "X" : " ") +
                                 "] Appeal Allowed (A)";
        if (focus == 3) wattron(win, A_REVERSE | A_BOLD);
        mvwaddnstr(win, 5, 2, clip(flags_line, w - 4).c_str(), w - 4);
        if (focus == 3) wattroff(win, A_REVERSE | A_BOLD);

        mvwprintw(win, 7, 2, "Active Bans (%zu):", snap.ban_entries.size());
        int list_h = std::max(1, h - 11);
        if (snap.ban_entries.empty()) {
            mvwaddnstr(win, 8, 4, "(none)", w - 6);
        } else {
            int offset = (unban_sel >= list_h) ? (unban_sel - list_h + 1) : 0;
            for (int i = 0; i < list_h && (offset + i) < static_cast<int>(snap.ban_entries.size()); ++i) {
                const auto& e = snap.ban_entries[static_cast<std::size_t>(offset + i)];
                std::string row = " " + e.key + " [" + (e.hard_ban() ? "HARD" : "SOFT") +
                                  " " + e.format_lift_at() + "] " + e.reason;
                bool highlighted = (focus == 4) && (offset + i == unban_sel);
                if (highlighted) wattron(win, A_REVERSE | A_BOLD);
                mvwaddnstr(win, 8 + i, 3, clip(row, w - 6).c_str(), w - 6);
                if (highlighted) wattroff(win, A_REVERSE | A_BOLD);
            }
        }

        if (!feedback.empty()) {
            mvwaddnstr(win, h - 3, 2, clip(feedback, w - 4).c_str(), w - 4);
        }
        const char* help_str = (focus == 4)
                                   ? "Up/Down: pick | Enter: UNBAN (confirm) | Tab: next | Esc: close"
                                   : (focus == 3)
                                         ? "H: hard/soft | A: appeal | Enter: BAN | Tab: next | Esc: close"
                                         : "Enter: BAN | Tab: next field | Esc: close";
        mvwaddnstr(win, h - 2, 2, clip(help_str, w - 4).c_str(), w - 4);

        if (focus == 0) {
            wmove(win, 2, 12 + static_cast<int>(call_input.size()));
        } else if (focus == 1) {
            wmove(win, 3, 12 + std::min(field_w - 1, static_cast<int>(lift_input.size())));
        } else if (focus == 2) {
            wmove(win, 4, 12 + std::min(field_w - 1, static_cast<int>(reason_input.size())));
        }
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break;

        if (ch == '\t') {
            focus = (focus + 1) % (snap.ban_entries.empty() ? 4 : 5);
            continue;
        }

        if (focus == 4) {
            if (ch == KEY_UP && unban_sel > 0) {
                --unban_sel;
            } else if (ch == KEY_DOWN &&
                       unban_sel + 1 < static_cast<int>(snap.ban_entries.size())) {
                ++unban_sel;
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                std::string call = snap.ban_entries[static_cast<std::size_t>(unban_sel)].key;
                curs_set(0);
                if (confirm_unban(call)) {
                    std::string msg;
                    engine_.unban_callsign(call, &msg);
                    feedback = msg;
                    set_status(msg);
                } else {
                    feedback = "Unban cancelled.";
                }
            }
        } else if (focus == 3) {
            if (ch == 'h' || ch == 'H') {
                hard_ban = !hard_ban;
            } else if (ch == 'a' || ch == 'A') {
                appeal_allowed = !appeal_allowed;
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                submit_ban();
            }
        } else if (focus == 0) {
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                submit_ban();
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                if (!call_input.empty()) call_input.pop_back();
            } else if (ch >= 32 && ch < 127 && call_input.size() < 9) {
                char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
                    call_input.push_back(c);
                }
            }
        } else if (focus == 1) {
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                submit_ban();
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                if (!lift_input.empty()) lift_input.pop_back();
            } else if (ch >= 32 && ch < 127 && lift_input.size() < 24) {
                lift_input.push_back(static_cast<char>(ch));
            }
        } else if (focus == 2) {
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (!call_input.empty()) submit_ban();
                else focus = 0;
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                if (!reason_input.empty()) reason_input.pop_back();
            } else if (ch >= 32 && ch < 127 && reason_input.size() < 120) {
                reason_input.push_back(static_cast<char>(ch));
            }
        }
    }

    curs_set(0);
    delwin(win);
}

void NcursesUI::modal_approve_incoming_files() {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(78, std::max(56, cols - 2));
    int h = std::min(22, std::max(14, rows - 2));
    if (rows < 12 || cols < 46) return;

    WINDOW* win = newwin(h, w, (rows - h) / 2, (cols - w) / 2);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    auto pending_items = engine_.list_pending_incoming_files();
    auto approved_items = engine_.list_approved_files();
    int tab = 0; // 0 = Pending Incoming, 1 = Approved Files
    int sel[2] = { 0, 0 };
    int top[2] = { 0, 0 };
    std::string feedback = "Files Area: " + engine_.bbs_files_path();

    auto prompt_text = [&](const std::string& label, std::string& out) -> bool {
        out.clear();
        curs_set(1);
        while (true) {
            mvwaddnstr(win, h - 2, 2, std::string(w - 4, ' ').c_str(), w - 4);
            std::string shown = label + out;
            if (has_colors_) wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
            mvwaddnstr(win, h - 2, 2, clip(shown, w - 4).c_str(), w - 4);
            if (has_colors_) wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
            wmove(win, h - 2, std::min(w - 3, 2 + static_cast<int>(shown.size())));
            wrefresh(win);
            int c = wgetch(win);
            if (c == ERR) continue;
            if (c == 27) {
                curs_set(0);
                return false;
            }
            if (c == '\n' || c == '\r' || c == KEY_ENTER) {
                curs_set(0);
                return true;
            }
            if ((c == KEY_BACKSPACE || c == 127 || c == '\b') && !out.empty()) {
                out.pop_back();
            } else if (c >= 0x20 && c < 0x7F && out.size() < 48) {
                out.push_back(static_cast<char>(c));
            }
        }
    };

    auto prompt_yes_no = [&](const std::string& question) -> bool {
        while (true) {
            mvwaddnstr(win, h - 2, 2, std::string(w - 4, ' ').c_str(), w - 4);
            if (has_colors_) wattron(win, COLOR_PAIR(CP_DANGER) | A_BOLD);
            mvwaddnstr(win, h - 2, 2, clip(question + " [Y/N]: ", w - 4).c_str(), w - 4);
            if (has_colors_) wattroff(win, COLOR_PAIR(CP_DANGER) | A_BOLD);
            wrefresh(win);
            int c = wgetch(win);
            if (c == ERR) continue;
            if (c == 'y' || c == 'Y') return true;
            if (c == 'n' || c == 'N' || c == 27) return false;
        }
    };

    static const char* TAB_LABELS[2] = { " 1:Pending Incoming ", " 2:Approved Files " };

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;
        int list_h = std::max(1, h - 8);
        int count = (tab == 0) ? static_cast<int>(pending_items.size())
                               : static_cast<int>(approved_items.size());
        if (sel[tab] >= count) sel[tab] = std::max(0, count - 1);
        if (sel[tab] < top[tab]) top[tab] = sel[tab];
        if (sel[tab] >= top[tab] + list_h) top[tab] = sel[tab] - list_h + 1;

        werase(win);
        box(win, 0, 0);
        if (has_colors_) wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
        mvwaddnstr(win, 0, 2, " Manage BBS Files Area ", w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);

        // Row 1: Tab bar matching GTK & Windows (Pending Incoming | Approved Files)
        int tx = 2;
        for (int t = 0; t < 2; ++t) {
            if (t == tab) {
                if (has_colors_) wattron(win, COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
                else wattron(win, A_REVERSE | A_BOLD);
            } else if (has_colors_) {
                wattron(win, COLOR_PAIR(CP_MENU));
            }
            mvwaddnstr(win, 1, tx, TAB_LABELS[t], w - tx - 2);
            if (t == tab) {
                if (has_colors_) wattroff(win, COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
                else wattroff(win, A_REVERSE | A_BOLD);
            } else if (has_colors_) {
                wattroff(win, COLOR_PAIR(CP_MENU));
            }
            tx += static_cast<int>(std::string_view(TAB_LABELS[t]).size()) + 1;
        }
        mvwhline(win, 2, 1, ACS_HLINE, w - 2);

        if (tab == 0) {
            std::ostringstream hdr;
            hdr << std::left << std::setw(14) << "Uploader"
                << std::setw(std::max(16, w - 30)) << "Filename"
                << std::right << std::setw(10) << "Size";
            wattron(win, A_BOLD);
            mvwaddnstr(win, 3, 2, clip(hdr.str(), w - 4).c_str(), w - 4);
            wattroff(win, A_BOLD);

            if (pending_items.empty()) {
                mvwaddnstr(win, 5, 2, "(No pending files in incoming/<CALLSIGN-SSID>/)", w - 4);
            } else {
                for (int r = 0; r < list_h; ++r) {
                    int idx = top[0] + r;
                    if (idx >= count) break;
                    const auto& pf = pending_items[static_cast<std::size_t>(idx)];
                    std::ostringstream row;
                    row << std::left << std::setw(14) << clip(pf.uploader, 13)
                        << std::setw(std::max(16, w - 30)) << clip(pf.filename, std::max(15, w - 31))
                        << std::right << std::setw(10) << pf.size_bytes;
                    if (idx == sel[0]) wattron(win, A_REVERSE | A_BOLD);
                    mvwaddnstr(win, 4 + r, 2, clip(row.str(), w - 4).c_str(), w - 4);
                    if (idx == sel[0]) wattroff(win, A_REVERSE | A_BOLD);
                }
            }
        } else {
            std::ostringstream hdr;
            hdr << std::left << std::setw(std::max(24, w - 16)) << "Approved File Path"
                << std::right << std::setw(10) << "Size";
            wattron(win, A_BOLD);
            mvwaddnstr(win, 3, 2, clip(hdr.str(), w - 4).c_str(), w - 4);
            wattroff(win, A_BOLD);

            if (approved_items.empty()) {
                mvwaddnstr(win, 5, 2, "(No approved files in BBS files area)", w - 4);
            } else {
                for (int r = 0; r < list_h; ++r) {
                    int idx = top[1] + r;
                    if (idx >= count) break;
                    const auto& af = approved_items[static_cast<std::size_t>(idx)];
                    std::ostringstream row;
                    row << std::left << std::setw(std::max(24, w - 16))
                        << clip(af.rel_path, std::max(23, w - 17))
                        << std::right << std::setw(10) << af.size_bytes;
                    if (idx == sel[1]) wattron(win, A_REVERSE | A_BOLD);
                    mvwaddnstr(win, 4 + r, 2, clip(row.str(), w - 4).c_str(), w - 4);
                    if (idx == sel[1]) wattroff(win, A_REVERSE | A_BOLD);
                }
            }
        }

        mvwhline(win, h - 3, 1, ACS_HLINE, w - 2);
        mvwaddnstr(win, h - 2, 2, clip(feedback, w - 4).c_str(), w - 4);
        if (has_colors_) wattron(win, COLOR_PAIR(CP_MENU));
        mvwhline(win, h - 1, 1, ' ', w - 2);
        const char* footer = (tab == 0)
            ? "Tab/1-2:Tabs  Enter/A:Approve & Move  D:Delete  R:Refresh  Esc:Close"
            : "Tab/1-2:Tabs  D/Del:Remove Approved File  R:Refresh  Esc:Close";
        mvwaddnstr(win, h - 1, 2, clip(footer, w - 4).c_str(), w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_MENU));
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || ch == 'Q') break;
        if (ch == '\t' || ch == KEY_RIGHT || ch == KEY_LEFT || ch == KEY_BTAB) {
            tab = 1 - tab;
        } else if (ch == '1') {
            tab = 0;
        } else if (ch == '2') {
            tab = 1;
        } else if (ch == KEY_UP && sel[tab] > 0) {
            --sel[tab];
        } else if (ch == KEY_DOWN && sel[tab] + 1 < count) {
            ++sel[tab];
        } else if (ch == KEY_PPAGE) {
            sel[tab] = std::max(0, sel[tab] - list_h);
        } else if (ch == KEY_NPAGE && count > 0) {
            sel[tab] = std::min(count - 1, sel[tab] + list_h);
        } else if (ch == 'r' || ch == 'R') {
            pending_items = engine_.list_pending_incoming_files();
            approved_items = engine_.list_approved_files();
            feedback = "Refreshed (" + std::to_string(pending_items.size()) + " pending, " +
                       std::to_string(approved_items.size()) + " approved).";
        } else if (tab == 0 && (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == 'a' || ch == 'A') && count > 0) {
            const auto pf = pending_items[static_cast<std::size_t>(sel[0])];
            std::string subdir;
            if (!prompt_text("Subfolder in files area (blank=root): ", subdir)) {
                feedback = "Approval cancelled.";
                continue;
            }
            std::string dest_path, err;
            bool exists = false;
            bool ok = engine_.approve_incoming_file(pf.uploader, pf.filename, subdir,
                                                    false, dest_path, exists, err);
            if (!ok && exists) {
                if (prompt_yes_no("File exists in files area. Overwrite " + pf.filename + "?")) {
                    ok = engine_.approve_incoming_file(pf.uploader, pf.filename, subdir,
                                                       true, dest_path, exists, err);
                } else {
                    feedback = "Overwrite declined; file kept in incoming/.";
                    continue;
                }
            }
            if (ok) {
                feedback = "Approved -> " + dest_path;
                set_status("Approved incoming file: " + pf.uploader + "/" + pf.filename);
                pending_items = engine_.list_pending_incoming_files();
                approved_items = engine_.list_approved_files();
            } else {
                feedback = "Error: " + err;
            }
        } else if (tab == 0 && (ch == 'd' || ch == 'D' || ch == KEY_DC) && count > 0) {
            const auto pf = pending_items[static_cast<std::size_t>(sel[0])];
            if (prompt_yes_no("Delete incoming/" + pf.uploader + "/" + pf.filename + "?")) {
                std::string err;
                if (engine_.delete_incoming_file(pf.uploader, pf.filename, err)) {
                    feedback = "Deleted " + pf.uploader + "/" + pf.filename;
                    set_status(feedback);
                    pending_items = engine_.list_pending_incoming_files();
                } else {
                    feedback = "Error: " + err;
                }
            } else {
                feedback = "Delete cancelled.";
            }
        } else if (tab == 1 && (ch == 'd' || ch == 'D' || ch == KEY_DC) && count > 0) {
            const auto af = approved_items[static_cast<std::size_t>(sel[1])];
            if (prompt_yes_no("Remove approved file /" + af.rel_path + "?")) {
                std::string err;
                if (engine_.delete_approved_file(af.rel_path, err)) {
                    feedback = "Removed approved file /" + af.rel_path;
                    set_status(feedback);
                    approved_items = engine_.list_approved_files();
                } else {
                    feedback = "Error: " + err;
                }
            } else {
                feedback = "Remove cancelled.";
            }
        }
    }

    delwin(win);
}

void NcursesUI::modal_rig_control() {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(76, cols - 2);
    int h = std::min(19, rows - 2);
    if (w < 54 || h < 14) {
        set_status("Terminal too small for Radio Control (min 56x16).");
        return;
    }
    int y = std::max(0, (rows - h) / 2);
    int x = std::max(0, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 250);

    auto prompt_val = [&](const std::string& label, std::string& val) -> bool {
        curs_set(1);
        while (true) {
            mvwhline(win, h - 2, 1, ' ', w - 2);
            std::string shown = label + val;
            if (static_cast<int>(shown.size()) > w - 4) {
                shown = shown.substr(shown.size() - static_cast<std::size_t>(w - 4));
            }
            mvwaddnstr(win, h - 2, 2, shown.c_str(), w - 4);
            wrefresh(win);
            int c = wgetch(win);
            if (c == ERR) continue;
            if (c == 27) { curs_set(0); return false; }
            if (c == '\n' || c == '\r' || c == KEY_ENTER) { curs_set(0); return true; }
            if (c == KEY_BACKSPACE || c == 127 || c == 8) {
                if (!val.empty()) val.pop_back();
            } else if (c >= 32 && c < 127 && val.size() < 48) {
                val.push_back(static_cast<char>(c));
            }
        }
    };

    auto& rig = engine_.rig_controller();
    std::string feedback;
    {
        auto rc = rig.config();
        if (!rc.enabled || rc.protocol == radio::RigProtocol::None) {
            feedback = "Rig control disabled. Enable it in Node -> Settings -> 5:Rig.";
        } else {
            std::string err;
            if (rig.refresh(err)) feedback = "Queried radio status.";
            else feedback = err;
        }
    }

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;
        auto rc = rig.config();
        auto st = rig.status();

        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Radio / Rig Control ");

        std::ostringstream hdr;
        hdr << "Protocol: " << radio::rig_protocol_to_string(rc.protocol)
            << "  |  Endpoint: " << rc.host << ":" << rc.port
            << "  [" << (rc.enabled ? (st.reachable ? "ONLINE" : "NO REPLY") : "DISABLED") << "]";
        wattron(win, A_BOLD);
        mvwaddnstr(win, 2, 2, clip(hdr.str(), w - 4).c_str(), w - 4);
        wattroff(win, A_BOLD);
        mvwhline(win, 3, 1, ACS_HLINE, w - 2);

        if (rc.protocol == radio::RigProtocol::RadioFoundry) {
            mvwaddnstr(win, 4, 2, clip("Zone / Pos   : Zone " + std::to_string(st.zone) +
                                       ", Channel Pos " + std::to_string(st.pos) + " (0-based)", w - 4).c_str(), w - 4);
            mvwaddnstr(win, 5, 2, clip("Channel Name : " + (st.channel_name.empty() ? "(unknown)" : st.channel_name), w - 4).c_str(), w - 4);
            mvwaddnstr(win, 6, 2, clip("Frequency    : " + std::to_string(st.freq_hz) + " Hz", w - 4).c_str(), w - 4);
            if (st.is_dmr) {
                mvwaddnstr(win, 7, 2, clip("DMR Params   : CC " + std::to_string(st.color_code) +
                                           "  TS " + std::to_string(st.timeslot) +
                                           "  TG " + std::to_string(st.talkgroup), w - 4).c_str(), w - 4);
            } else {
                mvwaddnstr(win, 7, 2, "DMR Params   : (analog / not reported)", w - 4);
            }
            mvwaddnstr(win, 8, 2, clip("Notifications: " + std::string(st.notifications_active ? "ACTIVE" : "OFF"), w - 4).c_str(), w - 4);
            mvwaddnstr(win, 9, 2, clip("Raw Status   : " + (st.raw_status.empty() ? "(none)" : st.raw_status), w - 4).c_str(), w - 4);
            wattron(win, A_BOLD);
            mvwaddnstr(win, 11, 2, "Commands (RadioFoundry / takRF UDP 4191):", w - 4);
            wattroff(win, A_BOLD);
            mvwaddnstr(win, 12, 2, clip(" [Q/R] Query (select)   [Z] Select Zone & Pos   [N] Stop Notify", w - 4).c_str(), w - 4);
            mvwaddnstr(win, 13, 2, clip(" [+/-] Step Channel     [[/]] Step Zone         [Esc] Close", w - 4).c_str(), w - 4);
        } else {
            mvwaddnstr(win, 4, 2, clip("Frequency    : " + std::to_string(st.freq_hz) + " Hz", w - 4).c_str(), w - 4);
            mvwaddnstr(win, 5, 2, clip("Mode / PB    : " + (st.mode.empty() ? "(unknown)" : st.mode) +
                                       " (" + std::to_string(st.passband_hz) + " Hz)", w - 4).c_str(), w - 4);
            mvwaddnstr(win, 6, 2, clip("PTT / VFO    : " + std::string(st.ptt > 0 ? "TX" : (st.ptt == 0 ? "RX" : "UNKNOWN")) +
                                       "  |  VFO: " + (st.vfo.empty() ? "(unknown)" : st.vfo), w - 4).c_str(), w - 4);
            wattron(win, A_BOLD);
            mvwaddnstr(win, 11, 2, "Commands (Hamlib rigctld TCP 4532):", w - 4);
            wattroff(win, A_BOLD);
            mvwaddnstr(win, 12, 2, clip(" [Q/R] Query Rig        [F] Set Frequency (Hz)  [M] Set Mode", w - 4).c_str(), w - 4);
            mvwaddnstr(win, 13, 2, clip(" [P]   Toggle PTT       [V] Set VFO             [Esc] Close", w - 4).c_str(), w - 4);
        }

        if (has_colors_) wattron(win, COLOR_PAIR(CP_WARN));
        mvwaddnstr(win, h - 3, 2, clip(feedback, w - 4).c_str(), w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_WARN));

        if (has_colors_) wattron(win, COLOR_PAIR(CP_MENU));
        mvwhline(win, h - 1, 1, ' ', w - 2);
        mvwaddnstr(win, h - 1, 2, "Q/R:Refresh  Esc:Close", w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_MENU));
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break;

        std::string err;
        if (ch == 'q' || ch == 'Q' || ch == 'r' || ch == 'R') {
            if (rig.refresh(err)) feedback = "Radio status refreshed.";
            else feedback = err;
        } else if (rc.protocol == radio::RigProtocol::RadioFoundry) {
            if (ch == '+' || ch == '=') {
                if (rig.step_channel(1, err)) feedback = "Stepped channel +1.";
                else feedback = err;
            } else if (ch == '-') {
                if (rig.step_channel(-1, err)) feedback = "Stepped channel -1.";
                else feedback = err;
            } else if (ch == ']') {
                if (rig.step_zone(1, err)) feedback = "Stepped zone +1.";
                else feedback = err;
            } else if (ch == '[') {
                if (rig.step_zone(-1, err)) feedback = "Stepped zone -1.";
                else feedback = err;
            } else if (ch == 'n' || ch == 'N') {
                if (rig.stop_notifications(err)) feedback = "Notifications stopped (select?notify=0).";
                else feedback = err;
            } else if (ch == 'z' || ch == 'Z') {
                std::string zs = std::to_string(std::max(0, st.zone));
                std::string ps = std::to_string(std::max(0, st.pos));
                if (prompt_val("Zone (0-based): ", zs) && prompt_val("Channel Pos (0-based): ", ps)) {
                    try {
                        int z = std::stoi(zs);
                        int p = std::stoi(ps);
                        if (rig.select_zone_channel(z, p, err)) feedback = "Selected zone " + zs + " pos " + ps + ".";
                        else feedback = err;
                    } catch (...) { feedback = "Invalid zone/pos number."; }
                }
            }
        } else if (rc.protocol == radio::RigProtocol::Rigctl) {
            if (ch == 'f' || ch == 'F') {
                std::string fs = std::to_string(st.freq_hz ? st.freq_hz : 145050000ULL);
                if (prompt_val("Frequency (Hz): ", fs)) {
                    try {
                        uint64_t hz = std::stoull(fs);
                        if (rig.set_frequency(hz, err)) feedback = "Frequency set to " + fs + " Hz.";
                        else feedback = err;
                    } catch (...) { feedback = "Invalid frequency."; }
                }
            } else if (ch == 'm' || ch == 'M') {
                std::string ms = st.mode.empty() ? "FM" : st.mode;
                std::string pbs = std::to_string(st.passband_hz);
                if (prompt_val("Mode (FM/USB/LSB/CW/PKTFM): ", ms) && prompt_val("Passband Hz (0=default): ", pbs)) {
                    try {
                        int pb = std::stoi(pbs);
                        if (rig.set_mode(ms, pb, err)) feedback = "Mode set to " + ms + ".";
                        else feedback = err;
                    } catch (...) { feedback = "Invalid passband."; }
                }
            } else if (ch == 'p' || ch == 'P') {
                bool next_tx = !(st.ptt > 0);
                if (rig.set_ptt(next_tx, err)) feedback = next_tx ? "PTT set to TX." : "PTT set to RX.";
                else feedback = err;
            } else if (ch == 'v' || ch == 'V') {
                std::string vs = st.vfo.empty() ? "VFOA" : st.vfo;
                if (prompt_val("VFO (VFOA/VFOB): ", vs)) {
                    if (rig.set_vfo(vs, err)) feedback = "VFO set to " + vs + ".";
                    else feedback = err;
                }
            }
        }
    }

    delwin(win);
}

void NcursesUI::modal_settings(int initial_tab) {
    node::NodeConfig draft = engine_.config();

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(78, cols - 2);
    int h = std::min(20, rows - 2);
    if (w < 56 || h < 14) {
        set_status("Terminal too small for Settings UI (min 58x16).");
        return;
    }
    int y = std::max(0, (rows - h) / 2);
    int x = std::max(0, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    auto prompt_edit = [&](const std::string& label, std::string& val, std::size_t max_len = 140) -> bool {
        curs_set(1);
        while (true) {
            mvwhline(win, h - 2, 1, ' ', w - 2);
            std::string shown = label + val;
            if (static_cast<int>(shown.size()) > w - 4) {
                shown = shown.substr(shown.size() - static_cast<std::size_t>(w - 4));
            }
            mvwaddnstr(win, h - 2, 2, shown.c_str(), w - 4);
            wrefresh(win);
            int c = wgetch(win);
            if (c == ERR) continue;
            if (c == 27) { curs_set(0); return false; }
            if (c == '\n' || c == '\r' || c == KEY_ENTER) { curs_set(0); return true; }
            if (c == KEY_BACKSPACE || c == 127 || c == 8) {
                if (!val.empty()) val.pop_back();
            } else if (c >= 32 && c < 127 && val.size() < max_len) {
                val.push_back(static_cast<char>(c));
            }
        }
    };

    auto show_notice = [&](const std::string& title, const std::string& msg) {
        int nw = std::min(72, cols - 4);
        int nh = 8;
        int ny = std::max(1, (rows - nh) / 2);
        int nx = std::max(1, (cols - nw) / 2);
        WINDOW* nwin = newwin(nh, nw, ny, nx);
        if (!nwin) return;
        keypad(nwin, TRUE);
        wtimeout(nwin, 200);
        werase(nwin);
        box(nwin, 0, 0);
        mvwprintw(nwin, 0, 2, " %s ", title.c_str());
        int lw = nw - 4;
        std::size_t pos = 0;
        for (int line = 2; line <= 4 && pos < msg.size(); ++line) {
            mvwaddnstr(nwin, line, 2, msg.substr(pos, static_cast<std::size_t>(lw)).c_str(), lw);
            pos += static_cast<std::size_t>(lw);
        }
        mvwaddnstr(nwin, nh - 2, 2, "Press any key to continue...", lw);
        wrefresh(nwin);
        while (true) {
            if (external_stop_flag_ && external_stop_flag_->load()) break;
            int c = wgetch(nwin);
            if (c != ERR) break;
        }
        delwin(nwin);
    };

    auto iface_summary = [](const radio::RadioInterfaceConfig& ic) -> std::string {
        auto get_p = [&](const char* k) -> std::string {
            auto it = ic.params.find(k);
            return (it != ic.params.end()) ? it->second : "";
        };
        if (ic.type == "axudp" || ic.type == "telnet") {
            std::string bh = get_p("bind_host");
            std::string bp = get_p("bind_port");
            return (bh.empty() ? "0.0.0.0" : bh) + ":" + bp;
        }
        if (ic.type == "axip") {
            std::string bh = get_p("bind_host");
            return (bh.empty() ? "0.0.0.0" : bh) + " (IP proto 93)";
        }
        if (ic.type == "kiss-serial") {
            return get_p("device") + " @" + get_p("baud");
        }
        if (ic.type == "kiss-tcp") {
            return get_p("mode") + " " + get_p("host") + ":" + get_p("port");
        }
        return ic.description;
    };

    static const char* TAB_NAMES[7] = {
        "1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn"
    };
    static const char* NODE_LABELS[10] = {
        "Node Callsign",
        "Node Alias",
        "Welcome Message",
        "Info Message",
        "Allow Incoming (Default)",
        "Learned Peer Policy",
        "New User Welcome",
        "Interfaces File",
        "Peers File",
        "Themes File"
    };
    static const char* BBS_LABELS[9] = {
        "Enable Packet BBS",
        "BBS SSID (0-15)",
        "Callsign Override",
        "BBS Alias",
        "Enable BBS Files Area",
        "Case-Insensitive Files",
        "Files Area Root Path",
        "Mail Storage Path",
        "BBS Welcome Message"
    };
    static const char* CHAT_LABELS[6] = {
        "Enable Multi-User CHAT",
        "CHAT SSID (0-15)",
        "Callsign Override",
        "CHAT Alias",
        "Default Topic",
        "CHAT Welcome Message"
    };
    static const char* RIG_LABELS[6] = {
        "Enable Rig Control",
        "Protocol (rigctl/rf)",
        "Radio Host / IP",
        "Port (4532 / 4191)",
        "Poll Interval (sec)",
        "RadioFoundry Notify"
    };
    static const char* PRN_LABELS[6] = {
        "Enable Line Printer",
        "Device (/dev/parport0,/dev/ttyS0)",
        "Protocol (generic/mx80/escp2)",
        "Serial Baud Rate",
        "Printer Log Mask (0x..)",
        "Condensed 132-Col Mode"
    };

    auto tab_item_count = [&](int t) -> int {
        if (t == 0) return 10;
        if (t == 1) return 9;
        if (t == 2) return 6;
        if (t == 3) return static_cast<int>(draft.interfaces.size());
        if (t == 4) return static_cast<int>(draft.peers.size());
        return 6;
    };

    auto tab_field_label = [&](int t, int idx) -> const char* {
        if (t == 0) return NODE_LABELS[idx];
        if (t == 1) return BBS_LABELS[idx];
        if (t == 2) return CHAT_LABELS[idx];
        if (t == 5) return RIG_LABELS[idx];
        if (t == 6) return PRN_LABELS[idx];
        return "";
    };

    auto tab_field_value = [&](int t, int idx) -> std::string {
        if (t == 0) {
            switch (idx) {
                case 0: return draft.callsign.str();
                case 1: return draft.alias.valid() ? draft.alias.str() : "(none)";
                case 2: return draft.welcome_text;
                case 3: return draft.info_text;
                case 4: return draft.accept_incoming_default ? "[ON]" : "[OFF]";
                case 5: return node::learned_peer_policy_to_string(draft.learned_peer_policy);
                case 6: return draft.new_user_welcome.empty() ? "(use default)" : draft.new_user_welcome;
                case 7: return draft.interfaces_file;
                case 8: return draft.peers_file;
                case 9: return draft.themes_file;
                default: return "";
            }
        }
        if (t == 1) {
            switch (idx) {
                case 0: return draft.bbs.enabled ? "[ON]" : "[OFF]";
                case 1: return std::to_string(static_cast<int>(draft.bbs.ssid)) +
                               " (resolves: " + draft.resolved_bbs_callsign().str() + ")";
                case 2: return draft.bbs.callsign.valid() ? draft.bbs.callsign.str() : "(auto from base+SSID)";
                case 3: return draft.bbs.alias.valid() ? draft.bbs.alias.str() : "(none)";
                case 4: return draft.bbs.files_enabled ? "[ON]" : "[OFF]";
                case 5: return draft.bbs.files_case_insensitive ? "[ON]" : "[OFF]";
                case 6: return draft.bbs.files_path;
                case 7: return draft.bbs.mail_path;
                case 8: return draft.bbs.welcome;
                default: return "";
            }
        }
        if (t == 2) {
            switch (idx) {
                case 0: return draft.chat.enabled ? "[ON]" : "[OFF]";
                case 1: return std::to_string(static_cast<int>(draft.chat.ssid)) +
                               " (resolves: " + draft.resolved_chat_callsign().str() + ")";
                case 2: return draft.chat.callsign.valid() ? draft.chat.callsign.str() : "(auto from base+SSID)";
                case 3: return draft.chat.alias.valid() ? draft.chat.alias.str() : "(none)";
                case 4: return draft.chat.default_topic;
                case 5: return draft.chat.welcome;
                default: return "";
            }
        }
        if (t == 5) {
            switch (idx) {
                case 0: return draft.rig.enabled ? "[ON]" : "[OFF]";
                case 1: return radio::rig_protocol_to_string(draft.rig.protocol);
                case 2: return draft.rig.host;
                case 3: return std::to_string(draft.rig.port);
                case 4: return std::to_string(draft.rig.poll_interval_sec) + " s";
                case 5: return draft.rig.notify ? "[ON]" : "[OFF]";
                default: return "";
            }
        }
        if (t == 6) {
            switch (idx) {
                case 0: return draft.logging.printer.enabled ? "[ON]" : "[OFF]";
                case 1: return draft.logging.printer.device.empty() ? "(none)" : draft.logging.printer.device;
                case 2: return draft.logging.printer.protocol.empty() ? "generic" : draft.logging.printer.protocol;
                case 3: return std::to_string(draft.logging.printer.baud);
                case 4: {
                    std::ostringstream oss;
                    oss << "0x" << std::hex << draft.logging.printer.level;
                    return oss.str();
                }
                case 5: return draft.logging.printer.condensed ? "[ON]" : "[OFF]";
                default: return "";
            }
        }
        return "";
    };

    int tab = (initial_tab >= 0 && initial_tab < 7) ? initial_tab : 0;
    int sel[7] = { 0, 0, 0, 0, 0, 0, 0 };
    int if_scroll = 0;
    int peer_scroll = 0;
    const int content_h = std::max(1, h - 8);
    std::string feedback = "Config: " + (draft.source_path.empty() ? std::string("./node.cfg") : draft.source_path);

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        int cnt = tab_item_count(tab);
        if (cnt <= 0) sel[tab] = 0;
        else if (sel[tab] >= cnt) sel[tab] = cnt - 1;
        if (tab == 3) {
            int list_h = std::max(1, content_h - 1);
            if (sel[3] < if_scroll) if_scroll = sel[3];
            if (sel[3] >= if_scroll + list_h) if_scroll = sel[3] - list_h + 1;
        } else if (tab == 4) {
            int list_h = std::max(1, content_h - 1);
            if (sel[4] < peer_scroll) peer_scroll = sel[4];
            if (sel[4] >= peer_scroll + list_h) peer_scroll = sel[4] - list_h + 1;
        }

        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Node Settings ");

        // Row 1: Tab bar matching GTK/Windows/DOS/TinyTMUX tabs
        int tx = 2;
        for (int t = 0; t < 7; ++t) {
            std::string lbl = std::string(" ") + TAB_NAMES[t] + " ";
            if (t == tab) {
                if (has_colors_) wattron(win, COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
                else wattron(win, A_REVERSE | A_BOLD);
            } else if (has_colors_) {
                wattron(win, COLOR_PAIR(CP_MENU));
            }
            mvwaddnstr(win, 1, tx, lbl.c_str(), w - tx - 2);
            if (t == tab) {
                if (has_colors_) wattroff(win, COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
                else wattroff(win, A_REVERSE | A_BOLD);
            } else if (has_colors_) {
                wattroff(win, COLOR_PAIR(CP_MENU));
            }
            tx += static_cast<int>(lbl.size()) + 1;
        }
        mvwhline(win, 2, 1, ACS_HLINE, w - 2);

        if (tab != 3 && tab != 4) {
            for (int i = 0; i < cnt && i < content_h; ++i) {
                std::string fval = tab_field_value(tab, i);
                bool dim_inline = (tab == 0 && (i == 7 || i == 8) && fval.empty());
                std::ostringstream row;
                row << std::left << std::setw(25) << tab_field_label(tab, i)
                    << ": " << (dim_inline ? std::string() : fval);
                if (i == sel[tab]) wattron(win, A_REVERSE | A_BOLD);
                mvwaddnstr(win, 3 + i, 2, clip(row.str(), w - 4).c_str(), w - 4);
                if (i == sel[tab]) wattroff(win, A_REVERSE | A_BOLD);
                if (dim_inline && 29 < w - 4) {
                    wattron(win, ((i == sel[tab]) ? (A_REVERSE | A_DIM) : A_DIM));
                    mvwaddnstr(win, 3 + i, 29, "<inline>", w - 31);
                    wattroff(win, ((i == sel[tab]) ? (A_REVERSE | A_DIM) : A_DIM));
                }
            }
        } else if (tab == 3) {
            wattron(win, A_BOLD | A_UNDERLINE);
            mvwaddnstr(win, 3, 2,
                       clip("ID           Type         Endpoint / Device             Peers", w - 4).c_str(),
                       w - 4);
            wattroff(win, A_BOLD | A_UNDERLINE);
            int list_h = std::max(1, content_h - 1);
            if (cnt == 0) {
                mvwaddnstr(win, 4, 2, "(no interfaces configured - press A to add)", w - 4);
            } else {
                for (int r = 0; r < list_h && if_scroll + r < cnt; ++r) {
                    int idx = if_scroll + r;
                    const auto& ic = draft.interfaces[static_cast<std::size_t>(idx)];
                    std::size_t pcnt = ic.peers.size();
                    for (const auto& p : draft.peers) {
                        if (p.interface_id == ic.id) ++pcnt;
                    }
                    std::ostringstream row;
                    row << std::left << std::setw(13) << clip(ic.id, 12)
                        << std::setw(13) << clip(ic.type, 12)
                        << std::setw(30) << clip(iface_summary(ic), 29)
                        << pcnt;
                    if (idx == sel[3]) wattron(win, A_REVERSE | A_BOLD);
                    mvwaddnstr(win, 4 + r, 2, clip(row.str(), w - 4).c_str(), w - 4);
                    if (idx == sel[3]) wattroff(win, A_REVERSE | A_BOLD);
                }
            }
        } else {
            wattron(win, A_BOLD | A_UNDERLINE);
            mvwaddnstr(win, 3, 2,
                       clip("ID          Callsign   Iface    Endpoint               ACL    Tag", w - 4).c_str(),
                       w - 4);
            wattroff(win, A_BOLD | A_UNDERLINE);
            int list_h = std::max(1, content_h - 1);
            if (cnt == 0) {
                mvwaddnstr(win, 4, 2, "(no peers configured - press A to add)", w - 4);
            } else {
                for (int r = 0; r < list_h && peer_scroll + r < cnt; ++r) {
                    int idx = peer_scroll + r;
                    const auto& p = draft.peers[static_cast<std::size_t>(idx)];
                    std::string ep = p.host;
                    if (!ep.empty() && p.port > 0) ep += ":" + std::to_string(p.port);
                    if (ep.empty()) ep = "(any)";
                    std::string acl = std::string(p.allow_inbound ? "IN" : "--") + "/" +
                                      std::string(p.allow_outbound ? "OUT" : "---");
                    std::ostringstream row;
                    row << std::left << std::setw(12) << clip(p.id, 11)
                        << std::setw(11) << clip(p.callsign, 10)
                        << std::setw(9) << clip(p.interface_id, 8)
                        << std::setw(23) << clip(ep, 22)
                        << std::setw(7) << acl
                        << (p.auto_learned ? "[AUTO]" : "");
                    if (idx == sel[4]) wattron(win, A_REVERSE | A_BOLD);
                    mvwaddnstr(win, 4 + r, 2, clip(row.str(), w - 4).c_str(), w - 4);
                    if (idx == sel[4]) wattroff(win, A_REVERSE | A_BOLD);
                }
            }
        }

        if (has_colors_) wattron(win, COLOR_PAIR(CP_WARN));
        mvwaddnstr(win, h - 3, 2, clip(feedback, w - 4).c_str(), w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_WARN));

        if (has_colors_) wattron(win, COLOR_PAIR(CP_MENU));
        mvwhline(win, h - 1, 1, ' ', w - 2);
        const char* help_bar =
            (tab == 3)
                ? "Tab/1-7:Tabs  A:Add  Enter/E:Edit  D/Del:Remove  S/F2:Save  Esc:Cancel"
                : (tab == 4)
                      ? "1-7:Tabs A:Add E:Edit D:Del I:In O:Out K:KeepAuto S/F2:Save Esc"
                      : "Tab/Left/Right/1-7:Tabs  Enter/Space:Edit  S/F2:Save & Apply  Esc:Cancel";
        mvwaddnstr(win, h - 1, 2, clip(help_bar, w - 4).c_str(), w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_MENU));
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break;

        if (ch == '\t' || ch == KEY_RIGHT) {
            tab = (tab + 1) % 7;
            continue;
        }
        if (ch == KEY_BTAB || ch == KEY_LEFT) {
            tab = (tab + 6) % 7;
            continue;
        }
        if (ch >= '1' && ch <= '7') {
            tab = ch - '1';
            continue;
        }
        if (ch == KEY_UP && sel[tab] > 0) {
            --sel[tab];
            continue;
        }
        if (ch == KEY_DOWN && sel[tab] + 1 < cnt) {
            ++sel[tab];
            continue;
        }

        if (ch == 's' || ch == 'S' || ch == KEY_F(2)) {
            auto res = engine_.apply_config(draft, true);
            if (!res.applied) {
                feedback = "Error: " + res.error;
                continue;
            }
            if (!res.saved_to_disk) {
                show_notice("Read-Only Config Notice",
                            res.warning.empty()
                                ? ("Config file '" + res.save_path +
                                   "' is not writable. Changes remain in memory only while running and will be lost on exit.")
                                : res.warning);
                set_status("Settings applied IN MEMORY ONLY (config not writable).");
            } else {
                std::string st = "Settings saved to " + res.save_path + " and applied.";
                if (res.signed_off_sessions > 0) {
                    st += " Signed off " + std::to_string(res.signed_off_sessions) + " session(s).";
                }
                if (!res.warning.empty()) {
                    st += " (" + res.warning + ")";
                }
                set_status(st);
            }
            break;
        }

        if (tab == 3) {
            if (ch == 'a' || ch == 'A') {
                radio::RadioInterfaceConfig ic;
                bool added = modal_add_interface_wizard(draft.interfaces, ic);
                touchwin(win);
                wrefresh(win);
                if (!added) {
                    feedback = "Add interface cancelled.";
                    continue;
                }
                std::string new_id = ic.id;
                draft.interfaces.push_back(std::move(ic));
                sel[3] = static_cast<int>(draft.interfaces.size()) - 1;
                feedback = "Added interface '" + new_id + "' (press S/F2 to Save & Apply).";
            } else if ((ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == 'e' || ch == 'E') && cnt > 0) {
                int idx = sel[3];
                radio::RadioInterfaceConfig ic;
                bool updated = modal_edit_interface(draft.interfaces, idx, ic);
                touchwin(win);
                wrefresh(win);
                if (!updated) {
                    feedback = "Edit interface cancelled.";
                    continue;
                }
                std::string new_id = ic.id;
                draft.interfaces[static_cast<std::size_t>(idx)] = std::move(ic);
                feedback = "Updated interface '" + new_id + "' (press S/F2 to Save & Apply).";
            } else if ((ch == 'd' || ch == 'D' || ch == KEY_DC) && cnt > 0) {
                std::string rem_id = draft.interfaces[static_cast<std::size_t>(sel[3])].id;
                mvwhline(win, h - 2, 1, ' ', w - 2);
                mvwaddnstr(win, h - 2, 2, ("Remove interface '" + rem_id + "'? [Y/N]: ").c_str(), w - 4);
                wrefresh(win);
                int yn = 0;
                while (true) {
                    int c = wgetch(win);
                    if (c == ERR) continue;
                    if (c == 'y' || c == 'Y') { yn = 1; break; }
                    if (c == 'n' || c == 'N' || c == 27) { yn = 0; break; }
                }
                if (yn) {
                    draft.interfaces.erase(draft.interfaces.begin() + sel[3]);
                    feedback = "Removed interface '" + rem_id + "'.";
                } else {
                    feedback = "Remove cancelled.";
                }
            }
            continue;
        }

        if (tab == 4) {
            if (ch == 'a' || ch == 'A') {
                radio::PeerConfig np;
                np.allow_inbound = true;
                np.allow_outbound = true;
                np.auto_learned = false;
                for (const auto& ic : draft.interfaces) {
                    if (ic.type != "telnet" && ic.type != "local") {
                        np.interface_id = ic.id;
                        break;
                    }
                }
                bool added = modal_edit_peer(draft.peers, draft.interfaces, -1, np);
                touchwin(win);
                wrefresh(win);
                if (!added) {
                    feedback = "Add peer cancelled.";
                    continue;
                }
                std::string new_id = np.id;
                draft.peers.push_back(std::move(np));
                sel[4] = static_cast<int>(draft.peers.size()) - 1;
                feedback = "Added peer '" + new_id + "' (press S/F2 to Save & Apply).";
            } else if ((ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == 'e' || ch == 'E') && cnt > 0) {
                int idx = sel[4];
                radio::PeerConfig np = draft.peers[static_cast<std::size_t>(idx)];
                bool updated = modal_edit_peer(draft.peers, draft.interfaces, idx, np);
                touchwin(win);
                wrefresh(win);
                if (!updated) {
                    feedback = "Edit peer cancelled.";
                    continue;
                }
                np.auto_learned = false;
                std::string new_id = np.id;
                draft.peers[static_cast<std::size_t>(idx)] = std::move(np);
                feedback = "Updated peer '" + new_id + "' (press S/F2 to Save & Apply).";
            } else if ((ch == 'd' || ch == 'D' || ch == KEY_DC) && cnt > 0) {
                std::string rem_id = draft.peers[static_cast<std::size_t>(sel[4])].id;
                mvwhline(win, h - 2, 1, ' ', w - 2);
                mvwaddnstr(win, h - 2, 2, ("Remove peer '" + rem_id + "'? [Y/N]: ").c_str(), w - 4);
                wrefresh(win);
                int yn = 0;
                while (true) {
                    int c = wgetch(win);
                    if (c == ERR) continue;
                    if (c == 'y' || c == 'Y') { yn = 1; break; }
                    if (c == 'n' || c == 'N' || c == 27) { yn = 0; break; }
                }
                if (yn) {
                    draft.peers.erase(draft.peers.begin() + sel[4]);
                    feedback = "Removed peer '" + rem_id + "'.";
                } else {
                    feedback = "Remove cancelled.";
                }
            } else if ((ch == 'i' || ch == 'I') && cnt > 0) {
                auto& p = draft.peers[static_cast<std::size_t>(sel[4])];
                p.allow_inbound = !p.allow_inbound;
                p.auto_learned = false;
                feedback = "Peer '" + p.id + "' allow_inbound = " + (p.allow_inbound ? "true" : "false");
            } else if ((ch == 'o' || ch == 'O') && cnt > 0) {
                auto& p = draft.peers[static_cast<std::size_t>(sel[4])];
                p.allow_outbound = !p.allow_outbound;
                p.auto_learned = false;
                feedback = "Peer '" + p.id + "' allow_outbound = " + (p.allow_outbound ? "true" : "false");
            } else if (ch == 'k' || ch == 'K') {
                int kept = 0;
                for (auto& p : draft.peers) {
                    if (p.auto_learned) {
                        p.auto_learned = false;
                        ++kept;
                    }
                }
                feedback = "Marked " + std::to_string(kept) + " auto-learned peer(s) persistent (press S/F2 to Save).";
            }
            continue;
        }

        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
            int f = sel[tab];
            if (tab == 0 && f == 4) { draft.accept_incoming_default = !draft.accept_incoming_default; continue; }
            if (tab == 0 && f == 5) {
                using LPP = radio::LearnedPeerPolicy;
                if (draft.learned_peer_policy == LPP::None) draft.learned_peer_policy = LPP::Inbound;
                else if (draft.learned_peer_policy == LPP::Inbound) draft.learned_peer_policy = LPP::Outbound;
                else if (draft.learned_peer_policy == LPP::Outbound) draft.learned_peer_policy = LPP::Both;
                else draft.learned_peer_policy = LPP::None;
                continue;
            }
            if (tab == 1 && f == 0) { draft.bbs.enabled = !draft.bbs.enabled; continue; }
            if (tab == 1 && f == 4) { draft.bbs.files_enabled = !draft.bbs.files_enabled; continue; }
            if (tab == 1 && f == 5) { draft.bbs.files_case_insensitive = !draft.bbs.files_case_insensitive; continue; }
            if (tab == 2 && f == 0) { draft.chat.enabled = !draft.chat.enabled; continue; }
            if (tab == 5 && f == 0) { draft.rig.enabled = !draft.rig.enabled; continue; }
            if (tab == 5 && f == 1) {
                if (draft.rig.protocol == radio::RigProtocol::Rigctl) {
                    draft.rig.protocol = radio::RigProtocol::RadioFoundry;
                    if (draft.rig.port == 4532 || draft.rig.port == 0) draft.rig.port = 4191;
                } else {
                    draft.rig.protocol = radio::RigProtocol::Rigctl;
                    if (draft.rig.port == 4191 || draft.rig.port == 0) draft.rig.port = 4532;
                }
                continue;
            }
            if (tab == 5 && f == 5) { draft.rig.notify = !draft.rig.notify; continue; }
            if (tab == 6 && f == 0) { draft.logging.printer.enabled = !draft.logging.printer.enabled; continue; }
            if (tab == 6 && f == 2) {
                if (draft.logging.printer.protocol == "generic") draft.logging.printer.protocol = "mx80";
                else if (draft.logging.printer.protocol == "mx80") draft.logging.printer.protocol = "escp2";
                else draft.logging.printer.protocol = "generic";
                continue;
            }
            if (tab == 6 && f == 5) { draft.logging.printer.condensed = !draft.logging.printer.condensed; continue; }

            std::string val;
            if (tab == 0) {
                if (f == 0) val = draft.callsign.str();
                else if (f == 1) val = draft.alias.valid() ? draft.alias.str() : "";
                else if (f == 2) val = draft.welcome_text;
                else if (f == 3) val = draft.info_text;
                else if (f == 6) val = draft.new_user_welcome;
                else if (f == 7) val = draft.interfaces_file;
                else if (f == 8) val = draft.peers_file;
                else if (f == 9) val = draft.themes_file;
            } else if (tab == 1) {
                if (f == 1) val = std::to_string(static_cast<int>(draft.bbs.ssid));
                else if (f == 2) val = draft.bbs.callsign.valid() ? draft.bbs.callsign.str() : "";
                else if (f == 3) val = draft.bbs.alias.valid() ? draft.bbs.alias.str() : "";
                else if (f == 6) val = draft.bbs.files_path;
                else if (f == 7) val = draft.bbs.mail_path;
                else if (f == 8) val = draft.bbs.welcome;
            } else if (tab == 2) {
                if (f == 1) val = std::to_string(static_cast<int>(draft.chat.ssid));
                else if (f == 2) val = draft.chat.callsign.valid() ? draft.chat.callsign.str() : "";
                else if (f == 3) val = draft.chat.alias.valid() ? draft.chat.alias.str() : "";
                else if (f == 4) val = draft.chat.default_topic;
                else if (f == 5) val = draft.chat.welcome;
            } else if (tab == 5) {
                if (f == 2) val = draft.rig.host;
                else if (f == 3) val = std::to_string(draft.rig.port);
                else if (f == 4) val = std::to_string(draft.rig.poll_interval_sec);
            } else if (tab == 6) {
                if (f == 1) val = draft.logging.printer.device;
                else if (f == 3) val = std::to_string(draft.logging.printer.baud);
                else if (f == 4) {
                    std::ostringstream oss;
                    oss << "0x" << std::hex << draft.logging.printer.level;
                    val = oss.str();
                }
            }

            if (!prompt_edit(std::string(tab_field_label(tab, f)) + ": ", val)) continue;

            if (tab == 0) {
                if (f == 0) {
                    auto parsed = node::NodeConfig::parse_callsign_with_default_ssid(val);
                    if (!parsed) feedback = "Invalid node callsign '" + val + "'.";
                    else { draft.callsign = *parsed; feedback = "Node callsign updated (press S/F2 to Save & Apply)."; }
                } else if (f == 1) {
                    if (val.empty()) draft.alias = ax25::Callsign{};
                    else if (auto p = ax25::Callsign::parse(val)) draft.alias = *p;
                    else feedback = "Invalid alias '" + val + "'.";
                } else if (f == 2) {
                    draft.welcome_text = val;
                } else if (f == 3) {
                    draft.info_text = val;
                } else if (f == 6) {
                    draft.new_user_welcome = val;
                } else if (f == 7) {
                    draft.interfaces_file = val;
                } else if (f == 8) {
                    draft.peers_file = val;
                } else if (f == 9) {
                    draft.themes_file = val;
                }
            } else if (tab == 1) {
                if (f == 1) {
                    try {
                        int sv = std::stoi(val);
                        if (sv < 0 || sv > 15) feedback = "SSID must be 0..15.";
                        else draft.bbs.ssid = static_cast<uint8_t>(sv);
                    } catch (...) { feedback = "Invalid SSID number."; }
                } else if (f == 2) {
                    if (val.empty()) draft.bbs.callsign = ax25::Callsign{};
                    else if (auto p = node::NodeConfig::parse_callsign_with_default_ssid(val, draft.bbs.ssid)) draft.bbs.callsign = *p;
                    else feedback = "Invalid BBS callsign '" + val + "'.";
                } else if (f == 3) {
                    if (val.empty()) draft.bbs.alias = ax25::Callsign{};
                    else if (auto p = ax25::Callsign::parse(val)) draft.bbs.alias = *p;
                    else feedback = "Invalid BBS alias '" + val + "'.";
                } else if (f == 6) {
                    if (!val.empty()) draft.bbs.files_path = val;
                } else if (f == 7) {
                    if (!val.empty()) draft.bbs.mail_path = val;
                } else if (f == 8) {
                    draft.bbs.welcome = val;
                }
            } else if (tab == 2) {
                if (f == 1) {
                    try {
                        int sv = std::stoi(val);
                        if (sv < 0 || sv > 15) feedback = "SSID must be 0..15.";
                        else draft.chat.ssid = static_cast<uint8_t>(sv);
                    } catch (...) { feedback = "Invalid SSID number."; }
                } else if (f == 2) {
                    if (val.empty()) draft.chat.callsign = ax25::Callsign{};
                    else if (auto p = node::NodeConfig::parse_callsign_with_default_ssid(val, draft.chat.ssid)) draft.chat.callsign = *p;
                    else feedback = "Invalid CHAT callsign '" + val + "'.";
                } else if (f == 3) {
                    if (val.empty()) draft.chat.alias = ax25::Callsign{};
                    else if (auto p = ax25::Callsign::parse(val)) draft.chat.alias = *p;
                    else feedback = "Invalid CHAT alias '" + val + "'.";
                } else if (f == 4) {
                    if (!val.empty()) draft.chat.default_topic = val;
                } else if (f == 5) {
                    draft.chat.welcome = val;
                }
            } else if (tab == 5) {
                if (f == 2) {
                    if (!val.empty()) draft.rig.host = val;
                } else if (f == 3) {
                    try {
                        int pv = std::stoi(val);
                        if (pv < 1 || pv > 65535) feedback = "Port must be 1..65535.";
                        else draft.rig.port = static_cast<uint16_t>(pv);
                    } catch (...) { feedback = "Invalid port number."; }
                } else if (f == 4) {
                    try {
                        int iv = std::stoi(val);
                        if (iv < 0 || iv > 3600) feedback = "Interval must be 0..3600.";
                        else draft.rig.poll_interval_sec = static_cast<uint32_t>(iv);
                    } catch (...) { feedback = "Invalid interval."; }
                }
            } else if (tab == 6) {
                if (f == 1) {
                    draft.logging.printer.device = val;
                } else if (f == 3) {
                    try {
                        int bv = std::stoi(val);
                        if (bv < 300) feedback = "Baud rate must be >= 300.";
                        else draft.logging.printer.baud = static_cast<uint32_t>(bv);
                    } catch (...) { feedback = "Invalid baud rate."; }
                } else if (f == 4) {
                    uint32_t mask = 0;
                    if (!node::parse_log_mask(val, mask)) feedback = "Invalid log mask (e.g. 0x77 or ALL).";
                    else draft.logging.printer.level = mask;
                }
            }
        }
    }

    delwin(win);
}

namespace {

struct NcursesDriverOpt {
    const char* type;
    const char* desc;
};

static const NcursesDriverOpt NC_IFACE_DRIVERS[5] = {
    { "axudp",       "AX.25 over UDP (BPQ / AXUDP)" },
    { "axip",        "AX.25 over IP (Protocol 93)" },
    { "kiss-serial", "KISS TNC over Serial Port" },
    { "kiss-tcp",    "KISS TNC over TCP (Direwolf)" },
    { "telnet",      "Plaintext Telnet User Port" }
};

static bool nc_ci_eq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

static void nc_apply_driver_defaults(int drv,
                                     std::string& f_host,
                                     std::string& f_port,
                                     std::string& f_mode,
                                     std::string& f_kport,
                                     std::string& f_netmask,
                                     std::string& f_peer,
                                     bool& f_in,
                                     bool& f_out,
                                     bool& f_same,
                                     std::string& f_lpol) {
    const std::string t = NC_IFACE_DRIVERS[drv].type;
    f_peer.clear();
    f_netmask.clear();
    f_kport = "0";
    f_mode = "client";
    f_in = true;
    f_out = (t != "telnet");
    f_same = false;
    f_lpol = "inherit";
    if (t == "axudp") {
        f_host = "0.0.0.0";
        f_port = "10093";
    } else if (t == "axip") {
        f_host = "0.0.0.0";
        f_port = "0";
    } else if (t == "kiss-serial") {
        f_host = "/dev/ttyUSB0";
        f_port = "9600";
    } else if (t == "kiss-tcp") {
        f_host = "127.0.0.1";
        f_port = "8001";
    } else {
        f_host = "0.0.0.0";
        f_port = "8010";
    }
}

static std::string nc_cycle_learned_policy(const std::string& cur, int dir) {
    static const char* const POLS[5] = {"inherit", "none", "inbound", "outbound", "both"};
    int idx = 0;
    for (int i = 0; i < 5; ++i) {
        if (cur == POLS[i]) { idx = i; break; }
    }
    return POLS[(idx + dir + 5) % 5];
}

static bool nc_is_valid_ipv4(const std::string& ip) {
    struct in_addr a{};
    return inet_pton(AF_INET, ip.c_str(), &a) == 1;
}

static void nc_build_iface_config(int drv,
                                  const std::string& f_id,
                                  const std::string& f_desc,
                                  const std::string& f_host,
                                  const std::string& f_port,
                                  const std::string& f_mode,
                                  const std::string& f_kport,
                                  const std::string& f_netmask,
                                  const std::string& f_peer,
                                  bool f_in,
                                  bool f_out,
                                  bool f_same,
                                  const std::string& f_lpol,
                                  const radio::RadioInterfaceConfig* prev,
                                  radio::RadioInterfaceConfig& out_iface) {
    out_iface = radio::RadioInterfaceConfig{};
    out_iface.id = f_id;
    out_iface.type = NC_IFACE_DRIVERS[drv].type;
    out_iface.description = f_desc;
    out_iface.allow_inbound = f_in;
    out_iface.allow_outbound = (drv == 4) ? false : f_out;
    out_iface.allow_same_interface = (drv == 4) ? false : f_same;
    if (drv == 0) {
        out_iface.params["bind_host"] = f_host;
        out_iface.params["bind_port"] = f_port;
        if (!f_netmask.empty()) out_iface.params["netmask"] = f_netmask;
        out_iface.params["auto_learn"] = "true";
        if (!f_peer.empty()) out_iface.peers.push_back(f_peer);
    } else if (drv == 1) {
        out_iface.params["bind_host"] = f_host;
        if (!f_netmask.empty()) out_iface.params["netmask"] = f_netmask;
        out_iface.params["auto_learn"] = "true";
        if (!f_peer.empty()) out_iface.peers.push_back(f_peer);
    } else if (drv == 2) {
        out_iface.params["device"] = f_host;
        out_iface.params["baud"] = f_port;
        out_iface.params["kiss_port"] = f_kport.empty() ? "0" : f_kport;
    } else if (drv == 3) {
        out_iface.params["mode"] = f_mode;
        out_iface.params["host"] = f_host;
        out_iface.params["port"] = f_port;
        out_iface.params["kiss_port"] = f_kport.empty() ? "0" : f_kport;
    } else {
        out_iface.params["bind_host"] = f_host;
        out_iface.params["bind_port"] = f_port;
    }
    if (drv != 4 && !f_lpol.empty() && f_lpol != "inherit") {
        out_iface.params["learned_peer_policy"] = f_lpol;
    }
    if (prev && (drv == 0 || drv == 1)) {
        for (std::size_t p = 1; p < prev->peers.size(); ++p) {
            out_iface.peers.push_back(prev->peers[p]);
        }
    }
}

} // namespace

bool NcursesUI::modal_add_interface_wizard(const std::vector<radio::RadioInterfaceConfig>& existing,
                                             radio::RadioInterfaceConfig& out_iface) {
    static const char* PROG[4] = {
        "[##......] 1/4", "[####....] 2/4", "[######..] 3/4", "[########] 4/4"
    };

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(68, cols - 4);
    int h = std::min(20, std::max(18, rows - 2));
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* wiz = newwin(h, w, y, x);
    if (!wiz) return false;
    keypad(wiz, TRUE);
    wtimeout(wiz, 200);

    int step = 0;
    int drv = 0;
    int last_drv = -1;
    int focus = 0;
    int btn = 1; // 0=Back, 1=Next/Finish, 2=Cancel
    std::string f_id, f_desc, f_host, f_port, f_mode = "client", f_kport = "0", f_netmask, f_peer;
    std::string f_lpol = "inherit";
    bool f_in = true, f_out = true, f_same = false;
    std::string err_msg;
    bool committed = false;

    auto apply_driver_defaults = [&]() {
        f_id = node::derive_default_iface_id(existing, NC_IFACE_DRIVERS[drv].type);
        f_desc.clear();
        nc_apply_driver_defaults(drv, f_host, f_port, f_mode, f_kport, f_netmask, f_peer, f_in, f_out, f_same, f_lpol);
        last_drv = drv;
    };

    auto field_count_for_step = [&]() -> int {
        if (step == 1) return 2;
        if (step == 2) {
            if (drv == 0 || drv == 3) return 8;
            if (drv == 1 || drv == 2) return 7;
            return 3; // telnet: Bind Host, TCP Port, Allow Inbound (outbound & same-iface locked)
        }
        return 0;
    };

    auto active_text_field = [&](int fidx) -> std::string* {
        if (step == 1) {
            return (fidx == 0) ? &f_id : &f_desc;
        }
        if (step == 2) {
            if (drv == 0) {
                if (fidx == 0) return &f_host;
                if (fidx == 1) return &f_port;
                if (fidx == 2) return &f_netmask;
                if (fidx == 3) return &f_peer;
                return nullptr;
            }
            if (drv == 1) {
                if (fidx == 0) return &f_host;
                if (fidx == 1) return &f_netmask;
                if (fidx == 2) return &f_peer;
                return nullptr;
            }
            if (drv == 2) {
                if (fidx == 0) return &f_host;
                if (fidx == 1) return &f_port;
                if (fidx == 2) return &f_kport;
                return nullptr;
            }
            if (drv == 3) {
                if (fidx == 1) return &f_host;
                if (fidx == 2) return &f_port;
                if (fidx == 3) return &f_kport;
                return nullptr;
            }
            if (fidx == 0) return &f_host;
            if (fidx == 1) return &f_port;
            return nullptr;
        }
        return nullptr;
    };

    auto is_toggle_field = [&](int fidx) -> bool {
        if (step != 2) return false;
        if (drv == 0 || drv == 3) return fidx >= 4;
        if (drv == 1 || drv == 2) return fidx >= 3;
        return fidx == 2;
    };

    auto validate_and_advance = [&]() {
        err_msg.clear();
        if (step == 0) {
            if (drv != last_drv) apply_driver_defaults();
            step = 1;
            focus = 0;
            btn = 1;
            return;
        }
        if (step == 1) {
            if (f_id.empty()) {
                err_msg = "Interface ID cannot be empty.";
                focus = 0;
                return;
            }
            for (std::size_t i = 0; i < existing.size(); ++i) {
                if (nc_ci_eq(existing[i].id, f_id)) {
                    err_msg = "Duplicate interface ID: " + f_id;
                    focus = 0;
                    return;
                }
            }
            step = 2;
            focus = 0;
            btn = 1;
            return;
        }
        if (step == 2) {
            if (f_host.empty()) {
                err_msg = (drv == 2) ? "Serial device cannot be empty." : "Host cannot be empty.";
                focus = (drv == 3) ? 1 : 0;
                return;
            }
            if (drv != 1 && f_port.empty()) {
                err_msg = (drv == 2) ? "Baud rate cannot be empty." : "Port cannot be empty.";
                focus = (drv == 3) ? 2 : 1;
                return;
            }
            if ((drv == 0 || drv == 1) && radio::is_all_ascii_digits(f_host)) {
                uint32_t did = 0;
                if (!radio::parse_dmr_id(f_host, did)) {
                    err_msg = "Invalid DMR ID (allowed 1..16777214).";
                    focus = 0;
                    return;
                }
            }
            if ((drv == 0 || drv == 1) && !f_netmask.empty() && !nc_is_valid_ipv4(f_netmask)) {
                err_msg = "Invalid IPv4 netmask (e.g. 255.0.0.0).";
                focus = (drv == 0) ? 2 : 1;
                return;
            }
            step = 3;
            focus = 0;
            btn = 1;
            return;
        }
        if (step == 3) {
            nc_build_iface_config(drv, f_id, f_desc, f_host, f_port, f_mode, f_kport,
                                  f_netmask, f_peer, f_in, f_out, f_same, f_lpol, nullptr, out_iface);
            committed = true;
        }
    };

    while (!committed) {
        if (external_stop_flag_ && external_stop_flag_->load()) {
            committed = false;
            break;
        }

        werase(wiz);
        if (has_colors_) wattron(wiz, COLOR_PAIR(CP_BORDER));
        box(wiz, 0, 0);
        if (has_colors_) wattroff(wiz, COLOR_PAIR(CP_BORDER));

        wattron(wiz, A_BOLD);
        mvwprintw(wiz, 0, 2, " Add Interface ");
        std::string prog_str = std::string(" ") + PROG[step] + " ";
        if (w > static_cast<int>(prog_str.size()) + 20) {
            mvwaddnstr(wiz, 0, w - static_cast<int>(prog_str.size()) - 2,
                       prog_str.c_str(), static_cast<int>(prog_str.size()));
        }
        wattroff(wiz, A_BOLD);

        std::string step_prompt = "Select the driver for the new interface:";
        if (step == 1) {
            step_prompt = "Enter a unique interface ID and optional description:";
        } else if (step == 2) {
            if (drv == 0) step_prompt = "Configure AXUDP bind host, UDP port, and optional peer:";
            else if (drv == 1) step_prompt = "Configure AXIP bind host and optional static peer:";
            else if (drv == 2) step_prompt = "Configure KISS serial device, baud rate, and KISS port:";
            else if (drv == 3) step_prompt = "Configure KISS-TCP mode, host, TCP port, and KISS port:";
            else step_prompt = "Configure Telnet bind host and TCP port:";
        } else if (step == 3) {
            step_prompt = "Review interface configuration before committing:";
        }
        wattron(wiz, A_BOLD);
        mvwaddnstr(wiz, 1, 2, step_prompt.c_str(), w - 4);
        wattroff(wiz, A_BOLD);

        if (step == 0) {
            if (has_colors_) wattron(wiz, COLOR_PAIR(CP_MENU) | A_BOLD);
            else wattron(wiz, A_REVERSE | A_BOLD);
            mvwhline(wiz, 2, 1, ' ', w - 2);
            mvwaddnstr(wiz, 2, 2, "#  Driver          Description", w - 4);
            if (has_colors_) wattroff(wiz, COLOR_PAIR(CP_MENU) | A_BOLD);
            else wattroff(wiz, A_REVERSE | A_BOLD);
            for (int i = 0; i < 5; ++i) {
                bool hi = (i == drv);
                if (hi) wattron(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
                mvwhline(wiz, 3 + i, 1, ' ', w - 2);
                mvwprintw(wiz, 3 + i, 2, "%d. %-14s %-*s",
                          i + 1, NC_IFACE_DRIVERS[i].type,
                          std::max(1, w - 21), clip(NC_IFACE_DRIVERS[i].desc, std::max(1, w - 21)).c_str());
                if (hi) wattroff(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            }
        } else if (step == 1) {
            mvwprintw(wiz, 3, 3, "Driver       : %s", NC_IFACE_DRIVERS[drv].type);
            auto draw_f = [&](int row, int idx, const char* lbl, const std::string& val) {
                bool hi = (focus == idx);
                mvwprintw(wiz, row, 3, "%-13s: ", lbl);
                if (hi) wattron(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
                mvwprintw(wiz, row, 18, " %-*s ", std::max(10, w - 23),
                          clip(val, std::max(10, w - 23)).c_str());
                if (hi) wattroff(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            };
            draw_f(5, 0, "Interface ID", f_id);
            draw_f(7, 1, "Description", f_desc);
        } else if (step == 2) {
            mvwprintw(wiz, 2, 3, "Interface    : %s (%s)", f_id.c_str(), NC_IFACE_DRIVERS[drv].type);
            auto draw_f = [&](int row, int idx, const char* lbl, const std::string& val) {
                bool hi = (focus == idx);
                mvwprintw(wiz, row, 3, "%-16s: ", lbl);
                if (hi) wattron(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
                mvwprintw(wiz, row, 21, " %-*s ", std::max(10, w - 26),
                          clip(val, std::max(10, w - 26)).c_str());
                if (hi) wattroff(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            };
            if (drv == 0) {
                draw_f(4, 0, "Bind Host / DMR", f_host);
                draw_f(5, 1, "UDP Port", f_port);
                draw_f(6, 2, "Netmask (opt)", f_netmask);
                draw_f(7, 3, "Peer (opt)", f_peer);
                draw_f(8, 4, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
                draw_f(9, 5, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
                draw_f(10, 6, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
                draw_f(11, 7, "Learned Policy", std::string("< ") + f_lpol + " >");
            } else if (drv == 1) {
                draw_f(4, 0, "Bind Host / DMR", f_host);
                draw_f(5, 1, "Netmask (opt)", f_netmask);
                draw_f(6, 2, "Peer (opt)", f_peer);
                draw_f(7, 3, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
                draw_f(8, 4, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
                draw_f(9, 5, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
                draw_f(10, 6, "Learned Policy", std::string("< ") + f_lpol + " >");
            } else if (drv == 2) {
                draw_f(4, 0, "Serial Device", f_host);
                draw_f(5, 1, "Baud Rate", f_port);
                draw_f(6, 2, "KISS Port", f_kport);
                draw_f(7, 3, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
                draw_f(8, 4, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
                draw_f(9, 5, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
                draw_f(10, 6, "Learned Policy", std::string("< ") + f_lpol + " >");
            } else if (drv == 3) {
                draw_f(4, 0, "Mode (Space)", f_mode);
                draw_f(5, 1, "Host / IP", f_host);
                draw_f(6, 2, "TCP Port", f_port);
                draw_f(7, 3, "KISS Port", f_kport);
                draw_f(8, 4, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
                draw_f(9, 5, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
                draw_f(10, 6, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
                draw_f(11, 7, "Learned Policy", std::string("< ") + f_lpol + " >");
            } else {
                draw_f(4, 0, "Bind Host/IP", f_host);
                draw_f(5, 1, "TCP Port", f_port);
                draw_f(6, 2, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
                mvwprintw(wiz, 7, 3, "%-16s: [OFF] (disabled for telnet)", "Allow Outbound");
                mvwprintw(wiz, 8, 3, "%-16s: [OFF] (disabled for telnet)", "Same-Iface Fwd");
            }
        } else {
            mvwprintw(wiz, 3, 3, "Driver       : %s", NC_IFACE_DRIVERS[drv].type);
            mvwprintw(wiz, 4, 3, "Interface ID : %s", f_id.c_str());
            mvwprintw(wiz, 5, 3, "Description  : %s", f_desc.empty() ? "(none)" : clip(f_desc, w - 20).c_str());
            if (drv == 2) {
                mvwprintw(wiz, 6, 3, "Serial Device: %s", clip(f_host, w - 20).c_str());
                mvwprintw(wiz, 7, 3, "Baud / KISS  : %s bps (KISS port %s)", f_port.c_str(), f_kport.c_str());
            } else if (drv == 3) {
                mvwprintw(wiz, 6, 3, "Endpoint     : %s %s:%s", f_mode.c_str(), f_host.c_str(), f_port.c_str());
                mvwprintw(wiz, 7, 3, "KISS Port    : %s", f_kport.c_str());
            } else {
                mvwprintw(wiz, 6, 3, "Bind Host/IP : %s%s", f_host.c_str(),
                          f_netmask.empty() ? "" : (std::string(" / ") + f_netmask).c_str());
                if (drv != 1) mvwprintw(wiz, 7, 3, "Port         : %s", f_port.c_str());
                if (drv == 0 || drv == 1) {
                    mvwprintw(wiz, 8, 3, "Static Peer  : %s", f_peer.empty() ? "(auto-learn)" : clip(f_peer, w - 20).c_str());
                }
            }
            mvwprintw(wiz, 9, 3, "Policy       : In=%s  Out=%s  SameIf=%s  Learn=%s",
                      f_in ? "ON" : "OFF",
                      (drv == 4) ? "OFF(locked)" : (f_out ? "ON" : "OFF"),
                      (drv == 4) ? "OFF" : (f_same ? "ON" : "OFF"),
                      (drv == 4) ? "n/a" : f_lpol.c_str());
            wattron(wiz, A_BOLD);
            mvwaddnstr(wiz, 11, 3, "Select Finish to add this interface to Settings draft.", w - 6);
            wattroff(wiz, A_BOLD);
        }

        if (!err_msg.empty()) {
            if (has_colors_) wattron(wiz, COLOR_PAIR(CP_DANGER) | A_BOLD);
            else wattron(wiz, A_BOLD);
            mvwaddnstr(wiz, h - 4, 2, clip(err_msg, w - 4).c_str(), w - 4);
            if (has_colors_) wattroff(wiz, COLOR_PAIR(CP_DANGER) | A_BOLD);
            else wattroff(wiz, A_BOLD);
        }

        if (has_colors_) wattron(wiz, COLOR_PAIR(CP_BORDER));
        mvwhline(wiz, h - 3, 1, ACS_HLINE, w - 2);
        if (has_colors_) wattroff(wiz, COLOR_PAIR(CP_BORDER));

        int fcnt = field_count_for_step();
        bool on_bar = (step == 0 || step == 3 || focus == fcnt);
        int bx = 3;
        if (step > 0) {
            bool bhi = (on_bar && btn == 0);
            if (bhi) wattron(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            mvwaddstr(wiz, h - 2, bx, "[ < Back ]");
            if (bhi) wattroff(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        } else {
            mvwaddstr(wiz, h - 2, bx, "  < Back  ");
        }
        bx += 12;
        {
            bool nhi = (on_bar && btn == 1);
            if (nhi) wattron(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            mvwaddstr(wiz, h - 2, bx, (step == 3) ? "[ Finish ]" : "[ Next > ]");
            if (nhi) wattroff(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        }
        bx += 12;
        {
            bool chi = (on_bar && btn == 2);
            if (chi) wattron(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            mvwaddstr(wiz, h - 2, bx, "[ Cancel ]");
            if (chi) wattroff(wiz, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        }
        mvwaddnstr(wiz, h - 2, bx + 12, (step > 0) ? "Ctrl+B:Back" : "1-5:Driver",
                   std::max(0, w - (bx + 14)));

        curs_set((active_text_field(focus) != nullptr) ? 1 : 0);
        wrefresh(wiz);

        int ch = wgetch(wiz);
        if (ch == ERR) continue;
        if (ch == 27) {
            committed = false;
            break;
        }
        if (ch == 2 || ch == KEY_PPAGE || ch == KEY_F(3)) { // Ctrl+B / PgUp / F3 -> Back
            if (step > 0) {
                --step;
                focus = 0;
                btn = 1;
                err_msg.clear();
            }
            continue;
        }

        if (step == 0) {
            if (ch == KEY_UP) drv = (drv + 4) % 5;
            else if (ch == KEY_DOWN) drv = (drv + 1) % 5;
            else if (ch == KEY_HOME) drv = 0;
            else if (ch == KEY_END) drv = 4;
            else if (ch >= '1' && ch <= '5') drv = ch - '1';
            else if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == '\t') btn = (btn == 1) ? 2 : 1;
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (btn == 2) { committed = false; break; }
                validate_and_advance();
            }
            continue;
        }

        if (step == 3) {
            if (ch == KEY_LEFT || ch == KEY_BTAB) btn = (btn + 2) % 3;
            else if (ch == KEY_RIGHT || ch == '\t') btn = (btn + 1) % 3;
            else if (ch == 'b' || ch == 'B') {
                step = 2;
                focus = 0;
                btn = 1;
                err_msg.clear();
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
                if (btn == 0) {
                    step = 2;
                    focus = 0;
                    btn = 1;
                    err_msg.clear();
                } else if (btn == 2) {
                    committed = false;
                    break;
                } else {
                    validate_and_advance();
                }
            }
            continue;
        }

        // Steps 1 & 2 (fields + button bar)
        if (ch == KEY_UP || ch == KEY_BTAB) {
            focus = (focus + fcnt) % (fcnt + 1);
            continue;
        }
        if (ch == KEY_DOWN || ch == '\t') {
            focus = (focus + 1) % (fcnt + 1);
            continue;
        }
        if (focus == fcnt) {
            if (ch == KEY_LEFT) btn = (btn + 2) % 3;
            else if (ch == KEY_RIGHT) btn = (btn + 1) % 3;
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
                if (btn == 0) {
                    --step;
                    focus = 0;
                    btn = 1;
                    err_msg.clear();
                } else if (btn == 2) {
                    committed = false;
                    break;
                } else {
                    validate_and_advance();
                }
            }
            continue;
        }

        if (step == 2 && drv == 3 && focus == 0) {
            if (ch == ' ' || ch == KEY_LEFT || ch == KEY_RIGHT) {
                f_mode = (f_mode == "client") ? "server" : "client";
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_advance();
            }
            continue;
        }
        if (is_toggle_field(focus)) {
            if (ch == ' ' || ch == KEY_LEFT || ch == KEY_RIGHT) {
                int rel = focus - ((drv == 0 || drv == 3) ? 4 : ((drv == 1 || drv == 2) ? 3 : 2));
                if (rel == 0) f_in = !f_in;
                else if (rel == 1 && drv != 4) f_out = !f_out;
                else if (rel == 2 && drv != 4) f_same = !f_same;
                else if (rel == 3 && drv != 4) f_lpol = nc_cycle_learned_policy(f_lpol, (ch == KEY_LEFT) ? -1 : 1);
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_advance();
            }
            continue;
        }

        std::string* fld = active_text_field(focus);
        if (fld) {
            std::size_t cap = (step == 1 && focus == 0) ? 15 : 48;
            if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (!fld->empty()) fld->pop_back();
                err_msg.clear();
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_advance();
            } else if (ch >= 32 && ch < 127 && fld->size() < cap) {
                fld->push_back(static_cast<char>(ch));
                err_msg.clear();
            }
        }
    }

    curs_set(0);
    delwin(wiz);
    return committed;
}

bool NcursesUI::modal_edit_interface(const std::vector<radio::RadioInterfaceConfig>& existing,
                                     int edit_idx,
                                     radio::RadioInterfaceConfig& out_iface) {
    if (edit_idx < 0 || static_cast<std::size_t>(edit_idx) >= existing.size()) {
        return false;
    }

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(68, cols - 4);
    int h = std::min(20, std::max(18, rows - 2));
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* dlg = newwin(h, w, y, x);
    if (!dlg) return false;
    keypad(dlg, TRUE);
    wtimeout(dlg, 200);

    const radio::RadioInterfaceConfig& src = existing[static_cast<std::size_t>(edit_idx)];
    auto get_p = [&](const char* k, const char* fallback) -> std::string {
        auto it = src.params.find(k);
        return (it != src.params.end() && !it->second.empty()) ? it->second : fallback;
    };

    int drv = 0;
    for (int i = 0; i < 5; ++i) {
        if (nc_ci_eq(src.type, NC_IFACE_DRIVERS[i].type)) { drv = i; break; }
    }
    std::string f_id = src.id;
    std::string f_desc = src.description;
    std::string f_mode = "client";
    std::string f_kport = "0";
    std::string f_netmask = get_p("netmask", "");
    std::string f_lpol = get_p("learned_peer_policy", "inherit");
    bool f_in = src.allow_inbound;
    bool f_out = (drv == 4) ? false : src.allow_outbound;
    bool f_same = (drv == 4) ? false : src.allow_same_interface;
    std::string f_host, f_port, f_peer;
    if (drv == 2) {
        f_host = get_p("device", "/dev/ttyUSB0");
        f_port = get_p("baud", "9600");
        f_kport = get_p("kiss_port", "0");
    } else if (drv == 3) {
        f_mode = get_p("mode", "client");
        f_host = get_p("host", "127.0.0.1");
        f_port = get_p("port", "8001");
        f_kport = get_p("kiss_port", "0");
    } else {
        f_host = get_p("bind_host", "0.0.0.0");
        if (drv == 0) f_port = get_p("bind_port", "10093");
        else if (drv == 4) f_port = get_p("bind_port", "8010");
        else f_port = "0";
        if (!src.peers.empty() && (drv == 0 || drv == 1)) f_peer = src.peers[0];
    }

    int focus = 0;
    int btn = 0; // 0=Save, 1=Cancel
    std::string err_msg;
    bool committed = false;

    auto field_count = [&]() -> int {
        if (drv == 0 || drv == 3) return 11;
        if (drv == 1 || drv == 2) return 10;
        return 6; // telnet: 0..4 + Allow Inbound(5)
    };

    auto active_text_field = [&](int fidx) -> std::string* {
        if (fidx == 0) return nullptr;
        if (fidx == 1) return &f_id;
        if (fidx == 2) return &f_desc;
        if (drv == 0) {
            if (fidx == 3) return &f_host;
            if (fidx == 4) return &f_port;
            if (fidx == 5) return &f_netmask;
            if (fidx == 6) return &f_peer;
            return nullptr;
        }
        if (drv == 1) {
            if (fidx == 3) return &f_host;
            if (fidx == 4) return &f_netmask;
            if (fidx == 5) return &f_peer;
            return nullptr;
        }
        if (drv == 2) {
            if (fidx == 3) return &f_host;
            if (fidx == 4) return &f_port;
            if (fidx == 5) return &f_kport;
            return nullptr;
        }
        if (drv == 3) {
            if (fidx == 4) return &f_host;
            if (fidx == 5) return &f_port;
            if (fidx == 6) return &f_kport;
            return nullptr;
        }
        if (fidx == 3) return &f_host;
        if (fidx == 4) return &f_port;
        return nullptr;
    };

    auto validate_and_save = [&]() {
        err_msg.clear();
        if (f_id.empty()) {
            err_msg = "Interface ID cannot be empty.";
            focus = 1;
            return;
        }
        for (std::size_t i = 0; i < existing.size(); ++i) {
            if (i == static_cast<std::size_t>(edit_idx)) continue;
            if (nc_ci_eq(existing[i].id, f_id)) {
                err_msg = "Duplicate interface ID: " + f_id;
                focus = 1;
                return;
            }
        }
        if (f_host.empty()) {
            err_msg = (drv == 2) ? "Serial device cannot be empty." : "Host cannot be empty.";
            focus = (drv == 3) ? 4 : 3;
            return;
        }
        if (drv != 1 && f_port.empty()) {
            err_msg = (drv == 2) ? "Baud rate cannot be empty." : "Port cannot be empty.";
            focus = (drv == 3) ? 5 : 4;
            return;
        }
        if ((drv == 0 || drv == 1) && radio::is_all_ascii_digits(f_host)) {
            uint32_t did = 0;
            if (!radio::parse_dmr_id(f_host, did)) {
                err_msg = "Invalid DMR ID (allowed 1..16777214).";
                focus = 3;
                return;
            }
        }
        if ((drv == 0 || drv == 1) && !f_netmask.empty() && !nc_is_valid_ipv4(f_netmask)) {
            err_msg = "Invalid IPv4 netmask (e.g. 255.0.0.0).";
            focus = (drv == 0) ? 5 : 4;
            return;
        }
        nc_build_iface_config(drv, f_id, f_desc, f_host, f_port, f_mode, f_kport,
                              f_netmask, f_peer, f_in, f_out, f_same, f_lpol, &src, out_iface);
        committed = true;
    };

    while (!committed) {
        if (external_stop_flag_ && external_stop_flag_->load()) {
            committed = false;
            break;
        }

        werase(dlg);
        if (has_colors_) wattron(dlg, COLOR_PAIR(CP_BORDER));
        box(dlg, 0, 0);
        if (has_colors_) wattroff(dlg, COLOR_PAIR(CP_BORDER));

        wattron(dlg, A_BOLD);
        mvwprintw(dlg, 0, 2, " Edit Interface ");
        mvwaddnstr(dlg, 1, 2, "Edit interface settings and select Save to update draft:", w - 4);
        wattroff(dlg, A_BOLD);

        auto draw_row = [&](int row, int idx, const char* lbl, const std::string& val) {
            bool hi = (focus == idx);
            mvwprintw(dlg, row, 3, "%-16s: ", lbl);
            if (hi) wattron(dlg, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            mvwprintw(dlg, row, 21, " %-*s ", std::max(10, w - 26),
                      clip(val, std::max(10, w - 26)).c_str());
            if (hi) wattroff(dlg, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        };

        draw_row(3, 0, "Driver (Lt/Rt)", std::string("< ") + NC_IFACE_DRIVERS[drv].type + " >");
        draw_row(4, 1, "Interface ID", f_id);
        draw_row(5, 2, "Description", f_desc);
        if (drv == 0) {
            draw_row(6, 3, "Bind Host / DMR", f_host);
            draw_row(7, 4, "UDP Port", f_port);
            draw_row(8, 5, "Netmask (opt)", f_netmask);
            draw_row(9, 6, "Peer (opt)", f_peer);
            draw_row(10, 7, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
            draw_row(11, 8, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
            draw_row(12, 9, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
            draw_row(13, 10, "Learned Policy", std::string("< ") + f_lpol + " >");
        } else if (drv == 1) {
            draw_row(6, 3, "Bind Host / DMR", f_host);
            draw_row(7, 4, "Netmask (opt)", f_netmask);
            draw_row(8, 5, "Peer (opt)", f_peer);
            draw_row(9, 6, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
            draw_row(10, 7, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
            draw_row(11, 8, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
            draw_row(12, 9, "Learned Policy", std::string("< ") + f_lpol + " >");
        } else if (drv == 2) {
            draw_row(6, 3, "Serial Device", f_host);
            draw_row(7, 4, "Baud Rate", f_port);
            draw_row(8, 5, "KISS Port", f_kport);
            draw_row(9, 6, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
            draw_row(10, 7, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
            draw_row(11, 8, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
            draw_row(12, 9, "Learned Policy", std::string("< ") + f_lpol + " >");
        } else if (drv == 3) {
            draw_row(6, 3, "Mode (Lt/Rt/Sp)", std::string("< ") + f_mode + " >");
            draw_row(7, 4, "Host / IP", f_host);
            draw_row(8, 5, "TCP Port", f_port);
            draw_row(9, 6, "KISS Port", f_kport);
            draw_row(10, 7, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
            draw_row(11, 8, "Allow Outbound", f_out ? "[ON]" : "[OFF]");
            draw_row(12, 9, "Same-Iface Fwd", f_same ? "[ON]" : "[OFF]");
            draw_row(13, 10, "Learned Policy", std::string("< ") + f_lpol + " >");
        } else {
            draw_row(6, 3, "Bind Host/IP", f_host);
            draw_row(7, 4, "TCP Port", f_port);
            draw_row(8, 5, "Allow Inbound", f_in ? "[ON]" : "[OFF]");
            mvwprintw(dlg, 9, 3, "%-16s: [OFF] (disabled for telnet)", "Allow Outbound");
            mvwprintw(dlg, 10, 3, "%-16s: [OFF] (disabled for telnet)", "Same-Iface Fwd");
        }

        if (!err_msg.empty()) {
            if (has_colors_) wattron(dlg, COLOR_PAIR(CP_DANGER) | A_BOLD);
            else wattron(dlg, A_BOLD);
            mvwaddnstr(dlg, h - 4, 2, clip(err_msg, w - 4).c_str(), w - 4);
            if (has_colors_) wattroff(dlg, COLOR_PAIR(CP_DANGER) | A_BOLD);
            else wattroff(dlg, A_BOLD);
        }

        if (has_colors_) wattron(dlg, COLOR_PAIR(CP_BORDER));
        mvwhline(dlg, h - 3, 1, ACS_HLINE, w - 2);
        if (has_colors_) wattroff(dlg, COLOR_PAIR(CP_BORDER));

        int fcnt = field_count();
        bool on_bar = (focus == fcnt);
        {
            bool shi = (on_bar && btn == 0);
            if (shi) wattron(dlg, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            mvwaddstr(dlg, h - 2, 3, "[  Save  ]");
            if (shi) wattroff(dlg, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        }
        {
            bool chi = (on_bar && btn == 1);
            if (chi) wattron(dlg, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
            mvwaddstr(dlg, h - 2, 16, "[ Cancel ]");
            if (chi) wattroff(dlg, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        }
        mvwaddnstr(dlg, h - 2, 28, "Up/Dn:Field  Enter:Save  Esc:Cancel",
                   std::max(0, w - 30));

        curs_set((focus < fcnt && active_text_field(focus) != nullptr) ? 1 : 0);
        wrefresh(dlg);

        int ch = wgetch(dlg);
        if (ch == ERR) continue;
        if (ch == 27) {
            committed = false;
            break;
        }
        if (ch == KEY_UP || ch == KEY_BTAB) {
            focus = (focus + fcnt) % (fcnt + 1);
            continue;
        }
        if (ch == KEY_DOWN || ch == '\t') {
            focus = (focus + 1) % (fcnt + 1);
            continue;
        }
        if (focus == fcnt) {
            if (ch == KEY_LEFT || ch == KEY_RIGHT) btn = (btn == 0) ? 1 : 0;
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
                if (btn == 1) {
                    committed = false;
                    break;
                }
                validate_and_save();
            }
            continue;
        }
        if (focus == 0) {
            if (ch == KEY_LEFT) {
                drv = (drv + 4) % 5;
                nc_apply_driver_defaults(drv, f_host, f_port, f_mode, f_kport, f_netmask, f_peer, f_in, f_out, f_same, f_lpol);
                err_msg.clear();
            } else if (ch == KEY_RIGHT || ch == ' ') {
                drv = (drv + 1) % 5;
                nc_apply_driver_defaults(drv, f_host, f_port, f_mode, f_kport, f_netmask, f_peer, f_in, f_out, f_same, f_lpol);
                err_msg.clear();
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_save();
            }
            continue;
        }
        if (drv == 3 && focus == 3) {
            if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == ' ') {
                f_mode = (f_mode == "client") ? "server" : "client";
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_save();
            }
            continue;
        }
        {
            int toggle_base = (drv == 0 || drv == 3) ? 7 : ((drv == 1 || drv == 2) ? 6 : 5);
            if (focus >= toggle_base && focus < fcnt) {
                if (ch == ' ' || ch == KEY_LEFT || ch == KEY_RIGHT) {
                    int rel = focus - toggle_base;
                    if (rel == 0) f_in = !f_in;
                    else if (rel == 1 && drv != 4) f_out = !f_out;
                    else if (rel == 2 && drv != 4) f_same = !f_same;
                    else if (rel == 3 && drv != 4) f_lpol = nc_cycle_learned_policy(f_lpol, (ch == KEY_LEFT) ? -1 : 1);
                } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                    validate_and_save();
                }
                continue;
            }
        }
        std::string* fld = active_text_field(focus);
        if (fld) {
            std::size_t cap = (focus == 1) ? 15 : 48;
            if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (!fld->empty()) fld->pop_back();
                err_msg.clear();
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_save();
            } else if (ch >= 32 && ch < 127 && fld->size() < cap) {
                fld->push_back(static_cast<char>(ch));
                err_msg.clear();
            }
        }
    }

    curs_set(0);
    delwin(dlg);
    return committed;
}

bool NcursesUI::modal_edit_peer(const std::vector<radio::PeerConfig>& existing,
                                const std::vector<radio::RadioInterfaceConfig>& ifaces,
                                int edit_idx,
                                radio::PeerConfig& io_peer) {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    const int w = std::min(68, cols - 4);
    const int h = 16;
    if (w < 50 || rows < h + 2) return false;
    const int y = std::max(1, (rows - h) / 2);
    const int x = std::max(1, (cols - w) / 2);

    WINDOW* dlg = newwin(h, w, y, x);
    if (!dlg) return false;
    keypad(dlg, TRUE);
    wtimeout(dlg, 200);

    std::vector<std::string> valid_ifaces;
    for (const auto& ic : ifaces) {
        if (ic.type != "telnet" && ic.type != "local") {
            valid_ifaces.push_back(ic.id);
        }
    }

    std::string f_id = io_peer.id;
    std::string f_call = io_peer.callsign;
    std::string f_desc = io_peer.description;
    std::string f_iface = io_peer.interface_id;
    if (f_iface.empty() && !valid_ifaces.empty()) f_iface = valid_ifaces[0];
    std::string f_host = io_peer.host;
    std::string f_port = (io_peer.port > 0) ? std::to_string(io_peer.port) : "";
    bool f_in = io_peer.allow_inbound;
    bool f_out = io_peer.allow_outbound;
    bool id_custom = (edit_idx >= 0) || !f_id.empty();

    int focus = 1; // Callsign first when adding
    if (edit_idx >= 0) focus = 0;
    int btn = 0;
    bool committed = false;
    std::string err_msg;

    auto validate_and_save = [&]() {
        if (f_call.empty()) {
            err_msg = "Callsign cannot be empty.";
            return;
        }
        auto parsed = ax25::Callsign::parse(f_call);
        if (!parsed) {
            err_msg = "Invalid AX.25 callsign '" + f_call + "'.";
            return;
        }
        if (f_id.empty()) {
            f_id = node::derive_default_peer_id(existing, parsed->str());
        }
        for (std::size_t i = 0; i < existing.size(); ++i) {
            if (static_cast<int>(i) == edit_idx) continue;
            if (nc_ci_eq(existing[i].id, f_id)) {
                err_msg = "Peer ID '" + f_id + "' already exists.";
                return;
            }
        }
        uint16_t pnum = 0;
        if (!f_port.empty()) {
            try {
                int pv = std::stoi(f_port);
                if (pv < 0 || pv > 65535) {
                    err_msg = "Port must be 0..65535.";
                    return;
                }
                pnum = static_cast<uint16_t>(pv);
            } catch (...) {
                err_msg = "Invalid port number.";
                return;
            }
        }
        if (!f_host.empty() && radio::is_all_ascii_digits(f_host)) {
            uint32_t did = 0;
            if (!radio::parse_dmr_id(f_host, did)) {
                err_msg = "Invalid DMR ID (allowed 1..16777214).";
                focus = 4;
                return;
            }
        }
        io_peer.id = f_id;
        io_peer.callsign = parsed->str();
        io_peer.description = f_desc;
        io_peer.interface_id = f_iface;
        io_peer.host = f_host;
        io_peer.port = pnum;
        io_peer.allow_inbound = f_in;
        io_peer.allow_outbound = f_out;
        io_peer.auto_learned = false;
        committed = true;
    };

    static const char* LABELS[8] = {
        "Peer ID",
        "Callsign",
        "Description",
        "Interface",
        "Host / DMR ID",
        "Port (0=default)",
        "Allow Inbound",
        "Allow Outbound"
    };

    while (!committed) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        werase(dlg);
        box(dlg, 0, 0);
        mvwprintw(dlg, 0, 2, (edit_idx >= 0) ? " Edit Peer " : " Add Peer ");

        auto val_for = [&](int r) -> std::string {
            switch (r) {
                case 0: return f_id;
                case 1: return f_call;
                case 2: return f_desc;
                case 3: return f_iface.empty() ? "(any)" : ("< " + f_iface + " >");
                case 4: return f_host;
                case 5: return f_port.empty() ? "(default)" : f_port;
                case 6: return f_in ? "[ON]" : "[OFF]";
                case 7: return f_out ? "[ON]" : "[OFF]";
                default: return "";
            }
        };

        for (int r = 0; r < 8; ++r) {
            std::ostringstream row;
            row << std::left << std::setw(18) << LABELS[r] << ": " << val_for(r);
            if (focus == r) wattron(dlg, A_REVERSE | A_BOLD);
            mvwaddnstr(dlg, 2 + r, 2, clip(row.str(), w - 4).c_str(), w - 4);
            if (focus == r) wattroff(dlg, A_REVERSE | A_BOLD);
        }

        if (has_colors_) wattron(dlg, COLOR_PAIR(CP_WARN));
        mvwaddnstr(dlg, 11, 2, clip(err_msg, w - 4).c_str(), w - 4);
        if (has_colors_) wattroff(dlg, COLOR_PAIR(CP_WARN));

        const std::string b0 = "[ Save ]";
        const std::string b1 = "[ Cancel ]";
        int bx = std::max(2, (w - static_cast<int>(b0.size() + 4 + b1.size())) / 2);
        if (focus == 8 && btn == 0) wattron(dlg, A_REVERSE | A_BOLD);
        mvwaddnstr(dlg, 12, bx, b0.c_str(), w - bx - 2);
        if (focus == 8 && btn == 0) wattroff(dlg, A_REVERSE | A_BOLD);
        if (focus == 8 && btn == 1) wattron(dlg, A_REVERSE | A_BOLD);
        mvwaddnstr(dlg, 12, bx + static_cast<int>(b0.size()) + 4, b1.c_str(), w - bx - 16);
        if (focus == 8 && btn == 1) wattroff(dlg, A_REVERSE | A_BOLD);

        if (has_colors_) wattron(dlg, COLOR_PAIR(CP_MENU));
        mvwhline(dlg, h - 1, 1, ' ', w - 2);
        mvwaddnstr(dlg, h - 1, 2, "Up/Down/Tab:Field  Left/Right/Space:Toggle  Enter:Save  Esc:Cancel", w - 4);
        if (has_colors_) wattroff(dlg, COLOR_PAIR(CP_MENU));
        curs_set((focus == 0 || focus == 1 || focus == 2 || focus == 4 || focus == 5) ? 1 : 0);
        wrefresh(dlg);

        int ch = wgetch(dlg);
        if (ch == ERR) continue;
        if (ch == 27) { committed = false; break; }
        if (ch == KEY_UP || ch == KEY_BTAB) { focus = (focus + 8) % 9; continue; }
        if (ch == KEY_DOWN || ch == '\t') { focus = (focus + 1) % 9; continue; }

        if (focus == 8) {
            if (ch == KEY_LEFT || ch == KEY_RIGHT) btn = 1 - btn;
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
                if (btn == 1) { committed = false; break; }
                validate_and_save();
            }
            continue;
        }
        if (focus == 3) {
            if (!valid_ifaces.empty() && (ch == KEY_LEFT || ch == KEY_RIGHT || ch == ' ')) {
                int idx = 0;
                for (std::size_t i = 0; i < valid_ifaces.size(); ++i) {
                    if (valid_ifaces[i] == f_iface) { idx = static_cast<int>(i); break; }
                }
                idx = (ch == KEY_LEFT) ? (idx + static_cast<int>(valid_ifaces.size()) - 1) % static_cast<int>(valid_ifaces.size())
                                       : (idx + 1) % static_cast<int>(valid_ifaces.size());
                f_iface = valid_ifaces[static_cast<std::size_t>(idx)];
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_save();
            }
            continue;
        }
        if (focus == 6 || focus == 7) {
            if (ch == ' ' || ch == KEY_LEFT || ch == KEY_RIGHT) {
                if (focus == 6) f_in = !f_in;
                else f_out = !f_out;
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                validate_and_save();
            }
            continue;
        }

        std::string* fld = (focus == 0) ? &f_id
                         : (focus == 1) ? &f_call
                         : (focus == 2) ? &f_desc
                         : (focus == 4) ? &f_host
                                        : &f_port;
        std::size_t cap = (focus == 0 || focus == 1) ? 16 : ((focus == 5) ? 5 : 48);
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!fld->empty()) fld->pop_back();
            if (focus == 0) id_custom = !f_id.empty();
            if (focus == 1 && !id_custom) {
                f_id = f_call.empty() ? std::string() : node::derive_default_peer_id(existing, f_call);
            }
            err_msg.clear();
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            validate_and_save();
        } else if (ch >= 32 && ch < 127 && fld->size() < cap) {
            fld->push_back(static_cast<char>(ch));
            if (focus == 0) id_custom = true;
            if (focus == 1 && !id_custom) {
                f_id = node::derive_default_peer_id(existing, f_call);
            }
            err_msg.clear();
        }
    }

    curs_set(0);
    delwin(dlg);
    return committed;
}

bool NcursesUI::modal_confirm_exit() {
    const std::size_t auto_cnt = engine_.auto_learned_peer_count();
    if (auto_cnt > 0) {
        int rows = 0, cols = 0;
        getmaxyx(stdscr, rows, cols);
        int w = std::min(72, cols - 4);
        int h = 11;
        int y = std::max(1, (rows - h) / 2);
        int x = std::max(1, (cols - w) / 2);

        WINDOW* win = newwin(h, w, y, x);
        if (!win) return false;
        keypad(win, TRUE);
        wtimeout(win, 200);

        enum class AutoExitAct { Cancel, SaveExit, DiscardExit, Review };
        AutoExitAct act = AutoExitAct::Cancel;
        int focus = 2; // Default [ Review Peers ]
        static const char* BTNS[4] = { "[ Save & Exit ]", "[ Discard & Exit ]", "[ Review Peers ]", "[ Cancel ]" };
        while (true) {
            if (external_stop_flag_ && external_stop_flag_->load()) { act = AutoExitAct::DiscardExit; break; }
            werase(win);
            if (has_colors_) wattron(win, COLOR_PAIR(CP_BORDER));
            box(win, 0, 0);
            if (has_colors_) wattroff(win, COLOR_PAIR(CP_BORDER));
            wattron(win, A_BOLD);
            mvwprintw(win, 0, 2, " Auto-Learned Peers ");
            wattroff(win, A_BOLD);

            std::string l1 = std::to_string(auto_cnt) + " auto-learned peer(s) added this session.";
            mvwaddnstr(win, 2, 3, l1.c_str(), w - 6);
            mvwaddnstr(win, 3, 3, "Save them to config, discard them, or review in Peers?", w - 6);

            int bx = 3;
            for (int i = 0; i < 4; ++i) {
                if (focus == i) wattron(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
                mvwaddnstr(win, 6, bx, BTNS[i], w - bx - 2);
                if (focus == i) wattroff(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
                bx += static_cast<int>(std::string(BTNS[i]).size()) + 2;
            }
            mvwhline(win, 8, 1, ACS_HLINE, w - 2);
            mvwaddnstr(win, 9, 2, "Left/Right/Tab:Select  Enter:Confirm  S/D/R/Esc", w - 4);
            wrefresh(win);

            int ch = wgetch(win);
            if (ch == ERR) continue;
            if (ch == KEY_LEFT || ch == KEY_BTAB) focus = (focus + 3) % 4;
            else if (ch == KEY_RIGHT || ch == '\t') focus = (focus + 1) % 4;
            else if (ch == 27 || ch == 'c' || ch == 'C') { act = AutoExitAct::Cancel; break; }
            else if (ch == 's' || ch == 'S') { act = AutoExitAct::SaveExit; break; }
            else if (ch == 'd' || ch == 'D') { act = AutoExitAct::DiscardExit; break; }
            else if (ch == 'r' || ch == 'R') { act = AutoExitAct::Review; break; }
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
                act = (focus == 0) ? AutoExitAct::SaveExit
                    : (focus == 1) ? AutoExitAct::DiscardExit
                    : (focus == 2) ? AutoExitAct::Review
                                   : AutoExitAct::Cancel;
                break;
            }
        }
        delwin(win);
        if (act == AutoExitAct::Cancel) return false;
        if (act == AutoExitAct::Review) {
            modal_settings(4);
            return false;
        }
        if (act == AutoExitAct::SaveExit) {
            std::string err;
            if (!engine_.persist_auto_learned_peers(&err)) {
                set_status("Save failed: " + err);
            }
            return true;
        }
        engine_.discard_auto_learned_peers();
        return true;
    }

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(56, cols - 4);
    int h = 9;
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return false;
    keypad(win, TRUE);
    wtimeout(win, 200);

    bool confirmed = false;
    int focus = 1; // Default to [ No / Cancel ]
    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) {
            confirmed = true;
            break;
        }

        auto snap = engine_.snapshot();
        werase(win);
        if (has_colors_) wattron(win, COLOR_PAIR(CP_BORDER));
        box(win, 0, 0);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_BORDER));

        wattron(win, A_BOLD);
        mvwprintw(win, 0, 2, " Exit OYK Node ");
        wattroff(win, A_BOLD);

        if (snap.total_active_connections > 0) {
            std::string l1 = "Exit OYK Node? (" + std::to_string(snap.total_active_connections) +
                             " active connection" +
                             (snap.total_active_connections == 1 ? ")" : "s)");
            mvwaddnstr(win, 2, 3, l1.c_str(), w - 6);
            mvwaddnstr(win, 3, 3, "Active sessions will be gracefully disconnected.", w - 6);
        } else {
            mvwaddnstr(win, 2, 3, "Are you sure you want to exit OYK Node?", w - 6);
            mvwaddnstr(win, 3, 3, "(No active connections)", w - 6);
        }

        const std::string yes_lbl = "[ Yes / Exit ]";
        const std::string no_lbl  = "[ No / Cancel ]";
        const int btn_total = static_cast<int>(yes_lbl.size() + 4 + no_lbl.size());
        const int bx = std::max(2, (w - btn_total) / 2);

        if (focus == 0) wattron(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        mvwaddnstr(win, 5, bx, yes_lbl.c_str(), w - bx - 2);
        if (focus == 0) wattroff(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);

        if (focus == 1) wattron(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);
        mvwaddnstr(win, 5, bx + static_cast<int>(yes_lbl.size()) + 4, no_lbl.c_str(),
                   w - (bx + static_cast<int>(yes_lbl.size()) + 4) - 2);
        if (focus == 1) wattroff(win, (has_colors_ ? COLOR_PAIR(CP_MENU_SEL) : A_REVERSE) | A_BOLD);

        if (has_colors_) wattron(win, COLOR_PAIR(CP_BORDER));
        mvwhline(win, 6, 1, ACS_HLINE, w - 2);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_BORDER));
        mvwaddnstr(win, 7, 2, "Left/Right/Tab:Select  Enter:OK  Y/N/Esc", w - 4);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == '\t' || ch == KEY_BTAB) {
            focus = 1 - focus;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
            confirmed = (focus == 0);
            break;
        } else if (ch == 'y' || ch == 'Y') {
            confirmed = true;
            break;
        } else if (ch == 'n' || ch == 'N' || ch == 27) {
            confirmed = false;
            break;
        }
    }

    delwin(win);
    return confirmed;
}

} // namespace oyk::ui
