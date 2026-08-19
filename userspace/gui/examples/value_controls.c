/** Buildable installed-SDK example for bounded value controls. */
#include <reist/gui/value_controls.h>

int main(void) {
    static const reist_gui_text_model_t model = {
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_text_model_t),
        1U, "Name", {0, 0, 160U, 24U}, 32U, 8U,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}
    };
    reist_gui_text_state_t state;
    reist_gui_value_result_t result;
    reist_gui_text_state_initialize(&state);
    reist_gui_value_result_initialize(&result);
    return reist_gui_text_configure(
        &model, &state, "REIST", &result) == 0 ? 0 : 1;
}
