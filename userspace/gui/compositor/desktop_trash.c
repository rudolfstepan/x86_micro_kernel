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

uint32_t desktop_trash_source_allowed(const char *path) {
    static const char *const protected_roots[] = {
        DESKTOP_TRASH_ROOT_PATH, "/bin", "/sbin", "/libexec", "/etc",
        "/usr", "/boot", "/dev", "/mnt", "/proc", "/sys", "/run",
        "/var",
    };
    uint32_t length = 0U;
    if (!canonical_absolute_path(path, &length)) return 0U;
    (void)length;
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

static int build_candidate(const char *name, uint32_t attempt,
                           char *stored_path, char *info_path) {
    uint32_t stored_used = 0U;
    uint32_t info_used = 0U;
    if (!append_text(stored_path, DESKTOP_TRASH_PATH_CAPACITY, &stored_used,
                     DESKTOP_TRASH_FILES_PATH "/") ||
        !append_text(stored_path, DESKTOP_TRASH_PATH_CAPACITY, &stored_used,
                     name) ||
        !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &info_used,
                     DESKTOP_TRASH_INFO_PATH "/") ||
        !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &info_used,
                     name)) return DESKTOP_TRASH_ECAPACITY;
    if (attempt != 0U) {
        if (!append_text(stored_path, DESKTOP_TRASH_PATH_CAPACITY,
                         &stored_used, ".") ||
            !append_number(stored_path, DESKTOP_TRASH_PATH_CAPACITY,
                           &stored_used, attempt) ||
            !append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY,
                         &info_used, ".") ||
            !append_number(info_path, DESKTOP_TRASH_PATH_CAPACITY,
                           &info_used, attempt)) return DESKTOP_TRASH_ECAPACITY;
    }
    if (!append_text(info_path, DESKTOP_TRASH_PATH_CAPACITY, &info_used,
                     ".trashinfo")) return DESKTOP_TRASH_ECAPACITY;
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

static int write_metadata(const char *info_path, const char *source_path) {
    char metadata[DESKTOP_TRASH_METADATA_CAPACITY];
    char date[20];
    uint32_t used = 0U;
    deletion_date(date);
    if (!append_text(metadata, sizeof(metadata), &used,
                     "[Trash Info]\nVersion=1\nPath=") ||
        !append_text(metadata, sizeof(metadata), &used, source_path) ||
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
        candidate_status = build_candidate(
            name, attempt, result->stored_path, result->info_path);
        if (candidate_status != DESKTOP_TRASH_OK) return candidate_status;
        x86os_file_info_t collision;
        int stored_status = x86os_stat(result->stored_path, &collision);
        int info_status = x86os_stat(result->info_path, &collision);
        if (stored_status == DESKTOP_TRASH_ENOENT &&
            info_status == DESKTOP_TRASH_ENOENT) break;
        if ((stored_status != 0 && stored_status != DESKTOP_TRASH_ENOENT) ||
            (info_status != 0 && info_status != DESKTOP_TRASH_ENOENT))
            return DESKTOP_TRASH_EIO;
        if (attempt + 1U == DESKTOP_TRASH_COLLISION_LIMIT)
            return DESKTOP_TRASH_ECAPACITY;
    }
    int metadata_status = write_metadata(
        result->info_path, request->source_path);
    if (metadata_status != DESKTOP_TRASH_OK) return metadata_status;
    if (x86os_rename(request->source_path, result->stored_path) != 0) {
        (void)x86os_unlink(result->info_path);
        desktop_trash_result_initialize(result);
        return DESKTOP_TRASH_ERENAME;
    }
    result->moved = 1U;
    state->available = 1U;
    state->full = 1U;
    bump_generation(state);
    return DESKTOP_TRASH_OK;
}
