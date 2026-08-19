#include "desktop_filetypes.h"

static void clear_bytes(void *value, size_t size) {
    uint8_t *bytes = (uint8_t *)value;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static uint32_t bounded_length(const char *text, size_t capacity,
                               size_t *length_out) {
    if (text == 0 || length_out == 0) return 0U;
    for (size_t length = 0U; length < capacity; ++length) {
        if (text[length] == '\0') {
            *length_out = length;
            return 1U;
        }
    }
    return 0U;
}

static uint32_t text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0U;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return left[index] == right[index];
}

static char lower_ascii(char value) {
    return value >= 'A' && value <= 'Z'
        ? (char)(value - 'A' + 'a') : value;
}

static uint32_t extension_valid(const char *extension) {
    size_t length = 0U;
    if (!bounded_length(extension, DESKTOP_FILETYPES_EXTENSION_CAPACITY,
                        &length) || length < 2U || extension[0] != '.')
        return 0U;
    for (size_t index = 1U; index < length; ++index) {
        char value = extension[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9'))) return 0U;
    }
    return 1U;
}

static uint32_t program_valid(const char *program) {
    size_t length = 0U;
    if (!bounded_length(program, DESKTOP_FILETYPES_PROGRAM_CAPACITY,
                        &length) || length < 6U || program[0] != '/' ||
        program[length - 4U] != '.' ||
        lower_ascii(program[length - 3U]) != 'p' ||
        lower_ascii(program[length - 2U]) != 'r' ||
        lower_ascii(program[length - 1U]) != 'g') return 0U;
    for (size_t index = 0U; index < length; ++index) {
        uint8_t value = (uint8_t)program[index];
        if (value < 0x21U || value > 0x7EU || value == '\\') return 0U;
        if (value == '/' && index + 1U < length &&
            program[index + 1U] == '/') return 0U;
        if (value == '/' && index + 2U < length &&
            program[index + 1U] == '.' &&
            (program[index + 2U] == '/' ||
             (program[index + 2U] == '.' && index + 3U < length &&
              program[index + 3U] == '/'))) return 0U;
    }
    return 1U;
}

static void copy_text(char *destination, size_t capacity,
                      const char *source) {
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void desktop_filetypes_initialize(desktop_filetypes_t *table) {
    if (table == 0) return;
    clear_bytes(table, sizeof(*table));
    table->version = DESKTOP_FILETYPES_VERSION;
}

static int publish_line(desktop_filetypes_t *candidate, char *line,
                        uint32_t *schema_seen) {
    if (line[0] == '\0' || line[0] == '#') return DESKTOP_FILETYPES_OK;
    char *equals = 0;
    for (size_t index = 0U; line[index] != '\0'; ++index) {
        uint8_t value = (uint8_t)line[index];
        if (value < 0x20U || value > 0x7EU) return DESKTOP_FILETYPES_EINVAL;
        if (line[index] == '=') {
            if (equals != 0) return DESKTOP_FILETYPES_EINVAL;
            equals = &line[index];
        }
    }
    if (equals == 0 || equals == line || equals[1] == '\0')
        return DESKTOP_FILETYPES_EINVAL;
    *equals = '\0';
    const char *value = equals + 1;
    if (!*schema_seen) {
        if (!text_equal(line, "schema") ||
            !text_equal(value, "reist.filetypes/1"))
            return DESKTOP_FILETYPES_EINVAL;
        *schema_seen = 1U;
        return DESKTOP_FILETYPES_OK;
    }
    if (!extension_valid(line) || !program_valid(value))
        return DESKTOP_FILETYPES_EINVAL;
    if (candidate->entry_count >= DESKTOP_FILETYPES_CAPACITY)
        return DESKTOP_FILETYPES_ECAPACITY;
    for (uint32_t index = 0U; index < candidate->entry_count; ++index)
        if (text_equal(candidate->entries[index].extension, line))
            return DESKTOP_FILETYPES_EINVAL;
    desktop_filetype_entry_t *entry =
        &candidate->entries[candidate->entry_count++];
    copy_text(entry->extension, sizeof(entry->extension), line);
    copy_text(entry->program, sizeof(entry->program), value);
    return DESKTOP_FILETYPES_OK;
}

int desktop_filetypes_parse(desktop_filetypes_t *table,
                            const char *data, size_t size) {
    if (table == 0 || data == 0 || size == 0U ||
        size > DESKTOP_FILETYPES_CONFIG_CAPACITY)
        return DESKTOP_FILETYPES_EINVAL;
    desktop_filetypes_t candidate;
    desktop_filetypes_initialize(&candidate);
    char line[DESKTOP_FILETYPES_PROGRAM_CAPACITY +
              DESKTOP_FILETYPES_EXTENSION_CAPACITY + 2U];
    size_t used = 0U;
    uint32_t schema_seen = 0U;
    for (size_t index = 0U; index <= size; ++index) {
        char value = index == size ? '\n' : data[index];
        if (value == '\r') {
            if (index + 1U < size && data[index + 1U] == '\n') continue;
            value = '\n';
        }
        if (value == '\n') {
            line[used] = '\0';
            int status = publish_line(&candidate, line, &schema_seen);
            if (status != DESKTOP_FILETYPES_OK) return status;
            used = 0U;
            continue;
        }
        if (used + 1U >= sizeof(line)) return DESKTOP_FILETYPES_ECAPACITY;
        line[used++] = value;
    }
    if (!schema_seen || candidate.entry_count == 0U)
        return DESKTOP_FILETYPES_EINVAL;
    desktop_filetypes_initialize(table);
    table->entry_count = candidate.entry_count;
    for (uint32_t index = 0U; index < candidate.entry_count; ++index) {
        copy_text(table->entries[index].extension,
                  sizeof(table->entries[index].extension),
                  candidate.entries[index].extension);
        copy_text(table->entries[index].program,
                  sizeof(table->entries[index].program),
                  candidate.entries[index].program);
    }
    return DESKTOP_FILETYPES_OK;
}

int desktop_filetypes_lookup(const desktop_filetypes_t *table,
                             const char *path, const char **program_out) {
    size_t length = 0U;
    if (table == 0 || table->version != DESKTOP_FILETYPES_VERSION ||
        table->entry_count > DESKTOP_FILETYPES_CAPACITY || path == 0 ||
        program_out == 0 ||
        !bounded_length(path, DESKTOP_FILETYPES_PROGRAM_CAPACITY, &length) ||
        length == 0U) return DESKTOP_FILETYPES_EINVAL;
    size_t extension = length;
    for (size_t index = length; index != 0U; --index) {
        char value = path[index - 1U];
        if (value == '/') break;
        if (value == '.') {
            extension = index - 1U;
            break;
        }
    }
    if (extension == length) return DESKTOP_FILETYPES_ENOTFOUND;
    for (uint32_t entry = 0U; entry < table->entry_count; ++entry) {
        const char *configured = table->entries[entry].extension;
        size_t index = 0U;
        while (extension + index < length && configured[index] != '\0' &&
               lower_ascii(path[extension + index]) == configured[index])
            ++index;
        if (extension + index == length && configured[index] == '\0') {
            *program_out = table->entries[entry].program;
            return DESKTOP_FILETYPES_OK;
        }
    }
    return DESKTOP_FILETYPES_ENOTFOUND;
}
