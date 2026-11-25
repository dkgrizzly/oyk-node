// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/radio/rig_control.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

namespace oyk::radio {

namespace {

std::string trim_ws(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parse_int_strict(const std::string& s, int& out) {
    std::string t = trim_ws(s);
    if (t.empty()) return false;
    char* endp = nullptr;
    long v = std::strtol(t.c_str(), &endp, 10);
    if (!endp || *endp != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

uint64_t parse_freq_to_hz(const std::string& raw) {
    std::string t = to_lower_ascii(trim_ws(raw));
    if (t.empty()) return 0;
    bool is_mhz = false;
    bool is_khz = false;
    if (t.size() >= 3 && t.substr(t.size() - 3) == "mhz") {
        is_mhz = true;
        t = trim_ws(t.substr(0, t.size() - 3));
    } else if (t.size() >= 3 && t.substr(t.size() - 3) == "khz") {
        is_khz = true;
        t = trim_ws(t.substr(0, t.size() - 3));
    } else if (t.size() >= 2 && t.substr(t.size() - 2) == "hz") {
        t = trim_ws(t.substr(0, t.size() - 2));
    }
    if (t.empty()) return 0;

    char* endp = nullptr;
    double val = std::strtod(t.c_str(), &endp);
    if (!endp || endp == t.c_str() || val < 0.0) return 0;

    if (is_mhz || (!is_khz && t.find('.') != std::string::npos && val < 100000.0)) {
        return static_cast<uint64_t>(std::llround(val * 1000000.0));
    }
    if (is_khz) {
        return static_cast<uint64_t>(std::llround(val * 1000.0));
    }
    return static_cast<uint64_t>(std::llround(val));
}

bool resolve_ipv4(const std::string& host, uint16_t port, sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1) {
        return true;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        return false;
    }
    bool ok = false;
    if (res->ai_addr && res->ai_addrlen >= sizeof(sockaddr_in)) {
        out.sin_addr = reinterpret_cast<const sockaddr_in*>(res->ai_addr)->sin_addr;
        ok = true;
    }
    freeaddrinfo(res);
    return ok;
}

std::vector<std::string> split_lines_trim(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (!line.empty()) {
            out.push_back(line);
        }
    }
    return out;
}

} // namespace

std::string rig_protocol_to_string(RigProtocol proto) {
    switch (proto) {
        case RigProtocol::Rigctl:       return "rigctl";
        case RigProtocol::RadioFoundry: return "radiofoundry";
        case RigProtocol::None:
        default:                        return "none";
    }
}

RigProtocol rig_protocol_from_string(const std::string& text) {
    std::string l = to_lower_ascii(trim_ws(text));
    if (l == "rigctl" || l == "rigctld" || l == "hamlib") return RigProtocol::Rigctl;
    if (l == "radiofoundry" || l == "takrf" || l == "rf-udp" || l == "udp4191") {
        return RigProtocol::RadioFoundry;
    }
    return RigProtocol::None;
}

uint16_t rig_default_port(RigProtocol proto) {
    switch (proto) {
        case RigProtocol::Rigctl:       return 4532;
        case RigProtocol::RadioFoundry: return 4191;
        case RigProtocol::None:
        default:                        return 0;
    }
}

std::string rf_build_select_cmd(int zone, int pos) {
    if (zone < 0) return "select";
    if (pos < 0) return "select?zone=" + std::to_string(zone);
    return "select?zone=" + std::to_string(zone) + "&pos=" + std::to_string(pos);
}

std::string rf_build_stop_notify_cmd() {
    return "select?notify=0";
}

std::string rf_build_step_channel_cmd(int delta) {
    return "step?channel=" + std::to_string(delta);
}

std::string rf_build_step_zone_cmd(int delta) {
    return "step?zone=" + std::to_string(delta);
}

bool rigctl_parse_rprt(const std::string& line, int& code_out) {
    std::string t = trim_ws(line);
    if (t.size() < 5) return false;
    if (t.rfind("RPRT ", 0) != 0) return false;
    return parse_int_strict(t.substr(5), code_out);
}

bool rf_parse_status_text(const std::string& text, RigStatus& out) {
    std::string clean = trim_ws(text);
    if (clean.empty()) return false;

    out.raw_status = clean;
    bool parsed_any = false;

    auto apply_kv = [&](const std::string& raw_k, const std::string& raw_v) {
        std::string k = to_lower_ascii(trim_ws(raw_k));
        std::string v = trim_ws(raw_v);
        // Strip surrounding quotes if present (e.g. JSON or quoted name)
        if (k.size() >= 2 && k.front() == '"' && k.back() == '"') k = k.substr(1, k.size() - 2);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (k.empty() || v.empty()) return;

        int iv = 0;
        if (k == "zone" || k == "z") {
            if (parse_int_strict(v, iv)) {
                out.zone = iv;
                parsed_any = true;
            }
        } else if (k == "pos" || k == "ch" || k == "channel" || k == "index") {
            if (parse_int_strict(v, iv)) {
                out.pos = iv;
                parsed_any = true;
            }
        } else if (k == "name" || k == "ch_name" || k == "channel_name" || k == "label") {
            out.channel_name = v;
            parsed_any = true;
        } else if (k == "freq" || k == "frequency" || k == "rx_freq" || k == "f") {
            uint64_t hz = parse_freq_to_hz(v);
            if (hz > 0) {
                out.freq_hz = hz;
                parsed_any = true;
            }
        } else if (k == "mode") {
            out.mode = v;
            if (to_lower_ascii(v) == "dmr") out.is_dmr = true;
            parsed_any = true;
        } else if (k == "cc" || k == "color_code" || k == "colorcode") {
            if (parse_int_strict(v, iv)) {
                out.color_code = iv;
                out.is_dmr = true;
                parsed_any = true;
            }
        } else if (k == "ts" || k == "timeslot" || k == "slot") {
            if (parse_int_strict(v, iv)) {
                out.timeslot = iv;
                out.is_dmr = true;
                parsed_any = true;
            }
        } else if (k == "tg" || k == "talkgroup") {
            if (parse_int_strict(v, iv)) {
                out.talkgroup = iv;
                out.is_dmr = true;
                parsed_any = true;
            }
        }
    };

    // Tokenize across lines, '&', ',', ';', or whitespace while preserving quoted values.
    // Supports formats like:
    //   "zone=2 pos=5 name=\"Local Rptr\" freq=446.000 cc=1 ts=2 tg=3100"
    //   "zone=2&pos=5&name=Local&freq=446000000&cc=1&ts=2&tg=3100"
    //   "zone: 2\npos: 5\nname: Local\nfreq: 446.000 MHz\ncc: 1\nts: 2\ntg: 3100"
    //   "{\"zone\":2,\"pos\":5,\"name\":\"Local\",\"freq\":446000000,\"cc\":1,\"ts\":2,\"tg\":3100}"
    std::string work = clean;
    if (!work.empty() && work.front() == '{' && work.back() == '}') {
        work = work.substr(1, work.size() - 2);
    }

    // Check if the payload is newline-delimited "key: value" or "key=value"
    if (work.find('\n') != std::string::npos) {
        for (const auto& line : split_lines_trim(work)) {
            std::size_t sep = line.find('=');
            if (sep == std::string::npos) sep = line.find(':');
            if (sep != std::string::npos) {
                apply_kv(line.substr(0, sep), line.substr(sep + 1));
            }
        }
    }

    if (!parsed_any) {
        // Parse flat delimiter-separated key=value or key:value pairs
        std::size_t i = 0;
        const std::size_t n = work.size();
        while (i < n) {
            while (i < n && (std::isspace(static_cast<unsigned char>(work[i])) ||
                             work[i] == '&' || work[i] == ',' || work[i] == ';')) {
                ++i;
            }
            if (i >= n) break;

            // Read key up to '=' or ':' or delimiter
            std::size_t k_start = i;
            while (i < n && work[i] != '=' && work[i] != ':' &&
                   work[i] != '&' && work[i] != ',' && work[i] != ';' &&
                   !std::isspace(static_cast<unsigned char>(work[i]))) {
                ++i;
            }
            std::string key = work.substr(k_start, i - k_start);
            while (i < n && std::isspace(static_cast<unsigned char>(work[i]))) ++i;
            if (i >= n || (work[i] != '=' && work[i] != ':')) {
                // Skip non-kv token
                while (i < n && !std::isspace(static_cast<unsigned char>(work[i])) &&
                       work[i] != '&' && work[i] != ',' && work[i] != ';') {
                    ++i;
                }
                continue;
            }
            ++i; // skip '=' or ':'
            while (i < n && std::isspace(static_cast<unsigned char>(work[i]))) ++i;

            std::string val;
            if (i < n && work[i] == '"') {
                ++i;
                while (i < n && work[i] != '"') {
                    val.push_back(work[i++]);
                }
                if (i < n && work[i] == '"') ++i;
            } else {
                std::size_t v_start = i;
                while (i < n && work[i] != '&' && work[i] != ',' && work[i] != ';' &&
                       !std::isspace(static_cast<unsigned char>(work[i]))) {
                    ++i;
                }
                val = work.substr(v_start, i - v_start);
            }
            apply_kv(key, val);
        }
    }

    return parsed_any || !clean.empty();
}

RigController::RigController() = default;

RigController::~RigController() {
    stop();
}

void RigController::configure(const RigConfig& cfg) {
    std::lock_guard<std::mutex> lk(mutex_);
    close_udp_socket_locked();
    cfg_ = cfg;
    if (cfg_.port == 0) {
        cfg_.port = rig_default_port(cfg_.protocol);
    }
    status_ = RigStatus{};
    status_.configured = cfg_.enabled && cfg_.protocol != RigProtocol::None && !cfg_.host.empty() && cfg_.port != 0;
    status_.protocol = cfg_.protocol;
    if (status_.configured) {
        status_.endpoint = cfg_.host + ":" + std::to_string(cfg_.port);
    }
    has_polled_ = false;
}

void RigController::stop() {
    std::lock_guard<std::mutex> lk(mutex_);
    close_udp_socket_locked();
    status_.reachable = false;
    status_.notifications_active = false;
}

RigConfig RigController::config() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return cfg_;
}

RigStatus RigController::status() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return status_;
}

bool RigController::ensure_udp_socket_locked(std::string& error_out) {
    if (udp_fd_ >= 0) return true;
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        error_out = "Failed to create UDP socket for RadioFoundry control";
        return false;
    }
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0; // ephemeral local port so radio can send change notifications back
    if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        ::close(fd);
        error_out = "Failed to bind local UDP socket for RadioFoundry control";
        return false;
    }
    udp_fd_ = fd;
    return true;
}

void RigController::close_udp_socket_locked() {
    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
}

void RigController::drain_udp_locked() {
    if (udp_fd_ < 0) return;
    char buf[1024];
    while (true) {
        sockaddr_in src{};
        socklen_t slen = sizeof(src);
        ssize_t n = ::recvfrom(udp_fd_, buf, sizeof(buf) - 1, 0,
                               reinterpret_cast<sockaddr*>(&src), &slen);
        if (n <= 0) break;
        buf[n] = '\0';
        std::string payload(buf, static_cast<std::size_t>(n));
        if (rf_parse_status_text(payload, status_)) {
            status_.reachable = true;
            status_.last_error.clear();
        }
    }
}

bool RigController::send_rf_command_locked(const std::string& cmd, std::string& error_out) {
    if (!cfg_.enabled || cfg_.protocol != RigProtocol::RadioFoundry) {
        error_out = "RadioFoundry UDP control is not enabled";
        return false;
    }
    if (!ensure_udp_socket_locked(error_out)) {
        status_.last_error = error_out;
        return false;
    }
    sockaddr_in dst{};
    uint16_t port = cfg_.port ? cfg_.port : rig_default_port(RigProtocol::RadioFoundry);
    if (!resolve_ipv4(cfg_.host, port, dst)) {
        error_out = "Unable to resolve host: " + cfg_.host;
        status_.last_error = error_out;
        status_.reachable = false;
        return false;
    }
    ssize_t sent = ::sendto(udp_fd_, cmd.data(), cmd.size(), 0,
                            reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
    if (sent < 0) {
        error_out = "UDP sendto failed";
        status_.last_error = error_out;
        status_.reachable = false;
        return false;
    }

    // Wait up to 400 ms for the radio's immediate status reply
    pollfd pfd{};
    pfd.fd = udp_fd_;
    pfd.events = POLLIN;
    int pr = ::poll(&pfd, 1, 400);
    if (pr > 0 && (pfd.revents & POLLIN)) {
        drain_udp_locked();
        if (cmd == rf_build_stop_notify_cmd()) {
            status_.notifications_active = false;
        } else {
            status_.notifications_active = cfg_.notify;
        }
        return true;
    }

    error_out = "No UDP reply from RadioFoundry radio at " + cfg_.host + ":" + std::to_string(port);
    status_.last_error = error_out;
    status_.reachable = false;
    return false;
}

bool RigController::rigctl_exchange(const std::string& host,
                                    uint16_t port,
                                    const std::string& cmd,
                                    int expected_lines,
                                    std::string& reply_out,
                                    std::string& error_out) {
    sockaddr_in dst{};
    if (!resolve_ipv4(host, port, dst)) {
        error_out = "Unable to resolve rigctld host: " + host;
        return false;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error_out = "Failed to create TCP socket for rigctld";
        return false;
    }

    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int cr = ::connect(fd, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
    if (cr < 0 && errno != EINPROGRESS) {
        ::close(fd);
        error_out = "Unable to connect to rigctld at " + host + ":" + std::to_string(port);
        return false;
    }
    if (cr < 0) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        if (::poll(&pfd, 1, 500) <= 0) {
            ::close(fd);
            error_out = "Timeout connecting to rigctld at " + host + ":" + std::to_string(port);
            return false;
        }
        int so_err = 0;
        socklen_t len = sizeof(so_err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) < 0 || so_err != 0) {
            ::close(fd);
            error_out = "Connection refused by rigctld at " + host + ":" + std::to_string(port);
            return false;
        }
    }

    std::string wire = cmd;
    if (wire.empty() || wire.back() != '\n') wire.push_back('\n');
    std::size_t off = 0;
    while (off < wire.size()) {
        ssize_t n = ::send(fd, wire.data() + off, wire.size() - off, 0);
        if (n <= 0) {
            ::close(fd);
            error_out = "Failed sending command to rigctld";
            return false;
        }
        off += static_cast<std::size_t>(n);
    }

    reply_out.clear();
    char buf[512];
    int lines_seen = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                       deadline - std::chrono::steady_clock::now())
                       .count();
        if (rem <= 0) break;
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int pr = ::poll(&pfd, 1, static_cast<int>(rem));
        if (pr <= 0) break;
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        reply_out.append(buf, static_cast<std::size_t>(n));
        lines_seen = static_cast<int>(std::count(reply_out.begin(), reply_out.end(), '\n'));
        if (reply_out.find("RPRT ") != std::string::npos || lines_seen >= expected_lines) {
            break;
        }
    }

    ::close(fd);
    if (reply_out.empty()) {
        error_out = "No response from rigctld";
        return false;
    }
    return true;
}

void RigController::poll() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!cfg_.enabled || cfg_.protocol != RigProtocol::RadioFoundry) {
        return;
    }
    drain_udp_locked();
    if (!cfg_.notify) return;

    auto now = std::chrono::steady_clock::now();
    uint32_t interval = cfg_.poll_interval_sec;
    if (interval == 0) return;
    if (interval > 55) interval = 55; // Keep RadioFoundry notifications alive (< 60s)

    if (!has_polled_ ||
        std::chrono::duration_cast<std::chrono::seconds>(now - last_poll_).count() >=
            static_cast<int64_t>(interval)) {
        has_polled_ = true;
        last_poll_ = now;
        std::string ignored_err;
        if (ensure_udp_socket_locked(ignored_err)) {
            sockaddr_in dst{};
            uint16_t port = cfg_.port ? cfg_.port : rig_default_port(RigProtocol::RadioFoundry);
            if (resolve_ipv4(cfg_.host, port, dst)) {
                const std::string cmd = rf_build_select_cmd();
                ::sendto(udp_fd_, cmd.data(), cmd.size(), 0,
                         reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
                status_.notifications_active = true;
            }
        }
    }
}

bool RigController::refresh(std::string& error_out) {
    RigConfig local_cfg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        local_cfg = cfg_;
        if (!cfg_.enabled || cfg_.protocol == RigProtocol::None) {
            error_out = "Rig control is disabled in configuration";
            status_.last_error = error_out;
            return false;
        }
        if (cfg_.protocol == RigProtocol::RadioFoundry) {
            last_poll_ = std::chrono::steady_clock::now();
            has_polled_ = true;
            return send_rf_command_locked(rf_build_select_cmd(), error_out);
        }
    }

    // Hamlib rigctld query: f (freq), m (mode + passband), t (ptt), v (vfo)
    uint16_t port = local_cfg.port ? local_cfg.port : rig_default_port(RigProtocol::Rigctl);
    std::string rep;
    if (!rigctl_exchange(local_cfg.host, port, "f", 1, rep, error_out)) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.reachable = false;
        status_.last_error = error_out;
        return false;
    }

    RigStatus next;
    next.configured = true;
    next.reachable = true;
    next.protocol = RigProtocol::Rigctl;
    next.endpoint = local_cfg.host + ":" + std::to_string(port);

    auto lines = split_lines_trim(rep);
    if (!lines.empty()) {
        int rprt = 0;
        if (rigctl_parse_rprt(lines[0], rprt) && rprt < 0) {
            error_out = "rigctld get_freq error: " + lines[0];
            std::lock_guard<std::mutex> lk(mutex_);
            status_.reachable = false;
            status_.last_error = error_out;
            return false;
        }
        next.freq_hz = parse_freq_to_hz(lines[0]);
    }

    std::string err_opt;
    if (rigctl_exchange(local_cfg.host, port, "m", 2, rep, err_opt)) {
        auto mlines = split_lines_trim(rep);
        int rprt = 0;
        if (!mlines.empty() && !rigctl_parse_rprt(mlines[0], rprt)) {
            next.mode = mlines[0];
            if (mlines.size() >= 2) {
                int pb = 0;
                if (parse_int_strict(mlines[1], pb)) next.passband_hz = pb;
            }
        }
    }
    if (rigctl_exchange(local_cfg.host, port, "t", 1, rep, err_opt)) {
        auto tlines = split_lines_trim(rep);
        int rprt = 0, ptt_v = -1;
        if (!tlines.empty() && !rigctl_parse_rprt(tlines[0], rprt) && parse_int_strict(tlines[0], ptt_v)) {
            next.ptt = ptt_v ? 1 : 0;
        }
    }
    if (rigctl_exchange(local_cfg.host, port, "v", 1, rep, err_opt)) {
        auto vlines = split_lines_trim(rep);
        int rprt = 0;
        if (!vlines.empty() && !rigctl_parse_rprt(vlines[0], rprt)) {
            next.vfo = vlines[0];
        }
    }

    std::ostringstream oss;
    oss << next.freq_hz << " Hz";
    if (!next.mode.empty()) oss << " " << next.mode << " (" << next.passband_hz << " Hz)";
    if (!next.vfo.empty()) oss << " " << next.vfo;
    if (next.ptt >= 0) oss << (next.ptt ? " [TX]" : " [RX]");
    next.raw_status = oss.str();

    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_ = next;
    }
    return true;
}

bool RigController::set_frequency(uint64_t hz, std::string& error_out) {
    RigConfig local_cfg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        local_cfg = cfg_;
    }
    if (!local_cfg.enabled || local_cfg.protocol != RigProtocol::Rigctl) {
        error_out = "Frequency tuning requires rigctl protocol";
        return false;
    }
    uint16_t port = local_cfg.port ? local_cfg.port : rig_default_port(RigProtocol::Rigctl);
    std::string rep;
    if (!rigctl_exchange(local_cfg.host, port, "F " + std::to_string(hz), 1, rep, error_out)) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    auto lines = split_lines_trim(rep);
    int rprt = 0;
    if (!lines.empty() && rigctl_parse_rprt(lines[0], rprt) && rprt != 0) {
        error_out = "rigctld set_freq rejected (" + lines[0] + ")";
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    status_.freq_hz = hz;
    status_.reachable = true;
    status_.last_error.clear();
    return true;
}

bool RigController::set_mode(const std::string& mode, int passband_hz, std::string& error_out) {
    RigConfig local_cfg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        local_cfg = cfg_;
    }
    if (!local_cfg.enabled || local_cfg.protocol != RigProtocol::Rigctl) {
        error_out = "Mode selection requires rigctl protocol";
        return false;
    }
    uint16_t port = local_cfg.port ? local_cfg.port : rig_default_port(RigProtocol::Rigctl);
    std::string cmd = "M " + mode + " " + std::to_string(passband_hz);
    std::string rep;
    if (!rigctl_exchange(local_cfg.host, port, cmd, 1, rep, error_out)) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    auto lines = split_lines_trim(rep);
    int rprt = 0;
    if (!lines.empty() && rigctl_parse_rprt(lines[0], rprt) && rprt != 0) {
        error_out = "rigctld set_mode rejected (" + lines[0] + ")";
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    status_.mode = mode;
    status_.passband_hz = passband_hz;
    status_.reachable = true;
    status_.last_error.clear();
    return true;
}

bool RigController::set_ptt(bool tx, std::string& error_out) {
    RigConfig local_cfg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        local_cfg = cfg_;
    }
    if (!local_cfg.enabled || local_cfg.protocol != RigProtocol::Rigctl) {
        error_out = "PTT control requires rigctl protocol";
        return false;
    }
    uint16_t port = local_cfg.port ? local_cfg.port : rig_default_port(RigProtocol::Rigctl);
    std::string rep;
    if (!rigctl_exchange(local_cfg.host, port, tx ? "T 1" : "T 0", 1, rep, error_out)) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    auto lines = split_lines_trim(rep);
    int rprt = 0;
    if (!lines.empty() && rigctl_parse_rprt(lines[0], rprt) && rprt != 0) {
        error_out = "rigctld set_ptt rejected (" + lines[0] + ")";
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    status_.ptt = tx ? 1 : 0;
    status_.reachable = true;
    status_.last_error.clear();
    return true;
}

bool RigController::set_vfo(const std::string& vfo, std::string& error_out) {
    RigConfig local_cfg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        local_cfg = cfg_;
    }
    if (!local_cfg.enabled || local_cfg.protocol != RigProtocol::Rigctl) {
        error_out = "VFO selection requires rigctl protocol";
        return false;
    }
    uint16_t port = local_cfg.port ? local_cfg.port : rig_default_port(RigProtocol::Rigctl);
    std::string rep;
    if (!rigctl_exchange(local_cfg.host, port, "V " + vfo, 1, rep, error_out)) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    auto lines = split_lines_trim(rep);
    int rprt = 0;
    if (!lines.empty() && rigctl_parse_rprt(lines[0], rprt) && rprt != 0) {
        error_out = "rigctld set_vfo rejected (" + lines[0] + ")";
        std::lock_guard<std::mutex> lk(mutex_);
        status_.last_error = error_out;
        return false;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    status_.vfo = vfo;
    status_.reachable = true;
    status_.last_error.clear();
    return true;
}

bool RigController::select_zone_channel(int zone, int pos, std::string& error_out) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (zone < 0) {
        error_out = "Zone index must be >= 0";
        return false;
    }
    last_poll_ = std::chrono::steady_clock::now();
    has_polled_ = true;
    return send_rf_command_locked(rf_build_select_cmd(zone, pos), error_out);
}

bool RigController::step_channel(int delta, std::string& error_out) {
    std::lock_guard<std::mutex> lk(mutex_);
    last_poll_ = std::chrono::steady_clock::now();
    has_polled_ = true;
    return send_rf_command_locked(rf_build_step_channel_cmd(delta), error_out);
}

bool RigController::step_zone(int delta, std::string& error_out) {
    std::lock_guard<std::mutex> lk(mutex_);
    last_poll_ = std::chrono::steady_clock::now();
    has_polled_ = true;
    return send_rf_command_locked(rf_build_step_zone_cmd(delta), error_out);
}

bool RigController::stop_notifications(std::string& error_out) {
    std::lock_guard<std::mutex> lk(mutex_);
    return send_rf_command_locked(rf_build_stop_notify_cmd(), error_out);
}

} // namespace oyk::radio
