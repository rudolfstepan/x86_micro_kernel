/** Buildable installed-SDK example for the asynchronous dialog controller. */
#include <reist/gui/dialog.h>

static const reist_gui_dialog_button_t buttons[] = {
    {"OK", REIST_GUI_DIALOG_RESPONSE_OK,
     REIST_GUI_DIALOG_ROLE_ACCEPT, 0U, 0U},
    {"Cancel", REIST_GUI_DIALOG_RESPONSE_CANCEL,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

int main(void) {
    const reist_gui_dialog_model_t model = {
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(reist_gui_dialog_model_t),
        .title = "SDK dialog",
        .message = "Asynchronous open and typed completion",
        .buttons = buttons,
        .button_count = 2U,
        .modality = REIST_GUI_DIALOG_APPLICATION_MODAL,
        .default_response = REIST_GUI_DIALOG_RESPONSE_OK,
        .cancel_response = REIST_GUI_DIALOG_RESPONSE_CANCEL,
        .owner_id = REIST_GUI_DIALOG_NO_OWNER,
        .flags = REIST_GUI_DIALOG_MOVABLE |
                 REIST_GUI_DIALOG_CLOSE_BUTTON,
    };
    const reist_gui_dialog_layout_t layout = {
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(reist_gui_dialog_layout_t),
        .surface_width = 640U,
        .surface_height = 480U,
        .work_area = {0, 24, 640U, 432U},
        .initial_bounds = {120, 100, 400U, 220U},
        .title_height = 24U,
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
    reist_gui_dialog_state_t state;
    reist_gui_dialog_state_initialize(&state);
    reist_gui_dialog_result_t result;
    reist_gui_dialog_result_initialize(&result);
    if (reist_gui_dialog_open(&model, &layout, &state, &result) != 0)
        return 1;

    reist_gui_dialog_event_t event;
    reist_gui_dialog_event_initialize(&event);
    event.type = REIST_GUI_DIALOG_EVENT_KEYBOARD;
    event.key = REIST_GUI_DIALOG_KEY_ENTER;
    if (reist_gui_dialog_dispatch(
            &model, &layout, &state, &event, &result) != 0 ||
        !result.completed)
        return 2;
    uint32_t response = REIST_GUI_DIALOG_RESPONSE_NONE;
    return reist_gui_dialog_response(&state, &response) == 0 &&
           response == REIST_GUI_DIALOG_RESPONSE_OK ? 0 : 3;
}
