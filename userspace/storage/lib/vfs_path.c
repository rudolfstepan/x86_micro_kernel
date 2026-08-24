/** Shared bounded canonical path resolver for Ring-3 VFS clients. */
#include "../include/reist/vfs_path.h"

static void path_zero(void *target, uint32_t length) {
    uint8_t *bytes = target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void path_copy(void *target, const void *source, uint32_t length) {
    uint8_t *out = target;
    const uint8_t *in = source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static uint32_t path_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int path_append(char *output, uint32_t *length, const char *path) {
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
        path_copy(output + *length, path + start, amount);
        *length += amount;
        output[*length] = '\0';
    }
    return 0;
}

static int path_drive_mount(char drive_letter,
                            char output[X86OS_VFS_SHADOW_PATH_CAPACITY],
                            uint32_t *length) {
    if (drive_letter >= 'a' && drive_letter <= 'z')
        drive_letter = (char)(drive_letter - ('a' - 'A'));
    for (uint32_t resource = 0U; resource < REIST_VFS_PATH_MAX_DRIVES;
         ++resource) {
        x86os_drive_info_t drive;
        path_zero(&drive, sizeof(drive));
        int status = x86os_drive_info(resource, &drive);
        if (status == 0) break;
        if (status < 0) return -5;
        uint32_t mount_length = path_length(drive.mount_point,
                                            sizeof(drive.mount_point));
        uint32_t name_length = path_length(drive.name, sizeof(drive.name));
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
        path_zero(output, X86OS_VFS_SHADOW_PATH_CAPACITY);
        path_copy(output, drive.mount_point, mount_length);
        output[mount_length] = '\0';
        *length = mount_length;
        return 0;
    }
    return -2;
}

int reist_vfs_resolve_path(const char *path,
                           char output[X86OS_VFS_SHADOW_PATH_CAPACITY],
                           uint32_t *length) {
    if (path == 0 || output == 0 || length == 0 || path[0U] == '\0')
        return -22;
    if (path_length(path, X86OS_VFS_SHADOW_PATH_CAPACITY) >=
        X86OS_VFS_SHADOW_PATH_CAPACITY) return -36;
    path_zero(output, X86OS_VFS_SHADOW_PATH_CAPACITY);
    output[0U] = '/'; output[1U] = '\0'; *length = 1U;
    const char *components = path;
    if (path[1U] == ':') {
        int status = path_drive_mount(path[0U], output, length);
        if (status != 0) return status;
        components = path + 2U;
    } else if (path[0U] != '/' && path[0U] != '\\') {
        char current[X86OS_VFS_SHADOW_PATH_CAPACITY];
        path_zero(current, sizeof(current));
        if (x86os_getcwd(current, sizeof(current)) != 0 || current[0U] != '/' ||
            path_length(current, sizeof(current)) >= sizeof(current)) return -36;
        int status = path_append(output, length, current);
        if (status != 0) return status;
    }
    int status = path_append(output, length, components);
    return status != 0 ? status :
        (*length > 0U && *length < X86OS_VFS_SHADOW_PATH_CAPACITY ? 0 : -36);
}
