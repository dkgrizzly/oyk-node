#include "oyk/ax25/callsign.hpp"
#include "oyk/node/node_config.hpp"
#include "oyk/node/node_engine.hpp"

#ifdef OYK_HAS_NCURSES
#include "oyk/ui/ncurses_ui.hpp"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
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
        << "      --callsign <CALL>    Override node callsign (e.g. N0CALL-7)\n"
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

#ifdef OYK_HAS_NCURSES
    if (!headless) {
        oyk::ui::NcursesUI ui(engine, &g_stop_requested);
        ui.run();
        engine.stop();
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
    return 0;
}
