/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_MAC_CAPTURE_HPP
#define OYK_MAC_CAPTURE_HPP

/*
 * Classic Macintosh QuickDraw Framebuffer Capture & Test Shutdown Helper
 *
 * When UITEST.FLG exists on the launch/system volume, dumps exact 24-bit BMP
 * captures of either the 1-bit monochrome screenBits (512x342 on Mac Plus/SE)
 * or the Color QuickDraw GDHandle PixMap (640x480+ on Mac II/Quadra/PPC)
 * to short HFS filenames, flushes volumes, and calls ShutDwnPower().
 */

void oyk_mac_mount_all_drives(unsigned long max_wait_ticks);
int  oyk_mac_is_ui_test_mode();
int  oyk_mac_capture_screen_bmp(const char* hfs_filename, int has_color_qd);
void oyk_mac_finish_test_and_shutdown(int vrefnum);

#endif /* OYK_MAC_CAPTURE_HPP */
