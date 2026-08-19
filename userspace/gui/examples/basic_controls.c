/** Buildable installed-header example for the REIST basic-control API. */
#include <reist/gui/control.h>

static const reist_gui_control_t example_controls[] = {
    {1U, REIST_GUI_CONTROL_ROLE_LABEL, "Network",
     {8, 8, 120U, 20U}, 0U, 0U, REIST_GUI_CONTROL_VISIBLE,
     REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}},
    {2U, REIST_GUI_CONTROL_ROLE_CHECKBOX, "Enabled",
     {8, 36, 140U, 24U}, 20U, 0U,
     REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED,
     REIST_GUI_CONTROL_CHECKED, {0U, 0U}},
};

int main(void) {
    reist_gui_control_model_t model = {
        .version = REIST_GUI_CONTROL_API_VERSION,
        .struct_size = sizeof(model),
        .controls = example_controls,
        .control_count = 2U,
        .surface_width = 320U,
        .surface_height = 200U,
        .damage_margin = 2U,
    };
    reist_gui_control_state_t state;
    reist_gui_control_state_initialize(&state);
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    if (reist_gui_control_configure(&model, &state, &result) != 0) return 1;

    reist_gui_control_event_t event;
    reist_gui_control_event_initialize(&event);
    event.type = REIST_GUI_CONTROL_EVENT_KEYBOARD;
    event.key = REIST_GUI_CONTROL_KEY_NEXT;
    reist_gui_control_result_initialize(&result);
    return reist_gui_control_dispatch(&model, &state, &event, &result);
}
