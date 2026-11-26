/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "win_service.hpp"

#include <stdio.h>
#include <string.h>

int oyk_win_build_service_cmdline(const char* exe_path,
                                  const char* config_path,
                                  const char* service_flag,
                                  char* out,
                                  size_t out_sz) {
    if (!exe_path || !exe_path[0] || !service_flag || !service_flag[0] || !out || out_sz == 0) {
        return 0;
    }
    size_t elen = strlen(exe_path);
    size_t flen = strlen(service_flag);
    size_t clen = (config_path && config_path[0]) ? strlen(config_path) : 0;
    static const char kCfgPrefix[] = " --config \"";
    size_t plen = sizeof(kCfgPrefix) - 1;
    size_t need = 1 + elen + 1 + 1 + flen + (clen > 0 ? (plen + clen + 1) : 0) + 1;
    if (need > out_sz) {
        return 0;
    }

    size_t pos = 0;
    out[pos++] = '"';
    memcpy(out + pos, exe_path, elen);
    pos += elen;
    out[pos++] = '"';
    out[pos++] = ' ';
    memcpy(out + pos, service_flag, flen);
    pos += flen;
    if (clen > 0) {
        memcpy(out + pos, kCfgPrefix, plen);
        pos += plen;
        memcpy(out + pos, config_path, clen);
        pos += clen;
        out[pos++] = '"';
    }
    out[pos] = '\0';
    return 1;
}

#if defined(_WIN32) && !defined(OYK_WIN16)

#include <windows.h>
#include <winsvc.h>

#ifndef ATTACH_PARENT_PROCESS
#define ATTACH_PARENT_PROCESS ((DWORD)-1)
#endif

#ifndef SERVICE_CONFIG_DESCRIPTION
#define SERVICE_CONFIG_DESCRIPTION 1
#endif

typedef BOOL  (WINAPI *OykAttachConsoleFn)(DWORD);
typedef DWORD (WINAPI *OykRegisterServiceProcessFn)(DWORD, DWORD);
typedef BOOL  (WINAPI *OykChangeServiceConfig2AFn)(SC_HANDLE, DWORD, LPVOID);

struct OykServiceDescA {
    LPSTR lpDescription;
};

static SERVICE_STATUS        g_svc_status;
static SERVICE_STATUS_HANDLE g_svc_status_handle = NULL;
static OykWinServiceInitFn   g_svc_on_init = NULL;
static OykWinServiceLoopFn   g_svc_on_loop = NULL;
static OykWinServiceStopFn   g_svc_on_stop = NULL;

int oyk_win_is_nt(void) {
#if defined(_WIN64)
    return 1;
#else
    /*
     * Bit 31 of GetVersion() is 0 on all Windows NT releases (NT 3.51 through
     * Windows 11) and 1 on Windows 95, 98, ME, and Win32s.
     */
    DWORD ver = GetVersion();
    return ((ver & 0x80000000UL) == 0UL) ? 1 : 0;
#endif
}

void oyk_win_report_message(const char* title, const char* msg, int is_error) {
    HMODULE hk32 = GetModuleHandleA("KERNEL32.DLL");
    OykAttachConsoleFn pAttachConsole = NULL;
    if (hk32) {
        FARPROC fp = GetProcAddress(hk32, "AttachConsole");
        if (fp) memcpy(&pAttachConsole, &fp, sizeof(pAttachConsole));
    }
    if (pAttachConsole && pAttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE hstd = GetStdHandle(is_error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (hstd && hstd != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            if (title && title[0]) {
                WriteFile(hstd, title, static_cast<DWORD>(strlen(title)), &written, NULL);
                WriteFile(hstd, ": ", 2, &written, NULL);
            }
            if (msg && msg[0]) {
                WriteFile(hstd, msg, static_cast<DWORD>(strlen(msg)), &written, NULL);
            }
            WriteFile(hstd, "\r\n", 2, &written, NULL);
            return;
        }
    }
    MessageBoxA(NULL,
                msg ? msg : "",
                title ? title : "OYK Packet Node",
                MB_OK | (is_error ? MB_ICONSTOP : MB_ICONINFORMATION));
}

static void format_win_err(DWORD err_code, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               NULL, err_code, 0, out, static_cast<DWORD>(out_sz - 1), NULL);
    if (len == 0) {
        sprintf(out, "Windows error %lu", static_cast<unsigned long>(err_code));
        return;
    }
    while (len > 0 && (out[len - 1] == '\r' || out[len - 1] == '\n' || out[len - 1] == ' ')) {
        out[--len] = '\0';
    }
}

static int resolve_module_and_config(const char* config_path,
                                     char* exe_out, size_t exe_sz,
                                     char* cfg_out, size_t cfg_sz,
                                     char* out_msg, size_t out_sz) {
    DWORD n = GetModuleFileNameA(NULL, exe_out, static_cast<DWORD>(exe_sz));
    if (n == 0 || n >= exe_sz) {
        snprintf_fallback:
        (void)0;
        if (out_msg && out_sz > 0) {
            strncpy(out_msg, "Failed to resolve executable path.", out_sz - 1);
            out_msg[out_sz - 1] = '\0';
        }
        return 0;
    }
    cfg_out[0] = '\0';
    if (config_path && config_path[0]) {
        DWORD full_n = GetFullPathNameA(config_path, static_cast<DWORD>(cfg_sz), cfg_out, NULL);
        if (full_n == 0 || full_n >= cfg_sz) {
            if (strlen(config_path) >= cfg_sz) goto snprintf_fallback;
            strcpy(cfg_out, config_path);
        }
    }
    return 1;
}

static int install_nt_service(const char* cmdline, char* out_msg, size_t out_sz) {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        char ebuf[128];
        format_win_err(GetLastError(), ebuf, sizeof(ebuf));
        sprintf(out_msg, "OpenSCManager failed (try elevated prompt): %.110s", ebuf);
        return 0;
    }

    SC_HANDLE svc = CreateServiceA(
        scm,
        OYK_WIN_SERVICE_NAME,
        OYK_WIN_SERVICE_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        cmdline,
        NULL, NULL, NULL, NULL, NULL);

    int updated = 0;
    if (!svc && GetLastError() == ERROR_SERVICE_EXISTS) {
        svc = OpenServiceA(scm, OYK_WIN_SERVICE_NAME, SERVICE_CHANGE_CONFIG);
        if (svc) {
            if (!ChangeServiceConfigA(svc,
                                      SERVICE_WIN32_OWN_PROCESS,
                                      SERVICE_AUTO_START,
                                      SERVICE_ERROR_NORMAL,
                                      cmdline,
                                      NULL, NULL, NULL, NULL, NULL,
                                      OYK_WIN_SERVICE_DISPLAY)) {
                char ebuf[128];
                format_win_err(GetLastError(), ebuf, sizeof(ebuf));
                sprintf(out_msg, "Failed to update existing service: %.110s", ebuf);
                CloseServiceHandle(svc);
                CloseServiceHandle(scm);
                return 0;
            }
            updated = 1;
        }
    }

    if (!svc) {
        char ebuf[128];
        format_win_err(GetLastError(), ebuf, sizeof(ebuf));
        sprintf(out_msg, "CreateService failed: %.110s", ebuf);
        CloseServiceHandle(scm);
        return 0;
    }

    HMODULE hadv = GetModuleHandleA("ADVAPI32.DLL");
    OykChangeServiceConfig2AFn pChangeDesc = NULL;
    if (hadv) {
        FARPROC fp = GetProcAddress(hadv, "ChangeServiceConfig2A");
        if (fp) memcpy(&pChangeDesc, &fp, sizeof(pChangeDesc));
    }
    if (pChangeDesc) {
        static char kDesc[] = "AX.25, AXUDP, AXIP, and Telnet Amateur Radio Packet Node (KR4OYK).";
        OykServiceDescA sd;
        sd.lpDescription = kDesc;
        pChangeDesc(svc, SERVICE_CONFIG_DESCRIPTION, &sd);
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (out_msg && out_sz > 0) {
        sprintf(out_msg,
                "Windows NT service '%s' %s successfully (auto-start).\nCommand: %.260s",
                OYK_WIN_SERVICE_NAME,
                updated ? "updated" : "installed",
                cmdline);
    }
    return 1;
}

static int uninstall_nt_service(char* out_msg, size_t out_sz) {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        char ebuf[128];
        format_win_err(GetLastError(), ebuf, sizeof(ebuf));
        sprintf(out_msg, "OpenSCManager failed (try elevated prompt): %.110s", ebuf);
        return 0;
    }

    SC_HANDLE svc = OpenServiceA(scm, OYK_WIN_SERVICE_NAME,
                                 SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            strncpy(out_msg, "Service 'OYKNode' is not installed.", out_sz - 1);
            out_msg[out_sz - 1] = '\0';
            return 0;
        }
        char ebuf[128];
        format_win_err(err, ebuf, sizeof(ebuf));
        sprintf(out_msg, "OpenService failed: %.110s", ebuf);
        return 0;
    }

    SERVICE_STATUS ss;
    memset(&ss, 0, sizeof(ss));
    if (ControlService(svc, SERVICE_CONTROL_STOP, &ss)) {
        for (int tries = 0; tries < 25; ++tries) {
            Sleep(200);
            if (!QueryServiceStatus(svc, &ss) || ss.dwCurrentState == SERVICE_STOPPED) {
                break;
            }
        }
    }

    if (!DeleteService(svc)) {
        char ebuf[128];
        format_win_err(GetLastError(), ebuf, sizeof(ebuf));
        sprintf(out_msg, "DeleteService failed: %.110s", ebuf);
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return 0;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    strncpy(out_msg, "Windows NT service 'OYKNode' uninstalled successfully.", out_sz - 1);
    out_msg[out_sz - 1] = '\0';
    return 1;
}

static int install_9x_runservice(const char* cmdline, char* out_msg, size_t out_sz) {
    HKEY hkey = NULL;
    DWORD disp = 0;
    LONG rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                              OYK_WIN9X_RUNSERVICES_KEY,
                              0, NULL, REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE, NULL, &hkey, &disp);
    if (rc != ERROR_SUCCESS) {
        char ebuf[128];
        format_win_err(static_cast<DWORD>(rc), ebuf, sizeof(ebuf));
        sprintf(out_msg, "Failed to open RunServices registry key: %.110s", ebuf);
        return 0;
    }

    rc = RegSetValueExA(hkey,
                        OYK_WIN_SERVICE_NAME,
                        0,
                        REG_SZ,
                        reinterpret_cast<const BYTE*>(cmdline),
                        static_cast<DWORD>(strlen(cmdline) + 1));
    RegCloseKey(hkey);
    if (rc != ERROR_SUCCESS) {
        char ebuf[128];
        format_win_err(static_cast<DWORD>(rc), ebuf, sizeof(ebuf));
        sprintf(out_msg, "Failed to write RunServices registry value: %.110s", ebuf);
        return 0;
    }

    if (out_msg && out_sz > 0) {
        sprintf(out_msg,
                "Windows 9x RunServices entry '%s' installed successfully.\nCommand: %.260s",
                OYK_WIN_SERVICE_NAME,
                cmdline);
    }
    return 1;
}

static int uninstall_9x_runservice(char* out_msg, size_t out_sz) {
    HKEY hkey = NULL;
    LONG rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                            OYK_WIN9X_RUNSERVICES_KEY,
                            0, KEY_SET_VALUE, &hkey);
    if (rc != ERROR_SUCCESS) {
        if (rc == ERROR_FILE_NOT_FOUND) {
            strncpy(out_msg, "Windows 9x RunServices entry 'OYKNode' is not installed.", out_sz - 1);
            out_msg[out_sz - 1] = '\0';
            return 0;
        }
        char ebuf[128];
        format_win_err(static_cast<DWORD>(rc), ebuf, sizeof(ebuf));
        sprintf(out_msg, "Failed to open RunServices registry key: %.110s", ebuf);
        return 0;
    }

    rc = RegDeleteValueA(hkey, OYK_WIN_SERVICE_NAME);
    RegCloseKey(hkey);
    if (rc != ERROR_SUCCESS) {
        if (rc == ERROR_FILE_NOT_FOUND) {
            strncpy(out_msg, "Windows 9x RunServices entry 'OYKNode' is not installed.", out_sz - 1);
            out_msg[out_sz - 1] = '\0';
            return 0;
        }
        char ebuf[128];
        format_win_err(static_cast<DWORD>(rc), ebuf, sizeof(ebuf));
        sprintf(out_msg, "Failed to delete RunServices registry value: %.110s", ebuf);
        return 0;
    }

    strncpy(out_msg, "Windows 9x RunServices entry 'OYKNode' uninstalled successfully.", out_sz - 1);
    out_msg[out_sz - 1] = '\0';
    return 1;
}

int oyk_win_service_install(const char* config_path, char* out_msg, size_t out_sz) {
    char exe_path[260], abs_cfg[260], cmdline[560];
    if (!resolve_module_and_config(config_path, exe_path, sizeof(exe_path),
                                   abs_cfg, sizeof(abs_cfg), out_msg, out_sz)) {
        return 0;
    }
    int is_nt = oyk_win_is_nt();
    const char* flag = is_nt ? "--service" : "--headless";
    if (!oyk_win_build_service_cmdline(exe_path, abs_cfg, flag, cmdline, sizeof(cmdline))) {
        if (out_msg && out_sz > 0) {
            strncpy(out_msg, "Service command line exceeds buffer limit.", out_sz - 1);
            out_msg[out_sz - 1] = '\0';
        }
        return 0;
    }
    if (is_nt) {
        return install_nt_service(cmdline, out_msg, out_sz);
    }
    return install_9x_runservice(cmdline, out_msg, out_sz);
}

int oyk_win_service_uninstall(char* out_msg, size_t out_sz) {
    if (oyk_win_is_nt()) {
        return uninstall_nt_service(out_msg, out_sz);
    }
    return uninstall_9x_runservice(out_msg, out_sz);
}

void oyk_win_register_9x_service(int enable) {
    if (oyk_win_is_nt()) {
        return;
    }
#if !defined(_WIN64)
    HMODULE hk32 = GetModuleHandleA("KERNEL32.DLL");
    OykRegisterServiceProcessFn pRegSvc = NULL;
    if (hk32) {
        FARPROC fp = GetProcAddress(hk32, "RegisterServiceProcess");
        if (fp) memcpy(&pRegSvc, &fp, sizeof(pRegSvc));
    }
    if (pRegSvc) {
        pRegSvc(0UL, enable ? 1UL : 0UL);
    }
#else
    (void)enable;
#endif
}

static void report_nt_service_status(DWORD state, DWORD win32_exit, DWORD svc_exit,
                                     DWORD wait_hint) {
    static DWORD s_checkpoint = 1;
    if (!g_svc_status_handle) return;

    g_svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState = state;
    g_svc_status.dwWin32ExitCode = win32_exit;
    g_svc_status.dwServiceSpecificExitCode = svc_exit;
    g_svc_status.dwWaitHint = wait_hint;
    if (state == SERVICE_START_PENDING) {
        g_svc_status.dwControlsAccepted = 0;
    } else {
        g_svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }
    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED) {
        g_svc_status.dwCheckPoint = 0;
    } else {
        g_svc_status.dwCheckPoint = s_checkpoint++;
    }
    SetServiceStatus(g_svc_status_handle, &g_svc_status);
}

static VOID WINAPI oyk_service_ctrl_handler(DWORD ctrl_code) {
    switch (ctrl_code) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            report_nt_service_status(SERVICE_STOP_PENDING, NO_ERROR, 0, 5000);
            if (g_svc_on_stop) {
                g_svc_on_stop();
            }
            break;
        case SERVICE_CONTROL_INTERROGATE:
            if (g_svc_status_handle) {
                SetServiceStatus(g_svc_status_handle, &g_svc_status);
            }
            break;
        default:
            break;
    }
}

static VOID WINAPI oyk_service_main(DWORD argc, LPSTR* argv) {
    g_svc_status_handle = RegisterServiceCtrlHandlerA(OYK_WIN_SERVICE_NAME,
                                                      oyk_service_ctrl_handler);
    if (!g_svc_status_handle) {
        return;
    }
    memset(&g_svc_status, 0, sizeof(g_svc_status));
    report_nt_service_status(SERVICE_START_PENDING, NO_ERROR, 0, 5000);

    char err[128];
    err[0] = '\0';
    if (!g_svc_on_init || !g_svc_on_init(static_cast<int>(argc), argv, err, sizeof(err))) {
        report_nt_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 1, 0);
        return;
    }

    report_nt_service_status(SERVICE_RUNNING, NO_ERROR, 0, 0);
    if (g_svc_on_loop) {
        g_svc_on_loop();
    }
    report_nt_service_status(SERVICE_STOP_PENDING, NO_ERROR, 0, 3000);
    report_nt_service_status(SERVICE_STOPPED, NO_ERROR, 0, 0);
}

int oyk_win_service_run_nt(OykWinServiceInitFn on_init,
                           OykWinServiceLoopFn on_loop,
                           OykWinServiceStopFn on_stop,
                           char* out_err,
                           size_t out_err_sz) {
    if (!oyk_win_is_nt()) {
        if (out_err && out_err_sz > 0) {
            strncpy(out_err,
                    "Windows NT SCM (--service) is unsupported on Windows 9x; use --headless.",
                    out_err_sz - 1);
            out_err[out_err_sz - 1] = '\0';
        }
        return 1;
    }

    g_svc_on_init = on_init;
    g_svc_on_loop = on_loop;
    g_svc_on_stop = on_stop;

    static char kSvcName[] = OYK_WIN_SERVICE_NAME;
    SERVICE_TABLE_ENTRYA table[2];
    table[0].lpServiceName = kSvcName;
    table[0].lpServiceProc = oyk_service_main;
    table[1].lpServiceName = NULL;
    table[1].lpServiceProc = NULL;

    if (!StartServiceCtrlDispatcherA(table)) {
        DWORD err = GetLastError();
        if (out_err && out_err_sz > 0) {
            if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                strncpy(out_err,
                        "--service must be started by the Windows Service Control Manager (use --headless for interactive background mode).",
                        out_err_sz - 1);
                out_err[out_err_sz - 1] = '\0';
            } else {
                char ebuf[128];
                format_win_err(err, ebuf, sizeof(ebuf));
                sprintf(out_err, "StartServiceCtrlDispatcher failed: %.110s", ebuf);
            }
        }
        return 1;
    }
    return 0;
}

#endif /* _WIN32 && !OYK_WIN16 */
