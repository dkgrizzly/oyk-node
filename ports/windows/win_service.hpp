/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_WIN_SERVICE_HPP
#define OYK_WIN_SERVICE_HPP

#include <stddef.h>

#define OYK_WIN_SERVICE_NAME    "OYKNode"
#define OYK_WIN_SERVICE_DISPLAY "OYK Amateur Radio Packet Node"
#define OYK_WIN9X_RUNSERVICES_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\RunServices"

/*
 * Build the executable command line for service auto-start registration
 * (Win32 SCM binPath on Windows NT..11, or RunServices value on Windows 9x).
 * Quotes exe_path and optional config_path; appends service_flag (e.g.
 * "--service" on NT or "--headless" on 9x).
 * Returns 1 on success, 0 if invalid arguments or output buffer too small.
 */
int oyk_win_build_service_cmdline(const char* exe_path,
                                  const char* config_path,
                                  const char* service_flag,
                                  char* out,
                                  size_t out_sz);

#if defined(_WIN32) && !defined(OYK_WIN16)

typedef int  (*OykWinServiceInitFn)(int argc, char* argv[], char* err, size_t err_sz);
typedef void (*OykWinServiceLoopFn)(void);
typedef void (*OykWinServiceStopFn)(void);

/* Returns 1 on Windows NT..Windows 11, 0 on Windows 95/98/ME. */
int oyk_win_is_nt(void);

/* Writes message to attached parent console if present, otherwise MessageBoxA. */
void oyk_win_report_message(const char* title, const char* msg, int is_error);

/* Installs service auto-start for the host OS (SCM on NT, RunServices on 9x). */
int oyk_win_service_install(const char* config_path, char* out_msg, size_t out_sz);

/* Uninstalls service auto-start for the host OS (SCM on NT, RunServices on 9x). */
int oyk_win_service_uninstall(char* out_msg, size_t out_sz);

/* Calls KERNEL32.DLL RegisterServiceProcess dynamically on Windows 9x only. */
void oyk_win_register_9x_service(int enable);

/* Runs the Win32 SCM service dispatcher on Windows NT..11 (--service). */
int oyk_win_service_run_nt(OykWinServiceInitFn on_init,
                           OykWinServiceLoopFn on_loop,
                           OykWinServiceStopFn on_stop,
                           char* out_err,
                           size_t out_err_sz);

#endif /* _WIN32 && !OYK_WIN16 */

#endif /* OYK_WIN_SERVICE_HPP */
