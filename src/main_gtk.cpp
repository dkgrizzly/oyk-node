// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ax25/callsign.hpp"
#include "oyk/node/node_config.hpp"
#include "oyk/node/node_engine.hpp"
#include "oyk/ui/gtk2_ui.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

namespace {

std::atomic<bool> g_stop_requested{false};

void signal_handler(int) {
    g_stop_requested.store(true);
}

void print_usage(const char* prog) {
    std::cout
        << "OYK Amateur Radio Packet Node (X11 GTK+ 2.0 GUI)\n"
        << "Usage: " << prog << " [options]\n\n"
        << "Options:\n"
        << "  -c, --config <path>      Load configuration file\n"
        << "      --callsign <CALL>    Override node callsign (e.g. N0CALL-7)\n"
        << "      --alias <ALIAS>      Override node alias (e.g. OYKNOD)\n"
        << "      --port <UDP_PORT>    Override first AXUDP bind port (default 10093)\n"
        << "  -h, --help               Show this help\n\n"
        << "Default config search order (when -c/--config is omitted):\n"
        << "  1. ~/.oyk/node.cfg\n"
        << "  2. /etc/oyk/node.cfg\n"
        << "  3. ./node.cfg\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string config_path;
    std::string override_call;
    std::string override_alias;
    std::string override_port;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--callsign" && i + 1 < argc) {
            override_call = argv[++i];
        } else if (arg == "--alias" && i + 1 < argc) {
            override_alias = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            override_port = argv[++i];
        }
    }

    oyk::node::NodeConfig cfg;
    if (!config_path.empty()) {
        std::string err;
        if (!oyk::node::NodeConfig::load_from_file(config_path, cfg, err)) {
            std::cerr << "Config error: " << err << "\n";
            return 1;
        }
    } else if (auto discovered = oyk::node::NodeConfig::find_default_config()) {
        std::string err;
        if (!oyk::node::NodeConfig::load_from_file(*discovered, cfg, err)) {
            std::cerr << "Config error (" << *discovered << "): " << err << "\n";
            return 1;
        }
    } else {
        cfg = oyk::node::NodeConfig::make_default();
    }

    if (!override_call.empty()) {
        auto parsed = oyk::ax25::Callsign::parse(override_call);
        if (!parsed) {
            std::cerr << "Invalid callsign: " << override_call << "\n";
            return 1;
        }
        cfg.callsign = *parsed;
    }
    if (!override_alias.empty()) {
        auto parsed = oyk::ax25::Callsign::parse(override_alias);
        if (!parsed) {
            std::cerr << "Invalid alias: " << override_alias << "\n";
            return 1;
        }
        cfg.alias = *parsed;
    }
    if (!override_port.empty() && !cfg.interfaces.empty()) {
        cfg.interfaces[0].params["bind_port"] = override_port;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    oyk::node::NodeEngine engine(cfg);
    std::string err;
    if (!engine.start(err)) {
        std::cerr << "Failed to start packet node: " << err << "\n";
        return 1;
    }

    oyk::ui::Gtk2UI ui(engine, &g_stop_requested);
    if (!ui.run(argc, argv)) {
        std::cerr << "Unable to initialize GTK+ 2.0 X11 display.\n";
        engine.stop();
        return 1;
    }

    engine.stop();
    return 0;
}
