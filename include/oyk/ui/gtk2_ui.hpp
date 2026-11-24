#pragma once

#include "oyk/node/node_engine.hpp"

#include <atomic>
#include <string>

namespace oyk::ui {

// Native X11 GUI using libgtk-2.0 matching the layout and controls of the ncurses UI.
class Gtk2UI {
public:
    Gtk2UI(node::NodeEngine& engine,
           std::atomic<bool>* external_stop_flag = nullptr);

    // Initializes GTK+ 2.0, builds the main window, and runs gtk_main() until clean exit.
    // Returns false if GTK cannot open an X11 display.
    bool run(int& argc, char**& argv);

private:
    struct Impl;
    node::NodeEngine& engine_;
    std::atomic<bool>* external_stop_flag_ = nullptr;
};

} // namespace oyk::ui
