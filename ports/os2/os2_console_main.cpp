/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "os2_net.hpp"
#include "oyk_gui.hpp"
#include "oyk_portable.hpp"
#include "oyk_tui.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __OS2__
#define INCL_VIO
#define INCL_KBD
#define INCL_MOU
#include <os2.h>
#endif

struct OykOs2ConsoleBuf {
    int  rows;
    int  cols;
    char ch[50][132];
    int  st[50][132];
};

static void os2_get_size(void* ctx, int* out_rows, int* out_cols) {
    const OykOs2ConsoleBuf* b = reinterpret_cast<const OykOs2ConsoleBuf*>(ctx);
    *out_rows = b->rows;
    *out_cols = b->cols;
}

static void os2_clear(void* ctx, int style) {
    OykOs2ConsoleBuf* b = reinterpret_cast<OykOs2ConsoleBuf*>(ctx);
    for (int r = 0; r < b->rows; ++r) {
        for (int c = 0; c < b->cols; ++c) {
            b->ch[r][c] = ' ';
            b->st[r][c] = style;
        }
    }
}

static void os2_put_text(void* ctx, int row, int col, int max_w, int style, const char* text) {
    OykOs2ConsoleBuf* b = reinterpret_cast<OykOs2ConsoleBuf*>(ctx);
    if (!text || row < 0 || row >= b->rows || max_w <= 0) return;
    for (int i = 0; i < max_w && text[i] != '\0'; ++i) {
        int c = col + i;
        if (c >= 0 && c < b->cols) {
            b->ch[row][c] = text[i];
            b->st[row][c] = style;
        }
    }
}

static void os2_put_box(void* ctx, int top, int left, int h, int w,
                        int border_style, int fill_style, int fill_interior) {
    OykOs2ConsoleBuf* b = reinterpret_cast<OykOs2ConsoleBuf*>(ctx);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            int rr = top + r;
            int cc = left + c;
            if (rr < 0 || rr >= b->rows || cc < 0 || cc >= b->cols) continue;
            if (r == 0 || r == h - 1) {
                b->ch[rr][cc] = (c == 0 || c == w - 1) ? '+' : '-';
                b->st[rr][cc] = border_style;
            } else if (c == 0 || c == w - 1) {
                b->ch[rr][cc] = '|';
                b->st[rr][cc] = border_style;
            } else if (fill_interior) {
                b->ch[rr][cc] = ' ';
                b->st[rr][cc] = fill_style;
            }
        }
    }
}

static void os2_put_hdiv(void* ctx, int row, int left, int w, int style) {
    OykOs2ConsoleBuf* b = reinterpret_cast<OykOs2ConsoleBuf*>(ctx);
    if (row < 0 || row >= b->rows) return;
    for (int c = 0; c < w; ++c) {
        int cc = left + c;
        if (cc >= 0 && cc < b->cols) {
            b->ch[row][cc] = (c == 0 || c == w - 1) ? '+' : '-';
            b->st[row][cc] = style;
        }
    }
}

static void os2_put_shadow(void* ctx, int top, int left, int h, int w) {
    OykOs2ConsoleBuf* b = reinterpret_cast<OykOs2ConsoleBuf*>(ctx);
    for (int r = 1; r <= h; ++r) {
        int rr = top + r;
        int cc = left + w;
        if (rr >= 0 && rr < b->rows && cc >= 0 && cc < b->cols) {
            b->st[rr][cc] = OYK_TUI_STYLE_DIM;
        }
    }
    for (int c = 2; c <= w; ++c) {
        int rr = top + h;
        int cc = left + c;
        if (rr >= 0 && rr < b->rows && cc >= 0 && cc < b->cols) {
            b->st[rr][cc] = OYK_TUI_STYLE_DIM;
        }
    }
}

static void os2_present(void* ctx) {
#ifdef __OS2__
    const OykOs2ConsoleBuf* b = reinterpret_cast<const OykOs2ConsoleBuf*>(ctx);
    for (int r = 0; r < b->rows; ++r) {
        BYTE row_cells[132 * 2];
        for (int c = 0; c < b->cols; ++c) {
            row_cells[c * 2] = (BYTE)b->ch[r][c];
            row_cells[c * 2 + 1] = (b->st[r][c] == OYK_TUI_STYLE_HEADER ||
                                    b->st[r][c] == OYK_TUI_STYLE_MENU_SEL)
                                       ? (BYTE)0x70
                                       : (BYTE)0x1F;
        }
        VioWrtCellStr((PCH)row_cells, (USHORT)(b->cols * 2), (USHORT)r, 0, 0);
    }
#else
    (void)ctx;
#endif
}

static int buf_contains(const OykOs2ConsoleBuf* b, const char* needle) {
    size_t nlen = strlen(needle);
    for (int r = 0; r < b->rows; ++r) {
        for (int c = 0; c + (int)nlen <= b->cols; ++c) {
            if (memcmp(&b->ch[r][c], needle, nlen) == 0) return 1;
        }
    }
    return 0;
}

static void render_os2_console_screen(const OykTuiBackend* bk, const OykPortableNode* node,
                                      int screen_id, int settings_tab, int wiz_step) {
    int rows = 25;
    int cols = 80;
    bk->get_size(bk->ctx, &rows, &cols);
    const OykConfig& cfg = node->config();
    char call_s[16];
    char alias_s[16];
    oyk_format_callsign(&cfg.callsign, call_s, sizeof(call_s));
    oyk_format_callsign(&cfg.alias, alias_s, sizeof(alias_s));

    if (screen_id == OYK_SCR_TERMINAL || screen_id == OYK_SCR_TERM_HELP) {
        OykTuiTermLayout tl;
        oyk_tui_compute_term_layout(rows, cols, &tl);
        bk->clear(bk->ctx, OYK_TUI_STYLE_BG);
        oyk_tui_draw_menu_bar(bk, cols, 1, "Tab/F9:Console");
        bk->put_text(bk->ctx, tl.body_top, 1, cols - 2, OYK_TUI_STYLE_NORMAL,
                     "OS/2 Local Packet Terminal Ready");
        oyk_tui_draw_status_bar(bk, tl.hotkey_row, cols,
                                "Ctrl-A Z:Help | Tab/F9:Console | [EOL:LF] [Echo:ON]");
        bk->put_text(bk->ctx, tl.input_row, 1, cols - 2, OYK_TUI_STYLE_DLG_INPUT, "> ");
        if (screen_id == OYK_SCR_TERM_HELP) {
            oyk_tui_draw_term_help_modal(bk, rows, cols);
        }
        bk->present(bk->ctx);
        return;
    }

    OykTuiConsoleLayout cl;
    oyk_tui_compute_console_layout(rows, cols, &cl);
    bk->clear(bk->ctx, OYK_TUI_STYLE_BG);
    oyk_tui_draw_menu_bar(bk, cols, 0, "Tab/F9:Term  Ctrl-N:Menu");
    {
        char hdr[96];
        oyk_gui_format_console_header(call_s, alias_s, node->uptime_seconds(),
                                      node->total_active_connections(),
                                      node->accept_incoming(), 0, hdr, sizeof(hdr));
        oyk_tui_draw_status_banner(bk, cols, hdr, node->accept_incoming(), 0);
    }
    bk->put_box(bk->ctx, cl.iface_box.top, cl.iface_box.left, cl.iface_box.h, cl.iface_box.w,
                OYK_TUI_STYLE_BORDER, OYK_TUI_STYLE_BG, 1);
    bk->put_text(bk->ctx, cl.iface_box.top, cl.iface_box.left + 2, 14, OYK_TUI_STYLE_TITLE,
                 " Interfaces ");
    bk->put_box(bk->ctx, cl.heard_box.top, cl.heard_box.left, cl.heard_box.h, cl.heard_box.w,
                OYK_TUI_STYLE_BORDER, OYK_TUI_STYLE_BG, 1);
    bk->put_text(bk->ctx, cl.heard_box.top, cl.heard_box.left + 2, 30, OYK_TUI_STYLE_TITLE,
                 " Last 5 New Callsigns Heard ");
    bk->put_box(bk->ctx, cl.conn_box.top, cl.conn_box.left, cl.conn_box.h, cl.conn_box.w,
                OYK_TUI_STYLE_BORDER, OYK_TUI_STYLE_BG, 1);
    bk->put_text(bk->ctx, cl.conn_box.top, cl.conn_box.left + 2, 22, OYK_TUI_STYLE_TITLE,
                 " Active Connections ");
    bk->put_box(bk->ctx, cl.log_box.top, cl.log_box.left, cl.log_box.h, cl.log_box.w,
                OYK_TUI_STYLE_BORDER, OYK_TUI_STYLE_BG, 1);
    bk->put_text(bk->ctx, cl.log_box.top, cl.log_box.left + 2, 21, OYK_TUI_STYLE_TITLE,
                 " Node Activity Log ");
    oyk_tui_draw_status_bar(bk, cl.status_row, cols, "OS/2 2.0+ VIO | Ctrl-N:Menu  Tab/F9:Term  Esc:Close");

    if (screen_id == OYK_SCR_MENU) {
        OykTuiRect mr = { 1, 1, 12, 30 };
        static const char* const items[4] = { "Settings", "Terminal", "Files", "About" };
        static const int irows[4] = { 2, 3, 4, 5 };
        static const int srows[1] = { 6 };
        oyk_tui_draw_dropdown_menu(bk, &mr, items, irows, 4, srows, 1, 0);
    } else if (screen_id == OYK_SCR_ABOUT) {
        oyk_tui_draw_about_modal(bk, rows, cols, "OS/2 2.0+ 32-bit VIO Console");
    } else if (screen_id == OYK_SCR_FILES) {
        OykGuiFileMgr fm;
        oyk_gui_filemgr_init(&fm, "incoming", "files");
        oyk_tui_draw_dual_pane_modal(bk, rows, cols, &fm, 0, "File Manager", "", 0);
    } else if (screen_id != OYK_SCR_MAIN) {
        OykTuiRect r;
        oyk_tui_center_modal(rows, cols, 16, 70, &r);
        oyk_tui_draw_modal(bk, &r,
                           (screen_id == OYK_SCR_THEME) ? "Node Settings" : oyk_gui_screen_title(screen_id),
                           1);
        if (screen_id == OYK_SCR_SETTINGS || screen_id == OYK_SCR_PEERS ||
            screen_id == OYK_SCR_RIG || screen_id == OYK_SCR_THEME) {
            static const char* const tabs[8] = {
                "1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn", "8:Thm"
            };
            const int active_tab = (screen_id == OYK_SCR_THEME) ? 7 : settings_tab;
            oyk_tui_draw_tabs(bk, r.top + 1, r.left + 2, r.w - 4, tabs, 8, active_tab);
            bk->put_hdiv(bk->ctx, r.top + 2, r.left, r.w, OYK_TUI_STYLE_DLG_BORDER);
            if (active_tab == 7) {
                bk->put_text(bk->ctx, r.top + 4, r.left + 2, r.w - 4, OYK_TUI_STYLE_DLG_FILL,
                             "Role / Key / Color / Preview");
            }
        } else if (screen_id == OYK_SCR_ADD_IFACE) {
            char sb[32];
            sprintf(sb, "Step %d: Wizard", wiz_step + 1);
            bk->put_text(bk->ctx, r.top + 2, r.left + 2, r.w - 4, OYK_TUI_STYLE_DLG_FILL, sb);
        } else if (screen_id == OYK_SCR_TERMINATE) {
            bk->put_text(bk->ctx, r.top + 2, r.left + 2, r.w - 4, OYK_TUI_STYLE_LIST_HDR,
                         "ID    Callsign    Port       State");
        }
        static const OykTuiButton btns[2] = { { "OK", 1 }, { "Cancel", 1 } };
        oyk_tui_draw_button_row(bk, r.top + r.h - 2, r.left + 2, r.w - 4, btns, 2, 0, NULL);
        oyk_tui_draw_modal_footer(bk, &r, "Ready", "Esc:Close");
    }
    bk->present(bk->ctx);
}

static int os2_console_handle_key(int key, int* screen_id, int* settings_tab) {
    if (!screen_id || !settings_tab) return -1;
    const int cur = *screen_id;
    const int ch = (key >= 'A' && key <= 'Z') ? (key + ('a' - 'A')) : key;
    if (cur == OYK_SCR_MAIN) {
        if (key == 0x0E || key == 0x10A) { *screen_id = OYK_SCR_MENU; return 0; }
        if (key == '\t' || key == 0x109) { *screen_id = OYK_SCR_TERMINAL; return 0; }
        return -1;
    }
    if (cur == OYK_SCR_MENU) {
        if (key == 27 || key == 0x0E || key == 0x10A) { *screen_id = OYK_SCR_MAIN; return 0; }
        if (ch == 's') { *screen_id = OYK_SCR_SETTINGS; *settings_tab = 0; return 0; }
        if (ch == 't') { *screen_id = OYK_SCR_TERMINAL; return 0; }
        if (ch == 'f') { *screen_id = OYK_SCR_FILES; return 0; }
        if (ch == '?') { *screen_id = OYK_SCR_ABOUT; return 0; }
        if (ch == 'q' || ch == 'x') { *screen_id = OYK_SCR_EXIT_PEERS; return 0; }
        return -1;
    }
    if (cur == OYK_SCR_TERMINAL) {
        if (key == '\t' || key == 0x109 || key == 27) { *screen_id = OYK_SCR_MAIN; return 0; }
        return -1;
    }
    if (cur == OYK_SCR_TERM_HELP) {
        if (key == 27) { *screen_id = OYK_SCR_TERMINAL; return 0; }
        return -1;
    }
    if (key == 27) { *screen_id = OYK_SCR_MAIN; return 0; }
    if (key == 0x0E || key == 0x10A || key == 0x109) return -1;
    if (cur == OYK_SCR_SETTINGS || cur == OYK_SCR_PEERS || cur == OYK_SCR_RIG || cur == OYK_SCR_THEME) {
        if (key == '\t') {
            *settings_tab = (*settings_tab + 1) % 8;
            *screen_id = (*settings_tab == 7) ? OYK_SCR_THEME : OYK_SCR_SETTINGS;
            return 0;
        }
        if (key >= '1' && key <= '8') {
            *settings_tab = key - '1';
            *screen_id = (*settings_tab == 7) ? OYK_SCR_THEME : OYK_SCR_SETTINGS;
            return 0;
        }
    }
    return -1;
}

static int run_os2_console_selftest() {
    OykOs2ConsoleBuf* buf = new OykOs2ConsoleBuf;
    buf->rows = 25;
    buf->cols = 80;
    OykTuiBackend bk;
    bk.ctx = buf;
    bk.get_size = os2_get_size;
    bk.clear = os2_clear;
    bk.put_text = os2_put_text;
    bk.put_box = os2_put_box;
    bk.put_hdiv = os2_put_hdiv;
    bk.put_shadow = os2_put_shadow;
    bk.present = os2_present;

    OykPortableNode* node = new OykPortableNode();
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    char err[64];
    int fail = 0;
    if (node->init(cfg, NULL, NULL, err, sizeof(err)) != 1) {
        ++fail;
    } else {
        static const int screens[23] = {
            OYK_SCR_MAIN, OYK_SCR_MENU,
            OYK_SCR_SETTINGS, OYK_SCR_SETTINGS, OYK_SCR_SETTINGS, OYK_SCR_SETTINGS,
            OYK_SCR_PEERS, OYK_SCR_RIG, OYK_SCR_SETTINGS,
            OYK_SCR_ADD_IFACE, OYK_SCR_ADD_IFACE, OYK_SCR_ADD_IFACE, OYK_SCR_ADD_IFACE,
            OYK_SCR_EDIT_IFACE, OYK_SCR_EDIT_PEER, OYK_SCR_TERMINATE, OYK_SCR_BAN,
            OYK_SCR_EXIT_PEERS, OYK_SCR_THEME, OYK_SCR_TERMINAL, OYK_SCR_TERM_HELP,
            OYK_SCR_FILES, OYK_SCR_ABOUT
        };
        static const int tabs[23] = {
            0, 0, 0, 1, 2, 3, 4, 5, 6,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        static const int steps[23] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        for (int i = 0; i < 23; ++i) {
            render_os2_console_screen(&bk, node, screens[i], tabs[i], steps[i]);
            if (screens[i] == OYK_SCR_ABOUT && !buf_contains(buf, "O Y K")) ++fail;
            if (screens[i] == OYK_SCR_TERMINAL && !buf_contains(buf, "Ctrl-A Z:Help")) ++fail;
            if (screens[i] == OYK_SCR_TERMINATE && !buf_contains(buf, "Callsign")) ++fail;
        }
        /* Hotkey & modal preemption checks */
        {
            int sid = OYK_SCR_MAIN;
            int stab = 0;
            if (os2_console_handle_key('s', &sid, &stab) == 0 ||
                os2_console_handle_key('m', &sid, &stab) == 0 ||
                os2_console_handle_key('q', &sid, &stab) == 0 ||
                sid != OYK_SCR_MAIN) ++fail;
            if (os2_console_handle_key(0x0E, &sid, &stab) != 0 || sid != OYK_SCR_MENU) ++fail;
            if (os2_console_handle_key('s', &sid, &stab) != 0 || sid != OYK_SCR_SETTINGS) ++fail;
            if (os2_console_handle_key(0x0E, &sid, &stab) == 0 ||
                os2_console_handle_key(0x10A, &sid, &stab) == 0 ||
                os2_console_handle_key(0x109, &sid, &stab) == 0 ||
                sid != OYK_SCR_SETTINGS) ++fail;
            if (os2_console_handle_key('8', &sid, &stab) != 0 || sid != OYK_SCR_THEME || stab != 7) ++fail;
            if (os2_console_handle_key(27, &sid, &stab) != 0 || sid != OYK_SCR_MAIN) ++fail;
        }
        node->shutdown();
    }
    delete cfg;
    delete node;
    delete buf;
    return fail;
}

int main(int argc, char** argv) {
    int unit_test = 0;
    int headless = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--unit-test") == 0) unit_test = 1;
        else if (strcmp(argv[i], "--headless") == 0) headless = 1;
    }
    if (unit_test) {
        int rc = run_os2_console_selftest();
        if (rc == 0) printf("OYK OS/2 2.0+ VIO console self-test PASSED\n");
        return rc == 0 ? 0 : 1;
    }

    OykOs2NetState net;
    oyk_os2_net_init(&net);
    OykPortableNode* node = new OykPortableNode();
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    char err[64];
    if (node->init(cfg, NULL, NULL, err, sizeof(err)) == 1) {
        if (!headless) {
            OykOs2ConsoleBuf* buf = new OykOs2ConsoleBuf;
            buf->rows = 25;
            buf->cols = 80;
#ifdef __OS2__
            VIOMODEINFO vm;
            memset(&vm, 0, sizeof(vm));
            vm.cb = sizeof(vm);
            if (VioGetMode(&vm, 0) == 0 && vm.row >= 24 && vm.row <= 50 && vm.col >= 80 && vm.col <= 132) {
                buf->rows = vm.row;
                buf->cols = vm.col;
            }
#endif
            OykTuiBackend bk;
            bk.ctx = buf;
            bk.get_size = os2_get_size;
            bk.clear = os2_clear;
            bk.put_text = os2_put_text;
            bk.put_box = os2_put_box;
            bk.put_hdiv = os2_put_hdiv;
            bk.put_shadow = os2_put_shadow;
            bk.present = os2_present;
            render_os2_console_screen(&bk, node, OYK_SCR_MAIN, 0, 0);
            delete buf;
        }
        node->shutdown();
    }
    delete cfg;
    delete node;
    oyk_os2_net_shutdown(&net);
    return 0;
}
