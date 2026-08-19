/**
 * @file userspace/gui/apps/control_gallery/main.c
 * @brief Interactive gallery for the currently implemented REIST GUI API.
 *
 * This program is a temporary full-screen graphical client while the Surface
 * IPC protocol is still pending. It consumes only public x86os and
 * libreistgui headers; it does not include compositor-private state.
 */
#include "x86os.h"
#include "reist/gui/dialog.h"
#include "reist/gui/menu.h"

#define GALLERY_MENU_COUNT 3U
#define GALLERY_TEXT_LIMIT 128U

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
    uint32_t dialog_kind;
    uint32_t exit_requested;
    uint32_t redraw;
} gallery_state_t;

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

static void fill(reist_gui_rect_t rect, uint32_t color) {
    if (rect.width != 0U && rect.height != 0U)
        (void)x86os_fill_rect(
            rect.x, rect.y, rect.width, rect.height, color);
}

static void text(const x86os_display_info_t *display,
                 int32_t x, int32_t y, const char *value,
                 uint32_t maximum_width, uint32_t foreground,
                 uint32_t background) {
    if (display == 0 || value == 0 || display->font_width == 0U) return;
    size_t length = bounded_length(value, GALLERY_TEXT_LIMIT);
    size_t capacity = maximum_width / display->font_width;
    if (length > capacity) length = capacity;
    if (length != 0U)
        (void)x86os_draw_text_pixels(
            x, y, value, length, foreground, background);
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

static void render_gallery(const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 18U;
    uint32_t bottom = display->height > 20U ? display->height - 20U : top;
    reist_gui_rect_t frame = {
        24, (int32_t)top,
        display->width > 48U ? display->width - 48U : 1U,
        bottom > top ? bottom - top : 1U,
    };
    fill((reist_gui_rect_t){
        frame.x + 5, frame.y + 5, frame.width, frame.height}, color_dark);
    bevel(frame, color_face, 1U);
    reist_gui_rect_t title = {
        frame.x + 3, frame.y + 3,
        frame.width > 6U ? frame.width - 6U : 1U,
        menu_height(display),
    };
    fill(title, color_active);
    text(display, title.x + 10,
         title.y + (int32_t)((title.height - display->font_height) / 2U),
         "REIST GUI Control Gallery", title.width > 20U ? title.width - 20U : 1U,
         color_title_text, color_active);

    int32_t x = frame.x + 16;
    int32_t y = title.y + (int32_t)title.height + 14;
    uint32_t line = max_u32(display->font_height + 5U, 20U);
    uint32_t content_width = frame.width > 32U ? frame.width - 32U : 1U;
    text(display, x, y, "Implementiert und interaktiv", content_width,
         color_text, color_face);
    y += (int32_t)line;
    text(display, x, y, "[x] Menueleiste und Popup-Menue", content_width,
         color_text, color_face);
    y += (int32_t)line;
    text(display, x, y, "[x] Modal/modeless Dialog + Response", content_width,
         color_text, color_face);
    y += (int32_t)line;
    text(display, x, y, "[x] Dialogbuttons, Fokus, Enter/Escape", content_width,
         color_text, color_face);
    y += (int32_t)line;
    text(display, x, y, "[x] Titelleisten-Drag mit Pointer-Capture", content_width,
         color_text, color_face);

    y += (int32_t)(line + 8U);
    text(display, x, y, "Geplant - bewusst deaktiviert", content_width,
         color_shadow, color_face);
    static const char *const planned[] = {
        "[ ] Label/Button als allgemeines Control",
        "[ ] Checkbox, Radio, Textfeld, TextArea",
        "[ ] Liste, Baum, Scrollbar, Slider",
        "[ ] Tabs, ComboBox, Toolbar, Tooltip",
        "[ ] Surface-IPC und Accessibility-Baum",
    };
    for (uint32_t index = 0U;
         index < sizeof(planned) / sizeof(planned[0]); ++index) {
        y += (int32_t)line;
        if ((int64_t)y + display->font_height >=
            frame.y + (int64_t)frame.height - 30)
            break;
        text(display, x, y, planned[index], content_width,
             color_shadow, color_face);
    }
    text(display, frame.x + 12,
         frame.y + (int32_t)frame.height -
             (int32_t)display->font_height - 8,
         "Dialoge-Menue: Demo starten   ESC: zur Shell",
         frame.width > 24U ? frame.width - 24U : 1U,
         color_text, color_face);
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
    render_gallery(display);
    render_menu(display, state);
    render_dialog(display, state);
}

static void render(const x86os_display_info_t *display,
                   const gallery_state_t *state) {
    uint32_t serial = 0U;
    uint32_t transaction = x86os_display_frame_begin(&serial) == 0;
    render_scene(display, state);
    if (transaction && x86os_display_frame_commit(serial) != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_scene(display, state);
    }
}

static void initialize(gallery_state_t *state) {
    reist_gui_menu_state_initialize(&state->menu);
    reist_gui_dialog_state_initialize(&state->dialog);
    state->dialog_kind = GALLERY_DIALOG_NONE;
    state->exit_requested = 0U;
    state->redraw = 1U;
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
    return result.consumed;
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
    if (state->menu.open_menu == REIST_GUI_MENU_NO_INDEX) return 0U;
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

static int read_key(void) {
    int key = x86os_getchar_nonblocking();
    if (key != 0x1B) return key;
    int prefix = x86os_getchar_nonblocking();
    if (prefix == 0) return GALLERY_KEY_ESCAPE;
    if (prefix != '[') return GALLERY_KEY_NONE;
    int value = x86os_getchar_nonblocking();
    if (value == 0) return GALLERY_KEY_NONE;
    if (value == 'A') return GALLERY_KEY_UP;
    if (value == 'B') return GALLERY_KEY_DOWN;
    if (value == 'C') return GALLERY_KEY_RIGHT;
    if (value == 'D') return GALLERY_KEY_LEFT;
    for (uint32_t consumed = 0U;
         consumed < 4U && value >= 0x30 && value <= 0x3F; ++consumed) {
        value = x86os_getchar_nonblocking();
        if (value == 0) return GALLERY_KEY_NONE;
    }
    return GALLERY_KEY_NONE;
}

static void move_pointer(const x86os_display_info_t *display,
                         int32_t *x, int32_t *y,
                         int32_t delta_x, int32_t delta_y) {
    int64_t next_x = (int64_t)*x + delta_x;
    int64_t next_y = (int64_t)*y + delta_y;
    if (next_x < 0) next_x = 0;
    if (next_y < 0) next_y = 0;
    if (next_x >= display->width) next_x = display->width - 1U;
    if (next_y >= display->height) next_y = display->height - 1U;
    *x = (int32_t)next_x;
    *y = (int32_t)next_y;
}

int main(int argc, char **argv) {
    x86os_display_info_t display;
    uint32_t runtime_activated = 0U;
    if (argc == 2 && argv != 0 && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: guidemo\n");
        return 0;
    }
    if (argc != 1) {
        x86os_puts("Usage: guidemo [--help]\n");
        return 2;
    }
    if (x86os_display_info(&display) != 0) {
        if (x86os_display_activate() == 0) runtime_activated = 1U;
        if (x86os_display_info(&display) != 0) {
            x86os_puts("guidemo: Grafikmodus nicht verfuegbar\n");
            return 1;
        }
    }
    if (display.version != X86OS_DISPLAY_ABI_VERSION ||
        display.struct_size < sizeof(display) ||
        display.width < 320U || display.height < 240U ||
        display.font_width == 0U || display.font_height == 0U) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("guidemo: ungueltige Display-ABI\n");
        return 1;
    }

    gallery_state_t state;
    initialize(&state);
    reist_gui_menu_layout_t menu_metrics = menu_layout(&display);
    reist_gui_dialog_layout_t dialog_metrics = dialog_layout(&display);
    if (reist_gui_menu_validate(
            &gallery_menu_model, &menu_metrics, &state.menu) != 0 ||
        reist_gui_dialog_validate(
            &modeless_dialog_model, &dialog_metrics, &state.dialog) != 0 ||
        reist_gui_dialog_validate(
            &modal_dialog_model, &dialog_metrics, &state.dialog) != 0 ||
        reist_gui_dialog_validate(
            &about_dialog_model, &dialog_metrics, &state.dialog) != 0) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("guidemo: GUI-API/Layout nicht kompatibel\n");
        return 1;
    }

    int32_t pointer_x = (int32_t)(display.width / 2U);
    int32_t pointer_y = (int32_t)(display.height / 2U);
    uint32_t previous_buttons = 0U;
    x86os_puts("GUIDEMO_OK\n");
    render(&display, &state);
    state.redraw = 0U;
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);

    while (!state.exit_requested) {
        int key = read_key();
        uint32_t mouse_count = 0U;
        for (; mouse_count < 32U; ++mouse_count) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            move_pointer(&display, &pointer_x, &pointer_y,
                         mouse.delta_x, mouse.delta_y);
            (void)dispatch_pointer(
                &state, &display, pointer_x, pointer_y, 0U, 0U);
            uint32_t left =
                (mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t previous =
                (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            if (left && !previous)
                (void)dispatch_pointer(
                    &state, &display, pointer_x, pointer_y, 1U, 1U);
            else if (!left && previous)
                (void)dispatch_pointer(
                    &state, &display, pointer_x, pointer_y, 1U, 0U);
            previous_buttons = mouse.buttons;
        }

        if (key != 0 && key != GALLERY_KEY_NONE) {
            uint32_t consumed = dispatch_keyboard(&state, &display, key);
            if (!consumed && key == GALLERY_KEY_ESCAPE)
                state.exit_requested = 1U;
        }
        if (state.redraw) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            render(&display, &state);
            state.redraw = 0U;
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if (mouse_count != 0U) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else {
            (void)x86os_sleep_ms(5U);
        }
    }

    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    if (runtime_activated) {
        if (x86os_display_deactivate() != 0) {
            x86os_puts("guidemo: VGA-Rueckkehr fehlgeschlagen\n");
            return 1;
        }
    } else {
        x86os_clear();
    }
    x86os_puts("GUIDEMO_EXIT_OK\n");
    return 0;
}
