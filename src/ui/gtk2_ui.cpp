// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ui/gtk2_ui.hpp"

#include <gtk/gtk.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace oyk::ui {

namespace {

std::string format_hms(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S");
    return oss.str();
}

std::string format_duration(uint64_t secs) {
    uint64_t h = secs / 3600;
    uint64_t m = (secs % 3600) / 60;
    uint64_t s = secs % 60;
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << h << ":"
        << std::setw(2) << std::setfill('0') << m << ":"
        << std::setw(2) << std::setfill('0') << s;
    return oss.str();
}

std::string escape_markup(const std::string& text) {
    gchar* esc = g_markup_escape_text(text.c_str(), -1);
    std::string out(esc ? esc : "");
    g_free(esc);
    return out;
}

void add_text_column(GtkTreeView* tv, const char* title, int col_idx, int min_width = -1) {
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
        title, renderer, "text", col_idx, static_cast<const char*>(nullptr));
    gtk_tree_view_column_set_resizable(col, TRUE);
    if (min_width > 0) {
        gtk_tree_view_column_set_min_width(col, min_width);
    }
    gtk_tree_view_append_column(tv, col);
}

struct GtkAppState {
    node::NodeEngine* engine = nullptr;
    std::atomic<bool>* stop_flag = nullptr;

    GtkWidget* window = nullptr;
    GtkWidget* header_info_label = nullptr;
    GtkWidget* incoming_badge_label = nullptr;
    GtkWidget* sessions_frame = nullptr;
    GtkWidget* status_label = nullptr;

    GtkListStore* iface_store = nullptr;
    GtkListStore* heard_store = nullptr;
    GtkListStore* session_store = nullptr;
    GtkWidget* session_tree = nullptr;

    GtkWidget* log_view = nullptr;
    GtkTextBuffer* log_buffer = nullptr;
    std::string last_rendered_logs;
};

void set_status_text(GtkAppState* st, const std::string& msg) {
    if (st && st->status_label) {
        gtk_label_set_text(GTK_LABEL(st->status_label), msg.c_str());
    }
}

void refresh_ui(GtkAppState* st) {
    node::NodeSnapshot snap = st->engine->snapshot();

    // Header summary
    std::ostringstream hdr;
    hdr << "<b>OYK PACKET NODE: " << escape_markup(snap.node_callsign);
    if (!snap.node_alias.empty()) {
        hdr << " (" << escape_markup(snap.node_alias) << ")";
    }
    hdr << "</b>   |   Uptime: " << format_duration(snap.uptime_seconds)
        << "   |   Active Connections: <b>" << snap.total_active_connections << "</b>";
    gtk_label_set_markup(GTK_LABEL(st->header_info_label), hdr.str().c_str());

    if (snap.accept_incoming) {
        gtk_label_set_markup(
            GTK_LABEL(st->incoming_badge_label),
            "<span background=\"#1e7e34\" foreground=\"#ffffff\" weight=\"bold\">  INCOMING: ALLOW  </span>");
    } else {
        gtk_label_set_markup(
            GTK_LABEL(st->incoming_badge_label),
            "<span background=\"#b71c1c\" foreground=\"#ffffff\" weight=\"bold\">  INCOMING: DENY  </span>");
    }

    // Radio Interfaces table
    gtk_list_store_clear(st->iface_store);
    for (const auto& iface : snap.interfaces) {
        GtkTreeIter iter;
        std::string rxtx = std::to_string(iface.rx_frames) + "/" + std::to_string(iface.tx_frames);
        std::string conn_str = std::to_string(iface.active_connections);
        gtk_list_store_append(st->iface_store, &iter);
        gtk_list_store_set(st->iface_store, &iter,
                           0, iface.id.c_str(),
                           1, iface.type.c_str(),
                           2, conn_str.c_str(),
                           3, iface.status.c_str(),
                           4, rxtx.c_str(),
                           -1);
    }

    // Last 5 New Callsigns Heard table
    gtk_list_store_clear(st->heard_store);
    for (std::size_t i = 0; i < snap.last_new_callsigns.size(); ++i) {
        const auto& rec = snap.last_new_callsigns[i];
        GtkTreeIter iter;
        std::string idx = std::to_string(i + 1) + ".";
        std::string hms = format_hms(rec.first_heard);
        gtk_list_store_append(st->heard_store, &iter);
        gtk_list_store_set(st->heard_store, &iter,
                           0, idx.c_str(),
                           1, rec.callsign.c_str(),
                           2, rec.interface_id.c_str(),
                           3, hms.c_str(),
                           -1);
    }

    // Active Connections frame title & table (preserve selection by session ID if possible)
    uint64_t selected_sid = 0;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(st->session_tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter sel_iter;
    if (gtk_tree_selection_get_selected(sel, &model, &sel_iter)) {
        guint64 raw_sid = 0;
        gtk_tree_model_get(model, &sel_iter, 7, &raw_sid, -1);
        selected_sid = static_cast<uint64_t>(raw_sid);
    }

    std::ostringstream sf_title;
    sf_title << "Active Connections (" << snap.active_sessions.size()
             << ")  |  Banned Callsigns: " << snap.ban_entries.size();
    gtk_frame_set_label(GTK_FRAME(st->sessions_frame), sf_title.str().c_str());

    gtk_list_store_clear(st->session_store);
    for (const auto& s : snap.active_sessions) {
        GtkTreeIter iter;
        std::string id_str = "#" + std::to_string(s.id);
        std::string dir_str = (s.direction == node::SessionDirection::Inbound) ? "INBOUND" : "OUTBOUND";
        std::string st_str = node::Session::state_name(s.state);
        std::string since_str = format_hms(s.connected_at);
        std::string role = (s.mode == node::SessionMode::RestrictedBan)
                               ? "BAN NOTICE"
                               : (s.bridged_session_id ? ("<-> #" + std::to_string(s.bridged_session_id))
                                                       : "NODE SHELL");
        gtk_list_store_append(st->session_store, &iter);
        gtk_list_store_set(st->session_store, &iter,
                           0, id_str.c_str(),
                           1, s.remote_call.c_str(),
                           2, s.interface_id.c_str(),
                           3, dir_str.c_str(),
                           4, st_str.c_str(),
                           5, since_str.c_str(),
                           6, role.c_str(),
                           7, static_cast<guint64>(s.id),
                           -1);
        if (s.id == selected_sid) {
            gtk_tree_selection_select_iter(sel, &iter);
        }
    }

    // Activity Log
    std::ostringstream log_oss;
    for (const auto& line : snap.recent_logs) {
        log_oss << line << "\n";
    }
    std::string combined = log_oss.str();
    if (combined != st->last_rendered_logs) {
        st->last_rendered_logs = combined;
        gtk_text_buffer_set_text(st->log_buffer, combined.c_str(), -1);
        GtkTextIter end_iter;
        gtk_text_buffer_get_end_iter(st->log_buffer, &end_iter);
        GtkTextMark* mark = gtk_text_buffer_create_mark(st->log_buffer, nullptr, &end_iter, FALSE);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(st->log_view), mark, 0.0, FALSE, 0.0, 1.0);
        gtk_text_buffer_delete_mark(st->log_buffer, mark);
    }
}

gboolean on_timer_tick(gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st->stop_flag && st->stop_flag->load()) {
        gtk_main_quit();
        return FALSE;
    }
    refresh_ui(st);
    return TRUE;
}

void on_allow_deny_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Incoming Connections Policy",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Allow Incoming", 1,
        "Deny Incoming", 2,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        static_cast<const char*>(nullptr));

    std::string info = std::string("Current policy: ") +
                       (st->engine->accept_incoming() ? "ALLOWED" : "DENIED") +
                       "\n\nChoose whether to allow or deny new incoming connections:";
    GtkWidget* lbl = gtk_label_new(info.c_str());
    gtk_misc_set_padding(GTK_MISC(lbl), 12, 12);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), lbl, TRUE, TRUE, 4);
    gtk_widget_show_all(dlg);

    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == 1) {
        st->engine->set_accept_incoming(true);
        set_status_text(st, "New incoming connections are now ALLOWED.");
    } else if (resp == 2) {
        st->engine->set_accept_incoming(false);
        set_status_text(st, "New incoming connections are now DENIED.");
    }
    gtk_widget_destroy(dlg);
    refresh_ui(st);
}

void on_terminate_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    auto snap = st->engine->snapshot();
    if (snap.active_sessions.empty()) {
        set_status_text(st, "No active connections to terminate.");
        return;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Terminate Connection",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Terminate", GTK_RESPONSE_ACCEPT,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        static_cast<const char*>(nullptr));

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;
    GtkWidget* lbl = gtk_label_new("Select an active connection to terminate:");
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 6);

    GtkWidget* combo = gtk_combo_box_new_text();
    std::vector<uint64_t> ids;
    for (const auto& s : snap.active_sessions) {
        std::string item = "#" + std::to_string(s.id) + "  " + s.remote_call +
                           " on " + s.interface_id + " [" + node::Session::state_name(s.state) + "]";
        gtk_combo_box_append_text(GTK_COMBO_BOX(combo), item.c_str());
        ids.push_back(s.id);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 6);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (idx >= 0 && static_cast<std::size_t>(idx) < ids.size()) {
            std::string msg;
            st->engine->terminate_session(ids[static_cast<std::size_t>(idx)], &msg);
            set_status_text(st, msg);
        }
    }
    gtk_widget_destroy(dlg);
    refresh_ui(st);
}

void populate_ban_dialog_store(node::NodeEngine* engine, GtkListStore* store) {
    gtk_list_store_clear(store);
    auto snap = engine->snapshot();
    for (const auto& e : snap.ban_entries) {
        GtkTreeIter iter;
        std::string type_s = e.hard_ban() ? "HARD" : "SOFT";
        std::string flags_s = e.format_flags_binary();
        std::string lifts_s = e.format_lift_at();
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, e.key.c_str(),
                           1, type_s.c_str(),
                           2, flags_s.c_str(),
                           3, lifts_s.c_str(),
                           4, e.reason.c_str(),
                           -1);
    }
}

void on_ban_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Ban / Unban Callsign (banlist.cfg)",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Ban Callsign", 1,
        "Unban Selected", 2,
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 380);

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;

    GtkWidget* table = gtk_table_new(3, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 6);

    GtkWidget* call_lbl = gtk_label_new("Callsign (e.g. B4DCAL or B4DCAL-7):");
    gtk_misc_set_alignment(GTK_MISC(call_lbl), 0.0f, 0.5f);
    GtkWidget* call_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(call_entry), 9);

    GtkWidget* reason_lbl = gtk_label_new("Reason:");
    gtk_misc_set_alignment(GTK_MISC(reason_lbl), 0.0f, 0.5f);
    GtkWidget* reason_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(reason_entry), "Banned by sysop.");

    GtkWidget* chk_hard = gtk_check_button_new_with_label("Hard Ban (block after initial notice)");
    GtkWidget* chk_appeal = gtk_check_button_new_with_label("Appeal Allowed");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_appeal), TRUE);

    gtk_table_attach(GTK_TABLE(table), call_lbl, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), call_entry, 1, 2, 0, 1,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), reason_lbl, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), reason_entry, 1, 2, 1, 2,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), chk_hard, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), chk_appeal, 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 0);

    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 4);

    GtkListStore* ban_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                                 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* ban_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ban_store));
    add_text_column(GTK_TREE_VIEW(ban_tv), "Callsign", 0, 90);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Type", 1, 60);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Flags", 2, 70);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Lifts", 3, 110);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Reason", 4, 180);

    GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), ban_tv);
    gtk_box_pack_start(GTK_BOX(vbox), sc, TRUE, TRUE, 4);

    GtkWidget* fb_lbl = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(fb_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), fb_lbl, FALSE, FALSE, 4);

    populate_ban_dialog_store(st->engine, ban_store);
    gtk_widget_show_all(dlg);

    while (true) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == 1) {
            std::string call = gtk_entry_get_text(GTK_ENTRY(call_entry));
            std::string reason = gtk_entry_get_text(GTK_ENTRY(reason_entry));
            uint32_t flags = 0;
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_hard))) {
                flags |= node::BAN_FLAG_HARD_BAN;
            }
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_appeal))) {
                flags |= node::BAN_FLAG_APPEAL_ALLOWED;
            }
            std::string msg;
            if (st->engine->ban_callsign(call, &msg, true, flags, reason)) {
                gtk_entry_set_text(GTK_ENTRY(call_entry), "");
                gtk_label_set_text(GTK_LABEL(fb_lbl), msg.c_str());
                set_status_text(st, msg);
                populate_ban_dialog_store(st->engine, ban_store);
                refresh_ui(st);
            } else {
                gtk_label_set_text(GTK_LABEL(fb_lbl), ("Error: " + msg).c_str());
            }
        } else if (resp == 2) {
            GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ban_tv));
            GtkTreeModel* model = nullptr;
            GtkTreeIter iter;
            if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
                gchar* key = nullptr;
                gtk_tree_model_get(model, &iter, 0, &key, -1);
                if (key) {
                    std::string msg;
                    st->engine->unban_callsign(key, &msg);
                    g_free(key);
                    gtk_label_set_text(GTK_LABEL(fb_lbl), msg.c_str());
                    set_status_text(st, msg);
                    populate_ban_dialog_store(st->engine, ban_store);
                    refresh_ui(st);
                }
            }
        } else {
            break;
        }
    }

    g_object_unref(ban_store);
    gtk_widget_destroy(dlg);
}

bool confirm_and_exit(GtkAppState* st) {
    auto snap = st->engine->snapshot();
    std::string msg = "Disconnect " + std::to_string(snap.total_active_connections) +
                      " active session(s) and exit cleanly?";
    GtkWidget* dlg = gtk_message_dialog_new(
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", msg.c_str());
    gtk_window_set_title(GTK_WINDOW(dlg), "Exit OYK Packet Node");
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (resp == GTK_RESPONSE_YES) {
        gtk_main_quit();
        return true;
    }
    return false;
}

void on_exit_clicked(GtkWidget*, gpointer user_data) {
    confirm_and_exit(static_cast<GtkAppState*>(user_data));
}

gboolean on_window_delete(GtkWidget*, GdkEvent*, gpointer user_data) {
    // Return FALSE only if confirmed to allow close; TRUE cancels close
    return confirm_and_exit(static_cast<GtkAppState*>(user_data)) ? FALSE : TRUE;
}

} // namespace

Gtk2UI::Gtk2UI(node::NodeEngine& engine, std::atomic<bool>* external_stop_flag)
    : engine_(engine), external_stop_flag_(external_stop_flag) {}

bool Gtk2UI::run(int& argc, char**& argv) {
    if (!gtk_init_check(&argc, &argv)) {
        return false;
    }

    GtkAppState st;
    st.engine = &engine_;
    st.stop_flag = external_stop_flag_;

    st.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(st.window), "OYK Amateur Radio Packet Node");
    gtk_window_set_default_size(GTK_WINDOW(st.window), 920, 640);
    g_signal_connect(st.window, "delete-event", G_CALLBACK(on_window_delete), &st);

    GtkWidget* root_vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_add(GTK_CONTAINER(st.window), root_vbox);

    // Menu bar
    GtkWidget* menubar = gtk_menu_bar_new();
    GtkWidget* node_menu = gtk_menu_new();
    GtkWidget* node_root = gtk_menu_item_new_with_mnemonic("_Node");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(node_root), node_menu);

    GtkWidget* mi_allow = gtk_menu_item_new_with_mnemonic("_Allow / Deny Incoming...");
    GtkWidget* mi_term  = gtk_menu_item_new_with_mnemonic("_Terminate Connection...");
    GtkWidget* mi_ban   = gtk_menu_item_new_with_mnemonic("_Ban / Unban Callsign...");
    GtkWidget* mi_sep   = gtk_separator_menu_item_new();
    GtkWidget* mi_exit  = gtk_menu_item_new_with_mnemonic("E_xit Node");

    g_signal_connect(mi_allow, "activate", G_CALLBACK(on_allow_deny_clicked), &st);
    g_signal_connect(mi_term,  "activate", G_CALLBACK(on_terminate_clicked), &st);
    g_signal_connect(mi_ban,   "activate", G_CALLBACK(on_ban_clicked), &st);
    g_signal_connect(mi_exit,  "activate", G_CALLBACK(on_exit_clicked), &st);

    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_allow);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_term);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_ban);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_sep);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_exit);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), node_root);
    gtk_box_pack_start(GTK_BOX(root_vbox), menubar, FALSE, FALSE, 0);

    // Top Header Status Bar
    GtkWidget* hdr_hbox = gtk_hbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hdr_hbox), 6);
    st.header_info_label = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(st.header_info_label), 0.0f, 0.5f);
    st.incoming_badge_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(hdr_hbox), st.header_info_label, TRUE, TRUE, 4);
    gtk_box_pack_end(GTK_BOX(hdr_hbox), st.incoming_badge_label, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(root_vbox), hdr_hbox, FALSE, FALSE, 0);

    // Upper split: Interfaces (left) + Last 5 New Callsigns Heard (right)
    GtkWidget* top_hpaned = gtk_hpaned_new();
    gtk_paned_set_position(GTK_PANED(top_hpaned), 520);

    // Left: Radio Interfaces
    GtkWidget* iface_frame = gtk_frame_new("Radio Interfaces (Active Connections)");
    st.iface_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                        G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* iface_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st.iface_store));
    add_text_column(GTK_TREE_VIEW(iface_tv), "Port", 0, 80);
    add_text_column(GTK_TREE_VIEW(iface_tv), "Type", 1, 75);
    add_text_column(GTK_TREE_VIEW(iface_tv), "Conn", 2, 55);
    add_text_column(GTK_TREE_VIEW(iface_tv), "Status", 3, 170);
    add_text_column(GTK_TREE_VIEW(iface_tv), "RX/TX", 4, 80);
    GtkWidget* iface_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(iface_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(iface_sc), iface_tv);
    gtk_container_add(GTK_CONTAINER(iface_frame), iface_sc);
    gtk_paned_pack1(GTK_PANED(top_hpaned), iface_frame, TRUE, FALSE);

    // Right: Last 5 New Callsigns Heard
    GtkWidget* heard_frame = gtk_frame_new("Last 5 New Callsigns Heard");
    st.heard_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* heard_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st.heard_store));
    add_text_column(GTK_TREE_VIEW(heard_tv), "#", 0, 32);
    add_text_column(GTK_TREE_VIEW(heard_tv), "Callsign", 1, 95);
    add_text_column(GTK_TREE_VIEW(heard_tv), "Port", 2, 85);
    add_text_column(GTK_TREE_VIEW(heard_tv), "Heard", 3, 80);
    GtkWidget* heard_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(heard_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(heard_sc), heard_tv);
    gtk_container_add(GTK_CONTAINER(heard_frame), heard_sc);
    gtk_paned_pack2(GTK_PANED(top_hpaned), heard_frame, TRUE, FALSE);

    gtk_widget_set_size_request(top_hpaned, -1, 175);
    gtk_box_pack_start(GTK_BOX(root_vbox), top_hpaned, FALSE, TRUE, 2);

    // Middle: Active Connections
    st.sessions_frame = gtk_frame_new("Active Connections (0)  |  Banned Callsigns: 0");
    st.session_store = gtk_list_store_new(8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                          G_TYPE_STRING, G_TYPE_UINT64);
    st.session_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st.session_store));
    add_text_column(GTK_TREE_VIEW(st.session_tree), "ID", 0, 55);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Callsign", 1, 100);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Port", 2, 90);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Dir", 3, 85);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "State", 4, 105);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Since", 5, 85);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Role / Bridge", 6, 130);
    GtkWidget* sess_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sess_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sess_sc), st.session_tree);
    gtk_container_add(GTK_CONTAINER(st.sessions_frame), sess_sc);
    gtk_widget_set_size_request(st.sessions_frame, -1, 160);
    gtk_box_pack_start(GTK_BOX(root_vbox), st.sessions_frame, TRUE, TRUE, 2);

    // Lower: Node Activity Log
    GtkWidget* log_frame = gtk_frame_new("Node Activity Log");
    st.log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st.log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(st.log_view), FALSE);
    PangoFontDescription* mono = pango_font_description_from_string("Monospace 9");
    gtk_widget_modify_font(st.log_view, mono);
    pango_font_description_free(mono);
    st.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(st.log_view));

    GtkWidget* log_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(log_sc), st.log_view);
    gtk_container_add(GTK_CONTAINER(log_frame), log_sc);
    gtk_widget_set_size_request(log_frame, -1, 150);
    gtk_box_pack_start(GTK_BOX(root_vbox), log_frame, TRUE, TRUE, 2);

    // Status message line
    st.status_label = gtk_label_new("Ready.");
    gtk_misc_set_alignment(GTK_MISC(st.status_label), 0.0f, 0.5f);
    gtk_misc_set_padding(GTK_MISC(st.status_label), 6, 2);
    gtk_box_pack_start(GTK_BOX(root_vbox), st.status_label, FALSE, FALSE, 0);

    // Bottom Action Button Bar matching the ncurses menu
    GtkWidget* btn_hbox = gtk_hbox_new(TRUE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(btn_hbox), 6);
    GtkWidget* b_allow = gtk_button_new_with_label("Allow / Deny Incoming");
    GtkWidget* b_term  = gtk_button_new_with_label("Terminate Connection");
    GtkWidget* b_ban   = gtk_button_new_with_label("Ban / Unban Callsign");
    GtkWidget* b_exit  = gtk_button_new_with_label("Exit Node");

    g_signal_connect(b_allow, "clicked", G_CALLBACK(on_allow_deny_clicked), &st);
    g_signal_connect(b_term,  "clicked", G_CALLBACK(on_terminate_clicked), &st);
    g_signal_connect(b_ban,   "clicked", G_CALLBACK(on_ban_clicked), &st);
    g_signal_connect(b_exit,  "clicked", G_CALLBACK(on_exit_clicked), &st);

    gtk_box_pack_start(GTK_BOX(btn_hbox), b_allow, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), b_term,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), b_ban,   TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), b_exit,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root_vbox), btn_hbox, FALSE, FALSE, 0);

    refresh_ui(&st);
    guint timer_id = g_timeout_add(250, on_timer_tick, &st);

    gtk_widget_show_all(st.window);
    gtk_main();

    g_source_remove(timer_id);
    g_object_unref(st.iface_store);
    g_object_unref(st.heard_store);
    g_object_unref(st.session_store);
    return true;
}

} // namespace oyk::ui
