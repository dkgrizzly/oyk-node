// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/ax25/callsign.hpp"
#include "oyk/protocol/yapp.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace oyk::node {

enum class BbsMsgType : char {
    Personal = 'P',
    Bulletin = 'B',
    Traffic  = 'T'
};

enum class BbsMsgStatus : char {
    Unread   = 'N',
    Read     = 'Y',
    Bulletin = '$',
    Killed   = 'K'
};

struct BbsMessage {
    uint64_t id = 0;
    BbsMsgType type = BbsMsgType::Personal;
    BbsMsgStatus status = BbsMsgStatus::Unread;
    std::string from;     // Sender callsign (uppercase)
    std::string to;       // Recipient callsign or bulletin topic (uppercase)
    std::string at_bbs;   // Optional @BBS or distribution (uppercase)
    std::string date;     // YYYYMMDD-HHMMSS
    std::string subject;
    std::string body;
};

struct PendingIncomingFile {
    std::string uploader;    // Subdirectory name (CALLSIGN-SSID)
    std::string filename;    // Base filename
    std::string full_path;   // Full filesystem path inside incoming/
    uint64_t size_bytes = 0;
};

struct ApprovedFileEntry {
    std::string rel_path;    // Relative path inside files_path (using '/')
    std::string full_path;   // Full filesystem path inside files_path
    uint64_t size_bytes = 0;
};

struct BbsDirEntry {
    std::string name;
    uint64_t size_bytes = 0;
    bool is_dir = false;
    bool is_parent = false;  // true ONLY for ".." inside subdirectories
};

struct BbsActionResult {
    std::string response_text;
    std::vector<uint8_t> tx_bytes; // Raw binary bytes (used for YAPP transfers)
    std::string log_info;          // Optional informational log entry for NodeEngine
    bool left_bbs = false;
    bool disconnect_session = false;
};

// Classic Amateur Radio Packet Bulletin Board System (PBBS) with sandboxed Files Area.
// Persists messages as human-readable M<id>.MSG files under mail_path.
// Serves downloadable files strictly inside files_path; uploads go to incoming/<CALLSIGN-SSID>/.
class BbsServer {
public:
    bool configure(std::string bbs_callsign,
                   std::string bbs_alias,
                   std::string mail_path,
                   std::string files_path,
                   std::string incoming_root,
                   std::string welcome,
                   std::size_t max_msg_bytes,
                   std::string& error_out);

    BbsActionResult enter(uint64_t session_id,
                          const std::string& callsign,
                          const std::string& interface_id,
                          bool direct_connect,
                          bool is_sysop,
                          bool is_local_loopback,
                          std::chrono::system_clock::time_point now);

    void leave(uint64_t session_id);

    BbsActionResult handle_line(uint64_t session_id,
                                const std::string& raw_line,
                                std::chrono::system_clock::time_point now);

    // Feeds raw binary bytes when a BBS session is in an active YAPP download or upload.
    BbsActionResult handle_raw_bytes(uint64_t session_id,
                                     const std::vector<uint8_t>& data);

    bool has_session(uint64_t session_id) const;
    bool is_yapp_active(uint64_t session_id) const;
    std::size_t active_message_count() const;
    std::size_t unread_count_for(const std::string& callsign) const;
    const std::string& mail_path() const noexcept { return mail_path_; }
    const std::string& files_path() const noexcept { return files_path_; }
    const std::string& incoming_root() const noexcept { return incoming_root_; }
    bool files_enabled() const noexcept { return files_enabled_; }
    void set_files_enabled(bool enabled) noexcept { files_enabled_ = enabled; }
    bool files_case_insensitive() const noexcept { return files_case_insensitive_; }
    void set_files_case_insensitive(bool enabled) noexcept { files_case_insensitive_ = enabled; }

    // Strictly validates and resolves a virtual path inside files_root.
    // Rejects directory traversal (.., \, drive letters, null bytes, and symlinks escaping root).
    static bool resolve_safe_files_path(const std::string& files_root,
                                        const std::string& current_rel_dir,
                                        const std::string& user_arg,
                                        bool allow_parent_in_root,
                                        std::string& resolved_abs_out,
                                        std::string& resolved_rel_out,
                                        std::string& error_out);

    // Resolves a BBS files-area path with optional case-insensitive fallback:
    //   1. Exact case-sensitive match
    //   2. Forced UPPERCASE
    //   3. Forced lowercase
    //   4. Case-insensitive directory/glob scan inside files_root
    static bool resolve_files_path_with_case(const std::string& files_root,
                                             const std::string& current_rel_dir,
                                             const std::string& user_arg,
                                             bool allow_parent_in_root,
                                             bool case_insensitive,
                                             std::string& resolved_abs_out,
                                             std::string& resolved_rel_out,
                                             std::string& error_out);

    // Sysop incoming queue & approved files management
    static std::vector<PendingIncomingFile> list_pending_incoming(const std::string& incoming_root);
    static bool approve_incoming_file(const std::string& incoming_root,
                                      const std::string& files_root,
                                      const std::string& uploader,
                                      const std::string& filename,
                                      const std::string& dest_subdir,
                                      bool allow_overwrite,
                                      std::string& dest_path_out,
                                      bool& already_exists_out,
                                      std::string& error_out);
    static bool delete_incoming_file(const std::string& incoming_root,
                                     const std::string& uploader,
                                     const std::string& filename,
                                     std::string& error_out);
    static std::vector<ApprovedFileEntry> list_approved_files(const std::string& files_root);
    static bool delete_approved_file(const std::string& files_root,
                                     const std::string& rel_path,
                                     std::string& error_out);

    // Jailed dual-pane File Manager operations (incoming_root or files_root)
    static bool list_dir_entries(const std::string& root,
                                 const std::string& rel_dir,
                                 std::vector<BbsDirEntry>& out,
                                 std::string& norm_rel_out,
                                 std::string& error_out);
    static bool enter_subdir(const std::string& root,
                             const std::string& cur_rel,
                             const std::string& child_name,
                             std::string& next_rel_out,
                             std::string& error_out);
    static bool approve_incoming_rel_file(const std::string& incoming_root,
                                          const std::string& inc_rel_file,
                                          const std::string& files_root,
                                          const std::string& dest_rel_dir,
                                          bool allow_overwrite,
                                          std::string& dest_path_out,
                                          bool& already_exists_out,
                                          std::string& error_out);
    static bool import_local_file(const std::string& local_src_path,
                                  const std::string& target_root,
                                  const std::string& target_rel_dir,
                                  bool allow_overwrite,
                                  std::string& dest_path_out,
                                  bool& already_exists_out,
                                  std::string& error_out);
    static bool mkdir_safe(const std::string& root,
                           const std::string& cur_rel,
                           const std::string& new_name,
                           std::string& error_out);
    static bool rename_safe(const std::string& root,
                            const std::string& cur_rel,
                            const std::string& old_name,
                            const std::string& new_name,
                            std::string& error_out);
    static bool delete_safe(const std::string& root,
                            const std::string& cur_rel,
                            const std::string& name,
                            std::string& error_out);
    static bool read_text_preview(const std::string& root,
                                  const std::string& cur_rel,
                                  const std::string& filename,
                                  std::string& content_out,
                                  bool& truncated_out,
                                  std::string& error_out);

private:
    enum class ComposeStage {
        Command,
        Subject,
        Body
    };

    struct BbsSessionState {
        uint64_t session_id = 0;
        std::string callsign;      // Full uppercase callsign (e.g. W1AW-7)
        std::string base_callsign; // Base uppercase callsign (e.g. W1AW)
        ax25::Callsign parsed_call{"UNKNOWN", 0};
        std::string interface_id;
        bool direct_connect = false;
        bool is_sysop = false;
        bool is_local_loopback = false;

        ComposeStage stage = ComposeStage::Command;
        BbsMsgType draft_type = BbsMsgType::Personal;
        std::string draft_to;
        std::string draft_at;
        std::string draft_subject;
        std::string draft_body;

        // Current virtual subdirectory inside files_path_ ("" = files area root)
        std::string cur_files_rel;

        // Active YAPP transfer state (download from files_path_ or upload into incoming_root_)
        bool yapp_active = false;
        bool yapp_expect_upload = false;
        std::unique_ptr<protocol::YappSession> yapp;
    };

    bool load_messages_from_disk(std::string& error_out);
    bool save_message_to_disk(const BbsMessage& msg) const;
    std::string message_file_path(uint64_t id) const;
    std::string prompt_string() const;

    static std::string base_call(const std::string& call);
    static bool call_matches(const std::string& target, const BbsSessionState& st);
    bool can_read(const BbsMessage& msg, const BbsSessionState& st) const;
    bool can_kill(const BbsMessage& msg, const BbsSessionState& st) const;

    std::string format_list(const std::vector<const BbsMessage*>& msgs) const;
    std::string format_message(const BbsMessage& msg) const;
    std::string format_files_listing(const std::string& abs_dir, const std::string& rel_dir) const;

    std::string bbs_callsign_ = "N0CALL-11";
    std::string bbs_alias_ = "OYKBBS";
    std::string mail_path_ = "mail";
    std::string files_path_ = "files";
    bool files_enabled_ = true;
    bool files_case_insensitive_ = true;
    bool loaded_once_ = false;
    std::string incoming_root_ = "incoming";
    std::string welcome_ = "Welcome to OYK Packet BBS! Type H for help, B to exit.";
    std::size_t max_msg_bytes_ = 8192;

    uint64_t next_msg_id_ = 1;
    std::map<uint64_t, BbsMessage> messages_;
    std::map<uint64_t, BbsSessionState> sessions_;
};

} // namespace oyk::node
