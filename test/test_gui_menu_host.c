#include "reist/gui/menu.h"

#include <assert.h>

enum {
    ACTION_ABOUT = 100U,
    ACTION_EXIT,
    ACTION_WINDOW,
    ACTION_HELP
};

static const reist_gui_menu_item_t workspace_items[] = {
    {"About", ACTION_ABOUT, 0U, 0U, 0U},
    {"Exit", ACTION_EXIT, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t window_items[] = {
    {"Shell", ACTION_WINDOW, 0U, 0U, 0U},
    {"Files", ACTION_WINDOW, 1U, 0U, 0U},
};

static const reist_gui_menu_item_t help_items[] = {
    {"Help", ACTION_HELP, 7U, 0U, 0U},
    {"Unavailable", ACTION_HELP, 8U, REIST_GUI_MENU_ITEM_DISABLED, 0U},
};

static const reist_gui_menu_t menus[] = {
    {"Workspace", workspace_items, 2U, 0U, 0U},
    {"Windows", window_items, 2U, 0U, 0U},
    {"Help", help_items, 2U, 0U, 0U},
};

static reist_gui_menu_model_t model(void) {
    return (reist_gui_menu_model_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(reist_gui_menu_model_t),
        .menus = menus,
        .menu_count = 3U,
    };
}

static reist_gui_menu_layout_t layout(void) {
    return (reist_gui_menu_layout_t){
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
        .damage_margin = 4U,
    };
}

static reist_gui_menu_result_t dispatch(
    const reist_gui_menu_model_t *menu_model,
    const reist_gui_menu_layout_t *menu_layout,
    reist_gui_menu_state_t *state, uint32_t type,
    int32_t x, int32_t y, uint32_t pressed, uint32_t key) {
    reist_gui_menu_event_t event;
    reist_gui_menu_event_initialize(&event);
    event.type = type;
    event.x = x;
    event.y = y;
    event.button = type == REIST_GUI_MENU_EVENT_POINTER_BUTTON
        ? REIST_GUI_MENU_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    event.key = key;
    reist_gui_menu_result_t result;
    reist_gui_menu_result_initialize(&result);
    assert(reist_gui_menu_dispatch(
               menu_model, menu_layout, state, &event, &result) == 0);
    assert(result.damage_count <= REIST_GUI_MENU_DAMAGE_CAPACITY);
    return result;
}

int main(void) {
    reist_gui_menu_model_t menu_model = model();
    reist_gui_menu_layout_t menu_layout = layout();
    reist_gui_menu_state_t state;
    reist_gui_menu_state_initialize(&state);
    assert(reist_gui_menu_validate(
               &menu_model, &menu_layout, &state) == 0);
    state.capture_menu = 0U;
    assert(reist_gui_menu_validate(
               &menu_model, &menu_layout, &state) ==
           REIST_GUI_MENU_EINVAL);
    reist_gui_menu_state_initialize(&state);

    reist_gui_rect_t workspace_title;
    reist_gui_rect_t help_title;
    reist_gui_rect_t help_item;
    assert(reist_gui_menu_title_rect(
               &menu_model, &menu_layout, 0U, &workspace_title) == 0);
    assert(reist_gui_menu_title_rect(
               &menu_model, &menu_layout, 2U, &help_title) == 0);
    assert(workspace_title.x == 0 && workspace_title.height == 30U);
    assert(help_title.x > workspace_title.x);

    reist_gui_menu_result_t result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        workspace_title.x + 2, workspace_title.y + 2, 1U, 0U);
    assert(result.consumed && state.open_menu == 0U);
    result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        workspace_title.x + 2, workspace_title.y + 2, 0U, 0U);
    assert(result.consumed && state.open_menu == 0U);

    result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_MOTION,
        help_title.x + 2, help_title.y + 2, 0U, 0U);
    assert(result.consumed && state.open_menu == 2U);
    assert(reist_gui_menu_item_rect(
               &menu_model, &menu_layout, 2U, 0U, &help_item) == 0);
    (void)dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_MOTION,
        help_item.x + 2, help_item.y + 2, 0U, 0U);
    assert(state.hot_item == 0U);
    (void)dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        help_item.x + 2, help_item.y + 2, 1U, 0U);
    result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        help_item.x + 2, help_item.y + 2, 0U, 0U);
    assert(result.activated && result.action == ACTION_HELP);
    assert(result.target == 7U);
    assert(state.open_menu == REIST_GUI_MENU_NO_INDEX);

    (void)dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        help_title.x + 2, help_title.y + 2, 1U, 0U);
    (void)dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        help_title.x + 2, help_title.y + 2, 0U, 0U);
    result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_MENU_KEY_DOWN);
    assert(result.consumed && state.hot_item == 0U);
    result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_KEYBOARD, 0, 0, 0U,
        REIST_GUI_MENU_KEY_ENTER);
    assert(result.activated && result.action == ACTION_HELP);

    reist_gui_rect_t disabled_item;
    (void)dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        help_title.x + 2, help_title.y + 2, 1U, 0U);
    (void)dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        help_title.x + 2, help_title.y + 2, 0U, 0U);
    assert(reist_gui_menu_item_rect(
               &menu_model, &menu_layout, 2U, 1U,
               &disabled_item) == REIST_GUI_MENU_OK);
    result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_MOTION,
        disabled_item.x + 2, disabled_item.y + 2, 0U, 0U);
    assert(result.consumed && state.hot_item == REIST_GUI_MENU_NO_INDEX);
    (void)dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        disabled_item.x + 2, disabled_item.y + 2, 1U, 0U);
    result = dispatch(
        &menu_model, &menu_layout, &state,
        REIST_GUI_MENU_EVENT_POINTER_BUTTON,
        disabled_item.x + 2, disabled_item.y + 2, 0U, 0U);
    assert(!result.activated && state.open_menu == 2U);

    reist_gui_menu_state_initialize(&state);
    menu_model.menu_count = REIST_GUI_MENU_MAX_MENUS + 1U;
    assert(reist_gui_menu_validate(
               &menu_model, &menu_layout, &state) != 0);
    menu_model = model();
    menu_layout.version++;
    assert(reist_gui_menu_validate(
               &menu_model, &menu_layout, &state) != 0);

    /* Even the smallest accepted metrics retain a non-negative content row
     * after the popup's two-pixel border is removed. */
    static const reist_gui_menu_item_t tiny_items[] = {
        {"I", ACTION_ABOUT, 0U, 0U, 0U},
    };
    static const reist_gui_menu_t tiny_menus[] = {
        {"M", tiny_items, 1U, 0U, 0U},
    };
    reist_gui_menu_model_t tiny_model = {
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(reist_gui_menu_model_t),
        .menus = tiny_menus,
        .menu_count = 1U,
    };
    reist_gui_menu_layout_t tiny_layout = {
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(reist_gui_menu_layout_t),
        .surface_width = 8U,
        .surface_height = 8U,
        .bar = {0, 0, 8U, 1U},
        .font_width = 1U,
        .font_height = 1U,
    };
    reist_gui_menu_state_initialize(&state);
    reist_gui_rect_t tiny_popup;
    reist_gui_rect_t tiny_item;
    assert(reist_gui_menu_popup_rect(
               &tiny_model, &tiny_layout, 0U, &tiny_popup) == 0);
    assert(reist_gui_menu_item_rect(
               &tiny_model, &tiny_layout, 0U, 0U, &tiny_item) == 0);
    assert(tiny_popup.width == 7U);
    assert(tiny_item.width == 3U);
    return 0;
}
