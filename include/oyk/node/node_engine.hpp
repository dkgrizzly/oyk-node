// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/ax25/callsign.hpp"
#include "oyk/ax25/frame.hpp"
#include "oyk/node/ban_list.hpp"
#include "oyk/node/bbs_server.hpp"
#include "oyk/node/chat_room.hpp"
#include "oyk/node/command_handler.hpp"
#include "oyk/node/logger.hpp"
#include "oyk/node/node_config.hpp"
#include "oyk/node/session.hpp"
#include "oyk/radio/radio_interface.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace oyk::node {

struct InterfaceSnapshot {
    std::string id;
    std::string type;
    std::string description;
    std::string status;
    bool running = false;
    std::size_t active_connections = 0;
    uint64_t rx_frames = 0;
    uint64_t tx_frames = 0;
    uint64_t rx_errors = 0;
    uint64_t tx_errors = 0;
};

struct NewCallsignRecord {
    std::string callsign;
    std::string interface_id;
    std::chrono::system_clock::time_point first_heard;
};

struct MHeardRecord {
    std::string callsign;
    std::string interface_id;
    std::chrono::system_clock::time_point last_heard;
    uint64_t frame_count = 0;
};

struct SessionSnapshot {
    uint64_t id = 0;
    std::string remote_call;
    std::string interface_id;
    SessionState state = SessionState::Closed;
    SessionDirection direction = SessionDirection::Inbound;
    SessionMode mode = SessionMode::Normal;
    uint64_t bridged_session_id = 0;
    std::chrono::system_clock::time_point connected_at;
};

struct NodeSnapshot {
    std::string node_callsign;
    std::string node_alias;
    std::string welcome_text;
    std::string info_text;
    uint64_t uptime_seconds = 0;
    bool accept_incoming = true;
    bool manual_deny_incoming = false;
    std::size_t total_active_connections = 0;

    std::vector<InterfaceSnapshot> interfaces;
    std::vector<NewCallsignRecord> last_new_callsigns; // Up to 5 newest distinct callsigns heard
    std::vector<SessionSnapshot> active_sessions;
    std::vector<std::string> banned_callsigns;
    std::vector<BanEntry> ban_entries;
    std::vector<std::string> recent_logs;
};

struct ConfigApplyResult {
    bool applied = false;
    bool saved_to_disk = false;
    bool identity_restarted = false;
    bool interfaces_reloaded = false;
    std::size_t signed_off_sessions = 0;
    std::string save_path;
    std::string warning;
    std::string error;
};

class NodeEngine {
public:
    using ClockFn = std::function<std::chrono::system_clock::time_point()>;

    explicit NodeEngine(NodeConfig config);
    ~NodeEngine();

    NodeEngine(const NodeEngine&) = delete;
    NodeEngine& operator=(const NodeEngine&) = delete;

    // Optional clock override (primarily for deterministic testing of daily rotation & soft bans)
    void set_clock(ClockFn clock_fn);

    // Add an already-instantiated radio interface (for custom drivers or tests)
    bool add_interface(std::unique_ptr<radio::RadioInterface> iface, std::string& error_out);

    // Creates interfaces from config (if not already added), starts all interfaces & timer thread.
    bool start(std::string& error_out);
    void stop();
    bool is_running() const noexcept { return running_.load(); }

    // Local operator controls (thread-safe, called by ncurses UI or management code)
    void set_accept_incoming(bool allow);
    bool toggle_accept_incoming();
    bool accept_incoming() const;

    bool terminate_session(uint64_t session_id, std::string* message_out = nullptr);
    bool ban_callsign(std::string_view callsign_spec,
                      std::string* message_out = nullptr,
                      bool disconnect_active = true,
                      uint32_t flags = BAN_FLAG_APPEAL_ALLOWED,
                      const std::string& reason = "Banned by sysop.",
                      std::optional<std::chrono::system_clock::time_point> lift_at = std::nullopt);
    bool ban_callsign_with_lift_spec(std::string_view callsign_spec,
                                     std::string_view lift_spec,
                                     std::string* message_out = nullptr,
                                     bool disconnect_active = true,
                                     uint32_t flags = BAN_FLAG_APPEAL_ALLOWED,
                                     const std::string& reason = "Banned by sysop.");
    bool unban_callsign(std::string_view callsign_spec, std::string* message_out = nullptr);
    bool is_banned(const ax25::Callsign& call) const;

    // Query & live configuration APIs (thread-safe)
    NodeConfig config() const;
    ConfigApplyResult apply_config(NodeConfig new_cfg, bool save_to_disk = true);
    NodeSnapshot snapshot() const;
    std::vector<MHeardRecord> mheard(std::size_t max_entries = 20) const;
    Logger& logger() noexcept { return logger_; }

    // Sysop incoming file approval workflow (incoming/<CALLSIGN-SSID>/<file> -> BBS filespath)
    std::string bbs_files_path() const;
    std::string bbs_incoming_path() const;
    std::vector<PendingIncomingFile> list_pending_incoming_files() const;
    bool approve_incoming_file(const std::string& uploader,
                               const std::string& filename,
                               const std::string& dest_subdir,
                               bool allow_overwrite,
                               std::string& dest_path_out,
                               bool& already_exists_out,
                               std::string& error_out);
    bool delete_incoming_file(const std::string& uploader,
                              const std::string& filename,
                              std::string& error_out);
    std::vector<ApprovedFileEntry> list_approved_files() const;
    bool delete_approved_file(const std::string& rel_path,
                              std::string& error_out);

    // Extension point for future BBS / Chat executables
    CommandHandler& command_handler() noexcept { return command_handler_; }

    // Local operator packet terminal (outbound AX.25 binary-safe stream, supports YAPP)
    struct LocalTerminalCallbacks {
        std::function<void(uint64_t session_id)> on_connected;
        std::function<void(uint64_t session_id, const std::vector<uint8_t>& bytes)> on_rx_bytes;
        std::function<void(uint64_t session_id, const std::string& reason)> on_closed;
    };

    bool open_local_terminal(const std::string& port_spec,
                             const std::string& target_call_str,
                             LocalTerminalCallbacks callbacks,
                             uint64_t& session_id_out,
                             ax25::Callsign* resolved_call_out,
                             std::string& error_out,
                             bool* is_local_out = nullptr);
    bool send_local_terminal_bytes(uint64_t session_id, const std::vector<uint8_t>& data);
    bool close_local_terminal(uint64_t session_id);

    // For tests/diagnostics
    radio::RadioInterface* find_interface(const std::string& id) const;

private:
    struct PendingTx {
        radio::RadioInterface* iface = nullptr;
        ax25::Frame frame;
        std::string endpoint;
    };

    struct PendingCommand {
        uint64_t session_id = 0;
        std::string line;
    };

    struct PendingLocalEvent {
        uint64_t session_id = 0;
        bool connected = false;
        bool closed = false;
        std::string close_reason;
        std::vector<uint8_t> rx_bytes;
        LocalTerminalCallbacks callbacks;
    };

    void on_radio_frame(const ax25::Frame& frame, const radio::RadioFrameMeta& meta);
    void timer_loop();
    void dispatch_tx(const std::vector<PendingTx>& pending);
    void dispatch_local_events(const std::vector<PendingLocalEvent>& events);
    void queue_session_frames(Session& sess,
                              const std::vector<ax25::Frame>& frames,
                              std::vector<PendingTx>& pending);
    void deliver_text_locked(Session& sess,
                             const std::string& text,
                             std::vector<PendingTx>& pending,
                             std::vector<PendingLocalEvent>& pending_local);
    void apply_chat_result_locked(const ChatActionResult& res,
                                  uint64_t actor_sid,
                                  std::vector<PendingTx>& pending,
                                  std::vector<PendingLocalEvent>& pending_local);
    void apply_bbs_result_locked(const BbsActionResult& res,
                                 uint64_t actor_sid,
                                 std::vector<PendingTx>& pending,
                                 std::vector<PendingLocalEvent>& pending_local);
    void process_session_events(Session& sess,
                                SessionEvents& ev,
                                std::vector<PendingTx>& pending,
                                std::vector<PendingCommand>& pending_cmds,
                                std::vector<PendingLocalEvent>& pending_local);
    void execute_pending_commands(const std::vector<PendingCommand>& pending_cmds);
    void handle_restricted_ban_line_locked(Session& sess,
                                           const std::string& line,
                                           std::vector<PendingTx>& pending);
    void start_outbound_connect(Session& inbound_sess,
                                const std::string& port_spec,
                                const std::string& target_call_str,
                                std::vector<PendingTx>& pending);
    void teardown_bridge_locked(Session& closed_sess,
                                const std::string& reason,
                                std::vector<PendingTx>& pending);
    void record_heard_locked(const ax25::Callsign& src,
                             const std::string& interface_id,
                             std::chrono::system_clock::time_point now);
    bool is_for_node(const ax25::Callsign& dst) const;
    bool is_for_bbs(const ax25::Callsign& dst) const;
    bool is_for_chat(const ax25::Callsign& dst) const;
    bool is_own_callsign(const ax25::Callsign& call) const;
    bool is_banned_locked(const ax25::Callsign& call) const;
    std::string resolve_port_id_locked(const std::string& port_spec) const;
    std::chrono::system_clock::time_point current_time() const;
    void log_msg(LogLevel level, const std::string& msg);
    std::string welcome_banner() const;
    std::string prompt_string() const;
    static std::string build_ban_notice(const BanEntry& entry, bool can_appeal);

    NodeConfig config_;
    CommandHandler command_handler_;
    Logger logger_;
    ClockFn clock_fn_;

    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point started_at_{};
    std::chrono::steady_clock::time_point last_beacon_at_{};

    mutable std::mutex state_mutex_;
    bool accept_incoming_ = true;
    bool manual_deny_incoming_ = false;
    uint64_t next_session_id_ = 1;

    std::vector<std::unique_ptr<radio::RadioInterface>> interfaces_;
    std::map<std::string, radio::RadioInterface*> iface_by_id_;

    std::map<uint64_t, std::unique_ptr<Session>> sessions_;
    std::map<uint64_t, LocalTerminalCallbacks> local_callbacks_;

    // Heard tracking
    std::set<std::string> seen_callsigns_;
    std::deque<NewCallsignRecord> last_new_callsigns_; // Most recent 5 new callsigns (newest first)
    std::map<std::string, MHeardRecord> mheard_;

    // Persistent banlist (banlist.cfg), packet BBS, and multi-user CHAT conference room
    BanList ban_list_;
    BbsServer bbs_server_;
    ChatRoom chat_room_;

    std::thread timer_thread_;
};

} // namespace oyk::node
