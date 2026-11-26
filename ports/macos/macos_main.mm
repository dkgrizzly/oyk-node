/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Native AppKit Frontend for macOS (Mac OS X 10.4 Tiger PowerPC -> macOS 15+ Apple Silicon)
 *
 * Compiled non-ARC (-fno-objc-arc) so a single Objective-C++ source builds cleanly on:
 *   - Mac OS X 10.4-10.5 PowerPC (ppc) via Tiger/Leopard SDK + OykPortableNode
 *   - macOS Intel (x86_64) & Apple Silicon (arm64) via full C++20 NodeEngine
 * Driven by shared oyk_gui schemas/controllers and oyk_term line/hotkey logic.
 */

#import <Cocoa/Cocoa.h>

#include "macos_engine_bridge.hpp"
#include "oyk_gui.hpp"
#include "oyk_term.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NSColor* oyk_color_from_u32(oyk_u32 rgb) {
    CGFloat r = static_cast<CGFloat>((rgb >> 16) & 0xFFU) / 255.0;
    CGFloat g = static_cast<CGFloat>((rgb >> 8)  & 0xFFU) / 255.0;
    CGFloat b = static_cast<CGFloat>(rgb & 0xFFU) / 255.0;
    return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0];
}

@interface OykAppDelegate : NSObject {
    OykMacOsEngine* engine_;
    NSWindow* mainWindow_;
    NSTextField* bannerLabel_;
    NSTextView* consoleTextView_;
    NSToolbar* toolbar_;
    NSMenuItem* allowItem_;
    NSMenuItem* denyItem_;
    NSTimer* pollTimer_;
    NSWindow* modalWindow_;

    int uiTestMode_;
    NSString* captureDir_;
    OykGuiAddIfaceWizard wiz_;
    OykGuiThemeCtrl themeCtrl_;
    OykThemeConfig activeTheme_;
    OykConfig draftCfg_;
    OykTermEolParser termEol_;
}
- (void)startWithArgs:(const char*)cfgPath;
- (void)dispatchGuiAction:(int)act;
@end

@implementation OykAppDelegate

- (BOOL)detectDarkAppearance {
    if ([NSApp respondsToSelector:@selector(effectiveAppearance)]) {
        id appObj = [NSApp performSelector:@selector(effectiveAppearance)];
        if (appObj) {
            NSString* name = [appObj performSelector:@selector(name)];
            if (name && ([name rangeOfString:@"Dark"].location != NSNotFound)) {
                return YES;
            }
        }
    }
    return NO;
}

- (void)captureWindow:(NSWindow*)win asFlow:(NSString*)flow {
    if (!uiTestMode_ || !captureDir_ || !win) return;
    [win display];
    NSView* cv = [win contentView];
    NSRect bounds = [cv bounds];
    NSBitmapImageRep* rep = [cv bitmapImageRepForCachingDisplayInRect:bounds];
    if (!rep) return;
    [cv cacheDisplayInRect:bounds toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSPNGFileType properties:[NSDictionary dictionary]];
    if (png) {
        NSString* outPath = [NSString stringWithFormat:@"%@/macos_%@.png", captureDir_, flow];
        [png writeToFile:outPath atomically:YES];
    }
}

- (void)buildMenuBar {
    NSMenu* menubar = [[[NSMenu alloc] initWithTitle:@"MainMenu"] autorelease];

    /* 1. Application Menu ("OYK Node") */
    NSMenuItem* appItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* appMenu = [[[NSMenu alloc] initWithTitle:@"OYK Node"] autorelease];
    [appMenu addItemWithTitle:@"About OYK Packet Node..." action:@selector(showAbout:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Settings..." action:@selector(openSettings:) keyEquivalent:@","];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit OYK Node" action:@selector(requestExit:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [menubar addItem:appItem];

    /* 2. File Menu */
    NSMenuItem* fileItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* fileMenu = [[[NSMenu alloc] initWithTitle:@"File"] autorelease];
    [fileMenu addItemWithTitle:@"Open Config..." action:@selector(openConfigPanel:) keyEquivalent:@"o"];
    [fileMenu addItemWithTitle:@"Manage Files Area..." action:@selector(openFiles:) keyEquivalent:@""];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    [fileItem setSubmenu:fileMenu];
    [menubar addItem:fileItem];

    /* 3. Edit Menu */
    NSMenuItem* editItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* editMenu = [[[NSMenu alloc] initWithTitle:@"Edit"] autorelease];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    [editItem setSubmenu:editMenu];
    [menubar addItem:editItem];

    /* 4. Canonical Node Menu driven by oyk_gui_node_menu_item() */
    NSMenuItem* nodeTop = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* nodeMenu = [[[NSMenu alloc] initWithTitle:@"Node"] autorelease];
    int cnt = oyk_gui_node_menu_count();
    for (int i = 0; i < cnt; ++i) {
        const OykGuiMenuItem* gi = oyk_gui_node_menu_item(i);
        if (!gi) continue;
        if (gi->is_separator) {
            [nodeMenu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSString* title = [NSString stringWithUTF8String:gi->macos_label];
        NSString* key = @"";
        if (gi->action == OYK_ACT_LOCAL_TERM) key = @"t";
        else if (gi->action == OYK_ACT_SETTINGS) key = @",";
        else if (gi->action == OYK_ACT_EXIT) key = @"q";
        NSMenuItem* mi = [[[NSMenuItem alloc] initWithTitle:title
                                                     action:@selector(handleNodeMenuItem:)
                                              keyEquivalent:key] autorelease];
        [mi setTag:gi->action];
        [mi setTarget:self];
        if (gi->action == OYK_ACT_ALLOW_INCOMING) allowItem_ = mi;
        if (gi->action == OYK_ACT_DENY_INCOMING) denyItem_ = mi;
        [nodeMenu addItem:mi];
    }
    [nodeTop setSubmenu:nodeMenu];
    [menubar addItem:nodeTop];

    /* 5. Window & 6. Help */
    NSMenuItem* winItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* winMenu = [[[NSMenu alloc] initWithTitle:@"Window"] autorelease];
    [winMenu addItemWithTitle:@"Operator Console" action:@selector(focusConsole:) keyEquivalent:@"1"];
    [winItem setSubmenu:winMenu];
    [menubar addItem:winItem];
    [NSApp setWindowsMenu:winMenu];

    NSMenuItem* helpItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* helpMenu = [[[NSMenu alloc] initWithTitle:@"Help"] autorelease];
    [helpMenu addItemWithTitle:@"Terminal Hotkeys (^A ?)" action:@selector(showHotkeys:) keyEquivalent:@"?"];
    [helpItem setSubmenu:helpMenu];
    [menubar addItem:helpItem];

    [NSApp setMainMenu:menubar];
}

- (void)refreshConsole {
    if (!engine_ || !mainWindow_) return;
    OykMacOsSnapshot* snap = new OykMacOsSnapshot();
    engine_->get_snapshot(snap);

    char banner[160];
    oyk_gui_format_console_header(snap->node_callsign, snap->node_alias,
                                  snap->uptime_seconds, snap->active_connections,
                                  snap->accept_incoming, 0, banner, sizeof(banner));
    char full_banner[200];
    snprintf(full_banner, sizeof(full_banner), "%s   %s",
             banner, oyk_gui_format_incoming_pill(snap->accept_incoming, 0));
    [bannerLabel_ setStringValue:[NSString stringWithUTF8String:full_banner]];

    NSMutableString* body = [NSMutableString string];
    [body appendFormat:@"INTERFACES (%s  |  %s  |  %s  |  %s  |  %s)\n",
                       oyk_gui_table_column(OYK_TBL_IFACES, 0),
                       oyk_gui_table_column(OYK_TBL_IFACES, 1),
                       oyk_gui_table_column(OYK_TBL_IFACES, 2),
                       oyk_gui_table_column(OYK_TBL_IFACES, 3),
                       oyk_gui_table_column(OYK_TBL_IFACES, 4)];
    for (int i = 0; i < snap->iface_count; ++i) {
        [body appendFormat:@"  %-10s %-11s %-4d %-16s %u/%u\n",
                           snap->ifaces[i].id, snap->ifaces[i].type,
                           snap->ifaces[i].active_conns, snap->ifaces[i].status,
                           static_cast<unsigned int>(snap->ifaces[i].rx_frames),
                           static_cast<unsigned int>(snap->ifaces[i].tx_frames)];
    }
    [body appendFormat:@"\nLAST 5 NEW CALLSIGNS HEARD (%s | %s | %s | %s)\n",
                       oyk_gui_table_column(OYK_TBL_HEARD, 0),
                       oyk_gui_table_column(OYK_TBL_HEARD, 1),
                       oyk_gui_table_column(OYK_TBL_HEARD, 2),
                       oyk_gui_table_column(OYK_TBL_HEARD, 3)];
    for (int i = 0; i < snap->heard_count; ++i) {
        [body appendFormat:@"  %-2d %-10s %-10s %s\n",
                           i + 1, snap->heard[i].callsign,
                           snap->heard[i].iface_id, snap->heard[i].when_hms];
    }
    [body appendFormat:@"\nACTIVE CONNECTIONS (%s | %s | %s | %s | %s | %s)\n",
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 0),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 1),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 2),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 3),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 4),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 5)];
    if (snap->session_count == 0) {
        [body appendString:@"  (no active connections)\n"];
    } else {
        for (int i = 0; i < snap->session_count; ++i) {
            [body appendFormat:@"  %-4u %-10s %-9s %-6s %-10s %s\n",
                               static_cast<unsigned int>(snap->sessions[i].id),
                               snap->sessions[i].callsign, snap->sessions[i].iface_id,
                               snap->sessions[i].direction, snap->sessions[i].state,
                               snap->sessions[i].since_hms];
        }
    }
    [body appendString:@"\nNODE ACTIVITY LOG\n"];
    for (int i = 0; i < snap->log_count; ++i) {
        [body appendFormat:@"  %s\n", snap->logs[i]];
    }

    [consoleTextView_ setString:body];
    if (allowItem_ && denyItem_) {
        [allowItem_ setState:(snap->accept_incoming ? NSOnState : NSOffState)];
        [denyItem_ setState:(snap->accept_incoming ? NSOffState : NSOnState)];
    }
    delete snap;
}

- (NSWindow*)presentDialogScreen:(int)screenId bodyText:(NSString*)text height:(CGFloat)h {
    NSRect frame = NSMakeRect(160, 180, 660, h);
    NSWindow* w = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [w setTitle:[NSString stringWithUTF8String:oyk_gui_screen_title(screenId)]];
    [w setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];

    NSTextView* tv = [[[NSTextView alloc] initWithFrame:NSMakeRect(16, 48, 628, h - 68)] autorelease];
    [tv setEditable:NO];
    [tv setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [tv setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_FG])];
    [tv setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];
    [tv setString:text];
    [[w contentView] addSubview:tv];

    NSButton* closeBtn = [[[NSButton alloc] initWithFrame:NSMakeRect(556, 12, 88, 28)] autorelease];
    [closeBtn setTitle:@"Close"];
    [closeBtn setTarget:w];
    [closeBtn setAction:@selector(performClose:)];
    [[w contentView] addSubview:closeBtn];
    [w makeKeyAndOrderFront:nil];
    return w;
}

- (NSString*)buildScreenText:(int)scr {
    OykMacOsSnapshot* snap = new OykMacOsSnapshot();
    engine_->get_snapshot(snap);
    NSMutableString* s = [NSMutableString string];
    if (scr == OYK_SCR_SETTINGS) {
        [s appendString:@"Tabs: "];
        for (int t = 0; t < oyk_gui_settings_tab_count(); ++t) {
            [s appendFormat:@"[%d:%s] ", t + 1, oyk_gui_settings_tab_title(t)];
        }
        [s appendFormat:@"\n\nCallsign: %s\nAlias:    %s\nIncoming: %s\nEngine:   %s\n",
                        snap->node_callsign, snap->node_alias,
                        snap->accept_incoming ? "ALLOW" : "DENY",
                        engine_->uses_portable_core() ? "Portable C++98 (PowerPC)" : "C++20 NodeEngine"];
    } else if (scr == OYK_SCR_ADD_IFACE) {
        [s appendFormat:@"%s\n\nAvailable Drivers:\n", oyk_gui_wiz_step_prompt(&wiz_)];
        int cnt = oyk_gui_wiz_driver_count(wiz_.driver_mask);
        for (int i = 0; i < cnt; ++i) {
            [s appendFormat:@"  (%c) %-12s %s\n",
                            (i == wiz_.selected_driver_idx) ? '*' : ' ',
                            oyk_gui_wiz_driver_type(wiz_.driver_mask, i),
                            oyk_gui_wiz_driver_desc(wiz_.driver_mask, i)];
        }
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        [s appendFormat:@"Interface ID: axudp0\nType:         axudp\nBind Host:    127.0.0.1\nUDP Port:     10093\nPolicy:       Inbound & Outbound Allowed\n"];
    } else if (scr == OYK_SCR_PEERS) {
        [s appendFormat:@"%s | %s | %s | %s | %s\n\n",
                        oyk_gui_table_column(OYK_TBL_PEERS, 0),
                        oyk_gui_table_column(OYK_TBL_PEERS, 1),
                        oyk_gui_table_column(OYK_TBL_PEERS, 2),
                        oyk_gui_table_column(OYK_TBL_PEERS, 3),
                        oyk_gui_table_column(OYK_TBL_PEERS, 4)];
        const OykConfig& c = engine_->portable_config();
        for (int i = 0; i < c.peer_count; ++i) {
            [s appendFormat:@"  %-8s %-9s %-8s %s:%u  %s\n",
                            c.peers[i].id, c.peers[i].callsign, c.peers[i].interface_id,
                            c.peers[i].host, static_cast<unsigned int>(c.peers[i].port),
                            c.peers[i].auto_learned ? "AUTO" : "STATIC"];
        }
    } else if (scr == OYK_SCR_EDIT_PEER) {
        [s appendString:@"Peer ID:   remote0\nCallsign:  R3MOT-7\nInterface: axudp0\nHost/Port: 12.34.56.78:10093\n"];
    } else if (scr == OYK_SCR_TERMINATE) {
        const char* cols[4];
        oyk_gui_terminate_columns(cols, 4);
        [s appendFormat:@"%-5s %-12s %-10s %-12s\n\n", cols[0], cols[1], cols[2], cols[3]];
        for (int i = 0; i < snap->session_count; ++i) {
            [s appendFormat:@"%-5u %-12s %-10s %-12s\n",
                            static_cast<unsigned int>(snap->sessions[i].id),
                            snap->sessions[i].callsign, snap->sessions[i].iface_id,
                            snap->sessions[i].state];
        }
    } else if (scr == OYK_SCR_BAN) {
        [s appendFormat:@"%s | %s | %s | %s\n\n",
                        oyk_gui_table_column(OYK_TBL_BANS, 0),
                        oyk_gui_table_column(OYK_TBL_BANS, 1),
                        oyk_gui_table_column(OYK_TBL_BANS, 2),
                        oyk_gui_table_column(OYK_TBL_BANS, 4)];
        for (int i = 0; i < snap->ban_count; ++i) {
            [s appendFormat:@"%-10s %-16s %-16s %s\n",
                            snap->bans[i].callsign, snap->bans[i].banned_at,
                            snap->bans[i].lift_at, snap->bans[i].reason];
        }
    } else if (scr == OYK_SCR_EXIT_PEERS) {
        OykGuiExitPrompt ep;
        oyk_gui_build_exit_prompt(snap->auto_learned_count > 0 ? snap->auto_learned_count : 1,
                                  snap->active_connections, &ep);
        [s appendString:[NSString stringWithUTF8String:ep.body]];
    } else if (scr == OYK_SCR_THEME) {
        [s appendFormat:@"Active Theme: %s\n\n%-16s %-8s %-9s\n",
                        themeCtrl_.active_name,
                        oyk_gui_table_column(OYK_TBL_THEME, 0),
                        oyk_gui_table_column(OYK_TBL_THEME, 1),
                        oyk_gui_table_column(OYK_TBL_THEME, 2)];
        for (int r = 0; r < OYK_THEME_COLOR_COUNT; ++r) {
            char hx[12];
            oyk_theme_format_hex(activeTheme_.colors[r], hx, sizeof(hx));
            [s appendFormat:@"%-16s %-8s %-9s\n",
                            oyk_theme_role_label(r), oyk_theme_role_key(r), hx];
        }
    } else if (scr == OYK_SCR_TERMINAL) {
        [s appendString:@"Ctrl+A ? Help | Ctrl+A C Connect | Ctrl+A D Disconnect | Ctrl+A X Console\n\n"];
        [s appendString:@"*** Connected to N0BBS-7 via axudp0\nWelcome to OYK Packet BBS (YAPP-C).\nBBS> L\n"];
    } else if (scr == OYK_SCR_FILES) {
        [s appendFormat:@"PENDING INCOMING (%s | %s | %s)\n  W4ABC-7  PACKETS.TXT  2048\n\nAPPROVED (%s | %s)\n  README.TXT  1420\n",
                        oyk_gui_table_column(OYK_TBL_FILES_INCOMING, 0),
                        oyk_gui_table_column(OYK_TBL_FILES_INCOMING, 1),
                        oyk_gui_table_column(OYK_TBL_FILES_INCOMING, 2),
                        oyk_gui_table_column(OYK_TBL_FILES_APPROVED, 0),
                        oyk_gui_table_column(OYK_TBL_FILES_APPROVED, 1)];
    }
    delete snap;
    return s;
}

- (void)runUiCaptureTour {
    struct CapItem { int scr; const char* flow; };
    static const CapItem kFlows[12] = {
        { OYK_SCR_MAIN,       "main"       },
        { OYK_SCR_SETTINGS,   "settings"   },
        { OYK_SCR_ADD_IFACE,  "add_iface"  },
        { OYK_SCR_EDIT_IFACE, "edit_iface" },
        { OYK_SCR_PEERS,      "peers"      },
        { OYK_SCR_EDIT_PEER,  "edit_peer"  },
        { OYK_SCR_TERMINATE,  "terminate"  },
        { OYK_SCR_BAN,        "ban"        },
        { OYK_SCR_EXIT_PEERS, "exit"       },
        { OYK_SCR_THEME,      "theme"      },
        { OYK_SCR_TERMINAL,   "terminal"   },
        { OYK_SCR_FILES,      "files"      }
    };
    [self refreshConsole];
    [self captureWindow:mainWindow_ asFlow:@"main"];
    for (int i = 1; i < 12; ++i) {
        NSWindow* w = [self presentDialogScreen:kFlows[i].scr
                                       bodyText:[self buildScreenText:kFlows[i].scr]
                                         height:380.0];
        [self captureWindow:w asFlow:[NSString stringWithUTF8String:kFlows[i].flow]];
        [w close];
        [w release];
    }
    engine_->stop();
    [NSApp terminate:nil];
}

- (void)dispatchGuiAction:(int)act {
    if (act == OYK_ACT_ALLOW_INCOMING) { engine_->set_accept_incoming(1); [self refreshConsole]; }
    else if (act == OYK_ACT_DENY_INCOMING) { engine_->set_accept_incoming(0); [self refreshConsole]; }
    else if (act == OYK_ACT_EXIT) [self requestExit:nil];
    else {
        int scr = OYK_SCR_SETTINGS;
        if (act == OYK_ACT_LOCAL_TERM) scr = OYK_SCR_TERMINAL;
        else if (act == OYK_ACT_FILES) scr = OYK_SCR_FILES;
        else if (act == OYK_ACT_PEERS) scr = OYK_SCR_PEERS;
        else if (act == OYK_ACT_THEME) scr = OYK_SCR_THEME;
        else if (act == OYK_ACT_TERMINATE) scr = OYK_SCR_TERMINATE;
        else if (act == OYK_ACT_BAN) scr = OYK_SCR_BAN;
        else if (act == OYK_ACT_RIG) scr = OYK_SCR_RIG;
        NSWindow* w = [self presentDialogScreen:scr bodyText:[self buildScreenText:scr] height:380.0];
        (void)w;
    }
}

- (void)handleNodeMenuItem:(id)sender { [self dispatchGuiAction:static_cast<int>([sender tag])]; }
- (void)openSettings:(id)sender { (void)sender; [self dispatchGuiAction:OYK_ACT_SETTINGS]; }
- (void)openFiles:(id)sender { (void)sender; [self dispatchGuiAction:OYK_ACT_FILES]; }
- (void)focusConsole:(id)sender { (void)sender; [mainWindow_ makeKeyAndOrderFront:nil]; }
- (void)showAbout:(id)sender {
    (void)sender;
    NSRunAlertPanel(@"About OYK Packet Node",
                    @"OYK Amateur Radio Packet Node (macOS AppKit)\nShared oyk_gui + oyk_term middleware",
                    @"OK", nil, nil);
}
- (void)showHotkeys:(id)sender {
    (void)sender;
    NSMutableString* s = [NSMutableString string];
    for (int i = 0; i < oyk_term_help_count(); ++i) {
        const OykTermHelpEntry* e = oyk_term_help_entry(i);
        if (e) [s appendFormat:@"%-8s %s\n", e->key_label, e->action];
    }
    NSWindow* w = [self presentDialogScreen:OYK_SCR_TERMINAL bodyText:s height:340.0];
    (void)w;
}
- (void)openConfigPanel:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    if ([op runModal] == NSOKButton && [[op filenames] count] > 0) {
        NSString* p = [[op filenames] objectAtIndex:0];
        char err[96];
        engine_->init_and_start([p UTF8String], 0, err, sizeof(err));
        [self refreshConsole];
    }
}
- (void)requestExit:(id)sender {
    (void)sender;
    OykMacOsSnapshot* snap = new OykMacOsSnapshot();
    engine_->get_snapshot(snap);
    if (snap->auto_learned_count > 0) {
        OykGuiExitPrompt ep;
        oyk_gui_build_exit_prompt(snap->auto_learned_count, snap->active_connections, &ep);
        NSInteger r = NSRunAlertPanel([NSString stringWithUTF8String:ep.title],
                                      @"%@", @"Save & Quit", @"Discard", @"Cancel",
                                      [NSString stringWithUTF8String:ep.body]);
        if (r == NSAlertDefaultReturn) {
            char err[96];
            engine_->persist_auto_learned(err, sizeof(err));
        } else if (r == NSAlertOtherReturn) {
            delete snap;
            return;
        }
    }
    delete snap;
    engine_->stop();
    [NSApp terminate:nil];
}

- (void)onPollTimer:(NSTimer*)t {
    (void)t;
    engine_->poll();
    [self refreshConsole];
}

- (void)startWithArgs:(const char*)cfgPath {
    const char* tenv = getenv("OYK_UI_TEST_MODE");
    const char* cenv = getenv("OYK_UI_CAPTURE_DIR");
    uiTestMode_ = (tenv && strcmp(tenv, "1") == 0) ? 1 : 0;
    if (cenv && cenv[0]) captureDir_ = [[NSString stringWithUTF8String:cenv] retain];

    engine_ = new OykMacOsEngine();
    char err[128];
    err[0] = '\0';
    engine_->init_and_start(cfgPath, uiTestMode_, err, sizeof(err));
    draftCfg_ = engine_->portable_config();
    oyk_gui_wiz_init(&wiz_, &draftCfg_, OYK_DRV_ALL);
    oyk_gui_theme_init(&themeCtrl_, &draftCfg_, 1, [self detectDarkAppearance] ? 1 : 0);
    oyk_gui_theme_resolve(&themeCtrl_, &activeTheme_);
    oyk_term_eol_init(&termEol_, OYK_TERM_EOL_AUTO, 1);

    [self buildMenuBar];

    NSRect frame = NSMakeRect(120, 120, 760, 520);
    mainWindow_ = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [mainWindow_ setTitle:[NSString stringWithUTF8String:oyk_gui_screen_title(OYK_SCR_MAIN)]];
    [mainWindow_ setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];

    bannerLabel_ = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 484, 736, 24)];
    [bannerLabel_ setEditable:NO];
    [bannerLabel_ setBezeled:NO];
    [bannerLabel_ setDrawsBackground:YES];
    [bannerLabel_ setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_HEADER_BG])];
    [bannerLabel_ setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_HEADER_FG])];
    [bannerLabel_ setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [bannerLabel_ setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [[mainWindow_ contentView] addSubview:bannerLabel_];

    NSScrollView* sv = [[[NSScrollView alloc] initWithFrame:NSMakeRect(12, 12, 736, 464)] autorelease];
    [sv setHasVerticalScroller:YES];
    [sv setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    consoleTextView_ = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 720, 464)];
    [consoleTextView_ setEditable:NO];
    [consoleTextView_ setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [consoleTextView_ setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];
    [consoleTextView_ setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_FG])];
    [sv setDocumentView:consoleTextView_];
    [[mainWindow_ contentView] addSubview:sv];

    [mainWindow_ makeKeyAndOrderFront:nil];
    [self refreshConsole];

    if (uiTestMode_) {
        [self performSelector:@selector(runUiCaptureTour) withObject:nil afterDelay:0.15];
    } else {
        pollTimer_ = [[NSTimer scheduledTimerWithTimeInterval:0.25
                                                       target:self
                                                     selector:@selector(onPollTimer:)
                                                     userInfo:nil
                                                      repeats:YES] retain];
    }
}

@end

int main(int argc, const char* argv[]) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    const char* cfg = NULL;
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0) cfg = argv[i + 1];
    }
    OykAppDelegate* del = [[OykAppDelegate alloc] init];
    [NSApp setDelegate:del];
    [del startWithArgs:cfg];
    [NSApp run];
    [del release];
    [pool release];
    return 0;
}
