/**
 * @file userspace/gui/compositor/desktop_layout.c
 * @brief Heap-free desktop icon layout and reist.desktop-layout/1 codec.
 */
#include "desktop_layout.h"

#include <limits.h>

static desktop_layout_document_t desktop_layout_parse_workspace;
static desktop_layout_document_t desktop_layout_move_workspace;

static void clear_bytes(void *value, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_bytes(void *destination, const void *source, uint32_t size) {
    volatile uint8_t *output = (volatile uint8_t *)destination;
    const volatile uint8_t *input = (const volatile uint8_t *)source;
    for (uint32_t index = 0U; index < size; ++index)
        output[index] = input[index];
}

static uint32_t text_length(const char *text, uint32_t capacity,
                            uint32_t *length_out) {
    if (text == 0 || capacity == 0U) return 0U;
    uint32_t length = 0U;
    while (length < capacity && text[length] != '\0') ++length;
    if (length == 0U || length == capacity) return 0U;
    if (length_out != 0) *length_out = length;
    return 1U;
}

static uint32_t prefix_equal(const char *text, const char *prefix) {
    if (text == 0 || prefix == 0) return 0U;
    uint32_t index = 0U;
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) return 0U;
        ++index;
    }
    return 1U;
}

static uint32_t identity_valid(const desktop_layout_identity_t *identity) {
    uint32_t length = 0U;
    if (identity == 0 ||
        !text_length(identity->value, DESKTOP_LAYOUT_IDENTITY_CAPACITY,
                     &length))
        return 0U;
    uint32_t prefix = prefix_equal(identity->value, "builtin:") ? 8U
        : prefix_equal(identity->value, "desktop:") ? 8U : 0U;
    if (prefix == 0U || length == prefix) return 0U;
    for (uint32_t index = prefix; index < length; ++index) {
        uint8_t byte = (uint8_t)identity->value[index];
        if (byte == 0U || byte == '/' || byte < 0x20U || byte == 0x7FU)
            return 0U;
    }
    return 1U;
}

static int identity_compare(const desktop_layout_identity_t *left,
                            const desktop_layout_identity_t *right) {
    for (uint32_t index = 0U;
         index < DESKTOP_LAYOUT_IDENTITY_CAPACITY; ++index) {
        uint8_t a = (uint8_t)left->value[index];
        uint8_t b = (uint8_t)right->value[index];
        if (a < b) return -1;
        if (a > b) return 1;
        if (a == 0U) return 0;
    }
    return 0;
}

static uint32_t document_valid(const desktop_layout_document_t *document) {
    if (document == 0 ||
        document->entry_count > DESKTOP_LAYOUT_ENTRY_CAPACITY)
        return 0U;
    for (uint32_t left = 0U; left < document->entry_count; ++left) {
        const desktop_layout_entry_t *entry = &document->entries[left];
        if (!identity_valid(&entry->identity) ||
            entry->column >= DESKTOP_LAYOUT_GRID_AXIS_CAPACITY ||
            entry->row >= DESKTOP_LAYOUT_GRID_AXIS_CAPACITY)
            return 0U;
        for (uint32_t right = left + 1U;
             right < document->entry_count; ++right) {
            const desktop_layout_entry_t *other = &document->entries[right];
            if (identity_compare(&entry->identity, &other->identity) == 0 ||
                (entry->column == other->column &&
                 entry->row == other->row))
                return 0U;
        }
    }
    return 1U;
}

void desktop_layout_document_initialize(desktop_layout_document_t *document) {
    if (document != 0) clear_bytes(document, sizeof(*document));
}

void desktop_layout_view_initialize(desktop_layout_view_t *view) {
    if (view != 0) clear_bytes(view, sizeof(*view));
}

static int identity_from_text(desktop_layout_identity_t *identity,
                              const char *prefix, const char *value) {
    if (identity == 0 || prefix == 0 || value == 0)
        return DESKTOP_LAYOUT_EINVAL;
    desktop_layout_identity_t candidate;
    clear_bytes(&candidate, sizeof(candidate));
    uint32_t used = 0U;
    while (prefix[used] != '\0') {
        if (used + 1U >= DESKTOP_LAYOUT_IDENTITY_CAPACITY)
            return DESKTOP_LAYOUT_ECAPACITY;
        candidate.value[used] = prefix[used];
        ++used;
    }
    uint32_t source = 0U;
    while (value[source] != '\0') {
        uint8_t byte = (uint8_t)value[source];
        if (byte == '/' || byte < 0x20U || byte == 0x7FU)
            return DESKTOP_LAYOUT_EINVAL;
        if (used + 1U >= DESKTOP_LAYOUT_IDENTITY_CAPACITY)
            return DESKTOP_LAYOUT_ECAPACITY;
        candidate.value[used++] = value[source++];
    }
    if (source == 0U || !identity_valid(&candidate))
        return DESKTOP_LAYOUT_EINVAL;
    copy_bytes(identity, &candidate, sizeof(candidate));
    return DESKTOP_LAYOUT_OK;
}

int desktop_layout_identity_builtin(desktop_layout_identity_t *identity,
                                    uint32_t builtin_index) {
    static const char *const names[DESKTOP_LAYOUT_BUILTIN_COUNT] = {
        "computer", "control", "trash"
    };
    if (builtin_index >= DESKTOP_LAYOUT_BUILTIN_COUNT)
        return DESKTOP_LAYOUT_EINVAL;
    return identity_from_text(identity, "builtin:", names[builtin_index]);
}

int desktop_layout_identity_file(desktop_layout_identity_t *identity,
                                 const char *filename) {
    return identity_from_text(identity, "desktop:", filename);
}

static int hex_value(uint8_t byte) {
    if (byte >= '0' && byte <= '9') return (int)(byte - '0');
    if (byte >= 'a' && byte <= 'f') return (int)(byte - 'a') + 10;
    return -1;
}

static uint32_t parse_decimal(const uint8_t *bytes, uint32_t size,
                              uint32_t *value_out) {
    if (bytes == 0 || size == 0U || value_out == 0 ||
        (size > 1U && bytes[0] == '0')) return 0U;
    uint32_t value = 0U;
    for (uint32_t index = 0U; index < size; ++index) {
        if (bytes[index] < '0' || bytes[index] > '9') return 0U;
        uint32_t digit = (uint32_t)(bytes[index] - '0');
        if (value > (UINT32_MAX - digit) / 10U) return 0U;
        value = value * 10U + digit;
    }
    *value_out = value;
    return 1U;
}

static uint32_t schema_prefix(uint8_t *output) {
    static const char prefix[] = "schema=" DESKTOP_LAYOUT_SCHEMA "\n";
    uint32_t index = 0U;
    while (prefix[index] != '\0') {
        if (output != 0) output[index] = (uint8_t)prefix[index];
        ++index;
    }
    return index;
}

int desktop_layout_parse(const uint8_t *bytes, uint32_t size,
                         desktop_layout_document_t *document) {
    if (bytes == 0 || document == 0 || size > DESKTOP_LAYOUT_FILE_CAPACITY)
        return size > DESKTOP_LAYOUT_FILE_CAPACITY
            ? DESKTOP_LAYOUT_ECAPACITY : DESKTOP_LAYOUT_EINVAL;
    uint8_t expected[32];
    uint32_t expected_size = schema_prefix(expected);
    if (size < expected_size) return DESKTOP_LAYOUT_EMALFORMED;
    for (uint32_t index = 0U; index < expected_size; ++index)
        if (bytes[index] != expected[index])
            return DESKTOP_LAYOUT_EMALFORMED;

    desktop_layout_document_initialize(&desktop_layout_parse_workspace);
    uint32_t offset = expected_size;
    while (offset < size) {
        static const char key[] = "icon=";
        for (uint32_t index = 0U; index < sizeof(key) - 1U; ++index)
            if (offset + index >= size ||
                bytes[offset + index] != (uint8_t)key[index])
                return DESKTOP_LAYOUT_EMALFORMED;
        uint32_t line_end = offset;
        while (line_end < size && bytes[line_end] != '\n') ++line_end;
        if (line_end == size ||
            desktop_layout_parse_workspace.entry_count >=
                DESKTOP_LAYOUT_ENTRY_CAPACITY)
            return line_end == size ? DESKTOP_LAYOUT_EMALFORMED
                                    : DESKTOP_LAYOUT_ECAPACITY;
        uint32_t first = offset + (uint32_t)(sizeof(key) - 1U);
        uint32_t comma_one = first;
        while (comma_one < line_end && bytes[comma_one] != ',') ++comma_one;
        uint32_t comma_two = comma_one + 1U;
        while (comma_two < line_end && bytes[comma_two] != ',') ++comma_two;
        uint32_t hex_size = comma_one - first;
        if (comma_one == line_end || comma_two >= line_end ||
            hex_size == 0U || (hex_size & 1U) != 0U ||
            hex_size / 2U >= DESKTOP_LAYOUT_IDENTITY_CAPACITY)
            return DESKTOP_LAYOUT_EMALFORMED;
        desktop_layout_entry_t entry;
        clear_bytes(&entry, sizeof(entry));
        for (uint32_t index = 0U; index < hex_size; index += 2U) {
            int high = hex_value(bytes[first + index]);
            int low = hex_value(bytes[first + index + 1U]);
            if (high < 0 || low < 0) return DESKTOP_LAYOUT_EMALFORMED;
            entry.identity.value[index / 2U] =
                (char)(((uint32_t)high << 4U) | (uint32_t)low);
            if (entry.identity.value[index / 2U] == '\0')
                return DESKTOP_LAYOUT_EMALFORMED;
        }
        if (!identity_valid(&entry.identity) ||
            !parse_decimal(bytes + comma_one + 1U,
                           comma_two - comma_one - 1U, &entry.column) ||
            !parse_decimal(bytes + comma_two + 1U,
                           line_end - comma_two - 1U, &entry.row) ||
            entry.column >= DESKTOP_LAYOUT_GRID_AXIS_CAPACITY ||
            entry.row >= DESKTOP_LAYOUT_GRID_AXIS_CAPACITY)
            return DESKTOP_LAYOUT_EMALFORMED;
        for (uint32_t index = 0U;
             index < desktop_layout_parse_workspace.entry_count; ++index) {
            const desktop_layout_entry_t *existing =
                &desktop_layout_parse_workspace.entries[index];
            if (identity_compare(&existing->identity, &entry.identity) == 0 ||
                (existing->column == entry.column &&
                 existing->row == entry.row))
                return DESKTOP_LAYOUT_EMALFORMED;
        }
        copy_bytes(
            &desktop_layout_parse_workspace.entries[
                desktop_layout_parse_workspace.entry_count++],
            &entry, sizeof(entry));
        offset = line_end + 1U;
    }
    copy_bytes(document, &desktop_layout_parse_workspace, sizeof(*document));
    return DESKTOP_LAYOUT_OK;
}

static int append_byte(uint8_t *bytes, uint32_t capacity,
                       uint32_t *used, uint8_t value) {
    if (*used >= capacity) return DESKTOP_LAYOUT_ECAPACITY;
    bytes[(*used)++] = value;
    return DESKTOP_LAYOUT_OK;
}

static int append_decimal(uint8_t *bytes, uint32_t capacity,
                          uint32_t *used, uint32_t value) {
    uint8_t reversed[10];
    uint32_t count = 0U;
    do {
        reversed[count++] = (uint8_t)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(reversed));
    while (count != 0U) {
        int status = append_byte(bytes, capacity, used, reversed[--count]);
        if (status != DESKTOP_LAYOUT_OK) return status;
    }
    return DESKTOP_LAYOUT_OK;
}

int desktop_layout_serialize(const desktop_layout_document_t *document,
                             uint8_t *bytes, uint32_t capacity,
                             uint32_t *size_out) {
    if (!document_valid(document) || bytes == 0 || size_out == 0)
        return DESKTOP_LAYOUT_EINVAL;
    uint32_t used = 0U;
    uint8_t schema[32];
    uint32_t schema_size = schema_prefix(schema);
    for (uint32_t index = 0U; index < schema_size; ++index) {
        int status = append_byte(bytes, capacity, &used, schema[index]);
        if (status != DESKTOP_LAYOUT_OK) return status;
    }
    uint8_t emitted[DESKTOP_LAYOUT_ENTRY_CAPACITY];
    clear_bytes(emitted, sizeof(emitted));
    static const uint8_t hex[] = "0123456789abcdef";
    for (uint32_t output_index = 0U;
         output_index < document->entry_count; ++output_index) {
        uint32_t selected = DESKTOP_LAYOUT_ENTRY_CAPACITY;
        for (uint32_t index = 0U; index < document->entry_count; ++index) {
            if (emitted[index]) continue;
            if (selected == DESKTOP_LAYOUT_ENTRY_CAPACITY ||
                identity_compare(&document->entries[index].identity,
                                 &document->entries[selected].identity) < 0)
                selected = index;
        }
        if (selected == DESKTOP_LAYOUT_ENTRY_CAPACITY)
            return DESKTOP_LAYOUT_EINVAL;
        emitted[selected] = 1U;
        static const char key[] = "icon=";
        for (uint32_t index = 0U; index < sizeof(key) - 1U; ++index)
            if (append_byte(bytes, capacity, &used,
                            (uint8_t)key[index]) != DESKTOP_LAYOUT_OK)
                return DESKTOP_LAYOUT_ECAPACITY;
        const desktop_layout_entry_t *entry = &document->entries[selected];
        uint32_t identity_size = 0U;
        (void)text_length(entry->identity.value,
                          DESKTOP_LAYOUT_IDENTITY_CAPACITY, &identity_size);
        for (uint32_t index = 0U; index < identity_size; ++index) {
            uint8_t value = (uint8_t)entry->identity.value[index];
            if (append_byte(bytes, capacity, &used, hex[value >> 4U]) !=
                    DESKTOP_LAYOUT_OK ||
                append_byte(bytes, capacity, &used, hex[value & 0xFU]) !=
                    DESKTOP_LAYOUT_OK)
                return DESKTOP_LAYOUT_ECAPACITY;
        }
        if (append_byte(bytes, capacity, &used, ',') != DESKTOP_LAYOUT_OK ||
            append_decimal(bytes, capacity, &used, entry->column) !=
                DESKTOP_LAYOUT_OK ||
            append_byte(bytes, capacity, &used, ',') != DESKTOP_LAYOUT_OK ||
            append_decimal(bytes, capacity, &used, entry->row) !=
                DESKTOP_LAYOUT_OK ||
            append_byte(bytes, capacity, &used, '\n') != DESKTOP_LAYOUT_OK)
            return DESKTOP_LAYOUT_ECAPACITY;
    }
    *size_out = used;
    return DESKTOP_LAYOUT_OK;
}

static uint32_t occupied(const desktop_layout_view_t *view,
                         uint32_t count, uint32_t column, uint32_t row,
                         uint32_t excluded) {
    for (uint32_t index = 0U; index < count; ++index)
        if (index != excluded && view->entries[index].column == column &&
            view->entries[index].row == row)
            return 1U;
    return 0U;
}

static int nearest_free(const desktop_layout_view_t *view, uint32_t count,
                        uint32_t desired_column, uint32_t desired_row,
                        uint32_t excluded, desktop_layout_cell_t *cell) {
    if (view == 0 || cell == 0 || view->columns == 0U || view->rows == 0U)
        return DESKTOP_LAYOUT_EINVAL;
    if (!occupied(view, count, desired_column, desired_row, excluded)) {
        cell->column = desired_column;
        cell->row = desired_row;
        return DESKTOP_LAYOUT_OK;
    }
    uint64_t total = (uint64_t)view->columns * view->rows;
    uint32_t limit = total > DESKTOP_LAYOUT_SEARCH_CAPACITY
        ? DESKTOP_LAYOUT_SEARCH_CAPACITY : (uint32_t)total;
    uint32_t best = UINT32_MAX;
    uint32_t best_distance = UINT32_MAX;
    for (uint32_t linear = 0U; linear < limit; ++linear) {
        uint32_t column = linear / view->rows;
        uint32_t row = linear % view->rows;
        if (occupied(view, count, column, row, excluded)) continue;
        uint32_t dx = column > desired_column
            ? column - desired_column : desired_column - column;
        uint32_t dy = row > desired_row
            ? row - desired_row : desired_row - row;
        uint32_t distance = dx + dy;
        if (distance < best_distance) {
            best = linear;
            best_distance = distance;
        }
    }
    if (best == UINT32_MAX) return DESKTOP_LAYOUT_EOCCUPIED;
    cell->column = best / view->rows;
    cell->row = best % view->rows;
    return DESKTOP_LAYOUT_OK;
}

static const desktop_layout_entry_t *find_entry(
    const desktop_layout_document_t *document,
    const desktop_layout_identity_t *identity) {
    for (uint32_t index = 0U; index < document->entry_count; ++index)
        if (identity_compare(&document->entries[index].identity, identity) == 0)
            return &document->entries[index];
    return 0;
}

int desktop_layout_resolve(
    const desktop_layout_document_t *document,
    const desktop_layout_identity_t *identities, uint32_t identity_count,
    desktop_rect_t work_area, uint32_t cell_height, uint32_t generation,
    desktop_layout_view_t *view) {
    if (!document_valid(document) || identities == 0 || view == 0 ||
        identity_count > DESKTOP_LAYOUT_ENTRY_CAPACITY ||
        work_area.width == 0U || work_area.height == 0U ||
        work_area.x < 0 || work_area.y < 0 || cell_height == 0U ||
        generation == 0U)
        return identity_count > DESKTOP_LAYOUT_ENTRY_CAPACITY
            ? DESKTOP_LAYOUT_ECAPACITY : DESKTOP_LAYOUT_EINVAL;
    for (uint32_t left = 0U; left < identity_count; ++left) {
        if (!identity_valid(&identities[left])) return DESKTOP_LAYOUT_EINVAL;
        for (uint32_t right = left + 1U; right < identity_count; ++right)
            if (identity_compare(&identities[left], &identities[right]) == 0)
                return DESKTOP_LAYOUT_EINVAL;
    }
    desktop_layout_view_initialize(view);
    view->generation = generation;
    view->work_area = work_area;
    view->cell_height = cell_height > work_area.height
        ? work_area.height : cell_height;
    view->rows = work_area.height / view->cell_height;
    if (view->rows == 0U) view->rows = 1U;
    if (view->rows > DESKTOP_LAYOUT_GRID_AXIS_CAPACITY)
        view->rows = DESKTOP_LAYOUT_GRID_AXIS_CAPACITY;
    uint32_t required_columns = identity_count / view->rows +
        (identity_count % view->rows != 0U);
    uint32_t natural_columns =
        work_area.width / DESKTOP_LAYOUT_PREFERRED_CELL_WIDTH;
    if (natural_columns == 0U) natural_columns = 1U;
    view->columns = natural_columns > required_columns
        ? natural_columns : required_columns;
    if (view->columns == 0U) view->columns = 1U;
    if (view->columns > DESKTOP_LAYOUT_GRID_AXIS_CAPACITY)
        return DESKTOP_LAYOUT_ECAPACITY;
    view->cell_width = work_area.width / view->columns;
    if (view->cell_width == 0U) return DESKTOP_LAYOUT_ECAPACITY;
    if (view->cell_width > DESKTOP_LAYOUT_PREFERRED_CELL_WIDTH)
        view->cell_width = DESKTOP_LAYOUT_PREFERRED_CELL_WIDTH;

    for (uint32_t index = 0U; index < identity_count; ++index) {
        const desktop_layout_entry_t *stored =
            find_entry(document, &identities[index]);
        uint32_t default_column = index / view->rows;
        uint32_t default_row = index % view->rows;
        uint32_t desired_column = stored != 0 ? stored->column : default_column;
        uint32_t desired_row = stored != 0 ? stored->row : default_row;
        if (desired_column >= view->columns)
            desired_column = view->columns - 1U;
        if (desired_row >= view->rows) desired_row = view->rows - 1U;
        desktop_layout_cell_t cell;
        int status = nearest_free(view, index, desired_column, desired_row,
                                  UINT32_MAX, &cell);
        if (status != DESKTOP_LAYOUT_OK) return status;
        copy_bytes(&view->entries[index].identity, &identities[index],
                   sizeof(identities[index]));
        view->entries[index].column = cell.column;
        view->entries[index].row = cell.row;
        ++view->entry_count;
    }
    return DESKTOP_LAYOUT_OK;
}

desktop_rect_t desktop_layout_view_rect(const desktop_layout_view_t *view,
                                        uint32_t index) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (view == 0 || index >= view->entry_count || view->cell_width == 0U ||
        view->cell_height == 0U ||
        view->entries[index].column >= view->columns ||
        view->entries[index].row >= view->rows)
        return empty;
    uint64_t x = (uint64_t)(uint32_t)view->work_area.x +
        (uint64_t)view->entries[index].column * view->cell_width;
    uint64_t y = (uint64_t)(uint32_t)view->work_area.y +
        (uint64_t)view->entries[index].row * view->cell_height;
    uint64_t right = (uint64_t)(uint32_t)view->work_area.x +
        view->work_area.width;
    uint64_t bottom = (uint64_t)(uint32_t)view->work_area.y +
        view->work_area.height;
    if (x >= right || y >= bottom || x > INT32_MAX || y > INT32_MAX)
        return empty;
    desktop_rect_t rect = {
        (int32_t)x, (int32_t)y,
        view->cell_width,
        view->cell_height
    };
    if (x + rect.width > right) rect.width = (uint32_t)(right - x);
    if (y + rect.height > bottom) rect.height = (uint32_t)(bottom - y);
    return rect;
}

int desktop_layout_drop(const desktop_layout_view_t *view,
                        uint32_t source_index, int32_t x, int32_t y,
                        desktop_layout_cell_t *cell) {
    if (view == 0 || cell == 0 || source_index >= view->entry_count ||
        view->cell_width == 0U || view->cell_height == 0U ||
        x < view->work_area.x || y < view->work_area.y ||
        (uint64_t)(uint32_t)(x - view->work_area.x) >= view->work_area.width ||
        (uint64_t)(uint32_t)(y - view->work_area.y) >= view->work_area.height)
        return DESKTOP_LAYOUT_EINVAL;
    uint32_t column = (uint32_t)(x - view->work_area.x) / view->cell_width;
    uint32_t row = (uint32_t)(y - view->work_area.y) / view->cell_height;
    if (column >= view->columns) column = view->columns - 1U;
    if (row >= view->rows) row = view->rows - 1U;
    return nearest_free(view, view->entry_count, column, row,
                        source_index, cell);
}

int desktop_layout_move_document(const desktop_layout_view_t *view,
                                 uint32_t source_index,
                                 desktop_layout_cell_t cell,
                                 desktop_layout_document_t *candidate) {
    if (view == 0 || candidate == 0 || source_index >= view->entry_count ||
        view->entry_count > DESKTOP_LAYOUT_ENTRY_CAPACITY ||
        cell.column >= view->columns || cell.row >= view->rows ||
        occupied(view, view->entry_count, cell.column, cell.row,
                 source_index))
        return DESKTOP_LAYOUT_EINVAL;
    desktop_layout_document_initialize(&desktop_layout_move_workspace);
    desktop_layout_move_workspace.entry_count = view->entry_count;
    for (uint32_t index = 0U; index < view->entry_count; ++index) {
        desktop_layout_entry_t *entry =
            &desktop_layout_move_workspace.entries[index];
        copy_bytes(&entry->identity, &view->entries[index].identity,
                   sizeof(entry->identity));
        entry->column = index == source_index
            ? cell.column : view->entries[index].column;
        entry->row = index == source_index
            ? cell.row : view->entries[index].row;
    }
    if (!document_valid(&desktop_layout_move_workspace))
        return DESKTOP_LAYOUT_EINVAL;
    copy_bytes(candidate, &desktop_layout_move_workspace, sizeof(*candidate));
    return DESKTOP_LAYOUT_OK;
}
