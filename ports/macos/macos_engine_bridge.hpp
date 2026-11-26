/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_MACOS_ENGINE_BRIDGE_HPP
#define OYK_MACOS_ENGINE_BRIDGE_HPP

/*
 * Dual-Core C++ Bridge for macOS (Mac OS X 10.4 Tiger PowerPC -> macOS 15+ Apple Silicon)
 *
 * Exposes a pure C++98 POD/struct surface to the AppKit Objective-C++ UI (`macos_main.mm`)
 * while dispatching to either:
 *   - OYK_MACOS_USE_PORTABLE=1: Strict C++98 OykPortableNode + BSD sockets + POSIX termios
 *     (Mac OS X 10.4-10.6 PowerPC & host C++ bridge tests on Linux)
 *   - OYK_MACOS_USE_PORTABLE=0: Full C++20 oyk::node::NodeEngine
 *     (Modern macOS Intel x86_64 & Apple Silicon arm64)
 */

#include "oyk_portable.hpp"
#include "oyk_gui.hpp"
#include "oyk_term.hpp"

#ifndef OYK_MACOS_USE_PORTABLE
#  if defined(__ppc__) || defined(__ppc64__) || defined(__POWERPC__) || !defined(__APPLE__)
#    define OYK_MACOS_USE_PORTABLE 1
#  else
#    define OYK_MACOS_USE_PORTABLE 0
#  endif
#endif

struct OykMacOsIfaceSnap {
    char id[16];
    char type[16];
    char status[36];
    int running;
    int active_conns;
    oyk_u32 rx_frames;
    oyk_u32 tx_frames;
};

struct OykMacOsHeardSnap {
    char callsign[12];
    char iface_id[16];
    char when_hms[10];
};

struct OykMacOsSessionSnap {
    oyk_u32 id;
    char callsign[16];
    char iface_id[16];
    char direction[12];
    char state[16];
    char since_hms[10];
    int is_local;
};

struct OykMacOsBanSnap {
    char callsign[16];
    char banned_at[16];
    char lift_at[16];
    oyk_u32 flags;
    char reason[96];
};

struct OykMacOsSnapshot {
    char node_callsign[16];
    char node_alias[16];
    oyk_u32 uptime_seconds;
    int accept_incoming;
    int manual_deny_incoming;
    int active_connections;
    int iface_count;
    OykMacOsIfaceSnap ifaces[OYK_MAX_INTERFACES];
    int heard_count;
    OykMacOsHeardSnap heard[OYK_MAX_NEW_HEARD];
    int session_count;
    OykMacOsSessionSnap sessions[OYK_MAX_SESSIONS];
    int ban_count;
    OykMacOsBanSnap bans[16];
    int log_count;
    char logs[24][112];
    int pending_count;
    OykPendingIncoming pending[12];
    int approved_count;
    OykApprovedFile approved[16];
    int auto_learned_count;
};

typedef void (*OykMacOsTermRxFn)(oyk_u32 session_id,
                                  int event_type,
                                  const oyk_u8* data,
                                  size_t len,
                                  const char* reason,
                                  void* ctx);

class OykMacOsEngine {
public:
    OykMacOsEngine();
    ~OykMacOsEngine();

    int init_and_start(const char* config_path, int seed_demo_data,
                       char* err_out, size_t err_cap);
    void stop();
    void poll();

    int is_running() const;
    int uses_portable_core() const;
    const char* config_path() const;
    const OykConfig& portable_config() const;

    void get_snapshot(OykMacOsSnapshot* out) const;
    void set_accept_incoming(int allow);
    int apply_config(const OykConfig* new_cfg, int save_to_disk,
                     char* err_out, size_t err_cap);

    int open_local_terminal(const char* port_id, const char* target_call,
                            oyk_u32* sess_id_out, char* resolved_out, size_t rcap,
                            char* err_out, size_t ecap);
    int send_local_bytes(oyk_u32 session_id, const oyk_u8* data, size_t len);
    int close_local_terminal(oyk_u32 session_id);
    void set_term_rx_callback(OykMacOsTermRxFn cb, void* ctx);

    int terminate_session(oyk_u32 session_id, char* msg_out, size_t cap);
    int ban_callsign(const char* call_spec, const char* lift_spec,
                     oyk_u32 flags, const char* reason,
                     char* msg_out, size_t cap);
    int unban_callsign(const char* call_spec, char* msg_out, size_t cap);
    int persist_auto_learned(char* err_out, size_t cap);
    void discard_auto_learned();
    void log_message(oyk_u32 level, const char* text);

private:
    struct Impl;
    Impl* impl_;

    OykMacOsEngine(const OykMacOsEngine&);
    OykMacOsEngine& operator=(const OykMacOsEngine&);
};

#endif /* OYK_MACOS_ENGINE_BRIDGE_HPP */
