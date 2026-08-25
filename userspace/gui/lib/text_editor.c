#include "reist/gui/text_editor.h"

#include "../../../include/reist/utf.h"

static void clear_bytes(void *value, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static uint32_t words_zero(const uint32_t *words, uint32_t count) {
    for (uint32_t index = 0U; index < count; ++index)
        if (words[index] != 0U) return 0U;
    return 1U;
}

static uint32_t bounded_length(const char *text, uint32_t capacity,
                               uint32_t *length_out) {
    if (text == NULL || length_out == NULL) return 0U;
    for (uint32_t length = 0U; length < capacity; ++length) {
        if (text[length] == '\0') {
            *length_out = length;
            return 1U;
        }
    }
    return 0U;
}

static uint32_t name_valid(const char *name) {
    uint32_t length = 0U;
    return bounded_length(name, REIST_GUI_TEXT_EDITOR_NAME_LIMIT, &length) &&
           length != 0U;
}

static uint32_t rect_valid(reist_gui_rect_t rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.width != 0U &&
           rect.height != 0U &&
           (uint64_t)(uint32_t)rect.x + rect.width <= INT32_MAX &&
           (uint64_t)(uint32_t)rect.y + rect.height <= INT32_MAX;
}

static uint32_t flags_valid(uint32_t flags) {
    return (flags & ~(REIST_GUI_TEXT_EDITOR_VISIBLE |
                      REIST_GUI_TEXT_EDITOR_ENABLED |
                      REIST_GUI_TEXT_EDITOR_READ_ONLY)) == 0U;
}

static uint32_t interactive(uint32_t flags) {
    return (flags & (REIST_GUI_TEXT_EDITOR_VISIBLE |
                     REIST_GUI_TEXT_EDITOR_ENABLED)) ==
           (REIST_GUI_TEXT_EDITOR_VISIBLE |
            REIST_GUI_TEXT_EDITOR_ENABLED);
}

static uint32_t model_valid(const reist_gui_text_editor_model_t *model) {
    return model != NULL &&
           model->version == REIST_GUI_TEXT_EDITOR_API_VERSION &&
           model->struct_size == sizeof(*model) && model->id != 0U &&
           name_valid(model->name) && rect_valid(model->bounds) &&
           model->glyph_width != 0U && model->glyph_height != 0U &&
           model->glyph_width <= model->bounds.width &&
           model->glyph_height <= model->bounds.height &&
           flags_valid(model->flags) && words_zero(model->reserved, 4U);
}

static uint32_t point_inside(reist_gui_rect_t rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y &&
           (uint64_t)(uint32_t)(x - rect.x) < rect.width &&
           (uint64_t)(uint32_t)(y - rect.y) < rect.height;
}

static uint32_t result_valid(const reist_gui_text_editor_result_t *result) {
    return result != NULL &&
           result->version == REIST_GUI_TEXT_EDITOR_API_VERSION &&
           result->struct_size == sizeof(*result) &&
           result->damage_count <= REIST_GUI_TEXT_EDITOR_DAMAGE_CAPACITY &&
           result->full_redraw <= 1U && words_zero(result->reserved, 4U);
}

static uint32_t event_valid(const reist_gui_text_editor_event_t *event) {
    if (event == NULL ||
        event->version != REIST_GUI_TEXT_EDITOR_API_VERSION ||
        event->struct_size != sizeof(*event) ||
        !words_zero(event->reserved, 4U)) return 0U;
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_POINTER_MOTION)
        return event->button == 0U && event->pressed == 0U &&
               event->key == 0U && event->codepoint == 0U &&
               event->focused == 0U;
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_POINTER_BUTTON)
        return event->button == REIST_GUI_TEXT_EDITOR_BUTTON_LEFT &&
               event->pressed <= 1U && event->key == 0U &&
               event->codepoint == 0U && event->focused == 0U;
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_KEYBOARD)
        return event->button == 0U && event->pressed == 0U &&
               event->codepoint == 0U && event->focused == 0U &&
               event->key >= REIST_GUI_TEXT_EDITOR_KEY_LEFT &&
               event->key <= REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_END;
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_TEXT)
        return event->button == 0U && event->pressed == 0U &&
               event->key == 0U && event->focused == 0U &&
               event->codepoint >= 0x20U && event->codepoint <= 0x10FFFFU &&
               !(event->codepoint >= 0x7FU &&
                 event->codepoint <= 0x9FU) &&
               !(event->codepoint >= 0xD800U &&
                 event->codepoint <= 0xDFFFU);
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_FOCUS)
        return event->button == 0U && event->pressed == 0U &&
               event->key == 0U && event->codepoint == 0U &&
               event->focused <= 1U;
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_CANCEL)
        return event->button == 0U && event->pressed == 0U &&
               event->key == 0U && event->codepoint == 0U &&
               event->focused == 0U;
    return 0U;
}

static uint32_t line_length(const char *line) {
    uint32_t length = 0U;
    while (length < REIST_GUI_TEXT_EDITOR_LINE_CAPACITY &&
           line[length] != '\0') ++length;
    return length;
}

static uint32_t scalar_printable(uint32_t scalar) {
    return scalar >= 0x20U && !(scalar >= 0x7FU && scalar <= 0x9FU);
}

static uint32_t line_scalar_count(const char *line) {
    uint32_t bytes = line_length(line);
    size_t count = 0U;
    if (!reist_utf8_scan(line, bytes, &count) || count > UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)count;
}

static uint32_t line_byte_offset(const char *line, uint32_t scalar_column) {
    uint32_t bytes = line_length(line);
    size_t offset = 0U;
    uint32_t column = 0U;
    while (offset < bytes && column < scalar_column) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(line + offset, bytes - offset,
                                   &consumed, &scalar))
            return UINT32_MAX;
        offset += consumed;
        ++column;
    }
    return column == scalar_column ? (uint32_t)offset : UINT32_MAX;
}

static uint32_t encode_scalar(uint32_t scalar, char bytes[4]) {
    if (!scalar_printable(scalar) || scalar > 0x10FFFFU ||
        (scalar >= 0xD800U && scalar <= 0xDFFFU)) return 0U;
    if (scalar <= 0x7FU) {
        bytes[0] = (char)scalar;
        return 1U;
    }
    if (scalar <= 0x7FFU) {
        bytes[0] = (char)(0xC0U | (scalar >> 6U));
        bytes[1] = (char)(0x80U | (scalar & 0x3FU));
        return 2U;
    }
    if (scalar <= 0xFFFFU) {
        bytes[0] = (char)(0xE0U | (scalar >> 12U));
        bytes[1] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
        bytes[2] = (char)(0x80U | (scalar & 0x3FU));
        return 3U;
    }
    bytes[0] = (char)(0xF0U | (scalar >> 18U));
    bytes[1] = (char)(0x80U | ((scalar >> 12U) & 0x3FU));
    bytes[2] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
    bytes[3] = (char)(0x80U | (scalar & 0x3FU));
    return 4U;
}

static void clear_line(char *line) {
    for (uint32_t index = 0U;
         index < REIST_GUI_TEXT_EDITOR_LINE_CAPACITY; ++index)
        line[index] = '\0';
}

static void copy_line(char *destination, const char *source) {
    uint32_t index = 0U;
    while (index + 1U < REIST_GUI_TEXT_EDITOR_LINE_CAPACITY &&
           source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index++] = '\0';
    while (index < REIST_GUI_TEXT_EDITOR_LINE_CAPACITY)
        destination[index++] = '\0';
}

static void request_full(const reist_gui_text_editor_model_t *model,
                         reist_gui_text_editor_result_t *result) {
    result->damage_count = 0U;
    result->full_redraw = 1U;
    result->damage[0] = model->bounds;
}

static void publish_state(const reist_gui_text_editor_model_t *model,
                          const reist_gui_text_editor_state_t *state,
                          reist_gui_text_editor_result_t *result) {
    result->control_id = model->id;
    result->modified = state->modified;
    result->cursor_line = state->cursor_line;
    result->cursor_column = state->cursor_column;
    result->first_line = state->first_line;
    result->first_column = state->first_column;
}

void reist_gui_text_editor_state_initialize(
    reist_gui_text_editor_state_t *state) {
    if (state == NULL) return;
    clear_bytes(state, sizeof(*state));
    state->version = REIST_GUI_TEXT_EDITOR_API_VERSION;
    state->struct_size = sizeof(*state);
}

void reist_gui_text_editor_event_initialize(
    reist_gui_text_editor_event_t *event) {
    if (event == NULL) return;
    clear_bytes(event, sizeof(*event));
    event->version = REIST_GUI_TEXT_EDITOR_API_VERSION;
    event->struct_size = sizeof(*event);
}

void reist_gui_text_editor_result_initialize(
    reist_gui_text_editor_result_t *result) {
    if (result == NULL) return;
    clear_bytes(result, sizeof(*result));
    result->version = REIST_GUI_TEXT_EDITOR_API_VERSION;
    result->struct_size = sizeof(*result);
}

int reist_gui_text_editor_validate(
    const reist_gui_text_editor_model_t *model,
    const reist_gui_text_editor_state_t *state) {
    if (!model_valid(model) || state == NULL ||
        state->version != REIST_GUI_TEXT_EDITOR_API_VERSION ||
        state->struct_size != sizeof(*state) || state->configured > 1U ||
        state->focused > 1U || state->captured > 1U ||
        state->modified > 1U || !words_zero(state->reserved, 4U))
        return REIST_GUI_TEXT_EDITOR_EINVAL;
    if (!state->configured) {
        return state->focused == 0U && state->captured == 0U &&
               state->modified == 0U && state->line_count == 0U &&
               state->cursor_line == 0U && state->cursor_column == 0U &&
               state->preferred_column == 0U && state->first_line == 0U &&
               state->first_column == 0U && state->lines[0][0] == '\0'
            ? REIST_GUI_TEXT_EDITOR_OK : REIST_GUI_TEXT_EDITOR_EINVAL;
    }
    if (state->line_count == 0U ||
        state->line_count > REIST_GUI_TEXT_EDITOR_MAX_LINES ||
        state->cursor_line >= state->line_count ||
        state->first_line >= state->line_count ||
        (!interactive(model->flags) && (state->focused || state->captured)) ||
        (!state->focused && state->captured))
        return REIST_GUI_TEXT_EDITOR_EINVAL;
    for (uint32_t line = 0U; line < state->line_count; ++line) {
        uint32_t length = 0U;
        if (!bounded_length(state->lines[line],
                            REIST_GUI_TEXT_EDITOR_LINE_CAPACITY, &length))
            return REIST_GUI_TEXT_EDITOR_EINVAL;
        size_t offset = 0U;
        uint32_t columns = 0U;
        while (offset < length) {
            size_t consumed = 0U;
            uint32_t scalar = 0U;
            if (!reist_utf8_decode_one(state->lines[line] + offset,
                                       length - offset, &consumed, &scalar) ||
                !scalar_printable(scalar))
                return REIST_GUI_TEXT_EDITOR_EINVAL;
            offset += consumed;
            ++columns;
        }
        if (line == state->cursor_line && state->cursor_column > columns)
            return REIST_GUI_TEXT_EDITOR_EINVAL;
    }
    return REIST_GUI_TEXT_EDITOR_OK;
}

int reist_gui_text_editor_configure(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    reist_gui_text_editor_result_t *result) {
    if (!model_valid(model) || state == NULL ||
        state->version != REIST_GUI_TEXT_EDITOR_API_VERSION ||
        state->struct_size != sizeof(*state) || state->configured != 0U ||
        !words_zero(state->reserved, 4U) || !result_valid(result))
        return REIST_GUI_TEXT_EDITOR_EINVAL;
    reist_gui_text_editor_state_initialize(state);
    state->configured = 1U;
    state->line_count = 1U;
    publish_state(model, state, result);
    request_full(model, result);
    return REIST_GUI_TEXT_EDITOR_OK;
}

static int validate_input_text(const char *text, size_t length) {
    if (text == NULL && length != 0U) return REIST_GUI_TEXT_EDITOR_EINVAL;
    uint32_t lines = 1U;
    uint32_t line_bytes = 0U;
    for (size_t index = 0U; index < length;) {
        uint8_t value = (uint8_t)text[index];
        if (value == '\r' || value == '\n') {
            if (value == '\r' && index + 1U < length &&
                text[index + 1U] == '\n') ++index;
            if (lines == REIST_GUI_TEXT_EDITOR_MAX_LINES)
                return REIST_GUI_TEXT_EDITOR_ECAPACITY;
            ++lines;
            line_bytes = 0U;
            ++index;
        } else {
            size_t consumed = 0U;
            uint32_t scalar = 0U;
            if (!reist_utf8_decode_one(text + index, length - index,
                                       &consumed, &scalar) ||
                !scalar_printable(scalar))
                return REIST_GUI_TEXT_EDITOR_EINVAL;
            if (consumed >= REIST_GUI_TEXT_EDITOR_LINE_CAPACITY - line_bytes)
                return REIST_GUI_TEXT_EDITOR_ECAPACITY;
            line_bytes += (uint32_t)consumed;
            index += consumed;
        }
    }
    return REIST_GUI_TEXT_EDITOR_OK;
}

int reist_gui_text_editor_set_text(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    const char *text, size_t length,
    reist_gui_text_editor_result_t *result) {
    if (reist_gui_text_editor_validate(model, state) != 0 ||
        !state->configured || !result_valid(result))
        return REIST_GUI_TEXT_EDITOR_EINVAL;
    int validation = validate_input_text(text, length);
    if (validation != REIST_GUI_TEXT_EDITOR_OK) return validation;

    uint32_t focused = state->focused;
    reist_gui_text_editor_state_initialize(state);
    state->configured = 1U;
    state->focused = focused;
    state->line_count = 1U;
    uint32_t line = 0U;
    uint32_t column = 0U;
    for (size_t index = 0U; index < length; ++index) {
        uint8_t value = (uint8_t)text[index];
        if (value == '\r' || value == '\n') {
            if (value == '\r' && index + 1U < length &&
                text[index + 1U] == '\n') ++index;
            state->lines[line][column] = '\0';
            ++line;
            ++state->line_count;
            column = 0U;
        } else {
            state->lines[line][column++] = (char)value;
        }
    }
    state->lines[line][column] = '\0';
    publish_state(model, state, result);
    result->changed = 1U;
    request_full(model, result);
    return REIST_GUI_TEXT_EDITOR_OK;
}

int reist_gui_text_editor_get_text(
    const reist_gui_text_editor_model_t *model,
    const reist_gui_text_editor_state_t *state,
    char *text, size_t capacity, size_t *length_out) {
    if (reist_gui_text_editor_validate(model, state) != 0 ||
        !state->configured || text == NULL || length_out == NULL)
        return REIST_GUI_TEXT_EDITOR_EINVAL;
    size_t required = state->line_count - 1U;
    for (uint32_t line = 0U; line < state->line_count; ++line)
        required += line_length(state->lines[line]);
    if (required >= capacity) return REIST_GUI_TEXT_EDITOR_ECAPACITY;
    size_t offset = 0U;
    for (uint32_t line = 0U; line < state->line_count; ++line) {
        uint32_t length = line_length(state->lines[line]);
        for (uint32_t column = 0U; column < length; ++column)
            text[offset++] = state->lines[line][column];
        if (line + 1U < state->line_count) text[offset++] = '\n';
    }
    text[offset] = '\0';
    *length_out = offset;
    return REIST_GUI_TEXT_EDITOR_OK;
}

int reist_gui_text_editor_mark_saved(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    reist_gui_text_editor_result_t *result) {
    if (reist_gui_text_editor_validate(model, state) != 0 ||
        !state->configured || !result_valid(result))
        return REIST_GUI_TEXT_EDITOR_EINVAL;
    publish_state(model, state, result);
    if (state->modified) {
        state->modified = 0U;
        result->changed = 1U;
        request_full(model, result);
    }
    publish_state(model, state, result);
    return REIST_GUI_TEXT_EDITOR_OK;
}

static uint32_t visible_rows(const reist_gui_text_editor_model_t *model) {
    uint32_t rows = model->bounds.height / model->glyph_height;
    return rows == 0U ? 1U : rows;
}

static uint32_t visible_columns(const reist_gui_text_editor_model_t *model) {
    uint32_t columns = model->bounds.width / model->glyph_width;
    return columns == 0U ? 1U : columns;
}

static uint32_t keep_cursor_visible(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state) {
    uint32_t old_line = state->first_line;
    uint32_t old_column = state->first_column;
    uint32_t rows = visible_rows(model);
    uint32_t columns = visible_columns(model);
    if (state->cursor_line < state->first_line)
        state->first_line = state->cursor_line;
    else if (state->cursor_line >= state->first_line + rows)
        state->first_line = state->cursor_line - rows + 1U;
    if (state->cursor_column < state->first_column)
        state->first_column = state->cursor_column;
    else if (state->cursor_column >= state->first_column + columns)
        state->first_column = state->cursor_column - columns + 1U;
    return old_line != state->first_line ||
           old_column != state->first_column;
}

static void insert_character(reist_gui_text_editor_state_t *state,
                             const char *value, uint32_t value_bytes) {
    char *line = state->lines[state->cursor_line];
    uint32_t length = line_length(line);
    uint32_t offset = line_byte_offset(line, state->cursor_column);
    for (uint32_t index = length + 1U; index > offset; --index)
        line[index + value_bytes - 1U] = line[index - 1U];
    for (uint32_t index = 0U; index < value_bytes; ++index)
        line[offset + index] = value[index];
    ++state->cursor_column;
    state->preferred_column = state->cursor_column;
    state->modified = 1U;
}

static uint32_t insert_newline(reist_gui_text_editor_state_t *state) {
    if (state->line_count == REIST_GUI_TEXT_EDITOR_MAX_LINES) return 0U;
    uint32_t split = line_byte_offset(
        state->lines[state->cursor_line], state->cursor_column);
    if (split == UINT32_MAX) return 0U;
    for (uint32_t index = state->line_count;
         index > state->cursor_line + 1U; --index)
        copy_line(state->lines[index], state->lines[index - 1U]);
    copy_line(state->lines[state->cursor_line + 1U],
              state->lines[state->cursor_line] + split);
    state->lines[state->cursor_line][split] = '\0';
    ++state->line_count;
    ++state->cursor_line;
    state->cursor_column = 0U;
    state->preferred_column = 0U;
    state->modified = 1U;
    return 1U;
}

static uint32_t delete_at_cursor(reist_gui_text_editor_state_t *state) {
    char *line = state->lines[state->cursor_line];
    uint32_t length = line_length(line);
    uint32_t columns = line_scalar_count(line);
    if (state->cursor_column < columns) {
        uint32_t offset = line_byte_offset(line, state->cursor_column);
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (offset == UINT32_MAX ||
            !reist_utf8_decode_one(line + offset, length - offset,
                                   &consumed, &scalar)) return 0U;
        for (uint32_t index = offset;
             index + consumed <= length; ++index)
            line[index] = line[index + consumed];
    } else if (state->cursor_line + 1U < state->line_count) {
        uint32_t next_length =
            line_length(state->lines[state->cursor_line + 1U]);
        if (length + next_length >= REIST_GUI_TEXT_EDITOR_LINE_CAPACITY)
            return 0U;
        for (uint32_t index = 0U; index <= next_length; ++index)
            line[length + index] =
                state->lines[state->cursor_line + 1U][index];
        for (uint32_t index = state->cursor_line + 1U;
             index + 1U < state->line_count; ++index)
            copy_line(state->lines[index], state->lines[index + 1U]);
        --state->line_count;
        clear_line(state->lines[state->line_count]);
    } else return 0U;
    state->modified = 1U;
    return 1U;
}

static uint32_t backspace(reist_gui_text_editor_state_t *state) {
    if (state->cursor_column != 0U) {
        --state->cursor_column;
        state->preferred_column = state->cursor_column;
        return delete_at_cursor(state);
    }
    if (state->cursor_line == 0U) return 0U;
    uint32_t previous = line_length(state->lines[state->cursor_line - 1U]);
    uint32_t previous_columns =
        line_scalar_count(state->lines[state->cursor_line - 1U]);
    uint32_t current = line_length(state->lines[state->cursor_line]);
    if (previous + current >= REIST_GUI_TEXT_EDITOR_LINE_CAPACITY) return 0U;
    --state->cursor_line;
    state->cursor_column = previous_columns;
    state->preferred_column = previous_columns;
    return delete_at_cursor(state);
}

static void move_cursor(reist_gui_text_editor_state_t *state,
                        uint32_t key, uint32_t page_rows) {
    uint32_t length = line_scalar_count(state->lines[state->cursor_line]);
    if (key == REIST_GUI_TEXT_EDITOR_KEY_LEFT) {
        if (state->cursor_column != 0U) --state->cursor_column;
        else if (state->cursor_line != 0U) {
            --state->cursor_line;
            state->cursor_column =
                line_scalar_count(state->lines[state->cursor_line]);
        }
        state->preferred_column = state->cursor_column;
    } else if (key == REIST_GUI_TEXT_EDITOR_KEY_RIGHT) {
        if (state->cursor_column < length) ++state->cursor_column;
        else if (state->cursor_line + 1U < state->line_count) {
            ++state->cursor_line;
            state->cursor_column = 0U;
        }
        state->preferred_column = state->cursor_column;
    } else if (key == REIST_GUI_TEXT_EDITOR_KEY_UP ||
               key == REIST_GUI_TEXT_EDITOR_KEY_DOWN ||
               key == REIST_GUI_TEXT_EDITOR_KEY_PAGE_UP ||
               key == REIST_GUI_TEXT_EDITOR_KEY_PAGE_DOWN) {
        uint32_t amount = (key == REIST_GUI_TEXT_EDITOR_KEY_PAGE_UP ||
                           key == REIST_GUI_TEXT_EDITOR_KEY_PAGE_DOWN)
            ? page_rows : 1U;
        if (key == REIST_GUI_TEXT_EDITOR_KEY_UP ||
            key == REIST_GUI_TEXT_EDITOR_KEY_PAGE_UP)
            state->cursor_line = state->cursor_line > amount
                ? state->cursor_line - amount : 0U;
        else {
            uint64_t next = (uint64_t)state->cursor_line + amount;
            state->cursor_line = next >= state->line_count
                ? state->line_count - 1U : (uint32_t)next;
        }
        length = line_scalar_count(state->lines[state->cursor_line]);
        state->cursor_column = state->preferred_column < length
            ? state->preferred_column : length;
    } else if (key == REIST_GUI_TEXT_EDITOR_KEY_HOME) {
        state->cursor_column = 0U;
        state->preferred_column = 0U;
    } else if (key == REIST_GUI_TEXT_EDITOR_KEY_END) {
        state->cursor_column = length;
        state->preferred_column = length;
    } else if (key == REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_HOME) {
        state->cursor_line = 0U;
        state->cursor_column = 0U;
        state->preferred_column = 0U;
    } else if (key == REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_END) {
        state->cursor_line = state->line_count - 1U;
        state->cursor_column =
            line_scalar_count(state->lines[state->cursor_line]);
        state->preferred_column = state->cursor_column;
    }
}

static void place_cursor(const reist_gui_text_editor_model_t *model,
                         reist_gui_text_editor_state_t *state,
                         int32_t x, int32_t y) {
    uint32_t row = (uint32_t)(y - model->bounds.y) / model->glyph_height;
    uint32_t column =
        (uint32_t)(x - model->bounds.x) / model->glyph_width;
    uint64_t line = (uint64_t)state->first_line + row;
    state->cursor_line = line >= state->line_count
        ? state->line_count - 1U : (uint32_t)line;
    uint64_t requested = (uint64_t)state->first_column + column;
    uint32_t length = line_scalar_count(state->lines[state->cursor_line]);
    state->cursor_column = requested > length ? length : (uint32_t)requested;
    state->preferred_column = state->cursor_column;
}

int reist_gui_text_editor_dispatch(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    const reist_gui_text_editor_event_t *event,
    reist_gui_text_editor_result_t *result) {
    if (reist_gui_text_editor_validate(model, state) != 0 ||
        !state->configured || !event_valid(event) || !result_valid(result))
        return REIST_GUI_TEXT_EDITOR_EINVAL;
    publish_state(model, state, result);

    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_CANCEL) {
        if (state->captured) {
            state->captured = 0U;
            result->consumed = 1U;
            request_full(model, result);
        }
        publish_state(model, state, result);
        return REIST_GUI_TEXT_EDITOR_OK;
    }
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_FOCUS) {
        if (event->focused && !interactive(model->flags))
            return REIST_GUI_TEXT_EDITOR_EINVAL;
        if (state->focused != event->focused) {
            state->focused = event->focused;
            if (!state->focused) state->captured = 0U;
            result->focus_changed = 1U;
            request_full(model, result);
        }
        publish_state(model, state, result);
        return REIST_GUI_TEXT_EDITOR_OK;
    }
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_POINTER_MOTION) {
        result->consumed = state->captured ||
            point_inside(model->bounds, event->x, event->y);
        return REIST_GUI_TEXT_EDITOR_OK;
    }
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_POINTER_BUTTON) {
        if (!interactive(model->flags)) return REIST_GUI_TEXT_EDITOR_OK;
        if (event->pressed) {
            if (!point_inside(model->bounds, event->x, event->y))
                return REIST_GUI_TEXT_EDITOR_OK;
            state->focused = 1U;
            state->captured = 1U;
            place_cursor(model, state, event->x, event->y);
            result->focus_changed = 1U;
            result->consumed = 1U;
            request_full(model, result);
        } else if (state->captured) {
            state->captured = 0U;
            result->consumed = 1U;
            request_full(model, result);
        }
        publish_state(model, state, result);
        return REIST_GUI_TEXT_EDITOR_OK;
    }
    if (!state->focused) return REIST_GUI_TEXT_EDITOR_OK;

    result->consumed = 1U;
    uint32_t old_line = state->cursor_line;
    uint32_t old_column = state->cursor_column;
    uint32_t changed = 0U;
    if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_TEXT) {
        char encoded[4];
        uint32_t encoded_bytes = encode_scalar(event->codepoint, encoded);
        uint32_t current_bytes =
            line_length(state->lines[state->cursor_line]);
        if ((model->flags & REIST_GUI_TEXT_EDITOR_READ_ONLY) == 0U &&
            encoded_bytes != 0U &&
            encoded_bytes < REIST_GUI_TEXT_EDITOR_LINE_CAPACITY -
                                current_bytes) {
            insert_character(state, encoded, encoded_bytes);
            changed = 1U;
        }
    } else if (event->type == REIST_GUI_TEXT_EDITOR_EVENT_KEYBOARD) {
        if (event->key <= REIST_GUI_TEXT_EDITOR_KEY_PAGE_DOWN ||
            event->key == REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_HOME ||
            event->key == REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_END) {
            move_cursor(state, event->key, visible_rows(model));
        } else if ((model->flags & REIST_GUI_TEXT_EDITOR_READ_ONLY) == 0U &&
                   event->key == REIST_GUI_TEXT_EDITOR_KEY_BACKSPACE) {
            changed = backspace(state);
        } else if ((model->flags & REIST_GUI_TEXT_EDITOR_READ_ONLY) == 0U &&
                   event->key == REIST_GUI_TEXT_EDITOR_KEY_DELETE) {
            changed = delete_at_cursor(state);
        } else if ((model->flags & REIST_GUI_TEXT_EDITOR_READ_ONLY) == 0U &&
                   event->key == REIST_GUI_TEXT_EDITOR_KEY_ENTER) {
            changed = insert_newline(state);
        } else result->consumed = 0U;
    } else result->consumed = 0U;

    uint32_t view_changed = keep_cursor_visible(model, state);
    if (result->consumed &&
        (changed || old_line != state->cursor_line ||
         old_column != state->cursor_column || view_changed))
        request_full(model, result);
    if (changed) {
        result->changed = 1U;
        state->modified = 1U;
    }
    publish_state(model, state, result);
    return REIST_GUI_TEXT_EDITOR_OK;
}
