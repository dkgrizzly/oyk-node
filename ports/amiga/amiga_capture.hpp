/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_AMIGA_CAPTURE_HPP
#define OYK_AMIGA_CAPTURE_HPP

struct Screen;

int  oyk_amiga_is_ui_test_mode();
int  oyk_amiga_capture_screen_bmp(const char* path, struct Screen* scr);
void oyk_amiga_finish_test();

#endif /* OYK_AMIGA_CAPTURE_HPP */
