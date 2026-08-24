/**
 * @file userspace/storage/lib/vfs_stat_client.c
 * @brief Fixed-storage client for generic VFS shadow FAT stat operation 3.
 */
#include "../include/reist/vfs_stat_client.h"

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

static int client_append_components(char *output, uint32_t *length,
                                    const char *path) {
    uint32_t cursor = 0U;
    while (path[cursor] != '\0') {
        while (path[cursor] == '/' || path[cursor] == '\\') ++cursor;
        if (path[cursor] == '\0') break;
        uint32_t start = cursor;
        while (path[cursor] != '\0' && path[cursor] != '/' &&
               path[cursor] != '\\') ++cursor;
        uint32_t component_length = cursor - start;
        if (component_length == 1U && path[start] == '.') continue;
        if (component_length == 2U && path[start] == '.' &&
            path[start + 1U] == '.') {
            while (*length > 1U && output[*length - 1U] != '/') --*length;
            if (*length > 1U) --*length;
            output[*length] = '\0';
            continue;
        }
        if (component_length == 0U ||
            component_length >= X86OS_VFS_SHADOW_PATH_CAPACITY) return -22;
        if (*length > 1U) {
            if (*length + 1U >= X86OS_VFS_SHADOW_PATH_CAPACITY) return -36;
            output[(*length)++] = '/';
        }
        if (component_length >=
            X86OS_VFS_SHADOW_PATH_CAPACITY - *length) return -36;
        client_copy(output + *length, path + start, component_length);
        *length += component_length;
        output[*length] = '\0';
    }
    return 0;
}

static int client_drive_mount(char drive_letter, char output[192],
                              uint32_t *length) {
    if (drive_letter >= 'a' && drive_letter <= 'z')
        drive_letter = (char)(drive_letter - ('a' - 'A'));
    for (uint32_t resource = 0U; resource < REIST_VFS_STAT_MAX_DRIVES;
         ++resource) {
        x86os_drive_info_t drive;
        client_zero(&drive, sizeof(drive));
        int status = x86os_drive_info(resource, &drive);
        if (status == 0) break;
        if (status < 0) return -5;
        uint32_t mount_length = client_length(
            drive.mount_point, sizeof(drive.mount_point));
        uint32_t name_length = client_length(drive.name, sizeof(drive.name));
        if (mount_length == 0U ||
            mount_length >= sizeof(drive.mount_point) ||
            mount_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
            drive.mount_point[0U] != '/') continue;
        char mapped = '\0';
        if (mount_length == 1U && drive_letter == 'C') mapped = 'C';
        if (name_length == 4U && drive.name[3U] >= '0' &&
            drive.name[3U] <= '9') {
            if (drive.type == X86OS_DRIVE_FDD)
                mapped = (char)('A' + drive.name[3U] - '0');
            if (drive.type == X86OS_DRIVE_ATA ||
                drive.type == X86OS_DRIVE_AHCI)
                mapped = (char)('C' + drive.name[3U] - '0');
        }
        if (mapped != drive_letter) continue;
        client_zero(output, X86OS_VFS_SHADOW_PATH_CAPACITY);
        client_copy(output, drive.mount_point, mount_length);
        *length = mount_length;
        return 0;
    }
    return -2;
}

static int client_resolve_path(const char *path, char output[192],
                               uint32_t *length) {
    if (path == 0 || output == 0 || length == 0 || path[0U] == '\0')
        return -22;
    if (client_length(path, X86OS_VFS_SHADOW_PATH_CAPACITY) >=
        X86OS_VFS_SHADOW_PATH_CAPACITY) return -36;
    client_zero(output, X86OS_VFS_SHADOW_PATH_CAPACITY);
    output[0U] = '/';
    output[1U] = '\0';
    *length = 1U;
    const char *components = path;
    if (path[1U] == ':') {
        int status = client_drive_mount(path[0U], output, length);
        if (status != 0) return status;
        components = path + 2U;
    } else if (path[0U] != '/' && path[0U] != '\\') {
        char current[X86OS_VFS_SHADOW_PATH_CAPACITY];
        client_zero(current, sizeof(current));
        if (x86os_getcwd(current, sizeof(current)) != 0 ||
            current[0U] != '/' ||
            client_length(current, sizeof(current)) >= sizeof(current))
            return -36;
        int status = client_append_components(output, length, current);
        if (status != 0) return status;
    }
    int status = client_append_components(output, length, components);
    if (status != 0) return status;
    return *length > 0U && *length < X86OS_VFS_SHADOW_PATH_CAPACITY
        ? 0 : -36;
}

static int client_frame_valid(const x86os_vfs_shadow_frame_t *frame,
                              const char *path, uint32_t path_length) {
    if (frame->version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame->struct_size != sizeof(*frame) ||
        frame->operation != X86OS_VFS_SHADOW_FAT_STAT ||
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
    int status = client_resolve_path(path, resolved, &path_length);
    if (status != 0) return status;
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_FAT_STAT;
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
