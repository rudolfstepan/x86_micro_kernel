/**
 * @file userspace/storage/lib/vfs_stat_client.c
 * @brief Fixed-storage client for authoritative VFS filesystem stat operation 5.
 */
#include "../include/reist/vfs_stat_client.h"
#include "../include/reist/vfs_path.h"

static void client_zero(void *target, uint32_t length) {
    uint8_t *bytes = (uint8_t *)target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void client_copy(void *target, const void *source, uint32_t length) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static uint32_t client_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int client_frame_valid(const x86os_vfs_shadow_frame_t *frame,
                              const char *path, uint32_t path_length) {
    if (frame->version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame->struct_size != sizeof(*frame) ||
        frame->operation != X86OS_VFS_SHADOW_FS_STAT_AUTHORITY ||
        frame->flags != 0U || frame->path_length != path_length ||
        frame->path[path_length] != '\0') return 0;
    for (uint32_t index = 0U; index < path_length; ++index)
        if (frame->path[index] != path[index]) return 0;
    for (uint32_t index = 0U; index < 5U; ++index)
        if (frame->reserved[index] != 0U) return 0;
    if (frame->result != 0) {
        const uint8_t *bytes = (const uint8_t *)&frame->info;
        for (uint32_t index = 0U; index < sizeof(frame->info); ++index)
            if (bytes[index] != 0U) return 0;
        return 1;
    }
    if (frame->info.type != X86OS_FILE &&
        frame->info.type != X86OS_DIRECTORY) return 0;
    return client_length(frame->info.name, sizeof(frame->info.name)) <
        sizeof(frame->info.name);
}

static int client_cancel_failure(x86os_storage_handle_t handle,
                                 int failure) {
    int cancel = x86os_storage_cancel(handle);
    return cancel == 0 || cancel == -22 ? failure : cancel;
}

int reist_vfs_stat(const char *path, x86os_file_info_t *info,
                   uint32_t timeout_ms) {
    if (info == 0 || timeout_ms == 0U || timeout_ms > 60000U) return -22;
    client_zero(info, sizeof(*info));
    x86os_vfs_shadow_frame_t frame;
    client_zero(&frame, sizeof(frame));
    char resolved[X86OS_VFS_SHADOW_PATH_CAPACITY];
    uint32_t path_length = 0U;
    int status = reist_vfs_resolve_path(path, resolved, &path_length);
    if (status != 0) return status;
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_FS_STAT_AUTHORITY;
    frame.path_length = path_length;
    client_copy(frame.path, resolved, path_length + 1U);

    uint64_t now = 0U;
    if (x86os_monotonic_ms(&now) != 0) return -5;
    uint64_t deadline = UINT64_MAX - now < timeout_ms
        ? UINT64_MAX : now + timeout_ms;
    const x86os_storage_submit_t request = {
        X86OS_STORAGE_REQUEST_VERSION, sizeof(request),
        X86OS_STORAGE_VFS_SHADOW_STAT, 0U, 0U, sizeof(frame), timeout_ms,
    };
    x86os_storage_handle_t handle = 0U;
    status = x86os_storage_submit(&request, &frame, &handle);
    if (status != 0 || handle == 0U) return status != 0 ? status : -5;

    int32_t service_result = -5;
    for (;;) {
        if (x86os_monotonic_ms(&now) != 0)
            return client_cancel_failure(handle, -5);
        if (now >= deadline)
            return client_cancel_failure(handle, -110);
        status = x86os_storage_collect(handle, &service_result, &frame);
        if (status == 0) break;
        if (status != -11) return client_cancel_failure(handle, status);
        if (x86os_sleep_ms(1U) != 0 && x86os_yield() != 0)
            return client_cancel_failure(handle, -5);
    }
    if (service_result != 0) return service_result;
    if (!client_frame_valid(&frame, resolved, path_length)) return -84;
    if (frame.result == 0) client_copy(info, &frame.info, sizeof(*info));
    return frame.result;
}
