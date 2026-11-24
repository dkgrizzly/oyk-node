#pragma once

#include "oyk/node/node_engine.hpp"

#include <atomic>
#include <string>

namespace oyk::ui {

// Local ncurses text-mode operator console for the packet node.
// Runs on the main thread and polls NodeEngine::snapshot() at ~5 Hz.
class NcursesUI {
public:
    explicit NcursesUI(node::NodeEngine& engine,
                       std::atomic<bool>* external_stop_flag = nullptr);

    // Enters ncurses mode, runs the interactive UI loop until clean exit,
    // and guarantees terminal restoration on return.
    void run();

private:
    enum class MenuAction {
        AllowDenyIncoming = 0,
        TerminateConnection = 1,
        BanCallsign = 2,
        ExitNode = 3
    };

    void draw_screen(const node::NodeSnapshot& snap);
    void draw_header(const node::NodeSnapshot& snap, int cols);
    void draw_interfaces_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);
    void draw_new_callsigns_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);
    void draw_sessions_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);
    void draw_log_pane(const node::NodeSnapshot& snap, int y, int x, int h, int w);
    void draw_menu_bar(int y, int cols);

    void handle_menu_action(MenuAction action, bool& should_exit);
    void modal_allow_deny_incoming();
    void modal_terminate_connection();
    void modal_ban_callsign();
    bool modal_confirm_exit();

    void draw_box(int y, int x, int h, int w, const std::string& title);
    void set_status(const std::string& msg);

    node::NodeEngine& engine_;
    std::atomic<bool>* external_stop_flag_ = nullptr;
    int selected_menu_ = 0;
    std::string status_message_;
    bool has_colors_ = false;
};

} // namespace oyk::ui
