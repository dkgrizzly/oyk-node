// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/node/node_engine.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace oyk::ui {

// Local ncurses text-mode operator console for the packet node.
// Runs on the main thread and polls NodeEngine::snapshot() at ~5 Hz.
class NcursesUI {
public:
    explicit NcursesUI(node::NodeEngine& engine,
                       std::atomic<bool>* external_stop_flag = nullptr);
    ~NcursesUI();

    // Enters ncurses mode, runs the interactive UI loop until exit,
    // and guarantees terminal restoration on return.
    void run();

private:
    struct TermState;

    void draw_screen(const node::NodeSnapshot& snap);
    void draw_menu_bar(int cols);
    void draw_header(const node::NodeSnapshot& snap, int y, int cols);
    void draw_interfaces_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);
    void draw_new_callsigns_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);
    void draw_sessions_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);
    void draw_log_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);

    void open_node_menu(bool& should_exit);
    void modal_local_terminal();
    bool has_active_terminal() const;
    void close_terminal_session();
    bool modal_file_selector(const std::string& title, std::string& selected_path_out);
    void modal_terminate_connection();
    void modal_ban_callsign();
    void modal_approve_incoming_files();
    bool modal_confirm_exit();

    void draw_box(int y, int x, int h, int w, const std::string& title);
    void set_status(const std::string& msg);

    node::NodeEngine& engine_;
    std::atomic<bool>* external_stop_flag_ = nullptr;
    std::string status_message_;
    bool has_colors_ = false;
    std::shared_ptr<TermState> term_state_;
};

} // namespace oyk::ui
