/** Fixed-frame symbolic-link client for the Ring-3 Storage Service. */
#include "../include/reist/vfs_symlink_client.h"

#include "../include/reist/vfs_path.h"

_Static_assert(sizeof(x86os_vfs_symlink_frame_t) == X86OS_STORAGE_BLOCK_SIZE,
               "symbolic-link request must remain one fixed payload");

static void symlink_zero(void *target, uint32_t length) {
    uint8_t *bytes = target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void symlink_copy(void *target, const void *source, uint32_t length) {
    uint8_t *out = target;
    const uint8_t *in = source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static uint32_t symlink_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int symlink_reserved_zero(const uint32_t *reserved) {
    for (uint32_t index = 0U; index < 25U; ++index)
        if (reserved[index] != 0U) return 0;
    return 1;
}

static int symlink_cancel_failure(x86os_storage_handle_t handle,
                                  int failure) {
    int cancel = x86os_storage_cancel(handle);
    return cancel == 0 || cancel == -22 ? failure : cancel;
}

static int symlink_deadline(uint32_t timeout_ms, uint64_t *start,
                            uint64_t *deadline) {
    if (start == 0 || deadline == 0 ||
        x86os_monotonic_ms(start) != 0) return -5;
    *deadline = UINT64_MAX - *start < timeout_ms
        ? UINT64_MAX : *start + timeout_ms;
    return 0;
}

static int symlink_remaining(uint64_t start, uint64_t deadline,
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

static int symlink_transact(x86os_vfs_symlink_frame_t *frame,
                            uint32_t request_operation,
                            uint64_t start, uint64_t deadline) {
    uint32_t timeout_ms = 0U;
    int status = symlink_remaining(start, deadline, &timeout_ms);
    if (status != 0) return status;
    const x86os_storage_submit_t request = {
        X86OS_STORAGE_REQUEST_VERSION, sizeof(request), request_operation,
        0U, 0U, sizeof(*frame), timeout_ms,
    };
    x86os_storage_handle_t handle = 0U;
    status = x86os_storage_submit(&request, frame, &handle);
    if (status != 0 || handle == 0U) return status != 0 ? status : -5;
    for (;;) {
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) != 0 || now < start)
            return symlink_cancel_failure(handle, -5);
        if (now >= deadline)
            return symlink_cancel_failure(handle, -110);
        int32_t service_result = -5;
        status = x86os_storage_collect(handle, &service_result, frame);
        if (status == 0) return service_result;
        if (status != -11) return symlink_cancel_failure(handle, status);
        if (x86os_sleep_ms(1U) != 0 && x86os_yield() != 0)
            return symlink_cancel_failure(handle, -5);
    }
}

static int symlink_path(const char *path, x86os_vfs_symlink_frame_t *frame) {
    uint32_t length = 0U;
    int status = reist_vfs_resolve_path(path, frame->path, &length);
    if (status != 0) return status;
    frame->path_length = length;
    return 0;
}

static int symlink_frame_valid(const x86os_vfs_symlink_frame_t *frame,
                               uint32_t operation, const char *path,
                               uint32_t path_length) {
    if (frame->version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame->struct_size != sizeof(*frame) ||
        frame->operation != operation || frame->flags != 0U ||
        frame->path_length != path_length ||
        frame->path[path_length] != '\0' ||
        !symlink_reserved_zero(frame->reserved)) return 0;
    for (uint32_t index = 0U; index <= path_length; ++index)
        if (frame->path[index] != path[index]) return 0;
    return 1;
}

int reist_vfs_symlink(const char *target, const char *link_path,
                      uint32_t timeout_ms) {
    if (timeout_ms == 0U || timeout_ms > 60000U) return -22;
    uint64_t start = 0U;
    uint64_t deadline = 0U;
    int status = symlink_deadline(timeout_ms, &start, &deadline);
    if (status != 0) return status;
    uint32_t target_length = symlink_length(
        target, X86OS_VFS_SYMLINK_TARGET_CAPACITY);
    if (target_length == 0U ||
        target_length >= X86OS_VFS_SYMLINK_TARGET_CAPACITY) return -36;
    for (uint32_t index = 0U; index < target_length; ++index) {
        uint8_t value = (uint8_t)target[index];
        if (value < 0x20U || value > 0x7EU) return -22;
    }
    x86os_vfs_symlink_frame_t frame;
    symlink_zero(&frame, sizeof(frame));
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_FS_SYMLINK;
    frame.target_length = target_length;
    symlink_copy(frame.target, target, target_length + 1U);
    status = symlink_path(link_path, &frame);
    if (status != 0) return status;
    x86os_vfs_symlink_frame_t request_frame;
    symlink_copy(&request_frame, &frame, sizeof(frame));
    char expected_path[X86OS_VFS_SHADOW_PATH_CAPACITY];
    uint32_t expected_length = frame.path_length;
    symlink_copy(expected_path, frame.path, expected_length + 1U);
    for (uint32_t attempt = 0U;
         attempt <= REIST_VFS_SYMLINK_MAX_RECOVERY_RETRIES; ++attempt) {
        symlink_copy(&frame, &request_frame, sizeof(frame));
        status = symlink_transact(
            &frame, X86OS_STORAGE_VFS_SYMLINK, start, deadline);
        if (status != 0) return status;
        if (!symlink_frame_valid(&frame, X86OS_VFS_SHADOW_FS_SYMLINK,
                                 expected_path, expected_length) ||
            frame.target_length != target_length ||
            frame.target[target_length] != '\0') return -84;
        for (uint32_t index = 0U; index <= target_length; ++index)
            if (frame.target[index] != target[index]) return -84;
        if (frame.result != -11 ||
            attempt == REIST_VFS_SYMLINK_MAX_RECOVERY_RETRIES)
            return frame.result;
    }
    return -5;
}

int reist_vfs_readlink(const char *path, char *target, size_t capacity,
                       uint32_t timeout_ms) {
    if (target == 0 || capacity == 0U || timeout_ms == 0U ||
        timeout_ms > 60000U) return -22;
    uint64_t start = 0U;
    uint64_t deadline = 0U;
    int status = symlink_deadline(timeout_ms, &start, &deadline);
    if (status != 0) return status;
    x86os_vfs_symlink_frame_t frame;
    symlink_zero(&frame, sizeof(frame));
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_FS_READLINK;
    status = symlink_path(path, &frame);
    if (status != 0) return status;
    char expected_path[X86OS_VFS_SHADOW_PATH_CAPACITY];
    uint32_t expected_length = frame.path_length;
    symlink_copy(expected_path, frame.path, expected_length + 1U);
    status = symlink_transact(
        &frame, X86OS_STORAGE_VFS_SHADOW_STAT, start, deadline);
    if (status != 0) return status;
    if (!symlink_frame_valid(&frame, X86OS_VFS_SHADOW_FS_READLINK,
                             expected_path, expected_length)) return -84;
    if (frame.result != 0) return frame.result;
    if (frame.target_length == 0U ||
        frame.target_length >= X86OS_VFS_SYMLINK_TARGET_CAPACITY ||
        frame.target[frame.target_length] != '\0') return -84;
    uint32_t copied = frame.target_length < capacity
        ? frame.target_length : (uint32_t)capacity;
    symlink_copy(target, frame.target, copied);
    return (int)copied;
}
