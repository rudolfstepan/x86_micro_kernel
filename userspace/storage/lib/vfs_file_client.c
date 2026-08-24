/** Fixed, generation-safe client sessions over service-owned VFS objects. */
#include "../include/reist/vfs_file_client.h"

#include "../include/reist/vfs_path.h"

#define FILE_HANDLE_SLOT_MASK 0xFFU
#define FILE_HANDLE_GENERATION_MAX 0x00FFFFFFU

typedef struct {
    uint32_t object_token;
    uint32_t service_generation;
    uint32_t offset;
    uint32_t timeout_ms;
    uint32_t rights;
    uint32_t generation;
    uint8_t in_use;
    uint8_t retired;
} file_session_t;

static file_session_t sessions[REIST_VFS_FILE_CAPACITY];

static void file_zero(void *target, uint32_t length) {
    uint8_t *bytes = target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void file_copy(void *target, const void *source, uint32_t length) {
    uint8_t *out = target;
    const uint8_t *in = source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static int file_bytes_zero(const void *source, uint32_t length) {
    const uint8_t *bytes = source;
    for (uint32_t index = 0U; index < length; ++index)
        if (bytes[index] != 0U) return 0;
    return 1;
}

static reist_vfs_file_handle_t file_handle(uint32_t slot,
                                           uint32_t generation) {
    return (generation << 8U) | (slot + 1U);
}

static int file_resolve(reist_vfs_file_handle_t handle, uint32_t *slot_out,
                        file_session_t **session_out) {
    uint32_t encoded_slot = handle & FILE_HANDLE_SLOT_MASK;
    uint32_t generation = handle >> 8U;
    if (encoded_slot == 0U || encoded_slot > REIST_VFS_FILE_CAPACITY ||
        generation == 0U || slot_out == 0 || session_out == 0) return -9;
    uint32_t slot = encoded_slot - 1U;
    file_session_t *session = &sessions[slot];
    if (session->in_use == 0U || session->retired != 0U ||
        session->generation != generation) return -9;
    *slot_out = slot;
    *session_out = session;
    return 0;
}

static int file_transact(void *frame, uint32_t timeout_ms) {
    x86os_storage_submit_t request = {
        .version = X86OS_STORAGE_REQUEST_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_STORAGE_VFS_SHADOW_STAT,
        .resource = 0U,
        .offset = 0U,
        .length = X86OS_STORAGE_BLOCK_SIZE,
        .timeout_ms = timeout_ms,
    };
    x86os_storage_handle_t request_handle = 0U;
    int status = x86os_storage_submit(&request, frame, &request_handle);
    if (status != 0 || request_handle == 0U)
        return status != 0 ? status : -5;
    uint32_t start = x86os_uptime_ms();
    uint32_t deadline = start + timeout_ms;
    if (deadline < start) deadline = UINT32_MAX;
    for (;;) {
        uint32_t now = x86os_uptime_ms();
        if (now < start) { (void)x86os_storage_cancel(request_handle); return -5; }
        if (now >= deadline) {
            (void)x86os_storage_cancel(request_handle);
            return -110;
        }
        int32_t service_result = 0;
        status = x86os_storage_collect(request_handle, &service_result, frame);
        if (status == 0) return service_result;
        if (status != -11) {
            (void)x86os_storage_cancel(request_handle);
            return status;
        }
        if (x86os_sleep_ms(1U) != 0 && x86os_yield() != 0) {
            (void)x86os_storage_cancel(request_handle);
            return -5;
        }
    }
}

static int file_control(file_session_t *session, uint32_t operation,
                        x86os_file_info_t *info) {
    x86os_vfs_shadow_object_frame_t frame;
    file_zero(&frame, sizeof(frame));
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = operation;
    frame.object_token = session->object_token;
    frame.service_generation = session->service_generation;
    int status = file_transact(&frame, session->timeout_ms);
    if (status != 0) return status;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) || frame.operation != operation ||
        frame.flags != 0U || frame.path_length != 0U ||
        frame.object_token != session->object_token ||
        frame.service_generation != session->service_generation)
        return -84;
    if (frame.result != 0) return frame.result;
    if (info != 0) file_copy(info, &frame.info, sizeof(*info));
    return 0;
}

static void file_release(file_session_t *session) {
    session->object_token = 0U;
    session->service_generation = 0U;
    session->offset = 0U;
    session->timeout_ms = 0U;
    session->rights = 0U;
    session->in_use = 0U;
    if (session->generation == FILE_HANDLE_GENERATION_MAX) {
        session->retired = 1U;
    } else {
        ++session->generation;
    }
}

static int file_open(const char *path, uint32_t timeout_ms, uint32_t rights,
                     uint32_t operation, reist_vfs_file_handle_t *handle) {
    if (handle == 0 || timeout_ms == 0U || timeout_ms > 60000U ||
        rights == 0U || (rights & ~X86OS_VFS_OBJECT_RIGHT_ALL) != 0U ||
        (operation != X86OS_VFS_SHADOW_OBJECT_OPEN &&
         operation != X86OS_VFS_SHADOW_OBJECT_OPEN_RIGHTS)) return -22;
    *handle = REIST_VFS_FILE_INVALID_HANDLE;
    uint32_t free_slot = UINT32_MAX;
    for (uint32_t slot = 0U; slot < REIST_VFS_FILE_CAPACITY; ++slot)
        if (sessions[slot].in_use == 0U && sessions[slot].retired == 0U) {
            free_slot = slot;
            break;
        }
    if (free_slot == UINT32_MAX) return -24;
    char resolved[X86OS_VFS_SHADOW_PATH_CAPACITY];
    uint32_t length = 0U;
    int status = reist_vfs_resolve_path(path, resolved, &length);
    if (status != 0) return status;
    x86os_vfs_shadow_object_frame_t frame;
    file_zero(&frame, sizeof(frame));
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = operation;
    frame.flags = operation == X86OS_VFS_SHADOW_OBJECT_OPEN_RIGHTS
        ? rights : 0U;
    frame.path_length = length;
    file_copy(frame.path, resolved, length + 1U);
    status = file_transact(&frame, timeout_ms);
    if (status != 0) return status;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) ||
        frame.operation != operation ||
        frame.flags != (operation == X86OS_VFS_SHADOW_OBJECT_OPEN_RIGHTS
            ? rights : 0U) || frame.path_length != length ||
        frame.result != 0 || frame.object_token == 0U ||
        frame.service_generation == 0U) return frame.result != 0
            ? frame.result : -84;
    if (frame.info.type != X86OS_FILE) return -21;
    file_session_t *session = &sessions[free_slot];
    if (session->generation == 0U) session->generation = 1U;
    session->object_token = frame.object_token;
    session->service_generation = frame.service_generation;
    session->offset = 0U;
    session->timeout_ms = timeout_ms;
    session->rights = rights;
    session->in_use = 1U;
    *handle = file_handle(free_slot, session->generation);
    return 0;
}

int reist_vfs_file_open(const char *path, uint32_t timeout_ms,
                        reist_vfs_file_handle_t *handle) {
    return file_open(path, timeout_ms, X86OS_VFS_OBJECT_RIGHT_DATA,
                     X86OS_VFS_SHADOW_OBJECT_OPEN, handle);
}

int reist_vfs_file_open_rights(const char *path, uint32_t timeout_ms,
                               uint32_t rights,
                               reist_vfs_file_handle_t *handle) {
    return file_open(path, timeout_ms, rights,
                     X86OS_VFS_SHADOW_OBJECT_OPEN_RIGHTS, handle);
}

int reist_vfs_file_read(reist_vfs_file_handle_t handle, void *data,
                        size_t capacity) {
    if (data == 0 || capacity == 0U ||
        capacity > X86OS_VFS_SHADOW_READ_CAPACITY) return -22;
    uint32_t slot = 0U;
    file_session_t *session = 0;
    int status = file_resolve(handle, &slot, &session);
    (void)slot;
    if (status != 0) return status;
    if ((session->rights & X86OS_VFS_OBJECT_RIGHT_READ) == 0U) return -13;
    x86os_vfs_shadow_object_read_frame_t frame;
    file_zero(&frame, sizeof(frame));
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_OBJECT_READ;
    frame.object_token = session->object_token;
    frame.service_generation = session->service_generation;
    frame.offset = session->offset;
    frame.requested = (uint32_t)capacity;
    status = file_transact(&frame, session->timeout_ms);
    if (status != 0) return status;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) ||
        frame.operation != X86OS_VFS_SHADOW_OBJECT_READ ||
        frame.flags != 0U || frame.object_token != session->object_token ||
        frame.service_generation != session->service_generation ||
        frame.offset != session->offset || frame.requested != capacity ||
        frame.transferred > capacity) return -84;
    if (frame.result != 0) return frame.result;
    if (UINT32_MAX - session->offset < frame.transferred) return -75;
    file_copy(data, frame.data, frame.transferred);
    session->offset += frame.transferred;
    return (int)frame.transferred;
}

int reist_vfs_file_fstat(reist_vfs_file_handle_t handle,
                         x86os_file_info_t *info) {
    if (info == 0) return -22;
    file_zero(info, sizeof(*info));
    uint32_t slot = 0U;
    file_session_t *session = 0;
    int status = file_resolve(handle, &slot, &session);
    (void)slot;
    if (status != 0) return status;
    if ((session->rights & X86OS_VFS_OBJECT_RIGHT_STAT) == 0U) return -13;
    return file_control(session, X86OS_VFS_SHADOW_OBJECT_FSTAT, info);
}

int reist_vfs_file_seek(reist_vfs_file_handle_t handle, int64_t offset,
                        uint32_t whence, uint32_t *new_offset) {
    if (new_offset == 0 || whence > REIST_VFS_SEEK_END) return -22;
    *new_offset = 0U;
    uint32_t slot = 0U;
    file_session_t *session = 0;
    int status = file_resolve(handle, &slot, &session);
    (void)slot;
    if (status != 0) return status;
    if ((session->rights & X86OS_VFS_OBJECT_RIGHT_SEEK) == 0U) return -13;
    uint32_t base = whence == REIST_VFS_SEEK_CUR ? session->offset : 0U;
    if (whence == REIST_VFS_SEEK_END) {
        x86os_file_info_t info;
        status = file_control(session, X86OS_VFS_SHADOW_OBJECT_FSTAT, &info);
        if (status != 0) return status;
        if (info.type != X86OS_FILE) return -21;
        base = info.size;
    }
    uint32_t candidate;
    if (offset >= 0) {
        uint64_t positive = (uint64_t)offset;
        if (positive > UINT32_MAX - base) return -75;
        candidate = base + (uint32_t)positive;
    } else {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1U;
        if (magnitude > base) return -22;
        candidate = base - (uint32_t)magnitude;
    }
    session->offset = candidate;
    *new_offset = candidate;
    return 0;
}

int reist_vfs_file_rights(reist_vfs_file_handle_t handle,
                          uint32_t *rights) {
    if (rights == 0) return -22;
    *rights = 0U;
    uint32_t slot = 0U;
    file_session_t *session = 0;
    int status = file_resolve(handle, &slot, &session);
    (void)slot;
    if (status != 0) return status;
    *rights = session->rights;
    return 0;
}

int reist_vfs_file_delegate(
        reist_vfs_file_handle_t handle,
        const x86os_process_identity_t *target, uint32_t rights) {
    if (target == 0 || target->version != 1U ||
        target->struct_size != sizeof(*target) || target->pid <= 0 ||
        target->generation == 0U || rights == 0U ||
        (rights & ~X86OS_VFS_OBJECT_RIGHT_ALL) != 0U) return -22;
    uint32_t slot = 0U;
    file_session_t *session = 0;
    int status = file_resolve(handle, &slot, &session);
    (void)slot;
    if (status != 0) return status;
    if ((session->rights & X86OS_VFS_OBJECT_RIGHT_DELEGATE) == 0U ||
        (rights & ~session->rights) != 0U) return -13;
    x86os_vfs_shadow_object_delegate_frame_t frame;
    file_zero(&frame, sizeof(frame));
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_OBJECT_DELEGATE;
    frame.object_token = session->object_token;
    frame.service_generation = session->service_generation;
    frame.target_pid = target->pid;
    frame.target_generation = target->generation;
    frame.rights = rights;
    status = file_transact(&frame, session->timeout_ms);
    if (status != 0) return status;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) ||
        frame.operation != X86OS_VFS_SHADOW_OBJECT_DELEGATE ||
        frame.flags != 0U || frame.object_token != session->object_token ||
        frame.service_generation != session->service_generation ||
        frame.target_pid != target->pid ||
        frame.target_generation != target->generation ||
        frame.rights != rights ||
        !file_bytes_zero(frame.reserved, sizeof(frame.reserved))) return -84;
    return frame.result;
}

int reist_vfs_file_adopt(uint32_t timeout_ms,
                         reist_vfs_file_handle_t *handle) {
    if (handle == 0 || timeout_ms == 0U || timeout_ms > 60000U) return -22;
    *handle = REIST_VFS_FILE_INVALID_HANDLE;
    uint32_t free_slot = UINT32_MAX;
    for (uint32_t slot = 0U; slot < REIST_VFS_FILE_CAPACITY; ++slot)
        if (sessions[slot].in_use == 0U && sessions[slot].retired == 0U) {
            free_slot = slot;
            break;
        }
    if (free_slot == UINT32_MAX) return -24;
    x86os_vfs_shadow_object_frame_t frame;
    file_zero(&frame, sizeof(frame));
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_OBJECT_ADOPT;
    int status = file_transact(&frame, timeout_ms);
    if (status != 0) return status;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) ||
        frame.operation != X86OS_VFS_SHADOW_OBJECT_ADOPT ||
        frame.path_length != 0U ||
        !file_bytes_zero(frame.path, sizeof(frame.path)) ||
        !file_bytes_zero(&frame.info, sizeof(frame.info)) ||
        !file_bytes_zero(frame.reserved, sizeof(frame.reserved))) return -84;
    if (frame.result != 0) return frame.result;
    if (frame.object_token == 0U || frame.service_generation == 0U ||
        frame.flags == 0U ||
        (frame.flags & ~X86OS_VFS_OBJECT_RIGHT_ALL) != 0U) return -84;
    for (uint32_t slot = 0U; slot < REIST_VFS_FILE_CAPACITY; ++slot)
        if (sessions[slot].in_use != 0U &&
            sessions[slot].object_token == frame.object_token &&
            sessions[slot].service_generation == frame.service_generation)
            return -17;
    file_session_t *session = &sessions[free_slot];
    if (session->generation == 0U) session->generation = 1U;
    session->object_token = frame.object_token;
    session->service_generation = frame.service_generation;
    session->offset = 0U;
    session->timeout_ms = timeout_ms;
    session->rights = frame.flags;
    session->in_use = 1U;
    *handle = file_handle(free_slot, session->generation);
    return 0;
}

int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    uint32_t slot = 0U;
    file_session_t *session = 0;
    int status = file_resolve(handle, &slot, &session);
    (void)slot;
    if (status != 0) return status;
    status = file_control(session, X86OS_VFS_SHADOW_OBJECT_CLOSE, 0);
    file_release(session);
    return status;
}
