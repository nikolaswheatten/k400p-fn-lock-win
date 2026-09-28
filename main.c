#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <hidapi.h>

#define HIDPP_PKT_LEN 7
#define MAX_PATHS 32
#define MAX_PATH_LEN 512
#define MAX_CMD 8192
#define TASK_NAME "K400pFnLock"
#define RESIDENT_CLASS_NAME "K400pFnLockResidentWnd"
#define RESIDENT_MUTEX_NAME "K400pFnLockResidentMutex"
#define LOG_SUBDIR "k400p-fn-lock"
#define DEFAULT_WAIT_MINUTES 10
#define DEFAULT_RETRY_SECONDS 15
#define DEFAULT_REAPPLY_MINUTES 15
#define WM_TRAYICON (WM_APP + 1)
#define WM_TRAYPREF (WM_APP + 2)
#define TRAY_ID 1
#define IDM_REAPPLY 1001
#define IDM_EXIT 1002
#define IDM_HIDE_ICON 1003
#define IDM_ABOUT 1004
#define IDI_APPICON 101
#define HOMEPAGE_URL "https://github.com/nikolaswheatten/k400p-fn-lock-win"
#define TIMER_REAPPLY 1

static const int LOGITECH_VID = 0x46d;
static const int TARGET_USAGE = 1;
static const int TARGET_USAGE_PAGE = 65280;

static const unsigned char FN_LOCK[] = {0x10, 0x01, 0x09, 0x19, 0x00, 0x00, 0x00};
static const unsigned char DEVICE_INDICES[] = {1, 2, 3, 4, 5, 6, 0xFF};
static const int DEVICE_INDEX_COUNT = 7;

typedef struct
{
    int apply;
    int diagnose;
    int probe;
    int install;
    int uninstall;
    int help;
    int quiet;
    int wait;
    int resident;
    int hide_icon;
    int show_icon;
    int max_wait_minutes;
    int retry_seconds;
} Options;

static int g_quiet = 0;
static FILE *g_apply_log = NULL;
static FILE *g_install_log = NULL;

static int path_seen(const char paths[][MAX_PATH_LEN], int count, const char *path)
{
    int i;
    for (i = 0; i < count; i++)
    {
        if (strcmp(paths[i], path) == 0)
            return 1;
    }
    return 0;
}

static int ensure_log_dir(char *dir_out, size_t dir_cap)
{
    const char *local = getenv("LOCALAPPDATA");
    if (!local || !local[0])
        return -1;

    if (snprintf(dir_out, dir_cap, "%s\\%s", local, LOG_SUBDIR) >= (int)dir_cap)
        return -1;

    if (!CreateDirectoryA(dir_out, NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return -1;
    }
    return 0;
}

static FILE *open_log_file(const char *name)
{
    char dir[MAX_PATH_LEN];
    char path[MAX_PATH_LEN];
    FILE *f;

    if (ensure_log_dir(dir, sizeof(dir)) != 0)
        return NULL;

    if (snprintf(path, sizeof(path), "%s\\%s", dir, name) >= (int)sizeof(path))
        return NULL;

    f = fopen(path, "a");
    return f;
}

static void log_line(FILE *log, const char *level, const char *message)
{
    time_t now;
    struct tm tm_info;
    char ts[32];

    if (!log)
        return;

    time(&now);
    localtime_s(&tm_info, &now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);
    fprintf(log, "%s [%s] %s\n", ts, level, message);
    fflush(log);
}

static void msgf(FILE *log, int to_console, const char *level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    log_line(log, level, buf);
    if (to_console)
        printf("%s\n", buf);
}

static void outf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    log_line(g_apply_log, "INFO", buf);
    if (!g_quiet)
        printf("%s\n", buf);
}

static void errf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    log_line(g_apply_log, "ERROR", buf);
    if (!g_quiet)
        fprintf(stderr, "%s\n", buf);
}

static void print_hex_verbose(const char *label, const unsigned char *buf, int len)
{
    char line[512];
    char hex[256];
    int i, pos = 0;

    for (i = 0; i < len && pos < (int)sizeof(hex) - 3; i++)
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x", buf[i]);

    snprintf(line, sizeof(line), "%s (%d bytes): %s", label, len, hex);
    outf("%s", line);
}

static int hidpp_ack(const unsigned char *response, int len)
{
    return len >= 3 && response[2] == 0x8F;
}

static int collect_hidpp_paths(char paths[][MAX_PATH_LEN], int max_paths)
{
    struct hid_device_info *devs, *cur_dev;
    int count = 0;

    devs = hid_enumerate(LOGITECH_VID, 0);
    for (cur_dev = devs; cur_dev; cur_dev = cur_dev->next)
    {
        if (cur_dev->usage != TARGET_USAGE || cur_dev->usage_page != TARGET_USAGE_PAGE)
            continue;
        if (count >= max_paths)
            break;
        if (path_seen(paths, count, cur_dev->path))
            continue;

        strncpy(paths[count], cur_dev->path, MAX_PATH_LEN - 1);
        paths[count][MAX_PATH_LEN - 1] = '\0';
        count++;
    }
    hid_free_enumeration(devs);
    return count;
}

static int send_fn_lock_slot(hid_device *handle, unsigned char device_index, int verbose)
{
    unsigned char pkt[HIDPP_PKT_LEN];
    unsigned char response[65];
    int write_res;
    int read_res;

    memcpy(pkt, FN_LOCK, HIDPP_PKT_LEN);
    pkt[1] = device_index;

    write_res = hid_write(handle, pkt, HIDPP_PKT_LEN);
    if (write_res != HIDPP_PKT_LEN)
        return -1;

    if (verbose)
    {
        if (device_index == 0xFF)
            outf("  slot direct (FF):");
        else
            outf("  slot %u:", device_index);
        print_hex_verbose("  sent", pkt, HIDPP_PKT_LEN);
    }

    memset(response, 0, sizeof(response));
    read_res = hid_read_timeout(handle, response, sizeof(response), 500);
    if (read_res > 0)
    {
        if (verbose)
            print_hex_verbose("  reply", response, read_res);
        return 0;
    }

    if (verbose)
        outf("  -> no reply (write accepted)");
    return 0;
}

static int apply_on_path(const char *path, int verbose, int *writes_ok)
{
    hid_device *handle;
    int i;
    int ok = 0;

    if (verbose)
        outf("HID++ interface: %s", path);

    handle = hid_open_path(path);
    if (!handle)
    {
        if (verbose)
            outf("  ERROR: could not open device");
        return 0;
    }

    for (i = 0; i < DEVICE_INDEX_COUNT; i++)
    {
        if (send_fn_lock_slot(handle, DEVICE_INDICES[i], verbose) == 0)
        {
            ok = 1;
            if (writes_ok)
                (*writes_ok)++;
        }
    }

    hid_close(handle);
    return ok;
}

static int probe_interfaces(void)
{
    char paths[MAX_PATHS][MAX_PATH_LEN];
    return collect_hidpp_paths(paths, MAX_PATHS) > 0 ? 0 : 2;
}

static int apply_fn_lock_verbose(int verbose)
{
    char paths[MAX_PATHS][MAX_PATH_LEN];
    int path_count;
    int i;
    int writes_ok = 0;

    if (verbose)
    {
        outf("k400p-fn-lock: universal Fn Lock for Logitech K400+");
        outf("(all HID++ receivers, device slots 1-6 and direct FF)");
        outf("");
    }

    path_count = collect_hidpp_paths(paths, MAX_PATHS);
    if (path_count == 0)
    {
        errf("ERROR: no Logitech HID++ interface found.");
        errf("Plug in the K400+ USB dongle and turn the keyboard on.");
        return 2;
    }

    if (verbose)
    {
        outf("Found %d HID++ interface(s).", path_count);
        outf("Sending Fn Lock to every receiver and every device slot...");
        outf("");
    }
    else
    {
        log_line(g_apply_log, "INFO", "Applying Fn Lock (universal sweep)");
    }

    for (i = 0; i < path_count; i++)
        apply_on_path(paths[i], verbose, &writes_ok);

    if (writes_ok == 0)
    {
        errf("ERROR: could not write to any HID++ interface.");
        return 3;
    }

    if (verbose)
    {
        outf("Done: %d command(s) sent.", writes_ok);
        outf("Test F2 in Explorer on the K400+ keyboard.");
        outf("Fn Lock lasts until reboot.");
    }
    else
    {
        log_line(g_apply_log, "OK", "Fn Lock applied successfully");
    }

    return 0;
}

static int apply_with_wait(int verbose)
{
    ULONGLONG deadline;
    ULONGLONG retry_ms;
    int attempt = 0;

    deadline = GetTickCount64() + (ULONGLONG)DEFAULT_WAIT_MINUTES * 60 * 1000;
    retry_ms = (ULONGLONG)DEFAULT_RETRY_SECONDS * 1000;

    log_line(g_apply_log, "INFO", "=== Fn Lock apply started (wait mode) ===");

    while (GetTickCount64() < deadline)
    {
        int probe_code = probe_interfaces();
        if (probe_code != 0)
        {
            outf("Logitech HID++ interface not ready, waiting %ds...", DEFAULT_RETRY_SECONDS);
            Sleep((DWORD)retry_ms);
            continue;
        }

        if (attempt == 0)
            log_line(g_apply_log, "OK", "Logitech HID++ interface ready");

        attempt++;
        outf("Attempt %d: applying Fn Lock", attempt);

        {
            int code = apply_fn_lock_verbose(verbose && !g_quiet);
            if (code == 0)
                return 0;
        }

        outf("Fn Lock failed, retrying in %ds...", DEFAULT_RETRY_SECONDS);
        Sleep((DWORD)retry_ms);

        if (probe_interfaces() != 0)
            outf("HID++ interface lost, waiting for reconnect...");
    }

    errf("Fn Lock could not be applied after %d attempt(s)", attempt);
    return 3;
}

static int get_exe_path(char *buf, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)cap);
    if (n == 0 || n >= cap)
        return -1;
    return 0;
}

static int run_command_hidden(const char *cmd)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdline[MAX_CMD];
    DWORD wait;
    int exit_code = 1;

    if (strlen(cmd) + 1 >= sizeof(cmdline))
        return -1;

    strcpy(cmdline, cmd);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;

    wait = WaitForSingleObject(pi.hProcess, INFINITE);
    if (wait == WAIT_OBJECT_0)
    {
        DWORD ec = 1;
        if (GetExitCodeProcess(pi.hProcess, &ec))
            exit_code = (int)ec;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exit_code;
}

static int set_run_key(const char *command)
{
    HKEY hkey;
    LONG rc;

    rc = RegCreateKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL);
    if (rc != ERROR_SUCCESS)
        return -1;

    rc = RegSetValueExA(hkey, TASK_NAME, 0, REG_SZ,
        (const BYTE *)command, (DWORD)strlen(command) + 1);
    RegCloseKey(hkey);
    return rc == ERROR_SUCCESS ? 0 : -1;
}

static int remove_run_key(void)
{
    HKEY hkey;
    LONG rc;

    rc = RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hkey);
    if (rc != ERROR_SUCCESS)
        return -1;

    rc = RegDeleteValueA(hkey, TASK_NAME);
    RegCloseKey(hkey);
    return (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND) ? 0 : -1;
}

#define SETTINGS_KEY "Software\\K400pFnLock"
#define HIDE_ICON_VALUE "HideTrayIcon"

static int get_hide_icon_pref(void)
{
    HKEY hkey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExA(hkey, HIDE_ICON_VALUE, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
        value = 0;
    RegCloseKey(hkey);
    return value != 0;
}

static int set_hide_icon_pref(int hide)
{
    HKEY hkey;
    DWORD value = hide ? 1 : 0;
    LONG rc;

    rc = RegCreateKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL);
    if (rc != ERROR_SUCCESS)
        return -1;
    rc = RegSetValueExA(hkey, HIDE_ICON_VALUE, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(hkey);
    return rc == ERROR_SUCCESS ? 0 : -1;
}

static int spawn_detached(const char *cmd)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdline[MAX_CMD];

    if (strlen(cmd) + 1 >= sizeof(cmdline))
        return -1;
    strcpy(cmdline, cmd);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

/* Every previous approach here spawned a fresh process on a schedule
   (Task Scheduler, then schtasks/XML) and fought that process's console
   window flashing on screen and stealing focus - fatal for a fullscreen
   game. A resident app removes the recurring spawn entirely: one process
   started at logon lives for the session and reacts to real OS events
   (session unlock, resume from sleep) via a hidden message window, with a
   periodic timer only as a fallback. See run_resident(). */
static int install_autostart(void)
{
    char exe_path[MAX_PATH_LEN];
    char command[MAX_PATH_LEN + 32];
    char cleanup_cmd[MAX_CMD];
    int code;

    g_install_log = open_log_file("install.log");
    msgf(g_install_log, 1, "INFO", "=== K400+ Fn Lock autostart setup ===");

    if (get_exe_path(exe_path, sizeof(exe_path)) != 0)
    {
        msgf(g_install_log, 1, "ERROR", "Could not determine executable path.");
        if (g_install_log) fclose(g_install_log);
        return 1;
    }

    /* Best-effort cleanup of the old Task Scheduler-based autostart from
       earlier versions; it's fine if there's nothing to remove. */
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "schtasks.exe /Delete /TN \"%s\" /F", TASK_NAME);
    run_command_hidden(cleanup_cmd);

    snprintf(command, sizeof(command), "\"%s\" --resident", exe_path);
    if (set_run_key(command) != 0)
    {
        msgf(g_install_log, 1, "ERROR", "Could not write the autostart registry entry.");
        if (g_install_log) fclose(g_install_log);
        return 2;
    }
    msgf(g_install_log, 1, "INFO", "Registered autostart entry: %s", command);

    if (probe_interfaces() != 0)
    {
        msgf(g_install_log, 1, "INFO", "Live test skipped: HID++ receiver not detected right now.");
    }
    else
    {
        msgf(g_install_log, 1, "INFO", "Running live test...");
        g_apply_log = open_log_file("apply.log");
        g_quiet = 1;
        if (hid_init() == 0)
        {
            code = apply_with_wait(0);
            hid_exit();
            if (code == 0)
                msgf(g_install_log, 1, "OK", "Live test OK: Fn Lock applied.");
            else
                msgf(g_install_log, 1, "ERROR", "Live test FAIL (exit %d). Check apply.log", code);
        }
        else
        {
            msgf(g_install_log, 1, "ERROR", "hid_init failed during live test.");
        }
        g_quiet = 0;
        if (g_apply_log) { fclose(g_apply_log); g_apply_log = NULL; }
    }

    if (spawn_detached(command) == 0)
        msgf(g_install_log, 1, "INFO", "Background app started now (also starts at every future logon).");
    else
        msgf(g_install_log, 1, "INFO", "Could not start the background app now; it will start at next logon.");

    msgf(g_install_log, 1, "INFO", "Done.");
    if (g_install_log) fclose(g_install_log);
    return 0;
}

static int uninstall_autostart(void)
{
    char cleanup_cmd[MAX_CMD];
    HWND running;

    g_install_log = open_log_file("install.log");
    msgf(g_install_log, 1, "INFO", "Removing autostart...");

    if (remove_run_key() == 0)
        msgf(g_install_log, 1, "INFO", "Removed autostart registry entry.");
    else
        msgf(g_install_log, 1, "INFO", "Autostart registry entry was not present.");

    /* Best-effort cleanup of the old Task Scheduler-based autostart. */
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "schtasks.exe /Delete /TN \"%s\" /F", TASK_NAME);
    run_command_hidden(cleanup_cmd);

    running = FindWindowA(RESIDENT_CLASS_NAME, NULL);
    if (running)
    {
        PostMessageA(running, WM_CLOSE, 0, 0);
        msgf(g_install_log, 1, "INFO", "Signalled the running background app to exit.");
    }

    msgf(g_install_log, 1, "INFO", "Done.");
    if (g_install_log) fclose(g_install_log);
    return 0;
}

static void resident_apply(const char *reason)
{
    char msg[128];

    snprintf(msg, sizeof(msg), "Reapplying Fn Lock (%s)", reason);
    log_line(g_apply_log, "INFO", msg);

    if (hid_init() != 0)
    {
        log_line(g_apply_log, "ERROR", "hid_init failed");
        return;
    }
    apply_fn_lock_verbose(0);
    hid_exit();
}

static NOTIFYICONDATAA g_tray_nid;
static int g_tray_added = 0;

static void add_tray_icon(HWND hwnd)
{
    memset(&g_tray_nid, 0, sizeof(g_tray_nid));
    g_tray_nid.cbSize = sizeof(g_tray_nid);
    g_tray_nid.hWnd = hwnd;
    g_tray_nid.uID = TRAY_ID;
    g_tray_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray_nid.uCallbackMessage = WM_TRAYICON;
    g_tray_nid.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDI_APPICON));
    strncpy(g_tray_nid.szTip, "K400+ Fn Lock", sizeof(g_tray_nid.szTip) - 1);
    g_tray_added = Shell_NotifyIconA(NIM_ADD, &g_tray_nid);
}

static void remove_tray_icon(void)
{
    if (g_tray_added)
    {
        Shell_NotifyIconA(NIM_DELETE, &g_tray_nid);
        g_tray_added = 0;
    }
}

static void show_about(HWND hwnd)
{
    char exe_path[MAX_PATH_LEN];
    char log_dir[MAX_PATH_LEN];
    char text[1024];

    if (get_exe_path(exe_path, sizeof(exe_path)) != 0)
        strcpy(exe_path, "(unknown)");
    if (ensure_log_dir(log_dir, sizeof(log_dir)) != 0)
        strcpy(log_dir, "(unavailable)");

    snprintf(text, sizeof(text),
        "K400+ Fn Lock\r\n"
        "Build: %s %s\r\n\r\n"
        "Universal Fn Lock for the Logitech K400+ keyboard (all HID++ receivers, "
        "device slots 1-6 + FF), kept applied by this resident background app "
        "reacting to logon, session unlock and resume-from-sleep.\r\n\r\n"
        "License: MIT\r\n"
        "Homepage: " HOMEPAGE_URL "\r\n\r\n"
        "Running from: %s\r\n"
        "Logs: %s",
        __DATE__, __TIME__, exe_path, log_dir);

    MessageBoxA(hwnd, text, "About K400+ Fn Lock", MB_OK | MB_ICONINFORMATION);
}

static void show_tray_menu(HWND hwnd)
{
    POINT pt;
    HMENU menu = CreatePopupMenu();

    GetCursorPos(&pt);
    AppendMenuA(menu, MF_STRING, IDM_REAPPLY, "Reapply Fn Lock now");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_HIDE_ICON, "Hide icon (use --show-icon to bring it back)");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_ABOUT, "About");
    AppendMenuA(menu, MF_STRING, IDM_EXIT, "Exit");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK resident_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        if (!get_hide_icon_pref())
            add_tray_icon(hwnd);
        WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
        SetTimer(hwnd, TIMER_REAPPLY, (UINT)DEFAULT_REAPPLY_MINUTES * 60 * 1000, NULL);
        resident_apply("startup");
        return 0;

    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_UNLOCK)
            resident_apply("session unlock");
        return 0;

    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND)
            resident_apply("resume from sleep");
        return TRUE;

    case WM_TIMER:
        if (wp == TIMER_REAPPLY)
            resident_apply("periodic");
        return 0;

    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP)
            show_tray_menu(hwnd);
        return 0;

    /* Sent by a second invocation (--hide-icon / --show-icon) to an
       already-running resident process, so the change takes effect without
       needing a logoff/logon. */
    case WM_TRAYPREF:
        if (wp)
        {
            set_hide_icon_pref(1);
            remove_tray_icon();
        }
        else
        {
            set_hide_icon_pref(0);
            if (!g_tray_added)
                add_tray_icon(hwnd);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == IDM_REAPPLY)
            resident_apply("manual");
        else if (LOWORD(wp) == IDM_HIDE_ICON)
        {
            set_hide_icon_pref(1);
            remove_tray_icon();
        }
        else if (LOWORD(wp) == IDM_ABOUT)
            show_about(hwnd);
        else if (LOWORD(wp) == IDM_EXIT)
            DestroyWindow(hwnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        WTSUnRegisterSessionNotification(hwnd);
        KillTimer(hwnd, TIMER_REAPPLY);
        remove_tray_icon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int run_resident(void)
{
    HANDLE mutex;
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;

    mutex = CreateMutexA(NULL, TRUE, RESIDENT_MUTEX_NAME);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0; /* another instance is already running */

    g_quiet = 1;
    g_apply_log = open_log_file("apply.log");
    log_line(g_apply_log, "INFO", "=== K400+ Fn Lock resident app started ===");

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = resident_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = RESIDENT_CLASS_NAME;
    wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(IDI_APPICON));
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(0, RESIDENT_CLASS_NAME, RESIDENT_CLASS_NAME, 0,
        0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd)
    {
        log_line(g_apply_log, "ERROR", "Could not create message window.");
        if (g_apply_log) fclose(g_apply_log);
        CloseHandle(mutex);
        return 1;
    }

    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    log_line(g_apply_log, "INFO", "=== K400+ Fn Lock resident app stopped ===");
    if (g_apply_log) fclose(g_apply_log);
    CloseHandle(mutex);
    return 0;
}

static void print_help(const char *argv0)
{
    printf("k400p-fn-lock - Fn Lock for Logitech K400+ (Windows)\n\n");
    printf("Usage:\n");
    printf("  %s                    Apply Fn Lock quietly\n", argv0);
    printf("  %s --apply [--quiet]   Universal HID++ sweep (all receivers, slots 1-6 + FF)\n", argv0);
    printf("  %s --apply --wait      Wait for dongle, retry (autostart mode)\n", argv0);
    printf("  %s --diagnose          Apply with verbose output\n", argv0);
    printf("  %s --probe             Exit 0 if HID++ receiver present\n", argv0);
    printf("  %s --install           Start a background app at logon (tray icon, no console flash)\n", argv0);
    printf("  %s --uninstall         Remove autostart and stop the background app\n", argv0);
    printf("  %s --resident          (internal) run the background app in this process\n", argv0);
    printf("  %s --hide-icon         Hide the tray icon (background app keeps running)\n", argv0);
    printf("  %s --show-icon         Show the tray icon again\n", argv0);
    printf("  %s --help               Show this help\n", argv0);
    printf("\nLogs: %%LOCALAPPDATA%%\\%s\\apply.log, install.log\n", LOG_SUBDIR);
}

static int parse_options(int argc, char **argv, Options *opt)
{
    int i;

    memset(opt, 0, sizeof(*opt));
    opt->max_wait_minutes = DEFAULT_WAIT_MINUTES;
    opt->retry_seconds = DEFAULT_RETRY_SECONDS;

    if (argc <= 1)
    {
        opt->apply = 1;
        opt->quiet = 1;
        return 0;
    }

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            opt->help = 1;
        else if (strcmp(argv[i], "--apply") == 0)
            opt->apply = 1;
        else if (strcmp(argv[i], "--diagnose") == 0)
            opt->diagnose = 1;
        else if (strcmp(argv[i], "--probe") == 0)
            opt->probe = 1;
        else if (strcmp(argv[i], "--install") == 0)
            opt->install = 1;
        else if (strcmp(argv[i], "--uninstall") == 0)
            opt->uninstall = 1;
        else if (strcmp(argv[i], "--quiet") == 0)
            opt->quiet = 1;
        else if (strcmp(argv[i], "--wait") == 0)
            opt->wait = 1;
        else if (strcmp(argv[i], "--resident") == 0)
            opt->resident = 1;
        else if (strcmp(argv[i], "--hide-icon") == 0)
            opt->hide_icon = 1;
        else if (strcmp(argv[i], "--show-icon") == 0)
            opt->show_icon = 1;
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (!opt->help && !opt->apply && !opt->diagnose && !opt->probe && !opt->install && !opt->uninstall &&
        !opt->resident && !opt->hide_icon && !opt->show_icon)
    {
        fprintf(stderr, "No mode selected. Use --help.\n");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    Options opt;
    int res;
    int code = 1;

    /* A console-subsystem exe gets a console window allocated whenever it's
       launched without one, e.g. by Task Scheduler - this flashed on every
       15-minute run. GUI subsystem was tried and reverted: cmd.exe and
       PowerShell don't wait for a GUI-subsystem process before returning to
       the prompt, so interactive runs raced with their own output. Instead,
       detect whether this console was created just for us (nobody else is
       attached to it - a shared console from an interactive shell always has
       the shell's own process attached too) and hide only that case, before
       it can be seen. */
    {
        DWORD console_pids[2];
        if (GetConsoleProcessList(console_pids, 2) <= 1)
        {
            HWND console_wnd = GetConsoleWindow();
            if (console_wnd)
                ShowWindow(console_wnd, SW_HIDE);
        }
    }

    if (parse_options(argc, argv, &opt) != 0)
    {
        print_help(argv[0]);
        return 1;
    }

    if (opt.help)
    {
        print_help(argv[0]);
        return 0;
    }

    g_quiet = opt.quiet;

    if (opt.install || opt.uninstall)
    {
        if (opt.install)
            return install_autostart();
        return uninstall_autostart();
    }

    if (opt.resident)
        return run_resident();

    if (opt.hide_icon || opt.show_icon)
    {
        int hide = opt.hide_icon;
        HWND running = FindWindowA(RESIDENT_CLASS_NAME, NULL);

        set_hide_icon_pref(hide);
        if (running)
            PostMessageA(running, WM_TRAYPREF, (WPARAM)hide, 0);

        if (!g_quiet)
            printf("Tray icon will be %s. %s\n",
                hide ? "hidden" : "shown",
                running ? "Applied immediately." : "It will apply the next time the background app starts.");
        return 0;
    }

    g_apply_log = open_log_file("apply.log");

    res = hid_init();
    if (res != 0)
    {
        errf("ERROR: hid_init failed");
        if (g_apply_log) fclose(g_apply_log);
        return 1;
    }

    if (opt.probe)
    {
        code = probe_interfaces();
        hid_exit();
        if (g_apply_log) fclose(g_apply_log);
        return code;
    }

    if (opt.diagnose)
    {
        g_quiet = 0;
        code = apply_fn_lock_verbose(1);
    }
    else if (opt.wait)
    {
        code = apply_with_wait(opt.quiet ? 0 : 1);
    }
    else if (opt.apply)
    {
        code = apply_fn_lock_verbose(!opt.quiet);
    }

    hid_exit();
    if (g_apply_log) fclose(g_apply_log);
    return code;
}
