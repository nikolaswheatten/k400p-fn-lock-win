#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hidapi.h>

static const int SEQ_LEN = 7;
static const int K400P_VID = 0x46d;
static const int K400P_PID = 0xc52b;
static const int TARGET_USAGE = 1;
static const int TARGET_USAGE_PAGE = 65280;
static const unsigned char SW_ID = 0x0B;
static const char *DEBUG_RUN_ID = "post-fix-v3";

static const unsigned char LEGACY_FN_LOCK[] = {0x10, 0x01, 0x09, 0x19, 0x00, 0x00, 0x00};

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

static void debug_log_hex(const char *hypothesisId, const char *location, const char *message,
    const unsigned char *buf, int len)
{
    char data[512];
    char hex[256];
    int i, pos = 0;

    for (i = 0; i < len && pos < (int)sizeof(hex) - 3; i++)
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x", buf[i]);

    snprintf(data, sizeof(data), "{\"len\":%d,\"hex\":\"%s\"}", len, hex);
    debug_log(hypothesisId, location, message, data);
}
// #endregion

static int send_packet(hid_device *handle, const unsigned char *packet, int len, unsigned char *response, int response_cap)
{
    int res;
    int read_res;

    res = hid_write(handle, packet, len);
    if (res != len)
        return -1;

    memset(response, 0, response_cap);
    read_res = hid_read_timeout(handle, response, response_cap, 500);
    return read_res;
}

static int get_feature_index(hid_device *handle, unsigned char device_index, unsigned short feature_id,
    unsigned char *feature_index_out)
{
    unsigned char request[SEQ_LEN];
    unsigned char response[65];
    int read_res;

    request[0] = 0x10;
    request[1] = device_index;
    request[2] = 0x00;
    request[3] = 0x00 | SW_ID;
    request[4] = (unsigned char)((feature_id >> 8) & 0xFF);
    request[5] = (unsigned char)(feature_id & 0xFF);
    request[6] = 0x00;

    // #region agent log
    debug_log_hex("K", "main.c:get_feature", "get_feature_request", request, SEQ_LEN);
    // #endregion

    read_res = send_packet(handle, request, SEQ_LEN, response, sizeof(response));
    // #region agent log
    debug_log_hex("K", "main.c:get_feature", "get_feature_response", response, read_res > 0 ? read_res : 0);
    // #endregion

    if (read_res >= 6 && response[5] != 0x00)
    {
        *feature_index_out = response[5];
        return 0;
    }

    return -1;
}

static int set_fn_lock_packet(hid_device *handle, unsigned char device_index, unsigned char feature_index,
    unsigned char function_with_swid, unsigned char param)
{
    unsigned char request[SEQ_LEN];
    unsigned char response[65];
    int read_res;

    request[0] = 0x10;
    request[1] = device_index;
    request[2] = feature_index;
    request[3] = function_with_swid;
    request[4] = param;
    request[5] = 0x00;
    request[6] = 0x00;

    // #region agent log
    debug_log_hex("K", "main.c:set_fn", "set_fn_request", request, SEQ_LEN);
    // #endregion

    read_res = send_packet(handle, request, SEQ_LEN, response, sizeof(response));
    // #region agent log
    debug_log_hex("K", "main.c:set_fn", "set_fn_response", response, read_res > 0 ? read_res : 0);
    // #endregion

    return (read_res >= 0) ? 0 : -1;
}

static int try_fn_lock(hid_device *handle)
{
    unsigned char feature_index = 0;
    unsigned char device_indices[] = {0x01, 0xFF};
    unsigned char response[65];
    int i;

    for (i = 0; i < 2; i++)
    {
        unsigned char dev_idx = device_indices[i];

        if (get_feature_index(handle, dev_idx, 0x40A2, &feature_index) == 0)
        {
            // #region agent log
            {
                char buf[96];
                snprintf(buf, sizeof(buf),
                    "{\"device_index\":%u,\"feature_index\":%u,\"feature_id\":\"0x40A2\"}",
                    dev_idx, feature_index);
                debug_log("K", "main.c:discover", "found_new_fn_inversion", buf);
            }
            // #endregion
            if (set_fn_lock_packet(handle, dev_idx, feature_index, 0x10 | SW_ID, 0x00) == 0)
                return 0;
        }

        if (get_feature_index(handle, dev_idx, 0x40A0, &feature_index) == 0)
        {
            // #region agent log
            {
                char buf[96];
                snprintf(buf, sizeof(buf),
                    "{\"device_index\":%u,\"feature_index\":%u,\"feature_id\":\"0x40A0\"}",
                    dev_idx, feature_index);
                debug_log("K", "main.c:discover", "found_fn_inversion", buf);
            }
            // #endregion
            if (set_fn_lock_packet(handle, dev_idx, feature_index, 0x10 | SW_ID, 0x00) == 0)
                return 0;
        }
    }

    feature_index = 0x09;
    if (set_fn_lock_packet(handle, 0x01, feature_index, 0x10 | SW_ID, 0x00) == 0)
        return 0;
    if (set_fn_lock_packet(handle, 0x01, feature_index, 0x10 | SW_ID, 0x01) == 0)
        return 0;

    // #region agent log
    debug_log_hex("L", "main.c:fallback", "legacy_fn_lock_request", LEGACY_FN_LOCK, SEQ_LEN);
    // #endregion
    if (send_packet(handle, LEGACY_FN_LOCK, SEQ_LEN, response, sizeof(response)) >= 0)
        return 0;

    return -1;
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
    if (res != 0)
        return 1;

    devs = hid_enumerate(K400P_VID, K400P_PID);
    cur_dev = devs;
    while (cur_dev)
    {
        device_count++;
        if (cur_dev->usage == TARGET_USAGE && cur_dev->usage_page == TARGET_USAGE_PAGE)
        {
            match_count++;
            handle = hid_open_path(cur_dev->path);
            if (handle == NULL)
                break;

            result = try_fn_lock(handle);
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
