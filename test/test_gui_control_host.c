#include <reist/gui/control.h>

#include <assert.h>

enum {
    ID_LABEL = 1U,
    ID_BUTTON,
    ID_CHECK,
    ID_RADIO_A,
    ID_RADIO_B,
    ID_DISABLED
};

static const reist_gui_control_t controls[] = {
    {ID_LABEL, REIST_GUI_CONTROL_ROLE_LABEL, "Optionen",
     {10, 10, 120U, 20U}, 0U, 0U, REIST_GUI_CONTROL_VISIBLE,
     REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}},
    {ID_BUTTON, REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Anwenden",
     {10, 40, 100U, 30U}, 101U, 0U,
     REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED |
         REIST_GUI_CONTROL_DEFAULT,
     REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}},
    {ID_CHECK, REIST_GUI_CONTROL_ROLE_CHECKBOX, "Aktiv",
     {10, 80, 140U, 24U}, 102U, 0U,
     REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED |
         REIST_GUI_CONTROL_TRISTATE,
     REIST_GUI_CONTROL_MIXED, {0U, 0U}},
    {ID_RADIO_A, REIST_GUI_CONTROL_ROLE_RADIO_BUTTON, "Modus A",
     {10, 112, 140U, 24U}, 103U, 7U,
     REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED,
     REIST_GUI_CONTROL_CHECKED, {0U, 0U}},
    {ID_RADIO_B, REIST_GUI_CONTROL_ROLE_RADIO_BUTTON, "Modus B",
     {10, 140, 140U, 24U}, 104U, 7U,
     REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED,
     REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}},
    {ID_DISABLED, REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Gesperrt",
     {10, 172, 100U, 30U}, 105U, 0U, REIST_GUI_CONTROL_VISIBLE,
     REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}},
};

static reist_gui_control_model_t model(void) {
    reist_gui_control_model_t value = {
        .version = REIST_GUI_CONTROL_API_VERSION,
        .struct_size = sizeof(reist_gui_control_model_t),
        .controls = controls,
        .control_count = sizeof(controls) / sizeof(controls[0]),
        .surface_width = 320U,
        .surface_height = 240U,
        .damage_margin = 3U,
    };
    return value;
}

static reist_gui_control_result_t dispatch(
    const reist_gui_control_model_t *control_model,
    reist_gui_control_state_t *state, uint32_t type,
    int32_t x, int32_t y, uint32_t pressed, uint32_t key) {
    reist_gui_control_event_t event;
    reist_gui_control_event_initialize(&event);
    event.type = type;
    event.x = x;
    event.y = y;
    event.button = type == REIST_GUI_CONTROL_EVENT_POINTER_BUTTON
        ? REIST_GUI_CONTROL_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    event.key = key;
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    assert(reist_gui_control_dispatch(
               control_model, state, &event, &result) == 0);
    assert(result.damage_count <= REIST_GUI_CONTROL_DAMAGE_CAPACITY);
    return result;
}

int main(void) {
    reist_gui_control_model_t control_model = model();
    reist_gui_control_state_t state;
    reist_gui_control_state_initialize(&state);
    assert(reist_gui_control_validate(&control_model, &state) == 0);

    /* Mixed requires an explicit tri-state descriptor. */
    reist_gui_control_t invalid_controls[6];
    for (uint32_t index = 0U; index < 6U; ++index)
        invalid_controls[index] = controls[index];
    reist_gui_control_model_t invalid = control_model;
    invalid.controls = invalid_controls;
    invalid_controls[2].flags &= ~REIST_GUI_CONTROL_TRISTATE;
    assert(reist_gui_control_validate(&invalid, &state) ==
           REIST_GUI_CONTROL_EINVAL);
    invalid_controls[2].flags |= REIST_GUI_CONTROL_TRISTATE;
    assert(reist_gui_control_validate(&invalid, &state) == 0);
    invalid_controls[4].initial_check = REIST_GUI_CONTROL_CHECKED;
    assert(reist_gui_control_validate(&invalid, &state) ==
           REIST_GUI_CONTROL_EINVAL);
    invalid_controls[4].initial_check = REIST_GUI_CONTROL_UNCHECKED;
    invalid_controls[0].bounds.width = 400U;
    assert(reist_gui_control_validate(&invalid, &state) ==
           REIST_GUI_CONTROL_EOVERFLOW);
    invalid_controls[0].bounds.width = controls[0].bounds.width;
    control_model = invalid;

    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    assert(reist_gui_control_configure(
               &control_model, &state, &result) == 0);
    assert(state.configured && state.control_count == 6U);
    assert(state.check[2] == REIST_GUI_CONTROL_MIXED);
    assert(state.check[3] == REIST_GUI_CONTROL_CHECKED);
    assert(result.damage_count == 1U);

    /* Tab enters the default push button and treats a radio group as one stop. */
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_NEXT);
    assert(result.focus_changed && result.focused_id == ID_BUTTON);
    reist_gui_control_event_t rejected_event;
    reist_gui_control_event_initialize(&rejected_event);
    rejected_event.type = REIST_GUI_CONTROL_EVENT_KEYBOARD;
    rejected_event.key = REIST_GUI_CONTROL_KEY_NEXT;
    reist_gui_control_result_t rejected_result;
    reist_gui_control_result_initialize(&rejected_result);
    rejected_result.consumed = 1U;
    assert(reist_gui_control_dispatch(
               &control_model, &state, &rejected_event,
               &rejected_result) == REIST_GUI_CONTROL_EINVAL);
    assert(state.focused == 1U);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_NEXT);
    assert(result.focused_id == ID_CHECK);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_NEXT);
    assert(result.focused_id == ID_RADIO_A);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_NEXT);
    assert(result.focused_id == ID_BUTTON);

    /* Enter activates a push button; Space toggles mixed to checked. */
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_ENTER);
    assert(result.activated && result.control_id == ID_BUTTON &&
           result.action == 101U);
    reist_gui_control_result_initialize(&result);
    assert(reist_gui_control_focus(
               &control_model, &state, ID_CHECK,
               REIST_GUI_CONTROL_FOCUS_PROGRAMMATIC, &result) == 0);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_SPACE);
    assert(result.activated && result.value_changed &&
           result.control_id == ID_CHECK &&
           result.check_state == REIST_GUI_CONTROL_CHECKED);

    /* Pointer capture survives leaving, but release outside does not click. */
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_POINTER_BUTTON, 20, 50, 1U, 0U);
    assert(result.consumed && state.captured == 1U && state.armed);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_POINTER_MOTION, 300, 220, 0U, 0U);
    assert(result.consumed && !state.armed);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_POINTER_BUTTON, 300, 220, 0U, 0U);
    assert(result.consumed && !result.activated &&
           state.captured == REIST_GUI_CONTROL_NO_INDEX);

    /* A complete click activates, while disabled controls remain inert. */
    (void)dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_POINTER_BUTTON, 20, 50, 1U, 0U);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_POINTER_BUTTON, 20, 50, 0U, 0U);
    assert(result.activated && result.control_id == ID_BUTTON);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_POINTER_BUTTON, 20, 180, 1U, 0U);
    assert(!result.consumed && state.captured == REIST_GUI_CONTROL_NO_INDEX);

    /* Radio arrows move focus, select exactly one member and wrap. */
    reist_gui_control_result_initialize(&result);
    assert(reist_gui_control_focus(
               &control_model, &state, ID_RADIO_A,
               REIST_GUI_CONTROL_FOCUS_PROGRAMMATIC, &result) == 0);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_RIGHT);
    assert(result.activated && result.value_changed &&
           result.focused_id == ID_RADIO_B &&
           state.check[3] == REIST_GUI_CONTROL_UNCHECKED &&
           state.check[4] == REIST_GUI_CONTROL_CHECKED);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_CONTROL_KEY_RIGHT);
    assert(result.focused_id == ID_RADIO_A &&
           state.check[3] == REIST_GUI_CONTROL_CHECKED &&
           state.check[4] == REIST_GUI_CONTROL_UNCHECKED);

    /* Programmatic state maintains the same exclusive-group invariant. */
    reist_gui_control_result_initialize(&result);
    assert(reist_gui_control_set_check(
               &control_model, &state, ID_RADIO_B,
               REIST_GUI_CONTROL_CHECKED, &result) == 0);
    assert(result.value_changed && state.check[3] == 0U &&
           state.check[4] == REIST_GUI_CONTROL_CHECKED);

    /* Cancel always releases a grab without activation. */
    (void)dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_POINTER_BUTTON, 20, 50, 1U, 0U);
    result = dispatch(&control_model, &state,
        REIST_GUI_CONTROL_EVENT_CANCEL, 0, 0, 0U, 0U);
    assert(result.consumed && state.captured == REIST_GUI_CONTROL_NO_INDEX);
    return 0;
}
