#include <assert.h>
#include <string.h>
#include "reist/gui/file_dialog.h"

static reist_gui_file_dialog_layout_t layout(void) {
    return (reist_gui_file_dialog_layout_t){
        REIST_GUI_FILE_DIALOG_API_VERSION,
        sizeof(reist_gui_file_dialog_layout_t),
        {20, 20, 400U, 160U}, {20, 20, 400U, 30U},
        {40, 70, 360U, 24U}, {210, 120, 90U, 28U},
        {310, 120, 90U, 28U}, 8U, {0U, 0U, 0U, 0U}};
}

static reist_gui_file_dialog_event_t key(uint32_t value) {
    reist_gui_file_dialog_event_t event;
    reist_gui_file_dialog_event_initialize(&event);
    event.type = REIST_GUI_FILE_DIALOG_EVENT_KEYBOARD;
    event.key = value;
    return event;
}

int main(void) {
    const reist_gui_file_dialog_model_t model = {
        REIST_GUI_FILE_DIALOG_API_VERSION, sizeof(model),
        REIST_GUI_FILE_DIALOG_SAVE, "Speichern unter", "Speichern",
        {0U, 0U, 0U, 0U}};
    reist_gui_file_dialog_layout_t metrics = layout();
    reist_gui_file_dialog_state_t state;
    reist_gui_file_dialog_state_initialize(&state);
    reist_gui_file_dialog_result_t result;
    reist_gui_file_dialog_result_initialize(&result);
    assert(reist_gui_file_dialog_open(
               &model, &metrics, &state, "/readme.txt", &result) == 0);
    assert(state.visible && state.focus == REIST_GUI_FILE_DIALOG_FOCUS_PATH);

    reist_gui_file_dialog_event_t event = key(
        REIST_GUI_FILE_DIALOG_KEY_END);
    reist_gui_file_dialog_result_initialize(&result);
    assert(reist_gui_file_dialog_dispatch(
               &model, &metrics, &state, &event, &result) == 0);
    event = key(REIST_GUI_FILE_DIALOG_KEY_ENTER);
    reist_gui_file_dialog_result_initialize(&result);
    assert(reist_gui_file_dialog_dispatch(
               &model, &metrics, &state, &event, &result) == 0);
    assert(result.completed &&
           result.response == REIST_GUI_FILE_DIALOG_RESPONSE_ACCEPT);
    assert(strcmp(result.path, "/readme.txt") == 0);

    reist_gui_file_dialog_state_initialize(&state);
    reist_gui_file_dialog_result_initialize(&result);
    assert(reist_gui_file_dialog_open(
               &model, &metrics, &state, "/other.txt", &result) == 0);
    event = key(REIST_GUI_FILE_DIALOG_KEY_ESCAPE);
    reist_gui_file_dialog_result_initialize(&result);
    assert(reist_gui_file_dialog_dispatch(
               &model, &metrics, &state, &event, &result) == 0);
    assert(result.completed &&
           result.response == REIST_GUI_FILE_DIALOG_RESPONSE_CANCEL);
    assert(result.path[0] == '\0');
    return 0;
}
