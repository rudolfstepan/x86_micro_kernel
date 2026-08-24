/** Fixed, generation-safe, path-backed read sessions for Ring-3 clients. */
#include "../include/reist/vfs_file_client.h"

#include "../include/reist/vfs_path.h"
#include "../include/reist/vfs_read_client.h"
#include "../include/reist/vfs_stat_client.h"

#define FILE_HANDLE_SLOT_MASK 0xFFU
#define FILE_HANDLE_GENERATION_MAX 0x00FFFFFFU

typedef struct {
    char path[X86OS_VFS_SHADOW_PATH_CAPACITY];
    uint32_t offset;
    uint32_t timeout_ms;
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

int reist_vfs_file_open(const char *path, uint32_t timeout_ms,
                        reist_vfs_file_handle_t *handle) {
    if (handle == 0 || timeout_ms == 0U || timeout_ms > 60000U) return -22;
    *handle = REIST_VFS_FILE_INVALID_HANDLE;
    char resolved[X86OS_VFS_SHADOW_PATH_CAPACITY];
    uint32_t length = 0U;
    int status = reist_vfs_resolve_path(path, resolved, &length);
    if (status != 0) return status;
    x86os_file_info_t info;
    status = reist_vfs_stat(resolved, &info, timeout_ms);
    if (status != 0) return status;
    if (info.type != X86OS_FILE) return -21;
    for (uint32_t slot = 0U; slot < REIST_VFS_FILE_CAPACITY; ++slot) {
        file_session_t *session = &sessions[slot];
        if (session->in_use != 0U || session->retired != 0U) continue;
        if (session->generation == 0U) session->generation = 1U;
        file_zero(session->path, sizeof(session->path));
        file_copy(session->path, resolved, length + 1U);
        session->offset = 0U;
        session->timeout_ms = timeout_ms;
        session->in_use = 1U;
        *handle = file_handle(slot, session->generation);
        return 0;
    }
    return -24;
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
    int amount = reist_vfs_read_at(session->path, session->offset, data,
                                   capacity, session->timeout_ms);
    if (amount <= 0) return amount;
    if (UINT32_MAX - session->offset < (uint32_t)amount) return -75;
    session->offset += (uint32_t)amount;
    return amount;
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
    return reist_vfs_stat(session->path, info, session->timeout_ms);
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
    uint32_t base = 0U;
    if (whence == REIST_VFS_SEEK_CUR) base = session->offset;
    if (whence == REIST_VFS_SEEK_END) {
        x86os_file_info_t info;
        status = reist_vfs_stat(session->path, &info, session->timeout_ms);
        if (status != 0) return status;
        if (info.type != X86OS_FILE || info.size > UINT32_MAX) return -75;
        base = (uint32_t)info.size;
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

int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    uint32_t slot = 0U;
    file_session_t *session = 0;
    int status = file_resolve(handle, &slot, &session);
    (void)slot;
    if (status != 0) return status;
    file_zero(session->path, sizeof(session->path));
    session->offset = 0U;
    session->timeout_ms = 0U;
    session->in_use = 0U;
    if (session->generation == FILE_HANDLE_GENERATION_MAX) {
        session->retired = 1U;
    } else {
        ++session->generation;
    }
    return 0;
}
