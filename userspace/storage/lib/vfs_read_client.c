/**
 * @file userspace/storage/lib/vfs_read_client.c
 * @brief Fixed-storage client for VFS read-at and readdir-at operations 6/7.
 */
#include "../include/reist/vfs_read_client.h"

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

static int read_append(char *output, uint32_t *length, const char *path) {
    uint32_t cursor = 0U;
    while (path[cursor] != '\0') {
        while (path[cursor] == '/' || path[cursor] == '\\') ++cursor;
        if (path[cursor] == '\0') break;
        uint32_t start = cursor;
        while (path[cursor] != '\0' && path[cursor] != '/' &&
               path[cursor] != '\\') ++cursor;
        uint32_t amount = cursor - start;
        if (amount == 1U && path[start] == '.') continue;
        if (amount == 2U && path[start] == '.' && path[start + 1U] == '.') {
            while (*length > 1U && output[*length - 1U] != '/') --*length;
            if (*length > 1U) --*length;
            output[*length] = '\0';
            continue;
        }
        if (amount == 0U || amount >= X86OS_VFS_SHADOW_PATH_CAPACITY)
            return -22;
        if (*length > 1U) {
            if (*length + 1U >= X86OS_VFS_SHADOW_PATH_CAPACITY) return -36;
            output[(*length)++] = '/';
        }
        if (amount >= X86OS_VFS_SHADOW_PATH_CAPACITY - *length) return -36;
        read_copy(output + *length, path + start, amount);
        *length += amount;
        output[*length] = '\0';
    }
    return 0;
}

static int read_drive_mount(char drive_letter, char output[192],
                            uint32_t *length) {
    if (drive_letter >= 'a' && drive_letter <= 'z')
        drive_letter = (char)(drive_letter - ('a' - 'A'));
    for (uint32_t resource = 0U; resource < 22U; ++resource) {
        x86os_drive_info_t drive;
        read_zero(&drive, sizeof(drive));
        int status = x86os_drive_info(resource, &drive);
        if (status == 0) break;
        if (status < 0) return -5;
        uint32_t mount_length = read_length(drive.mount_point,
                                            sizeof(drive.mount_point));
        uint32_t name_length = read_length(drive.name, sizeof(drive.name));
        if (mount_length == 0U || mount_length >= sizeof(drive.mount_point) ||
            mount_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
            drive.mount_point[0U] != '/') continue;
        char mapped = '\0';
        if (mount_length == 1U && drive_letter == 'C') mapped = 'C';
        if (name_length == 4U && drive.name[3U] >= '0' &&
            drive.name[3U] <= '9') {
            if (drive.type == X86OS_DRIVE_FDD)
                mapped = (char)('A' + drive.name[3U] - '0');
            if (drive.type == X86OS_DRIVE_ATA || drive.type == X86OS_DRIVE_AHCI)
                mapped = (char)('C' + drive.name[3U] - '0');
        }
        if (mapped != drive_letter) continue;
        read_zero(output, X86OS_VFS_SHADOW_PATH_CAPACITY);
        read_copy(output, drive.mount_point, mount_length);
        *length = mount_length;
        return 0;
    }
    return -2;
}

static int read_resolve_path(const char *path, char output[192],
                             uint32_t *length) {
    if (path == 0 || output == 0 || length == 0 || path[0U] == '\0') return -22;
    if (read_length(path, X86OS_VFS_SHADOW_PATH_CAPACITY) >=
        X86OS_VFS_SHADOW_PATH_CAPACITY) return -36;
    read_zero(output, X86OS_VFS_SHADOW_PATH_CAPACITY);
    output[0U] = '/'; output[1U] = '\0'; *length = 1U;
    const char *components = path;
    if (path[1U] == ':') {
        int status = read_drive_mount(path[0U], output, length);
        if (status != 0) return status;
        components = path + 2U;
    } else if (path[0U] != '/' && path[0U] != '\\') {
        char current[X86OS_VFS_SHADOW_PATH_CAPACITY];
        read_zero(current, sizeof(current));
        if (x86os_getcwd(current, sizeof(current)) != 0 || current[0U] != '/' ||
            read_length(current, sizeof(current)) >= sizeof(current)) return -36;
        int status = read_append(output, length, current);
        if (status != 0) return status;
    }
    return read_append(output, length, components);
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
    int status = read_resolve_path(path, resolved, &length);
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
    int status = read_resolve_path(path, resolved, &length);
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
