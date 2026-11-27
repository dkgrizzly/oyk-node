// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include <jni.h>
#include <android/log.h>
#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "oyk/node/node_config.hpp"
#include "oyk/node/node_engine.hpp"
#include "oyk_ppp.hpp"

#define OYK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "OykNative", __VA_ARGS__)
#define OYK_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OykNative", __VA_ARGS__)

namespace {

std::mutex g_engine_mu;
std::unique_ptr<oyk::node::NodeEngine> g_engine;
std::string g_config_path;

std::mutex g_ppp_mu;
OykPppLink g_ppp;
bool g_ppp_inited = false;
std::vector<oyk_u8> g_ppp_tx_accum;

size_t jni_ppp_tx_cb(const oyk_u8* data, size_t len, void* /*ctx*/) {
    if (!data || len == 0) return 0;
    g_ppp_tx_accum.insert(g_ppp_tx_accum.end(), data, data + len);
    return len;
}

std::string jstring_to_std(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* utf = env->GetStringUTFChars(s, nullptr);
    if (!utf) return {};
    std::string out(utf);
    env->ReleaseStringUTFChars(s, utf);
    return out;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

} // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeStart(JNIEnv* env, jclass, jstring config_path) {
    const std::string path = jstring_to_std(env, config_path);
    std::lock_guard<std::mutex> lk(g_engine_mu);
    if (g_engine && g_engine->is_running()) {
        return JNI_TRUE;
    }

    oyk::node::NodeConfig cfg = oyk::node::NodeConfig::make_default();
    if (!path.empty()) {
        std::string err;
        if (!oyk::node::NodeConfig::load_from_file(path, cfg, err)) {
            OYK_LOGI("Config load fallback to default (%s): %s", path.c_str(), err.c_str());
            cfg = oyk::node::NodeConfig::make_default();
            cfg.source_path = path;
        }
        g_config_path = path;
    }

    auto engine = std::make_unique<oyk::node::NodeEngine>(cfg);
    std::string start_err;
    if (!engine->start(start_err)) {
        OYK_LOGE("Failed to start NodeEngine: %s", start_err.c_str());
        return JNI_FALSE;
    }
    g_engine = std::move(engine);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeStop(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_engine_mu);
    if (g_engine) {
        g_engine->discard_auto_learned_peers();
        g_engine->stop();
        g_engine.reset();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeIsRunning(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_engine_mu);
    return (g_engine && g_engine->is_running()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeGetStatusJson(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lk(g_engine_mu);
    if (!g_engine) {
        return env->NewStringUTF("{\"running\":false,\"connections\":0}");
    }
    const auto snap = g_engine->snapshot();
    std::ostringstream oss;
    oss << "{\"running\":" << (g_engine->is_running() ? "true" : "false")
        << ",\"callsign\":\"" << json_escape(snap.node_callsign) << "\""
        << ",\"alias\":\"" << json_escape(snap.node_alias) << "\""
        << ",\"uptime\":" << snap.uptime_seconds
        << ",\"connections\":" << snap.total_active_connections
        << ",\"auto_learned_peers\":" << g_engine->auto_learned_peer_count()
        << ",\"accept_incoming\":" << (snap.accept_incoming ? "true" : "false")
        << ",\"new_heard\":[";
    for (std::size_t hi = 0; hi < snap.last_new_callsigns.size(); ++hi) {
        const auto& nh = snap.last_new_callsigns[hi];
        std::time_t tt = std::chrono::system_clock::to_time_t(nh.first_heard);
        std::tm tm_val{};
        localtime_r(&tt, &tm_val);
        char hbuf[16];
        std::snprintf(hbuf, sizeof(hbuf), "%02d:%02d:%02d",
                      tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
        if (hi > 0) oss << ",";
        oss << "{\"callsign\":\"" << json_escape(nh.callsign) << "\""
            << ",\"iface\":\"" << json_escape(nh.interface_id) << "\""
            << ",\"heard_hms\":\"" << hbuf << "\"}";
    }
    oss << "]}";
    const std::string json = oss.str();
    return env->NewStringUTF(json.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeTerminateSession(JNIEnv*, jclass, jlong sid) {
    std::lock_guard<std::mutex> lk(g_engine_mu);
    if (!g_engine || sid <= 0) return JNI_FALSE;
    std::string msg;
    return g_engine->terminate_session(static_cast<uint64_t>(sid), &msg) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeBanCallsign(JNIEnv* env, jclass,
                                                           jstring call, jstring reason) {
    const std::string c = jstring_to_std(env, call);
    const std::string r = jstring_to_std(env, reason);
    std::lock_guard<std::mutex> lk(g_engine_mu);
    if (!g_engine || c.empty()) return JNI_FALSE;
    std::string msg;
    return g_engine->ban_callsign(c, &msg, true, oyk::node::BAN_FLAG_APPEAL_ALLOWED, r)
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeUnbanCallsign(JNIEnv* env, jclass, jstring call) {
    const std::string c = jstring_to_std(env, call);
    std::lock_guard<std::mutex> lk(g_engine_mu);
    if (!g_engine || c.empty()) return JNI_FALSE;
    std::string msg;
    return g_engine->unban_callsign(c, &msg) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativeSendTerminalCommand(JNIEnv* env, jclass, jstring cmd) {
    const std::string c = jstring_to_std(env, cmd);
    std::lock_guard<std::mutex> lk(g_engine_mu);
    if (!g_engine) {
        return env->NewStringUTF("Node not running.");
    }
    const auto snap = g_engine->snapshot();
    std::ostringstream oss;
    oss << "OK [" << snap.node_callsign << "] cmd=" << c
        << " active=" << snap.total_active_connections;
    return env->NewStringUTF(oss.str().c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativePppInit(JNIEnv* env, jclass,
                                                       jstring local_ip, jstring peer_ip,
                                                       jint udp_port) {
    const std::string lip_s = jstring_to_std(env, local_ip);
    const std::string pip_s = jstring_to_std(env, peer_ip);
    struct in_addr la{};
    struct in_addr pa{};
    if (inet_pton(AF_INET, lip_s.c_str(), &la) != 1) return JNI_FALSE;
    if (inet_pton(AF_INET, pip_s.c_str(), &pa) != 1) pa.s_addr = 0;

    std::lock_guard<std::mutex> lk(g_ppp_mu);
    g_ppp_tx_accum.clear();
    g_ppp.init(static_cast<oyk_u32>(la.s_addr),
               static_cast<oyk_u32>(pa.s_addr),
               static_cast<oyk_u16>(udp_port > 0 ? udp_port : 10093),
               0x4F594B31UL,
               jni_ppp_tx_cb, nullptr);
    g_ppp.open(100UL);
    g_ppp_inited = true;
    return JNI_TRUE;
}

JNIEXPORT jbyteArray JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativePppFeedRx(JNIEnv* env, jclass, jbyteArray rx_bytes) {
    std::lock_guard<std::mutex> lk(g_ppp_mu);
    g_ppp_tx_accum.clear();
    if (g_ppp_inited && rx_bytes) {
        const jsize len = env->GetArrayLength(rx_bytes);
        if (len > 0) {
            std::vector<jbyte> tmp(static_cast<size_t>(len));
            env->GetByteArrayRegion(rx_bytes, 0, len, tmp.data());
            g_ppp.feed_serial_bytes(reinterpret_cast<const oyk_u8*>(tmp.data()),
                                    static_cast<size_t>(len), 100UL);
        }
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(g_ppp_tx_accum.size()));
    if (!g_ppp_tx_accum.empty()) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(g_ppp_tx_accum.size()),
                                reinterpret_cast<const jbyte*>(g_ppp_tx_accum.data()));
    }
    return out;
}

JNIEXPORT jbyteArray JNICALL
Java_com_kr4oyk_oyknode_NativeNodeBridge_nativePppTick(JNIEnv* env, jclass, jlong now_ms) {
    std::lock_guard<std::mutex> lk(g_ppp_mu);
    g_ppp_tx_accum.clear();
    if (g_ppp_inited) {
        g_ppp.tick(static_cast<oyk_u32>(now_ms & 0xFFFFFFFFULL));
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(g_ppp_tx_accum.size()));
    if (!g_ppp_tx_accum.empty()) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(g_ppp_tx_accum.size()),
                                reinterpret_cast<const jbyte*>(g_ppp_tx_accum.data()));
    }
    return out;
}

} // extern "C"
