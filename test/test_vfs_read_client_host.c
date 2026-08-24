/** Host behavior test for bounded VFS read-at/readdir-at client adapters. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_read_client.h"

enum { MODE_OK, MODE_NOT_FOUND, MODE_EOF, MODE_TIMEOUT, MODE_CORRUPT };

static int mode;
static uint32_t collect_calls;
static uint32_t submit_calls;
static uint32_t cancel_calls;
static uint64_t clock_ms;
static uint8_t captured[X86OS_STORAGE_BLOCK_SIZE];

int x86os_getcwd(char *buffer, size_t size) {
    static const char cwd[] = "/USR/BIN";
    if (size < sizeof(cwd)) return -1;
    memcpy(buffer, cwd, sizeof(cwd));
    return 0;
}

int x86os_drive_info(uint32_t resource, x86os_drive_info_t *info) {
    memset(info, 0, sizeof(*info));
    if (resource > 1U) return 0;
    info->type = X86OS_DRIVE_ATA;
    strcpy(info->name, resource == 0U ? "hdd0" : "hdd1");
    strcpy(info->mount_point, resource == 0U ? "/" : "/mnt/disk");
    return 1;
}

int x86os_monotonic_ms(uint64_t *milliseconds) {
    *milliseconds = clock_ms++;
    return 0;
}

int x86os_sleep_ms(uint32_t milliseconds) { (void)milliseconds; return 0; }
int x86os_yield(void) { return 0; }

int x86os_storage_submit(const x86os_storage_submit_t *request,
                         const void *data, x86os_storage_handle_t *handle) {
    if (request == 0 || data == 0 || handle == 0 ||
        request->operation != X86OS_STORAGE_VFS_SHADOW_STAT ||
        request->length != X86OS_STORAGE_BLOCK_SIZE) return -22;
    ++submit_calls;
    memcpy(captured, data, sizeof(captured));
    *handle = 0x202U;
    return 0;
}

int x86os_storage_collect(x86os_storage_handle_t handle, int32_t *result,
                          void *data) {
    if (handle != 0x202U || result == 0 || data == 0) return -22;
    ++collect_calls;
    if (mode == MODE_TIMEOUT || collect_calls == 1U) return -11;
    memcpy(data, captured, sizeof(captured));
    *result = 0;
    uint32_t operation = ((x86os_vfs_shadow_read_frame_t *)data)->operation;
    if (operation == X86OS_VFS_SHADOW_FS_READ_AT) {
        x86os_vfs_shadow_read_frame_t *frame = data;
        if (mode == MODE_NOT_FOUND) { frame->result = -2; return 0; }
        frame->result = 0;
        frame->transferred = 5U;
        memcpy(frame->data, "HELLO", 5U);
        if (mode == MODE_CORRUPT) frame->data[6U] = 1U;
        return 0;
    }
    x86os_vfs_shadow_readdir_frame_t *frame = data;
    if (mode == MODE_NOT_FOUND) { frame->result = -2; return 0; }
    if (mode == MODE_EOF) { frame->result = 1; return 0; }
    frame->result = 0;
    frame->info.type = X86OS_FILE;
    frame->info.size = 5U;
    strcpy(frame->info.name, "HELLO.TXT");
    if (mode == MODE_CORRUPT) frame->reserved[0U] = 1U;
    return 0;
}

int x86os_storage_cancel(x86os_storage_handle_t handle) {
    if (handle != 0x202U) return -22;
    ++cancel_calls;
    return 0;
}

static void reset(int selected_mode) {
    mode = selected_mode;
    collect_calls = 0U;
    submit_calls = 0U;
    cancel_calls = 0U;
    clock_ms = 0U;
    memset(captured, 0, sizeof(captured));
}

int main(void) {
    uint8_t data[8];
    reset(MODE_OK);
    if (reist_vfs_read_at("../README.TXT", 3U, data, sizeof(data), 1000U) != 5 ||
        memcmp(data, "HELLO", 5U) != 0 || data[5U] != 0U ||
        strcmp(((x86os_vfs_shadow_read_frame_t *)captured)->path,
               "/USR/README.TXT") != 0) return 1;

    reset(MODE_NOT_FOUND);
    memset(data, 0xA5, sizeof(data));
    if (reist_vfs_read_at("D:\\MISSING", 0U, data, sizeof(data), 1000U) != -2 ||
        data[0U] != 0U || strcmp(((x86os_vfs_shadow_read_frame_t *)captured)->path,
                                "/mnt/disk/MISSING") != 0) return 2;

    x86os_file_info_t info;
    reset(MODE_OK);
    if (reist_vfs_readdir_at("/", 7U, &info, 1000U) != 1 ||
        strcmp(info.name, "HELLO.TXT") != 0 || info.size != 5U) return 3;
    reset(MODE_EOF);
    memset(&info, 0xA5, sizeof(info));
    if (reist_vfs_readdir_at("/", 8U, &info, 1000U) != 0 ||
        info.name[0U] != '\0') return 4;
    reset(MODE_NOT_FOUND);
    if (reist_vfs_readdir_at("/MISSING", 0U, &info, 1000U) != -2) return 5;
    reset(MODE_CORRUPT);
    if (reist_vfs_readdir_at("/", 0U, &info, 1000U) != -84) return 6;
    reset(MODE_TIMEOUT);
    if (reist_vfs_read_at("/README.TXT", 0U, data, sizeof(data), 3U) != -110 ||
        cancel_calls != 1U || collect_calls > 3U) return 7;
    reset(MODE_OK);
    if (reist_vfs_read_at("/README.TXT", 0U, data, 257U, 1000U) != -22 ||
        submit_calls != 0U) return 8;
    return 0;
}
