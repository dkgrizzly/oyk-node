// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/ax25/frame.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace oyk::radio {

enum class LearnedPeerPolicy {
    Inherit = 0,
    None,
    Inbound,
    Outbound,
    Both
};

struct PeerConfig {
    std::string id;
    std::string callsign;
    std::string description;
    std::string interface_id;
    std::string host;
    uint16_t port = 0;
    bool allow_inbound = true;
    bool allow_outbound = true;
    bool auto_learned = false;

    bool operator==(const PeerConfig&) const = default;
};

struct RadioFrameMeta {
    std::string interface_id;
    std::string peer_endpoint; // Transport-specific return address (e.g. "ip:port" for AXUDP)
    std::chrono::system_clock::time_point received_at;
};

struct RadioInterfaceConfig {
    std::string id;          // Unique identifier (e.g. "axudp0")
    std::string type;        // Driver type (e.g. "axudp"; future: "kiss-tcp", "kiss-serial", "agwpe")
    std::string description; // Human-readable label
    bool allow_inbound = true;
    bool allow_outbound = true;
    bool allow_same_interface = false;
    std::map<std::string, std::string> params;
    std::vector<std::string> peers; // Optional peer specs (driver-defined)
};

// RadioFoundry DMR ID <-> IPv4 & Netmask utilities (used by NodeConfig, AxudpInterface, AxipInterface)
bool is_all_ascii_digits(const std::string& text) noexcept;
bool parse_dmr_id(const std::string& text, uint32_t& id_out) noexcept;
std::string dmr_id_to_ipv4(uint32_t dmr_id, uint8_t prefix_octet = 12);
bool resolve_host_or_dmr_id(const std::string& host_spec,
                            uint8_t prefix_octet,
                            std::string& resolved_out);
uint8_t dmr_prefix_for_iface(const RadioInterfaceConfig& ic);
bool validate_iface_and_peer_addresses(const std::vector<RadioInterfaceConfig>& ifaces,
                                       const std::vector<PeerConfig>& peers,
                                       std::string& error_out);

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

    // Synchronize static [peer] routes owned by this interface.
    virtual void sync_peers(const std::vector<PeerConfig>& peers) { (void)peers; }

    virtual RadioInterfaceStats stats() const = 0;
};

} // namespace oyk::radio
