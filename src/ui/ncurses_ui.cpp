// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ui/ncurses_ui.hpp"
#include "oyk/protocol/yapp.hpp"

#include <ncurses.h>

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
        init_pair(CP_HEADER, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_OK, COLOR_GREEN, -1);
        init_pair(CP_WARN, COLOR_YELLOW, -1);
        init_pair(CP_DANGER, COLOR_RED, -1);
        init_pair(CP_MENU, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_MENU_SEL, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_BORDER, COLOR_CYAN, -1);
        init_pair(CP_NC_PANEL, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_NC_DIR, COLOR_YELLOW, COLOR_BLUE);
        init_pair(CP_NC_SEL, COLOR_BLACK, COLOR_CYAN);
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
    static const char* ITEMS[7] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [L] Local Terminal...          ",
        " [F] Approve Incoming Files...  ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [X] Exit                       "
    };
    // Row offsets inside menu window (with separators after Deny and Ban)
    static const int ITEM_ROWS[7] = { 1, 2, 4, 5, 6, 7, 9 };

    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    const int w = std::min(36, cols - 2);
    const int h = 11;
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
        mvwhline(win, 8, 1, ACS_HLINE, w - 2);

        for (int i = 0; i < 7; ++i) {
            if (i == sel) wattron(win, A_REVERSE | A_BOLD);
            mvwaddnstr(win, ITEM_ROWS[i], 1, clip(ITEMS[i], w - 2).c_str(), w - 2);
            if (i == sel) wattroff(win, A_REVERSE | A_BOLD);
        }
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27 || ch == KEY_F(10)) break;
        if (ch == KEY_UP) {
            sel = (sel + 6) % 7;
        } else if (ch == KEY_DOWN) {
            sel = (sel + 1) % 7;
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
        } else if (ch == 't' || ch == 'T') {
            chosen = 4;
        } else if (ch == 'b' || ch == 'B') {
            chosen = 5;
        } else if (ch == 'x' || ch == 'X' || ch == 'q' || ch == 'Q') {
            chosen = 6;
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
            modal_terminate_connection();
            break;
        case 5:
            modal_ban_callsign();
            break;
        case 6:
            if (modal_confirm_exit()) {
                should_exit = true;
            }
            break;
        default:
            break;
    }
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
    int w = std::min(76, std::max(54, cols - 4));
    int h = std::min(22, std::max(14, rows - 2));
    if (rows < 12 || cols < 46) return;

    WINDOW* win = newwin(h, w, (rows - h) / 2, (cols - w) / 2);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    auto items = engine_.list_pending_incoming_files();
    int sel = 0;
    int top = 0;
    std::string feedback = "Target BBS Files Area: " + engine_.bbs_files_path();

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

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;
        int list_h = std::max(1, h - 7);
        int count = static_cast<int>(items.size());
        if (sel >= count) sel = std::max(0, count - 1);
        if (sel < top) top = sel;
        if (sel >= top + list_h) top = sel - list_h + 1;

        werase(win);
        box(win, 0, 0);
        if (has_colors_) wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
        mvwaddnstr(win, 0, 2, " Approve Incoming Files (incoming/ -> BBS files/) ", w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);

        mvwaddnstr(win, 1, 2,
                   clip("Incoming: " + engine_.bbs_incoming_path() + "  ->  Files Area: " +
                        engine_.bbs_files_path(), w - 4).c_str(),
                   w - 4);
        mvwhline(win, 2, 1, ACS_HLINE, w - 2);

        std::ostringstream hdr;
        hdr << std::left << std::setw(14) << "Uploader"
            << std::setw(std::max(16, w - 30)) << "Filename"
            << std::right << std::setw(10) << "Size";
        wattron(win, A_BOLD);
        mvwaddnstr(win, 3, 2, clip(hdr.str(), w - 4).c_str(), w - 4);
        wattroff(win, A_BOLD);

        if (items.empty()) {
            mvwaddnstr(win, 5, 2, "(No pending files in incoming/<CALLSIGN-SSID>/)", w - 4);
        } else {
            for (int r = 0; r < list_h; ++r) {
                int idx = top + r;
                if (idx >= count) break;
                const auto& pf = items[static_cast<std::size_t>(idx)];
                std::ostringstream row;
                row << std::left << std::setw(14) << clip(pf.uploader, 13)
                    << std::setw(std::max(16, w - 30)) << clip(pf.filename, std::max(15, w - 31))
                    << std::right << std::setw(10) << pf.size_bytes;
                if (idx == sel) wattron(win, A_REVERSE | A_BOLD);
                mvwaddnstr(win, 4 + r, 2, clip(row.str(), w - 4).c_str(), w - 4);
                if (idx == sel) wattroff(win, A_REVERSE | A_BOLD);
            }
        }

        mvwhline(win, h - 3, 1, ACS_HLINE, w - 2);
        mvwaddnstr(win, h - 2, 2, clip(feedback, w - 4).c_str(), w - 4);
        if (has_colors_) wattron(win, COLOR_PAIR(CP_MENU));
        mvwaddnstr(win, h - 1, 2,
                   clip("Enter/A:Approve & Move  D:Delete  R:Refresh  Esc:Close", w - 4).c_str(),
                   w - 4);
        if (has_colors_) wattroff(win, COLOR_PAIR(CP_MENU));
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27 || ch == 'q' || ch == 'Q') break;
        if (ch == KEY_UP && sel > 0) {
            --sel;
        } else if (ch == KEY_DOWN && sel + 1 < count) {
            ++sel;
        } else if (ch == KEY_PPAGE) {
            sel = std::max(0, sel - list_h);
        } else if (ch == KEY_NPAGE && count > 0) {
            sel = std::min(count - 1, sel + list_h);
        } else if (ch == 'r' || ch == 'R') {
            items = engine_.list_pending_incoming_files();
            feedback = "Refreshed (" + std::to_string(items.size()) + " pending).";
        } else if ((ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == 'a' || ch == 'A') && count > 0) {
            const auto pf = items[static_cast<std::size_t>(sel)];
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
                items = engine_.list_pending_incoming_files();
            } else {
                feedback = "Error: " + err;
            }
        } else if ((ch == 'd' || ch == 'D' || ch == KEY_DC) && count > 0) {
            const auto pf = items[static_cast<std::size_t>(sel)];
            if (prompt_yes_no("Delete incoming/" + pf.uploader + "/" + pf.filename + "?")) {
                std::string err;
                if (engine_.delete_incoming_file(pf.uploader, pf.filename, err)) {
                    feedback = "Deleted " + pf.uploader + "/" + pf.filename;
                    set_status(feedback);
                    items = engine_.list_pending_incoming_files();
                } else {
                    feedback = "Error: " + err;
                }
            } else {
                feedback = "Delete cancelled.";
            }
        }
    }

    delwin(win);
}

bool NcursesUI::modal_confirm_exit() {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(40, cols - 4);
    int h = 6;
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return false;
    keypad(win, TRUE);
    wtimeout(win, 200);

    bool confirmed = false;
    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) {
            confirmed = true;
            break;
        }

        auto snap = engine_.snapshot();
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Exit ");
        if (snap.total_active_connections > 0) {
            mvwprintw(win, 2, 2, "Exit node? (%zu active)", snap.total_active_connections);
        } else {
            mvwprintw(win, 2, 2, "Exit node?");
        }
        mvwaddnstr(win, 3, 2, "[Y] Yes   [N] No", w - 4);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 'y' || ch == 'Y') {
            confirmed = true;
            break;
        }
        if (ch == 'n' || ch == 'N' || ch == 27) {
            confirmed = false;
            break;
        }
    }

    delwin(win);
    return confirmed;
}

} // namespace oyk::ui
