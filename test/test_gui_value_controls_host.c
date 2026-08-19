#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "reist/gui/value_controls.h"

static void focus_event(reist_gui_value_event_t *event, uint32_t focused) {
    reist_gui_value_event_initialize(event);
    event->type = REIST_GUI_VALUE_EVENT_FOCUS;
    event->focused = focused;
}

static void key_event(reist_gui_value_event_t *event, uint32_t key) {
    reist_gui_value_event_initialize(event);
    event->type = REIST_GUI_VALUE_EVENT_KEYBOARD;
    event->key = key;
}

static void test_text_editing_is_bounded(void) {
    const reist_gui_text_model_t model = {
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_text_model_t),
        1U, "Name", {10, 10, 120U, 22U}, 8U, 8U,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}
    };
    reist_gui_text_state_t state;
    reist_gui_value_result_t result;
    reist_gui_value_event_t event;
    reist_gui_text_state_initialize(&state);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_text_configure(&model, &state, "abc", &result) == 0);
    focus_event(&event, 1U);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_text_dispatch(&model, &state, &event, &result) == 0);
    key_event(&event, REIST_GUI_VALUE_KEY_LEFT);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_text_dispatch(&model, &state, &event, &result) == 0);
    reist_gui_value_event_initialize(&event);
    event.type = REIST_GUI_VALUE_EVENT_TEXT;
    event.codepoint = 'X';
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_text_dispatch(&model, &state, &event, &result) == 0);
    assert(strcmp(state.text, "abXc") == 0 && result.changed);
    key_event(&event, REIST_GUI_VALUE_KEY_BACKSPACE);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_text_dispatch(&model, &state, &event, &result) == 0);
    assert(strcmp(state.text, "abc") == 0);
}

static const reist_gui_list_item_t items[] = {
    {10U, "Alpha", REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
     {0U, 0U}},
    {11U, "Beta", REIST_GUI_VALUE_VISIBLE, {0U, 0U}},
    {12U, "Gamma", REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
     {0U, 0U}},
};

static void test_list_skips_disabled_items(void) {
    const reist_gui_list_model_t model = {
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_list_model_t),
        2U, "Auswahl", items, 3U, {0, 0, 160U, 60U}, 20U,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}
    };
    reist_gui_list_state_t state;
    reist_gui_value_result_t result;
    reist_gui_value_event_t event;
    reist_gui_list_state_initialize(&state);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_list_configure(&model, &state, 10U, &result) == 0);
    focus_event(&event, 1U);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_list_dispatch(&model, &state, &event, &result) == 0);
    key_event(&event, REIST_GUI_VALUE_KEY_DOWN);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_list_dispatch(&model, &state, &event, &result) == 0);
    assert(state.selected == 2U && result.selected_id == 12U);
}

static void test_ranges_clamp_steps_and_progress_is_inert(void) {
    const reist_gui_range_model_t slider = {
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_range_model_t),
        3U, "Lautstaerke", {0, 0, 101U, 20U}, 0, 100, 5U, 20U,
        REIST_GUI_RANGE_SLIDER, REIST_GUI_HORIZONTAL,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}
    };
    reist_gui_range_state_t state;
    reist_gui_value_result_t result;
    reist_gui_value_event_t event;
    reist_gui_range_state_initialize(&state);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_configure(&slider, &state, 95, &result) == 0);
    focus_event(&event, 1U);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_dispatch(&slider, &state, &event, &result) == 0);
    key_event(&event, REIST_GUI_VALUE_KEY_RIGHT);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_dispatch(&slider, &state, &event, &result) == 0);
    assert(state.value == 100);
    key_event(&event, REIST_GUI_VALUE_KEY_PAGE_DOWN);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_dispatch(&slider, &state, &event, &result) == 0);
    assert(state.value == 80);

    reist_gui_range_model_t progress = slider;
    progress.id = 4U;
    progress.name = "Fortschritt";
    progress.role = REIST_GUI_RANGE_PROGRESS;
    progress.flags |= REIST_GUI_VALUE_READ_ONLY;
    reist_gui_range_state_initialize(&state);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_configure(&progress, &state, 40, &result) == 0);
    key_event(&event, REIST_GUI_VALUE_KEY_RIGHT);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_dispatch(&progress, &state, &event, &result) == 0);
    assert(!result.consumed && state.value == 40);

    reist_gui_range_model_t extreme = slider;
    extreme.minimum = INT32_MIN;
    extreme.maximum = INT32_MAX;
    reist_gui_range_state_initialize(&state);
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_configure(&extreme, &state, 0, &result) == 0);
    reist_gui_value_event_initialize(&event);
    event.type = REIST_GUI_VALUE_EVENT_POINTER_BUTTON;
    event.x = 100;
    event.y = 10;
    event.button = REIST_GUI_VALUE_BUTTON_LEFT;
    event.pressed = 1U;
    reist_gui_value_result_initialize(&result);
    assert(reist_gui_range_dispatch(&extreme, &state, &event, &result) == 0);
    assert(state.value == INT32_MAX);
}

int main(void) {
    test_text_editing_is_bounded();
    test_list_skips_disabled_items();
    test_ranges_clamp_steps_and_progress_is_inert();
    return 0;
}
