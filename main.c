#include <stdio.h>
#include <string.h>

#include <hidapi.h>

#define HIDPP_PKT_LEN 7
#define MAX_PATHS 32
#define MAX_PATH_LEN 512

static const int LOGITECH_VID = 0x46d;
static const int TARGET_USAGE = 1;
static const int TARGET_USAGE_PAGE = 65280;

static const unsigned char FN_LOCK[] = {0x10, 0x01, 0x09, 0x19, 0x00, 0x00, 0x00};

static const unsigned char DEVICE_INDICES[] = {1, 2, 3, 4, 5, 6, 0xFF};
static const int DEVICE_INDEX_COUNT = 7;

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

static void print_hex(const char *label, const unsigned char *buf, int len)
{
    int i;
    printf("%s (%d bytes): ", label, len);
    for (i = 0; i < len; i++)
        printf("%02x ", buf[i]);
    printf("\n");
}

static int hidpp_ack(const unsigned char *response, int len)
{
    return len >= 3 && response[2] == 0x8F;
}

static const char *slot_label(unsigned char device_index)
{
    return device_index == 0xFF ? "direct (FF)" : NULL;
}

static int send_fn_lock(hid_device *handle, unsigned char device_index, int verbose)
{
    unsigned char pkt[HIDPP_PKT_LEN];
    unsigned char response[65];
    int write_res;
    int read_res;
    char slot_buf[16];

    memcpy(pkt, FN_LOCK, HIDPP_PKT_LEN);
    pkt[1] = device_index;

    write_res = hid_write(handle, pkt, HIDPP_PKT_LEN);
    if (write_res != HIDPP_PKT_LEN)
        return -1;

    if (verbose)
    {
        if (slot_label(device_index))
            printf("  slot %s:\n", slot_label(device_index));
        else
        {
            snprintf(slot_buf, sizeof(slot_buf), "%u", device_index);
            printf("  slot %s:\n", slot_buf);
        }
        print_hex("  sent", pkt, HIDPP_PKT_LEN);
    }

    memset(response, 0, sizeof(response));
    read_res = hid_read_timeout(handle, response, sizeof(response), 500);
    if (read_res > 0)
    {
        if (verbose)
            print_hex("  reply", response, read_res);
        return hidpp_ack(response, read_res) ? 0 : 0;
    }

    if (verbose)
        printf("  -> no reply (write accepted)\n");
    return 0;
}

static int try_hidpp_path(const char *path, int verbose)
{
    hid_device *handle;
    unsigned char idx;
    int i;
    int writes_ok = 0;

    printf("HID++ interface: %s\n", path);

    handle = hid_open_path(path);
    if (handle == NULL)
    {
        printf("  ERROR: could not open device\n");
        return 0;
    }

    for (i = 0; i < DEVICE_INDEX_COUNT; i++)
    {
        idx = DEVICE_INDICES[i];
        if (send_fn_lock(handle, idx, verbose) == 0)
            writes_ok++;
    }

    hid_close(handle);
    return writes_ok > 0;
}

int main(void)
{
    struct hid_device_info *devs, *cur_dev;
    char paths[MAX_PATHS][MAX_PATH_LEN];
    int path_count = 0;
    int i;
    int res;
    int any_ok = 0;

    printf("k400p-fn-lock: universal Fn Lock for Logitech K400+\n");
    printf("(all HID++ receivers, device slots 1-6 and direct FF)\n\n");

    res = hid_init();
    if (res != 0)
    {
        printf("ERROR: hid_init failed\n");
        return 1;
    }

    devs = hid_enumerate(LOGITECH_VID, 0);
    for (cur_dev = devs; cur_dev; cur_dev = cur_dev->next)
    {
        if (cur_dev->usage != TARGET_USAGE || cur_dev->usage_page != TARGET_USAGE_PAGE)
            continue;
        if (path_count >= MAX_PATHS)
            break;
        if (path_seen(paths, path_count, cur_dev->path))
            continue;

        strncpy(paths[path_count], cur_dev->path, MAX_PATH_LEN - 1);
        paths[path_count][MAX_PATH_LEN - 1] = '\0';
        path_count++;
    }
    hid_free_enumeration(devs);

    if (path_count == 0)
    {
        printf("ERROR: no Logitech HID++ interface found.\n");
        printf("Plug in the K400+ USB dongle and turn the keyboard on.\n");
        hid_exit();
        return 1;
    }

    printf("Found %d HID++ interface(s).\n\n", path_count);

    for (i = 0; i < path_count; i++)
    {
        if (try_hidpp_path(paths[i], 1))
            any_ok = 1;
        printf("\n");
    }

    if (!any_ok)
    {
        printf("ERROR: could not send Fn Lock to any interface.\n");
        hid_exit();
        return 1;
    }

    printf("Fn Lock commands sent. Test F2 in Explorer on the K400+ keyboard.\n");
    printf("Setting lasts until reboot.\n");

    hid_exit();
    return 0;
}
