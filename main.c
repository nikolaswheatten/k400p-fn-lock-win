#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <hidapi.h>

#pragma comment(lib, "hid.lib")

static const int SEQ_LEN = 7;
static const unsigned char K400P_SEQ_FN_LOCK[] = {0x10, 0x01, 0x09, 0x19, 0x00, 0x00, 0x00};
static const int K400P_VID = 0x46d;
static const int K400P_PID = 0xc52b;
static const int TARGET_USAGE = 1;
static const int TARGET_USAGE_PAGE = 65280;
static const char *DEBUG_RUN_ID = "post-fix-v2";

// #region agent log
static void debug_log(const char *hypothesisId, const char *location, const char *message, const char *dataJson)
{
    FILE *f;
    const char *paths[] = {
        "debug-033532.log",
        "..\\debug-033532.log",
        "d:\\Desk\\k400p-fn-lock-win-main\\debug-033532.log",
        NULL
    };
    long long ts = (long long)time(NULL) * 1000;
    int i;

    for (i = 0; paths[i]; i++)
    {
        f = fopen(paths[i], "a");
        if (f)
        {
            fprintf(f,
                "{\"sessionId\":\"033532\",\"runId\":\"%s\",\"hypothesisId\":\"%s\","
                "\"location\":\"%s\",\"message\":\"%s\",\"data\":%s,\"timestamp\":%lld}\n",
                DEBUG_RUN_ID, hypothesisId, location, message, dataJson ? dataJson : "{}", ts);
            fclose(f);
            break;
        }
    }
}
// #endregion

static int path_to_wide(const char *path, wchar_t *wpath, size_t wpath_len)
{
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)wpath_len) == 0)
        return 0;
    return 1;
}

static int send_fn_lock(const char *path, hid_device *handle)
{
    unsigned char buf[65];
    wchar_t wpath[512];
    HANDLE win_handle = INVALID_HANDLE_VALUE;
    PHIDP_PREPARSED_DATA preparsed = NULL;
    HIDP_CAPS caps;
    DWORD gle = 0;
    ULONG report_len = SEQ_LEN;
    BOOL ok;
    int hidapi_result = -1;
    int any_success = 0;

    memset(&caps, 0, sizeof(caps));
    memset(buf, 0, sizeof(buf));
    memcpy(buf, K400P_SEQ_FN_LOCK, SEQ_LEN);

    if (path_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0])))
    {
        win_handle = CreateFileW(
            wpath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);
        gle = GetLastError();
        // #region agent log
        {
            char logbuf[128];
            snprintf(logbuf, sizeof(logbuf),
                "{\"open_rw_ok\":%s,\"win32_error\":%lu}",
                win_handle != INVALID_HANDLE_VALUE ? "true" : "false", gle);
            debug_log("H", "main.c:win32_open", "create_file_rw", logbuf);
        }
        // #endregion

        if (win_handle == INVALID_HANDLE_VALUE)
        {
            win_handle = CreateFileW(
                wpath,
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                0,
                NULL);
            gle = GetLastError();
            // #region agent log
            {
                char logbuf[128];
                snprintf(logbuf, sizeof(logbuf),
                    "{\"open_ro_ok\":%s,\"win32_error\":%lu}",
                    win_handle != INVALID_HANDLE_VALUE ? "true" : "false", gle);
                debug_log("H", "main.c:win32_open", "create_file_ro", logbuf);
            }
            // #endregion
        }
    }

    if (win_handle != INVALID_HANDLE_VALUE)
    {
        if (HidD_GetPreparsedData(win_handle, &preparsed))
        {
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
                caps.OutputReportByteLength > 0)
            {
                report_len = caps.OutputReportByteLength;
            }
            HidD_FreePreparsedData(preparsed);
        }

        // #region agent log
        {
            char logbuf[160];
            snprintf(logbuf, sizeof(logbuf),
                "{\"output_report_length\":%u,\"feature_report_length\":%u,\"send_length\":%lu}",
                caps.OutputReportByteLength, caps.FeatureReportByteLength, report_len);
            debug_log("J", "main.c:caps", "hid_report_lengths", logbuf);
        }
        // #endregion

        ok = HidD_SetOutputReport(win_handle, buf, report_len);
        gle = GetLastError();
        // #region agent log
        {
            char logbuf[128];
            snprintf(logbuf, sizeof(logbuf),
                "{\"ok\":%s,\"win32_error\":%lu,\"report_len\":%lu}",
                ok ? "true" : "false", gle, report_len);
            debug_log("H", "main.c:set_output_report", "HidD_SetOutputReport", logbuf);
        }
        // #endregion
        if (ok)
            any_success = 1;

        CloseHandle(win_handle);
    }

    if (handle != NULL)
    {
        hidapi_result = hid_write(handle, K400P_SEQ_FN_LOCK, SEQ_LEN);
        // #region agent log
        {
            const wchar_t *err = hid_error(handle);
            char err_utf8[256] = "";
            char logbuf[512];
            if (err && err[0])
                wcstombs(err_utf8, err, sizeof(err_utf8) - 1);
            snprintf(logbuf, sizeof(logbuf),
                "{\"method\":\"hid_write\",\"write_result\":%d,\"expected\":%d,\"hid_error\":\"%s\"}",
                hidapi_result, SEQ_LEN, err_utf8);
            debug_log("D", "main.c:hid_write", "hid_write_result", logbuf);
        }
        // #endregion
        if (hidapi_result == SEQ_LEN)
            any_success = 1;
    }

    return any_success ? 0 : 1;
}

int main(void)
{
    int res;
    int result = 1;
    int device_count = 0;
    int match_count = 0;
    hid_device *handle = NULL;
    struct hid_device_info *devs, *cur_dev;

    // #region agent log
    debug_log("E", "main.c:main", "program_start", "{}");
    // #endregion

    res = hid_init();
    // #region agent log
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"hid_init_result\":%d}", res);
        debug_log("E", "main.c:hid_init", "hid_init_done", buf);
    }
    // #endregion
    if (res != 0)
        return 1;

    devs = hid_enumerate(K400P_VID, K400P_PID);
    cur_dev = devs;
    while (cur_dev)
    {
        device_count++;
        // #region agent log
        {
            char buf[512];
            snprintf(buf, sizeof(buf),
                "{\"index\":%d,\"vid\":\"0x%04x\",\"pid\":\"0x%04x\",\"usage\":%d,"
                "\"usage_page\":%d,\"path\":\"%s\",\"matches_target\":%s}",
                device_count,
                cur_dev->vendor_id,
                cur_dev->product_id,
                cur_dev->usage,
                cur_dev->usage_page,
                cur_dev->path ? cur_dev->path : "",
                (cur_dev->usage == TARGET_USAGE && cur_dev->usage_page == TARGET_USAGE_PAGE) ? "true" : "false");
            debug_log("A", "main.c:enumerate", "hid_device_found", buf);
        }
        // #endregion

        if (cur_dev->usage == TARGET_USAGE && cur_dev->usage_page == TARGET_USAGE_PAGE)
        {
            match_count++;
            // #region agent log
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "{\"path\":\"%s\",\"match_index\":%d}",
                    cur_dev->path ? cur_dev->path : "", match_count);
                debug_log("B", "main.c:match", "target_interface_matched", buf);
            }
            // #endregion

            handle = hid_open_path(cur_dev->path);
            // #region agent log
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "{\"open_ok\":%s,\"path\":\"%s\"}",
                    handle ? "true" : "false",
                    cur_dev->path ? cur_dev->path : "");
                debug_log("C", "main.c:open", "hid_open_path_result", buf);
            }
            // #endregion

            result = send_fn_lock(cur_dev->path, handle);
            if (handle != NULL)
                hid_close(handle);
            break;
        }
        cur_dev = cur_dev->next;
    }

    // #region agent log
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "{\"device_count\":%d,\"match_count\":%d,\"exit_code\":%d}",
            device_count, match_count, result);
        debug_log("A", "main.c:exit", "program_finish", buf);
    }
    // #endregion

    hid_free_enumeration(devs);
    hid_exit();
    return result;
}
