/**
 * @file userspace/gui/apps/notepad/main.c
 * @brief Bounded graphical REIST text editor reference application.
 *
 * This is a temporary full-screen GUI client until the versioned Surface IPC
 * is available. Document editing is delegated to the public renderer-neutral
 * text-editor controller; this file owns rendering, persistence, menus,
 * dialogs and the bounded event loop. It has no private compositor access.
 */
#include "x86os.h"
#include "reist/gui/dialog.h"
#include "reist/gui/menu.h"
#include "reist/gui/text_editor.h"

#define NOTEPAD_PATH_CAPACITY 256U
#define NOTEPAD_STATUS_CAPACITY 128U
#define NOTEPAD_TEXT_LIMIT 256U
#define NOTEPAD_MOUSE_BATCH_LIMIT 32U

enum {
    NOTEPAD_KEY_NONE = 0x100,
    NOTEPAD_KEY_ESCAPE,
    NOTEPAD_KEY_UP,
    NOTEPAD_KEY_DOWN,
    NOTEPAD_KEY_LEFT,
    NOTEPAD_KEY_RIGHT,
    NOTEPAD_KEY_HOME,
    NOTEPAD_KEY_END,
    NOTEPAD_KEY_DELETE,
    NOTEPAD_KEY_PAGE_UP,
    NOTEPAD_KEY_PAGE_DOWN
};

enum {
    NOTEPAD_ACTION_SAVE = 1U,
    NOTEPAD_ACTION_EXIT,
    NOTEPAD_ACTION_ABOUT
};

enum {
    NOTEPAD_DIALOG_NONE = 0U,
    NOTEPAD_DIALOG_CONFIRM_EXIT,
    NOTEPAD_DIALOG_ERROR,
    NOTEPAD_DIALOG_ABOUT
};

static const uint32_t color_desktop = 0x00006E8EU;
static const uint32_t color_face = 0x00C8C8C8U;
static const uint32_t color_light = 0x00FFFFFFU;
static const uint32_t color_shadow = 0x00606060U;
static const uint32_t color_dark = 0x00181818U;
static const uint32_t color_active = 0x00000088U;
static const uint32_t color_text = 0x00000000U;
static const uint32_t color_title_text = 0x00FFFFFFU;
static const uint32_t color_editor = 0x00FFFFFFU;

static const reist_gui_menu_item_t file_items[] = {
    {"Speichern", NOTEPAD_ACTION_SAVE, 0U, 0U, 0U},
    {"Beenden", NOTEPAD_ACTION_EXIT, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t help_items[] = {
    {"Ueber Editor", NOTEPAD_ACTION_ABOUT, 0U, 0U, 0U},
};

static const reist_gui_menu_t menus[] = {
    {"Datei", file_items, 2U, 0U, 0U},
    {"Hilfe", help_items, 1U, 0U, 0U},
};

static const reist_gui_menu_model_t menu_model = {
    REIST_GUI_MENU_API_VERSION, sizeof(reist_gui_menu_model_t),
    menus, 2U, {0U, 0U, 0U, 0U}
};

static const reist_gui_dialog_button_t confirm_buttons[] = {
    {"Speichern", REIST_GUI_DIALOG_RESPONSE_SAVE,
     REIST_GUI_DIALOG_ROLE_ACCEPT, 0U, 0U},
    {"Verwerfen", REIST_GUI_DIALOG_RESPONSE_DISCARD,
     REIST_GUI_DIALOG_ROLE_DESTRUCTIVE, 0U, 0U},
    {"Abbrechen", REIST_GUI_DIALOG_RESPONSE_CANCEL,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

static const reist_gui_dialog_button_t close_buttons[] = {
    {"Schliessen", REIST_GUI_DIALOG_RESPONSE_CLOSE,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

static const reist_gui_dialog_button_t ok_buttons[] = {
    {"OK", REIST_GUI_DIALOG_RESPONSE_OK,
     REIST_GUI_DIALOG_ROLE_ACCEPT, 0U, 0U},
};

static const reist_gui_dialog_model_t confirm_model = {
    REIST_GUI_DIALOG_API_VERSION, sizeof(reist_gui_dialog_model_t),
    "Ungespeicherte Aenderungen",
    "Die Datei wurde geaendert.",
    "Vor dem Schliessen speichern?",
    confirm_buttons, 3U, REIST_GUI_DIALOG_APPLICATION_MODAL,
    REIST_GUI_DIALOG_RESPONSE_SAVE, REIST_GUI_DIALOG_RESPONSE_CANCEL,
    REIST_GUI_DIALOG_NO_OWNER, 0U,
    REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
    {0U, 0U, 0U, 0U}
};

static const reist_gui_dialog_model_t about_model = {
    REIST_GUI_DIALOG_API_VERSION, sizeof(reist_gui_dialog_model_t),
    "Ueber REIST Editor",
    "Grafischer Texteditor auf der oeffentlichen REIST GUI-API",
    "Fester ASCII/LF-Puffer, atomarer FAT-Speicherpfad und modale Dialoge.",
    close_buttons, 1U, REIST_GUI_DIALOG_APPLICATION_MODAL,
    REIST_GUI_DIALOG_RESPONSE_CLOSE, REIST_GUI_DIALOG_RESPONSE_CLOSE,
    REIST_GUI_DIALOG_NO_OWNER, 0U,
    REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
    {0U, 0U, 0U, 0U}
};

typedef struct notepad_state {
    reist_gui_menu_state_t menu;
    reist_gui_dialog_state_t dialog;
    reist_gui_dialog_model_t error_model;
    reist_gui_text_editor_state_t editor;
    reist_gui_text_editor_model_t editor_model;
    char path[NOTEPAD_PATH_CAPACITY];
    char status[NOTEPAD_STATUS_CAPACITY];
    char error_detail[NOTEPAD_STATUS_CAPACITY];
    uint32_t dialog_kind;
    uint32_t exists;
    uint32_t io_blocked;
    uint32_t redraw;
    uint32_t exit_requested;
} notepad_state_t;

/* Large document storage remains static so the process stack stays bounded. */
static notepad_state_t application;
static char serialized[REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY + 1U];

static size_t bounded_length(const char *text_value, size_t capacity) {
    size_t length = 0U;
    if (text_value == 0) return 0U;
    while (length < capacity && text_value[length] != '\0') ++length;
    return length;
}

static uint32_t text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0U;
    while (index < NOTEPAD_TEXT_LIMIT && left[index] && right[index]) {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return index < NOTEPAD_TEXT_LIMIT && left[index] == right[index];
}

static uint32_t copy_text(char *destination, size_t capacity,
                          const char *source) {
    size_t length = bounded_length(source, capacity);
    if (destination == 0 || source == 0 || capacity == 0U ||
        length >= capacity) return 0U;
    for (size_t index = 0U; index <= length; ++index)
        destination[index] = source[index];
    return 1U;
}

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static uint32_t line_length(const char *line) {
    uint32_t length = 0U;
    while (length < REIST_GUI_TEXT_EDITOR_LINE_CAPACITY && line[length])
        ++length;
    return length;
}

static void append_text(char *output, size_t capacity, size_t *used,
                        const char *value) {
    if (output == 0 || used == 0 || value == 0) return;
    size_t index = 0U;
    while (*used + 1U < capacity && value[index] != '\0')
        output[(*used)++] = value[index++];
    output[*used] = '\0';
}

static void append_unsigned(char *output, size_t capacity, size_t *used,
                            uint32_t value) {
    char reverse[10];
    uint32_t count = 0U;
    do {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(reverse));
    while (count != 0U && *used + 1U < capacity)
        output[(*used)++] = reverse[--count];
    output[*used] = '\0';
}

static void fill(reist_gui_rect_t rect, uint32_t color) {
    if (rect.width && rect.height)
        (void)x86os_fill_rect(rect.x, rect.y, rect.width, rect.height, color);
}

static void text(const x86os_display_info_t *display,
                 int32_t x, int32_t y, const char *value,
                 uint32_t maximum_width, uint32_t foreground,
                 uint32_t background) {
    if (display == 0 || value == 0 || display->font_width == 0U) return;
    size_t length = bounded_length(value, NOTEPAD_TEXT_LIMIT);
    size_t capacity = maximum_width / display->font_width;
    if (length > capacity) length = capacity;
    if (length)
        (void)x86os_draw_text_pixels(
            x, y, value, length, foreground, background);
}

static void bevel(reist_gui_rect_t rect, uint32_t face, uint32_t raised) {
    if (!rect.width || !rect.height) return;
    fill(rect, face);
    if (rect.width < 2U || rect.height < 2U) return;
    uint32_t first = raised ? color_light : color_shadow;
    uint32_t second = raised ? color_shadow : color_light;
    fill((reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, first);
    fill((reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, first);
    fill((reist_gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                            rect.width, 1U}, second);
    fill((reist_gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y,
                            1U, rect.height}, second);
}

static uint32_t menu_height(const x86os_display_info_t *display) {
    return max_u32(display->font_height + 12U, 30U);
}

static reist_gui_menu_layout_t menu_layout(
    const x86os_display_info_t *display) {
    return (reist_gui_menu_layout_t){
        REIST_GUI_MENU_API_VERSION, sizeof(reist_gui_menu_layout_t),
        display->width, display->height,
        {0, 0, display->width, menu_height(display)},
        display->font_width, display->font_height,
        8U, 8U, 4U, 6U, {0U, 0U, 0U, 0U}};
}

static reist_gui_dialog_layout_t dialog_layout(
    const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 8U;
    uint32_t work_height = display->height > top + 8U
        ? display->height - top - 8U : 1U;
    uint32_t width = display->width > 48U ? display->width - 48U : 1U;
    if (width > 620U) width = 620U;
    uint32_t height = work_height > 230U ? 230U : work_height;
    return (reist_gui_dialog_layout_t){
        REIST_GUI_DIALOG_API_VERSION, sizeof(reist_gui_dialog_layout_t),
        display->width, display->height,
        {0, (int32_t)top, display->width, work_height},
        {(int32_t)((display->width - width) / 2U),
         (int32_t)(top + (work_height - height) / 2U), width, height},
        menu_height(display), 3U, display->font_width, display->font_height,
        88U, max_u32(display->font_height + 10U, 26U),
        8U, 8U, 12U, 6U, {0U, 0U, 0U, 0U}};
}

static reist_gui_rect_t editor_frame(const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 8U;
    return (reist_gui_rect_t){
        8, (int32_t)top,
        display->width > 16U ? display->width - 16U : 1U,
        display->height > top + 8U ? display->height - top - 8U : 1U};
}

static const reist_gui_dialog_model_t *dialog_model(
    const notepad_state_t *state) {
    if (state->dialog_kind == NOTEPAD_DIALOG_CONFIRM_EXIT)
        return &confirm_model;
    if (state->dialog_kind == NOTEPAD_DIALOG_ERROR)
        return &state->error_model;
    if (state->dialog_kind == NOTEPAD_DIALOG_ABOUT) return &about_model;
    return 0;
}

static void render_menu(const x86os_display_info_t *display,
                        const notepad_state_t *state) {
    reist_gui_menu_layout_t layout = menu_layout(display);
    bevel(layout.bar, color_face, 1U);
    for (uint32_t index = 0U; index < menu_model.menu_count; ++index) {
        reist_gui_rect_t title;
        if (reist_gui_menu_title_rect(
                &menu_model, &layout, index, &title) != 0) continue;
        uint32_t active = state->menu.open_menu == index;
        uint32_t background = active ? color_active : color_face;
        fill(title, background);
        uint32_t y = title.height > display->font_height
            ? (title.height - display->font_height) / 2U : 0U;
        text(display, title.x + (int32_t)layout.title_padding_x,
             title.y + (int32_t)y, menu_model.menus[index].label,
             title.width - layout.title_padding_x * 2U,
             active ? color_title_text : color_text, background);
    }
    if (state->menu.open_menu == REIST_GUI_MENU_NO_INDEX) return;
    uint32_t menu_index = state->menu.open_menu;
    reist_gui_rect_t popup;
    if (reist_gui_menu_popup_rect(
            &menu_model, &layout, menu_index, &popup) != 0) return;
    fill((reist_gui_rect_t){popup.x + 4, popup.y + 4,
                            popup.width, popup.height}, color_dark);
    bevel(popup, color_face, 1U);
    const reist_gui_menu_t *menu = &menu_model.menus[menu_index];
    for (uint32_t index = 0U; index < menu->item_count; ++index) {
        reist_gui_rect_t item;
        if (reist_gui_menu_item_rect(
                &menu_model, &layout, menu_index, index, &item) != 0)
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

static void render_editor(const x86os_display_info_t *display,
                          const notepad_state_t *state) {
    reist_gui_rect_t frame = editor_frame(display);
    fill((reist_gui_rect_t){frame.x + 4, frame.y + 4,
                            frame.width, frame.height}, color_dark);
    bevel(frame, color_face, 1U);
    uint32_t title_height = menu_height(display);
    reist_gui_rect_t title = {frame.x + 3, frame.y + 3,
        frame.width > 6U ? frame.width - 6U : 1U, title_height};
    fill(title, color_active);
    char title_text[NOTEPAD_STATUS_CAPACITY];
    size_t used = 0U;
    title_text[0] = '\0';
    append_text(title_text, sizeof(title_text), &used, "REIST Editor - ");
    append_text(title_text, sizeof(title_text), &used, state->path);
    if (state->editor.modified)
        append_text(title_text, sizeof(title_text), &used, " *");
    text(display, title.x + 10,
         title.y + (int32_t)((title.height - display->font_height) / 2U),
         title_text, title.width > 20U ? title.width - 20U : 1U,
         color_title_text, color_active);

    reist_gui_rect_t editor = state->editor_model.bounds;
    bevel((reist_gui_rect_t){editor.x - 2, editor.y - 2,
                             editor.width + 4U, editor.height + 4U},
          color_face, 0U);
    fill(editor, color_editor);
    uint32_t rows = editor.height / display->font_height;
    uint32_t columns = editor.width / display->font_width;
    for (uint32_t row = 0U; row < rows; ++row) {
        uint32_t line_index = state->editor.first_line + row;
        if (line_index >= state->editor.line_count) break;
        const char *line = state->editor.lines[line_index];
        uint32_t length = line_length(line);
        if (state->editor.first_column >= length) continue;
        uint32_t amount = length - state->editor.first_column;
        if (amount > columns) amount = columns;
        (void)x86os_draw_text_pixels(
            editor.x, editor.y + (int32_t)(row * display->font_height),
            line + state->editor.first_column, amount,
            color_text, color_editor);
    }
    if (state->editor.focused && !state->dialog.visible &&
        state->menu.open_menu == REIST_GUI_MENU_NO_INDEX &&
        state->editor.cursor_line >= state->editor.first_line &&
        state->editor.cursor_column >= state->editor.first_column) {
        uint32_t row = state->editor.cursor_line - state->editor.first_line;
        uint32_t column =
            state->editor.cursor_column - state->editor.first_column;
        if (row < rows && column < columns)
            fill((reist_gui_rect_t){
                editor.x + (int32_t)(column * display->font_width),
                editor.y + (int32_t)(row * display->font_height),
                2U, display->font_height}, color_dark);
    }

    reist_gui_rect_t status = {
        frame.x + 6,
        frame.y + (int32_t)frame.height - (int32_t)display->font_height - 8,
        frame.width > 12U ? frame.width - 12U : 1U,
        display->font_height + 4U};
    fill(status, color_face);
    char status_text[NOTEPAD_STATUS_CAPACITY];
    used = 0U;
    status_text[0] = '\0';
    append_text(status_text, sizeof(status_text), &used, state->status);
    append_text(status_text, sizeof(status_text), &used, "  Ln ");
    append_unsigned(status_text, sizeof(status_text), &used,
                    state->editor.cursor_line + 1U);
    append_text(status_text, sizeof(status_text), &used, ", Sp ");
    append_unsigned(status_text, sizeof(status_text), &used,
                    state->editor.cursor_column + 1U);
    append_text(status_text, sizeof(status_text), &used,
                "  Ctrl+S Speichern  Esc Beenden");
    text(display, status.x + 4, status.y + 2, status_text,
         status.width > 8U ? status.width - 8U : 1U,
         color_text, color_face);
}

static void render_dialog(const x86os_display_info_t *display,
                          const notepad_state_t *state) {
    if (!state->dialog.visible) return;
    const reist_gui_dialog_model_t *model = dialog_model(state);
    reist_gui_dialog_layout_t layout = dialog_layout(display);
    reist_gui_rect_t frame;
    reist_gui_rect_t title;
    reist_gui_rect_t close;
    if (model == 0 || reist_gui_dialog_frame_rect(
            model, &layout, &state->dialog, &frame) != 0 ||
        reist_gui_dialog_title_rect(
            model, &layout, &state->dialog, &title) != 0 ||
        reist_gui_dialog_close_rect(
            model, &layout, &state->dialog, &close) != 0) return;
    fill((reist_gui_rect_t){frame.x + 5, frame.y + 5,
                            frame.width, frame.height}, color_dark);
    bevel(frame, color_face, 1U);
    fill(title, color_active);
    bevel(close, color_face,
          state->dialog.capture_kind != REIST_GUI_DIALOG_CAPTURE_CLOSE);
    if (close.width > 8U && close.height > 8U)
        fill((reist_gui_rect_t){close.x + 4, close.y + 4,
             close.width - 8U, close.height - 8U}, color_dark);
    uint32_t title_y = title.height > display->font_height
        ? (title.height - display->font_height) / 2U : 0U;
    text(display, title.x + (int32_t)close.width + 8,
         title.y + (int32_t)title_y, model->title,
         title.width > close.width + 16U
            ? title.width - close.width - 16U : 1U,
         color_title_text, color_active);
    int32_t content_x = frame.x + 14;
    int32_t content_y = title.y + (int32_t)title.height + 14;
    uint32_t content_width = frame.width > 28U ? frame.width - 28U : 1U;
    text(display, content_x, content_y, model->message,
         content_width, color_text, color_face);
    if (model->detail)
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
            bevel((reist_gui_rect_t){button.x - 2, button.y - 2,
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
                         const notepad_state_t *state) {
    fill((reist_gui_rect_t){0, 0, display->width, display->height},
         color_desktop);
    render_editor(display, state);
    render_menu(display, state);
    render_dialog(display, state);
}

static void render(const x86os_display_info_t *display,
                   const notepad_state_t *state) {
    uint32_t serial = 0U;
    uint32_t transaction = x86os_display_frame_begin(&serial) == 0;
    render_scene(display, state);
    if (transaction && x86os_display_frame_commit(serial) != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_scene(display, state);
    }
}

static int write_all(int descriptor, const char *data, size_t size) {
    size_t offset = 0U;
    while (offset < size) {
        int amount = x86os_write(descriptor, data + offset, size - offset);
        if (amount <= 0) return -1;
        offset += (size_t)amount;
    }
    return 0;
}

static int make_temp_path(const char *path, char temp[NOTEPAD_PATH_CAPACITY]) {
    size_t length = bounded_length(path, NOTEPAD_PATH_CAPACITY);
    if (length == 0U || length >= NOTEPAD_PATH_CAPACITY) return -1;
    size_t prefix = 0U;
    for (size_t index = 0U; index < length; ++index)
        if (path[index] == '/') prefix = index + 1U;
    static const char leaf[] = "RNP00000.TMP";
    if (prefix + sizeof(leaf) > NOTEPAD_PATH_CAPACITY) return -1;
    for (size_t index = 0U; index < prefix; ++index) temp[index] = path[index];
    for (size_t index = 0U; index < sizeof(leaf); ++index)
        temp[prefix + index] = leaf[index];
    uint32_t pid = (uint32_t)x86os_getpid();
    for (uint32_t digit = 0U; digit < 5U; ++digit) {
        temp[prefix + 7U - digit] = (char)('0' + pid % 10U);
        pid /= 10U;
    }
    return text_equal(path, temp) ? -1 : 0;
}

static int save_document(notepad_state_t *state) {
    size_t length = 0U;
    if (state->io_blocked) return -1;
    if (reist_gui_text_editor_get_text(
            &state->editor_model, &state->editor,
            serialized, sizeof(serialized), &length) != 0) return -1;
    char temp[NOTEPAD_PATH_CAPACITY];
    if (make_temp_path(state->path, temp) != 0) return -1;
    (void)x86os_unlink(temp);
    int descriptor = x86os_create(temp);
    if (descriptor < 0) return -1;
    int write_status = write_all(descriptor, serialized, length);
    int sync_status = write_status == 0 ? x86os_fsync(descriptor) : -1;
    int close_status = x86os_close(descriptor);
    if (write_status != 0 || sync_status < 0 || close_status < 0 ||
        x86os_rename(temp, state->path) != 0) {
        (void)x86os_unlink(temp);
        return -1;
    }
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    if (reist_gui_text_editor_mark_saved(
            &state->editor_model, &state->editor, &result) != 0) return -1;
    state->exists = 1U;
    (void)copy_text(state->status, sizeof(state->status), "Gespeichert");
    state->redraw = 1U;
    return 0;
}

static int load_document(notepad_state_t *state) {
    x86os_file_info_t info;
    if (x86os_stat(state->path, &info) != 0) {
        state->exists = 0U;
        (void)copy_text(state->status, sizeof(state->status), "Neue Datei");
        return 0;
    }
    if (info.type != X86OS_FILE ||
        info.size > REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY) return -2;
    int descriptor = x86os_open(state->path);
    if (descriptor < 0) return -1;
    size_t offset = 0U;
    while (offset < info.size) {
        int amount = x86os_read(
            descriptor, serialized + offset, info.size - offset);
        if (amount <= 0) {
            (void)x86os_close(descriptor);
            return -1;
        }
        offset += (size_t)amount;
    }
    char extra;
    int extra_read = x86os_read(descriptor, &extra, 1U);
    int close_status = x86os_close(descriptor);
    if (extra_read != 0 || close_status < 0) return -1;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    int status = reist_gui_text_editor_set_text(
        &state->editor_model, &state->editor,
        serialized, offset, &result);
    if (status == REIST_GUI_TEXT_EDITOR_ECAPACITY) return -2;
    if (status != 0) return -3;
    state->exists = 1U;
    (void)copy_text(state->status, sizeof(state->status), "Geladen");
    return 0;
}

static void initialize_error_model(notepad_state_t *state,
                                   const char *message,
                                   const char *detail) {
    (void)copy_text(state->error_detail, sizeof(state->error_detail), detail);
    state->error_model = (reist_gui_dialog_model_t){
        REIST_GUI_DIALOG_API_VERSION, sizeof(reist_gui_dialog_model_t),
        "Editorfehler", message, state->error_detail,
        ok_buttons, 1U, REIST_GUI_DIALOG_APPLICATION_MODAL,
        REIST_GUI_DIALOG_RESPONSE_OK, REIST_GUI_DIALOG_RESPONSE_OK,
        REIST_GUI_DIALOG_NO_OWNER, 0U,
        REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
        {0U, 0U, 0U, 0U}};
}

static void open_dialog(notepad_state_t *state,
                        const x86os_display_info_t *display,
                        uint32_t kind) {
    const reist_gui_dialog_model_t *model;
    state->dialog_kind = kind;
    model = dialog_model(state);
    reist_gui_dialog_layout_t layout = dialog_layout(display);
    reist_gui_dialog_result_t result;
    reist_gui_dialog_result_initialize(&result);
    if (model == 0 || reist_gui_dialog_open(
            model, &layout, &state->dialog, &result) != 0) {
        reist_gui_dialog_state_initialize(&state->dialog);
        state->dialog_kind = NOTEPAD_DIALOG_NONE;
    }
    state->redraw = 1U;
}

static void open_error(notepad_state_t *state,
                       const x86os_display_info_t *display,
                       const char *message, const char *detail) {
    initialize_error_model(state, message, detail);
    open_dialog(state, display, NOTEPAD_DIALOG_ERROR);
}

static void request_exit(notepad_state_t *state,
                         const x86os_display_info_t *display) {
    if (state->editor.modified)
        open_dialog(state, display, NOTEPAD_DIALOG_CONFIRM_EXIT);
    else state->exit_requested = 1U;
}

static void complete_dialog(notepad_state_t *state,
                            const x86os_display_info_t *display,
                            uint32_t kind, uint32_t response) {
    state->dialog_kind = NOTEPAD_DIALOG_NONE;
    state->redraw = 1U;
    if (kind != NOTEPAD_DIALOG_CONFIRM_EXIT) return;
    if (response == REIST_GUI_DIALOG_RESPONSE_DISCARD)
        state->exit_requested = 1U;
    else if (response == REIST_GUI_DIALOG_RESPONSE_SAVE) {
        if (save_document(state) == 0) state->exit_requested = 1U;
        else open_error(state, display, "Datei konnte nicht gespeichert werden.",
                        state->path);
    }
}

static void apply_menu_result(notepad_state_t *state,
                              const x86os_display_info_t *display,
                              const reist_gui_menu_result_t *result) {
    if (result->damage_count || result->full_redraw) state->redraw = 1U;
    if (!result->activated) return;
    if (result->action == NOTEPAD_ACTION_SAVE) {
        if (save_document(state) != 0)
            open_error(state, display, "Datei konnte nicht gespeichert werden.",
                       state->path);
    } else if (result->action == NOTEPAD_ACTION_EXIT)
        request_exit(state, display);
    else if (result->action == NOTEPAD_ACTION_ABOUT)
        open_dialog(state, display, NOTEPAD_DIALOG_ABOUT);
}

static uint32_t dispatch_pointer(notepad_state_t *state,
                                 const x86os_display_info_t *display,
                                 int32_t x, int32_t y,
                                 uint32_t button_event, uint32_t pressed) {
    if (state->dialog.visible) {
        uint32_t kind = state->dialog_kind;
        const reist_gui_dialog_model_t *model = dialog_model(state);
        reist_gui_dialog_layout_t layout = dialog_layout(display);
        reist_gui_dialog_event_t event;
        reist_gui_dialog_event_initialize(&event);
        event.type = button_event ? REIST_GUI_DIALOG_EVENT_POINTER_BUTTON
                                  : REIST_GUI_DIALOG_EVENT_POINTER_MOTION;
        event.x = x;
        event.y = y;
        event.button = button_event ? REIST_GUI_DIALOG_BUTTON_LEFT : 0U;
        event.pressed = pressed;
        reist_gui_dialog_result_t result;
        reist_gui_dialog_result_initialize(&result);
        if (model == 0 || reist_gui_dialog_dispatch(
                model, &layout, &state->dialog, &event, &result) != 0)
            return 1U;
        if (result.damage_count || result.full_redraw) state->redraw = 1U;
        if (result.completed)
            complete_dialog(state, display, kind, result.response);
        return result.consumed || state->dialog.visible;
    }

    reist_gui_menu_layout_t layout = menu_layout(display);
    reist_gui_menu_event_t menu_event;
    reist_gui_menu_event_initialize(&menu_event);
    menu_event.type = button_event ? REIST_GUI_MENU_EVENT_POINTER_BUTTON
                                   : REIST_GUI_MENU_EVENT_POINTER_MOTION;
    menu_event.x = x;
    menu_event.y = y;
    menu_event.button = button_event ? REIST_GUI_MENU_BUTTON_LEFT : 0U;
    menu_event.pressed = pressed;
    reist_gui_menu_result_t menu_result;
    reist_gui_menu_result_initialize(&menu_result);
    if (reist_gui_menu_dispatch(
            &menu_model, &layout, &state->menu,
            &menu_event, &menu_result) != 0) return 1U;
    apply_menu_result(state, display, &menu_result);
    if (menu_result.consumed) return 1U;

    reist_gui_text_editor_event_t event;
    reist_gui_text_editor_event_initialize(&event);
    event.type = button_event ? REIST_GUI_TEXT_EDITOR_EVENT_POINTER_BUTTON
                              : REIST_GUI_TEXT_EDITOR_EVENT_POINTER_MOTION;
    event.x = x;
    event.y = y;
    event.button = button_event ? REIST_GUI_TEXT_EDITOR_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    if (reist_gui_text_editor_dispatch(
            &state->editor_model, &state->editor,
            &event, &result) != 0) return 1U;
    if (result.damage_count || result.full_redraw) state->redraw = 1U;
    return result.consumed;
}

static uint32_t dialog_key(int key) {
    if (key == NOTEPAD_KEY_LEFT || key == NOTEPAD_KEY_UP)
        return REIST_GUI_DIALOG_KEY_PREVIOUS;
    if (key == NOTEPAD_KEY_RIGHT || key == NOTEPAD_KEY_DOWN || key == '\t')
        return REIST_GUI_DIALOG_KEY_NEXT;
    if (key == '\r' || key == '\n') return REIST_GUI_DIALOG_KEY_ENTER;
    if (key == NOTEPAD_KEY_ESCAPE) return REIST_GUI_DIALOG_KEY_ESCAPE;
    return 0U;
}

static uint32_t menu_key(int key) {
    if (key == NOTEPAD_KEY_LEFT) return REIST_GUI_MENU_KEY_LEFT;
    if (key == NOTEPAD_KEY_RIGHT || key == '\t')
        return REIST_GUI_MENU_KEY_RIGHT;
    if (key == NOTEPAD_KEY_UP) return REIST_GUI_MENU_KEY_UP;
    if (key == NOTEPAD_KEY_DOWN) return REIST_GUI_MENU_KEY_DOWN;
    if (key == '\r' || key == '\n') return REIST_GUI_MENU_KEY_ENTER;
    if (key == NOTEPAD_KEY_ESCAPE) return REIST_GUI_MENU_KEY_ESCAPE;
    return 0U;
}

static uint32_t editor_key(int key) {
    if (key == NOTEPAD_KEY_LEFT) return REIST_GUI_TEXT_EDITOR_KEY_LEFT;
    if (key == NOTEPAD_KEY_RIGHT) return REIST_GUI_TEXT_EDITOR_KEY_RIGHT;
    if (key == NOTEPAD_KEY_UP) return REIST_GUI_TEXT_EDITOR_KEY_UP;
    if (key == NOTEPAD_KEY_DOWN) return REIST_GUI_TEXT_EDITOR_KEY_DOWN;
    if (key == NOTEPAD_KEY_HOME) return REIST_GUI_TEXT_EDITOR_KEY_HOME;
    if (key == NOTEPAD_KEY_END) return REIST_GUI_TEXT_EDITOR_KEY_END;
    if (key == NOTEPAD_KEY_PAGE_UP)
        return REIST_GUI_TEXT_EDITOR_KEY_PAGE_UP;
    if (key == NOTEPAD_KEY_PAGE_DOWN)
        return REIST_GUI_TEXT_EDITOR_KEY_PAGE_DOWN;
    if (key == NOTEPAD_KEY_DELETE) return REIST_GUI_TEXT_EDITOR_KEY_DELETE;
    if (key == 8 || key == 127) return REIST_GUI_TEXT_EDITOR_KEY_BACKSPACE;
    if (key == '\r' || key == '\n') return REIST_GUI_TEXT_EDITOR_KEY_ENTER;
    return 0U;
}

static uint32_t dispatch_keyboard(notepad_state_t *state,
                                  const x86os_display_info_t *display,
                                  int key) {
    if (state->dialog.visible) {
        uint32_t mapped = dialog_key(key);
        if (!mapped) return 1U;
        uint32_t kind = state->dialog_kind;
        const reist_gui_dialog_model_t *model = dialog_model(state);
        reist_gui_dialog_layout_t layout = dialog_layout(display);
        reist_gui_dialog_event_t event;
        reist_gui_dialog_event_initialize(&event);
        event.type = REIST_GUI_DIALOG_EVENT_KEYBOARD;
        event.key = mapped;
        reist_gui_dialog_result_t result;
        reist_gui_dialog_result_initialize(&result);
        if (model == 0 || reist_gui_dialog_dispatch(
                model, &layout, &state->dialog, &event, &result) != 0)
            return 1U;
        if (result.damage_count || result.full_redraw) state->redraw = 1U;
        if (result.completed)
            complete_dialog(state, display, kind, result.response);
        return 1U;
    }
    if (state->menu.open_menu != REIST_GUI_MENU_NO_INDEX) {
        uint32_t mapped = menu_key(key);
        if (!mapped) return 1U;
        reist_gui_menu_layout_t layout = menu_layout(display);
        reist_gui_menu_event_t event;
        reist_gui_menu_event_initialize(&event);
        event.type = REIST_GUI_MENU_EVENT_KEYBOARD;
        event.key = mapped;
        reist_gui_menu_result_t result;
        reist_gui_menu_result_initialize(&result);
        if (reist_gui_menu_dispatch(
                &menu_model, &layout, &state->menu,
                &event, &result) != 0) return 1U;
        apply_menu_result(state, display, &result);
        return 1U;
    }
    if (key == 19) {
        if (save_document(state) != 0)
            open_error(state, display, "Datei konnte nicht gespeichert werden.",
                       state->path);
        return 1U;
    }
    if (key == 24 || key == NOTEPAD_KEY_ESCAPE) {
        request_exit(state, display);
        return 1U;
    }
    reist_gui_text_editor_event_t event;
    reist_gui_text_editor_event_initialize(&event);
    uint32_t mapped = editor_key(key);
    if (mapped) {
        event.type = REIST_GUI_TEXT_EDITOR_EVENT_KEYBOARD;
        event.key = mapped;
    } else if (key >= 0x20 && key <= 0x7E) {
        event.type = REIST_GUI_TEXT_EDITOR_EVENT_TEXT;
        event.codepoint = (uint32_t)key;
    } else return 0U;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    if (reist_gui_text_editor_dispatch(
            &state->editor_model, &state->editor,
            &event, &result) != 0) return 1U;
    if (result.damage_count || result.full_redraw) state->redraw = 1U;
    if (result.changed)
        (void)copy_text(state->status, sizeof(state->status), "Geaendert");
    return result.consumed;
}

static int read_escape_byte(void) {
    for (uint32_t attempt = 0U; attempt < 20U; ++attempt) {
        int value = x86os_getchar_nonblocking();
        if (value != 0) return value;
        (void)x86os_sleep_ms(1U);
    }
    return 0;
}

static int read_key(void) {
    int key = x86os_getchar_nonblocking();
    if (key != 0x1B) return key;
    int prefix = read_escape_byte();
    if (prefix == 0) return NOTEPAD_KEY_ESCAPE;
    if (prefix != '[') return NOTEPAD_KEY_NONE;
    int value = read_escape_byte();
    if (value == 'A') return NOTEPAD_KEY_UP;
    if (value == 'B') return NOTEPAD_KEY_DOWN;
    if (value == 'C') return NOTEPAD_KEY_RIGHT;
    if (value == 'D') return NOTEPAD_KEY_LEFT;
    if (value == 'H') return NOTEPAD_KEY_HOME;
    if (value == 'F') return NOTEPAD_KEY_END;
    if (value == '3' && read_escape_byte() == '~') return NOTEPAD_KEY_DELETE;
    if (value == '5' && read_escape_byte() == '~') return NOTEPAD_KEY_PAGE_UP;
    if (value == '6' && read_escape_byte() == '~') return NOTEPAD_KEY_PAGE_DOWN;
    for (uint32_t consumed = 0U; consumed < 4U &&
         value >= 0x30 && value <= 0x3F; ++consumed)
        value = read_escape_byte();
    return NOTEPAD_KEY_NONE;
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

static int initialize(notepad_state_t *state,
                      const x86os_display_info_t *display,
                      const char *path) {
    reist_gui_menu_state_initialize(&state->menu);
    reist_gui_dialog_state_initialize(&state->dialog);
    reist_gui_text_editor_state_initialize(&state->editor);
    if (!copy_text(state->path, sizeof(state->path), path)) return -4;
    reist_gui_rect_t frame = editor_frame(display);
    uint32_t title = menu_height(display);
    uint32_t status = display->font_height + 12U;
    int32_t top = frame.y + 3 + (int32_t)title + 8;
    int32_t bottom = frame.y + (int32_t)frame.height - (int32_t)status - 6;
    state->editor_model = (reist_gui_text_editor_model_t){
        REIST_GUI_TEXT_EDITOR_API_VERSION,
        sizeof(reist_gui_text_editor_model_t), 1U, "Dokumenttext",
        {frame.x + 8, top,
         frame.width > 16U ? frame.width - 16U : 1U,
         bottom > top ? (uint32_t)(bottom - top) : 1U},
        display->font_width, display->font_height,
        REIST_GUI_TEXT_EDITOR_VISIBLE | REIST_GUI_TEXT_EDITOR_ENABLED,
        {0U, 0U, 0U, 0U}};
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    if (reist_gui_text_editor_configure(
            &state->editor_model, &state->editor, &result) != 0) return -4;
    reist_gui_text_editor_event_t focus;
    reist_gui_text_editor_event_initialize(&focus);
    focus.type = REIST_GUI_TEXT_EDITOR_EVENT_FOCUS;
    focus.focused = 1U;
    reist_gui_text_editor_result_initialize(&result);
    if (reist_gui_text_editor_dispatch(
            &state->editor_model, &state->editor, &focus, &result) != 0)
        return -4;
    state->dialog_kind = NOTEPAD_DIALOG_NONE;
    state->redraw = 1U;
    return load_document(state);
}

int main(int argc, char **argv) {
    if (argc == 2 && argv && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: notepad [file]\n");
        return 0;
    }
    if (argc > 2 || argv == 0) {
        x86os_puts("Usage: notepad [file]\n");
        return 2;
    }
    const char *path = argc == 2 ? argv[1] : "/untitled.txt";
    x86os_display_info_t display;
    uint32_t runtime_activated = 0U;
    if (x86os_display_info(&display) != 0) {
        if (x86os_display_activate() == 0) runtime_activated = 1U;
        if (x86os_display_info(&display) != 0) {
            x86os_puts("notepad: Grafikmodus nicht verfuegbar\n");
            return 1;
        }
    }
    if (display.version != X86OS_DISPLAY_ABI_VERSION ||
        display.struct_size < sizeof(display) ||
        display.width < 320U || display.height < 240U ||
        display.font_width == 0U || display.font_height == 0U) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("notepad: ungueltige Display-ABI\n");
        return 1;
    }

    int load_status = initialize(&application, &display, path);
    if (load_status == -4) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("notepad: Initialisierung fehlgeschlagen\n");
        return 1;
    }
    if (load_status < 0 && load_status != -4) {
        application.io_blocked = 1U;
        application.editor_model.flags |= REIST_GUI_TEXT_EDITOR_READ_ONLY;
    }
    if (load_status == -1)
        open_error(&application, &display,
                   "Datei konnte nicht gelesen werden.", path);
    else if (load_status == -2)
        open_error(&application, &display,
                   "Datei ueberschreitet die Editorkapazitaet.", path);
    else if (load_status == -3)
        open_error(&application, &display,
                   "Datei enthaelt nicht unterstuetzte Zeichen.", path);

    reist_gui_menu_layout_t menu_metrics = menu_layout(&display);
    if (reist_gui_menu_validate(
            &menu_model, &menu_metrics, &application.menu) != 0 ||
        reist_gui_text_editor_validate(
            &application.editor_model, &application.editor) != 0) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("notepad: GUI-API/Layout nicht kompatibel\n");
        return 1;
    }

    int32_t pointer_x = (int32_t)(display.width / 2U);
    int32_t pointer_y = (int32_t)(display.height / 2U);
    uint32_t previous_buttons = 0U;
    render(&display, &application);
    application.redraw = 0U;
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);

    while (!application.exit_requested) {
        int key = read_key();
        uint32_t mouse_count = 0U;
        for (; mouse_count < NOTEPAD_MOUSE_BATCH_LIMIT; ++mouse_count) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            move_pointer(&display, &pointer_x, &pointer_y,
                         mouse.delta_x, mouse.delta_y);
            (void)dispatch_pointer(
                &application, &display, pointer_x, pointer_y, 0U, 0U);
            uint32_t left =
                (mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t previous =
                (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            if (left && !previous)
                (void)dispatch_pointer(
                    &application, &display, pointer_x, pointer_y, 1U, 1U);
            else if (!left && previous)
                (void)dispatch_pointer(
                    &application, &display, pointer_x, pointer_y, 1U, 0U);
            previous_buttons = mouse.buttons;
        }
        if (key && key != NOTEPAD_KEY_NONE)
            (void)dispatch_keyboard(&application, &display, key);
        if (application.redraw) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            render(&display, &application);
            application.redraw = 0U;
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if (mouse_count) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else {
            (void)x86os_sleep_ms(5U);
        }
    }

    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    if (runtime_activated) {
        if (x86os_display_deactivate() != 0) {
            x86os_puts("notepad: VGA-Rueckkehr fehlgeschlagen\n");
            return 1;
        }
    } else x86os_clear();
    return 0;
}
