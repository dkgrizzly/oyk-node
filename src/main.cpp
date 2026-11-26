// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ax25/callsign.hpp"
#include "oyk/node/node_config.hpp"
#include "oyk/node/node_engine.hpp"

#ifdef OYK_HAS_NCURSES
#include "oyk/ui/ncurses_ui.hpp"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop_requested{false};

void signal_handler(int) {
    g_stop_requested.store(true);
}

void print_usage(const char* prog) {
    std::cout
        << "OYK Amateur Radio Packet Node\n"
        << "Usage: " << prog << " [options]\n\n"
        << "Options:\n"
        << "  -c, --config <path>      Load configuration file\n"
        << "      --callsign <CALL>    Override node callsign (e.g. N0CALL or N0CALL-7; default SSID 7)\n"
        << "      --alias <ALIAS>      Override node alias (e.g. OYKNOD)\n"
        << "      --port <UDP_PORT>    Override first AXUDP bind port (default 10093)\n"
        << "      --headless           Run without ncurses UI (log to stdout)\n"
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
    bool headless = false;

    auto take_val = [&](int& idx, const std::string& a, const std::string& name, std::string& dst) -> bool {
        if (a == name) {
            if (idx + 1 >= argc) return false;
            dst = argv[++idx];
            return true;
        }
        std::string pfx = name + "=";
        if (a.rfind(pfx, 0) == 0) {
            dst = a.substr(pfx.size());
            return true;
        }
        return false;
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-c" || arg == "--config" || arg.rfind("--config=", 0) == 0) {
            if (!take_val(i, arg, (arg == "-c") ? "-c" : "--config", config_path)) {
                std::cerr << "Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--callsign" || arg.rfind("--callsign=", 0) == 0) {
            if (!take_val(i, arg, "--callsign", override_call)) {
                std::cerr << "Missing value for --callsign\n";
                return 1;
            }
        } else if (arg == "--alias" || arg.rfind("--alias=", 0) == 0) {
            if (!take_val(i, arg, "--alias", override_alias)) {
                std::cerr << "Missing value for --alias\n";
                return 1;
            }
        } else if (arg == "--port" || arg.rfind("--port=", 0) == 0) {
            if (!take_val(i, arg, "--port", override_port)) {
                std::cerr << "Missing value for --port\n";
                return 1;
            }
        } else if (arg == "--headless") {
            headless = true;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
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
        auto parsed = oyk::node::NodeConfig::parse_callsign_with_default_ssid(
            override_call, cfg.callsign.ssid());
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

#ifdef OYK_HAS_NCURSES
    if (!headless) {
        oyk::ui::NcursesUI ui(engine, &g_stop_requested);
        ui.run();
        engine.stop();
        const std::string signoff_call = engine.config().callsign.str();
        std::printf("73 de %s\n", signoff_call.c_str());
        std::fflush(stdout);
        return 0;
    }
#else
    if (!headless) {
        std::cerr << "Warning: built without ncurses support; running in headless mode.\n";
    }
#endif

    std::cout << "OYK Packet Node " << cfg.callsign.str()
              << " running in headless mode. Press Ctrl+C to stop.\n";
    std::size_t printed_logs = 0;
    while (!g_stop_requested.load()) {
        auto snap = engine.snapshot();
        while (printed_logs < snap.recent_logs.size()) {
            std::cout << snap.recent_logs[printed_logs++] << "\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "Shutting down cleanly...\n";
    engine.stop();
    const std::string signoff_call = engine.config().callsign.str();
    std::printf("73 de %s\n", signoff_call.c_str());
    std::fflush(stdout);
    return 0;
}
