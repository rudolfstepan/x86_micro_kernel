/**
 * @file userspace/gui/compositor/desktop_shortcut.c
 * @brief Bounded reist.shortcut/1 sibling-file implementation.
 */
#include "desktop_shortcut.h"

#include "reist/vfs_file_client.h"
#include "reist/vfs_stat_client.h"
#include "../../../include/reist/utf.h"

typedef struct desktop_shortcut_budget {
    uint64_t started_ms;
    uint64_t deadline_ms;
} desktop_shortcut_budget_t;

static void clear_bytes(void *value, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_info(x86os_file_info_t *destination,
                      const x86os_file_info_t *source) {
    if (destination == 0 || source == 0) return;
    for (uint32_t index = 0U; index < sizeof(destination->name); ++index)
        destination->name[index] = source->name[index];
    destination->type = source->type;
    destination->size = source->size;
    destination->create_time = source->create_time;
    destination->modify_time = source->modify_time;
    destination->access_time = source->access_time;
}

static uint8_t fold_ascii(uint8_t value) {
    return value >= 'A' && value <= 'Z'
        ? (uint8_t)(value + ('a' - 'A')) : value;
}

static uint32_t text_length(const char *text, uint32_t capacity,
                            uint32_t *length_out) {
    if (text == 0 || length_out == 0) return 0U;
    for (uint32_t index = 0U; index < capacity; ++index) {
        if (text[index] == '\0') {
            *length_out = index;
            return 1U;
        }
    }
    return 0U;
}

static uint32_t text_equal_ascii_case(const char *left, const char *right,
                                      uint32_t capacity) {
    if (left == 0 || right == 0) return 0U;
    for (uint32_t index = 0U; index < capacity; ++index) {
        uint8_t a = fold_ascii((uint8_t)left[index]);
        uint8_t b = fold_ascii((uint8_t)right[index]);
        if (a != b) return 0U;
        if (a == 0U) return 1U;
    }
    return 0U;
}

static uint32_t identity_equal(const x86os_file_info_t *left,
                               const x86os_file_info_t *right) {
    return left != 0 && right != 0 && left->type == right->type &&
        left->size == right->size &&
        left->create_time == right->create_time &&
        left->modify_time == right->modify_time &&
        left->access_time == right->access_time &&
        text_equal_ascii_case(left->name, right->name,
                              sizeof(left->name));
}

/* A private temporary child changes mutable directory metadata. The stable
 * directory authority is therefore its type, creation identity and leaf. */
static uint32_t directory_authority_equal(
    const x86os_file_info_t *left, const x86os_file_info_t *right) {
    return left != 0 && right != 0 &&
        left->type == X86OS_DIRECTORY && right->type == X86OS_DIRECTORY &&
        left->create_time == right->create_time &&
        text_equal_ascii_case(left->name, right->name,
                              sizeof(left->name));
}

static uint32_t path_is_canonical(const char *path, uint32_t directory,
                                  uint32_t *length_out) {
    uint32_t length = 0U;
    if (!text_length(path, DESKTOP_SHORTCUT_PATH_CAPACITY, &length) ||
        length == 0U || path[0] != '/') return 0U;
    if (length == 1U) {
        if (!directory) return 0U;
        if (length_out != 0) *length_out = length;
        return 1U;
    }
    if (path[length - 1U] == '/') return 0U;
    uint32_t component_start = 1U;
    for (uint32_t index = 1U; index <= length; ++index) {
        uint8_t value = (uint8_t)path[index];
        if (value != '/' && value != 0U) {
            if (value < 0x20U || value == 0x7FU || value == '\\')
                return 0U;
            continue;
        }
        uint32_t component_length = index - component_start;
        if (component_length == 0U ||
            (component_length == 1U && path[component_start] == '.') ||
            (component_length == 2U && path[component_start] == '.' &&
             path[component_start + 1U] == '.')) return 0U;
        component_start = index + 1U;
    }
    size_t scalars = 0U;
    if (!reist_utf8_scan(path, length, &scalars) || scalars == 0U)
        return 0U;
    if (length_out != 0) *length_out = length;
    return 1U;
}

static uint32_t display_name_valid(const char *name, uint32_t *length_out) {
    uint32_t length = 0U;
    if (!text_length(name, DESKTOP_SHORTCUT_DISPLAY_NAME_CAPACITY, &length) ||
        length == 0U) return 0U;
    size_t offset = 0U;
    size_t scalars = 0U;
    while (offset < length) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(name + offset, length - offset,
                                   &consumed, &scalar) ||
            scalar < 0x20U || scalar == 0x7FU) return 0U;
        offset += consumed;
        ++scalars;
    }
    if (scalars == 0U) return 0U;
    if (length_out != 0) *length_out = length;
    return 1U;
}

static uint32_t has_program_extension(const char *path) {
    uint32_t length = 0U;
    if (!text_length(path, DESKTOP_SHORTCUT_PATH_CAPACITY, &length) ||
        length < 4U || path[length - 4U] != '.') return 0U;
    return fold_ascii((uint8_t)path[length - 3U]) == 'p' &&
        fold_ascii((uint8_t)path[length - 2U]) == 'r' &&
        fold_ascii((uint8_t)path[length - 1U]) == 'g';
}

static uint32_t target_kind_valid(uint32_t kind, const char *path) {
    if (kind == DESKTOP_SHORTCUT_TARGET_PROGRAM)
        return has_program_extension(path);
    if (kind == DESKTOP_SHORTCUT_TARGET_FILE)
        return !has_program_extension(path);
    return 0U;
}

uint32_t desktop_shortcut_is_filename(const char *name) {
    uint32_t length = 0U;
    if (!text_length(name, DESKTOP_SHORTCUT_FILENAME_CAPACITY, &length) ||
        length < 5U || length > 12U) return 0U;
    uint32_t dot = length - 4U;
    if (dot == 0U || dot > 8U || name[dot] != '.' ||
        fold_ascii((uint8_t)name[dot + 1U]) != 'l' ||
        fold_ascii((uint8_t)name[dot + 2U]) != 'n' ||
        fold_ascii((uint8_t)name[dot + 3U]) != 'k') return 0U;
    for (uint32_t index = 0U; index < dot; ++index) {
        uint8_t value = (uint8_t)name[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '-')) return 0U;
    }
    return 1U;
}

static uint32_t append_bytes(char *destination, uint32_t capacity,
                             uint32_t *used, const char *source,
                             uint32_t length) {
    if (destination == 0 || used == 0 || source == 0 ||
        *used > capacity || length > capacity - *used) return 0U;
    for (uint32_t index = 0U; index < length; ++index)
        destination[(*used)++] = source[index];
    return 1U;
}

static uint32_t append_text(char *destination, uint32_t capacity,
                            uint32_t *used, const char *source) {
    uint32_t length = 0U;
    return text_length(source, capacity, &length) &&
        append_bytes(destination, capacity, used, source, length);
}

static uint32_t build_child_path(const char *directory, const char *name,
                                 char path[DESKTOP_SHORTCUT_PATH_CAPACITY]) {
    uint32_t directory_length = 0U;
    uint32_t name_length = 0U;
    if (!path_is_canonical(directory, 1U, &directory_length) ||
        !text_length(name, DESKTOP_SHORTCUT_FILENAME_CAPACITY,
                     &name_length) || name_length == 0U) return 0U;
    uint32_t used = 0U;
    if (!append_bytes(path, DESKTOP_SHORTCUT_PATH_CAPACITY, &used,
                      directory, directory_length) ||
        (directory_length != 1U &&
         !append_text(path, DESKTOP_SHORTCUT_PATH_CAPACITY, &used, "/")) ||
        !append_bytes(path, DESKTOP_SHORTCUT_PATH_CAPACITY, &used,
                      name, name_length) ||
        used >= DESKTOP_SHORTCUT_PATH_CAPACITY) return 0U;
    path[used] = '\0';
    return 1U;
}

static int parse_prefixed_line(
    const uint8_t *bytes, uint32_t size, uint32_t *offset,
    const char *prefix, char *value, uint32_t capacity,
    uint32_t *value_length) {
    uint32_t prefix_length = 0U;
    if (bytes == 0 || offset == 0 || prefix == 0 || value == 0 ||
        capacity == 0U || value_length == 0 ||
        !text_length(prefix, DESKTOP_SHORTCUT_FILE_CAPACITY,
                     &prefix_length) || *offset > size ||
        prefix_length > size - *offset) return DESKTOP_SHORTCUT_EINVAL;
    for (uint32_t index = 0U; index < prefix_length; ++index)
        if (bytes[*offset + index] != (uint8_t)prefix[index])
            return DESKTOP_SHORTCUT_EINVAL;
    uint32_t start = *offset + prefix_length;
    uint32_t end = start;
    while (end < size && bytes[end] != '\n') ++end;
    if (end == size || end == start || end - start >= capacity)
        return DESKTOP_SHORTCUT_EINVAL;
    for (uint32_t index = start; index < end; ++index) {
        if (bytes[index] < 0x20U || bytes[index] == 0x7FU)
            return DESKTOP_SHORTCUT_EINVAL;
        value[index - start] = (char)bytes[index];
    }
    *value_length = end - start;
    value[*value_length] = '\0';
    *offset = end + 1U;
    return DESKTOP_SHORTCUT_OK;
}

static int parse_shortcut(const uint8_t *bytes, uint32_t size,
                          desktop_shortcut_resolve_result_t *result) {
    static const char schema[] = "schema=" DESKTOP_SHORTCUT_SCHEMA "\n";
    if (bytes == 0 || result == 0 || size == 0U ||
        size > DESKTOP_SHORTCUT_FILE_CAPACITY)
        return DESKTOP_SHORTCUT_EINVAL;
    for (uint32_t index = 0U; index < sizeof(schema) - 1U; ++index)
        if (index >= size || bytes[index] != (uint8_t)schema[index])
            return DESKTOP_SHORTCUT_EINVAL;
    uint32_t offset = sizeof(schema) - 1U;
    uint32_t length = 0U;
    if (parse_prefixed_line(
            bytes, size, &offset, "name=", result->display_name,
            sizeof(result->display_name), &length) != DESKTOP_SHORTCUT_OK ||
        !display_name_valid(result->display_name, 0))
        return DESKTOP_SHORTCUT_EINVAL;
    char kind[8];
    if (parse_prefixed_line(bytes, size, &offset, "kind=", kind,
                            sizeof(kind), &length) != DESKTOP_SHORTCUT_OK)
        return DESKTOP_SHORTCUT_EINVAL;
    if (length == 7U && kind[0] == 'p' && kind[1] == 'r' &&
        kind[2] == 'o' && kind[3] == 'g' && kind[4] == 'r' &&
        kind[5] == 'a' && kind[6] == 'm')
        result->target_kind = DESKTOP_SHORTCUT_TARGET_PROGRAM;
    else if (length == 4U && kind[0] == 'f' && kind[1] == 'i' &&
             kind[2] == 'l' && kind[3] == 'e')
        result->target_kind = DESKTOP_SHORTCUT_TARGET_FILE;
    else return DESKTOP_SHORTCUT_EINVAL;
    if (parse_prefixed_line(
            bytes, size, &offset, "target=", result->target_path,
            sizeof(result->target_path), &length) != DESKTOP_SHORTCUT_OK ||
        offset != size ||
        !path_is_canonical(result->target_path, 0U, 0) ||
        !target_kind_valid(result->target_kind, result->target_path))
        return DESKTOP_SHORTCUT_EINVAL;
    return DESKTOP_SHORTCUT_OK;
}

static int serialize_shortcut(
    const desktop_shortcut_create_request_t *request,
    char bytes[DESKTOP_SHORTCUT_FILE_CAPACITY], uint32_t *size_out) {
    uint32_t name_length = 0U;
    uint32_t target_length = 0U;
    if (request == 0 || bytes == 0 || size_out == 0 ||
        !display_name_valid(request->display_name, &name_length) ||
        !path_is_canonical(request->target_path, 0U, &target_length) ||
        !target_kind_valid(request->target_kind, request->target_path))
        return DESKTOP_SHORTCUT_EINVAL;
    const char *kind = request->target_kind ==
        DESKTOP_SHORTCUT_TARGET_PROGRAM ? "program" : "file";
    uint32_t used = 0U;
    if (!append_text(bytes, DESKTOP_SHORTCUT_FILE_CAPACITY, &used,
                     "schema=" DESKTOP_SHORTCUT_SCHEMA "\nname=") ||
        !append_bytes(bytes, DESKTOP_SHORTCUT_FILE_CAPACITY, &used,
                      request->display_name, name_length) ||
        !append_text(bytes, DESKTOP_SHORTCUT_FILE_CAPACITY, &used,
                     "\nkind=") ||
        !append_text(bytes, DESKTOP_SHORTCUT_FILE_CAPACITY, &used, kind) ||
        !append_text(bytes, DESKTOP_SHORTCUT_FILE_CAPACITY, &used,
                     "\ntarget=") ||
        !append_bytes(bytes, DESKTOP_SHORTCUT_FILE_CAPACITY, &used,
                      request->target_path, target_length) ||
        !append_text(bytes, DESKTOP_SHORTCUT_FILE_CAPACITY, &used, "\n"))
        return DESKTOP_SHORTCUT_ECAPACITY;
    *size_out = used;
    return DESKTOP_SHORTCUT_OK;
}

static int budget_start(desktop_shortcut_budget_t *budget) {
    if (budget == 0 || x86os_monotonic_ms(&budget->started_ms) != 0)
        return DESKTOP_SHORTCUT_EIO;
    budget->deadline_ms = UINT64_MAX - budget->started_ms <
            DESKTOP_SHORTCUT_OPERATION_TIMEOUT_MS
        ? UINT64_MAX
        : budget->started_ms + DESKTOP_SHORTCUT_OPERATION_TIMEOUT_MS;
    return DESKTOP_SHORTCUT_OK;
}

static int budget_remaining(const desktop_shortcut_budget_t *budget,
                            uint32_t *timeout_out) {
    uint64_t now = 0U;
    if (budget == 0 || timeout_out == 0 ||
        x86os_monotonic_ms(&now) != 0 || now < budget->started_ms)
        return DESKTOP_SHORTCUT_EIO;
    if (now >= budget->deadline_ms) return DESKTOP_SHORTCUT_ETIMEDOUT;
    uint64_t remaining = budget->deadline_ms - now;
    if (remaining > DESKTOP_SHORTCUT_REQUEST_TIMEOUT_MS)
        remaining = DESKTOP_SHORTCUT_REQUEST_TIMEOUT_MS;
    if (remaining == 0U) return DESKTOP_SHORTCUT_ETIMEDOUT;
    *timeout_out = (uint32_t)remaining;
    return DESKTOP_SHORTCUT_OK;
}

static int map_vfs_status(int status) {
    if (status == 0) return DESKTOP_SHORTCUT_OK;
    if (status == DESKTOP_SHORTCUT_ENOENT)
        return DESKTOP_SHORTCUT_ENOENT;
    if (status == DESKTOP_SHORTCUT_ETIMEDOUT)
        return DESKTOP_SHORTCUT_ETIMEDOUT;
    return DESKTOP_SHORTCUT_EIO;
}

static int stat_nofollow(
    const char *path, x86os_file_info_t *info,
    const desktop_shortcut_budget_t *budget) {
    uint32_t timeout = 0U;
    int status = budget_remaining(budget, &timeout);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    return map_vfs_status(reist_vfs_lstat(path, info, timeout));
}

static int path_absent(const char *path,
                       const desktop_shortcut_budget_t *budget) {
    x86os_file_info_t info;
    int status = stat_nofollow(path, &info, budget);
    if (status == DESKTOP_SHORTCUT_ENOENT) return DESKTOP_SHORTCUT_OK;
    if (status == DESKTOP_SHORTCUT_OK) return DESKTOP_SHORTCUT_EEXIST;
    return status;
}

static void close_file_cleanup(reist_vfs_file_handle_t handle) {
    if (handle == REIST_VFS_FILE_INVALID_HANDLE) return;
    (void)reist_vfs_file_set_timeout(handle, 1U);
    (void)reist_vfs_file_close(handle);
}

static int load_shortcut(
    const char *path, const x86os_file_info_t *expected,
    const desktop_shortcut_budget_t *budget,
    desktop_shortcut_resolve_result_t *result) {
    uint32_t timeout = 0U;
    int status = budget_remaining(budget, &timeout);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int open_status = reist_vfs_file_open_flags(
        path, timeout,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT,
        X86OS_O_NOFOLLOW, &handle);
    if (open_status != 0) return map_vfs_status(open_status);
    status = budget_remaining(budget, &timeout);
    if (status != DESKTOP_SHORTCUT_OK ||
        reist_vfs_file_set_timeout(handle, timeout) != 0) {
        close_file_cleanup(handle);
        return status != DESKTOP_SHORTCUT_OK ? status
                                             : DESKTOP_SHORTCUT_EIO;
    }
    x86os_file_info_t actual;
    if (reist_vfs_file_fstat(handle, &actual) != 0 ||
        actual.type != X86OS_FILE ||
        (expected != 0 && !identity_equal(&actual, expected)) ||
        actual.size == 0U || actual.size > DESKTOP_SHORTCUT_FILE_CAPACITY) {
        close_file_cleanup(handle);
        return expected != 0 ? DESKTOP_SHORTCUT_ESTALE
                             : DESKTOP_SHORTCUT_EINVAL;
    }
    uint8_t bytes[DESKTOP_SHORTCUT_FILE_CAPACITY];
    uint32_t used = 0U;
    while (used < actual.size) {
        status = budget_remaining(budget, &timeout);
        if (status != DESKTOP_SHORTCUT_OK ||
            reist_vfs_file_set_timeout(handle, timeout) != 0) {
            close_file_cleanup(handle);
            return status != DESKTOP_SHORTCUT_OK ? status
                                                 : DESKTOP_SHORTCUT_EIO;
        }
        int amount = reist_vfs_file_read_bulk(
            handle, bytes + used, actual.size - used);
        if (amount <= 0 || (uint32_t)amount > actual.size - used) {
            close_file_cleanup(handle);
            return DESKTOP_SHORTCUT_EIO;
        }
        used += (uint32_t)amount;
    }
    uint8_t trailing = 0U;
    if (reist_vfs_file_read_bulk(handle, &trailing, 1U) != 0) {
        close_file_cleanup(handle);
        return DESKTOP_SHORTCUT_ESTALE;
    }
    status = budget_remaining(budget, &timeout);
    if (status != DESKTOP_SHORTCUT_OK ||
        reist_vfs_file_set_timeout(handle, timeout) != 0) {
        close_file_cleanup(handle);
        return status != DESKTOP_SHORTCUT_OK ? status
                                             : DESKTOP_SHORTCUT_EIO;
    }
    if (reist_vfs_file_close(handle) != 0) return DESKTOP_SHORTCUT_EIO;
    return parse_shortcut(bytes, used, result);
}

static uint32_t hash_byte(uint32_t hash, uint8_t value) {
    return (hash ^ value) * 16777619U;
}

static uint32_t request_hash(
    const desktop_shortcut_create_request_t *request) {
    uint32_t hash = 2166136261U;
    const char *values[2] = {
        request->directory_path, request->target_path
    };
    for (uint32_t value = 0U; value < 2U; ++value) {
        for (uint32_t index = 0U;
             index < DESKTOP_SHORTCUT_PATH_CAPACITY &&
             values[value][index] != '\0'; ++index)
            hash = hash_byte(hash, (uint8_t)values[value][index]);
        hash = hash_byte(hash, 0U);
    }
    return hash;
}

static void shortcut_base_name(
    const desktop_shortcut_create_request_t *request,
    char base[9], uint32_t *length_out) {
    uint32_t used = 0U;
    uint32_t name_length = 0U;
    if (text_length(request->target_identity.name,
                    sizeof(request->target_identity.name), &name_length)) {
        for (uint32_t index = 0U; index < name_length && used < 8U; ++index) {
            uint8_t value = (uint8_t)request->target_identity.name[index];
            if (value == '.') break;
            if ((value >= 'a' && value <= 'z'))
                value = (uint8_t)(value - ('a' - 'A'));
            if ((value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') ||
                value == '_' || value == '-')
                base[used++] = (char)value;
        }
    }
    if (used == 0U) {
        base[0] = 'L'; base[1] = 'I'; base[2] = 'N'; base[3] = 'K';
        used = 4U;
    }
    base[used] = '\0';
    *length_out = used;
}

static void final_filename(
    const desktop_shortcut_create_request_t *request, uint32_t attempt,
    char filename[DESKTOP_SHORTCUT_FILENAME_CAPACITY]) {
    static const char digits[] = "0123456789ABCDEF";
    char base[9];
    uint32_t base_length = 0U;
    shortcut_base_name(request, base, &base_length);
    uint32_t used = 0U;
    uint32_t keep = attempt == 0U ? base_length :
        (base_length < 6U ? base_length : 6U);
    for (uint32_t index = 0U; index < keep; ++index)
        filename[used++] = base[index];
    if (attempt != 0U) {
        filename[used++] = digits[(attempt >> 4U) & 0xFU];
        filename[used++] = digits[attempt & 0xFU];
    }
    filename[used++] = '.';
    filename[used++] = 'L';
    filename[used++] = 'N';
    filename[used++] = 'K';
    filename[used] = '\0';
}

static void temporary_filename(
    uint32_t hash, uint32_t attempt,
    char filename[DESKTOP_SHORTCUT_FILENAME_CAPACITY]) {
    static const char digits[] = "0123456789ABCDEF";
    filename[0] = 'R';
    filename[1] = 'L';
    filename[2] = digits[(hash >> 12U) & 0xFU];
    filename[3] = digits[(hash >> 8U) & 0xFU];
    filename[4] = digits[(hash >> 4U) & 0xFU];
    filename[5] = digits[hash & 0xFU];
    filename[6] = digits[(attempt >> 4U) & 0xFU];
    filename[7] = digits[attempt & 0xFU];
    filename[8] = '.';
    filename[9] = 'T';
    filename[10] = 'M';
    filename[11] = 'P';
    filename[12] = '\0';
}

static int write_complete(
    int descriptor, const char *bytes, uint32_t size,
    const desktop_shortcut_budget_t *budget) {
    uint32_t used = 0U;
    uint32_t calls = 0U;
    while (used < size && calls < DESKTOP_SHORTCUT_FILE_CAPACITY) {
        uint32_t timeout = 0U;
        int status = budget_remaining(budget, &timeout);
        if (status != DESKTOP_SHORTCUT_OK) return status;
        (void)timeout;
        int amount = x86os_write(descriptor, bytes + used, size - used);
        if (amount <= 0 || (uint32_t)amount > size - used)
            return DESKTOP_SHORTCUT_EIO;
        used += (uint32_t)amount;
        ++calls;
    }
    return used == size ? DESKTOP_SHORTCUT_OK
                        : DESKTOP_SHORTCUT_ECAPACITY;
}

void desktop_shortcut_create_request_initialize(
    desktop_shortcut_create_request_t *request) {
    if (request != 0) clear_bytes(request, sizeof(*request));
}

void desktop_shortcut_create_result_initialize(
    desktop_shortcut_create_result_t *result) {
    if (result != 0) clear_bytes(result, sizeof(*result));
}

void desktop_shortcut_resolve_result_initialize(
    desktop_shortcut_resolve_result_t *result) {
    if (result != 0) clear_bytes(result, sizeof(*result));
}

int desktop_shortcut_prepare_directory(x86os_file_info_t *identity_out) {
    if (identity_out == 0) return DESKTOP_SHORTCUT_EINVAL;
    clear_bytes(identity_out, sizeof(*identity_out));
    desktop_shortcut_budget_t budget;
    int status = budget_start(&budget);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    x86os_file_info_t info;
    status = stat_nofollow(DESKTOP_SHORTCUT_DIRECTORY, &info, &budget);
    if (status == DESKTOP_SHORTCUT_ENOENT) {
        if (x86os_mkdir(DESKTOP_SHORTCUT_DIRECTORY) != 0) {
            status = stat_nofollow(
                DESKTOP_SHORTCUT_DIRECTORY, &info, &budget);
            if (status != DESKTOP_SHORTCUT_OK) return DESKTOP_SHORTCUT_EIO;
        } else {
            status = stat_nofollow(
                DESKTOP_SHORTCUT_DIRECTORY, &info, &budget);
            if (status != DESKTOP_SHORTCUT_OK) return status;
        }
    } else if (status != DESKTOP_SHORTCUT_OK) {
        return status;
    }
    if (info.type != X86OS_DIRECTORY) return DESKTOP_SHORTCUT_ENOTDIR;
    copy_info(identity_out, &info);
    return DESKTOP_SHORTCUT_OK;
}

int desktop_shortcut_create(
    const desktop_shortcut_create_request_t *request,
    desktop_shortcut_create_result_t *result) {
    if (result != 0) desktop_shortcut_create_result_initialize(result);
    if (request == 0 || result == 0 ||
        !path_is_canonical(request->directory_path, 1U, 0) ||
        request->directory_identity.type != X86OS_DIRECTORY ||
        request->target_identity.type != X86OS_FILE)
        return DESKTOP_SHORTCUT_EINVAL;
    char document[DESKTOP_SHORTCUT_FILE_CAPACITY];
    uint32_t document_size = 0U;
    int status = serialize_shortcut(request, document, &document_size);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    desktop_shortcut_budget_t budget;
    status = budget_start(&budget);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    x86os_file_info_t current_directory;
    status = stat_nofollow(
        request->directory_path, &current_directory, &budget);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    if (!directory_authority_equal(
            &current_directory, &request->directory_identity))
        return DESKTOP_SHORTCUT_ESTALE;
    x86os_file_info_t current_target;
    status = stat_nofollow(request->target_path, &current_target, &budget);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    if (!identity_equal(&current_target, &request->target_identity) ||
        current_target.type != X86OS_FILE)
        return DESKTOP_SHORTCUT_ESTALE;

    uint32_t hash = request_hash(request);
    for (uint32_t attempt = 0U;
         attempt < DESKTOP_SHORTCUT_COLLISION_LIMIT; ++attempt) {
        char final_name[DESKTOP_SHORTCUT_FILENAME_CAPACITY];
        char temp_name[DESKTOP_SHORTCUT_FILENAME_CAPACITY];
        char final_path[DESKTOP_SHORTCUT_PATH_CAPACITY];
        char temp_path[DESKTOP_SHORTCUT_PATH_CAPACITY];
        final_filename(request, attempt, final_name);
        temporary_filename(hash, attempt, temp_name);
        if (!desktop_shortcut_is_filename(final_name) ||
            !build_child_path(request->directory_path, final_name,
                              final_path) ||
            !build_child_path(request->directory_path, temp_name,
                              temp_path))
            return DESKTOP_SHORTCUT_ECAPACITY;
        status = path_absent(final_path, &budget);
        if (status == DESKTOP_SHORTCUT_EEXIST) continue;
        if (status != DESKTOP_SHORTCUT_OK) return status;
        status = path_absent(temp_path, &budget);
        if (status == DESKTOP_SHORTCUT_EEXIST) continue;
        if (status != DESKTOP_SHORTCUT_OK) return status;
        int descriptor = x86os_create(temp_path);
        if (descriptor < 0) {
            if (descriptor == DESKTOP_SHORTCUT_EEXIST) continue;
            return DESKTOP_SHORTCUT_EIO;
        }
        status = write_complete(descriptor, document, document_size, &budget);
        int sync_status =
            status == DESKTOP_SHORTCUT_OK ? x86os_fsync(descriptor) : -1;
        int close_status = x86os_close(descriptor);
        if (status != DESKTOP_SHORTCUT_OK || sync_status != 0 ||
            close_status != 0) {
            (void)x86os_unlink(temp_path);
            return status != DESKTOP_SHORTCUT_OK ? status
                                                 : DESKTOP_SHORTCUT_EIO;
        }
        status = stat_nofollow(
            request->directory_path, &current_directory, &budget);
        if (status != DESKTOP_SHORTCUT_OK ||
            !directory_authority_equal(
                &current_directory, &request->directory_identity)) {
            (void)x86os_unlink(temp_path);
            return status != DESKTOP_SHORTCUT_OK ? status
                                                 : DESKTOP_SHORTCUT_ESTALE;
        }
        status = stat_nofollow(
            request->target_path, &current_target, &budget);
        if (status != DESKTOP_SHORTCUT_OK ||
            !identity_equal(&current_target, &request->target_identity)) {
            (void)x86os_unlink(temp_path);
            return status != DESKTOP_SHORTCUT_OK ? status
                                                 : DESKTOP_SHORTCUT_ESTALE;
        }
        status = path_absent(final_path, &budget);
        if (status != DESKTOP_SHORTCUT_OK ||
            x86os_rename(temp_path, final_path) != 0) {
            (void)x86os_unlink(temp_path);
            return status == DESKTOP_SHORTCUT_EEXIST
                ? DESKTOP_SHORTCUT_EEXIST
                : status != DESKTOP_SHORTCUT_OK
                    ? status : DESKTOP_SHORTCUT_EIO;
        }
        x86os_file_info_t published;
        status = stat_nofollow(final_path, &published, &budget);
        if (status != DESKTOP_SHORTCUT_OK ||
            published.type != X86OS_FILE ||
            published.size != document_size)
            return status != DESKTOP_SHORTCUT_OK ? status
                                                 : DESKTOP_SHORTCUT_EIO;
        result->created = 1U;
        uint32_t name_length = 0U;
        uint32_t path_length = 0U;
        (void)text_length(final_name, sizeof(final_name), &name_length);
        (void)text_length(final_path, sizeof(final_path), &path_length);
        for (uint32_t index = 0U; index <= name_length; ++index)
            result->filename[index] = final_name[index];
        for (uint32_t index = 0U; index <= path_length; ++index)
            result->shortcut_path[index] = final_path[index];
        copy_info(&result->shortcut_identity, &published);
        return DESKTOP_SHORTCUT_OK;
    }
    return DESKTOP_SHORTCUT_ECAPACITY;
}

int desktop_shortcut_resolve(
    const char *shortcut_path,
    const x86os_file_info_t *shortcut_identity,
    desktop_shortcut_resolve_result_t *result) {
    if (result != 0) desktop_shortcut_resolve_result_initialize(result);
    uint32_t path_length = 0U;
    if (shortcut_path == 0 || shortcut_identity == 0 || result == 0 ||
        shortcut_identity->type != X86OS_FILE ||
        !path_is_canonical(shortcut_path, 0U, &path_length))
        return DESKTOP_SHORTCUT_EINVAL;
    uint32_t leaf = path_length;
    while (leaf != 0U && shortcut_path[leaf - 1U] != '/') --leaf;
    if (!desktop_shortcut_is_filename(shortcut_path + leaf))
        return DESKTOP_SHORTCUT_EINVAL;
    desktop_shortcut_budget_t budget;
    int status = budget_start(&budget);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    status = load_shortcut(
        shortcut_path, shortcut_identity, &budget, result);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    x86os_file_info_t target;
    status = stat_nofollow(result->target_path, &target, &budget);
    if (status != DESKTOP_SHORTCUT_OK) return status;
    if (target.type != X86OS_FILE ||
        !target_kind_valid(result->target_kind, result->target_path))
        return DESKTOP_SHORTCUT_ESTALE;
    copy_info(&result->target_identity, &target);
    return DESKTOP_SHORTCUT_OK;
}
