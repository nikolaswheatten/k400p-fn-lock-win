#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <windows.h>

#include <hidapi.h>

#define HIDPP_PKT_LEN 7
#define MAX_PATHS 32
#define MAX_PATH_LEN 512
#define MAX_CMD 8192
#define TASK_NAME "K400pFnLock"
#define LOG_SUBDIR "k400p-fn-lock"
#define DEFAULT_WAIT_MINUTES 10
#define DEFAULT_RETRY_SECONDS 15
#define DEFAULT_LOGON_DELAY 30

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

static int install_autostart(void)
{
    char exe_path[MAX_PATH_LEN];
    char ps[MAX_CMD];
    char ps_escaped[MAX_PATH_LEN * 2];
    size_t i, j;
    int code;

    g_install_log = open_log_file("install.log");
    msgf(g_install_log, 1, "INFO", "=== K400+ Fn Lock autostart setup ===");

    if (get_exe_path(exe_path, sizeof(exe_path)) != 0)
    {
        msgf(g_install_log, 1, "ERROR", "Could not determine executable path.");
        return 1;
    }

    for (i = 0, j = 0; exe_path[i] && j + 2 < sizeof(ps_escaped); i++)
    {
        if (exe_path[i] == '\'')
        {
            ps_escaped[j++] = '\'';
            ps_escaped[j++] = '\'';
        }
        else
            ps_escaped[j++] = exe_path[i];
    }
    ps_escaped[j] = '\0';

    snprintf(ps, sizeof(ps),
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \""
        "$ErrorActionPreference='Stop';"
        "$TaskName='%s';"
        "$ExePath='%s';"
        "$existing=Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue;"
        "if($existing){Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false};"
        "$action=New-ScheduledTaskAction -Execute $ExePath -Argument '--apply --wait --quiet';"
        "$triggerLogon=New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME;"
        "$triggerLogon.Delay='PT%dS';"
        "$unlockClass=Get-CimClass -ClassName MSFT_TaskSessionStateChangeTrigger -Namespace Root/Microsoft/Windows/TaskScheduler;"
        "$triggerUnlock=New-CimInstance -CimClass $unlockClass -ClientOnly;"
        "$triggerUnlock.Enabled=$true;"
        "$triggerUnlock.StateChange=8;"
        "$triggerUnlock.UserId=$env:USERNAME;"
        "$settings=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew;"
        "$principal=New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited;"
        "Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger @($triggerLogon,$triggerUnlock) -Settings $settings -Principal $principal -Description 'Apply Logitech K400+ Fn Lock after logon/unlock' -Force | Out-Null;"
        "$task=Get-ScheduledTask -TaskName $TaskName;"
        "if(-not $task){exit 3};"
        "if($task.Actions[0].Execute -ne $ExePath){exit 3};"
        "exit 0"
        "\"",
        TASK_NAME, ps_escaped, DEFAULT_LOGON_DELAY);

    code = run_command_hidden(ps);
    if (code != 0)
    {
        msgf(g_install_log, 1, "ERROR", "Failed to register scheduled task (exit %d).", code);
        if (g_install_log) fclose(g_install_log);
        return 2;
    }

    msgf(g_install_log, 1, "INFO", "Registered scheduled task '%s' (logon delay %ds + session unlock).", TASK_NAME, DEFAULT_LOGON_DELAY);

    if (probe_interfaces() != 0)
    {
        msgf(g_install_log, 1, "INFO", "Live test skipped: HID++ receiver not detected right now.");
        msgf(g_install_log, 1, "INFO", "Done. Task '%s' is installed.", TASK_NAME);
        if (g_install_log) fclose(g_install_log);
        return 0;
    }

    msgf(g_install_log, 1, "INFO", "Running live test...");
    g_apply_log = open_log_file("apply.log");
    g_quiet = 1;
    if (hid_init() != 0)
    {
        msgf(g_install_log, 1, "ERROR", "hid_init failed during live test.");
        if (g_install_log) fclose(g_install_log);
        if (g_apply_log) fclose(g_apply_log);
        return 4;
    }
    code = apply_with_wait(0);
    hid_exit();
    g_quiet = 0;

    if (code == 0)
    {
        msgf(g_install_log, 1, "OK", "Live test OK: Fn Lock applied.");
        msgf(g_install_log, 1, "INFO", "Done. Task '%s' is installed and verified.", TASK_NAME);
    }
    else
    {
        msgf(g_install_log, 1, "ERROR", "Live test FAIL (exit %d). Check apply.log", code);
        if (g_install_log) fclose(g_install_log);
        if (g_apply_log) fclose(g_apply_log);
        return 4;
    }

    if (g_install_log) fclose(g_install_log);
    if (g_apply_log) fclose(g_apply_log);
    return 0;
}

static int uninstall_autostart(void)
{
    char ps[MAX_CMD];
    int code;

    g_install_log = open_log_file("install.log");
    msgf(g_install_log, 1, "INFO", "Removing scheduled task '%s'...", TASK_NAME);

    snprintf(ps, sizeof(ps),
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \""
        "$t=Get-ScheduledTask -TaskName '%s' -ErrorAction SilentlyContinue;"
        "if($t){Unregister-ScheduledTask -TaskName '%s' -Confirm:$false;"
        "Write-Host 'Removed scheduled task.'}else{Write-Host 'Task was not installed.'};"
        "exit 0"
        "\"",
        TASK_NAME, TASK_NAME);

    code = run_command_hidden(ps);
    if (g_install_log) fclose(g_install_log);
    return code == 0 ? 0 : 1;
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
    printf("  %s --install           Register logon/unlock scheduled task\n", argv0);
    printf("  %s --uninstall         Remove scheduled task\n", argv0);
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
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (!opt->help && !opt->apply && !opt->diagnose && !opt->probe && !opt->install && !opt->uninstall)
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

    if (!opt.install && !opt.uninstall)
        g_apply_log = open_log_file("apply.log");

    if (opt.install || opt.uninstall)
    {
        if (opt.install)
            return install_autostart();
        return uninstall_autostart();
    }

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
