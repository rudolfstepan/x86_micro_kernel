/**
 * @file userspace/gui/apps/control_gallery/main.c
 * @brief Interactive gallery for the currently implemented REIST GUI API.
 *
 * The compositor delegates exactly one Surface endpoint. The gallery owns no
 * global display or raw-input authority and never blocks the desktop while it
 * is open.
 */
#include "x86os.h"
#include "reist/gui/control.h"
#include "reist/gui/container.h"
#include "reist/gui/dialog.h"
#include "reist/gui/menu.h"
#include "reist/gui/surface_client.h"
#include "reist/gui/tabs.h"
#include "reist/gui/value_controls.h"

#define GALLERY_MENU_COUNT 3U
#define GALLERY_TEXT_LIMIT 128U
#define GALLERY_SURFACE_EVENT_BATCH_LIMIT 32U
#define GALLERY_SURFACE_CREATE_ATTEMPTS 250U
#define GALLERY_DEFAULT_WIDTH 800U
#define GALLERY_DEFAULT_HEIGHT 600U
#define GALLERY_MIN_WIDTH 640U
#define GALLERY_MIN_HEIGHT 480U
#define GALLERY_FONT_WIDTH 8U
#define GALLERY_FONT_HEIGHT 16U

enum {
    GALLERY_KEY_NONE = 0x100,
    GALLERY_KEY_ESCAPE,
    GALLERY_KEY_UP,
    GALLERY_KEY_DOWN,
    GALLERY_KEY_LEFT,
    GALLERY_KEY_RIGHT
};

enum {
    GALLERY_ACTION_EXIT = 1U,
    GALLERY_ACTION_MODELESS,
    GALLERY_ACTION_MODAL,
    GALLERY_ACTION_ABOUT
};

enum {
    GALLERY_CONTROL_LABEL = 1U,
    GALLERY_CONTROL_BUTTON,
    GALLERY_CONTROL_CHECKBOX,
    GALLERY_CONTROL_RADIO_CLASSIC,
    GALLERY_CONTROL_RADIO_CONTRAST,
    GALLERY_CONTROL_DISABLED
};

#define GALLERY_CONTROL_COUNT 6U
#define GALLERY_CONTROL_ACTION_DIALOG 100U

enum {
    GALLERY_TAB_BASIS = 1U,
    GALLERY_TAB_INPUT,
    GALLERY_TAB_SELECTION,
    GALLERY_TAB_VALUES
};

enum {
    GALLERY_PAGE_BASIS = 101U,
    GALLERY_PAGE_INPUT,
    GALLERY_PAGE_SELECTION,
    GALLERY_PAGE_VALUES
};

enum {
    GALLERY_FOCUS_TABS = 1U,
    GALLERY_FOCUS_BASIC,
    GALLERY_FOCUS_TEXT,
    GALLERY_FOCUS_LIST,
    GALLERY_FOCUS_SCROLLBAR,
    GALLERY_FOCUS_SLIDER,
    GALLERY_FOCUS_SPIN
};

enum {
    GALLERY_TEXT_NAME = 200U,
    GALLERY_LIST_THEME,
    GALLERY_SCROLLBAR,
    GALLERY_SLIDER,
    GALLERY_SPIN,
    GALLERY_PROGRESS
};

enum {
    GALLERY_NODE_ROOT = 1U,
    GALLERY_NODE_BASIS_PAGE = 10U,
    GALLERY_NODE_BASIS_GROUP,
    GALLERY_NODE_BASIS_FIRST,
    GALLERY_NODE_INPUT_PAGE = 20U,
    GALLERY_NODE_INPUT_GROUP,
    GALLERY_NODE_TEXT,
    GALLERY_NODE_SELECTION_PAGE = 30U,
    GALLERY_NODE_SELECTION_GROUP,
    GALLERY_NODE_LIST,
    GALLERY_NODE_SCROLLBAR,
    GALLERY_NODE_VALUES_PAGE = 40U,
    GALLERY_NODE_VALUES_GROUP,
    GALLERY_NODE_SLIDER,
    GALLERY_NODE_SPIN,
    GALLERY_NODE_PROGRESS
};

enum {
    GALLERY_DIALOG_NONE = 0U,
    GALLERY_DIALOG_MODELESS,
    GALLERY_DIALOG_MODAL,
    GALLERY_DIALOG_ABOUT
};

static const uint32_t color_desktop = 0x00006E8EU;
static const uint32_t color_face = 0x00C8C8C8U;
static const uint32_t color_light = 0x00FFFFFFU;
static const uint32_t color_shadow = 0x00606060U;
static const uint32_t color_dark = 0x00181818U;
static const uint32_t color_active = 0x00000088U;
static const uint32_t color_inactive = 0x00787878U;
static const uint32_t color_text = 0x00000000U;
static const uint32_t color_title_text = 0x00FFFFFFU;
static reist_gui_surface_client_t *gallery_surface;
static uint32_t gallery_paint_failed;

static const reist_gui_menu_item_t gallery_items[] = {
    {"Beenden", GALLERY_ACTION_EXIT, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t dialog_items[] = {
    {"Modeless anzeigen", GALLERY_ACTION_MODELESS, 0U, 0U, 0U},
    {"Modal anzeigen", GALLERY_ACTION_MODAL, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t info_items[] = {
    {"Ueber Control Gallery", GALLERY_ACTION_ABOUT, 0U, 0U, 0U},
};

static const reist_gui_menu_t gallery_menus[GALLERY_MENU_COUNT] = {
    {"Galerie", gallery_items, 1U, 0U, 0U},
    {"Dialoge", dialog_items, 2U, 0U, 0U},
    {"Info", info_items, 1U, 0U, 0U},
};

static const reist_gui_menu_model_t gallery_menu_model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = gallery_menus,
    .menu_count = GALLERY_MENU_COUNT,
};

static const reist_gui_dialog_button_t close_button[] = {
    {"Schliessen", REIST_GUI_DIALOG_RESPONSE_CLOSE,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

static const reist_gui_dialog_button_t decision_buttons[] = {
    {"OK", REIST_GUI_DIALOG_RESPONSE_OK,
     REIST_GUI_DIALOG_ROLE_ACCEPT, 0U, 0U},
    {"Abbrechen", REIST_GUI_DIALOG_RESPONSE_CANCEL,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

static const reist_gui_dialog_model_t modeless_dialog_model = {
    .version = REIST_GUI_DIALOG_API_VERSION,
    .struct_size = sizeof(reist_gui_dialog_model_t),
    .title = "Modeless Dialog",
    .message = "Ausserhalb kann weitergearbeitet werden.",
    .detail = "Titelleiste ziehen; ESC liefert CLOSE.",
    .buttons = close_button,
    .button_count = 1U,
    .modality = REIST_GUI_DIALOG_MODELESS,
    .default_response = REIST_GUI_DIALOG_RESPONSE_CLOSE,
    .cancel_response = REIST_GUI_DIALOG_RESPONSE_CLOSE,
    .owner_id = REIST_GUI_DIALOG_NO_OWNER,
    .flags = REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
};

static const reist_gui_dialog_model_t modal_dialog_model = {
    .version = REIST_GUI_DIALOG_API_VERSION,
    .struct_size = sizeof(reist_gui_dialog_model_t),
    .title = "Application-modal Dialog",
    .message = "Der Hintergrund ist bis zur Response inert.",
    .detail = "Enter nutzt Default; ESC nutzt Cancel.",
    .buttons = decision_buttons,
    .button_count = 2U,
    .modality = REIST_GUI_DIALOG_APPLICATION_MODAL,
    .default_response = REIST_GUI_DIALOG_RESPONSE_OK,
    .cancel_response = REIST_GUI_DIALOG_RESPONSE_CANCEL,
    .owner_id = REIST_GUI_DIALOG_NO_OWNER,
    .flags = REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
};

static const reist_gui_dialog_model_t about_dialog_model = {
    .version = REIST_GUI_DIALOG_API_VERSION,
    .struct_size = sizeof(reist_gui_dialog_model_t),
    .title = "Ueber REIST GUI Control Gallery",
    .message = "Interaktiver Nachweis der installierten GUI-API",
    .detail = "Keine geplante Komponente wird als fertig ausgegeben.",
    .buttons = close_button,
    .button_count = 1U,
    .modality = REIST_GUI_DIALOG_APPLICATION_MODAL,
    .default_response = REIST_GUI_DIALOG_RESPONSE_CLOSE,
    .cancel_response = REIST_GUI_DIALOG_RESPONSE_CLOSE,
    .owner_id = REIST_GUI_DIALOG_NO_OWNER,
    .flags = REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
};

typedef struct {
    reist_gui_menu_state_t menu;
    reist_gui_dialog_state_t dialog;
    reist_gui_control_state_t control;
    reist_gui_control_t controls[GALLERY_CONTROL_COUNT];
    reist_gui_control_model_t control_model;
    reist_gui_tabs_state_t tabs;
    reist_gui_tab_t tab_items[4];
    reist_gui_tabs_model_t tab_model;
    reist_gui_node_t tree_nodes[22];
    reist_gui_tree_model_t tree_model;
    reist_gui_text_state_t text_field;
    reist_gui_text_model_t text_model;
    reist_gui_list_state_t list;
    reist_gui_list_model_t list_model;
    reist_gui_range_state_t scrollbar;
    reist_gui_range_model_t scrollbar_model;
    reist_gui_range_state_t slider;
    reist_gui_range_model_t slider_model;
    reist_gui_range_state_t spin;
    reist_gui_range_model_t spin_model;
    reist_gui_range_state_t progress;
    reist_gui_range_model_t progress_model;
    uint32_t focus_target;
    uint32_t dialog_kind;
    uint32_t exit_requested;
    uint32_t redraw;
} gallery_state_t;

static const reist_gui_list_item_t gallery_list_items[] = {
    {1U, "Klassisch", REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
     {0U, 0U}},
    {2U, "Kontrast", REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
     {0U, 0U}},
    {3U, "Systemvorgabe", REIST_GUI_VALUE_VISIBLE, {0U, 0U}},
    {4U, "Benutzerdefiniert",
     REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED, {0U, 0U}},
};

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static size_t bounded_length(const char *text, size_t limit) {
    size_t length = 0U;
    if (text == 0) return 0U;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

static uint32_t text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0U;
    while (index < GALLERY_TEXT_LIMIT &&
           left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return index < GALLERY_TEXT_LIMIT &&
           left[index] == '\0' && right[index] == '\0';
}

static uint32_t selected_page(const gallery_state_t *state) {
    if (state->tabs.selected >= state->tab_model.tab_count)
        return GALLERY_PAGE_BASIS;
    return state->tab_items[state->tabs.selected].page_id;
}

static void decimal_text(int32_t value, char output[16]) {
    char reverse[12];
    uint32_t count = 0U;
    uint32_t write = 0U;
    uint32_t magnitude;
    if (value < 0) {
        output[write++] = '-';
        magnitude = (uint32_t)(-(int64_t)value);
    } else magnitude = (uint32_t)value;
    do {
        reverse[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && count < sizeof(reverse));
    while (count != 0U) output[write++] = reverse[--count];
    output[write] = '\0';
}

static void fill(reist_gui_rect_t rect, uint32_t color) {
    if (gallery_surface != 0 && rect.width != 0U && rect.height != 0U &&
        reist_gui_surface_client_paint_fill(
            gallery_surface, rect, color) != 0)
        gallery_paint_failed = 1U;
}

static void text(const x86os_display_info_t *display,
                 int32_t x, int32_t y, const char *value,
                 uint32_t maximum_width, uint32_t foreground,
                 uint32_t background) {
    if (display == 0 || value == 0 || display->font_width == 0U) return;
    size_t length = bounded_length(value, GALLERY_TEXT_LIMIT);
    size_t capacity = maximum_width / display->font_width;
    if (length > capacity) length = capacity;
    if (length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY)
        length = REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY - 1U;
    if (gallery_surface != 0 && length != 0U &&
        reist_gui_surface_client_paint_text(
            gallery_surface, x, y, maximum_width, value, (uint32_t)length,
            foreground, background) != 0)
        gallery_paint_failed = 1U;
}

static void bevel(reist_gui_rect_t rect, uint32_t face, uint32_t raised) {
    if (rect.width == 0U || rect.height == 0U) return;
    fill(rect, face);
    if (rect.width < 2U || rect.height < 2U) return;
    uint32_t top_left = raised ? color_light : color_shadow;
    uint32_t bottom_right = raised ? color_shadow : color_light;
    fill((reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, top_left);
    fill((reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, top_left);
    fill((reist_gui_rect_t){
        rect.x, rect.y + (int32_t)rect.height - 1,
        rect.width, 1U}, bottom_right);
    fill((reist_gui_rect_t){
        rect.x + (int32_t)rect.width - 1, rect.y,
        1U, rect.height}, bottom_right);
}

static void outline(reist_gui_rect_t rect, uint32_t color) {
    if (rect.width == 0U || rect.height == 0U) return;
    fill((reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, color);
    fill((reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, color);
    if (rect.height > 1U)
        fill((reist_gui_rect_t){
            rect.x, rect.y + (int32_t)rect.height - 1,
            rect.width, 1U}, color);
    if (rect.width > 1U)
        fill((reist_gui_rect_t){
            rect.x + (int32_t)rect.width - 1, rect.y,
            1U, rect.height}, color);
}

static uint32_t menu_height(const x86os_display_info_t *display) {
    return max_u32(display->font_height + 12U, 30U);
}

static reist_gui_menu_layout_t menu_layout(
    const x86os_display_info_t *display) {
    return (reist_gui_menu_layout_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(reist_gui_menu_layout_t),
        .surface_width = display->width,
        .surface_height = display->height,
        .bar = {0, 0, display->width, menu_height(display)},
        .font_width = display->font_width,
        .font_height = display->font_height,
        .title_padding_x = 8U,
        .item_padding_x = 8U,
        .item_padding_y = 4U,
        .damage_margin = 6U,
    };
}

static reist_gui_dialog_layout_t dialog_layout(
    const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 8U;
    uint32_t work_height = display->height > top + 8U
        ? display->height - top - 8U : 1U;
    uint32_t width = display->width > 48U ? display->width - 48U : 1U;
    if (width > 520U) width = 520U;
    uint32_t height = work_height > 230U ? 230U : work_height;
    return (reist_gui_dialog_layout_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(reist_gui_dialog_layout_t),
        .surface_width = display->width,
        .surface_height = display->height,
        .work_area = {0, (int32_t)top, display->width, work_height},
        .initial_bounds = {
            (int32_t)((display->width - width) / 2U),
            (int32_t)(top + (work_height - height) / 2U),
            width, height,
        },
        .title_height = menu_height(display),
        .border_width = 3U,
        .font_width = display->font_width,
        .font_height = display->font_height,
        .button_min_width = 88U,
        .button_height = max_u32(display->font_height + 10U, 26U),
        .button_gap = 8U,
        .button_padding_x = 8U,
        .content_padding = 12U,
        .damage_margin = 6U,
    };
}

static reist_gui_rect_t gallery_frame(
    const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 18U;
    uint32_t bottom = display->height > 20U ? display->height - 20U : top;
    return (reist_gui_rect_t){
        24, (int32_t)top,
        display->width > 48U ? display->width - 48U : 1U,
        bottom > top ? bottom - top : 1U};
}

static const reist_gui_dialog_model_t *dialog_model(uint32_t kind) {
    if (kind == GALLERY_DIALOG_MODELESS) return &modeless_dialog_model;
    if (kind == GALLERY_DIALOG_MODAL) return &modal_dialog_model;
    if (kind == GALLERY_DIALOG_ABOUT) return &about_dialog_model;
    return 0;
}

static void render_menu(const x86os_display_info_t *display,
                        const gallery_state_t *state) {
    reist_gui_menu_layout_t layout = menu_layout(display);
    bevel(layout.bar, color_face, 1U);
    for (uint32_t index = 0U;
         index < gallery_menu_model.menu_count; ++index) {
        reist_gui_rect_t title;
        if (reist_gui_menu_title_rect(
                &gallery_menu_model, &layout, index, &title) != 0)
            continue;
        uint32_t active = state->menu.open_menu == index;
        uint32_t background = active ? color_active : color_face;
        fill(title, background);
        uint32_t y = title.height > display->font_height
            ? (title.height - display->font_height) / 2U : 0U;
        text(display, title.x + (int32_t)layout.title_padding_x,
             title.y + (int32_t)y,
             gallery_menu_model.menus[index].label,
             title.width - layout.title_padding_x * 2U,
             active ? color_title_text : color_text, background);
    }

    if (state->menu.open_menu == REIST_GUI_MENU_NO_INDEX) return;
    uint32_t menu_index = state->menu.open_menu;
    reist_gui_rect_t popup;
    if (reist_gui_menu_popup_rect(
            &gallery_menu_model, &layout, menu_index, &popup) != 0)
        return;
    fill((reist_gui_rect_t){
        popup.x + 4, popup.y + 4, popup.width, popup.height}, color_dark);
    bevel(popup, color_face, 1U);
    const reist_gui_menu_t *menu = &gallery_menu_model.menus[menu_index];
    for (uint32_t index = 0U; index < menu->item_count; ++index) {
        reist_gui_rect_t item;
        if (reist_gui_menu_item_rect(
                &gallery_menu_model, &layout,
                menu_index, index, &item) != 0)
            continue;
        uint32_t hot = state->menu.hot_item == index;
        uint32_t background = hot ? color_active : color_face;
        if (hot) fill(item, background);
        uint32_t y = item.height > display->font_height
            ? (item.height - display->font_height) / 2U : 0U;
        text(display, item.x + (int32_t)layout.item_padding_x,
             item.y + (int32_t)y, menu->items[index].label,
             item.width - layout.item_padding_x * 2U,
             hot ? color_title_text : color_text, background);
    }
}

static void render_basic_controls(const x86os_display_info_t *display,
                                  const gallery_state_t *state) {
    for (uint32_t index = 0U;
         index < state->control_model.control_count; ++index) {
        const reist_gui_control_t *control = &state->controls[index];
        uint32_t enabled =
            (control->flags & REIST_GUI_CONTROL_ENABLED) != 0U;
        uint32_t foreground = enabled ? color_text : color_shadow;
        if (control->role == REIST_GUI_CONTROL_ROLE_LABEL) {
            text(display, control->bounds.x, control->bounds.y,
                 control->label, control->bounds.width,
                 color_text, color_face);
            continue;
        }
        uint32_t pressed = state->control.captured == index &&
                           state->control.armed;
        if (control->role == REIST_GUI_CONTROL_ROLE_PUSH_BUTTON) {
            if (state->control.focused == index)
                outline((reist_gui_rect_t){
                    control->bounds.x - 2, control->bounds.y - 2,
                    control->bounds.width + 4U,
                    control->bounds.height + 4U}, color_dark);
            bevel(control->bounds, color_face, !pressed);
            size_t length = bounded_length(
                control->label, REIST_GUI_CONTROL_LABEL_LIMIT);
            uint32_t label_width = (uint32_t)length * display->font_width;
            text(display,
                 control->bounds.x + (int32_t)((control->bounds.width >
                     label_width ? control->bounds.width - label_width : 0U) /
                     2U),
                 control->bounds.y + (int32_t)((control->bounds.height >
                     display->font_height ? control->bounds.height -
                     display->font_height : 0U) / 2U),
                 control->label, control->bounds.width,
                 foreground, color_face);
            continue;
        }
        uint32_t marker_size = control->bounds.height > 18U
            ? 18U : control->bounds.height;
        reist_gui_rect_t marker = {
            control->bounds.x,
            control->bounds.y + (int32_t)((control->bounds.height -
                                           marker_size) / 2U),
            marker_size, marker_size};
        if (state->control.focused == index) {
            size_t focus_length = bounded_length(
                control->label, REIST_GUI_CONTROL_LABEL_LIMIT);
            uint32_t focus_text_width =
                (uint32_t)focus_length * display->font_width;
            uint32_t available = control->bounds.width > marker.width + 8U
                ? control->bounds.width - marker.width - 8U : 0U;
            if (focus_text_width > available) focus_text_width = available;
            outline((reist_gui_rect_t){
                control->bounds.x - 2, control->bounds.y - 2,
                marker.width + 8U + focus_text_width + 4U,
                control->bounds.height + 4U}, color_dark);
        }
        bevel(marker, color_light, 0U);
        uint32_t checked = state->control.check[index];
        if (checked == REIST_GUI_CONTROL_CHECKED && marker_size > 8U)
            fill((reist_gui_rect_t){
                marker.x + 4, marker.y + 4,
                marker.width - 8U, marker.height - 8U}, color_active);
        else if (checked == REIST_GUI_CONTROL_MIXED && marker_size > 8U)
            fill((reist_gui_rect_t){
                marker.x + 4,
                marker.y + (int32_t)(marker.height / 2U) - 1,
                marker.width - 8U, 3U}, color_active);
        text(display, marker.x + (int32_t)marker.width + 8,
             control->bounds.y + (int32_t)((control->bounds.height >
                 display->font_height ? control->bounds.height -
                 display->font_height : 0U) / 2U),
             control->label,
             control->bounds.width > marker.width + 8U
                ? control->bounds.width - marker.width - 8U : 1U,
             foreground, color_face);
    }
}

static void render_tabs(const x86os_display_info_t *display,
                        const gallery_state_t *state) {
    bevel(state->tab_model.content, color_face, 1U);
    for (uint32_t index = 0U; index < state->tab_model.tab_count; ++index) {
        reist_gui_rect_t rect;
        if (reist_gui_tabs_tab_rect(
                &state->tab_model, index, &rect) != 0) continue;
        uint32_t selected = state->tabs.selected == index;
        uint32_t hot = state->tabs.hovered == index;
        uint32_t background = selected ? color_face :
            (hot ? color_light : color_inactive);
        bevel(rect, background, selected || !state->tabs.armed);
        if (selected && rect.height > 2U)
            fill((reist_gui_rect_t){
                rect.x + 2, rect.y + (int32_t)rect.height - 2,
                rect.width > 4U ? rect.width - 4U : 1U, 2U}, color_face);
        if (state->tabs.focused == index &&
            state->focus_target == GALLERY_FOCUS_TABS)
            outline((reist_gui_rect_t){
                rect.x + 3, rect.y + 3,
                rect.width > 6U ? rect.width - 6U : 1U,
                rect.height > 6U ? rect.height - 6U : 1U}, color_dark);
        size_t length = bounded_length(
            state->tab_items[index].label, REIST_GUI_TABS_LABEL_LIMIT);
        uint32_t label_width = (uint32_t)length * display->font_width;
        text(display,
             rect.x + (int32_t)((rect.width > label_width
                ? rect.width - label_width : 0U) / 2U),
             rect.y + (int32_t)((rect.height > display->font_height
                ? rect.height - display->font_height : 0U) / 2U),
             state->tab_items[index].label, rect.width,
             color_text, background);
    }
}

static void render_text_page(const x86os_display_info_t *display,
                             const gallery_state_t *state) {
    const reist_gui_text_model_t *model = &state->text_model;
    text(display, model->bounds.x,
         model->bounds.y - (int32_t)display->font_height - 4,
         "Einzeiliges Textfeld", model->bounds.width, color_text, color_face);
    bevel(model->bounds, color_light, 0U);
    text(display, model->bounds.x + 5,
         model->bounds.y + (int32_t)((model->bounds.height >
             display->font_height ? model->bounds.height -
             display->font_height : 0U) / 2U),
         state->text_field.text,
         model->bounds.width > 10U ? model->bounds.width - 10U : 1U,
         color_text, color_light);
    if (state->text_field.focused) {
        int32_t cursor_x = model->bounds.x + 5 +
            (int32_t)(state->text_field.cursor * model->glyph_width);
        fill((reist_gui_rect_t){
            cursor_x, model->bounds.y + 4,
            2U, model->bounds.height > 8U ? model->bounds.height - 8U : 1U},
            color_active);
        outline((reist_gui_rect_t){
            model->bounds.x - 2, model->bounds.y - 2,
            model->bounds.width + 4U, model->bounds.height + 4U}, color_dark);
    }
    text(display, model->bounds.x,
         model->bounds.y + (int32_t)model->bounds.height + 10,
         "ASCII-Eingabe; Cursor mit Links/Rechts", model->bounds.width,
         color_shadow, color_face);
}

static void render_scrollbar(const gallery_state_t *state) {
    const reist_gui_range_model_t *model = &state->scrollbar_model;
    bevel(model->bounds, color_face, 0U);
    uint32_t extent = model->bounds.height > 12U
        ? model->bounds.height - 12U : 1U;
    uint32_t offset = (uint32_t)(state->scrollbar.value - model->minimum) *
        extent / (uint32_t)(model->maximum - model->minimum);
    reist_gui_rect_t thumb = {
        model->bounds.x + 3, model->bounds.y + 3 + (int32_t)offset,
        model->bounds.width > 6U ? model->bounds.width - 6U : 1U, 9U};
    bevel(thumb, color_face, 1U);
    if (state->scrollbar.focused) outline((reist_gui_rect_t){
        model->bounds.x - 2, model->bounds.y - 2,
        model->bounds.width + 4U, model->bounds.height + 4U}, color_dark);
}

static void render_list_page(const x86os_display_info_t *display,
                             const gallery_state_t *state) {
    const reist_gui_list_model_t *model = &state->list_model;
    text(display, model->bounds.x,
         model->bounds.y - (int32_t)display->font_height - 4,
         "Liste: Theme-Auswahl", model->bounds.width, color_text, color_face);
    bevel(model->bounds, color_light, 0U);
    uint32_t rows = model->bounds.height / model->row_height;
    for (uint32_t row = 0U; row < rows; ++row) {
        uint32_t index = state->list.top_index + row;
        if (index >= model->item_count) break;
        reist_gui_rect_t item = {
            model->bounds.x + 2,
            model->bounds.y + 2 + (int32_t)(row * model->row_height),
            model->bounds.width > 4U ? model->bounds.width - 4U : 1U,
            model->row_height};
        uint32_t selected = state->list.selected == index;
        uint32_t background = selected ? color_active : color_light;
        fill(item, background);
        uint32_t enabled = (model->items[index].flags &
            REIST_GUI_VALUE_ENABLED) != 0U;
        text(display, item.x + 5,
             item.y + (int32_t)((item.height > display->font_height
                ? item.height - display->font_height : 0U) / 2U),
             model->items[index].label,
             item.width > 10U ? item.width - 10U : 1U,
             selected ? color_title_text :
                 (enabled ? color_text : color_shadow), background);
    }
    if (state->list.focused) outline((reist_gui_rect_t){
        model->bounds.x - 2, model->bounds.y - 2,
        model->bounds.width + 4U, model->bounds.height + 4U}, color_dark);
    text(display, state->scrollbar_model.bounds.x,
         model->bounds.y - (int32_t)display->font_height - 4,
         "Scroll", state->scrollbar_model.bounds.width, color_text, color_face);
    render_scrollbar(state);
}

static uint32_t range_fraction(const reist_gui_range_model_t *model,
                               const reist_gui_range_state_t *state,
                               uint32_t extent) {
    return (uint32_t)(state->value - model->minimum) * extent /
        (uint32_t)(model->maximum - model->minimum);
}

static void render_values_page(const x86os_display_info_t *display,
                               const gallery_state_t *state) {
    const reist_gui_range_model_t *slider = &state->slider_model;
    text(display, slider->bounds.x,
         slider->bounds.y - (int32_t)display->font_height - 3,
         "Slider: Lautstaerke", slider->bounds.width, color_text, color_face);
    fill((reist_gui_rect_t){
        slider->bounds.x, slider->bounds.y + (int32_t)(slider->bounds.height / 2U),
        slider->bounds.width, 3U}, color_shadow);
    uint32_t slider_offset = range_fraction(
        slider, &state->slider, slider->bounds.width > 12U
            ? slider->bounds.width - 12U : 1U);
    bevel((reist_gui_rect_t){
        slider->bounds.x + (int32_t)slider_offset, slider->bounds.y,
        12U, slider->bounds.height}, color_face, 1U);
    if (state->slider.focused) outline((reist_gui_rect_t){
        slider->bounds.x - 2, slider->bounds.y - 2,
        slider->bounds.width + 4U, slider->bounds.height + 4U}, color_dark);

    const reist_gui_range_model_t *spin = &state->spin_model;
    text(display, spin->bounds.x,
         spin->bounds.y - (int32_t)display->font_height - 3,
         "SpinBox: Anzahl", spin->bounds.width, color_text, color_face);
    bevel(spin->bounds, color_light, 0U);
    char number[16];
    decimal_text(state->spin.value, number);
    text(display, spin->bounds.x + 6,
         spin->bounds.y + (int32_t)((spin->bounds.height >
             display->font_height ? spin->bounds.height -
             display->font_height : 0U) / 2U),
         number, spin->bounds.width > 12U ? spin->bounds.width - 12U : 1U,
         color_text, color_light);
    if (state->spin.focused) outline((reist_gui_rect_t){
        spin->bounds.x - 2, spin->bounds.y - 2,
        spin->bounds.width + 4U, spin->bounds.height + 4U}, color_dark);

    const reist_gui_range_model_t *progress = &state->progress_model;
    text(display, progress->bounds.x,
         progress->bounds.y - (int32_t)display->font_height - 3,
         "Fortschrittsanzeige", progress->bounds.width, color_text, color_face);
    bevel(progress->bounds, color_light, 0U);
    uint32_t progress_width = range_fraction(
        progress, &state->progress,
        progress->bounds.width > 6U ? progress->bounds.width - 6U : 1U);
    fill((reist_gui_rect_t){
        progress->bounds.x + 3, progress->bounds.y + 3,
        progress_width,
        progress->bounds.height > 6U ? progress->bounds.height - 6U : 1U},
        color_active);
}

static void render_gallery(const x86os_display_info_t *display,
                           const gallery_state_t *state) {
    reist_gui_rect_t frame = gallery_frame(display);
    fill((reist_gui_rect_t){
        frame.x + 5, frame.y + 5, frame.width, frame.height}, color_dark);
    bevel(frame, color_face, 1U);
    reist_gui_rect_t title = {
        frame.x + 3, frame.y + 3,
        frame.width > 6U ? frame.width - 6U : 1U,
        menu_height(display)};
    fill(title, color_active);
    text(display, title.x + 10,
         title.y + (int32_t)((title.height - display->font_height) / 2U),
         "REIST GUI Control Gallery", title.width > 20U ? title.width - 20U : 1U,
         color_title_text, color_active);
    render_tabs(display, state);
    uint32_t page = selected_page(state);
    if (page == GALLERY_PAGE_BASIS) render_basic_controls(display, state);
    else if (page == GALLERY_PAGE_INPUT) render_text_page(display, state);
    else if (page == GALLERY_PAGE_SELECTION) render_list_page(display, state);
    else if (page == GALLERY_PAGE_VALUES) render_values_page(display, state);
}

static void render_dialog(const x86os_display_info_t *display,
                          const gallery_state_t *state) {
    if (!state->dialog.visible) return;
    const reist_gui_dialog_model_t *model =
        dialog_model(state->dialog_kind);
    reist_gui_dialog_layout_t layout = dialog_layout(display);
    reist_gui_rect_t frame;
    reist_gui_rect_t title;
    reist_gui_rect_t close;
    if (model == 0 || reist_gui_dialog_frame_rect(
            model, &layout, &state->dialog, &frame) != 0 ||
        reist_gui_dialog_title_rect(
            model, &layout, &state->dialog, &title) != 0 ||
        reist_gui_dialog_close_rect(
            model, &layout, &state->dialog, &close) != 0)
        return;
    fill((reist_gui_rect_t){
        frame.x + 5, frame.y + 5, frame.width, frame.height}, color_dark);
    bevel(frame, color_face, 1U);
    uint32_t title_color = state->dialog.active
        ? color_active : color_inactive;
    fill(title, title_color);
    bevel(close, color_face,
          state->dialog.capture_kind != REIST_GUI_DIALOG_CAPTURE_CLOSE);
    if (close.width > 8U && close.height > 8U)
        fill((reist_gui_rect_t){
            close.x + 4, close.y + 4,
            close.width - 8U, close.height - 8U}, color_dark);
    uint32_t title_y = title.height > display->font_height
        ? (title.height - display->font_height) / 2U : 0U;
    text(display, title.x + (int32_t)close.width + 8,
         title.y + (int32_t)title_y, model->title,
         title.width > close.width + 16U
            ? title.width - close.width - 16U : 1U,
         color_title_text, title_color);
    int32_t content_x = frame.x + 14;
    int32_t content_y = title.y + (int32_t)title.height + 14;
    uint32_t content_width = frame.width > 28U ? frame.width - 28U : 1U;
    text(display, content_x, content_y, model->message, content_width,
         color_text, color_face);
    if (model->detail != 0)
        text(display, content_x,
             content_y + (int32_t)max_u32(display->font_height + 6U, 20U),
             model->detail, content_width, color_shadow, color_face);

    for (uint32_t index = 0U; index < model->button_count; ++index) {
        reist_gui_rect_t button;
        if (reist_gui_dialog_button_rect(
                model, &layout, &state->dialog, index, &button) != 0)
            continue;
        uint32_t pressed =
            state->dialog.capture_kind == REIST_GUI_DIALOG_CAPTURE_BUTTON &&
            state->dialog.capture_button == index &&
            state->dialog.hot_button == index;
        if (state->dialog.focused_button == index)
            bevel((reist_gui_rect_t){
                button.x - 2, button.y - 2,
                button.width + 4U, button.height + 4U}, color_dark, 0U);
        bevel(button, color_face, !pressed);
        size_t length = bounded_length(
            model->buttons[index].label, REIST_GUI_DIALOG_LABEL_LIMIT);
        uint32_t label_width = (uint32_t)length * display->font_width;
        text(display,
             button.x + (int32_t)((button.width > label_width
                ? button.width - label_width : 0U) / 2U),
             button.y + (int32_t)((button.height > display->font_height
                ? button.height - display->font_height : 0U) / 2U),
             model->buttons[index].label, button.width,
             color_text, color_face);
    }
}

static void render_scene(const x86os_display_info_t *display,
                         const gallery_state_t *state) {
    fill((reist_gui_rect_t){0, 0, display->width, display->height},
         color_desktop);
    render_gallery(display, state);
    render_menu(display, state);
    render_dialog(display, state);
}

static int render(reist_gui_surface_client_t *client,
                  const x86os_display_info_t *display,
                  const gallery_state_t *state) {
    gallery_surface = client;
    gallery_paint_failed = 0U;
    if (reist_gui_surface_client_paint_begin(client) != 0) return -1;
    render_scene(display, state);
    if (gallery_paint_failed != 0U) return -1;
    return reist_gui_surface_client_paint_commit(client);
}

static int initialize(gallery_state_t *state,
                      const x86os_display_info_t *display) {
    reist_gui_menu_state_initialize(&state->menu);
    reist_gui_dialog_state_initialize(&state->dialog);
    reist_gui_control_state_initialize(&state->control);
    reist_gui_tabs_state_initialize(&state->tabs);
    reist_gui_text_state_initialize(&state->text_field);
    reist_gui_list_state_initialize(&state->list);
    reist_gui_range_state_initialize(&state->scrollbar);
    reist_gui_range_state_initialize(&state->slider);
    reist_gui_range_state_initialize(&state->spin);
    reist_gui_range_state_initialize(&state->progress);

    reist_gui_rect_t frame = gallery_frame(display);
    uint32_t title_height = menu_height(display);
    uint32_t tab_height = max_u32(display->font_height + 6U, 22U);
    reist_gui_rect_t tab_bar = {
        frame.x + 8,
        frame.y + 3 + (int32_t)title_height + 8,
        frame.width > 16U ? frame.width - 16U : 1U,
        tab_height};
    int32_t content_bottom = frame.y + (int32_t)frame.height - 8;
    reist_gui_rect_t content = {
        tab_bar.x, tab_bar.y + (int32_t)tab_bar.height,
        tab_bar.width,
        content_bottom > tab_bar.y + (int32_t)tab_bar.height
            ? (uint32_t)(content_bottom - tab_bar.y -
                         (int32_t)tab_bar.height) : 1U};
    uint32_t tab_width = tab_bar.width / 4U;
    state->tab_items[0] = (reist_gui_tab_t){
        GALLERY_TAB_BASIS, GALLERY_PAGE_BASIS, "Basis", tab_width,
        REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}};
    state->tab_items[1] = (reist_gui_tab_t){
        GALLERY_TAB_INPUT, GALLERY_PAGE_INPUT, "Eingabe", tab_width,
        REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}};
    state->tab_items[2] = (reist_gui_tab_t){
        GALLERY_TAB_SELECTION, GALLERY_PAGE_SELECTION, "Auswahl", tab_width,
        REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}};
    state->tab_items[3] = (reist_gui_tab_t){
        GALLERY_TAB_VALUES, GALLERY_PAGE_VALUES, "Werte",
        tab_bar.width - tab_width * 3U,
        REIST_GUI_TAB_VISIBLE | REIST_GUI_TAB_ENABLED, {0U, 0U, 0U}};
    state->tab_model = (reist_gui_tabs_model_t){
        REIST_GUI_TABS_API_VERSION, sizeof(reist_gui_tabs_model_t),
        state->tab_items, 4U, tab_bar, content, 3U,
        {0U, 0U, 0U, 0U}};
    reist_gui_tabs_result_t tab_result;
    reist_gui_tabs_result_initialize(&tab_result);
    if (reist_gui_tabs_configure(
            &state->tab_model, &state->tabs,
            GALLERY_TAB_BASIS, &tab_result) != 0) return -1;

    uint32_t row = max_u32(display->font_height + 4U, 20U);
    int32_t left = content.x + 14;
    int32_t top = content.y + 6;
    uint32_t width = content.width > 28U ? content.width - 28U : 1U;
    if (width > 300U) width = 300U;
    uint32_t button_width = width > 208U ? 96U : width / 2U - 4U;
    state->controls[0] = (reist_gui_control_t){
        GALLERY_CONTROL_LABEL, REIST_GUI_CONTROL_ROLE_LABEL,
        "Label: Zustand und Eingabe", {left, top, width, row},
        0U, 0U, REIST_GUI_CONTROL_VISIBLE,
        REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}};
    state->controls[1] = (reist_gui_control_t){
        GALLERY_CONTROL_BUTTON, REIST_GUI_CONTROL_ROLE_PUSH_BUTTON,
        "Dialog", {left, top + (int32_t)row, button_width, row + 4U},
        GALLERY_CONTROL_ACTION_DIALOG, 0U,
        REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED |
            REIST_GUI_CONTROL_DEFAULT,
        REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}};
    state->controls[2] = (reist_gui_control_t){
        GALLERY_CONTROL_CHECKBOX, REIST_GUI_CONTROL_ROLE_CHECKBOX,
        "Effekte aktiv", {left, top + (int32_t)(row * 2U) + 8,
                           width, row},
        0U, 0U, REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED |
            REIST_GUI_CONTROL_TRISTATE,
        REIST_GUI_CONTROL_MIXED, {0U, 0U}};
    state->controls[3] = (reist_gui_control_t){
        GALLERY_CONTROL_RADIO_CLASSIC,
        REIST_GUI_CONTROL_ROLE_RADIO_BUTTON,
        "Theme: Klassisch", {left, top + (int32_t)(row * 3U) + 8,
                              width, row},
        0U, 1U, REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED,
        REIST_GUI_CONTROL_CHECKED, {0U, 0U}};
    state->controls[4] = (reist_gui_control_t){
        GALLERY_CONTROL_RADIO_CONTRAST,
        REIST_GUI_CONTROL_ROLE_RADIO_BUTTON,
        "Theme: Kontrast", {left, top + (int32_t)(row * 4U) + 8,
                             width, row},
        0U, 1U, REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED,
        REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}};
    state->controls[5] = (reist_gui_control_t){
        GALLERY_CONTROL_DISABLED, REIST_GUI_CONTROL_ROLE_PUSH_BUTTON,
        "Gesperrt", {left + (int32_t)button_width + 10,
                       top + (int32_t)row, button_width, row + 4U},
        0U, 0U, REIST_GUI_CONTROL_VISIBLE,
        REIST_GUI_CONTROL_UNCHECKED, {0U, 0U}};
    state->control_model = (reist_gui_control_model_t){
        .version = REIST_GUI_CONTROL_API_VERSION,
        .struct_size = sizeof(reist_gui_control_model_t),
        .controls = state->controls,
        .control_count = GALLERY_CONTROL_COUNT,
        .surface_width = display->width,
        .surface_height = display->height,
        .damage_margin = 4U,
    };
    reist_gui_control_result_t configured;
    reist_gui_control_result_initialize(&configured);
    if (reist_gui_control_configure(
            &state->control_model, &state->control, &configured) != 0)
        return -1;

    state->text_model = (reist_gui_text_model_t){
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_text_model_t),
        GALLERY_TEXT_NAME, "Einzeiliges Textfeld",
        {content.x + 16,
         content.y + (int32_t)display->font_height + 10,
         content.width > 32U ? content.width - 32U : 1U, 26U},
        48U, display->font_width,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}};
    reist_gui_value_result_t value_result;
    reist_gui_value_result_initialize(&value_result);
    if (reist_gui_text_configure(
            &state->text_model, &state->text_field,
            "REIST Desktop", &value_result) != 0) return -1;

    uint32_t list_row = max_u32(display->font_height + 4U, 20U);
    uint32_t list_width = content.width > 76U ? content.width - 76U : 1U;
    state->list_model = (reist_gui_list_model_t){
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_list_model_t),
        GALLERY_LIST_THEME, "Theme-Auswahl", gallery_list_items, 4U,
        {content.x + 16,
         content.y + (int32_t)display->font_height + 10,
         list_width, list_row * 4U},
        list_row,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}};
    reist_gui_value_result_initialize(&value_result);
    if (reist_gui_list_configure(
            &state->list_model, &state->list, 1U, &value_result) != 0)
        return -1;
    state->scrollbar_model = (reist_gui_range_model_t){
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_range_model_t),
        GALLERY_SCROLLBAR, "Vertikale Scrollbar",
        {state->list_model.bounds.x + (int32_t)state->list_model.bounds.width + 10,
         state->list_model.bounds.y, 24U, state->list_model.bounds.height},
        0, 100, 5U, 25U, REIST_GUI_RANGE_SCROLLBAR, REIST_GUI_VERTICAL,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}};
    reist_gui_value_result_initialize(&value_result);
    if (reist_gui_range_configure(
            &state->scrollbar_model, &state->scrollbar, 25,
            &value_result) != 0) return -1;

    int32_t slider_y = content.y + (int32_t)display->font_height + 4;
    state->slider_model = (reist_gui_range_model_t){
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_range_model_t),
        GALLERY_SLIDER, "Lautstaerke",
        {content.x + 16, slider_y,
         content.width > 32U ? content.width - 32U : 1U, 18U},
        0, 100, 5U, 20U, REIST_GUI_RANGE_SLIDER, REIST_GUI_HORIZONTAL,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}};
    state->spin_model = (reist_gui_range_model_t){
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_range_model_t),
        GALLERY_SPIN, "Anzahl",
        {content.x + 16,
         slider_y + (int32_t)display->font_height + 21,
         content.width > 120U ? 104U : content.width - 32U, 22U},
        0, 99, 1U, 10U, REIST_GUI_RANGE_SPIN_BOX, REIST_GUI_HORIZONTAL,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED,
        {0U, 0U, 0U, 0U}};
    state->progress_model = (reist_gui_range_model_t){
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_range_model_t),
        GALLERY_PROGRESS, "Fortschrittsanzeige",
        {content.x + 16,
         state->spin_model.bounds.y + (int32_t)display->font_height + 25,
         content.width > 32U ? content.width - 32U : 1U, 20U},
        0, 100, 1U, 10U, REIST_GUI_RANGE_PROGRESS, REIST_GUI_HORIZONTAL,
        REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED |
            REIST_GUI_VALUE_READ_ONLY,
        {0U, 0U, 0U, 0U}};
    reist_gui_value_result_initialize(&value_result);
    if (reist_gui_range_configure(
            &state->slider_model, &state->slider, 60, &value_result) != 0)
        return -1;
    reist_gui_value_result_initialize(&value_result);
    if (reist_gui_range_configure(
            &state->spin_model, &state->spin, 5, &value_result) != 0)
        return -1;
    reist_gui_value_result_initialize(&value_result);
    if (reist_gui_range_configure(
            &state->progress_model, &state->progress, 40,
            &value_result) != 0) return -1;

    uint32_t node_flags = REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED;
    state->tree_nodes[0] = (reist_gui_node_t){
        GALLERY_NODE_ROOT, 0U, REIST_GUI_NODE_CONTAINER, 0U, "Gallery root",
        {0, 0, display->width, display->height}, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[1] = (reist_gui_node_t){
        GALLERY_NODE_BASIS_PAGE, GALLERY_NODE_ROOT,
        REIST_GUI_NODE_CONTAINER, 0U, "Basis page", content, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[2] = (reist_gui_node_t){
        GALLERY_NODE_BASIS_GROUP, GALLERY_NODE_BASIS_PAGE,
        REIST_GUI_NODE_CONTAINER, 0U, "Basis controls",
        {8, 4, content.width > 16U ? content.width - 16U : 1U,
         content.height > 8U ? content.height - 8U : 1U}, node_flags,
        {0U, 0U, 0U, 0U}};
    for (uint32_t index = 0U; index < GALLERY_CONTROL_COUNT; ++index) {
        reist_gui_rect_t bounds = state->controls[index].bounds;
        state->tree_nodes[3U + index] = (reist_gui_node_t){
            GALLERY_NODE_BASIS_FIRST + index, GALLERY_NODE_BASIS_GROUP,
            REIST_GUI_NODE_CONTROL, state->controls[index].id,
            state->controls[index].label,
            {bounds.x - content.x - 8, bounds.y - content.y - 4,
             bounds.width, bounds.height},
            (state->controls[index].flags & REIST_GUI_CONTROL_ENABLED) != 0U
                ? node_flags : REIST_GUI_NODE_VISIBLE,
            {0U, 0U, 0U, 0U}};
    }
    state->tree_nodes[9] = (reist_gui_node_t){
        GALLERY_NODE_INPUT_PAGE, GALLERY_NODE_ROOT,
        REIST_GUI_NODE_CONTAINER, 0U, "Input page", content, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[10] = (reist_gui_node_t){
        GALLERY_NODE_INPUT_GROUP, GALLERY_NODE_INPUT_PAGE,
        REIST_GUI_NODE_CONTAINER, 0U, "Text controls",
        {8, 4, content.width > 16U ? content.width - 16U : 1U,
         content.height > 8U ? content.height - 8U : 1U}, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[11] = (reist_gui_node_t){
        GALLERY_NODE_TEXT, GALLERY_NODE_INPUT_GROUP, REIST_GUI_NODE_CONTROL,
        GALLERY_TEXT_NAME, "Einzeiliges Textfeld",
        {state->text_model.bounds.x - content.x - 8,
         state->text_model.bounds.y - content.y - 4,
         state->text_model.bounds.width, state->text_model.bounds.height},
        node_flags, {0U, 0U, 0U, 0U}};
    state->tree_nodes[12] = (reist_gui_node_t){
        GALLERY_NODE_SELECTION_PAGE, GALLERY_NODE_ROOT,
        REIST_GUI_NODE_CONTAINER, 0U, "Selection page", content, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[13] = (reist_gui_node_t){
        GALLERY_NODE_SELECTION_GROUP, GALLERY_NODE_SELECTION_PAGE,
        REIST_GUI_NODE_CONTAINER, 0U, "Selection controls",
        {8, 4, content.width > 16U ? content.width - 16U : 1U,
         content.height > 8U ? content.height - 8U : 1U}, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[14] = (reist_gui_node_t){
        GALLERY_NODE_LIST, GALLERY_NODE_SELECTION_GROUP,
        REIST_GUI_NODE_CONTROL, GALLERY_LIST_THEME, "Theme list",
        {state->list_model.bounds.x - content.x - 8,
         state->list_model.bounds.y - content.y - 4,
         state->list_model.bounds.width, state->list_model.bounds.height},
        node_flags, {0U, 0U, 0U, 0U}};
    state->tree_nodes[15] = (reist_gui_node_t){
        GALLERY_NODE_SCROLLBAR, GALLERY_NODE_SELECTION_GROUP,
        REIST_GUI_NODE_CONTROL, GALLERY_SCROLLBAR, "Vertikale Scrollbar",
        {state->scrollbar_model.bounds.x - content.x - 8,
         state->scrollbar_model.bounds.y - content.y - 4,
         state->scrollbar_model.bounds.width,
         state->scrollbar_model.bounds.height}, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[16] = (reist_gui_node_t){
        GALLERY_NODE_VALUES_PAGE, GALLERY_NODE_ROOT,
        REIST_GUI_NODE_CONTAINER, 0U, "Values page", content, node_flags,
        {0U, 0U, 0U, 0U}};
    state->tree_nodes[17] = (reist_gui_node_t){
        GALLERY_NODE_VALUES_GROUP, GALLERY_NODE_VALUES_PAGE,
        REIST_GUI_NODE_CONTAINER, 0U, "Value controls",
        {8, 4, content.width > 16U ? content.width - 16U : 1U,
         content.height > 8U ? content.height - 8U : 1U}, node_flags,
        {0U, 0U, 0U, 0U}};
    const reist_gui_range_model_t *range_models[3] = {
        &state->slider_model, &state->spin_model, &state->progress_model};
    const uint32_t node_ids[3] = {
        GALLERY_NODE_SLIDER, GALLERY_NODE_SPIN, GALLERY_NODE_PROGRESS};
    for (uint32_t index = 0U; index < 3U; ++index) {
        const reist_gui_range_model_t *model = range_models[index];
        state->tree_nodes[18U + index] = (reist_gui_node_t){
            node_ids[index], GALLERY_NODE_VALUES_GROUP,
            REIST_GUI_NODE_CONTROL, model->id, model->name,
            {model->bounds.x - content.x - 8,
             model->bounds.y - content.y - 4,
             model->bounds.width, model->bounds.height}, node_flags,
            {0U, 0U, 0U, 0U}};
    }
    state->tree_model = (reist_gui_tree_model_t){
        REIST_GUI_TREE_API_VERSION, sizeof(reist_gui_tree_model_t),
        state->tree_nodes, 21U, {0U, 0U, 0U, 0U}};
    if (reist_gui_tree_validate(&state->tree_model) != 0) return -1;

    state->focus_target = GALLERY_FOCUS_TABS;
    state->dialog_kind = GALLERY_DIALOG_NONE;
    state->exit_requested = 0U;
    state->redraw = 1U;
    return 0;
}

static void open_dialog(gallery_state_t *state,
                        const x86os_display_info_t *display,
                        uint32_t kind) {
    reist_gui_dialog_layout_t layout = dialog_layout(display);
    if (state->dialog.visible) {
        const reist_gui_dialog_model_t *previous =
            dialog_model(state->dialog_kind);
        reist_gui_dialog_result_t closed;
        reist_gui_dialog_result_initialize(&closed);
        if (previous == 0 || reist_gui_dialog_complete(
                previous, &layout, &state->dialog,
                previous->cancel_response, &closed) != 0) {
            reist_gui_dialog_state_initialize(&state->dialog);
        }
    }
    const reist_gui_dialog_model_t *model = dialog_model(kind);
    reist_gui_dialog_result_t opened;
    reist_gui_dialog_result_initialize(&opened);
    state->dialog_kind = kind;
    if (model == 0 || reist_gui_dialog_open(
            model, &layout, &state->dialog, &opened) != 0) {
        reist_gui_dialog_state_initialize(&state->dialog);
        state->dialog_kind = GALLERY_DIALOG_NONE;
    }
    state->redraw = 1U;
}

static void set_focus_target(gallery_state_t *state, uint32_t target);

static uint32_t apply_dialog_result(
    gallery_state_t *state,
    const reist_gui_dialog_result_t *result) {
    if (result->damage_count != 0U || result->full_redraw)
        state->redraw = 1U;
    if (result->completed) state->dialog_kind = GALLERY_DIALOG_NONE;
    return result->consumed;
}

static void apply_menu_result(gallery_state_t *state,
                              const x86os_display_info_t *display,
                              const reist_gui_menu_result_t *result) {
    if (result->damage_count != 0U || result->full_redraw)
        state->redraw = 1U;
    if (!result->activated) return;
    if (result->action == GALLERY_ACTION_EXIT)
        state->exit_requested = 1U;
    else if (result->action == GALLERY_ACTION_MODELESS)
        open_dialog(state, display, GALLERY_DIALOG_MODELESS);
    else if (result->action == GALLERY_ACTION_MODAL)
        open_dialog(state, display, GALLERY_DIALOG_MODAL);
    else if (result->action == GALLERY_ACTION_ABOUT)
        open_dialog(state, display, GALLERY_DIALOG_ABOUT);
}

static uint32_t apply_control_result(
    gallery_state_t *state, const x86os_display_info_t *display,
    const reist_gui_control_result_t *result) {
    if (result->damage_count != 0U || result->full_redraw)
        state->redraw = 1U;
    if (result->focus_changed)
        set_focus_target(state, GALLERY_FOCUS_BASIC);
    if (result->activated &&
        result->action == GALLERY_CONTROL_ACTION_DIALOG)
        open_dialog(state, display, GALLERY_DIALOG_MODELESS);
    return result->consumed;
}

static void focus_event(reist_gui_value_event_t *event, uint32_t focused) {
    reist_gui_value_event_initialize(event);
    event->type = REIST_GUI_VALUE_EVENT_FOCUS;
    event->focused = focused;
}

static void set_focus_target(gallery_state_t *state, uint32_t target) {
    const uint32_t targets[] = {
        GALLERY_FOCUS_TEXT, GALLERY_FOCUS_LIST, GALLERY_FOCUS_SCROLLBAR,
        GALLERY_FOCUS_SLIDER, GALLERY_FOCUS_SPIN};
    for (uint32_t index = 0U; index < 5U; ++index) {
        if (targets[index] == target) continue;
        reist_gui_value_event_t event;
        reist_gui_value_result_t result;
        focus_event(&event, 0U);
        reist_gui_value_result_initialize(&result);
        if (targets[index] == GALLERY_FOCUS_TEXT)
            (void)reist_gui_text_dispatch(
                &state->text_model, &state->text_field, &event, &result);
        else if (targets[index] == GALLERY_FOCUS_LIST)
            (void)reist_gui_list_dispatch(
                &state->list_model, &state->list, &event, &result);
        else if (targets[index] == GALLERY_FOCUS_SCROLLBAR)
            (void)reist_gui_range_dispatch(
                &state->scrollbar_model, &state->scrollbar, &event, &result);
        else if (targets[index] == GALLERY_FOCUS_SLIDER)
            (void)reist_gui_range_dispatch(
                &state->slider_model, &state->slider, &event, &result);
        else if (targets[index] == GALLERY_FOCUS_SPIN)
            (void)reist_gui_range_dispatch(
                &state->spin_model, &state->spin, &event, &result);
        if (result.damage_count != 0U || result.full_redraw)
            state->redraw = 1U;
    }
    if (target >= GALLERY_FOCUS_TEXT && target <= GALLERY_FOCUS_SPIN) {
        reist_gui_value_event_t event;
        reist_gui_value_result_t result;
        focus_event(&event, 1U);
        reist_gui_value_result_initialize(&result);
        if (target == GALLERY_FOCUS_TEXT)
            (void)reist_gui_text_dispatch(
                &state->text_model, &state->text_field, &event, &result);
        else if (target == GALLERY_FOCUS_LIST)
            (void)reist_gui_list_dispatch(
                &state->list_model, &state->list, &event, &result);
        else if (target == GALLERY_FOCUS_SCROLLBAR)
            (void)reist_gui_range_dispatch(
                &state->scrollbar_model, &state->scrollbar, &event, &result);
        else if (target == GALLERY_FOCUS_SLIDER)
            (void)reist_gui_range_dispatch(
                &state->slider_model, &state->slider, &event, &result);
        else if (target == GALLERY_FOCUS_SPIN)
            (void)reist_gui_range_dispatch(
                &state->spin_model, &state->spin, &event, &result);
        if (result.damage_count != 0U || result.full_redraw)
            state->redraw = 1U;
    }
    state->focus_target = target;
}

static uint32_t apply_value_result(
    gallery_state_t *state, uint32_t target,
    const reist_gui_value_result_t *result) {
    if (result->damage_count != 0U || result->full_redraw)
        state->redraw = 1U;
    if (result->focus_changed) set_focus_target(state, target);
    if (target == GALLERY_FOCUS_SLIDER && result->changed) {
        reist_gui_value_result_t linked;
        reist_gui_value_result_initialize(&linked);
        if (reist_gui_range_set(
                &state->progress_model, &state->progress,
                result->value, &linked) == 0 &&
            (linked.damage_count != 0U || linked.full_redraw))
            state->redraw = 1U;
    }
    return result->consumed;
}

static uint32_t dispatch_tabs_pointer(
    gallery_state_t *state, int32_t x, int32_t y,
    uint32_t button_event, uint32_t pressed) {
    reist_gui_tabs_event_t event;
    reist_gui_tabs_event_initialize(&event);
    event.type = button_event ? REIST_GUI_TABS_EVENT_POINTER_BUTTON :
                               REIST_GUI_TABS_EVENT_POINTER_MOTION;
    event.x = x;
    event.y = y;
    event.button = button_event ? REIST_GUI_TABS_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    reist_gui_tabs_result_t result;
    reist_gui_tabs_result_initialize(&result);
    if (reist_gui_tabs_dispatch(
            &state->tab_model, &state->tabs, &event, &result) != 0)
        return 1U;
    if (result.damage_count != 0U || result.full_redraw)
        state->redraw = 1U;
    if (result.focus_changed && button_event && pressed)
        set_focus_target(state, GALLERY_FOCUS_TABS);
    if (result.selection_changed) {
        set_focus_target(state, GALLERY_FOCUS_TABS);
        state->redraw = 1U;
    }
    return result.consumed;
}

static uint32_t dispatch_value_pointer(
    gallery_state_t *state, uint32_t target,
    int32_t x, int32_t y, uint32_t button_event, uint32_t pressed) {
    reist_gui_value_event_t event;
    reist_gui_value_event_initialize(&event);
    event.type = button_event ? REIST_GUI_VALUE_EVENT_POINTER_BUTTON :
                               REIST_GUI_VALUE_EVENT_POINTER_MOTION;
    event.x = x;
    event.y = y;
    event.button = button_event ? REIST_GUI_VALUE_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    reist_gui_value_result_t result;
    reist_gui_value_result_initialize(&result);
    int status;
    if (target == GALLERY_FOCUS_TEXT)
        status = reist_gui_text_dispatch(
            &state->text_model, &state->text_field, &event, &result);
    else if (target == GALLERY_FOCUS_LIST)
        status = reist_gui_list_dispatch(
            &state->list_model, &state->list, &event, &result);
    else if (target == GALLERY_FOCUS_SCROLLBAR)
        status = reist_gui_range_dispatch(
            &state->scrollbar_model, &state->scrollbar, &event, &result);
    else if (target == GALLERY_FOCUS_SLIDER)
        status = reist_gui_range_dispatch(
            &state->slider_model, &state->slider, &event, &result);
    else status = reist_gui_range_dispatch(
        &state->spin_model, &state->spin, &event, &result);
    return status == 0 ? apply_value_result(state, target, &result) : 1U;
}

static uint32_t dispatch_pointer(gallery_state_t *state,
                                 const x86os_display_info_t *display,
                                 int32_t x, int32_t y,
                                 uint32_t button_event,
                                 uint32_t pressed) {
    if (state->dialog.visible) {
        const reist_gui_dialog_model_t *model =
            dialog_model(state->dialog_kind);
        reist_gui_dialog_layout_t layout = dialog_layout(display);
        reist_gui_dialog_event_t event;
        reist_gui_dialog_event_initialize(&event);
        event.type = button_event
            ? REIST_GUI_DIALOG_EVENT_POINTER_BUTTON
            : REIST_GUI_DIALOG_EVENT_POINTER_MOTION;
        event.x = x;
        event.y = y;
        event.button = button_event ? REIST_GUI_DIALOG_BUTTON_LEFT : 0U;
        event.pressed = pressed;
        reist_gui_dialog_result_t result;
        reist_gui_dialog_result_initialize(&result);
        if (model == 0 || reist_gui_dialog_dispatch(
                model, &layout, &state->dialog, &event, &result) != 0) {
            reist_gui_dialog_state_initialize(&state->dialog);
            state->dialog_kind = GALLERY_DIALOG_NONE;
            state->redraw = 1U;
            return 1U;
        }
        if (apply_dialog_result(state, &result)) return 1U;
    }

    reist_gui_menu_layout_t layout = menu_layout(display);
    reist_gui_menu_event_t event;
    reist_gui_menu_event_initialize(&event);
    event.type = button_event
        ? REIST_GUI_MENU_EVENT_POINTER_BUTTON
        : REIST_GUI_MENU_EVENT_POINTER_MOTION;
    event.x = x;
    event.y = y;
    event.button = button_event ? REIST_GUI_MENU_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    reist_gui_menu_result_t result;
    reist_gui_menu_result_initialize(&result);
    if (reist_gui_menu_dispatch(
            &gallery_menu_model, &layout, &state->menu,
            &event, &result) != 0) {
        reist_gui_menu_state_initialize(&state->menu);
        state->redraw = 1U;
        return 1U;
    }
    apply_menu_result(state, display, &result);
    if (result.consumed) return 1U;

    if (dispatch_tabs_pointer(
            state, x, y, button_event, pressed)) return 1U;

    uint32_t page = selected_page(state);
    if (page == GALLERY_PAGE_INPUT)
        return dispatch_value_pointer(
            state, GALLERY_FOCUS_TEXT, x, y, button_event, pressed);
    if (page == GALLERY_PAGE_SELECTION) {
        if (dispatch_value_pointer(
                state, GALLERY_FOCUS_LIST,
                x, y, button_event, pressed)) return 1U;
        return dispatch_value_pointer(
            state, GALLERY_FOCUS_SCROLLBAR,
            x, y, button_event, pressed);
    }
    if (page == GALLERY_PAGE_VALUES) {
        if (dispatch_value_pointer(
                state, GALLERY_FOCUS_SLIDER,
                x, y, button_event, pressed)) return 1U;
        return dispatch_value_pointer(
            state, GALLERY_FOCUS_SPIN,
            x, y, button_event, pressed);
    }
    if (page != GALLERY_PAGE_BASIS) return 0U;

    reist_gui_control_event_t control_event;
    reist_gui_control_event_initialize(&control_event);
    control_event.type = button_event
        ? REIST_GUI_CONTROL_EVENT_POINTER_BUTTON
        : REIST_GUI_CONTROL_EVENT_POINTER_MOTION;
    control_event.x = x;
    control_event.y = y;
    control_event.button = button_event
        ? REIST_GUI_CONTROL_BUTTON_LEFT : 0U;
    control_event.pressed = pressed;
    reist_gui_control_result_t control_result;
    reist_gui_control_result_initialize(&control_result);
    if (reist_gui_control_dispatch(
            &state->control_model, &state->control,
            &control_event, &control_result) != 0) {
        state->redraw = 1U;
        return 1U;
    }
    return apply_control_result(state, display, &control_result);
}

static uint32_t dialog_key(int key) {
    if (key == GALLERY_KEY_LEFT || key == GALLERY_KEY_UP)
        return REIST_GUI_DIALOG_KEY_PREVIOUS;
    if (key == GALLERY_KEY_RIGHT || key == GALLERY_KEY_DOWN || key == '\t')
        return REIST_GUI_DIALOG_KEY_NEXT;
    if (key == '\r' || key == '\n') return REIST_GUI_DIALOG_KEY_ENTER;
    if (key == GALLERY_KEY_ESCAPE) return REIST_GUI_DIALOG_KEY_ESCAPE;
    return 0U;
}

static uint32_t menu_key(int key) {
    if (key == GALLERY_KEY_LEFT) return REIST_GUI_MENU_KEY_LEFT;
    if (key == GALLERY_KEY_RIGHT || key == '\t')
        return REIST_GUI_MENU_KEY_RIGHT;
    if (key == GALLERY_KEY_UP) return REIST_GUI_MENU_KEY_UP;
    if (key == GALLERY_KEY_DOWN) return REIST_GUI_MENU_KEY_DOWN;
    if (key == '\r' || key == '\n') return REIST_GUI_MENU_KEY_ENTER;
    if (key == GALLERY_KEY_ESCAPE) return REIST_GUI_MENU_KEY_ESCAPE;
    return 0U;
}

static uint32_t control_key(int key) {
    if (key == '\t') return REIST_GUI_CONTROL_KEY_NEXT;
    if (key == ' ') return REIST_GUI_CONTROL_KEY_SPACE;
    if (key == '\r' || key == '\n') return REIST_GUI_CONTROL_KEY_ENTER;
    if (key == GALLERY_KEY_LEFT) return REIST_GUI_CONTROL_KEY_LEFT;
    if (key == GALLERY_KEY_RIGHT) return REIST_GUI_CONTROL_KEY_RIGHT;
    if (key == GALLERY_KEY_UP) return REIST_GUI_CONTROL_KEY_UP;
    if (key == GALLERY_KEY_DOWN) return REIST_GUI_CONTROL_KEY_DOWN;
    return 0U;
}

static uint32_t tabs_key(int key) {
    if (key == GALLERY_KEY_LEFT) return REIST_GUI_TABS_KEY_LEFT;
    if (key == GALLERY_KEY_RIGHT) return REIST_GUI_TABS_KEY_RIGHT;
    if (key == '\r' || key == '\n') return REIST_GUI_TABS_KEY_ENTER;
    if (key == ' ') return REIST_GUI_TABS_KEY_SPACE;
    return 0U;
}

static uint32_t value_key(int key) {
    if (key == GALLERY_KEY_LEFT) return REIST_GUI_VALUE_KEY_LEFT;
    if (key == GALLERY_KEY_RIGHT) return REIST_GUI_VALUE_KEY_RIGHT;
    if (key == GALLERY_KEY_UP) return REIST_GUI_VALUE_KEY_UP;
    if (key == GALLERY_KEY_DOWN) return REIST_GUI_VALUE_KEY_DOWN;
    if (key == 8 || key == 127) return REIST_GUI_VALUE_KEY_BACKSPACE;
    if (key == '\r' || key == '\n') return REIST_GUI_VALUE_KEY_ENTER;
    return 0U;
}

static uint32_t dispatch_tabs_keyboard(gallery_state_t *state, int key) {
    uint32_t mapped = tabs_key(key);
    if (mapped == 0U) return 0U;
    reist_gui_tabs_event_t event;
    reist_gui_tabs_event_initialize(&event);
    event.type = REIST_GUI_TABS_EVENT_KEYBOARD;
    event.key = mapped;
    reist_gui_tabs_result_t result;
    reist_gui_tabs_result_initialize(&result);
    if (reist_gui_tabs_dispatch(
            &state->tab_model, &state->tabs, &event, &result) != 0)
        return 1U;
    if (result.damage_count != 0U || result.full_redraw ||
        result.selection_changed) state->redraw = 1U;
    return result.consumed;
}

static uint32_t dispatch_value_keyboard(
    gallery_state_t *state, uint32_t target, int key) {
    reist_gui_value_event_t event;
    reist_gui_value_event_initialize(&event);
    if (target == GALLERY_FOCUS_TEXT && key >= 0x20 && key <= 0x7E) {
        event.type = REIST_GUI_VALUE_EVENT_TEXT;
        event.codepoint = (uint32_t)key;
    } else {
        uint32_t mapped = value_key(key);
        if (mapped == 0U) return 0U;
        event.type = REIST_GUI_VALUE_EVENT_KEYBOARD;
        event.key = mapped;
    }
    reist_gui_value_result_t result;
    reist_gui_value_result_initialize(&result);
    int status;
    if (target == GALLERY_FOCUS_TEXT)
        status = reist_gui_text_dispatch(
            &state->text_model, &state->text_field, &event, &result);
    else if (target == GALLERY_FOCUS_LIST)
        status = reist_gui_list_dispatch(
            &state->list_model, &state->list, &event, &result);
    else if (target == GALLERY_FOCUS_SCROLLBAR)
        status = reist_gui_range_dispatch(
            &state->scrollbar_model, &state->scrollbar, &event, &result);
    else if (target == GALLERY_FOCUS_SLIDER)
        status = reist_gui_range_dispatch(
            &state->slider_model, &state->slider, &event, &result);
    else status = reist_gui_range_dispatch(
        &state->spin_model, &state->spin, &event, &result);
    return status == 0 ? apply_value_result(state, target, &result) : 1U;
}

static uint32_t focus_first_basic(
    gallery_state_t *state, const x86os_display_info_t *display) {
    reist_gui_control_event_t event;
    reist_gui_control_event_initialize(&event);
    event.type = REIST_GUI_CONTROL_EVENT_KEYBOARD;
    event.key = REIST_GUI_CONTROL_KEY_NEXT;
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    if (reist_gui_control_dispatch(
            &state->control_model, &state->control,
            &event, &result) != 0) return 1U;
    return apply_control_result(state, display, &result);
}

static uint32_t last_basic_focus(const gallery_state_t *state) {
    for (uint32_t step = 0U; step < state->control_model.control_count; ++step) {
        uint32_t index = state->control_model.control_count - 1U - step;
        const reist_gui_control_t *control = &state->controls[index];
        if ((control->flags & (REIST_GUI_CONTROL_VISIBLE |
                               REIST_GUI_CONTROL_ENABLED)) !=
            (REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED) ||
            control->role == REIST_GUI_CONTROL_ROLE_LABEL) continue;
        if (control->role != REIST_GUI_CONTROL_ROLE_RADIO_BUTTON ||
            state->control.check[index] == REIST_GUI_CONTROL_CHECKED)
            return index;
    }
    return REIST_GUI_CONTROL_NO_INDEX;
}

static uint32_t dispatch_keyboard(gallery_state_t *state,
                                  const x86os_display_info_t *display,
                                  int key) {
    if (state->dialog.visible) {
        uint32_t mapped = dialog_key(key);
        if (mapped != 0U) {
            const reist_gui_dialog_model_t *model =
                dialog_model(state->dialog_kind);
            reist_gui_dialog_layout_t layout = dialog_layout(display);
            reist_gui_dialog_event_t event;
            reist_gui_dialog_event_initialize(&event);
            event.type = REIST_GUI_DIALOG_EVENT_KEYBOARD;
            event.key = mapped;
            reist_gui_dialog_result_t result;
            reist_gui_dialog_result_initialize(&result);
            if (model != 0 && reist_gui_dialog_dispatch(
                    model, &layout, &state->dialog,
                    &event, &result) == 0 &&
                apply_dialog_result(state, &result))
                return 1U;
        } else if (state->dialog.active ||
                   state->dialog.modality != REIST_GUI_DIALOG_MODELESS) {
            return 1U;
        }
    }
    if (state->menu.open_menu != REIST_GUI_MENU_NO_INDEX) {
        uint32_t mapped = menu_key(key);
        if (mapped == 0U) return 1U;
        reist_gui_menu_layout_t layout = menu_layout(display);
        reist_gui_menu_event_t event;
        reist_gui_menu_event_initialize(&event);
        event.type = REIST_GUI_MENU_EVENT_KEYBOARD;
        event.key = mapped;
        reist_gui_menu_result_t result;
        reist_gui_menu_result_initialize(&result);
        if (reist_gui_menu_dispatch(
                &gallery_menu_model, &layout, &state->menu,
                &event, &result) != 0) {
            reist_gui_menu_state_initialize(&state->menu);
            state->redraw = 1U;
            return 1U;
        }
        apply_menu_result(state, display, &result);
        return result.consumed;
    }

    if (state->focus_target == GALLERY_FOCUS_TABS) {
        if (key == '\t') {
            uint32_t page = selected_page(state);
            if (page == GALLERY_PAGE_BASIS)
                return focus_first_basic(state, display);
            set_focus_target(state,
                page == GALLERY_PAGE_INPUT ? GALLERY_FOCUS_TEXT :
                page == GALLERY_PAGE_SELECTION ? GALLERY_FOCUS_LIST :
                GALLERY_FOCUS_SLIDER);
            state->redraw = 1U;
            return 1U;
        }
        return dispatch_tabs_keyboard(state, key);
    }
    if (key == '\t' && state->focus_target != GALLERY_FOCUS_BASIC) {
        uint32_t next = GALLERY_FOCUS_TABS;
        if (state->focus_target == GALLERY_FOCUS_LIST)
            next = GALLERY_FOCUS_SCROLLBAR;
        else if (state->focus_target == GALLERY_FOCUS_SLIDER)
            next = GALLERY_FOCUS_SPIN;
        set_focus_target(state, next);
        state->redraw = 1U;
        return 1U;
    }
    if (state->focus_target >= GALLERY_FOCUS_TEXT &&
        state->focus_target <= GALLERY_FOCUS_SPIN)
        return dispatch_value_keyboard(state, state->focus_target, key);

    if (key == '\t' && state->focus_target == GALLERY_FOCUS_BASIC &&
        state->control.focused == last_basic_focus(state)) {
        set_focus_target(state, GALLERY_FOCUS_TABS);
        state->redraw = 1U;
        return 1U;
    }

    uint32_t mapped = control_key(key);
    if (mapped == 0U) return 0U;
    reist_gui_control_event_t event;
    reist_gui_control_event_initialize(&event);
    event.type = REIST_GUI_CONTROL_EVENT_KEYBOARD;
    event.key = mapped;
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    if (reist_gui_control_dispatch(
            &state->control_model, &state->control,
            &event, &result) != 0) return 1U;
    return apply_control_result(state, display, &result);
}

static void display_from_surface(x86os_display_info_t *display,
                                 const reist_gui_surface_client_t *client) {
    display->version = X86OS_DISPLAY_ABI_VERSION;
    display->struct_size = sizeof(*display);
    display->width = client->width;
    display->height = client->height;
    display->pitch = client->width * 4U;
    display->bits_per_pixel = 32U;
    display->red_field_position = 16U;
    display->red_mask_size = 8U;
    display->green_field_position = 8U;
    display->green_mask_size = 8U;
    display->blue_field_position = 0U;
    display->blue_mask_size = 8U;
    display->font_width = GALLERY_FONT_WIDTH;
    display->font_height = GALLERY_FONT_HEIGHT;
}

static int validate_gallery(const gallery_state_t *state,
                            const x86os_display_info_t *display) {
    reist_gui_menu_layout_t menu_metrics = menu_layout(display);
    reist_gui_dialog_layout_t dialog_metrics = dialog_layout(display);
    return reist_gui_menu_validate(
            &gallery_menu_model, &menu_metrics, &state->menu) != 0 ||
        reist_gui_dialog_validate(
            &modeless_dialog_model, &dialog_metrics, &state->dialog) != 0 ||
        reist_gui_dialog_validate(
            &modal_dialog_model, &dialog_metrics, &state->dialog) != 0 ||
        reist_gui_dialog_validate(
            &about_dialog_model, &dialog_metrics, &state->dialog) != 0 ||
        reist_gui_control_validate(
            &state->control_model, &state->control) != 0 ||
        reist_gui_tabs_validate(&state->tab_model, &state->tabs) != 0 ||
        reist_gui_tree_validate(&state->tree_model) != 0 ||
        reist_gui_text_validate(&state->text_model, &state->text_field) != 0 ||
        reist_gui_list_validate(&state->list_model, &state->list) != 0 ||
        reist_gui_range_validate(
            &state->scrollbar_model, &state->scrollbar) != 0 ||
        reist_gui_range_validate(&state->slider_model, &state->slider) != 0 ||
        reist_gui_range_validate(&state->spin_model, &state->spin) != 0 ||
        reist_gui_range_validate(&state->progress_model, &state->progress) != 0
        ? -1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && argv != 0 && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: guidemo --reist-surface=<handle>\n");
        return 0;
    }
    x86os_ipc_handle_t endpoint = 0U;
    if (argc != 2 ||
        reist_gui_surface_endpoint_from_argv(argc, argv, &endpoint) != 0) {
        x86os_puts("guidemo: compositor endpoint required\n");
        return 2;
    }

    reist_gui_surface_client_t client;
    int result = reist_gui_surface_client_init(&client, endpoint);
    if (result == 0) {
        result = -9;
        for (uint32_t attempt = 0U;
             attempt < GALLERY_SURFACE_CREATE_ATTEMPTS; ++attempt) {
            result = reist_gui_surface_client_create(
                &client, REIST_GUI_SURFACE_ROLE_TOPLEVEL,
                GALLERY_DEFAULT_WIDTH, GALLERY_DEFAULT_HEIGHT);
            if (result == 0 || (result != -9 && result != -13)) break;
            (void)x86os_sleep_ms(1U);
        }
    }
    if (result == 0)
        result = reist_gui_surface_client_ack_configure(
            &client, client.configured_serial);
    if (result == 0)
        result = reist_gui_surface_client_set_title(
            &client, "REIST GUI Control Gallery");
    if (result != 0 || client.width < GALLERY_MIN_WIDTH ||
        client.height < GALLERY_MIN_HEIGHT) {
        if (result == 0) (void)reist_gui_surface_client_destroy(&client);
        (void)x86os_ipc_release(endpoint);
        return 1;
    }

    static gallery_state_t state;
    x86os_display_info_t display;
    display_from_surface(&display, &client);
    if (initialize(&state, &display) != 0 ||
        validate_gallery(&state, &display) != 0) {
        x86os_puts("guidemo: Control-API/Layout nicht kompatibel\n");
        (void)reist_gui_surface_client_destroy(&client);
        (void)x86os_ipc_release(endpoint);
        return 1;
    }
    x86os_puts("GUIDEMO_OK\n");
    result = render(&client, &display, &state);
    if (result != 0) {
        (void)reist_gui_surface_client_destroy(&client);
        (void)x86os_ipc_release(endpoint);
        return 1;
    }
    state.redraw = 0U;
    x86os_puts("GUIDEMO_SURFACE_READY\n");

    while (!state.exit_requested) {
        uint32_t processed = 0U;
        for (; processed < GALLERY_SURFACE_EVENT_BATCH_LIMIT; ++processed) {
            reist_gui_surface_message_t message;
            int receive = reist_gui_surface_client_receive(
                &client, &message, 0U);
            if (receive == -11) break;
            if (receive != 0) {
                result = receive;
                state.exit_requested = 1U;
                break;
            }
            if (message.type == REIST_GUI_SURFACE_CLOSE) {
                state.exit_requested = 1U;
            } else if (message.type == REIST_GUI_SURFACE_CONFIGURE) {
                result = reist_gui_surface_client_accept_configure(
                    &client, &message);
                if (result == 0 && client.width >= GALLERY_MIN_WIDTH &&
                    client.height >= GALLERY_MIN_HEIGHT) {
                    display_from_surface(&display, &client);
                    result = initialize(&state, &display);
                    if (result == 0)
                        result = validate_gallery(&state, &display);
                } else if (result == 0) result = -22;
                if (result != 0) state.exit_requested = 1U;
                else state.redraw = 1U;
            } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                       message.input.type ==
                           REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
                (void)dispatch_pointer(
                    &state, &display, message.input.x, message.input.y,
                    0U, 0U);
            } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                       message.input.type ==
                           REIST_GUI_SURFACE_INPUT_POINTER_BUTTON) {
                (void)dispatch_pointer(
                    &state, &display, message.input.x, message.input.y,
                    1U, message.input.pressed);
            } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                       message.input.type ==
                           REIST_GUI_SURFACE_INPUT_KEYBOARD &&
                       message.input.pressed) {
                int key = (int)message.input.key;
                uint32_t consumed = dispatch_keyboard(
                    &state, &display, key);
                if (!consumed && key == GALLERY_KEY_ESCAPE)
                    state.exit_requested = 1U;
            }
        }
        if (state.redraw) {
            result = render(&client, &display, &state);
            state.redraw = 0U;
            if (result != 0) state.exit_requested = 1U;
        } else if (processed == 0U) {
            (void)x86os_sleep_ms(5U);
        }
    }

    (void)reist_gui_surface_client_destroy(&client);
    (void)x86os_ipc_release(endpoint);
    x86os_puts("GUIDEMO_EXIT_OK\n");
    return result == 0 ? 0 : 1;
}
