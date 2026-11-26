/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_GSOS_CAPTURE_H
#define OYK_GSOS_CAPTURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int  oyk_gsos_is_ui_test_mode(void);
int  oyk_gsos_write_text_file(const char *gsos_path, const char *data, unsigned long len);
int  oyk_gsos_capture_screen_bmp(const char *gsos_path);
void oyk_gsos_finish_test(void);

#ifdef __cplusplus
}
#endif

#endif /* OYK_GSOS_CAPTURE_H */
