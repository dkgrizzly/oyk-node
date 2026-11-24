#include "oyk/ui/ncurses_ui.hpp"

#include <ncurses.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
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

NcursesUI::NcursesUI(node::NodeEngine& engine, std::atomic<bool>* external_stop_flag)
    : engine_(engine),
      external_stop_flag_(external_stop_flag),
      status_message_("Ready. Use 1-4 or Left/Right + Enter to select a menu action.") {}

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
        init_pair(CP_MENU, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_MENU_SEL, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_BORDER, COLOR_CYAN, -1);
    }

    bool should_exit = false;
    while (!should_exit) {
        if (external_stop_flag_ && external_stop_flag_->load()) {
            break;
        }

        node::NodeSnapshot snap = engine_.snapshot();
        draw_screen(snap);

        int ch = getch();
        if (ch == ERR) {
            continue;
        }
        if (ch == KEY_RESIZE) {
            continue;
        }

        switch (ch) {
            case KEY_LEFT:
                selected_menu_ = (selected_menu_ + 3) % 4;
                break;
            case KEY_RIGHT:
            case '\t':
                selected_menu_ = (selected_menu_ + 1) % 4;
                break;
            case '\n':
            case '\r':
            case KEY_ENTER:
                handle_menu_action(static_cast<MenuAction>(selected_menu_), should_exit);
                break;
            case '1':
            case 'a':
            case 'A':
                selected_menu_ = 0;
                handle_menu_action(MenuAction::AllowDenyIncoming, should_exit);
                break;
            case '2':
            case 't':
            case 'T':
                selected_menu_ = 1;
                handle_menu_action(MenuAction::TerminateConnection, should_exit);
                break;
            case '3':
            case 'b':
            case 'B':
                selected_menu_ = 2;
                handle_menu_action(MenuAction::BanCallsign, should_exit);
                break;
            case '4':
            case 'q':
            case 'Q':
                selected_menu_ = 3;
                handle_menu_action(MenuAction::ExitNode, should_exit);
                break;
            default:
                break;
        }
    }
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
        mvprintw(4, 0, "Keys: [1] Allow/Deny  [2] Terminate  [3] Ban  [4] Exit");
        refresh();
        return;
    }

    draw_header(snap, cols);

    // Vertical layout:
    // Row 0: Header bar
    // Rows 1..(top_h): Interfaces (left) + Last 5 New Callsigns Heard (right)
    // Middle: Active Connections & Banned Summary
    // Lower: Activity Log
    // Row rows-2: Status message
    // Row rows-1: Menu bar
    const int avail_h = rows - 3; // exclude header (1) + status (1) + menu (1)
    const int top_h = std::max(8, std::min(10, avail_h / 3 + 1));
    const int mid_h = std::max(5, (avail_h - top_h) / 2);
    const int bot_h = std::max(4, avail_h - top_h - mid_h);

    const int right_w = std::max(34, std::min(44, cols * 42 / 100));
    const int left_w = cols - right_w;

    draw_interfaces_pane(snap, 1, 0, top_h, left_w);
    draw_new_callsigns_pane(snap, 1, left_w, top_h, right_w);
    draw_sessions_pane(snap, 1 + top_h, 0, mid_h, cols);
    draw_log_pane(snap, 1 + top_h + mid_h, 0, bot_h, cols);

    // Status message line
    if (has_colors_) attron(COLOR_PAIR(CP_WARN));
    print_clipped(rows - 2, 1, cols - 2, status_message_);
    if (has_colors_) attroff(COLOR_PAIR(CP_WARN));

    draw_menu_bar(rows - 1, cols);
    refresh();
}

void NcursesUI::draw_header(const node::NodeSnapshot& snap, int cols) {
    if (has_colors_) attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
    else attron(A_REVERSE | A_BOLD);

    mvhline(0, 0, ' ', cols);

    std::ostringstream left;
    left << " OYK PACKET NODE: " << snap.node_callsign;
    if (!snap.node_alias.empty()) {
        left << " (" << snap.node_alias << ")";
    }
    left << "  |  Up: " << format_duration(snap.uptime_seconds)
         << "  |  Active: " << snap.total_active_connections;
    print_clipped(0, 0, cols - 24, left.str());

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
    mvaddnstr(0, bx, badge.c_str(), cols - bx);
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

void NcursesUI::draw_menu_bar(int y, int cols) {
    static const char* ITEMS[4] = {
        "[1] Allow/Deny Incoming",
        "[2] Terminate Connection",
        "[3] Ban/Unban Callsign",
        "[4] Exit Node"
    };

    if (has_colors_) attron(COLOR_PAIR(CP_MENU));
    mvhline(y, 0, ' ', cols);
    if (has_colors_) attroff(COLOR_PAIR(CP_MENU));

    int x = 1;
    for (int i = 0; i < 4; ++i) {
        std::string label = std::string(" ") + ITEMS[i] + " ";
        if (x >= cols) break;
        if (i == selected_menu_) {
            if (has_colors_) attron(COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
            else attron(A_REVERSE | A_BOLD);
        } else {
            if (has_colors_) attron(COLOR_PAIR(CP_MENU));
        }
        mvaddnstr(y, x, label.c_str(), cols - x);
        if (i == selected_menu_) {
            if (has_colors_) attroff(COLOR_PAIR(CP_MENU_SEL) | A_BOLD);
            else attroff(A_REVERSE | A_BOLD);
        } else {
            if (has_colors_) attroff(COLOR_PAIR(CP_MENU));
        }
        x += static_cast<int>(label.size()) + 1;
    }
}

void NcursesUI::handle_menu_action(MenuAction action, bool& should_exit) {
    switch (action) {
        case MenuAction::AllowDenyIncoming:
            modal_allow_deny_incoming();
            break;
        case MenuAction::TerminateConnection:
            modal_terminate_connection();
            break;
        case MenuAction::BanCallsign:
            modal_ban_callsign();
            break;
        case MenuAction::ExitNode:
            if (modal_confirm_exit()) {
                should_exit = true;
            }
            break;
    }
}

void NcursesUI::modal_allow_deny_incoming() {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(56, cols - 4);
    int h = 9;
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    int choice = engine_.accept_incoming() ? 0 : 1; // 0 = Allow, 1 = Deny
    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Incoming Connections Policy ");
        mvwprintw(win, 2, 2, "Current status: %s",
                  engine_.accept_incoming() ? "ALLOWED" : "DENIED");

        const char* opts[2] = {
            " [A] Allow new incoming connections ",
            " [D] Deny new incoming connections  "
        };
        for (int i = 0; i < 2; ++i) {
            if (i == choice) wattron(win, A_REVERSE | A_BOLD);
            mvwaddnstr(win, 4 + i, 3, opts[i], w - 6);
            if (i == choice) wattroff(win, A_REVERSE | A_BOLD);
        }
        mvwaddnstr(win, h - 2, 2, "Up/Down + Enter, A/D, or Esc to cancel", w - 4);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break; // Esc
        if (ch == KEY_UP || ch == KEY_DOWN) {
            choice = 1 - choice;
        } else if (ch == 'a' || ch == 'A') {
            engine_.set_accept_incoming(true);
            set_status("New incoming connections are now ALLOWED.");
            break;
        } else if (ch == 'd' || ch == 'D') {
            engine_.set_accept_incoming(false);
            set_status("New incoming connections are now DENIED.");
            break;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            bool allow = (choice == 0);
            engine_.set_accept_incoming(allow);
            set_status(allow ? "New incoming connections are now ALLOWED."
                             : "New incoming connections are now DENIED.");
            break;
        }
    }

    delwin(win);
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
    int w = std::min(72, cols - 4);
    int h = std::min(19, rows - 4);
    int y = std::max(1, (rows - h) / 2);
    int x = std::max(1, (cols - w) / 2);

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return;
    keypad(win, TRUE);
    wtimeout(win, 200);

    std::string call_input;
    std::string reason_input = "Banned by sysop.";
    bool hard_ban = false;
    bool appeal_allowed = true;
    std::string feedback;
    int unban_sel = 0;
    int focus = 0; // 0=callsign, 1=reason, 2=flags, 3=unban list

    while (true) {
        if (external_stop_flag_ && external_stop_flag_->load()) break;

        auto snap = engine_.snapshot();
        if (snap.ban_entries.empty()) {
            if (focus == 3) focus = 0;
            unban_sel = 0;
        } else if (unban_sel >= static_cast<int>(snap.ban_entries.size())) {
            unban_sel = static_cast<int>(snap.ban_entries.size()) - 1;
        }

        curs_set((focus == 0 || focus == 1) ? 1 : 0);
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Ban / Unban Callsign (banlist.cfg) ");

        mvwaddnstr(win, 2, 2, "Callsign: ", w - 4);
        if (focus == 0) wattron(win, A_UNDERLINE | A_BOLD);
        mvwaddnstr(win, 2, 12,
                   (call_input + std::string(12 - std::min<std::size_t>(12, call_input.size()), ' ')).c_str(),
                   w - 14);
        if (focus == 0) wattroff(win, A_UNDERLINE | A_BOLD);

        mvwaddnstr(win, 3, 2, "Reason  : ", w - 4);
        if (focus == 1) wattron(win, A_UNDERLINE | A_BOLD);
        int reason_w = std::max(10, w - 14);
        mvwaddnstr(win, 3, 12, clip(reason_input, reason_w).c_str(), reason_w);
        if (focus == 1) wattroff(win, A_UNDERLINE | A_BOLD);

        std::string flags_line = std::string("Options : [") + (hard_ban ? "X" : " ") +
                                 "] Hard Ban (H)   [" + (appeal_allowed ? "X" : " ") +
                                 "] Appeal Allowed (A)";
        if (focus == 2) wattron(win, A_REVERSE | A_BOLD);
        mvwaddnstr(win, 4, 2, clip(flags_line, w - 4).c_str(), w - 4);
        if (focus == 2) wattroff(win, A_REVERSE | A_BOLD);

        mvwprintw(win, 6, 2, "Active Bans (%zu):", snap.ban_entries.size());
        int list_h = std::max(1, h - 10);
        if (snap.ban_entries.empty()) {
            mvwaddnstr(win, 7, 4, "(none)", w - 6);
        } else {
            int offset = (unban_sel >= list_h) ? (unban_sel - list_h + 1) : 0;
            for (int i = 0; i < list_h && (offset + i) < static_cast<int>(snap.ban_entries.size()); ++i) {
                const auto& e = snap.ban_entries[static_cast<std::size_t>(offset + i)];
                std::string row = " " + e.key + " [" + (e.hard_ban() ? "HARD" : "SOFT") +
                                  " " + e.format_flags_binary() + "] " + e.reason;
                bool highlighted = (focus == 3) && (offset + i == unban_sel);
                if (highlighted) wattron(win, A_REVERSE | A_BOLD);
                mvwaddnstr(win, 7 + i, 3, clip(row, w - 6).c_str(), w - 6);
                if (highlighted) wattroff(win, A_REVERSE | A_BOLD);
            }
        }

        if (!feedback.empty()) {
            mvwaddnstr(win, h - 3, 2, clip(feedback, w - 4).c_str(), w - 4);
        }
        const char* help_str = (focus == 3)
                                   ? "Up/Down: pick | Enter: UNBAN | Tab: next field | Esc: close"
                                   : (focus == 2)
                                         ? "H: hard/soft | A: appeal | Enter: BAN | Tab: next | Esc: close"
                                         : "Enter: BAN | Tab: next field | Esc: close";
        mvwaddnstr(win, h - 2, 2, clip(help_str, w - 4).c_str(), w - 4);

        if (focus == 0) {
            wmove(win, 2, 12 + static_cast<int>(call_input.size()));
        } else if (focus == 1) {
            wmove(win, 3, 12 + std::min(reason_w - 1, static_cast<int>(reason_input.size())));
        }
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == 27) break;

        if (ch == '\t') {
            focus = (focus + 1) % (snap.ban_entries.empty() ? 3 : 4);
            continue;
        }

        if (focus == 3) {
            if (ch == KEY_UP && unban_sel > 0) {
                --unban_sel;
            } else if (ch == KEY_DOWN &&
                       unban_sel + 1 < static_cast<int>(snap.ban_entries.size())) {
                ++unban_sel;
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                std::string call = snap.ban_entries[static_cast<std::size_t>(unban_sel)].key;
                std::string msg;
                engine_.unban_callsign(call, &msg);
                feedback = msg;
                set_status(msg);
            }
        } else if (focus == 2) {
            if (ch == 'h' || ch == 'H') {
                hard_ban = !hard_ban;
            } else if (ch == 'a' || ch == 'A') {
                appeal_allowed = !appeal_allowed;
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (!call_input.empty()) {
                    uint32_t flags = (appeal_allowed ? node::BAN_FLAG_APPEAL_ALLOWED : 0) |
                                     (hard_ban ? node::BAN_FLAG_HARD_BAN : 0);
                    std::string msg;
                    if (engine_.ban_callsign(call_input, &msg, true, flags, reason_input)) {
                        call_input.clear();
                        feedback = msg;
                        set_status(msg);
                    } else {
                        feedback = "Error: " + msg;
                    }
                } else {
                    feedback = "Enter a callsign first.";
                }
            }
        } else if (focus == 0) {
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (!call_input.empty()) {
                    uint32_t flags = (appeal_allowed ? node::BAN_FLAG_APPEAL_ALLOWED : 0) |
                                     (hard_ban ? node::BAN_FLAG_HARD_BAN : 0);
                    std::string msg;
                    if (engine_.ban_callsign(call_input, &msg, true, flags, reason_input)) {
                        call_input.clear();
                        feedback = msg;
                        set_status(msg);
                    } else {
                        feedback = "Error: " + msg;
                    }
                }
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
                if (!call_input.empty()) {
                    uint32_t flags = (appeal_allowed ? node::BAN_FLAG_APPEAL_ALLOWED : 0) |
                                     (hard_ban ? node::BAN_FLAG_HARD_BAN : 0);
                    std::string msg;
                    if (engine_.ban_callsign(call_input, &msg, true, flags, reason_input)) {
                        call_input.clear();
                        feedback = msg;
                        set_status(msg);
                    } else {
                        feedback = "Error: " + msg;
                    }
                } else {
                    focus = 0;
                }
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

bool NcursesUI::modal_confirm_exit() {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    int w = std::min(56, cols - 4);
    int h = 7;
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
        mvwprintw(win, 0, 2, " Exit Packet Node ");
        mvwprintw(win, 2, 2, "Disconnect %zu active session(s) and exit cleanly?",
                  snap.total_active_connections);
        mvwaddnstr(win, 4, 2, "Press [Y] to exit cleanly, or [N]/Esc to stay.", w - 4);
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
