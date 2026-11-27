/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ATARI_CAPTURE_HPP
#define OYK_ATARI_CAPTURE_HPP

int  oyk_atari_is_ui_test_mode();
int  oyk_atari_capture_screen_bmp(const char* path, short vdi_handle, int w, int h, int colors);
void oyk_atari_finish_test();

#endif /* OYK_ATARI_CAPTURE_HPP */
