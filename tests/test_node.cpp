// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ax25/callsign.hpp"
#include "oyk/ax25/crc.hpp"
#include "oyk/ax25/frame.hpp"
#include "oyk/node/ban_list.hpp"
#include "oyk/node/logger.hpp"
#include "oyk/node/node_config.hpp"
#include "oyk/node/node_engine.hpp"
#include "oyk/radio/axip_interface.hpp"
#include "oyk/radio/axudp_interface.hpp"
#include "oyk/radio/interface_registry.hpp"
#include "oyk/radio/kiss.hpp"
#include "oyk/radio/kiss_serial_interface.hpp"
#include "oyk/radio/kiss_tcp_interface.hpp"
#include "oyk/radio/telnet_interface.hpp"
#include "oyk/protocol/yapp.hpp"

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace oyk;

// Minimal AXUDP peer helper over a real localhost UDP socket for integration testing
class TestAxudpClient {
public:
    TestAxudpClient(ax25::Callsign call, uint16_t target_port)
        : call_(std::move(call)) {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        assert(fd_ >= 0);

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        local.sin_port = 0;
        int rc = bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local));
        assert(rc == 0);

        socklen_t len = sizeof(local);
        getsockname(fd_, reinterpret_cast<sockaddr*>(&local), &len);
        local_port_ = ntohs(local.sin_port);

        target_.sin_family = AF_INET;
        target_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        target_.sin_port = htons(target_port);
    }

    ~TestAxudpClient() {
        if (fd_ >= 0) close(fd_);
    }

    uint16_t local_port() const { return local_port_; }
    const ax25::Callsign& call() const { return call_; }

    void send(const ax25::Frame& f) {
        auto raw = f.encode();
        assert(!raw.empty());
        ax25::append_fcs(raw);
        ssize_t n = sendto(fd_, raw.data(), raw.size(), 0,
                           reinterpret_cast<const sockaddr*>(&target_), sizeof(target_));
        assert(n == static_cast<ssize_t>(raw.size()));
    }

    std::optional<ax25::Frame> recv(int timeout_ms = 1000) {
        pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc <= 0) return std::nullopt;

        uint8_t buf[2048];
        ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n <= 2) return std::nullopt;
        assert(ax25::verify_fcs(buf, static_cast<std::size_t>(n)));
        return ax25::Frame::decode(buf, static_cast<std::size_t>(n) - 2);
    }

    // Collects all I-frame payloads until no more arrive within quiet_ms, acknowledging each with RR
    std::string recv_all_text(const ax25::Callsign& peer, uint8_t& vr, int quiet_ms = 120) {
        std::string out;
        while (auto f = recv(quiet_ms)) {
            if (f->type() == ax25::FrameType::I) {
                out.append(reinterpret_cast<const char*>(f->info.data()), f->info.size());
                vr = static_cast<uint8_t>((f->ns() + 1) & 0x07);
                send(ax25::Frame::make_rr(peer, call_, vr, false, false));
            }
        }
        return out;
    }

private:
    ax25::Callsign call_;
    int fd_ = -1;
    uint16_t local_port_ = 0;
    sockaddr_in target_{};
};

void test_callsign_and_crc() {
    auto c1 = ax25::Callsign::parse("w1aw-7");
    assert(c1.has_value());
    assert(c1->base() == "W1AW");
    assert(c1->ssid() == 7);
    assert(c1->str() == "W1AW-7");

    auto c2 = ax25::Callsign::parse("K7OYK-0");
    assert(c2.has_value());
    assert(c2->str() == "K7OYK");

    assert(!ax25::Callsign::parse("TOOLONG1").has_value());
    assert(!ax25::Callsign::parse("W1AW-16").has_value());
    assert(!ax25::Callsign::parse("BAD!").has_value());

    auto enc = c1->encode_ax25(true, true);
    bool cbit = false, last = false;
    auto dec = ax25::Callsign::decode_ax25(enc.data(), &cbit, &last);
    assert(dec.has_value() && *dec == *c1);
    assert(cbit && last);

    // Standard CRC-16/X-25 check value for "123456789" is 0x906E
    const uint8_t vec[] = {'1','2','3','4','5','6','7','8','9'};
    uint16_t fcs = ax25::crc16_fcs(vec, sizeof(vec));
    assert(fcs == 0x906E);

    std::vector<uint8_t> pkt(vec, vec + sizeof(vec));
    ax25::append_fcs(pkt);
    assert(ax25::verify_fcs(pkt.data(), pkt.size()));
    pkt[3] ^= 0x01;
    assert(!ax25::verify_fcs(pkt.data(), pkt.size()));

    std::cout << "[PASS] test_callsign_and_crc\n";
}

void test_frame_codec() {
    ax25::Callsign dst("N0CALL", 7);
    ax25::Callsign src("W1AW", 1);
    std::vector<uint8_t> data = {'H', 'E', 'L', 'L', 'O', '\r'};
    auto f = ax25::Frame::make_i(dst, src, 3, 5, data, true);
    f.digipeaters.push_back({ax25::Callsign("WIDE1", 1), true});
    f.digipeaters.push_back({ax25::Callsign("WIDE2", 1), false});

    auto raw = f.encode();
    auto dec = ax25::Frame::decode(raw.data(), raw.size());
    assert(dec.has_value());
    assert(dec->destination == dst);
    assert(dec->source == src);
    assert(dec->type() == ax25::FrameType::I);
    assert(dec->ns() == 3);
    assert(dec->nr() == 5);
    assert(dec->pf());
    assert(dec->has_pid && dec->pid == ax25::PID_NO_LAYER3);
    assert(dec->info == data);
    assert(dec->digipeaters.size() == 2);
    assert(dec->digipeaters[0].call.str() == "WIDE1-1" && dec->digipeaters[0].repeated);
    assert(dec->digipeaters[1].call.str() == "WIDE2-1" && !dec->digipeaters[1].repeated);

    std::cout << "[PASS] test_frame_codec\n";
}

void test_config_loader() {
    const std::string path = "/tmp/oyk_node_test.conf";
    {
        std::ofstream ofs(path);
        ofs << "[node]\n"
            << "callsign = K7OYK-7\n"
            << "alias = OYK\n"
            << "accept_incoming = false\n"
            << "ban = N0BAD\n"
            << "[interface axudp0]\n"
            << "type = axudp\n"
            << "bind_host = 127.0.0.1\n"
            << "bind_port = 0\n"
            << "peer = W1AW@127.0.0.1:10099\n";
    }
    node::NodeConfig cfg;
    std::string err;
    assert(node::NodeConfig::load_from_file(path, cfg, err));
    assert(cfg.source_path == path);
    assert(cfg.callsign.str() == "K7OYK-7");
    assert(cfg.alias.str() == "OYK");
    assert(!cfg.accept_incoming_default);
    assert(cfg.banned_callsigns.size() == 1 && cfg.banned_callsigns[0] == "N0BAD");
    assert(cfg.interfaces.size() == 1 && cfg.interfaces[0].id == "axudp0");
    assert(cfg.interfaces[0].type == "axudp");
    std::remove(path.c_str());

    // Verify default search path order: ~/.oyk/node.cfg -> /etc/oyk/node.cfg -> ./node.cfg
    auto paths = node::NodeConfig::default_search_paths();
    assert(paths.size() >= 2);
    if (paths.size() == 3) {
        assert(paths[0].ends_with("/.oyk/node.cfg"));
        assert(paths[1] == "/etc/oyk/node.cfg");
        assert(paths[2] == "./node.cfg");
    } else {
        assert(paths[0] == "/etc/oyk/node.cfg");
        assert(paths[1] == "./node.cfg");
    }

    // Verify discovery precedence using temporary files
    const std::string p1 = "/tmp/oyk_cfg_first.cfg";
    const std::string p2 = "/tmp/oyk_cfg_second.cfg";
    const std::string p3 = "/tmp/oyk_cfg_third.cfg";
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove(p3.c_str());

    // None exist
    assert(!node::NodeConfig::find_default_config({p1, p2, p3}).has_value());

    // Only 3rd exists -> picks 3rd
    { std::ofstream f(p3); f << "[node]\ncallsign = W3AAA\n"; }
    auto found = node::NodeConfig::find_default_config({p1, p2, p3});
    assert(found.has_value() && *found == p3);

    // 2nd and 3rd exist -> picks 2nd
    { std::ofstream f(p2); f << "[node]\ncallsign = W2BBB\n"; }
    found = node::NodeConfig::find_default_config({p1, p2, p3});
    assert(found.has_value() && *found == p2);

    // 1st, 2nd, and 3rd exist -> picks 1st
    { std::ofstream f(p1); f << "[node]\ncallsign = W1CCC\n"; }
    found = node::NodeConfig::find_default_config({p1, p2, p3});
    assert(found.has_value() && *found == p1);

    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove(p3.c_str());

    std::cout << "[PASS] test_config_loader\n";
}

void test_node_axudp_end_to_end() {
    assert(radio::InterfaceRegistry::instance().has_type("axudp"));

    node::NodeConfig cfg;
    cfg.callsign = ax25::Callsign("K7OYK", 7);
    cfg.alias = ax25::Callsign("OYKNOD", 0);
    cfg.t1_ms = 500;
    cfg.logging.log_path = "/tmp/oyk_test_axudp_logs/";
    cfg.banlist_path = "/tmp/oyk_test_axudp_banlist.cfg";
    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);

    radio::RadioInterfaceConfig if0;
    if0.id = "axudp0";
    if0.type = "axudp";
    if0.description = "Test AXUDP 0";
    if0.params["bind_host"] = "127.0.0.1";
    if0.params["bind_port"] = "0";

    radio::RadioInterfaceConfig if1;
    if1.id = "axudp1";
    if1.type = "axudp";
    if1.description = "Test AXUDP 1";
    if1.params["bind_host"] = "127.0.0.1";
    if1.params["bind_port"] = "0";

    cfg.interfaces = {if0, if1};

    node::NodeEngine engine(cfg);
    std::string err;
    assert(engine.start(err));

    auto* ax0 = dynamic_cast<radio::AxudpInterface*>(engine.find_interface("axudp0"));
    auto* ax1 = dynamic_cast<radio::AxudpInterface*>(engine.find_interface("axudp1"));
    assert(ax0 && ax1);
    const uint16_t port0 = ax0->bound_port();
    const uint16_t port1 = ax1->bound_port();
    assert(port0 > 0 && port1 > 0);

    // 1. Connect W1AW on axudp0
    TestAxudpClient w1aw(ax25::Callsign("W1AW", 0), port0);
    w1aw.send(ax25::Frame::make_sabm(cfg.callsign, w1aw.call()));

    auto ua = w1aw.recv();
    assert(ua.has_value() && ua->type() == ax25::FrameType::UA);

    uint8_t vr = 0;
    std::string banner = w1aw.recv_all_text(cfg.callsign, vr);
    assert(banner.find("Connected to K7OYK-7") != std::string::npos);

    auto snap = engine.snapshot();
    assert(snap.total_active_connections == 1);
    assert(snap.interfaces[0].active_connections == 1);
    assert(snap.interfaces[1].active_connections == 0);

    // Send INFO, PORTS, HELP, MHEARD commands
    std::string cmd_info = "INFO\r";
    w1aw.send(ax25::Frame::make_i(cfg.callsign, w1aw.call(), 0, vr,
                                  std::vector<uint8_t>(cmd_info.begin(), cmd_info.end())));
    std::string info_resp = w1aw.recv_all_text(cfg.callsign, vr);
    assert(info_resp.find("K7OYK-7 (OYKNOD)") != std::string::npos);

    std::string cmd_ports = "PORTS\r";
    w1aw.send(ax25::Frame::make_i(cfg.callsign, w1aw.call(), 1, vr,
                                  std::vector<uint8_t>(cmd_ports.begin(), cmd_ports.end())));
    std::string ports_resp = w1aw.recv_all_text(cfg.callsign, vr);
    assert(ports_resp.find("axudp0") != std::string::npos);
    assert(ports_resp.find("axudp1") != std::string::npos);

    std::string cmd_help = "?\r";
    w1aw.send(ax25::Frame::make_i(cfg.callsign, w1aw.call(), 2, vr,
                                  std::vector<uint8_t>(cmd_help.begin(), cmd_help.end())));
    std::string help_resp = w1aw.recv_all_text(cfg.callsign, vr);
    assert(help_resp.find("CONNECT") != std::string::npos);

    // 2. Connect K7XYZ on axudp1
    TestAxudpClient k7xyz(ax25::Callsign("K7XYZ", 1), port1);
    k7xyz.send(ax25::Frame::make_sabm(cfg.alias, k7xyz.call()));
    auto ua2 = k7xyz.recv();
    assert(ua2.has_value() && ua2->type() == ax25::FrameType::UA);
    uint8_t vr2 = 0;
    (void)k7xyz.recv_all_text(cfg.alias, vr2);

    snap = engine.snapshot();
    assert(snap.total_active_connections == 2);
    assert(snap.interfaces[0].active_connections == 1);
    assert(snap.interfaces[1].active_connections == 1);

    // 3. Test Deny / Allow incoming connections
    engine.set_accept_incoming(false);
    TestAxudpClient n1den(ax25::Callsign("N1DEN", 0), port0);
    n1den.send(ax25::Frame::make_sabm(cfg.callsign, n1den.call()));
    auto dm = n1den.recv();
    assert(dm.has_value() && dm->type() == ax25::FrameType::DM);
    assert(engine.snapshot().total_active_connections == 2);

    engine.set_accept_incoming(true);

    // 4. Test Terminate a specific connection (terminate W1AW, keep K7XYZ-1)
    uint64_t w1aw_sid = 0;
    for (const auto& s : engine.snapshot().active_sessions) {
        if (s.remote_call == "W1AW") w1aw_sid = s.id;
    }
    assert(w1aw_sid != 0);
    std::string term_msg;
    assert(engine.terminate_session(w1aw_sid, &term_msg));

    auto disc = w1aw.recv();
    assert(disc.has_value() && disc->type() == ax25::FrameType::DISC);
    w1aw.send(ax25::Frame::make_ua(cfg.callsign, w1aw.call()));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    snap = engine.snapshot();
    assert(snap.interfaces[0].active_connections == 0);
    assert(snap.interfaces[1].active_connections == 1);

    // 5. Test Ban Callsign (hard-banned + already notified immediately disconnects active K7XYZ-1 and rejects K7XYZ-5)
    std::string ban_msg;
    assert(engine.ban_callsign("K7XYZ", &ban_msg, true,
                               node::BAN_FLAG_HARD_BAN | node::BAN_FLAG_NOTIFIED,
                               "Test hard ban"));
    auto disc2 = k7xyz.recv();
    assert(disc2.has_value() && disc2->type() == ax25::FrameType::DISC);
    k7xyz.send(ax25::Frame::make_ua(cfg.alias, k7xyz.call()));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(engine.snapshot().total_active_connections == 0);

    TestAxudpClient k7xyz5(ax25::Callsign("K7XYZ", 5), port1);
    k7xyz5.send(ax25::Frame::make_sabm(cfg.callsign, k7xyz5.call()));
    auto dm_ban = k7xyz5.recv();
    assert(dm_ban.has_value() && dm_ban->type() == ax25::FrameType::DM);

    // Unban and confirm reconnection works
    assert(engine.unban_callsign("K7XYZ", &ban_msg));
    k7xyz5.send(ax25::Frame::make_sabm(cfg.callsign, k7xyz5.call()));
    auto ua_unban = k7xyz5.recv();
    assert(ua_unban.has_value() && ua_unban->type() == ax25::FrameType::UA);

    // Specific SSID ban: ban N1TEST-7 only, leaving N1TEST-1 allowed
    assert(engine.ban_callsign("N1TEST-7", &ban_msg, true));
    assert(engine.is_banned(ax25::Callsign("N1TEST", 7)));
    assert(!engine.is_banned(ax25::Callsign("N1TEST", 1)));

    // 6. Test Last 5 New Callsigns Heard (send UI beacons from 6 fresh stations + repeat oldest)
    const std::vector<std::string> calls = {"AA1AA", "BB2BB", "CC3CC", "DD4DD", "EE5EE", "FF6FF"};
    for (const auto& c : calls) {
        TestAxudpClient cl(*ax25::Callsign::parse(c), port0);
        cl.send(ax25::Frame::make_ui(ax25::Callsign("ID", 0), cl.call(), {'h', 'i'}));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Repeat AA1AA - should NOT change last_new_callsigns order since AA1AA was already heard!
    {
        TestAxudpClient cl(*ax25::Callsign::parse("AA1AA"), port0);
        cl.send(ax25::Frame::make_ui(ax25::Callsign("ID", 0), cl.call(), {'a', 'g', 'a', 'i', 'n'}));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    snap = engine.snapshot();
    assert(snap.last_new_callsigns.size() == 5);
    assert(snap.last_new_callsigns[0].callsign == "FF6FF");
    assert(snap.last_new_callsigns[1].callsign == "EE5EE");
    assert(snap.last_new_callsigns[2].callsign == "DD4DD");
    assert(snap.last_new_callsigns[3].callsign == "CC3CC");
    assert(snap.last_new_callsigns[4].callsign == "BB2BB");

    // 7. Test cross-port CONNECT gateway (axudp0 -> axudp1)
    TestAxudpClient remote_target(ax25::Callsign("N9REM", 0), port1);
    assert(ax1->add_peer("N9REM", "127.0.0.1", remote_target.local_port()));

    TestAxudpClient caller(ax25::Callsign("W2SRC", 0), port0);
    caller.send(ax25::Frame::make_sabm(cfg.callsign, caller.call()));
    assert(caller.recv()->type() == ax25::FrameType::UA);
    uint8_t caller_vr = 0;
    (void)caller.recv_all_text(cfg.callsign, caller_vr);

    std::string ccmd = "CONNECT axudp1 N9REM\r";
    caller.send(ax25::Frame::make_i(cfg.callsign, caller.call(), 0, caller_vr,
                                    std::vector<uint8_t>(ccmd.begin(), ccmd.end())));

    auto out_sabm = remote_target.recv(1000);
    assert(out_sabm.has_value() && out_sabm->type() == ax25::FrameType::SABM);
    remote_target.send(ax25::Frame::make_ua(cfg.callsign, remote_target.call()));

    std::string conn_notice = caller.recv_all_text(cfg.callsign, caller_vr, 150);
    assert(conn_notice.find("Connected to N9REM") != std::string::npos);

    // Send bridged message caller -> remote_target
    std::string msg = "CQ DE W2SRC\r";
    caller.send(ax25::Frame::make_i(cfg.callsign, caller.call(), 1, caller_vr,
                                    std::vector<uint8_t>(msg.begin(), msg.end())));
    uint8_t rem_vr = 0;
    std::string rem_got = remote_target.recv_all_text(cfg.callsign, rem_vr, 150);
    assert(rem_got == msg);

    // Remote disconnects -> caller returns to node prompt
    remote_target.send(ax25::Frame::make_disc(cfg.callsign, remote_target.call()));
    auto rem_ua = remote_target.recv();
    assert(rem_ua.has_value() && rem_ua->type() == ax25::FrameType::UA);

    std::string disc_notice = caller.recv_all_text(cfg.callsign, caller_vr, 150);
    assert(disc_notice.find("Disconnected from N9REM") != std::string::npos);

    // 8. Clean shutdown sends 73 sign-off and disconnects remaining active sessions
    engine.stop();
    auto shutdown_signoff = caller.recv(500);
    assert(shutdown_signoff.has_value() && shutdown_signoff->type() == ax25::FrameType::I);
    std::string signoff_txt(shutdown_signoff->info.begin(), shutdown_signoff->info.end());
    assert(signoff_txt.find("73 de K7OYK-7") != std::string::npos);
    auto shutdown_disc = caller.recv(500);
    assert(shutdown_disc.has_value() && shutdown_disc->type() == ax25::FrameType::DISC);

    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);

    std::cout << "[PASS] test_node_axudp_end_to_end\n";
}

void test_kiss_codec() {
    std::vector<uint8_t> tricky = {
        0x00, radio::KISS_FEND, 0x11, radio::KISS_FESC, 0x22,
        radio::KISS_TFEND, radio::KISS_TFESC, radio::KISS_FEND
    };
    auto enc = radio::kiss_encode(5, tricky, radio::KISS_CMD_DATA);
    assert(enc.front() == radio::KISS_FEND && enc.back() == radio::KISS_FEND);

    // Feed 1 byte at a time to test streaming reassembly
    radio::KissDecoder dec;
    std::vector<radio::KissFrame> out;
    for (uint8_t b : enc) {
        auto chunk = dec.feed(&b, 1);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    assert(out.size() == 1);
    assert(out[0].port == 5);
    assert(out[0].command == radio::KISS_CMD_DATA);
    assert(out[0].payload == tricky);

    auto param_enc = radio::kiss_encode_param(2, radio::KISS_CMD_TXDELAY, 35);
    auto param_out = dec.feed(param_enc.data(), param_enc.size());
    assert(param_out.size() == 1);
    assert(param_out[0].port == 2);
    assert(param_out[0].command == radio::KISS_CMD_TXDELAY);
    assert(param_out[0].payload.size() == 1 && param_out[0].payload[0] == 35);

    std::cout << "[PASS] test_kiss_codec\n";
}

void test_axip_packet_parsing() {
    auto& reg = radio::InterfaceRegistry::instance();
    assert(reg.has_type("axip"));
    assert(reg.has_type("kiss-serial"));
    assert(reg.has_type("kiss-tcp"));
    assert(reg.has_type("telnet"));

    // Build AX.25 payload + CRC-16 FCS
    ax25::Frame sabm = ax25::Frame::make_sabm(ax25::Callsign("K7OYK", 7), ax25::Callsign("W1AXP", 0));
    std::vector<uint8_t> ax_payload = sabm.encode();
    ax25::append_fcs(ax_payload);

    // Wrap in a 24-byte IPv4 header (IHL = 6 -> 24 bytes with 4 bytes of IP options), protocol 93
    std::vector<uint8_t> ip_pkt(24, 0);
    ip_pkt[0] = 0x46; // IPv4, IHL = 6 (24 bytes)
    uint16_t tot = static_cast<uint16_t>(24 + ax_payload.size());
    ip_pkt[2] = static_cast<uint8_t>((tot >> 8) & 0xFF);
    ip_pkt[3] = static_cast<uint8_t>(tot & 0xFF);
    ip_pkt[9] = static_cast<uint8_t>(radio::IPPROTO_AXIP);
    ip_pkt.insert(ip_pkt.end(), ax_payload.begin(), ax_payload.end());

    const uint8_t* extracted = nullptr;
    std::size_t extracted_len = 0;
    assert(radio::AxipInterface::extract_ipv4_axip_payload(
        ip_pkt.data(), ip_pkt.size(), extracted, extracted_len));
    assert(extracted_len == ax_payload.size());
    assert(ax25::verify_fcs(extracted, extracted_len));
    auto decoded = ax25::Frame::decode(extracted, extracted_len - 2);
    assert(decoded.has_value());
    assert(decoded->source.str() == "W1AXP");
    assert(decoded->type() == ax25::FrameType::SABM);

    // Reject wrong IP protocol
    ip_pkt[9] = 17; // UDP
    assert(!radio::AxipInterface::extract_ipv4_axip_payload(
        ip_pkt.data(), ip_pkt.size(), extracted, extracted_len));

    // Reject truncated IP header
    assert(!radio::AxipInterface::extract_ipv4_axip_payload(ip_pkt.data(), 10, extracted, extracted_len));

    // Verify AxipInterface instantiation and graceful start/error behavior
    radio::RadioInterfaceConfig axip_cfg;
    axip_cfg.id = "axip0";
    axip_cfg.type = "axip";
    axip_cfg.peers.push_back("W1AXP@127.0.0.1");
    std::string err;
    auto axip = reg.create(axip_cfg, err);
    assert(axip != nullptr);
    if (axip->start([](const ax25::Frame&, const radio::RadioFrameMeta&) {}, err)) {
        assert(axip->is_running());
        axip->stop();
    } else {
        // Without CAP_NET_RAW, error should clearly mention privileges
        assert(err.find("CAP_NET_RAW") != std::string::npos);
    }

    std::cout << "[PASS] test_axip_packet_parsing\n";
}

void test_kiss_serial_pty_end_to_end() {
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master_fd < 0) {
        std::cout << "[SKIP] test_kiss_serial_pty_end_to_end (no /dev/ptmx)\n";
        return;
    }
    assert(grantpt(master_fd) == 0);
    assert(unlockpt(master_fd) == 0);
    const char* slave_name = ptsname(master_fd);
    assert(slave_name != nullptr);

    // Put master PTY in raw mode so kernel doesn't mangle bytes (0x03/0x0D/etc.)
    termios mtio{};
    if (tcgetattr(master_fd, &mtio) == 0) {
        cfmakeraw(&mtio);
        tcsetattr(master_fd, TCSANOW, &mtio);
    }

    node::NodeConfig cfg;
    cfg.callsign = ax25::Callsign("K7OYK", 7);
    cfg.alias = ax25::Callsign("OYKNOD", 0);
    cfg.logging.log_path = "/tmp/oyk_test_kiss_ser_logs/";
    cfg.banlist_path = "/tmp/oyk_test_kiss_ser_banlist.cfg";
    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);
    radio::RadioInterfaceConfig kiss_cfg;
    kiss_cfg.id = "kiss0";
    kiss_cfg.type = "kiss-serial";
    kiss_cfg.params["device"] = slave_name;
    kiss_cfg.params["baud"] = "9600";
    kiss_cfg.params["kiss_port"] = "0";
    kiss_cfg.params["txdelay"] = "25";
    cfg.interfaces = {kiss_cfg};

    node::NodeEngine engine(cfg);
    std::string err;
    assert(engine.start(err));

    radio::KissDecoder master_dec;
    auto recv_kiss = [&](int timeout_ms) -> std::vector<radio::KissFrame> {
        std::vector<radio::KissFrame> got;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        uint8_t buf[512];
        while (std::chrono::steady_clock::now() < deadline) {
            pollfd pfd{master_fd, POLLIN, 0};
            if (poll(&pfd, 1, 30) > 0 && (pfd.revents & POLLIN)) {
                ssize_t n = read(master_fd, buf, sizeof(buf));
                if (n > 0) {
                    auto frames = master_dec.feed(buf, static_cast<std::size_t>(n));
                    got.insert(got.end(), frames.begin(), frames.end());
                    if (!got.empty()) break;
                }
            }
        }
        return got;
    };

    // Expect initial TXDELAY=25 KISS command frame
    auto init_frames = recv_kiss(500);
    bool saw_txdelay = false;
    for (const auto& kf : init_frames) {
        if (kf.command == radio::KISS_CMD_TXDELAY && kf.payload.size() == 1 && kf.payload[0] == 25) {
            saw_txdelay = true;
        }
    }
    assert(saw_txdelay);

    // Send SABM from K1SER via KISS
    ax25::Callsign k1ser("K1SER", 0);
    auto sabm_kiss = radio::kiss_encode(0, ax25::Frame::make_sabm(cfg.callsign, k1ser).encode());
    ssize_t wn = write(master_fd, sabm_kiss.data(), sabm_kiss.size());
    assert(wn == static_cast<ssize_t>(sabm_kiss.size()));

    // Receive UA + banner I-frame
    bool saw_ua = false;
    std::string banner;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
    while (std::chrono::steady_clock::now() < deadline && (!saw_ua || banner.find("K7OYK-7") == std::string::npos)) {
        auto kfs = recv_kiss(100);
        for (const auto& kf : kfs) {
            if (kf.command != radio::KISS_CMD_DATA) continue;
            auto f = ax25::Frame::decode(kf.payload.data(), kf.payload.size());
            if (!f) continue;
            if (f->type() == ax25::FrameType::UA) saw_ua = true;
            if (f->type() == ax25::FrameType::I) {
                banner.append(reinterpret_cast<const char*>(f->info.data()), f->info.size());
            }
        }
    }
    assert(saw_ua);
    assert(banner.find("Connected to K7OYK-7") != std::string::npos);

    auto snap = engine.snapshot();
    assert(snap.interfaces.size() == 1);
    assert(snap.interfaces[0].id == "kiss0");
    assert(snap.interfaces[0].active_connections == 1);
    assert(!snap.last_new_callsigns.empty() && snap.last_new_callsigns[0].callsign == "K1SER");

    engine.stop();
    close(master_fd);
    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);
    std::cout << "[PASS] test_kiss_serial_pty_end_to_end\n";
}

void test_kiss_tcp_end_to_end() {
    // 1. Start a NodeEngine with a kiss-tcp SERVER on 127.0.0.1:0
    node::NodeConfig cfg;
    cfg.callsign = ax25::Callsign("K7OYK", 7);
    cfg.alias = ax25::Callsign("OYKNOD", 0);
    cfg.logging.log_path = "/tmp/oyk_test_kiss_tcp_logs/";
    cfg.banlist_path = "/tmp/oyk_test_kiss_tcp_banlist.cfg";
    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);

    radio::RadioInterfaceConfig srv_cfg;
    srv_cfg.id = "kisssrv0";
    srv_cfg.type = "kiss-tcp";
    srv_cfg.params["mode"] = "server";
    srv_cfg.params["host"] = "127.0.0.1";
    srv_cfg.params["port"] = "0";
    cfg.interfaces = {srv_cfg};

    node::NodeEngine engine(cfg);
    std::string err;
    assert(engine.start(err));

    auto* srv_iface = dynamic_cast<radio::KissTcpInterface*>(engine.find_interface("kisssrv0"));
    assert(srv_iface != nullptr);
    const uint16_t srv_port = srv_iface->bound_port();
    assert(srv_port > 0);

    // 2. Connect a KissTcpInterface in CLIENT mode to the server!
    radio::RadioInterfaceConfig cli_cfg;
    cli_cfg.id = "kisscli0";
    cli_cfg.type = "kiss-tcp";
    cli_cfg.params["mode"] = "client";
    cli_cfg.params["host"] = "127.0.0.1";
    cli_cfg.params["port"] = std::to_string(srv_port);
    cli_cfg.params["reconnect_ms"] = "100";

    radio::KissTcpInterface client_iface(cli_cfg);
    std::mutex rx_mu;
    std::vector<ax25::Frame> client_rx;
    assert(client_iface.start(
        [&](const ax25::Frame& f, const radio::RadioFrameMeta&) {
            std::lock_guard<std::mutex> lk(rx_mu);
            client_rx.push_back(f);
        },
        err));

    // Wait for TCP connection to establish
    for (int i = 0; i < 40 && !client_iface.is_connected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    assert(client_iface.is_connected());

    // Send SABM from W1KIS over KISS TCP client -> NodeEngine KISS TCP server
    ax25::Callsign w1kis("W1KIS", 2);
    assert(client_iface.send_frame(ax25::Frame::make_sabm(cfg.callsign, w1kis)));

    bool saw_ua = false;
    std::string banner;
    for (int i = 0; i < 40; ++i) {
        {
            std::lock_guard<std::mutex> lk(rx_mu);
            for (const auto& f : client_rx) {
                if (f.type() == ax25::FrameType::UA) saw_ua = true;
                if (f.type() == ax25::FrameType::I) {
                    banner.append(reinterpret_cast<const char*>(f.info.data()), f.info.size());
                }
            }
            client_rx.clear();
        }
        if (saw_ua && banner.find("Connected to K7OYK-7") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    assert(saw_ua);
    assert(banner.find("Connected to K7OYK-7") != std::string::npos);

    auto snap = engine.snapshot();
    assert(snap.interfaces[0].active_connections == 1);
    assert(!snap.last_new_callsigns.empty() && snap.last_new_callsigns[0].callsign == "W1KIS-2");

    client_iface.stop();
    engine.stop();
    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);
    std::cout << "[PASS] test_kiss_tcp_end_to_end\n";
}

void test_telnet_end_to_end() {
    node::NodeConfig cfg;
    cfg.callsign = ax25::Callsign("K7OYK", 7);
    cfg.alias = ax25::Callsign("OYKNOD", 0);
    cfg.t1_ms = 400;
    cfg.logging.log_path = "/tmp/oyk_test_telnet_logs/";
    cfg.banlist_path = "/tmp/oyk_test_telnet_banlist.cfg";
    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);

    radio::RadioInterfaceConfig tel_cfg;
    tel_cfg.id = "telnet0";
    tel_cfg.type = "telnet";
    tel_cfg.params["bind_host"] = "127.0.0.1";
    tel_cfg.params["bind_port"] = "0";

    radio::RadioInterfaceConfig ax_cfg;
    ax_cfg.id = "axudp0";
    ax_cfg.type = "axudp";
    ax_cfg.params["bind_host"] = "127.0.0.1";
    ax_cfg.params["bind_port"] = "0";

    cfg.interfaces = {tel_cfg, ax_cfg};

    node::NodeEngine engine(cfg);
    std::string err;
    assert(engine.start(err));

    auto* tel = dynamic_cast<radio::TelnetInterface*>(engine.find_interface("telnet0"));
    auto* ax0 = dynamic_cast<radio::AxudpInterface*>(engine.find_interface("axudp0"));
    assert(tel && ax0);
    const uint16_t tel_port = tel->bound_port();
    const uint16_t ax_port = ax0->bound_port();
    assert(tel_port > 0 && ax_port > 0);

    auto tcp_connect = [&]() -> int {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(tel_port);
        int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        assert(rc == 0);
        return fd;
    };

    auto tcp_recv_until = [](int fd, const std::string& needle, int timeout_ms = 1000) -> std::string {
        std::string acc;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char buf[512];
        while (std::chrono::steady_clock::now() < deadline) {
            pollfd pfd{fd, POLLIN, 0};
            int rc = poll(&pfd, 1, 50);
            if (rc > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                acc.append(buf, static_cast<std::size_t>(n));
                if (!needle.empty() && acc.find(needle) != std::string::npos) break;
            }
        }
        return acc;
    };

    // 1. Connect via Telnet with IAC bytes + callsign login
    int cfd = tcp_connect();
    std::string prompt = tcp_recv_until(cfd, "Callsign: ");
    assert(prompt.find("Callsign: ") != std::string::npos);

    // Send Telnet IAC WILL SGA + "W1TEL\r\n"
    const uint8_t login_seq[] = {255, 251, 3, 'W', '1', 'T', 'E', 'L', '\r', '\n'};
    assert(send(cfd, login_seq, sizeof(login_seq), 0) == static_cast<ssize_t>(sizeof(login_seq)));

    std::string welcome = tcp_recv_until(cfd, "K7OYK-7> ");
    assert(welcome.find("Connected to K7OYK-7") != std::string::npos);

    auto snap = engine.snapshot();
    assert(snap.interfaces[0].id == "telnet0" && snap.interfaces[0].active_connections == 1);
    assert(snap.interfaces[1].id == "axudp0" && snap.interfaces[1].active_connections == 0);
    assert(!snap.last_new_callsigns.empty() && snap.last_new_callsigns[0].callsign == "W1TEL");

    // 2. Run PORTS command over Telnet using CR-only line ending (e.g. PuTTY)
    // Verify Telnet sends a leading CRLF before the response so the prompt line is not overwritten
    const std::string ports_cmd = "PORTS\r";
    assert(send(cfd, ports_cmd.data(), ports_cmd.size(), 0) == static_cast<ssize_t>(ports_cmd.size()));
    std::string ports_out = tcp_recv_until(cfd, "K7OYK-7> ");
    assert(ports_out.size() >= 2 && ports_out[0] == '\r' && ports_out[1] == '\n');
    assert(ports_out.find("telnet0") != std::string::npos);
    assert(ports_out.find("axudp0") != std::string::npos);

    // 3. Cross-port CONNECT from Telnet -> AXUDP peer!
    TestAxudpClient ax_peer(ax25::Callsign("N9AXP", 0), ax_port);
    assert(ax0->add_peer("N9AXP", "127.0.0.1", ax_peer.local_port()));

    const std::string ccmd = "CONNECT axudp0 N9AXP\r\n";
    assert(send(cfd, ccmd.data(), ccmd.size(), 0) == static_cast<ssize_t>(ccmd.size()));

    auto sabm = ax_peer.recv(1000);
    assert(sabm.has_value() && sabm->type() == ax25::FrameType::SABM);
    ax_peer.send(ax25::Frame::make_ua(cfg.callsign, ax_peer.call()));

    std::string bridged_ok = tcp_recv_until(cfd, "Connected to N9AXP");
    assert(bridged_ok.find("Connected to N9AXP") != std::string::npos);

    const std::string chat_msg = "HELLO OVER AXUDP\r\n";
    assert(send(cfd, chat_msg.data(), chat_msg.size(), 0) == static_cast<ssize_t>(chat_msg.size()));
    uint8_t ax_vr = 0;
    std::string ax_got = ax_peer.recv_all_text(cfg.callsign, ax_vr, 150);
    assert(ax_got.find("HELLO OVER AXUDP") != std::string::npos);

    // Disconnect remote AXUDP peer -> Telnet user returns to node prompt
    ax_peer.send(ax25::Frame::make_disc(cfg.callsign, ax_peer.call()));
    assert(ax_peer.recv(500)->type() == ax25::FrameType::UA);
    std::string returned = tcp_recv_until(cfd, "K7OYK-7> ");
    assert(returned.find("Disconnected from N9AXP") != std::string::npos);

    // 4. Test Allow/Deny incoming on Telnet
    engine.set_accept_incoming(false);
    int cfd_den = tcp_connect();
    (void)tcp_recv_until(cfd_den, "Callsign: ");
    const std::string den_call = "W2DEN\r\n";
    assert(send(cfd_den, den_call.data(), den_call.size(), 0) == static_cast<ssize_t>(den_call.size()));
    std::string den_resp = tcp_recv_until(cfd_den, "rejected");
    assert(den_resp.find("rejected") != std::string::npos);
    close(cfd_den);
    engine.set_accept_incoming(true);

    // 5. Test Ban on active Telnet user disconnects them cleanly
    std::string msg;
    assert(engine.ban_callsign("W1TEL", &msg, true));
    std::string disc_out = tcp_recv_until(cfd, "Disconnected");
    assert(disc_out.find("Disconnected") != std::string::npos);
    close(cfd);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(engine.snapshot().interfaces[0].active_connections == 0);

    // 6. Test incoming-only enforcement: AXUDP user trying CONNECT telnet0 gets immediate DM
    TestAxudpClient ax_caller(ax25::Callsign("W3AXC", 0), ax_port);
    ax_caller.send(ax25::Frame::make_sabm(cfg.callsign, ax_caller.call()));
    assert(ax_caller.recv()->type() == ax25::FrameType::UA);
    uint8_t cvr = 0;
    (void)ax_caller.recv_all_text(cfg.callsign, cvr);

    std::string out_tel = "CONNECT telnet0 W9OUT\r";
    ax_caller.send(ax25::Frame::make_i(cfg.callsign, ax_caller.call(), 0, cvr,
                                       std::vector<uint8_t>(out_tel.begin(), out_tel.end())));
    std::string refused = ax_caller.recv_all_text(cfg.callsign, cvr, 200);
    assert(refused.find("Connection refused") != std::string::npos);

    // 7. Graceful shutdown with active Telnet + AXUDP connections sends 73 sign-off and DISC
    int cfd_shut = tcp_connect();
    (void)tcp_recv_until(cfd_shut, "Callsign: ");
    const std::string shut_login = "W4SHU\r";
    assert(send(cfd_shut, shut_login.data(), shut_login.size(), 0) == static_cast<ssize_t>(shut_login.size()));
    std::string shut_welcome = tcp_recv_until(cfd_shut, "K7OYK-7> ");
    assert(shut_welcome.find("Connected to K7OYK-7") != std::string::npos);

    std::thread stop_thr([&]() { engine.stop(); });

    std::string shut_tel = tcp_recv_until(cfd_shut, "Disconnected", 1000);
    assert(shut_tel.find("Node shutting down. 73 de K7OYK-7") != std::string::npos);
    assert(shut_tel.find("Disconnected") != std::string::npos);
    close(cfd_shut);

    // AXUDP peer also receives 73 sign-off I-frame and DISC, and replies UA
    bool ax_got_73 = false;
    bool ax_got_disc = false;
    auto ax_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while ((!ax_got_73 || !ax_got_disc) && std::chrono::steady_clock::now() < ax_deadline) {
        auto f = ax_caller.recv(100);
        if (!f) continue;
        if (f->type() == ax25::FrameType::I) {
            std::string txt(f->info.begin(), f->info.end());
            if (txt.find("73 de K7OYK-7") != std::string::npos) ax_got_73 = true;
            ax_caller.send(ax25::Frame::make_rr(cfg.callsign, ax_caller.call(),
                                                static_cast<uint8_t>((f->ns() + 1) & 0x07),
                                                false, false));
        } else if (f->type() == ax25::FrameType::DISC) {
            ax_got_disc = true;
            ax_caller.send(ax25::Frame::make_ua(cfg.callsign, ax_caller.call(), f->pf()));
        }
    }
    assert(ax_got_73 && ax_got_disc);
    stop_thr.join();

    std::filesystem::remove_all(cfg.logging.log_path);
    std::filesystem::remove(cfg.banlist_path);
    std::cout << "[PASS] test_telnet_end_to_end\n";
}

std::string read_file_text(const std::string& path) {
    std::ifstream ifs(path);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

void test_logging_and_daily_rotation() {
    namespace fs = std::filesystem;
    const std::string dir = "/tmp/oyk_test_logging_suite";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // 1. Mask parsing (hex, binary, decimal, symbolic)
    uint32_t m = 0;
    assert(node::parse_log_mask("0", m) && m == node::LOG_SILENT);
    assert(node::parse_log_mask("0x37", m) && m == node::DEFAULT_NODE_LOG_MASK);
    assert(node::parse_log_mask("0b01110111", m) && m == node::DEFAULT_CONSOLE_LOG_MASK);
    assert(node::parse_log_mask("64", m) && m == node::LOG_APPEAL);
    assert(node::parse_log_mask("ERROR|WARNING|INFO|KICK|BAN", m) && m == 0x37);
    assert(node::parse_log_mask("DEBUG", m) && m == 0x80);
    assert(!node::parse_log_mask("INVALID_LVL", m));

    // 2. Config file parsing for logpath, consolelevel, [logfiles], banlist=
    const std::string cfg_path = dir + "/node.cfg";
    {
        std::ofstream ofs(cfg_path);
        ofs << "[node]\n"
            << "callsign = K7OYK-7\n"
            << "logpath = " << dir << "/logs/\n"
            << "consolelevel = 0x77\n"
            << "[logfiles]\n"
            << "node.log = 0x37\n"
            << "appeals.log = 0x40\n"
            << "debug.log = 0x80\n";
    }
    node::NodeConfig cfg;
    std::string err;
    assert(node::NodeConfig::load_from_file(cfg_path, cfg, err));
    assert(cfg.logging.log_path == dir + "/logs/");
    assert(cfg.logging.console_level == 0x77);
    assert(cfg.logging.log_files["node.log"] == 0x37);
    assert(cfg.logging.log_files["appeals.log"] == 0x40);
    assert(cfg.logging.log_files["debug.log"] == 0x80);
    // Default banlist.cfg sits in the same directory as node.cfg
    assert(cfg.resolved_banlist_path() == dir + "/banlist.cfg");

    // Override banlist= in [node]
    cfg.banlist_path = "/tmp/custom_bans.cfg";
    assert(cfg.resolved_banlist_path() == "/tmp/custom_bans.cfg");

    // 3. Configure Logger and verify severity routing + console filtering + daily rotation
    node::Logger logger;
    assert(logger.configure(cfg.logging, err));

    std::chrono::system_clock::time_point day1, day2;
    assert(node::BanList::parse_timestamp("20261124-120000", day1));
    assert(node::BanList::parse_timestamp("20261125-000500", day2));

    logger.log_at(node::LOG_INFO, "Day1 info msg", day1);
    logger.log_at(node::LOG_KICK, "Day1 kick msg", day1);
    logger.log_at(node::LOG_BAN, "Day1 ban msg", day1);
    logger.log_at(node::LOG_DEBUG, "Day1 secret debug msg", day1);
    logger.log_appeal("W1BAD", "Please unban me", day1);

    // Console must include INFO/KICK/BAN/APPEAL and exclude DEBUG (consolelevel=0x77)
    auto clines = logger.recent_console_logs();
    bool saw_info = false, saw_kick = false, saw_ban = false, saw_appeal = false, saw_debug = false;
    for (const auto& l : clines) {
        if (l.find("Day1 info msg") != std::string::npos) saw_info = true;
        if (l.find("Day1 kick msg") != std::string::npos) saw_kick = true;
        if (l.find("Day1 ban msg") != std::string::npos) saw_ban = true;
        if (l.find("W1BAD 20261124-120000 Please unban me") != std::string::npos) saw_appeal = true;
        if (l.find("Day1 secret debug msg") != std::string::npos) saw_debug = true;
    }
    assert(saw_info && saw_kick && saw_ban && saw_appeal && !saw_debug);

    // Check file routing before rotation
    std::string node_day1 = read_file_text(dir + "/logs/node.log");
    assert(node_day1.find("Day1 info msg") != std::string::npos);
    assert(node_day1.find("Day1 kick msg") != std::string::npos);
    assert(node_day1.find("Day1 ban msg") != std::string::npos);
    assert(node_day1.find("Day1 secret debug msg") == std::string::npos);
    assert(node_day1.find("Please unban me") == std::string::npos);

    std::string debug_day1 = read_file_text(dir + "/logs/debug.log");
    assert(debug_day1.find("Day1 secret debug msg") != std::string::npos);
    assert(debug_day1.find("Day1 info msg") == std::string::npos);

    std::string appeals_day1 = read_file_text(dir + "/logs/appeals.log");
    assert(appeals_day1 == "W1BAD 20261124-120000 Please unban me\n");

    // 4. Trigger daily rotation on Day 2
    logger.log_at(node::LOG_INFO, "Day2 fresh msg", day2);

    std::string rotated_node = dir + "/logs/node.log.20261124";
    assert(fs::exists(rotated_node));
    assert(read_file_text(rotated_node).find("Day1 info msg") != std::string::npos);

    std::string active_node = read_file_text(dir + "/logs/node.log");
    assert(active_node.find("Day2 fresh msg") != std::string::npos);
    assert(active_node.find("Day1 info msg") == std::string::npos);

    fs::remove_all(dir);
    std::cout << "[PASS] test_logging_and_daily_rotation\n";
}

void test_banlist_format_and_flags() {
    namespace fs = std::filesystem;
    const std::string path = "/tmp/oyk_test_banlist_parse.cfg";
    {
        std::ofstream ofs(path);
        ofs << "# Comment line\n"
            << "B4DCAL=20261102-130105,20271102-130105,0b0000,Banned for abusive language.\n"
            << "H1HEX-7=20261102-130105,permanent,0x09,Hard ban, with commas, preserved!\n"
            << "D1DEC=20261102-130105,20261103-000000,11,Already expired ban.\n";
    }

    node::BanList bl;
    std::string err;
    assert(bl.load_from_file(path, err));

    std::chrono::system_clock::time_point now_tp;
    assert(node::BanList::parse_timestamp("20261124-140000", now_tp));

    // Exact example line from prompt
    auto b1 = bl.find_active(ax25::Callsign("B4DCAL", 0), now_tp);
    assert(b1.has_value());
    assert(!b1->permanent);
    assert(b1->flags == 0);
    assert(b1->reason == "Banned for abusive language.");
    // Base callsign also matches any SSID
    assert(bl.is_banned(ax25::Callsign("B4DCAL", 9), now_tp));

    // Hex flags 0x09 = HARD_BAN (0x08) | APPEAL_ALLOWED (0x01), permanent, SSID-specific
    auto b2 = bl.find_active(ax25::Callsign("H1HEX", 7), now_tp);
    assert(b2.has_value());
    assert(b2->permanent);
    assert(b2->hard_ban() && b2->appeal_allowed() && !b2->notified() && !b2->appeal_used());
    assert(b2->reason == "Hard ban, with commas, preserved!");
    assert(!bl.is_banned(ax25::Callsign("H1HEX", 1), now_tp));

    // Expired ban (lift_at 20261103 < now 20261124) -> not active!
    assert(!bl.is_banned(ax25::Callsign("D1DEC", 0), now_tp));

    // Appeal text sanitization: strip control chars & non-ASCII, clamp to 140 chars
    std::string dirty = "  \x01\x1BHello\tWorld\x7F\xC3\xA9 " + std::string(200, 'A');
    std::string clean = node::BanList::sanitize_appeal_text(dirty);
    assert(clean.size() == node::MAX_APPEAL_LENGTH);
    for (unsigned char c : clean) {
        assert(c >= 0x20 && c <= 0x7E);
    }
    assert(clean.rfind("HelloWorld ", 0) == 0);

    // Save and reload round-trip
    const std::string path2 = "/tmp/oyk_test_banlist_saved.cfg";
    assert(bl.save_to_file(path2));
    node::BanList bl2;
    assert(bl2.load_from_file(path2, err));
    auto b2_reloaded = bl2.find_active(ax25::Callsign("H1HEX", 7), now_tp);
    assert(b2_reloaded.has_value() && b2_reloaded->flags == 0x09 && b2_reloaded->permanent);

    fs::remove(path);
    fs::remove(path2);
    std::cout << "[PASS] test_banlist_format_and_flags\n";
}

void test_restricted_ban_and_appeal_sessions() {
    namespace fs = std::filesystem;
    const std::string dir = "/tmp/oyk_test_ban_sessions";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const std::string ban_file = dir + "/banlist.cfg";
    {
        std::ofstream ofs(ban_file);
        // B4DCAL: unnotified HARD BAN + APPEAL ALLOWED (0b1001)
        ofs << "B4DCAL=20261102-130105,20271102-130105,0b1001,Banned for abusive language.\n";
        // S0FT: unnotified SOFT BAN + APPEAL ALLOWED (0b0001)
        ofs << "S0FT=20261102-130105,permanent,0b0001,Soft ban pending review.\n";
        // N0APP: unnotified HARD BAN + NO APPEAL (0b1000)
        ofs << "N0APP=20261102-130105,permanent,0b1000,Severe violation.\n";
    }

    node::NodeConfig cfg;
    cfg.callsign = ax25::Callsign("K7OYK", 7);
    cfg.alias = ax25::Callsign("OYKNOD", 0);
    cfg.t1_ms = 500;
    cfg.logging.log_path = dir + "/logs/";
    cfg.banlist_path = ban_file;

    radio::RadioInterfaceConfig ax_cfg;
    ax_cfg.id = "axudp0";
    ax_cfg.type = "axudp";
    ax_cfg.params["bind_host"] = "127.0.0.1";
    ax_cfg.params["bind_port"] = "0";
    cfg.interfaces = {ax_cfg};

    std::chrono::system_clock::time_point sim_now;
    assert(node::BanList::parse_timestamp("20261124-150000", sim_now));
    std::mutex clock_mu;

    node::NodeEngine engine(cfg);
    engine.set_clock([&]() {
        std::lock_guard<std::mutex> lk(clock_mu);
        return sim_now;
    });

    std::string err;
    assert(engine.start(err));

    auto* ax0 = dynamic_cast<radio::AxudpInterface*>(engine.find_interface("axudp0"));
    assert(ax0 != nullptr);
    const uint16_t port0 = ax0->bound_port();

    // Helper: collect frames until DISC or quiet
    auto recv_until_quiet = [](TestAxudpClient& cl, const ax25::Callsign& peer,
                               uint8_t& vr, bool& saw_disc, int quiet_ms = 150) {
        std::string text;
        saw_disc = false;
        while (auto f = cl.recv(quiet_ms)) {
            if (f->type() == ax25::FrameType::I) {
                text.append(reinterpret_cast<const char*>(f->info.data()), f->info.size());
                vr = static_cast<uint8_t>((f->ns() + 1) & 0x07);
                cl.send(ax25::Frame::make_rr(peer, cl.call(), vr, false, false));
            } else if (f->type() == ax25::FrameType::DISC) {
                saw_disc = true;
                cl.send(ax25::Frame::make_ua(peer, cl.call(), f->pf()));
            }
        }
        return text;
    };

    // --- A: Unnotified HARD BAN + APPEAL ALLOWED (B4DCAL) ---
    {
        TestAxudpClient bad(ax25::Callsign("B4DCAL", 0), port0);
        bad.send(ax25::Frame::make_sabm(cfg.callsign, bad.call()));
        auto ua = bad.recv();
        assert(ua.has_value() && ua->type() == ax25::FrameType::UA);

        uint8_t vr = 0;
        bool saw_disc = false;
        std::string notice = recv_until_quiet(bad, cfg.callsign, vr, saw_disc);
        assert(!saw_disc); // Held open waiting for appeal!
        assert(notice.find("RESTRICTED SESSION") != std::string::npos);
        assert(notice.find("Banned for abusive language.") != std::string::npos);
        assert(notice.find("Appeal>") != std::string::npos);

        // Submit an appeal with control chars and >140 chars
        std::string long_appeal = "\x07Sorry for the language! " + std::string(160, 'x') + "\r";
        bad.send(ax25::Frame::make_i(cfg.callsign, bad.call(), 0, vr,
                                     std::vector<uint8_t>(long_appeal.begin(), long_appeal.end())));
        std::string ack = recv_until_quiet(bad, cfg.callsign, vr, saw_disc);
        assert(ack.find("Appeal recorded (140 chars)") != std::string::npos);
        assert(saw_disc);

        // Verify appeals.log contains ONLY callsign, timestamp, and <=140 base ASCII chars
        std::string appeals_txt = read_file_text(dir + "/logs/appeals.log");
        assert(appeals_txt.find("B4DCAL 20261124-150000 Sorry for the language! ") == 0);
        // Strip trailing newline and verify exact appeal payload length == 140
        assert(!appeals_txt.empty() && appeals_txt.back() == '\n');
        appeals_txt.pop_back();
        std::string prefix = "B4DCAL 20261124-150000 ";
        std::string msg_part = appeals_txt.substr(prefix.size());
        assert(msg_part.size() == 140);
        for (unsigned char c : msg_part) {
            assert(c >= 0x20 && c <= 0x7E);
        }

        // Verify banlist.cfg persisted NOTIFIED (0b0010) and APPEAL_USED (0b0100) -> 0b1111
        node::BanList verify_bl;
        assert(verify_bl.load_from_file(ban_file, err));
        auto entry = verify_bl.find_active(ax25::Callsign("B4DCAL", 0), sim_now);
        assert(entry.has_value() && entry->notified() && entry->appeal_used() && entry->hard_ban());

        // Subsequent connect by hard-banned B4DCAL MUST be rejected with DM immediately!
        bad.send(ax25::Frame::make_sabm(cfg.callsign, bad.call()));
        auto dm = bad.recv();
        assert(dm.has_value() && dm->type() == ax25::FrameType::DM);
    }

    // --- B: Unnotified SOFT BAN + APPEAL ALLOWED (S0FT) ---
    {
        TestAxudpClient soft(ax25::Callsign("S0FT", 0), port0);
        // Day 1, 1st connect (unnotified): gets notice + appeal prompt, sends QUIT (saves appeal for later)
        soft.send(ax25::Frame::make_sabm(cfg.callsign, soft.call()));
        assert(soft.recv()->type() == ax25::FrameType::UA);
        uint8_t vr = 0;
        bool saw_disc = false;
        std::string notice = recv_until_quiet(soft, cfg.callsign, vr, saw_disc);
        assert(!saw_disc && notice.find("SOFT BAN") != std::string::npos);

        std::string quit_cmd = "QUIT\r";
        soft.send(ax25::Frame::make_i(cfg.callsign, soft.call(), 0, vr,
                                      std::vector<uint8_t>(quit_cmd.begin(), quit_cmd.end())));
        std::string bye_txt = recv_until_quiet(soft, cfg.callsign, vr, saw_disc);
        assert(saw_disc && bye_txt.find("No appeal submitted") != std::string::npos);

        // Day 1, 2nd connect (same day): rejected with DM!
        soft.send(ax25::Frame::make_sabm(cfg.callsign, soft.call()));
        auto dm_same_day = soft.recv();
        assert(dm_same_day.has_value() && dm_same_day->type() == ax25::FrameType::DM);

        // Advance clock to Day 2: soft-banned user gets 1 status/appeal check!
        {
            std::lock_guard<std::mutex> lk(clock_mu);
            assert(node::BanList::parse_timestamp("20261125-090000", sim_now));
        }
        soft.send(ax25::Frame::make_sabm(cfg.callsign, soft.call()));
        assert(soft.recv()->type() == ax25::FrameType::UA);
        vr = 0;
        std::string day2_notice = recv_until_quiet(soft, cfg.callsign, vr, saw_disc);
        assert(!saw_disc && day2_notice.find("Appeal>") != std::string::npos);

        std::string appeal2 = "Requesting review please.\r";
        soft.send(ax25::Frame::make_i(cfg.callsign, soft.call(), 0, vr,
                                      std::vector<uint8_t>(appeal2.begin(), appeal2.end())));
        std::string ack2 = recv_until_quiet(soft, cfg.callsign, vr, saw_disc);
        assert(saw_disc && ack2.find("Appeal recorded") != std::string::npos);

        // Advance clock to Day 3: soft-banned user can still check status once, but appeal is already used -> immediate DISC!
        {
            std::lock_guard<std::mutex> lk(clock_mu);
            assert(node::BanList::parse_timestamp("20261126-100000", sim_now));
        }
        soft.send(ax25::Frame::make_sabm(cfg.callsign, soft.call()));
        assert(soft.recv()->type() == ax25::FrameType::UA);
        vr = 0;
        std::string day3_notice = recv_until_quiet(soft, cfg.callsign, vr, saw_disc);
        assert(saw_disc && day3_notice.find("Already submitted") != std::string::npos);

        // Day 3, 2nd connect: rejected with DM!
        soft.send(ax25::Frame::make_sabm(cfg.callsign, soft.call()));
        assert(soft.recv()->type() == ax25::FrameType::DM);
    }

    // --- C: Unnotified HARD BAN WITHOUT APPEAL (N0APP) ---
    {
        TestAxudpClient noapp(ax25::Callsign("N0APP", 0), port0);
        noapp.send(ax25::Frame::make_sabm(cfg.callsign, noapp.call()));
        assert(noapp.recv()->type() == ax25::FrameType::UA);
        uint8_t vr = 0;
        bool saw_disc = false;
        std::string notice = recv_until_quiet(noapp, cfg.callsign, vr, saw_disc);
        assert(saw_disc);
        assert(notice.find("Severe violation.") != std::string::npos);
        assert(notice.find("Not permitted") != std::string::npos);

        // 2nd connect rejected immediately with DM
        noapp.send(ax25::Frame::make_sabm(cfg.callsign, noapp.call()));
        assert(noapp.recv()->type() == ax25::FrameType::DM);
    }

    engine.stop();
    fs::remove_all(dir);
    std::cout << "[PASS] test_restricted_ban_and_appeal_sessions\n";
}

void test_config_line_ending_preservation() {
    namespace fs = std::filesystem;
    const std::string dir = "/tmp/oyk_test_eol_suite";
    fs::remove_all(dir);
    fs::create_directories(dir);

    auto write_bin = [](const std::string& p, const std::string& data) {
        std::ofstream ofs(p, std::ios::out | std::ios::binary | std::ios::trunc);
        ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    };

    // 1. node.cfg with MIXED line endings (first line is CRLF, later lines LF & CR)
    //    First line ending (CRLF) MUST take precedence on write-back!
    const std::string cfg_mixed = dir + "/node.cfg";
    write_bin(cfg_mixed,
              "[node]\r\n"
              "callsign = K7EOL-7\n"
              "alias = EOLNOD\r"
              "logpath = " + dir + "/logs/\r\n");

    node::NodeConfig cfg;
    std::string err;
    assert(node::NodeConfig::load_from_file(cfg_mixed, cfg, err));
    assert(cfg.callsign.str() == "K7EOL-7");
    assert(cfg.alias.str() == "EOLNOD");
    assert(cfg.has_detected_line_ending);
    assert(cfg.line_ending == core::LineEnding::CRLF);

    const std::string cfg_out = dir + "/node_out.cfg";
    assert(cfg.save_to_file(cfg_out));
    std::string raw_out;
    assert(core::read_file_binary(cfg_out, raw_out));
    auto info_out = core::inspect_line_endings(raw_out);
    assert(info_out.found_any && !info_out.mixed && info_out.first == core::LineEnding::CRLF);

    // 2. CR-only banlist.cfg preserves CR when written back
    const std::string ban_cr = dir + "/banlist_cr.cfg";
    write_bin(ban_cr,
              "B4DCAL=20261102-130105,20271102-130105,0b0000,CR line ending test.\r"
              "W1MAC=20261102-130105,permanent,0b0001,Second CR entry.\r");
    node::BanList bl_cr;
    assert(bl_cr.load_from_file(ban_cr, err));
    assert(bl_cr.line_ending() == core::LineEnding::CR);
    // Clean uniform CR should NOT be overridden by node.cfg's CRLF
    bl_cr.apply_preferred_line_ending(core::LineEnding::CRLF, true);
    assert(bl_cr.line_ending() == core::LineEnding::CR);
    assert(bl_cr.save_to_file(ban_cr));
    assert(core::read_file_binary(ban_cr, raw_out));
    auto info_cr = core::inspect_line_endings(raw_out);
    assert(info_cr.found_any && !info_cr.mixed && info_cr.first == core::LineEnding::CR);

    // 3. Mixed banlist.cfg gets overridden by node.cfg's first line ending (CRLF)
    const std::string ban_mixed = dir + "/banlist_mixed.cfg";
    write_bin(ban_mixed,
              "B4DCAL=20261102-130105,20271102-130105,0b0000,Line one LF.\n"
              "W1MIX=20261102-130105,permanent,0b0001,Line two CR.\r");
    node::BanList bl_mixed;
    assert(bl_mixed.load_from_file(ban_mixed, err));
    bl_mixed.apply_preferred_line_ending(cfg.line_ending, true);
    assert(bl_mixed.line_ending() == core::LineEnding::CRLF);
    assert(bl_mixed.save_to_file(ban_mixed));
    assert(core::read_file_binary(ban_mixed, raw_out));
    auto info_mx = core::inspect_line_endings(raw_out);
    assert(info_mx.found_any && !info_mx.mixed && info_mx.first == core::LineEnding::CRLF);

    fs::remove_all(dir);
    std::cout << "[PASS] test_config_line_ending_preservation\n";
}

void test_local_terminal_and_yapp() {
    namespace fs = std::filesystem;

    // 1. Filename sanitization & path traversal protection
    assert(protocol::is_safe_yapp_filename("important-file.txt"));
    assert(protocol::is_safe_yapp_filename("bulletin_01.bin"));
    assert(!protocol::is_safe_yapp_filename("../important-file.txt"));
    assert(!protocol::is_safe_yapp_filename("..\\important-file.txt"));
    assert(!protocol::is_safe_yapp_filename("foo/../bar.txt"));
    assert(!protocol::is_safe_yapp_filename("sub/file.txt"));
    assert(!protocol::is_safe_yapp_filename("sub\\file.txt"));
    assert(!protocol::is_safe_yapp_filename("C:\\Windows\\system.ini"));
    assert(!protocol::is_safe_yapp_filename(".env"));
    assert(!protocol::is_safe_yapp_filename("CON"));
    assert(!protocol::is_safe_yapp_filename("COM1.txt"));

    ax25::Callsign remote_call("N0CALL", 11);
    auto resolved = protocol::resolve_yapp_incoming_path("incoming", remote_call, "important-file.txt");
    assert(resolved == fs::path("incoming") / "N0CALL-11" / "important-file.txt");
    assert(protocol::resolve_yapp_incoming_path("incoming", remote_call, "../escape.txt").empty());
    assert(protocol::resolve_yapp_incoming_path("incoming", remote_call, "..\\escape.txt").empty());

    // 2. End-to-end Local Terminal outbound session + bidirectional YAPP-C transfer over AXUDP
    const fs::path tmp_root = "/tmp/oyk_test_yapp_e2e";
    fs::remove_all(tmp_root);
    fs::create_directories(tmp_root);
    const fs::path incoming_dir = tmp_root / "incoming";
    const fs::path remote_in_dir = tmp_root / "remote_incoming";
    const fs::path upload_src = tmp_root / "upload-test.bin";
    const fs::path download_src = tmp_root / "important-file.txt";

    // Write binary payload with all byte values (0x00..0xFF) to prove binary AX.25 + YAPP-C safety
    {
        std::vector<uint8_t> bin_data(300);
        for (std::size_t i = 0; i < bin_data.size(); ++i) {
            bin_data[i] = static_cast<uint8_t>(i & 0xFFu);
        }
        std::FILE* fp = std::fopen(upload_src.c_str(), "wb");
        assert(fp != nullptr);
        assert(std::fwrite(bin_data.data(), 1, bin_data.size(), fp) == bin_data.size());
        std::fclose(fp);
    }
    {
        const std::string text = "Important packet radio bulletin from N0CALL-11!\r\n73!\r\n";
        std::FILE* fp = std::fopen(download_src.c_str(), "wb");
        assert(fp != nullptr);
        assert(std::fwrite(text.data(), 1, text.size(), fp) == text.size());
        std::fclose(fp);
    }

    node::NodeConfig cfg;
    cfg.callsign = ax25::Callsign("K7OYK", 7);
    cfg.t1_ms = 300;
    cfg.logging.log_path = (tmp_root / "logs").string();
    cfg.banlist_path = (tmp_root / "banlist.cfg").string();
    radio::RadioInterfaceConfig ax_cfg;
    ax_cfg.id = "axudp0";
    ax_cfg.type = "axudp";
    ax_cfg.params["bind_host"] = "127.0.0.1";
    ax_cfg.params["bind_port"] = "0";
    cfg.interfaces = {ax_cfg};

    node::NodeEngine engine(cfg);
    std::string err;
    assert(engine.start(err));
    auto* ax0 = dynamic_cast<radio::AxudpInterface*>(engine.find_interface("axudp0"));
    assert(ax0 != nullptr);

    TestAxudpClient remote_node(remote_call, ax0->bound_port());
    assert(ax0->add_peer("N0CALL-11", "127.0.0.1", remote_node.local_port()));

    std::mutex term_mtx;
    bool local_connected = false;
    bool local_closed = false;
    protocol::YappSession local_yapp(incoming_dir);
    local_yapp.set_remote_callsign(remote_call);
    uint64_t local_sid = 0;

    local_yapp.set_send_callback([&](const std::vector<uint8_t>& pkt) {
        engine.send_local_terminal_bytes(local_sid, pkt);
    });

    node::NodeEngine::LocalTerminalCallbacks cbs;
    cbs.on_connected = [&](uint64_t) {
        std::lock_guard<std::mutex> lk(term_mtx);
        local_connected = true;
    };
    cbs.on_rx_bytes = [&](uint64_t, const std::vector<uint8_t>& bytes) {
        std::lock_guard<std::mutex> lk(term_mtx);
        std::vector<uint8_t> plain;
        local_yapp.feed_bytes(bytes.data(), bytes.size(), plain);
    };
    cbs.on_closed = [&](uint64_t, const std::string&) {
        std::lock_guard<std::mutex> lk(term_mtx);
        local_closed = true;
    };

    ax25::Callsign resolved_c;
    assert(engine.open_local_terminal("axudp0", "N0CALL-11", std::move(cbs), local_sid, &resolved_c, err));
    assert(resolved_c == remote_call);

    auto sabm = remote_node.recv(1000);
    assert(sabm.has_value() && sabm->type() == ax25::FrameType::SABM);
    remote_node.send(ax25::Frame::make_ua(cfg.callsign, remote_call, true));

    auto wait_until = [](auto pred, int timeout_ms = 1500) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (pred()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return pred();
    };

    assert(wait_until([&]() {
        std::lock_guard<std::mutex> lk(term_mtx);
        return local_connected;
    }));

    // Remote YAPP helper over AXUDP I-frames
    uint8_t rem_vs = 0;
    uint8_t rem_vr = 0;
    protocol::YappSession remote_yapp(remote_in_dir);
    remote_yapp.set_remote_callsign(cfg.callsign);
    remote_yapp.set_send_callback([&](const std::vector<uint8_t>& pkt) {
        std::size_t pos = 0;
        while (pos < pkt.size()) {
            std::size_t chunk = std::min<std::size_t>(200, pkt.size() - pos);
            std::vector<uint8_t> seg(pkt.begin() + static_cast<std::ptrdiff_t>(pos),
                                     pkt.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
            remote_node.send(ax25::Frame::make_i(cfg.callsign, remote_call, rem_vs, rem_vr, seg, false));
            rem_vs = static_cast<uint8_t>((rem_vs + 1) & 0x07u);
            pos += chunk;
        }
    });

    auto pump_remote = [&](auto done_pred, int timeout_ms = 2000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!done_pred() && std::chrono::steady_clock::now() < deadline) {
            auto f = remote_node.recv(30);
            if (!f) continue;
            if (f->type() == ax25::FrameType::I && f->ns() == rem_vr) {
                rem_vr = static_cast<uint8_t>((rem_vr + 1) & 0x07u);
                remote_node.send(ax25::Frame::make_rr(cfg.callsign, remote_call, rem_vr, false, false));
                std::vector<uint8_t> plain;
                remote_yapp.feed_bytes(f->info.data(), f->info.size(), plain);
            }
        }
        return done_pred();
    };

    // 2a. Local Terminal uploads binary file to remote node via YAPP-C
    {
        std::lock_guard<std::mutex> lk(term_mtx);
        assert(local_yapp.start_send(upload_src, err));
    }
    assert(pump_remote([&]() {
        std::lock_guard<std::mutex> lk(term_mtx);
        return local_yapp.progress().state == protocol::YappState::Complete &&
               remote_yapp.progress().state == protocol::YappState::Complete;
    }));
    const fs::path rem_saved = remote_in_dir / "K7OYK-7" / "upload-test.bin";
    assert(fs::exists(rem_saved) && fs::file_size(rem_saved) == 300);

    // 2b. Remote node N0CALL-11 sends important-file.txt -> local terminal saves to incoming/N0CALL-11/important-file.txt
    {
        std::lock_guard<std::mutex> lk(term_mtx);
        local_yapp.reset();
    }
    remote_yapp.reset();
    assert(remote_yapp.start_send(download_src, err));
    assert(pump_remote([&]() {
        std::lock_guard<std::mutex> lk(term_mtx);
        return local_yapp.progress().state == protocol::YappState::Complete &&
               remote_yapp.progress().state == protocol::YappState::Complete;
    }));
    const fs::path local_saved = incoming_dir / "N0CALL-11" / "important-file.txt";
    assert(fs::exists(local_saved));
    std::string content1;
    assert(core::read_file_binary(local_saved.string(), content1));
    assert(content1 == "Important packet radio bulletin from N0CALL-11!\r\n73!\r\n");

    // 2c. Remote sends important-file.txt again -> pauses in RecvAwaitOverwrite; declining preserves original!
    {
        const std::string new_txt = "OVERWRITTEN!\r\n";
        std::FILE* fp = std::fopen(download_src.c_str(), "wb");
        assert(fp != nullptr);
        assert(std::fwrite(new_txt.data(), 1, new_txt.size(), fp) == new_txt.size());
        std::fclose(fp);
    }
    {
        std::lock_guard<std::mutex> lk(term_mtx);
        local_yapp.reset();
    }
    remote_yapp.reset();
    assert(remote_yapp.start_send(download_src, err));
    assert(pump_remote([&]() {
        std::lock_guard<std::mutex> lk(term_mtx);
        return local_yapp.awaiting_overwrite();
    }));
    {
        std::lock_guard<std::mutex> lk(term_mtx);
        local_yapp.resolve_overwrite(false);
    }
    assert(pump_remote([&]() {
        std::lock_guard<std::mutex> lk(term_mtx);
        return local_yapp.progress().state == protocol::YappState::Aborted &&
               remote_yapp.progress().state == protocol::YappState::Aborted;
    }));
    std::string content_after_decline;
    assert(core::read_file_binary(local_saved.string(), content_after_decline));
    assert(content_after_decline == content1);

    // 2d. Remote sends important-file.txt a third time -> approving overwrite replaces file!
    {
        std::lock_guard<std::mutex> lk(term_mtx);
        local_yapp.reset();
    }
    remote_yapp.reset();
    assert(remote_yapp.start_send(download_src, err));
    assert(pump_remote([&]() {
        std::lock_guard<std::mutex> lk(term_mtx);
        return local_yapp.awaiting_overwrite();
    }));
    {
        std::lock_guard<std::mutex> lk(term_mtx);
        local_yapp.resolve_overwrite(true);
    }
    assert(pump_remote([&]() {
        std::lock_guard<std::mutex> lk(term_mtx);
        return local_yapp.progress().state == protocol::YappState::Complete;
    }));
    std::string content_after_allow;
    assert(core::read_file_binary(local_saved.string(), content_after_allow));
    assert(content_after_allow == "OVERWRITTEN!\r\n");

    assert(engine.close_local_terminal(local_sid));
    engine.stop();
    fs::remove_all(tmp_root);
    std::cout << "[PASS] test_local_terminal_and_yapp\n";
}

void test_ban_lift_chat_and_local_loopback() {
    namespace fs = std::filesystem;

    // 1. Ban lift spec parser (default permanent, minutes, hours, days, weeks, dates)
    std::chrono::system_clock::time_point base_now{};
    assert(node::BanList::parse_timestamp("20261124-120000", base_now));

    std::optional<std::chrono::system_clock::time_point> lift_opt;
    std::string perr;
    assert(node::BanList::parse_lift_spec("", base_now, lift_opt, &perr) && !lift_opt.has_value());
    assert(node::BanList::parse_lift_spec("permanent", base_now, lift_opt, &perr) && !lift_opt.has_value());
    assert(node::BanList::parse_lift_spec("30m", base_now, lift_opt, &perr) && lift_opt.has_value());
    assert(node::BanList::format_timestamp(*lift_opt) == "20261124-123000");
    assert(node::BanList::parse_lift_spec("12 hours", base_now, lift_opt, &perr) && lift_opt.has_value());
    assert(node::BanList::format_timestamp(*lift_opt) == "20261125-000000");
    assert(node::BanList::parse_lift_spec("7d", base_now, lift_opt, &perr) && lift_opt.has_value());
    assert(node::BanList::format_timestamp(*lift_opt) == "20261201-120000");
    assert(node::BanList::parse_lift_spec("2027-06-15", base_now, lift_opt, &perr) && lift_opt.has_value());
    assert(node::BanList::format_timestamp(*lift_opt) == "20270615-235959");
    assert(node::BanList::parse_lift_spec("2027-06-15 08:45", base_now, lift_opt, &perr) && lift_opt.has_value());
    assert(node::BanList::format_timestamp(*lift_opt) == "20270615-084500");
    assert(!node::BanList::parse_lift_spec("not-a-duration", base_now, lift_opt, &perr));
    assert(!node::BanList::parse_lift_spec("0m", base_now, lift_opt, &perr));

    // 2. Example configs/node.cfg places CHAT on SSID 12 by default
    node::NodeConfig loaded_cfg;
    std::string err;
    assert(node::NodeConfig::load_from_file((fs::exists("configs/node.cfg") ? "configs/node.cfg" : "../configs/node.cfg"), loaded_cfg, err));
    assert(loaded_cfg.chat.enabled);
    assert(loaded_cfg.chat.ssid == 12);
    assert(loaded_cfg.resolved_chat_callsign().str() == "N0CALL-12");

    // 3. Multi-user CHAT (SSID 12 direct connect + Local Terminal loopback)
    const fs::path tmp_root = "/tmp/oyk_test_chat_local";
    fs::remove_all(tmp_root);
    fs::create_directories(tmp_root);

    node::NodeConfig cfg;
    cfg.callsign = ax25::Callsign("K7OYK", 7);
    cfg.chat.enabled = true;
    cfg.chat.ssid = 12;
    cfg.t1_ms = 300;
    cfg.logging.log_path = (tmp_root / "logs").string();
    cfg.banlist_path = (tmp_root / "banlist.cfg").string();

    radio::RadioInterfaceConfig ax_cfg;
    ax_cfg.id = "axudp0";
    ax_cfg.type = "axudp";
    ax_cfg.params["bind_host"] = "127.0.0.1";
    ax_cfg.params["bind_port"] = "0";
    cfg.interfaces = {ax_cfg};

    node::NodeEngine engine(cfg);
    assert(engine.start(err));
    auto* ax0 = dynamic_cast<radio::AxudpInterface*>(engine.find_interface("axudp0"));
    assert(ax0 != nullptr);

    // Local Terminal connects to local node (is_local == true!)
    std::mutex lmtx;
    bool local_conn = false;
    std::string local_rx;
    node::NodeEngine::LocalTerminalCallbacks cbs;
    cbs.on_connected = [&](uint64_t) {
        std::lock_guard<std::mutex> lk(lmtx);
        local_conn = true;
    };
    cbs.on_rx_bytes = [&](uint64_t, const std::vector<uint8_t>& b) {
        std::lock_guard<std::mutex> lk(lmtx);
        local_rx.append(reinterpret_cast<const char*>(b.data()), b.size());
    };
    uint64_t local_sid = 0;
    ax25::Callsign local_target;
    bool is_local = false;
    assert(engine.open_local_terminal("local", "", std::move(cbs), local_sid, &local_target, err, &is_local));
    assert(is_local);
    assert(local_conn);
    assert(local_target.str() == "K7OYK-7");
    assert(local_rx.find("Connected to K7OYK-7") != std::string::npos);

    // Local operator enters CHAT via node shell
    const std::string enter_chat = "CHAT\r";
    assert(engine.send_local_terminal_bytes(local_sid, std::vector<uint8_t>(enter_chat.begin(), enter_chat.end())));
    assert(local_rx.find("K7OYK-12 Packet Chat") != std::string::npos);

    // Remote AXUDP user connects directly to CHAT SSID 12 (K7OYK-12)
    ax25::Callsign chat_call("K7OYK", 12);
    TestAxudpClient rem(ax25::Callsign("W1CHT", 0), ax0->bound_port());
    rem.send(ax25::Frame::make_sabm(chat_call, rem.call()));
    auto ua = rem.recv(1000);
    assert(ua.has_value() && ua->type() == ax25::FrameType::UA);
    uint8_t rem_vr = 0;
    std::string rem_welcome = rem.recv_all_text(chat_call, rem_vr, 200);
    assert(rem_welcome.find("K7OYK-12 Packet Chat") != std::string::npos);

    // Local user broadcasts a chat message -> remote W1CHT receives it!
    const std::string hello = "Hello from local sysop!\r";
    assert(engine.send_local_terminal_bytes(local_sid, std::vector<uint8_t>(hello.begin(), hello.end())));
    std::string rem_got = rem.recv_all_text(chat_call, rem_vr, 200);
    assert(rem_got.find("<K7OYK-7> Hello from local sysop!") != std::string::npos);

    // Remote W1CHT replies -> local terminal receives it!
    const std::string reply = "Hi back from RF!\r";
    rem.send(ax25::Frame::make_i(chat_call, rem.call(), 0, rem_vr,
                                 std::vector<uint8_t>(reply.begin(), reply.end())));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lk(lmtx);
            if (local_rx.find("<W1CHT> Hi back from RF!") != std::string::npos) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    {
        std::lock_guard<std::mutex> lk(lmtx);
        assert(local_rx.find("<W1CHT> Hi back from RF!") != std::string::npos);
    }

    // Remote W1CHT leaves direct SSID-12 CHAT with /q -> gets 73 + DISC
    const std::string quit_chat = "/q\r";
    rem.send(ax25::Frame::make_i(chat_call, rem.call(), 1, rem_vr,
                                 std::vector<uint8_t>(quit_chat.begin(), quit_chat.end())));
    bool got_73 = false, got_disc = false;
    auto qdl = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while ((!got_73 || !got_disc) && std::chrono::steady_clock::now() < qdl) {
        auto f = rem.recv(50);
        if (!f) continue;
        if (f->type() == ax25::FrameType::I) {
            std::string s(f->info.begin(), f->info.end());
            if (s.find("73 de K7OYK-12") != std::string::npos) got_73 = true;
            rem_vr = static_cast<uint8_t>((f->ns() + 1) & 0x07u);
            rem.send(ax25::Frame::make_rr(chat_call, rem.call(), rem_vr, false, false));
        } else if (f->type() == ax25::FrameType::DISC) {
            got_disc = true;
            rem.send(ax25::Frame::make_ua(chat_call, rem.call(), f->pf()));
        }
    }
    assert(got_73 && got_disc);

    // Local user leaves CHAT with /q -> returns cleanly to K7OYK-7> node prompt
    assert(engine.send_local_terminal_bytes(local_sid, std::vector<uint8_t>(quit_chat.begin(), quit_chat.end())));
    {
        std::lock_guard<std::mutex> lk(lmtx);
        assert(local_rx.find("Returning to node.") != std::string::npos);
    }

    assert(engine.close_local_terminal(local_sid));
    engine.stop();
    fs::remove_all(tmp_root);
    std::cout << "[PASS] test_ban_lift_chat_and_local_loopback\n";
}

} // namespace

int main() {
    test_callsign_and_crc();
    test_frame_codec();
    test_config_loader();
    test_node_axudp_end_to_end();
    test_kiss_codec();
    test_axip_packet_parsing();
    test_kiss_serial_pty_end_to_end();
    test_kiss_tcp_end_to_end();
    test_telnet_end_to_end();
    test_logging_and_daily_rotation();
    test_banlist_format_and_flags();
    test_restricted_ban_and_appeal_sessions();
    test_config_line_ending_preservation();
    test_local_terminal_and_yapp();
    test_ban_lift_chat_and_local_loopback();
    std::cout << "All oyk-node tests passed!\n";
    return 0;
}
