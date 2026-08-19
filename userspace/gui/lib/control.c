#include <reist/gui/control.h>

#include <stddef.h>

#define CONTROL_ALLOWED_FLAGS (REIST_GUI_CONTROL_VISIBLE | \
    REIST_GUI_CONTROL_ENABLED | REIST_GUI_CONTROL_DEFAULT | \
    REIST_GUI_CONTROL_TRISTATE)

static void clear_bytes(void *object, size_t size) {
    volatile unsigned char *bytes = (volatile unsigned char *)object;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static uint32_t words_zero(const uint32_t *words, uint32_t count) {
    for (uint32_t index = 0U; index < count; ++index)
        if (words[index] != 0U) return 0U;
    return 1U;
}

static uint32_t text_valid(const char *text) {
    if (text == NULL || text[0] == '\0') return 0U;
    for (uint32_t index = 1U; index < REIST_GUI_CONTROL_LABEL_LIMIT; ++index)
        if (text[index] == '\0') return 1U;
    return 0U;
}

static uint32_t visible_enabled(const reist_gui_control_t *control) {
    return (control->flags & (REIST_GUI_CONTROL_VISIBLE |
                              REIST_GUI_CONTROL_ENABLED)) ==
           (REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED);
}

static uint32_t interactive(const reist_gui_control_t *control) {
    return visible_enabled(control) &&
           control->role != REIST_GUI_CONTROL_ROLE_LABEL;
}

static uint32_t point_inside(reist_gui_rect_t rect, int32_t x, int32_t y) {
    if (x < rect.x || y < rect.y) return 0U;
    return (uint32_t)(x - rect.x) < rect.width &&
           (uint32_t)(y - rect.y) < rect.height;
}

static uint32_t hit_test(const reist_gui_control_model_t *model,
                         int32_t x, int32_t y) {
    for (uint32_t offset = 0U; offset < model->control_count; ++offset) {
        uint32_t index = model->control_count - offset - 1U;
        const reist_gui_control_t *control = &model->controls[index];
        if (interactive(control) && point_inside(control->bounds, x, y))
            return index;
    }
    return REIST_GUI_CONTROL_NO_INDEX;
}

static uint32_t result_valid(const reist_gui_control_result_t *result) {
    return result != NULL &&
           result->version == REIST_GUI_CONTROL_API_VERSION &&
           result->struct_size == sizeof(*result) &&
           result->consumed == 0U && result->activated == 0U &&
           result->control_id == REIST_GUI_CONTROL_NO_ID &&
           result->action == 0U && result->value_changed == 0U &&
           result->check_state == REIST_GUI_CONTROL_UNCHECKED &&
           result->focus_changed == 0U &&
           result->focused_id == REIST_GUI_CONTROL_NO_ID &&
           result->focus_reason == REIST_GUI_CONTROL_FOCUS_NONE &&
           result->damage_count == 0U && result->full_redraw == 0U &&
           words_zero(result->reserved, 4U);
}

static void add_damage(const reist_gui_control_model_t *model,
                       reist_gui_control_result_t *result,
                       reist_gui_rect_t rect) {
    uint32_t margin = model->damage_margin;
    int32_t left = rect.x > (int32_t)margin ? rect.x - (int32_t)margin : 0;
    int32_t top = rect.y > (int32_t)margin ? rect.y - (int32_t)margin : 0;
    uint32_t right = (uint32_t)rect.x + rect.width;
    uint32_t bottom = (uint32_t)rect.y + rect.height;
    if (right <= model->surface_width &&
        margin <= model->surface_width - right) right += margin;
    else right = model->surface_width;
    if (bottom <= model->surface_height &&
        margin <= model->surface_height - bottom) bottom += margin;
    else bottom = model->surface_height;
    if (result->full_redraw) return;
    if (result->damage_count >= REIST_GUI_CONTROL_DAMAGE_CAPACITY) {
        result->damage_count = 1U;
        result->damage[0] = (reist_gui_rect_t){
            0, 0, model->surface_width, model->surface_height};
        result->full_redraw = 1U;
        return;
    }
    result->damage[result->damage_count++] = (reist_gui_rect_t){
        left, top, right - (uint32_t)left, bottom - (uint32_t)top};
}

static void damage_index(const reist_gui_control_model_t *model,
                         reist_gui_control_result_t *result,
                         uint32_t index) {
    if (index < model->control_count)
        add_damage(model, result, model->controls[index].bounds);
}

void reist_gui_control_state_initialize(reist_gui_control_state_t *state) {
    if (state == NULL) return;
    clear_bytes(state, sizeof(*state));
    state->version = REIST_GUI_CONTROL_API_VERSION;
    state->struct_size = sizeof(*state);
    state->focused = REIST_GUI_CONTROL_NO_INDEX;
    state->hovered = REIST_GUI_CONTROL_NO_INDEX;
    state->captured = REIST_GUI_CONTROL_NO_INDEX;
}

void reist_gui_control_event_initialize(reist_gui_control_event_t *event) {
    if (event == NULL) return;
    clear_bytes(event, sizeof(*event));
    event->version = REIST_GUI_CONTROL_API_VERSION;
    event->struct_size = sizeof(*event);
}

void reist_gui_control_result_initialize(reist_gui_control_result_t *result) {
    if (result == NULL) return;
    clear_bytes(result, sizeof(*result));
    result->version = REIST_GUI_CONTROL_API_VERSION;
    result->struct_size = sizeof(*result);
}

static int validate_model(const reist_gui_control_model_t *model) {
    if (model == NULL || model->version != REIST_GUI_CONTROL_API_VERSION ||
        model->struct_size != sizeof(*model) || model->controls == NULL ||
        model->control_count == 0U ||
        model->control_count > REIST_GUI_CONTROL_CAPACITY ||
        model->surface_width == 0U || model->surface_height == 0U ||
        model->damage_margin > 16U || !words_zero(model->reserved, 4U))
        return REIST_GUI_CONTROL_EINVAL;

    uint32_t default_count = 0U;
    for (uint32_t index = 0U; index < model->control_count; ++index) {
        const reist_gui_control_t *control = &model->controls[index];
        if (control->id == REIST_GUI_CONTROL_NO_ID ||
            control->role < REIST_GUI_CONTROL_ROLE_LABEL ||
            control->role > REIST_GUI_CONTROL_ROLE_RADIO_BUTTON ||
            !text_valid(control->label) ||
            (control->flags & ~CONTROL_ALLOWED_FLAGS) != 0U ||
            control->initial_check > REIST_GUI_CONTROL_MIXED ||
            !words_zero(control->reserved, 2U))
            return REIST_GUI_CONTROL_EINVAL;
        if (control->bounds.x < 0 || control->bounds.y < 0 ||
            control->bounds.width == 0U || control->bounds.height == 0U ||
            (uint32_t)control->bounds.x >= model->surface_width ||
            (uint32_t)control->bounds.y >= model->surface_height ||
            control->bounds.width >
                model->surface_width - (uint32_t)control->bounds.x ||
            control->bounds.height >
                model->surface_height - (uint32_t)control->bounds.y)
            return REIST_GUI_CONTROL_EOVERFLOW;
        for (uint32_t other = 0U; other < index; ++other)
            if (model->controls[other].id == control->id)
                return REIST_GUI_CONTROL_EINVAL;

        if (control->role == REIST_GUI_CONTROL_ROLE_LABEL) {
            if (control->action != 0U || control->group != 0U ||
                control->initial_check != REIST_GUI_CONTROL_UNCHECKED ||
                (control->flags & (REIST_GUI_CONTROL_ENABLED |
                    REIST_GUI_CONTROL_DEFAULT |
                    REIST_GUI_CONTROL_TRISTATE)) != 0U)
                return REIST_GUI_CONTROL_EINVAL;
        } else if (control->role == REIST_GUI_CONTROL_ROLE_PUSH_BUTTON) {
            if (control->group != 0U ||
                control->initial_check != REIST_GUI_CONTROL_UNCHECKED ||
                (control->flags & REIST_GUI_CONTROL_TRISTATE) != 0U)
                return REIST_GUI_CONTROL_EINVAL;
            if ((control->flags & REIST_GUI_CONTROL_DEFAULT) != 0U)
                ++default_count;
        } else if (control->role == REIST_GUI_CONTROL_ROLE_CHECKBOX) {
            if (control->group != 0U ||
                (control->flags & REIST_GUI_CONTROL_DEFAULT) != 0U ||
                (control->initial_check == REIST_GUI_CONTROL_MIXED &&
                 (control->flags & REIST_GUI_CONTROL_TRISTATE) == 0U))
                return REIST_GUI_CONTROL_EINVAL;
        } else {
            if (control->group == 0U ||
                control->initial_check == REIST_GUI_CONTROL_MIXED ||
                (control->flags & (REIST_GUI_CONTROL_DEFAULT |
                    REIST_GUI_CONTROL_TRISTATE)) != 0U)
                return REIST_GUI_CONTROL_EINVAL;
            if (control->initial_check == REIST_GUI_CONTROL_CHECKED) {
                for (uint32_t other = 0U; other < index; ++other) {
                    const reist_gui_control_t *prior =
                        &model->controls[other];
                    if (prior->role == REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
                        prior->group == control->group &&
                        prior->initial_check == REIST_GUI_CONTROL_CHECKED)
                        return REIST_GUI_CONTROL_EINVAL;
                }
            }
        }
    }
    return default_count <= 1U ? REIST_GUI_CONTROL_OK
                               : REIST_GUI_CONTROL_EINVAL;
}

static int validate_state(const reist_gui_control_model_t *model,
                          const reist_gui_control_state_t *state) {
    if (state == NULL || state->version != REIST_GUI_CONTROL_API_VERSION ||
        state->struct_size != sizeof(*state) ||
        state->configured > 1U || state->armed > 1U ||
        state->focus_reason > REIST_GUI_CONTROL_FOCUS_PROGRAMMATIC ||
        !words_zero(state->reserved, 4U))
        return REIST_GUI_CONTROL_EINVAL;
    if (!state->configured)
        return state->control_count == 0U &&
               state->focused == REIST_GUI_CONTROL_NO_INDEX &&
               state->hovered == REIST_GUI_CONTROL_NO_INDEX &&
               state->captured == REIST_GUI_CONTROL_NO_INDEX &&
               state->armed == 0U ? REIST_GUI_CONTROL_OK
                                  : REIST_GUI_CONTROL_EINVAL;
    if (state->control_count != model->control_count ||
        (state->focused != REIST_GUI_CONTROL_NO_INDEX &&
         state->focused >= model->control_count) ||
        (state->hovered != REIST_GUI_CONTROL_NO_INDEX &&
         state->hovered >= model->control_count) ||
        (state->captured != REIST_GUI_CONTROL_NO_INDEX &&
         state->captured >= model->control_count) ||
        (state->captured == REIST_GUI_CONTROL_NO_INDEX && state->armed))
        return REIST_GUI_CONTROL_EINVAL;
    if ((state->focused == REIST_GUI_CONTROL_NO_INDEX) !=
        (state->focus_reason == REIST_GUI_CONTROL_FOCUS_NONE) ||
        (state->focused != REIST_GUI_CONTROL_NO_INDEX &&
         !interactive(&model->controls[state->focused])) ||
        (state->hovered != REIST_GUI_CONTROL_NO_INDEX &&
         !interactive(&model->controls[state->hovered])) ||
        (state->captured != REIST_GUI_CONTROL_NO_INDEX &&
         !interactive(&model->controls[state->captured])))
        return REIST_GUI_CONTROL_EINVAL;
    for (uint32_t index = 0U; index < model->control_count; ++index) {
        const reist_gui_control_t *control = &model->controls[index];
        if (state->check[index] > REIST_GUI_CONTROL_MIXED ||
            ((control->role == REIST_GUI_CONTROL_ROLE_LABEL ||
              control->role == REIST_GUI_CONTROL_ROLE_PUSH_BUTTON) &&
             state->check[index] != REIST_GUI_CONTROL_UNCHECKED) ||
            (control->role == REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
             state->check[index] == REIST_GUI_CONTROL_MIXED) ||
            (control->role == REIST_GUI_CONTROL_ROLE_CHECKBOX &&
             state->check[index] == REIST_GUI_CONTROL_MIXED &&
             (control->flags & REIST_GUI_CONTROL_TRISTATE) == 0U))
            return REIST_GUI_CONTROL_EINVAL;
        if (control->role == REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
            state->check[index] == REIST_GUI_CONTROL_CHECKED) {
            for (uint32_t other = 0U; other < index; ++other)
                if (model->controls[other].role ==
                        REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
                    model->controls[other].group == control->group &&
                    state->check[other] == REIST_GUI_CONTROL_CHECKED)
                    return REIST_GUI_CONTROL_EINVAL;
        }
    }
    for (uint32_t index = model->control_count;
         index < REIST_GUI_CONTROL_CAPACITY; ++index)
        if (state->check[index] != REIST_GUI_CONTROL_UNCHECKED)
            return REIST_GUI_CONTROL_EINVAL;
    return REIST_GUI_CONTROL_OK;
}

int reist_gui_control_validate(const reist_gui_control_model_t *model,
                               const reist_gui_control_state_t *state) {
    int status = validate_model(model);
    return status == REIST_GUI_CONTROL_OK
        ? validate_state(model, state) : status;
}

int reist_gui_control_configure(const reist_gui_control_model_t *model,
                                reist_gui_control_state_t *state,
                                reist_gui_control_result_t *result) {
    int status = validate_model(model);
    if (status != REIST_GUI_CONTROL_OK) return status;
    if (state == NULL ||
        state->version != REIST_GUI_CONTROL_API_VERSION ||
        state->struct_size != sizeof(*state) ||
        !words_zero(state->reserved, 4U) || !result_valid(result))
        return REIST_GUI_CONTROL_EINVAL;
    reist_gui_control_state_initialize(state);
    state->configured = 1U;
    state->control_count = model->control_count;
    for (uint32_t index = 0U; index < model->control_count; ++index)
        state->check[index] = model->controls[index].initial_check;
    add_damage(model, result, (reist_gui_rect_t){
        0, 0, model->surface_width, model->surface_height});
    return REIST_GUI_CONTROL_OK;
}

int reist_gui_control_index(const reist_gui_control_model_t *model,
                            uint32_t control_id, uint32_t *index_out) {
    int status = validate_model(model);
    if (status != REIST_GUI_CONTROL_OK) return status;
    if (index_out == NULL || control_id == REIST_GUI_CONTROL_NO_ID)
        return REIST_GUI_CONTROL_EINVAL;
    for (uint32_t index = 0U; index < model->control_count; ++index) {
        if (model->controls[index].id == control_id) {
            *index_out = index;
            return REIST_GUI_CONTROL_OK;
        }
    }
    return REIST_GUI_CONTROL_EINVAL;
}

static void publish_focus(const reist_gui_control_model_t *model,
                          reist_gui_control_state_t *state,
                          reist_gui_control_result_t *result,
                          uint32_t index, uint32_t reason) {
    uint32_t old = state->focused;
    if (old == index && state->focus_reason == reason) return;
    damage_index(model, result, old);
    state->focused = index;
    state->focus_reason = reason;
    damage_index(model, result, index);
    result->focus_changed = 1U;
    result->focused_id = index < model->control_count
        ? model->controls[index].id : REIST_GUI_CONTROL_NO_ID;
    result->focus_reason = reason;
}

int reist_gui_control_focus(const reist_gui_control_model_t *model,
                            reist_gui_control_state_t *state,
                            uint32_t control_id, uint32_t reason,
                            reist_gui_control_result_t *result) {
    if (reist_gui_control_validate(model, state) != REIST_GUI_CONTROL_OK ||
        !state->configured || !result_valid(result) ||
        reason < REIST_GUI_CONTROL_FOCUS_POINTER ||
        reason > REIST_GUI_CONTROL_FOCUS_PROGRAMMATIC)
        return REIST_GUI_CONTROL_EINVAL;
    uint32_t index;
    if (reist_gui_control_index(model, control_id, &index) != 0 ||
        !interactive(&model->controls[index]))
        return REIST_GUI_CONTROL_EINVAL;
    publish_focus(model, state, result, index, reason);
    return REIST_GUI_CONTROL_OK;
}

static void publish_activation(const reist_gui_control_model_t *model,
                               reist_gui_control_state_t *state,
                               reist_gui_control_result_t *result,
                               uint32_t index) {
    const reist_gui_control_t *control = &model->controls[index];
    if (control->role == REIST_GUI_CONTROL_ROLE_CHECKBOX) {
        uint32_t next = state->check[index] == REIST_GUI_CONTROL_CHECKED
            ? REIST_GUI_CONTROL_UNCHECKED : REIST_GUI_CONTROL_CHECKED;
        if (next != state->check[index]) {
            state->check[index] = next;
            result->value_changed = 1U;
            damage_index(model, result, index);
        }
    } else if (control->role == REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
               state->check[index] != REIST_GUI_CONTROL_CHECKED) {
        for (uint32_t other = 0U; other < model->control_count; ++other) {
            if (other != index && model->controls[other].role ==
                    REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
                model->controls[other].group == control->group &&
                state->check[other] == REIST_GUI_CONTROL_CHECKED) {
                state->check[other] = REIST_GUI_CONTROL_UNCHECKED;
                damage_index(model, result, other);
            }
        }
        state->check[index] = REIST_GUI_CONTROL_CHECKED;
        result->value_changed = 1U;
        damage_index(model, result, index);
    }
    result->activated = 1U;
    result->control_id = control->id;
    result->action = control->action;
    result->check_state = state->check[index];
}

int reist_gui_control_set_check(const reist_gui_control_model_t *model,
                                reist_gui_control_state_t *state,
                                uint32_t control_id, uint32_t check_state,
                                reist_gui_control_result_t *result) {
    if (reist_gui_control_validate(model, state) != REIST_GUI_CONTROL_OK ||
        !state->configured || !result_valid(result) ||
        check_state > REIST_GUI_CONTROL_MIXED)
        return REIST_GUI_CONTROL_EINVAL;
    uint32_t index;
    if (reist_gui_control_index(model, control_id, &index) != 0)
        return REIST_GUI_CONTROL_EINVAL;
    const reist_gui_control_t *control = &model->controls[index];
    if (control->role != REIST_GUI_CONTROL_ROLE_CHECKBOX &&
        control->role != REIST_GUI_CONTROL_ROLE_RADIO_BUTTON)
        return REIST_GUI_CONTROL_EINVAL;
    if ((check_state == REIST_GUI_CONTROL_MIXED &&
         (control->role != REIST_GUI_CONTROL_ROLE_CHECKBOX ||
          (control->flags & REIST_GUI_CONTROL_TRISTATE) == 0U)))
        return REIST_GUI_CONTROL_EINVAL;
    if (control->role == REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
        check_state == REIST_GUI_CONTROL_CHECKED) {
        for (uint32_t other = 0U; other < model->control_count; ++other) {
            if (other != index && model->controls[other].role ==
                    REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
                model->controls[other].group == control->group &&
                state->check[other] == REIST_GUI_CONTROL_CHECKED) {
                state->check[other] = REIST_GUI_CONTROL_UNCHECKED;
                damage_index(model, result, other);
            }
        }
    }
    if (state->check[index] != check_state) {
        state->check[index] = check_state;
        result->value_changed = 1U;
        result->control_id = control->id;
        result->check_state = check_state;
        damage_index(model, result, index);
    }
    return REIST_GUI_CONTROL_OK;
}

static uint32_t radio_tab_target(const reist_gui_control_model_t *model,
                                 const reist_gui_control_state_t *state,
                                 uint32_t index) {
    const reist_gui_control_t *control = &model->controls[index];
    if (control->role != REIST_GUI_CONTROL_ROLE_RADIO_BUTTON) return 1U;
    if (state->check[index] == REIST_GUI_CONTROL_CHECKED) return 1U;
    for (uint32_t other = 0U; other < model->control_count; ++other)
        if (model->controls[other].role ==
                REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
            model->controls[other].group == control->group &&
            visible_enabled(&model->controls[other]) &&
            state->check[other] == REIST_GUI_CONTROL_CHECKED)
            return 0U;
    for (uint32_t other = 0U; other < index; ++other)
        if (model->controls[other].role ==
                REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
            model->controls[other].group == control->group &&
            visible_enabled(&model->controls[other]))
            return 0U;
    return 1U;
}

static uint32_t next_focus(const reist_gui_control_model_t *model,
                           const reist_gui_control_state_t *state,
                           uint32_t reverse) {
    uint32_t start = state->focused == REIST_GUI_CONTROL_NO_INDEX
        ? (reverse ? 0U : model->control_count - 1U) : state->focused;
    for (uint32_t step = 1U; step <= model->control_count; ++step) {
        uint32_t index = reverse
            ? (start + model->control_count -
               (step % model->control_count)) % model->control_count
            : (start + step) % model->control_count;
        if (interactive(&model->controls[index]) &&
            radio_tab_target(model, state, index)) return index;
    }
    return REIST_GUI_CONTROL_NO_INDEX;
}

static uint32_t next_radio(const reist_gui_control_model_t *model,
                           uint32_t current, uint32_t reverse) {
    uint32_t group = model->controls[current].group;
    for (uint32_t step = 1U; step <= model->control_count; ++step) {
        uint32_t index = reverse
            ? (current + model->control_count -
               (step % model->control_count)) % model->control_count
            : (current + step) % model->control_count;
        if (model->controls[index].role ==
                REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
            model->controls[index].group == group &&
            interactive(&model->controls[index])) return index;
    }
    return current;
}

static uint32_t default_button(const reist_gui_control_model_t *model) {
    for (uint32_t index = 0U; index < model->control_count; ++index)
        if (model->controls[index].role ==
                REIST_GUI_CONTROL_ROLE_PUSH_BUTTON &&
            interactive(&model->controls[index]) &&
            (model->controls[index].flags & REIST_GUI_CONTROL_DEFAULT) != 0U)
            return index;
    return REIST_GUI_CONTROL_NO_INDEX;
}

static int validate_event(const reist_gui_control_event_t *event) {
    if (event == NULL || event->version != REIST_GUI_CONTROL_API_VERSION ||
        event->struct_size != sizeof(*event) ||
        !words_zero(event->reserved, 4U)) return REIST_GUI_CONTROL_EINVAL;
    if (event->type == REIST_GUI_CONTROL_EVENT_POINTER_MOTION)
        return event->button == 0U && event->pressed == 0U && event->key == 0U
            ? REIST_GUI_CONTROL_OK : REIST_GUI_CONTROL_EINVAL;
    if (event->type == REIST_GUI_CONTROL_EVENT_POINTER_BUTTON)
        return event->button == REIST_GUI_CONTROL_BUTTON_LEFT &&
               event->pressed <= 1U && event->key == 0U
            ? REIST_GUI_CONTROL_OK : REIST_GUI_CONTROL_EINVAL;
    if (event->type == REIST_GUI_CONTROL_EVENT_KEYBOARD)
        return event->button == 0U && event->pressed == 0U &&
               event->key >= REIST_GUI_CONTROL_KEY_NEXT &&
               event->key <= REIST_GUI_CONTROL_KEY_DOWN
            ? REIST_GUI_CONTROL_OK : REIST_GUI_CONTROL_EINVAL;
    if (event->type == REIST_GUI_CONTROL_EVENT_CANCEL)
        return event->button == 0U && event->pressed == 0U && event->key == 0U
            ? REIST_GUI_CONTROL_OK : REIST_GUI_CONTROL_EINVAL;
    return REIST_GUI_CONTROL_EINVAL;
}

int reist_gui_control_dispatch(const reist_gui_control_model_t *model,
                               reist_gui_control_state_t *state,
                               const reist_gui_control_event_t *event,
                               reist_gui_control_result_t *result) {
    if (reist_gui_control_validate(model, state) != REIST_GUI_CONTROL_OK ||
        !state->configured || validate_event(event) != 0 ||
        !result_valid(result)) return REIST_GUI_CONTROL_EINVAL;

    if (event->type == REIST_GUI_CONTROL_EVENT_CANCEL) {
        if (state->captured != REIST_GUI_CONTROL_NO_INDEX) {
            damage_index(model, result, state->captured);
            state->captured = REIST_GUI_CONTROL_NO_INDEX;
            state->armed = 0U;
            result->consumed = 1U;
        }
        return REIST_GUI_CONTROL_OK;
    }

    if (event->type == REIST_GUI_CONTROL_EVENT_POINTER_MOTION) {
        uint32_t hit = hit_test(model, event->x, event->y);
        if (hit != state->hovered) {
            damage_index(model, result, state->hovered);
            state->hovered = hit;
            damage_index(model, result, hit);
        }
        if (state->captured != REIST_GUI_CONTROL_NO_INDEX) {
            uint32_t armed = point_inside(
                model->controls[state->captured].bounds, event->x, event->y);
            if (armed != state->armed) {
                state->armed = armed;
                damage_index(model, result, state->captured);
            }
            result->consumed = 1U;
        } else if (hit != REIST_GUI_CONTROL_NO_INDEX) {
            result->consumed = 1U;
        }
        return REIST_GUI_CONTROL_OK;
    }

    if (event->type == REIST_GUI_CONTROL_EVENT_POINTER_BUTTON) {
        if (event->pressed) {
            if (state->captured != REIST_GUI_CONTROL_NO_INDEX)
                return REIST_GUI_CONTROL_EINVAL;
            uint32_t hit = hit_test(model, event->x, event->y);
            if (hit != state->hovered) {
                damage_index(model, result, state->hovered);
                state->hovered = hit;
                damage_index(model, result, hit);
            }
            if (hit == REIST_GUI_CONTROL_NO_INDEX) return REIST_GUI_CONTROL_OK;
            state->captured = hit;
            state->armed = 1U;
            damage_index(model, result, hit);
            publish_focus(model, state, result, hit,
                          REIST_GUI_CONTROL_FOCUS_POINTER);
            result->consumed = 1U;
            return REIST_GUI_CONTROL_OK;
        }
        if (state->captured == REIST_GUI_CONTROL_NO_INDEX)
            return REIST_GUI_CONTROL_OK;
        uint32_t captured = state->captured;
        uint32_t activate = point_inside(
            model->controls[captured].bounds, event->x, event->y);
        uint32_t hit = hit_test(model, event->x, event->y);
        if (hit != state->hovered) {
            damage_index(model, result, state->hovered);
            state->hovered = hit;
            damage_index(model, result, hit);
        }
        state->captured = REIST_GUI_CONTROL_NO_INDEX;
        state->armed = 0U;
        damage_index(model, result, captured);
        result->consumed = 1U;
        if (activate) publish_activation(model, state, result, captured);
        return REIST_GUI_CONTROL_OK;
    }

    result->consumed = 1U;
    if (event->key == REIST_GUI_CONTROL_KEY_NEXT ||
        event->key == REIST_GUI_CONTROL_KEY_PREVIOUS) {
        uint32_t next = next_focus(model, state,
            event->key == REIST_GUI_CONTROL_KEY_PREVIOUS);
        if (next != REIST_GUI_CONTROL_NO_INDEX)
            publish_focus(model, state, result, next,
                          REIST_GUI_CONTROL_FOCUS_KEYBOARD);
        return REIST_GUI_CONTROL_OK;
    }

    uint32_t focused = state->focused;
    if (focused != REIST_GUI_CONTROL_NO_INDEX &&
        model->controls[focused].role ==
            REIST_GUI_CONTROL_ROLE_RADIO_BUTTON &&
        (event->key == REIST_GUI_CONTROL_KEY_LEFT ||
         event->key == REIST_GUI_CONTROL_KEY_RIGHT ||
         event->key == REIST_GUI_CONTROL_KEY_UP ||
         event->key == REIST_GUI_CONTROL_KEY_DOWN)) {
        uint32_t reverse = event->key == REIST_GUI_CONTROL_KEY_LEFT ||
                           event->key == REIST_GUI_CONTROL_KEY_UP;
        uint32_t next = next_radio(model, focused, reverse);
        publish_focus(model, state, result, next,
                      REIST_GUI_CONTROL_FOCUS_KEYBOARD);
        publish_activation(model, state, result, next);
        return REIST_GUI_CONTROL_OK;
    }

    uint32_t target = REIST_GUI_CONTROL_NO_INDEX;
    if (event->key == REIST_GUI_CONTROL_KEY_SPACE &&
        focused != REIST_GUI_CONTROL_NO_INDEX)
        target = focused;
    else if (event->key == REIST_GUI_CONTROL_KEY_ENTER) {
        if (focused != REIST_GUI_CONTROL_NO_INDEX &&
            model->controls[focused].role ==
                REIST_GUI_CONTROL_ROLE_PUSH_BUTTON)
            target = focused;
        else target = default_button(model);
    }
    if (target != REIST_GUI_CONTROL_NO_INDEX &&
        interactive(&model->controls[target]))
        publish_activation(model, state, result, target);
    else result->consumed = 0U;
    return REIST_GUI_CONTROL_OK;
}
