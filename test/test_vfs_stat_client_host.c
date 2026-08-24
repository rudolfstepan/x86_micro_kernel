/** Host behavior test for the controlled Ring-3 VFS stat client adapter. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_stat_client.h"

enum { MODE_OK, MODE_NOT_FOUND, MODE_TIMEOUT, MODE_CORRUPT };

static int mode;
static uint32_t collect_calls;
static uint32_t submit_calls;
static uint32_t sleep_calls;
static uint32_t cancel_calls;
static uint64_t clock_ms;
static x86os_vfs_shadow_frame_t captured;

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
    info->sectors = 4096U;
    strcpy(info->name, resource == 0U ? "hdd0" : "hdd1");
    strcpy(info->mount_point, resource == 0U ? "/" : "/mnt/disk");
    return 1;
}

int x86os_monotonic_ms(uint64_t *milliseconds) {
    *milliseconds = clock_ms++;
    return 0;
}

int x86os_sleep_ms(uint32_t milliseconds) {
    (void)milliseconds;
    ++sleep_calls;
    return 0;
}

int x86os_yield(void) { return 0; }

int x86os_storage_submit(const x86os_storage_submit_t *request,
                         const void *data, x86os_storage_handle_t *handle) {
    if (request == NULL || data == NULL || handle == NULL ||
        request->operation != X86OS_STORAGE_VFS_SHADOW_STAT ||
        request->length != X86OS_STORAGE_BLOCK_SIZE) return -22;
    ++submit_calls;
    memcpy(&captured, data, sizeof(captured));
    *handle = 0x101U;
    return 0;
}

int x86os_storage_collect(x86os_storage_handle_t handle, int32_t *result,
                          void *data) {
    if (handle != 0x101U || result == NULL || data == NULL) return -22;
    ++collect_calls;
    if (mode == MODE_TIMEOUT || collect_calls == 1U) return -11;
    x86os_vfs_shadow_frame_t *frame = data;
    *frame = captured;
    *result = 0;
    if (mode == MODE_NOT_FOUND) {
        frame->result = -2;
        return 0;
    }
    frame->result = 0;
    strcpy(frame->info.name, "README.TXT");
    frame->info.type = X86OS_FILE;
    frame->info.size = 7U;
    if (mode == MODE_CORRUPT) frame->reserved[0U] = 1U;
    return 0;
}

int x86os_storage_cancel(x86os_storage_handle_t handle) {
    if (handle != 0x101U) return -22;
    ++cancel_calls;
    return 0;
}

static void reset(int selected_mode) {
    mode = selected_mode;
    collect_calls = 0U;
    submit_calls = 0U;
    sleep_calls = 0U;
    cancel_calls = 0U;
    clock_ms = 0U;
    memset(&captured, 0, sizeof(captured));
}

int main(void) {
    x86os_file_info_t info;
    reset(MODE_OK);
    if (reist_vfs_stat("../README.TXT", &info, 1000U) != 0 ||
        strcmp(captured.path, "/USR/README.TXT") != 0 ||
        captured.operation != X86OS_VFS_SHADOW_FS_STAT_AUTHORITY ||
        strcmp(info.name, "README.TXT") != 0 || info.size != 7U ||
        sleep_calls != 1U) return 1;

    reset(MODE_OK);
    if (reist_vfs_stat("D:\\DOCS\\..\\README.TXT", &info, 1000U) != 0 ||
        strcmp(captured.path, "/mnt/disk/README.TXT") != 0) return 2;

    reset(MODE_NOT_FOUND);
    memset(&info, 0xA5, sizeof(info));
    if (reist_vfs_stat("/MISSING", &info, 1000U) != -2 ||
        info.type != 0U || info.name[0U] != '\0') return 3;

    reset(MODE_CORRUPT);
    if (reist_vfs_stat("/README.TXT", &info, 1000U) != -84) return 4;

    reset(MODE_TIMEOUT);
    if (reist_vfs_stat("/README.TXT", &info, 3U) != -110 ||
        collect_calls > 3U || sleep_calls > 3U || cancel_calls != 1U)
        return 5;

    char long_path[193];
    long_path[0U] = '/';
    for (uint32_t index = 1U; index < sizeof(long_path) - 1U; ++index)
        long_path[index] = 'A';
    long_path[sizeof(long_path) - 1U] = '\0';
    reset(MODE_OK);
    if (reist_vfs_stat(long_path, &info, 1000U) != -36 ||
        submit_calls != 0U) return 6;
    return 0;
}
