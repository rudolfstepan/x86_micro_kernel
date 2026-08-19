#include "reist/gui/tabs.h"

static void clear_bytes(void *value, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static uint32_t words_zero(const uint32_t *words, uint32_t count) {
    for (uint32_t index = 0U; index < count; ++index)
        if (words[index] != 0U) return 0U;
    return 1U;
}

static uint32_t text_valid(const char *value) {
    if (value == NULL) return 0U;
    for (uint32_t index = 0U; index < REIST_GUI_TABS_LABEL_LIMIT; ++index)
        if (value[index] == '\0') return index != 0U;
    return 0U;
}

static uint32_t rect_end_valid(reist_gui_rect_t rect) {
    if (rect.width == 0U || rect.height == 0U || rect.x < 0 || rect.y < 0)
        return 0U;
    return (uint64_t)(uint32_t)rect.x + rect.width <= INT32_MAX &&
           (uint64_t)(uint32_t)rect.y + rect.height <= INT32_MAX;
}

static uint32_t selectable(const reist_gui_tab_t *tab) {
    return (tab->flags & (REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED)) ==
           (REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED);
}

static int validate_model(const reist_gui_tabs_model_t *model) {
    if (model == NULL || model->version != REIST_GUI_TABS_API_VERSION ||
        model->struct_size != sizeof(*model) || model->tabs == NULL ||
        model->tab_count == 0U ||
        model->tab_count > REIST_GUI_TABS_CAPACITY ||
        model->damage_margin > 1024U ||
        !rect_end_valid(model->tab_bar) || !rect_end_valid(model->content) ||
        !words_zero(model->reserved, 4U)) return REIST_GUI_TABS_EINVAL;
    if (model->content.y < model->tab_bar.y + (int32_t)model->tab_bar.height)
        return REIST_GUI_TABS_EINVAL;
    uint64_t used = 0U;
    uint32_t selectable_count = 0U;
    for (uint32_t index = 0U; index < model->tab_count; ++index) {
        const reist_gui_tab_t *tab = &model->tabs[index];
        if (tab->id == REIST_GUI_TABS_NO_ID ||
            tab->page_id == REIST_GUI_TABS_NO_ID || !text_valid(tab->label) ||
            tab->width == 0U ||
            (tab->flags & ~(REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED)) != 0U ||
            !words_zero(tab->reserved, 3U)) return REIST_GUI_TABS_EINVAL;
        for (uint32_t other = 0U; other < index; ++other)
            if (model->tabs[other].id == tab->id ||
                model->tabs[other].page_id == tab->page_id)
                return REIST_GUI_TABS_EINVAL;
        if ((tab->flags & REIST_GUI_TAB_VISIBLE) != 0U) {
            used += tab->width;
            if (used > model->tab_bar.width) return REIST_GUI_TABS_ECAPACITY;
        }
        if (selectable(tab)) ++selectable_count;
    }
    return selectable_count != 0U ? REIST_GUI_TABS_OK : REIST_GUI_TABS_EINVAL;
}

static uint32_t result_valid(const reist_gui_tabs_result_t *result) {
    return result != NULL && result->version == REIST_GUI_TABS_API_VERSION &&
           result->struct_size == sizeof(*result) &&
           result->damage_count <= REIST_GUI_TABS_DAMAGE_CAPACITY &&
           result->full_redraw <= 1U && words_zero(result->reserved, 4U);
}

void reist_gui_tabs_state_initialize(reist_gui_tabs_state_t *state) {
    if (state == NULL) return;
    clear_bytes(state, sizeof(*state));
    state->version = REIST_GUI_TABS_API_VERSION;
    state->struct_size = sizeof(*state);
    state->selected = REIST_GUI_TABS_NO_INDEX;
    state->focused = REIST_GUI_TABS_NO_INDEX;
    state->hovered = REIST_GUI_TABS_NO_INDEX;
    state->captured = REIST_GUI_TABS_NO_INDEX;
}

void reist_gui_tabs_event_initialize(reist_gui_tabs_event_t *event) {
    if (event == NULL) return;
    clear_bytes(event, sizeof(*event));
    event->version = REIST_GUI_TABS_API_VERSION;
    event->struct_size = sizeof(*event);
}

void reist_gui_tabs_result_initialize(reist_gui_tabs_result_t *result) {
    if (result == NULL) return;
    clear_bytes(result, sizeof(*result));
    result->version = REIST_GUI_TABS_API_VERSION;
    result->struct_size = sizeof(*result);
    result->selected_id = REIST_GUI_TABS_NO_ID;
    result->page_id = REIST_GUI_TABS_NO_ID;
    result->focused_id = REIST_GUI_TABS_NO_ID;
}

int reist_gui_tabs_tab_rect(const reist_gui_tabs_model_t *model,
                            uint32_t index, reist_gui_rect_t *rect_out) {
    int status = validate_model(model);
    if (status != REIST_GUI_TABS_OK) return status;
    if (rect_out == NULL || index >= model->tab_count)
        return REIST_GUI_TABS_EINVAL;
    uint64_t offset = 0U;
    for (uint32_t current = 0U; current < index; ++current)
        if ((model->tabs[current].flags & REIST_GUI_TAB_VISIBLE) != 0U)
            offset += model->tabs[current].width;
    if ((model->tabs[index].flags & REIST_GUI_TAB_VISIBLE) == 0U)
        return REIST_GUI_TABS_EINVAL;
    *rect_out = (reist_gui_rect_t){
        model->tab_bar.x + (int32_t)offset, model->tab_bar.y,
        model->tabs[index].width, model->tab_bar.height};
    return REIST_GUI_TABS_OK;
}

static int validate_state(const reist_gui_tabs_model_t *model,
                          const reist_gui_tabs_state_t *state) {
    if (state == NULL || state->version != REIST_GUI_TABS_API_VERSION ||
        state->struct_size != sizeof(*state) || state->configured > 1U ||
        state->armed > 1U || !words_zero(state->reserved, 4U))
        return REIST_GUI_TABS_EINVAL;
    if (!state->configured)
        return state->tab_count == 0U &&
               state->selected == REIST_GUI_TABS_NO_INDEX &&
               state->focused == REIST_GUI_TABS_NO_INDEX &&
               state->hovered == REIST_GUI_TABS_NO_INDEX &&
               state->captured == REIST_GUI_TABS_NO_INDEX && !state->armed
            ? REIST_GUI_TABS_OK : REIST_GUI_TABS_EINVAL;
    if (state->tab_count != model->tab_count ||
        state->selected >= model->tab_count ||
        !selectable(&model->tabs[state->selected])) return REIST_GUI_TABS_EINVAL;
    const uint32_t indexes[] = {
        state->focused, state->hovered, state->captured};
    for (uint32_t slot = 0U; slot < 3U; ++slot)
        if (indexes[slot] != REIST_GUI_TABS_NO_INDEX &&
            (indexes[slot] >= model->tab_count ||
             !selectable(&model->tabs[indexes[slot]])))
            return REIST_GUI_TABS_EINVAL;
    if (state->captured == REIST_GUI_TABS_NO_INDEX && state->armed != 0U)
        return REIST_GUI_TABS_EINVAL;
    return REIST_GUI_TABS_OK;
}

int reist_gui_tabs_validate(const reist_gui_tabs_model_t *model,
                            const reist_gui_tabs_state_t *state) {
    int status = validate_model(model);
    return status == REIST_GUI_TABS_OK ? validate_state(model, state) : status;
}

static void damage(reist_gui_tabs_result_t *result, reist_gui_rect_t rect,
                   uint32_t margin) {
    if (result->full_redraw) return;
    int64_t x = (int64_t)rect.x - margin;
    int64_t y = (int64_t)rect.y - margin;
    uint64_t width = (uint64_t)rect.width + margin * 2U;
    uint64_t height = (uint64_t)rect.height + margin * 2U;
    if (x < 0) { width -= (uint64_t)(-x); x = 0; }
    if (y < 0) { height -= (uint64_t)(-y); y = 0; }
    if (result->damage_count == REIST_GUI_TABS_DAMAGE_CAPACITY) {
        result->damage_count = 0U;
        result->full_redraw = 1U;
        return;
    }
    result->damage[result->damage_count++] = (reist_gui_rect_t){
        (int32_t)x, (int32_t)y, (uint32_t)width, (uint32_t)height};
}

static void damage_tab(const reist_gui_tabs_model_t *model,
                       reist_gui_tabs_result_t *result, uint32_t index) {
    reist_gui_rect_t rect;
    if (index != REIST_GUI_TABS_NO_INDEX &&
        reist_gui_tabs_tab_rect(model, index, &rect) == REIST_GUI_TABS_OK)
        damage(result, rect, model->damage_margin);
}

static uint32_t index_from_id(const reist_gui_tabs_model_t *model,
                              uint32_t id) {
    for (uint32_t index = 0U; index < model->tab_count; ++index)
        if (model->tabs[index].id == id) return index;
    return REIST_GUI_TABS_NO_INDEX;
}

static uint32_t first_selectable(const reist_gui_tabs_model_t *model,
                                 uint32_t reverse) {
    for (uint32_t step = 0U; step < model->tab_count; ++step) {
        uint32_t index = reverse ? model->tab_count - 1U - step : step;
        if (selectable(&model->tabs[index])) return index;
    }
    return REIST_GUI_TABS_NO_INDEX;
}

int reist_gui_tabs_configure(const reist_gui_tabs_model_t *model,
                             reist_gui_tabs_state_t *state,
                             uint32_t initial_tab_id,
                             reist_gui_tabs_result_t *result) {
    int status = validate_model(model);
    if (status != REIST_GUI_TABS_OK) return status;
    if (state == NULL || state->version != REIST_GUI_TABS_API_VERSION ||
        state->struct_size != sizeof(*state) ||
        !words_zero(state->reserved, 4U) || !result_valid(result))
        return REIST_GUI_TABS_EINVAL;
    uint32_t selected = initial_tab_id == REIST_GUI_TABS_NO_ID
        ? first_selectable(model, 0U) : index_from_id(model, initial_tab_id);
    if (selected == REIST_GUI_TABS_NO_INDEX ||
        !selectable(&model->tabs[selected])) return REIST_GUI_TABS_EINVAL;
    reist_gui_tabs_state_initialize(state);
    state->configured = 1U;
    state->tab_count = model->tab_count;
    state->selected = selected;
    state->focused = selected;
    result->selection_changed = 1U;
    result->focus_changed = 1U;
    result->selected_id = model->tabs[selected].id;
    result->page_id = model->tabs[selected].page_id;
    result->focused_id = model->tabs[selected].id;
    damage(result, model->tab_bar, model->damage_margin);
    damage(result, model->content, model->damage_margin);
    return REIST_GUI_TABS_OK;
}

static void select_index(const reist_gui_tabs_model_t *model,
                         reist_gui_tabs_state_t *state,
                         reist_gui_tabs_result_t *result, uint32_t index,
                         uint32_t move_focus) {
    if (move_focus && state->focused != index) {
        damage_tab(model, result, state->focused);
        state->focused = index;
        result->focus_changed = 1U;
        result->focused_id = model->tabs[index].id;
        damage_tab(model, result, index);
    }
    if (state->selected != index) {
        damage_tab(model, result, state->selected);
        state->selected = index;
        result->selection_changed = 1U;
        result->selected_id = model->tabs[index].id;
        result->page_id = model->tabs[index].page_id;
        damage_tab(model, result, index);
        damage(result, model->content, model->damage_margin);
    }
}

int reist_gui_tabs_select(const reist_gui_tabs_model_t *model,
                          reist_gui_tabs_state_t *state,
                          uint32_t tab_id,
                          reist_gui_tabs_result_t *result) {
    if (reist_gui_tabs_validate(model, state) != REIST_GUI_TABS_OK ||
        !state->configured || !result_valid(result))
        return REIST_GUI_TABS_EINVAL;
    uint32_t index = index_from_id(model, tab_id);
    if (index == REIST_GUI_TABS_NO_INDEX || !selectable(&model->tabs[index]))
        return REIST_GUI_TABS_EINVAL;
    select_index(model, state, result, index, 1U);
    return REIST_GUI_TABS_OK;
}

int reist_gui_tabs_selected(const reist_gui_tabs_model_t *model,
                            const reist_gui_tabs_state_t *state,
                            uint32_t *tab_id_out, uint32_t *page_id_out) {
    if (reist_gui_tabs_validate(model, state) != REIST_GUI_TABS_OK ||
        !state->configured || tab_id_out == NULL || page_id_out == NULL)
        return REIST_GUI_TABS_EINVAL;
    *tab_id_out = model->tabs[state->selected].id;
    *page_id_out = model->tabs[state->selected].page_id;
    return REIST_GUI_TABS_OK;
}

static uint32_t hit_test(const reist_gui_tabs_model_t *model,
                         int32_t x, int32_t y) {
    for (uint32_t index = 0U; index < model->tab_count; ++index) {
        reist_gui_rect_t rect;
        if (!selectable(&model->tabs[index]) ||
            reist_gui_tabs_tab_rect(model, index, &rect) != 0) continue;
        if (x >= rect.x && y >= rect.y &&
            (uint64_t)(uint32_t)(x - rect.x) < rect.width &&
            (uint64_t)(uint32_t)(y - rect.y) < rect.height) return index;
    }
    return REIST_GUI_TABS_NO_INDEX;
}

static uint32_t next_selectable(const reist_gui_tabs_model_t *model,
                                uint32_t current, uint32_t reverse) {
    for (uint32_t step = 1U; step <= model->tab_count; ++step) {
        uint32_t index = reverse
            ? (current + model->tab_count - (step % model->tab_count)) %
                model->tab_count
            : (current + step) % model->tab_count;
        if (selectable(&model->tabs[index])) return index;
    }
    return current;
}

static uint32_t event_valid(const reist_gui_tabs_event_t *event) {
    if (event == NULL || event->version != REIST_GUI_TABS_API_VERSION ||
        event->struct_size != sizeof(*event) ||
        !words_zero(event->reserved, 4U)) return 0U;
    if (event->type == REIST_GUI_TABS_EVENT_POINTER_MOTION)
        return event->button == 0U && event->pressed == 0U && event->key == 0U;
    if (event->type == REIST_GUI_TABS_EVENT_POINTER_BUTTON)
        return event->button == REIST_GUI_TABS_BUTTON_LEFT &&
               event->pressed <= 1U && event->key == 0U;
    if (event->type == REIST_GUI_TABS_EVENT_KEYBOARD)
        return event->button == 0U && event->pressed == 0U &&
               event->key >= REIST_GUI_TABS_KEY_LEFT &&
               event->key <= REIST_GUI_TABS_KEY_SPACE;
    if (event->type == REIST_GUI_TABS_EVENT_CANCEL)
        return event->button == 0U && event->pressed == 0U && event->key == 0U;
    return 0U;
}

int reist_gui_tabs_dispatch(const reist_gui_tabs_model_t *model,
                            reist_gui_tabs_state_t *state,
                            const reist_gui_tabs_event_t *event,
                            reist_gui_tabs_result_t *result) {
    if (reist_gui_tabs_validate(model, state) != REIST_GUI_TABS_OK ||
        !state->configured || !event_valid(event) || !result_valid(result))
        return REIST_GUI_TABS_EINVAL;
    if (event->type == REIST_GUI_TABS_EVENT_CANCEL) {
        if (state->captured != REIST_GUI_TABS_NO_INDEX) {
            damage_tab(model, result, state->captured);
            state->captured = REIST_GUI_TABS_NO_INDEX;
            state->armed = 0U;
            result->consumed = 1U;
        }
        return REIST_GUI_TABS_OK;
    }
    if (event->type == REIST_GUI_TABS_EVENT_POINTER_MOTION) {
        uint32_t hit = hit_test(model, event->x, event->y);
        if (hit != state->hovered) {
            damage_tab(model, result, state->hovered);
            state->hovered = hit;
            damage_tab(model, result, hit);
        }
        if (state->captured != REIST_GUI_TABS_NO_INDEX) {
            uint32_t armed = hit == state->captured;
            if (armed != state->armed) {
                state->armed = armed;
                damage_tab(model, result, state->captured);
            }
            result->consumed = 1U;
        } else result->consumed = hit != REIST_GUI_TABS_NO_INDEX;
        return REIST_GUI_TABS_OK;
    }
    if (event->type == REIST_GUI_TABS_EVENT_POINTER_BUTTON) {
        if (event->pressed) {
            if (state->captured != REIST_GUI_TABS_NO_INDEX)
                return REIST_GUI_TABS_EINVAL;
            uint32_t hit = hit_test(model, event->x, event->y);
            if (hit == REIST_GUI_TABS_NO_INDEX) return REIST_GUI_TABS_OK;
            state->hovered = hit;
            state->captured = hit;
            state->armed = 1U;
            damage_tab(model, result, hit);
            if (state->focused != hit) {
                damage_tab(model, result, state->focused);
                state->focused = hit;
                result->focus_changed = 1U;
                result->focused_id = model->tabs[hit].id;
            }
            result->consumed = 1U;
            return REIST_GUI_TABS_OK;
        }
        if (state->captured == REIST_GUI_TABS_NO_INDEX)
            return REIST_GUI_TABS_OK;
        uint32_t captured = state->captured;
        uint32_t hit = hit_test(model, event->x, event->y);
        state->captured = REIST_GUI_TABS_NO_INDEX;
        state->armed = 0U;
        damage_tab(model, result, captured);
        result->consumed = 1U;
        if (hit == captured) select_index(model, state, result, captured, 1U);
        return REIST_GUI_TABS_OK;
    }
    result->consumed = 1U;
    uint32_t target = state->focused != REIST_GUI_TABS_NO_INDEX
        ? state->focused : state->selected;
    if (event->key == REIST_GUI_TABS_KEY_LEFT ||
        event->key == REIST_GUI_TABS_KEY_RIGHT)
        target = next_selectable(model, target,
            event->key == REIST_GUI_TABS_KEY_LEFT);
    else if (event->key == REIST_GUI_TABS_KEY_HOME)
        target = first_selectable(model, 0U);
    else if (event->key == REIST_GUI_TABS_KEY_END)
        target = first_selectable(model, 1U);
    else if (event->key != REIST_GUI_TABS_KEY_ENTER &&
             event->key != REIST_GUI_TABS_KEY_SPACE) {
        result->consumed = 0U;
        return REIST_GUI_TABS_OK;
    }
    select_index(model, state, result, target, 1U);
    return REIST_GUI_TABS_OK;
}
