/**
 * @file userspace/programs/desktop.c
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

static size_t bounded_text_length(const char *text, size_t maximum) {
    size_t length = 0U;
    if (text == 0) return 0U;
    while (length < maximum && text[length] != '\0') ++length;
    return length;
}

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static uint32_t min_u32(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static void draw_text_bounded(const x86os_display_info_t *display,
                              int32_t x, int32_t y, const char *text,
                              uint32_t maximum_width, uint32_t foreground,
                              uint32_t background) {
    if (display == 0 || text == 0 || display->font_width == 0U) return;
    size_t capacity = maximum_width / display->font_width;
    if (capacity > X86OS_DISPLAY_MAX_TEXT)
        capacity = X86OS_DISPLAY_MAX_TEXT;
    size_t length = bounded_text_length(text, capacity);
    if (length != 0U) {
        (void)x86os_draw_text_pixels(x, y, text, length,
                                     foreground, background);
    }
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

static void draw_bevel(desktop_rect_t rect, uint32_t face,
                       uint32_t raised) {
    if (rect.width == 0U || rect.height == 0U) return;
    (void)x86os_fill_rect(rect.x, rect.y, rect.width, rect.height, face);
    if (rect.width < 2U || rect.height < 2U) return;
    uint32_t top_left = raised ? color_light : color_shadow;
    uint32_t bottom_right = raised ? color_shadow : color_light;
    (void)x86os_fill_rect(rect.x, rect.y, rect.width, 1U, top_left);
    (void)x86os_fill_rect(rect.x, rect.y, 1U, rect.height, top_left);
    (void)x86os_fill_rect(rect.x, rect.y + (int32_t)rect.height - 1,
                          rect.width, 1U, bottom_right);
    (void)x86os_fill_rect(rect.x + (int32_t)rect.width - 1, rect.y,
                          1U, rect.height, bottom_right);
}

static void render_icon(const x86os_display_info_t *display,
                        const desktop_wm_t *manager, uint32_t index) {
    desktop_rect_t rect = desktop_icon_rect(display, index);
    uint32_t selected = manager->selected == index;
    uint32_t icon_size = min_u32(rect.height > display->font_height + 6U
        ? rect.height - display->font_height - 6U : 12U, 28U);
    desktop_rect_t symbol = {
        rect.x + 5,
        rect.y + 3,
        icon_size,
        icon_size
    };
    if (selected) draw_bevel(rect, color_face, 0U);
    draw_bevel(symbol, apps[index].accent, 1U);
    if (symbol.width > 8U && symbol.height > 8U) {
        (void)x86os_fill_rect(symbol.x + 4, symbol.y + 4,
                              symbol.width - 8U, symbol.height - 8U,
                              color_client);
    }
    uint32_t text_y_offset = rect.height > display->font_height + 3U
        ? rect.height - display->font_height - 3U : 0U;
    draw_text_bounded(display, rect.x + 4,
                      rect.y + (int32_t)text_y_offset,
                      apps[index].title, rect.width - 8U,
                      selected ? color_title_text : color_title_text,
                      selected ? color_active : color_desktop);
}

static void render_window(const x86os_display_info_t *display,
                          const desktop_wm_t *manager,
                          uint32_t window_index) {
    if (window_index >= DESKTOP_WM_CAPACITY) return;
    const desktop_window_t *window = &manager->windows[window_index];
    if (window->visible == 0U || window->app_index >= APP_COUNT) return;
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
    uint32_t active = manager->focused == (int32_t)window_index;
    uint32_t title_color = active ? color_active : color_inactive;

    (void)x86os_fill_rect(shadow.x, shadow.y, shadow.width, shadow.height,
                          color_dark);
    draw_bevel(frame, color_face, 1U);
    (void)x86os_fill_rect(title.x, title.y, title.width, title.height,
                          title_color);
    (void)x86os_fill_rect(client.x, client.y, client.width, client.height,
                          color_client);

    desktop_rect_t close = desktop_wm_close_rect(manager, window_index);
    draw_bevel(close, color_face, 1U);
    if (close.width > 8U && close.height > 8U) {
        (void)x86os_fill_rect(close.x + 4, close.y + 4,
                              close.width - 8U, close.height - 8U,
                              color_dark);
    }

    uint32_t title_x = (uint32_t)(close.x - title.x) + close.width + 6U;
    uint32_t title_y = title.height > display->font_height
        ? (title.height - display->font_height) / 2U : 0U;
    if (title_x + 3U < title.width) {
        draw_text_bounded(display, title.x + (int32_t)title_x,
                          title.y + (int32_t)title_y,
                          apps[window->app_index].title,
                          title.width - title_x - 3U,
                          color_title_text, title_color);
    }

    uint32_t padding = 10U;
    uint32_t line = max_u32(display->font_height + 5U, 18U);
    if (client.width > padding * 2U && client.height > padding * 2U) {
        uint32_t text_width = client.width - padding * 2U;
        int32_t text_x = client.x + (int32_t)padding;
        int32_t text_y = client.y + (int32_t)padding;
        draw_text_bounded(display, text_x, text_y,
                          apps[window->app_index].description, text_width,
                          color_text, color_client);
        if (client.height > padding * 2U + line) {
            draw_text_bounded(display, text_x, text_y + (int32_t)line,
                              apps[window->app_index].program, text_width,
                              apps[window->app_index].accent, color_client);
        }
        if (client.height > padding * 2U + line * 3U) {
            draw_text_bounded(display, text_x, text_y + (int32_t)(line * 3U),
                              "ENTER startet Legacy-App im Vollbild",
                              text_width, color_shadow, color_client);
        }
    }
}

static void render_desktop(const x86os_display_info_t *display,
                           const desktop_wm_t *manager) {
    uint32_t menu = menu_height(display);
    uint32_t status = status_height(display);
    desktop_rect_t menu_rect = {0, 0, display->width, menu};
    desktop_rect_t status_rect = {
        0, (int32_t)(display->height - status), display->width, status
    };

    (void)x86os_fill_rect(0, 0, display->width, display->height,
                          color_desktop);
    draw_bevel(menu_rect, color_face, 1U);
    draw_text_bounded(display, 10,
                      (int32_t)((menu - display->font_height) / 2U),
                      "REIST Desktop   Fenster   Hilfe",
                      display->width > 20U ? display->width - 20U : 1U,
                      color_text, color_face);

    for (uint32_t index = 0U; index < APP_COUNT; ++index)
        render_icon(display, manager, index);

    for (uint32_t position = 0U; position < DESKTOP_WM_CAPACITY; ++position) {
        uint32_t window_index = manager->z_order[position];
        if (window_index < DESKTOP_WM_CAPACITY)
            render_window(display, manager, window_index);
    }

    draw_bevel(status_rect, color_face, 0U);
    draw_text_bounded(display, 10,
                      status_rect.y +
                          (int32_t)((status - display->font_height) / 2U),
                      "Maus: Fokus / Ziehen / Schliessen   ENTER: Start   ESC: Shell",
                      display->width > 20U ? display->width - 20U : 1U,
                      color_text, color_face);
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

int main(void) {
    x86os_display_info_t display;
    desktop_wm_t manager;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t previous_buttons = 0U;
    unsigned int runtime_activated = 0U;

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
    render_desktop(&display, &manager);
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);

    for (;;) {
        int key = read_key();
        unsigned int redraw = 0U;
        unsigned int mouse_events = 0U;
        for (; mouse_events < 32U; ++mouse_events) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            move_pointer(&display, &pointer_x, &pointer_y,
                         mouse.delta_x, mouse.delta_y);

            uint32_t left_down =
                (mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t left_was_down =
                (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            if (left_down && !left_was_down) {
                int window = desktop_wm_window_at(&manager,
                                                   pointer_x, pointer_y);
                if (window != DESKTOP_WM_NO_WINDOW) {
                    redraw |= desktop_wm_pointer_press(&manager,
                                                       pointer_x, pointer_y);
                } else {
                    int icon = desktop_icon_at_position(&display,
                                                        pointer_x, pointer_y);
                    if (icon != DESKTOP_WM_NO_WINDOW)
                        redraw |= desktop_wm_open(&manager, (uint32_t)icon);
                }
            } else if (left_down) {
                redraw |= desktop_wm_pointer_motion(&manager,
                                                     pointer_x, pointer_y);
            } else if (left_was_down) {
                redraw |= desktop_wm_pointer_release(&manager,
                                                      pointer_x, pointer_y);
            }
            previous_buttons = mouse.buttons;
        }

        if (key == '\t' || key == DESKTOP_KEY_RIGHT) {
            uint32_t next = (manager.selected + 1U) % APP_COUNT;
            redraw |= desktop_wm_select(&manager, next);
        } else if (key == DESKTOP_KEY_LEFT) {
            uint32_t next = (manager.selected + APP_COUNT - 1U) % APP_COUNT;
            redraw |= desktop_wm_select(&manager, next);
        } else if (key == DESKTOP_KEY_UP) {
            uint32_t next = (manager.selected + APP_COUNT - 2U) % APP_COUNT;
            redraw |= desktop_wm_select(&manager, next);
        } else if (key == DESKTOP_KEY_DOWN) {
            uint32_t next = (manager.selected + 2U) % APP_COUNT;
            redraw |= desktop_wm_select(&manager, next);
        } else if (key == '\r' || key == '\n') {
            redraw |= desktop_wm_open(&manager, manager.selected);
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            launch_app(manager.selected);
            redraw = 1U;
        } else if (key == DESKTOP_KEY_ESCAPE) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            if (runtime_activated && x86os_display_deactivate() != 0) {
                x86os_puts("desktop: VGA-Rueckkehr fehlgeschlagen\n");
                (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
                continue;
            }
            if (!runtime_activated) x86os_clear();
            x86os_puts("DESKTOP_EXIT_OK\n");
            return 0;
        }

        if (redraw) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            render_desktop(&display, &manager);
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if (mouse_events != 0U) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else {
            (void)x86os_sleep_ms(5U);
        }
    }
}
