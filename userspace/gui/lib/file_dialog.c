/** @file file_dialog.c @brief Renderer-neutral bounded file dialog. */
#include "reist/gui/file_dialog.h"

static void zero_bytes(void *memory, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (uint32_t i = 0U; i < size; ++i) bytes[i] = 0U;
}

static void copy_bytes(void *destination, const void *source, uint32_t size) {
    volatile uint8_t *output = (volatile uint8_t *)destination;
    const volatile uint8_t *input = (const volatile uint8_t *)source;
    for (uint32_t i = 0U; i < size; ++i) output[i] = input[i];
}

static uint32_t reserved_zero(const uint32_t *values) {
    for (uint32_t i = 0U; i < 4U; ++i)
        if (values[i] != 0U) return 0U;
    return 1U;
}

static uint32_t text_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static uint32_t valid_rect(reist_gui_rect_t rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.width != 0U &&
        rect.height != 0U && rect.width <= INT32_MAX &&
        rect.height <= INT32_MAX &&
        rect.x <= INT32_MAX - (int32_t)rect.width &&
        rect.y <= INT32_MAX - (int32_t)rect.height;
}

static uint32_t contains(reist_gui_rect_t rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y &&
        x < rect.x + (int32_t)rect.width &&
        y < rect.y + (int32_t)rect.height;
}

static uint32_t copy_path(char *destination, const char *source) {
    uint32_t length = text_length(
        source, REIST_GUI_FILE_DIALOG_PATH_CAPACITY);
    if (length == 0U || length >= REIST_GUI_FILE_DIALOG_PATH_CAPACITY ||
        source[0] != '/') return UINT32_MAX;
    for (uint32_t i = 0U; i <= length; ++i) destination[i] = source[i];
    return length;
}

void reist_gui_file_dialog_state_initialize(
    reist_gui_file_dialog_state_t *state) {
    if (state == 0) return;
    zero_bytes(state, sizeof(*state));
    state->version = REIST_GUI_FILE_DIALOG_API_VERSION;
    state->struct_size = sizeof(*state);
    state->focus = REIST_GUI_FILE_DIALOG_FOCUS_PATH;
}

void reist_gui_file_dialog_event_initialize(
    reist_gui_file_dialog_event_t *event) {
    if (event == 0) return;
    zero_bytes(event, sizeof(*event));
    event->version = REIST_GUI_FILE_DIALOG_API_VERSION;
    event->struct_size = sizeof(*event);
}

void reist_gui_file_dialog_result_initialize(
    reist_gui_file_dialog_result_t *result) {
    if (result == 0) return;
    zero_bytes(result, sizeof(*result));
    result->version = REIST_GUI_FILE_DIALOG_API_VERSION;
    result->struct_size = sizeof(*result);
}

int reist_gui_file_dialog_validate(
    const reist_gui_file_dialog_model_t *model,
    const reist_gui_file_dialog_layout_t *layout,
    const reist_gui_file_dialog_state_t *state) {
    if (model == 0 || layout == 0 || state == 0 ||
        model->version != REIST_GUI_FILE_DIALOG_API_VERSION ||
        model->struct_size != sizeof(*model) ||
        (model->mode != REIST_GUI_FILE_DIALOG_OPEN &&
         model->mode != REIST_GUI_FILE_DIALOG_SAVE) ||
        text_length(model->title, 64U) == 0U ||
        text_length(model->title, 64U) >= 64U ||
        text_length(model->accept_label, 32U) == 0U ||
        text_length(model->accept_label, 32U) >= 32U ||
        !reserved_zero(model->reserved) ||
        layout->version != REIST_GUI_FILE_DIALOG_API_VERSION ||
        layout->struct_size != sizeof(*layout) || layout->glyph_width == 0U ||
        !valid_rect(layout->frame) || !valid_rect(layout->title) ||
        !valid_rect(layout->path) || !valid_rect(layout->accept_button) ||
        !valid_rect(layout->cancel_button) ||
        !reserved_zero(layout->reserved) ||
        state->version != REIST_GUI_FILE_DIALOG_API_VERSION ||
        state->struct_size != sizeof(*state) || state->visible > 1U ||
        state->focus < REIST_GUI_FILE_DIALOG_FOCUS_PATH ||
        state->focus > REIST_GUI_FILE_DIALOG_FOCUS_CANCEL ||
        state->capture > REIST_GUI_FILE_DIALOG_FOCUS_CANCEL ||
        state->length >= REIST_GUI_FILE_DIALOG_PATH_CAPACITY ||
        state->cursor > state->length || state->path[state->length] != '\0' ||
        !reserved_zero(state->reserved)) return REIST_GUI_FILE_DIALOG_EINVAL;
    return REIST_GUI_FILE_DIALOG_OK;
}

int reist_gui_file_dialog_open(
    const reist_gui_file_dialog_model_t *model,
    const reist_gui_file_dialog_layout_t *layout,
    reist_gui_file_dialog_state_t *state, const char *initial_path,
    reist_gui_file_dialog_result_t *result) {
    if (state == 0 || result == 0 ||
        result->version != REIST_GUI_FILE_DIALOG_API_VERSION ||
        result->struct_size != sizeof(*result) ||
        !reserved_zero(result->reserved)) return REIST_GUI_FILE_DIALOG_EINVAL;
    reist_gui_file_dialog_state_t next;
    reist_gui_file_dialog_state_initialize(&next);
    uint32_t length = copy_path(next.path, initial_path);
    if (length == UINT32_MAX) return REIST_GUI_FILE_DIALOG_ECAPACITY;
    next.length = length;
    next.cursor = length;
    next.visible = 1U;
    if (reist_gui_file_dialog_validate(model, layout, &next) != 0)
        return REIST_GUI_FILE_DIALOG_EINVAL;
    copy_bytes(state, &next, sizeof(next));
    result->consumed = 1U;
    result->full_redraw = 1U;
    return REIST_GUI_FILE_DIALOG_OK;
}

static void complete(reist_gui_file_dialog_state_t *state,
                     reist_gui_file_dialog_result_t *result,
                     uint32_t response) {
    state->visible = 0U;
    state->capture = 0U;
    result->consumed = 1U;
    result->completed = 1U;
    result->response = response;
    result->full_redraw = 1U;
    if (response == REIST_GUI_FILE_DIALOG_RESPONSE_ACCEPT)
        for (uint32_t i = 0U; i <= state->length; ++i)
            result->path[i] = state->path[i];
}

static int dispatch_validated(
    const reist_gui_file_dialog_layout_t *layout,
    reist_gui_file_dialog_state_t *state,
    const reist_gui_file_dialog_event_t *event,
    reist_gui_file_dialog_result_t *result) {
    result->consumed = 1U;
    if (event->type == REIST_GUI_FILE_DIALOG_EVENT_POINTER_MOTION) return 0;
    if (event->type == REIST_GUI_FILE_DIALOG_EVENT_POINTER_BUTTON) {
        if (event->button != 1U || event->pressed > 1U) return -1;
        uint32_t hit = contains(layout->path, event->x, event->y)
            ? REIST_GUI_FILE_DIALOG_FOCUS_PATH
            : contains(layout->accept_button, event->x, event->y)
                ? REIST_GUI_FILE_DIALOG_FOCUS_ACCEPT
                : contains(layout->cancel_button, event->x, event->y)
                    ? REIST_GUI_FILE_DIALOG_FOCUS_CANCEL : 0U;
        if (event->pressed) {
            state->capture = hit;
            if (hit != 0U) state->focus = hit;
        } else {
            uint32_t captured = state->capture;
            state->capture = 0U;
            if (captured == hit && hit == REIST_GUI_FILE_DIALOG_FOCUS_ACCEPT &&
                state->length != 0U)
                complete(state, result, REIST_GUI_FILE_DIALOG_RESPONSE_ACCEPT);
            else if (captured == hit &&
                     hit == REIST_GUI_FILE_DIALOG_FOCUS_CANCEL)
                complete(state, result, REIST_GUI_FILE_DIALOG_RESPONSE_CANCEL);
        }
        result->full_redraw = 1U;
        return 0;
    }
    if (event->type == REIST_GUI_FILE_DIALOG_EVENT_TEXT) {
        if (state->focus != REIST_GUI_FILE_DIALOG_FOCUS_PATH ||
            event->codepoint < 0x20U || event->codepoint > 0x7eU)
            return 0;
        if (state->length + 1U >= REIST_GUI_FILE_DIALOG_PATH_CAPACITY)
            return REIST_GUI_FILE_DIALOG_ECAPACITY;
        for (uint32_t i = state->length + 1U; i > state->cursor; --i)
            state->path[i] = state->path[i - 1U];
        state->path[state->cursor++] = (char)event->codepoint;
        ++state->length;
        result->full_redraw = 1U;
        return 0;
    }
    if (event->type != REIST_GUI_FILE_DIALOG_EVENT_KEYBOARD) return -1;
    if (event->key == REIST_GUI_FILE_DIALOG_KEY_ESCAPE) {
        complete(state, result, REIST_GUI_FILE_DIALOG_RESPONSE_CANCEL);
    } else if (event->key == REIST_GUI_FILE_DIALOG_KEY_TAB) {
        state->focus = state->focus == REIST_GUI_FILE_DIALOG_FOCUS_CANCEL
            ? REIST_GUI_FILE_DIALOG_FOCUS_PATH : state->focus + 1U;
        result->full_redraw = 1U;
    } else if (event->key == REIST_GUI_FILE_DIALOG_KEY_ENTER) {
        if (state->focus == REIST_GUI_FILE_DIALOG_FOCUS_CANCEL)
            complete(state, result, REIST_GUI_FILE_DIALOG_RESPONSE_CANCEL);
        else if (state->length != 0U)
            complete(state, result, REIST_GUI_FILE_DIALOG_RESPONSE_ACCEPT);
    } else if (state->focus == REIST_GUI_FILE_DIALOG_FOCUS_PATH) {
        if (event->key == REIST_GUI_FILE_DIALOG_KEY_LEFT && state->cursor)
            --state->cursor;
        else if (event->key == REIST_GUI_FILE_DIALOG_KEY_RIGHT &&
                 state->cursor < state->length) ++state->cursor;
        else if (event->key == REIST_GUI_FILE_DIALOG_KEY_HOME) state->cursor = 0U;
        else if (event->key == REIST_GUI_FILE_DIALOG_KEY_END)
            state->cursor = state->length;
        else if (event->key == REIST_GUI_FILE_DIALOG_KEY_BACKSPACE &&
                 state->cursor != 0U) {
            for (uint32_t i = state->cursor - 1U; i < state->length; ++i)
                state->path[i] = state->path[i + 1U];
            --state->cursor;
            --state->length;
        } else if (event->key == REIST_GUI_FILE_DIALOG_KEY_DELETE &&
                   state->cursor < state->length) {
            for (uint32_t i = state->cursor; i < state->length; ++i)
                state->path[i] = state->path[i + 1U];
            --state->length;
        }
        result->full_redraw = 1U;
    }
    return 0;
}

int reist_gui_file_dialog_dispatch(
    const reist_gui_file_dialog_model_t *model,
    const reist_gui_file_dialog_layout_t *layout,
    reist_gui_file_dialog_state_t *state,
    const reist_gui_file_dialog_event_t *event,
    reist_gui_file_dialog_result_t *result) {
    if (event == 0 || result == 0 ||
        event->version != REIST_GUI_FILE_DIALOG_API_VERSION ||
        event->struct_size != sizeof(*event) ||
        !reserved_zero(event->reserved) ||
        result->version != REIST_GUI_FILE_DIALOG_API_VERSION ||
        result->struct_size != sizeof(*result) ||
        !reserved_zero(result->reserved) ||
        reist_gui_file_dialog_validate(model, layout, state) != 0 ||
        !state->visible) return REIST_GUI_FILE_DIALOG_EINVAL;
    reist_gui_file_dialog_state_t next;
    copy_bytes(&next, state, sizeof(next));
    int status = dispatch_validated(layout, &next, event, result);
    if (status != 0) return status;
    if (reist_gui_file_dialog_validate(model, layout, &next) != 0)
        return REIST_GUI_FILE_DIALOG_EINVAL;
    copy_bytes(state, &next, sizeof(next));
    return REIST_GUI_FILE_DIALOG_OK;
}
