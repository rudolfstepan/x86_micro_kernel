/**
 * @file userspace/programs/desktop.c
 * @brief Startet die grafische Userspace-Oberfläche.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

#define APP_COUNT 4U

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
    {"Shell",   "Terminal und Befehle",   "/bin/shell.prg",       0,             0x003B82F6U},
    {"Dateien", "Dateien und Laufwerke",  "/bin/ls.prg",          0,             0x0010B981U},
    {"Editor",  "Textdateien bearbeiten", "/bin/edit.prg",        "desktop.txt", 0x00A855F7U},
    {"System",  "Systeminformationen",    "/sbin/sysinfo.prg",    0,             0x00F59E0BU},
};

static const uint32_t color_background = 0x000B1220U;
static const uint32_t color_topbar = 0x00111B2EU;
static const uint32_t color_card = 0x0018263BU;
static const uint32_t color_card_selected = 0x00223552U;
static const uint32_t color_border = 0x00344A67U;
static const uint32_t color_text = 0x00F8FAFCU;
static const uint32_t color_muted = 0x009FB0C5U;

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static void draw_text(int32_t x, int32_t y, const char *text,
                      uint32_t foreground, uint32_t background) {
    (void)x86os_draw_text_pixels(x, y, text, text_length(text),
                                 foreground, background);
}

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static void render_card(const x86os_display_info_t *display,
                        unsigned int index, unsigned int selected,
                        uint32_t margin, uint32_t top, uint32_t gap,
                        uint32_t card_width, uint32_t card_height) {
    uint32_t column = index % 2U;
    uint32_t row = index / 2U;
    int32_t x = (int32_t)(margin + column * (card_width + gap));
    int32_t y = (int32_t)(top + row * (card_height + gap));
    uint32_t background = index == selected ? color_card_selected : color_card;
    uint32_t font_width = max_u32(display->font_width, 8U);
    uint32_t font_height = max_u32(display->font_height, 16U);
    uint32_t padding = max_u32(margin / 3U, 6U);

    (void)x86os_fill_rect(x + 4, y + 5, card_width, card_height, 0x00060B13U);
    (void)x86os_fill_rect(x, y, card_width, card_height,
                          index == selected ? apps[index].accent : color_border);
    (void)x86os_fill_rect(x + 2, y + 2, card_width - 4U, card_height - 4U,
                          background);
    (void)x86os_fill_rect(x + 2, y + 2, 7U, card_height - 4U,
                          apps[index].accent);

    draw_text(x + (int32_t)padding, y + (int32_t)padding,
              apps[index].title, color_text, background);
    if (card_height >= padding * 2U + font_height * 4U) {
        draw_text(x + (int32_t)padding,
                  y + (int32_t)(padding + font_height * 2U),
                  apps[index].description, color_muted, background);
    }
    draw_text(x + (int32_t)padding,
              y + (int32_t)(card_height - padding - font_height),
              apps[index].program,
              index == selected ? apps[index].accent : color_muted,
              background);

    if (index == selected && card_width > 24U * font_width) {
        const char *action = "ENTER: STARTEN";
        uint32_t action_width = (uint32_t)text_length(action) * font_width;
        draw_text(x + (int32_t)(card_width - padding - action_width),
                  y + (int32_t)(card_height - padding - font_height),
                  action, color_text, background);
    }
}

static void render_desktop(const x86os_display_info_t *display,
                           unsigned int selected) {
    uint32_t font_width = max_u32(display->font_width, 8U);
    uint32_t font_height = max_u32(display->font_height, 16U);
    uint32_t margin = display->width / 24U;
    uint32_t topbar_height = max_u32(font_height * 3U, 52U);
    uint32_t footer_height = max_u32(font_height * 3U, 48U);
    uint32_t gap;
    uint32_t top;
    uint32_t available_height;
    uint32_t card_width;
    uint32_t card_height;
    const char *status = "RING 3  |  BEREIT";
    const char *help = "TAB / PFEILE: AUSWAHL    ENTER: START    ESC: SHELL";
    uint32_t status_width = (uint32_t)text_length(status) * font_width;

    if (margin < 16U) margin = 16U;
    gap = margin;
    top = topbar_height + margin;
    card_width = (display->width - margin * 2U - gap) / 2U;
    available_height = display->height - top - footer_height - margin;
    card_height = (available_height - gap) / 2U;

    (void)x86os_fill_rect(0, 0, display->width, display->height,
                          color_background);
    (void)x86os_fill_rect(0, 0, display->width, topbar_height, color_topbar);
    (void)x86os_fill_rect(0, (int32_t)topbar_height - 2,
                          display->width, 2U, apps[selected].accent);
    draw_text((int32_t)margin,
              (int32_t)((topbar_height - font_height) / 2U),
              "REIST OS Desktop", color_text, color_topbar);
    if (status_width + margin < display->width) {
        draw_text((int32_t)(display->width - margin - status_width),
                  (int32_t)((topbar_height - font_height) / 2U),
                  status, color_muted, color_topbar);
    }

    for (unsigned int index = 0; index < APP_COUNT; ++index) {
        render_card(display, index, selected, margin, top, gap,
                    card_width, card_height);
    }

    draw_text((int32_t)margin,
              (int32_t)(display->height - footer_height + font_height),
              help, color_muted, color_background);
}

static int app_at_position(const x86os_display_info_t *display,
                           int32_t pointer_x, int32_t pointer_y) {
    uint32_t font_height = max_u32(display->font_height, 16U);
    uint32_t margin = display->width / 24U;
    uint32_t topbar_height = max_u32(font_height * 3U, 52U);
    uint32_t footer_height = max_u32(font_height * 3U, 48U);
    if (margin < 16U) margin = 16U;
    uint32_t gap = margin;
    uint32_t top = topbar_height + margin;
    uint32_t card_width = (display->width - margin * 2U - gap) / 2U;
    uint32_t available_height =
        display->height - top - footer_height - margin;
    uint32_t card_height = (available_height - gap) / 2U;
    for (unsigned int index = 0U; index < APP_COUNT; ++index) {
        uint32_t column = index % 2U;
        uint32_t row = index / 2U;
        int32_t x = (int32_t)(margin + column * (card_width + gap));
        int32_t y = (int32_t)(top + row * (card_height + gap));
        if (pointer_x >= x && pointer_y >= y &&
            pointer_x < x + (int32_t)card_width &&
            pointer_y < y + (int32_t)card_height) return (int)index;
    }
    return -1;
}

static int read_escape_byte(void) {
    for (unsigned int attempt = 0; attempt < 20U; ++attempt) {
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

    /* A CSI sequence ends at its first 0x40..0x7e byte.  Consume unknown
     * sequences completely so Home/PageUp/F-keys cannot leak a trailing '~'
     * into a child or be mistaken for the desktop's bare-Escape shortcut. */
    for (;;) {
        value = read_escape_byte();
        if (value == 0) return DESKTOP_KEY_NONE;
        if (value < 0x40 || value > 0x7E) continue;
        if (value == 'A') return DESKTOP_KEY_UP;
        if (value == 'B') return DESKTOP_KEY_DOWN;
        if (value == 'C') return DESKTOP_KEY_RIGHT;
        if (value == 'D') return DESKTOP_KEY_LEFT;
        return DESKTOP_KEY_NONE;
    }
}

static void drain_input(void) {
    while (x86os_getchar_nonblocking() != 0) {
    }
}

static void print_integer(int value) {
    char digits[12];
    unsigned int count = 0;
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

static void launch_app(const x86os_display_info_t *display,
                       unsigned int index) {
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
    render_desktop(display, index);
}

int main(void) {
    x86os_display_info_t display;
    unsigned int selected = 0;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t previous_buttons = 0U;

    int display_status = x86os_display_info(&display);
    if (display_status != 0) {
        (void)x86os_display_activate();
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

    pointer_x = (int32_t)(display.width / 2U);
    pointer_y = (int32_t)(display.height / 2U);
    render_desktop(&display, selected);
    x86os_puts("DESKTOP_OK\n");
    render_desktop(&display, selected);
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);

    for (;;) {
        int key = read_key();
        unsigned int previous = selected;
        unsigned int redraw = 0U;
        unsigned int mouse_events = 0U;
        for (; mouse_events < 32U; ++mouse_events) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            pointer_x += mouse.delta_x;
            pointer_y += mouse.delta_y;
            if (pointer_x < 0) pointer_x = 0;
            if (pointer_y < 0) pointer_y = 0;
            if (pointer_x >= (int32_t)display.width)
                pointer_x = (int32_t)display.width - 1;
            if (pointer_y >= (int32_t)display.height)
                pointer_y = (int32_t)display.height - 1;
            if ((mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U &&
                (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) == 0U) {
                int hit = app_at_position(&display, pointer_x, pointer_y);
                if (hit >= 0) selected = (unsigned int)hit;
            }
            previous_buttons = mouse.buttons;
        }

        if (key == '\t' || key == DESKTOP_KEY_RIGHT) {
            selected = (selected + 1U) % APP_COUNT;
        } else if (key == DESKTOP_KEY_LEFT) {
            selected = (selected + APP_COUNT - 1U) % APP_COUNT;
        } else if (key == DESKTOP_KEY_UP || key == DESKTOP_KEY_DOWN) {
            selected = (selected + 2U) % APP_COUNT;
        } else if (key == '\r' || key == '\n') {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            launch_app(&display, selected);
            redraw = 1U;
        } else if (key == DESKTOP_KEY_ESCAPE) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            x86os_puts("DESKTOP_EXIT_OK\n");
            x86os_clear();
            return 0;
        }

        if (selected != previous) redraw = 1U;
        if (redraw) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            render_desktop(&display, selected);
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if (mouse_events != 0U) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else {
            (void)x86os_sleep_ms(5U);
        }
    }
}
