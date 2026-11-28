/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * 16-bit MS-DOS Unit Test Executable (testdos.exe)
 *
 * Built with OpenWatcom v2 (-0 -ml -os -bt=dos) and executed headlessly inside
 * qemu-system-i386 via ports/dos/run_qemu_tests.sh.
 *
 * Verifies:
 *   1. 16-bit Large-model integer widths & DOS CRLF default line ending
 *   2. Optional LIM EMS 4.0 (INT 67h) detection, allocation, and page read/write
 *   3. DOS 8.3 daily log rotation across a day rollover
 *   4. Full portable AX.25 / CRC-16 / KISS / config EOL / ban & appeal suite
 */

#include "../compat/oyk_portable.hpp"
#include "dos_keymap.hpp"
#include "ems.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int oyk_run_portable_tests();

static int g_failures = 0;

static void check_cond(int cond, const char* label, int line) {
    if (!cond) {
        printf("[FAIL] %s (line %d)\n", label, line);
        ++g_failures;
    }
}

#define CHECK(cond, label) check_cond((cond) ? 1 : 0, (label), __LINE__)

static void test_dos_16bit_types_and_eol() {
    CHECK(sizeof(oyk_u8) == 1, "sizeof(oyk_u8) == 1");
    CHECK(sizeof(oyk_u16) == 2, "sizeof(oyk_u16) == 2");
    CHECK(sizeof(oyk_u32) == 4, "sizeof(oyk_u32) == 4");
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    CHECK(sizeof(int) == 2, "16-bit DOS sizeof(int) == 2");
    CHECK(oyk_platform_default_eol() == OYK_EOL_CRLF, "DOS default EOL is CRLF");
#endif
    printf("[PASS] test_dos_16bit_types_and_eol (int=%u, u32=%u)\n",
           static_cast<unsigned int>(sizeof(int)),
           static_cast<unsigned int>(sizeof(oyk_u32)));
}

static void test_dos_ems4() {
    OykEms4 ems;
    if (ems.init(2)) {
        CHECK(ems.available() == 1, "EMS available");
        CHECK(ems.version_bcd() >= 0x40, "EMS version >= 4.0");
        CHECK(ems.page_frame_seg() != 0, "EMS page frame non-zero");

        const char pat0[] = "OYK-EMS-PAGE0-TEST-DATA-8088";
        const char pat1[] = "OYK-EMS-PAGE1-SECOND-BUFFER!";
        char rbuf0[64];
        char rbuf1[64];
        memset(rbuf0, 0, sizeof(rbuf0));
        memset(rbuf1, 0, sizeof(rbuf1));

        CHECK(ems.write_page(0, 0, pat0, static_cast<oyk_u16>(strlen(pat0) + 1)), "EMS write page 0");
        CHECK(ems.write_page(1, 128, pat1, static_cast<oyk_u16>(strlen(pat1) + 1)), "EMS write page 1");
        CHECK(ems.read_page(0, 0, rbuf0, static_cast<oyk_u16>(strlen(pat0) + 1)), "EMS read page 0");
        CHECK(ems.read_page(1, 128, rbuf1, static_cast<oyk_u16>(strlen(pat1) + 1)), "EMS read page 1");
        CHECK(strcmp(rbuf0, pat0) == 0, "EMS page 0 data integrity");
        CHECK(strcmp(rbuf1, pat1) == 0, "EMS page 1 data integrity");

        ems.shutdown();
        CHECK(ems.available() == 0, "EMS released cleanly");
        printf("[PASS] test_dos_ems4 (EMS %X.%X frame=0x%04X)\n",
               static_cast<unsigned int>((ems.version_bcd() >> 4) & 0x0F),
               static_cast<unsigned int>(ems.version_bcd() & 0x0F),
               static_cast<unsigned int>(ems.page_frame_seg()));
    } else {
        CHECK(ems.available() == 0, "No EMS driver -> graceful fallback");
        printf("[PASS] test_dos_ems4 (no EMS driver present; fallback verified)\n");
    }
}

static void test_dos_daily_rotation() {
    OykConfig* cfg = new OykConfig;
    oyk_config_init_default(cfg);
    strcpy(cfg->log_path, "drot");
    strcpy(cfg->banlist_path, "drotban.cfg");

    OykPortableNode* node = new OykPortableNode;
    char err[64];
    /* Day 1: 2026-11-24 12:00:00 */
    node->set_now_override(1795521600L);
    CHECK(node->init(cfg, NULL, NULL, err, sizeof(err)), "DOS node init for rotation test");
    node->log_message(OYK_LOG_INFO, "Day1 DOS log line");

    /* Day 2: +86400 seconds -> triggers daily rotation */
    node->set_now_override(1795521600L + 86400L);
    node->log_message(OYK_LOG_INFO, "Day2 DOS log line");
    node->shutdown();

    delete node;
    delete cfg;

    FILE* fp = fopen("drot\\node.log", "rb");
    if (!fp) fp = fopen("drot/node.log", "rb");
    CHECK(fp != NULL, "Active node.log exists after rotation");
    if (fp) {
        char buf[256];
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        CHECK(strstr(buf, "Day2 DOS log line") != NULL, "Active node.log has Day 2 entry");
        CHECK(strstr(buf, "Day1 DOS log line") == NULL, "Active node.log rotated out Day 1");
    }

    remove("drot\\node.log");
    remove("drot\\1124nod.log");
    remove("drotban.cfg");
    printf("[PASS] test_dos_daily_rotation\n");
}

static void test_dos_keymap_discipline() {
    /* Closed-menu single-letter hotkeys must be rejected */
    CHECK(oyk_dos_map_main_key('s', 0, 0) == OYK_DACT_NONE, "Closed menu ignores 's'");
    CHECK(oyk_dos_map_main_key('m', 0, 0) == OYK_DACT_NONE, "Closed menu ignores 'm'");
    CHECK(oyk_dos_map_main_key('q', 0, 0) == OYK_DACT_NONE, "Closed menu ignores 'q'");
    CHECK(oyk_dos_map_main_key('?', 0, 0) == OYK_DACT_NONE, "Closed menu ignores '?'");
    CHECK(oyk_dos_map_main_key(0x0E, 0, 0) == OYK_DACT_OPEN_MENU, "Ctrl-N opens menu");
    CHECK(oyk_dos_map_main_key(0, 68, 0) == OYK_DACT_OPEN_MENU, "F10 opens menu");
    CHECK(oyk_dos_map_main_key('\t', 0, 0) == OYK_DACT_TERM, "Tab opens terminal");
    /* Modal preemption & Esc dismissal */
    CHECK(oyk_dos_map_main_key(0x0E, 0, 1) == OYK_DACT_NONE, "Modal preempts Ctrl-N");
    CHECK(oyk_dos_map_main_key(0, 68, 1) == OYK_DACT_NONE, "Modal preempts F10");
    CHECK(oyk_dos_map_main_key('\t', 0, 1) == OYK_DACT_NONE, "Modal preempts Tab");
    CHECK(oyk_dos_map_main_key(0, 67, 1) == OYK_DACT_NONE, "Modal preempts F9");
    CHECK(oyk_dos_map_main_key(27, 0, 1) == OYK_DACT_CLOSE, "Esc dismisses modal");
    /* Menu deduplication */
    CHECK(oyk_dos_map_menu_key('p', 0) == OYK_DACT_NONE, "Menu drops Peers");
    CHECK(oyk_dos_map_menu_key('h', 0) == OYK_DACT_NONE, "Menu drops Theme");
    CHECK(oyk_dos_map_menu_key('?', 0) == OYK_DACT_ABOUT, "Menu maps ? to About");
    printf("[PASS] test_dos_keymap_discipline\n");
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== OYK DOS TESTS START ===\n");
    test_dos_16bit_types_and_eol();
    test_dos_ems4();
    test_dos_daily_rotation();
    test_dos_keymap_discipline();
    oyk_run_portable_tests();

    if (g_failures == 0) {
        printf("=== OYK DOS TESTS PASS ===\n");
        return 0;
    }
    printf("=== OYK DOS TESTS FAIL (%d failures) ===\n", g_failures);
    return 1;
}
