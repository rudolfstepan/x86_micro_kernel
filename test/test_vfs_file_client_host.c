/** Host behavior test for stable service-owned VFS read objects. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_file_client.h"

static int cwd_variant;
static int stat_failure;
static int read_failure;
static uint32_t observed_offset;
static uint32_t open_calls;
static uint32_t close_calls;
static uint32_t delegate_calls;
static uint32_t delegated_rights;
static uint32_t observed_timeout;
static int delegation_pending;
static int bulk_corrupt;
static uint32_t next_request = 1U;
static char opened_path[X86OS_VFS_SHADOW_PATH_CAPACITY];

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

int x86os_storage_submit(const x86os_storage_submit_t *request,
                         const void *data, x86os_storage_handle_t *handle) {
    if (request == 0 || data == 0 || handle == 0 ||
        (request->operation != X86OS_STORAGE_VFS_SHADOW_STAT &&
         request->operation != X86OS_STORAGE_VFS_BULK_READ) ||
        request->length != X86OS_STORAGE_BLOCK_SIZE) return -22;
    observed_timeout = request->timeout_ms;
    uint32_t operation = ((const x86os_vfs_shadow_object_frame_t *)data)->operation;
    if (request->operation == X86OS_STORAGE_VFS_BULK_READ) {
        if (operation != X86OS_VFS_SHADOW_OBJECT_BULK_READ) return -22;
        *handle = next_request++;
        return 0;
    }
    if (operation == X86OS_VFS_SHADOW_OBJECT_READ) {
        x86os_vfs_shadow_object_read_frame_t *frame =
            (x86os_vfs_shadow_object_read_frame_t *)(uintptr_t)data;
        if ((frame->object_token != 77U && frame->object_token != 88U) ||
            frame->service_generation != 9U)
            return -9;
        observed_offset = frame->offset;
        if (read_failure) {
            frame->result = -5;
        } else {
            uint32_t remaining = frame->offset < 10U
                ? 10U - frame->offset : 0U;
            frame->transferred = remaining < frame->requested
                ? remaining : frame->requested;
            for (uint32_t index = 0U; index < frame->transferred; ++index)
                frame->data[index] = (uint8_t)('0' + frame->offset + index);
        }
    } else if (operation == X86OS_VFS_SHADOW_OBJECT_DELEGATE) {
        x86os_vfs_shadow_object_delegate_frame_t *frame =
            (x86os_vfs_shadow_object_delegate_frame_t *)(uintptr_t)data;
        if (frame->object_token != 77U || frame->service_generation != 9U ||
            frame->target_pid != 42 || frame->target_generation != 7U ||
            frame->rights == 0U) return -22;
        delegated_rights = frame->rights;
        delegation_pending = 1;
        ++delegate_calls;
    } else {
        x86os_vfs_shadow_object_frame_t *frame =
            (x86os_vfs_shadow_object_frame_t *)(uintptr_t)data;
        if (operation == X86OS_VFS_SHADOW_OBJECT_OPEN ||
            operation == X86OS_VFS_SHADOW_OBJECT_OPEN_RIGHTS) {
            ++open_calls;
            strcpy(opened_path, frame->path);
            if (strstr(frame->path, "DIR") != 0) {
                frame->result = -21;
            } else {
                frame->object_token = 77U;
                frame->service_generation = 9U;
                frame->info.type = X86OS_FILE;
                frame->info.size = 10U;
                strcpy(frame->info.name, "FILE.TXT");
            }
        } else if (operation == X86OS_VFS_SHADOW_OBJECT_ADOPT) {
            if (!delegation_pending) {
                frame->result = -11;
            } else {
                delegation_pending = 0;
                frame->object_token = 88U;
                frame->service_generation = 9U;
                frame->flags = delegated_rights;
            }
        } else if (operation == X86OS_VFS_SHADOW_OBJECT_FSTAT) {
            if ((frame->object_token != 77U && frame->object_token != 88U) ||
                frame->service_generation != 9U)
                return -9;
            if (stat_failure) {
                frame->result = -2;
            } else {
                frame->info.type = X86OS_FILE;
                frame->info.size = 10U;
            }
        } else if (operation == X86OS_VFS_SHADOW_OBJECT_CLOSE) {
            if ((frame->object_token != 77U && frame->object_token != 88U) ||
                frame->service_generation != 9U)
                return -9;
            ++close_calls;
        } else {
            return -22;
        }
    }
    *handle = next_request++;
    return 0;
}

int x86os_storage_collect(x86os_storage_handle_t handle, int32_t *result,
                          void *data) {
    (void)data;
    if (handle == 0U || result == 0) return -22;
    *result = 0;
    return 0;
}

int x86os_storage_cancel(x86os_storage_handle_t handle) {
    return handle == 0U ? -22 : 0;
}

static uint32_t test_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFU;
}

int x86os_storage_bulk_collect(x86os_storage_handle_t handle,
        int32_t *result, void *frame_data, void *data, uint32_t capacity,
        uint32_t *transferred) {
    if (handle == 0U || result == 0 || frame_data == 0 || data == 0 ||
        transferred == 0) return -22;
    x86os_vfs_shadow_object_bulk_read_frame_t *frame = frame_data;
    observed_offset = frame->offset;
    uint32_t remaining = frame->offset < 10U ? 10U - frame->offset : 0U;
    uint32_t amount = remaining < frame->requested
        ? remaining : frame->requested;
    if (amount > capacity) return -90;
    for (uint32_t index = 0U; index < amount; ++index)
        ((uint8_t *)data)[index] = (uint8_t)('0' + frame->offset + index);
    frame->transferred = amount;
    frame->data_crc32 = test_crc32(data, amount) ^
        (bulk_corrupt ? 1U : 0U);
    *result = 0;
    *transferred = amount;
    return 0;
}

int x86os_monotonic_ms(uint64_t *value) {
    static uint64_t now;
    if (value == 0) return -22;
    *value = ++now;
    return 0;
}

int x86os_sleep_ms(uint32_t milliseconds) {
    return milliseconds == 0U ? -22 : 0;
}

int x86os_yield(void) { return 0; }

int main(void) {
    reist_vfs_file_handle_t first = 0U;
    uint8_t data[3];
    if (reist_vfs_file_open("FILE.TXT", 1000U, &first) != 0 || first == 0U ||
        strcmp(opened_path, "/A/FILE.TXT") != 0) return 1;
    if (reist_vfs_file_set_timeout(first, 17U) != 0 ||
        reist_vfs_file_set_timeout(first, 0U) != -22 ||
        reist_vfs_file_set_timeout(first, 60001U) != -22 ||
        reist_vfs_file_set_timeout(0U, 17U) != -9) return 25;
    cwd_variant = 1;
    if (reist_vfs_file_read(first, data, sizeof(data)) != 3 ||
        observed_offset != 0U || strcmp(opened_path, "/A/FILE.TXT") != 0 ||
        memcmp(data, "012", 3U) != 0 || observed_timeout != 17U) return 2;
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
    uint8_t bulk[10];
    if (reist_vfs_file_seek(first, 0, REIST_VFS_SEEK_SET, &position) != 0 ||
        reist_vfs_file_read_bulk(first, bulk, sizeof(bulk)) != 10 ||
        memcmp(bulk, "0123456789", sizeof(bulk)) != 0) return 21;
    if (reist_vfs_file_seek(first, 0, REIST_VFS_SEEK_SET, &position) != 0)
        return 22;
    bulk_corrupt = 1;
    if (reist_vfs_file_read_bulk(first, bulk, sizeof(bulk)) != -84) return 23;
    bulk_corrupt = 0;
    if (reist_vfs_file_read(first, data, sizeof(data)) != 3 ||
        observed_offset != 0U) return 24;
    x86os_file_info_t info;
    if (reist_vfs_file_fstat(first, &info) != 0 || info.size != 10U)
        return 11;
    uint32_t rights = 0U;
    x86os_process_identity_t target = {1U, sizeof(target), 42, 7U};
    if (reist_vfs_file_rights(first, &rights) != 0 ||
        rights != REIST_VFS_FILE_RIGHT_DATA ||
        reist_vfs_file_delegate(first, &target,
                                REIST_VFS_FILE_RIGHT_READ) != -13)
        return 18;
    if (reist_vfs_file_seek(first, -11, REIST_VFS_SEEK_END, &position) != -22 ||
        reist_vfs_file_seek(first, INT64_MAX, REIST_VFS_SEEK_CUR,
                            &position) != -75) return 12;
    if (reist_vfs_file_close(first) != 0 || close_calls != 1U ||
        reist_vfs_file_read(first, data, sizeof(data)) != -9 ||
        reist_vfs_file_close(first) != -9) return 13;

    reist_vfs_file_handle_t source = REIST_VFS_FILE_INVALID_HANDLE;
    reist_vfs_file_handle_t adopted = REIST_VFS_FILE_INVALID_HANDLE;
    if (reist_vfs_file_open_rights(
            "FILE.TXT", 1000U, REIST_VFS_FILE_RIGHT_ALL, &source) != 0 ||
        reist_vfs_file_delegate(source, &target,
                                REIST_VFS_FILE_RIGHT_READ) != 0 ||
        delegate_calls != 1U ||
        reist_vfs_file_adopt(1000U, &adopted) != 0 || adopted == 0U ||
        reist_vfs_file_rights(adopted, &rights) != 0 ||
        rights != REIST_VFS_FILE_RIGHT_READ ||
        reist_vfs_file_read(adopted, data, 1U) != 1 ||
        reist_vfs_file_fstat(adopted, &info) != -13 ||
        reist_vfs_file_seek(adopted, 0, REIST_VFS_SEEK_SET, &position) != -13)
        return 19;
    reist_vfs_file_handle_t duplicate = 99U;
    if (reist_vfs_file_adopt(1000U, &duplicate) != -11 || duplicate != 0U ||
        reist_vfs_file_close(adopted) != 0 ||
        reist_vfs_file_fstat(source, &info) != 0 ||
        reist_vfs_file_close(source) != 0) return 20;

    reist_vfs_file_handle_t handles[REIST_VFS_FILE_CAPACITY];
    for (uint32_t index = 0U; index < REIST_VFS_FILE_CAPACITY; ++index)
        if (reist_vfs_file_open("FILE.TXT", 1000U, &handles[index]) != 0)
            return 14;
    reist_vfs_file_handle_t excess = 123U;
    uint32_t calls_before_excess = open_calls;
    if (reist_vfs_file_open("FILE.TXT", 1000U, &excess) != -24 ||
        excess != REIST_VFS_FILE_INVALID_HANDLE ||
        open_calls != calls_before_excess) return 15;
    for (uint32_t index = 0U; index < REIST_VFS_FILE_CAPACITY; ++index)
        if (reist_vfs_file_close(handles[index]) != 0) return 16;
    if (reist_vfs_file_open("/DIR", 1000U, &excess) != -21) return 17;
    return 0;
}
