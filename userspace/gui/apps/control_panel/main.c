/**
 * @file main.c
 * @brief Fixed-capacity graphical REIST system control panel.
 *
 * The application is an unprivileged Surface client. Mutations cross a fresh
 * Ring-3 CONFIG.PRG process boundary; failed authorization or persistence
 * leaves the panel available in read-only mode.
 */
#include "x86os.h"
#include "reist/config.h"
#include "reist/gui/surface_client.h"
#include "reist/vfs_file_client.h"

#include <stddef.h>
#include <stdint.h>

#define CONTROL_PANEL_APPLET_COUNT 5U
#define CONTROL_PANEL_READ_CAPACITY REIST_CONFIG_FILE_CAPACITY
#define CONTROL_PANEL_CREATE_ATTEMPTS 250U
#define CONTROL_PANEL_PAINT_ATTEMPTS 20U

#define CONTROL_PANEL_KEY_ESCAPE 0x101U
#define CONTROL_PANEL_KEY_UP 0x102U
#define CONTROL_PANEL_KEY_DOWN 0x103U
#define CONTROL_PANEL_KEY_LEFT 0x104U
#define CONTROL_PANEL_KEY_RIGHT 0x105U
#define CONTROL_PANEL_KEY_ENTER 13U

typedef struct control_panel_applet {
    const char *label;
    const char *description;
    const char *target;
    const char *path;
    const char *schema;
    const char *key;
    const char *values[3];
    uint32_t value_count;
} control_panel_applet_t;

typedef struct control_panel_state {
    reist_gui_surface_client_t *client;
    uint32_t selected;
    uint32_t current_value[CONTROL_PANEL_APPLET_COUNT];
    uint32_t valid[CONTROL_PANEL_APPLET_COUNT];
    uint32_t exit_requested;
    uint32_t redraw;
    char status[40];
} control_panel_state_t;

static const control_panel_applet_t applets[CONTROL_PANEL_APPLET_COUNT] = {
    {"Tastatur", "Tastaturbelegung", "input", "/etc/reist/input.conf",
     "reist.input/1", "keyboard.layout", {"de", "us", "at"}, 3U},
    {"Maus", "Tasten, Zeiger und Mausrad", 0, 0, 0, 0, {"Oeffnen", 0, 0}, 1U},
    {"System", "Systemsprache", "system", "/etc/reist/system.conf",
     "reist.system/1", "locale", {"de_AT", "en_US", "de_DE"}, 3U},
    {"Desktop", "Oberflaechenthema", "desktop", "/etc/reist/desktop.conf",
     "reist.desktop/1", "theme", {"classic", "contrast", "classic"}, 2U},
    {"Anzeige", "Aufloesung und Anzeigemodus", 0, 0, 0, 0, {"Oeffnen", 0, 0}, 1U},
};

static char config_buffer[CONTROL_PANEL_READ_CAPACITY];
static reist_config_document_t config_document;

static size_t bounded_length(const char *text, size_t capacity) {
    size_t length = 0U;
    if (text == 0) return 0U;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static uint32_t text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0U;
    while (index < REIST_CONFIG_VALUE_CAPACITY && left[index] != '\0' &&
           right[index] != '\0') {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return index < REIST_CONFIG_VALUE_CAPACITY &&
           left[index] == '\0' && right[index] == '\0';
}

static void set_status(control_panel_state_t *state, const char *status) {
    size_t length = bounded_length(status, sizeof(state->status));
    if (length >= sizeof(state->status)) length = sizeof(state->status) - 1U;
    for (size_t index = 0U; index < length; ++index)
        state->status[index] = status[index];
    state->status[length] = '\0';
}

static int read_config(const control_panel_applet_t *applet,
                       reist_config_document_t *document) {
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = reist_vfs_file_open_rights(
        applet->path, REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT, &handle);
    if (status != 0) return status;
    x86os_file_info_t info;
    status = reist_vfs_file_fstat(handle, &info);
    if (status != 0 || info.type != X86OS_FILE ||
        info.size > sizeof(config_buffer)) {
        (void)reist_vfs_file_close(handle);
        return status != 0 ? status : -75;
    }
    size_t used = 0U;
    while (used < info.size) {
        size_t remaining = info.size - used;
        size_t request = remaining > X86OS_VFS_SHADOW_READ_CAPACITY
            ? X86OS_VFS_SHADOW_READ_CAPACITY : remaining;
        int amount = reist_vfs_file_read(
            handle, config_buffer + used, request);
        if (amount <= 0) {
            (void)reist_vfs_file_close(handle);
            return amount < 0 ? amount : -5;
        }
        used += (size_t)amount;
    }
    char extra = 0;
    int extra_status = reist_vfs_file_read(handle, &extra, 1U);
    int close_status = reist_vfs_file_close(handle);
    if (extra_status != 0 || close_status != 0) return -75;
    return reist_config_parse(
        config_buffer, used, applet->schema, document);
}

static void load_applet(control_panel_state_t *state, uint32_t index) {
    if (index >= CONTROL_PANEL_APPLET_COUNT) return;
    if (index == 4U || index == 1U) { state->valid[index] = 1U; state->current_value[index] = 0U; return; }
    const control_panel_applet_t *applet = &applets[index];
    state->valid[index] = 0U;
    if (read_config(applet, &config_document) != 0) return;
    const char *value = reist_config_get(&config_document, applet->key);
    for (uint32_t option = 0U; option < applet->value_count; ++option) {
        if (text_equal(value, applet->values[option])) {
            state->current_value[index] = option;
            state->valid[index] = 1U;
            return;
        }
    }
}

static void load_all(control_panel_state_t *state) {
    for (uint32_t index = 0U; index < CONTROL_PANEL_APPLET_COUNT; ++index)
        load_applet(state, index);
}

static int apply_selected(control_panel_state_t *state, int32_t direction) {
    if (state->selected == 1U) {
        int status = reist_gui_surface_client_open_mouse(state->client);
        set_status(state, status == 0 ? "Maus angefordert" : "Maus konnte nicht geoeffnet werden");
        state->redraw = 1U;
        return status;
    }
    if (state->selected == 4U) {
        int status = reist_gui_surface_client_open_display(state->client);
        set_status(state, status == 0 ? "Anzeige angefordert" : "Anzeige konnte nicht geoeffnet werden");
        state->redraw = 1U;
        return status;
    }
    const control_panel_applet_t *applet = &applets[state->selected];
    if (!state->valid[state->selected]) {
        set_status(state, "Nur-Lese-Modus: Konfiguration ungueltig");
        state->redraw = 1U;
        return -1;
    }
    uint32_t current = state->current_value[state->selected];
    uint32_t next = direction < 0
        ? (current == 0U ? applet->value_count - 1U : current - 1U)
        : (current + 1U) % applet->value_count;
    const char *arguments[] = {
        "/sbin/config.prg", "set", applet->target, applet->key,
        applet->values[next],
    };
    int pid = x86os_spawnv("/sbin/config.prg", 5, arguments);
    int child_status = 1;
    if (pid < 0 || x86os_wait(pid, &child_status) != pid || child_status != 0) {
        set_status(state, "Nur-Lese-Modus: Aenderung verweigert");
        state->redraw = 1U;
        return -1;
    }
    load_applet(state, state->selected);
    if (!state->valid[state->selected] ||
        state->current_value[state->selected] != next) {
        set_status(state, "Nur-Lese-Modus: Ruecklesen fehlgeschlagen");
        state->redraw = 1U;
        return -1;
    }
    set_status(state, "Gespeichert - wirksam nach Neustart");
    state->redraw = 1U;
    return 0;
}

static reist_gui_rect_t applet_rect(uint32_t index) {
    return (reist_gui_rect_t){16, 44 + (int32_t)(index * 66U), 176U, 54U};
}

static reist_gui_rect_t value_rect(uint32_t width, uint32_t height) {
    uint32_t available_width = width > 230U ? width - 230U : 1U;
    uint32_t button_width = available_width > 250U ? 250U : available_width;
    uint32_t y = height > 170U ? 128U : height / 2U;
    return (reist_gui_rect_t){216, (int32_t)y, button_width, 42U};
}

static uint32_t point_in_rect(reist_gui_rect_t rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y &&
        (uint32_t)(x - rect.x) < rect.width &&
        (uint32_t)(y - rect.y) < rect.height;
}

static int paint_fill(reist_gui_surface_client_t *client,
                      reist_gui_rect_t rect, uint32_t color) {
    if (rect.width == 0U || rect.height == 0U) return 0;
    return reist_gui_surface_client_paint_fill(client, rect, color);
}

static int paint_text(reist_gui_surface_client_t *client, int32_t x, int32_t y,
                      uint32_t width, const char *text, uint32_t foreground,
                      uint32_t background) {
    size_t length = bounded_length(
        text, REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY);
    return reist_gui_surface_client_paint_text(
        client, x, y, width, text, (uint32_t)length,
        foreground, background);
}

static int render(control_panel_state_t *state,
                  reist_gui_surface_client_t *client) {
    static const uint32_t face = 0x00C8C8C8U;
    static const uint32_t dark = 0x00202020U;
    static const uint32_t active = 0x00000088U;
    static const uint32_t white = 0x00FFFFFFU;
    if (reist_gui_surface_client_paint_begin(client) != 0) return -1;
    if (paint_fill(client, (reist_gui_rect_t){0, 0, client->width,
                   client->height}, face) != 0 ||
        paint_text(client, 16, 14, client->width > 32U ? client->width - 32U : 1U,
                   "Systemsteuerung", dark, face) != 0) return -1;
    for (uint32_t index = 0U; index < CONTROL_PANEL_APPLET_COUNT; ++index) {
        reist_gui_rect_t rect = applet_rect(index);
        uint32_t background = state->selected == index ? active : white;
        uint32_t foreground = state->selected == index ? white : dark;
        if (paint_fill(client, rect, background) != 0 ||
            paint_text(client, rect.x + 8, rect.y + 18,
                       rect.width > 16U ? rect.width - 16U : 1U,
                       applets[index].label, foreground, background) != 0)
            return -1;
    }
    const control_panel_applet_t *selected = &applets[state->selected];
    if (paint_text(client, 216, 58,
                   client->width > 232U ? client->width - 232U : 1U,
                   selected->description, dark, face) != 0) return -1;
    const char *value = state->valid[state->selected]
        ? selected->values[state->current_value[state->selected]]
        : "ungueltig";
    reist_gui_rect_t button = value_rect(client->width, client->height);
    if (paint_fill(client, button, active) != 0 ||
        paint_text(client, button.x + 10, button.y + 13,
                   button.width > 20U ? button.width - 20U : 1U,
                   value, white, active) != 0 ||
        paint_text(client, 216, button.y + 58,
                   client->width > 232U ? client->width - 232U : 1U,
                   "Klick oder LINKS/RECHTS/ENTER", dark, face) != 0 ||
        paint_text(client, 16,
                   client->height > 28U ? (int32_t)client->height - 24 : 0,
                   client->width > 32U ? client->width - 32U : 1U,
                   state->status, dark, face) != 0) return -1;
    return reist_gui_surface_client_paint_commit(client);
}

static void handle_pointer(control_panel_state_t *state,
                           const reist_gui_surface_client_t *client,
                           const reist_gui_surface_input_t *input) {
    if (input->type != REIST_GUI_SURFACE_INPUT_POINTER_BUTTON ||
        input->button != 1U || !input->pressed) return;
    for (uint32_t index = 0U; index < CONTROL_PANEL_APPLET_COUNT; ++index) {
        if (point_in_rect(applet_rect(index), input->x, input->y)) {
            state->selected = index;
            set_status(state, "Applet ausgewaehlt");
            state->redraw = 1U;
            return;
        }
    }
    if (point_in_rect(value_rect(client->width, client->height),
                      input->x, input->y))
        (void)apply_selected(state, 1);
}

static void handle_keyboard(control_panel_state_t *state, uint32_t key) {
    if (key == CONTROL_PANEL_KEY_ESCAPE || key == 27U) {
        state->exit_requested = 1U;
        return;
    }
    if (key == CONTROL_PANEL_KEY_UP) {
        state->selected = state->selected == 0U
            ? CONTROL_PANEL_APPLET_COUNT - 1U : state->selected - 1U;
    } else if (key == CONTROL_PANEL_KEY_DOWN || key == '\t') {
        state->selected = (state->selected + 1U) % CONTROL_PANEL_APPLET_COUNT;
    } else if (key == CONTROL_PANEL_KEY_LEFT) {
        (void)apply_selected(state, -1);
        return;
    } else if (key == CONTROL_PANEL_KEY_RIGHT ||
               key == CONTROL_PANEL_KEY_ENTER || key == '\n' || key == ' ') {
        (void)apply_selected(state, 1);
        return;
    } else return;
    set_status(state, "Applet ausgewaehlt");
    state->redraw = 1U;
}

int main(int argc, char **argv) {
    x86os_ipc_handle_t endpoint = 0U;
    if (reist_gui_surface_endpoint_from_argv(argc, argv, &endpoint) != 0) {
        x86os_puts("control: compositor endpoint missing\n");
        return 2;
    }
    reist_gui_surface_client_t client;
    if (reist_gui_surface_client_init(&client, endpoint) != 0) return 1;
    int status = -9;
    for (uint32_t attempt = 0U; attempt < CONTROL_PANEL_CREATE_ATTEMPTS;
         ++attempt) {
        status = reist_gui_surface_client_create(
            &client, REIST_GUI_SURFACE_ROLE_TOPLEVEL, 620U, 450U);
        if (status == 0 || (status != -9 && status != -13)) break;
        (void)x86os_sleep_ms(1U);
    }
    if (status != 0 || reist_gui_surface_client_ack_configure(
            &client, client.configured_serial) != 0 ||
        reist_gui_surface_client_set_title(&client, "Systemsteuerung") != 0) {
        (void)x86os_ipc_release(endpoint);
        return 1;
    }

    control_panel_state_t state = {0};
    state.client = &client;
    load_all(&state);
    set_status(&state, "Bereit");
    state.redraw = 1U;
    x86os_puts("CONTROL_PANEL_READY\n");
    while (!state.exit_requested) {
        if (state.redraw) {
            int paint_status = -11;
            for (uint32_t attempt = 0U; attempt < CONTROL_PANEL_PAINT_ATTEMPTS;
                 ++attempt) {
                paint_status = render(&state, &client);
                if (paint_status == 0) break;
                (void)x86os_sleep_ms(5U);
            }
            if (paint_status != 0) break;
            state.redraw = 0U;
        }
        reist_gui_surface_message_t message;
        status = reist_gui_surface_client_receive(&client, &message, 0U);
        if (status == -11) {
            (void)x86os_sleep_ms(5U);
            continue;
        }
        if (status != 0) break;
        if (message.type == REIST_GUI_SURFACE_CLOSE) break;
        if (message.type == REIST_GUI_SURFACE_CONFIGURE) {
            if (reist_gui_surface_client_accept_configure(
                    &client, &message) != 0) break;
            state.redraw = 1U;
        } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                   message.input.type == REIST_GUI_SURFACE_INPUT_KEYBOARD &&
                   message.input.pressed) {
            handle_keyboard(&state, message.input.key);
        } else if (message.type == REIST_GUI_SURFACE_INPUT) {
            handle_pointer(&state, &client, &message.input);
        }
    }
    (void)reist_gui_surface_client_destroy(&client);
    (void)x86os_ipc_release(endpoint);
    return 0;
}
