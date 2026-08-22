/** @file config.c @brief Bounded reist.config/1 parser implementation. */
#include "reist/config.h"

static size_t bounded_length(const char *text, size_t capacity) {
    size_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static uint32_t text_equal(const char *left, const char *right,
                           size_t capacity) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0U;
    while (index < capacity && left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return index < capacity && left[index] == right[index];
}

static uint32_t segment_equal(const char *text, const char *segment,
                              size_t length) {
    if (text == 0 || segment == 0 || text[length] != '\0') return 0U;
    for (size_t index = 0U; index < length; ++index)
        if (text[index] != segment[index]) return 0U;
    return 1U;
}

static int copy_text(char *destination, size_t capacity,
                     const char *source, size_t length) {
    if (destination == 0 || source == 0 || capacity == 0U ||
        length >= capacity) return REIST_CONFIG_ECAPACITY;
    for (size_t index = 0U; index < length; ++index)
        destination[index] = source[index];
    destination[length] = '\0';
    return REIST_CONFIG_OK;
}

static uint32_t valid_key(const char *key, size_t length) {
    if (key == 0 || length == 0U || length >= REIST_CONFIG_KEY_CAPACITY)
        return 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char value = (unsigned char)key[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) return 0U;
    }
    return 1U;
}

static uint32_t valid_value(const char *value, size_t length,
                            size_t capacity) {
    if (value == 0 || length == 0U || length >= capacity) return 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char character = (unsigned char)value[index];
        if (character < 0x20U || character > 0x7EU) return 0U;
    }
    return 1U;
}

void reist_config_initialize(reist_config_document_t *document) {
    if (document == 0) return;
    document->schema[0] = '\0';
    document->entry_count = 0U;
    for (uint32_t index = 0U; index < REIST_CONFIG_ENTRY_CAPACITY; ++index) {
        document->entries[index].key[0] = '\0';
        document->entries[index].value[0] = '\0';
    }
}

static int fail(reist_config_document_t *document, int status) {
    reist_config_initialize(document);
    return status;
}

int reist_config_parse(const char *data, size_t length,
                       const char *expected_schema,
                       reist_config_document_t *document) {
    reist_config_initialize(document);
    if (data == 0 || expected_schema == 0 || document == 0 || length == 0U ||
        length > REIST_CONFIG_FILE_CAPACITY || data[length - 1U] != '\n')
        return fail(document, REIST_CONFIG_EINVAL);
    size_t expected_length = bounded_length(
        expected_schema, REIST_CONFIG_SCHEMA_CAPACITY);
    if (!valid_value(expected_schema, expected_length,
                     REIST_CONFIG_SCHEMA_CAPACITY))
        return fail(document, REIST_CONFIG_ESCHEMA);

    uint32_t effective_lines = 0U;
    size_t offset = 0U;
    while (offset < length) {
        size_t line_start = offset;
        while (offset < length && data[offset] != '\n') ++offset;
        if (offset >= length) return fail(document, REIST_CONFIG_EINVAL);
        size_t line_length = offset - line_start;
        ++offset;
        if (line_length != 0U && data[line_start + line_length - 1U] == '\r')
            --line_length;
        if (line_length >= REIST_CONFIG_LINE_CAPACITY)
            return fail(document, REIST_CONFIG_ECAPACITY);
        if (line_length == 0U || data[line_start] == '#') continue;

        size_t equals = 0U;
        while (equals < line_length && data[line_start + equals] != '=')
            ++equals;
        if (equals == 0U || equals == line_length)
            return fail(document, REIST_CONFIG_EINVAL);
        const char *key = data + line_start;
        const char *value = data + line_start + equals + 1U;
        size_t value_length = line_length - equals - 1U;
        if (!valid_key(key, equals) ||
            !valid_value(value, value_length, REIST_CONFIG_VALUE_CAPACITY))
            return fail(document, REIST_CONFIG_EINVAL);

        if (effective_lines == 0U) {
            if (equals != 6U || key[0] != 's' || key[1] != 'c' ||
                key[2] != 'h' || key[3] != 'e' || key[4] != 'm' ||
                key[5] != 'a' || value_length != expected_length)
                return fail(document, REIST_CONFIG_ESCHEMA);
            for (size_t index = 0U; index < expected_length; ++index)
                if (value[index] != expected_schema[index])
                    return fail(document, REIST_CONFIG_ESCHEMA);
            if (copy_text(document->schema, sizeof(document->schema),
                          value, value_length) != 0)
                return fail(document, REIST_CONFIG_ECAPACITY);
            ++effective_lines;
            continue;
        }
        if (equals == 6U && key[0] == 's' && key[1] == 'c' && key[2] == 'h' &&
            key[3] == 'e' && key[4] == 'm' && key[5] == 'a')
            return fail(document, REIST_CONFIG_EDUPLICATE);
        for (uint32_t index = 0U; index < document->entry_count; ++index) {
            if (bounded_length(document->entries[index].key,
                               REIST_CONFIG_KEY_CAPACITY) == equals &&
                segment_equal(document->entries[index].key, key, equals))
                return fail(document, REIST_CONFIG_EDUPLICATE);
        }
        if (document->entry_count >= REIST_CONFIG_ENTRY_CAPACITY)
            return fail(document, REIST_CONFIG_ECAPACITY);
        reist_config_entry_t *entry =
            &document->entries[document->entry_count++];
        if (copy_text(entry->key, sizeof(entry->key), key, equals) != 0 ||
            copy_text(entry->value, sizeof(entry->value),
                      value, value_length) != 0)
            return fail(document, REIST_CONFIG_ECAPACITY);
        ++effective_lines;
    }
    if (effective_lines == 0U || document->schema[0] == '\0')
        return fail(document, REIST_CONFIG_ESCHEMA);
    return REIST_CONFIG_OK;
}

const char *reist_config_get(const reist_config_document_t *document,
                             const char *key) {
    if (document == 0 || key == 0) return 0;
    size_t key_length = bounded_length(key, REIST_CONFIG_KEY_CAPACITY);
    if (!valid_key(key, key_length)) return 0;
    for (uint32_t index = 0U; index < document->entry_count; ++index)
        if (text_equal(document->entries[index].key, key,
                       REIST_CONFIG_KEY_CAPACITY))
            return document->entries[index].value;
    return 0;
}

int reist_config_set(reist_config_document_t *document, const char *key,
                     const char *value) {
    if (document == 0 || key == 0 || value == 0 ||
        document->schema[0] == '\0' ||
        document->entry_count > REIST_CONFIG_ENTRY_CAPACITY)
        return REIST_CONFIG_EINVAL;
    size_t key_length = bounded_length(key, REIST_CONFIG_KEY_CAPACITY);
    size_t value_length = bounded_length(value, REIST_CONFIG_VALUE_CAPACITY);
    if (!valid_key(key, key_length) ||
        !valid_value(value, value_length, REIST_CONFIG_VALUE_CAPACITY))
        return REIST_CONFIG_EINVAL;
    reist_config_entry_t *entry = 0;
    for (uint32_t index = 0U; index < document->entry_count; ++index)
        if (text_equal(document->entries[index].key, key,
                       REIST_CONFIG_KEY_CAPACITY)) entry = &document->entries[index];
    if (entry == 0) {
        if (document->entry_count >= REIST_CONFIG_ENTRY_CAPACITY)
            return REIST_CONFIG_ECAPACITY;
        entry = &document->entries[document->entry_count];
        if (copy_text(entry->key, sizeof(entry->key), key, key_length) != 0)
            return REIST_CONFIG_ECAPACITY;
        ++document->entry_count;
    }
    return copy_text(entry->value, sizeof(entry->value), value, value_length);
}

static int append(char *output, size_t capacity, size_t *used,
                  const char *text) {
    size_t length = bounded_length(text, capacity);
    if (length >= capacity || *used > capacity || length >= capacity - *used)
        return REIST_CONFIG_ECAPACITY;
    for (size_t index = 0U; index < length; ++index)
        output[(*used)++] = text[index];
    return REIST_CONFIG_OK;
}

int reist_config_serialize(const reist_config_document_t *document,
                           char *output, size_t capacity,
                           size_t *length_out) {
    if (document == 0 || output == 0 || length_out == 0 || capacity == 0U ||
        capacity > REIST_CONFIG_FILE_CAPACITY || document->schema[0] == '\0' ||
        document->entry_count > REIST_CONFIG_ENTRY_CAPACITY)
        return REIST_CONFIG_EINVAL;
    size_t used = 0U;
    output[0] = '\0';
    if (append(output, capacity, &used, "schema=") != 0 ||
        append(output, capacity, &used, document->schema) != 0 ||
        append(output, capacity, &used, "\n") != 0)
        return REIST_CONFIG_ECAPACITY;
    for (uint32_t index = 0U; index < document->entry_count; ++index) {
        const reist_config_entry_t *entry = &document->entries[index];
        size_t key_length = bounded_length(entry->key, sizeof(entry->key));
        size_t value_length = bounded_length(entry->value, sizeof(entry->value));
        if (!valid_key(entry->key, key_length) ||
            !valid_value(entry->value, value_length, sizeof(entry->value)) ||
            append(output, capacity, &used, entry->key) != 0 ||
            append(output, capacity, &used, "=") != 0 ||
            append(output, capacity, &used, entry->value) != 0 ||
            append(output, capacity, &used, "\n") != 0)
            return REIST_CONFIG_ECAPACITY;
    }
    output[used] = '\0';
    *length_out = used;
    return REIST_CONFIG_OK;
}
