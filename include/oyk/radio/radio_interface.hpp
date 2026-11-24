#pragma once

#include "oyk/ax25/frame.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace oyk::radio {

struct RadioFrameMeta {
    std::string interface_id;
    std::string peer_endpoint; // Transport-specific return address (e.g. "ip:port" for AXUDP)
    std::chrono::system_clock::time_point received_at;
};

struct RadioInterfaceConfig {
    std::string id;          // Unique identifier (e.g. "axudp0")
    std::string type;        // Driver type (e.g. "axudp"; future: "kiss-tcp", "kiss-serial", "agwpe")
    std::string description; // Human-readable label
    std::map<std::string, std::string> params;
    std::vector<std::string> peers; // Optional peer specs (driver-defined)
};

struct RadioInterfaceStats {
    uint64_t rx_frames = 0;
    uint64_t tx_frames = 0;
    uint64_t rx_errors = 0;
    uint64_t tx_errors = 0;
    bool running = false;
};

// Abstract base class for all radio/link-layer interfaces.
// New radio drivers (KISS TCP/Serial, AGWPE, AF_AX25, etc.) implement this
// interface and register a factory with InterfaceRegistry.
class RadioInterface {
public:
    using FrameCallback = std::function<void(const ax25::Frame&, const RadioFrameMeta&)>;

    virtual ~RadioInterface() = default;

    virtual std::string id() const = 0;
    virtual std::string type() const = 0;
    virtual std::string description() const = 0;
    virtual std::string status_summary() const = 0;

    virtual bool start(FrameCallback on_frame, std::string& error_out) = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    // Transmit an AX.25 frame. `target_endpoint` optionally specifies the
    // transport peer (e.g. "192.0.2.10:10093" for AXUDP) when known.
    virtual bool send_frame(const ax25::Frame& frame,
                            const std::string& target_endpoint = "") = 0;

    virtual RadioInterfaceStats stats() const = 0;
};

} // namespace oyk::radio
