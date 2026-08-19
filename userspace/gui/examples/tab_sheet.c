/** Buildable installed-SDK example for the renderer-independent tab API. */
#include <reist/gui/tabs.h>

int main(void) {
    static const reist_gui_tab_t pages[] = {
        {1U, 101U, "General", 72U,
         REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}},
        {2U, 102U, "Details", 72U,
         REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}},
    };
    static const reist_gui_tabs_model_t model = {
        REIST_GUI_TABS_API_VERSION, sizeof(reist_gui_tabs_model_t), pages, 2U,
        {0, 0, 160U, 24U}, {0, 24, 160U, 96U}, 2U,
        {0U, 0U, 0U, 0U}
    };
    reist_gui_tabs_state_t state;
    reist_gui_tabs_result_t result;
    reist_gui_tabs_state_initialize(&state);
    reist_gui_tabs_result_initialize(&result);
    return reist_gui_tabs_configure(&model, &state, 1U, &result) == 0 ? 0 : 1;
}
