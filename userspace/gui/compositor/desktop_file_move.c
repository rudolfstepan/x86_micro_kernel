/**
 * @file userspace/gui/compositor/desktop_file_move.c
 * @brief Verified bounded cross-directory MOVE in the desktop process.
 */
#include "desktop_file_move.h"

#include "reist/vfs_file_client.h"
#include "reist/vfs_stat_client.h"
#include "../../../include/reist/utf.h"

typedef struct desktop_file_move_budget {
    uint64_t started_ms;
    uint64_t deadline_ms;
} desktop_file_move_budget_t;

static uint8_t move_chunk[DESKTOP_FILE_MOVE_CHUNK_CAPACITY];

static void clear_bytes(void *value, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_info(x86os_file_info_t *destination,
                      const x86os_file_info_t *source) {
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

static uint32_t path_equal(const char *left, const char *right) {
    return text_equal_ascii_case(
        left, right, DESKTOP_FILE_MOVE_PATH_CAPACITY);
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

static uint32_t directory_authority_equal(
    const x86os_file_info_t *left, const x86os_file_info_t *right) {
    return left != 0 && right != 0 &&
        left->type == X86OS_DIRECTORY && right->type == X86OS_DIRECTORY &&
        left->create_time == right->create_time &&
        text_equal_ascii_case(left->name, right->name,
                              sizeof(left->name));
}

static uint32_t canonical_path(const char *path, uint32_t directory,
                               uint32_t *length_out) {
    uint32_t length = 0U;
    if (!text_length(path, DESKTOP_FILE_MOVE_PATH_CAPACITY, &length) ||
        length == 0U || path[0] != '/') return 0U;
    if (length == 1U) {
        if (!directory) return 0U;
        if (length_out != 0) *length_out = 1U;
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

static uint32_t path_has_prefix(const char *path, const char *prefix) {
    for (uint32_t index = 0U;
         index < DESKTOP_FILE_MOVE_PATH_CAPACITY; ++index) {
        uint8_t value = fold_ascii((uint8_t)path[index]);
        uint8_t expected = fold_ascii((uint8_t)prefix[index]);
        if (expected == 0U) return value == 0U || value == '/';
        if (value != expected) return 0U;
    }
    return 0U;
}

static uint32_t hex_digit(uint8_t value) {
    value = fold_ascii(value);
    return (value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f');
}

static uint32_t private_temp_name(const char *name) {
    uint32_t length = 0U;
    if (name == 0 ||
        !text_length(name, 13U, &length) || length != 12U ||
        fold_ascii((uint8_t)name[0]) != 'r' ||
        fold_ascii((uint8_t)name[1]) != 'm') return 0U;
    for (uint32_t index = 2U; index < 8U; ++index)
        if (!hex_digit((uint8_t)name[index])) return 0U;
    return name[8] == '.' &&
        fold_ascii((uint8_t)name[9]) == 't' &&
        fold_ascii((uint8_t)name[10]) == 'm' &&
        fold_ascii((uint8_t)name[11]) == 'p' &&
        name[12] == '\0';
}

static uint32_t path_leaf(
    const char *path, uint32_t length, const char **leaf_out) {
    uint32_t offset = length;
    while (offset != 0U && path[offset - 1U] != '/') --offset;
    if (offset >= length || leaf_out == 0) return 0U;
    *leaf_out = path + offset;
    return 1U;
}

static uint32_t protected_path(const char *path) {
    static const char *const roots[] = {
        "/trash", "/bin", "/sbin", "/libexec", "/etc", "/usr",
        "/boot", "/dev", "/mnt", "/proc", "/sys", "/run", "/var"
    };
    for (uint32_t index = 0U;
         index < sizeof(roots) / sizeof(roots[0]); ++index)
        if (path_has_prefix(path, roots[index])) return 1U;
    return 0U;
}

uint32_t desktop_file_move_source_allowed(const char *path) {
    uint32_t length = 0U;
    const char *leaf = 0;
    return canonical_path(path, 0U, &length) &&
        path_leaf(path, length, &leaf) && !private_temp_name(leaf) &&
        !protected_path(path);
}

uint32_t desktop_file_move_destination_allowed(const char *path) {
    uint32_t length = 0U;
    return canonical_path(path, 1U, &length) && length > 1U &&
        !protected_path(path);
}

static uint32_t source_belongs_to_directory(
    const char *source, const char *directory, const char *expected_leaf) {
    uint32_t source_length = 0U;
    uint32_t directory_length = 0U;
    const char *leaf = 0;
    if (!canonical_path(source, 0U, &source_length) ||
        !canonical_path(directory, 1U, &directory_length) ||
        !path_leaf(source, source_length, &leaf) ||
        !text_equal_ascii_case(leaf, expected_leaf,
                               sizeof(((x86os_file_info_t *)0)->name)))
        return 0U;
    uint32_t parent_length = (uint32_t)(leaf - source);
    if (parent_length > 1U) --parent_length;
    uint32_t normalized_directory = directory_length;
    if (normalized_directory > 1U &&
        directory[normalized_directory - 1U] == '/')
        --normalized_directory;
    if (parent_length != normalized_directory) return 0U;
    for (uint32_t index = 0U; index < parent_length; ++index)
        if (fold_ascii((uint8_t)source[index]) !=
            fold_ascii((uint8_t)directory[index])) return 0U;
    return 1U;
}

static uint32_t build_child_path(
    const char *directory, const char *name,
    char path[DESKTOP_FILE_MOVE_PATH_CAPACITY]) {
    uint32_t directory_length = 0U;
    uint32_t name_length = 0U;
    if (!canonical_path(directory, 1U, &directory_length) ||
        !text_length(name, sizeof(((x86os_file_info_t *)0)->name),
                     &name_length) || name_length == 0U)
        return 0U;
    uint32_t separator = directory_length == 1U ? 0U : 1U;
    uint64_t needed =
        (uint64_t)directory_length + separator + name_length + 1U;
    if (needed > DESKTOP_FILE_MOVE_PATH_CAPACITY) return 0U;
    uint32_t used = 0U;
    for (; used < directory_length; ++used) path[used] = directory[used];
    if (separator) path[used++] = '/';
    for (uint32_t index = 0U; index < name_length; ++index)
        path[used++] = name[index];
    path[used] = '\0';
    return 1U;
}

static int budget_start(desktop_file_move_budget_t *budget) {
    if (budget == 0 || x86os_monotonic_ms(&budget->started_ms) != 0)
        return DESKTOP_FILE_MOVE_EIO;
    budget->deadline_ms = UINT64_MAX - budget->started_ms <
            DESKTOP_FILE_MOVE_TIMEOUT_MS
        ? UINT64_MAX : budget->started_ms + DESKTOP_FILE_MOVE_TIMEOUT_MS;
    return DESKTOP_FILE_MOVE_OK;
}

static int budget_remaining(const desktop_file_move_budget_t *budget,
                            uint32_t *timeout_out) {
    uint64_t now = 0U;
    if (budget == 0 || timeout_out == 0 ||
        x86os_monotonic_ms(&now) != 0 || now < budget->started_ms)
        return DESKTOP_FILE_MOVE_EIO;
    if (now >= budget->deadline_ms) return DESKTOP_FILE_MOVE_ETIMEDOUT;
    uint64_t remaining = budget->deadline_ms - now;
    if (remaining > DESKTOP_FILE_MOVE_REQUEST_TIMEOUT_MS)
        remaining = DESKTOP_FILE_MOVE_REQUEST_TIMEOUT_MS;
    if (remaining == 0U) return DESKTOP_FILE_MOVE_ETIMEDOUT;
    *timeout_out = (uint32_t)remaining;
    return DESKTOP_FILE_MOVE_OK;
}

static int map_vfs_status(int status) {
    if (status == 0) return DESKTOP_FILE_MOVE_OK;
    if (status == DESKTOP_FILE_MOVE_ENOENT)
        return DESKTOP_FILE_MOVE_ENOENT;
    if (status == DESKTOP_FILE_MOVE_ETIMEDOUT)
        return DESKTOP_FILE_MOVE_ETIMEDOUT;
    return DESKTOP_FILE_MOVE_EIO;
}

static int stat_nofollow(
    const char *path, x86os_file_info_t *info,
    const desktop_file_move_budget_t *budget) {
    uint32_t timeout = 0U;
    int status = budget_remaining(budget, &timeout);
    if (status != DESKTOP_FILE_MOVE_OK) return status;
    return map_vfs_status(reist_vfs_lstat(path, info, timeout));
}

static int path_absent(const char *path,
                       const desktop_file_move_budget_t *budget) {
    x86os_file_info_t info;
    int status = stat_nofollow(path, &info, budget);
    if (status == DESKTOP_FILE_MOVE_ENOENT) return DESKTOP_FILE_MOVE_OK;
    if (status == DESKTOP_FILE_MOVE_OK) return DESKTOP_FILE_MOVE_EEXIST;
    return status;
}

static void close_source_cleanup(reist_vfs_file_handle_t handle) {
    if (handle == REIST_VFS_FILE_INVALID_HANDLE) return;
    (void)reist_vfs_file_set_timeout(handle, 1U);
    (void)reist_vfs_file_close(handle);
}

static uint32_t hash_bytes(uint32_t hash, const uint8_t *bytes,
                           uint32_t size) {
    for (uint32_t index = 0U; index < size; ++index)
        hash = (hash ^ bytes[index]) * 16777619U;
    return hash;
}

static uint32_t request_hash(const desktop_file_move_request_t *request) {
    uint32_t hash = 2166136261U;
    for (uint32_t index = 0U;
         index < DESKTOP_FILE_MOVE_PATH_CAPACITY &&
         request->source_path[index] != '\0'; ++index)
        hash = (hash ^ (uint8_t)request->source_path[index]) * 16777619U;
    return hash;
}

static void temp_filename(uint32_t hash, uint32_t attempt, char name[13]) {
    static const char digits[] = "0123456789ABCDEF";
    name[0] = 'R';
    name[1] = 'M';
    name[2] = digits[(hash >> 12U) & 0xFU];
    name[3] = digits[(hash >> 8U) & 0xFU];
    name[4] = digits[(hash >> 4U) & 0xFU];
    name[5] = digits[hash & 0xFU];
    name[6] = digits[(attempt >> 4U) & 0xFU];
    name[7] = digits[attempt & 0xFU];
    name[8] = '.';
    name[9] = 'T';
    name[10] = 'M';
    name[11] = 'P';
    name[12] = '\0';
}

static int write_chunk_all(
    int descriptor, const uint8_t *bytes, uint32_t size,
    const desktop_file_move_budget_t *budget) {
    uint32_t used = 0U;
    uint32_t calls = 0U;
    while (used < size && calls < DESKTOP_FILE_MOVE_CHUNK_CAPACITY) {
        uint32_t timeout = 0U;
        int status = budget_remaining(budget, &timeout);
        if (status != DESKTOP_FILE_MOVE_OK) return status;
        (void)timeout;
        int amount = x86os_write(descriptor, bytes + used, size - used);
        if (amount <= 0 || (uint32_t)amount > size - used)
            return DESKTOP_FILE_MOVE_EIO;
        used += (uint32_t)amount;
        ++calls;
    }
    return used == size ? DESKTOP_FILE_MOVE_OK
                        : DESKTOP_FILE_MOVE_ECAPACITY;
}

static int open_bound_file(
    const char *path, const x86os_file_info_t *expected,
    const desktop_file_move_budget_t *budget,
    reist_vfs_file_handle_t *handle_out) {
    uint32_t timeout = 0U;
    int status = budget_remaining(budget, &timeout);
    if (status != DESKTOP_FILE_MOVE_OK) return status;
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int open_status = reist_vfs_file_open_flags(
        path, timeout,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT,
        X86OS_O_NOFOLLOW, &handle);
    if (open_status != 0) return map_vfs_status(open_status);
    status = budget_remaining(budget, &timeout);
    if (status != DESKTOP_FILE_MOVE_OK ||
        reist_vfs_file_set_timeout(handle, timeout) != 0) {
        close_source_cleanup(handle);
        return status != DESKTOP_FILE_MOVE_OK ? status
                                              : DESKTOP_FILE_MOVE_EIO;
    }
    x86os_file_info_t actual;
    if (reist_vfs_file_fstat(handle, &actual) != 0 ||
        actual.type != X86OS_FILE ||
        (expected != 0 && !identity_equal(&actual, expected))) {
        close_source_cleanup(handle);
        return expected != 0 ? DESKTOP_FILE_MOVE_ESTALE
                             : DESKTOP_FILE_MOVE_EINVAL;
    }
    *handle_out = handle;
    return DESKTOP_FILE_MOVE_OK;
}

static int readback_temp(
    const char *path, uint32_t expected_size, uint32_t expected_hash,
    const desktop_file_move_budget_t *budget,
    x86os_file_info_t *identity_out) {
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = open_bound_file(path, 0, budget, &handle);
    if (status != DESKTOP_FILE_MOVE_OK) return status;
    x86os_file_info_t info;
    if (reist_vfs_file_fstat(handle, &info) != 0 ||
        info.size != expected_size) {
        close_source_cleanup(handle);
        return DESKTOP_FILE_MOVE_EIO;
    }
    uint32_t hash = 2166136261U;
    uint32_t read = 0U;
    while (read < expected_size) {
        uint32_t timeout = 0U;
        status = budget_remaining(budget, &timeout);
        uint32_t request = expected_size - read;
        if (request > sizeof(move_chunk)) request = sizeof(move_chunk);
        if (status != DESKTOP_FILE_MOVE_OK ||
            reist_vfs_file_set_timeout(handle, timeout) != 0) {
            close_source_cleanup(handle);
            return status != DESKTOP_FILE_MOVE_OK ? status
                                                  : DESKTOP_FILE_MOVE_EIO;
        }
        int amount = reist_vfs_file_read_bulk(handle, move_chunk, request);
        if (amount <= 0 || (uint32_t)amount > request) {
            close_source_cleanup(handle);
            return DESKTOP_FILE_MOVE_EIO;
        }
        hash = hash_bytes(hash, move_chunk, (uint32_t)amount);
        read += (uint32_t)amount;
    }
    uint8_t trailing = 0U;
    if (reist_vfs_file_read_bulk(handle, &trailing, 1U) != 0) {
        close_source_cleanup(handle);
        return DESKTOP_FILE_MOVE_EIO;
    }
    if (reist_vfs_file_close(handle) != 0 || hash != expected_hash)
        return DESKTOP_FILE_MOVE_EIO;
    copy_info(identity_out, &info);
    return DESKTOP_FILE_MOVE_OK;
}

void desktop_file_move_request_initialize(
    desktop_file_move_request_t *request) {
    if (request != 0) clear_bytes(request, sizeof(*request));
}

void desktop_file_move_result_initialize(
    desktop_file_move_result_t *result) {
    if (result != 0) clear_bytes(result, sizeof(*result));
}

int desktop_file_move_execute(
    const desktop_file_move_request_t *request,
    desktop_file_move_result_t *result) {
    if (result != 0) desktop_file_move_result_initialize(result);
    if (request == 0 || result == 0)
        return DESKTOP_FILE_MOVE_EINVAL;
    if (request->source_identity.size > DESKTOP_FILE_MOVE_MAX_BYTES)
        return DESKTOP_FILE_MOVE_ECAPACITY;
    if (request->source_identity.type != X86OS_FILE ||
        request->source_directory_identity.type != X86OS_DIRECTORY ||
        request->destination_directory_identity.type != X86OS_DIRECTORY ||
        !desktop_file_move_source_allowed(request->source_path) ||
        !desktop_file_move_destination_allowed(
            request->destination_directory_path) ||
        !source_belongs_to_directory(
            request->source_path, request->source_directory_path,
            request->source_identity.name) ||
        path_equal(request->source_directory_path,
                   request->destination_directory_path))
        return DESKTOP_FILE_MOVE_EINVAL;

    char destination[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    if (!build_child_path(
            request->destination_directory_path,
            request->source_identity.name, destination))
        return DESKTOP_FILE_MOVE_ECAPACITY;
    desktop_file_move_budget_t budget;
    int status = budget_start(&budget);
    if (status != DESKTOP_FILE_MOVE_OK) return status;

    x86os_file_info_t source_directory;
    x86os_file_info_t destination_directory;
    x86os_file_info_t source;
    status = stat_nofollow(
        request->source_directory_path, &source_directory, &budget);
    if (status != DESKTOP_FILE_MOVE_OK) return status;
    status = stat_nofollow(
        request->destination_directory_path, &destination_directory,
        &budget);
    if (status != DESKTOP_FILE_MOVE_OK) return status;
    status = stat_nofollow(request->source_path, &source, &budget);
    if (status != DESKTOP_FILE_MOVE_OK) return status;
    if (!directory_authority_equal(
            &source_directory, &request->source_directory_identity) ||
        !directory_authority_equal(
            &destination_directory,
            &request->destination_directory_identity) ||
        !identity_equal(&source, &request->source_identity))
        return DESKTOP_FILE_MOVE_ESTALE;
    status = path_absent(destination, &budget);
    if (status != DESKTOP_FILE_MOVE_OK) return status;

    reist_vfs_file_handle_t source_handle =
        REIST_VFS_FILE_INVALID_HANDLE;
    status = open_bound_file(
        request->source_path, &request->source_identity, &budget,
        &source_handle);
    if (status != DESKTOP_FILE_MOVE_OK) return status;

    char temporary[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    int descriptor = -1;
    uint32_t hash_seed = request_hash(request);
    for (uint32_t attempt = 0U;
         attempt < DESKTOP_FILE_MOVE_TEMP_ATTEMPTS; ++attempt) {
        char name[13];
        temp_filename(hash_seed, attempt, name);
        if (!build_child_path(
                request->destination_directory_path, name, temporary)) {
            close_source_cleanup(source_handle);
            return DESKTOP_FILE_MOVE_ECAPACITY;
        }
        status = path_absent(temporary, &budget);
        if (status == DESKTOP_FILE_MOVE_EEXIST) continue;
        if (status != DESKTOP_FILE_MOVE_OK) {
            close_source_cleanup(source_handle);
            return status;
        }
        descriptor = x86os_create(temporary);
        if (descriptor >= 0) break;
        if (descriptor != DESKTOP_FILE_MOVE_EEXIST) {
            close_source_cleanup(source_handle);
            return DESKTOP_FILE_MOVE_EIO;
        }
    }
    if (descriptor < 0) {
        close_source_cleanup(source_handle);
        return DESKTOP_FILE_MOVE_ECAPACITY;
    }
    uint32_t destination_length = 0U;
    uint32_t temporary_length = 0U;
    (void)text_length(destination, sizeof(destination), &destination_length);
    (void)text_length(temporary, sizeof(temporary), &temporary_length);
    for (uint32_t index = 0U; index <= destination_length; ++index)
        result->destination_path[index] = destination[index];
    for (uint32_t index = 0U; index <= temporary_length; ++index)
        result->temporary_path[index] = temporary[index];

    uint32_t source_hash = 2166136261U;
    uint32_t copied = 0U;
    while (copied < request->source_identity.size) {
        uint32_t timeout = 0U;
        status = budget_remaining(&budget, &timeout);
        uint32_t request_size = request->source_identity.size - copied;
        if (request_size > sizeof(move_chunk))
            request_size = sizeof(move_chunk);
        if (status != DESKTOP_FILE_MOVE_OK ||
            reist_vfs_file_set_timeout(source_handle, timeout) != 0) {
            status = status != DESKTOP_FILE_MOVE_OK
                ? status : DESKTOP_FILE_MOVE_EIO;
            break;
        }
        int amount = reist_vfs_file_read_bulk(
            source_handle, move_chunk, request_size);
        if (amount <= 0 || (uint32_t)amount > request_size) {
            status = DESKTOP_FILE_MOVE_EIO;
            break;
        }
        source_hash = hash_bytes(
            source_hash, move_chunk, (uint32_t)amount);
        status = write_chunk_all(
            descriptor, move_chunk, (uint32_t)amount, &budget);
        if (status != DESKTOP_FILE_MOVE_OK) break;
        copied += (uint32_t)amount;
        result->bytes_copied = copied;
    }
    uint8_t trailing = 0U;
    if (status == DESKTOP_FILE_MOVE_OK &&
        reist_vfs_file_read_bulk(source_handle, &trailing, 1U) != 0)
        status = DESKTOP_FILE_MOVE_ESTALE;
    x86os_file_info_t final_source;
    if (status == DESKTOP_FILE_MOVE_OK &&
        (reist_vfs_file_fstat(source_handle, &final_source) != 0 ||
         !identity_equal(&final_source, &request->source_identity)))
        status = DESKTOP_FILE_MOVE_ESTALE;
    if (reist_vfs_file_close(source_handle) != 0 &&
        status == DESKTOP_FILE_MOVE_OK)
        status = DESKTOP_FILE_MOVE_EIO;
    int sync_status =
        status == DESKTOP_FILE_MOVE_OK ? x86os_fsync(descriptor) : -1;
    int close_status = x86os_close(descriptor);
    if (status != DESKTOP_FILE_MOVE_OK || sync_status != 0 ||
        close_status != 0) {
        (void)x86os_unlink(temporary);
        return status != DESKTOP_FILE_MOVE_OK ? status
                                             : DESKTOP_FILE_MOVE_EIO;
    }

    x86os_file_info_t verified_temp;
    status = readback_temp(
        temporary, request->source_identity.size, source_hash,
        &budget, &verified_temp);
    if (status != DESKTOP_FILE_MOVE_OK) {
        (void)x86os_unlink(temporary);
        return status;
    }
    status = stat_nofollow(
        request->destination_directory_path, &destination_directory,
        &budget);
    if (status == DESKTOP_FILE_MOVE_OK)
        status = stat_nofollow(
            request->source_directory_path, &source_directory, &budget);
    if (status == DESKTOP_FILE_MOVE_OK)
        status = stat_nofollow(request->source_path, &source, &budget);
    if (status != DESKTOP_FILE_MOVE_OK ||
        !directory_authority_equal(
            &destination_directory,
            &request->destination_directory_identity) ||
        !directory_authority_equal(
            &source_directory, &request->source_directory_identity) ||
        !identity_equal(&source, &request->source_identity)) {
        (void)x86os_unlink(temporary);
        return status != DESKTOP_FILE_MOVE_OK ? status
                                             : DESKTOP_FILE_MOVE_ESTALE;
    }
    status = path_absent(destination, &budget);
    if (status != DESKTOP_FILE_MOVE_OK ||
        x86os_rename(temporary, destination) != 0) {
        (void)x86os_unlink(temporary);
        return status == DESKTOP_FILE_MOVE_EEXIST
            ? DESKTOP_FILE_MOVE_EEXIST
            : status != DESKTOP_FILE_MOVE_OK
                ? status : DESKTOP_FILE_MOVE_EIO;
    }
    result->destination_published = 1U;
    x86os_file_info_t published;
    status = stat_nofollow(destination, &published, &budget);
    if (status != DESKTOP_FILE_MOVE_OK ||
        published.type != X86OS_FILE ||
        published.size != request->source_identity.size) {
        result->duplicate_retained = 1U;
        return DESKTOP_FILE_MOVE_EPARTIAL;
    }
    copy_info(&result->destination_identity, &published);

    status = stat_nofollow(
        request->source_directory_path, &source_directory, &budget);
    if (status == DESKTOP_FILE_MOVE_OK)
        status = stat_nofollow(request->source_path, &source, &budget);
    if (status != DESKTOP_FILE_MOVE_OK ||
        !directory_authority_equal(
            &source_directory, &request->source_directory_identity) ||
        !identity_equal(&source, &request->source_identity) ||
        x86os_unlink(request->source_path) != 0) {
        result->duplicate_retained = 1U;
        return DESKTOP_FILE_MOVE_EPARTIAL;
    }
    result->source_removed = 1U;
    return DESKTOP_FILE_MOVE_OK;
}
