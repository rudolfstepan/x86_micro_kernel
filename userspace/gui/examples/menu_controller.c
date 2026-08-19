/**
 * @file userspace/gui/examples/menu_controller.c
 * @brief Minimal client of the installed `reist-gui` static library.
 *
 * This example intentionally has no framebuffer dependency. A real renderer
 * paints the rectangles returned by the geometry API, forwards normalized
 * input while the menu owns capture, and recomposes every reported damage
 * rectangle. The example only proves model construction and event dispatch.
 */
#include <reist/gui/menu.h>

enum {
    EXAMPLE_ACTION_ABOUT = 1U,
    EXAMPLE_ACTION_EXIT
};

/* Models are immutable and may remain in read-only program storage. */
static const reist_gui_menu_item_t workspace_items[] = {
    {"About", EXAMPLE_ACTION_ABOUT, 0U, 0U, 0U},
    {"Exit", EXAMPLE_ACTION_EXIT, 0U, 0U, 0U},
};

static const reist_gui_menu_t menus[] = {
    {"Workspace", workspace_items, 2U, 0U, 0U},
};

static const reist_gui_menu_model_t model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = menus,
    .menu_count = 1U,
};

static reist_gui_menu_result_t pointer_button(
    const reist_gui_menu_layout_t *layout, reist_gui_menu_state_t *state,
    int32_t x, int32_t y, uint32_t pressed) {
    reist_gui_menu_event_t event;
    reist_gui_menu_event_initialize(&event);
    event.type = REIST_GUI_MENU_EVENT_POINTER_BUTTON;
    event.x = x;
    event.y = y;
    event.button = REIST_GUI_MENU_BUTTON_LEFT;
    event.pressed = pressed;

    reist_gui_menu_result_t result;
    reist_gui_menu_result_initialize(&result);
    if (reist_gui_menu_dispatch(
            &model, layout, state, &event, &result) != REIST_GUI_MENU_OK) {
        /* A production client would close the popup and report the error. */
        reist_gui_menu_state_initialize(state);
    }
    return result;
}

int main(void) {
    const reist_gui_menu_layout_t layout = {
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(reist_gui_menu_layout_t),
        .surface_width = 640U,
        .surface_height = 480U,
        .bar = {0, 0, 640U, 30U},
        .font_width = 8U,
        .font_height = 16U,
        .title_padding_x = 8U,
        .item_padding_x = 8U,
        .item_padding_y = 4U,
        .damage_margin = 6U,
    };
    reist_gui_menu_state_t state;
    reist_gui_menu_state_initialize(&state);
    if (reist_gui_menu_validate(&model, &layout, &state) !=
        REIST_GUI_MENU_OK) return 1;

    reist_gui_rect_t title;
    reist_gui_rect_t about;
    if (reist_gui_menu_title_rect(&model, &layout, 0U, &title) !=
            REIST_GUI_MENU_OK ||
        reist_gui_menu_item_rect(&model, &layout, 0U, 0U, &about) !=
            REIST_GUI_MENU_OK) return 2;

    /* Button-down and button-up are both routed through implicit capture. */
    (void)pointer_button(&layout, &state, title.x + 1, title.y + 1, 1U);
    (void)pointer_button(&layout, &state, title.x + 1, title.y + 1, 0U);
    (void)pointer_button(&layout, &state, about.x + 1, about.y + 1, 1U);
    reist_gui_menu_result_t result = pointer_button(
        &layout, &state, about.x + 1, about.y + 1, 0U);
    return result.activated && result.action == EXAMPLE_ACTION_ABOUT
        ? 0 : 3;
}
