/**
 * @file userspace/gui/compositor/desktop.c
 * @brief Classic Ring-3 desktop and fixed-capacity window-manager frontend.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Display/input results and all geometry are validated or bounded.
 * Safety: Window state is fixed-capacity; child cleanup and event work are
 * bounded. Legacy console applications intentionally run full-screen.
 */
#include "x86os.h"
#include "desktop_explorer.h"
#include "desktop_filetypes.h"
#include "desktop_wm.h"
#include "desktop_surface.h"
#include "desktop_surface_runtime.h"
#include "reist/image.h"
#include "reist/gui/dialog.h"
#include "reist/gui/menu.h"

#define DESKTOP_ICON_COUNT 2U
#define DESKTOP_ICON_WIDTH 176U
#define DESKTOP_ARGUMENT_LIMIT 32U
#define DESKTOP_MENU_COUNT 3U
#define DESKTOP_METRICS_VERSION 1U
#define DESKTOP_RENDER_PROBE_STEPS 8U
#define DESKTOP_RENDER_PROBE_STEP_X 4
#define DESKTOP_MOUSE_BATCH_LIMIT 32U
#define DESKTOP_FILE_ICON_SIZE 32U
#define DESKTOP_FILE_ICON_PIXELS \
    (DESKTOP_FILE_ICON_SIZE * DESKTOP_FILE_ICON_SIZE)
#define DESKTOP_FILE_ICON_ENCODED_CAPACITY 8192U

_Static_assert(DESKTOP_EXPLORER_WINDOW_CAPACITY == DESKTOP_WM_CAPACITY,
               "explorer and window-manager capacities must match");

enum {
    DESKTOP_KEY_NONE = 0x100,
    DESKTOP_KEY_ESCAPE,
    DESKTOP_KEY_UP,
    DESKTOP_KEY_DOWN,
    DESKTOP_KEY_LEFT,
    DESKTOP_KEY_RIGHT
};

typedef struct {
    const char *title;
    const char *path;
    uint32_t accent;
} desktop_icon_t;

static const desktop_icon_t desktop_icons[DESKTOP_ICON_COUNT] = {
    {"Computer", "/", 0x0000479DU},
    {"Systemsteuerung", "/usr/gui/bin/control.prg", 0x00806020U},
};

static uint32_t control_panel_selected;
static uint32_t control_panel_pressed;
static uint64_t control_panel_last_click_ms;

enum {
    DESKTOP_MENU_WORKSPACE = 0U,
    DESKTOP_MENU_WINDOWS,
    DESKTOP_MENU_HELP
};

enum {
    DESKTOP_MENU_ACTION_NONE = 0U,
    DESKTOP_MENU_ACTION_ABOUT,
    DESKTOP_MENU_ACTION_EXIT,
    DESKTOP_MENU_ACTION_OPEN_ROOT,
    DESKTOP_MENU_ACTION_CLOSE_ALL,
    DESKTOP_MENU_ACTION_HELP
};

enum {
    DESKTOP_DIALOG_NONE = 0U,
    DESKTOP_DIALOG_HELP,
    DESKTOP_DIALOG_ABOUT,
    DESKTOP_DIALOG_ERROR
};

enum {
    DESKTOP_UI_ACTION_NONE = 0U,
    DESKTOP_UI_ACTION_EXIT,
    DESKTOP_UI_ACTION_OPEN_ROOT,
    DESKTOP_UI_ACTION_CLOSE_ALL
};

enum {
    DESKTOP_MOVE_CACHE_NONE = 0U,
    DESKTOP_MOVE_CACHE_WINDOW,
    DESKTOP_MOVE_CACHE_DIALOG
};

/* Application policy stays outside libreistgui: the library returns these
 * opaque IDs while this compositor translates them into typed WM actions. */
static const reist_gui_menu_item_t workspace_menu_items[] = {
    {"Computer oeffnen", DESKTOP_MENU_ACTION_OPEN_ROOT, 0U, 0U, 0U},
    {"Ueber REIST Workspace", DESKTOP_MENU_ACTION_ABOUT, 0U, 0U, 0U},
    {"Desktop beenden", DESKTOP_MENU_ACTION_EXIT, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t window_menu_items[] = {
    {"Neues Stammfenster", DESKTOP_MENU_ACTION_OPEN_ROOT, 0U, 0U, 0U},
    {"Alle Fenster schliessen", DESKTOP_MENU_ACTION_CLOSE_ALL, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t help_menu_items[] = {
    {"Desktop-Hilfe", DESKTOP_MENU_ACTION_HELP, 0U, 0U, 0U},
    {"Ueber REIST Workspace", DESKTOP_MENU_ACTION_ABOUT, 0U, 0U, 0U},
};

static const reist_gui_menu_t desktop_menus[DESKTOP_MENU_COUNT] = {
    {"REIST Workspace", workspace_menu_items,
     sizeof(workspace_menu_items) / sizeof(workspace_menu_items[0]), 0U, 0U},
    {"Fenster", window_menu_items,
     sizeof(window_menu_items) / sizeof(window_menu_items[0]), 0U, 0U},
    {"Hilfe", help_menu_items,
     sizeof(help_menu_items) / sizeof(help_menu_items[0]), 0U, 0U},
};

static const reist_gui_menu_model_t desktop_menu_model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = desktop_menus,
    .menu_count = DESKTOP_MENU_COUNT,
};

static const reist_gui_dialog_button_t help_dialog_buttons[] = {
    {"Schliessen", REIST_GUI_DIALOG_RESPONSE_CLOSE,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

static const reist_gui_dialog_button_t about_dialog_buttons[] = {
    {"OK", REIST_GUI_DIALOG_RESPONSE_OK,
     REIST_GUI_DIALOG_ROLE_ACCEPT, 0U, 0U},
};

static const reist_gui_dialog_button_t error_dialog_buttons[] = {
    {"OK", REIST_GUI_DIALOG_RESPONSE_OK,
     REIST_GUI_DIALOG_ROLE_ACCEPT, 0U, 0U},
};

/* Help is intentionally modeless so its outside events can continue to the
 * desktop. About is application-modal and makes every underlying target
 * inert. Both use the same public asynchronous controller. */
static const reist_gui_dialog_model_t help_dialog_model = {
    .version = REIST_GUI_DIALOG_API_VERSION,
    .struct_size = sizeof(reist_gui_dialog_model_t),
    .title = "Desktop-Hilfe",
    .message = "Menues: Klick, Pfeile und Enter",
    .detail = "Fenster: Titelleiste/Rand ziehen; ESC schliesst",
    .buttons = help_dialog_buttons,
    .button_count = 1U,
    .modality = REIST_GUI_DIALOG_MODELESS,
    .default_response = REIST_GUI_DIALOG_RESPONSE_CLOSE,
    .cancel_response = REIST_GUI_DIALOG_RESPONSE_CLOSE,
    .owner_id = REIST_GUI_DIALOG_NO_OWNER,
    .flags = REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
};

static const reist_gui_dialog_model_t about_dialog_model = {
    .version = REIST_GUI_DIALOG_API_VERSION,
    .struct_size = sizeof(reist_gui_dialog_model_t),
    .title = "Ueber REIST Workspace",
    .message = "REIST Workspace",
    .detail = "Modularer Ring-3 Desktop; GUI-API Version 1",
    .buttons = about_dialog_buttons,
    .button_count = 1U,
    .modality = REIST_GUI_DIALOG_APPLICATION_MODAL,
    .default_response = REIST_GUI_DIALOG_RESPONSE_OK,
    .cancel_response = REIST_GUI_DIALOG_RESPONSE_OK,
    .owner_id = REIST_GUI_DIALOG_NO_OWNER,
    .flags = REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
};

_Static_assert(
    sizeof(window_menu_items) / sizeof(window_menu_items[0]) <=
        REIST_GUI_MENU_MAX_ITEMS,
    "window menu exceeds fixed item capacity");

/* Deliberately small, high-contrast palette inspired by classic desktops. */
static const uint32_t color_desktop = 0x00006E8EU;
static const uint32_t color_face = 0x00C8C8C8U;
static const uint32_t color_light = 0x00FFFFFFU;
static const uint32_t color_shadow = 0x00606060U;
static const uint32_t color_dark = 0x00181818U;
static const uint32_t color_active = 0x00000088U;
static const uint32_t color_inactive = 0x00787878U;
static const uint32_t color_client = 0x00E8E8E8U;
static const uint32_t color_text = 0x00000000U;
static const uint32_t color_title_text = 0x00FFFFFFU;

typedef struct desktop_file_icon_cache_entry {
    uint32_t valid;
    uint32_t client[DESKTOP_FILE_ICON_PIXELS];
    uint32_t selected[DESKTOP_FILE_ICON_PIXELS];
    uint32_t desktop[DESKTOP_FILE_ICON_PIXELS];
} desktop_file_icon_cache_entry_t;

static desktop_file_icon_cache_entry_t
    desktop_file_icon_cache[DESKTOP_EXPLORER_ICON_COUNT];
static uint8_t desktop_file_icon_encoded[DESKTOP_FILE_ICON_ENCODED_CAPACITY];
static uint32_t desktop_file_icon_decoded[DESKTOP_FILE_ICON_PIXELS];
static const char *const desktop_file_icon_paths[
    DESKTOP_EXPLORER_ICON_COUNT] = {
        "/usr/share/icons/folder-empty.ico",
        "/usr/share/icons/folder-full.ico",
        "/usr/share/icons/program.ico",
        "/usr/share/icons/text.ico",
        "/usr/share/icons/audio.ico",
        "/usr/share/icons/image.ico",
        "/usr/share/icons/settings.ico",
        "/usr/share/icons/unknown.ico",
};

_Static_assert(sizeof(desktop_file_icon_paths) /
                   sizeof(desktop_file_icon_paths[0]) ==
                   DESKTOP_EXPLORER_ICON_COUNT,
               "file icon path table is incomplete");

typedef struct {
    const x86os_display_info_t *display;
    desktop_rect_t clip;
    uint32_t omitted_kind;
    uint32_t omitted_window;
} desktop_render_context_t;

typedef struct {
    uint32_t full_frames;
    uint32_t full_total_ms;
    uint32_t full_max_ms;
    uint32_t dirty_frames;
    uint32_t dirty_total_ms;
    uint32_t dirty_max_ms;
    uint32_t drag_frames;
    uint32_t drag_total_ms;
    uint32_t drag_max_ms;
    uint32_t resize_frames;
    uint32_t resize_total_ms;
    uint32_t resize_max_ms;
    uint32_t fallback_frames;
    uint32_t damage_regions;
    uint32_t damage_max;
    uint32_t clock_errors;
    uint32_t probe_errors;
} desktop_render_metrics_t;

typedef struct {
    reist_gui_menu_state_t menu;
    reist_gui_dialog_state_t dialog;
    uint32_t dialog_kind;
    reist_gui_dialog_model_t error_model;
    char error_detail[REIST_GUI_DIALOG_TEXT_LIMIT];
} desktop_ui_state_t;

typedef struct {
    uint32_t consumed;
    uint32_t action;
    uint32_t target;
} desktop_ui_result_t;

typedef struct {
    desktop_rect_t source;
    desktop_rect_t destination;
    uint32_t kind;
    uint32_t window_index;
    uint32_t valid;
} desktop_move_cache_t;

typedef struct {
    uint32_t valid;
    uint32_t root;
    uint32_t window_index;
    uint32_t entry_index;
} desktop_activation_t;

static size_t bounded_text_length(const char *text, size_t maximum) {
    size_t length = 0U;
    if (text == 0) return 0U;
    while (length < maximum && text[length] != '\0') ++length;
    return length;
}

static uint32_t text_equal(const char *left, const char *right) {
    if (left == 0 || right == 0) return 0U;
    for (uint32_t index = 0U; index < DESKTOP_ARGUMENT_LIMIT; ++index) {
        if (left[index] != right[index]) return 0U;
        if (left[index] == '\0') return 1U;
    }
    return 0U;
}

/* FAT directory entries and aliases may preserve or synthesize different
 * ASCII case than the canonical paths in /etc/reist/filetypes.conf. Program
 * classification must therefore follow the filesystem's case-insensitive
 * naming contract; otherwise a GUI executable silently falls back to the
 * synchronous full-screen launcher. */
static uint32_t path_equal_ascii_case(const char *left, const char *right) {
    if (left == 0 || right == 0) return 0U;
    for (uint32_t index = 0U;
         index < DESKTOP_EXPLORER_PATH_CAPACITY; ++index) {
        char left_value = left[index];
        char right_value = right[index];
        if (left_value >= 'A' && left_value <= 'Z')
            left_value = (char)(left_value + ('a' - 'A'));
        if (right_value >= 'A' && right_value <= 'Z')
            right_value = (char)(right_value + ('a' - 'A'));
        if (left_value != right_value) return 0U;
        if (left_value == '\0') return 1U;
    }
    return 0U;
}

static uint32_t saturating_add_u32(uint32_t left, uint32_t right) {
    return left > UINT32_MAX - right ? UINT32_MAX : left + right;
}

static void saturating_increment(uint32_t *value) {
    if (value != 0 && *value != UINT32_MAX) ++*value;
}

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static uint32_t min_u32(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static uint32_t intersect_rects(desktop_rect_t left, desktop_rect_t right,
                                desktop_rect_t *intersection) {
    int64_t x = left.x > right.x ? left.x : right.x;
    int64_t y = left.y > right.y ? left.y : right.y;
    int64_t left_right = (int64_t)left.x + left.width;
    int64_t right_right = (int64_t)right.x + right.width;
    int64_t left_bottom = (int64_t)left.y + left.height;
    int64_t right_bottom = (int64_t)right.y + right.height;
    int64_t maximum_x = left_right < right_right ? left_right : right_right;
    int64_t maximum_y = left_bottom < right_bottom
        ? left_bottom : right_bottom;
    if (x >= maximum_x || y >= maximum_y) return 0U;
    if (intersection != 0) {
        *intersection = (desktop_rect_t){
            (int32_t)x, (int32_t)y,
            (uint32_t)(maximum_x - x), (uint32_t)(maximum_y - y)
        };
    }
    return 1U;
}

static int read_file_bounded(const char *path, uint8_t *bytes,
                             size_t capacity, size_t *size_out) {
    if (path == 0 || bytes == 0 || capacity == 0U || size_out == 0) return -22;
    int descriptor = x86os_open(path);
    if (descriptor < 0) return descriptor;
    size_t used = 0U;
    while (used < capacity) {
        int amount = x86os_read(descriptor, bytes + used, capacity - used);
        if (amount < 0 || (size_t)amount > capacity - used) {
            (void)x86os_close(descriptor);
            return -5;
        }
        if (amount == 0) break;
        used += (size_t)amount;
    }
    uint8_t extra = 0U;
    int extra_read = x86os_read(descriptor, &extra, 1U);
    int close_status = x86os_close(descriptor);
    if (extra_read != 0 || close_status < 0 || used == 0U) return -75;
    *size_out = used;
    return 0;
}

static uint32_t compose_icon_pixel(uint32_t argb, uint32_t background) {
    uint32_t alpha = argb >> 24U;
    uint32_t inverse = 255U - alpha;
    uint32_t red = (((argb >> 16U) & 0xFFU) * alpha +
                    ((background >> 16U) & 0xFFU) * inverse + 127U) / 255U;
    uint32_t green = (((argb >> 8U) & 0xFFU) * alpha +
                      ((background >> 8U) & 0xFFU) * inverse + 127U) / 255U;
    uint32_t blue = ((argb & 0xFFU) * alpha +
                     (background & 0xFFU) * inverse + 127U) / 255U;
    return (red << 16U) | (green << 8U) | blue;
}

static void desktop_file_icon_cache_initialize(void) {
    for (uint32_t kind = 0U; kind < DESKTOP_EXPLORER_ICON_COUNT; ++kind) {
        desktop_file_icon_cache[kind].valid = 0U;
        size_t encoded_size = 0U;
        if (read_file_bounded(
                desktop_file_icon_paths[kind], desktop_file_icon_encoded,
                sizeof(desktop_file_icon_encoded), &encoded_size) != 0)
            continue;
        reist_image_info_t info;
        if (reist_image_decode_ico(
                desktop_file_icon_encoded, encoded_size,
                desktop_file_icon_decoded, DESKTOP_FILE_ICON_PIXELS,
                &info) != 0 || info.width != DESKTOP_FILE_ICON_SIZE ||
            info.height != DESKTOP_FILE_ICON_SIZE ||
            info.stride_pixels != DESKTOP_FILE_ICON_SIZE ||
            info.format != REIST_IMAGE_FORMAT_ICO ||
            (info.flags & REIST_IMAGE_FLAG_ALPHA) == 0U)
            continue;
        for (uint32_t pixel = 0U; pixel < DESKTOP_FILE_ICON_PIXELS; ++pixel) {
            uint32_t argb = desktop_file_icon_decoded[pixel];
            desktop_file_icon_cache[kind].client[pixel] =
                compose_icon_pixel(argb, color_client);
            desktop_file_icon_cache[kind].selected[pixel] =
                compose_icon_pixel(argb, color_face);
            desktop_file_icon_cache[kind].desktop[pixel] =
                compose_icon_pixel(argb, color_desktop);
        }
        desktop_file_icon_cache[kind].valid = 1U;
    }
}

static uint32_t draw_cached_file_icon(
    const desktop_render_context_t *context, desktop_rect_t bounds,
    uint32_t kind, uint32_t selected, uint32_t desktop_background) {
    if (context == 0 || kind >= DESKTOP_EXPLORER_ICON_COUNT ||
        bounds.width != DESKTOP_FILE_ICON_SIZE ||
        bounds.height != DESKTOP_FILE_ICON_SIZE ||
        !desktop_file_icon_cache[kind].valid) return 0U;
    desktop_rect_t clipped;
    if (!intersect_rects(bounds, context->clip, &clipped)) return 1U;
    const uint32_t *pixels = selected
        ? desktop_file_icon_cache[kind].selected
        : desktop_background ? desktop_file_icon_cache[kind].desktop
                             : desktop_file_icon_cache[kind].client;
    uint32_t source_x = (uint32_t)(clipped.x - bounds.x);
    uint32_t source_y = (uint32_t)(clipped.y - bounds.y);
    (void)x86os_draw_pixels(
        clipped.x, clipped.y, clipped.width, clipped.height,
        pixels + (size_t)source_y * DESKTOP_FILE_ICON_SIZE + source_x,
        DESKTOP_FILE_ICON_SIZE);
    return 1U;
}

static void fill_rect_clipped(const desktop_render_context_t *context,
                              desktop_rect_t rect, uint32_t color) {
    desktop_rect_t clipped;
    if (context == 0 || context->display == 0 ||
        !intersect_rects(rect, context->clip, &clipped)) return;
    (void)x86os_fill_rect(clipped.x, clipped.y, clipped.width, clipped.height,
                          color);
}

static void draw_text_clipped(const desktop_render_context_t *context,
                              int32_t x, int32_t y, const char *text,
                              uint32_t maximum_width, uint32_t foreground,
                              uint32_t background) {
    if (context == 0 || context->display == 0 || text == 0 ||
        context->display->font_width == 0U ||
        context->display->font_height == 0U) return;
    const x86os_display_info_t *display = context->display;
    int64_t clip_left = context->clip.x;
    int64_t clip_top = context->clip.y;
    int64_t clip_right = clip_left + context->clip.width;
    int64_t clip_bottom = clip_top + context->clip.height;
    int64_t text_top = y;
    int64_t text_bottom = text_top + display->font_height;
    if (text_top >= clip_bottom || text_bottom <= clip_top) return;
    size_t capacity = maximum_width / display->font_width;
    if (capacity > X86OS_DISPLAY_MAX_TEXT)
        capacity = X86OS_DISPLAY_MAX_TEXT;
    size_t length = bounded_text_length(text, capacity);
    size_t first = 0U;
    while (first < length) {
        int64_t glyph_left = (int64_t)x + first * display->font_width;
        int64_t glyph_right = glyph_left + display->font_width;
        if (glyph_left < clip_right && glyph_right > clip_left) break;
        ++first;
    }
    size_t end = first;
    while (end < length) {
        int64_t glyph_left = (int64_t)x + end * display->font_width;
        int64_t glyph_right = glyph_left + display->font_width;
        if (glyph_left >= clip_right || glyph_right <= clip_left) break;
        ++end;
    }
    if (end != first)
        (void)x86os_draw_text_pixels_clipped(
            x + (int32_t)(first * display->font_width), y,
            text + first, end - first, foreground, background,
            context->clip.x, context->clip.y,
            context->clip.width, context->clip.height);
}

static uint32_t menu_height(const x86os_display_info_t *display) {
    return max_u32(display->font_height + 12U, 30U);
}

static uint32_t status_height(const x86os_display_info_t *display) {
    return max_u32(display->font_height + 10U, 28U);
}

static uint32_t point_in_rect(desktop_rect_t rect, int32_t x, int32_t y) {
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return x >= rect.x && y >= rect.y &&
           (int64_t)x < right && (int64_t)y < bottom;
}

static desktop_rect_t desktop_rect_from_gui(reist_gui_rect_t rect) {
    return (desktop_rect_t){rect.x, rect.y, rect.width, rect.height};
}

static reist_gui_menu_layout_t desktop_menu_layout(
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

static void desktop_ui_initialize(desktop_ui_state_t *ui) {
    if (ui == 0) return;
    reist_gui_menu_state_initialize(&ui->menu);
    reist_gui_dialog_state_initialize(&ui->dialog);
    ui->dialog_kind = DESKTOP_DIALOG_NONE;
    ui->error_detail[0] = '\0';
    ui->error_model = (reist_gui_dialog_model_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(reist_gui_dialog_model_t),
        .title = "Fehler",
        .message = "Der Vorgang ist fehlgeschlagen.",
        .detail = ui->error_detail,
        .buttons = error_dialog_buttons,
        .button_count = 1U,
        .modality = REIST_GUI_DIALOG_APPLICATION_MODAL,
        .default_response = REIST_GUI_DIALOG_RESPONSE_OK,
        .cancel_response = REIST_GUI_DIALOG_RESPONSE_OK,
        .owner_id = REIST_GUI_DIALOG_NO_OWNER,
        .flags = REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
    };
}

static const reist_gui_dialog_model_t *desktop_dialog_model(
    const desktop_ui_state_t *ui, uint32_t kind) {
    if (kind == DESKTOP_DIALOG_HELP) return &help_dialog_model;
    if (kind == DESKTOP_DIALOG_ABOUT) return &about_dialog_model;
    if (kind == DESKTOP_DIALOG_ERROR && ui != 0) return &ui->error_model;
    return 0;
}

static reist_gui_dialog_layout_t desktop_dialog_layout(
    const x86os_display_info_t *display) {
    uint32_t top = menu_height(display) + 12U;
    uint32_t bottom = display->height > status_height(display) + 12U
        ? display->height - status_height(display) - 12U : top + 1U;
    uint32_t available_height = bottom > top ? bottom - top : 1U;
    uint32_t width = display->width > 32U ? display->width - 32U : 1U;
    if (width > 560U) width = 560U;
    uint32_t line = max_u32(display->font_height + 6U, 18U);
    uint32_t height = menu_height(display) + line * 6U +
                      display->font_height + 38U;
    if (height > available_height) height = available_height;
    uint32_t button_height = max_u32(display->font_height + 10U, 24U);
    return (reist_gui_dialog_layout_t){
        .version = REIST_GUI_DIALOG_API_VERSION,
        .struct_size = sizeof(reist_gui_dialog_layout_t),
        .surface_width = display->width,
        .surface_height = display->height,
        .work_area = {0, (int32_t)top, display->width, available_height},
        .initial_bounds = {
            (int32_t)((display->width - width) / 2U),
            (int32_t)(top + (available_height - height) / 2U),
            width, height,
        },
        .title_height = menu_height(display),
        .border_width = 3U,
        .font_width = display->font_width,
        .font_height = display->font_height,
        .button_min_width = 80U,
        .button_height = button_height,
        .button_gap = 8U,
        .button_padding_x = 8U,
        .content_padding = 10U,
        .damage_margin = 6U,
    };
}

static void collect_menu_damage(
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_result_t *menu_result) {
    if (dirty == 0 || menu_result == 0) return;
    if (menu_result->full_redraw || menu_result->damage_count != 0U) {
        /* The current text syscall clips only at the screen edge. Repainting
         * a narrow scene region can therefore clear half of a glyph while
         * draw_text_clipped() correctly refuses to draw that partial glyph.
         * Publish one atomic full scene for visible menu state changes until
         * the raster ABI provides an explicit glyph clip rectangle. Motion
         * inside an unchanged item reports no damage and remains redraw-free. */
        desktop_dirty_full(dirty);
    }
}

static void collect_dialog_damage(
    desktop_dirty_region_t *dirty,
    const reist_gui_dialog_result_t *dialog_result) {
    if (dirty == 0 || dialog_result == 0) return;
    if (dialog_result->full_redraw) {
        desktop_dirty_full(dirty);
        return;
    }
    for (uint32_t index = 0U;
         index < dialog_result->damage_count; ++index) {
        desktop_dirty_add(
            dirty, desktop_rect_from_gui(dialog_result->damage[index]));
    }
}

static desktop_ui_result_t desktop_ui_result_none(void) {
    return (desktop_ui_result_t){
        .consumed = 0U,
        .action = DESKTOP_UI_ACTION_NONE,
        .target = DESKTOP_WM_NO_TARGET,
    };
}

static void desktop_ui_open_dialog(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, uint32_t kind) {
    const reist_gui_dialog_model_t *model = desktop_dialog_model(ui, kind);
    reist_gui_dialog_layout_t layout = desktop_dialog_layout(display);
    if (model == 0) return;

    if (ui->dialog.visible) {
        const reist_gui_dialog_model_t *previous =
            desktop_dialog_model(ui, ui->dialog_kind);
        reist_gui_dialog_result_t closed;
        reist_gui_dialog_result_initialize(&closed);
        if (previous == 0 || reist_gui_dialog_complete(
                previous, &layout, &ui->dialog,
                previous->cancel_response, &closed) != 0) {
            reist_gui_dialog_state_initialize(&ui->dialog);
            ui->dialog_kind = DESKTOP_DIALOG_NONE;
            desktop_dirty_full(dirty);
        } else {
            collect_dialog_damage(dirty, &closed);
        }
    }

    reist_gui_dialog_result_t opened;
    reist_gui_dialog_result_initialize(&opened);
    ui->dialog_kind = kind;
    if (reist_gui_dialog_open(
            model, &layout, &ui->dialog, &opened) != 0) {
        reist_gui_dialog_state_initialize(&ui->dialog);
        ui->dialog_kind = DESKTOP_DIALOG_NONE;
        desktop_dirty_full(dirty);
        return;
    }
    collect_dialog_damage(dirty, &opened);
}

static void desktop_ui_open_error(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const char *message, const char *detail) {
    if (ui == 0 || display == 0 || dirty == 0 || message == 0) return;
    uint32_t index = 0U;
    if (detail != 0) {
        while (index + 1U < sizeof(ui->error_detail) &&
               detail[index] != '\0') {
            ui->error_detail[index] = detail[index];
            ++index;
        }
    }
    ui->error_detail[index] = '\0';
    ui->error_model.message = message;
    desktop_ui_open_dialog(
        ui, display, dirty, DESKTOP_DIALOG_ERROR);
}

static desktop_ui_result_t desktop_ui_apply_dialog_result(
    desktop_ui_state_t *ui, desktop_dirty_region_t *dirty,
    const reist_gui_dialog_result_t *dialog_result) {
    desktop_ui_result_t result = desktop_ui_result_none();
    result.consumed = dialog_result->consumed;
    collect_dialog_damage(dirty, dialog_result);
    if (dialog_result->completed)
        ui->dialog_kind = DESKTOP_DIALOG_NONE;
    return result;
}

static desktop_ui_result_t desktop_ui_apply_menu_result(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_result_t *menu_result) {
    desktop_ui_result_t result = desktop_ui_result_none();
    result.consumed = menu_result->consumed;
    collect_menu_damage(dirty, menu_result);
    if (!menu_result->activated) return result;
    if (menu_result->action == DESKTOP_MENU_ACTION_HELP ||
        menu_result->action == DESKTOP_MENU_ACTION_ABOUT) {
        desktop_ui_open_dialog(
            ui, display, dirty,
            menu_result->action == DESKTOP_MENU_ACTION_HELP
                ? DESKTOP_DIALOG_HELP : DESKTOP_DIALOG_ABOUT);
    } else if (menu_result->action == DESKTOP_MENU_ACTION_EXIT) {
        result.action = DESKTOP_UI_ACTION_EXIT;
    } else if (menu_result->action == DESKTOP_MENU_ACTION_OPEN_ROOT) {
        result.action = DESKTOP_UI_ACTION_OPEN_ROOT;
    } else if (menu_result->action == DESKTOP_MENU_ACTION_CLOSE_ALL) {
        result.action = DESKTOP_UI_ACTION_CLOSE_ALL;
    }
    return result;
}

static desktop_ui_result_t desktop_ui_dispatch_menu(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_event_t *event) {
    desktop_ui_result_t result = desktop_ui_result_none();
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    reist_gui_menu_result_t menu_result;
    reist_gui_menu_result_initialize(&menu_result);
    /* A corrupt API state is reset closed and never forwarded to the WM. */
    if (reist_gui_menu_dispatch(
            &desktop_menu_model, &layout, &ui->menu,
            event, &menu_result) != 0) {
        result.consumed = 1U;
        desktop_dirty_full(dirty);
        desktop_ui_initialize(ui);
        return result;
    }
    return desktop_ui_apply_menu_result(
        ui, display, dirty, &menu_result);
}

static desktop_ui_result_t desktop_ui_dispatch_dialog_pointer(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t x, int32_t y,
    uint32_t button_event, uint32_t pressed) {
    desktop_ui_result_t result = desktop_ui_result_none();
    const reist_gui_dialog_model_t *model =
        desktop_dialog_model(ui, ui->dialog_kind);
    reist_gui_dialog_layout_t layout = desktop_dialog_layout(display);
    reist_gui_dialog_event_t event;
    reist_gui_dialog_event_initialize(&event);
    event.type = button_event
        ? REIST_GUI_DIALOG_EVENT_POINTER_BUTTON
        : REIST_GUI_DIALOG_EVENT_POINTER_MOTION;
    event.x = x;
    event.y = y;
    event.button = button_event ? REIST_GUI_DIALOG_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    reist_gui_dialog_result_t dialog_result;
    reist_gui_dialog_result_initialize(&dialog_result);
    if (model == 0 || reist_gui_dialog_dispatch(
            model, &layout, &ui->dialog, &event, &dialog_result) != 0) {
        result.consumed = 1U;
        reist_gui_dialog_state_initialize(&ui->dialog);
        ui->dialog_kind = DESKTOP_DIALOG_NONE;
        desktop_dirty_full(dirty);
        return result;
    }
    return desktop_ui_apply_dialog_result(ui, dirty, &dialog_result);
}

static desktop_ui_result_t desktop_ui_pointer_event(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t x, int32_t y,
    uint32_t button_event, uint32_t pressed) {
    if (ui->dialog.visible) {
        desktop_ui_result_t dialog_result =
            desktop_ui_dispatch_dialog_pointer(
            ui, display, dirty, x, y, button_event, pressed);
        if (dialog_result.consumed) return dialog_result;
    }
    reist_gui_menu_event_t event;
    reist_gui_menu_event_initialize(&event);
    event.type = button_event
        ? REIST_GUI_MENU_EVENT_POINTER_BUTTON
        : REIST_GUI_MENU_EVENT_POINTER_MOTION;
    event.x = x;
    event.y = y;
    event.button = button_event ? REIST_GUI_MENU_BUTTON_LEFT : 0U;
    event.pressed = pressed;
    return desktop_ui_dispatch_menu(ui, display, dirty, &event);
}

static uint32_t desktop_ui_owns_pointer(const desktop_ui_state_t *ui) {
    return (ui->dialog.visible &&
            (ui->dialog.capture_kind != REIST_GUI_DIALOG_CAPTURE_NONE ||
             ui->dialog.modality != REIST_GUI_DIALOG_MODELESS)) ||
           ui->menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
           ui->menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE;
}

static uint32_t desktop_menu_key_from_input(int key) {
    if (key == DESKTOP_KEY_LEFT) return REIST_GUI_MENU_KEY_LEFT;
    if (key == DESKTOP_KEY_RIGHT || key == '\t')
        return REIST_GUI_MENU_KEY_RIGHT;
    if (key == DESKTOP_KEY_UP) return REIST_GUI_MENU_KEY_UP;
    if (key == DESKTOP_KEY_DOWN) return REIST_GUI_MENU_KEY_DOWN;
    if (key == '\r' || key == '\n') return REIST_GUI_MENU_KEY_ENTER;
    if (key == DESKTOP_KEY_ESCAPE) return REIST_GUI_MENU_KEY_ESCAPE;
    return 0U;
}

static uint32_t desktop_dialog_key_from_input(int key) {
    if (key == DESKTOP_KEY_LEFT || key == DESKTOP_KEY_UP)
        return REIST_GUI_DIALOG_KEY_PREVIOUS;
    if (key == DESKTOP_KEY_RIGHT || key == DESKTOP_KEY_DOWN || key == '\t')
        return REIST_GUI_DIALOG_KEY_NEXT;
    if (key == '\r' || key == '\n') return REIST_GUI_DIALOG_KEY_ENTER;
    if (key == DESKTOP_KEY_ESCAPE) return REIST_GUI_DIALOG_KEY_ESCAPE;
    return 0U;
}

static desktop_ui_result_t desktop_ui_keyboard_event(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int key) {
    desktop_ui_result_t result = desktop_ui_result_none();
    if (ui->dialog.visible) {
        uint32_t dialog_key = desktop_dialog_key_from_input(key);
        if (dialog_key != 0U) {
            const reist_gui_dialog_model_t *model =
                desktop_dialog_model(ui, ui->dialog_kind);
            reist_gui_dialog_layout_t layout =
                desktop_dialog_layout(display);
            reist_gui_dialog_event_t event;
            reist_gui_dialog_event_initialize(&event);
            event.type = REIST_GUI_DIALOG_EVENT_KEYBOARD;
            event.key = dialog_key;
            reist_gui_dialog_result_t dialog_result;
            reist_gui_dialog_result_initialize(&dialog_result);
            if (model == 0 || reist_gui_dialog_dispatch(
                    model, &layout, &ui->dialog,
                    &event, &dialog_result) != 0) {
                result.consumed = 1U;
                reist_gui_dialog_state_initialize(&ui->dialog);
                ui->dialog_kind = DESKTOP_DIALOG_NONE;
                desktop_dirty_full(dirty);
                return result;
            }
            result = desktop_ui_apply_dialog_result(
                ui, dirty, &dialog_result);
        } else {
            result.consumed =
                ui->dialog.modality != REIST_GUI_DIALOG_MODELESS ||
                ui->dialog.active;
        }
        if (result.consumed) return result;
    }
    if (ui->menu.open_menu == REIST_GUI_MENU_NO_INDEX) return result;
    uint32_t menu_key = desktop_menu_key_from_input(key);
    if (menu_key == 0U) {
        result.consumed = 1U;
        return result;
    }
    reist_gui_menu_event_t event;
    reist_gui_menu_event_initialize(&event);
    event.type = REIST_GUI_MENU_EVENT_KEYBOARD;
    event.key = menu_key;
    return desktop_ui_dispatch_menu(ui, display, dirty, &event);
}

static desktop_rect_t desktop_icon_rect(const x86os_display_info_t *display,
                                        uint32_t index) {
    desktop_rect_t rect = {0, 0, 0U, 0U};
    if (index >= DESKTOP_ICON_COUNT) return rect;
    uint32_t top = menu_height(display) + 8U;
    rect.x = 8;
    rect.y = (int32_t)(top + index *
        max_u32(display->font_height + 42U, 68U));
    rect.width = min_u32(
        DESKTOP_ICON_WIDTH,
        display->width > 16U ? display->width - 16U : 1U);
    rect.height = max_u32(display->font_height + 42U, 68U);
    return rect;
}

static int desktop_icon_at_position(const x86os_display_info_t *display,
                                    int32_t x, int32_t y) {
    for (uint32_t index = 0U; index < DESKTOP_ICON_COUNT; ++index) {
        if (point_in_rect(desktop_icon_rect(display, index), x, y))
            return (int)index;
    }
    return DESKTOP_WM_NO_WINDOW;
}

static void draw_bevel(const desktop_render_context_t *context,
                       desktop_rect_t rect, uint32_t face,
                       uint32_t raised) {
    if (rect.width == 0U || rect.height == 0U) return;
    fill_rect_clipped(context, rect, face);
    if (rect.width < 2U || rect.height < 2U) return;
    uint32_t top_left = raised ? color_light : color_shadow;
    uint32_t bottom_right = raised ? color_shadow : color_light;
    fill_rect_clipped(context,
                      (desktop_rect_t){rect.x, rect.y, rect.width, 1U},
                      top_left);
    fill_rect_clipped(context,
                      (desktop_rect_t){rect.x, rect.y, 1U, rect.height},
                      top_left);
    fill_rect_clipped(
        context,
        (desktop_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                         rect.width, 1U},
        bottom_right);
    fill_rect_clipped(
        context,
        (desktop_rect_t){rect.x + (int32_t)rect.width - 1, rect.y,
                         1U, rect.height},
        bottom_right);
}

static void draw_file_icon_fallback(
    const desktop_render_context_t *context, desktop_rect_t symbol,
    uint32_t kind) {
    static const uint32_t accents[DESKTOP_EXPLORER_ICON_COUNT] = {
        0x00C58A18U, 0x00D29A20U, 0x00007896U, 0x00E4E0D2U,
        0x00513A8CU, 0x002B8A68U, 0x00808A96U, 0x0096A0ACU,
    };
    if (kind >= DESKTOP_EXPLORER_ICON_COUNT) kind =
        DESKTOP_EXPLORER_ICON_UNKNOWN;
    uint32_t accent = accents[kind];
    draw_bevel(context, symbol, accent, 1U);
    if (symbol.width < 16U || symbol.height < 16U) return;
    if (kind == DESKTOP_EXPLORER_ICON_FOLDER_EMPTY ||
        kind == DESKTOP_EXPLORER_ICON_FOLDER_FULL) {
        fill_rect_clipped(context,
            (desktop_rect_t){symbol.x + 3, symbol.y - 2,
                             symbol.width / 2U, 5U}, accent);
        if (kind == DESKTOP_EXPLORER_ICON_FOLDER_FULL)
            fill_rect_clipped(context,
                (desktop_rect_t){symbol.x + 9, symbol.y + 7,
                                 symbol.width - 13U, symbol.height - 11U},
                color_light);
    } else if (kind == DESKTOP_EXPLORER_ICON_PROGRAM) {
        fill_rect_clipped(context,
            (desktop_rect_t){symbol.x + 5, symbol.y + 5,
                             symbol.width - 10U, symbol.height - 12U},
            0x0000A8C8U);
        fill_rect_clipped(context,
            (desktop_rect_t){symbol.x + 11,
                             symbol.y + (int32_t)symbol.height - 5,
                             symbol.width - 22U, 3U}, color_dark);
    } else if (kind == DESKTOP_EXPLORER_ICON_TEXT ||
               kind == DESKTOP_EXPLORER_ICON_SETTINGS ||
               kind == DESKTOP_EXPLORER_ICON_UNKNOWN) {
        for (uint32_t line = 0U; line < 3U; ++line)
            fill_rect_clipped(context,
                (desktop_rect_t){symbol.x + 6,
                                 symbol.y + 7 + (int32_t)(line * 6U),
                                 symbol.width - 12U, 2U}, color_dark);
    } else if (kind == DESKTOP_EXPLORER_ICON_AUDIO) {
        fill_rect_clipped(context,
            (desktop_rect_t){symbol.x + 7, symbol.y + 7,
                             symbol.width - 14U, symbol.height - 14U},
            color_dark);
    } else {
        fill_rect_clipped(context,
            (desktop_rect_t){symbol.x + 5, symbol.y + 5,
                             symbol.width - 10U, symbol.height - 10U},
            0x0000A070U);
    }
}

static int32_t centered_text_x(
    const x86os_display_info_t *display, desktop_rect_t bounds,
    const char *text, uint32_t horizontal_padding) {
    if (display == 0 || text == 0 || display->font_width == 0U ||
        bounds.width <= horizontal_padding * 2U)
        return bounds.x;
    uint32_t usable = bounds.width - horizontal_padding * 2U;
    size_t maximum_chars = usable / display->font_width;
    size_t length = bounded_text_length(text, maximum_chars + 1U);
    if (length > maximum_chars)
        return bounds.x + (int32_t)horizontal_padding;
    uint32_t text_width = (uint32_t)length * display->font_width;
    return bounds.x + (int32_t)horizontal_padding +
        (int32_t)((usable - text_width) / 2U);
}

static void render_icon(const desktop_render_context_t *context,
                        const desktop_explorer_t *explorer, uint32_t index) {
    const x86os_display_info_t *display = context->display;
    desktop_rect_t rect = desktop_icon_rect(display, index);
    if (!intersect_rects(rect, context->clip, 0)) return;
    uint32_t selected = index == 0U
        ? explorer != 0 && explorer->desktop_selected
        : control_panel_selected;
    uint32_t icon_size = min_u32(rect.height > display->font_height + 6U
        ? rect.height - display->font_height - 6U : 12U,
        DESKTOP_FILE_ICON_SIZE);
    desktop_rect_t symbol = {
        rect.x + (int32_t)((rect.width - icon_size) / 2U),
        rect.y + 3,
        icon_size,
        icon_size
    };
    desktop_rect_t focus = {
        symbol.x - 2,
        symbol.y - 2,
        symbol.width + 4U,
        symbol.height + 4U
    };
    /* The large cell remains the predictable mouse/keyboard target. Visual
     * focus is deliberately compact; otherwise sparse desktop rows look like
     * selected panels instead of selected icons. */
    if (selected) draw_bevel(context, focus, color_face, 0U);
    uint32_t kind = index == 0U ? DESKTOP_EXPLORER_ICON_FOLDER_FULL
                                : DESKTOP_EXPLORER_ICON_SETTINGS;
    if (!draw_cached_file_icon(context, symbol, kind, selected, 1U))
        draw_file_icon_fallback(context, symbol, kind);
    /* Anchor the caption to the visible symbol, not to the bottom of the
     * deliberately tall hit cell.  This keeps icon and title recognizable as
     * one desktop object while preserving the generous input target. */
    uint32_t text_y_offset = 3U + symbol.height + 3U;
    if (text_y_offset + display->font_height > rect.height) {
        text_y_offset = rect.height > display->font_height
            ? rect.height - display->font_height : 0U;
    }
    draw_text_clipped(
                      context,
                      centered_text_x(
                          display, rect, desktop_icons[index].title, 4U),
                      rect.y + (int32_t)text_y_offset,
                      desktop_icons[index].title,
                      rect.width - 8U, color_title_text,
                      selected ? color_active : color_desktop);
}

static void render_resize_grip(const desktop_render_context_t *context,
                               const desktop_window_t *window) {
    if (window == 0 || window->width < 16U || window->height < 16U) return;
    int32_t right = window->x + (int32_t)window->width;
    int32_t bottom = window->y + (int32_t)window->height;
    for (uint32_t row = 0U; row < 3U; ++row) {
        for (uint32_t column = 0U; column <= row; ++column) {
            fill_rect_clipped(
                context,
                (desktop_rect_t){right - 4 - (int32_t)(column * 4U),
                                 bottom - 4 - (int32_t)((row - column) * 4U),
                                 2U, 2U},
                color_shadow);
        }
    }
}

static desktop_rect_t desktop_window_client_rect(
    const desktop_wm_t *manager, uint32_t window_index) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY) return empty;
    const desktop_window_t *window = &manager->windows[window_index];
    uint32_t border = manager->frame_border;
    if (window->width <= border * 2U ||
        window->height <= border * 2U + manager->title_height) return empty;
    return (desktop_rect_t){
        window->x + (int32_t)border,
        window->y + (int32_t)border + (int32_t)manager->title_height,
        window->width - border * 2U,
        window->height - border * 2U - manager->title_height,
    };
}

static void render_explorer_entry(
    const desktop_render_context_t *context,
    const desktop_explorer_window_t *explorer_window,
    desktop_rect_t client, uint32_t entry_index) {
    desktop_rect_t cell = desktop_explorer_entry_rect(
        explorer_window, client, entry_index);
    if (cell.width == 0U || cell.height == 0U ||
        !intersect_rects(cell, context->clip, 0)) return;
    const x86os_display_info_t *display = context->display;
    const x86os_file_info_t *entry = &explorer_window->entries[entry_index];
    uint32_t selected = explorer_window->selected == entry_index;
    uint32_t symbol_width = min_u32(DESKTOP_FILE_ICON_SIZE,
        cell.width > 12U ? cell.width - 12U : 1U);
    uint32_t symbol_height = min_u32(DESKTOP_FILE_ICON_SIZE,
        cell.height > display->font_height + 12U
            ? cell.height - display->font_height - 12U : 1U);
    desktop_rect_t symbol = {
        cell.x + (int32_t)((cell.width - symbol_width) / 2U),
        cell.y + 5,
        symbol_width,
        symbol_height,
    };
    desktop_rect_t focus = {
        symbol.x - 3, symbol.y - 3,
        symbol.width + 6U,
        symbol.height + display->font_height + 10U,
    };
    if (selected) draw_bevel(context, focus, color_face, 0U);
    uint32_t kind = desktop_explorer_icon_kind(
        entry, explorer_window->directory_nonempty[entry_index]);
    if (!draw_cached_file_icon(context, symbol, kind, selected, 0U))
        draw_file_icon_fallback(context, symbol, kind);
    int32_t label_y = symbol.y + (int32_t)symbol.height + 5;
    uint32_t label_width = cell.width > 6U ? cell.width - 6U : 1U;
    draw_text_clipped(
        context, centered_text_x(display, cell, entry->name, 3U),
        label_y, entry->name, label_width,
        selected ? color_title_text : color_text,
        selected ? color_active : color_client);
}

static void render_window(const desktop_render_context_t *context,
                          const desktop_wm_t *manager,
                          const desktop_explorer_t *explorer,
                          const desktop_surface_manager_t *surfaces,
                          uint32_t window_index) {
    if (window_index >= DESKTOP_WM_CAPACITY) return;
    const x86os_display_info_t *display = context->display;
    const desktop_window_t *window = &manager->windows[window_index];
    const desktop_explorer_window_t *explorer_window = explorer != 0 &&
        explorer->windows[window_index].active
        ? &explorer->windows[window_index] : 0;
    const desktop_surface_slot_t *surface = 0;
    if (surfaces != 0) {
        for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index) {
            if (surfaces->slots[index].active &&
                surfaces->slots[index].window_index == window_index) {
                surface = &surfaces->slots[index];
                break;
            }
        }
    }
    if (window->visible == 0U ||
        (explorer_window == 0 && surface == 0)) return;
    if (!intersect_rects(desktop_wm_window_bounds(manager, window_index),
                         context->clip, 0)) return;
    uint32_t border = manager->frame_border;
    if (window->width <= border * 2U ||
        window->height <= border * 2U + manager->title_height) return;

    desktop_rect_t shadow = {
        window->x + 4, window->y + 4, window->width, window->height
    };
    desktop_rect_t frame = {
        window->x, window->y, window->width, window->height
    };
    desktop_rect_t title = {
        window->x + (int32_t)border,
        window->y + (int32_t)border,
        window->width - border * 2U,
        manager->title_height
    };
    desktop_rect_t client = desktop_window_client_rect(manager, window_index);
    uint32_t active = manager->keyboard_focus == (int32_t)window_index;
    uint32_t title_color = active ? color_active : color_inactive;

    fill_rect_clipped(context, shadow, color_dark);
    draw_bevel(context, frame, color_face, 1U);
    fill_rect_clipped(context, title, title_color);
    fill_rect_clipped(context, client, color_client);

    desktop_rect_t close = desktop_wm_close_rect(manager, window_index);
    draw_bevel(context, close, color_face, 1U);
    if (close.width > 8U && close.height > 8U) {
        fill_rect_clipped(
            context,
            (desktop_rect_t){close.x + 4, close.y + 4,
                             close.width - 8U, close.height - 8U},
            color_dark);
    }

    uint32_t title_x = (uint32_t)(close.x - title.x) + close.width + 6U;
    uint32_t title_y = title.height > display->font_height
        ? (title.height - display->font_height) / 2U : 0U;
    if (title_x + 3U < title.width) {
        draw_text_clipped(context, title.x + (int32_t)title_x,
                          title.y + (int32_t)title_y,
                          explorer_window != 0 ? explorer_window->path
                                               : surface->title,
                          title.width - title_x - 3U, color_title_text,
                          title_color);
    }

    desktop_rect_t surface_clip = {0, 0, 0U, 0U};
    uint32_t surface_visible = surface != 0 &&
        intersect_rects(client, context->clip, &surface_clip);
    desktop_render_context_t surface_context = *context;
    surface_context.clip = surface_clip;
    if (surface_visible && surface->committed &&
        surface->committed_buffer != 0U) {
            (void)x86os_display_surface_buffer_draw(
                (int)surface->owner.pid,
                surface->owner.process_generation,
                surface->committed_buffer,
                surface->committed_buffer_generation,
                (uint32_t)(surface_clip.x - client.x),
                (uint32_t)(surface_clip.y - client.y),
                surface_clip.x, surface_clip.y,
                surface_clip.width, surface_clip.height);
    }
    if (surface_visible)
        for (uint32_t index = 0U;
             index < surface->committed_paint_count; ++index) {
            const desktop_surface_paint_command_t *command =
                &surface->committed_paint[index];
            desktop_rect_t bounds = {
                client.x + command->rect.x,
                client.y + command->rect.y,
                command->rect.width, command->rect.height,
            };
            if (command->type == DESKTOP_SURFACE_PAINT_FILL)
                fill_rect_clipped(
                    &surface_context, bounds, command->foreground);
            else if (command->type == DESKTOP_SURFACE_PAINT_TEXT)
                draw_text_clipped(
                    &surface_context, bounds.x, bounds.y, command->text,
                    bounds.width, command->foreground,
                    command->background);
        }
    if (explorer_window != 0)
        for (uint32_t entry = 0U; entry < explorer_window->entry_count; ++entry)
            render_explorer_entry(context, explorer_window, client, entry);
    if (explorer_window != 0 && explorer_window->truncated &&
        client.height > display->font_height + 4U)
        draw_text_clipped(
            context, client.x + 4,
            client.y + (int32_t)client.height -
                (int32_t)display->font_height - 3,
            "Weitere Eintraege nicht angezeigt", client.width - 8U,
            color_shadow, color_client);
    render_resize_grip(context, window);
}

static void render_menu_bar(const desktop_render_context_t *context,
                            const desktop_ui_state_t *ui) {
    const x86os_display_info_t *display = context->display;
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    desktop_rect_t bar = desktop_rect_from_gui(layout.bar);
    draw_bevel(context, bar, color_face, 1U);
    for (uint32_t index = 0U; index < desktop_menu_model.menu_count;
         ++index) {
        reist_gui_rect_t gui_title;
        if (reist_gui_menu_title_rect(
                &desktop_menu_model, &layout, index, &gui_title) != 0)
            continue;
        desktop_rect_t title = desktop_rect_from_gui(gui_title);
        uint32_t active = ui != 0 && ui->menu.open_menu == index;
        uint32_t background = active ? color_active : color_face;
        uint32_t foreground = active ? color_title_text : color_text;
        if (active) fill_rect_clipped(context, title, background);
        uint32_t text_y = title.height > display->font_height
            ? (title.height - display->font_height) / 2U : 0U;
        draw_text_clipped(
            context,
            title.x + (int32_t)layout.title_padding_x,
            title.y + (int32_t)text_y,
            desktop_menu_model.menus[index].label,
            title.width > layout.title_padding_x * 2U
                ? title.width - layout.title_padding_x * 2U : 1U,
            foreground, background);
    }
}

static void render_menu_popup(const desktop_render_context_t *context,
                              const desktop_wm_t *manager,
                              const desktop_ui_state_t *ui) {
    if (ui == 0 || ui->menu.open_menu == REIST_GUI_MENU_NO_INDEX ||
        ui->menu.open_menu >= desktop_menu_model.menu_count) return;
    const x86os_display_info_t *display = context->display;
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    uint32_t menu_index = ui->menu.open_menu;
    reist_gui_rect_t gui_popup;
    if (reist_gui_menu_popup_rect(
            &desktop_menu_model, &layout, menu_index, &gui_popup) != 0)
        return;
    desktop_rect_t popup = desktop_rect_from_gui(gui_popup);
    /* Popup and shadow are composed after all ordinary windows. */
    fill_rect_clipped(
        context,
        (desktop_rect_t){popup.x + 4, popup.y + 4,
                         popup.width, popup.height},
        color_dark);
    draw_bevel(context, popup, color_face, 1U);

    const reist_gui_menu_t *menu_model =
        &desktop_menu_model.menus[menu_index];
    for (uint32_t item_index = 0U;
         item_index < menu_model->item_count; ++item_index) {
        reist_gui_rect_t gui_item;
        if (reist_gui_menu_item_rect(
                &desktop_menu_model, &layout, menu_index,
                item_index, &gui_item) != 0)
            continue;
        desktop_rect_t item = desktop_rect_from_gui(gui_item);
        const reist_gui_menu_item_t *model_item =
            &menu_model->items[item_index];
        uint32_t disabled =
            (model_item->flags & REIST_GUI_MENU_ITEM_DISABLED) != 0U;
        uint32_t hot = !disabled && ui->menu.hot_item == item_index;
        uint32_t pressed = hot &&
            ui->menu.capture_kind == REIST_GUI_MENU_CAPTURE_ITEM &&
            ui->menu.capture_menu == menu_index &&
            ui->menu.capture_item == item_index;
        uint32_t background = hot ? color_active : color_face;
        uint32_t foreground = disabled
            ? color_shadow : (hot ? color_title_text : color_text);
        if (hot) {
            fill_rect_clipped(context, item, background);
            if (pressed) draw_bevel(context, item, background, 0U);
        }
        uint32_t text_y = item.height > display->font_height
            ? (item.height - display->font_height) / 2U : 0U;
        int32_t marker_x = item.x + (int32_t)layout.item_padding_x;
        int32_t label_x = marker_x + (int32_t)(display->font_width * 2U);
        if (menu_index == DESKTOP_MENU_WINDOWS &&
            model_item->target < DESKTOP_WM_CAPACITY &&
            manager->windows[model_item->target].visible != 0U) {
            const char *marker = manager->keyboard_focus ==
                    (int32_t)model_item->target ? "*" : "+";
            draw_text_clipped(
                context, marker_x, item.y + (int32_t)text_y, marker,
                display->font_width, foreground, background);
        }
        uint32_t used = layout.item_padding_x * 2U +
                        display->font_width * 2U;
        draw_text_clipped(
            context, label_x, item.y + (int32_t)text_y,
            model_item->label,
            item.width > used ? item.width - used : 1U,
            foreground, background);
    }
}

static void render_system_dialog(const desktop_render_context_t *context,
                                 const desktop_ui_state_t *ui) {
    if (ui == 0 || !ui->dialog.visible) return;
    const x86os_display_info_t *display = context->display;
    const reist_gui_dialog_model_t *model =
        desktop_dialog_model(ui, ui->dialog_kind);
    reist_gui_dialog_layout_t layout = desktop_dialog_layout(display);
    reist_gui_rect_t gui_dialog;
    reist_gui_rect_t gui_title;
    reist_gui_rect_t gui_close;
    if (model == 0 || reist_gui_dialog_frame_rect(
            model, &layout, &ui->dialog, &gui_dialog) != 0 ||
        reist_gui_dialog_title_rect(
            model, &layout, &ui->dialog, &gui_title) != 0 ||
        reist_gui_dialog_close_rect(
            model, &layout, &ui->dialog, &gui_close) != 0)
        return;
    desktop_rect_t dialog = desktop_rect_from_gui(gui_dialog);
    desktop_rect_t title = desktop_rect_from_gui(gui_title);
    desktop_rect_t close = desktop_rect_from_gui(gui_close);
    /* Dialogs remain the final scene layer below the hardware pointer. */
    fill_rect_clipped(
        context,
        (desktop_rect_t){dialog.x + 4, dialog.y + 4,
                         dialog.width, dialog.height},
        color_dark);
    draw_bevel(context, dialog, color_face, 1U);
    uint32_t title_color = ui->dialog.active
        ? color_active : color_inactive;
    fill_rect_clipped(context, title, title_color);
    draw_bevel(
        context, close, color_face,
        ui->dialog.capture_kind != REIST_GUI_DIALOG_CAPTURE_CLOSE);
    if (close.width > 8U && close.height > 8U) {
        fill_rect_clipped(
            context,
            (desktop_rect_t){close.x + 4, close.y + 4,
                             close.width - 8U, close.height - 8U},
            color_dark);
    }
    uint32_t title_offset = close.width + 8U;
    uint32_t title_y = title.height > display->font_height
        ? (title.height - display->font_height) / 2U : 0U;
    draw_text_clipped(
        context, title.x + (int32_t)title_offset,
        title.y + (int32_t)title_y, model->title,
        title.width > title_offset + 4U
            ? title.width - title_offset - 4U : 1U,
        color_title_text, title_color);

    uint32_t padding = 14U;
    uint32_t line = max_u32(display->font_height + 6U, 18U);
    int32_t text_x = dialog.x + (int32_t)padding;
    int32_t text_y = title.y + (int32_t)title.height + 12;
    uint32_t text_width = dialog.width > padding * 2U
        ? dialog.width - padding * 2U : 1U;
    reist_gui_rect_t first_button;
    if (reist_gui_dialog_button_rect(
            model, &layout, &ui->dialog, 0U, &first_button) == 0) {
        draw_text_clipped(
            context, text_x, text_y, model->message, text_width,
            color_text, color_face);
        if (model->detail != 0 &&
            (int64_t)text_y + line + display->font_height <=
                first_button.y - 6)
            draw_text_clipped(
                context, text_x, text_y + (int32_t)line,
                model->detail, text_width, color_shadow, color_face);
    }

    for (uint32_t index = 0U; index < model->button_count; ++index) {
        reist_gui_rect_t gui_button;
        if (reist_gui_dialog_button_rect(
                model, &layout, &ui->dialog, index, &gui_button) != 0)
            continue;
        desktop_rect_t button = desktop_rect_from_gui(gui_button);
        uint32_t pressed =
            ui->dialog.capture_kind == REIST_GUI_DIALOG_CAPTURE_BUTTON &&
            ui->dialog.capture_button == index &&
            ui->dialog.hot_button == index;
        if (ui->dialog.focused_button == index) {
            desktop_rect_t focus = {
                button.x - 2, button.y - 2,
                button.width + 4U, button.height + 4U,
            };
            draw_bevel(context, focus, color_dark, 0U);
        }
        draw_bevel(context, button, color_face, !pressed);
        size_t label_length = bounded_text_length(
            model->buttons[index].label, REIST_GUI_DIALOG_LABEL_LIMIT);
        uint64_t measured = (uint64_t)label_length * display->font_width;
        uint32_t label_width = measured > UINT32_MAX
            ? UINT32_MAX : (uint32_t)measured;
        int32_t label_x = button.x + (int32_t)((button.width > label_width
            ? button.width - label_width : 0U) / 2U);
        int32_t label_y = button.y + (int32_t)((
            button.height > display->font_height
                ? button.height - display->font_height : 0U) / 2U);
        uint32_t disabled =
            (model->buttons[index].flags &
             REIST_GUI_DIALOG_BUTTON_DISABLED) != 0U;
        draw_text_clipped(
            context, label_x, label_y, model->buttons[index].label,
            button.width, disabled ? color_shadow : color_text, color_face);
    }
}

static void render_desktop_clip(const desktop_render_context_t *context,
                                const desktop_wm_t *manager,
                                const desktop_explorer_t *explorer,
                                const desktop_surface_manager_t *surfaces,
                                const desktop_ui_state_t *ui) {
    const x86os_display_info_t *display = context->display;
    uint32_t status = status_height(display);
    desktop_rect_t status_rect = {
        0, (int32_t)(display->height - status), display->width, status
    };

    fill_rect_clipped(
        context, (desktop_rect_t){0, 0, display->width, display->height},
        color_desktop);
    render_menu_bar(context, ui);

    for (uint32_t index = 0U; index < DESKTOP_ICON_COUNT; ++index)
        render_icon(context, explorer, index);

    for (uint32_t position = 0U; position < DESKTOP_WM_CAPACITY; ++position) {
        uint32_t window_index = manager->z_order[position];
        if (window_index < DESKTOP_WM_CAPACITY &&
            (context->omitted_kind != DESKTOP_MOVE_CACHE_WINDOW ||
             context->omitted_window != window_index))
            render_window(context, manager, explorer, surfaces, window_index);
    }

    draw_bevel(context, status_rect, color_face, 0U);
    draw_text_clipped(
        context, 10,
        status_rect.y +
            (int32_t)((status - display->font_height) / 2U),
        "Menue: Klick/Pfeile/ENTER   Fenster: Ziehen/Groesse   ESC: Zurueck/Shell",
        display->width > 20U ? display->width - 20U : 1U,
        color_text, color_face);
    render_menu_popup(context, manager, ui);
    if (context->omitted_kind != DESKTOP_MOVE_CACHE_DIALOG)
        render_system_dialog(context, ui);
}

static void render_dirty_regions(const x86os_display_info_t *display,
                                 const desktop_wm_t *manager,
                                 const desktop_explorer_t *explorer,
                                 const desktop_surface_manager_t *surfaces,
                                 const desktop_ui_state_t *ui,
                                 const desktop_dirty_region_t *dirty,
                                 uint32_t omitted_kind,
                                 uint32_t omitted_window) {
    if (display == 0 || manager == 0 || dirty == 0) return;
    for (uint32_t index = 0U; index < dirty->count; ++index) {
        desktop_render_context_t context = {
            .display = display,
            /* Every primitive, including glyph foreground and background,
             * is clipped to this exact invalid region. Nothing can leak over
             * a damage edge and violate the scene's z-order. */
            .clip = dirty->rects[index],
            .omitted_kind = omitted_kind,
            .omitted_window = omitted_window,
        };
        if (context.clip.width != 0U && context.clip.height != 0U)
            render_desktop_clip(&context, manager, explorer, surfaces, ui);
    }
}

static void render_desktop(const x86os_display_info_t *display,
                           const desktop_wm_t *manager,
                           const desktop_explorer_t *explorer,
                           const desktop_surface_manager_t *surfaces,
                           const desktop_ui_state_t *ui,
                           const desktop_dirty_region_t *dirty) {
    render_dirty_regions(
        display, manager, explorer, surfaces, ui, dirty,
        DESKTOP_MOVE_CACHE_NONE, DESKTOP_WM_NO_TARGET);
}

static uint32_t render_desktop_frame(const x86os_display_info_t *display,
                                     const desktop_wm_t *manager,
                                     const desktop_explorer_t *explorer,
                                     const desktop_surface_manager_t *surfaces,
                                     const desktop_ui_state_t *ui,
                                     const desktop_dirty_region_t *dirty) {
    if (dirty == 0 || dirty->count == 0U) return 0U;
    uint32_t serial = 0U;
    int begin = x86os_display_frame_begin(&serial);
    if (begin != 0) {
        /* Oversized/direct framebuffers retain the compatible immediate path. */
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return 1U;
    }
    render_desktop(display, manager, explorer, surfaces, ui, dirty);
    if (x86os_display_frame_commit(serial) != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return 1U;
    }
    return 0U;
}

static uint32_t render_desktop_cached_move_frame(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces,
    const desktop_ui_state_t *ui,
    const desktop_dirty_region_t *dirty,
    const desktop_move_cache_t *move) {
    if (move == 0 || !move->valid)
        return render_desktop_frame(display, manager, explorer, surfaces, ui, dirty);
    if (move->kind != DESKTOP_MOVE_CACHE_DIALOG &&
        (move->kind != DESKTOP_MOVE_CACHE_WINDOW ||
         move->window_index >= DESKTOP_WM_CAPACITY))
        return render_desktop_frame(display, manager, explorer, surfaces, ui, dirty);

    uint32_t serial = 0U;
    int begin = x86os_display_frame_begin(&serial);
    if (begin != 0) {
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return 1U;
    }
    int staged = x86os_display_frame_stage_blit(
        serial, (uint32_t)move->source.x, (uint32_t)move->source.y,
        (uint32_t)move->destination.x,
        (uint32_t)move->destination.y,
        move->source.width, move->source.height);
    if (staged != 0) {
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
    } else {
        desktop_dirty_region_t cleanup;
        desktop_dirty_initialize(
            &cleanup, display->width, display->height);
        desktop_dirty_add(&cleanup, move->source);
        render_dirty_regions(
            display, manager, explorer, surfaces, ui, &cleanup,
            move->kind, move->window_index);
    }
    if (x86os_display_frame_commit(serial) != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return 1U;
    }
    return 0U;
}

static void record_render_metrics(desktop_render_metrics_t *metrics,
                                  const desktop_dirty_region_t *dirty,
                                  uint32_t drag, uint32_t resize,
                                  uint32_t fallback,
                                  uint32_t clock_valid,
                                  uint32_t elapsed_ms) {
    if (metrics == 0 || dirty == 0) return;
    metrics->damage_regions = saturating_add_u32(
        metrics->damage_regions, dirty->count);
    if (dirty->count > metrics->damage_max)
        metrics->damage_max = dirty->count;
    if (fallback) saturating_increment(&metrics->fallback_frames);

    uint32_t *frames = dirty->full
        ? &metrics->full_frames : &metrics->dirty_frames;
    uint32_t *total = dirty->full
        ? &metrics->full_total_ms : &metrics->dirty_total_ms;
    uint32_t *maximum = dirty->full
        ? &metrics->full_max_ms : &metrics->dirty_max_ms;
    saturating_increment(frames);
    if (drag) saturating_increment(&metrics->drag_frames);
    if (resize) saturating_increment(&metrics->resize_frames);
    if (!clock_valid) {
        saturating_increment(&metrics->clock_errors);
        return;
    }
    *total = saturating_add_u32(*total, elapsed_ms);
    if (elapsed_ms > *maximum) *maximum = elapsed_ms;
    if (drag) {
        metrics->drag_total_ms = saturating_add_u32(
            metrics->drag_total_ms, elapsed_ms);
        if (elapsed_ms > metrics->drag_max_ms)
            metrics->drag_max_ms = elapsed_ms;
    }
    if (resize) {
        metrics->resize_total_ms = saturating_add_u32(
            metrics->resize_total_ms, elapsed_ms);
        if (elapsed_ms > metrics->resize_max_ms)
            metrics->resize_max_ms = elapsed_ms;
    }
}

static void render_desktop_measured(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces,
    const desktop_ui_state_t *ui,
    const desktop_dirty_region_t *dirty,
    const desktop_move_cache_t *move_cache,
    uint32_t drag, uint32_t resize, desktop_render_metrics_t *metrics) {
    if (dirty == 0 || dirty->count == 0U) return;
    uint64_t started_ms = 0U;
    uint64_t finished_ms = 0U;
    uint32_t clock_valid = x86os_monotonic_ms(&started_ms) == 0;
    uint32_t fallback = render_desktop_cached_move_frame(
        display, manager, explorer, surfaces, ui, dirty, move_cache);
    if (!clock_valid || x86os_monotonic_ms(&finished_ms) != 0 ||
        finished_ms < started_ms) {
        clock_valid = 0U;
    }
    uint64_t elapsed = clock_valid ? finished_ms - started_ms : 0U;
    uint32_t elapsed_ms = elapsed > UINT32_MAX
        ? UINT32_MAX : (uint32_t)elapsed;
    record_render_metrics(metrics, dirty, drag, resize, fallback,
                          clock_valid, elapsed_ms);
}

static int read_escape_byte(void) {
    for (unsigned int attempt = 0U; attempt < 20U; ++attempt) {
        int value = x86os_getchar_nonblocking();
        if (value != 0) return value;
        (void)x86os_sleep_ms(1U);
    }
    return 0;
}

static int read_key(void) {
    int value = x86os_getchar_nonblocking();
    if (value == 0) return DESKTOP_KEY_NONE;
    if (value != 0x1B) return value;

    int prefix = read_escape_byte();
    if (prefix == 0) return DESKTOP_KEY_ESCAPE;
    if (prefix != '[') return DESKTOP_KEY_NONE;

    /* Consume a complete ANSI CSI sequence. Only a bare Escape exits. */
    for (unsigned int byte = 0U; byte < 16U; ++byte) {
        value = read_escape_byte();
        if (value == 0) return DESKTOP_KEY_NONE;
        if (value < 0x40 || value > 0x7E) continue;
        if (value == 'A') return DESKTOP_KEY_UP;
        if (value == 'B') return DESKTOP_KEY_DOWN;
        if (value == 'C') return DESKTOP_KEY_RIGHT;
        if (value == 'D') return DESKTOP_KEY_LEFT;
        return DESKTOP_KEY_NONE;
    }
    return DESKTOP_KEY_NONE;
}

#define DESKTOP_SURFACE_CONTENT_TAG 0x80000000U

static uint32_t surface_uses_window(
    const desktop_surface_manager_t *surfaces, uint32_t window_index) {
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index)
        if (surfaces->slots[index].active &&
            surfaces->slots[index].window_index == window_index)
            return 1U;
    return 0U;
}

static desktop_surface_slot_t *surface_for_window(
    desktop_surface_manager_t *surfaces, uint32_t window_index) {
    if (surfaces == 0 || window_index >= DESKTOP_WM_CAPACITY) return 0;
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index)
        if (surfaces->slots[index].active &&
            surfaces->slots[index].window_index == window_index)
            return &surfaces->slots[index];
    return 0;
}

static uint32_t next_surface_input_serial(void) {
    static uint32_t serial;
    ++serial;
    if (serial == 0U) ++serial;
    return serial;
}

static uint32_t enqueue_surface_pointer(
    const desktop_wm_t *manager, desktop_surface_manager_t *surfaces,
    int32_t window_index, uint32_t type, int32_t pointer_x,
    int32_t pointer_y, int32_t delta_x, int32_t delta_y,
    uint32_t pressed, uint32_t allow_outside) {
    if (manager == 0 || surfaces == 0 || window_index < 0 ||
        window_index >= (int32_t)DESKTOP_WM_CAPACITY) return 0U;
    desktop_surface_slot_t *surface = surface_for_window(
        surfaces, (uint32_t)window_index);
    if (surface == 0) return 0U;
    desktop_rect_t client = desktop_window_client_rect(
        manager, (uint32_t)window_index);
    if (!allow_outside &&
        (pointer_x < client.x || pointer_y < client.y ||
        pointer_x >= client.x + (int32_t)client.width ||
        pointer_y >= client.y + (int32_t)client.height))
        return 0U;
    int32_t local_x = pointer_x - client.x;
    int32_t local_y = pointer_y - client.y;
    if (local_x < 0) local_x = 0;
    if (local_y < 0) local_y = 0;
    if (local_x >= (int32_t)client.width)
        local_x = (int32_t)client.width - 1;
    if (local_y >= (int32_t)client.height)
        local_y = (int32_t)client.height - 1;
    reist_gui_surface_input_t event = {
        type, next_surface_input_serial(),
        local_x, local_y,
        delta_x, delta_y,
        type == REIST_GUI_SURFACE_INPUT_POINTER_BUTTON ? 1U : 0U,
        pressed, 0U, 0U,
    };
    return desktop_surface_input_enqueue(
        surfaces, surface->owner, surface->handle, &event) == 0;
}

static uint32_t enqueue_surface_keyboard(
    const desktop_wm_t *manager, desktop_surface_manager_t *surfaces,
    int key) {
    if (manager == 0 || surfaces == 0 || key == DESKTOP_KEY_NONE ||
        manager->keyboard_focus < 0 ||
        manager->keyboard_focus >= (int32_t)DESKTOP_WM_CAPACITY)
        return 0U;
    desktop_surface_slot_t *surface = surface_for_window(
        surfaces, (uint32_t)manager->keyboard_focus);
    if (surface == 0) return 0U;
    reist_gui_surface_input_t event = {
        REIST_GUI_SURFACE_INPUT_KEYBOARD, next_surface_input_serial(),
        0, 0, 0, 0, 0U, 1U, (uint32_t)key, 0U,
    };
    return desktop_surface_input_enqueue(
        surfaces, surface->owner, surface->handle, &event) == 0;
}

/** Publish acknowledged Ring-3 surfaces as ordinary server-decorated windows. */
static void sync_surface_windows(
    desktop_wm_t *manager, const desktop_explorer_t *explorer,
    desktop_surface_manager_t *surfaces,
    desktop_surface_runtime_t *runtime,
    desktop_dirty_region_t *dirty) {
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        desktop_window_t *window = &manager->windows[index];
        if ((window->content_id & DESKTOP_SURFACE_CONTENT_TAG) != 0U &&
            !surface_uses_window(surfaces, index)) {
            (void)desktop_wm_close(manager, index);
            window->content_id = index;
            desktop_dirty_full(dirty);
        }
    }
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index) {
        desktop_surface_slot_t *surface = &surfaces->slots[index];
        if (!surface->active) continue;
        if (surface->window_index != DESKTOP_SURFACE_NO_SLOT) {
            if (surface->window_index < DESKTOP_WM_CAPACITY &&
                manager->windows[surface->window_index].visible &&
                !surface->close_sent) {
                desktop_rect_t client = desktop_window_client_rect(
                    manager, surface->window_index);
                if (surface->acknowledged_serial ==
                        surface->configured_serial &&
                    (surface->width != client.width ||
                     surface->height != client.height)) {
                    reist_gui_surface_configure_t configure;
                    (void)desktop_surface_reconfigure(
                        surfaces, surface->owner, surface->handle,
                        client.width, client.height, &configure);
                }
                if (surface->acknowledged_serial !=
                        surface->configured_serial &&
                    !surface->configure_sent) {
                    reist_gui_surface_configure_t configure = {
                        surface->configured_serial, surface->width,
                        surface->height, 0U, 0U,
                    };
                    if (desktop_surface_runtime_send_configure(
                            runtime, surface->owner, surface->handle,
                            &configure) == 0)
                        surface->configure_sent = 1U;
                }
            }
            if (surface->paint_generation !=
                surface->presented_generation &&
                surface->window_index < DESKTOP_WM_CAPACITY) {
                desktop_dirty_add(
                    dirty, desktop_wm_window_bounds(
                        manager, surface->window_index));
                surface->presented_generation = surface->paint_generation;
            }
            if (surface->window_index < DESKTOP_WM_CAPACITY &&
                !manager->windows[surface->window_index].visible &&
                !surface->close_sent) {
                if (desktop_surface_runtime_send_close(
                        runtime, surface->owner, surface->handle) == 0) {
                    surface->close_sent = 1U;
                    /* Closing a client surface is negotiated. Keep the
                     * window visible while the application presents a save
                     * confirmation, and remove it only after DESTROY or
                     * process revocation. */
                    (void)desktop_wm_open(
                        manager, surface->window_index);
                    surface->close_sent = 0U;
                    desktop_dirty_add(
                        dirty, desktop_wm_window_bounds(
                            manager, surface->window_index));
                }
            }
            continue;
        }
        if (surface->acknowledged_serial == 0U) continue;
        if (surface->close_sent) continue;
        uint32_t chosen = DESKTOP_WM_CAPACITY;
        for (uint32_t candidate = 0U;
             candidate < DESKTOP_WM_CAPACITY; ++candidate) {
            if (!manager->windows[candidate].visible &&
                !explorer->windows[candidate].active &&
                !surface_uses_window(surfaces, candidate)) {
                chosen = candidate;
                break;
            }
        }
        if (chosen == DESKTOP_WM_CAPACITY) continue;
        desktop_window_t *window = &manager->windows[chosen];
        uint32_t available_width = (uint32_t)(
            manager->work_right - manager->work_left);
        uint32_t available_height = (uint32_t)(
            manager->work_bottom - manager->work_top);
        uint32_t decorated_width = surface->width + manager->frame_border * 2U;
        uint32_t decorated_height = surface->height + manager->title_height +
                                    manager->frame_border * 2U;
        if (decorated_width < manager->minimum_width)
            decorated_width = manager->minimum_width;
        if (decorated_height < manager->minimum_height)
            decorated_height = manager->minimum_height;
        if (decorated_width > available_width) decorated_width = available_width;
        if (decorated_height > available_height) decorated_height = available_height;
        window->width = decorated_width;
        window->height = decorated_height;
        window->x = manager->work_left +
            (int32_t)((available_width - decorated_width) / 2U);
        window->y = manager->work_top +
            (int32_t)((available_height - decorated_height) / 2U);
        window->content_id = DESKTOP_SURFACE_CONTENT_TAG | surface->handle.id;
        surface->window_index = chosen;
        (void)desktop_wm_open(manager, chosen);
        desktop_dirty_full(dirty);
    }
}

static void print_unsigned(uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_metric(const char *name, uint32_t value) {
    x86os_putchar(' ');
    x86os_puts(name);
    x86os_putchar('=');
    print_unsigned(value);
}

static void print_render_metrics(const desktop_render_metrics_t *metrics) {
    if (metrics == 0) return;
    x86os_puts("DESKTOP_METRICS");
    print_metric("version", DESKTOP_METRICS_VERSION);
    print_metric("full_frames", metrics->full_frames);
    print_metric("full_total_ms", metrics->full_total_ms);
    print_metric("full_max_ms", metrics->full_max_ms);
    print_metric("dirty_frames", metrics->dirty_frames);
    print_metric("dirty_total_ms", metrics->dirty_total_ms);
    print_metric("dirty_max_ms", metrics->dirty_max_ms);
    print_metric("drag_frames", metrics->drag_frames);
    print_metric("drag_total_ms", metrics->drag_total_ms);
    print_metric("drag_max_ms", metrics->drag_max_ms);
    print_metric("resize_frames", metrics->resize_frames);
    print_metric("resize_total_ms", metrics->resize_total_ms);
    print_metric("resize_max_ms", metrics->resize_max_ms);
    print_metric("fallback_frames", metrics->fallback_frames);
    print_metric("damage_regions", metrics->damage_regions);
    print_metric("damage_max", metrics->damage_max);
    print_metric("clock_errors", metrics->clock_errors);
    print_metric("probe_errors", metrics->probe_errors);
    x86os_putchar('\n');
}

static uint32_t desktop_try_exit(
    int32_t pointer_x, int32_t pointer_y, uint32_t runtime_activated,
    const desktop_render_metrics_t *metrics) {
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    if (runtime_activated && x86os_display_deactivate() != 0) {
        x86os_puts("desktop: VGA-Rueckkehr fehlgeschlagen\n");
        (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        return 0U;
    }
    if (!runtime_activated) x86os_clear();
    print_render_metrics(metrics);
    x86os_puts("DESKTOP_EXIT_OK\n");
    return 1U;
}

static char filetypes_config[DESKTOP_FILETYPES_CONFIG_CAPACITY + 1U];
/* The syscall copies argv synchronously, but keeping the launch handoff in
 * fixed application storage avoids exposing deep compositor stack frames as
 * cross-boundary string/vector inputs. The desktop event loop is serialized,
 * so exactly one launch transaction can own these buffers at a time. */
static char launch_program_path[DESKTOP_FILETYPES_PROGRAM_CAPACITY];
static char launch_document_path[DESKTOP_EXPLORER_PATH_CAPACITY];
static char launch_surface_argument[40];
static const char *launch_arguments[3];

static int copy_launch_text(char *destination, uint32_t capacity,
                            const char *source) {
    if (destination == 0 || source == 0 || capacity == 0U) return -22;
    uint32_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    if (source[index] != '\0') return -36;
    destination[index] = '\0';
    return 0;
}

static int load_filetypes(desktop_filetypes_t *filetypes) {
    desktop_filetypes_initialize(filetypes);
    int descriptor = x86os_open("/etc/reist/filetypes.conf");
    if (descriptor < 0) return -1;
    size_t used = 0U;
    while (used < DESKTOP_FILETYPES_CONFIG_CAPACITY) {
        int amount = x86os_read(
            descriptor, filetypes_config + used,
            DESKTOP_FILETYPES_CONFIG_CAPACITY - used);
        if (amount < 0) {
            (void)x86os_close(descriptor);
            return -1;
        }
        if (amount == 0) break;
        used += (size_t)amount;
    }
    char extra;
    int extra_read = x86os_read(descriptor, &extra, 1U);
    int close_status = x86os_close(descriptor);
    if (extra_read != 0 || close_status < 0 || used == 0U) return -1;
    return desktop_filetypes_parse(filetypes, filetypes_config, used) == 0
        ? 0 : -1;
}

static int format_surface_argument(x86os_ipc_handle_t endpoint) {
    static const char prefix[] = "--reist-surface=";
    uint32_t used = 0U;
    while (prefix[used] != '\0') {
        launch_surface_argument[used] = prefix[used];
        ++used;
    }
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + endpoint % 10U);
        endpoint /= 10U;
    } while (endpoint != 0U && count < sizeof(digits));
    if (endpoint != 0U || used + count >= sizeof(launch_surface_argument))
        return -36;
    while (count != 0U) launch_surface_argument[used++] = digits[--count];
    launch_surface_argument[used] = '\0';
    return 0;
}

static uint32_t program_uses_surface(const char *program) {
    return path_equal_ascii_case(program, "/usr/gui/bin/surfacedemo.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/notepad.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/imageviewer.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/control.prg");
}

static int launch_program(desktop_surface_runtime_t *surface_runtime,
                          const char *program, const char *document) {
    int status = 0;
    int pid;
    if (program == 0 || program[0] == '\0') return -22;
    int copy_status = copy_launch_text(
        launch_program_path, sizeof(launch_program_path), program);
    if (copy_status != 0) return copy_status;
    if (surface_runtime != 0 && program_uses_surface(program)) {
        x86os_ipc_handle_t endpoint = 0U;
        int reserve = desktop_surface_runtime_reserve(
            surface_runtime, &endpoint);
        if (reserve != 0) return reserve;
        int formatted = format_surface_argument(endpoint);
        if (formatted != 0) {
            desktop_surface_runtime_cancel(surface_runtime, endpoint);
            return formatted;
        }
        launch_arguments[0] = launch_program_path;
        launch_arguments[1] = launch_surface_argument;
        int argument_count = 2;
        if (document != 0) {
            copy_status = copy_launch_text(
                launch_document_path, sizeof(launch_document_path), document);
            if (copy_status != 0) {
                desktop_surface_runtime_cancel(surface_runtime, endpoint);
                return copy_status;
            }
            launch_arguments[2] = launch_document_path;
            argument_count = 3;
        }
        pid = x86os_spawnv(
            launch_program_path, argument_count, launch_arguments);
        if (pid < 0) {
            desktop_surface_runtime_cancel(surface_runtime, endpoint);
            return pid;
        }
        int bound = desktop_surface_runtime_bind(
            surface_runtime, endpoint, pid);
        if (bound != 0) {
            (void)x86os_kill(pid);
            (void)x86os_wait(pid, &status);
            desktop_surface_runtime_cancel(surface_runtime, endpoint);
            return bound;
        }
        return 0;
    }
    /* Legacy full-screen clients remain synchronous until migrated to the
     * Surface ABI. */
    if (document != 0) {
        copy_status = copy_launch_text(
            launch_document_path, sizeof(launch_document_path), document);
        if (copy_status != 0) return copy_status;
        launch_arguments[0] = launch_program_path;
        launch_arguments[1] = launch_document_path;
        pid = x86os_spawnv(launch_program_path, 2, launch_arguments);
    } else pid = x86os_spawn(launch_program_path);
    if (pid >= 0) {
        int wait_result = x86os_wait(pid, &status);
        if (wait_result != pid) {
            (void)x86os_kill(pid);
            (void)x86os_wait(pid, &status);
            return -5;
        }
    } else {
        return pid;
    }
    return 0;
}

static uint32_t active_surface_count(
    const desktop_surface_manager_t *surfaces) {
    uint32_t count = 0U;
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index)
        if (surfaces->slots[index].active) ++count;
    return count;
}

static int launch_surface_probe_client(
    desktop_surface_runtime_t *runtime,
    desktop_surface_manager_t *surfaces,
    const char *program, const char *argument,
    uint32_t expected_surface_count) {
    int result = launch_program(runtime, program, argument);
    if (result != 0) return result;
    /* The probe starts several clients without user input between launches.
     * Service each new endpoint before launching the next process so a client
     * cannot exhaust its bounded configure timeout behind later spawns. */
    for (uint32_t attempt = 0U; attempt < 250U; ++attempt) {
        result = desktop_surface_runtime_poll(runtime, surfaces);
        if (result != 0) return result;
        if (active_surface_count(surfaces) >= expected_surface_count)
            return 0;
        (void)x86os_sleep_ms(1U);
    }
    return -110;
}

static void clip_pointer(const x86os_display_info_t *display,
                         int32_t *pointer_x, int32_t *pointer_y) {
    if (*pointer_x < 0) *pointer_x = 0;
    if (*pointer_y < 0) *pointer_y = 0;
    if (*pointer_x >= (int32_t)display->width)
        *pointer_x = (int32_t)display->width - 1;
    if (*pointer_y >= (int32_t)display->height)
        *pointer_y = (int32_t)display->height - 1;
}

static void move_pointer(const x86os_display_info_t *display,
                         int32_t *pointer_x, int32_t *pointer_y,
                         int32_t delta_x, int32_t delta_y) {
    int64_t next_x = (int64_t)*pointer_x + delta_x;
    int64_t next_y = (int64_t)*pointer_y + delta_y;
    if (next_x < INT32_MIN) next_x = INT32_MIN;
    if (next_x > INT32_MAX) next_x = INT32_MAX;
    if (next_y < INT32_MIN) next_y = INT32_MIN;
    if (next_y > INT32_MAX) next_y = INT32_MAX;
    *pointer_x = (int32_t)next_x;
    *pointer_y = (int32_t)next_y;
    clip_pointer(display, pointer_x, pointer_y);
}

static uint32_t explorer_key_from_input(int key) {
    if (key == DESKTOP_KEY_LEFT) return DESKTOP_EXPLORER_KEY_LEFT;
    if (key == DESKTOP_KEY_RIGHT) return DESKTOP_EXPLORER_KEY_RIGHT;
    if (key == DESKTOP_KEY_UP) return DESKTOP_EXPLORER_KEY_UP;
    if (key == DESKTOP_KEY_DOWN) return DESKTOP_EXPLORER_KEY_DOWN;
    if (key == '\r' || key == '\n') return DESKTOP_EXPLORER_KEY_ENTER;
    return 0U;
}

static void collect_dispatch_result(
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    const desktop_wm_dispatch_result_t *result) {
    (void)display;
    desktop_dirty_add_regions(dirty, &result->dirty);
}

static uint32_t dispatch_desktop_event(
    desktop_wm_t *manager, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const desktop_wm_event_t *event,
    uint32_t *target) {
    desktop_wm_dispatch_result_t result;
    if (desktop_wm_dispatch(manager, event, &result) != 0) return 0U;
    collect_dispatch_result(display, dirty, &result);
    if ((result.flags & DESKTOP_WM_RESULT_LAUNCH) != 0U && target != 0)
        *target = result.target;
    return result.flags;
}

static uint32_t open_explorer_path(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui,
    const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const char *path, uint32_t *target) {
    uint32_t slot = desktop_explorer_free_window(explorer);
    if (slot >= DESKTOP_WM_CAPACITY) {
        desktop_ui_open_error(
            ui, display, dirty, "Kein weiteres Fenster verfuegbar.", path);
        return 0U;
    }
    if (desktop_explorer_open(explorer, slot, path) !=
        DESKTOP_EXPLORER_OK) {
        desktop_ui_open_error(
            ui, display, dirty, "Ordner kann nicht geoeffnet werden.", path);
        return 0U;
    }
    desktop_wm_event_t open = {
        .type = DESKTOP_WM_EVENT_OPEN,
        .target = slot,
    };
    return dispatch_desktop_event(
        manager, display, dirty, &open, target);
}

static uint32_t close_all_explorer_windows(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, uint32_t *target) {
    uint32_t actions = 0U;
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        if (!explorer->windows[index].active) continue;
        desktop_wm_event_t close = {
            .type = DESKTOP_WM_EVENT_CLOSE,
            .target = index,
        };
        actions |= dispatch_desktop_event(
            manager, display, dirty, &close, target);
        desktop_explorer_close(explorer, index);
    }
    return actions;
}

static uint32_t has_program_extension(const char *path) {
    uint32_t length = 0U;
    if (path == 0) return 0U;
    while (length < DESKTOP_EXPLORER_PATH_CAPACITY && path[length] != '\0')
        ++length;
    if (length < 4U || length == DESKTOP_EXPLORER_PATH_CAPACITY) return 0U;
    const char *extension = &path[length - 4U];
    return extension[0] == '.' &&
        (extension[1] == 'p' || extension[1] == 'P') &&
        (extension[2] == 'r' || extension[2] == 'R') &&
        (extension[3] == 'g' || extension[3] == 'G');
}

static uint32_t apply_desktop_activation(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    const desktop_filetypes_t *filetypes,
    desktop_surface_runtime_t *surface_runtime,
    desktop_ui_state_t *ui,
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    desktop_activation_t *activation, uint32_t *target,
    int32_t pointer_x, int32_t pointer_y) {
    if (activation == 0 || !activation->valid) return 0U;
    activation->valid = 0U;
    if (activation->root)
        return open_explorer_path(
            manager, explorer, ui, display, dirty, "/", target);
    if (activation->window_index >= DESKTOP_WM_CAPACITY ||
        !explorer->windows[activation->window_index].active ||
        activation->entry_index >=
            explorer->windows[activation->window_index].entry_count)
        return 0U;

    desktop_explorer_window_t *window =
        &explorer->windows[activation->window_index];
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    if (desktop_explorer_child_path(
            window, activation->entry_index, path, sizeof(path)) !=
        DESKTOP_EXPLORER_OK) return 0U;
    if (window->entries[activation->entry_index].type == X86OS_DIRECTORY)
        return open_explorer_path(
            manager, explorer, ui, display, dirty, path, target);
    const char *program = path;
    const char *document = 0;
    if (!has_program_extension(path)) {
        if (desktop_filetypes_lookup(filetypes, path, &program) != 0) {
            desktop_ui_open_error(
                ui, display, dirty, "Keine Dateizuordnung vorhanden.", path);
            return 0U;
        }
        document = path;
    }
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    int launch_status = launch_program(surface_runtime, program, document);
    desktop_dirty_full(dirty);
    if (launch_status != 0) {
        const char *message = "Programm konnte nicht gestartet werden.";
        if (launch_status == -2)
            message = "Programmdatei nicht gefunden oder ungueltig.";
        else if (launch_status == -11)
            message = "Kein freier Prozessplatz verfuegbar.";
        else if (launch_status == -12)
            message = "Nicht genug Speicher fuer das Programm.";
        else if (launch_status == -14)
            message = "Programmargumente konnten nicht uebergeben werden.";
        else if (launch_status == -16)
            message = "Kein freier Scheduler-Task verfuegbar.";
        else if (launch_status == -28)
            message = "Keine freie IPC-Ressource verfuegbar.";
        else if (launch_status == -75)
            message = "Surface-Clientkapazitaet ist erschoepft.";
        else if (launch_status == -3)
            message = "Programm endete vor der Surface-Bindung.";
        else if (launch_status == -9)
            message = "Surface-Endpunkt ist beim Besitzer ungueltig (-9).";
        else if (launch_status == -13)
            message = "Surface-Delegation wurde verweigert (-13).";
        else if (launch_status == -22 || launch_status == -36)
            message = "Programmpfad ist ungueltig oder zu lang.";
        desktop_ui_open_error(
            ui, display, dirty, message, path);
    }
    return 0U;
}

static void apply_control_panel_activation(
    desktop_surface_runtime_t *surface_runtime, desktop_ui_state_t *ui,
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    int32_t pointer_x, int32_t pointer_y) {
    static const char path[] = "/usr/gui/bin/control.prg";
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    int launch_status = launch_program(surface_runtime, path, 0);
    desktop_dirty_full(dirty);
    if (launch_status != 0)
        desktop_ui_open_error(
            ui, display, dirty,
            "Systemsteuerung konnte nicht gestartet werden.", path);
}

static uint32_t apply_desktop_ui_result(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui,
    const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const desktop_ui_result_t *ui_result,
    uint32_t *target) {
    if (ui_result == 0) return 0U;
    if (ui_result->action == DESKTOP_UI_ACTION_EXIT)
        return DESKTOP_WM_RESULT_EXIT;
    if (ui_result->action == DESKTOP_UI_ACTION_OPEN_ROOT)
        return open_explorer_path(
            manager, explorer, ui, display, dirty, "/", target);
    if (ui_result->action == DESKTOP_UI_ACTION_CLOSE_ALL)
        return close_all_explorer_windows(
            manager, explorer, display, dirty, target);
    return 0U;
}

static void accumulate_mouse_delta(int32_t *total, int32_t delta) {
    if (total == 0) return;
    int64_t sum = (int64_t)*total + delta;
    if (sum < INT32_MIN) sum = INT32_MIN;
    if (sum > INT32_MAX) sum = INT32_MAX;
    *total = (int32_t)sum;
}

static uint32_t desktop_move_capture_geometry(
    const desktop_wm_t *manager, const desktop_ui_state_t *ui,
    uint32_t *kind, uint32_t *window_index, desktop_rect_t *bounds) {
    if (manager == 0 || ui == 0 || kind == 0 || window_index == 0 ||
        bounds == 0) return 0U;
    if (ui->dialog.visible &&
        ui->dialog.capture_kind == REIST_GUI_DIALOG_CAPTURE_MOVE) {
        if (ui->dialog.bounds.x < 0 || ui->dialog.bounds.y < 0 ||
            ui->dialog.bounds.width > UINT32_MAX - 4U ||
            ui->dialog.bounds.height > UINT32_MAX - 4U) return 0U;
        *kind = DESKTOP_MOVE_CACHE_DIALOG;
        *window_index = DESKTOP_WM_NO_TARGET;
        *bounds = (desktop_rect_t){
            ui->dialog.bounds.x, ui->dialog.bounds.y,
            ui->dialog.bounds.width + 4U,
            ui->dialog.bounds.height + 4U,
        };
        return 1U;
    }
    if (manager->capture_kind != DESKTOP_WM_CAPTURE_MOVE ||
        manager->capture_window < 0 ||
        manager->capture_window >= (int32_t)DESKTOP_WM_CAPACITY)
        return 0U;
    uint32_t index = (uint32_t)manager->capture_window;
    /* A scene-level pixel cache is valid only for an unobscured top layer.
     * Modeless dialogs are composed above ordinary windows, so keep using the
     * general redraw path while one is visible. */
    if (ui->dialog.visible ||
        manager->z_order[DESKTOP_WM_CAPACITY - 1U] != index)
        return 0U;
    desktop_rect_t rect = desktop_wm_window_bounds(manager, index);
    if (rect.x < 0 || rect.y < 0) return 0U;
    *kind = DESKTOP_MOVE_CACHE_WINDOW;
    *window_index = index;
    *bounds = rect;
    return 1U;
}

static void desktop_move_cache_capture(
    desktop_move_cache_t *move, uint32_t kind, uint32_t window_index,
    desktop_rect_t source, desktop_rect_t destination) {
    if (move == 0 || kind == DESKTOP_MOVE_CACHE_NONE ||
        source.x < 0 || source.y < 0 || destination.x < 0 ||
        destination.y < 0 || source.width == 0U || source.height == 0U ||
        source.width != destination.width ||
        source.height != destination.height ||
        (source.x == destination.x && source.y == destination.y)) return;
    move->source = source;
    move->destination = destination;
    move->kind = kind;
    move->window_index = window_index;
    move->valid = 1U;
}

/* Relative USB reports are coalesced until a button edge.  A compositor
 * needs the latest pointer position for the next frame, not every transient
 * position that was queued while the previous frame reached scanout.  Button
 * edges remain strict ordering boundaries, preserving implicit grabs. */
static uint32_t dispatch_pointer_motion(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui,
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    int32_t *pointer_x, int32_t *pointer_y,
    int32_t delta_x, int32_t delta_y, uint32_t *target,
    uint32_t *drag_render, uint32_t *resize_render,
    desktop_move_cache_t *move_cache) {
    if (delta_x == 0 && delta_y == 0) return 0U;
    move_pointer(display, pointer_x, pointer_y, delta_x, delta_y);

    uint32_t move_kind = DESKTOP_MOVE_CACHE_NONE;
    uint32_t move_window = DESKTOP_WM_NO_TARGET;
    desktop_rect_t move_source = {0, 0, 0U, 0U};
    uint32_t can_cache = dirty->count == 0U &&
        desktop_move_capture_geometry(
            manager, ui, &move_kind, &move_window, &move_source);

    if (manager->capture_kind == DESKTOP_WM_CAPTURE_MOVE ||
        ui->dialog.capture_kind == REIST_GUI_DIALOG_CAPTURE_MOVE)
        *drag_render = 1U;
    if (manager->capture_kind == DESKTOP_WM_CAPTURE_RESIZE)
        *resize_render = 1U;

    uint32_t actions = 0U;
    uint32_t ui_motion_consumed = 0U;
    if (manager->capture_kind == DESKTOP_WM_CAPTURE_NONE) {
        desktop_ui_result_t ui_motion = desktop_ui_pointer_event(
            ui, display, dirty, *pointer_x, *pointer_y, 0U, 0U);
        ui_motion_consumed = ui_motion.consumed;
        actions |= apply_desktop_ui_result(
            manager, explorer, ui, display, dirty, &ui_motion, target);
    }
    if (!ui_motion_consumed) {
        desktop_wm_event_t motion = {
            .type = DESKTOP_WM_EVENT_POINTER_MOTION,
            .x = *pointer_x,
            .y = *pointer_y,
        };
        actions |= dispatch_desktop_event(
            manager, display, dirty, &motion, target);
    }
    if (can_cache) {
        uint32_t destination_kind = DESKTOP_MOVE_CACHE_NONE;
        uint32_t destination_window = DESKTOP_WM_NO_TARGET;
        desktop_rect_t destination = {0, 0, 0U, 0U};
        if (desktop_move_capture_geometry(
                manager, ui, &destination_kind, &destination_window,
                &destination) && destination_kind == move_kind &&
            destination_window == move_window) {
            desktop_move_cache_capture(
                move_cache, move_kind, move_window,
                move_source, destination);
        }
    }
    return actions;
}

static void collect_explorer_pointer_result(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    desktop_dirty_region_t *dirty,
    const desktop_explorer_result_t *result, uint32_t root,
    desktop_activation_t *activation) {
    if (result == 0) return;
    if (result->selection_changed) {
        if (root) {
            desktop_dirty_add(dirty, desktop_icon_rect(display, 0U));
        } else if (result->window_index < DESKTOP_WM_CAPACITY) {
            desktop_dirty_add(
                dirty, desktop_wm_window_bounds(
                    manager, result->window_index));
        }
    }
    if (!result->activated || activation == 0 || activation->valid) return;
    activation->valid = 1U;
    activation->root = root;
    activation->window_index = result->window_index;
    activation->entry_index = result->entry_index;
}

static uint32_t control_panel_pointer_button(
    desktop_explorer_t *explorer, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t pointer_x, int32_t pointer_y,
    uint32_t buttons, uint32_t previous_buttons) {
    uint32_t left_down =
        (buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    uint32_t left_was_down =
        (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    uint32_t hit = desktop_icon_at_position(
        display, pointer_x, pointer_y) == 1;
    if (left_down && !left_was_down) {
        uint32_t old_selected = control_panel_selected;
        control_panel_selected = hit;
        control_panel_pressed = hit;
        if (hit && explorer != 0) explorer->desktop_selected = 0U;
        if (old_selected != control_panel_selected || hit)
            desktop_dirty_add(dirty, desktop_icon_rect(display, 1U));
        return 0U;
    }
    if (!left_down && left_was_down) {
        uint32_t activate = 0U;
        if (control_panel_pressed && hit) {
            uint64_t now_ms = 0U;
            if (x86os_monotonic_ms(&now_ms) == 0 &&
                control_panel_last_click_ms != 0U &&
                now_ms >= control_panel_last_click_ms &&
                now_ms - control_panel_last_click_ms <=
                    DESKTOP_EXPLORER_DOUBLE_CLICK_MS)
                activate = 1U;
            control_panel_last_click_ms = now_ms;
        }
        control_panel_pressed = 0U;
        return activate;
    }
    return 0U;
}

static uint32_t dispatch_pointer_button(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui,
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    int32_t pointer_x, int32_t pointer_y, uint32_t buttons,
    uint32_t previous_buttons, uint32_t *target,
    desktop_activation_t *activation) {
    uint32_t actions = 0U;
    uint32_t left_down =
        (buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    uint32_t left_was_down =
        (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    if (left_down && !left_was_down) {
        uint32_t ui_press_consumed = 0U;
        if (manager->capture_kind == DESKTOP_WM_CAPTURE_NONE) {
            desktop_ui_result_t ui_press = desktop_ui_pointer_event(
                ui, display, dirty, pointer_x, pointer_y, 1U, 1U);
            ui_press_consumed = ui_press.consumed;
            actions |= apply_desktop_ui_result(
                manager, explorer, ui, display, dirty, &ui_press, target);
        }
        if (!ui_press_consumed) {
            int window = desktop_wm_window_at(
                manager, pointer_x, pointer_y);
            desktop_wm_event_t press = {
                .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
                .x = pointer_x,
                .y = pointer_y,
                .button = DESKTOP_WM_BUTTON_LEFT,
                .pressed = 1U,
            };
            actions |= dispatch_desktop_event(
                manager, display, dirty, &press, target);
            desktop_explorer_result_t explorer_result;
            desktop_explorer_result_initialize(&explorer_result);
            if (window >= 0 &&
                manager->capture_kind == DESKTOP_WM_CAPTURE_CLIENT &&
                manager->capture_window == window) {
                (void)desktop_explorer_pointer_press(
                    explorer, (uint32_t)window,
                    desktop_window_client_rect(manager, (uint32_t)window),
                    pointer_x, pointer_y, &explorer_result);
                collect_explorer_pointer_result(
                    display, manager, dirty, &explorer_result, 0U,
                    activation);
            } else if (window == DESKTOP_WM_NO_WINDOW) {
                uint32_t hit = desktop_icon_at_position(
                    display, pointer_x, pointer_y) == 0;
                desktop_explorer_desktop_press(
                    explorer, hit, &explorer_result);
                collect_explorer_pointer_result(
                    display, manager, dirty, &explorer_result, 1U,
                    activation);
            }
        }
    } else if (!left_down && left_was_down) {
        uint32_t captured_kind = manager->capture_kind;
        int32_t captured_window = manager->capture_window;
        uint32_t ui_release_consumed = 0U;
        if (manager->capture_kind == DESKTOP_WM_CAPTURE_NONE) {
            desktop_ui_result_t ui_release = desktop_ui_pointer_event(
                ui, display, dirty, pointer_x, pointer_y, 1U, 0U);
            ui_release_consumed = ui_release.consumed;
            actions |= apply_desktop_ui_result(
                manager, explorer, ui, display, dirty, &ui_release, target);
        }
        if (!ui_release_consumed) {
            desktop_explorer_result_t explorer_result;
            desktop_explorer_result_initialize(&explorer_result);
            uint64_t now_ms = 0U;
            (void)x86os_monotonic_ms(&now_ms);
            if (captured_kind == DESKTOP_WM_CAPTURE_CLIENT &&
                captured_window >= 0 &&
                captured_window < (int32_t)DESKTOP_WM_CAPACITY) {
                (void)desktop_explorer_pointer_release(
                    explorer, (uint32_t)captured_window,
                    desktop_window_client_rect(
                        manager, (uint32_t)captured_window),
                    pointer_x, pointer_y, now_ms, &explorer_result);
                collect_explorer_pointer_result(
                    display, manager, dirty, &explorer_result, 0U,
                    activation);
            } else if (captured_kind == DESKTOP_WM_CAPTURE_NONE) {
                uint32_t hit = desktop_icon_at_position(
                    display, pointer_x, pointer_y) == 0;
                desktop_explorer_desktop_release(
                    explorer, hit, now_ms, &explorer_result);
                collect_explorer_pointer_result(
                    display, manager, dirty, &explorer_result, 1U,
                    activation);
            }
            desktop_wm_event_t release = {
                .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
                .x = pointer_x,
                .y = pointer_y,
                .button = DESKTOP_WM_BUTTON_LEFT,
                .pressed = 0U,
            };
            actions |= dispatch_desktop_event(
                manager, display, dirty, &release, target);
            if (captured_kind == DESKTOP_WM_CAPTURE_CLOSE &&
                captured_window >= 0 &&
                captured_window < (int32_t)DESKTOP_WM_CAPACITY &&
                manager->windows[captured_window].visible == 0U)
                desktop_explorer_close(
                    explorer, (uint32_t)captured_window);
        }
    }
    return actions;
}

static void render_probe_error(desktop_render_metrics_t *metrics) {
    if (metrics != 0) saturating_increment(&metrics->probe_errors);
}

static void run_menu_probe(const x86os_display_info_t *display,
                           desktop_ui_state_t *ui,
                           desktop_render_metrics_t *metrics) {
    if (display == 0 || ui == 0 || metrics == 0) {
        render_probe_error(metrics);
        return;
    }
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    reist_gui_rect_t help_title;
    reist_gui_rect_t help_item;
    if (reist_gui_menu_validate(
            &desktop_menu_model, &layout, &ui->menu) != 0 ||
        reist_gui_menu_title_rect(
            &desktop_menu_model, &layout, DESKTOP_MENU_HELP,
            &help_title) != 0 ||
        reist_gui_menu_item_rect(
            &desktop_menu_model, &layout, DESKTOP_MENU_HELP, 0U,
            &help_item) != 0) {
        render_probe_error(metrics);
        desktop_ui_initialize(ui);
        return;
    }

    desktop_dirty_region_t dirty;
    desktop_dirty_initialize(&dirty, display->width, display->height);
    desktop_ui_result_t result = desktop_ui_pointer_event(
        ui, display, &dirty, help_title.x + 2, help_title.y + 2,
        1U, 1U);
    if (!result.consumed || ui->menu.open_menu != DESKTOP_MENU_HELP)
        render_probe_error(metrics);
    desktop_dirty_initialize(&dirty, display->width, display->height);
    result = desktop_ui_pointer_event(
        ui, display, &dirty, help_title.x + 2, help_title.y + 2,
        1U, 0U);
    if (!result.consumed ||
        ui->menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE)
        render_probe_error(metrics);

    desktop_dirty_initialize(&dirty, display->width, display->height);
    result = desktop_ui_pointer_event(
        ui, display, &dirty, help_item.x + 2, help_item.y + 2,
        1U, 1U);
    if (!result.consumed ||
        ui->menu.capture_kind != REIST_GUI_MENU_CAPTURE_ITEM)
        render_probe_error(metrics);
    desktop_dirty_initialize(&dirty, display->width, display->height);
    result = desktop_ui_pointer_event(
        ui, display, &dirty, help_item.x + 2, help_item.y + 2,
        1U, 0U);
    if (!result.consumed || ui->dialog_kind != DESKTOP_DIALOG_HELP ||
        !ui->dialog.visible ||
        ui->menu.open_menu != REIST_GUI_MENU_NO_INDEX)
        render_probe_error(metrics);

    reist_gui_dialog_layout_t dialog_layout =
        desktop_dialog_layout(display);
    reist_gui_rect_t dialog_frame;
    reist_gui_rect_t dialog_title;
    reist_gui_rect_t dialog_close;
    if (reist_gui_dialog_frame_rect(
            &help_dialog_model, &dialog_layout, &ui->dialog,
            &dialog_frame) != 0 ||
        reist_gui_dialog_title_rect(
            &help_dialog_model, &dialog_layout, &ui->dialog,
            &dialog_title) != 0 ||
        reist_gui_dialog_close_rect(
            &help_dialog_model, &dialog_layout, &ui->dialog,
            &dialog_close) != 0) {
        render_probe_error(metrics);
    } else {
        int32_t drag_x = dialog_close.x +
            (int32_t)dialog_close.width + 8;
        int32_t drag_y = dialog_title.y + 3;
        desktop_dirty_initialize(&dirty, display->width, display->height);
        result = desktop_ui_pointer_event(
            ui, display, &dirty, drag_x, drag_y, 1U, 1U);
        if (!result.consumed ||
            ui->dialog.capture_kind != REIST_GUI_DIALOG_CAPTURE_MOVE)
            render_probe_error(metrics);
        desktop_dirty_initialize(&dirty, display->width, display->height);
        result = desktop_ui_pointer_event(
            ui, display, &dirty, drag_x + 12, drag_y + 8, 0U, 0U);
        if (!result.consumed || ui->dialog.bounds.x == dialog_frame.x ||
            ui->dialog.bounds.y == dialog_frame.y)
            render_probe_error(metrics);
        desktop_dirty_initialize(&dirty, display->width, display->height);
        result = desktop_ui_pointer_event(
            ui, display, &dirty, drag_x + 12, drag_y + 8, 1U, 0U);
        if (!result.consumed ||
            ui->dialog.capture_kind != REIST_GUI_DIALOG_CAPTURE_NONE)
            render_probe_error(metrics);
    }

    desktop_dirty_initialize(&dirty, display->width, display->height);
    result = desktop_ui_keyboard_event(
        ui, display, &dirty, DESKTOP_KEY_ESCAPE);
    uint32_t response = REIST_GUI_DIALOG_RESPONSE_NONE;
    if (!result.consumed || ui->dialog_kind != DESKTOP_DIALOG_NONE ||
        ui->dialog.visible ||
        reist_gui_dialog_response(&ui->dialog, &response) != 0 ||
        response != REIST_GUI_DIALOG_RESPONSE_CLOSE ||
        desktop_ui_owns_pointer(ui))
        render_probe_error(metrics);
}

static void run_render_probe(
    const x86os_display_info_t *display, desktop_wm_t *manager,
    desktop_explorer_t *explorer, desktop_ui_state_t *ui,
    int32_t *pointer_x, int32_t *pointer_y,
    desktop_render_metrics_t *metrics) {
    if (display == 0 || manager == 0 || explorer == 0 ||
        pointer_x == 0 || pointer_y == 0 || metrics == 0 ||
        manager->windows[0].visible == 0U ||
        !explorer->windows[0].active) {
        render_probe_error(metrics);
        return;
    }
    desktop_window_t *window = &manager->windows[0];
    *pointer_x = window->x + (int32_t)(window->width / 2U);
    *pointer_y = window->y + (int32_t)manager->frame_border +
                 (int32_t)(manager->title_height / 2U);
    clip_pointer(display, pointer_x, pointer_y);
    (void)x86os_pointer_update(*pointer_x, *pointer_y, 1U);

    desktop_dirty_region_t dirty;
    uint32_t target = DESKTOP_WM_NO_TARGET;
    desktop_wm_event_t event = {
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = *pointer_x,
        .y = *pointer_y,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 1U,
    };
    desktop_dirty_initialize(&dirty, display->width, display->height);
    (void)dispatch_desktop_event(manager, display, &dirty, &event, &target);
    if (manager->capture_kind != DESKTOP_WM_CAPTURE_MOVE)
        render_probe_error(metrics);
    for (uint32_t step = 0U; step < DESKTOP_RENDER_PROBE_STEPS; ++step) {
        uint32_t move_kind = DESKTOP_MOVE_CACHE_NONE;
        uint32_t move_window = DESKTOP_WM_NO_TARGET;
        desktop_rect_t move_source = {0, 0, 0U, 0U};
        desktop_move_cache_t move_cache = {0};
        if (!desktop_move_capture_geometry(
                manager, ui, &move_kind, &move_window, &move_source))
            render_probe_error(metrics);
        move_pointer(display, pointer_x, pointer_y,
                     DESKTOP_RENDER_PROBE_STEP_X, 0);
        desktop_dirty_initialize(&dirty, display->width, display->height);
        event = (desktop_wm_event_t){
            .type = DESKTOP_WM_EVENT_POINTER_MOTION,
            .x = *pointer_x,
            .y = *pointer_y,
        };
        (void)dispatch_desktop_event(
            manager, display, &dirty, &event, &target);
        uint32_t destination_kind = DESKTOP_MOVE_CACHE_NONE;
        uint32_t destination_window = DESKTOP_WM_NO_TARGET;
        desktop_rect_t destination = {0, 0, 0U, 0U};
        if (!desktop_move_capture_geometry(
                manager, ui, &destination_kind, &destination_window,
                &destination) || destination_kind != move_kind ||
            destination_window != move_window) {
            render_probe_error(metrics);
        } else {
            desktop_move_cache_capture(
                &move_cache, move_kind, move_window,
                move_source, destination);
        }
        if (dirty.count == 0U) {
            render_probe_error(metrics);
            continue;
        }
        if (!move_cache.valid) render_probe_error(metrics);
        (void)x86os_pointer_update(*pointer_x, *pointer_y, 0U);
        render_desktop_measured(
            display, manager, explorer, 0, ui, &dirty,
            move_cache.valid ? &move_cache : 0,
            1U, 0U, metrics);
        (void)x86os_pointer_update(*pointer_x, *pointer_y, 1U);
    }
    event = (desktop_wm_event_t){
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = *pointer_x,
        .y = *pointer_y,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 0U,
    };
    desktop_dirty_initialize(&dirty, display->width, display->height);
    (void)dispatch_desktop_event(manager, display, &dirty, &event, &target);

    *pointer_x = window->x + (int32_t)window->width - 1;
    *pointer_y = window->y + (int32_t)window->height - 1;
    clip_pointer(display, pointer_x, pointer_y);
    (void)x86os_pointer_update(*pointer_x, *pointer_y, 1U);
    event = (desktop_wm_event_t){
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = *pointer_x,
        .y = *pointer_y,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 1U,
    };
    desktop_dirty_initialize(&dirty, display->width, display->height);
    (void)dispatch_desktop_event(manager, display, &dirty, &event, &target);
    if (manager->capture_kind != DESKTOP_WM_CAPTURE_RESIZE)
        render_probe_error(metrics);
    for (uint32_t step = 0U; step < DESKTOP_RENDER_PROBE_STEPS; ++step) {
        move_pointer(display, pointer_x, pointer_y,
                     DESKTOP_RENDER_PROBE_STEP_X, 2);
        desktop_dirty_initialize(&dirty, display->width, display->height);
        event = (desktop_wm_event_t){
            .type = DESKTOP_WM_EVENT_POINTER_MOTION,
            .x = *pointer_x,
            .y = *pointer_y,
        };
        (void)dispatch_desktop_event(
            manager, display, &dirty, &event, &target);
        if (dirty.count == 0U) {
            render_probe_error(metrics);
            continue;
        }
        (void)x86os_pointer_update(*pointer_x, *pointer_y, 0U);
        render_desktop_measured(
            display, manager, explorer, 0, ui, &dirty, 0, 0U, 1U, metrics);
        (void)x86os_pointer_update(*pointer_x, *pointer_y, 1U);
    }
    event = (desktop_wm_event_t){
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = *pointer_x,
        .y = *pointer_y,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 0U,
    };
    desktop_dirty_initialize(&dirty, display->width, display->height);
    (void)dispatch_desktop_event(manager, display, &dirty, &event, &target);
    if (metrics->drag_frames != DESKTOP_RENDER_PROBE_STEPS ||
        metrics->resize_frames != DESKTOP_RENDER_PROBE_STEPS ||
        manager->capture_kind != DESKTOP_WM_CAPTURE_NONE)
        render_probe_error(metrics);
    run_menu_probe(display, ui, metrics);
}

int main(int argc, char **argv) {
    x86os_display_info_t display;
    desktop_wm_t manager;
    static desktop_surface_manager_t surfaces;
    desktop_surface_runtime_t surface_runtime;
    static desktop_explorer_t explorer;
    static desktop_filetypes_t filetypes;
    desktop_ui_state_t ui;
    desktop_render_metrics_t metrics = {0};
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t previous_buttons = 0U;
    unsigned int runtime_activated = 0U;
    uint32_t render_probe = 0U;
    uint32_t surface_probe = 0U;
    uint32_t notepad_probe = 0U;
    uint32_t control_probe = 0U;
    uint32_t surface_probe_reported = 0U;
    uint32_t surface_probe_created_reported = 0U;

    if (argc == 2 && argv != 0 && text_equal(argv[1], "--render-probe")) {
        render_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--surface-probe")) {
        surface_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--notepad-probe")) {
        notepad_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--control-probe")) {
        control_probe = 1U;
    } else if (argc != 1) {
        x86os_puts(
            "Usage: desktop [--render-probe|--surface-probe|"
            "--notepad-probe|--control-probe]\n");
        return 2;
    }

    int display_status = x86os_display_info(&display);
    if (display_status != 0) {
        if (x86os_display_activate() == 0) runtime_activated = 1U;
        display_status = x86os_display_info(&display);
    }
    if (display_status != 0 ||
        display.version != X86OS_DISPLAY_ABI_VERSION ||
        display.struct_size < sizeof(display) ||
        display.width < 320U || display.height < 240U ||
        display.font_width == 0U || display.font_height == 0U) {
        x86os_puts("desktop: Grafikmodus nicht verfuegbar\n");
        return 1;
    }

    uint32_t menu = menu_height(&display);
    uint32_t status = status_height(&display);
    desktop_ui_initialize(&ui);
    reist_gui_menu_layout_t menu_layout = desktop_menu_layout(&display);
    reist_gui_dialog_layout_t dialog_layout =
        desktop_dialog_layout(&display);
    if (reist_gui_menu_validate(
            &desktop_menu_model, &menu_layout, &ui.menu) != 0 ||
        reist_gui_dialog_validate(
            &help_dialog_model, &dialog_layout, &ui.dialog) != 0 ||
        reist_gui_dialog_validate(
            &about_dialog_model, &dialog_layout, &ui.dialog) != 0 ||
        reist_gui_dialog_validate(
            &ui.error_model, &dialog_layout, &ui.dialog) != 0) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("desktop: GUI-API/Layout nicht kompatibel\n");
        return 1;
    }
    desktop_wm_initialize(&manager, display.width, display.height,
                          (int32_t)menu + 4,
                          (int32_t)(display.height - status - 4U),
                          max_u32(display.font_height + 8U, 24U));
    desktop_surface_initialize(&surfaces);
    if (desktop_surface_runtime_initialize(&surface_runtime) != 0) {
        if (runtime_activated) (void)x86os_display_deactivate();
        x86os_puts("desktop: Surface-IPC konnte nicht gestartet werden\n");
        return 1;
    }
    desktop_explorer_initialize(&explorer);
    /* Optional assets are read and decoded exactly once before the first
     * frame. Missing or malformed files leave fixed-cost vector fallbacks. */
    desktop_file_icon_cache_initialize();
    int filetypes_status = load_filetypes(&filetypes);
    pointer_x = (int32_t)(display.width / 2U);
    pointer_y = (int32_t)(display.height / 2U);
    x86os_puts("DESKTOP_OK\n");
    desktop_dirty_region_t initial_dirty;
    desktop_dirty_initialize(&initial_dirty, display.width, display.height);
    uint32_t initial_target = DESKTOP_WM_NO_TARGET;
    (void)open_explorer_path(
        &manager, &explorer, &ui, &display,
        &initial_dirty, "/", &initial_target);
    if (filetypes_status != 0)
        desktop_ui_open_error(
            &ui, &display, &initial_dirty,
            "Dateizuordnungen sind ungueltig.",
            "/etc/reist/filetypes.conf");
    desktop_dirty_full(&initial_dirty);
    render_desktop_measured(
        &display, &manager, &explorer, &surfaces, &ui,
        &initial_dirty, 0, 0U, 0U, &metrics);
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
    if (surface_probe || notepad_probe || control_probe) {
        int probe_status = control_probe
            ? launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/CONTROL.PRG", 0, 1U)
            : launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/NOTEPAD.PRG", "/README.TXT", 1U);
        if (surface_probe) {
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/NOTEPAD.PRG", "--menu-probe", 2U);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/NOTEPAD.PRG", "--file-dialog-probe", 3U);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/NOTEPAD.PRG", "--hover-probe", 4U);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/IMAGEVIEWER.PRG",
                    "/USR/SHARE/IMAGES/DEMO-COLORS.GIF", 5U);
        }
        if (probe_status != 0) {
            x86os_puts(control_probe
                ? "DESKTOP_CONTROL_FAIL launch\n"
                : (surface_probe
                    ? "DESKTOP_SURFACE_FAIL launch\n"
                    : "DESKTOP_NOTEPAD_FAIL launch\n"));
            desktop_surface_runtime_shutdown(&surface_runtime);
            if (runtime_activated) (void)x86os_display_deactivate();
            return 1;
        }
        x86os_puts(control_probe
            ? "DESKTOP_CONTROL_STAGE client-bound\n"
            : (surface_probe
                ? "DESKTOP_SURFACE_STAGE client-bound\n"
                : "DESKTOP_NOTEPAD_STAGE client-bound\n"));
    }
    if (render_probe) {
        run_render_probe(
            &display, &manager, &explorer, &ui,
            &pointer_x, &pointer_y, &metrics);
        if (desktop_try_exit(
                pointer_x, pointer_y, runtime_activated, &metrics)) {
            desktop_surface_runtime_shutdown(&surface_runtime);
            return 0;
        }
        render_probe_error(&metrics);
    }

    for (;;) {
        int surface_poll_status = desktop_surface_runtime_poll(
            &surface_runtime, &surfaces);
        if (surface_probe && surface_poll_status != 0) {
            x86os_puts("DESKTOP_SURFACE_FAIL protocol status=");
            x86os_print_number(surface_poll_status);
            x86os_putchar('\n');
            desktop_surface_runtime_shutdown(&surface_runtime);
            if (runtime_activated) (void)x86os_display_deactivate();
            return 1;
        }
        int key = read_key();
        desktop_dirty_region_t dirty;
        desktop_dirty_initialize(&dirty, display.width, display.height);
        sync_surface_windows(
            &manager, &explorer, &surfaces, &surface_runtime, &dirty);
        if ((surface_probe || control_probe) && !surface_probe_reported) {
            for (uint32_t surface_index = 0U;
                 surface_index < DESKTOP_SURFACE_CAPACITY; ++surface_index) {
                if (surfaces.slots[surface_index].active &&
                    !surface_probe_created_reported) {
                    x86os_puts("DESKTOP_SURFACE_STAGE created\n");
                    surface_probe_created_reported = 1U;
                }
                if (surfaces.slots[surface_index].active &&
                    surfaces.slots[surface_index].acknowledged_serial != 0U &&
                    surfaces.slots[surface_index].window_index !=
                        DESKTOP_SURFACE_NO_SLOT) {
                    x86os_puts(control_probe
                        ? "DESKTOP_CONTROL_OK\n"
                        : "DESKTOP_SURFACE_OK\n");
                    surface_probe_reported = 1U;
                    break;
                }
            }
        }
        uint32_t actions = 0U;
        uint32_t action_target = DESKTOP_WM_NO_TARGET;
        uint32_t drag_render = 0U;
        uint32_t resize_render = 0U;
        desktop_move_cache_t move_cache = {0};
        desktop_activation_t activation = {
            .window_index = DESKTOP_WM_NO_TARGET,
            .entry_index = DESKTOP_EXPLORER_NO_ENTRY,
        };
        uint32_t control_panel_activate = 0U;
        int32_t pending_delta_x = 0;
        int32_t pending_delta_y = 0;
        unsigned int mouse_events = 0U;
        for (; mouse_events < DESKTOP_MOUSE_BATCH_LIMIT; ++mouse_events) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            accumulate_mouse_delta(&pending_delta_x, mouse.delta_x);
            accumulate_mouse_delta(&pending_delta_y, mouse.delta_y);
            uint32_t left_down =
                (mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t left_was_down =
                (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            if (left_down != left_was_down) {
                actions |= dispatch_pointer_motion(
                    &manager, &explorer, &ui, &display, &dirty,
                    &pointer_x, &pointer_y,
                    pending_delta_x, pending_delta_y, &action_target,
                    &drag_render, &resize_render, &move_cache);
                pending_delta_x = 0;
                pending_delta_y = 0;
                if (!ui.dialog.visible &&
                    manager.capture_kind == DESKTOP_WM_CAPTURE_NONE &&
                    desktop_wm_window_at(
                        &manager, pointer_x, pointer_y) ==
                            DESKTOP_WM_NO_WINDOW)
                    control_panel_activate |= control_panel_pointer_button(
                        &explorer, &display, &dirty,
                        pointer_x, pointer_y, mouse.buttons,
                        previous_buttons);
                int32_t captured_surface_window = manager.capture_window;
                actions |= dispatch_pointer_button(
                    &manager, &explorer, &ui, &display, &dirty,
                    pointer_x, pointer_y, mouse.buttons,
                    previous_buttons, &action_target, &activation);
                int32_t surface_button_window = left_down
                    ? manager.capture_window : captured_surface_window;
                (void)enqueue_surface_pointer(
                    &manager, &surfaces, surface_button_window,
                    REIST_GUI_SURFACE_INPUT_POINTER_BUTTON,
                    pointer_x, pointer_y, 0, 0, left_down,
                    !left_down || manager.capture_kind ==
                        DESKTOP_WM_CAPTURE_CLIENT);
            }
            previous_buttons = mouse.buttons;
        }
        actions |= dispatch_pointer_motion(
            &manager, &explorer, &ui, &display, &dirty,
            &pointer_x, &pointer_y,
            pending_delta_x, pending_delta_y, &action_target,
            &drag_render, &resize_render, &move_cache);
        if (pending_delta_x != 0 || pending_delta_y != 0) {
            int32_t surface_motion_window = manager.capture_kind ==
                DESKTOP_WM_CAPTURE_CLIENT ? manager.capture_window
                                          : desktop_wm_window_at(
                                                &manager, pointer_x, pointer_y);
            (void)enqueue_surface_pointer(
                &manager, &surfaces, surface_motion_window,
                REIST_GUI_SURFACE_INPUT_POINTER_MOTION,
                pointer_x, pointer_y, pending_delta_x, pending_delta_y, 0U,
                manager.capture_kind == DESKTOP_WM_CAPTURE_CLIENT);
        }

        desktop_ui_result_t ui_key = desktop_ui_keyboard_event(
            &ui, &display, &dirty, key);
        actions |= apply_desktop_ui_result(
            &manager, &explorer, &ui, &display, &dirty,
            &ui_key, &action_target);
        if (!ui_key.consumed) {
            uint32_t surface_key_consumed = enqueue_surface_keyboard(
                &manager, &surfaces, key);
            uint32_t explorer_key = explorer_key_from_input(key);
            if (!surface_key_consumed && explorer_key != 0U &&
                manager.keyboard_focus >= 0 &&
                manager.keyboard_focus < (int32_t)DESKTOP_WM_CAPACITY) {
                uint32_t focused = (uint32_t)manager.keyboard_focus;
                desktop_rect_t client = desktop_window_client_rect(
                    &manager, focused);
                uint32_t columns = client.width /
                    DESKTOP_EXPLORER_ICON_WIDTH;
                desktop_explorer_result_t explorer_result;
                desktop_explorer_result_initialize(&explorer_result);
                (void)desktop_explorer_keyboard(
                    &explorer, focused, columns != 0U ? columns : 1U,
                    explorer_key, &explorer_result);
                collect_explorer_pointer_result(
                    &display, &manager, &dirty, &explorer_result, 0U,
                    &activation);
            } else if (!surface_key_consumed && key == DESKTOP_KEY_ESCAPE) {
                desktop_wm_event_t keyboard = {
                    .type = DESKTOP_WM_EVENT_KEYBOARD,
                    .key = DESKTOP_WM_KEY_ESCAPE,
                };
                actions |= dispatch_desktop_event(
                    &manager, &display, &dirty, &keyboard,
                    &action_target);
            }
        }

        if ((actions & DESKTOP_WM_RESULT_EXIT) != 0U) {
            if (desktop_try_exit(
                    pointer_x, pointer_y, runtime_activated, &metrics)) {
                desktop_surface_runtime_shutdown(&surface_runtime);
                return 0;
            }
        }

        actions |= apply_desktop_activation(
            &manager, &explorer, &filetypes, &surface_runtime,
            &ui, &display, &dirty,
            &activation, &action_target, pointer_x, pointer_y);
        if (control_panel_activate)
            apply_control_panel_activation(
                &surface_runtime, &ui, &display, &dirty,
                pointer_x, pointer_y);
        sync_surface_windows(
            &manager, &explorer, &surfaces, &surface_runtime, &dirty);

        /* The cached path represents exactly one unobscured move.  Any
         * concurrent keyboard/action damage or resize falls back to the
         * ordinary compositor so unrelated invalid regions are not lost. */
        if (move_cache.valid &&
            (!drag_render || resize_render || dirty.full ||
             key != DESKTOP_KEY_NONE ||
             (actions & (DESKTOP_WM_RESULT_LAUNCH |
                         DESKTOP_WM_RESULT_EXIT)) != 0U))
            move_cache.valid = 0U;

        if (dirty.count != 0U) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            render_desktop_measured(
                &display, &manager, &explorer, &surfaces, &ui, &dirty,
                move_cache.valid ? &move_cache : 0,
                drag_render, resize_render, &metrics);
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if (mouse_events != 0U) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else {
            (void)x86os_sleep_ms(5U);
        }
    }
}
