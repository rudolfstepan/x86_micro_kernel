/**
 * @file userspace/gui/compositor/desktop_trash.c
 * @brief Recoverable single-user trash implementation for REIST.
 */
#include "desktop_trash.h"

static void clear_bytes(void *value, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
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

static uint32_t path_has_prefix(const char *path, const char *prefix) {
    uint32_t index = 0U;
    while (prefix[index] != '\0') {
        if (fold_ascii((uint8_t)path[index]) !=
            fold_ascii((uint8_t)prefix[index])) return 0U;
        ++index;
    }
    return path[index] == '\0' || path[index] == '/';
}

static uint32_t canonical_absolute_path(const char *path, uint32_t *length) {
    if (!text_length(path, DESKTOP_TRASH_PATH_CAPACITY, length) ||
        *length < 2U || path[0] != '/' || path[*length - 1U] == '/') return 0U;
    uint32_t component_start = 1U;
    for (uint32_t index = 1U; index <= *length; ++index) {
        if (path[index] != '/' && path[index] != '\0') continue;
        uint32_t component_length = index - component_start;
        if (component_length == 0U ||
            (component_length == 1U && path[component_start] == '.') ||
            (component_length == 2U && path[component_start] == '.' &&
             path[component_start + 1U] == '.')) return 0U;
        component_start = index + 1U;
    }
    return 1U;
}

static uint32_t hex_digit(uint8_t value) {
    value = fold_ascii(value);
    return (value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f');
}

static uint32_t storage_name(const char *name) {
    uint32_t length = 0U;
    if (!text_length(name, DESKTOP_TRASH_STORAGE_NAME_LENGTH + 1U,
                     &length) ||
        length != DESKTOP_TRASH_STORAGE_NAME_LENGTH ||
        fold_ascii((uint8_t)name[0]) != 'r' ||
        fold_ascii((uint8_t)name[1]) != 't') return 0U;
    for (uint32_t index = 2U; index < 8U; ++index)
        if (!hex_digit((uint8_t)name[index])) return 0U;
    return name[8] == '.' &&
        fold_ascii((uint8_t)name[9]) == 't' &&
        fold_ascii((uint8_t)name[10]) == 'r' &&
        fold_ascii((uint8_t)name[11]) == 's' && name[12] == '\0';
}

uint32_t desktop_trash_source_allowed(const char *path) {
    static const char *const protected_roots[] = {
        DESKTOP_TRASH_ROOT_PATH, "/bin", "/sbin", "/libexec", "/etc",
        "/usr", "/boot", "/dev", "/mnt", "/proc", "/sys", "/run",
        "/var",
    };
    uint32_t length = 0U;
    if (!canonical_absolute_path(path, &length)) return 0U;
    uint32_t name_offset = length;
    while (name_offset != 0U && path[name_offset - 1U] != '/') --name_offset;
    if (storage_name(&path[name_offset])) return 0U;
    for (uint32_t index = 0U;
         index < sizeof(protected_roots) / sizeof(protected_roots[0]); ++index)
        if (path_has_prefix(path, protected_roots[index])) return 0U;
    return 1U;
}

static void bump_generation(desktop_trash_state_t *state) {
    if (++state->generation == 0U) state->generation = 1U;
}

void desktop_trash_state_initialize(desktop_trash_state_t *state) {
    if (state == 0) return;
    clear_bytes(state, sizeof(*state));
    state->generation = 1U;
}

void desktop_trash_request_initialize(desktop_trash_request_t *request) {
    if (request != 0) clear_bytes(request, sizeof(*request));
}

void desktop_trash_result_initialize(desktop_trash_result_t *result) {
    if (result != 0) clear_bytes(result, sizeof(*result));
}

void desktop_trash_restore_request_initialize(
    desktop_trash_restore_request_t *request) {
    if (request != 0) clear_bytes(request, sizeof(*request));
}

void desktop_trash_restore_result_initialize(
    desktop_trash_restore_result_t *result) {
    if (result != 0) clear_bytes(result, sizeof(*result));
}

void desktop_trash_empty_result_initialize(
    desktop_trash_empty_result_t *result) {
    if (result != 0) clear_bytes(result, sizeof(*result));
}

static int ensure_directory(const char *path) {
    x86os_file_info_t info;
    int status = x86os_stat(path, &info);
    if (status == 0) return info.type == X86OS_DIRECTORY
        ? DESKTOP_TRASH_OK : DESKTOP_TRASH_EIO;
    if (status != DESKTOP_TRASH_ENOENT || x86os_mkdir(path) != 0 ||
        x86os_stat(path, &info) != 0 || info.type != X86OS_DIRECTORY)
        return DESKTOP_TRASH_EIO;
    return DESKTOP_TRASH_OK;
}

static uint32_t dot_name(const char *name) {
    return name != 0 && name[0] == '.' &&
        (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

int desktop_trash_refresh(desktop_trash_state_t *state) {
    if (state == 0) return DESKTOP_TRASH_EINVAL;
    x86os_file_info_t directory;
    if (x86os_stat(DESKTOP_TRASH_FILES_PATH, &directory) != 0 ||
        directory.type != X86OS_DIRECTORY) {
        state->available = 0U;
        state->full = 0U;
        bump_generation(state);
        return DESKTOP_TRASH_EIO;
    }
    uint32_t full = 0U;
    uint32_t offset = 0U;
    for (uint32_t batch_index = 0U;
         batch_index < DESKTOP_TRASH_SCAN_BATCHES; ++batch_index) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(
            DESKTOP_TRASH_FILES_PATH, offset, entries);
        if (count < 0 || (uint32_t)count > X86OS_READDIR_BATCH_CAPACITY) {
            state->available = 0U;
            bump_generation(state);
            return DESKTOP_TRASH_EIO;
        }
        if (count == 0) break;
        for (int index = 0; index < count; ++index) {
            uint32_t name_length = 0U;
            if (!text_length(entries[index].name,
                             sizeof(entries[index].name), &name_length) ||
                name_length == 0U || !dot_name(entries[index].name)) {
                full = 1U;
                break;
            }
        }
        if (full) break;
        offset += (uint32_t)count;
        if (batch_index + 1U == DESKTOP_TRASH_SCAN_BATCHES) full = 1U;
    }
    uint32_t changed = state->available == 0U || state->full != full;
    state->available = 1U;
    state->full = full;
    if (changed) bump_generation(state);
    return DESKTOP_TRASH_OK;
}

int desktop_trash_prepare(desktop_trash_state_t *state) {
    if (state == 0) return DESKTOP_TRASH_EINVAL;
    if (ensure_directory(DESKTOP_TRASH_ROOT_PATH) != DESKTOP_TRASH_OK ||
        ensure_directory(DESKTOP_TRASH_FILES_PATH) != DESKTOP_TRASH_OK ||
        ensure_directory(DESKTOP_TRASH_INFO_PATH) != DESKTOP_TRASH_OK) {
        state->available = 0U;
        state->full = 0U;
        bump_generation(state);
        return DESKTOP_TRASH_EIO;
    }
    return desktop_trash_refresh(state);
}

static uint32_t identity_equal(const x86os_file_info_t *left,
                               const x86os_file_info_t *right) {
    return left->type == right->type && left->size == right->size &&
        left->create_time == right->create_time &&
        left->modify_time == right->modify_time &&
        left->access_time == right->access_time;
}

static uint32_t append_text(char *destination, uint32_t capacity,
                            uint32_t *used, const char *text) {
    uint32_t length = 0U;
    if (!text_length(text, capacity, &length) || *used >= capacity ||
        length >= capacity - *used) return 0U;
    for (uint32_t index = 0U; index < length; ++index)
        destination[(*used)++] = text[index];
    destination[*used] = '\0';
    return 1U;
}

static uint32_t append_number(char *destination, uint32_t capacity,
                              uint32_t *used, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    if (*used >= capacity || count >= capacity - *used) return 0U;
    while (count != 0U) destination[(*used)++] = digits[--count];
    destination[*used] = '\0';
    return 1U;
}

static int build_catalog_candidate(const char *name, uint32_t attempt,
                                   char *catalog_path, char *info_path) {
    uint32_t catalog_used = 0U;
    uint32_t info_used = 0U;
    if (!append_text(catalog_path, DESKTOP_TRASH_PATH_CAPACITY, &catalog_used,
                     DESKTOP_TRASH_FILES_PATH "/") ||
        !append_text(catalog_path, DESKTOP_TRASH_PATH_CAPACITY, &catalog_used,
                     name) ||
        !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &info_used,
                     DESKTOP_TRASH_INFO_PATH "/") ||
        !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &info_used,
                     name)) return DESKTOP_TRASH_ECAPACITY;
    if (attempt != 0U) {
        if (!append_text(catalog_path, DESKTOP_TRASH_PATH_CAPACITY,
                         &catalog_used, ".") ||
            !append_number(catalog_path, DESKTOP_TRASH_PATH_CAPACITY,
                           &catalog_used, attempt) ||
            !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY,
                         &info_used, ".") ||
            !append_number(info_path, DESKTOP_TRASH_PATH_CAPACITY,
                           &info_used, attempt)) return DESKTOP_TRASH_ECAPACITY;
    }
    if (!append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &info_used,
                     ".trashinfo")) return DESKTOP_TRASH_ECAPACITY;
    return DESKTOP_TRASH_OK;
}

static uint32_t hash_u32(uint32_t hash, uint32_t value) {
    for (uint32_t byte = 0U; byte < 4U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 16777619U;
    }
    return hash;
}

static int build_storage_path(const desktop_trash_request_t *request,
                              uint32_t attempt, char *stored_path) {
    uint32_t source_length = 0U;
    if (request == 0 || stored_path == 0 ||
        !text_length(request->source_path, DESKTOP_TRASH_PATH_CAPACITY,
                     &source_length)) return DESKTOP_TRASH_EINVAL;
    uint32_t name_offset = source_length;
    while (name_offset != 0U && request->source_path[name_offset - 1U] != '/')
        --name_offset;
    if (name_offset + DESKTOP_TRASH_STORAGE_NAME_LENGTH + 1U >
        DESKTOP_TRASH_PATH_CAPACITY) return DESKTOP_TRASH_ECAPACITY;

    uint32_t hash = 2166136261U;
    for (uint32_t index = 0U; index < source_length; ++index) {
        hash ^= (uint8_t)request->source_path[index];
        hash *= 16777619U;
    }
    hash = hash_u32(hash, request->identity.type);
    hash = hash_u32(hash, request->identity.size);
    hash = hash_u32(hash, request->identity.create_time);
    hash = hash_u32(hash, request->identity.modify_time);
    hash = hash_u32(hash, request->identity.access_time);
    hash = hash_u32(hash, attempt);

    for (uint32_t index = 0U; index < name_offset; ++index)
        stored_path[index] = request->source_path[index];
    uint32_t used = name_offset;
    stored_path[used++] = 'R';
    stored_path[used++] = 'T';
    static const char digits[] = "0123456789ABCDEF";
    for (uint32_t digit = 0U; digit < 6U; ++digit) {
        uint32_t shift = (5U - digit) * 4U;
        stored_path[used++] = digits[(hash >> shift) & 0xFU];
    }
    stored_path[used++] = '.';
    stored_path[used++] = 'T';
    stored_path[used++] = 'R';
    stored_path[used++] = 'S';
    stored_path[used] = '\0';
    return DESKTOP_TRASH_OK;
}

static uint32_t two_digits_valid(uint32_t value, uint32_t maximum) {
    return value <= maximum;
}

static uint32_t days_in_month(uint32_t year, uint32_t month) {
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month == 0U || month > 12U) return 0U;
    uint32_t result = days[month - 1U];
    if (month == 2U &&
        ((year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U))
        ++result;
    return result;
}

static void write_two(char *text, uint32_t offset, uint32_t value) {
    text[offset] = (char)('0' + (value / 10U) % 10U);
    text[offset + 1U] = (char)('0' + value % 10U);
}

static void deletion_date(char *text) {
    uint32_t date = x86os_get_date();
    uint32_t time = x86os_get_time();
    uint32_t year = date >> 16U;
    uint32_t month = (date >> 8U) & 0xFFU;
    uint32_t day = date & 0xFFU;
    uint32_t hour = (time >> 16U) & 0xFFU;
    uint32_t minute = (time >> 8U) & 0xFFU;
    uint32_t second = time & 0xFFU;
    if (year < 1970U || year > 9999U || month == 0U ||
        !two_digits_valid(month, 12U) || day == 0U ||
        day > days_in_month(year, month) ||
        !two_digits_valid(hour, 23U) ||
        !two_digits_valid(minute, 59U) || !two_digits_valid(second, 59U)) {
        year = month = day = hour = minute = second = 0U;
    }
    text[0] = (char)('0' + (year / 1000U) % 10U);
    text[1] = (char)('0' + (year / 100U) % 10U);
    text[2] = (char)('0' + (year / 10U) % 10U);
    text[3] = (char)('0' + year % 10U);
    text[4] = '-';
    write_two(text, 5U, month);
    text[7] = '-';
    write_two(text, 8U, day);
    text[10] = 'T';
    write_two(text, 11U, hour);
    text[13] = ':';
    write_two(text, 14U, minute);
    text[16] = ':';
    write_two(text, 17U, second);
    text[19] = '\0';
}

static int write_metadata(const char *info_path, const char *source_path,
                          const char *storage_path) {
    char metadata[DESKTOP_TRASH_METADATA_CAPACITY];
    char date[20];
    uint32_t used = 0U;
    deletion_date(date);
    if (!append_text(metadata, sizeof(metadata), &used,
                     "[Trash Info]\nVersion=2\nPath=") ||
        !append_text(metadata, sizeof(metadata), &used, source_path) ||
        !append_text(metadata, sizeof(metadata), &used, "\nStoragePath=") ||
        !append_text(metadata, sizeof(metadata), &used, storage_path) ||
        !append_text(metadata, sizeof(metadata), &used, "\nDeletionDate=") ||
        !append_text(metadata, sizeof(metadata), &used, date) ||
        !append_text(metadata, sizeof(metadata), &used, "\n"))
        return DESKTOP_TRASH_ECAPACITY;
    int descriptor = x86os_create(info_path);
    if (descriptor < 0) return DESKTOP_TRASH_EIO;
    uint32_t offset = 0U;
    while (offset < used) {
        int written = x86os_write(descriptor, metadata + offset, used - offset);
        if (written <= 0 || (uint32_t)written > used - offset) {
            (void)x86os_close(descriptor);
            (void)x86os_unlink(info_path);
            return DESKTOP_TRASH_EIO;
        }
        offset += (uint32_t)written;
    }
    int sync_status = x86os_fsync(descriptor);
    int close_status = x86os_close(descriptor);
    if (sync_status != 0 || close_status != 0) {
        (void)x86os_unlink(info_path);
        return DESKTOP_TRASH_EIO;
    }
    return DESKTOP_TRASH_OK;
}

static int create_catalog_marker(const char *catalog_path) {
    int descriptor = x86os_create(catalog_path);
    if (descriptor < 0) return DESKTOP_TRASH_EIO;
    int sync_status = x86os_fsync(descriptor);
    int close_status = x86os_close(descriptor);
    if (sync_status != 0 || close_status != 0) {
        (void)x86os_unlink(catalog_path);
        return DESKTOP_TRASH_EIO;
    }
    return DESKTOP_TRASH_OK;
}

int desktop_trash_move(desktop_trash_state_t *state,
                       const desktop_trash_request_t *request,
                       desktop_trash_result_t *result) {
    if (state == 0 || request == 0 || result == 0)
        return DESKTOP_TRASH_EINVAL;
    desktop_trash_result_initialize(result);
    if (!desktop_trash_source_allowed(request->source_path))
        return DESKTOP_TRASH_EPROTECTED;
    if (request->identity.type != X86OS_FILE &&
        request->identity.type != X86OS_DIRECTORY)
        return DESKTOP_TRASH_EINVAL;
    x86os_file_info_t current;
    if (x86os_stat(request->source_path, &current) != 0)
        return DESKTOP_TRASH_ENOENT;
    if (!identity_equal(&request->identity, &current))
        return DESKTOP_TRASH_ESTALE;
    if (desktop_trash_prepare(state) != DESKTOP_TRASH_OK)
        return DESKTOP_TRASH_EIO;

    uint32_t source_length = 0U;
    (void)text_length(request->source_path, DESKTOP_TRASH_PATH_CAPACITY,
                      &source_length);
    uint32_t name_offset = source_length;
    while (name_offset != 0U && request->source_path[name_offset - 1U] != '/')
        --name_offset;
    const char *name = &request->source_path[name_offset];
    int candidate_status = DESKTOP_TRASH_ECAPACITY;
    for (uint32_t attempt = 0U;
         attempt < DESKTOP_TRASH_COLLISION_LIMIT; ++attempt) {
        candidate_status = build_catalog_candidate(
            name, attempt, result->catalog_path, result->info_path);
        if (candidate_status == DESKTOP_TRASH_OK)
            candidate_status = build_storage_path(
                request, attempt, result->stored_path);
        if (candidate_status != DESKTOP_TRASH_OK) return candidate_status;
        x86os_file_info_t collision;
        int stored_status = x86os_stat(result->stored_path, &collision);
        int catalog_status = x86os_stat(result->catalog_path, &collision);
        int info_status = x86os_stat(result->info_path, &collision);
        if (stored_status == DESKTOP_TRASH_ENOENT &&
            catalog_status == DESKTOP_TRASH_ENOENT &&
            info_status == DESKTOP_TRASH_ENOENT) break;
        if ((stored_status != 0 && stored_status != DESKTOP_TRASH_ENOENT) ||
            (catalog_status != 0 &&
             catalog_status != DESKTOP_TRASH_ENOENT) ||
            (info_status != 0 && info_status != DESKTOP_TRASH_ENOENT))
            return DESKTOP_TRASH_EIO;
        if (attempt + 1U == DESKTOP_TRASH_COLLISION_LIMIT)
            return DESKTOP_TRASH_ECAPACITY;
    }
    int metadata_status = write_metadata(
        result->info_path, request->source_path, result->stored_path);
    if (metadata_status != DESKTOP_TRASH_OK) return metadata_status;
    if (create_catalog_marker(result->catalog_path) != DESKTOP_TRASH_OK) {
        (void)x86os_unlink(result->info_path);
        desktop_trash_result_initialize(result);
        return DESKTOP_TRASH_EIO;
    }
    if (x86os_rename(request->source_path, result->stored_path) != 0) {
        int catalog_cleanup = x86os_unlink(result->catalog_path);
        int info_cleanup = x86os_unlink(result->info_path);
        desktop_trash_result_initialize(result);
        (void)desktop_trash_refresh(state);
        return catalog_cleanup == 0 && info_cleanup == 0
            ? DESKTOP_TRASH_ERENAME : DESKTOP_TRASH_EIO;
    }
    result->moved = 1U;
    state->available = 1U;
    state->full = 1U;
    bump_generation(state);
    return DESKTOP_TRASH_OK;
}

typedef struct desktop_trash_metadata {
    char original_path[DESKTOP_TRASH_PATH_CAPACITY];
    char stored_path[DESKTOP_TRASH_PATH_CAPACITY];
} desktop_trash_metadata_t;

static uint32_t copy_text(char *destination, uint32_t capacity,
                          const char *source) {
    uint32_t length = 0U;
    if (destination == 0 || capacity == 0U ||
        !text_length(source, capacity, &length)) return 0U;
    for (uint32_t index = 0U; index <= length; ++index)
        destination[index] = source[index];
    return 1U;
}

static int catalog_info_path(const char *catalog_path, char *info_path) {
    static const char prefix[] = DESKTOP_TRASH_FILES_PATH "/";
    uint32_t catalog_length = 0U;
    uint32_t prefix_length = sizeof(prefix) - 1U;
    if (!canonical_absolute_path(catalog_path, &catalog_length) ||
        catalog_length <= prefix_length) return DESKTOP_TRASH_EINVAL;
    for (uint32_t index = 0U; index < prefix_length; ++index)
        if (fold_ascii((uint8_t)catalog_path[index]) !=
            fold_ascii((uint8_t)prefix[index])) return DESKTOP_TRASH_EINVAL;
    const char *name = &catalog_path[prefix_length];
    for (uint32_t index = 0U; name[index] != '\0'; ++index)
        if (name[index] == '/') return DESKTOP_TRASH_EINVAL;
    uint32_t used = 0U;
    if (!append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &used,
                     DESKTOP_TRASH_INFO_PATH "/") ||
        !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &used, name) ||
        !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &used,
                     ".trashinfo")) return DESKTOP_TRASH_ECAPACITY;
    return DESKTOP_TRASH_OK;
}

static int read_metadata_file(const char *info_path, char *metadata,
                              uint32_t *size_out) {
    int descriptor = x86os_open(info_path);
    if (descriptor < 0) return DESKTOP_TRASH_ENOENT;
    uint32_t used = 0U;
    int status = DESKTOP_TRASH_OK;
    while (used + 1U < DESKTOP_TRASH_METADATA_CAPACITY) {
        uint32_t remaining = DESKTOP_TRASH_METADATA_CAPACITY - 1U - used;
        int count = x86os_read(descriptor, metadata + used, remaining);
        if (count < 0 || (uint32_t)count > remaining) {
            status = DESKTOP_TRASH_EIO;
            break;
        }
        if (count == 0) break;
        used += (uint32_t)count;
    }
    if (status == DESKTOP_TRASH_OK &&
        used + 1U == DESKTOP_TRASH_METADATA_CAPACITY) {
        char extra = 0;
        int count = x86os_read(descriptor, &extra, 1U);
        if (count < 0 || count > 1) status = DESKTOP_TRASH_EIO;
        else if (count != 0) status = DESKTOP_TRASH_ECAPACITY;
    }
    if (x86os_close(descriptor) != 0 && status == DESKTOP_TRASH_OK)
        status = DESKTOP_TRASH_EIO;
    if (status != DESKTOP_TRASH_OK) return status;
    metadata[used] = '\0';
    *size_out = used;
    return DESKTOP_TRASH_OK;
}

static uint32_t consume_literal(const char *metadata, uint32_t size,
                                uint32_t *offset, const char *literal) {
    uint32_t length = 0U;
    if (!text_length(literal, DESKTOP_TRASH_METADATA_CAPACITY, &length) ||
        *offset > size || length > size - *offset) return 0U;
    for (uint32_t index = 0U; index < length; ++index)
        if (metadata[*offset + index] != literal[index]) return 0U;
    *offset += length;
    return 1U;
}

static uint32_t consume_path_line(const char *metadata, uint32_t size,
                                  uint32_t *offset, char *path) {
    uint32_t used = 0U;
    while (*offset < size && metadata[*offset] != '\n') {
        char value = metadata[(*offset)++];
        if (value == '\0' || value == '\r' ||
            used + 1U >= DESKTOP_TRASH_PATH_CAPACITY) return 0U;
        path[used++] = value;
    }
    if (used == 0U || *offset >= size || metadata[*offset] != '\n')
        return 0U;
    ++*offset;
    path[used] = '\0';
    return 1U;
}

static uint32_t deletion_date_line_valid(const char *metadata,
                                         uint32_t size,
                                         uint32_t *offset) {
    static const uint8_t separators[19] = {
        0U, 0U, 0U, 0U, '-', 0U, 0U, '-', 0U, 0U,
        'T', 0U, 0U, ':', 0U, 0U, ':', 0U, 0U,
    };
    if (*offset > size || 20U > size - *offset) return 0U;
    for (uint32_t index = 0U; index < 19U; ++index) {
        uint8_t value = (uint8_t)metadata[*offset + index];
        if (separators[index] != 0U) {
            if (value != separators[index]) return 0U;
        } else if (value < '0' || value > '9') return 0U;
    }
    if (metadata[*offset + 19U] != '\n') return 0U;
    *offset += 20U;
    return 1U;
}

static uint32_t same_parent_storage(const char *original,
                                    const char *stored) {
    uint32_t original_length = 0U;
    uint32_t stored_length = 0U;
    if (!canonical_absolute_path(original, &original_length) ||
        !canonical_absolute_path(stored, &stored_length)) return 0U;
    uint32_t original_name = original_length;
    uint32_t stored_name = stored_length;
    while (original_name != 0U && original[original_name - 1U] != '/')
        --original_name;
    while (stored_name != 0U && stored[stored_name - 1U] != '/')
        --stored_name;
    if (original_name != stored_name ||
        !storage_name(&stored[stored_name])) return 0U;
    for (uint32_t index = 0U; index < original_name; ++index)
        if (fold_ascii((uint8_t)original[index]) !=
            fold_ascii((uint8_t)stored[index])) return 0U;
    return desktop_trash_source_allowed(original);
}

static int parse_metadata(const char *metadata, uint32_t size,
                          desktop_trash_metadata_t *parsed) {
    uint32_t offset = 0U;
    clear_bytes(parsed, sizeof(*parsed));
    if (!consume_literal(metadata, size, &offset,
                         "[Trash Info]\nVersion=2\nPath=") ||
        !consume_path_line(metadata, size, &offset,
                           parsed->original_path) ||
        !consume_literal(metadata, size, &offset, "StoragePath=") ||
        !consume_path_line(metadata, size, &offset,
                           parsed->stored_path) ||
        !consume_literal(metadata, size, &offset, "DeletionDate=") ||
        !deletion_date_line_valid(metadata, size, &offset) ||
        offset != size ||
        !same_parent_storage(parsed->original_path, parsed->stored_path))
        return DESKTOP_TRASH_EINVAL;
    return DESKTOP_TRASH_OK;
}

static int load_catalog_metadata(
    const char *catalog_path, const x86os_file_info_t *identity,
    desktop_trash_metadata_t *metadata, char *info_path) {
    x86os_file_info_t current;
    if (catalog_info_path(catalog_path, info_path) != DESKTOP_TRASH_OK)
        return DESKTOP_TRASH_EINVAL;
    if (x86os_stat(catalog_path, &current) != 0 ||
        current.type != X86OS_FILE) return DESKTOP_TRASH_ENOENT;
    if (identity != 0 && !identity_equal(identity, &current))
        return DESKTOP_TRASH_ESTALE;
    char encoded[DESKTOP_TRASH_METADATA_CAPACITY];
    uint32_t encoded_size = 0U;
    int status = read_metadata_file(info_path, encoded, &encoded_size);
    if (status != DESKTOP_TRASH_OK) return status;
    status = parse_metadata(encoded, encoded_size, metadata);
    if (status != DESKTOP_TRASH_OK) return status;
    x86os_file_info_t stored;
    if (x86os_stat(metadata->stored_path, &stored) != 0 ||
        (stored.type != X86OS_FILE && stored.type != X86OS_DIRECTORY))
        return DESKTOP_TRASH_ENOENT;
    return DESKTOP_TRASH_OK;
}

int desktop_trash_restore(desktop_trash_state_t *state,
                          const desktop_trash_restore_request_t *request,
                          desktop_trash_restore_result_t *result) {
    if (state == 0 || request == 0 || result == 0)
        return DESKTOP_TRASH_EINVAL;
    desktop_trash_restore_result_initialize(result);
    desktop_trash_metadata_t metadata;
    int status = load_catalog_metadata(
        request->catalog_path, &request->identity, &metadata,
        result->info_path);
    if (status != DESKTOP_TRASH_OK) return status;
    if (!copy_text(result->original_path, sizeof(result->original_path),
                   metadata.original_path) ||
        !copy_text(result->stored_path, sizeof(result->stored_path),
                   metadata.stored_path)) return DESKTOP_TRASH_ECAPACITY;
    x86os_file_info_t collision;
    status = x86os_stat(metadata.original_path, &collision);
    if (status == 0) return DESKTOP_TRASH_ECOLLISION;
    if (status != DESKTOP_TRASH_ENOENT) return DESKTOP_TRASH_EIO;
    if (x86os_rename(metadata.stored_path, metadata.original_path) != 0)
        return DESKTOP_TRASH_ERENAME;
    result->restored = 1U;
    int catalog_cleanup = x86os_unlink(request->catalog_path);
    int info_cleanup = catalog_cleanup == 0
        ? x86os_unlink(result->info_path) : DESKTOP_TRASH_EIO;
    result->cleanup_complete =
        catalog_cleanup == 0 && info_cleanup == 0;
    int refresh_status = desktop_trash_refresh(state);
    if (!result->cleanup_complete || refresh_status != DESKTOP_TRASH_OK)
        return DESKTOP_TRASH_EIO;
    return DESKTOP_TRASH_OK;
}

static uint32_t safe_entry_name(const char *name) {
    uint32_t length = 0U;
    if (!text_length(name, sizeof(((x86os_file_info_t *)0)->name), &length) ||
        length == 0U || dot_name(name)) return 0U;
    for (uint32_t index = 0U; index < length; ++index)
        if (name[index] == '/') return 0U;
    return 1U;
}

static int append_child_path(const char *parent, const char *name,
                             char *child) {
    uint32_t used = 0U;
    if (!safe_entry_name(name) ||
        !append_text(child, DESKTOP_TRASH_PATH_CAPACITY, &used, parent) ||
        !append_text(child, DESKTOP_TRASH_PATH_CAPACITY, &used, "/") ||
        !append_text(child, DESKTOP_TRASH_PATH_CAPACITY, &used, name))
        return DESKTOP_TRASH_ECAPACITY;
    return DESKTOP_TRASH_OK;
}

static int remove_tree(const char *path, uint32_t depth,
                       uint32_t *remaining) {
    if (remaining == 0 || *remaining == 0U)
        return DESKTOP_TRASH_ECAPACITY;
    x86os_file_info_t info;
    if (x86os_stat(path, &info) != 0) return DESKTOP_TRASH_ENOENT;
    if (info.type == X86OS_FILE) {
        --*remaining;
        return x86os_unlink(path) == 0
            ? DESKTOP_TRASH_OK : DESKTOP_TRASH_EIO;
    }
    if (info.type != X86OS_DIRECTORY) return DESKTOP_TRASH_EINVAL;
    if (depth >= DESKTOP_TRASH_DELETE_DEPTH_LIMIT)
        return DESKTOP_TRASH_ECAPACITY;
    while (*remaining != 0U) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, 0U, entries);
        if (count < 0 || (uint32_t)count > X86OS_READDIR_BATCH_CAPACITY)
            return DESKTOP_TRASH_EIO;
        const char *child_name = 0;
        for (int index = 0; index < count; ++index) {
            if (dot_name(entries[index].name)) continue;
            if (!safe_entry_name(entries[index].name))
                return DESKTOP_TRASH_EINVAL;
            child_name = entries[index].name;
            break;
        }
        if (child_name == 0) {
            --*remaining;
            return x86os_rmdir(path) == 0
                ? DESKTOP_TRASH_OK : DESKTOP_TRASH_EIO;
        }
        char child[DESKTOP_TRASH_PATH_CAPACITY];
        int status = append_child_path(path, child_name, child);
        if (status != DESKTOP_TRASH_OK) return status;
        status = remove_tree(child, depth + 1U, remaining);
        if (status != DESKTOP_TRASH_OK) return status;
    }
    return DESKTOP_TRASH_ECAPACITY;
}

static int first_catalog_entry(x86os_file_info_t *entry,
                               char *catalog_path) {
    uint32_t offset = 0U;
    for (uint32_t batch = 0U;
         batch < DESKTOP_TRASH_SCAN_BATCHES; ++batch) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(
            DESKTOP_TRASH_FILES_PATH, offset, entries);
        if (count < 0 || (uint32_t)count > X86OS_READDIR_BATCH_CAPACITY)
            return DESKTOP_TRASH_EIO;
        if (count == 0) return DESKTOP_TRASH_ENOENT;
        for (int index = 0; index < count; ++index) {
            if (dot_name(entries[index].name)) continue;
            if (!safe_entry_name(entries[index].name))
                return DESKTOP_TRASH_EINVAL;
            clear_bytes(entry, sizeof(*entry));
            uint32_t name_length = 0U;
            (void)text_length(
                entries[index].name, sizeof(entries[index].name),
                &name_length);
            for (uint32_t name_index = 0U;
                 name_index <= name_length; ++name_index)
                entry->name[name_index] = entries[index].name[name_index];
            entry->type = entries[index].type;
            entry->size = entries[index].size;
            entry->create_time = entries[index].create_time;
            entry->modify_time = entries[index].modify_time;
            entry->access_time = entries[index].access_time;
            return append_child_path(
                DESKTOP_TRASH_FILES_PATH, entries[index].name,
                catalog_path);
        }
        offset += (uint32_t)count;
    }
    return DESKTOP_TRASH_ECAPACITY;
}

int desktop_trash_empty(desktop_trash_state_t *state,
                        desktop_trash_empty_result_t *result) {
    if (state == 0 || result == 0) return DESKTOP_TRASH_EINVAL;
    desktop_trash_empty_result_initialize(result);
    if (desktop_trash_prepare(state) != DESKTOP_TRASH_OK)
        return DESKTOP_TRASH_EIO;
    uint32_t remaining = DESKTOP_TRASH_DELETE_ENTRY_LIMIT;
    for (uint32_t item = 0U;
         item < DESKTOP_TRASH_EMPTY_CATALOG_LIMIT; ++item) {
        x86os_file_info_t catalog_identity;
        char catalog_path[DESKTOP_TRASH_PATH_CAPACITY];
        int status = first_catalog_entry(&catalog_identity, catalog_path);
        if (status == DESKTOP_TRASH_ENOENT) {
            (void)desktop_trash_refresh(state);
            return DESKTOP_TRASH_OK;
        }
        if (status != DESKTOP_TRASH_OK) {
            result->incomplete = 1U;
            (void)desktop_trash_refresh(state);
            return status;
        }
        desktop_trash_metadata_t metadata;
        char info_path[DESKTOP_TRASH_PATH_CAPACITY];
        status = load_catalog_metadata(
            catalog_path, &catalog_identity, &metadata, info_path);
        if (status == DESKTOP_TRASH_OK)
            status = remove_tree(metadata.stored_path, 0U, &remaining);
        if (status != DESKTOP_TRASH_OK ||
            x86os_unlink(catalog_path) != 0 ||
            x86os_unlink(info_path) != 0) {
            result->incomplete = 1U;
            (void)desktop_trash_refresh(state);
            return status == DESKTOP_TRASH_OK
                ? DESKTOP_TRASH_EIO : status;
        }
        ++result->removed_count;
    }
    result->incomplete = 1U;
    (void)desktop_trash_refresh(state);
    return DESKTOP_TRASH_ECAPACITY;
}
