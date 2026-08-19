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
#include "desktop_wm.h"

#define APP_COUNT 4U
#define DESKTOP_ARGUMENT_LIMIT 32U
#define DESKTOP_METRICS_VERSION 1U
#define DESKTOP_RENDER_PROBE_STEPS 8U
#define DESKTOP_RENDER_PROBE_STEP_X 4

_Static_assert(APP_COUNT == DESKTOP_WM_CAPACITY,
               "desktop app and window capacities must match");

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
    const char *description;
    const char *program;
    const char *argument;
    uint32_t accent;
} desktop_app_t;

static const desktop_app_t apps[APP_COUNT] = {
    {"Shell",   "Terminal und Befehle",   "/bin/shell.prg",    0,             0x0000479DU},
    {"Dateien", "Dateien und Laufwerke",  "/bin/ls.prg",       0,             0x00008844U},
    {"Editor",  "Textdateien bearbeiten", "/bin/edit.prg",     "desktop.txt", 0x00900080U},
    {"System",  "Systeminformationen",    "/sbin/sysinfo.prg", 0,             0x00A06000U},
};

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

typedef struct {
    const x86os_display_info_t *display;
    desktop_rect_t clip;
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
    if ((int64_t)y < clip_top ||
        (int64_t)y + display->font_height > clip_bottom) return;
    size_t capacity = maximum_width / display->font_width;
    if (capacity > X86OS_DISPLAY_MAX_TEXT)
        capacity = X86OS_DISPLAY_MAX_TEXT;
    size_t length = bounded_text_length(text, capacity);
    size_t first = 0U;
    while (first < length) {
        int64_t glyph_left = (int64_t)x + first * display->font_width;
        int64_t glyph_right = glyph_left + display->font_width;
        if (glyph_left >= clip_left && glyph_right <= clip_right) break;
        ++first;
    }
    size_t end = first;
    while (end < length) {
        int64_t glyph_left = (int64_t)x + end * display->font_width;
        int64_t glyph_right = glyph_left + display->font_width;
        if (glyph_left < clip_left || glyph_right > clip_right) break;
        ++end;
    }
    if (end != first)
        (void)x86os_draw_text_pixels(
            x + (int32_t)(first * display->font_width), y,
            text + first, end - first, foreground, background);
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

static desktop_rect_t desktop_icon_rect(const x86os_display_info_t *display,
                                        uint32_t index) {
    desktop_rect_t rect = {0, 0, 0U, 0U};
    uint32_t top = menu_height(display) + 6U;
    uint32_t bottom = display->height - status_height(display) - 6U;
    uint32_t available = bottom > top ? bottom - top : 1U;
    uint32_t gap = 4U;
    uint32_t height = available > gap * (APP_COUNT - 1U)
        ? (available - gap * (APP_COUNT - 1U)) / APP_COUNT : 1U;
    rect.x = 8;
    rect.y = (int32_t)(top + index * (height + gap));
    rect.width = min_u32(124U, display->width > 16U ? display->width - 16U : 1U);
    rect.height = height;
    return rect;
}

static int desktop_icon_at_position(const x86os_display_info_t *display,
                                    int32_t x, int32_t y) {
    for (uint32_t index = 0U; index < APP_COUNT; ++index) {
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

static void render_icon(const desktop_render_context_t *context,
                        const desktop_wm_t *manager, uint32_t index) {
    const x86os_display_info_t *display = context->display;
    desktop_rect_t rect = desktop_icon_rect(display, index);
    if (!intersect_rects(rect, context->clip, 0)) return;
    uint32_t selected = manager->selected == index;
    uint32_t icon_size = min_u32(rect.height > display->font_height + 6U
        ? rect.height - display->font_height - 6U : 12U, 28U);
    desktop_rect_t symbol = {
        rect.x + 5,
        rect.y + 3,
        icon_size,
        icon_size
    };
    if (selected) draw_bevel(context, rect, color_face, 0U);
    draw_bevel(context, symbol, apps[index].accent, 1U);
    if (symbol.width > 8U && symbol.height > 8U) {
        fill_rect_clipped(
            context,
            (desktop_rect_t){symbol.x + 4, symbol.y + 4,
                             symbol.width - 8U, symbol.height - 8U},
            color_client);
    }
    uint32_t text_y_offset = rect.height > display->font_height + 3U
        ? rect.height - display->font_height - 3U : 0U;
    draw_text_clipped(context, rect.x + 4,
                      rect.y + (int32_t)text_y_offset, apps[index].title,
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

static void render_window(const desktop_render_context_t *context,
                          const desktop_wm_t *manager,
                          uint32_t window_index) {
    if (window_index >= DESKTOP_WM_CAPACITY) return;
    const x86os_display_info_t *display = context->display;
    const desktop_window_t *window = &manager->windows[window_index];
    if (window->visible == 0U || window->app_index >= APP_COUNT) return;
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
    desktop_rect_t client = {
        title.x,
        title.y + (int32_t)title.height,
        title.width,
        window->height - border * 2U - title.height
    };
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
                          apps[window->app_index].title,
                          title.width - title_x - 3U, color_title_text,
                          title_color);
    }

    uint32_t padding = 10U;
    uint32_t line = max_u32(display->font_height + 5U, 18U);
    if (client.width > padding * 2U && client.height > padding * 2U) {
        uint32_t text_width = client.width - padding * 2U;
        int32_t text_x = client.x + (int32_t)padding;
        int32_t text_y = client.y + (int32_t)padding;
        draw_text_clipped(context, text_x, text_y,
                          apps[window->app_index].description, text_width,
                          color_text, color_client);
        if (client.height > padding * 2U + line) {
            draw_text_clipped(context, text_x, text_y + (int32_t)line,
                              apps[window->app_index].program, text_width,
                              apps[window->app_index].accent, color_client);
        }
        if (client.height > padding * 2U + line * 3U) {
            draw_text_clipped(
                context, text_x, text_y + (int32_t)(line * 3U),
                "ENTER startet Legacy-App im Vollbild", text_width,
                color_shadow, color_client);
        }
    }
    render_resize_grip(context, window);
}

static void render_desktop_clip(const desktop_render_context_t *context,
                                const desktop_wm_t *manager) {
    const x86os_display_info_t *display = context->display;
    uint32_t menu = menu_height(display);
    uint32_t status = status_height(display);
    desktop_rect_t menu_rect = {0, 0, display->width, menu};
    desktop_rect_t status_rect = {
        0, (int32_t)(display->height - status), display->width, status
    };

    fill_rect_clipped(
        context, (desktop_rect_t){0, 0, display->width, display->height},
        color_desktop);
    draw_bevel(context, menu_rect, color_face, 1U);
    draw_text_clipped(context, 10,
                      (int32_t)((menu - display->font_height) / 2U),
                      "REIST Workspace   Fenster   Hilfe",
                      display->width > 20U ? display->width - 20U : 1U,
                      color_text, color_face);

    for (uint32_t index = 0U; index < APP_COUNT; ++index)
        render_icon(context, manager, index);

    for (uint32_t position = 0U; position < DESKTOP_WM_CAPACITY; ++position) {
        uint32_t window_index = manager->z_order[position];
        if (window_index < DESKTOP_WM_CAPACITY)
            render_window(context, manager, window_index);
    }

    draw_bevel(context, status_rect, color_face, 0U);
    draw_text_clipped(
        context, 10,
        status_rect.y +
            (int32_t)((status - display->font_height) / 2U),
        "Maus: Fokus / Verschieben / Groesse / Schliessen   ENTER: Start   ESC: Shell",
        display->width > 20U ? display->width - 20U : 1U,
        color_text, color_face);
}

static desktop_rect_t expanded_render_clip(
    const x86os_display_info_t *display, desktop_rect_t dirty) {
    int64_t left = (int64_t)dirty.x - display->font_width;
    int64_t top = (int64_t)dirty.y - display->font_height;
    int64_t right = (int64_t)dirty.x + dirty.width + display->font_width;
    int64_t bottom = (int64_t)dirty.y + dirty.height + display->font_height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int64_t)display->width) right = display->width;
    if (bottom > (int64_t)display->height) bottom = display->height;
    if (left >= right || top >= bottom)
        return (desktop_rect_t){0, 0, 0U, 0U};
    return (desktop_rect_t){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top)
    };
}

static void render_dirty_regions(const x86os_display_info_t *display,
                                 const desktop_wm_t *manager,
                                 const desktop_dirty_region_t *dirty) {
    if (display == 0 || manager == 0 || dirty == 0) return;
    for (uint32_t index = 0U; index < dirty->count; ++index) {
        desktop_render_context_t context = {
            .display = display,
            .clip = expanded_render_clip(display, dirty->rects[index]),
        };
        if (context.clip.width != 0U && context.clip.height != 0U)
            render_desktop_clip(&context, manager);
    }
}

static void render_desktop(const x86os_display_info_t *display,
                           const desktop_wm_t *manager,
                           const desktop_dirty_region_t *dirty) {
    render_dirty_regions(display, manager, dirty);
}

static uint32_t render_desktop_frame(const x86os_display_info_t *display,
                                     const desktop_wm_t *manager,
                                     const desktop_dirty_region_t *dirty) {
    if (dirty == 0 || dirty->count == 0U) return 0U;
    uint32_t serial = 0U;
    int begin = x86os_display_frame_begin(&serial);
    if (begin != 0) {
        /* Oversized/direct framebuffers retain the compatible immediate path. */
        render_desktop(display, manager, dirty);
        return 1U;
    }
    render_desktop(display, manager, dirty);
    if (x86os_display_frame_commit(serial) != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_desktop(display, manager, dirty);
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
    const desktop_dirty_region_t *dirty, uint32_t drag, uint32_t resize,
    desktop_render_metrics_t *metrics) {
    if (dirty == 0 || dirty->count == 0U) return;
    uint64_t started_ms = 0U;
    uint64_t finished_ms = 0U;
    uint32_t clock_valid = x86os_monotonic_ms(&started_ms) == 0;
    uint32_t fallback = render_desktop_frame(display, manager, dirty);
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

static void drain_input(void) {
    for (unsigned int byte = 0U; byte < 256U; ++byte) {
        if (x86os_getchar_nonblocking() == 0) return;
    }
}

static void print_integer(int value) {
    char digits[12];
    unsigned int count = 0U;
    unsigned int magnitude;
    if (value < 0) {
        x86os_putchar('-');
        magnitude = 0U - (unsigned int)value;
    } else {
        magnitude = (unsigned int)value;
    }
    do {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
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

static void launch_app(unsigned int index) {
    int status = 0;
    int pid;
    x86os_puts("DESKTOP_LAUNCH:");
    x86os_puts(apps[index].program);
    x86os_putchar('\n');
    x86os_clear();

    if (apps[index].argument != 0) {
        const char *arguments[2] = {
            apps[index].program,
            apps[index].argument,
        };
        pid = x86os_spawnv(apps[index].program, 2, arguments);
    } else {
        pid = x86os_spawn(apps[index].program);
    }
    if (pid >= 0) {
        int wait_result = x86os_wait(pid, &status);
        if (wait_result != pid) {
            x86os_puts("Warten fehlgeschlagen (Status ");
            print_integer(wait_result);
            x86os_puts("); Kind wird beendet.\n");

            int kill_result = x86os_kill(pid);
            int reap_result = x86os_wait(pid, &status);
            if (reap_result != pid) {
                x86os_puts("Kindbereinigung fehlgeschlagen (Kill ");
                print_integer(kill_result);
                x86os_puts(", Wait ");
                print_integer(reap_result);
                x86os_puts(").\n");
            }
        }
    } else {
        x86os_puts("Start fehlgeschlagen (Status ");
        print_integer(pid);
        x86os_puts(").\n");
    }
    x86os_puts("\nTaste zum Desktop...");
    (void)x86os_getchar();
    drain_input();
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

static uint32_t wm_key_from_input(int key) {
    if (key == '\t' || key == DESKTOP_KEY_RIGHT)
        return DESKTOP_WM_KEY_RIGHT;
    if (key == DESKTOP_KEY_LEFT) return DESKTOP_WM_KEY_LEFT;
    if (key == DESKTOP_KEY_UP) return DESKTOP_WM_KEY_UP;
    if (key == DESKTOP_KEY_DOWN) return DESKTOP_WM_KEY_DOWN;
    if (key == '\r' || key == '\n') return DESKTOP_WM_KEY_ENTER;
    if (key == DESKTOP_KEY_ESCAPE) return DESKTOP_WM_KEY_ESCAPE;
    return 0U;
}

static void collect_dispatch_result(
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    const desktop_wm_dispatch_result_t *result) {
    desktop_dirty_add_regions(dirty, &result->dirty);
    if ((result->flags & DESKTOP_WM_RESULT_SELECTION_CHANGED) != 0U) {
        if (result->previous_selected < APP_COUNT)
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, result->previous_selected));
        if (result->selected < APP_COUNT)
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, result->selected));
    }
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

static void render_probe_error(desktop_render_metrics_t *metrics) {
    if (metrics != 0) saturating_increment(&metrics->probe_errors);
}

static void run_render_probe(
    const x86os_display_info_t *display, desktop_wm_t *manager,
    int32_t *pointer_x, int32_t *pointer_y,
    desktop_render_metrics_t *metrics) {
    if (display == 0 || manager == 0 || pointer_x == 0 || pointer_y == 0 ||
        metrics == 0 || manager->windows[0].visible == 0U) {
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
        if (dirty.count == 0U) {
            render_probe_error(metrics);
            continue;
        }
        (void)x86os_pointer_update(*pointer_x, *pointer_y, 0U);
        render_desktop_measured(display, manager, &dirty, 1U, 0U, metrics);
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
        render_desktop_measured(display, manager, &dirty, 0U, 1U, metrics);
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
}

int main(int argc, char **argv) {
    x86os_display_info_t display;
    desktop_wm_t manager;
    desktop_render_metrics_t metrics = {0};
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t previous_buttons = 0U;
    unsigned int runtime_activated = 0U;
    uint32_t render_probe = 0U;

    if (argc == 2 && argv != 0 && text_equal(argv[1], "--render-probe")) {
        render_probe = 1U;
    } else if (argc != 1) {
        x86os_puts("Usage: desktop [--render-probe]\n");
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
    desktop_wm_initialize(&manager, display.width, display.height,
                          (int32_t)menu + 4,
                          (int32_t)(display.height - status - 4U),
                          max_u32(display.font_height + 8U, 24U));
    pointer_x = (int32_t)(display.width / 2U);
    pointer_y = (int32_t)(display.height / 2U);
    x86os_puts("DESKTOP_OK\n");
    desktop_dirty_region_t initial_dirty;
    desktop_dirty_initialize(&initial_dirty, display.width, display.height);
    desktop_dirty_full(&initial_dirty);
    render_desktop_measured(
        &display, &manager, &initial_dirty, 0U, 0U, &metrics);
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
    if (render_probe) {
        run_render_probe(
            &display, &manager, &pointer_x, &pointer_y, &metrics);
        if (desktop_try_exit(
                pointer_x, pointer_y, runtime_activated, &metrics)) return 0;
        render_probe_error(&metrics);
    }

    for (;;) {
        int key = read_key();
        desktop_dirty_region_t dirty;
        desktop_dirty_initialize(&dirty, display.width, display.height);
        uint32_t actions = 0U;
        uint32_t action_target = DESKTOP_WM_NO_TARGET;
        uint32_t drag_render = 0U;
        uint32_t resize_render = 0U;
        unsigned int mouse_events = 0U;
        for (; mouse_events < 32U; ++mouse_events) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            move_pointer(&display, &pointer_x, &pointer_y,
                         mouse.delta_x, mouse.delta_y);

            desktop_wm_event_t motion = {
                .type = DESKTOP_WM_EVENT_POINTER_MOTION,
                .x = pointer_x,
                .y = pointer_y,
            };
            if (manager.capture_kind == DESKTOP_WM_CAPTURE_MOVE)
                drag_render = 1U;
            if (manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE)
                resize_render = 1U;
            actions |= dispatch_desktop_event(
                &manager, &display, &dirty, &motion, &action_target);

            uint32_t left_down =
                (mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t left_was_down =
                (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            if (left_down && !left_was_down) {
                int window = desktop_wm_window_at(&manager,
                                                   pointer_x, pointer_y);
                desktop_wm_event_t press = {
                    .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
                    .x = pointer_x,
                    .y = pointer_y,
                    .button = DESKTOP_WM_BUTTON_LEFT,
                    .pressed = 1U,
                };
                actions |= dispatch_desktop_event(
                    &manager, &display, &dirty, &press, &action_target);
                if (window == DESKTOP_WM_NO_WINDOW) {
                    int icon = desktop_icon_at_position(&display,
                                                        pointer_x, pointer_y);
                    if (icon != DESKTOP_WM_NO_WINDOW) {
                        desktop_wm_event_t open = {
                            .type = DESKTOP_WM_EVENT_OPEN,
                            .target = (uint32_t)icon,
                        };
                        actions |= dispatch_desktop_event(
                            &manager, &display, &dirty, &open,
                            &action_target);
                    }
                }
            } else if (!left_down && left_was_down) {
                desktop_wm_event_t release = {
                    .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
                    .x = pointer_x,
                    .y = pointer_y,
                    .button = DESKTOP_WM_BUTTON_LEFT,
                    .pressed = 0U,
                };
                actions |= dispatch_desktop_event(
                    &manager, &display, &dirty, &release, &action_target);
            }
            previous_buttons = mouse.buttons;
        }

        uint32_t wm_key = wm_key_from_input(key);
        if (wm_key != 0U) {
            desktop_wm_event_t keyboard = {
                .type = DESKTOP_WM_EVENT_KEYBOARD,
                .key = wm_key,
            };
            actions |= dispatch_desktop_event(
                &manager, &display, &dirty, &keyboard, &action_target);
        }

        if ((actions & DESKTOP_WM_RESULT_EXIT) != 0U) {
            if (desktop_try_exit(
                    pointer_x, pointer_y, runtime_activated, &metrics))
                return 0;
        }

        if ((actions & DESKTOP_WM_RESULT_LAUNCH) != 0U &&
            action_target < APP_COUNT) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            launch_app(action_target);
            desktop_dirty_full(&dirty);
        }

        if (dirty.count != 0U) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            render_desktop_measured(
                &display, &manager, &dirty, drag_render, resize_render,
                &metrics);
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if (mouse_events != 0U) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else {
            (void)x86os_sleep_ms(5U);
        }
    }
}
