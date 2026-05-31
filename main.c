#include <stdio.h>

#include <hidapi.h>

static const int SEQ_LEN = 7;
static const unsigned char K400P_SEQ_FN_LOCK[] = {0x10, 0x01, 0x09, 0x19, 0x00, 0x00, 0x00};
static const int K400P_VID = 0x46d;
static const int K400P_PID = 0xc52b;
static const int TARGET_USAGE = 1;
static const int TARGET_USAGE_PAGE = 65280;

int main(void)
{
    int res;
    int result = 1;
    hid_device *handle;
    struct hid_device_info *devs, *cur_dev;

    if (hid_init() != 0)
        return 1;

    devs = hid_enumerate(K400P_VID, K400P_PID);
    cur_dev = devs;
    while (cur_dev)
    {
        if (cur_dev->usage == TARGET_USAGE && cur_dev->usage_page == TARGET_USAGE_PAGE)
        {
            handle = hid_open_path(cur_dev->path);
            if (handle == NULL)
                break;

            res = hid_write(handle, K400P_SEQ_FN_LOCK, SEQ_LEN);
            if (res != SEQ_LEN)
                fprintf(stderr, "error: %ls\n", hid_error(handle));

            hid_close(handle);
            result = (res == SEQ_LEN) ? 0 : 1;
            break;
        }
        cur_dev = cur_dev->next;
    }

    hid_free_enumeration(devs);
    hid_exit();
    return result;
}
