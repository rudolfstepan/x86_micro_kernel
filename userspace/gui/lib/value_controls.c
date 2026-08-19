#include "reist/gui/value_controls.h"

static void clear_bytes(void *value, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static uint32_t words_zero(const uint32_t *words, uint32_t count) {
    for (uint32_t index = 0U; index < count; ++index)
        if (words[index] != 0U) return 0U;
    return 1U;
}

static uint32_t string_length(const char *text, uint32_t limit,
                              uint32_t *length_out) {
    if (text == NULL || length_out == NULL) return 0U;
    for (uint32_t length = 0U; length < limit; ++length) {
        if (text[length] == '\0') {
            *length_out = length;
            return 1U;
        }
    }
    return 0U;
}

static uint32_t name_valid(const char *name) {
    uint32_t length = 0U;
    return string_length(name, REIST_GUI_VALUE_NAME_LIMIT, &length) &&
           length != 0U;
}

static uint32_t rect_valid(reist_gui_rect_t rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.width != 0U &&
           rect.height != 0U &&
           (uint64_t)(uint32_t)rect.x + rect.width <= INT32_MAX &&
           (uint64_t)(uint32_t)rect.y + rect.height <= INT32_MAX;
}

static uint32_t point_inside(reist_gui_rect_t rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y &&
           (uint64_t)(uint32_t)(x - rect.x) < rect.width &&
           (uint64_t)(uint32_t)(y - rect.y) < rect.height;
}

/* Freestanding i386 has no implicit 64-bit division runtime. This fixed
 * 64-step long division keeps range interpolation self-contained. */
static uint32_t multiply_divide_u32(uint32_t left, uint32_t right,
                                    uint32_t divisor) {
    uint64_t product = (uint64_t)left * right;
    uint64_t quotient = 0U;
    uint64_t remainder = 0U;
    if (divisor == 0U) return UINT32_MAX;
    for (uint32_t remaining = 64U; remaining != 0U; --remaining) {
        uint32_t bit = remaining - 1U;
        remainder = (remainder << 1U) | ((product >> bit) & 1U);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (uint64_t)1U << bit;
        }
    }
    return quotient > UINT32_MAX ? UINT32_MAX : (uint32_t)quotient;
}

static uint32_t flags_valid(uint32_t flags) {
    return (flags & ~(REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED |
                      REIST_GUI_VALUE_READ_ONLY)) == 0U;
}

static uint32_t interactive(uint32_t flags) {
    return (flags & (REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED)) ==
           (REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED);
}

static uint32_t result_valid(const reist_gui_value_result_t *result) {
    return result != NULL && result->version == REIST_GUI_VALUE_API_VERSION &&
           result->struct_size == sizeof(*result) &&
           result->damage_count <= REIST_GUI_VALUE_DAMAGE_CAPACITY &&
           result->full_redraw <= 1U && words_zero(result->reserved, 4U);
}

static void damage(reist_gui_value_result_t *result, reist_gui_rect_t rect) {
    if (result->full_redraw) return;
    if (result->damage_count == REIST_GUI_VALUE_DAMAGE_CAPACITY) {
        result->damage_count = 0U;
        result->full_redraw = 1U;
        return;
    }
    result->damage[result->damage_count++] = rect;
}

void reist_gui_value_event_initialize(reist_gui_value_event_t *event) {
    if (event == NULL) return;
    clear_bytes(event, sizeof(*event));
    event->version = REIST_GUI_VALUE_API_VERSION;
    event->struct_size = sizeof(*event);
}

void reist_gui_value_result_initialize(reist_gui_value_result_t *result) {
    if (result == NULL) return;
    clear_bytes(result, sizeof(*result));
    result->version = REIST_GUI_VALUE_API_VERSION;
    result->struct_size = sizeof(*result);
}

static uint32_t event_valid(const reist_gui_value_event_t *event) {
    if (event == NULL || event->version != REIST_GUI_VALUE_API_VERSION ||
        event->struct_size != sizeof(*event) ||
        !words_zero(event->reserved, 4U)) return 0U;
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_MOTION)
        return event->button == 0U && !event->pressed && !event->key &&
               !event->codepoint && !event->focused;
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_BUTTON)
        return event->button == REIST_GUI_VALUE_BUTTON_LEFT &&
               event->pressed <= 1U && !event->key && !event->codepoint &&
               !event->focused;
    if (event->type == REIST_GUI_VALUE_EVENT_KEYBOARD)
        return !event->button && !event->pressed && !event->codepoint &&
               !event->focused && event->key >= REIST_GUI_VALUE_KEY_LEFT &&
               event->key <= REIST_GUI_VALUE_KEY_ENTER;
    if (event->type == REIST_GUI_VALUE_EVENT_TEXT)
        return !event->button && !event->pressed && !event->key &&
               !event->focused && event->codepoint >= 0x20U &&
               event->codepoint <= 0x7EU;
    if (event->type == REIST_GUI_VALUE_EVENT_FOCUS)
        return !event->button && !event->pressed && !event->key &&
               !event->codepoint && event->focused <= 1U;
    if (event->type == REIST_GUI_VALUE_EVENT_CANCEL)
        return !event->button && !event->pressed && !event->key &&
               !event->codepoint && !event->focused;
    return 0U;
}

static uint32_t text_model_valid(const reist_gui_text_model_t *model) {
    return model != NULL && model->version == REIST_GUI_VALUE_API_VERSION &&
           model->struct_size == sizeof(*model) &&
           model->id != REIST_GUI_VALUE_NO_ID && name_valid(model->name) &&
           rect_valid(model->bounds) && model->capacity >= 2U &&
           model->capacity <= REIST_GUI_TEXT_CAPACITY &&
           model->glyph_width != 0U && flags_valid(model->flags) &&
           words_zero(model->reserved, 4U);
}

void reist_gui_text_state_initialize(reist_gui_text_state_t *state) {
    if (state == NULL) return;
    clear_bytes(state, sizeof(*state));
    state->version = REIST_GUI_VALUE_API_VERSION;
    state->struct_size = sizeof(*state);
}

int reist_gui_text_validate(const reist_gui_text_model_t *model,
                            const reist_gui_text_state_t *state) {
    if (!text_model_valid(model) || state == NULL ||
        state->version != REIST_GUI_VALUE_API_VERSION ||
        state->struct_size != sizeof(*state) || state->configured > 1U ||
        state->focused > 1U || state->captured > 1U ||
        !words_zero(state->reserved, 4U)) return REIST_GUI_VALUE_EINVAL;
    if (!state->configured)
        return !state->focused && !state->captured && !state->length &&
               !state->cursor && state->text[0] == '\0'
            ? REIST_GUI_VALUE_OK : REIST_GUI_VALUE_EINVAL;
    if (state->length >= model->capacity || state->cursor > state->length ||
        state->text[state->length] != '\0') return REIST_GUI_VALUE_EINVAL;
    for (uint32_t index = 0U; index < state->length; ++index)
        if ((uint8_t)state->text[index] < 0x20U ||
            (uint8_t)state->text[index] > 0x7EU)
            return REIST_GUI_VALUE_EINVAL;
    if ((!interactive(model->flags) && (state->focused || state->captured)) ||
        (!state->focused && state->captured)) return REIST_GUI_VALUE_EINVAL;
    return REIST_GUI_VALUE_OK;
}

int reist_gui_text_configure(const reist_gui_text_model_t *model,
                             reist_gui_text_state_t *state,
                             const char *initial_text,
                             reist_gui_value_result_t *result) {
    uint32_t length = 0U;
    if (!text_model_valid(model) || state == NULL ||
        state->version != REIST_GUI_VALUE_API_VERSION ||
        state->struct_size != sizeof(*state) ||
        !words_zero(state->reserved, 4U) || !result_valid(result) ||
        !string_length(initial_text, model->capacity, &length))
        return REIST_GUI_VALUE_EINVAL;
    for (uint32_t index = 0U; index < length; ++index)
        if ((uint8_t)initial_text[index] < 0x20U ||
            (uint8_t)initial_text[index] > 0x7EU)
            return REIST_GUI_VALUE_EINVAL;
    reist_gui_text_state_initialize(state);
    for (uint32_t index = 0U; index <= length; ++index)
        state->text[index] = initial_text[index];
    state->configured = 1U;
    state->length = length;
    state->cursor = length;
    result->control_id = model->id;
    result->cursor = length;
    damage(result, model->bounds);
    return REIST_GUI_VALUE_OK;
}

static void text_cursor_from_pointer(const reist_gui_text_model_t *model,
                                     reist_gui_text_state_t *state,
                                     int32_t x) {
    uint32_t relative = x <= model->bounds.x
        ? 0U : (uint32_t)(x - model->bounds.x);
    uint32_t cursor = relative / model->glyph_width;
    state->cursor = cursor < state->length ? cursor : state->length;
}

static void text_changed(const reist_gui_text_model_t *model,
                         const reist_gui_text_state_t *state,
                         reist_gui_value_result_t *result) {
    result->changed = 1U;
    result->control_id = model->id;
    result->cursor = state->cursor;
    damage(result, model->bounds);
}

int reist_gui_text_dispatch(const reist_gui_text_model_t *model,
                            reist_gui_text_state_t *state,
                            const reist_gui_value_event_t *event,
                            reist_gui_value_result_t *result) {
    if (reist_gui_text_validate(model, state) != 0 || !state->configured ||
        !event_valid(event) || !result_valid(result))
        return REIST_GUI_VALUE_EINVAL;
    if (event->type == REIST_GUI_VALUE_EVENT_CANCEL) {
        if (state->captured) {
            state->captured = 0U;
            result->consumed = 1U;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_FOCUS) {
        if (!interactive(model->flags) && event->focused)
            return REIST_GUI_VALUE_EINVAL;
        if (state->focused != event->focused) {
            state->focused = event->focused;
            if (!state->focused) state->captured = 0U;
            result->focus_changed = 1U;
            result->control_id = model->id;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_MOTION) {
        result->consumed = state->captured ||
            point_inside(model->bounds, event->x, event->y);
        if (state->captured) {
            text_cursor_from_pointer(model, state, event->x);
            result->cursor = state->cursor;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_BUTTON) {
        if (!interactive(model->flags)) return REIST_GUI_VALUE_OK;
        if (event->pressed) {
            if (!point_inside(model->bounds, event->x, event->y))
                return REIST_GUI_VALUE_OK;
            state->focused = 1U;
            state->captured = 1U;
            text_cursor_from_pointer(model, state, event->x);
            result->focus_changed = 1U;
            result->cursor = state->cursor;
            result->control_id = model->id;
            result->consumed = 1U;
            damage(result, model->bounds);
        } else if (state->captured) {
            state->captured = 0U;
            text_cursor_from_pointer(model, state, event->x);
            result->cursor = state->cursor;
            result->consumed = 1U;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (!state->focused) return REIST_GUI_VALUE_OK;
    result->consumed = 1U;
    if (event->type == REIST_GUI_VALUE_EVENT_TEXT) {
        if ((model->flags & REIST_GUI_VALUE_READ_ONLY) != 0U ||
            state->length + 1U >= model->capacity) return REIST_GUI_VALUE_OK;
        for (uint32_t index = state->length; index > state->cursor; --index)
            state->text[index] = state->text[index - 1U];
        state->text[state->cursor++] = (char)event->codepoint;
        state->text[++state->length] = '\0';
        text_changed(model, state, result);
        return REIST_GUI_VALUE_OK;
    }
    if (event->key == REIST_GUI_VALUE_KEY_LEFT && state->cursor != 0U)
        --state->cursor;
    else if (event->key == REIST_GUI_VALUE_KEY_RIGHT &&
             state->cursor < state->length) ++state->cursor;
    else if (event->key == REIST_GUI_VALUE_KEY_HOME) state->cursor = 0U;
    else if (event->key == REIST_GUI_VALUE_KEY_END)
        state->cursor = state->length;
    else if ((model->flags & REIST_GUI_VALUE_READ_ONLY) == 0U &&
             event->key == REIST_GUI_VALUE_KEY_BACKSPACE &&
             state->cursor != 0U) {
        for (uint32_t index = state->cursor - 1U; index < state->length; ++index)
            state->text[index] = state->text[index + 1U];
        --state->cursor;
        --state->length;
        text_changed(model, state, result);
        return REIST_GUI_VALUE_OK;
    } else if ((model->flags & REIST_GUI_VALUE_READ_ONLY) == 0U &&
               event->key == REIST_GUI_VALUE_KEY_DELETE &&
               state->cursor < state->length) {
        for (uint32_t index = state->cursor; index < state->length; ++index)
            state->text[index] = state->text[index + 1U];
        --state->length;
        text_changed(model, state, result);
        return REIST_GUI_VALUE_OK;
    } else if (event->key == REIST_GUI_VALUE_KEY_ENTER) {
        result->activated = 1U;
        result->control_id = model->id;
        return REIST_GUI_VALUE_OK;
    } else {
        result->consumed = 0U;
        return REIST_GUI_VALUE_OK;
    }
    result->cursor = state->cursor;
    damage(result, model->bounds);
    return REIST_GUI_VALUE_OK;
}

static uint32_t list_item_selectable(const reist_gui_list_item_t *item) {
    return interactive(item->flags);
}

static uint32_t list_model_valid(const reist_gui_list_model_t *model) {
    if (model == NULL || model->version != REIST_GUI_VALUE_API_VERSION ||
        model->struct_size != sizeof(*model) ||
        model->id == REIST_GUI_VALUE_NO_ID || !name_valid(model->name) ||
        model->items == NULL || model->item_count == 0U ||
        model->item_count > REIST_GUI_LIST_CAPACITY ||
        !rect_valid(model->bounds) || model->row_height == 0U ||
        model->row_height > model->bounds.height ||
        !flags_valid(model->flags) || !words_zero(model->reserved, 4U))
        return 0U;
    uint32_t selectable_count = 0U;
    for (uint32_t index = 0U; index < model->item_count; ++index) {
        const reist_gui_list_item_t *item = &model->items[index];
        if (item->id == REIST_GUI_VALUE_NO_ID || !name_valid(item->label) ||
            !flags_valid(item->flags) || !words_zero(item->reserved, 2U))
            return 0U;
        for (uint32_t other = 0U; other < index; ++other)
            if (model->items[other].id == item->id) return 0U;
        if (list_item_selectable(item)) ++selectable_count;
    }
    return selectable_count != 0U;
}

void reist_gui_list_state_initialize(reist_gui_list_state_t *state) {
    if (state == NULL) return;
    clear_bytes(state, sizeof(*state));
    state->version = REIST_GUI_VALUE_API_VERSION;
    state->struct_size = sizeof(*state);
    state->selected = REIST_GUI_VALUE_NO_INDEX;
    state->captured = REIST_GUI_VALUE_NO_INDEX;
}

int reist_gui_list_validate(const reist_gui_list_model_t *model,
                            const reist_gui_list_state_t *state) {
    if (!list_model_valid(model) || state == NULL ||
        state->version != REIST_GUI_VALUE_API_VERSION ||
        state->struct_size != sizeof(*state) || state->configured > 1U ||
        state->focused > 1U || state->armed > 1U ||
        !words_zero(state->reserved, 4U)) return REIST_GUI_VALUE_EINVAL;
    if (!state->configured)
        return !state->focused && state->selected == REIST_GUI_VALUE_NO_INDEX &&
               state->top_index == 0U &&
               state->captured == REIST_GUI_VALUE_NO_INDEX && !state->armed
            ? REIST_GUI_VALUE_OK : REIST_GUI_VALUE_EINVAL;
    if (state->selected >= model->item_count ||
        !list_item_selectable(&model->items[state->selected]) ||
        state->top_index >= model->item_count ||
        (state->captured != REIST_GUI_VALUE_NO_INDEX &&
         (state->captured >= model->item_count ||
          !list_item_selectable(&model->items[state->captured]))) ||
        (state->captured == REIST_GUI_VALUE_NO_INDEX && state->armed) ||
        (!interactive(model->flags) &&
         (state->focused || state->captured != REIST_GUI_VALUE_NO_INDEX)))
        return REIST_GUI_VALUE_EINVAL;
    return REIST_GUI_VALUE_OK;
}

static uint32_t list_index(const reist_gui_list_model_t *model, uint32_t id) {
    for (uint32_t index = 0U; index < model->item_count; ++index)
        if (model->items[index].id == id) return index;
    return REIST_GUI_VALUE_NO_INDEX;
}

static uint32_t list_first(const reist_gui_list_model_t *model,
                           uint32_t reverse) {
    for (uint32_t step = 0U; step < model->item_count; ++step) {
        uint32_t index = reverse ? model->item_count - 1U - step : step;
        if (list_item_selectable(&model->items[index])) return index;
    }
    return REIST_GUI_VALUE_NO_INDEX;
}

static uint32_t visible_rows(const reist_gui_list_model_t *model) {
    uint32_t rows = model->bounds.height / model->row_height;
    return rows == 0U ? 1U : rows;
}

static void reveal(const reist_gui_list_model_t *model,
                   reist_gui_list_state_t *state) {
    uint32_t rows = visible_rows(model);
    if (state->selected < state->top_index) state->top_index = state->selected;
    else if (state->selected >= state->top_index + rows)
        state->top_index = state->selected - rows + 1U;
}

static void list_select(const reist_gui_list_model_t *model,
                        reist_gui_list_state_t *state,
                        reist_gui_value_result_t *result, uint32_t index) {
    if (state->selected == index) return;
    state->selected = index;
    reveal(model, state);
    result->changed = 1U;
    result->control_id = model->id;
    result->selected_id = model->items[index].id;
    damage(result, model->bounds);
}

int reist_gui_list_configure(const reist_gui_list_model_t *model,
                             reist_gui_list_state_t *state,
                             uint32_t initial_item_id,
                             reist_gui_value_result_t *result) {
    if (!list_model_valid(model) || state == NULL ||
        state->version != REIST_GUI_VALUE_API_VERSION ||
        state->struct_size != sizeof(*state) ||
        !words_zero(state->reserved, 4U) || !result_valid(result))
        return REIST_GUI_VALUE_EINVAL;
    uint32_t selected = initial_item_id == REIST_GUI_VALUE_NO_ID
        ? list_first(model, 0U) : list_index(model, initial_item_id);
    if (selected == REIST_GUI_VALUE_NO_INDEX ||
        !list_item_selectable(&model->items[selected]))
        return REIST_GUI_VALUE_EINVAL;
    reist_gui_list_state_initialize(state);
    state->configured = 1U;
    state->selected = selected;
    result->control_id = model->id;
    result->selected_id = model->items[selected].id;
    damage(result, model->bounds);
    return REIST_GUI_VALUE_OK;
}

static uint32_t list_hit(const reist_gui_list_model_t *model,
                         const reist_gui_list_state_t *state,
                         int32_t x, int32_t y) {
    if (!point_inside(model->bounds, x, y)) return REIST_GUI_VALUE_NO_INDEX;
    uint32_t row = (uint32_t)(y - model->bounds.y) / model->row_height;
    uint32_t index = state->top_index + row;
    return index < model->item_count && list_item_selectable(&model->items[index])
        ? index : REIST_GUI_VALUE_NO_INDEX;
}

static uint32_t list_next(const reist_gui_list_model_t *model,
                          uint32_t current, uint32_t reverse) {
    for (uint32_t step = 1U; step <= model->item_count; ++step) {
        uint32_t index = reverse
            ? (current + model->item_count - (step % model->item_count)) %
                model->item_count
            : (current + step) % model->item_count;
        if (list_item_selectable(&model->items[index])) return index;
    }
    return current;
}

int reist_gui_list_dispatch(const reist_gui_list_model_t *model,
                            reist_gui_list_state_t *state,
                            const reist_gui_value_event_t *event,
                            reist_gui_value_result_t *result) {
    if (reist_gui_list_validate(model, state) != 0 || !state->configured ||
        !event_valid(event) || !result_valid(result))
        return REIST_GUI_VALUE_EINVAL;
    if (event->type == REIST_GUI_VALUE_EVENT_CANCEL) {
        if (state->captured != REIST_GUI_VALUE_NO_INDEX) {
            state->captured = REIST_GUI_VALUE_NO_INDEX;
            state->armed = 0U;
            result->consumed = 1U;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_FOCUS) {
        if (!interactive(model->flags) && event->focused)
            return REIST_GUI_VALUE_EINVAL;
        if (state->focused != event->focused) {
            state->focused = event->focused;
            if (!state->focused) {
                state->captured = REIST_GUI_VALUE_NO_INDEX;
                state->armed = 0U;
            }
            result->focus_changed = 1U;
            result->control_id = model->id;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_MOTION) {
        uint32_t hit = list_hit(model, state, event->x, event->y);
        if (state->captured != REIST_GUI_VALUE_NO_INDEX) {
            uint32_t armed = hit == state->captured;
            if (state->armed != armed) {
                state->armed = armed;
                damage(result, model->bounds);
            }
            result->consumed = 1U;
        } else result->consumed = hit != REIST_GUI_VALUE_NO_INDEX;
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_BUTTON) {
        if (!interactive(model->flags)) return REIST_GUI_VALUE_OK;
        if (event->pressed) {
            uint32_t hit = list_hit(model, state, event->x, event->y);
            if (hit == REIST_GUI_VALUE_NO_INDEX) return REIST_GUI_VALUE_OK;
            state->focused = 1U;
            state->captured = hit;
            state->armed = 1U;
            result->focus_changed = 1U;
            result->control_id = model->id;
            result->consumed = 1U;
            damage(result, model->bounds);
        } else if (state->captured != REIST_GUI_VALUE_NO_INDEX) {
            uint32_t captured = state->captured;
            uint32_t hit = list_hit(model, state, event->x, event->y);
            state->captured = REIST_GUI_VALUE_NO_INDEX;
            state->armed = 0U;
            result->consumed = 1U;
            if (hit == captured) list_select(model, state, result, captured);
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (!state->focused || event->type != REIST_GUI_VALUE_EVENT_KEYBOARD)
        return REIST_GUI_VALUE_OK;
    result->consumed = 1U;
    uint32_t target = state->selected;
    if (event->key == REIST_GUI_VALUE_KEY_UP)
        target = list_next(model, target, 1U);
    else if (event->key == REIST_GUI_VALUE_KEY_DOWN)
        target = list_next(model, target, 0U);
    else if (event->key == REIST_GUI_VALUE_KEY_HOME)
        target = list_first(model, 0U);
    else if (event->key == REIST_GUI_VALUE_KEY_END)
        target = list_first(model, 1U);
    else if (event->key == REIST_GUI_VALUE_KEY_PAGE_UP ||
             event->key == REIST_GUI_VALUE_KEY_PAGE_DOWN) {
        uint32_t count = visible_rows(model);
        for (uint32_t step = 0U; step < count; ++step)
            target = list_next(model, target,
                event->key == REIST_GUI_VALUE_KEY_PAGE_UP);
    } else if (event->key == REIST_GUI_VALUE_KEY_ENTER) {
        result->activated = 1U;
        result->control_id = model->id;
        result->selected_id = model->items[state->selected].id;
        return REIST_GUI_VALUE_OK;
    } else {
        result->consumed = 0U;
        return REIST_GUI_VALUE_OK;
    }
    list_select(model, state, result, target);
    return REIST_GUI_VALUE_OK;
}

static uint32_t range_model_valid(const reist_gui_range_model_t *model) {
    if (model == NULL || model->version != REIST_GUI_VALUE_API_VERSION ||
        model->struct_size != sizeof(*model) ||
        model->id == REIST_GUI_VALUE_NO_ID || !name_valid(model->name) ||
        !rect_valid(model->bounds) || model->minimum >= model->maximum ||
        model->step == 0U || model->page_step == 0U ||
        model->role < REIST_GUI_RANGE_SLIDER ||
        model->role > REIST_GUI_RANGE_PROGRESS ||
        model->orientation < REIST_GUI_HORIZONTAL ||
        model->orientation > REIST_GUI_VERTICAL || !flags_valid(model->flags) ||
        !words_zero(model->reserved, 4U)) return 0U;
    if (model->role == REIST_GUI_RANGE_PROGRESS &&
        (model->flags & REIST_GUI_VALUE_READ_ONLY) == 0U) return 0U;
    return 1U;
}

void reist_gui_range_state_initialize(reist_gui_range_state_t *state) {
    if (state == NULL) return;
    clear_bytes(state, sizeof(*state));
    state->version = REIST_GUI_VALUE_API_VERSION;
    state->struct_size = sizeof(*state);
}

int reist_gui_range_validate(const reist_gui_range_model_t *model,
                             const reist_gui_range_state_t *state) {
    if (!range_model_valid(model) || state == NULL ||
        state->version != REIST_GUI_VALUE_API_VERSION ||
        state->struct_size != sizeof(*state) || state->configured > 1U ||
        state->focused > 1U || state->captured > 1U ||
        !words_zero(state->reserved, 4U)) return REIST_GUI_VALUE_EINVAL;
    if (!state->configured)
        return !state->focused && !state->captured && state->value == 0
            ? REIST_GUI_VALUE_OK : REIST_GUI_VALUE_EINVAL;
    if (state->value < model->minimum || state->value > model->maximum ||
        (!interactive(model->flags) && (state->focused || state->captured)) ||
        (model->role == REIST_GUI_RANGE_PROGRESS &&
         (state->focused || state->captured)) ||
        (!state->focused && state->captured)) return REIST_GUI_VALUE_EINVAL;
    return REIST_GUI_VALUE_OK;
}

int reist_gui_range_configure(const reist_gui_range_model_t *model,
                              reist_gui_range_state_t *state,
                              int32_t initial_value,
                              reist_gui_value_result_t *result) {
    if (!range_model_valid(model) || state == NULL ||
        state->version != REIST_GUI_VALUE_API_VERSION ||
        state->struct_size != sizeof(*state) ||
        !words_zero(state->reserved, 4U) || !result_valid(result) ||
        initial_value < model->minimum || initial_value > model->maximum)
        return REIST_GUI_VALUE_EINVAL;
    reist_gui_range_state_initialize(state);
    state->configured = 1U;
    state->value = initial_value;
    result->control_id = model->id;
    result->value = initial_value;
    damage(result, model->bounds);
    return REIST_GUI_VALUE_OK;
}

int reist_gui_range_set(const reist_gui_range_model_t *model,
                        reist_gui_range_state_t *state, int32_t value,
                        reist_gui_value_result_t *result) {
    if (reist_gui_range_validate(model, state) != 0 || !state->configured ||
        !result_valid(result) || value < model->minimum ||
        value > model->maximum) return REIST_GUI_VALUE_EINVAL;
    if (state->value != value) {
        state->value = value;
        result->changed = 1U;
        result->control_id = model->id;
        result->value = value;
        damage(result, model->bounds);
    }
    return REIST_GUI_VALUE_OK;
}

static int32_t range_from_pointer(const reist_gui_range_model_t *model,
                                  int32_t x, int32_t y) {
    uint32_t extent = model->orientation == REIST_GUI_HORIZONTAL
        ? model->bounds.width : model->bounds.height;
    int32_t origin = model->orientation == REIST_GUI_HORIZONTAL
        ? model->bounds.x : model->bounds.y;
    int32_t position = model->orientation == REIST_GUI_HORIZONTAL ? x : y;
    uint32_t coordinate = position <= origin
        ? 0U : (uint32_t)(position - origin);
    if (coordinate >= extent) coordinate = extent - 1U;
    uint32_t span = (uint32_t)((int64_t)model->maximum - model->minimum);
    uint32_t offset = multiply_divide_u32(
        span, coordinate, extent > 1U ? extent - 1U : 1U);
    return (int32_t)((int64_t)model->minimum + offset);
}

static int32_t range_step(const reist_gui_range_model_t *model,
                          int32_t value, uint32_t amount,
                          uint32_t decrement) {
    int64_t next = decrement ? (int64_t)value - amount :
                               (int64_t)value + amount;
    if (next < model->minimum) next = model->minimum;
    if (next > model->maximum) next = model->maximum;
    return (int32_t)next;
}

int reist_gui_range_dispatch(const reist_gui_range_model_t *model,
                             reist_gui_range_state_t *state,
                             const reist_gui_value_event_t *event,
                             reist_gui_value_result_t *result) {
    if (reist_gui_range_validate(model, state) != 0 || !state->configured ||
        !event_valid(event) || !result_valid(result))
        return REIST_GUI_VALUE_EINVAL;
    if (event->type == REIST_GUI_VALUE_EVENT_CANCEL) {
        if (state->captured) {
            state->captured = 0U;
            result->consumed = 1U;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_FOCUS) {
        if ((!interactive(model->flags) ||
             model->role == REIST_GUI_RANGE_PROGRESS) && event->focused)
            return REIST_GUI_VALUE_EINVAL;
        if (state->focused != event->focused) {
            state->focused = event->focused;
            if (!state->focused) state->captured = 0U;
            result->focus_changed = 1U;
            result->control_id = model->id;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (model->role == REIST_GUI_RANGE_PROGRESS ||
        !interactive(model->flags)) return REIST_GUI_VALUE_OK;
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_MOTION) {
        result->consumed = state->captured ||
            point_inside(model->bounds, event->x, event->y);
        if (state->captured) {
            int32_t value = range_from_pointer(model, event->x, event->y);
            (void)reist_gui_range_set(model, state, value, result);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (event->type == REIST_GUI_VALUE_EVENT_POINTER_BUTTON) {
        if (event->pressed) {
            if (!point_inside(model->bounds, event->x, event->y))
                return REIST_GUI_VALUE_OK;
            state->focused = 1U;
            state->captured = 1U;
            result->focus_changed = 1U;
            result->consumed = 1U;
            result->control_id = model->id;
            int32_t value = range_from_pointer(model, event->x, event->y);
            (void)reist_gui_range_set(model, state, value, result);
            damage(result, model->bounds);
        } else if (state->captured) {
            state->captured = 0U;
            result->consumed = 1U;
            damage(result, model->bounds);
        }
        return REIST_GUI_VALUE_OK;
    }
    if (!state->focused || event->type != REIST_GUI_VALUE_EVENT_KEYBOARD)
        return REIST_GUI_VALUE_OK;
    result->consumed = 1U;
    int32_t next = state->value;
    if (event->key == REIST_GUI_VALUE_KEY_LEFT ||
        event->key == REIST_GUI_VALUE_KEY_DOWN)
        next = range_step(model, next, model->step, 1U);
    else if (event->key == REIST_GUI_VALUE_KEY_RIGHT ||
             event->key == REIST_GUI_VALUE_KEY_UP)
        next = range_step(model, next, model->step, 0U);
    else if (event->key == REIST_GUI_VALUE_KEY_PAGE_UP)
        next = range_step(model, next, model->page_step, 0U);
    else if (event->key == REIST_GUI_VALUE_KEY_PAGE_DOWN)
        next = range_step(model, next, model->page_step, 1U);
    else if (event->key == REIST_GUI_VALUE_KEY_HOME) next = model->minimum;
    else if (event->key == REIST_GUI_VALUE_KEY_END) next = model->maximum;
    else if (event->key == REIST_GUI_VALUE_KEY_ENTER) {
        result->activated = 1U;
        result->control_id = model->id;
        result->value = state->value;
        return REIST_GUI_VALUE_OK;
    } else {
        result->consumed = 0U;
        return REIST_GUI_VALUE_OK;
    }
    return reist_gui_range_set(model, state, next, result);
}
