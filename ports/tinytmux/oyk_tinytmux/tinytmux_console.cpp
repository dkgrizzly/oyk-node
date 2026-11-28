/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_console.hpp"
#include "board_tinytmux.hpp"
#include "tinytmux_fs.hpp"
#include "oyk_tui.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum TinyTmuxVKey {
    TMUX_KEY_UP    = 0x101,
    TMUX_KEY_DOWN  = 0x102,
    TMUX_KEY_RIGHT = 0x103,
    TMUX_KEY_LEFT  = 0x104,
    TMUX_KEY_F2    = 0x105,
    TMUX_KEY_F10   = 0x106,
    TMUX_KEY_HOME  = 0x107,
    TMUX_KEY_END   = 0x108,
    TMUX_KEY_PGUP  = 0x109,
    TMUX_KEY_PGDN  = 0x10A,
    TMUX_KEY_F9    = 0x10B,
    TMUX_KEY_BTAB  = 0x10C
};

static int strieq_ci(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? static_cast<char>(*b - 32) : *b;
        if (ca != cb) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static void con_str_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

struct TinyTmuxDriverOpt {
    const char* type;
    const char* desc;
};

static const TinyTmuxDriverOpt TINYTMUX_DRIVERS[7] = {
    { "axudp",          "AX.25 over UDP (Ethernet/USB)" },
    { "axip",           "AX.25 over IP (Protocol 93)" },
    { "ppp",            "PPP over Hardware UART (serN)" },
    { "kiss-serial",    "KISS TNC over UART (serN)" },
    { "usb-cdc-net",    "AXUDP over USB Host CDC-ECM" },
    { "usb-cdc-serial", "KISS over USB Host CDC-ACM" },
    { "telnet",         "Plaintext Telnet User Port" }
};

TinyTmuxConsole::TinyTmuxConsole()
    : node_(NULL), bridge_(NULL), tx_fn_(NULL), tx_ctx_(NULL), now_ms_(0),
      term_type_(TMUX_TERM_VT100), term_rows_(25), term_cols_(80), view_(TMUX_VIEW_CONSOLE), menu_sel_(0),
      files_focus_btn_(0), fm_preview_active_(0),
      settings_tab_(0), theme_scroll_(0), theme_preview_dirty_(0), settings_iface_field_(0),
      prompt_kind_(PROMPT_NONE), prompt_len_(0),
      wiz_edit_idx_(-1), wiz_step_(0), wiz_drv_(0), wiz_last_drv_(-1), wiz_focus_(0), wiz_btn_(1),
      lterm_sid_(0), lterm_is_local_(0), lterm_opening_(0), lterm_scroll_(0),
      lterm_ctrl_a_(0), lterm_help_open_(0),
      lterm_line_count_(0), lterm_curline_len_(0), lterm_input_len_(0),
      esc_state_(ESC_IDLE), esc_len_(0), x10_len_(0), mouse_enabled_(0), esc_start_ms_(0),
      last_marchc_redraw_ms_(0),
      bootloader_pending_(0), bootloader_start_ms_(0), storage_sel_(0) {
    esc_buf_[0] = '\0';
    strcpy(status_line_, "TinyTMUX Ready. Ctrl-N:Menu  Tab/F9:Term");
    dialog_fb_[0] = '\0';
    memset(&file_mgr_, 0, sizeof(file_mgr_));
    fm_preview_buf_[0] = '\0';
    oyk_config_init_default(&draft_);
    memset(settings_sel_, 0, sizeof(settings_sel_));
    prompt_label_[0] = '\0';
    prompt_buf_[0] = '\0';
    ban_call_tmp_[0] = '\0';
    ban_action_tmp_[0] = '\0';
    wiz_id_[0] = '\0';
    wiz_desc_[0] = '\0';
    wiz_ep_[0] = '\0';
    wiz_port_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_err_[0] = '\0';
    memset(lterm_lines_, 0, sizeof(lterm_lines_));
    lterm_curline_[0] = '\0';
    lterm_input_[0] = '\0';
    oyk_term_eol_init(&lterm_eol_, OYK_TERM_EOL_CR, 1);
}

void TinyTmuxConsole::init(OykPortableNode* node, TinyTmuxConsoleTxFn tx_fn, void* tx_ctx) {
    node_ = node;
    tx_fn_ = tx_fn;
    tx_ctx_ = tx_ctx;
    term_type_ = TMUX_TERM_VT100;
    term_rows_ = 25;
    term_cols_ = 80;
    view_ = TMUX_VIEW_CONSOLE;
    menu_sel_ = 0;
    files_focus_btn_ = 0;
    fm_preview_active_ = 0;
    memset(&file_mgr_, 0, sizeof(file_mgr_));
    fm_preview_buf_[0] = '\0';
    prompt_kind_ = PROMPT_NONE;
    prompt_len_ = 0;
    wiz_edit_idx_ = -1;
    wiz_step_ = 0;
    wiz_drv_ = 0;
    wiz_last_drv_ = -1;
    wiz_focus_ = 0;
    wiz_btn_ = 1;
    wiz_id_[0] = '\0';
    wiz_desc_[0] = '\0';
    wiz_ep_[0] = '\0';
    wiz_port_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_err_[0] = '\0';
    lterm_sid_ = 0;
    lterm_is_local_ = 0;
    lterm_opening_ = 0;
    lterm_scroll_ = 0;
    lterm_line_count_ = 0;
    lterm_curline_len_ = 0;
    lterm_input_len_ = 0;
    oyk_term_eol_init(&lterm_eol_, OYK_TERM_EOL_CR, 1);
    bootloader_pending_ = 0;
    bootloader_start_ms_ = 0;
    reset_escape();
    if (node_) draft_ = node_->config();
    push_lterm_line("*** OYK TinyTMUX Local Terminal (Ctrl+A Z:Help  Ctrl+A B:BBS  Ctrl+A T:Chat)");
}

void TinyTmuxConsole::set_net_bridge(TinyTmuxNetBridge* bridge) {
    bridge_ = bridge;
}

const char* TinyTmuxConsole::term_type_label() const {
    switch (term_type_) {
        case TMUX_TERM_VT220:    return "VT220";
        case TMUX_TERM_VT340:    return "VT340";
        case TMUX_TERM_XTERM256: return "xterm-256";
        default:                 return "VT100";
    }
}

OYK_FLASHMEM void TinyTmuxConsole::send_size_probe() {
    write_str("\033[s\033[999;999H\033[6n\033[u");
}

void TinyTmuxConsole::on_usb_connect() {
    term_type_ = TMUX_TERM_VT100;
    term_rows_ = 25;
    term_cols_ = 80;
    mouse_enabled_ = 0;
    reset_escape();
    /* Asynchronous DA1 + DA2 + CPR size inquiry; safe fallback stays 80x25 VT100. */
    write_str("\033[c\033[>c");
    send_size_probe();
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::disable_mouse() {
    if (mouse_enabled_) {
        mouse_enabled_ = 0;
        write_str("\033[?1006l\033[?1000l");
    }
}

static const char* tmux_format_sgr_colors(int slot, oyk_u32 fg_rgb, oyk_u32 bg_rgb,
                                          int use_bg, int bold, TinyTmuxTermType tt) {
    static char bufs[8][32];
    char* b = bufs[slot & 7];
    if (tt >= TMUX_TERM_XTERM256) {
        int fg = oyk_rgb_to_xterm256(fg_rgb);
        if (use_bg) {
            int bg = oyk_rgb_to_xterm256(bg_rgb);
            sprintf(b, "\033[%s38;5;%d;48;5;%dm", bold ? "1;" : "", fg, bg);
        } else {
            sprintf(b, "\033[%s38;5;%dm", bold ? "1;" : "", fg);
        }
        return b;
    }
    int fg16 = oyk_rgb_to_ansi16(fg_rgb);
    int fgc = (fg16 < 8) ? (30 + fg16) : (82 + fg16);
    if (use_bg) {
        int bg16 = oyk_rgb_to_ansi16(bg_rgb);
        int bgc = (bg16 < 8) ? (40 + bg16) : (92 + bg16);
        sprintf(b, "\033[%s%d;%dm", bold ? "1;" : "", fgc, bgc);
    } else {
        sprintf(b, "\033[%s%dm", bold ? "1;" : "", fgc);
    }
    return b;
}

const OykConfig* TinyTmuxConsole::active_theme_cfg() const {
    if (view_ == TMUX_VIEW_SETTINGS && settings_tab_ == 7 && theme_preview_dirty_) return &draft_;
    return node_ ? &node_->config() : NULL;
}

const char* TinyTmuxConsole::sgr_border() const {
    if (term_type_ < TMUX_TERM_VT220) return "";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(0, tc.colors[OYK_TR_BORDER], tc.colors[OYK_TR_BG], 0, 0, term_type_);
}

const char* TinyTmuxConsole::sgr_title() const {
    if (term_type_ < TMUX_TERM_VT220) return "\033[1m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(1, tc.colors[OYK_TR_ACCENT], tc.colors[OYK_TR_BG], 0, 1, term_type_);
}

const char* TinyTmuxConsole::sgr_header() const {
    if (term_type_ < TMUX_TERM_VT220) return "\033[1m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(2, tc.colors[OYK_TR_HEADER_FG], tc.colors[OYK_TR_HEADER_BG], 1, 1, term_type_);
}

const char* TinyTmuxConsole::sgr_menubar() const {
    if (term_type_ < TMUX_TERM_VT220) return "\033[7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(3, tc.colors[OYK_TR_BUTTON_FG], tc.colors[OYK_TR_BUTTON_BG], 1, 0, term_type_);
}

const char* TinyTmuxConsole::sgr_menusel() const {
    if (term_type_ < TMUX_TERM_VT220) return "\033[7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(4, tc.colors[OYK_TR_SELECTION_FG], tc.colors[OYK_TR_SELECTION_BG], 1, 1, term_type_);
}

const char* TinyTmuxConsole::sgr_list_hdr() const {
    if (term_type_ < TMUX_TERM_VT220) return "\033[1;7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(5, tc.colors[OYK_TR_PANEL_FG], tc.colors[OYK_TR_PANEL_BG], 1, 1, term_type_);
}

const char* TinyTmuxConsole::sgr_sel() const {
    if (term_type_ < TMUX_TERM_VT220) return "\033[7m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(6, tc.colors[OYK_TR_SELECTION_FG], tc.colors[OYK_TR_SELECTION_BG], 1, 1, term_type_);
}

const char* TinyTmuxConsole::sgr_warn() const {
    if (term_type_ < TMUX_TERM_VT220) return "\033[1m";
    OykThemeConfig tc;
    oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return tmux_format_sgr_colors(7, tc.colors[OYK_TR_WARN], tc.colors[OYK_TR_BG], 0, 1, term_type_);
}

OYK_FLASHMEM void TinyTmuxConsole::draw_dec_box(int row, int col, int h, int w,
                                                const char* title, int clear_interior, int with_close) {
    if (h < 2 || w < 4) return;
    char line[256];
    int inner = w - 2;
    if (inner > 130) inner = 130;

    /* Top border */
    sprintf(line, "\033[%d;%dH%s\033(0l", row, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'k';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");

    if (title && title[0] && inner > 4) {
        char tbuf[80];
        int max_t = with_close ? (inner - 9) : (inner - 4);
        if (max_t > 68) max_t = 68;
        if (max_t > 2) {
            sprintf(tbuf, " %.*s ", max_t, title);
            sprintf(line, "\033[%d;%dH%s%s\033[0m", row, col + 2, sgr_title(), tbuf);
            write_str(line);
        }
    }

    if (with_close && w >= 12) {
        int close_col = col + w - 5;
        const char* csgr = (term_type_ >= TMUX_TERM_VT220) ? "\033[1;91m" : "\033[7m";
        sprintf(line, "\033[%d;%dH%s[X]\033[0m", row, close_col, csgr);
        write_str(line);
    }

    /* Sides */
    for (int r = 1; r < h - 1; ++r) {
        if (clear_interior) {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B\033[0m%*s%s\033(0x\033(B\033[0m",
                    row + r, col, sgr_border(), inner, "", sgr_border());
            write_str(line);
        } else {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B\033[0m\033[%d;%dH%s\033(0x\033(B\033[0m",
                    row + r, col, sgr_border(),
                    row + r, col + w - 1, sgr_border());
            write_str(line);
        }
    }

    /* Bottom border */
    sprintf(line, "\033[%d;%dH%s\033(0m", row + h - 1, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'j';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");
}

OYK_FLASHMEM void TinyTmuxConsole::draw_dec_shadow(int row, int col, int h, int w) {
    if (h < 2 || w < 4) return;
    char line[256];
    const int max_cols = term_cols_;
    const int max_rows = term_rows_;
    const char* sattr = (term_type_ >= TMUX_TERM_VT220) ? "\033[0;90;40m" : "\033[0m";
    int r_end = row + h;
    int c_right = col + w;
    if (c_right <= max_cols) {
        int rw = (c_right + 1 <= max_cols) ? 2 : 1;
        for (int r = row + 1; r <= r_end && r < max_rows; ++r) {
            if (r < 1) continue;
            sprintf(line, "\033[%d;%dH%s\033(0%s\033(B\033[0m",
                    r, c_right, sattr, (rw == 2) ? "aa" : "a");
            write_str(line);
        }
    }
    if (r_end >= 1 && r_end < max_rows) {
        int bx = col + 2;
        if (bx <= max_cols) {
            int bw = w - 2;
            if (bx + bw - 1 > max_cols) bw = max_cols - bx + 1;
            if (bw > 0) {
                char shades[136];
                if (bw > 134) bw = 134;
                for (int i = 0; i < bw; ++i) shades[i] = 'a';
                shades[bw] = '\0';
                sprintf(line, "\033[%d;%dH%s\033(0%s\033(B\033[0m",
                        r_end, bx, sattr, shades);
                write_str(line);
            }
        }
    }
}

OYK_FLASHMEM void TinyTmuxConsole::draw_dec_hdiv(int row, int col, int w) {
    if (w < 4) return;
    char line[160];
    int inner = w - 2;
    if (inner > 130) inner = 130;
    sprintf(line, "\033[%d;%dH%s\033(0t", row, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) line[i] = 'q';
    line[inner] = 'u';
    line[inner + 1] = '\0';
    write_str(line);
    write_str("\033(B\033[0m");
}

void TinyTmuxConsole::write_str(const char* s) {
    if (tx_fn_ && s && s[0]) {
        tx_fn_(s, strlen(s), tx_ctx_);
    }
}

void TinyTmuxConsole::tui_get_size_cb(void* ctx, int* out_rows, int* out_cols) {
    const TinyTmuxConsole* c = static_cast<const TinyTmuxConsole*>(ctx);
    if (out_rows) *out_rows = c ? c->term_rows_ : 25;
    if (out_cols) *out_cols = c ? c->term_cols_ : 80;
}

void TinyTmuxConsole::tui_put_text_cb(void* ctx, int row, int col, int max_w, int style, const char* text) {
    TinyTmuxConsole* c = static_cast<TinyTmuxConsole*>(ctx);
    if (!c || max_w <= 0) return;
    if (max_w > 132) max_w = 132;
    const char* sgr = "\033[0m";
    switch (style) {
        case OYK_TUI_STYLE_BORDER:
        case OYK_TUI_STYLE_DLG_BORDER: sgr = c->sgr_border(); break;
        case OYK_TUI_STYLE_TITLE:      sgr = c->sgr_title(); break;
        case OYK_TUI_STYLE_HEADER:     sgr = c->sgr_header(); break;
        case OYK_TUI_STYLE_MENU_BAR:
        case OYK_TUI_STYLE_DLG_BAR:    sgr = c->sgr_menubar(); break;
        case OYK_TUI_STYLE_MENU_SEL:
        case OYK_TUI_STYLE_DLG_SEL:    sgr = c->sgr_sel(); break;
        case OYK_TUI_STYLE_LIST_HDR:   sgr = c->sgr_list_hdr(); break;
        case OYK_TUI_STYLE_WARN:
        case OYK_TUI_STYLE_ERROR:      sgr = c->sgr_warn(); break;
        case OYK_TUI_STYLE_CLOSE_BTN:
            sgr = (c->term_type_ >= TMUX_TERM_VT220) ? "\033[1;37;41m" : "\033[7m";
            break;
        default: sgr = "\033[0m"; break;
    }
    char out[256];
    const char* txt = text ? text : "";
    sprintf(out, "\033[%d;%dH%s%-*.*s\033[0m", row + 1, col + 1, sgr, max_w, max_w, txt);
    c->write_str(out);
}

void TinyTmuxConsole::tui_put_box_cb(void* ctx, int top, int left, int h, int w,
                                     int /*bdr*/, int /*fill*/, int fill_interior) {
    TinyTmuxConsole* c = static_cast<TinyTmuxConsole*>(ctx);
    if (c) c->draw_dec_box(top + 1, left + 1, h, w, NULL, fill_interior, 0);
}

void TinyTmuxConsole::tui_put_hdiv_cb(void* ctx, int row, int left, int w, int /*style*/) {
    TinyTmuxConsole* c = static_cast<TinyTmuxConsole*>(ctx);
    if (c) c->draw_dec_hdiv(row + 1, left + 1, w);
}

void TinyTmuxConsole::tui_put_shadow_cb(void* ctx, int top, int left, int h, int w) {
    TinyTmuxConsole* c = static_cast<TinyTmuxConsole*>(ctx);
    if (c) c->draw_dec_shadow(top + 1, left + 1, h, w);
}

OykTuiBackend TinyTmuxConsole::make_tui_backend() {
    OykTuiBackend b;
    memset(&b, 0, sizeof(b));
    b.ctx = this;
    b.get_size = TinyTmuxConsole::tui_get_size_cb;
    b.put_text = TinyTmuxConsole::tui_put_text_cb;
    b.put_box = TinyTmuxConsole::tui_put_box_cb;
    b.put_hdiv = TinyTmuxConsole::tui_put_hdiv_cb;
    b.put_shadow = TinyTmuxConsole::tui_put_shadow_cb;
    return b;
}

OYK_FLASHMEM void TinyTmuxConsole::push_lterm_line(const char* line) {
    if (!line) return;
    if (lterm_line_count_ < 32) {
        strncpy(lterm_lines_[lterm_line_count_], line, 199);
        lterm_lines_[lterm_line_count_][199] = '\0';
        lterm_line_count_++;
    } else {
        for (int i = 1; i < 32; ++i) {
            memcpy(lterm_lines_[i - 1], lterm_lines_[i], 200);
        }
        strncpy(lterm_lines_[31], line, 199);
        lterm_lines_[31][199] = '\0';
    }
    lterm_scroll_ = 0;
}

OYK_FLASHMEM void TinyTmuxConsole::on_local_term_event(oyk_u32 sid, int event_type,
                                                       const oyk_u8* data, size_t len,
                                                       const char* reason) {
    if (sid != lterm_sid_) {
        if (lterm_opening_ && lterm_sid_ == 0 && sid != 0) {
            lterm_sid_ = sid;
        } else {
            return;
        }
    }
    if (event_type == 1) {
        push_lterm_line("*** Connected.");
    } else if (event_type == 2 && data && len > 0) {
        for (size_t i = 0; i < len; ++i) {
            oyk_u8 ch = data[i];
            int act = oyk_term_eol_feed_byte(&lterm_eol_, ch);
            if (act == OYK_TERM_ACT_NEWLINE) {
                lterm_curline_[lterm_curline_len_] = '\0';
                push_lterm_line(lterm_curline_);
                lterm_curline_len_ = 0;
            } else if (act == OYK_TERM_ACT_BS) {
                if (lterm_curline_len_ > 0) {
                    --lterm_curline_len_;
                    lterm_curline_[lterm_curline_len_] = '\0';
                }
            } else if (act == OYK_TERM_ACT_CHAR) {
                if (lterm_curline_len_ >= 199) {
                    lterm_curline_[lterm_curline_len_] = '\0';
                    push_lterm_line(lterm_curline_);
                    lterm_curline_len_ = 0;
                }
                lterm_curline_[lterm_curline_len_++] = (ch == '\t') ? ' ' : static_cast<char>(ch);
                lterm_curline_[lterm_curline_len_] = '\0';
                lterm_scroll_ = 0;
            }
        }
    } else if (event_type == 3) {
        if (lterm_curline_len_ > 0) {
            lterm_curline_[lterm_curline_len_] = '\0';
            push_lterm_line(lterm_curline_);
            lterm_curline_len_ = 0;
        }
        lterm_sid_ = 0;
        lterm_is_local_ = 0;
        char msg[80];
        sprintf(msg, "*** Disconnected (%s)", reason ? reason : "Closed");
        push_lterm_line(msg);
    }
    if (!lterm_opening_ && view_ == TMUX_VIEW_LOCAL_TERM) {
        render_local_terminal();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render() {
    if (!node_ || !tx_fn_) return;
    if (term_type_ >= TMUX_TERM_VT220) {
        write_str("\033[?25l");
    }
    switch (view_) {
        case TMUX_VIEW_LOCAL_TERM:
            render_local_terminal();
            break;
        case TMUX_VIEW_FILES:
            render_console();
            render_files();
            break;
        case TMUX_VIEW_RIG:
            render_rig();
            break;
        case TMUX_VIEW_SETTINGS:
            render_console();
            render_settings();
            break;
        case TMUX_VIEW_PSRAM:
            render_psram();
            break;
        case TMUX_VIEW_BOOTLOADER:
            render_console();
            render_bootloader();
            break;
        case TMUX_VIEW_STORAGE:
            render_storage();
            break;
        case TMUX_VIEW_ABOUT:
            render_console();
            render_about();
            break;
        case TMUX_VIEW_MENU:
            render_console();
            render_menu();
            break;
        default:
            render_console();
            break;
    }
    if (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD) {
        render_add_iface_wizard();
    } else if (prompt_kind_ == PROMPT_SETTINGS_EDIT_IFACE) {
        render_edit_iface_dialog();
    } else if (prompt_kind_ == PROMPT_SETTINGS_EDIT_PEER) {
        render_edit_peer_dialog();
    } else if (prompt_kind_ == PROMPT_CONFIRM_FORMAT) {
        render_confirm_format_modal();
    } else if (prompt_kind_ == PROMPT_TERMINATE_SID) {
        render_terminate_dialog();
    } else if (prompt_kind_ == PROMPT_BAN_CALLSIGN) {
        render_ban_dialog();
    } else if (prompt_kind_ != PROMPT_NONE) {
        render_prompt_overlay();
    } else if (view_ == TMUX_VIEW_LOCAL_TERM && lterm_help_open_) {
        render_lterm_help_modal();
    }
    if (term_type_ >= TMUX_TERM_VT220 &&
        ((view_ == TMUX_VIEW_LOCAL_TERM && !lterm_help_open_) ||
         (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD &&
          (wiz_step_ == 1 ||
           (wiz_step_ == 2 &&
            wiz_focus_ < ((wiz_drv_ == 0 || wiz_drv_ == 4) ? 3 : ((wiz_drv_ == 1) ? 3 : 2))))) ||
         (prompt_kind_ == PROMPT_SETTINGS_EDIT_IFACE &&
          wiz_focus_ >= 1 &&
          wiz_focus_ < ((wiz_drv_ == 0 || wiz_drv_ == 4) ? 6 : ((wiz_drv_ == 1) ? 6 : 5))) ||
         (prompt_kind_ == PROMPT_SETTINGS_EDIT_PEER &&
          (wiz_focus_ == 0 || wiz_focus_ == 1 || wiz_focus_ == 2 || wiz_focus_ == 4 || wiz_focus_ == 5)) ||
         (prompt_kind_ == PROMPT_BAN_CALLSIGN && !wiz_btn_ && wiz_focus_ <= 2) ||
         (prompt_kind_ != PROMPT_NONE &&
          prompt_kind_ != PROMPT_TERMINATE_SID &&
          prompt_kind_ != PROMPT_BAN_CALLSIGN &&
          prompt_kind_ != PROMPT_SETTINGS_ADD_WIZARD &&
          prompt_kind_ != PROMPT_SETTINGS_EDIT_IFACE &&
          prompt_kind_ != PROMPT_SETTINGS_EDIT_PEER))) {
        write_str("\033[?25h");
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_console() {
    char buf[320];
    char call_s[16], psram_s[8], usbh_s[8];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    tinytmux_psram_format_short(tinytmux_psram_info(), psram_s, sizeof(psram_s));
    if (bridge_) {
        bridge_->usb_host()->format_short_status(usbh_s, sizeof(usbh_s));
    } else {
        strcpy(usbh_s, "-");
    }

    OykTuiConsoleLayout lay;
    oyk_tui_compute_console_layout(term_rows_, term_cols_, &lay);
    const int ncols = lay.cols;

    write_str("\033[H\033[2J");

    /* Row 1: Top Menu Bar (full display width) */
    int hint_col = ncols - 34;
    if (hint_col < 9) hint_col = 9;
    sprintf(buf, "%s%-*s\033[0m\033[1;2H%s Node \033[0m\033[1;%dH%s %-21s  [%9s]\033[0m",
            sgr_menubar(), ncols, "",
            sgr_menusel(),
            hint_col, sgr_menubar(),
            lterm_sid_ ? "Tab/F9:Term(ACTIVE)" : "Tab/F9:Term | Ctrl-N",
            term_type_label());
    write_str(buf);

    /* Row 2: Header Bar with PSRAM and USBH hardware indicators (full width) */
    char hdr_txt[160];
    sprintf(hdr_txt, " OYK TMUX: %-9s Up:%-5lus C:%-2d [IN:%-5s] PSRAM:%-3s USBH:%-3s",
            call_s,
            static_cast<unsigned long>(node_->uptime_seconds()),
            node_->total_active_connections(),
            node_->accept_incoming() ? "ALLOW" : "DENY",
            psram_s,
            usbh_s);
    sprintf(buf, "\033[2;1H%s%-*.*s\033[0m", sgr_header(), ncols, ncols, hdr_txt);
    write_str(buf);

    if (lay.too_small) {
        sprintf(buf, "\033[4;2HTerminal too small (%dx%d). Minimum: 60x16.", ncols, lay.rows);
        write_str(buf);
        return;
    }

    /* Top split boxes: Interfaces (left) & Last 5 New Callsigns Heard (right) */
    draw_dec_box(lay.iface_box.top + 1, lay.iface_box.left + 1,
                 lay.iface_box.h, lay.iface_box.w, "Interfaces", 0, 0);
    draw_dec_box(lay.heard_box.top + 1, lay.heard_box.left + 1,
                 lay.heard_box.h, lay.heard_box.w, "Last 5 New Callsigns Heard", 0, 0);
    sprintf(buf, "\033[%d;%dH%s\033(0w\033(B\033[0m",
            lay.heard_box.top + 1, lay.heard_box.left + 1, sgr_border());
    write_str(buf);

    const int iw = lay.iface_box.w - 2;
    const int hw = lay.heard_box.w - 2;
    const int il = lay.iface_box.left + 2;
    const int hl = lay.heard_box.left + 2;
    const int top_hdr_row = lay.iface_box.top + 2;

    char lhdr[128], rhdr[128];
    sprintf(lhdr, " %-8s %-12s %-2s %-14s", "Port", "Type", "C", "Status / RX-TX");
    sprintf(rhdr, " %-2s %-9s %-7s %-8s", "#", "Callsign", "Iface", "Time");
    sprintf(buf, "\033[%d;%dH%s%-*.*s\033[0m\033[%d;%dH%s%-*.*s\033[0m",
            top_hdr_row, il, sgr_list_hdr(), iw, iw, lhdr,
            top_hdr_row, hl, sgr_list_hdr(), hw, hw, rhdr);
    write_str(buf);

    const int top_vis = (lay.iface_vis_rows > lay.heard_vis_rows) ? lay.iface_vis_rows : lay.heard_vis_rows;
    for (int i = 0; i < top_vis; ++i) {
        char left[128], right[128];
        if (i < lay.iface_vis_rows && i < node_->iface_count()) {
            const OykIfaceRuntime& r = node_->iface(i);
            sprintf(left, " %-8.8s %-12.12s %-2d %-14.14s",
                    r.cfg.id, r.cfg.type, node_->active_connections_on(i), r.status);
        } else {
            left[0] = '\0';
        }
        if (i < lay.heard_vis_rows && i < node_->new_heard_count()) {
            const OykNewHeard& nh = node_->new_heard(i);
            sprintf(right, " %d %-9.9s %-7.7s %-8.8s", i + 1, nh.callsign, nh.iface_id, nh.time_hms);
        } else {
            right[0] = '\0';
        }
        sprintf(buf, "\033[%d;%dH%-*.*s\033[%d;%dH%-*.*s",
                top_hdr_row + 1 + i, il, iw, iw, left,
                top_hdr_row + 1 + i, hl, hw, hw, right);
        write_str(buf);
    }

    /* Middle box: Active Connections */
    char conn_title[48];
    sprintf(conn_title, "Active Connections (%d) | Bans: %d",
            node_->total_active_connections(), node_->ban_count());
    draw_dec_box(lay.conn_box.top + 1, lay.conn_box.left + 1,
                 lay.conn_box.h, lay.conn_box.w, conn_title, 0, 0);
    const int cw = lay.conn_box.w - 2;
    char chdr[140];
    sprintf(chdr, " %-4s %-10s %-8s %-8s %-43s", "#", "Callsign", "Iface", "Since", "Service / Role");
    sprintf(buf, "\033[%d;%dH%s%-*.*s\033[0m",
            lay.conn_box.top + 2, lay.conn_box.left + 2, sgr_list_hdr(), cw, cw, chdr);
    write_str(buf);

    int shown = 0;
    for (int i = 0; i < node_->max_sessions() && shown < lay.conn_vis_rows; ++i) {
        const OykSession& s = node_->session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12], crow[160];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            const char* role = s.restricted_ban ? "BAN NOTICE"
                                                : (s.in_bbs ? "BBS"
                                                            : (s.in_chat ? "CHAT"
                                                                         : (s.is_local_term ? "LOCAL TERM" : "NODE SHELL")));
            sprintf(crow, " #%-3lu %-10.10s %-8.8s %-8.8s %-43.43s",
                    static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local",
                    s.since_hms, role);
            sprintf(buf, "\033[%d;%dH%-*.*s",
                    lay.conn_box.top + 3 + shown, lay.conn_box.left + 2, cw, cw, crow);
            write_str(buf);
            ++shown;
        }
    }
    for (int r = shown; r < lay.conn_vis_rows; ++r) {
        sprintf(buf, "\033[%d;%dH%-*.*s",
                lay.conn_box.top + 3 + r, lay.conn_box.left + 2, cw, cw,
                (r == 0 && shown == 0) ? " (no active connections)" : "");
        write_str(buf);
    }

    /* Lower box: Node Activity Log */
    draw_dec_box(lay.log_box.top + 1, lay.log_box.left + 1,
                 lay.log_box.h, lay.log_box.w, "Node Activity Log", 0, 0);
    const int lw = lay.log_box.w - 2;
    const int lvis = lay.log_vis_rows;
    int total_logs = node_->console_log_count();
    int start = (total_logs > lvis) ? (total_logs - lvis) : 0;
    int lrow = 0;
    for (int i = start; i < total_logs && lrow < lvis; ++i, ++lrow) {
        char lline[160];
        sprintf(lline, " %s", node_->console_log_line(i));
        sprintf(buf, "\033[%d;%dH%-*.*s", lay.log_box.top + 2 + lrow, lay.log_box.left + 2, lw, lw, lline);
        write_str(buf);
    }
    for (; lrow < lvis; ++lrow) {
        sprintf(buf, "\033[%d;%dH%-*s", lay.log_box.top + 2 + lrow, lay.log_box.left + 2, lw, "");
        write_str(buf);
    }

    /* Bottom Status Line */
    sprintf(buf, "\033[%d;1H%s %-*.*s \033[0m",
            lay.status_row + 1, sgr_warn(), ncols - 2, ncols - 2, status_line_);
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::render_menu() {
    static const char* ITEMS[13] = {
        " [A] Allow Incoming Connections   ",
        " [D] Deny Incoming Connections    ",
        " [L] Local Terminal...            ",
        " [F] Manage Files Area...         ",
        " [R] Radio Control...             ",
        " [S] Settings...                  ",
        " [T] Terminate Connection...      ",
        " [B] Ban / Unban Callsign...      ",
        " [?] About OYK-Node...            ",
        " [P] PSRAM Diagnostics...         ",
        " [G] Storage Manager...           ",
        " [U] Enter HalfKay Bootloader     ",
        " [X] Close Menu                   "
    };
    static const int ITEM_ROWS[13] = { 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17 };

    draw_dec_box(2, 2, 17, 36, "Node Menu", 1);
    draw_dec_shadow(2, 2, 17, 36);
    draw_dec_hdiv(5, 2, 36);
    draw_dec_hdiv(16, 2, 36);
    for (int i = 0; i < 13; ++i) {
        char row[80];
        sprintf(row, "\033[%d;3H%s%.34s\033[0m",
                ITEM_ROWS[i], (i == menu_sel_) ? sgr_menusel() : "", ITEMS[i]);
        write_str(row);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_local_terminal() {
    char buf[320];
    OykTuiTermLayout tlay;
    oyk_tui_compute_term_layout(term_rows_, term_cols_, &tlay);
    const int ncols = tlay.cols;
    const int body_w = (ncols > 2) ? (ncols - 2) : 1;
    const int body_h = (tlay.body_rows > 0) ? tlay.body_rows : 1;
    const int show_live = (lterm_curline_len_ > 0) ? 1 : 0;

    int total_rows = 0;
    for (int i = 0; i < lterm_line_count_; ++i) {
        total_rows += oyk_term_count_wrapped_rows(lterm_lines_[i], strlen(lterm_lines_[i]), body_w);
    }
    if (show_live) {
        lterm_curline_[lterm_curline_len_] = '\0';
        total_rows += oyk_term_count_wrapped_rows(lterm_curline_, static_cast<size_t>(lterm_curline_len_), body_w);
    }
    int max_scroll = (total_rows > body_h) ? (total_rows - body_h) : 0;
    if (lterm_scroll_ > max_scroll) lterm_scroll_ = max_scroll;
    if (lterm_scroll_ < 0) lterm_scroll_ = 0;
    int first_vis = max_scroll - lterm_scroll_;

    write_str("\033[H\033[2J");
    char hdr[180];
    if (lterm_scroll_ > 0) {
        sprintf(hdr, " OYK LOCAL TERMINAL (%s) [EOL:%s] [UP %d] | Tab:Console",
                lterm_sid_ ? "CONNECTED" : "DISCONNECTED",
                oyk_term_eol_label(&lterm_eol_), lterm_scroll_);
    } else {
        sprintf(hdr, " OYK LOCAL TERMINAL (%s) [EOL:%s] | Ctrl+A Z:Help  Tab:Console",
                lterm_sid_ ? "CONNECTED" : "DISCONNECTED",
                oyk_term_eol_label(&lterm_eol_));
    }
    sprintf(buf, "\033[%d;1H%s%-*.*s\033[0m", tlay.header_row + 1, sgr_header(), ncols, ncols, hdr);
    write_str(buf);

    int vis_idx = 0;
    int out_row = 0;
    const int nlines = lterm_line_count_ + show_live;
    for (int li = 0; li < nlines && out_row < body_h; ++li) {
        const char* ln = (li < lterm_line_count_) ? lterm_lines_[li] : lterm_curline_;
        OykTermWrapSpan spans[16];
        int sc = oyk_term_wrap_line(ln, strlen(ln), body_w, spans, 16);
        for (int si = 0; si < sc && out_row < body_h; ++si) {
            if (vis_idx >= first_vis) {
                char seg[160];
                size_t slen = spans[si].len;
                if (slen >= sizeof(seg)) slen = sizeof(seg) - 1;
                memcpy(seg, ln + spans[si].start, slen);
                seg[slen] = '\0';
                sprintf(buf, "\033[%d;2H%-*.*s", tlay.body_top + 1 + out_row, body_w, body_w, seg);
                write_str(buf);
                ++out_row;
            }
            ++vis_idx;
        }
    }
    const char* hk_bar = oyk_term_hotkey_bar_text(1, lterm_ctrl_a_);
    sprintf(buf, "\033[%d;1H%s %-*.*s \033[0m",
            tlay.hotkey_row + 1, sgr_menubar(), ncols - 2, ncols - 2, hk_bar);
    write_str(buf);
    int in_avail = (ncols > 3) ? (ncols - 3) : 1;
    size_t in_start = 0;
    int cur_col = 0;
    oyk_term_input_slice(static_cast<size_t>(lterm_input_len_), in_avail, &in_start, &cur_col);
    sprintf(buf, "\033[%d;1H> %-*.*s\033[%d;%dH",
            tlay.input_row + 1, in_avail, in_avail, lterm_input_ + in_start,
            tlay.input_row + 1, 3 + cur_col);
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::render_lterm_help_modal() {
    OykTuiBackend b = make_tui_backend();
    oyk_tui_draw_term_help_modal(&b, term_rows_, term_cols_);
}

void TinyTmuxConsole::refresh_files_lists() {
    const char* froot = (node_ && node_->config().bbs_files_path[0])
                            ? node_->config().bbs_files_path
                            : "files";
    if (strcmp(file_mgr_.files.root, froot) != 0 || !file_mgr_.inc.root[0]) {
        oyk_gui_filemgr_init(&file_mgr_, "incoming", froot);
    } else {
        oyk_gui_filemgr_refresh(&file_mgr_);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_files() {
    OykTuiBackend b = make_tui_backend();
    oyk_tui_draw_dual_pane_modal(&b, term_rows_, term_cols_, &file_mgr_,
                                 files_focus_btn_, "Files Area Manager",
                                 fm_preview_buf_, fm_preview_active_);
}

OYK_FLASHMEM void TinyTmuxConsole::render_rig() {
    char buf[192];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s RADIO CONTROL (Hamlib rigctl / RadioFoundry UDP 4191)         Esc:Close \033[0m", sgr_header());
    write_str(buf);
    draw_dec_box(2, 1, 20, 80, "Transceiver Control", 0);

    const OykConfig& cfg = node_->config();
    if (!cfg.rig_enabled) {
        write_str("\033[4;3HRig control is DISABLED. Configure in Settings (S) -> Tab 5:Rig.");
    } else {
        OykRigStatus st;
        oyk_rig_status_init(&st, oyk_rig_parse_protocol(cfg.rig_protocol));
        if (bridge_) st = *bridge_->rig_status();

        sprintf(buf, "\033[4;3HProtocol : %-14s  Target: %s:%u  Notify: %s",
                oyk_rig_protocol_name(st.protocol),
                cfg.rig_host[0] ? cfg.rig_host : "(peer)",
                static_cast<unsigned int>(cfg.rig_port ? cfg.rig_port : oyk_rig_default_port(st.protocol)),
                cfg.rig_notify ? "ON" : "OFF");
        write_str(buf);
        sprintf(buf, "\033[5;3HStatus   : %s", st.reachable ? "REACHABLE" : "NO RESPONSE / IDLE");
        write_str(buf);

        char summary[128];
        oyk_rig_format_summary(&st, summary, sizeof(summary));
        sprintf(buf, "\033[6;3HRadio    : %.64s", summary);
        write_str(buf);

        if (st.protocol == OYK_RIG_PROTO_RADIOFOUNDRY) {
            write_str("\033[8;3HCommands (RadioFoundry / takRF UDP 4191):");
            write_str("\033[9;3H  [J/K] Step Channel -/+1    [H/L] Step Zone -/+1");
            write_str("\033[10;3H  [S]   Select Zone:Pos      [N]   Stop Notify     [R] Poll Now");
        } else {
            write_str("\033[8;3HCommands (Hamlib rigctld TCP 4532):");
            write_str("\033[9;3H  [F] Set Frequency (Hz)     [M] Set Mode          [P] Toggle PTT");
            write_str("\033[10;3H  [V] Set VFO (VFOA/VFOB)    [R] Poll Status");
        }
    }
    sprintf(buf, "\033[24;1H%s Feedback: %-68.68s \033[0m",
            sgr_warn(), dialog_fb_[0] ? dialog_fb_ : "Ready.");
    write_str(buf);
}

int TinyTmuxConsole::settings_tab_item_count(int tab) const {
    if (tab == 0) return 11;
    if (tab == 1) return 8;
    if (tab == 2) return 6;
    if (tab == 3) return draft_.iface_count;
    if (tab == 4) return draft_.peer_count;
    if (tab == 5) return 6;
    if (tab == 7) return OYK_THEME_COLOR_COUNT;
    return 6;
}

OYK_FLASHMEM void TinyTmuxConsole::render_settings() {
    static const char* TAB_NAMES[8] = {
        "1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn", "8:Thm"
    };
    static const char* NODE_LABELS[11] = {
        "Node Callsign", "Node Alias", "Welcome Message", "Info Message",
        "Allow Incoming (Default)", "Allow Logging to Flash",
        "Learned Peer Policy", "New User Welcome",
        "Interfaces File", "Peers File", "Themes File"
    };
    static const char* BBS_LABELS[8] = {
        "Enable Packet BBS", "BBS SSID (0-15)", "Callsign Override", "BBS Alias",
        "Enable BBS Files Area", "Files Area Root Path", "Mail Storage Path", "BBS Welcome Message"
    };
    static const char* CHAT_LABELS[6] = {
        "Enable Multi-User CHAT", "CHAT SSID (0-15)", "Callsign Override", "CHAT Alias",
        "Default Topic", "CHAT Welcome Message"
    };
    static const char* RIG_LABELS[6] = {
        "Enable Rig Control", "Protocol (rigctl/radiofoundry)", "Radio Host / IP",
        "Port (4532 / 4191)", "Poll Interval (sec)", "RadioFoundry Notify"
    };
    static const char* PRN_LABELS[6] = {
        "Enable Line Printer", "Device (usb-printer/serN)", "Protocol (generic/mx80/escp2)",
        "Serial Baud Rate", "Printer Log Mask (0x..)", "Condensed 132-Col Mode"
    };

    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 21, 74, &mr);
    const int top = mr.top + 1;
    const int left = mr.left + 1;
    char buf[192];
    draw_dec_box(top, left, mr.h, mr.w, "Node Settings", 1, 1);
    draw_dec_shadow(top, left, mr.h, mr.w);
    sprintf(buf, "\033[%d;%dH", top + 1, left + 2);
    write_str(buf);
    for (int t = 0; t < 8; ++t) {
        sprintf(buf, "%s %s \033[0m ", (t == settings_tab_) ? sgr_menusel() : sgr_menubar(), TAB_NAMES[t]);
        write_str(buf);
    }
    draw_dec_hdiv(top + 2, left, mr.w);

    int cnt = settings_tab_item_count(settings_tab_);
    if (settings_tab_ == 7) {
        const char* active = draft_.theme_name[0] ? draft_.theme_name : "dark";
        OykThemeConfig pal;
        oyk_theme_resolve_effective(&draft_, 0, 1, &pal);
        sprintf(buf, "\033[%d;%dHTheme  : < %-18.18s > (%s)",
                top + 3, left + 2, active,
                oyk_theme_is_builtin(active) ? "Built-in (not saved)" : "Custom");
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%s %-16s %-13s %-8s Preview \033[0m",
                top + 4, left + 2, sgr_list_hdr(), "Role", "Key", "Color");
        write_str(buf);
        const int vis_rows = 10;
        if (settings_sel_[7] < theme_scroll_) theme_scroll_ = settings_sel_[7];
        if (settings_sel_[7] >= theme_scroll_ + vis_rows) theme_scroll_ = settings_sel_[7] - vis_rows + 1;
        for (int r = 0; r < vis_rows && (theme_scroll_ + r) < OYK_THEME_COLOR_COUNT; ++r) {
            int idx = theme_scroll_ + r;
            char hx[12];
            oyk_theme_format_hex(pal.colors[idx], hx, sizeof(hx));
            sprintf(buf, "\033[%d;%dH%s %-16.16s %-13.13s %-8.8s\033[0m ",
                    top + 5 + r, left + 2,
                    (idx == settings_sel_[7]) ? sgr_sel() : "",
                    oyk_theme_role_label(idx), oyk_theme_role_key(idx), hx);
            write_str(buf);
            if (term_type_ >= TMUX_TERM_VT220) {
                write_str(tmux_format_sgr_colors(6, pal.colors[idx], pal.colors[idx], 1, 0, term_type_));
                write_str("    \033[0m");
            } else {
                write_str("[##]");
            }
        }
        sprintf(buf, "\033[%d;%dH%s %-70.70s \033[0m",
                top + 19, left + 1,
                sgr_menubar(), "Tab/1-8:Tabs  Lt/Rt:Theme  Enter:Edit  X:Del  S/F2:Save  Esc:Cancel");
        write_str(buf);
    } else if (settings_tab_ != 3 && settings_tab_ != 4) {
        const char** labels = (settings_tab_ == 0) ? NODE_LABELS
                            : ((settings_tab_ == 1) ? BBS_LABELS
                                                    : ((settings_tab_ == 2) ? CHAT_LABELS
                                                                            : ((settings_tab_ == 5) ? RIG_LABELS : PRN_LABELS)));
        for (int i = 0; i < cnt; ++i) {
            char val[56], cs[16];
            val[0] = '\0';
            if (settings_tab_ == 0) {
                if (i == 0) oyk_format_callsign(&draft_.callsign, val, sizeof(val));
                else if (i == 1) { if (draft_.alias.base[0]) oyk_format_callsign(&draft_.alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 2) { strncpy(val, draft_.welcome_text, 44); val[44] = '\0'; }
                else if (i == 3) { strncpy(val, draft_.info_text, 44); val[44] = '\0'; }
                else if (i == 4) strcpy(val, draft_.accept_incoming ? "[ON]" : "[OFF]");
                else if (i == 5) strcpy(val, draft_.allow_flash_logging ? "[ON]" : "[OFF]");
                else if (i == 6) strcpy(val, oyk_format_learned_policy(draft_.learned_peer_policy));
                else if (i == 7) { if (draft_.new_user_welcome[0]) { strncpy(val, draft_.new_user_welcome, 44); val[44] = '\0'; } else strcpy(val, "(use default)"); }
                else if (i == 8) { strncpy(val, draft_.interfaces_file, 44); val[44] = '\0'; }
                else if (i == 9) { strncpy(val, draft_.peers_file, 44); val[44] = '\0'; }
                else { strncpy(val, draft_.themes_file, 44); val[44] = '\0'; }
            } else if (settings_tab_ == 1) {
                if (i == 0) strcpy(val, draft_.bbs_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign bc; oyk_config_resolved_bbs_callsign(&draft_, &bc); oyk_format_callsign(&bc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.bbs_ssid), cs); }
                else if (i == 2) { if (draft_.bbs_callsign.base[0]) oyk_format_callsign(&draft_.bbs_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.bbs_alias.base[0]) oyk_format_callsign(&draft_.bbs_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) strcpy(val, draft_.bbs_files_enabled ? "[ON]" : "[OFF]");
                else if (i == 5) { strncpy(val, draft_.bbs_files_path, 44); val[44] = '\0'; }
                else if (i == 6) { strncpy(val, draft_.bbs_mail_path, 44); val[44] = '\0'; }
                else { strncpy(val, draft_.bbs_welcome, 44); val[44] = '\0'; }
            } else if (settings_tab_ == 2) {
                if (i == 0) strcpy(val, draft_.chat_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { OykCallsign cc; oyk_config_resolved_chat_callsign(&draft_, &cc); oyk_format_callsign(&cc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft_.chat_ssid), cs); }
                else if (i == 2) { if (draft_.chat_callsign.base[0]) oyk_format_callsign(&draft_.chat_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                else if (i == 3) { if (draft_.chat_alias.base[0]) oyk_format_callsign(&draft_.chat_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                else if (i == 4) { strncpy(val, draft_.chat_topic, 44); val[44] = '\0'; }
                else { strncpy(val, draft_.chat_welcome, 44); val[44] = '\0'; }
            } else if (settings_tab_ == 5) {
                if (i == 0) strcpy(val, draft_.rig_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { strncpy(val, draft_.rig_protocol[0] ? draft_.rig_protocol : "none", 44); val[44] = '\0'; }
                else if (i == 2) { strncpy(val, draft_.rig_host, 44); val[44] = '\0'; }
                else if (i == 3) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_port));
                else if (i == 4) sprintf(val, "%u", static_cast<unsigned int>(draft_.rig_poll_sec));
                else strcpy(val, draft_.rig_notify ? "[ON]" : "[OFF]");
            } else {
                if (i == 0) strcpy(val, draft_.printer_enabled ? "[ON]" : "[OFF]");
                else if (i == 1) { strncpy(val, draft_.printer_device[0] ? draft_.printer_device : "(none)", 44); val[44] = '\0'; }
                else if (i == 2) { strncpy(val, draft_.printer_protocol[0] ? draft_.printer_protocol : "generic", 44); val[44] = '\0'; }
                else if (i == 3) sprintf(val, "%lu", static_cast<unsigned long>(draft_.printer_baud ? draft_.printer_baud : 9600UL));
                else if (i == 4) sprintf(val, "0x%lX", static_cast<unsigned long>(draft_.printer_level));
                else strcpy(val, draft_.printer_condensed ? "[ON]" : "[OFF]");
            }
            if (settings_tab_ == 0 && (i == 8 || i == 9 || i == 10) && val[0] == '\0' && term_type_ >= TMUX_TERM_VT340) {
                sprintf(buf, "\033[%d;%dH%s %-26.26s : \033[2m<inline>\033[22m                                \033[0m",
                        top + 3 + i, left + 2, (i == settings_sel_[settings_tab_]) ? sgr_sel() : "", labels[i]);
            } else {
                sprintf(buf, "\033[%d;%dH%s %-26.26s : %-40.40s\033[0m",
                        top + 3 + i, left + 2, (i == settings_sel_[settings_tab_]) ? sgr_sel() : "", labels[i], val);
            }
            write_str(buf);
        }
        sprintf(buf, "\033[%d;%dH%s %-70.70s \033[0m",
                top + 19, left + 1,
                sgr_menubar(), "Tab/1-8:Tabs  Up/Dn:Select  Enter/E:Edit  S/F2:Save  Esc:Cancel");
        write_str(buf);
    } else if (settings_tab_ == 3) {
        sprintf(buf, "\033[%d;%dH%s %-8s %-13s %-21s %-23s\033[0m",
                top + 3, left + 2, sgr_list_hdr(), "ID", "Type", "Dev/Host", "Port/Baud");
        write_str(buf);
        if (draft_.iface_count == 0) {
            sprintf(buf, "\033[%d;%dH (no interfaces configured; press N to add)", top + 4, left + 2);
            write_str(buf);
        } else {
            for (int i = 0; i < draft_.iface_count && i < 12; ++i) {
                const OykIfaceConfig& ic = draft_.ifaces[i];
                const char* loc = ic.device[0] ? ic.device : ic.host;
                char pb[28];
                sprintf(pb, "%u/%lu", static_cast<unsigned int>(ic.port), static_cast<unsigned long>(ic.baud));
                sprintf(buf, "\033[%d;%dH%s %-8.8s %-13.13s %-21.21s %-23.23s\033[0m",
                        top + 4 + i, left + 2, (i == settings_sel_[3]) ? sgr_sel() : "",
                        ic.id, ic.type, loc, pb);
                write_str(buf);
            }
        }
        sprintf(buf, "\033[%d;%dH%s %-70.70s \033[0m",
                top + 19, left + 1,
                sgr_menubar(), "N/A:New  Enter/E:Edit  D:Del  H:Host/Dev  S/F2:Save  Esc:Cancel");
        write_str(buf);
    } else {
        sprintf(buf, "\033[%d;%dH%s %-9s %-9s %-7s %-20s %-7s %-11s\033[0m",
                top + 3, left + 2, sgr_list_hdr(), "ID", "Callsign", "Iface", "Endpoint", "ACL", "Tag");
        write_str(buf);
        if (draft_.peer_count == 0) {
            sprintf(buf, "\033[%d;%dH (no peers configured; press A to add)", top + 4, left + 2);
            write_str(buf);
        } else {
            for (int i = 0; i < draft_.peer_count && i < 12; ++i) {
                const OykPeerConfig& pc = draft_.peers[i];
                char ep[26], acl[10];
                if (pc.host[0] && pc.port > 0) sprintf(ep, "%.14s:%u", pc.host, static_cast<unsigned int>(pc.port));
                else if (pc.host[0]) sprintf(ep, "%.19s", pc.host);
                else strcpy(ep, "(any)");
                sprintf(acl, "%s/%s", pc.allow_inbound ? "IN" : "--", pc.allow_outbound ? "OUT" : "---");
                sprintf(buf, "\033[%d;%dH%s %-9.9s %-9.9s %-7.7s %-20.20s %-7.7s %-11.11s\033[0m",
                        top + 4 + i, left + 2, (i == settings_sel_[4]) ? sgr_sel() : "",
                        pc.id, pc.callsign, pc.interface_id, ep, acl,
                        pc.auto_learned ? "[AUTO]" : "");
                write_str(buf);
            }
        }
        sprintf(buf, "\033[%d;%dH%s %-70.70s \033[0m",
                top + 19, left + 1,
                sgr_menubar(), "A:Add  E:Edit  D:Del  I:In  O:Out  K:KeepAuto  S/F2:Save  Esc:Back");
        write_str(buf);
    }
    sprintf(buf, "\033[%d;%dH%s%-70.70s\033[0m",
            top + 17, left + 2, sgr_warn(),
            dialog_fb_[0] ? dialog_fb_ : "Edit settings, then press S or F2 to Save & Apply.");
    write_str(buf);
    draw_dec_hdiv(top + 18, left, mr.w);
}

static void format_progress_bar(oyk_u8 pct, char* out, size_t out_sz) {
    if (!out || out_sz < 23) return;
    int filled = (static_cast<int>(pct) * 20) / 100;
    if (filled < 0) filled = 0;
    if (filled > 20) filled = 20;
    out[0] = '[';
    for (int i = 0; i < 20; ++i) {
        out[1 + i] = (i < filled) ? '#' : '.';
    }
    out[21] = ']';
    out[22] = '\0';
}

OYK_FLASHMEM void TinyTmuxConsole::render_psram() {
    char buf[192], tag[8];
    const TinyTmuxPsramInfo* info = tinytmux_psram_info();
    const TinyTmuxMarchCStatus* mc = tinytmux_psram_marchc_status();
    tinytmux_psram_format_short(info, tag, sizeof(tag));
    write_str("\033[H\033[2J");
    sprintf(buf, "%s TEENSY 4.1 FLEXSPI2 PSRAM DIAGNOSTICS   T:Quick  M:MarchC-  Esc:Back \033[0m", sgr_header());
    write_str(buf);
    draw_dec_box(2, 1, 21, 80, "FlexSPI2 Hardware & March C-", 0);

    sprintf(buf, "\033[3;3HStatus      : %-6s  Base: 0x%08lX  Quick test: %lu ms",
            tag,
            static_cast<unsigned long>(info->base_addr),
            static_cast<unsigned long>(info->test_time_ms));
    write_str(buf);
    sprintf(buf, "\033[4;3HCapacity    : %lu MB usable / %lu MB detected (%d/%d chips passed)",
            static_cast<unsigned long>(info->usable_bytes / (1024UL * 1024UL)),
            static_cast<unsigned long>(info->detected_bytes / (1024UL * 1024UL)),
            info->usable_chip_count, info->chip_count);
    write_str(buf);

    int r = 6;
    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        const TinyTmuxPsramChipInfo& c = info->chips[i];
        const char* kind_str = "EMPTY";
        if (c.chip_kind == TMUX_FLEXSPI_PSRAM) kind_str = "PSRAM";
        else if (c.chip_kind == TMUX_FLEXSPI_FLASH_NOR) kind_str = "QSPI NOR";
        else if (c.chip_kind == TMUX_FLEXSPI_FLASH_NAND) kind_str = "QSPI NAND";
        else if (c.detected) kind_str = "DETECTED";
        sprintf(buf, "\033[%d;3HChip %d (CS%d @ 0x%08lX): %-9s  %s  %lu MB  JEDEC=%02X %02X %02X",
                r++, i, i,
                static_cast<unsigned long>(c.base_addr),
                kind_str,
                (c.chip_kind == TMUX_FLEXSPI_PSRAM) ? (c.passed ? "PASS" : "FAIL")
                                                    : (c.detected ? "FLASH" : "-"),
                static_cast<unsigned long>(c.size_bytes / (1024UL * 1024UL)),
                static_cast<unsigned int>(c.jedec_id[0]),
                static_cast<unsigned int>(c.jedec_id[1]),
                static_cast<unsigned int>(c.jedec_id[2]));
        write_str(buf);
        if (c.chip_kind == TMUX_FLEXSPI_PSRAM && mc->state != TMUX_MARCHC_IDLE &&
            mc->chip_result[i] != TMUX_MARCHC_CHIP_NONE) {
            char bar[28];
            format_progress_bar(mc->chip_percent[i], bar, sizeof(bar));
            const char* res_s = "WAIT";
            if (mc->chip_result[i] == TMUX_MARCHC_CHIP_RUNNING) res_s = mc->pass_label;
            else if (mc->chip_result[i] == TMUX_MARCHC_CHIP_PASS) res_s = "PASS";
            else if (mc->chip_result[i] == TMUX_MARCHC_CHIP_FAIL) res_s = "FAIL";
            sprintf(buf, "\033[%d;5HMarch C-: %s %3u%%  %-16s",
                    r++, bar, static_cast<unsigned int>(mc->chip_percent[i]), res_s);
            write_str(buf);
        }
        if (c.detected && c.chip_kind == TMUX_FLEXSPI_PSRAM && !c.passed) {
            sprintf(buf, "\033[%d;5HFault @ 0x%08lX  Check footprint: %s",
                    r++, static_cast<unsigned long>(c.fail_addr),
                    tinytmux_psram_footprint_label(i));
            write_str(buf);
        }
    }

    if (info->misplaced_flash_cs0) {
        sprintf(buf, "\033[%d;3H%s WARNING: QSPI FLASH ON WRONG PADS (CS0 / Pin 48 small footprint)! \033[0m",
                r++, sgr_sel());
        write_str(buf);
        sprintf(buf, "\033[%d;3HTeensy 4.1 LittleFS_QSPI only supports CS1 (large pads / Pin 51).", r++);
        write_str(buf);
        sprintf(buf, "\033[%d;3HStartup CS0 probes PSRAM first; move flash to CS1 to use QSPI storage.", r++);
        write_str(buf);
    }

    ++r;
    if (mc->state == TMUX_MARCHC_RUNNING) {
        sprintf(buf, "\033[%d;3HMarch C- RUNNING: Chip %d (%s)  Elapsed: %lu ms  (Esc: Cancel)",
                r, mc->active_chip, mc->pass_label, static_cast<unsigned long>(mc->elapsed_ms));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_PASSED) {
        sprintf(buf, "\033[%d;3HMarch C- PASSED cleanly on all detected chips in %lu ms.",
                r, static_cast<unsigned long>(mc->elapsed_ms));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_FAILED) {
        sprintf(buf, "\033[%d;3H%s MARCH C- FAIL: Chip %d @ 0x%08lX (exp %08lX got %08lX) \033[0m",
                r++, sgr_sel(), mc->fail_chip,
                static_cast<unsigned long>(mc->fail_addr),
                static_cast<unsigned long>(mc->fail_expected),
                static_cast<unsigned long>(mc->fail_actual));
        write_str(buf);
        sprintf(buf, "\033[%d;3HCheck/resolder footprint: %s", r, tinytmux_psram_footprint_label(mc->fail_chip));
        write_str(buf);
    } else if (mc->state == TMUX_MARCHC_CANCELLED) {
        sprintf(buf, "\033[%d;3HMarch C- test cancelled by operator.", r);
        write_str(buf);
    } else {
        sprintf(buf, "\033[%d;3HPress T for quick self-test, M for full March C- test, Esc to return.", r);
        write_str(buf);
    }
    sprintf(buf, "\033[24;1H%s T:Quick Test  M:Full March C- Test  Esc:Return                             \033[0m",
            sgr_menubar());
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::render_storage() {
    char buf[200];
    write_str("\033[H\033[2J");
    sprintf(buf, "%s STORAGE MANAGER (Config, Files, Mail & Logs)                 Esc:Back \033[0m", sgr_header());
    write_str(buf);
    draw_dec_box(2, 1, 20, 80, "Non-Volatile Storage Tiers", 0);
    write_str("\033[3;3HSearch order: 1) microSD  2) QSPI Flash  3) Internal 4 MB Flash");
    sprintf(buf, "\033[5;2H%s %-2s %-28s %-9s %-14s %-21s\033[0m",
            sgr_list_hdr(), "#", "Storage Device", "Status", "Used / Total", "Log Policy");
    write_str(buf);

    int active = tinytmux_fs_active_tier();
    for (int t = TMUX_STORAGE_SD; t <= TMUX_STORAGE_INTERNAL; ++t) {
        TinyTmuxStorageTierInfo ti;
        tinytmux_fs_tier_info(t, &ti);
        const char* st = ti.mounted ? "ACTIVE" : (ti.detected ? "DETECTED" : "ABSENT");
        char cap[40];
        if (ti.total_kb >= 1024UL) {
            sprintf(cap, "%lu/%lu MB", static_cast<unsigned long>(ti.used_kb / 1024UL),
                    static_cast<unsigned long>(ti.total_kb / 1024UL));
        } else {
            sprintf(cap, "%lu/%lu KB", static_cast<unsigned long>(ti.used_kb),
                    static_cast<unsigned long>(ti.total_kb));
        }
        const char* logs = (t == TMUX_STORAGE_SD)
                               ? "Logs: ALLOWED"
                               : (node_ && node_->config().allow_flash_logging ? "Logs: ALLOWED (flash)"
                                                                               : "Logs: BLOCKED (SD)");
        sprintf(buf, "\033[%d;2H%s %d. %-28.28s %-9s %-14s %-20s\033[0m",
                5 + t, (t - 1 == storage_sel_) ? sgr_sel() : "",
                t, ti.label, st, cap, logs);
        write_str(buf);
    }
    sprintf(buf, "\033[10;3HActive tier: %d (%s)", active, tinytmux_fs_status_label());
    write_str(buf);
    sprintf(buf, "\033[22;1H%s Up/Dn:Select  F:Partition/Format (ERASES ALL DATA)  R:Rescan  Esc:Back    \033[0m",
            sgr_menubar());
    write_str(buf);
    sprintf(buf, "\033[24;1H%s %-78.78s \033[0m",
            sgr_warn(),
            dialog_fb_[0] ? dialog_fb_ : "Select a tier and press F to partition/format (ERASES ALL DATA).");
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::handle_storage_key(int key) {
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == TMUX_KEY_UP) {
        storage_sel_ = (storage_sel_ + 2) % 3;
        render();
    } else if (key == TMUX_KEY_DOWN) {
        storage_sel_ = (storage_sel_ + 1) % 3;
        render();
    } else if (key == TMUX_KEY_HOME) {
        storage_sel_ = 0;
        render();
    } else if (key == TMUX_KEY_END) {
        storage_sel_ = 2;
        render();
    } else if (key == 'r' || key == 'R') {
        int ok = tinytmux_fs_rescan();
        node_->set_storage_is_flash(tinytmux_fs_is_flash_storage());
        sprintf(dialog_fb_, "Rescan complete: %s.", ok ? tinytmux_fs_status_label() : "NO STORAGE");
        render();
    } else if (key == 'f' || key == 'F') {
        begin_prompt(PROMPT_CONFIRM_FORMAT, "Confirm Format", "");
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_bootloader() {
    char buf[160];
    int active = node_ ? node_->total_active_connections() : 0;
    int disc_left = node_ ? node_->disconnecting_count() : 0;
    int auto_cnt = node_ ? node_->auto_learned_peer_count() : 0;
    const char* title = (auto_cnt > 0 && !bootloader_pending_)
                            ? "Auto-Learned Peers - BOOTSEL"
                            : "Confirm Bootloader Entry";
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 13, 72, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_dec_box(top, left, mr.h, mr.w, title, 1, 1);
    draw_dec_shadow(top, left, mr.h, mr.w);

    if (!bootloader_pending_) {
        OykTuiBackend b = make_tui_backend();
        if (auto_cnt > 0) {
            sprintf(buf, "\033[%d;%dH%d auto-learned peer(s) were added this session.", top + 2, left + 2, auto_cnt);
            write_str(buf);
            sprintf(buf, "\033[%d;%dHSave them to config, discard them, or review in Peers", top + 4, left + 2);
            write_str(buf);
            sprintf(buf, "\033[%d;%dHbefore entering HalfKay bootloader?", top + 5, left + 2);
            write_str(buf);
            sprintf(buf, "\033[%d;%dHS:Save & Exit  D:Discard  R:Review Peers  N/Esc:Cancel", top + 7, left + 2);
            write_str(buf);
            draw_dec_hdiv(top + 10, left, mr.w);
            OykTuiButton btns[4];
            btns[0].label = "Save & Exit";  btns[0].enabled = 1;
            btns[1].label = "Discard";      btns[1].enabled = 1;
            btns[2].label = "Review Peers"; btns[2].enabled = 1;
            btns[3].label = "Cancel";       btns[3].enabled = 1;
            oyk_tui_draw_button_row(&b, mr.top + 11, mr.left + 2, mr.w - 4,
                                    btns, 4, 0, NULL);
        } else {
            if (active > 0) {
                sprintf(buf, "\033[%d;%dHEnter HalfKay bootloader? (%d active connection(s))", top + 2, left + 2, active);
                write_str(buf);
                sprintf(buf, "\033[%d;%dHRemote users will be notified that the node is going offline for", top + 4, left + 2);
                write_str(buf);
                sprintf(buf, "\033[%d;%dHa firmware upgrade and gracefully disconnected before rebooting.", top + 5, left + 2);
                write_str(buf);
            } else {
                sprintf(buf, "\033[%d;%dHEnter HalfKay bootloader? (no active connections)", top + 2, left + 2);
                write_str(buf);
            }
            sprintf(buf, "\033[%d;%dH%s WARNING: Node will stay OFFLINE in HalfKay bootloader mode \033[0m",
                    top + 7, left + 2, sgr_sel());
            write_str(buf);
            sprintf(buf, "\033[%d;%dHuntil you flash new firmware or power-cycle the Teensy 4.1 board.", top + 8, left + 2);
            write_str(buf);
            draw_dec_hdiv(top + 10, left, mr.w);
            OykTuiButton btns[2];
            btns[0].label = "Bootloader"; btns[0].enabled = 1;
            btns[1].label = "Cancel";     btns[1].enabled = 1;
            oyk_tui_draw_button_row(&b, mr.top + 11, mr.left + 2, mr.w - 4,
                                    btns, 2, 0, "Y:Bootloader  N/Esc:Cancel");
        }
    } else {
        oyk_u32 elapsed = (now_ms_ >= bootloader_start_ms_) ? (now_ms_ - bootloader_start_ms_) : 0UL;
        sprintf(buf, "\033[%d;%dHNotifying users & draining AX.25 disconnects (%d left, %lu/3000ms)...",
                top + 3, left + 2, disc_left, static_cast<unsigned long>(elapsed));
        write_str(buf);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_about() {
    OykTuiBackend b = make_tui_backend();
    oyk_tui_draw_about_modal(&b, term_rows_, term_cols_, "Platform: Teensy 4.1 / TinyTMUX");
}

static oyk_u8 tmux_cycle_lpol(oyk_u8 cur, int dir) {
    return static_cast<oyk_u8>((static_cast<int>(cur) + dir + 5) % 5);
}

int TinyTmuxConsole::wizard_step_field_count() const {
    if (wiz_step_ == 1) return 2;
    if (wiz_step_ == 2) {
        if (wiz_drv_ == 0) return 8;
        if (wiz_drv_ == 1 || wiz_drv_ == 4) return 7;
        if (wiz_drv_ == 6) return 3;
        return 6;
    }
    return 0;
}

int TinyTmuxConsole::edit_iface_field_count() const {
    if (wiz_drv_ == 0) return 11;
    if (wiz_drv_ == 1 || wiz_drv_ == 4) return 10;
    if (wiz_drv_ == 6) return 6;
    return 9;
}

OYK_FLASHMEM void TinyTmuxConsole::open_add_iface_wizard() {
    if (draft_.iface_count >= OYK_MAX_INTERFACES) {
        strcpy(dialog_fb_, "Maximum interface slots reached.");
        render();
        return;
    }
    prompt_kind_ = PROMPT_SETTINGS_ADD_WIZARD;
    wiz_edit_idx_ = -1;
    wiz_step_ = 0;
    wiz_drv_ = 0;
    wiz_last_drv_ = -1;
    wiz_focus_ = 0;
    wiz_btn_ = 1;
    wiz_err_[0] = '\0';
    wiz_id_[0] = '\0';
    wiz_desc_[0] = '\0';
    wiz_ep_[0] = '\0';
    wiz_port_[0] = '\0';
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_flags_ = 0x03U;
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::open_edit_iface_dialog(int edit_idx) {
    if (edit_idx < 0 || edit_idx >= draft_.iface_count) return;
    prompt_kind_ = PROMPT_SETTINGS_EDIT_IFACE;
    wiz_edit_idx_ = edit_idx;
    wiz_focus_ = 0;
    wiz_btn_ = 0; /* Save */
    wiz_err_[0] = '\0';

    const OykIfaceConfig& ic = draft_.ifaces[edit_idx];
    wiz_drv_ = 0;
    for (int i = 0; i < 7; ++i) {
        if (strieq_ci(ic.type, TINYTMUX_DRIVERS[i].type)) {
            wiz_drv_ = i;
            break;
        }
    }
    wiz_last_drv_ = wiz_drv_;
    con_str_copy(wiz_id_, sizeof(wiz_id_), ic.id);
    con_str_copy(wiz_desc_, sizeof(wiz_desc_), ic.description);
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_flags_ = static_cast<oyk_u8>(
        (ic.block_inbound ? 0U : 1U) |
        ((wiz_drv_ == 6 || ic.block_outbound) ? 0U : 2U) |
        ((static_cast<oyk_u8>(ic.learned_peer_policy) & 0x07U) << 2) |
        ((wiz_drv_ != 6 && ic.allow_same_interface) ? 0x20U : 0U));
    if (strieq_ci(ic.type, "ppp") || strieq_ci(ic.type, "kiss-serial") ||
        strieq_ci(ic.type, "usb-cdc-serial")) {
        con_str_copy(wiz_ep_, sizeof(wiz_ep_), ic.device[0] ? ic.device : "ser0");
        sprintf(wiz_port_, "%lu",
                static_cast<unsigned long>(ic.baud ? ic.baud : (strieq_ci(ic.type, "kiss-serial") ? 9600UL : 115200UL)));
    } else {
        con_str_copy(wiz_ep_, sizeof(wiz_ep_), ic.host[0] ? ic.host : "0.0.0.0");
        sprintf(wiz_port_, "%u", static_cast<unsigned int>(ic.port));
        con_str_copy(wiz_mask_, sizeof(wiz_mask_), ic.netmask);
        if (ic.peer_count > 0 && (wiz_drv_ == 0 || wiz_drv_ == 1)) {
            oyk_format_peer_spec(&ic.peers[0], wiz_peer_, sizeof(wiz_peer_));
        }
    }
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::wizard_apply_driver_defaults() {
    const char* t = TINYTMUX_DRIVERS[wiz_drv_].type;
    if (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD) {
        oyk_derive_default_iface_id(&draft_, t, wiz_id_, sizeof(wiz_id_));
        wiz_desc_[0] = '\0';
    }
    wiz_mask_[0] = '\0';
    wiz_peer_[0] = '\0';
    wiz_flags_ = static_cast<oyk_u8>(1U | ((wiz_drv_ == 6) ? 0U : 2U));
    if (strieq_ci(t, "usb-cdc-net")) {
        strcpy(wiz_ep_, "192.168.7.2");
        strcpy(wiz_port_, "10093");
    } else if (strieq_ci(t, "usb-cdc-serial")) {
        strcpy(wiz_ep_, "usbh0");
        strcpy(wiz_port_, "115200");
    } else if (strieq_ci(t, "ppp")) {
        strcpy(wiz_ep_, "ser0");
        strcpy(wiz_port_, "115200");
    } else if (strieq_ci(t, "kiss-serial")) {
        strcpy(wiz_ep_, "ser0");
        strcpy(wiz_port_, "9600");
    } else if (strieq_ci(t, "telnet")) {
        strcpy(wiz_ep_, "0.0.0.0");
        strcpy(wiz_port_, "8023");
    } else {
        strcpy(wiz_ep_, "0.0.0.0");
        strcpy(wiz_port_, strieq_ci(t, "axudp") ? "10093" : "0");
    }
    wiz_last_drv_ = wiz_drv_;
}

OYK_FLASHMEM void TinyTmuxConsole::commit_iface_fields(int slot) {
    if (slot < 0 || slot >= OYK_MAX_INTERFACES) return;
    int is_new = (slot >= draft_.iface_count) ? 1 : 0;
    if (is_new && draft_.iface_count >= OYK_MAX_INTERFACES) {
        strcpy(dialog_fb_, "Maximum interface slots reached.");
        return;
    }
    OykIfaceConfig old_ic = draft_.ifaces[slot];
    OykIfaceConfig& ic = draft_.ifaces[slot];
    memset(&ic, 0, sizeof(ic));
    con_str_copy(ic.id, sizeof(ic.id), wiz_id_);
    con_str_copy(ic.type, sizeof(ic.type), TINYTMUX_DRIVERS[wiz_drv_].type);
    con_str_copy(ic.description, sizeof(ic.description), wiz_desc_);
    ic.block_inbound = (wiz_flags_ & 1U) ? 0 : 1;
    ic.block_outbound = (wiz_drv_ == 6 || (wiz_flags_ & 2U) == 0U) ? 1 : 0;
    ic.allow_same_interface = (wiz_drv_ != 6 && (wiz_flags_ & 0x20U) != 0U) ? 1 : 0;
    ic.learned_peer_policy = (wiz_drv_ == 6) ? OYK_LEARN_INHERIT : static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    if (strieq_ci(ic.type, "usb-cdc-net")) {
        strcpy(ic.device, "usbh0");
        con_str_copy(ic.host, sizeof(ic.host), wiz_ep_[0] ? wiz_ep_ : "192.168.7.2");
        if (wiz_mask_[0]) con_str_copy(ic.netmask, sizeof(ic.netmask), wiz_mask_);
        long pv = strtol(wiz_port_, NULL, 10);
        ic.port = (pv > 0 && pv <= 65535L) ? static_cast<oyk_u16>(pv) : 10093U;
        ic.baud = 115200UL;
    } else if (strieq_ci(ic.type, "usb-cdc-serial")) {
        con_str_copy(ic.device, sizeof(ic.device), wiz_ep_[0] ? wiz_ep_ : "usbh0");
        strcpy(ic.host, "192.168.7.2");
        long bv = strtol(wiz_port_, NULL, 10);
        ic.baud = (bv >= 300L) ? static_cast<oyk_u32>(bv) : 115200UL;
        ic.port = 10093U;
    } else if (strieq_ci(ic.type, "ppp") || strieq_ci(ic.type, "kiss-serial")) {
        con_str_copy(ic.device, sizeof(ic.device), wiz_ep_[0] ? wiz_ep_ : "ser0");
        long bv = strtol(wiz_port_, NULL, 10);
        ic.baud = (bv >= 300L) ? static_cast<oyk_u32>(bv)
                               : (strieq_ci(ic.type, "ppp") ? 115200UL : 9600UL);
        if (strieq_ci(ic.type, "ppp")) ic.port = 10093U;
    } else {
        con_str_copy(ic.host, sizeof(ic.host), wiz_ep_[0] ? wiz_ep_ : "0.0.0.0");
        if ((wiz_drv_ == 0 || wiz_drv_ == 1) && wiz_mask_[0]) {
            con_str_copy(ic.netmask, sizeof(ic.netmask), wiz_mask_);
        }
        long pv = strtol(wiz_port_, NULL, 10);
        if (strieq_ci(ic.type, "telnet")) {
            ic.port = (pv > 0 && pv <= 65535L) ? static_cast<oyk_u16>(pv) : 8023U;
        } else if (strieq_ci(ic.type, "axudp")) {
            ic.port = (pv > 0 && pv <= 65535L) ? static_cast<oyk_u16>(pv) : 10093U;
        }
        if (strieq_ci(ic.type, "axudp") || strieq_ci(ic.type, "axip")) {
            if (!is_new && old_ic.peer_count > 1 &&
                (strieq_ci(old_ic.type, "axudp") || strieq_ci(old_ic.type, "axip"))) {
                for (int p = 1; p < old_ic.peer_count && ic.peer_count < OYK_MAX_PEERS; ++p) {
                    ic.peers[ic.peer_count++] = old_ic.peers[p];
                }
            }
            if (wiz_peer_[0]) {
                OykPeerEntry pe;
                memset(&pe, 0, sizeof(pe));
                if (oyk_parse_peer_spec(wiz_peer_, pe.callsign, sizeof(pe.callsign),
                                        &pe.ip, &pe.port, ic.port ? ic.port : 10093U)) {
                    if (ic.peer_count > 0 && !is_new) {
                        for (int p = (ic.peer_count < OYK_MAX_PEERS ? ic.peer_count : OYK_MAX_PEERS - 1); p > 0; --p) {
                            ic.peers[p] = ic.peers[p - 1];
                        }
                        ic.peers[0] = pe;
                        if (ic.peer_count < OYK_MAX_PEERS) ic.peer_count++;
                    } else {
                        ic.peers[0] = pe;
                        ic.peer_count = 1;
                    }
                }
            }
        }
    }
    settings_sel_[3] = slot;
    if (is_new) {
        draft_.iface_count++;
        sprintf(dialog_fb_, "Added %s (%s) to draft (press S/F2 to save).", ic.id, ic.type);
    } else {
        sprintf(dialog_fb_, "Updated %s (%s) in draft (press S/F2 to save).", ic.id, ic.type);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_add_iface_wizard() {
    static const char* PROG[4] = {
        "[##......] 1/4", "[####....] 2/4", "[######..] 3/4", "[########] 4/4"
    };
    char buf[192];
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 19, 68, &mr);
    const int top = mr.top + 1, left = mr.left + 1, h = mr.h, w = mr.w;
    draw_dec_box(top, left, h, w, "Add Interface", 1, 1);
    draw_dec_shadow(top, left, h, w);

    /* Top-right progress indicator left of [X] */
    sprintf(buf, "\033[%d;%dH%s %s \033[0m", top, left + w - 22, sgr_title(), PROG[wiz_step_]);
    write_str(buf);

    const char* drv = TINYTMUX_DRIVERS[wiz_drv_].type;
    const char* prompt_txt = "Select the driver for the new interface:";
    if (wiz_step_ == 1) {
        prompt_txt = "Enter a unique interface ID and optional description:";
    } else if (wiz_step_ == 2) {
        if (wiz_drv_ == 0) prompt_txt = "Configure AXUDP bind host/DMR, port, netmask, and policy:";
        else if (wiz_drv_ == 1) prompt_txt = "Configure AXIP bind host/DMR, netmask, and policy:";
        else if (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) prompt_txt = "Configure serial UART/USB device, baud rate, and policy:";
        else if (wiz_drv_ == 4) prompt_txt = "Configure USB-CDC host/DMR, port, netmask, and policy:";
        else prompt_txt = "Configure Telnet bind host IP, TCP port, and policy:";
    } else if (wiz_step_ == 3) {
        prompt_txt = "Review interface configuration before committing:";
    }
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 1, left + 2, sgr_title(), prompt_txt);
    write_str(buf);

    const int ain = (wiz_flags_ & 1U) ? 1 : 0;
    const int aout = (wiz_drv_ != 6 && (wiz_flags_ & 2U)) ? 1 : 0;
    const oyk_u8 lpol = static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    if (wiz_step_ == 0) {
        sprintf(buf, "\033[%d;%dH%s %-2s %-15s %-44s \033[0m",
                top + 2, left + 1, sgr_list_hdr(), "#", "Driver", "Description");
        write_str(buf);
        for (int i = 0; i < 7; ++i) {
            sprintf(buf, "\033[%d;%dH%s %d. %-15.15s %-44.44s \033[0m",
                    top + 3 + i, left + 1, (i == wiz_drv_) ? sgr_sel() : "",
                    i + 1, TINYTMUX_DRIVERS[i].type, TINYTMUX_DRIVERS[i].desc);
            write_str(buf);
        }
    } else if (wiz_step_ == 1) {
        sprintf(buf, "\033[%d;%dHDriver       : %s", top + 3, left + 3, drv);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHInterface ID : %s %-36.36s \033[0m",
                top + 5, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", wiz_id_);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHDescription  : %s %-36.36s \033[0m",
                top + 7, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", wiz_desc_);
        write_str(buf);
    } else if (wiz_step_ == 2) {
        const char* ep_lbl = (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                 ? "Serial Device  "
                                 : (wiz_drv_ == 4 ? "Host / DMR ID  " : "Bind Host/DMR  ");
        sprintf(buf, "\033[%d;%dHInterface    : %s (%s)", top + 2, left + 3, wiz_id_, drv);
        write_str(buf);
        int r = top + 4;
        int fi = 0;
        sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                r, left + 3, ep_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_ep_);
        write_str(buf);
        ++r; ++fi;
        if (wiz_drv_ != 1) {
            const char* p_lbl = (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                    ? "Baud Rate      "
                                    : (wiz_drv_ == 6 ? "TCP Port       " : "UDP Port       ");
            sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                    r, left + 3, p_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_port_);
            write_str(buf);
            ++r; ++fi;
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) {
            sprintf(buf, "\033[%d;%dHNetmask (opt)  : %s %-36.36s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_mask_);
            write_str(buf);
            ++r; ++fi;
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1) {
            sprintf(buf, "\033[%d;%dHPeer (opt)     : %s %-36.36s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_peer_);
            write_str(buf);
            ++r; ++fi;
        }
        sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-12.12s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", ain ? "[ON]" : "[OFF]");
        write_str(buf);
        ++r; ++fi;
        if (wiz_drv_ == 6) {
            sprintf(buf, "\033[%d;%dHAllow Outbound : [OFF] (disabled for telnet)", r, left + 3);
            write_str(buf);
            ++r;
            sprintf(buf, "\033[%d;%dHSame-Iface Fwd : [OFF] (disabled for telnet)", r, left + 3);
            write_str(buf);
        } else {
            const int asame = (wiz_flags_ & 0x20U) ? 1 : 0;
            sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-12.12s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", aout ? "[ON]" : "[OFF]");
            write_str(buf);
            ++r; ++fi;
            sprintf(buf, "\033[%d;%dHSame-Iface Fwd : %s %-12.12s \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", asame ? "[ON]" : "[OFF]");
            write_str(buf);
            ++r; ++fi;
            sprintf(buf, "\033[%d;%dHLearned Policy : %s < %s > \033[0m",
                    r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", oyk_format_learned_policy(lpol));
            write_str(buf);
        }
    } else {
        sprintf(buf, "\033[%d;%dHDriver       : %-46.46s", top + 3, left + 3, drv);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHInterface ID : %-46.46s", top + 4, left + 3, wiz_id_);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHDescription  : %-46.46s", top + 5, left + 3,
                wiz_desc_[0] ? wiz_desc_ : "(none)");
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%s: %-46.46s", top + 6, left + 3,
                (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) ? "Device       " : "Host / DMR   ",
                wiz_ep_);
        write_str(buf);
        if (wiz_drv_ != 1) {
            sprintf(buf, "\033[%d;%dH%s: %-46.46s", top + 7, left + 3,
                    (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) ? "Baud Rate    " : "Port         ",
                    wiz_port_);
            write_str(buf);
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) {
            sprintf(buf, "\033[%d;%dHNetmask      : %-46.46s", top + 8, left + 3,
                    wiz_mask_[0] ? wiz_mask_ : "(any)");
            write_str(buf);
        }
        if (wiz_drv_ == 0 || wiz_drv_ == 1) {
            sprintf(buf, "\033[%d;%dHStatic Peer  : %-46.46s", top + 9, left + 3,
                    wiz_peer_[0] ? wiz_peer_ : "(auto-learn)");
            write_str(buf);
        }
        sprintf(buf, "\033[%d;%dHPolicy       : In=%s  Out=%s  SameIf=%s  Learn=%s",
                top + 10, left + 3,
                ain ? "ON" : "OFF",
                (wiz_drv_ == 6) ? "OFF" : (aout ? "ON" : "OFF"),
                (wiz_drv_ == 6) ? "OFF" : ((wiz_flags_ & 0x20U) ? "ON" : "OFF"),
                (wiz_drv_ == 6) ? "n/a" : oyk_format_learned_policy(lpol));
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%sPress Finish to add this interface to your Settings draft.\033[0m",
                top + 12, left + 3, sgr_title());
        write_str(buf);
    }

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 15, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }

    draw_dec_hdiv(top + 16, left, w);

    int fcnt = wizard_step_field_count();
    int on_bar = (wiz_step_ == 0 || wiz_step_ == 3 || wiz_focus_ == fcnt);
    OykTuiBackend b = make_tui_backend();
    OykTuiButton btns[3];
    btns[0].label = "< Back";
    btns[0].enabled = (wiz_step_ > 0) ? 1 : 0;
    btns[1].label = (wiz_step_ == 3) ? "Finish" : "Next >";
    btns[1].enabled = 1;
    btns[2].label = "Cancel";
    btns[2].enabled = 1;
    oyk_tui_draw_button_row(&b, mr.top + 17, mr.left + 2, mr.w - 4,
                            btns, 3, on_bar ? wiz_btn_ : -1,
                            (wiz_step_ > 0) ? "Ctrl+B:Back" : "1-7:Pick");
}

OYK_FLASHMEM void TinyTmuxConsole::render_edit_iface_dialog() {
    char buf[192];
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 19, 68, &mr);
    const int top = mr.top + 1, left = mr.left + 1, h = mr.h, w = mr.w;
    draw_dec_box(top, left, h, w, "Edit Interface", 1, 1);
    draw_dec_shadow(top, left, h, w);

    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m",
            top + 1, left + 2, sgr_title(), "Edit interface settings and select Save to update draft:");
    write_str(buf);

    const int ain = (wiz_flags_ & 1U) ? 1 : 0;
    const int aout = (wiz_drv_ != 6 && (wiz_flags_ & 2U)) ? 1 : 0;
    const oyk_u8 lpol = static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U);

    char drv_lbl[32];
    sprintf(drv_lbl, "< %s >", TINYTMUX_DRIVERS[wiz_drv_].type);
    sprintf(buf, "\033[%d;%dHDriver (Lt/Rt) : %s %-36.36s \033[0m",
            top + 3, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", drv_lbl);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface ID   : %s %-36.36s \033[0m",
            top + 4, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHDescription    : %s %-36.36s \033[0m",
            top + 5, left + 3, (wiz_focus_ == 2) ? sgr_menubar() : "", wiz_desc_);
    write_str(buf);

    const int is_ser = (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5) ? 1 : 0;
    const char* ep_lbl = is_ser ? "Serial Device  "
                                : (wiz_drv_ == 4 ? "Host / DMR ID  " : "Bind Host/DMR  ");
    sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
            top + 6, left + 3, ep_lbl, (wiz_focus_ == 3) ? sgr_menubar() : "", wiz_ep_);
    write_str(buf);

    int r = top + 7;
    int fi = 4;
    if (wiz_drv_ != 1) {
        const char* p_lbl = is_ser ? "Baud Rate      "
                                   : (wiz_drv_ == 6 ? "TCP Port       " : "UDP Port       ");
        sprintf(buf, "\033[%d;%dH%s: %s %-36.36s \033[0m",
                r, left + 3, p_lbl, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_port_);
        write_str(buf);
        ++r; ++fi;
    }
    if (wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) {
        sprintf(buf, "\033[%d;%dHNetmask (opt)  : %s %-36.36s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_mask_);
        write_str(buf);
        ++r; ++fi;
    }
    if (wiz_drv_ == 0 || wiz_drv_ == 1) {
        sprintf(buf, "\033[%d;%dHStatic Peer    : %s %-36.36s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", wiz_peer_);
        write_str(buf);
        ++r; ++fi;
    }
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-12.12s \033[0m",
            r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", ain ? "[ON]" : "[OFF]");
    write_str(buf);
    ++r; ++fi;
    if (wiz_drv_ == 6) {
        sprintf(buf, "\033[%d;%dHAllow Outbound : [OFF] (disabled for telnet)", r, left + 3);
        write_str(buf);
        ++r;
        sprintf(buf, "\033[%d;%dHSame-Iface Fwd : [OFF] (disabled for telnet)", r, left + 3);
        write_str(buf);
    } else {
        const int asame = (wiz_flags_ & 0x20U) ? 1 : 0;
        sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-12.12s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", aout ? "[ON]" : "[OFF]");
        write_str(buf);
        ++r; ++fi;
        sprintf(buf, "\033[%d;%dHSame-Iface Fwd : %s %-12.12s \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", asame ? "[ON]" : "[OFF]");
        write_str(buf);
        ++r; ++fi;
        sprintf(buf, "\033[%d;%dHLearned Policy : %s < %s > \033[0m",
                r, left + 3, (wiz_focus_ == fi) ? sgr_menubar() : "", oyk_format_learned_policy(lpol));
        write_str(buf);
    }

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 15, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }

    draw_dec_hdiv(top + 16, left, w);

    int fcnt = edit_iface_field_count();
    int on_bar = (wiz_focus_ == fcnt);
    OykTuiBackend b = make_tui_backend();
    OykTuiButton btns[2];
    btns[0].label = " Save ";
    btns[0].enabled = 1;
    btns[1].label = "Cancel";
    btns[1].enabled = 1;
    oyk_tui_draw_button_row(&b, mr.top + 17, mr.left + 2, mr.w - 4,
                            btns, 2, on_bar ? wiz_btn_ : -1,
                            "Up/Dn:Field  Enter:Save");
}

OYK_FLASHMEM void TinyTmuxConsole::render_confirm_format_modal() {
    static const char* NAMES[3] = {
        "microSD Card (SDIO)", "QSPI Flash (FlexSPI2 CS1)", "Internal 4 MB Flash (LittleFS)"
    };
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 12, 68, &mr);
    const int top = mr.top + 1, left = mr.left + 1, h = mr.h, w = mr.w;
    const char* target = (storage_sel_ >= 0 && storage_sel_ < 3) ? NAMES[storage_sel_] : "Storage";
    char buf[160];
    draw_dec_box(top, left, h, w, "CONFIRM DESTRUCTIVE FORMAT", 1, 1);
    draw_dec_shadow(top, left, h, w);
    sprintf(buf, "\033[%d;%dHTarget : %-56.56s", top + 2, left + 2, target);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s WARNING: Partitioning/formatting ERASES ALL DATA on this tier, \033[0m",
            top + 4, left + 2, sgr_sel());
    write_str(buf);
    sprintf(buf, "\033[%d;%dHincluding configs, mail, files, and logs!", top + 5, left + 2);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHType FORMAT (uppercase) and press Enter to confirm:", top + 7, left + 2);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s > %-60.60s \033[0m", top + 8, left + 2, sgr_menubar(), prompt_buf_);
    write_str(buf);
    draw_dec_hdiv(top + 9, left, w);
    sprintf(buf, "\033[%d;%dH%s [Enter] Confirm Format                 [Esc] Cancel          \033[0m",
            top + 10, left + 1, sgr_menubar());
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::open_edit_peer_dialog(int edit_idx) {
    prompt_kind_ = PROMPT_SETTINGS_EDIT_PEER;
    wiz_edit_idx_ = edit_idx;
    wiz_focus_ = (edit_idx >= 0) ? 0 : 1;
    wiz_btn_ = 0; /* Save */
    wiz_err_[0] = '\0';
    if (edit_idx >= 0 && edit_idx < draft_.peer_count) {
        const OykPeerConfig& pc = draft_.peers[edit_idx];
        con_str_copy(wiz_id_, sizeof(wiz_id_), pc.id);
        con_str_copy(ban_call_tmp_, sizeof(ban_call_tmp_), pc.callsign);
        con_str_copy(wiz_desc_, sizeof(wiz_desc_), pc.description);
        con_str_copy(ban_action_tmp_, sizeof(ban_action_tmp_), pc.interface_id);
        con_str_copy(wiz_ep_, sizeof(wiz_ep_), pc.host);
        if (pc.port > 0) sprintf(wiz_port_, "%u", static_cast<unsigned int>(pc.port));
        else wiz_port_[0] = '\0';
        wiz_step_ = (pc.allow_inbound ? 1 : 0) | (pc.allow_outbound ? 2 : 0);
        wiz_last_drv_ = 1; /* id_custom */
    } else {
        wiz_id_[0] = '\0';
        ban_call_tmp_[0] = '\0';
        wiz_desc_[0] = '\0';
        ban_action_tmp_[0] = '\0';
        for (int i = 0; i < draft_.iface_count; ++i) {
            if (!strieq_ci(draft_.ifaces[i].type, "telnet") &&
                !strieq_ci(draft_.ifaces[i].type, "local")) {
                con_str_copy(ban_action_tmp_, sizeof(ban_action_tmp_), draft_.ifaces[i].id);
                break;
            }
        }
        wiz_ep_[0] = '\0';
        wiz_port_[0] = '\0';
        wiz_step_ = 3; /* allow_in | allow_out */
        wiz_last_drv_ = 0;
    }
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::render_edit_peer_dialog() {
    char buf[192];
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 16, 68, &mr);
    const int top = mr.top + 1, left = mr.left + 1, h = mr.h, w = mr.w;
    draw_dec_box(top, left, h, w, (wiz_edit_idx_ >= 0) ? "Edit Peer" : "Add Peer", 1, 1);
    draw_dec_shadow(top, left, h, w);

    char if_lbl[32], in_lbl[8], out_lbl[8];
    sprintf(if_lbl, "< %s >", ban_action_tmp_[0] ? ban_action_tmp_ : "(any)");
    strcpy(in_lbl, (wiz_step_ & 1) ? "[ON]" : "[OFF]");
    strcpy(out_lbl, (wiz_step_ & 2) ? "[ON]" : "[OFF]");

    sprintf(buf, "\033[%d;%dHPeer ID        : %s %-36.36s \033[0m", top + 2, left + 3, (wiz_focus_ == 0) ? sgr_menubar() : "", wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHCallsign       : %s %-36.36s \033[0m", top + 3, left + 3, (wiz_focus_ == 1) ? sgr_menubar() : "", ban_call_tmp_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHDescription    : %s %-36.36s \033[0m", top + 4, left + 3, (wiz_focus_ == 2) ? sgr_menubar() : "", wiz_desc_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface      : %s %-36.36s \033[0m", top + 5, left + 3, (wiz_focus_ == 3) ? sgr_menubar() : "", if_lbl);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHHost / DMR ID  : %s %-36.36s \033[0m", top + 6, left + 3, (wiz_focus_ == 4) ? sgr_menubar() : "", wiz_ep_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHPort (0=def)   : %s %-36.36s \033[0m", top + 7, left + 3, (wiz_focus_ == 5) ? sgr_menubar() : "", wiz_port_[0] ? wiz_port_ : "(default)");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s %-10.10s \033[0m", top + 8, left + 3, (wiz_focus_ == 6) ? sgr_menubar() : "", in_lbl);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Outbound : %s %-10.10s \033[0m", top + 9, left + 3, (wiz_focus_ == 7) ? sgr_menubar() : "", out_lbl);
    write_str(buf);

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 11, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }

    draw_dec_hdiv(top + 13, left, w);
    int on_bar = (wiz_focus_ == 8);
    OykTuiBackend b = make_tui_backend();
    OykTuiButton btns[2];
    btns[0].label = " Save ";
    btns[0].enabled = 1;
    btns[1].label = "Cancel";
    btns[1].enabled = 1;
    oyk_tui_draw_button_row(&b, mr.top + 14, mr.left + 2, mr.w - 4,
                            btns, 2, on_bar ? wiz_btn_ : -1,
                            "Up/Dn:Field  Lt/Rt/Spc:Toggle");
}

OYK_FLASHMEM void TinyTmuxConsole::commit_peer_fields(int slot) {
    if (slot < 0 || slot >= OYK_MAX_CONFIG_PEERS) return;
    int is_new = (slot >= draft_.peer_count) ? 1 : 0;
    if (is_new && draft_.peer_count >= OYK_MAX_CONFIG_PEERS) {
        strcpy(dialog_fb_, "Maximum peer slots reached.");
        return;
    }
    OykCallsign cs;
    oyk_parse_callsign(ban_call_tmp_, &cs);
    char cfmt[16];
    oyk_format_callsign(&cs, cfmt, sizeof(cfmt));
    OykPeerConfig& pc = draft_.peers[slot];
    memset(&pc, 0, sizeof(pc));
    con_str_copy(pc.id, sizeof(pc.id), wiz_id_);
    con_str_copy(pc.callsign, sizeof(pc.callsign), cfmt);
    con_str_copy(pc.description, sizeof(pc.description), wiz_desc_);
    con_str_copy(pc.interface_id, sizeof(pc.interface_id), ban_action_tmp_);
    con_str_copy(pc.host, sizeof(pc.host), wiz_ep_);
    pc.port = wiz_port_[0] ? static_cast<oyk_u16>(strtoul(wiz_port_, NULL, 10)) : 0;
    pc.allow_inbound = (wiz_step_ & 1) ? 1 : 0;
    pc.allow_outbound = (wiz_step_ & 2) ? 1 : 0;
    pc.auto_learned = 0;
    if (is_new) {
        draft_.peer_count++;
        settings_sel_[4] = slot;
        sprintf(dialog_fb_, "Added peer %s in draft (press S/F2 to save).", pc.id);
    } else {
        sprintf(dialog_fb_, "Updated peer %s in draft (press S/F2 to save).", pc.id);
    }
}

OYK_FLASHMEM int TinyTmuxConsole::edit_peer_try_save() {
    wiz_err_[0] = '\0';
    OykCallsign cs;
    if (!oyk_parse_callsign(ban_call_tmp_, &cs)) {
        strcpy(wiz_err_, "Invalid AX.25 callsign.");
        wiz_focus_ = 1;
        return 0;
    }
    char cfmt[16];
    oyk_format_callsign(&cs, cfmt, sizeof(cfmt));
    if (!wiz_id_[0]) {
        oyk_derive_default_peer_id(&draft_, cfmt, wiz_id_, sizeof(wiz_id_));
    }
    for (int i = 0; i < draft_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        if (i == wiz_edit_idx_) continue;
        if (strieq_ci(draft_.peers[i].id, wiz_id_)) {
            sprintf(wiz_err_, "Duplicate peer ID: %.24s", wiz_id_);
            wiz_focus_ = 0;
            return 0;
        }
    }
    if (wiz_port_[0]) {
        long p = strtol(wiz_port_, NULL, 10);
        if (p < 0L || p > 65535L) {
            strcpy(wiz_err_, "Port must be 0..65535.");
            wiz_focus_ = 5;
            return 0;
        }
    }
    if (wiz_ep_[0] && oyk_is_all_digits(wiz_ep_)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(wiz_ep_, &did)) {
            strcpy(wiz_err_, "Invalid DMR ID (allowed 1..16777214).");
            wiz_focus_ = 4;
            return 0;
        }
    }
    int slot = (wiz_edit_idx_ >= 0) ? wiz_edit_idx_ : draft_.peer_count;
    commit_peer_fields(slot);
    prompt_kind_ = PROMPT_NONE;
    return 1;
}

OYK_FLASHMEM void TinyTmuxConsole::handle_edit_peer_key(int key) {
    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        strcpy(dialog_fb_, "Edit peer cancelled.");
        render();
        return;
    }
    if (key == TMUX_KEY_UP) {
        wiz_focus_ = (wiz_focus_ + 8) % 9;
        render();
        return;
    }
    if (key == TMUX_KEY_DOWN || key == '\t') {
        wiz_focus_ = (wiz_focus_ + 1) % 9;
        render();
        return;
    }
    if (wiz_focus_ == 8) {
        if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
            wiz_btn_ = 1 - wiz_btn_;
        } else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 1) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Edit peer cancelled.");
            } else {
                edit_peer_try_save();
            }
        }
        render();
        return;
    }
    if (wiz_focus_ == 3) {
        if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT || key == ' ') {
            const char* opts[OYK_MAX_INTERFACES];
            int oc = 0;
            for (int i = 0; i < draft_.iface_count && oc < OYK_MAX_INTERFACES; ++i) {
                if (!strieq_ci(draft_.ifaces[i].type, "telnet") &&
                    !strieq_ci(draft_.ifaces[i].type, "local")) {
                    opts[oc++] = draft_.ifaces[i].id;
                }
            }
            if (oc > 0) {
                int idx = 0;
                for (int i = 0; i < oc; ++i) {
                    if (strieq_ci(opts[i], ban_action_tmp_)) { idx = i; break; }
                }
                idx = (key == TMUX_KEY_LEFT) ? (idx + oc - 1) % oc : (idx + 1) % oc;
                con_str_copy(ban_action_tmp_, sizeof(ban_action_tmp_), opts[idx]);
            }
            render();
        } else if (key == '\r' || key == '\n') {
            edit_peer_try_save();
            render();
        }
        return;
    }
    if (wiz_focus_ == 6 || wiz_focus_ == 7) {
        if (key == ' ' || key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
            wiz_step_ ^= (wiz_focus_ == 6) ? 1 : 2;
            render();
        } else if (key == '\r' || key == '\n') {
            edit_peer_try_save();
            render();
        }
        return;
    }
    char* target = NULL;
    size_t cap = 0;
    if (wiz_focus_ == 0) { target = wiz_id_; cap = sizeof(wiz_id_); }
    else if (wiz_focus_ == 1) { target = ban_call_tmp_; cap = sizeof(ban_call_tmp_); }
    else if (wiz_focus_ == 2) { target = wiz_desc_; cap = sizeof(wiz_desc_); }
    else if (wiz_focus_ == 4) { target = wiz_ep_; cap = sizeof(wiz_ep_); }
    else if (wiz_focus_ == 5) { target = wiz_port_; cap = sizeof(wiz_port_); }

    if (key == '\r' || key == '\n') {
        edit_peer_try_save();
        render();
    } else if (target && (key == 8 || key == 127)) {
        size_t n = strlen(target);
        if (n > 0) target[n - 1] = '\0';
        if (wiz_focus_ == 0) wiz_last_drv_ = (wiz_id_[0] != '\0');
        if (wiz_focus_ == 1 && !wiz_last_drv_) {
            if (ban_call_tmp_[0]) oyk_derive_default_peer_id(&draft_, ban_call_tmp_, wiz_id_, sizeof(wiz_id_));
            else wiz_id_[0] = '\0';
        }
        wiz_err_[0] = '\0';
        render();
    } else if (target && key >= 0x20 && key < 0x7F) {
        size_t n = strlen(target);
        if (n + 1 < cap) {
            target[n] = static_cast<char>(key);
            target[n + 1] = '\0';
        }
        if (wiz_focus_ == 0) wiz_last_drv_ = 1;
        if (wiz_focus_ == 1 && !wiz_last_drv_) {
            oyk_derive_default_peer_id(&draft_, ban_call_tmp_, wiz_id_, sizeof(wiz_id_));
        }
        wiz_err_[0] = '\0';
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::open_terminate_dialog() {
    if (!node_ || node_->total_active_connections() == 0) {
        strcpy(status_line_, "No active connections to terminate.");
        render();
        return;
    }
    prompt_kind_ = PROMPT_TERMINATE_SID;
    wiz_edit_idx_ = 0;
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::render_terminate_dialog() {
    char buf[192];
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 16, 64, &mr);
    const int top = mr.top + 1, left = mr.left + 1, h = mr.h, w = mr.w;
    draw_dec_box(top, left, h, w, "Terminate Active Connection", 1, 1);
    draw_dec_shadow(top, left, h, w);
    sprintf(buf, "\033[%d;%dH%s %-5s %-11s %-10s %-30s \033[0m",
            top + 2, left + 2, sgr_list_hdr(), "ID", "Callsign", "Port", "State");
    write_str(buf);

    int row = 0;
    const int list_h = h - 6;
    for (int i = 0; i < node_->max_sessions() && row < list_h; ++i) {
        const OykSession& s = node_->session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[16], id_s[16];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(id_s, "#%lu", static_cast<unsigned long>(s.id));
            const char* ifid = (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local";
            sprintf(buf, "\033[%d;%dH%s %-5s %-11.11s %-10.10s %-30.30s \033[0m",
                    top + 3 + row, left + 2,
                    (row == wiz_edit_idx_) ? sgr_sel() : "",
                    id_s, cs, ifid, "[CONNECTED]");
            write_str(buf);
            ++row;
        }
    }
    if (row == 0) {
        sprintf(buf, "\033[%d;%dH(no active connections)", top + 4, left + 4);
        write_str(buf);
    }
    draw_dec_hdiv(top + h - 3, left, w);
    sprintf(buf, "\033[%d;%dHUp/Down: select | Enter: terminate | Esc: cancel",
            top + h - 2, left + 2);
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::handle_terminate_key(int key) {
    oyk_u32 sids[OYK_MAX_SESSIONS];
    int scnt = 0;
    for (int i = 0; i < node_->max_sessions() && scnt < OYK_MAX_SESSIONS; ++i) {
        if (node_->session_slot(i).state == OYK_SESS_CONNECTED) {
            sids[scnt++] = node_->session_slot(i).id;
        }
    }
    if (scnt == 0 || key == 27) {
        if (scnt == 0) strcpy(status_line_, "All active connections have closed.");
        prompt_kind_ = PROMPT_NONE;
        render();
        return;
    }
    if (wiz_edit_idx_ >= scnt) wiz_edit_idx_ = scnt - 1;
    if (key == TMUX_KEY_UP && wiz_edit_idx_ > 0) {
        --wiz_edit_idx_;
        render();
    } else if (key == TMUX_KEY_DOWN && wiz_edit_idx_ + 1 < scnt) {
        ++wiz_edit_idx_;
        render();
    } else if (key == '\r' || key == '\n') {
        char msg[96];
        node_->terminate_session(sids[wiz_edit_idx_], msg, sizeof(msg));
        con_str_copy(status_line_, sizeof(status_line_), msg);
        prompt_kind_ = PROMPT_NONE;
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::open_ban_dialog() {
    prompt_kind_ = PROMPT_BAN_CALLSIGN;
    ban_call_tmp_[0] = '\0';
    strcpy(wiz_id_, "permanent");
    strcpy(wiz_desc_, "Banned by sysop.");
    wiz_flags_ = 0x02U; /* Appeal Allowed default on, Hard Ban default off */
    wiz_focus_ = 0;
    wiz_edit_idx_ = 0;
    wiz_btn_ = 0; /* confirm unban inactive */
    wiz_err_[0] = '\0';
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::render_ban_dialog() {
    char buf[192];
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 21, 74, &mr);
    const int top = mr.top + 1, left = mr.left + 1, h = mr.h, w = mr.w;
    draw_dec_box(top, left, h, w, "Ban / Unban Callsign (banlist.cfg)", 1, 1);
    draw_dec_shadow(top, left, h, w);

    sprintf(buf, "\033[%d;%dHCallsign: %s %-12.12s \033[0m",
            top + 2, left + 2, (wiz_focus_ == 0 && !wiz_btn_) ? sgr_sel() : sgr_menubar(), ban_call_tmp_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHLift At : %s %-15.15s \033[0m  (permanent, 30m, 12h, 7d, YYYY-MM-DD)",
            top + 3, left + 2, (wiz_focus_ == 1 && !wiz_btn_) ? sgr_sel() : sgr_menubar(), wiz_id_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHReason  : %s %-56.56s \033[0m",
            top + 4, left + 2, (wiz_focus_ == 2 && !wiz_btn_) ? sgr_sel() : sgr_menubar(), wiz_desc_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%sOptions : [%c] Hard Ban (H)   [%c] Appeal Allowed (A)     \033[0m",
            top + 5, left + 2,
            (wiz_focus_ == 3 && !wiz_btn_) ? sgr_sel() : "",
            (wiz_flags_ & 1U) ? 'X' : ' ',
            (wiz_flags_ & 2U) ? 'X' : ' ');
    write_str(buf);

    int bcnt = node_->ban_count();
    sprintf(buf, "\033[%d;%dHActive Bans (%d):", top + 7, left + 2, bcnt);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s %-10s %-5s %-16s %-34s \033[0m",
            top + 8, left + 2, sgr_list_hdr(), "Callsign", "Type", "Lift At", "Reason");
    write_str(buf);

    const int list_h = 7;
    if (bcnt == 0) {
        sprintf(buf, "\033[%d;%dH(none)", top + 9, left + 4);
        write_str(buf);
    } else {
        int off = (wiz_edit_idx_ >= list_h) ? (wiz_edit_idx_ - list_h + 1) : 0;
        for (int i = 0; i < list_h && (off + i) < bcnt; ++i) {
            const OykBanEntry& e = node_->ban_entry(off + i);
            int hi = (wiz_focus_ == 4 && !wiz_btn_ && (off + i) == wiz_edit_idx_);
            sprintf(buf, "\033[%d;%dH%s %-10.10s %-5s %-16.16s %-34.34s \033[0m",
                    top + 9 + i, left + 2, hi ? sgr_sel() : "",
                    e.key, (e.flags & OYK_BAN_HARD_BAN) ? "HARD" : "SOFT",
                    e.lift_at, e.reason);
            write_str(buf);
        }
    }

    if (wiz_err_[0]) {
        sprintf(buf, "\033[%d;%dH%s %-68.68s \033[0m", top + h - 3, left + 2, sgr_warn(), wiz_err_);
        write_str(buf);
    }
    draw_dec_hdiv(top + h - 2, left, w);
    const char* help = (wiz_focus_ == 4)
                           ? "Up/Down: pick | Enter: UNBAN (confirm) | Tab: next | Esc: close"
                           : ((wiz_focus_ == 3)
                                  ? "H: hard/soft | A: appeal | Enter: BAN | Tab: next | Esc: close"
                                  : "Enter: BAN | Tab: next field | Esc: close");
    sprintf(buf, "\033[%d;%dH%-68.68s", top + h - 1, left + 2, help);
    write_str(buf);

    if (wiz_btn_ && bcnt > 0 && wiz_edit_idx_ >= 0 && wiz_edit_idx_ < bcnt) {
        OykTuiRect cmr;
        oyk_tui_center_modal(term_rows_, term_cols_, 6, 46, &cmr);
        draw_dec_box(cmr.top + 1, cmr.left + 1, cmr.h, cmr.w, "Confirm Unban", 1, 1);
        draw_dec_shadow(cmr.top + 1, cmr.left + 1, cmr.h, cmr.w);
        sprintf(buf, "\033[%d;%dHUnban callsign %-24.24s",
                cmr.top + 3, cmr.left + 3, node_->ban_entry(wiz_edit_idx_).key);
        write_str(buf);
        sprintf(buf, "\033[%d;%dH[Y] Yes   [N] No", cmr.top + 4, cmr.left + 3);
        write_str(buf);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_ban_key(int key) {
    int bcnt = node_->ban_count();
    if (bcnt == 0) {
        if (wiz_focus_ == 4) wiz_focus_ = 0;
        wiz_edit_idx_ = 0;
        wiz_btn_ = 0;
    } else if (wiz_edit_idx_ >= bcnt) {
        wiz_edit_idx_ = bcnt - 1;
    }

    if (wiz_btn_) {
        if (key == 'y' || key == 'Y') {
            char msg[96];
            node_->unban_callsign(node_->ban_entry(wiz_edit_idx_).key, msg, sizeof(msg));
            con_str_copy(wiz_err_, sizeof(wiz_err_), msg);
            con_str_copy(status_line_, sizeof(status_line_), msg);
            wiz_btn_ = 0;
        } else if (key == 'n' || key == 'N' || key == 27) {
            wiz_btn_ = 0;
        }
        render();
        return;
    }

    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        render();
        return;
    }
    if (key == '\t') {
        wiz_focus_ = (wiz_focus_ + 1) % (bcnt > 0 ? 5 : 4);
        render();
        return;
    }
    if (wiz_focus_ == 4) {
        if (key == TMUX_KEY_UP && wiz_edit_idx_ > 0) {
            --wiz_edit_idx_;
            render();
        } else if (key == TMUX_KEY_DOWN && wiz_edit_idx_ + 1 < bcnt) {
            ++wiz_edit_idx_;
            render();
        } else if (key == '\r' || key == '\n') {
            wiz_btn_ = 1;
            render();
        }
        return;
    }

    if (key == '\r' || key == '\n') {
        if (!ban_call_tmp_[0]) {
            strcpy(wiz_err_, "Enter a callsign first.");
        } else {
            oyk_u32 flags = ((wiz_flags_ & 1U) ? OYK_BAN_HARD_BAN : 0UL) |
                            ((wiz_flags_ & 2U) ? OYK_BAN_APPEAL_ALLOWED : 0UL);
            char msg[96];
            if (node_->ban_callsign(ban_call_tmp_, flags,
                                    wiz_desc_[0] ? wiz_desc_ : "Banned by sysop.",
                                    wiz_id_[0] ? wiz_id_ : "permanent", msg, sizeof(msg))) {
                ban_call_tmp_[0] = '\0';
                strcpy(wiz_id_, "permanent");
                con_str_copy(wiz_err_, sizeof(wiz_err_), msg);
                con_str_copy(status_line_, sizeof(status_line_), msg);
            } else {
                sprintf(wiz_err_, "Error: %.46s", msg);
            }
        }
        render();
        return;
    }

    if (wiz_focus_ == 3) {
        if (key == 'h' || key == 'H') wiz_flags_ ^= 1U;
        else if (key == 'a' || key == 'A') wiz_flags_ ^= 2U;
        render();
        return;
    }

    char* target = (wiz_focus_ == 0) ? ban_call_tmp_ : ((wiz_focus_ == 1) ? wiz_id_ : wiz_desc_);
    size_t cap = (wiz_focus_ == 0) ? 12U : ((wiz_focus_ == 1) ? sizeof(wiz_id_) : sizeof(wiz_desc_));
    if (key == 8 || key == 127) {
        size_t n = strlen(target);
        if (n > 0) target[n - 1] = '\0';
        render();
    } else if (key >= 0x20 && key < 0x7F) {
        size_t n = strlen(target);
        if (n + 1 < cap) {
            char ch = static_cast<char>(key);
            if (wiz_focus_ == 0 && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32);
            target[n] = ch;
            target[n + 1] = '\0';
        }
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::render_prompt_overlay() {
    char buf[192];
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 6, 68, &mr);
    const int top = mr.top + 1, left = mr.left + 1, h = mr.h, w = mr.w;
    draw_dec_box(top, left, h, w, "Input", 1, 1);
    draw_dec_shadow(top, left, h, w);
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + 1, left + 2, sgr_title(), prompt_label_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH%s > %-60.60s \033[0m", top + 2, left + 2, sgr_menubar(), prompt_buf_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH[Enter] Accept                              [Esc] Cancel", top + 4, left + 2);
    write_str(buf);
}

OYK_FLASHMEM void TinyTmuxConsole::open_settings(int initial_tab) {
    if (node_) draft_ = node_->config();
    settings_tab_ = (initial_tab >= 0 && initial_tab < 8) ? initial_tab : 0;
    theme_scroll_ = 0;
    theme_preview_dirty_ = 0;
    dialog_fb_[0] = '\0';
    view_ = TMUX_VIEW_SETTINGS;
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::save_and_apply_settings() {
    if (!node_) return;
    OykConfigApplyResult res;
    node_->apply_config(&draft_, 1, &res);
    node_->set_storage_is_flash(tinytmux_fs_is_flash_storage());
    if (bridge_) {
        bridge_->rebind_interfaces(now_ms_);
    }
    if (!res.applied) {
        sprintf(dialog_fb_, "Apply failed: %.60s", res.error);
        render();
        return;
    }
    theme_preview_dirty_ = 0;
    if (res.saved_to_disk) {
        if (res.identity_restarted) {
            sprintf(status_line_, "Settings saved & applied (signed off %d session(s)).",
                    res.signed_off_sessions);
        } else {
            strcpy(status_line_, "Settings saved to disk and applied.");
        }
    } else {
        strcpy(status_line_, "Settings applied IN MEMORY ONLY (config not writable).");
    }
    view_ = TMUX_VIEW_CONSOLE;
    render();
}

void TinyTmuxConsole::begin_prompt(PromptKind kind, const char* label, const char* prefill) {
    prompt_kind_ = kind;
    con_str_copy(prompt_label_, sizeof(prompt_label_), label ? label : "Input:");
    con_str_copy(prompt_buf_, sizeof(prompt_buf_), prefill ? prefill : "");
    prompt_len_ = static_cast<int>(strlen(prompt_buf_));
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::edit_current_settings_field() {
    int idx = settings_sel_[settings_tab_];
    if (settings_tab_ == 0) {
        if (idx == 0) {
            char cs[16]; oyk_format_callsign(&draft_.callsign, cs, sizeof(cs));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Callsign (e.g. N0CALL-7):", cs);
        } else if (idx == 1) {
            char cs[16]; oyk_format_callsign(&draft_.alias, cs, sizeof(cs));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Alias (blank for none):", cs);
        } else if (idx == 2) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Welcome Message:", draft_.welcome_text);
        } else if (idx == 3) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Node Info Message:", draft_.info_text);
        } else if (idx == 4) {
            draft_.accept_incoming = draft_.accept_incoming ? 0 : 1;
            render();
        } else if (idx == 5) {
            draft_.allow_flash_logging = draft_.allow_flash_logging ? 0 : 1;
            render();
        } else if (idx == 6) {
            if (draft_.learned_peer_policy == OYK_LEARN_NONE) draft_.learned_peer_policy = OYK_LEARN_INBOUND;
            else if (draft_.learned_peer_policy == OYK_LEARN_INBOUND) draft_.learned_peer_policy = OYK_LEARN_OUTBOUND;
            else if (draft_.learned_peer_policy == OYK_LEARN_OUTBOUND) draft_.learned_peer_policy = OYK_LEARN_BOTH;
            else draft_.learned_peer_policy = OYK_LEARN_NONE;
            render();
        } else if (idx == 7) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "New User Welcome:", draft_.new_user_welcome);
        } else if (idx == 8) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Interfaces File (blank=inline):", draft_.interfaces_file);
        } else if (idx == 9) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Peers File (blank=inline):", draft_.peers_file);
        } else {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Themes File (blank=inline):", draft_.themes_file);
        }
    } else if (settings_tab_ == 1) {
        if (idx == 0) { draft_.bbs_enabled = draft_.bbs_enabled ? 0 : 1; render(); }
        else if (idx == 1) { char n[8]; sprintf(n, "%u", static_cast<unsigned int>(draft_.bbs_ssid)); begin_prompt(PROMPT_SETTINGS_VALUE, "BBS SSID (0-15):", n); }
        else if (idx == 2) { char cs[16]; oyk_format_callsign(&draft_.bbs_callsign, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Callsign Override (blank=auto):", cs); }
        else if (idx == 3) { char cs[16]; oyk_format_callsign(&draft_.bbs_alias, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Alias (blank=none):", cs); }
        else if (idx == 4) { draft_.bbs_files_enabled = draft_.bbs_files_enabled ? 0 : 1; render(); }
        else if (idx == 5) begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Files Area Root Path:", draft_.bbs_files_path);
        else if (idx == 6) begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Mail Storage Path:", draft_.bbs_mail_path);
        else begin_prompt(PROMPT_SETTINGS_VALUE, "BBS Welcome Message:", draft_.bbs_welcome);
    } else if (settings_tab_ == 2) {
        if (idx == 0) { draft_.chat_enabled = draft_.chat_enabled ? 0 : 1; render(); }
        else if (idx == 1) { char n[8]; sprintf(n, "%u", static_cast<unsigned int>(draft_.chat_ssid)); begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT SSID (0-15):", n); }
        else if (idx == 2) { char cs[16]; oyk_format_callsign(&draft_.chat_callsign, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Callsign Override (blank=auto):", cs); }
        else if (idx == 3) { char cs[16]; oyk_format_callsign(&draft_.chat_alias, cs, sizeof(cs)); begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Alias (blank=none):", cs); }
        else if (idx == 4) begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Default Topic:", draft_.chat_topic);
        else begin_prompt(PROMPT_SETTINGS_VALUE, "CHAT Welcome Message:", draft_.chat_welcome);
    } else if (settings_tab_ == 3) {
        if (idx >= 0 && idx < draft_.iface_count) {
            open_edit_iface_dialog(idx);
        }
    } else if (settings_tab_ == 4) {
        if (idx >= 0 && idx < draft_.peer_count) {
            open_edit_peer_dialog(idx);
        }
    } else if (settings_tab_ == 5) {
        if (idx == 0) { draft_.rig_enabled = draft_.rig_enabled ? 0 : 1; render(); }
        else if (idx == 1) {
            if (strieq_ci(draft_.rig_protocol, "radiofoundry")) {
                strcpy(draft_.rig_protocol, "rigctl");
                if (draft_.rig_port == 0 || draft_.rig_port == OYK_RF_DEFAULT_PORT) {
                    draft_.rig_port = OYK_RIGCTL_DEFAULT_PORT;
                }
            } else {
                strcpy(draft_.rig_protocol, "radiofoundry");
                if (draft_.rig_port == 0 || draft_.rig_port == OYK_RIGCTL_DEFAULT_PORT) {
                    draft_.rig_port = OYK_RF_DEFAULT_PORT;
                }
            }
            render();
        } else if (idx == 2) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Radio Host / IP:", draft_.rig_host);
        } else if (idx == 3) {
            char p[16]; sprintf(p, "%u", static_cast<unsigned int>(draft_.rig_port));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Rig Port (4532/4191):", p);
        } else if (idx == 4) {
            char p[16]; sprintf(p, "%u", static_cast<unsigned int>(draft_.rig_poll_sec));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Rig Poll Interval (seconds):", p);
        } else {
            draft_.rig_notify = draft_.rig_notify ? 0 : 1;
            render();
        }
    } else if (settings_tab_ == 7) {
        OykThemeConfig pal;
        oyk_theme_resolve_effective(&draft_, 0, 1, &pal);
        char hx[16], pr[40];
        oyk_theme_format_hex(pal.colors[idx], hx, sizeof(hx));
        sprintf(pr, "%s (#RRGGBB):", oyk_theme_role_key(idx));
        begin_prompt(PROMPT_SETTINGS_VALUE, pr, hx);
    } else {
        if (idx == 0) { draft_.printer_enabled = draft_.printer_enabled ? 0 : 1; render(); }
        else if (idx == 1) {
            begin_prompt(PROMPT_SETTINGS_VALUE, "Printer Device (usb-printer / ser0..7):", draft_.printer_device);
        } else if (idx == 2) {
            if (strieq_ci(draft_.printer_protocol, "generic")) strcpy(draft_.printer_protocol, "mx80");
            else if (strieq_ci(draft_.printer_protocol, "mx80")) strcpy(draft_.printer_protocol, "escp2");
            else strcpy(draft_.printer_protocol, "generic");
            render();
        } else if (idx == 3) {
            char p[16]; sprintf(p, "%lu", static_cast<unsigned long>(draft_.printer_baud ? draft_.printer_baud : 9600UL));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Printer Serial Baud Rate:", p);
        } else if (idx == 4) {
            char p[16]; sprintf(p, "0x%lX", static_cast<unsigned long>(draft_.printer_level));
            begin_prompt(PROMPT_SETTINGS_VALUE, "Printer Log Mask (e.g. 0x77 or ALL):", p);
        } else {
            draft_.printer_condensed = draft_.printer_condensed ? 0 : 1;
            render();
        }
    }
}

void TinyTmuxConsole::commit_prompt() {
    PromptKind pk = prompt_kind_;
    char val[128];
    con_str_copy(val, sizeof(val), prompt_buf_);
    prompt_kind_ = PROMPT_NONE;
    prompt_buf_[0] = '\0';
    prompt_len_ = 0;

    if (pk == PROMPT_LTERM_CONNECT) {
        if (val[0]) open_lterm_target(val);
        else render();
        return;
    }

    if (pk == PROMPT_FM_UPLOAD) {
        int ex = 0;
        if (val[0]) oyk_gui_filemgr_upload(&file_mgr_, val, 1, &ex);
        render();
        return;
    }
    if (pk == PROMPT_FM_MKDIR) {
        if (val[0]) oyk_gui_filemgr_mkdir(&file_mgr_, val);
        render();
        return;
    }
    if (pk == PROMPT_FM_RENAME) {
        if (val[0]) oyk_gui_filemgr_rename(&file_mgr_, val);
        render();
        return;
    }

    if (pk == PROMPT_CONFIRM_FORMAT) {
        if (strcmp(val, "FORMAT") == 0) {
            char msg[96];
            int ok = tinytmux_fs_format_tier(storage_sel_ + 1, msg, sizeof(msg));
            node_->set_storage_is_flash(tinytmux_fs_is_flash_storage());
            sprintf(dialog_fb_, "%s%.70s", ok ? "OK: " : "ERROR: ", msg);
        } else {
            strcpy(dialog_fb_, "Format cancelled (confirmation text did not match FORMAT).");
        }
        render();
        return;
    }

    if (pk == PROMPT_SETTINGS_VALUE) {
        int idx = settings_sel_[settings_tab_];
        if (settings_tab_ == 0) {
            if (idx == 0) oyk_parse_callsign(val, &draft_.callsign);
            else if (idx == 1) {
                if (!val[0]) memset(&draft_.alias, 0, sizeof(draft_.alias));
                else oyk_parse_callsign(val, &draft_.alias);
            } else if (idx == 2) con_str_copy(draft_.welcome_text, sizeof(draft_.welcome_text), val);
            else if (idx == 3) con_str_copy(draft_.info_text, sizeof(draft_.info_text), val);
            else if (idx == 7) con_str_copy(draft_.new_user_welcome, sizeof(draft_.new_user_welcome), val);
            else if (idx == 8) con_str_copy(draft_.interfaces_file, sizeof(draft_.interfaces_file), val);
            else if (idx == 9) con_str_copy(draft_.peers_file, sizeof(draft_.peers_file), val);
            else if (idx == 10) con_str_copy(draft_.themes_file, sizeof(draft_.themes_file), val);
        } else if (settings_tab_ == 1) {
            if (idx == 1) { long v = strtol(val, NULL, 10); if (v >= 0 && v <= 15) draft_.bbs_ssid = static_cast<oyk_u8>(v); }
            else if (idx == 2) { if (!val[0]) memset(&draft_.bbs_callsign, 0, sizeof(draft_.bbs_callsign)); else oyk_parse_callsign(val, &draft_.bbs_callsign); }
            else if (idx == 3) { if (!val[0]) memset(&draft_.bbs_alias, 0, sizeof(draft_.bbs_alias)); else oyk_parse_callsign(val, &draft_.bbs_alias); }
            else if (idx == 5) con_str_copy(draft_.bbs_files_path, sizeof(draft_.bbs_files_path), val);
            else if (idx == 6) con_str_copy(draft_.bbs_mail_path, sizeof(draft_.bbs_mail_path), val);
            else if (idx == 7) con_str_copy(draft_.bbs_welcome, sizeof(draft_.bbs_welcome), val);
        } else if (settings_tab_ == 2) {
            if (idx == 1) { long v = strtol(val, NULL, 10); if (v >= 0 && v <= 15) draft_.chat_ssid = static_cast<oyk_u8>(v); }
            else if (idx == 2) { if (!val[0]) memset(&draft_.chat_callsign, 0, sizeof(draft_.chat_callsign)); else oyk_parse_callsign(val, &draft_.chat_callsign); }
            else if (idx == 3) { if (!val[0]) memset(&draft_.chat_alias, 0, sizeof(draft_.chat_alias)); else oyk_parse_callsign(val, &draft_.chat_alias); }
            else if (idx == 4) con_str_copy(draft_.chat_topic, sizeof(draft_.chat_topic), val);
            else if (idx == 5) con_str_copy(draft_.chat_welcome, sizeof(draft_.chat_welcome), val);
        } else if (settings_tab_ == 3 && idx >= 0 && idx < draft_.iface_count) {
            if (settings_iface_field_ == 1) {
                if (strncmp(val, "ser", 3) == 0 || strncmp(val, "usbh", 4) == 0) {
                    con_str_copy(draft_.ifaces[idx].device, sizeof(draft_.ifaces[idx].device), val);
                } else {
                    con_str_copy(draft_.ifaces[idx].host, sizeof(draft_.ifaces[idx].host), val);
                }
            }
        } else if (settings_tab_ == 5) {
            if (idx == 2) con_str_copy(draft_.rig_host, sizeof(draft_.rig_host), val);
            else if (idx == 3) { long p = strtol(val, NULL, 10); if (p > 0 && p <= 65535L) draft_.rig_port = static_cast<oyk_u16>(p); }
            else if (idx == 4) { long p = strtol(val, NULL, 10); if (p >= 0 && p <= 3600L) draft_.rig_poll_sec = static_cast<oyk_u16>(p); }
        } else if (settings_tab_ == 6) {
            if (idx == 1) con_str_copy(draft_.printer_device, sizeof(draft_.printer_device), val);
            else if (idx == 3) { long b = strtol(val, NULL, 10); if (b >= 300L) draft_.printer_baud = static_cast<oyk_u32>(b); }
            else if (idx == 4) { oyk_parse_log_mask(val, &draft_.printer_level); }
        } else if (settings_tab_ == 7) {
            oyk_u32 new_rgb = 0;
            if (!oyk_theme_parse_hex(val, &new_rgb)) {
                strcpy(dialog_fb_, "Invalid #RRGGBB hex color.");
            } else {
                const char* active = draft_.theme_name[0] ? draft_.theme_name : "dark";
                if (oyk_theme_is_builtin(active)) {
                    if (draft_.theme_count < OYK_MAX_THEMES) {
                        OykThemeConfig base_pal;
                        oyk_theme_resolve_effective(&draft_, 0, 1, &base_pal);
                        int dark_b = (strcmp(active, "light") == 0 || strcmp(active, "LIGHT") == 0) ? 0 : 1;
                        char cname[24];
                        oyk_theme_make_clone_name(&draft_, dark_b, cname, sizeof(cname));
                        OykThemeConfig* nt = &draft_.themes[draft_.theme_count++];
                        *nt = base_pal;
                        nt->is_dark_base = dark_b ? 1 : 0;
                        con_str_copy(nt->name, sizeof(nt->name), cname);
                        con_str_copy(draft_.theme_name, sizeof(draft_.theme_name), cname);
                    } else {
                        strcpy(dialog_fb_, "Maximum custom themes reached.");
                    }
                }
                for (int ti = 0; ti < draft_.theme_count; ++ti) {
                    if (strcmp(draft_.themes[ti].name, draft_.theme_name) == 0) {
                        draft_.themes[ti].colors[idx] = new_rgb;
                        theme_preview_dirty_ = 1;
                        sprintf(dialog_fb_, "Updated %s (%s).", draft_.theme_name, oyk_theme_role_key(idx));
                        break;
                    }
                }
            }
        }
        render();
        return;
    }

    if (pk == PROMPT_RIG_FREQ && bridge_) {
        oyk_u32 hz = static_cast<oyk_u32>(strtoul(val, NULL, 10));
        strcpy(dialog_fb_, bridge_->rig_set_freq(hz) ? "Frequency updated." : bridge_->rig_status()->last_error);
        render();
        return;
    }

    if (pk == PROMPT_RIG_MODE && bridge_) {
        strcpy(dialog_fb_, bridge_->rig_set_mode(val, 0) ? "Mode updated." : bridge_->rig_status()->last_error);
        render();
        return;
    }

    if (pk == PROMPT_RIG_VFO && bridge_) {
        strcpy(dialog_fb_, bridge_->rig_set_vfo(val) ? "VFO updated." : bridge_->rig_status()->last_error);
        render();
        return;
    }

    if (pk == PROMPT_RIG_RF_SELECT && bridge_) {
        int zone = 0, pos = 0;
        const char* sep = strchr(val, ':');
        if (!sep) sep = strchr(val, ',');
        if (sep) {
            zone = atoi(val);
            pos = atoi(sep + 1);
        } else {
            zone = bridge_->rig_status()->zone >= 0 ? bridge_->rig_status()->zone : 0;
            pos = atoi(val);
        }
        strcpy(dialog_fb_, bridge_->rig_rf_select(zone, pos) ? "RadioFoundry select sent." : bridge_->rig_status()->last_error);
        render();
        return;
    }
}

OYK_FLASHMEM int TinyTmuxConsole::wizard_try_advance() {
    wiz_err_[0] = '\0';
    if (wiz_step_ == 0) {
        if (wiz_drv_ != wiz_last_drv_) {
            wizard_apply_driver_defaults();
        }
        wiz_step_ = 1;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 1) {
        if (!wiz_id_[0]) {
            strcpy(wiz_err_, "Interface ID cannot be empty.");
            wiz_focus_ = 0;
            return 0;
        }
        for (int i = 0; i < draft_.iface_count; ++i) {
            if (strieq_ci(draft_.ifaces[i].id, wiz_id_)) {
                sprintf(wiz_err_, "Duplicate interface ID: %.24s", wiz_id_);
                wiz_focus_ = 0;
                return 0;
            }
        }
        wiz_step_ = 2;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 2) {
        if (!wiz_ep_[0]) {
            strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                 ? "Serial device cannot be empty."
                                 : "Host / IP cannot be empty.");
            wiz_focus_ = 0;
            return 0;
        }
        if (wiz_drv_ != 1 && !wiz_port_[0]) {
            strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                                 ? "Baud rate cannot be empty."
                                 : "Port cannot be empty.");
            wiz_focus_ = 1;
            return 0;
        }
        if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && oyk_is_all_digits(wiz_ep_)) {
            oyk_u32 did = 0;
            if (!oyk_parse_dmr_id(wiz_ep_, &did)) {
                strcpy(wiz_err_, "Invalid DMR ID (allowed 1..16777214).");
                wiz_focus_ = 0;
                return 0;
            }
        }
        if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && wiz_mask_[0]) {
            oyk_u32 mip = 0;
            if (!oyk_parse_ipv4(wiz_mask_, &mip)) {
                strcpy(wiz_err_, "Invalid IPv4 netmask (e.g. 255.0.0.0).");
                wiz_focus_ = (wiz_drv_ == 1) ? 1 : 2;
                return 0;
            }
        }
        wiz_step_ = 3;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        return 0;
    }
    if (wiz_step_ == 3) {
        commit_iface_fields(draft_.iface_count);
        prompt_kind_ = PROMPT_NONE;
        return 1;
    }
    return 0;
}

OYK_FLASHMEM int TinyTmuxConsole::edit_iface_try_save() {
    wiz_err_[0] = '\0';
    if (!wiz_id_[0]) {
        strcpy(wiz_err_, "Interface ID cannot be empty.");
        wiz_focus_ = 1;
        return 0;
    }
    for (int i = 0; i < draft_.iface_count; ++i) {
        if (i == wiz_edit_idx_) continue;
        if (strieq_ci(draft_.ifaces[i].id, wiz_id_)) {
            sprintf(wiz_err_, "Duplicate interface ID: %.24s", wiz_id_);
            wiz_focus_ = 1;
            return 0;
        }
    }
    if (!wiz_ep_[0]) {
        strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                             ? "Serial device cannot be empty."
                             : "Host / IP cannot be empty.");
        wiz_focus_ = 3;
        return 0;
    }
    if (wiz_drv_ != 1 && !wiz_port_[0]) {
        strcpy(wiz_err_, (wiz_drv_ == 2 || wiz_drv_ == 3 || wiz_drv_ == 5)
                             ? "Baud rate cannot be empty."
                             : "Port cannot be empty.");
        wiz_focus_ = 4;
        return 0;
    }
    if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && oyk_is_all_digits(wiz_ep_)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(wiz_ep_, &did)) {
            strcpy(wiz_err_, "Invalid DMR ID (allowed 1..16777214).");
            wiz_focus_ = 3;
            return 0;
        }
    }
    if ((wiz_drv_ == 0 || wiz_drv_ == 1 || wiz_drv_ == 4) && wiz_mask_[0]) {
        oyk_u32 mip = 0;
        if (!oyk_parse_ipv4(wiz_mask_, &mip)) {
            strcpy(wiz_err_, "Invalid IPv4 netmask (e.g. 255.0.0.0).");
            wiz_focus_ = (wiz_drv_ == 1) ? 4 : 5;
            return 0;
        }
    }
    commit_iface_fields(wiz_edit_idx_);
    prompt_kind_ = PROMPT_NONE;
    wiz_edit_idx_ = -1;
    return 1;
}

OYK_FLASHMEM void TinyTmuxConsole::handle_edit_iface_key(int key) {
    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        wiz_edit_idx_ = -1;
        strcpy(dialog_fb_, "Edit interface cancelled.");
        render();
        return;
    }
    int fcnt = edit_iface_field_count();
    if (wiz_focus_ > fcnt) wiz_focus_ = fcnt;

    if (key == TMUX_KEY_UP) {
        wiz_focus_ = (wiz_focus_ + fcnt) % (fcnt + 1);
        render();
        return;
    }
    if (key == TMUX_KEY_DOWN || key == '\t') {
        wiz_focus_ = (wiz_focus_ + 1) % (fcnt + 1);
        render();
        return;
    }

    if (wiz_focus_ == fcnt) {
        if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) wiz_btn_ = (wiz_btn_ == 0) ? 1 : 0;
        else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 1) {
                prompt_kind_ = PROMPT_NONE;
                wiz_edit_idx_ = -1;
                strcpy(dialog_fb_, "Edit interface cancelled.");
            } else {
                edit_iface_try_save();
            }
        } else {
            return;
        }
        render();
        return;
    }

    if (wiz_focus_ == 0) {
        if (key == TMUX_KEY_LEFT) {
            wiz_drv_ = (wiz_drv_ + 6) % 7;
            wizard_apply_driver_defaults();
            wiz_err_[0] = '\0';
            render();
        } else if (key == TMUX_KEY_RIGHT || key == ' ') {
            wiz_drv_ = (wiz_drv_ + 1) % 7;
            wizard_apply_driver_defaults();
            wiz_err_[0] = '\0';
            render();
        } else if (key == '\r' || key == '\n') {
            edit_iface_try_save();
            render();
        }
        return;
    }

    int tbase = (wiz_drv_ == 0) ? 7 : ((wiz_drv_ == 1 || wiz_drv_ == 4) ? 6 : 5);
    if (wiz_focus_ >= tbase) {
        int rel = wiz_focus_ - tbase;
        if (key == ' ' || key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
            if (rel == 0) wiz_flags_ ^= 1U;
            else if (rel == 1 && wiz_drv_ != 6) wiz_flags_ ^= 2U;
            else if (rel == 2 && wiz_drv_ != 6) wiz_flags_ ^= 0x20U;
            else if (rel == 3 && wiz_drv_ != 6) {
                oyk_u8 lp = tmux_cycle_lpol(static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U),
                                            (key == TMUX_KEY_LEFT) ? -1 : 1);
                wiz_flags_ = static_cast<oyk_u8>((wiz_flags_ & 0x23U) | ((lp & 0x07U) << 2));
            }
            render();
        } else if (key == '\r' || key == '\n') {
            edit_iface_try_save();
            render();
        }
        return;
    }

    char* target = NULL;
    size_t cap = 0;
    if (wiz_focus_ == 1) { target = wiz_id_; cap = sizeof(wiz_id_); }
    else if (wiz_focus_ == 2) { target = wiz_desc_; cap = sizeof(wiz_desc_); }
    else if (wiz_focus_ == 3) { target = wiz_ep_; cap = sizeof(wiz_ep_); }
    else if (wiz_drv_ == 1) {
        if (wiz_focus_ == 4) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
        else if (wiz_focus_ == 5) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
    } else {
        if (wiz_focus_ == 4) { target = wiz_port_; cap = sizeof(wiz_port_); }
        else if ((wiz_drv_ == 0 || wiz_drv_ == 4) && wiz_focus_ == 5) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
        else if (wiz_drv_ == 0 && wiz_focus_ == 6) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
    }

    if (key == '\r' || key == '\n') {
        edit_iface_try_save();
        render();
    } else if (target && (key == 8 || key == 127)) {
        size_t n = strlen(target);
        if (n > 0) target[n - 1] = '\0';
        wiz_err_[0] = '\0';
        render();
    } else if (target && key >= 0x20 && key < 0x7F) {
        size_t n = strlen(target);
        if (n + 1 < cap) {
            target[n] = static_cast<char>(key);
            target[n + 1] = '\0';
        }
        wiz_err_[0] = '\0';
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_add_wizard_key(int key) {
    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        strcpy(dialog_fb_, "Add interface cancelled.");
        render();
        return;
    }
    if (key == 0x02 && wiz_step_ > 0) { /* Ctrl+B -> Back */
        --wiz_step_;
        wiz_focus_ = 0;
        wiz_btn_ = 1;
        wiz_err_[0] = '\0';
        render();
        return;
    }

    if (wiz_step_ == 0) {
        if (key == TMUX_KEY_UP) wiz_drv_ = (wiz_drv_ + 6) % 7;
        else if (key == TMUX_KEY_DOWN) wiz_drv_ = (wiz_drv_ + 1) % 7;
        else if (key == TMUX_KEY_HOME) wiz_drv_ = 0;
        else if (key == TMUX_KEY_END) wiz_drv_ = 6;
        else if (key >= '1' && key <= '7') wiz_drv_ = key - '1';
        else if (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT || key == '\t') wiz_btn_ = (wiz_btn_ == 1) ? 2 : 1;
        else if (key == '\r' || key == '\n') {
            if (wiz_btn_ == 2) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Add interface cancelled.");
            } else {
                wizard_try_advance();
            }
        } else {
            return;
        }
        render();
        return;
    }

    if (wiz_step_ == 3) {
        if (key == TMUX_KEY_LEFT) wiz_btn_ = (wiz_btn_ + 2) % 3;
        else if (key == TMUX_KEY_RIGHT || key == '\t') wiz_btn_ = (wiz_btn_ + 1) % 3;
        else if (key == 'b' || key == 'B') {
            wiz_step_ = 2;
            wiz_focus_ = 0;
            wiz_btn_ = 1;
            wiz_err_[0] = '\0';
        } else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 0) {
                wiz_step_ = 2;
                wiz_focus_ = 0;
                wiz_btn_ = 1;
                wiz_err_[0] = '\0';
            } else if (wiz_btn_ == 2) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Add interface cancelled.");
            } else {
                wizard_try_advance();
            }
        } else {
            return;
        }
        render();
        return;
    }

    int fcnt = wizard_step_field_count();
    if (key == TMUX_KEY_UP) {
        wiz_focus_ = (wiz_focus_ + fcnt) % (fcnt + 1);
        render();
        return;
    }
    if (key == TMUX_KEY_DOWN || key == '\t') {
        wiz_focus_ = (wiz_focus_ + 1) % (fcnt + 1);
        render();
        return;
    }

    if (wiz_focus_ == fcnt) {
        if (key == TMUX_KEY_LEFT) wiz_btn_ = (wiz_btn_ + 2) % 3;
        else if (key == TMUX_KEY_RIGHT) wiz_btn_ = (wiz_btn_ + 1) % 3;
        else if (key == '\r' || key == '\n' || key == ' ') {
            if (wiz_btn_ == 0) {
                --wiz_step_;
                wiz_focus_ = 0;
                wiz_btn_ = 1;
                wiz_err_[0] = '\0';
            } else if (wiz_btn_ == 2) {
                prompt_kind_ = PROMPT_NONE;
                strcpy(dialog_fb_, "Add interface cancelled.");
            } else {
                wizard_try_advance();
            }
        } else {
            return;
        }
        render();
        return;
    }

    if (wiz_step_ == 2) {
        int tbase = (wiz_drv_ == 0) ? 4 : ((wiz_drv_ == 1 || wiz_drv_ == 4) ? 3 : 2);
        if (wiz_focus_ >= tbase) {
            int rel = wiz_focus_ - tbase;
            if (key == ' ' || key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
                if (rel == 0) wiz_flags_ ^= 1U;
                else if (rel == 1 && wiz_drv_ != 6) wiz_flags_ ^= 2U;
                else if (rel == 2 && wiz_drv_ != 6) wiz_flags_ ^= 0x20U;
                else if (rel == 3 && wiz_drv_ != 6) {
                    oyk_u8 lp = tmux_cycle_lpol(static_cast<oyk_u8>((wiz_flags_ >> 2) & 0x07U),
                                                (key == TMUX_KEY_LEFT) ? -1 : 1);
                    wiz_flags_ = static_cast<oyk_u8>((wiz_flags_ & 0x23U) | ((lp & 0x07U) << 2));
                }
                render();
            } else if (key == '\r' || key == '\n') {
                wizard_try_advance();
                render();
            }
            return;
        }
    }

    char* target = NULL;
    size_t cap = 0;
    if (wiz_step_ == 1) {
        if (wiz_focus_ == 0) { target = wiz_id_; cap = sizeof(wiz_id_); }
        else { target = wiz_desc_; cap = sizeof(wiz_desc_); }
    } else {
        if (wiz_focus_ == 0) { target = wiz_ep_; cap = sizeof(wiz_ep_); }
        else if (wiz_drv_ == 1) {
            if (wiz_focus_ == 1) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
            else if (wiz_focus_ == 2) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
        } else {
            if (wiz_focus_ == 1) { target = wiz_port_; cap = sizeof(wiz_port_); }
            else if ((wiz_drv_ == 0 || wiz_drv_ == 4) && wiz_focus_ == 2) { target = wiz_mask_; cap = sizeof(wiz_mask_); }
            else if (wiz_drv_ == 0 && wiz_focus_ == 3) { target = wiz_peer_; cap = sizeof(wiz_peer_); }
        }
    }

    if (key == '\r' || key == '\n') {
        wizard_try_advance();
        render();
    } else if (target && (key == 8 || key == 127)) {
        size_t n = strlen(target);
        if (n > 0) target[n - 1] = '\0';
        wiz_err_[0] = '\0';
        render();
    } else if (target && key >= 0x20 && key < 0x7F) {
        size_t n = strlen(target);
        if (n + 1 < cap) {
            target[n] = static_cast<char>(key);
            target[n + 1] = '\0';
        }
        wiz_err_[0] = '\0';
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_prompt_key(int key) {
    if (prompt_kind_ == PROMPT_TERMINATE_SID) {
        handle_terminate_key(key);
        return;
    }
    if (prompt_kind_ == PROMPT_BAN_CALLSIGN) {
        handle_ban_key(key);
        return;
    }
    if (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD) {
        handle_add_wizard_key(key);
        return;
    }
    if (prompt_kind_ == PROMPT_SETTINGS_EDIT_IFACE) {
        handle_edit_iface_key(key);
        return;
    }
    if (prompt_kind_ == PROMPT_SETTINGS_EDIT_PEER) {
        handle_edit_peer_key(key);
        return;
    }

    if (key == 27) {
        prompt_kind_ = PROMPT_NONE;
        prompt_buf_[0] = '\0';
        prompt_len_ = 0;
        render();
    } else if (key == '\r' || key == '\n') {
        commit_prompt();
    } else if ((key == 8 || key == 127) && prompt_len_ > 0) {
        prompt_buf_[--prompt_len_] = '\0';
        render();
    } else if (key >= 0x20 && key < 0x7F && prompt_len_ < 120) {
        prompt_buf_[prompt_len_++] = static_cast<char>(key);
        prompt_buf_[prompt_len_] = '\0';
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::exec_menu_hotkey(int key) {
    if (key == 'a' || key == 'A') {
        node_->set_accept_incoming(1);
        strcpy(status_line_, "New incoming connections are now ALLOWED.");
        render();
    } else if (key == 'd' || key == 'D') {
        node_->set_accept_incoming(0);
        strcpy(status_line_, "New incoming connections are now DENIED.");
        render();
    } else if (key == 'l' || key == 'L') {
        lterm_ctrl_a_ = 0;
        lterm_help_open_ = 0;
        view_ = TMUX_VIEW_LOCAL_TERM;
        render();
    } else if (key == 'f' || key == 'F') {
        dialog_fb_[0] = '\0';
        fm_preview_active_ = 0;
        files_focus_btn_ = 0;
        refresh_files_lists();
        view_ = TMUX_VIEW_FILES;
        render();
    } else if (key == 'r' || key == 'R') {
        dialog_fb_[0] = '\0';
        if (bridge_) bridge_->sync_rig_config();
        view_ = TMUX_VIEW_RIG;
        render();
    } else if (key == 's' || key == 'S') {
        open_settings(0);
    } else if (key == 't' || key == 'T') {
        open_terminate_dialog();
    } else if (key == 'b' || key == 'B') {
        open_ban_dialog();
    } else if (key == '?') {
        view_ = TMUX_VIEW_ABOUT;
        render();
    } else if (key == 'p' || key == 'P') {
        view_ = TMUX_VIEW_PSRAM;
        render();
    } else if (key == 'g' || key == 'G') {
        storage_sel_ = 0;
        dialog_fb_[0] = '\0';
        view_ = TMUX_VIEW_STORAGE;
        render();
    } else if (key == 'u' || key == 'U') {
        bootloader_pending_ = 0;
        view_ = TMUX_VIEW_BOOTLOADER;
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_console_key(int key) {
    if (key == 0x0E || key == TMUX_KEY_F10) {
        view_ = TMUX_VIEW_MENU;
        menu_sel_ = 0;
        render();
    } else if (key == '\t' || key == TMUX_KEY_F9) {
        lterm_ctrl_a_ = 0;
        lterm_help_open_ = 0;
        view_ = TMUX_VIEW_LOCAL_TERM;
        render();
    } else if (key == 0x0C) {
        send_size_probe();
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_menu_key(int key) {
    if (key == 27 || key == TMUX_KEY_F10 || key == 0x0E || key == 'x' || key == 'X') {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == TMUX_KEY_UP) {
        menu_sel_ = (menu_sel_ + 12) % 13;
        render();
    } else if (key == TMUX_KEY_DOWN) {
        menu_sel_ = (menu_sel_ + 1) % 13;
        render();
    } else if (key == TMUX_KEY_HOME) {
        menu_sel_ = 0;
        render();
    } else if (key == TMUX_KEY_END) {
        menu_sel_ = 12;
        render();
    } else if (key == '\r' || key == '\n') {
        static const int KEYS[13] = { 'a', 'd', 'l', 'f', 'r', 's', 't', 'b', '?', 'p', 'g', 'u', 'x' };
        int k = KEYS[menu_sel_];
        view_ = TMUX_VIEW_CONSOLE;
        if (k == 'x') render();
        else exec_menu_hotkey(k);
    } else if (key == 'a' || key == 'A' || key == 'd' || key == 'D' ||
               key == 'l' || key == 'L' || key == 'f' || key == 'F' ||
               key == 'r' || key == 'R' || key == 's' || key == 'S' ||
               key == 't' || key == 'T' || key == 'b' || key == 'B' ||
               key == '?' ||
               key == 'p' || key == 'P' || key == 'u' || key == 'U' ||
               key == 'g' || key == 'G') {
        view_ = TMUX_VIEW_CONSOLE;
        exec_menu_hotkey(key);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_files_key(int key) {
    OykGuiFilePane* p = (file_mgr_.active_pane == 0) ? &file_mgr_.inc : &file_mgr_.files;
    if (fm_preview_active_) {
        if (key == 27 || key == '\r' || key == '\n' || key == 'q' || key == 'Q' || key == 'v' || key == 'V') {
            fm_preview_active_ = 0;
            render();
        }
        return;
    }
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        render();
    } else if (key == '\t' || key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT) {
        file_mgr_.active_pane = 1 - file_mgr_.active_pane;
        render();
    } else if (key == '1') {
        file_mgr_.active_pane = 0;
        render();
    } else if (key == '2') {
        file_mgr_.active_pane = 1;
        render();
    } else if (key == 'r' || key == 'R') {
        oyk_gui_filemgr_refresh(&file_mgr_);
        strcpy(file_mgr_.feedback, "Refreshed files lists.");
        render();
    } else if (key == TMUX_KEY_UP) {
        if (p->count > 0 && p->sel > 0) --p->sel;
        render();
    } else if (key == TMUX_KEY_DOWN) {
        if (p->count > 0 && p->sel + 1 < p->count) ++p->sel;
        render();
    } else if (key == TMUX_KEY_HOME) {
        p->sel = 0;
        render();
    } else if (key == TMUX_KEY_END) {
        if (p->count > 0) p->sel = p->count - 1;
        render();
    } else if (key == 8 || key == 127 || key == '<' || key == '-') {
        oyk_gui_filemgr_up(&file_mgr_, file_mgr_.active_pane);
        render();
    } else if (key == '\r' || key == '\n') {
        int act = oyk_gui_filemgr_activate(&file_mgr_, file_mgr_.active_pane);
        if (act == 0 && p->count > 0 && p->sel >= 0 && p->sel < p->count && !p->entries[p->sel].is_dir) {
            int trunc = 0;
            if (oyk_gui_filemgr_view(&file_mgr_, fm_preview_buf_, sizeof(fm_preview_buf_), &trunc)) {
                fm_preview_active_ = 1;
            }
        }
        render();
    } else if (key == 'a' || key == 'A') {
        int ex = 0;
        oyk_gui_filemgr_approve(&file_mgr_, 1, &ex);
        render();
    } else if (key == 'u' || key == 'U') {
        begin_prompt(PROMPT_FM_UPLOAD, "Upload local file path:", "");
    } else if (key == 'n' || key == 'N') {
        begin_prompt(PROMPT_FM_MKDIR, "New subdirectory name:", "");
    } else if (key == 'm' || key == 'M') {
        const char* cur = (p->count > 0 && p->sel >= 0 && p->sel < p->count && !p->entries[p->sel].is_parent)
                              ? p->entries[p->sel].name
                              : "";
        if (cur[0]) begin_prompt(PROMPT_FM_RENAME, "Rename selected entry to:", cur);
    } else if (key == 'd' || key == 'D') {
        oyk_gui_filemgr_delete(&file_mgr_);
        render();
    } else if (key == 'v' || key == 'V') {
        int trunc = 0;
        if (oyk_gui_filemgr_view(&file_mgr_, fm_preview_buf_, sizeof(fm_preview_buf_), &trunc)) {
            fm_preview_active_ = 1;
        }
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_rig_key(int key) {
    if (key == 27) {
        view_ = TMUX_VIEW_CONSOLE;
        render();
        return;
    }
    if (!bridge_ || !node_->config().rig_enabled) {
        strcpy(dialog_fb_, "Rig control not available.");
        render();
        return;
    }
    int proto = oyk_rig_parse_protocol(node_->config().rig_protocol);
    if (key == 'r' || key == 'R') {
        strcpy(dialog_fb_, bridge_->rig_poll_now(now_ms_) ? "Rig polled." : bridge_->rig_status()->last_error);
    } else if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
        if (key == 'j' || key == 'J') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_channel(-1) ? "Channel -1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'k' || key == 'K') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_channel(1) ? "Channel +1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'h' || key == 'H') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_zone(-1) ? "Zone -1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'l' || key == 'L') {
            strcpy(dialog_fb_, bridge_->rig_rf_step_zone(1) ? "Zone +1 sent." : bridge_->rig_status()->last_error);
        } else if (key == 'n' || key == 'N') {
            strcpy(dialog_fb_, bridge_->rig_rf_stop_notify() ? "Stop notify sent." : bridge_->rig_status()->last_error);
        } else if (key == 's' || key == 'S') {
            begin_prompt(PROMPT_RIG_RF_SELECT, "Select Zone:Pos (e.g. 0:1):", "0:0");
            return;
        }
    } else {
        if (key == 'f' || key == 'F') {
            begin_prompt(PROMPT_RIG_FREQ, "Frequency in Hz (e.g. 145050000):", "");
            return;
        } else if (key == 'm' || key == 'M') {
            begin_prompt(PROMPT_RIG_MODE, "Mode (e.g. FM, USB, PKTFM):", bridge_->rig_status()->mode);
            return;
        } else if (key == 'p' || key == 'P') {
            int next_ptt = (bridge_->rig_status()->ptt == 1) ? 0 : 1;
            strcpy(dialog_fb_, bridge_->rig_set_ptt(next_ptt) ? "PTT updated." : bridge_->rig_status()->last_error);
        } else if (key == 'v' || key == 'V') {
            begin_prompt(PROMPT_RIG_VFO, "VFO (VFOA/VFOB):", bridge_->rig_status()->vfo);
            return;
        }
    }
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::handle_settings_key(int key) {
    int cnt = settings_tab_item_count(settings_tab_);
    if (key == 27) {
        theme_preview_dirty_ = 0;
        view_ = TMUX_VIEW_CONSOLE;
        strcpy(status_line_, "Settings cancelled (no changes applied).");
        render();
    } else if (key == '\t' || (settings_tab_ != 7 && key == TMUX_KEY_RIGHT)) {
        settings_tab_ = (settings_tab_ + 1) % 8;
        render();
    } else if (key == TMUX_KEY_BTAB || (settings_tab_ != 7 && key == TMUX_KEY_LEFT)) {
        settings_tab_ = (settings_tab_ + 7) % 8;
        render();
    } else if (key >= '1' && key <= '8') {
        settings_tab_ = key - '1';
        render();
    } else if (key == TMUX_KEY_UP && cnt > 0) {
        settings_sel_[settings_tab_] = (settings_sel_[settings_tab_] + cnt - 1) % cnt;
        render();
    } else if (key == TMUX_KEY_DOWN && cnt > 0) {
        settings_sel_[settings_tab_] = (settings_sel_[settings_tab_] + 1) % cnt;
        render();
    } else if (key == TMUX_KEY_HOME && cnt > 0) {
        settings_sel_[settings_tab_] = 0;
        render();
    } else if (key == TMUX_KEY_END && cnt > 0) {
        settings_sel_[settings_tab_] = cnt - 1;
        render();
    } else if (key == TMUX_KEY_F2 || key == 's' || key == 'S') {
        save_and_apply_settings();
    } else if (settings_tab_ == 7 && (key == TMUX_KEY_LEFT || key == TMUX_KEY_RIGHT || key == '<' || key == '>')) {
        const char* names[OYK_MAX_THEMES + 2];
        int n_names = 0;
        names[n_names++] = "dark";
        names[n_names++] = "light";
        for (int ti = 0; ti < draft_.theme_count && ti < OYK_MAX_THEMES; ++ti) {
            if (!oyk_theme_is_builtin(draft_.themes[ti].name)) {
                names[n_names++] = draft_.themes[ti].name;
            }
        }
        int ci = 0;
        for (int i = 0; i < n_names; ++i) {
            if (strcmp(names[i], draft_.theme_name) == 0) { ci = i; break; }
        }
        int next = (key == TMUX_KEY_RIGHT || key == '>');
        ci = next ? ((ci + 1) % n_names) : ((ci + n_names - 1) % n_names);
        con_str_copy(draft_.theme_name, sizeof(draft_.theme_name), names[ci]);
        theme_preview_dirty_ = 1;
        sprintf(dialog_fb_, "Previewing '%.14s' (S/F2:Save  Esc:Revert).", draft_.theme_name);
        render();
    } else if (settings_tab_ == 7 && (key == 'x' || key == 'X')) {
        if (oyk_theme_is_builtin(draft_.theme_name)) {
            strcpy(dialog_fb_, "Built-in themes cannot be deleted.");
        } else {
            for (int ti = 0; ti < draft_.theme_count; ++ti) {
                if (strcmp(draft_.themes[ti].name, draft_.theme_name) == 0) {
                    for (int k = ti; k + 1 < draft_.theme_count; ++k) draft_.themes[k] = draft_.themes[k + 1];
                    --draft_.theme_count;
                    break;
                }
            }
            con_str_copy(draft_.theme_name, sizeof(draft_.theme_name), "dark");
            theme_preview_dirty_ = 1;
            strcpy(dialog_fb_, "Deleted custom theme; previewing dark.");
        }
        render();
    } else if (key == '\r' || key == '\n' || key == 'e' || key == 'E') {
        settings_iface_field_ = 0;
        edit_current_settings_field();
    } else if (settings_tab_ == 3 && (key == 'h' || key == 'H')) {
        if (cnt > 0) {
            int idx = settings_sel_[3];
            settings_iface_field_ = 1;
            const char* cur = draft_.ifaces[idx].device[0] ? draft_.ifaces[idx].device : draft_.ifaces[idx].host;
            begin_prompt(PROMPT_SETTINGS_VALUE, "Interface Device (ser0/usbh0) or Host IP:", cur);
        }
    } else if (settings_tab_ == 3 && (key == 'n' || key == 'N' || key == 'a' || key == 'A')) {
        open_add_iface_wizard();
    } else if (settings_tab_ == 3 && (key == 'd' || key == 'D' || key == 'x' || key == 'X')) {
        if (draft_.iface_count > 0) {
            int idx = settings_sel_[3];
            for (int k = idx + 1; k < draft_.iface_count; ++k) {
                draft_.ifaces[k - 1] = draft_.ifaces[k];
            }
            draft_.iface_count--;
            if (settings_sel_[3] >= draft_.iface_count && draft_.iface_count > 0) {
                settings_sel_[3] = draft_.iface_count - 1;
            }
            strcpy(dialog_fb_, "Interface removed from draft.");
            render();
        }
    } else if (settings_tab_ == 4 && (key == 'n' || key == 'N' || key == 'a' || key == 'A')) {
        open_edit_peer_dialog(-1);
    } else if (settings_tab_ == 4 && (key == 'd' || key == 'D' || key == 'x' || key == 'X')) {
        if (draft_.peer_count > 0) {
            int idx = settings_sel_[4];
            for (int k = idx + 1; k < draft_.peer_count; ++k) {
                draft_.peers[k - 1] = draft_.peers[k];
            }
            draft_.peer_count--;
            if (settings_sel_[4] >= draft_.peer_count && draft_.peer_count > 0) {
                settings_sel_[4] = draft_.peer_count - 1;
            }
            strcpy(dialog_fb_, "Peer removed from draft.");
            render();
        }
    } else if (settings_tab_ == 4 && (key == 'i' || key == 'I') && cnt > 0) {
        OykPeerConfig& pc = draft_.peers[settings_sel_[4]];
        pc.allow_inbound = pc.allow_inbound ? 0 : 1;
        pc.auto_learned = 0;
        sprintf(dialog_fb_, "Peer %s allow_inbound=%s", pc.id, pc.allow_inbound ? "true" : "false");
        render();
    } else if (settings_tab_ == 4 && (key == 'o' || key == 'O') && cnt > 0) {
        OykPeerConfig& pc = draft_.peers[settings_sel_[4]];
        pc.allow_outbound = pc.allow_outbound ? 0 : 1;
        pc.auto_learned = 0;
        sprintf(dialog_fb_, "Peer %s allow_outbound=%s", pc.id, pc.allow_outbound ? "true" : "false");
        render();
    } else if (settings_tab_ == 4 && (key == 'k' || key == 'K')) {
        int kept = 0;
        for (int i = 0; i < draft_.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
            if (draft_.peers[i].auto_learned) {
                draft_.peers[i].auto_learned = 0;
                ++kept;
            }
        }
        sprintf(dialog_fb_, "Marked %d auto-learned peer(s) persistent.", kept);
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_psram_key(int key) {
    if (key == 27) {
        if (tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING) {
            tinytmux_psram_marchc_cancel();
            render();
        } else {
            view_ = TMUX_VIEW_CONSOLE;
            render();
        }
    } else if (key == 't' || key == 'T' || key == 'r' || key == 'R') {
        if (tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING) {
            tinytmux_psram_marchc_cancel();
        }
        tinytmux_psram_retest();
        render();
    } else if (key == 'm' || key == 'M') {
        if (tinytmux_psram_marchc_status()->state != TMUX_MARCHC_RUNNING) {
            tinytmux_psram_marchc_start(now_ms_);
            last_marchc_redraw_ms_ = now_ms_;
            render();
        }
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_bootloader_key(int key) {
    if (bootloader_pending_) return;
    int auto_cnt = node_ ? node_->auto_learned_peer_count() : 0;
    if (key == 27 || key == 'n' || key == 'N') {
        view_ = TMUX_VIEW_CONSOLE;
        render();
        return;
    }
    if (auto_cnt > 0) {
        if (key == 'r' || key == 'R') {
            open_settings(4);
            return;
        }
        if (key == 's' || key == 'S') {
            char err[64];
            node_->persist_auto_learned_peers(err, sizeof(err));
        } else if (key == 'd' || key == 'D') {
            node_->discard_auto_learned_peers();
        } else {
            return;
        }
    } else if (key != 'y' && key != 'Y') {
        return;
    }
    char call_s[16], notice[128];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    sprintf(notice, "\r*** Node %s going offline for firmware upgrade. Expect a delay before reconnect.\r", call_s);
    node_->begin_shutdown_with_notice(notice);
    disable_mouse();
    bootloader_pending_ = 1;
    bootloader_start_ms_ = now_ms_;
    render();
}

OYK_FLASHMEM void TinyTmuxConsole::open_lterm_target(const char* target) {
    if (!node_ || !target || !target[0]) return;
    if (lterm_sid_ != 0) node_->close_local_terminal(lterm_sid_);
    lterm_sid_ = 0;
    lterm_curline_len_ = 0;
    lterm_curline_[0] = '\0';
    lterm_scroll_ = 0;
    oyk_term_eol_init(&lterm_eol_, OYK_TERM_EOL_CR, 1);
    char err[64];
    lterm_opening_ = 1;
    int ok = node_->open_local_terminal(-1, target, &lterm_sid_, NULL, &lterm_is_local_, err, sizeof(err));
    lterm_opening_ = 0;
    if (!ok) {
        push_lterm_line(err);
    }
    render();
}

OYK_FLASHMEM int TinyTmuxConsole::exec_lterm_cmd(int cmd) {
    switch (cmd) {
        case OYK_TCMD_CONNECT:
            begin_prompt(PROMPT_LTERM_CONNECT, "Target (LOCAL, BBS, CHAT, or CALL@ip:port):", "");
            return 0;
        case OYK_TCMD_LOCAL:
            open_lterm_target("LOCAL");
            return 0;
        case OYK_TCMD_BBS:
            open_lterm_target("BBS");
            return 0;
        case OYK_TCMD_CHAT:
            open_lterm_target("CHAT");
            return 0;
        case OYK_TCMD_DISCONNECT:
            if (lterm_sid_ != 0) {
                node_->close_local_terminal(lterm_sid_);
                lterm_sid_ = 0;
            }
            render();
            return 0;
        case OYK_TCMD_YAPP_SEND:
            push_lterm_line("*** YAPP upload via serial console is unsupported (receive works).");
            render();
            return 0;
        case OYK_TCMD_YAPP_ABORT:
            push_lterm_line("*** No interactive YAPP upload active.");
            render();
            return 0;
        case OYK_TCMD_CYCLE_EOL: {
            oyk_term_eol_cycle_mode(&lterm_eol_);
            char msg[80];
            sprintf(msg, "*** Terminal EOL set to %s.", oyk_term_eol_label(&lterm_eol_));
            push_lterm_line(msg);
            render();
            return 0;
        }
        case OYK_TCMD_PORTS:
            push_lterm_line("*** Configured Interfaces:");
            for (int pi = 0; pi < node_->iface_count(); ++pi) {
                char pline[120];
                const OykIfaceRuntime& ir = node_->iface(pi);
                sprintf(pline, "  %-10s [%-8s] %s", ir.cfg.id, ir.cfg.type, ir.status);
                push_lterm_line(pline);
            }
            render();
            return 0;
        case OYK_TCMD_CLEAR:
            lterm_line_count_ = 0;
            lterm_curline_len_ = 0;
            lterm_curline_[0] = '\0';
            lterm_scroll_ = 0;
            render();
            return 0;
        case OYK_TCMD_REDRAW:
            send_size_probe();
            render();
            return 0;
        case OYK_TCMD_CONSOLE:
            lterm_ctrl_a_ = 0;
            lterm_help_open_ = 0;
            view_ = TMUX_VIEW_CONSOLE;
            strcpy(status_line_, lterm_sid_
                                     ? "Console (Local Terminal active; press Tab to return)"
                                     : "Returned to Operator Console.");
            render();
            return 1;
        case OYK_TCMD_SEND_CTRL_A:
            if (lterm_sid_ != 0) {
                oyk_u8 b = 0x01U;
                node_->send_local_bytes(lterm_sid_, &b, 1);
            } else {
                push_lterm_line("*** Not connected. Press Ctrl+A Z for help or Ctrl+A B / F2.");
            }
            render();
            return 0;
        default:
            return 0;
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_lterm_key(int key) {
    if (lterm_help_open_) {
        if (key == 27 || key == '\r' || key == '\n' || key == ' ') {
            lterm_help_open_ = 0;
            render();
            return;
        }
        int cmd = oyk_term_decode_ctrl_a_cmd(key);
        if (cmd != OYK_TCMD_NONE && cmd != OYK_TCMD_HELP && cmd != OYK_TCMD_CANCEL) {
            lterm_help_open_ = 0;
            exec_lterm_cmd(cmd);
        }
        return;
    }
    if (lterm_ctrl_a_) {
        lterm_ctrl_a_ = 0;
        int cmd = oyk_term_decode_ctrl_a_cmd(key);
        if (cmd == OYK_TCMD_CANCEL) {
            render();
            return;
        }
        if (cmd == OYK_TCMD_HELP) {
            lterm_help_open_ = 1;
            render();
            return;
        }
        if (cmd != OYK_TCMD_NONE) {
            exec_lterm_cmd(cmd);
        } else {
            render();
        }
        return;
    }
    if (key == 0x01) {
        lterm_ctrl_a_ = 1;
        render();
        return;
    }
    if (key == 27 || key == '\t') {
        exec_lterm_cmd(OYK_TCMD_CONSOLE);
        return;
    }
    if (key == TMUX_KEY_F2) {
        exec_lterm_cmd(OYK_TCMD_CONNECT);
        return;
    }
    if (key == TMUX_KEY_PGUP) {
        lterm_scroll_ += 6;
        render();
        return;
    }
    if (key == TMUX_KEY_PGDN) {
        lterm_scroll_ = (lterm_scroll_ > 6) ? (lterm_scroll_ - 6) : 0;
        render();
        return;
    }
    if (key == TMUX_KEY_HOME) {
        lterm_scroll_ = 10000;
        render();
        return;
    }
    if (key == TMUX_KEY_END) {
        lterm_scroll_ = 0;
        render();
        return;
    }
    if (key == '\r' || key == '\n') {
        lterm_input_[lterm_input_len_] = '\0';
        char line[164];
        strcpy(line, lterm_input_);
        lterm_input_len_ = 0;
        lterm_input_[0] = '\0';

        char echo[200];
        sprintf(echo, "> %.190s", line);
        push_lterm_line(echo);
        if (lterm_sid_ != 0) {
            size_t n = strlen(line);
            size_t eol_len = 0;
            const char* eol_s = oyk_term_eol_tx_bytes(&lterm_eol_, &eol_len);
            memcpy(line + n, eol_s, eol_len);
            n += eol_len;
            line[n] = '\0';
            node_->send_local_bytes(lterm_sid_, reinterpret_cast<const oyk_u8*>(line), n);
        } else {
            push_lterm_line("*** Not connected. Press Ctrl+A Z for help or Ctrl+A B / F2.");
        }
        render();
        return;
    }
    if (key == 0x0C) {
        exec_lterm_cmd(OYK_TCMD_REDRAW);
        return;
    }
    if ((key == 8 || key == 127) && lterm_input_len_ > 0) {
        lterm_input_[--lterm_input_len_] = '\0';
        render();
    } else if (key >= 0x20 && key < 0x7F && lterm_input_len_ < 154) {
        lterm_input_[lterm_input_len_++] = static_cast<char>(key);
        lterm_input_[lterm_input_len_] = '\0';
        render();
    }
}

void TinyTmuxConsole::reset_escape() {
    esc_state_ = ESC_IDLE;
    esc_len_ = 0;
    x10_len_ = 0;
    esc_buf_[0] = '\0';
}

OYK_FLASHMEM void TinyTmuxConsole::dispatch_key(int key) {
    if (!node_) return;
    if (prompt_kind_ != PROMPT_NONE) {
        handle_prompt_key(key);
        return;
    }

    switch (view_) {
        case TMUX_VIEW_CONSOLE:    handle_console_key(key); break;
        case TMUX_VIEW_MENU:       handle_menu_key(key); break;
        case TMUX_VIEW_LOCAL_TERM: handle_lterm_key(key); break;
        case TMUX_VIEW_FILES:      handle_files_key(key); break;
        case TMUX_VIEW_RIG:        handle_rig_key(key); break;
        case TMUX_VIEW_SETTINGS:   handle_settings_key(key); break;
        case TMUX_VIEW_PSRAM:      handle_psram_key(key); break;
        case TMUX_VIEW_BOOTLOADER: handle_bootloader_key(key); break;
        case TMUX_VIEW_STORAGE:    handle_storage_key(key); break;
        case TMUX_VIEW_ABOUT:
            if (key == 27 || key == '\r' || key == '\n' || key == 'q' || key == 'Q') {
                view_ = TMUX_VIEW_CONSOLE;
                render();
            }
            break;
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_da_csi() {
    TinyTmuxTermType cand = term_type_;
    if (esc_buf_[0] == '?') {
        const char* p = esc_buf_ + 1;
        int idx = 0;
        while (*p) {
            int v = atoi(p);
            if (idx == 0) {
                if (v == 62 && cand < TMUX_TERM_VT220) cand = TMUX_TERM_VT220;
                else if (v >= 63 && v <= 65 && cand < TMUX_TERM_VT340) cand = TMUX_TERM_VT340;
            } else if (v == 22 && cand < TMUX_TERM_VT340) {
                cand = TMUX_TERM_VT340;
            }
            const char* semi = strchr(p, ';');
            if (!semi) break;
            p = semi + 1;
            ++idx;
        }
    } else if (esc_buf_[0] == '>') {
        int pp = -1, pv = 0;
        const char* p = esc_buf_ + 1;
        if (*p) pp = atoi(p);
        const char* semi = strchr(p, ';');
        if (semi) pv = atoi(semi + 1);
        if ((pp == 0 && pv >= 95) ||
            (pp == 1 && pv >= 200) ||
            (pp == 41 && pv >= 200) ||
            (pp == 65 && pv >= 5000) ||
            pp == 83 || pp == 84 || pp == 85) {
            cand = TMUX_TERM_XTERM256;
        } else if (pp == 18 || pp == 19 || pp == 24 || pp == 41 ||
                   pp == 61 || pp == 64 || pp == 65) {
            if (cand < TMUX_TERM_VT340) cand = TMUX_TERM_VT340;
        } else if (pp == 1) {
            if (cand < TMUX_TERM_VT220) cand = TMUX_TERM_VT220;
        }
    }
    if (cand > term_type_) {
        term_type_ = cand;
        if (term_type_ >= TMUX_TERM_VT340 && !mouse_enabled_) {
            mouse_enabled_ = 1;
            write_str("\033[?1000h\033[?1006h");
        }
        render();
    }
}

OYK_FLASHMEM void TinyTmuxConsole::handle_mouse_event(int row1, int col1, int kind) {
    if (!node_) return;
    const int r0 = row1 - 1;
    const int c0 = col1 - 1;
    /* kind: 0=left, 2=right, 4=wheel_up, 5=wheel_down */
    if (prompt_kind_ != PROMPT_NONE) {
        if (kind == 4) dispatch_key(TMUX_KEY_UP);
        else if (kind == 5) dispatch_key(TMUX_KEY_DOWN);
        else if (kind == 2) dispatch_key(27);
        else if (kind == 0) {
            OykTuiRect mr;
            if (prompt_kind_ == PROMPT_BAN_CALLSIGN) {
                if (wiz_btn_) {
                    oyk_tui_center_modal(term_rows_, term_cols_, 6, 46, &mr);
                    if (oyk_tui_hit_close_btn(&mr, r0, c0)) { dispatch_key(27); return; }
                } else {
                    oyk_tui_center_modal(term_rows_, term_cols_, 21, 74, &mr);
                    if (oyk_tui_hit_close_btn(&mr, r0, c0)) { dispatch_key(27); return; }
                }
            } else {
                int ph = 6, pw = 68;
                if (prompt_kind_ == PROMPT_TERMINATE_SID) { ph = 16; pw = 64; }
                else if (prompt_kind_ == PROMPT_SETTINGS_ADD_WIZARD ||
                         prompt_kind_ == PROMPT_SETTINGS_EDIT_IFACE) { ph = 19; pw = 68; }
                else if (prompt_kind_ == PROMPT_SETTINGS_EDIT_PEER) { ph = 16; pw = 68; }
                else if (prompt_kind_ == PROMPT_CONFIRM_FORMAT) { ph = 12; pw = 68; }
                oyk_tui_center_modal(term_rows_, term_cols_, ph, pw, &mr);
                if (oyk_tui_hit_close_btn(&mr, r0, c0)) { dispatch_key(27); return; }
            }
        }
        return;
    }

    if (kind == 4 || kind == 5) {
        if (view_ == TMUX_VIEW_LOCAL_TERM) {
            dispatch_key((kind == 4) ? TMUX_KEY_PGUP : TMUX_KEY_PGDN);
        } else if (view_ == TMUX_VIEW_MENU || view_ == TMUX_VIEW_SETTINGS ||
                   view_ == TMUX_VIEW_FILES || view_ == TMUX_VIEW_STORAGE) {
            dispatch_key((kind == 4) ? TMUX_KEY_UP : TMUX_KEY_DOWN);
        }
        return;
    }

    if (view_ == TMUX_VIEW_LOCAL_TERM && lterm_help_open_) {
        if (kind == 2) {
            lterm_help_open_ = 0;
            render();
            return;
        }
        if (kind == 0) {
            OykTuiRect mr;
            oyk_tui_center_modal(term_rows_, term_cols_, 19, 68, &mr);
            if (oyk_tui_hit_close_btn(&mr, r0, c0) || !oyk_tui_point_in_rect(&mr, r0, c0)) {
                lterm_help_open_ = 0;
                render();
            }
        }
        return;
    }

    if (kind == 2) {
        if (view_ == TMUX_VIEW_CONSOLE) dispatch_key(TMUX_KEY_F10);
        else dispatch_key(27);
        return;
    }

    if (kind != 0) return;

    if (view_ == TMUX_VIEW_LOCAL_TERM) {
        OykTuiTermLayout tlay;
        oyk_tui_compute_term_layout(term_rows_, term_cols_, &tlay);
        if (r0 == tlay.hotkey_row) {
            if (c0 >= 1 && c0 <= 14) { lterm_ctrl_a_ = 0; lterm_help_open_ = 1; render(); }
            else if (c0 >= 16 && c0 <= 23) exec_lterm_cmd(OYK_TCMD_CONNECT);
            else if (c0 >= 25 && c0 <= 44) exec_lterm_cmd(OYK_TCMD_BBS);
            else if (c0 >= 46 && c0 <= 58) exec_lterm_cmd(OYK_TCMD_CYCLE_EOL);
            else if (c0 >= 60) exec_lterm_cmd(OYK_TCMD_CONSOLE);
        }
        return;
    }

    if (view_ == TMUX_VIEW_CONSOLE) {
        if (row1 == 1) {
            if (col1 <= 9 || col1 >= term_cols_ - 11) dispatch_key(TMUX_KEY_F10);
            else if (col1 >= term_cols_ - 31) dispatch_key('\t');
        } else if (row1 == 2 && col1 >= 40) {
            exec_menu_hotkey(node_->accept_incoming() ? 'd' : 'a');
        }
    } else if (view_ == TMUX_VIEW_MENU) {
        static const int ITEM_ROWS[13] = { 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17 };
        if (row1 < 2 || row1 > 18 || col1 < 2 || col1 > 37) {
            dispatch_key(27);
            return;
        }
        for (int i = 0; i < 13; ++i) {
            if (row1 == ITEM_ROWS[i]) {
                menu_sel_ = i;
                dispatch_key('\r');
                break;
            }
        }
    } else if (view_ == TMUX_VIEW_SETTINGS) {
        static const int TAB_LENS[8] = { 8, 7, 8, 10, 9, 7, 7, 7 }; /* " 1:Node " .. " 8:Thm " */
        OykTuiRect mr;
        oyk_tui_center_modal(term_rows_, term_cols_, 21, 74, &mr);
        if (oyk_tui_hit_close_btn(&mr, r0, c0)) {
            dispatch_key(27);
            return;
        }
        const int top1 = mr.top + 1;
        const int left1 = mr.left + 1;
        if (row1 == top1 + 1) {
            int tx = left1 + 2;
            for (int t = 0; t < 8; ++t) {
                if (col1 >= tx && col1 < tx + TAB_LENS[t]) {
                    settings_tab_ = t;
                    render();
                    break;
                }
                tx += TAB_LENS[t] + 1;
            }
        } else if (row1 == top1 + 19) {
            if (col1 >= left1 + 49) dispatch_key(27);
            else if (col1 >= left1 + 35) dispatch_key('s');
        } else if (settings_tab_ == 7 && row1 == top1 + 3 && col1 >= left1 + 10 && col1 <= left1 + 34) {
            dispatch_key((col1 < left1 + 21) ? TMUX_KEY_LEFT : TMUX_KEY_RIGHT);
        } else {
            int row_base = top1 + ((settings_tab_ == 7) ? 5 : ((settings_tab_ == 3 || settings_tab_ == 4) ? 4 : 3));
            int scroll_off = (settings_tab_ == 7) ? theme_scroll_ : 0;
            int cnt = settings_tab_item_count(settings_tab_);
            if (row1 >= row_base && row1 < top1 + 16) {
                int idx = scroll_off + (row1 - row_base);
                if (idx >= 0 && idx < cnt) {
                    if (idx == settings_sel_[settings_tab_]) {
                        dispatch_key('\r');
                    } else {
                        settings_sel_[settings_tab_] = idx;
                        render();
                    }
                }
            }
        }
    } else if (view_ == TMUX_VIEW_BOOTLOADER && !bootloader_pending_) {
        OykTuiRect mr;
        oyk_tui_center_modal(term_rows_, term_cols_, 13, 72, &mr);
        if (oyk_tui_hit_close_btn(&mr, r0, c0)) {
            dispatch_key(27);
            return;
        }
        OykTuiButton btns[4];
        btns[0].label = "Save & Exit";  btns[0].enabled = 1;
        btns[1].label = "Discard";      btns[1].enabled = 1;
        btns[2].label = "Review Peers"; btns[2].enabled = 1;
        btns[3].label = "Cancel";       btns[3].enabled = 1;
        int hit = oyk_tui_hit_button_row(mr.top + 11, mr.left + 2, mr.w - 4, btns, 4, r0, c0);
        if (hit == 0) dispatch_key('s');
        else if (hit == 1) dispatch_key('d');
        else if (hit == 2) dispatch_key('r');
        else if (hit == 3) dispatch_key(27);
    }
}

OYK_FLASHMEM void TinyTmuxConsole::process_csi_final(char final_ch) {
    if ((final_ch == 'M' || final_ch == 'm') && esc_buf_[0] == '<') {
        if (final_ch == 'm') return; /* Ignore release */
        const char* p = esc_buf_ + 1;
        int btn = atoi(p);
        const char* s1 = strchr(p, ';');
        if (!s1) return;
        int col1 = atoi(s1 + 1);
        const char* s2 = strchr(s1 + 1, ';');
        if (!s2) return;
        int row1 = atoi(s2 + 1);
        int kind = -1;
        if (btn == 64 || (btn & 0x43) == 64) kind = 4;
        else if (btn == 65 || (btn & 0x43) == 65) kind = 5;
        else if ((btn & 64) == 0) {
            if ((btn & 3) == 0) kind = 0;
            else if ((btn & 3) == 2) kind = 2;
        }
        if (kind >= 0) handle_mouse_event(row1, col1, kind);
        return;
    }
    switch (final_ch) {
        case 'A': dispatch_key(TMUX_KEY_UP); break;
        case 'B': dispatch_key(TMUX_KEY_DOWN); break;
        case 'C': dispatch_key(TMUX_KEY_RIGHT); break;
        case 'D': dispatch_key(TMUX_KEY_LEFT); break;
        case 'Z': dispatch_key(TMUX_KEY_BTAB); break;
        case 'H': dispatch_key(TMUX_KEY_HOME); break;
        case 'F': dispatch_key(TMUX_KEY_END); break;
        case 'c':
            if (esc_buf_[0] == '?' || esc_buf_[0] == '>') {
                handle_da_csi();
            }
            break;
        case 'R':
            if (esc_buf_[0] != '?') {
                const char* semi = strchr(esc_buf_, ';');
                if (semi) {
                    int r = atoi(esc_buf_);
                    int c = atoi(semi + 1);
                    if (r >= 24 && c >= 80) {
                        if (r > 50) r = 50;
                        if (c > 132) c = 132;
                        if (r != term_rows_ || c != term_cols_) {
                            term_rows_ = r;
                            term_cols_ = c;
                            render();
                        }
                    }
                }
            }
            break;
        case '~': {
            int num = atoi(esc_buf_);
            if (num == 1 || num == 7) dispatch_key(TMUX_KEY_HOME);
            else if (num == 4 || num == 8) dispatch_key(TMUX_KEY_END);
            else if (num == 5) dispatch_key((view_ == TMUX_VIEW_LOCAL_TERM) ? TMUX_KEY_PGUP : TMUX_KEY_UP);
            else if (num == 6) dispatch_key((view_ == TMUX_VIEW_LOCAL_TERM) ? TMUX_KEY_PGDN : TMUX_KEY_DOWN);
            else if (num == 12) dispatch_key(TMUX_KEY_F2);
            else if (num == 20) dispatch_key(TMUX_KEY_F9);
            else if (num == 21) dispatch_key(TMUX_KEY_F10);
            break;
        }
        default:
            break;
    }
}

OYK_FLASHMEM void TinyTmuxConsole::process_ss3_final(char ch) {
    switch (ch) {
        case 'A': dispatch_key(TMUX_KEY_UP); break;
        case 'B': dispatch_key(TMUX_KEY_DOWN); break;
        case 'C': dispatch_key(TMUX_KEY_RIGHT); break;
        case 'D': dispatch_key(TMUX_KEY_LEFT); break;
        case 'H': dispatch_key(TMUX_KEY_HOME); break;
        case 'F': dispatch_key(TMUX_KEY_END); break;
        case 'Q': dispatch_key(TMUX_KEY_F2); break;
        case 'M': dispatch_key('\r'); break;
        default:
            break;
    }
}

void TinyTmuxConsole::tick(oyk_u32 now_ms) {
    now_ms_ = now_ms;
    if (!node_) return;
    if (esc_state_ == ESC_GOT_ESC) {
        if ((now_ms_ - esc_start_ms_) >= 40UL) {
            reset_escape();
            dispatch_key(27);
        }
    } else if (esc_state_ != ESC_IDLE) {
        if ((now_ms_ - esc_start_ms_) >= 100UL) {
            reset_escape();
        }
    }

    if (tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING) {
        int still_running = tinytmux_psram_marchc_step(now_ms_, 16384UL);
        if (view_ == TMUX_VIEW_PSRAM &&
            (!still_running || (now_ms_ - last_marchc_redraw_ms_) >= 120UL)) {
            last_marchc_redraw_ms_ = now_ms_;
            render();
        }
    }

    if (bootloader_pending_) {
        if (node_->disconnecting_count() == 0 || (now_ms_ - bootloader_start_ms_) >= 3000UL) {
            bootloader_pending_ = 0;
            node_->finish_shutdown();
            tinytmux_fs_unmount();
            char signoff_call[24], signoff_line[48];
            oyk_format_callsign(&node_->config().callsign, signoff_call, sizeof(signoff_call));
            sprintf(signoff_line, "\r\n73 de %s\r\n", signoff_call);
            write_str(signoff_line);
            write_str("\r\n\033[7m Node halted in HalfKay bootloader. Flash firmware or power-cycle board. \033[0m\r\n");
            tinytmux_board_enter_bootloader();
        }
    }
}

void TinyTmuxConsole::feed_input(const oyk_u8* data, size_t len) {
    if (!node_ || !data) return;
    for (size_t i = 0; i < len; ++i) {
        oyk_u8 b = data[i];
        switch (esc_state_) {
            case ESC_IDLE:
                if (b == 0x1BU) {
                    esc_state_ = ESC_GOT_ESC;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else if (b == 0x9BU) {
                    esc_state_ = ESC_CSI;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else {
                    dispatch_key(static_cast<int>(b));
                }
                break;

            case ESC_GOT_ESC:
                if (b == '[') {
                    esc_state_ = ESC_CSI;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                } else if (b == 'O') {
                    esc_state_ = ESC_SS3;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                } else if (b == 0x1BU) {
                    reset_escape();
                    dispatch_key(27);
                } else {
                    reset_escape();
                    dispatch_key(27);
                    dispatch_key(static_cast<int>(b));
                }
                break;

            case ESC_CSI:
                if (b == 0x1BU) {
                    esc_state_ = ESC_GOT_ESC;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else if (esc_len_ == 0 && b == '[') {
                    esc_state_ = ESC_CSI_LINUX;
                } else if (esc_len_ == 0 && b == 'M') {
                    esc_state_ = ESC_CSI_MOUSE_X10;
                    x10_len_ = 0;
                } else if (b >= 0x20U && b <= 0x3FU) {
                    if (esc_len_ < static_cast<int>(sizeof(esc_buf_) - 1)) {
                        esc_buf_[esc_len_++] = static_cast<char>(b);
                        esc_buf_[esc_len_] = '\0';
                    }
                } else if (b >= 0x40U && b <= 0x7EU) {
                    char final_ch = static_cast<char>(b);
                    esc_buf_[esc_len_] = '\0';
                    process_csi_final(final_ch);
                    reset_escape();
                } else {
                    reset_escape();
                }
                break;

            case ESC_CSI_LINUX:
                reset_escape();
                if (b == 'B') {
                    dispatch_key(TMUX_KEY_F2);
                }
                break;

            case ESC_SS3:
                if (b == 0x1BU) {
                    esc_state_ = ESC_GOT_ESC;
                    esc_len_ = 0;
                    esc_buf_[0] = '\0';
                    esc_start_ms_ = now_ms_;
                } else {
                    char ch = static_cast<char>(b);
                    reset_escape();
                    process_ss3_final(ch);
                }
                break;

            case ESC_CSI_MOUSE_X10:
                x10_buf_[x10_len_++] = b;
                if (x10_len_ >= 3) {
                    int cb = static_cast<int>(x10_buf_[0]) - 32;
                    int col1 = static_cast<int>(x10_buf_[1]) - 32;
                    int row1 = static_cast<int>(x10_buf_[2]) - 32;
                    reset_escape();
                    int kind = -1;
                    if (cb == 64 || (cb & 0x43) == 64) kind = 4;
                    else if (cb == 65 || (cb & 0x43) == 65) kind = 5;
                    else if ((cb & 3) == 0) kind = 0;
                    else if ((cb & 3) == 2) kind = 2;
                    if (kind >= 0) handle_mouse_event(row1, col1, kind);
                }
                break;
        }
    }
}
