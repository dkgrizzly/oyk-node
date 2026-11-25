// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace oyk::radio {

enum class RigProtocol {
    None = 0,
    Rigctl,       // Hamlib rigctld TCP protocol (default port 4532)
    RadioFoundry  // RadioFoundry / takRF UDP zone & channel control (default port 4191)
};

struct RigConfig {
    bool enabled = false;
    RigProtocol protocol = RigProtocol::Rigctl;
    std::string host = "127.0.0.1";
    uint16_t port = 4532;
    uint32_t poll_interval_sec = 30; // RadioFoundry recommends <=60s keepalive for notifications
    bool notify = true;              // Receive RadioFoundry UDP 4191 asynchronous status updates
};

struct RigStatus {
    bool configured = false;
    bool reachable = false;
    RigProtocol protocol = RigProtocol::None;
    std::string endpoint;
    std::string last_error;
    std::string raw_status;

    // Hamlib rigctl fields (freq_hz is also populated for RadioFoundry)
    uint64_t freq_hz = 0;
    std::string mode;
    int passband_hz = 0;
    int ptt = -1; // -1 = unknown, 0 = RX, 1 = TX
    std::string vfo;

    // RadioFoundry / takRF UDP 4191 fields (0-based zone and pos)
    int zone = -1;
    int pos = -1;
    std::string channel_name;
    bool is_dmr = false;
    int color_code = -1;
    int timeslot = -1;
    int talkgroup = -1;
    bool notifications_active = false;
};

// Protocol string helpers
std::string rig_protocol_to_string(RigProtocol proto);
RigProtocol rig_protocol_from_string(const std::string& text);
uint16_t rig_default_port(RigProtocol proto);

// Pure protocol encoders/parsers (also unit-tested directly)
std::string rf_build_select_cmd(int zone = -1, int pos = -1);
std::string rf_build_stop_notify_cmd();
std::string rf_build_step_channel_cmd(int delta);
std::string rf_build_step_zone_cmd(int delta);
bool rf_parse_status_text(const std::string& text, RigStatus& out);

bool rigctl_parse_rprt(const std::string& line, int& code_out);

// Thread-safe RigController supporting Hamlib rigctld (TCP) and RadioFoundry (UDP 4191)
class RigController {
public:
    RigController();
    ~RigController();

    RigController(const RigController&) = delete;
    RigController& operator=(const RigController&) = delete;

    void configure(const RigConfig& cfg);
    void stop();

    RigConfig config() const;
    RigStatus status() const;

    // Periodic poll (drains unsolicited RadioFoundry UDP updates & sends keepalive when due)
    void poll();

    // Query full current state from the radio
    bool refresh(std::string& error_out);

    // Hamlib rigctld operations (TCP)
    bool set_frequency(uint64_t hz, std::string& error_out);
    bool set_mode(const std::string& mode, int passband_hz, std::string& error_out);
    bool set_ptt(bool tx, std::string& error_out);
    bool set_vfo(const std::string& vfo, std::string& error_out);

    // RadioFoundry / takRF UDP 4191 operations
    bool select_zone_channel(int zone, int pos, std::string& error_out);
    bool step_channel(int delta, std::string& error_out);
    bool step_zone(int delta, std::string& error_out);
    bool stop_notifications(std::string& error_out);

private:
    bool ensure_udp_socket_locked(std::string& error_out);
    void close_udp_socket_locked();
    void drain_udp_locked();
    bool send_rf_command_locked(const std::string& cmd, std::string& error_out);

    bool rigctl_exchange(const std::string& host,
                         uint16_t port,
                         const std::string& cmd,
                         int expected_lines,
                         std::string& reply_out,
                         std::string& error_out);

    mutable std::mutex mutex_;
    RigConfig cfg_;
    RigStatus status_;
    int udp_fd_ = -1;
    std::chrono::steady_clock::time_point last_poll_{};
    bool has_polled_ = false;
};

} // namespace oyk::radio
