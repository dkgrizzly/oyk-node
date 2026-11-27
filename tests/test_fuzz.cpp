// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ax25/callsign.hpp"
#include "oyk/ax25/crc.hpp"
#include "oyk/ax25/frame.hpp"
#include "oyk/node/ban_list.hpp"
#include "oyk/node/node_config.hpp"
#include "oyk/radio/kiss.hpp"

#include "oyk_portable.hpp"
#include "oyk_ppp.hpp"
#include "oyk_term.hpp"
#include "oyk_yapp.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define oyk_getpid _getpid
#else
#include <unistd.h>
#define oyk_getpid getpid
#endif

namespace {

struct XorShift64 {
    std::uint64_t s;
    explicit XorShift64(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t next() {
        std::uint64_t x = s;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        s = x;
        return x * 0x2545F4914F6CDD1DULL;
    }
    std::size_t range(std::size_t n) {
        return n ? static_cast<std::size_t>(next() % n) : 0U;
    }
    std::uint8_t byte() { return static_cast<std::uint8_t>(next() & 0xFFU); }
};

void mutate_bytes(XorShift64& rng,
                  const std::vector<std::vector<std::uint8_t>>& seeds,
                  std::vector<std::uint8_t>& out,
                  std::size_t max_len) {
    out.clear();
    if (seeds.empty() || rng.range(10) == 0) {
        std::size_t len = rng.range(std::min<std::size_t>(max_len, 320));
        out.resize(len);
        for (std::size_t i = 0; i < len; ++i) out[i] = rng.byte();
        return;
    }
    out = seeds[rng.range(seeds.size())];
    std::size_t ops = 1 + rng.range(4);
    for (std::size_t k = 0; k < ops; ++k) {
        std::size_t kind = rng.range(6);
        if (kind == 0 && !out.empty()) {
            out[rng.range(out.size())] ^= static_cast<std::uint8_t>(1U << rng.range(8));
        } else if (kind == 1 && !out.empty()) {
            out[rng.range(out.size())] = rng.byte();
        } else if (kind == 2 && out.size() > 1) {
            out.resize(rng.range(out.size()) + 1);
        } else if (kind == 3 && out.size() < max_len) {
            std::size_t add = 1 + rng.range(24);
            for (std::size_t i = 0; i < add && out.size() < max_len; ++i) {
                out.push_back(rng.byte());
            }
        } else if (kind == 4 && out.size() > 2) {
            std::size_t pos = rng.range(out.size());
            out.erase(out.begin() + static_cast<std::ptrdiff_t>(pos));
        } else if (kind == 5 && !out.empty()) {
            static const std::uint8_t kCtrl[6] = { '\r', '\n', '\0', 0x7EU, 0xC0U, 0xDBU };
            out[rng.range(out.size())] = kCtrl[rng.range(6)];
        }
    }
}

std::vector<std::uint8_t> to_bytes(const char* s) {
    const std::size_t n = std::strlen(s);
    return std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s),
                                     reinterpret_cast<const std::uint8_t*>(s) + n);
}

void fuzz_ax25(XorShift64& rng, std::size_t iters) {
    std::vector<std::vector<std::uint8_t>> seeds;
    auto c1 = oyk::ax25::Callsign::parse("KR4OYK-7");
    auto c2 = oyk::ax25::Callsign::parse("W4ABC-1");
    if (c1 && c2) {
        seeds.push_back(oyk::ax25::Frame::make_sabm(*c1, *c2).encode());
        std::vector<uint8_t> msg{'H', 'e', 'l', 'l', 'o', '\r', '\n'};
        seeds.push_back(oyk::ax25::Frame::make_ui(*c1, *c2, msg).encode());
        seeds.push_back(oyk::ax25::Frame::make_i(*c1, *c2, 3, 5, msg, true).encode());
    }
    std::vector<std::uint8_t> buf;
    for (std::size_t i = 0; i < iters; ++i) {
        mutate_bytes(rng, seeds, buf, 512);
        const uint8_t* ptr = buf.empty() ? reinterpret_cast<const uint8_t*>("") : buf.data();
        (void)oyk::ax25::crc16_fcs(ptr, buf.size());
        (void)oyk::ax25::verify_fcs(ptr, buf.size());
        auto decoded = oyk::ax25::Frame::decode(ptr, buf.size());
        if (decoded) {
            (void)decoded->type_name();
            (void)decoded->summary();
            (void)decoded->encode();
        }

        OykFrame pf;
        (void)oyk_crc16_fcs(ptr, buf.size());
        (void)oyk_verify_fcs(ptr, buf.size());
        if (oyk_ax25_decode(ptr, buf.size(), &pf)) {
            oyk_u8 enc[512];
            (void)oyk_ax25_encode(&pf, enc, sizeof(enc));
        }
    }
}

void fuzz_kiss(XorShift64& rng, std::size_t iters) {
    std::vector<std::vector<std::uint8_t>> seeds;
    seeds.push_back(oyk::radio::kiss_encode(0, {0x82, 0xA0, 0x9C, 0xC0, 0xDB, 0x03, 0xF0, 'O', 'K'}));
    seeds.push_back(oyk::radio::kiss_encode_param(2, oyk::radio::KISS_CMD_TXDELAY, 30));

    oyk::radio::KissDecoder dec20;
    OykKissDecoder dec98;
    oyk_kiss_decoder_init(&dec98);
    std::vector<std::uint8_t> buf;
    for (std::size_t i = 0; i < iters; ++i) {
        mutate_bytes(rng, seeds, buf, 2400);
        if ((i & 31U) == 0U) {
            dec20.reset();
            oyk_kiss_decoder_init(&dec98);
        }
        const uint8_t* ptr = buf.empty() ? reinterpret_cast<const uint8_t*>("") : buf.data();
        (void)dec20.feed(ptr, buf.size());
        for (std::size_t j = 0; j < buf.size(); ++j) {
            oyk_u8 port = 0, cmd = 0;
            (void)oyk_kiss_decoder_feed_byte(&dec98, buf[j], &port, &cmd);
        }
    }
}

void fuzz_config(XorShift64& rng, std::size_t iters) {
    std::vector<std::vector<std::uint8_t>> seeds;
    seeds.push_back(to_bytes(
        "[node]\r\ncallsign = KR4OYK-7\r\nalias = OYKNOD\r\n"
        "[interface.axudp0]\r\ntype = axudp\r\nhost = 127.0.0.1\r\nport = 10093\r\n"
        "[peer.p0]\r\ncallsign = W4ABC-7\r\ninterface = axudp0\r\nhost = 127.0.0.1\r\nport = 10093\r\n"));
    seeds.push_back(to_bytes(
        "[node]\rcallsign = N0CALL\r[bbs]\renabled = 1\rssid = 11\r[chat]\renabled = 1\rssid = 12\r"
        "[printer]\renabled = 1\rdriver = escp2\rcolumns = 80\r"));

    char tmp_path[128];
    std::snprintf(tmp_path, sizeof(tmp_path), "/tmp/oyk_fuzz_cfg_%ld.cfg",
                  static_cast<long>(oyk_getpid()));

    const std::size_t file_iters = std::min<std::size_t>(iters, 600);
    std::vector<std::uint8_t> buf;
    OykConfig* pcfg = new OykConfig();
    for (std::size_t i = 0; i < file_iters; ++i) {
        mutate_bytes(rng, seeds, buf, 4096);
        std::FILE* fp = std::fopen(tmp_path, "wb");
        if (fp) {
            if (!buf.empty()) std::fwrite(buf.data(), 1, buf.size(), fp);
            std::fclose(fp);
        }
        oyk::node::NodeConfig cfg20;
        std::string err20;
        (void)oyk::node::NodeConfig::load_from_file(tmp_path, cfg20, err20);

        char err98[96];
        (void)oyk_config_load(tmp_path, pcfg, err98, sizeof(err98));
    }
    delete pcfg;
    std::remove(tmp_path);

    /* Keep high-iteration pass on in-memory callsign/peer spec parsers */
    for (std::size_t i = 0; i < iters; ++i) {
        mutate_bytes(rng, seeds, buf, 96);
        std::string s(buf.begin(), buf.end());
        (void)oyk::node::NodeConfig::parse_callsign_with_default_ssid(s);
        std::vector<oyk::radio::PeerConfig> existing;
        oyk::radio::PeerConfig peer_out;
        (void)oyk::node::parse_legacy_peer_spec(s, "axudp0", 10093, existing, peer_out);

        char cstr[97];
        std::size_t n = std::min<std::size_t>(s.size(), 96U);
        if (n > 0U) std::memcpy(cstr, s.data(), n);
        cstr[n] = '\0';
        OykCallsign cs;
        (void)oyk_parse_callsign(cstr, &cs);
        char pcall[16];
        oyk_u32 ip = 0;
        oyk_u16 port = 0;
        (void)oyk_parse_peer_spec(cstr, pcall, sizeof(pcall), &ip, &port, 10093);
    }
}

void fuzz_banlist(XorShift64& rng, std::size_t iters) {
    std::vector<std::vector<std::uint8_t>> seeds;
    seeds.push_back(to_bytes("N0BAD|20260101-120000|20260108-120000|0b1001|Flooding\r\n"));
    seeds.push_back(to_bytes("W4XYZ-7,permanent,0x05,Needs manual review\n"));
    seeds.push_back(to_bytes("  \x01Hello \x1b[31mworld\r\n\tAppeal text here!  "));

    char tmp_path[128];
    std::snprintf(tmp_path, sizeof(tmp_path), "/tmp/oyk_fuzz_ban_%ld.cfg",
                  static_cast<long>(oyk_getpid()));
    const auto now = std::chrono::system_clock::now();
    std::vector<std::uint8_t> buf;

    for (std::size_t i = 0; i < iters; ++i) {
        mutate_bytes(rng, seeds, buf, 256);
        std::string s(buf.begin(), buf.end());
        (void)oyk::node::BanList::normalize_key(s);
        uint32_t f20 = 0;
        (void)oyk::node::BanList::parse_flags(s, f20);
        std::chrono::system_clock::time_point tp;
        (void)oyk::node::BanList::parse_timestamp(s, tp);
        std::optional<std::chrono::system_clock::time_point> lift;
        (void)oyk::node::BanList::parse_lift_spec(s, now, lift);
        (void)oyk::node::BanList::sanitize_appeal_text(s);

        char cstr[257];
        std::size_t n = std::min<std::size_t>(s.size(), 256U);
        if (n > 0U) std::memcpy(cstr, s.data(), n);
        cstr[n] = '\0';
        oyk_u32 f98 = 0;
        (void)oyk_parse_flags_u32(cstr, &f98);
        char out[160];
        (void)oyk_parse_lift_spec(cstr, static_cast<time_t>(1700000000L), out, sizeof(out));
        oyk_sanitize_appeal(cstr, out, sizeof(out));

        if (i < std::min<std::size_t>(iters, 400)) {
            std::FILE* fp = std::fopen(tmp_path, "wb");
            if (fp) {
                if (!buf.empty()) std::fwrite(buf.data(), 1, buf.size(), fp);
                std::fclose(fp);
            }
            oyk::node::BanList bl;
            std::string berr;
            (void)bl.load_from_file(tmp_path, berr);
        }
    }
    std::remove(tmp_path);
}

int yapp_noop_send(const oyk_u8*, size_t, void*) { return 1; }
int yapp_deny_overwrite(const char*, const char*, void*) { return 0; }

void fuzz_yapp(XorShift64& rng, std::size_t iters) {
    std::vector<std::vector<std::uint8_t>> seeds;
    seeds.push_back({OYK_YAPP_ENQ, 0x01});
    seeds.push_back({OYK_YAPP_SOH, 12, 't', 'e', 's', 't', '.', 't', 'x', 't', 0, '1', '0', 0});
    seeds.push_back({OYK_YAPP_STX, 4, 'a', 'b', 'c', 'd', 0x12});
    seeds.push_back({OYK_YAPP_ETX, 0x01});
    seeds.push_back({OYK_YAPP_EOT, 0x01});
    seeds.push_back({OYK_YAPP_CAN, 5, 'a', 'b', 'o', 'r', 't'});

    char ydir[128];
    std::snprintf(ydir, sizeof(ydir), "/tmp/oyk_fuzz_yapp_%ld", static_cast<long>(oyk_getpid()));
    OykYappSession* ys = new OykYappSession();
    oyk_yapp_init(ys, ydir, yapp_noop_send, NULL, yapp_deny_overwrite, NULL);
    OykCallsign rc;
    oyk_parse_callsign("W4ABC-7", &rc);
    oyk_yapp_set_remote(ys, &rc);

    std::vector<std::uint8_t> buf;
    oyk_u8 plain[512];
    for (std::size_t i = 0; i < iters; ++i) {
        if ((i & 63U) == 0U) {
            oyk_yapp_reset(ys);
            oyk_yapp_set_remote(ys, &rc);
        }
        mutate_bytes(rng, seeds, buf, 300);
        const oyk_u8* ptr = buf.empty() ? reinterpret_cast<const oyk_u8*>("") : buf.data();
        (void)oyk_yapp_feed_bytes(ys, ptr, buf.size(), plain, sizeof(plain));
        if (!buf.empty()) {
            std::string fn(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(buf.size(), 96U)));
            std::replace(fn.begin(), fn.end(), '\0', '_');
            (void)oyk_yapp_is_safe_filename(fn.c_str());
        }
    }
    oyk_yapp_reset(ys);
    delete ys;
}

void fuzz_terminal(XorShift64& rng, std::size_t iters) {
    std::vector<std::vector<std::uint8_t>> seeds;
    seeds.push_back(to_bytes("Welcome to KR4OYK-7\r\nLine two\rLine three\n\rPrompt> "));
    seeds.push_back(to_bytes("Long words supercalifragilisticexpialidocious wrap-check end.\n"));

    OykTermEolParser eol;
    oyk_term_eol_init(&eol, OYK_TERM_EOL_CRLF, 1);
    std::vector<std::uint8_t> buf;
    OykTermWrapSpan spans[32];
    char dst[512];
    for (std::size_t i = 0; i < iters; ++i) {
        if ((i & 31U) == 0U) {
            oyk_term_eol_reset(&eol, OYK_TERM_EOL_CR, static_cast<int>(i & 1U));
        }
        mutate_bytes(rng, seeds, buf, 256);
        for (std::size_t j = 0; j < buf.size(); ++j) {
            (void)oyk_term_eol_feed_byte(&eol, buf[j]);
        }
        std::string txt(buf.begin(), buf.end());
        int w = static_cast<int>((i % 132U)) - 2;
        (void)oyk_term_wrap_line(txt.data(), txt.size(), w, spans, 32);
        (void)oyk_term_count_wrapped_rows(txt.data(), txt.size(), w);
        (void)oyk_term_format_tx_text(txt.c_str(), static_cast<int>(i % 4U), dst, sizeof(dst));
        (void)oyk_term_decode_ctrl_a_cmd(static_cast<int>(rng.byte()));
    }
}

size_t ppp_noop_tx(const oyk_u8*, size_t len, void*) { return len; }
void ppp_noop_ax25(int, const OykFrame*, oyk_u32, oyk_u16, void*) {}
void ppp_noop_ip(const oyk_u8*, size_t, void*) {}
void ppp_noop_udp(oyk_u32, oyk_u16, oyk_u16, const oyk_u8*, size_t, void*) {}

void fuzz_ppp(XorShift64& rng, std::size_t iters) {
    std::vector<std::vector<std::uint8_t>> seeds;
    oyk_u8 raw_out[256];
    const oyk_u8 lcp_req[] = {0x01, 0x01, 0x00, 0x0A, 0x05, 0x06, 0x12, 0x34, 0x56, 0x78};
    size_t n1 = oyk_ppp_hdlc_encode(OYK_PPP_PROTO_LCP, lcp_req, sizeof(lcp_req),
                                    0xFFFFFFFFUL, raw_out, sizeof(raw_out));
    if (n1 > 0) seeds.emplace_back(raw_out, raw_out + n1);

    const oyk_u8 ipcp_req[] = {0x01, 0x02, 0x00, 0x0A, 0x03, 0x06, 10, 0, 0, 2};
    size_t n2 = oyk_ppp_hdlc_encode(OYK_PPP_PROTO_IPCP, ipcp_req, sizeof(ipcp_req),
                                    0x00000000UL, raw_out, sizeof(raw_out));
    if (n2 > 0) seeds.emplace_back(raw_out, raw_out + n2);

    const oyk_u8 ipv4_udp[] = {
        0x45, 0x00, 0x00, 28, 0x00, 0x01, 0x00, 0x00, 64, OYK_IP_PROTO_UDP, 0, 0,
        10, 0, 0, 2, 10, 0, 0, 1, 0x27, 0x6D, 0x27, 0x6D, 0x00, 8, 0, 0
    };
    size_t n3 = oyk_ppp_hdlc_encode(OYK_PPP_PROTO_IP, ipv4_udp, sizeof(ipv4_udp),
                                    0x00000000UL, raw_out, sizeof(raw_out));
    if (n3 > 0) seeds.emplace_back(raw_out, raw_out + n3);

    OykPppLink* link = new OykPppLink();
    link->init(0x0A000001UL, 0x0A000002UL, 10093U, 0x11223344UL, ppp_noop_tx, NULL);
    link->set_ax25_rx_callback(ppp_noop_ax25, NULL);
    link->set_raw_ip_rx_callback(ppp_noop_ip, NULL);
    link->set_udp_rx_callback(ppp_noop_udp, NULL);
    link->open(0);

    std::vector<std::uint8_t> buf;
    for (std::size_t i = 0; i < iters; ++i) {
        if ((i & 127U) == 0U) {
            link->init(0x0A000001UL, 0x0A000002UL, 10093U, 0x11223344UL, ppp_noop_tx, NULL);
            link->set_ax25_rx_callback(ppp_noop_ax25, NULL);
            link->set_raw_ip_rx_callback(ppp_noop_ip, NULL);
            link->set_udp_rx_callback(ppp_noop_udp, NULL);
            link->open(static_cast<oyk_u32>(i));
        }
        mutate_bytes(rng, seeds, buf, 1600);
        const oyk_u8* ptr = buf.empty() ? reinterpret_cast<const oyk_u8*>("") : buf.data();
        link->feed_serial_bytes(ptr, buf.size(), static_cast<oyk_u32>(i * 10U));
        if ((i & 31U) == 0U) link->tick(static_cast<oyk_u32>(i * 10U));
    }
    delete link;
}

} // namespace

int main() {
    std::uint64_t seed = 0x4F594B32303236ULL;
    if (const char* env_s = std::getenv("OYK_FUZZ_SEED")) {
        if (env_s[0]) seed = std::strtoull(env_s, nullptr, 0);
    }
    std::size_t iters = 5000U;
    if (const char* env_i = std::getenv("OYK_FUZZ_ITERS")) {
        if (env_i[0]) {
            const std::size_t parsed = static_cast<std::size_t>(std::strtoul(env_i, nullptr, 10));
            if (parsed > 0U) iters = parsed;
        }
    }

    XorShift64 rng(seed);
    fuzz_ax25(rng, iters);
    fuzz_kiss(rng, iters);
    fuzz_config(rng, iters);
    fuzz_banlist(rng, iters);
    fuzz_yapp(rng, iters);
    fuzz_terminal(rng, iters);
    fuzz_ppp(rng, iters);

    std::printf("[PASS] Seeded mutation fuzzing passed (iters=%zu, seed=0x%llx, 7 parsers).\n",
                iters, static_cast<unsigned long long>(seed));
    return 0;
}
