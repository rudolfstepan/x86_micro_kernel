#include <assert.h>
#include <stdint.h>

#include "reist/gui/tabs.h"

static const reist_gui_tab_t tabs[] = {
    {1U, 11U, "Basis", 60U,
     REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}},
    {2U, 12U, "Eingabe", 70U,
     REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}},
    {3U, 13U, "Auswahl", 70U,
     REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}},
};

static const reist_gui_tabs_model_t model = {
    REIST_GUI_TABS_API_VERSION, sizeof(reist_gui_tabs_model_t),
    tabs, 3U, {10, 10, 220U, 24U}, {10, 34, 220U, 140U}, 2U,
    {0U, 0U, 0U, 0U}
};

static void keyboard(reist_gui_tabs_state_t *state, uint32_t key,
                     reist_gui_tabs_result_t *result) {
    reist_gui_tabs_event_t event;
    reist_gui_tabs_event_initialize(&event);
    event.type = REIST_GUI_TABS_EVENT_KEYBOARD;
    event.key = key;
    reist_gui_tabs_result_initialize(result);
    assert(reist_gui_tabs_dispatch(&model, state, &event, result) == 0);
}

static void test_keyboard_and_programmatic_selection(void) {
    reist_gui_tabs_state_t state;
    reist_gui_tabs_result_t result;
    reist_gui_tabs_state_initialize(&state);
    reist_gui_tabs_result_initialize(&result);
    assert(reist_gui_tabs_configure(&model, &state, 1U, &result) == 0);
    assert(state.selected == 0U && result.page_id == 11U);

    keyboard(&state, REIST_GUI_TABS_KEY_RIGHT, &result);
    assert(result.consumed && result.selection_changed);
    assert(state.selected == 1U && result.page_id == 12U);
    keyboard(&state, REIST_GUI_TABS_KEY_LEFT, &result);
    assert(state.selected == 0U);
    keyboard(&state, REIST_GUI_TABS_KEY_LEFT, &result);
    assert(state.selected == 2U && result.page_id == 13U);
    keyboard(&state, REIST_GUI_TABS_KEY_HOME, &result);
    assert(state.selected == 0U);
    keyboard(&state, REIST_GUI_TABS_KEY_END, &result);
    assert(state.selected == 2U);

    reist_gui_tabs_result_initialize(&result);
    assert(reist_gui_tabs_select(&model, &state, 2U, &result) == 0);
    assert(state.selected == 1U && result.selection_changed);
    uint32_t tab_id = 0U;
    uint32_t page_id = 0U;
    assert(reist_gui_tabs_selected(
        &model, &state, &tab_id, &page_id) == 0);
    assert(tab_id == 2U && page_id == 12U);
}

static void pointer(reist_gui_tabs_state_t *state, int32_t x, int32_t y,
                    uint32_t pressed, reist_gui_tabs_result_t *result) {
    reist_gui_tabs_event_t event;
    reist_gui_tabs_event_initialize(&event);
    event.type = REIST_GUI_TABS_EVENT_POINTER_BUTTON;
    event.x = x;
    event.y = y;
    event.button = REIST_GUI_TABS_BUTTON_LEFT;
    event.pressed = pressed;
    reist_gui_tabs_result_initialize(result);
    assert(reist_gui_tabs_dispatch(&model, state, &event, result) == 0);
}

static void test_pointer_capture_requires_same_tab_release(void) {
    reist_gui_tabs_state_t state;
    reist_gui_tabs_result_t result;
    reist_gui_tabs_state_initialize(&state);
    reist_gui_tabs_result_initialize(&result);
    assert(reist_gui_tabs_configure(&model, &state, 1U, &result) == 0);

    pointer(&state, 75, 20, 1U, &result);
    assert(state.captured == 1U && state.armed);
    pointer(&state, 160, 20, 0U, &result);
    assert(state.selected == 0U && state.captured == REIST_GUI_TABS_NO_INDEX);

    pointer(&state, 75, 20, 1U, &result);
    pointer(&state, 75, 20, 0U, &result);
    assert(state.selected == 1U && result.selection_changed);
}

static void test_invalid_models_fail_before_mutation(void) {
    reist_gui_tabs_model_t invalid = model;
    reist_gui_tabs_state_t state;
    reist_gui_tabs_result_t result;
    reist_gui_tabs_state_initialize(&state);
    reist_gui_tabs_state_t before = state;
    reist_gui_tabs_result_initialize(&result);
    invalid.tab_bar.width = 100U;
    assert(reist_gui_tabs_configure(&invalid, &state, 1U, &result) ==
           REIST_GUI_TABS_ECAPACITY);
    assert(state.configured == before.configured);
    assert(state.selected == before.selected);
}

int main(void) {
    test_keyboard_and_programmatic_selection();
    test_pointer_capture_requires_same_tab_release();
    test_invalid_models_fail_before_mutation();
    return 0;
}
