/** Host behavior test for generation-safe path-backed VFS read sessions. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_file_client.h"

static int cwd_variant;
static int stat_failure;
static int read_failure;
static uint32_t observed_offset;
static char observed_path[X86OS_VFS_SHADOW_PATH_CAPACITY];

int reist_vfs_resolve_path(const char *path, char *output, uint32_t *length) {
    if (path == 0 || output == 0 || length == 0) return -22;
    const char *resolved = path[0] == '/' ? path :
        (cwd_variant == 0 ? "/A/FILE.TXT" : "/B/FILE.TXT");
    size_t amount = strlen(resolved);
    if (amount >= X86OS_VFS_SHADOW_PATH_CAPACITY) return -36;
    memcpy(output, resolved, amount + 1U);
    *length = (uint32_t)amount;
    return 0;
}

int reist_vfs_stat(const char *path, x86os_file_info_t *info,
                   uint32_t timeout_ms) {
    if (stat_failure) { memset(info, 0, sizeof(*info)); return -2; }
    if (timeout_ms != 1000U) return -22;
    memset(info, 0, sizeof(*info));
    info->type = strstr(path, "DIR") != 0 ? X86OS_DIRECTORY : X86OS_FILE;
    info->size = 10U;
    strcpy(info->name, info->type == X86OS_FILE ? "FILE.TXT" : "DIR");
    return 0;
}

int reist_vfs_read_at(const char *path, uint32_t offset, void *data,
                      size_t capacity, uint32_t timeout_ms) {
    observed_offset = offset;
    strcpy(observed_path, path);
    memset(data, 0, capacity);
    if (read_failure) return -5;
    if (timeout_ms != 1000U) return -22;
    uint32_t remaining = offset < 10U ? 10U - offset : 0U;
    uint32_t amount = remaining < capacity ? remaining : (uint32_t)capacity;
    for (uint32_t index = 0U; index < amount; ++index)
        ((uint8_t *)data)[index] = (uint8_t)('0' + offset + index);
    return (int)amount;
}

int main(void) {
    reist_vfs_file_handle_t first = 0U;
    uint8_t data[3];
    if (reist_vfs_file_open("FILE.TXT", 1000U, &first) != 0 || first == 0U)
        return 1;
    cwd_variant = 1;
    if (reist_vfs_file_read(first, data, sizeof(data)) != 3 ||
        observed_offset != 0U || strcmp(observed_path, "/A/FILE.TXT") != 0 ||
        memcmp(data, "012", 3U) != 0) return 2;
    if (reist_vfs_file_read(first, data, sizeof(data)) != 3 ||
        observed_offset != 3U || memcmp(data, "345", 3U) != 0) return 3;

    uint32_t position = 99U;
    if (reist_vfs_file_seek(first, 2, REIST_VFS_SEEK_SET, &position) != 0 ||
        position != 2U) return 4;
    if (reist_vfs_file_read(first, data, sizeof(data)) != 3 ||
        observed_offset != 2U) return 5;
    if (reist_vfs_file_seek(first, -1, REIST_VFS_SEEK_CUR, &position) != 0 ||
        position != 4U) return 6;
    if (reist_vfs_file_seek(first, -2, REIST_VFS_SEEK_END, &position) != 0 ||
        position != 8U) return 7;
    stat_failure = 1;
    if (reist_vfs_file_seek(first, 0, REIST_VFS_SEEK_END, &position) != -2)
        return 8;
    stat_failure = 0;
    read_failure = 1;
    if (reist_vfs_file_read(first, data, sizeof(data)) != -5 ||
        observed_offset != 8U) return 9;
    read_failure = 0;
    if (reist_vfs_file_read(first, data, sizeof(data)) != 2 ||
        observed_offset != 8U) return 10;
    x86os_file_info_t info;
    if (reist_vfs_file_fstat(first, &info) != 0 || info.size != 10U)
        return 11;
    if (reist_vfs_file_seek(first, -11, REIST_VFS_SEEK_END, &position) != -22 ||
        reist_vfs_file_seek(first, INT64_MAX, REIST_VFS_SEEK_CUR,
                            &position) != -75) return 12;
    if (reist_vfs_file_close(first) != 0 ||
        reist_vfs_file_read(first, data, sizeof(data)) != -9 ||
        reist_vfs_file_close(first) != -9) return 13;

    reist_vfs_file_handle_t handles[REIST_VFS_FILE_CAPACITY];
    for (uint32_t index = 0U; index < REIST_VFS_FILE_CAPACITY; ++index)
        if (reist_vfs_file_open("FILE.TXT", 1000U, &handles[index]) != 0)
            return 14;
    reist_vfs_file_handle_t excess = 123U;
    if (reist_vfs_file_open("FILE.TXT", 1000U, &excess) != -24 ||
        excess != REIST_VFS_FILE_INVALID_HANDLE) return 15;
    for (uint32_t index = 0U; index < REIST_VFS_FILE_CAPACITY; ++index)
        if (reist_vfs_file_close(handles[index]) != 0) return 16;
    if (reist_vfs_file_open("/DIR", 1000U, &excess) != -21) return 17;
    return 0;
}
