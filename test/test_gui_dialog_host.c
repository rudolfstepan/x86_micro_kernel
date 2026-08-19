#include "reist/gui/dialog.h"

#include <assert.h>

static const reist_gui_dialog_button_t question_buttons[] = {
    {"Ja", REIST_GUI_DIALOG_RESPONSE_YES,
     REIST_GUI_DIALOG_ROLE_ACCEPT, 0U, 0U},
    {"Nein", REIST_GUI_DIALOG_RESPONSE_NO,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
    {"Abbrechen", REIST_GUI_DIALOG_RESPONSE_CANCEL,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

static reist_gui_dialog_model_t model(uint32_t modality) {
    return (reist_gui_dialog_model_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(reist_gui_dialog_model_t),
        .title = "Frage",
        .message = "Aenderungen speichern?",
        .detail = "Die Antwort wird als stabile Response geliefert.",
        .buttons = question_buttons,
        .button_count = 3U,
        .modality = modality,
        .default_response = REIST_GUI_DIALOG_RESPONSE_YES,
        .cancel_response = REIST_GUI_DIALOG_RESPONSE_CANCEL,
        .owner_id = REIST_GUI_DIALOG_NO_OWNER,
        .flags = REIST_GUI_DIALOG_MOVABLE |
                 REIST_GUI_DIALOG_CLOSE_BUTTON,
    };
}

static reist_gui_dialog_layout_t layout(void) {
    return (reist_gui_dialog_layout_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(reist_gui_dialog_layout_t),
        .surface_width = 640U,
        .surface_height = 480U,
        .work_area = {0, 30, 640U, 420U},
        .initial_bounds = {100, 100, 380U, 220U},
        .title_height = 28U,
        .border_width = 3U,
        .font_width = 8U,
        .font_height = 16U,
        .button_min_width = 80U,
        .button_height = 28U,
        .button_gap = 8U,
        .button_padding_x = 8U,
        .content_padding = 12U,
        .damage_margin = 4U,
    };
}

static reist_gui_dialog_result_t dispatch(
    const reist_gui_dialog_model_t *dialog_model,
    const reist_gui_dialog_layout_t *dialog_layout,
    reist_gui_dialog_state_t *state, uint32_t type,
    int32_t x, int32_t y, uint32_t pressed, uint32_t key) {
    reist_gui_dialog_event_t event;
    reist_gui_dialog_event_initialize(&event);
    event.type = type;
    event.x = x;
    event.y = y;
    event.button = type == REIST_GUI_DIALOG_EVENT_POINTER_BUTTON
        ? REIST_GUI_DIALOG_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    event.key = key;
    reist_gui_dialog_result_t result;
    reist_gui_dialog_result_initialize(&result);
    assert(reist_gui_dialog_dispatch(
               dialog_model, dialog_layout, state, &event, &result) == 0);
    assert(result.damage_count <= REIST_GUI_DIALOG_DAMAGE_CAPACITY);
    return result;
}

static void open_dialog(const reist_gui_dialog_model_t *dialog_model,
                        const reist_gui_dialog_layout_t *dialog_layout,
                        reist_gui_dialog_state_t *state) {
    reist_gui_dialog_result_t result;
    reist_gui_dialog_result_initialize(&result);
    assert(reist_gui_dialog_open(
               dialog_model, dialog_layout, state, &result) == 0);
    assert(state->visible && state->active && state->generation != 0U);
    assert(result.damage_count == 1U && !result.completed);
}

int main(void) {
    reist_gui_dialog_model_t modal = model(
        REIST_GUI_DIALOG_APPLICATION_MODAL);
    reist_gui_dialog_layout_t metrics = layout();
    reist_gui_dialog_state_t state;
    reist_gui_dialog_state_initialize(&state);
    assert(reist_gui_dialog_validate(&modal, &metrics, &state) == 0);

    reist_gui_dialog_model_t invalid = model(
        REIST_GUI_DIALOG_WINDOW_MODAL);
    assert(reist_gui_dialog_validate(&invalid, &metrics, &state) ==
           REIST_GUI_DIALOG_EINVAL);
    invalid.owner_id = 7U;
    invalid.owner_generation = 3U;
    assert(reist_gui_dialog_validate(&invalid, &metrics, &state) == 0);

    open_dialog(&modal, &metrics, &state);
    assert(state.focused_button == 0U);
    uint32_t first_generation = state.generation;
    reist_gui_dialog_result_t busy;
    reist_gui_dialog_result_initialize(&busy);
    assert(reist_gui_dialog_open(&modal, &metrics, &state, &busy) ==
           REIST_GUI_DIALOG_EBUSY);

    reist_gui_rect_t frame;
    reist_gui_rect_t title;
    reist_gui_rect_t close;
    reist_gui_rect_t yes;
    assert(reist_gui_dialog_frame_rect(
               &modal, &metrics, &state, &frame) == 0);
    assert(reist_gui_dialog_title_rect(
               &modal, &metrics, &state, &title) == 0);
    assert(reist_gui_dialog_close_rect(
               &modal, &metrics, &state, &close) == 0);
    assert(reist_gui_dialog_button_rect(
               &modal, &metrics, &state, 0U, &yes) == 0);

    /* Title capture remains active outside the original frame and clamps the
     * complete dialog to the work area. */
    (void)dispatch(&modal, &metrics, &state,
                   REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
                   close.x + (int32_t)close.width + 10,
                   title.y + 4, 1U, 0U);
    assert(state.capture_kind == REIST_GUI_DIALOG_CAPTURE_MOVE);
    reist_gui_dialog_result_t moved = dispatch(
        &modal, &metrics, &state, REIST_GUI_DIALOG_EVENT_POINTER_MOTION,
        10000, 10000, 0U, 0U);
    assert(moved.consumed && moved.damage_count == 2U);
    assert(state.bounds.x == 260 && state.bounds.y == 230);
    (void)dispatch(&modal, &metrics, &state,
                   REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
                   10000, 10000, 0U, 0U);
    assert(state.capture_kind == REIST_GUI_DIALOG_CAPTURE_NONE);

    /* Modal outside input is inert and never leaks to an underlying window. */
    reist_gui_dialog_result_t outside = dispatch(
        &modal, &metrics, &state,
        REIST_GUI_DIALOG_EVENT_POINTER_BUTTON, 1, 1, 1U, 0U);
    assert(outside.consumed && state.visible);

    assert(reist_gui_dialog_button_rect(
               &modal, &metrics, &state, 0U, &yes) == 0);
    (void)dispatch(&modal, &metrics, &state,
                   REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
                   yes.x + 2, yes.y + 2, 1U, 0U);
    reist_gui_dialog_result_t answer = dispatch(
        &modal, &metrics, &state,
        REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
        yes.x + 2, yes.y + 2, 0U, 0U);
    assert(answer.completed &&
           answer.response == REIST_GUI_DIALOG_RESPONSE_YES);
    assert(!state.visible && state.completed);
    uint32_t response = 0U;
    assert(reist_gui_dialog_response(&state, &response) == 0);
    assert(response == REIST_GUI_DIALOG_RESPONSE_YES);

    /* Reopen is a new generation; keyboard focus wraps and Escape returns the
     * explicitly configured cancel response. */
    open_dialog(&modal, &metrics, &state);
    assert(state.generation != first_generation);
    (void)dispatch(&modal, &metrics, &state,
                   REIST_GUI_DIALOG_EVENT_KEYBOARD,
                   0, 0, 0U, REIST_GUI_DIALOG_KEY_PREVIOUS);
    assert(state.focused_button == 2U);
    answer = dispatch(&modal, &metrics, &state,
                      REIST_GUI_DIALOG_EVENT_KEYBOARD,
                      0, 0, 0U, REIST_GUI_DIALOG_KEY_ESCAPE);
    assert(answer.completed &&
           answer.response == REIST_GUI_DIALOG_RESPONSE_CANCEL);

    /* A modeless dialog relinquishes focus and passes outside pointer and
     * keyboard input through until clicked again. */
    reist_gui_dialog_model_t modeless = model(REIST_GUI_DIALOG_MODELESS);
    open_dialog(&modeless, &metrics, &state);
    outside = dispatch(&modeless, &metrics, &state,
                       REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
                       1, 1, 1U, 0U);
    assert(!outside.consumed && !state.active && state.visible);
    answer = dispatch(&modeless, &metrics, &state,
                      REIST_GUI_DIALOG_EVENT_KEYBOARD,
                      0, 0, 0U, REIST_GUI_DIALOG_KEY_ESCAPE);
    assert(!answer.consumed && !answer.completed);
    frame = state.bounds;
    (void)dispatch(&modeless, &metrics, &state,
                   REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
                   frame.x + 20, frame.y + 60, 1U, 0U);
    (void)dispatch(&modeless, &metrics, &state,
                   REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
                   frame.x + 20, frame.y + 60, 0U, 0U);
    assert(state.active && state.visible);
    answer = dispatch(&modeless, &metrics, &state,
                      REIST_GUI_DIALOG_EVENT_KEYBOARD,
                      0, 0, 0U, REIST_GUI_DIALOG_KEY_ESCAPE);
    assert(answer.completed &&
           answer.response == REIST_GUI_DIALOG_RESPONSE_CANCEL);

    /* Programmatic completion uses the same typed, once-only result path. */
    open_dialog(&modal, &metrics, &state);
    reist_gui_dialog_result_t completed;
    reist_gui_dialog_result_initialize(&completed);
    assert(reist_gui_dialog_complete(
               &modal, &metrics, &state,
               REIST_GUI_DIALOG_RESPONSE_NO, &completed) == 0);
    assert(completed.completed &&
           completed.response == REIST_GUI_DIALOG_RESPONSE_NO);
    assert(reist_gui_dialog_response(&state, &response) == 0 &&
           response == REIST_GUI_DIALOG_RESPONSE_NO);
    return 0;
}
