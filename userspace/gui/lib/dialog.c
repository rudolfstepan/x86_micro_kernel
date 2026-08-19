/**
 * @file userspace/gui/lib/dialog.c
 * @brief Heap-free implementation of the public REIST dialog controller.
 *
 * Public entry points validate the complete object graph before state
 * mutation. Geometry and event transitions are bounded by the public four-
 * button and four-damage capacities.
 */
#include "reist/gui/dialog.h"

#include <limits.h>

static uint32_t reserved_zero(const uint32_t *reserved, uint32_t count) {
    if (reserved == 0) return 0U;
    for (uint32_t index = 0U; index < count; ++index) {
        if (reserved[index] != 0U) return 0U;
    }
    return 1U;
}

static int bounded_text_length(const char *text, uint32_t limit,
                               uint32_t optional, uint32_t *length) {
    if (length == 0 || (text == 0 && !optional))
        return REIST_GUI_DIALOG_EINVAL;
    if (text == 0) {
        *length = 0U;
        return REIST_GUI_DIALOG_OK;
    }
    for (uint32_t index = 0U; index < limit; ++index) {
        if (text[index] == '\0') {
            if (index == 0U && !optional) return REIST_GUI_DIALOG_EINVAL;
            *length = index;
            return REIST_GUI_DIALOG_OK;
        }
    }
    return REIST_GUI_DIALOG_EOVERFLOW;
}

static uint32_t point_in_rect(reist_gui_rect_t rect, int32_t x, int32_t y) {
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return x >= rect.x && y >= rect.y &&
           (int64_t)x < right && (int64_t)y < bottom;
}

static uint32_t rect_inside(reist_gui_rect_t outer, reist_gui_rect_t inner) {
    int64_t outer_right = (int64_t)outer.x + outer.width;
    int64_t outer_bottom = (int64_t)outer.y + outer.height;
    int64_t inner_right = (int64_t)inner.x + inner.width;
    int64_t inner_bottom = (int64_t)inner.y + inner.height;
    return inner.width != 0U && inner.height != 0U &&
           inner.x >= outer.x && inner.y >= outer.y &&
           inner_right <= outer_right && inner_bottom <= outer_bottom;
}

static uint32_t button_width(const reist_gui_dialog_button_t *button,
                             const reist_gui_dialog_layout_t *layout) {
    uint32_t length = 0U;
    (void)bounded_text_length(
        button->label, REIST_GUI_DIALOG_LABEL_LIMIT, 0U, &length);
    uint64_t measured = (uint64_t)length * layout->font_width +
                        (uint64_t)layout->button_padding_x * 2U;
    if (measured < layout->button_min_width)
        measured = layout->button_min_width;
    return (uint32_t)measured;
}

static int find_response(const reist_gui_dialog_model_t *model,
                         uint32_t response, uint32_t enabled_only,
                         uint32_t *index) {
    if (response == REIST_GUI_DIALOG_RESPONSE_NONE)
        return REIST_GUI_DIALOG_EINVAL;
    for (uint32_t current = 0U;
         current < model->button_count; ++current) {
        if (model->buttons[current].response == response &&
            (!enabled_only ||
             (model->buttons[current].flags &
              REIST_GUI_DIALOG_BUTTON_DISABLED) == 0U)) {
            if (index != 0) *index = current;
            return REIST_GUI_DIALOG_OK;
        }
    }
    return REIST_GUI_DIALOG_EINVAL;
}

static int validate_layout(const reist_gui_dialog_layout_t *layout) {
    if (layout == 0 ||
        layout->version != REIST_GUI_DIALOG_API_VERSION ||
        layout->struct_size < sizeof(*layout) ||
        !reserved_zero(layout->reserved, 4U) ||
        layout->surface_width == 0U || layout->surface_height == 0U ||
        layout->surface_width > INT32_MAX ||
        layout->surface_height > INT32_MAX ||
        layout->font_width == 0U || layout->font_width > 64U ||
        layout->font_height == 0U || layout->font_height > 64U ||
        layout->title_height < layout->font_height ||
        layout->title_height > 128U ||
        layout->border_width == 0U || layout->border_width > 16U ||
        layout->button_min_width == 0U ||
        layout->button_min_width > 512U ||
        layout->button_height < layout->font_height ||
        layout->button_height > 128U ||
        layout->button_gap > 64U || layout->button_padding_x > 64U ||
        layout->content_padding > 128U || layout->damage_margin > 16U)
        return REIST_GUI_DIALOG_EINVAL;

    reist_gui_rect_t surface = {
        0, 0, layout->surface_width, layout->surface_height
    };
    if (!rect_inside(surface, layout->work_area) ||
        !rect_inside(layout->work_area, layout->initial_bounds))
        return REIST_GUI_DIALOG_EINVAL;

    uint64_t minimum_height =
        (uint64_t)layout->border_width * 2U + layout->title_height +
        layout->content_padding * 2U + layout->font_height +
        layout->button_height;
    uint64_t minimum_width =
        (uint64_t)layout->border_width * 2U +
        layout->content_padding * 2U + layout->button_min_width;
    if (layout->initial_bounds.height < minimum_height ||
        layout->initial_bounds.width < minimum_width)
        return REIST_GUI_DIALOG_EOVERFLOW;
    return REIST_GUI_DIALOG_OK;
}

static int validate_model(const reist_gui_dialog_model_t *model,
                          const reist_gui_dialog_layout_t *layout) {
    uint32_t ignored = 0U;
    if (model == 0 ||
        model->version != REIST_GUI_DIALOG_API_VERSION ||
        model->struct_size < sizeof(*model) ||
        !reserved_zero(model->reserved, 4U) ||
        model->buttons == 0 || model->button_count == 0U ||
        model->button_count > REIST_GUI_DIALOG_MAX_BUTTONS ||
        model->modality > REIST_GUI_DIALOG_APPLICATION_MODAL ||
        (model->flags & ~(REIST_GUI_DIALOG_MOVABLE |
                          REIST_GUI_DIALOG_CLOSE_BUTTON)) != 0U ||
        bounded_text_length(model->title, REIST_GUI_DIALOG_LABEL_LIMIT,
                            0U, &ignored) != 0 ||
        bounded_text_length(model->message, REIST_GUI_DIALOG_TEXT_LIMIT,
                            0U, &ignored) != 0 ||
        bounded_text_length(model->detail, REIST_GUI_DIALOG_TEXT_LIMIT,
                            1U, &ignored) != 0)
        return REIST_GUI_DIALOG_EINVAL;

    if ((model->owner_id == REIST_GUI_DIALOG_NO_OWNER) !=
        (model->owner_generation == 0U))
        return REIST_GUI_DIALOG_EINVAL;
    if (model->modality == REIST_GUI_DIALOG_WINDOW_MODAL &&
        model->owner_id == REIST_GUI_DIALOG_NO_OWNER)
        return REIST_GUI_DIALOG_EINVAL;

    uint64_t total_width = 0U;
    uint32_t enabled = 0U;
    for (uint32_t index = 0U; index < model->button_count; ++index) {
        const reist_gui_dialog_button_t *button = &model->buttons[index];
        uint32_t length = 0U;
        if (bounded_text_length(button->label,
                                REIST_GUI_DIALOG_LABEL_LIMIT,
                                0U, &length) != 0 ||
            button->response == REIST_GUI_DIALOG_RESPONSE_NONE ||
            button->role < REIST_GUI_DIALOG_ROLE_ACCEPT ||
            button->role > REIST_GUI_DIALOG_ROLE_ACTION ||
            (button->flags & ~REIST_GUI_DIALOG_BUTTON_DISABLED) != 0U ||
            button->reserved != 0U)
            return REIST_GUI_DIALOG_EINVAL;
        for (uint32_t previous = 0U; previous < index; ++previous) {
            if (model->buttons[previous].response == button->response)
                return REIST_GUI_DIALOG_EINVAL;
        }
        uint64_t width = (uint64_t)length * layout->font_width +
                         (uint64_t)layout->button_padding_x * 2U;
        if (width < layout->button_min_width)
            width = layout->button_min_width;
        if (width > UINT32_MAX) return REIST_GUI_DIALOG_EOVERFLOW;
        total_width += width;
        if ((button->flags & REIST_GUI_DIALOG_BUTTON_DISABLED) == 0U)
            ++enabled;
    }
    total_width += (uint64_t)(model->button_count - 1U) *
                   layout->button_gap;
    uint64_t available = layout->initial_bounds.width -
        (uint64_t)layout->border_width * 2U -
        (uint64_t)layout->content_padding * 2U;
    if (enabled == 0U || total_width > available)
        return REIST_GUI_DIALOG_EOVERFLOW;
    if (find_response(model, model->default_response, 1U, 0) != 0)
        return REIST_GUI_DIALOG_EINVAL;
    if (model->cancel_response != REIST_GUI_DIALOG_RESPONSE_NONE &&
        find_response(model, model->cancel_response, 1U, 0) != 0)
        return REIST_GUI_DIALOG_EINVAL;
    if ((model->flags & REIST_GUI_DIALOG_CLOSE_BUTTON) != 0U &&
        model->cancel_response == REIST_GUI_DIALOG_RESPONSE_NONE)
        return REIST_GUI_DIALOG_EINVAL;
    return REIST_GUI_DIALOG_OK;
}

static int validate_state(const reist_gui_dialog_model_t *model,
                          const reist_gui_dialog_layout_t *layout,
                          const reist_gui_dialog_state_t *state) {
    if (state == 0 ||
        state->version != REIST_GUI_DIALOG_API_VERSION ||
        state->struct_size < sizeof(*state) ||
        !reserved_zero(state->reserved, 4U) ||
        state->visible > 1U || state->active > 1U ||
        state->completed > 1U ||
        state->modality > REIST_GUI_DIALOG_APPLICATION_MODAL ||
        state->capture_kind > REIST_GUI_DIALOG_CAPTURE_BODY)
        return REIST_GUI_DIALOG_EINVAL;
    if (state->active && !state->visible)
        return REIST_GUI_DIALOG_EINVAL;
    if (state->visible) {
        if (state->completed || state->response != 0U ||
            state->generation == 0U || state->modality != model->modality ||
            state->owner_id != model->owner_id ||
            state->owner_generation != model->owner_generation ||
            !rect_inside(layout->work_area, state->bounds))
            return REIST_GUI_DIALOG_EINVAL;
    } else if (state->capture_kind != REIST_GUI_DIALOG_CAPTURE_NONE ||
               state->capture_button != REIST_GUI_DIALOG_NO_INDEX ||
               state->hot_button != REIST_GUI_DIALOG_NO_INDEX) {
        return REIST_GUI_DIALOG_EINVAL;
    }
    if (state->completed == (state->response == 0U))
        return REIST_GUI_DIALOG_EINVAL;
    if (state->focused_button != REIST_GUI_DIALOG_NO_INDEX &&
        (state->focused_button >= model->button_count ||
         (model->buttons[state->focused_button].flags &
          REIST_GUI_DIALOG_BUTTON_DISABLED) != 0U))
        return REIST_GUI_DIALOG_EINVAL;
    if (state->hot_button != REIST_GUI_DIALOG_NO_INDEX &&
        (state->hot_button >= model->button_count ||
         (model->buttons[state->hot_button].flags &
          REIST_GUI_DIALOG_BUTTON_DISABLED) != 0U))
        return REIST_GUI_DIALOG_EINVAL;
    if (state->capture_kind == REIST_GUI_DIALOG_CAPTURE_BUTTON) {
        if (state->capture_button >= model->button_count ||
            (model->buttons[state->capture_button].flags &
             REIST_GUI_DIALOG_BUTTON_DISABLED) != 0U)
            return REIST_GUI_DIALOG_EINVAL;
    } else if (state->capture_button != REIST_GUI_DIALOG_NO_INDEX) {
        return REIST_GUI_DIALOG_EINVAL;
    }
    if (state->capture_kind == REIST_GUI_DIALOG_CAPTURE_CLOSE &&
        (model->flags & REIST_GUI_DIALOG_CLOSE_BUTTON) == 0U)
        return REIST_GUI_DIALOG_EINVAL;
    if (state->capture_kind == REIST_GUI_DIALOG_CAPTURE_MOVE &&
        (model->flags & REIST_GUI_DIALOG_MOVABLE) == 0U)
        return REIST_GUI_DIALOG_EINVAL;
    return REIST_GUI_DIALOG_OK;
}

static int validate_result(const reist_gui_dialog_result_t *result) {
    return result != 0 &&
           result->version == REIST_GUI_DIALOG_API_VERSION &&
           result->struct_size >= sizeof(*result) &&
           reserved_zero(result->reserved, 4U)
        ? REIST_GUI_DIALOG_OK : REIST_GUI_DIALOG_EINVAL;
}

static int validate_event(const reist_gui_dialog_event_t *event) {
    if (event == 0 ||
        event->version != REIST_GUI_DIALOG_API_VERSION ||
        event->struct_size < sizeof(*event) ||
        !reserved_zero(event->reserved, 4U) ||
        event->type < REIST_GUI_DIALOG_EVENT_POINTER_MOTION ||
        event->type > REIST_GUI_DIALOG_EVENT_KEYBOARD)
        return REIST_GUI_DIALOG_EINVAL;
    if (event->type == REIST_GUI_DIALOG_EVENT_POINTER_MOTION)
        return event->button == 0U && event->pressed == 0U && event->key == 0U
            ? REIST_GUI_DIALOG_OK : REIST_GUI_DIALOG_EINVAL;
    if (event->type == REIST_GUI_DIALOG_EVENT_POINTER_BUTTON)
        return event->button == REIST_GUI_DIALOG_BUTTON_LEFT &&
               event->pressed <= 1U && event->key == 0U
            ? REIST_GUI_DIALOG_OK : REIST_GUI_DIALOG_EINVAL;
    return event->button == 0U && event->pressed == 0U &&
           event->key >= REIST_GUI_DIALOG_KEY_PREVIOUS &&
           event->key <= REIST_GUI_DIALOG_KEY_ESCAPE
        ? REIST_GUI_DIALOG_OK : REIST_GUI_DIALOG_EINVAL;
}

void reist_gui_dialog_state_initialize(reist_gui_dialog_state_t *state) {
    if (state == 0) return;
    *state = (reist_gui_dialog_state_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(*state),
        .owner_id = REIST_GUI_DIALOG_NO_OWNER,
        .focused_button = REIST_GUI_DIALOG_NO_INDEX,
        .hot_button = REIST_GUI_DIALOG_NO_INDEX,
        .capture_kind = REIST_GUI_DIALOG_CAPTURE_NONE,
        .capture_button = REIST_GUI_DIALOG_NO_INDEX,
    };
}

void reist_gui_dialog_event_initialize(reist_gui_dialog_event_t *event) {
    if (event == 0) return;
    *event = (reist_gui_dialog_event_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(*event),
    };
}

void reist_gui_dialog_result_initialize(reist_gui_dialog_result_t *result) {
    if (result == 0) return;
    *result = (reist_gui_dialog_result_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(*result),
    };
}

int reist_gui_dialog_validate(const reist_gui_dialog_model_t *model,
                              const reist_gui_dialog_layout_t *layout,
                              const reist_gui_dialog_state_t *state) {
    int status = validate_layout(layout);
    if (status != 0) return status;
    status = validate_model(model, layout);
    if (status != 0) return status;
    return validate_state(model, layout, state);
}

static reist_gui_rect_t frame_rect_unchecked(
    const reist_gui_dialog_layout_t *layout,
    const reist_gui_dialog_state_t *state) {
    return state->bounds.width != 0U && state->bounds.height != 0U
        ? state->bounds : layout->initial_bounds;
}

static reist_gui_rect_t title_rect_unchecked(
    const reist_gui_dialog_layout_t *layout,
    const reist_gui_dialog_state_t *state) {
    reist_gui_rect_t frame = frame_rect_unchecked(layout, state);
    return (reist_gui_rect_t){
        frame.x + (int32_t)layout->border_width,
        frame.y + (int32_t)layout->border_width,
        frame.width - layout->border_width * 2U,
        layout->title_height,
    };
}

static reist_gui_rect_t close_rect_unchecked(
    const reist_gui_dialog_layout_t *layout,
    const reist_gui_dialog_state_t *state) {
    reist_gui_rect_t title = title_rect_unchecked(layout, state);
    uint32_t inset = title.height > 6U ? 3U : 0U;
    uint32_t size = title.height > inset * 2U
        ? title.height - inset * 2U : 1U;
    return (reist_gui_rect_t){
        title.x + (int32_t)inset,
        title.y + (int32_t)inset,
        size, size,
    };
}

static uint32_t button_total_width(
    const reist_gui_dialog_model_t *model,
    const reist_gui_dialog_layout_t *layout) {
    uint32_t total = layout->button_gap * (model->button_count - 1U);
    for (uint32_t index = 0U; index < model->button_count; ++index)
        total += button_width(&model->buttons[index], layout);
    return total;
}

static reist_gui_rect_t button_rect_unchecked(
    const reist_gui_dialog_model_t *model,
    const reist_gui_dialog_layout_t *layout,
    const reist_gui_dialog_state_t *state, uint32_t button_index) {
    reist_gui_rect_t frame = frame_rect_unchecked(layout, state);
    uint32_t total = button_total_width(model, layout);
    int32_t x = frame.x + (int32_t)((frame.width - total) / 2U);
    for (uint32_t index = 0U; index < button_index; ++index)
        x += (int32_t)(button_width(&model->buttons[index], layout) +
                       layout->button_gap);
    return (reist_gui_rect_t){
        x,
        frame.y + (int32_t)frame.height -
            (int32_t)(layout->border_width + layout->content_padding +
                      layout->button_height),
        button_width(&model->buttons[button_index], layout),
        layout->button_height,
    };
}

int reist_gui_dialog_frame_rect(const reist_gui_dialog_model_t *model,
                                const reist_gui_dialog_layout_t *layout,
                                const reist_gui_dialog_state_t *state,
                                reist_gui_rect_t *rect) {
    int status = reist_gui_dialog_validate(model, layout, state);
    if (status != 0 || rect == 0)
        return status != 0 ? status : REIST_GUI_DIALOG_EINVAL;
    *rect = frame_rect_unchecked(layout, state);
    return REIST_GUI_DIALOG_OK;
}

int reist_gui_dialog_title_rect(const reist_gui_dialog_model_t *model,
                                const reist_gui_dialog_layout_t *layout,
                                const reist_gui_dialog_state_t *state,
                                reist_gui_rect_t *rect) {
    int status = reist_gui_dialog_validate(model, layout, state);
    if (status != 0 || rect == 0)
        return status != 0 ? status : REIST_GUI_DIALOG_EINVAL;
    *rect = title_rect_unchecked(layout, state);
    return REIST_GUI_DIALOG_OK;
}

int reist_gui_dialog_close_rect(const reist_gui_dialog_model_t *model,
                                const reist_gui_dialog_layout_t *layout,
                                const reist_gui_dialog_state_t *state,
                                reist_gui_rect_t *rect) {
    int status = reist_gui_dialog_validate(model, layout, state);
    if (status != 0 || rect == 0 ||
        (model->flags & REIST_GUI_DIALOG_CLOSE_BUTTON) == 0U)
        return status != 0 ? status : REIST_GUI_DIALOG_EINVAL;
    *rect = close_rect_unchecked(layout, state);
    return REIST_GUI_DIALOG_OK;
}

int reist_gui_dialog_button_rect(const reist_gui_dialog_model_t *model,
                                 const reist_gui_dialog_layout_t *layout,
                                 const reist_gui_dialog_state_t *state,
                                 uint32_t button_index,
                                 reist_gui_rect_t *rect) {
    int status = reist_gui_dialog_validate(model, layout, state);
    if (status != 0 || rect == 0 || button_index >= model->button_count)
        return status != 0 ? status : REIST_GUI_DIALOG_EINVAL;
    *rect = button_rect_unchecked(model, layout, state, button_index);
    return REIST_GUI_DIALOG_OK;
}

static void reset_result(reist_gui_dialog_result_t *result,
                         uint32_t generation) {
    uint32_t size = result->struct_size;
    *result = (reist_gui_dialog_result_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = size,
        .generation = generation,
    };
}

static reist_gui_rect_t expanded_damage(
    const reist_gui_dialog_layout_t *layout, reist_gui_rect_t rect) {
    int64_t left = (int64_t)rect.x - layout->damage_margin;
    int64_t top = (int64_t)rect.y - layout->damage_margin;
    int64_t right = (int64_t)rect.x + rect.width + layout->damage_margin;
    int64_t bottom = (int64_t)rect.y + rect.height + layout->damage_margin;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > layout->surface_width) right = layout->surface_width;
    if (bottom > layout->surface_height) bottom = layout->surface_height;
    return (reist_gui_rect_t){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top),
    };
}

static void add_damage(const reist_gui_dialog_layout_t *layout,
                       reist_gui_dialog_result_t *result,
                       reist_gui_rect_t rect) {
    if (rect.width == 0U || rect.height == 0U || result->full_redraw) return;
    rect = expanded_damage(layout, rect);
    if (result->damage_count >= REIST_GUI_DIALOG_DAMAGE_CAPACITY) {
        result->damage[0] = (reist_gui_rect_t){
            0, 0, layout->surface_width, layout->surface_height
        };
        result->damage_count = 1U;
        result->full_redraw = 1U;
        return;
    }
    result->damage[result->damage_count++] = rect;
}

static uint32_t first_enabled_button(
    const reist_gui_dialog_model_t *model) {
    for (uint32_t index = 0U; index < model->button_count; ++index) {
        if ((model->buttons[index].flags &
             REIST_GUI_DIALOG_BUTTON_DISABLED) == 0U)
            return index;
    }
    return REIST_GUI_DIALOG_NO_INDEX;
}

int reist_gui_dialog_open(const reist_gui_dialog_model_t *model,
                          const reist_gui_dialog_layout_t *layout,
                          reist_gui_dialog_state_t *state,
                          reist_gui_dialog_result_t *result) {
    int status = validate_layout(layout);
    if (status != 0) return status;
    status = validate_model(model, layout);
    if (status != 0) return status;
    status = validate_state(model, layout, state);
    if (status != 0) return status;
    status = validate_result(result);
    if (status != 0) return status;
    if (state->visible) return REIST_GUI_DIALOG_EBUSY;

    uint32_t generation = state->generation + 1U;
    if (generation == 0U) generation = 1U;
    uint32_t focused = first_enabled_button(model);
    (void)find_response(model, model->default_response, 1U, &focused);
    uint32_t size = state->struct_size;
    *state = (reist_gui_dialog_state_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = size,
        .visible = 1U,
        .active = 1U,
        .generation = generation,
        .modality = model->modality,
        .owner_id = model->owner_id,
        .owner_generation = model->owner_generation,
        .bounds = layout->initial_bounds,
        .focused_button = focused,
        .hot_button = REIST_GUI_DIALOG_NO_INDEX,
        .capture_kind = REIST_GUI_DIALOG_CAPTURE_NONE,
        .capture_button = REIST_GUI_DIALOG_NO_INDEX,
    };
    reset_result(result, generation);
    add_damage(layout, result, state->bounds);
    return REIST_GUI_DIALOG_OK;
}

static uint32_t button_at(const reist_gui_dialog_model_t *model,
                          const reist_gui_dialog_layout_t *layout,
                          const reist_gui_dialog_state_t *state,
                          int32_t x, int32_t y) {
    for (uint32_t index = 0U; index < model->button_count; ++index) {
        if ((model->buttons[index].flags &
             REIST_GUI_DIALOG_BUTTON_DISABLED) == 0U &&
            point_in_rect(button_rect_unchecked(
                model, layout, state, index), x, y))
            return index;
    }
    return REIST_GUI_DIALOG_NO_INDEX;
}

static void set_hot_button(const reist_gui_dialog_layout_t *layout,
                           reist_gui_dialog_state_t *state,
                           uint32_t button,
                           reist_gui_dialog_result_t *result) {
    if (state->hot_button == button) return;
    add_damage(layout, result, state->bounds);
    state->hot_button = button;
}

static void finish_dialog(const reist_gui_dialog_layout_t *layout,
                          reist_gui_dialog_state_t *state,
                          uint32_t response,
                          reist_gui_dialog_result_t *result) {
    add_damage(layout, result, state->bounds);
    state->visible = 0U;
    state->active = 0U;
    state->completed = 1U;
    state->response = response;
    state->focused_button = REIST_GUI_DIALOG_NO_INDEX;
    state->hot_button = REIST_GUI_DIALOG_NO_INDEX;
    state->capture_kind = REIST_GUI_DIALOG_CAPTURE_NONE;
    state->capture_button = REIST_GUI_DIALOG_NO_INDEX;
    state->drag_offset_x = 0;
    state->drag_offset_y = 0;
    result->completed = 1U;
    result->response = response;
    result->generation = state->generation;
}

static uint32_t next_enabled_button(
    const reist_gui_dialog_model_t *model, uint32_t current,
    int32_t direction) {
    uint32_t next = current;
    if (next == REIST_GUI_DIALOG_NO_INDEX)
        next = direction < 0 ? 0U : model->button_count - 1U;
    for (uint32_t attempt = 0U;
         attempt < model->button_count; ++attempt) {
        next = direction < 0
            ? (next + model->button_count - 1U) % model->button_count
            : (next + 1U) % model->button_count;
        if ((model->buttons[next].flags &
             REIST_GUI_DIALOG_BUTTON_DISABLED) == 0U)
            return next;
    }
    return REIST_GUI_DIALOG_NO_INDEX;
}

static void move_dialog(const reist_gui_dialog_layout_t *layout,
                        reist_gui_dialog_state_t *state,
                        int32_t pointer_x, int32_t pointer_y,
                        reist_gui_dialog_result_t *result) {
    int64_t desired_x = (int64_t)pointer_x - state->drag_offset_x;
    int64_t desired_y = (int64_t)pointer_y - state->drag_offset_y;
    int64_t minimum_x = layout->work_area.x;
    int64_t minimum_y = layout->work_area.y;
    int64_t maximum_x = (int64_t)layout->work_area.x +
        layout->work_area.width - state->bounds.width;
    int64_t maximum_y = (int64_t)layout->work_area.y +
        layout->work_area.height - state->bounds.height;
    if (desired_x < minimum_x) desired_x = minimum_x;
    if (desired_y < minimum_y) desired_y = minimum_y;
    if (desired_x > maximum_x) desired_x = maximum_x;
    if (desired_y > maximum_y) desired_y = maximum_y;
    if (state->bounds.x == (int32_t)desired_x &&
        state->bounds.y == (int32_t)desired_y)
        return;
    reist_gui_rect_t previous = state->bounds;
    add_damage(layout, result, previous);
    state->bounds.x = (int32_t)desired_x;
    state->bounds.y = (int32_t)desired_y;
    add_damage(layout, result, state->bounds);
}

static void dispatch_motion(const reist_gui_dialog_model_t *model,
                            const reist_gui_dialog_layout_t *layout,
                            reist_gui_dialog_state_t *state,
                            const reist_gui_dialog_event_t *event,
                            reist_gui_dialog_result_t *result) {
    if (state->capture_kind != REIST_GUI_DIALOG_CAPTURE_NONE) {
        result->consumed = 1U;
        if (state->capture_kind == REIST_GUI_DIALOG_CAPTURE_MOVE)
            move_dialog(layout, state, event->x, event->y, result);
        else if (state->capture_kind == REIST_GUI_DIALOG_CAPTURE_BUTTON)
            set_hot_button(
                layout, state,
                button_at(model, layout, state, event->x, event->y) ==
                        state->capture_button
                    ? state->capture_button : REIST_GUI_DIALOG_NO_INDEX,
                result);
        return;
    }
    if (point_in_rect(state->bounds, event->x, event->y)) {
        result->consumed = 1U;
        set_hot_button(
            layout, state,
            button_at(model, layout, state, event->x, event->y), result);
    } else {
        set_hot_button(
            layout, state, REIST_GUI_DIALOG_NO_INDEX, result);
        result->consumed = model->modality != REIST_GUI_DIALOG_MODELESS;
    }
}

static void dispatch_press(const reist_gui_dialog_model_t *model,
                           const reist_gui_dialog_layout_t *layout,
                           reist_gui_dialog_state_t *state,
                           const reist_gui_dialog_event_t *event,
                           reist_gui_dialog_result_t *result) {
    if (!point_in_rect(state->bounds, event->x, event->y)) {
        if (model->modality == REIST_GUI_DIALOG_MODELESS) {
            if (state->active) add_damage(layout, result, state->bounds);
            state->active = 0U;
            result->consumed = 0U;
        } else {
            result->consumed = 1U;
        }
        return;
    }
    result->consumed = 1U;
    if (!state->active) add_damage(layout, result, state->bounds);
    state->active = 1U;

    reist_gui_rect_t close = {0, 0, 0U, 0U};
    if ((model->flags & REIST_GUI_DIALOG_CLOSE_BUTTON) != 0U) {
        close = close_rect_unchecked(layout, state);
        if (point_in_rect(close, event->x, event->y)) {
            state->capture_kind = REIST_GUI_DIALOG_CAPTURE_CLOSE;
            add_damage(layout, result, state->bounds);
            return;
        }
    }
    uint32_t button = button_at(
        model, layout, state, event->x, event->y);
    if (button != REIST_GUI_DIALOG_NO_INDEX) {
        state->focused_button = button;
        state->hot_button = button;
        state->capture_kind = REIST_GUI_DIALOG_CAPTURE_BUTTON;
        state->capture_button = button;
        add_damage(layout, result, state->bounds);
        return;
    }
    reist_gui_rect_t title = title_rect_unchecked(layout, state);
    if ((model->flags & REIST_GUI_DIALOG_MOVABLE) != 0U &&
        point_in_rect(title, event->x, event->y)) {
        state->capture_kind = REIST_GUI_DIALOG_CAPTURE_MOVE;
        state->drag_offset_x = event->x - state->bounds.x;
        state->drag_offset_y = event->y - state->bounds.y;
        return;
    }
    state->capture_kind = REIST_GUI_DIALOG_CAPTURE_BODY;
}

static void dispatch_release(const reist_gui_dialog_model_t *model,
                             const reist_gui_dialog_layout_t *layout,
                             reist_gui_dialog_state_t *state,
                             const reist_gui_dialog_event_t *event,
                             reist_gui_dialog_result_t *result) {
    uint32_t capture = state->capture_kind;
    uint32_t captured_button = state->capture_button;
    if (capture == REIST_GUI_DIALOG_CAPTURE_NONE) {
        result->consumed = point_in_rect(
            state->bounds, event->x, event->y) ||
            model->modality != REIST_GUI_DIALOG_MODELESS;
        return;
    }
    result->consumed = 1U;
    state->capture_kind = REIST_GUI_DIALOG_CAPTURE_NONE;
    state->capture_button = REIST_GUI_DIALOG_NO_INDEX;
    state->drag_offset_x = 0;
    state->drag_offset_y = 0;
    if (capture == REIST_GUI_DIALOG_CAPTURE_BUTTON &&
        button_at(model, layout, state, event->x, event->y) ==
            captured_button) {
        finish_dialog(
            layout, state, model->buttons[captured_button].response, result);
        return;
    }
    if (capture == REIST_GUI_DIALOG_CAPTURE_CLOSE &&
        point_in_rect(close_rect_unchecked(layout, state),
                      event->x, event->y)) {
        finish_dialog(layout, state, model->cancel_response, result);
        return;
    }
    set_hot_button(
        layout, state,
        point_in_rect(state->bounds, event->x, event->y)
            ? button_at(model, layout, state, event->x, event->y)
            : REIST_GUI_DIALOG_NO_INDEX,
        result);
    if (capture == REIST_GUI_DIALOG_CAPTURE_BUTTON ||
        capture == REIST_GUI_DIALOG_CAPTURE_CLOSE)
        add_damage(layout, result, state->bounds);
}

static void dispatch_key(const reist_gui_dialog_model_t *model,
                         const reist_gui_dialog_layout_t *layout,
                         reist_gui_dialog_state_t *state,
                         const reist_gui_dialog_event_t *event,
                         reist_gui_dialog_result_t *result) {
    if (model->modality == REIST_GUI_DIALOG_MODELESS && !state->active)
        return;
    result->consumed = 1U;
    if (event->key == REIST_GUI_DIALOG_KEY_PREVIOUS ||
        event->key == REIST_GUI_DIALOG_KEY_NEXT) {
        uint32_t next = next_enabled_button(
            model, state->focused_button,
            event->key == REIST_GUI_DIALOG_KEY_PREVIOUS ? -1 : 1);
        if (next != state->focused_button) {
            state->focused_button = next;
            add_damage(layout, result, state->bounds);
        }
        return;
    }
    if (event->key == REIST_GUI_DIALOG_KEY_ESCAPE) {
        if (model->cancel_response != REIST_GUI_DIALOG_RESPONSE_NONE)
            finish_dialog(
                layout, state, model->cancel_response, result);
        return;
    }
    uint32_t button = state->focused_button;
    if (button == REIST_GUI_DIALOG_NO_INDEX)
        (void)find_response(
            model, model->default_response, 1U, &button);
    if (button != REIST_GUI_DIALOG_NO_INDEX)
        finish_dialog(
            layout, state, model->buttons[button].response, result);
}

int reist_gui_dialog_dispatch(const reist_gui_dialog_model_t *model,
                              const reist_gui_dialog_layout_t *layout,
                              reist_gui_dialog_state_t *state,
                              const reist_gui_dialog_event_t *event,
                              reist_gui_dialog_result_t *result) {
    int status = reist_gui_dialog_validate(model, layout, state);
    if (status != 0) return status;
    status = validate_event(event);
    if (status != 0) return status;
    status = validate_result(result);
    if (status != 0) return status;
    reset_result(result, state->generation);
    if (!state->visible) return REIST_GUI_DIALOG_OK;

    if (event->type == REIST_GUI_DIALOG_EVENT_POINTER_MOTION) {
        dispatch_motion(model, layout, state, event, result);
    } else if (event->type == REIST_GUI_DIALOG_EVENT_POINTER_BUTTON) {
        if (event->pressed)
            dispatch_press(model, layout, state, event, result);
        else
            dispatch_release(model, layout, state, event, result);
    } else {
        dispatch_key(model, layout, state, event, result);
    }
    return REIST_GUI_DIALOG_OK;
}

int reist_gui_dialog_complete(const reist_gui_dialog_model_t *model,
                              const reist_gui_dialog_layout_t *layout,
                              reist_gui_dialog_state_t *state,
                              uint32_t response,
                              reist_gui_dialog_result_t *result) {
    int status = reist_gui_dialog_validate(model, layout, state);
    if (status != 0) return status;
    status = validate_result(result);
    if (status != 0) return status;
    if (!state->visible ||
        find_response(model, response, 1U, 0) != 0)
        return REIST_GUI_DIALOG_EINVAL;
    reset_result(result, state->generation);
    finish_dialog(layout, state, response, result);
    return REIST_GUI_DIALOG_OK;
}

int reist_gui_dialog_response(const reist_gui_dialog_state_t *state,
                              uint32_t *response) {
    if (state == 0 || response == 0 ||
        state->version != REIST_GUI_DIALOG_API_VERSION ||
        state->struct_size < sizeof(*state) ||
        !reserved_zero(state->reserved, 4U))
        return REIST_GUI_DIALOG_EINVAL;
    if (!state->completed) return REIST_GUI_DIALOG_EAGAIN;
    *response = state->response;
    return REIST_GUI_DIALOG_OK;
}
