/** Fixed-frame namespace mutation client for the Ring-3 Storage Service. */
#include "../include/reist/vfs_namespace_client.h"

#include "../include/reist/vfs_path.h"

#include "x86os.h"

_Static_assert(sizeof(x86os_vfs_namespace_frame_t) ==
                   X86OS_STORAGE_BLOCK_SIZE,
               "namespace request must remain one fixed payload");

static void namespace_zero(void *target, uint32_t length) {
    uint8_t *bytes = target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void namespace_copy(void *target, const void *source,
                           uint32_t length) {
    uint8_t *out = target;
    const uint8_t *in = source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static int namespace_bytes_zero(const void *value, uint32_t length) {
    const uint8_t *bytes = value;
    for (uint32_t index = 0U; index < length; ++index)
        if (bytes[index] != 0U) return 0;
    return 1;
}

static int namespace_deadline(uint32_t timeout_ms, uint64_t *start,
                              uint64_t *deadline) {
    if (timeout_ms == 0U || timeout_ms > 60000U || start == 0 ||
        deadline == 0) return -22;
    if (x86os_monotonic_ms(start) != 0) return -5;
    *deadline = UINT64_MAX - *start < timeout_ms
        ? UINT64_MAX : *start + timeout_ms;
    return 0;
}

static int namespace_remaining(uint64_t start, uint64_t deadline,
                               uint32_t *remaining_ms) {
    uint64_t now = 0U;
    if (remaining_ms == 0 || x86os_monotonic_ms(&now) != 0 || now < start)
        return -5;
    if (now >= deadline) return -110;
    uint64_t remaining = deadline - now;
    if (remaining > 60000U) remaining = 60000U;
    *remaining_ms = (uint32_t)remaining;
    return *remaining_ms != 0U ? 0 : -110;
}

static int namespace_cancel(x86os_storage_handle_t handle, int failure) {
    int status = x86os_storage_cancel(handle);
    return status == 0 || status == -22 ? failure : status;
}

static int namespace_transact(x86os_vfs_namespace_frame_t *frame,
                              uint64_t start, uint64_t deadline) {
    uint32_t timeout_ms = 0U;
    int status = namespace_remaining(start, deadline, &timeout_ms);
    if (status != 0) return status;
    const x86os_storage_submit_t request = {
        X86OS_STORAGE_REQUEST_VERSION, sizeof(request),
        X86OS_STORAGE_VFS_NAMESPACE, 0U, 0U, sizeof(*frame), timeout_ms,
    };
    x86os_storage_handle_t handle = 0U;
    status = x86os_storage_submit(&request, frame, &handle);
    if (status != 0 || handle == 0U) return status != 0 ? status : -5;
    for (;;) {
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) != 0 || now < start)
            return namespace_cancel(handle, -5);
        if (now >= deadline) return namespace_cancel(handle, -110);
        int32_t service_result = -5;
        status = x86os_storage_collect(handle, &service_result, frame);
        if (status == 0) return service_result;
        if (status != -11) return namespace_cancel(handle, status);
        if (x86os_sleep_ms(1U) != 0 && x86os_yield() != 0)
            return namespace_cancel(handle, -5);
    }
}

static int namespace_path(const char *path, char *output,
                          uint32_t *length) {
    return reist_vfs_resolve_path(path, output, length);
}

static int namespace_frame_valid(
        const x86os_vfs_namespace_frame_t *frame, uint32_t operation,
        const char *source, uint32_t source_length,
        const char *destination, uint32_t destination_length) {
    if (frame->version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame->struct_size != sizeof(*frame) ||
        frame->operation != operation || frame->flags != 0U ||
        frame->source_length != source_length ||
        frame->destination_length != destination_length ||
        !namespace_bytes_zero(frame->reserved, sizeof(frame->reserved)))
        return 0;
    for (uint32_t index = 0U; index <= source_length; ++index)
        if (frame->source[index] != source[index]) return 0;
    for (uint32_t index = 0U; index <= destination_length; ++index)
        if (frame->destination[index] != destination[index]) return 0;
    return 1;
}

static int namespace_mutate(const char *source, const char *destination,
                            uint32_t operation, uint32_t timeout_ms) {
    uint64_t start = 0U;
    uint64_t deadline = 0U;
    int status = namespace_deadline(timeout_ms, &start, &deadline);
    if (status != 0) return status;
    x86os_vfs_namespace_frame_t request_frame;
    namespace_zero(&request_frame, sizeof(request_frame));
    request_frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    request_frame.struct_size = sizeof(request_frame);
    request_frame.operation = operation;
    status = namespace_path(source, request_frame.source,
                            &request_frame.source_length);
    if (status != 0) return status;
    if (operation == X86OS_VFS_SHADOW_FS_RENAME) {
        status = namespace_path(destination, request_frame.destination,
                                &request_frame.destination_length);
        if (status != 0) return status;
        if (request_frame.source_length == request_frame.destination_length) {
            uint32_t same = 1U;
            for (uint32_t index = 0U; index <= request_frame.source_length;
                 ++index)
                if (request_frame.source[index] !=
                    request_frame.destination[index]) same = 0U;
            if (same != 0U) return -22;
        }
    }
    for (uint32_t attempt = 0U;
         attempt <= REIST_VFS_NAMESPACE_MAX_RECOVERY_RETRIES; ++attempt) {
        x86os_vfs_namespace_frame_t frame;
        namespace_copy(&frame, &request_frame, sizeof(frame));
        status = namespace_transact(&frame, start, deadline);
        if (status != 0) return status;
        if (!namespace_frame_valid(
                &frame, operation, request_frame.source,
                request_frame.source_length, request_frame.destination,
                request_frame.destination_length)) return -84;
        if (frame.result != -11 ||
            attempt == REIST_VFS_NAMESPACE_MAX_RECOVERY_RETRIES)
            return frame.result;
    }
    return -5;
}

int reist_vfs_unlink(const char *path, uint32_t timeout_ms) {
    return namespace_mutate(path, 0, X86OS_VFS_SHADOW_FS_UNLINK,
                            timeout_ms);
}

int reist_vfs_rename(const char *source, const char *destination,
                     uint32_t timeout_ms) {
    if (destination == 0) return -22;
    return namespace_mutate(source, destination, X86OS_VFS_SHADOW_FS_RENAME,
                            timeout_ms);
}
