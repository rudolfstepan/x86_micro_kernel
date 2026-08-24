/**
 * @file userspace/storage/lib/vfs_read_client.c
 * @brief Fixed-storage client for VFS read-at and readdir-at operations 6/7.
 */
#include "../include/reist/vfs_read_client.h"
#include "../include/reist/vfs_path.h"

_Static_assert(sizeof(x86os_vfs_shadow_read_frame_t) ==
                   X86OS_STORAGE_BLOCK_SIZE,
               "VFS read frame must fill one protected payload");
_Static_assert(sizeof(x86os_vfs_shadow_readdir_frame_t) ==
                   X86OS_STORAGE_BLOCK_SIZE,
               "VFS readdir frame must fill one protected payload");

static void read_zero(void *target, uint32_t length) {
    uint8_t *bytes = (uint8_t *)target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void read_copy(void *target, const void *source, uint32_t length) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static uint32_t read_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int read_request(void *frame, uint32_t timeout_ms) {
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&now) != 0) return -5;
    uint64_t deadline = UINT64_MAX - now < timeout_ms
        ? UINT64_MAX : now + timeout_ms;
    const x86os_storage_submit_t request = {
        X86OS_STORAGE_REQUEST_VERSION, sizeof(request),
        X86OS_STORAGE_VFS_SHADOW_STAT, 0U, 0U,
        X86OS_STORAGE_BLOCK_SIZE, timeout_ms,
    };
    x86os_storage_handle_t handle = 0U;
    int status = x86os_storage_submit(&request, frame, &handle);
    if (status != 0 || handle == 0U) return status != 0 ? status : -5;
    int32_t service_result = -5;
    for (;;) {
        if (x86os_monotonic_ms(&now) != 0) {
            (void)x86os_storage_cancel(handle); return -5;
        }
        if (now >= deadline) { (void)x86os_storage_cancel(handle); return -110; }
        status = x86os_storage_collect(handle, &service_result, frame);
        if (status == 0) break;
        if (status != -11) { (void)x86os_storage_cancel(handle); return status; }
        if (x86os_sleep_ms(1U) != 0 && x86os_yield() != 0) {
            (void)x86os_storage_cancel(handle); return -5;
        }
    }
    return service_result;
}

static int read_path_equal(const char *left, const char *right,
                           uint32_t length) {
    for (uint32_t index = 0U; index <= length; ++index)
        if (left[index] != right[index]) return 0;
    return 1;
}

int reist_vfs_read_at(const char *path, uint32_t offset, void *data,
                      size_t capacity, uint32_t timeout_ms) {
    if (data == 0 || capacity == 0U ||
        capacity > X86OS_VFS_SHADOW_READ_CAPACITY || timeout_ms == 0U ||
        timeout_ms > 60000U) return -22;
    read_zero(data, (uint32_t)capacity);
    x86os_vfs_shadow_read_frame_t frame;
    read_zero(&frame, sizeof(frame));
    char resolved[192]; uint32_t length = 0U;
    int status = reist_vfs_resolve_path(path, resolved, &length);
    if (status != 0) return status;
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_FS_READ_AT;
    frame.path_length = length;
    frame.offset = offset;
    frame.requested = (uint32_t)capacity;
    read_copy(frame.path, resolved, length + 1U);
    status = read_request(&frame, timeout_ms);
    if (status != 0) return status;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) ||
        frame.operation != X86OS_VFS_SHADOW_FS_READ_AT || frame.flags != 0U ||
        frame.path_length != length || !read_path_equal(frame.path, resolved, length) ||
        frame.offset != offset || frame.requested != (uint32_t)capacity ||
        frame.transferred > capacity) return -84;
    for (uint32_t index = frame.transferred; index < sizeof(frame.data); ++index)
        if (frame.data[index] != 0U) return -84;
    for (uint32_t index = 0U; index < 7U; ++index)
        if (frame.reserved[index] != 0U) return -84;
    if (frame.result != 0) {
        if (frame.transferred != 0U) return -84;
        return frame.result;
    }
    read_copy(data, frame.data, frame.transferred);
    return (int)frame.transferred;
}

int reist_vfs_readdir_at(const char *path, uint32_t index,
                         x86os_file_info_t *info, uint32_t timeout_ms) {
    if (info == 0 || timeout_ms == 0U || timeout_ms > 60000U) return -22;
    read_zero(info, sizeof(*info));
    x86os_vfs_shadow_readdir_frame_t frame;
    read_zero(&frame, sizeof(frame));
    char resolved[192]; uint32_t length = 0U;
    int status = reist_vfs_resolve_path(path, resolved, &length);
    if (status != 0) return status;
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_FS_READDIR_AT;
    frame.path_length = length;
    frame.index = index;
    read_copy(frame.path, resolved, length + 1U);
    status = read_request(&frame, timeout_ms);
    if (status != 0) return status;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) ||
        frame.operation != X86OS_VFS_SHADOW_FS_READDIR_AT || frame.flags != 0U ||
        frame.path_length != length || !read_path_equal(frame.path, resolved, length) ||
        frame.index != index)
        return -84;
    for (uint32_t reserved = 0U; reserved < 4U; ++reserved)
        if (frame.reserved[reserved] != 0U) return -84;
    if (frame.result != 0) {
        const uint8_t *bytes = (const uint8_t *)&frame.info;
        for (uint32_t cursor = 0U; cursor < sizeof(frame.info); ++cursor)
            if (bytes[cursor] != 0U) return -84;
        return frame.result == 1 ? 0 : frame.result;
    }
    if ((frame.info.type != X86OS_FILE && frame.info.type != X86OS_DIRECTORY) ||
        read_length(frame.info.name, sizeof(frame.info.name)) >=
            sizeof(frame.info.name)) return -84;
    read_copy(info, &frame.info, sizeof(*info));
    return 1;
}
