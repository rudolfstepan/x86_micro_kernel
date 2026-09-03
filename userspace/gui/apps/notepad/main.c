/**
 * @file userspace/gui/apps/notepad/main.c
 * @brief Bounded graphical REIST text editor reference application.
 *
 * Under the desktop this process owns a compositor Surface and receives only
 * client-local input. A compatible full-screen path remains available when
 * invoked directly from the shell. Document editing is delegated to the
 * public renderer-neutral text-editor controller; this file owns rendering,
 * persistence, menus, dialogs and the bounded event loop.
 */
#include "x86os.h"
#include "reist/gui/dialog.h"
#include "reist/gui/file_dialog.h"
#include "reist/gui/menu.h"
#include "reist/gui/piece_document.h"
#include "reist/gui/surface_client.h"
#include "reist/gui/text_editor.h"
#include "reist/gui/value_controls.h"
#include "reist/vfs_file_client.h"

#include "../../../../include/reist/utf.h"

#define NOTEPAD_PATH_CAPACITY 256U
#define NOTEPAD_STATUS_CAPACITY 128U
#define NOTEPAD_TEXT_LIMIT 256U
#define NOTEPAD_MOUSE_BATCH_LIMIT 32U
#define NOTEPAD_PAINT_RETRY_LIMIT 3U
#define NOTEPAD_SCROLLBAR_EXTENT 18U
#define NOTEPAD_SCROLLBAR_MIN_THUMB 8U

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
    NOTEPAD_ACTION_OPEN = 1U,
    NOTEPAD_ACTION_SAVE,
    NOTEPAD_ACTION_SAVE_AS,
    NOTEPAD_ACTION_EXIT,
    NOTEPAD_ACTION_ABOUT
};

enum {
    NOTEPAD_DIALOG_NONE = 0U,
    NOTEPAD_DIALOG_CONFIRM_EXIT,
    NOTEPAD_DIALOG_ERROR,
    NOTEPAD_DIALOG_ABOUT
};

enum {
    NOTEPAD_SCROLL_NONE = 0U,
    NOTEPAD_SCROLL_VERTICAL,
    NOTEPAD_SCROLL_HORIZONTAL
};

enum {
    NOTEPAD_CONTROL_VERTICAL_SCROLL = 2U,
    NOTEPAD_CONTROL_HORIZONTAL_SCROLL
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
    {"Oeffnen...", NOTEPAD_ACTION_OPEN, 0U, 0U, 0U},
    {"Speichern", NOTEPAD_ACTION_SAVE, 0U, 0U, 0U},
    {"Speichern unter...", NOTEPAD_ACTION_SAVE_AS, 0U, 0U, 0U},
    {"Beenden", NOTEPAD_ACTION_EXIT, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t help_items[] = {
    {"Ueber Editor", NOTEPAD_ACTION_ABOUT, 0U, 0U, 0U},
};

static const reist_gui_menu_t menus[] = {
    {"Datei", file_items, 4U, 0U, 0U},
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
    "Fester UTF-8/LF-Puffer, atomarer FAT-Speicherpfad und modale Dialoge.",
    close_buttons, 1U, REIST_GUI_DIALOG_APPLICATION_MODAL,
    REIST_GUI_DIALOG_RESPONSE_CLOSE, REIST_GUI_DIALOG_RESPONSE_CLOSE,
    REIST_GUI_DIALOG_NO_OWNER, 0U,
    REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
    {0U, 0U, 0U, 0U}
};

static const reist_gui_file_dialog_model_t open_file_model = {
    REIST_GUI_FILE_DIALOG_API_VERSION, sizeof(reist_gui_file_dialog_model_t),
    REIST_GUI_FILE_DIALOG_OPEN, "Datei oeffnen", "Oeffnen",
    {0U, 0U, 0U, 0U}
};

static const reist_gui_file_dialog_model_t save_file_model = {
    REIST_GUI_FILE_DIALOG_API_VERSION, sizeof(reist_gui_file_dialog_model_t),
    REIST_GUI_FILE_DIALOG_SAVE, "Speichern unter", "Speichern",
    {0U, 0U, 0U, 0U}
};

typedef struct notepad_state {
    reist_gui_menu_state_t menu;
    reist_gui_dialog_state_t dialog;
    reist_gui_file_dialog_state_t file_dialog;
    reist_gui_dialog_model_t error_model;
    reist_gui_text_editor_state_t editor;
    reist_gui_text_editor_model_t editor_model;
    reist_gui_range_state_t vertical_scroll;
    reist_gui_range_state_t horizontal_scroll;
    reist_gui_range_model_t vertical_scroll_model;
    reist_gui_range_model_t horizontal_scroll_model;
    reist_gui_text_editor_viewport_t viewport;
    char path[NOTEPAD_PATH_CAPACITY];
    char status[NOTEPAD_STATUS_CAPACITY];
    char error_detail[NOTEPAD_STATUS_CAPACITY];
    uint32_t dialog_kind;
    uint32_t file_dialog_mode;
    uint32_t exists;
    uint32_t io_blocked;
    uint32_t redraw;
    uint32_t dynamic_redraw;
    uint32_t overlay_redraw;
    uint32_t hover_redraw;
    uint32_t scrollbar_redraw;
    uint32_t exit_requested;
    uint32_t scroll_drag;
    uint32_t scroll_drag_offset;
    uint32_t scroll_pending_value;
    uint32_t scroll_pending_valid;
    reist_gui_piece_document_t document;
    reist_vfs_file_handle_t source_object;
    uint32_t window_offset;
    uint32_t window_length;
    uint32_t previous_window_offset;
    uint32_t cache_start;
    uint32_t cache_size;
    uint8_t read_cache[REIST_GUI_PIECE_IO_CAPACITY];
} notepad_state_t;

/* Large document storage remains static so the process stack stays bounded. */
static notepad_state_t application;
static char serialized[REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY + 1U];
static reist_gui_piece_document_t document_snapshot;
static reist_gui_surface_client_t *paint_surface;
static reist_gui_surface_client_t *main_surface;
static reist_gui_surface_client_t dialog_surface;
static x86os_display_info_t dialog_display;
static uint32_t dialog_surface_active;
static int paint_status;

static void copy_piece_document(reist_gui_piece_document_t *destination,
                                const reist_gui_piece_document_t *source) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    for (uint32_t index = 0U; index < sizeof(*destination); ++index)
        to[index] = from[index];
}

static uint32_t notepad_modified(const notepad_state_t *state) {
    return state != 0 && (state->editor.modified || state->document.modified);
}

static int object_read_at(reist_vfs_file_handle_t object, uint32_t offset,
                          void *data, uint32_t size) {
    uint32_t positioned = 0U;
    if (reist_vfs_file_set_timeout(object, 1000U) != 0 ||
        reist_vfs_file_seek(object, offset, REIST_VFS_SEEK_SET, &positioned) != 0 ||
        positioned != offset) return -1;
    uint32_t completed = 0U;
    while (completed < size) {
        uint32_t amount = size - completed;
        if (amount > REIST_GUI_PIECE_IO_CAPACITY)
            amount = REIST_GUI_PIECE_IO_CAPACITY;
        if (reist_vfs_file_set_timeout(object, 1000U) != 0) return -1;
        int count = reist_vfs_file_read_bulk(
            object, (uint8_t *)data + completed, amount);
        if (count <= 0 || (uint32_t)count > amount) return -1;
        completed += (uint32_t)count;
    }
    return 0;
}

static int piece_read_original(void *context, uint32_t offset,
                               void *data, uint32_t size) {
    notepad_state_t *state = (notepad_state_t *)context;
    if (state == 0 || state->source_object == REIST_VFS_FILE_INVALID_HANDLE)
        return -1;
    if (size <= sizeof(state->read_cache)) {
        if (state->cache_size == 0U || offset < state->cache_start ||
            offset - state->cache_start > state->cache_size ||
            size > state->cache_size - (offset - state->cache_start)) {
            uint32_t fill = state->document.original_size - offset;
            if (fill > sizeof(state->read_cache)) fill = sizeof(state->read_cache);
            if (object_read_at(state->source_object, offset,
                               state->read_cache, fill) != 0) return -1;
            state->cache_start = offset;
            state->cache_size = fill;
        }
        uint8_t *output = (uint8_t *)data;
        uint32_t within = offset - state->cache_start;
        for (uint32_t index = 0U; index < size; ++index)
            output[index] = state->read_cache[within + index];
        return 0;
    }
    return object_read_at(state->source_object, offset, data, size);
}

static void request_overlay_redraw(notepad_state_t *state) {
    if (state == 0) return;
    if (main_surface != 0)
        state->overlay_redraw = state->hover_redraw = 1U;
    else
        state->redraw = 1U;
}

static void request_hover_redraw(notepad_state_t *state) {
    if (state == 0) return;
    if (main_surface != 0)
        state->hover_redraw = 1U;
    else
        state->redraw = 1U;
}

static void request_dynamic_redraw(notepad_state_t *state) {
    if (state == 0) return;
    if (main_surface != 0)
        state->dynamic_redraw = 1U;
    else
        state->redraw = 1U;
}

static void request_scrollbar_redraw(notepad_state_t *state) {
    if (state == 0) return;
    if (main_surface != 0) {
        state->scrollbar_redraw = 1U;
        state->hover_redraw = 1U;
    } else {
        state->redraw = 1U;
    }
}

static uint32_t paint_status_retryable(int status) {
    return status == -11 || status == -22 || status == -75 ||
        status == -110 || status == -114;
}

static uint32_t same_surface(reist_gui_surface_handle_t left,
                             reist_gui_surface_handle_t right) {
    return left.id == right.id && left.generation == right.generation;
}

static int accept_configure_bounded(
    reist_gui_surface_client_t *client,
    const reist_gui_surface_message_t *message) {
    int status = -11;
    for (uint32_t attempt = 0U; attempt < NOTEPAD_PAINT_RETRY_LIMIT;
         ++attempt) {
        status = reist_gui_surface_client_accept_configure(client, message);
        if (status == 0) return 0;
        if (!paint_status_retryable(status)) return status;
        if (attempt + 1U < NOTEPAD_PAINT_RETRY_LIMIT)
            (void)x86os_sleep_ms(5U);
    }
    return status;
}

static void report_paint_failure(const char *message, int status) {
    x86os_puts(message);
    x86os_print_number(status);
    x86os_putchar('\n');
}

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

static uint32_t utf8_slice(const char *value, size_t length,
                           uint32_t first_scalar, uint32_t maximum_scalars,
                           size_t *offset_out, size_t *bytes_out,
                           uint32_t *scalars_out) {
    if (value == 0 || offset_out == 0 || bytes_out == 0 ||
        scalars_out == 0) return 0U;
    size_t total_scalars = 0U;
    if (!reist_utf8_scan(value, length, &total_scalars) ||
        first_scalar > total_scalars) return 0U;
    size_t offset = 0U;
    for (uint32_t column = 0U; column < first_scalar; ++column) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(value + offset, length - offset,
                                   &consumed, &scalar)) return 0U;
        offset += consumed;
    }
    size_t bytes = 0U;
    size_t scalars = 0U;
    if (!reist_utf8_prefix(value + offset, length - offset,
                           maximum_scalars, &bytes, &scalars) ||
        scalars > UINT32_MAX) return 0U;
    *offset_out = offset;
    *bytes_out = bytes;
    *scalars_out = (uint32_t)scalars;
    return 1U;
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
    if (!rect.width || !rect.height) return;
    if (paint_surface != 0) {
        if (rect.x < 0) {
            uint32_t amount = (uint32_t)(-rect.x);
            if (amount >= rect.width) return;
            rect.width -= amount;
            rect.x = 0;
        }
        if (rect.y < 0) {
            uint32_t amount = (uint32_t)(-rect.y);
            if (amount >= rect.height) return;
            rect.height -= amount;
            rect.y = 0;
        }
        if ((uint32_t)rect.x >= paint_surface->width ||
            (uint32_t)rect.y >= paint_surface->height) return;
        if (rect.width > paint_surface->width - (uint32_t)rect.x)
            rect.width = paint_surface->width - (uint32_t)rect.x;
        if (rect.height > paint_surface->height - (uint32_t)rect.y)
            rect.height = paint_surface->height - (uint32_t)rect.y;
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_fill(
                paint_surface, rect, color);
        return;
    }
    (void)x86os_fill_rect(rect.x, rect.y, rect.width, rect.height, color);
}

static void text(const x86os_display_info_t *display,
                 int32_t x, int32_t y, const char *value,
                 uint32_t maximum_width, uint32_t foreground,
                 uint32_t background) {
    if (display == 0 || value == 0 || display->font_width == 0U) return;
    size_t length = bounded_length(value, NOTEPAD_TEXT_LIMIT);
    if (paint_surface != 0) {
        if (x < 0 || y < 0 || (uint32_t)x >= paint_surface->width ||
            (uint32_t)y >= paint_surface->height) return;
        uint32_t available = paint_surface->width - (uint32_t)x;
        if (maximum_width > available) maximum_width = available;
    }
    size_t capacity = maximum_width / display->font_width;
    size_t selected_bytes = 0U;
    size_t selected_scalars = 0U;
    if (!reist_utf8_prefix(value, length, capacity,
                           &selected_bytes, &selected_scalars) ||
        selected_bytes == 0U) return;
    if (paint_surface != 0) {
        size_t offset = 0U;
        uint32_t painted_scalars = 0U;
        while (offset < selected_bytes && paint_status == 0) {
            size_t chunk_bytes = 0U;
            uint32_t chunk_scalars = 0U;
            while (offset + chunk_bytes < selected_bytes) {
                size_t consumed = 0U;
                uint32_t scalar = 0U;
                if (!reist_utf8_decode_one(
                        value + offset + chunk_bytes,
                        selected_bytes - offset - chunk_bytes,
                        &consumed, &scalar) ||
                    chunk_bytes + consumed >=
                        REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY) break;
                chunk_bytes += consumed;
                ++chunk_scalars;
            }
            if (chunk_bytes == 0U) return;
            uint32_t chunk_width = chunk_scalars * display->font_width;
            paint_status = reist_gui_surface_client_paint_text(
                paint_surface,
                x + (int32_t)(painted_scalars * display->font_width), y,
                chunk_width, value + offset, (uint32_t)chunk_bytes,
                foreground, background);
            offset += chunk_bytes;
            painted_scalars += chunk_scalars;
        }
        return;
    }
    (void)x86os_draw_text_pixels(
        x, y, value, selected_bytes, foreground, background);
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
        8U, 8U, 4U, 6U, {0U, 0U, 0U, 0U},
        REIST_GUI_MENU_POPUP_BELOW};
}

static reist_gui_dialog_layout_t dialog_layout(
    const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 8U;
    uint32_t work_height = display->height > top + 8U
        ? display->height - top - 8U : 1U;
    uint32_t width = display->width > 48U ? display->width - 48U : 1U;
    if (width > 620U) width = 620U;
    uint32_t height = work_height > 230U ? 230U : work_height;
    reist_gui_rect_t work = {0, (int32_t)top, display->width, work_height};
    reist_gui_rect_t initial = {
        (int32_t)((display->width - width) / 2U),
        (int32_t)(top + (work_height - height) / 2U), width, height};
    if (dialog_surface_active &&
        (paint_surface == &dialog_surface || display == &dialog_display)) {
        work = (reist_gui_rect_t){0, 0, display->width, display->height};
        initial = work;
    }
    return (reist_gui_dialog_layout_t){
        REIST_GUI_DIALOG_API_VERSION, sizeof(reist_gui_dialog_layout_t),
        display->width, display->height,
        work, initial,
        menu_height(display), 3U, display->font_width, display->font_height,
        88U, max_u32(display->font_height + 10U, 26U),
        8U, 8U, 12U, 6U, {0U, 0U, 0U, 0U}};
}

static reist_gui_file_dialog_layout_t file_dialog_layout(
    const x86os_display_info_t *display) {
    uint32_t width = display->width > 48U ? display->width - 48U : 1U;
    if (width > 620U) width = 620U;
    uint32_t height = 180U;
    if (height + 16U > display->height) height = display->height - 16U;
    int32_t x = (int32_t)((display->width - width) / 2U);
    int32_t y = (int32_t)((display->height - height) / 2U);
    uint32_t title_height = menu_height(display);
    uint32_t button_width = 104U;
    uint32_t button_height = max_u32(display->font_height + 10U, 26U);
    return (reist_gui_file_dialog_layout_t){
        REIST_GUI_FILE_DIALOG_API_VERSION,
        sizeof(reist_gui_file_dialog_layout_t),
        {x, y, width, height}, {x, y, width, title_height},
        {x + 16, y + (int32_t)title_height + 42,
         width > 32U ? width - 32U : 1U,
         max_u32(display->font_height + 10U, 26U)},
        {x + (int32_t)width - (int32_t)(button_width * 2U + 28U),
         y + (int32_t)height - (int32_t)button_height - 14,
         button_width, button_height},
        {x + (int32_t)width - (int32_t)(button_width + 16U),
         y + (int32_t)height - (int32_t)button_height - 14,
         button_width, button_height},
        display->font_width, {0U, 0U, 0U, 0U}};
}

static reist_gui_rect_t editor_frame(const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 4U;
    return (reist_gui_rect_t){
        4, (int32_t)top,
        display->width > 8U ? display->width - 8U : 1U,
        display->height > top + 4U ? display->height - top - 4U : 1U};
}

typedef struct notepad_scrollbar_geometry {
    reist_gui_rect_t decrement;
    reist_gui_rect_t increment;
    reist_gui_rect_t track;
    reist_gui_rect_t thumb;
    uint32_t travel;
} notepad_scrollbar_geometry_t;

static uint32_t point_inside(reist_gui_rect_t rect, int32_t x, int32_t y) {
    if (x < rect.x || y < rect.y) return 0U;
    return (uint32_t)(x - rect.x) < rect.width &&
           (uint32_t)(y - rect.y) < rect.height;
}

static uint32_t range_model_matches(
    const reist_gui_range_model_t *left,
    const reist_gui_range_model_t *right) {
    return left->version == right->version &&
        left->struct_size == right->struct_size && left->id == right->id &&
        left->bounds.x == right->bounds.x && left->bounds.y == right->bounds.y &&
        left->bounds.width == right->bounds.width &&
        left->bounds.height == right->bounds.height &&
        left->minimum == right->minimum && left->maximum == right->maximum &&
        left->step == right->step && left->page_step == right->page_step &&
        left->role == right->role &&
        left->orientation == right->orientation &&
        left->flags == right->flags;
}

static int synchronize_range(reist_gui_range_model_t *model,
                             reist_gui_range_state_t *range,
                             const reist_gui_range_model_t *next,
                             uint32_t value) {
    reist_gui_value_result_t result;
    reist_gui_value_result_initialize(&result);
    if (!range->configured || !range_model_matches(model, next)) {
        *model = *next;
        reist_gui_range_state_initialize(range);
        return reist_gui_range_configure(
            model, range, (int32_t)value, &result);
    }
    return reist_gui_range_set(model, range, (int32_t)value, &result);
}

static int synchronize_scrollbars(notepad_state_t *state) {
    reist_gui_text_editor_viewport_t viewport;
    if (reist_gui_text_editor_get_viewport(
            &state->editor_model, &state->editor, &viewport) != 0)
        return -1;
    reist_gui_text_editor_result_t editor_result;
    reist_gui_text_editor_result_initialize(&editor_result);
    if (reist_gui_text_editor_scroll_to(
            &state->editor_model, &state->editor,
            viewport.first_line, viewport.first_column,
            &editor_result) != 0)
        return -1;

    uint32_t vertical_maximum = viewport.maximum_first_line;
    uint32_t horizontal_maximum = viewport.maximum_first_column;
    reist_gui_rect_t editor = state->editor_model.bounds;
    reist_gui_range_model_t vertical = {
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_range_model_t),
        NOTEPAD_CONTROL_VERTICAL_SCROLL, "Vertikal scrollen",
        {editor.x + (int32_t)editor.width + 2, editor.y,
         NOTEPAD_SCROLLBAR_EXTENT, editor.height},
        0, (int32_t)(vertical_maximum ? vertical_maximum : 1U),
        1U, viewport.visible_lines,
        REIST_GUI_RANGE_SCROLLBAR, REIST_GUI_VERTICAL,
        REIST_GUI_VALUE_VISIBLE |
            (vertical_maximum ? REIST_GUI_VALUE_ENABLED : 0U),
        {0U, 0U, 0U, 0U}};
    reist_gui_range_model_t horizontal = {
        REIST_GUI_VALUE_API_VERSION, sizeof(reist_gui_range_model_t),
        NOTEPAD_CONTROL_HORIZONTAL_SCROLL, "Horizontal scrollen",
        {editor.x, editor.y + (int32_t)editor.height + 2,
         editor.width, NOTEPAD_SCROLLBAR_EXTENT},
        0, (int32_t)(horizontal_maximum ? horizontal_maximum : 1U),
        1U, viewport.visible_columns,
        REIST_GUI_RANGE_SCROLLBAR, REIST_GUI_HORIZONTAL,
        REIST_GUI_VALUE_VISIBLE |
            (horizontal_maximum ? REIST_GUI_VALUE_ENABLED : 0U),
        {0U, 0U, 0U, 0U}};
    if (synchronize_range(&state->vertical_scroll_model,
                          &state->vertical_scroll, &vertical,
                          viewport.first_line) != 0 ||
        synchronize_range(&state->horizontal_scroll_model,
                          &state->horizontal_scroll, &horizontal,
                          viewport.first_column) != 0)
        return -1;
    state->viewport = viewport;
    if (editor_result.full_redraw) state->redraw = 1U;
    return 0;
}

static notepad_scrollbar_geometry_t scrollbar_geometry(
    const reist_gui_range_model_t *model,
    const reist_gui_range_state_t *range,
    uint32_t maximum) {
    notepad_scrollbar_geometry_t geometry = {0};
    uint32_t extent = model->orientation == REIST_GUI_HORIZONTAL
        ? model->bounds.width : model->bounds.height;
    uint32_t cross = model->orientation == REIST_GUI_HORIZONTAL
        ? model->bounds.height : model->bounds.width;
    uint32_t button = cross;
    if (button * 2U > extent) button = extent / 2U;
    uint32_t track_extent = extent > button * 2U
        ? extent - button * 2U : 1U;
    if (model->orientation == REIST_GUI_HORIZONTAL) {
        geometry.decrement = (reist_gui_rect_t){
            model->bounds.x, model->bounds.y, button, model->bounds.height};
        geometry.increment = (reist_gui_rect_t){
            model->bounds.x + (int32_t)extent - (int32_t)button,
            model->bounds.y, button, model->bounds.height};
        geometry.track = (reist_gui_rect_t){
            model->bounds.x + (int32_t)button, model->bounds.y,
            track_extent, model->bounds.height};
    } else {
        geometry.decrement = (reist_gui_rect_t){
            model->bounds.x, model->bounds.y, model->bounds.width, button};
        geometry.increment = (reist_gui_rect_t){
            model->bounds.x,
            model->bounds.y + (int32_t)extent - (int32_t)button,
            model->bounds.width, button};
        geometry.track = (reist_gui_rect_t){
            model->bounds.x, model->bounds.y + (int32_t)button,
            model->bounds.width, track_extent};
    }
    uint32_t page = model->page_step ? model->page_step : 1U;
    uint32_t denominator = maximum <= UINT32_MAX - page
        ? maximum + page : UINT32_MAX;
    uint32_t thumb_product = page != 0U &&
        track_extent > UINT32_MAX / page ? UINT32_MAX : track_extent * page;
    uint32_t thumb_extent = maximum == 0U ? track_extent :
        thumb_product / denominator;
    if (thumb_extent < NOTEPAD_SCROLLBAR_MIN_THUMB)
        thumb_extent = NOTEPAD_SCROLLBAR_MIN_THUMB;
    if (thumb_extent > track_extent) thumb_extent = track_extent;
    geometry.travel = track_extent - thumb_extent;
    uint32_t range_value = range->value < 0 ? 0U : (uint32_t)range->value;
    if (range_value > maximum) range_value = maximum;
    uint32_t offset_product = range_value != 0U &&
        geometry.travel > UINT32_MAX / range_value
        ? UINT32_MAX : geometry.travel * range_value;
    uint32_t offset = maximum && geometry.travel
        ? offset_product / maximum : 0U;
    if (model->orientation == REIST_GUI_HORIZONTAL)
        geometry.thumb = (reist_gui_rect_t){
            geometry.track.x + (int32_t)offset, geometry.track.y,
            thumb_extent, geometry.track.height};
    else
        geometry.thumb = (reist_gui_rect_t){
            geometry.track.x, geometry.track.y + (int32_t)offset,
            geometry.track.width, thumb_extent};
    return geometry;
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
        uint32_t background = color_face;
        fill(title, color_face);
        uint32_t y = title.height > display->font_height
            ? (title.height - display->font_height) / 2U : 0U;
        text(display, title.x + (int32_t)layout.title_padding_x,
             title.y + (int32_t)y, menu_model.menus[index].label,
             title.width - layout.title_padding_x * 2U,
             color_text, background);
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
        uint32_t background = color_face;
        uint32_t y = item.height > display->font_height
            ? (item.height - display->font_height) / 2U : 0U;
        text(display, item.x + (int32_t)layout.item_padding_x,
             item.y + (int32_t)y, menu->items[index].label,
             item.width - layout.item_padding_x * 2U,
             color_text, background);
    }
}

static void render_menu_hover(const x86os_display_info_t *display,
                              const notepad_state_t *state) {
    if (state->menu.open_menu == REIST_GUI_MENU_NO_INDEX) return;
    reist_gui_menu_layout_t layout = menu_layout(display);
    uint32_t menu_index = state->menu.open_menu;
    reist_gui_rect_t title;
    if (reist_gui_menu_title_rect(
            &menu_model, &layout, menu_index, &title) == 0) {
        fill(title, color_active);
        uint32_t y = title.height > display->font_height
            ? (title.height - display->font_height) / 2U : 0U;
        text(display, title.x + (int32_t)layout.title_padding_x,
             title.y + (int32_t)y, menu_model.menus[menu_index].label,
             title.width - layout.title_padding_x * 2U,
             color_title_text, color_active);
    }
    const reist_gui_menu_t *menu = &menu_model.menus[menu_index];
    if (state->menu.hot_item >= menu->item_count) return;
    reist_gui_rect_t item;
    if (reist_gui_menu_item_rect(
            &menu_model, &layout, menu_index,
            state->menu.hot_item, &item) != 0) return;
    fill(item, color_active);
    uint32_t y = item.height > display->font_height
        ? (item.height - display->font_height) / 2U : 0U;
    text(display, item.x + (int32_t)layout.item_padding_x,
         item.y + (int32_t)y, menu->items[state->menu.hot_item].label,
         item.width - layout.item_padding_x * 2U,
         color_title_text, color_active);
}

static void render_scrollbar(const x86os_display_info_t *display,
                             const reist_gui_range_model_t *model,
                             const reist_gui_range_state_t *range,
                             uint32_t maximum) {
    notepad_scrollbar_geometry_t geometry =
        scrollbar_geometry(model, range, maximum);
    uint32_t enabled = maximum != 0U;
    bevel(model->bounds, color_face, 0U);
    fill(geometry.track, color_light);
    bevel(geometry.decrement, color_face, 1U);
    bevel(geometry.increment, color_face, 1U);
    bevel(geometry.thumb, color_face, enabled ? 1U : 0U);
    const char *decrement = model->orientation == REIST_GUI_HORIZONTAL
        ? "<" : "^";
    const char *increment = model->orientation == REIST_GUI_HORIZONTAL
        ? ">" : "v";
    uint32_t foreground = enabled ? color_text : color_shadow;
    int32_t decrement_x = geometry.decrement.x +
        (int32_t)((geometry.decrement.width > display->font_width
            ? geometry.decrement.width - display->font_width : 0U) / 2U);
    int32_t decrement_y = geometry.decrement.y +
        (int32_t)((geometry.decrement.height > display->font_height
            ? geometry.decrement.height - display->font_height : 0U) / 2U);
    int32_t increment_x = geometry.increment.x +
        (int32_t)((geometry.increment.width > display->font_width
            ? geometry.increment.width - display->font_width : 0U) / 2U);
    int32_t increment_y = geometry.increment.y +
        (int32_t)((geometry.increment.height > display->font_height
            ? geometry.increment.height - display->font_height : 0U) / 2U);
    text(display, decrement_x, decrement_y, decrement,
         display->font_width, foreground, color_face);
    text(display, increment_x, increment_y, increment,
         display->font_width, foreground, color_face);
}

/* During an implicit scrollbar grab, keep only the changing track and thumb
 * in the highest retained layer.  Replacing this small layer removes the old
 * thumb before drawing the new position, while the larger editor viewport
 * remains eligible for bounded coalescing on the next event-loop turn. */
static void render_scrollbar_feedback(
        const x86os_display_info_t *display,
        const notepad_state_t *state) {
    if (state->scroll_drag != NOTEPAD_SCROLL_VERTICAL &&
        state->scroll_drag != NOTEPAD_SCROLL_HORIZONTAL)
        return;
    const reist_gui_range_model_t *model =
        state->scroll_drag == NOTEPAD_SCROLL_VERTICAL
            ? &state->vertical_scroll_model
            : &state->horizontal_scroll_model;
    const reist_gui_range_state_t *range =
        state->scroll_drag == NOTEPAD_SCROLL_VERTICAL
            ? &state->vertical_scroll
            : &state->horizontal_scroll;
    uint32_t maximum = state->scroll_drag == NOTEPAD_SCROLL_VERTICAL
        ? state->viewport.maximum_first_line
        : state->viewport.maximum_first_column;
    if (maximum == 0U) return;
    notepad_scrollbar_geometry_t geometry =
        scrollbar_geometry(model, range, maximum);
    fill(geometry.track, color_light);
    bevel(geometry.thumb, color_face, 1U);
    (void)display;
}

static void render_editor_chrome(const x86os_display_info_t *display,
                                 const notepad_state_t *state) {
    reist_gui_rect_t frame = editor_frame(display);
    bevel(frame, color_face, 1U);
    reist_gui_rect_t editor = state->editor_model.bounds;
    bevel((reist_gui_rect_t){editor.x - 2, editor.y - 2,
                             editor.width + 4U, editor.height + 4U},
          color_face, 0U);
}

static void render_editor(const x86os_display_info_t *display,
                          const notepad_state_t *state) {
    reist_gui_rect_t frame = editor_frame(display);
    reist_gui_rect_t editor = state->editor_model.bounds;
    fill(editor, color_editor);
    uint32_t rows = editor.height / display->font_height;
    uint32_t columns = editor.width / display->font_width;
    for (uint32_t row = 0U; row < rows; ++row) {
        uint32_t line_index = state->editor.first_line + row;
        if (line_index >= state->editor.line_count) break;
        const char *line = state->editor.lines[line_index];
        uint32_t length = line_length(line);
        size_t offset = 0U;
        size_t amount = 0U;
        uint32_t scalar_amount = 0U;
        if (!utf8_slice(line, length, state->editor.first_column, columns,
                        &offset, &amount, &scalar_amount) ||
            amount == 0U) continue;
        uint32_t line_width = scalar_amount * display->font_width;
        text(display, editor.x,
             editor.y + (int32_t)(row * display->font_height),
             line + offset, line_width,
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

    render_scrollbar(display, &state->vertical_scroll_model,
                     &state->vertical_scroll,
                     state->viewport.maximum_first_line);
    render_scrollbar(display, &state->horizontal_scroll_model,
                     &state->horizontal_scroll,
                     state->viewport.maximum_first_column);
    bevel((reist_gui_rect_t){
              state->vertical_scroll_model.bounds.x,
              state->horizontal_scroll_model.bounds.y,
              state->vertical_scroll_model.bounds.width,
              state->horizontal_scroll_model.bounds.height},
          color_face, 1U);

    reist_gui_rect_t status = {
        frame.x + 6,
        frame.y + (int32_t)frame.height - (int32_t)display->font_height - 8,
        frame.width > 12U ? frame.width - 12U : 1U,
        display->font_height + 4U};
    fill(status, color_face);
    char status_text[NOTEPAD_STATUS_CAPACITY];
    size_t used = 0U;
    status_text[0] = '\0';
    append_text(status_text, sizeof(status_text), &used, state->path);
    if (notepad_modified(state))
        append_text(status_text, sizeof(status_text), &used, " *");
    append_text(status_text, sizeof(status_text), &used, "  ");
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
    uint32_t separate = dialog_surface_active &&
        paint_surface == &dialog_surface;
    if (separate) {
        fill(frame, color_face);
    } else {
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
    }
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

static void render_file_dialog(const x86os_display_info_t *display,
                               const notepad_state_t *state) {
    if (!state->file_dialog.visible) return;
    const reist_gui_file_dialog_model_t *model =
        state->file_dialog_mode == REIST_GUI_FILE_DIALOG_SAVE
            ? &save_file_model : &open_file_model;
    reist_gui_file_dialog_layout_t layout = file_dialog_layout(display);
    fill((reist_gui_rect_t){layout.frame.x + 5, layout.frame.y + 5,
                            layout.frame.width, layout.frame.height},
         color_dark);
    bevel(layout.frame, color_face, 1U);
    fill(layout.title, color_active);
    text(display, layout.title.x + 10,
         layout.title.y + (int32_t)((layout.title.height -
             display->font_height) / 2U), model->title,
         layout.title.width > 20U ? layout.title.width - 20U : 1U,
         color_title_text, color_active);
    text(display, layout.path.x, layout.path.y - (int32_t)display->font_height - 4,
         "Dateipfad:", layout.path.width, color_text, color_face);
    bevel(layout.path, color_editor,
          state->file_dialog.focus != REIST_GUI_FILE_DIALOG_FOCUS_PATH);
    uint32_t visible_chars = layout.path.width > 12U
        ? (layout.path.width - 12U) / display->font_width : 0U;
    uint32_t first = state->file_dialog.length > visible_chars
        ? state->file_dialog.length - visible_chars : 0U;
    text(display, layout.path.x + 6,
         layout.path.y + (int32_t)((layout.path.height -
             display->font_height) / 2U), state->file_dialog.path + first,
         layout.path.width > 12U ? layout.path.width - 12U : 1U,
         color_text, color_editor);
    if (state->file_dialog.focus == REIST_GUI_FILE_DIALOG_FOCUS_PATH &&
        state->file_dialog.cursor >= first) {
        uint32_t column = state->file_dialog.cursor - first;
        if (column <= visible_chars)
            fill((reist_gui_rect_t){
                layout.path.x + 6 + (int32_t)(column * display->font_width),
                layout.path.y + 5, 2U,
                layout.path.height > 10U ? layout.path.height - 10U : 1U},
                color_dark);
    }
    const reist_gui_rect_t buttons[2] = {
        layout.accept_button, layout.cancel_button};
    const char *labels[2] = {model->accept_label, "Abbrechen"};
    for (uint32_t index = 0U; index < 2U; ++index) {
        uint32_t focus = REIST_GUI_FILE_DIALOG_FOCUS_ACCEPT + index;
        if (state->file_dialog.focus == focus)
            bevel((reist_gui_rect_t){buttons[index].x - 2,
                    buttons[index].y - 2, buttons[index].width + 4U,
                    buttons[index].height + 4U}, color_dark, 0U);
        bevel(buttons[index], color_face,
              state->file_dialog.capture != focus);
        size_t length = bounded_length(labels[index], 32U);
        uint32_t label_width = (uint32_t)length * display->font_width;
        text(display,
             buttons[index].x + (int32_t)((buttons[index].width > label_width
                ? buttons[index].width - label_width : 0U) / 2U),
             buttons[index].y + (int32_t)((buttons[index].height >
                display->font_height ? buttons[index].height -
                display->font_height : 0U) / 2U), labels[index],
             buttons[index].width, color_text, color_face);
    }
}

static void render_base_scene(const x86os_display_info_t *display,
                              const notepad_state_t *state) {
    fill((reist_gui_rect_t){0, 0, display->width, display->height},
         paint_surface != 0 ? color_face : color_desktop);
    render_editor_chrome(display, state);
}

static void render_dynamic_scene(const x86os_display_info_t *display,
                                 const notepad_state_t *state) {
    render_editor(display, state);
}

static void render_overlay_scene(const x86os_display_info_t *display,
                                 const notepad_state_t *state) {
    render_menu(display, state);
    if (!dialog_surface_active) render_dialog(display, state);
    render_file_dialog(display, state);
}

static void render_hover_scene(const x86os_display_info_t *display,
                               const notepad_state_t *state) {
    render_scrollbar_feedback(display, state);
    render_menu_hover(display, state);
}

static void render_scene(const x86os_display_info_t *display,
                         const notepad_state_t *state) {
    render_base_scene(display, state);
    render_dynamic_scene(display, state);
    render_overlay_scene(display, state);
    render_hover_scene(display, state);
}

static void render_separate_dialog(const notepad_state_t *state) {
    if (!dialog_surface_active || !state->dialog.visible) return;
    reist_gui_surface_client_t *saved = paint_surface;
    paint_surface = &dialog_surface;
    paint_status = reist_gui_surface_client_paint_begin(&dialog_surface);
    if (paint_status == 0) render_dialog(&dialog_display, state);
    if (paint_status == 0)
        paint_status = reist_gui_surface_client_paint_commit(&dialog_surface);
    paint_surface = saved;
}

static void render(const x86os_display_info_t *display,
                   const notepad_state_t *state) {
    if (paint_surface != 0) {
        paint_status = reist_gui_surface_client_paint_begin_layer(
            paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_BASE);
        if (paint_status == 0) render_base_scene(display, state);
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_commit_layer(
                paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_BASE);
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_begin_layer(
                paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC);
        if (paint_status == 0) render_dynamic_scene(display, state);
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_commit_layer(
                paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC);
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_begin_layer(
                paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY);
        if (paint_status == 0) render_overlay_scene(display, state);
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_commit_layer(
                paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY);
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_begin_layer(
                paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_HOVER);
        if (paint_status == 0) render_hover_scene(display, state);
        if (paint_status == 0)
            paint_status = reist_gui_surface_client_paint_commit_layer(
                paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_HOVER);
        return;
    }
    uint32_t serial = 0U;
    uint32_t transaction = x86os_display_frame_begin(&serial) == 0;
    render_scene(display, state);
    if (transaction && x86os_display_frame_commit(serial) != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_scene(display, state);
    }
}

static void render_dynamic(const x86os_display_info_t *display,
                           const notepad_state_t *state) {
    if (paint_surface == 0) {
        render(display, state);
        return;
    }
    paint_status = reist_gui_surface_client_paint_begin_layer(
        paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC);
    if (paint_status == 0) render_dynamic_scene(display, state);
    if (paint_status == 0)
        paint_status = reist_gui_surface_client_paint_commit_layer(
            paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC);
}

static void render_overlay(const x86os_display_info_t *display,
                           const notepad_state_t *state) {
    if (paint_surface == 0) {
        render(display, state);
        return;
    }
    paint_status = reist_gui_surface_client_paint_begin_layer(
        paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY);
    if (paint_status == 0) render_overlay_scene(display, state);
    if (paint_status == 0)
        paint_status = reist_gui_surface_client_paint_commit_layer(
            paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY);
}

static void render_hover(const x86os_display_info_t *display,
                         const notepad_state_t *state) {
    if (paint_surface == 0) {
        render(display, state);
        return;
    }
    paint_status = reist_gui_surface_client_paint_begin_layer(
        paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_HOVER);
    if (paint_status == 0) render_hover_scene(display, state);
    if (paint_status == 0)
        paint_status = reist_gui_surface_client_paint_commit_layer(
            paint_surface, REIST_GUI_SURFACE_PAINT_LAYER_HOVER);
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

static int piece_write_descriptor(void *context, const void *data,
                                  uint32_t size) {
    if (context == 0) return -1;
    return write_all(*(int *)context, (const char *)data, size);
}

static int load_document(notepad_state_t *state);

static int sync_piece_window(notepad_state_t *state) {
    if (!state->editor.modified) return 0;
    size_t length = 0U;
    if (reist_gui_text_editor_get_text(
            &state->editor_model, &state->editor,
            serialized, sizeof(serialized), &length) != 0) return -1;
    copy_piece_document(&document_snapshot, &state->document);
    if (reist_gui_piece_document_erase(
            &state->document, state->window_offset,
            state->window_length) != 0 ||
        reist_gui_piece_document_insert(
            &state->document, state->window_offset,
            serialized, (uint32_t)length) != 0) {
        copy_piece_document(&state->document, &document_snapshot);
        return -1;
    }
    state->window_length = (uint32_t)length;
    return 0;
}

static int materialize_piece_window(notepad_state_t *state, uint32_t offset) {
    if (offset > state->document.size) return -1;
    uint32_t amount = state->document.size - offset;
    if (amount > REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY) amount =
        REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY;
    if (reist_gui_piece_document_read(
            &state->document, offset, serialized, amount) != 0) return -1;
    size_t valid_bytes = amount, scalars = 0U;
    while (valid_bytes != 0U &&
           !reist_utf8_scan(serialized, valid_bytes, &scalars) &&
           amount - valid_bytes < 3U) --valid_bytes;
    if (!reist_utf8_scan(serialized, valid_bytes, &scalars)) return -1;
    size_t selected = 0U;
    uint32_t lines_used = 1U, line_bytes = 0U;
    while (selected < valid_bytes) {
        size_t consumed = 0U; uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(serialized + selected,
                valid_bytes - selected, &consumed, &scalar)) return -1;
        if (scalar == '\n') {
            if (lines_used == REIST_GUI_TEXT_EDITOR_MAX_LINES) break;
            ++lines_used; line_bytes = 0U;
        } else {
            if (line_bytes + consumed >= REIST_GUI_TEXT_EDITOR_LINE_CAPACITY)
                break;
            line_bytes += (uint32_t)consumed;
        }
        selected += consumed;
    }
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    if (reist_gui_text_editor_set_text(
            &state->editor_model, &state->editor,
            serialized, selected, &result) != 0) return -1;
    state->window_offset = offset;
    state->window_length = (uint32_t)selected;
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
    if (state->io_blocked) return -1;
    if (sync_piece_window(state) != 0) return -1;
    char temp[NOTEPAD_PATH_CAPACITY];
    if (make_temp_path(state->path, temp) != 0) return -1;
    (void)x86os_unlink(temp);
    int descriptor = x86os_create(temp);
    if (descriptor < 0) return -1;
    int write_status = reist_gui_piece_document_stream(
        &state->document, piece_write_descriptor, &descriptor);
    int sync_status = write_status == 0 ? x86os_fsync(descriptor) : -1;
    int close_status = x86os_close(descriptor);
    if (write_status != 0 || sync_status < 0 || close_status < 0 ||
        x86os_rename(temp, state->path) != 0) {
        (void)x86os_unlink(temp);
        return -1;
    }
    if (state->source_object != REIST_VFS_FILE_INVALID_HANDLE) {
        (void)reist_vfs_file_set_timeout(state->source_object, 1U);
        (void)reist_vfs_file_close(state->source_object);
        state->source_object = REIST_VFS_FILE_INVALID_HANDLE;
    }
    if (load_document(state) != 0) return -1;
    (void)copy_text(state->status, sizeof(state->status), "Gespeichert");
    state->redraw = 1U;
    return 0;
}

static int load_document(notepad_state_t *state) {
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int open_status = reist_vfs_file_open_rights(
        state->path, REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT |
            REIST_VFS_FILE_RIGHT_SEEK, &handle);
    if (open_status == -2) {
        state->exists = 0U;
        if (state->source_object != REIST_VFS_FILE_INVALID_HANDLE) {
            (void)reist_vfs_file_set_timeout(state->source_object, 1U);
            (void)reist_vfs_file_close(state->source_object);
        }
        state->source_object = REIST_VFS_FILE_INVALID_HANDLE;
        reist_gui_piece_document_initialize(&state->document);
        state->window_offset = state->window_length = 0U;
        (void)copy_text(state->status, sizeof(state->status), "Neue Datei");
        return 0;
    }
    if (open_status != 0) return -1;
    x86os_file_info_t info;
    int stat_status = reist_vfs_file_fstat(handle, &info);
    if (stat_status != 0) {
        (void)reist_vfs_file_close(handle);
        return -1;
    }
    if (info.type != X86OS_FILE) {
        (void)reist_vfs_file_close(handle);
        return -2;
    }
    reist_vfs_file_handle_t previous_object = state->source_object;
    copy_piece_document(&document_snapshot, &state->document);
    state->source_object = handle;
    state->cache_start = state->cache_size = 0U;
    if (reist_gui_piece_document_open(
            &state->document, info.size, piece_read_original, state) != 0) {
        (void)reist_vfs_file_close(handle);
        state->source_object = previous_object;
        copy_piece_document(&state->document, &document_snapshot);
        return -1;
    }
    if (materialize_piece_window(state, 0U) != 0) {
        (void)reist_vfs_file_close(handle);
        state->source_object = previous_object;
        copy_piece_document(&state->document, &document_snapshot);
        return -3;
    }
    if (previous_object != REIST_VFS_FILE_INVALID_HANDLE) {
        (void)reist_vfs_file_set_timeout(previous_object, 1U);
        (void)reist_vfs_file_close(previous_object);
    }
    state->exists = 1U;
    (void)copy_text(state->status, sizeof(state->status),
        info.size > state->window_length
            ? "Geladen (Piece-Table-Fenster)" : "Geladen");
    return 0;
}

static int create_large_probe_document(const char *path) {
    static char block[REIST_GUI_PIECE_IO_CAPACITY];
    for (uint32_t offset = 0U; offset < sizeof(block); ++offset)
        block[offset] = (offset % 80U) == 79U
            ? '\n' : (char)('a' + (offset % 26U));
    int descriptor = x86os_create(path);
    if (descriptor < 0) return -1;
    for (uint32_t index = 0U; index < 13U; ++index) {
        if (write_all(descriptor, block, sizeof(block)) != 0) {
            (void)x86os_close(descriptor); (void)x86os_unlink(path); return -1;
        }
    }
    int sync_status = x86os_fsync(descriptor);
    int close_status = x86os_close(descriptor);
    if (sync_status != 0 || close_status != 0) {
        (void)x86os_unlink(path); return -1;
    }
    return 0;
}

static int run_large_document_probe(notepad_state_t *state) {
    reist_gui_text_editor_event_t event;
    reist_gui_text_editor_event_initialize(&event);
    event.type = REIST_GUI_TEXT_EDITOR_EVENT_TEXT;
    event.codepoint = 'X';
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    if (state->document.size <= REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY ||
        reist_gui_text_editor_dispatch(
            &state->editor_model, &state->editor, &event, &result) != 0 ||
        !result.changed || save_document(state) != 0 ||
        state->document.size <= REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY)
        return -1;
    char first = 0;
    if (reist_gui_piece_document_read(&state->document, 0U, &first, 1U) != 0 ||
        first != 'X') return -1;
    x86os_puts("NOTEPAD_PIECE_DOCUMENT_READY\n");
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

static void close_dialog_surface(void) {
    if (!dialog_surface_active) return;
    (void)reist_gui_surface_client_destroy(&dialog_surface);
    dialog_surface_active = 0U;
}

static int create_dialog_surface(const x86os_display_info_t *display,
                                 const reist_gui_dialog_model_t *model) {
    if (main_surface == 0 || display == 0 || model == 0) return -22;
    close_dialog_surface();
    int status = reist_gui_surface_client_init_shared(
        &dialog_surface, main_surface);
    if (status == 0)
        status = reist_gui_surface_client_create_dialog(
            &dialog_surface, main_surface->surface, 560U, 220U);
    if (status == 0)
        status = reist_gui_surface_client_ack_configure(
            &dialog_surface, dialog_surface.configured_serial);
    if (status == 0)
        status = reist_gui_surface_client_set_title(
            &dialog_surface, model->title);
    if (status != 0) {
        if (dialog_surface.surface.id != 0U)
            (void)reist_gui_surface_client_destroy(&dialog_surface);
        return status;
    }
    dialog_display = *display;
    dialog_display.width = dialog_surface.width;
    dialog_display.height = dialog_surface.height;
    dialog_surface_active = 1U;
    return 0;
}

static void open_dialog(notepad_state_t *state,
                        const x86os_display_info_t *display,
                        uint32_t kind) {
    const reist_gui_dialog_model_t *model;
    state->scroll_drag = NOTEPAD_SCROLL_NONE;
    state->scroll_drag_offset = 0U;
    state->dialog_kind = kind;
    model = dialog_model(state);
    if (main_surface != 0 && create_dialog_surface(display, model) != 0)
        dialog_surface_active = 0U;
    reist_gui_surface_client_t *saved = paint_surface;
    const x86os_display_info_t *layout_display = display;
    if (dialog_surface_active) {
        paint_surface = &dialog_surface;
        layout_display = &dialog_display;
    }
    reist_gui_dialog_layout_t layout = dialog_layout(layout_display);
    reist_gui_dialog_result_t result;
    reist_gui_dialog_result_initialize(&result);
    if (model == 0 || reist_gui_dialog_open(
            model, &layout, &state->dialog, &result) != 0) {
        reist_gui_dialog_state_initialize(&state->dialog);
        state->dialog_kind = NOTEPAD_DIALOG_NONE;
        close_dialog_surface();
    }
    paint_surface = saved;
    state->redraw = 1U;
}

static void open_error(notepad_state_t *state,
                       const x86os_display_info_t *display,
                       const char *message, const char *detail) {
    initialize_error_model(state, message, detail);
    open_dialog(state, display, NOTEPAD_DIALOG_ERROR);
}

static const reist_gui_file_dialog_model_t *active_file_dialog_model(
    const notepad_state_t *state) {
    return state->file_dialog_mode == REIST_GUI_FILE_DIALOG_SAVE
        ? &save_file_model : &open_file_model;
}

static void open_file_dialog(notepad_state_t *state,
                             const x86os_display_info_t *display,
                             uint32_t mode) {
    state->scroll_drag = NOTEPAD_SCROLL_NONE;
    state->scroll_drag_offset = 0U;
    reist_gui_file_dialog_state_initialize(&state->file_dialog);
    state->file_dialog_mode = mode;
    reist_gui_file_dialog_layout_t layout = file_dialog_layout(display);
    reist_gui_file_dialog_result_t result;
    reist_gui_file_dialog_result_initialize(&result);
    const reist_gui_file_dialog_model_t *model =
        active_file_dialog_model(state);
    if (reist_gui_file_dialog_open(
            model, &layout, &state->file_dialog, state->path, &result) != 0) {
        open_error(state, display, "Dateidialog konnte nicht geoeffnet werden.",
                   state->path);
        return;
    }
    state->redraw = 1U;
}

static void complete_file_dialog(
    notepad_state_t *state, const x86os_display_info_t *display,
    const reist_gui_file_dialog_result_t *result) {
    state->redraw = 1U;
    if (result->response != REIST_GUI_FILE_DIALOG_RESPONSE_ACCEPT) return;
    if (state->file_dialog_mode == REIST_GUI_FILE_DIALOG_OPEN) {
        if (notepad_modified(state)) {
            open_error(state, display,
                       "Ungespeicherte Aenderungen verhindern das Oeffnen.",
                       "Zuerst speichern oder den Editor neu starten.");
            return;
        }
        if (!copy_text(state->path, sizeof(state->path), result->path) ||
            load_document(state) != 0 || synchronize_scrollbars(state) != 0)
            open_error(state, display, "Datei konnte nicht gelesen werden.",
                       result->path);
    } else {
        if (!copy_text(state->path, sizeof(state->path), result->path)) {
            open_error(state, display, "Dateipfad ist zu lang.", result->path);
            return;
        }
        state->io_blocked = 0U;
        state->editor_model.flags &= ~REIST_GUI_TEXT_EDITOR_READ_ONLY;
        if (save_document(state) != 0)
            open_error(state, display, "Datei konnte nicht gespeichert werden.",
                       state->path);
    }
}

static void request_exit(notepad_state_t *state,
                         const x86os_display_info_t *display) {
    if (state->file_dialog.visible) {
        reist_gui_file_dialog_state_initialize(&state->file_dialog);
        state->redraw = 1U;
        return;
    }
    if (notepad_modified(state))
        open_dialog(state, display, NOTEPAD_DIALOG_CONFIRM_EXIT);
    else state->exit_requested = 1U;
}

static void complete_dialog(notepad_state_t *state,
                            const x86os_display_info_t *display,
                            uint32_t kind, uint32_t response) {
    state->dialog_kind = NOTEPAD_DIALOG_NONE;
    close_dialog_surface();
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
                              const reist_gui_menu_result_t *result,
                              uint32_t previous_open,
                              uint32_t previous_hot) {
    if (result->damage_count || result->full_redraw) {
        if (previous_open == state->menu.open_menu &&
            previous_open != REIST_GUI_MENU_NO_INDEX &&
            previous_hot != state->menu.hot_item)
            request_hover_redraw(state);
        else
            request_overlay_redraw(state);
    }
    if (!result->activated) return;
    if (result->action == NOTEPAD_ACTION_OPEN) {
        open_file_dialog(state, display, REIST_GUI_FILE_DIALOG_OPEN);
    } else if (result->action == NOTEPAD_ACTION_SAVE) {
        if (save_document(state) != 0)
            open_error(state, display, "Datei konnte nicht gespeichert werden.",
                       state->path);
    } else if (result->action == NOTEPAD_ACTION_SAVE_AS) {
        open_file_dialog(state, display, REIST_GUI_FILE_DIALOG_SAVE);
    } else if (result->action == NOTEPAD_ACTION_EXIT)
        request_exit(state, display);
    else if (result->action == NOTEPAD_ACTION_ABOUT)
        open_dialog(state, display, NOTEPAD_DIALOG_ABOUT);
}

static uint32_t dispatch_editor_pointer(notepad_state_t *state,
                                        int32_t x, int32_t y,
                                        uint32_t button_event,
                                        uint32_t pressed) {
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
    if (result.full_redraw && synchronize_scrollbars(state) != 0) return 1U;
    if (result.damage_count || result.full_redraw) state->redraw = 1U;
    return result.consumed;
}

static uint32_t scroll_coordinate(
    const reist_gui_range_model_t *model, int32_t x, int32_t y) {
    int32_t coordinate = model->orientation == REIST_GUI_HORIZONTAL ? x : y;
    return coordinate < 0 ? 0U : (uint32_t)coordinate;
}

static uint32_t scrollbar_state_valid(const notepad_state_t *state,
                                      uint32_t axis) {
    if (state == 0 || (axis != NOTEPAD_SCROLL_VERTICAL &&
        axis != NOTEPAD_SCROLL_HORIZONTAL)) return 0U;
    const reist_gui_range_model_t *model = axis == NOTEPAD_SCROLL_VERTICAL
        ? &state->vertical_scroll_model : &state->horizontal_scroll_model;
    const reist_gui_range_state_t *range = axis == NOTEPAD_SCROLL_VERTICAL
        ? &state->vertical_scroll : &state->horizontal_scroll;
    uint32_t maximum = axis == NOTEPAD_SCROLL_VERTICAL
        ? state->viewport.maximum_first_line
        : state->viewport.maximum_first_column;
    if (!range->configured || model->bounds.width == 0U ||
        model->bounds.height == 0U || range->value < 0 ||
        (uint32_t)range->value > maximum) return 0U;
    return 1U;
}

static uint32_t apply_scroll_value(notepad_state_t *state,
                                   uint32_t axis, uint32_t value) {
    reist_gui_range_model_t *model = axis == NOTEPAD_SCROLL_VERTICAL
        ? &state->vertical_scroll_model : &state->horizontal_scroll_model;
    reist_gui_range_state_t *range = axis == NOTEPAD_SCROLL_VERTICAL
        ? &state->vertical_scroll : &state->horizontal_scroll;
    uint32_t maximum = axis == NOTEPAD_SCROLL_VERTICAL
        ? state->viewport.maximum_first_line
        : state->viewport.maximum_first_column;
    if (!scrollbar_state_valid(state, axis)) return 0U;
    if (value > maximum) value = maximum;
    reist_gui_value_result_t range_result;
    reist_gui_value_result_initialize(&range_result);
    if (reist_gui_range_set(model, range, (int32_t)value,
                            &range_result) != 0)
        return 0U;
    reist_gui_text_editor_result_t editor_result;
    reist_gui_text_editor_result_initialize(&editor_result);
    uint32_t vertical_value = state->vertical_scroll.value < 0
        ? 0U : (uint32_t)state->vertical_scroll.value;
    uint32_t horizontal_value = state->horizontal_scroll.value < 0
        ? 0U : (uint32_t)state->horizontal_scroll.value;
    if (vertical_value > state->viewport.maximum_first_line)
        vertical_value = state->viewport.maximum_first_line;
    if (horizontal_value > state->viewport.maximum_first_column)
        horizontal_value = state->viewport.maximum_first_column;
    uint32_t first_line = axis == NOTEPAD_SCROLL_VERTICAL
        ? value : vertical_value;
    uint32_t first_column = axis == NOTEPAD_SCROLL_HORIZONTAL
        ? value : horizontal_value;
    if (reist_gui_text_editor_scroll_to(
            &state->editor_model, &state->editor,
            first_line, first_column, &editor_result) != 0)
        return 0U;
    state->viewport.first_line = state->editor.first_line;
    state->viewport.first_column = state->editor.first_column;
    if (range_result.changed || editor_result.full_redraw) {
        request_dynamic_redraw(state);
    }
    if (range_result.changed) request_scrollbar_redraw(state);
    return 1U;
}

static uint32_t dispatch_one_scrollbar(notepad_state_t *state,
                                       uint32_t axis,
                                       int32_t x, int32_t y,
                                       uint32_t button_event,
                                       uint32_t pressed) {
    if (!scrollbar_state_valid(state, axis)) {
        state->scroll_drag = NOTEPAD_SCROLL_NONE;
        state->scroll_drag_offset = 0U;
        return 1U;
    }
    reist_gui_range_model_t *model = axis == NOTEPAD_SCROLL_VERTICAL
        ? &state->vertical_scroll_model : &state->horizontal_scroll_model;
    reist_gui_range_state_t *range = axis == NOTEPAD_SCROLL_VERTICAL
        ? &state->vertical_scroll : &state->horizontal_scroll;
    uint32_t maximum = axis == NOTEPAD_SCROLL_VERTICAL
        ? state->viewport.maximum_first_line
        : state->viewport.maximum_first_column;
    notepad_scrollbar_geometry_t geometry =
        scrollbar_geometry(model, range, maximum);
    if (!button_event && state->scroll_drag == axis) {
        uint32_t coordinate = scroll_coordinate(model, x, y);
        uint32_t track_origin = model->orientation == REIST_GUI_HORIZONTAL
            ? (uint32_t)geometry.track.x : (uint32_t)geometry.track.y;
        uint32_t position = coordinate > track_origin +
                state->scroll_drag_offset
            ? coordinate - track_origin - state->scroll_drag_offset : 0U;
        if (position > geometry.travel) position = geometry.travel;
        uint32_t scaled = position != 0U && maximum > UINT32_MAX / position
            ? UINT32_MAX : maximum * position;
        uint32_t rounding = geometry.travel / 2U;
        scaled = scaled > UINT32_MAX - rounding
            ? UINT32_MAX : scaled + rounding;
        uint32_t value = maximum && geometry.travel
            ? scaled / geometry.travel : 0U;
        reist_gui_value_result_t preview_result;
        reist_gui_value_result_initialize(&preview_result);
        if (reist_gui_range_set(model, range, (int32_t)value,
                                &preview_result) == 0) {
            state->scroll_pending_value = value;
            state->scroll_pending_valid = 1U;
            if (preview_result.changed) request_scrollbar_redraw(state);
        }
        return 1U;
    }
    if (!button_event)
        return point_inside(model->bounds, x, y);
    if (!pressed) {
        if (state->scroll_drag == axis) {
            if (state->scroll_pending_valid)
                (void)apply_scroll_value(
                    state, axis, state->scroll_pending_value);
            state->scroll_drag = NOTEPAD_SCROLL_NONE;
            state->scroll_drag_offset = 0U;
            state->scroll_pending_valid = 0U;
            request_dynamic_redraw(state);
            request_scrollbar_redraw(state);
            return 1U;
        }
        return point_inside(model->bounds, x, y);
    }
    if (!point_inside(model->bounds, x, y)) return 0U;
    if (maximum == 0U) return 1U;
    uint32_t value = range->value < 0 ? 0U : (uint32_t)range->value;
    if (value > maximum) value = maximum;
    if (point_inside(geometry.decrement, x, y)) {
        if (value != 0U) --value;
    } else if (point_inside(geometry.increment, x, y)) {
        if (value < maximum) ++value;
    } else if (point_inside(geometry.thumb, x, y)) {
        uint32_t coordinate = scroll_coordinate(model, x, y);
        uint32_t thumb_origin = model->orientation == REIST_GUI_HORIZONTAL
            ? (uint32_t)geometry.thumb.x : (uint32_t)geometry.thumb.y;
        state->scroll_drag = axis;
        state->scroll_drag_offset = coordinate - thumb_origin;
        state->scroll_pending_value = value;
        state->scroll_pending_valid = 1U;
        request_scrollbar_redraw(state);
        return 1U;
    } else {
        uint32_t coordinate = scroll_coordinate(model, x, y);
        uint32_t thumb_origin = model->orientation == REIST_GUI_HORIZONTAL
            ? (uint32_t)geometry.thumb.x : (uint32_t)geometry.thumb.y;
        uint32_t page = model->page_step;
        if (coordinate < thumb_origin)
            value = value > page ? value - page : 0U;
        else
            value = maximum - value > page ? value + page : maximum;
    }
    (void)apply_scroll_value(state, axis, value);
    return 1U;
}

static uint32_t dispatch_scrollbars(notepad_state_t *state,
                                    int32_t x, int32_t y,
                                    uint32_t button_event,
                                    uint32_t pressed) {
    if (state->scroll_drag == NOTEPAD_SCROLL_VERTICAL)
        return dispatch_one_scrollbar(
            state, NOTEPAD_SCROLL_VERTICAL, x, y,
            button_event, pressed);
    if (state->scroll_drag == NOTEPAD_SCROLL_HORIZONTAL)
        return dispatch_one_scrollbar(
            state, NOTEPAD_SCROLL_HORIZONTAL, x, y,
            button_event, pressed);
    if (point_inside(state->vertical_scroll_model.bounds, x, y))
        return dispatch_one_scrollbar(
            state, NOTEPAD_SCROLL_VERTICAL, x, y,
            button_event, pressed);
    if (point_inside(state->horizontal_scroll_model.bounds, x, y))
        return dispatch_one_scrollbar(
            state, NOTEPAD_SCROLL_HORIZONTAL, x, y,
            button_event, pressed);
    return 0U;
}

static uint32_t dispatch_pointer(notepad_state_t *state,
                                 const x86os_display_info_t *display,
                                 int32_t x, int32_t y,
                                 uint32_t button_event, uint32_t pressed) {
    if (state->file_dialog.visible) {
        state->scroll_drag = NOTEPAD_SCROLL_NONE;
        reist_gui_file_dialog_event_t event;
        reist_gui_file_dialog_event_initialize(&event);
        event.type = button_event
            ? REIST_GUI_FILE_DIALOG_EVENT_POINTER_BUTTON
            : REIST_GUI_FILE_DIALOG_EVENT_POINTER_MOTION;
        event.x = x;
        event.y = y;
        event.button = button_event ? 1U : 0U;
        event.pressed = pressed;
        reist_gui_file_dialog_result_t result;
        reist_gui_file_dialog_result_initialize(&result);
        reist_gui_file_dialog_layout_t layout = file_dialog_layout(display);
        if (reist_gui_file_dialog_dispatch(
                active_file_dialog_model(state), &layout,
                &state->file_dialog, &event, &result) == 0) {
            if (result.full_redraw) state->redraw = 1U;
            if (result.completed)
                complete_file_dialog(state, display, &result);
        }
        return 1U;
    }
    if (state->dialog.visible) {
        state->scroll_drag = NOTEPAD_SCROLL_NONE;
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
    uint32_t previous_open = state->menu.open_menu;
    uint32_t previous_hot = state->menu.hot_item;
    if (reist_gui_menu_dispatch(
            &menu_model, &layout, &state->menu,
            &menu_event, &menu_result) != 0) return 1U;
    apply_menu_result(state, display, &menu_result,
                      previous_open, previous_hot);
    if (menu_result.consumed) {
        state->scroll_drag = NOTEPAD_SCROLL_NONE;
        return 1U;
    }
    if (state->editor.captured)
        return dispatch_editor_pointer(
            state, x, y, button_event, pressed);
    if (dispatch_scrollbars(state, x, y, button_event, pressed)) return 1U;
    return dispatch_editor_pointer(state, x, y, button_event, pressed);
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

static uint32_t file_dialog_key(int key) {
    if (key == '\t') return REIST_GUI_FILE_DIALOG_KEY_TAB;
    if (key == 8 || key == 127) return REIST_GUI_FILE_DIALOG_KEY_BACKSPACE;
    if (key == NOTEPAD_KEY_DELETE) return REIST_GUI_FILE_DIALOG_KEY_DELETE;
    if (key == NOTEPAD_KEY_LEFT) return REIST_GUI_FILE_DIALOG_KEY_LEFT;
    if (key == NOTEPAD_KEY_RIGHT) return REIST_GUI_FILE_DIALOG_KEY_RIGHT;
    if (key == NOTEPAD_KEY_HOME) return REIST_GUI_FILE_DIALOG_KEY_HOME;
    if (key == NOTEPAD_KEY_END) return REIST_GUI_FILE_DIALOG_KEY_END;
    if (key == '\r' || key == '\n') return REIST_GUI_FILE_DIALOG_KEY_ENTER;
    if (key == NOTEPAD_KEY_ESCAPE) return REIST_GUI_FILE_DIALOG_KEY_ESCAPE;
    return 0U;
}

static uint32_t dispatch_keyboard(notepad_state_t *state,
                                  const x86os_display_info_t *display,
                                  int key) {
    if (state->file_dialog.visible) {
        reist_gui_file_dialog_event_t event;
        reist_gui_file_dialog_event_initialize(&event);
        uint32_t mapped = file_dialog_key(key);
        if (mapped) {
            event.type = REIST_GUI_FILE_DIALOG_EVENT_KEYBOARD;
            event.key = mapped;
        } else if (key >= 0x20 && key <= 0x7e) {
            event.type = REIST_GUI_FILE_DIALOG_EVENT_TEXT;
            event.codepoint = (uint32_t)key;
        } else return 1U;
        reist_gui_file_dialog_result_t result;
        reist_gui_file_dialog_result_initialize(&result);
        reist_gui_file_dialog_layout_t layout = file_dialog_layout(display);
        if (reist_gui_file_dialog_dispatch(
                active_file_dialog_model(state), &layout,
                &state->file_dialog, &event, &result) == 0) {
            if (result.full_redraw) state->redraw = 1U;
            if (result.completed)
                complete_file_dialog(state, display, &result);
        }
        return 1U;
    }
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
        uint32_t previous_open = state->menu.open_menu;
        uint32_t previous_hot = state->menu.hot_item;
        if (reist_gui_menu_dispatch(
                &menu_model, &layout, &state->menu,
                &event, &result) != 0) return 1U;
        apply_menu_result(state, display, &result,
                          previous_open, previous_hot);
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
    if (key == NOTEPAD_KEY_PAGE_DOWN &&
        state->window_offset + state->window_length < state->document.size) {
        if (sync_piece_window(state) != 0) {
            open_error(state, display, "Aenderungskapazitaet ist erschoepft.",
                       state->path);
            return 1U;
        }
        uint32_t next = state->window_offset + state->window_length;
        state->previous_window_offset = state->window_offset;
        if (materialize_piece_window(state, next) != 0 ||
            synchronize_scrollbars(state) != 0) {
            open_error(state, display, "Dokumentfenster konnte nicht geladen werden.",
                       state->path);
            return 1U;
        }
        (void)copy_text(state->status, sizeof(state->status),
                        "Naechstes Piece-Table-Fenster");
        state->redraw = 1U;
        return 1U;
    }
    if (key == NOTEPAD_KEY_PAGE_UP && state->window_offset != 0U) {
        if (sync_piece_window(state) != 0 ||
            materialize_piece_window(state, state->previous_window_offset) != 0 ||
            synchronize_scrollbars(state) != 0) {
            open_error(state, display, "Dokumentfenster konnte nicht geladen werden.",
                       state->path);
            return 1U;
        }
        state->previous_window_offset = 0U;
        (void)copy_text(state->status, sizeof(state->status),
                        "Vorheriges Piece-Table-Fenster");
        state->redraw = 1U;
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
    if (synchronize_scrollbars(state) != 0) return 1U;
    if (result.damage_count || result.full_redraw) state->redraw = 1U;
    if (result.changed)
        (void)copy_text(state->status, sizeof(state->status), "Geaendert");
    return result.consumed;
}

static int run_menu_probe(notepad_state_t *state,
                          const x86os_display_info_t *display) {
    reist_gui_menu_layout_t layout = menu_layout(display);
    reist_gui_rect_t title;
    reist_gui_rect_t item;
    if (reist_gui_menu_title_rect(
            &menu_model, &layout, 1U, &title) != 0 ||
        reist_gui_menu_item_rect(
            &menu_model, &layout, 1U, 0U, &item) != 0)
        return -1;
    (void)dispatch_pointer(state, display, title.x + 2, title.y + 2, 1U, 1U);
    (void)dispatch_pointer(state, display, title.x + 2, title.y + 2, 1U, 0U);
    (void)dispatch_pointer(state, display, item.x + 2, item.y + 2, 1U, 1U);
    (void)dispatch_pointer(state, display, item.x + 2, item.y + 2, 1U, 0U);
    return state->dialog.visible &&
        state->dialog_kind == NOTEPAD_DIALOG_ABOUT ? 0 : -1;
}

static int run_file_dialog_probe(notepad_state_t *state,
                                 const x86os_display_info_t *display) {
    reist_gui_menu_layout_t layout = menu_layout(display);
    reist_gui_rect_t title;
    reist_gui_rect_t item;
    if (reist_gui_menu_title_rect(
            &menu_model, &layout, 0U, &title) != 0 ||
        reist_gui_menu_item_rect(
            &menu_model, &layout, 0U, 2U, &item) != 0)
        return -1;
    (void)dispatch_pointer(state, display, title.x + 2, title.y + 2, 1U, 1U);
    (void)dispatch_pointer(state, display, title.x + 2, title.y + 2, 1U, 0U);
    (void)dispatch_pointer(state, display, item.x + 2, item.y + 2, 1U, 1U);
    (void)dispatch_pointer(state, display, item.x + 2, item.y + 2, 1U, 0U);
    return state->file_dialog.visible &&
        state->file_dialog_mode == REIST_GUI_FILE_DIALOG_SAVE ? 0 : -1;
}

static int run_hover_probe(notepad_state_t *state,
                           const x86os_display_info_t *display) {
    reist_gui_menu_layout_t layout = menu_layout(display);
    reist_gui_rect_t title;
    if (reist_gui_menu_title_rect(
            &menu_model, &layout, 0U, &title) != 0) return -1;
    (void)dispatch_pointer(state, display, title.x + 2, title.y + 2, 1U, 1U);
    (void)dispatch_pointer(state, display, title.x + 2, title.y + 2, 1U, 0U);
    for (uint32_t index = 0U; index < menus[0].item_count; ++index) {
        reist_gui_rect_t item;
        if (reist_gui_menu_item_rect(
                &menu_model, &layout, 0U, index, &item) != 0) return -1;
        (void)dispatch_pointer(
            state, display, item.x + 2, item.y + 2, 0U, 0U);
        if (!state->redraw && !state->overlay_redraw &&
            !state->hover_redraw) return -1;
        if (state->redraw)
            render(display, state);
        else if (state->overlay_redraw)
            render_overlay(display, state);
        else
            render_hover(display, state);
        if (paint_status != 0) return paint_status;
        state->redraw = 0U;
        state->overlay_redraw = 0U;
        state->hover_redraw = 0U;
    }
    return 0;
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

static void update_editor_model(notepad_state_t *state,
                                const x86os_display_info_t *display) {
    state->scroll_drag = NOTEPAD_SCROLL_NONE;
    state->scroll_drag_offset = 0U;
    state->scroll_pending_valid = 0U;
    reist_gui_rect_t frame = editor_frame(display);
    uint32_t status = display->font_height + 12U;
    int32_t top = frame.y + 7;
    int32_t bottom = frame.y + (int32_t)frame.height -
        (int32_t)status - 6;
    uint32_t available_width = frame.width > 16U
        ? frame.width - 16U : 1U;
    uint32_t available_height = bottom > top
        ? (uint32_t)(bottom - top) : 1U;
    uint32_t scrollbar_space = NOTEPAD_SCROLLBAR_EXTENT + 2U;
    state->editor_model = (reist_gui_text_editor_model_t){
        REIST_GUI_TEXT_EDITOR_API_VERSION,
        sizeof(reist_gui_text_editor_model_t), 1U, "Dokumenttext",
        {frame.x + 8, top,
         available_width > scrollbar_space
             ? available_width - scrollbar_space : 1U,
         available_height > scrollbar_space
             ? available_height - scrollbar_space : 1U},
        display->font_width, display->font_height,
        REIST_GUI_TEXT_EDITOR_VISIBLE | REIST_GUI_TEXT_EDITOR_ENABLED,
        {0U, 0U, 0U, 0U}};
    if (state->io_blocked)
        state->editor_model.flags |= REIST_GUI_TEXT_EDITOR_READ_ONLY;
}

static int resize_editor_model(notepad_state_t *state,
                               const x86os_display_info_t *display) {
    uint32_t old_bottom = state->viewport.maximum_first_line != 0U &&
        state->viewport.first_line == state->viewport.maximum_first_line;
    uint32_t old_right = state->viewport.maximum_first_column != 0U &&
        state->viewport.first_column == state->viewport.maximum_first_column;
    uint32_t first_line = state->viewport.first_line;
    uint32_t first_column = state->viewport.first_column;
    update_editor_model(state, display);
    reist_gui_text_editor_viewport_t next;
    if (reist_gui_text_editor_get_viewport(
            &state->editor_model, &state->editor, &next) != 0) return -1;
    first_line = old_bottom ? next.maximum_first_line : first_line;
    first_column = old_right ? next.maximum_first_column : first_column;
    if (first_line > next.maximum_first_line)
        first_line = next.maximum_first_line;
    if (first_column > next.maximum_first_column)
        first_column = next.maximum_first_column;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_result_initialize(&result);
    if (reist_gui_text_editor_scroll_to(
            &state->editor_model, &state->editor,
            first_line, first_column, &result) != 0) return -1;
    return synchronize_scrollbars(state);
}

static int initialize(notepad_state_t *state,
                      const x86os_display_info_t *display,
                      const char *path) {
    reist_gui_menu_state_initialize(&state->menu);
    reist_gui_dialog_state_initialize(&state->dialog);
    reist_gui_file_dialog_state_initialize(&state->file_dialog);
    reist_gui_text_editor_state_initialize(&state->editor);
    reist_gui_range_state_initialize(&state->vertical_scroll);
    reist_gui_range_state_initialize(&state->horizontal_scroll);
    if (!copy_text(state->path, sizeof(state->path), path)) return -4;
    update_editor_model(state, display);
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
    state->file_dialog_mode = REIST_GUI_FILE_DIALOG_OPEN;
    state->redraw = 1U;
    int load_status = load_document(state);
    if (synchronize_scrollbars(state) != 0) return -4;
    return load_status;
}

static uint32_t is_surface_argument(const char *argument) {
    static const char prefix[] = "--reist-surface=";
    if (argument == 0) return 0U;
    uint32_t index = 0U;
    while (prefix[index] != '\0' && argument[index] == prefix[index])
        ++index;
    return prefix[index] == '\0';
}

int main(int argc, char **argv) {
    if (argc == 2 && argv && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: notepad [file]\n");
        return 0;
    }
    if (argc < 1 || argc > 3 || argv == 0) {
        x86os_puts("Usage: notepad [file]\n");
        return 2;
    }
    x86os_ipc_handle_t surface_endpoint = 0U;
    int endpoint_status = reist_gui_surface_endpoint_from_argv(
        argc, argv, &surface_endpoint);
    uint32_t surface_mode = endpoint_status == 0;
    if (endpoint_status != 0 && endpoint_status != -2) {
        x86os_puts("notepad: ungueltiger Surface-Endpunkt\n");
        return 2;
    }
    const char *path = "/untitled.txt";
    uint32_t document_seen = 0U;
    uint32_t menu_probe = 0U;
    uint32_t file_dialog_probe = 0U;
    uint32_t hover_probe = 0U;
    uint32_t dialog_probe = 0U;
    uint32_t large_document_probe = 0U;
    for (int argument = 1; argument < argc; ++argument) {
        if (is_surface_argument(argv[argument])) continue;
        if (text_equal(argv[argument], "--menu-probe")) {
            menu_probe = 1U;
            continue;
        }
        if (text_equal(argv[argument], "--file-dialog-probe")) {
            file_dialog_probe = 1U;
            continue;
        }
        if (text_equal(argv[argument], "--hover-probe")) {
            hover_probe = 1U;
            continue;
        }
        if (text_equal(argv[argument], "--dialog-probe")) {
            dialog_probe = 1U;
            continue;
        }
        if (text_equal(argv[argument], "--large-document-probe")) {
            large_document_probe = 1U;
            path = "/notepad-big.txt";
            document_seen = 1U;
            continue;
        }
        if (document_seen) {
            x86os_puts("Usage: notepad [file]\n");
            return 2;
        }
        path = argv[argument];
        document_seen = 1U;
    }

    if (large_document_probe) (void)x86os_unlink(path);
    x86os_display_info_t display;
    uint32_t runtime_activated = 0U;
    if (x86os_display_info(&display) != 0) {
        if (!surface_mode && x86os_display_activate() == 0)
            runtime_activated = 1U;
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

    reist_gui_surface_client_t surface_client;
    if (surface_mode) {
        if (reist_gui_surface_client_init(
                &surface_client, surface_endpoint) != 0) return 1;
        uint32_t width = display.width > 160U ? display.width - 160U : 480U;
        uint32_t height = display.height > 180U ? display.height - 180U : 320U;
        if (width > 760U) width = 760U;
        if (height > 540U) height = 540U;
        int create_status = -9;
        for (uint32_t attempt = 0U; attempt < 250U; ++attempt) {
            create_status = reist_gui_surface_client_create(
                &surface_client, REIST_GUI_SURFACE_ROLE_TOPLEVEL,
                width, height);
            if (create_status == 0) break;
            if (create_status != -9 && create_status != -13) break;
            (void)x86os_sleep_ms(1U);
        }
        if (create_status != 0 ||
            reist_gui_surface_client_ack_configure(
                &surface_client, surface_client.configured_serial) != 0 ||
            reist_gui_surface_client_set_title(
                &surface_client, "REIST Editor") != 0) {
            x86os_puts("notepad: Surface konnte nicht erstellt werden\n");
            return 1;
        }
        display.width = surface_client.width;
        display.height = surface_client.height;
        paint_surface = &surface_client;
        main_surface = &surface_client;
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
                   "Datei enthaelt ungueltiges UTF-8 oder Steuerzeichen.",
                   path);
    reist_gui_menu_layout_t menu_metrics = menu_layout(&display);
    reist_gui_file_dialog_layout_t file_metrics = file_dialog_layout(&display);
    if (reist_gui_menu_validate(
            &menu_model, &menu_metrics, &application.menu) != 0 ||
        reist_gui_file_dialog_validate(
            &open_file_model, &file_metrics, &application.file_dialog) != 0 ||
        reist_gui_file_dialog_validate(
            &save_file_model, &file_metrics, &application.file_dialog) != 0 ||
        reist_gui_text_editor_validate(
            &application.editor_model, &application.editor) != 0 ||
        reist_gui_range_validate(
            &application.vertical_scroll_model,
            &application.vertical_scroll) != 0 ||
        reist_gui_range_validate(
            &application.horizontal_scroll_model,
            &application.horizontal_scroll) != 0) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("notepad: GUI-API/Layout nicht kompatibel\n");
        return 1;
    }
    if (menu_probe && run_menu_probe(&application, &display) != 0) {
        x86os_puts("notepad: Menue-Probe fehlgeschlagen\n");
        return 1;
    }
    if (file_dialog_probe &&
        run_file_dialog_probe(&application, &display) != 0) {
        x86os_puts("notepad: Dateidialog-Probe fehlgeschlagen\n");
        return 1;
    }
    if (dialog_probe)
        open_dialog(&application, &display, NOTEPAD_DIALOG_ABOUT);

    int32_t pointer_x = (int32_t)(display.width / 2U);
    int32_t pointer_y = (int32_t)(display.height / 2U);
    uint32_t previous_buttons = 0U;
    render(&display, &application);
    render_separate_dialog(&application);
    if (surface_mode && paint_status != 0) {
        (void)reist_gui_surface_client_destroy(&surface_client);
        (void)x86os_ipc_release(surface_endpoint);
        report_paint_failure(
            "notepad: Surface-Frame konnte nicht publiziert werden: ",
            paint_status);
        return 1;
    }
    if (hover_probe) {
        int hover_status = run_hover_probe(&application, &display);
        if (hover_status != 0) {
            report_paint_failure(
                "notepad: Hover-Paint fehlgeschlagen: ", hover_status);
            return 1;
        }
    }
    if (surface_mode) {
        x86os_puts("NOTEPAD_SURFACE_READY\n");
        if (document_seen && load_status == 0 && !large_document_probe)
            x86os_puts("NOTEPAD_SURFACE_DOCUMENT_READY\n");
        if (menu_probe) x86os_puts("NOTEPAD_SURFACE_MENU_READY\n");
        if (file_dialog_probe)
            x86os_puts("NOTEPAD_SURFACE_FILE_DIALOG_READY\n");
        if (hover_probe) x86os_puts("NOTEPAD_SURFACE_HOVER_READY\n");
        if (dialog_probe && dialog_surface_active)
            x86os_puts("NOTEPAD_SURFACE_DIALOG_READY\n");
    }
    if (large_document_probe) {
        if (create_large_probe_document(path) != 0) {
            x86os_puts("NOTEPAD_PIECE_DOCUMENT_FAIL create\n");
            application.exit_requested = 1U;
        } else if (load_document(&application) != 0 ||
                   run_large_document_probe(&application) != 0) {
            x86os_puts("NOTEPAD_PIECE_DOCUMENT_FAIL edit-save-reopen\n");
            application.exit_requested = 1U;
        } else {
            render(&display, &application);
            if (surface_mode && paint_status != 0) {
                x86os_puts("NOTEPAD_PIECE_DOCUMENT_FAIL repaint\n");
                application.exit_requested = 1U;
            } else if (surface_mode) {
                x86os_puts("NOTEPAD_SURFACE_DOCUMENT_READY\n");
            }
        }
    }
    application.redraw = 0U;
    application.dynamic_redraw = 0U;
    application.overlay_redraw = 0U;
    application.hover_redraw = 0U;
    application.scrollbar_redraw = 0U;
    if (!surface_mode) (void)x86os_pointer_update(pointer_x, pointer_y, 1U);

    uint32_t paint_failures = 0U;
    uint32_t resize_marker_pending = 0U;
    while (!application.exit_requested) {
        int key = NOTEPAD_KEY_NONE;
        uint32_t mouse_count = 0U;
        uint32_t surface_events = 0U;
        if (surface_mode) {
            for (; surface_events < NOTEPAD_MOUSE_BATCH_LIMIT;
                 ++surface_events) {
                reist_gui_surface_message_t message;
                int receive_status = reist_gui_surface_client_receive(
                    &surface_client, &message, 0U);
                if (receive_status == -11) break;
                if (receive_status != 0) {
                    application.exit_requested = 1U;
                    break;
                }
                if (message.type == REIST_GUI_SURFACE_CONFIGURE) {
                    uint32_t is_dialog = dialog_surface_active &&
                        same_surface(message.surface,
                                     dialog_surface.surface);
                    reist_gui_surface_client_t *configured = is_dialog
                        ? &dialog_surface : &surface_client;
                    int configure_status = accept_configure_bounded(
                        configured, &message);
                    if (configure_status != 0) {
                        report_paint_failure(
                            "notepad: Resize verzoegert: ",
                            configure_status);
                        continue;
                    }
                    if (is_dialog) {
                        dialog_display.width = dialog_surface.width;
                        dialog_display.height = dialog_surface.height;
                    } else {
                        display.width = surface_client.width;
                        display.height = surface_client.height;
                        if (resize_editor_model(&application, &display) != 0) {
                            application.exit_requested = 1U;
                            break;
                        }
                        resize_marker_pending = 1U;
                    }
                    if (application.dialog.visible &&
                        (!dialog_surface_active || is_dialog)) {
                        uint32_t dialog_kind = application.dialog_kind;
                        reist_gui_dialog_state_initialize(
                            &application.dialog);
                        if (is_dialog) {
                            reist_gui_surface_client_t *saved = paint_surface;
                            paint_surface = &dialog_surface;
                            reist_gui_dialog_layout_t layout =
                                dialog_layout(&dialog_display);
                            reist_gui_dialog_result_t result;
                            reist_gui_dialog_result_initialize(&result);
                            application.dialog_kind = dialog_kind;
                            (void)reist_gui_dialog_open(
                                dialog_model(&application), &layout,
                                &application.dialog, &result);
                            paint_surface = saved;
                        } else {
                            open_dialog(
                                &application, &display, dialog_kind);
                        }
                    }
                    application.redraw = 1U;
                } else if (message.type == REIST_GUI_SURFACE_CLOSE) {
                    if (dialog_surface_active &&
                        same_surface(message.surface,
                                     dialog_surface.surface)) {
                        uint32_t kind = application.dialog_kind;
                        uint32_t response = dialog_model(&application)
                            ? dialog_model(&application)->cancel_response : 0U;
                        reist_gui_dialog_state_initialize(&application.dialog);
                        complete_dialog(
                            &application, &display, kind, response);
                    } else request_exit(&application, &display);
                } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                           (!dialog_surface_active ||
                            same_surface(message.surface,
                                         dialog_surface.surface)) &&
                           message.input.type ==
                               REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
                    pointer_x = message.input.x;
                    pointer_y = message.input.y;
                    (void)dispatch_pointer(
                        &application, dialog_surface_active
                            ? &dialog_display : &display,
                        pointer_x, pointer_y, 0U, 0U);
                    if (application.scrollbar_redraw &&
                        application.scroll_drag != NOTEPAD_SCROLL_NONE) {
                        ++surface_events;
                        break;
                    }
                } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                           (!dialog_surface_active ||
                            same_surface(message.surface,
                                         dialog_surface.surface)) &&
                           message.input.type ==
                               REIST_GUI_SURFACE_INPUT_POINTER_BUTTON) {
                    pointer_x = message.input.x;
                    pointer_y = message.input.y;
                    (void)dispatch_pointer(
                        &application, dialog_surface_active
                            ? &dialog_display : &display, pointer_x, pointer_y,
                        1U, message.input.pressed);
                    if (application.scrollbar_redraw &&
                        application.scroll_drag != NOTEPAD_SCROLL_NONE) {
                        ++surface_events;
                        break;
                    }
                } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                           (!dialog_surface_active ||
                            same_surface(message.surface,
                                         dialog_surface.surface)) &&
                           message.input.type ==
                               REIST_GUI_SURFACE_INPUT_KEYBOARD &&
                           message.input.pressed) {
                    (void)dispatch_keyboard(
                        &application, dialog_surface_active
                            ? &dialog_display : &display,
                        (int)message.input.key);
                }
            }
        } else {
            key = read_key();
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
                        &application, &display,
                        pointer_x, pointer_y, 1U, 1U);
                else if (!left && previous)
                    (void)dispatch_pointer(
                        &application, &display,
                        pointer_x, pointer_y, 1U, 0U);
                previous_buttons = mouse.buttons;
                if (application.scrollbar_redraw &&
                    application.scroll_drag != NOTEPAD_SCROLL_NONE) {
                    ++mouse_count;
                    break;
                }
            }
            if (key && key != NOTEPAD_KEY_NONE)
                (void)dispatch_keyboard(&application, &display, key);
        }
        if (application.redraw || application.dynamic_redraw ||
            application.overlay_redraw || application.hover_redraw ||
            application.scrollbar_redraw) {
            uint32_t full_redraw = application.redraw;
            uint32_t urgent_scrollbar = !full_redraw &&
                application.scrollbar_redraw &&
                application.scroll_drag != NOTEPAD_SCROLL_NONE;
            if (!surface_mode)
                (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            if (full_redraw)
                render(&display, &application);
            else if (urgent_scrollbar)
                render_hover(&display, &application);
            else {
                if (application.dynamic_redraw)
                    render_dynamic(&display, &application);
                if (paint_status == 0 && application.overlay_redraw)
                    render_overlay(&display, &application);
                if (paint_status == 0 && application.hover_redraw)
                    render_hover(&display, &application);
            }
            if (paint_status == 0 && full_redraw)
                render_separate_dialog(&application);
            if (!surface_mode)
                (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
            if (paint_status == 0) {
                if (full_redraw) {
                    application.redraw = 0U;
                    application.dynamic_redraw = 0U;
                    application.overlay_redraw = 0U;
                    application.hover_redraw = 0U;
                    application.scrollbar_redraw = 0U;
                } else if (urgent_scrollbar) {
                    application.hover_redraw = 0U;
                    application.scrollbar_redraw = 0U;
                } else {
                    if (application.dynamic_redraw)
                        application.dynamic_redraw = 0U;
                    if (application.overlay_redraw)
                        application.overlay_redraw = 0U;
                    if (application.hover_redraw) {
                        application.hover_redraw = 0U;
                        application.scrollbar_redraw = 0U;
                    }
                }
                paint_failures = 0U;
                if (full_redraw && resize_marker_pending) {
                    x86os_puts("NOTEPAD_SURFACE_RESIZE_OK\n");
                    resize_marker_pending = 0U;
                }
            } else if (surface_mode && paint_status_retryable(paint_status)) {
                if (paint_failures < NOTEPAD_PAINT_RETRY_LIMIT)
                    ++paint_failures;
                if (full_redraw)
                    application.redraw = 1U;
                else if (urgent_scrollbar) {
                    application.scrollbar_redraw = 1U;
                    application.hover_redraw = 1U;
                }
                else if (application.dynamic_redraw)
                    application.dynamic_redraw = 1U;
                else if (application.overlay_redraw)
                    application.overlay_redraw = 1U;
                else
                    application.hover_redraw = 1U;
                if (paint_failures == 1U)
                    report_paint_failure(
                        full_redraw
                            ? "notepad: Surface-Frame verzoegert: "
                            : "notepad: Surface-Overlay verzoegert: ",
                        paint_status);
                (void)x86os_sleep_ms(5U);
            } else {
                report_paint_failure(
                    "notepad: Surface-Frame dauerhaft fehlgeschlagen: ",
                    paint_status);
                application.exit_requested = 1U;
            }
        } else if (!surface_mode && mouse_count) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if ((!surface_mode && mouse_count == 0U) ||
                   (surface_mode && surface_events == 0U)) {
            (void)x86os_sleep_ms(5U);
        }
    }

    if (surface_mode) {
        close_dialog_surface();
        (void)reist_gui_surface_client_destroy(&surface_client);
        (void)x86os_ipc_release(surface_endpoint);
    } else (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    if (application.source_object != REIST_VFS_FILE_INVALID_HANDLE) {
        (void)reist_vfs_file_set_timeout(application.source_object, 1U);
        (void)reist_vfs_file_close(application.source_object);
        application.source_object = REIST_VFS_FILE_INVALID_HANDLE;
    }
    if (runtime_activated) {
        if (x86os_display_deactivate() != 0) {
            x86os_puts("notepad: VGA-Rueckkehr fehlgeschlagen\n");
            return 1;
        }
    } else if (!surface_mode) x86os_clear();
    return 0;
}
