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
#include "desktop_drag.h"
#include "desktop_explorer.h"
#include "desktop_file_move.h"
#include "desktop_filetypes.h"
#include "desktop_layout.h"
#include "desktop_shortcut.h"
#include "desktop_trash.h"
#include "desktop_wm.h"
#include "desktop_surface.h"
#include "desktop_surface_runtime.h"
#include "reist/image.h"
#include "reist/config.h"
#include "reist/vfs_file_client.h"
#include "reist/gui/dialog.h"
#include "reist/gui/font.h"
#include "reist/gui/font_catalog.h"
#include "reist/gui/menu.h"
#include "../../video/include/reist/svga2d.h"
#include "../../../include/reist/utf.h"
#include "../../../include/reist/unicode_vga_font.h"

#define DESKTOP_BUILTIN_ICON_COUNT 3U
#define DESKTOP_ICON_WIDTH 176U
#define DESKTOP_SVGA2D_CONNECT_ATTEMPTS 3U
#define DESKTOP_SVGA2D_RETRY_MS 50U
#define DESKTOP_SVGA2D_RECONNECT_MS 1000U
#define DESKTOP_SVGA2D_PROBE_READY_DEADLINE_MS 2000U
#define DESKTOP_SVGA2D_REPLY_MS 100U
#define DESKTOP_SVGA2D_ACTIVATE_REPLY_MS 500U
#define DESKTOP_ARGUMENT_LIMIT 32U
#define DESKTOP_MENU_FAST_FEEDBACK_WIDTH 4U
#define DESKTOP_MENU_COUNT 1U
#define DESKTOP_METRICS_VERSION 1U
#define DESKTOP_RENDER_FALLBACK (1U << 0)
#define DESKTOP_RENDER_ACCELERATED (1U << 1)
#define DESKTOP_RENDER_ACCELERATION_FALLBACK (1U << 2)
#define DESKTOP_RENDER_PROBE_STEPS 8U
#define DESKTOP_RENDER_PROBE_STEP_X 4
#define DESKTOP_MOUSE_BATCH_LIMIT 4U
#define DESKTOP_IDLE_POLL_MS 1U
#define DESKTOP_POINTER_CONTINUOUS_INPUT_INTERVAL_MS 16U
#define DESKTOP_HOVER_PROBE_VERSION 1U
#define DESKTOP_HOVER_PROBE_ITEMS 7U
#define DESKTOP_FILE_ICON_SIZE 32U
#define DESKTOP_FILE_ICON_PIXELS \
    (DESKTOP_FILE_ICON_SIZE * DESKTOP_FILE_ICON_SIZE)
#define DESKTOP_FILE_ICON_ENCODED_CAPACITY 8192U
#define DESKTOP_FILE_READ_TIMEOUT_MS 2000U
#define DESKTOP_SHORTCUT_PROBE_RESTART_TIMEOUT_MS 5000U
#define DESKTOP_FONT_FILE_CAPACITY (3U * 1024U * 1024U)
#define DESKTOP_FONT_MAPPING_CAPACITY 262144U
#define DESKTOP_FONT_PATH "/usr/share/fonts/reist-unicode.psf"
#define DESKTOP_EDITOR_FONT_FILE_CAPACITY 12288U
#define DESKTOP_EDITOR_FONT_MAPPING_CAPACITY 128U
#define DESKTOP_FONT_GLYPH_CACHE_CAPACITY 64U
#define DESKTOP_SPLASH_WIDTH 512U
#define DESKTOP_SPLASH_HEIGHT 288U
#define DESKTOP_SPLASH_BMP_HEADER_SIZE 54U
#define DESKTOP_SPLASH_BYTES_PER_PIXEL 3U
#define DESKTOP_SPLASH_STRIP_ROWS 36U
#define DESKTOP_SPLASH_STRIP_PIXELS \
    (DESKTOP_SPLASH_WIDTH * DESKTOP_SPLASH_STRIP_ROWS)
#define DESKTOP_SPLASH_BACKGROUND 0x00040A18U
#define DESKTOP_SPLASH_FOREGROUND 0x00E8FAFFU
#define DESKTOP_CLOCK_TEXT_CAPACITY 17U
#define DESKTOP_CLOCK_POLL_MS 1000U
#define DESKTOP_CLOCK_FALLBACK_POLLS 200U
#define DESKTOP_ACTION_OPEN_CONTROL_PANEL (1U << 16)
#define DESKTOP_ACTION_OPEN_TRASH (1U << 17)
#define DESKTOP_ACTION_EMPTY_TRASH (1U << 18)
#define DESKTOP_ACTION_CREATE_SHORTCUT (1U << 19)
#define DESKTOP_ACTION_OPEN_SHORTCUT (1U << 20)
#define DESKTOP_ACTION_REMOVE_SHORTCUT (1U << 21)
#define DESKTOP_ACTION_OPEN_BROWSER (1U << 22)
#define DESKTOP_TASKBAR_CAPTURE_BACKGROUND DESKTOP_WM_CAPACITY
#define DESKTOP_TRASH_TARGET_ID 1U
#define DESKTOP_DIRECTORY_TARGET_DESKTOP 2U
#define DESKTOP_DIRECTORY_TARGET_WINDOW_BASE 16U
#define DESKTOP_DIRECTORY_TARGET_CHILD_BASE 32U
#define DESKTOP_DIRECTORY_TARGET_DESKTOP_CHILD_BASE \
    (DESKTOP_DIRECTORY_TARGET_CHILD_BASE + \
     DESKTOP_WM_CAPACITY * DESKTOP_EXPLORER_ENTRY_CAPACITY)
#define DESKTOP_LAYOUT_TARGET_ID 2048U
#define DESKTOP_LAYOUT_SOURCE_ID 2049U
#define DESKTOP_LAYOUT_IO_TIMEOUT_MS 10000U
#define DESKTOP_LAYOUT_IO_CALL_CAPACITY 144U
#define DESKTOP_TRASH_ICON_COUNT 2U
#define DESKTOP_DRAG_FEEDBACK_SIZE 34U
#define DESKTOP_TRASH_ACTION_HEIGHT 34U
#define DESKTOP_EXPLORER_TOOLBAR_HEIGHT 32U
#define DESKTOP_EXPLORER_ADDRESS_HEIGHT 26U
#define DESKTOP_EXPLORER_STATUS_HEIGHT 24U
#define DESKTOP_EXPLORER_BUTTON_GAP 4U
#define DESKTOP_EXPLORER_BACK_WIDTH 96U
#define DESKTOP_EXPLORER_SMALL_BUTTON_WIDTH 36U
#define DESKTOP_EXPLORER_REFRESH_WIDTH 64U
#define DESKTOP_EXPLORER_VIEW_WIDTH 80U
#define DESKTOP_EXPLORER_DETAILS_ICON_SIZE 18U
#define DESKTOP_SYSTEM_SOUND_CONFIG_PATH "/etc/reist/sounds.conf"
#define DESKTOP_SYSTEM_SOUND_SCHEMA "reist.sounds/1"
#define DESKTOP_SYSTEM_SOUND_PLAYER "/usr/bin/wavplay.prg"
#define DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY 2U
#define DESKTOP_SURFACE_PROBE_READY_ATTEMPTS 3000U

_Static_assert(DESKTOP_EXPLORER_WINDOW_CAPACITY == DESKTOP_WM_CAPACITY,
               "explorer and window-manager capacities must match");
_Static_assert(DESKTOP_BUILTIN_ICON_COUNT + DESKTOP_EXPLORER_ENTRY_CAPACITY <=
                   DESKTOP_LAYOUT_ENTRY_CAPACITY,
               "desktop layout capacity must cover every visible icon");

enum {
    DESKTOP_KEY_NONE = 0x100,
    DESKTOP_KEY_ESCAPE,
    DESKTOP_KEY_UP,
    DESKTOP_KEY_DOWN,
    DESKTOP_KEY_LEFT,
    DESKTOP_KEY_RIGHT
};

typedef enum {
    DESKTOP_SYSTEM_SOUND_STARTUP = 0,
    DESKTOP_SYSTEM_SOUND_SHUTDOWN,
    DESKTOP_SYSTEM_SOUND_ERROR,
    DESKTOP_SYSTEM_SOUND_NOTIFICATION,
    DESKTOP_SYSTEM_SOUND_TRASH_DROP,
    DESKTOP_SYSTEM_SOUND_TRASH_EMPTY,
    DESKTOP_SYSTEM_SOUND_EVENT_COUNT,
    DESKTOP_SYSTEM_SOUND_NONE = UINT32_MAX,
} desktop_system_sound_event_t;

typedef struct {
    int32_t pid;
    uint32_t process_generation;
} desktop_system_sound_child_t;

typedef struct {
    uint32_t enabled;
    char paths[DESKTOP_SYSTEM_SOUND_EVENT_COUNT][REIST_CONFIG_VALUE_CAPACITY];
    desktop_system_sound_child_t
        children[DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY];
    uint32_t pending_event;
} desktop_system_sound_state_t;

enum desktop_explorer_navigation_action {
    DESKTOP_EXPLORER_NAVIGATION_NONE = 0U,
    DESKTOP_EXPLORER_NAVIGATION_BACK,
    DESKTOP_EXPLORER_NAVIGATION_FORWARD,
    DESKTOP_EXPLORER_NAVIGATION_UP,
    DESKTOP_EXPLORER_NAVIGATION_REFRESH,
    DESKTOP_EXPLORER_NAVIGATION_VIEW
};

enum desktop_shortcut_probe_restart_phase {
    DESKTOP_SHORTCUT_PROBE_RESTART_IDLE = 0U,
    DESKTOP_SHORTCUT_PROBE_RESTART_RUNNING,
    DESKTOP_SHORTCUT_PROBE_RESTART_COMPLETE,
    DESKTOP_SHORTCUT_PROBE_RESTART_FAILED
};

typedef struct desktop_explorer_chrome {
    desktop_rect_t toolbar;
    desktop_rect_t address;
    desktop_rect_t content;
    desktop_rect_t status;
    desktop_rect_t back;
    desktop_rect_t forward;
    desktop_rect_t up;
    desktop_rect_t refresh;
    desktop_rect_t view;
} desktop_explorer_chrome_t;

typedef struct desktop_explorer_detail_columns {
    desktop_rect_t name;
    desktop_rect_t type;
    desktop_rect_t size;
    desktop_rect_t modified;
} desktop_explorer_detail_columns_t;

static const char *const desktop_system_sound_keys[
        DESKTOP_SYSTEM_SOUND_EVENT_COUNT] = {
    "event.startup",
    "event.shutdown",
    "event.error",
    "event.notification",
    "event.trash_drop",
    "event.trash_empty",
};

typedef struct {
    const char *title;
    const char *path;
    uint32_t accent;
} desktop_icon_t;

static const desktop_icon_t desktop_icons[DESKTOP_BUILTIN_ICON_COUNT] = {
    {"Computer", "/", 0x0000479DU},
    {"Systemsteuerung", "/usr/gui/bin/control.prg", 0x00806020U},
    {"Papierkorb", DESKTOP_TRASH_FILES_PATH, 0x00607080U},
};

static uint32_t control_panel_selected;
static uint32_t control_panel_pressed;
static uint64_t control_panel_last_click_ms;
static uint32_t trash_selected;
static uint32_t trash_pressed;
static uint64_t trash_last_click_ms;
static uint32_t desktop_shortcut_selected = UINT32_MAX;
static uint32_t desktop_shortcut_pressed = UINT32_MAX;
static uint32_t desktop_shortcut_pressed_generation;
static uint32_t desktop_shortcut_last_click = UINT32_MAX;
static uint32_t desktop_shortcut_last_click_generation;
static uint64_t desktop_shortcut_last_click_ms;
static uint32_t desktop_shortcut_probe_enabled;
static int32_t desktop_shortcut_probe_restart_pid;
static uint32_t desktop_shortcut_probe_restart_generation;
static uint32_t desktop_shortcut_probe_restart_phase;
static uint64_t desktop_shortcut_probe_restart_deadline_ms;
static void desktop_shortcut_selection_reset(void);
static desktop_drag_state_t desktop_drag;
static desktop_layout_document_t desktop_layout_document;
static desktop_layout_document_t desktop_layout_candidate;
static desktop_layout_view_t desktop_layout_view;
static desktop_layout_view_t desktop_layout_probe_view;
static desktop_layout_identity_t desktop_layout_identities[
    DESKTOP_LAYOUT_ENTRY_CAPACITY];
static uint8_t desktop_layout_file_bytes[DESKTOP_LAYOUT_FILE_CAPACITY];
static uint32_t desktop_layout_drag_source_index = UINT32_MAX;
static desktop_layout_cell_t desktop_layout_hover_cell;
static uint32_t desktop_layout_hover_valid;
static uint32_t desktop_layout_probe_enabled;
static desktop_trash_state_t desktop_trash;
static uint32_t trash_restore_pressed_window = DESKTOP_WM_NO_TARGET;
static uint32_t explorer_navigation_pressed_window = DESKTOP_WM_NO_TARGET;
static uint32_t explorer_navigation_pressed_action;
static x86os_ipc_handle_t desktop_svga2d_endpoint;
static uint32_t desktop_svga2d_request_id;
static uint32_t desktop_svga2d_capabilities;
static uint32_t desktop_svga2d_observed_capabilities;
static uint32_t desktop_svga2d_reconnects;
static uint32_t desktop_svga2d_reconnect_attempts;
static uint64_t desktop_svga2d_next_reconnect_ms;
static int32_t desktop_svga2d_last_connect_status;
static int32_t desktop_svga2d_last_service_status;
static int32_t desktop_svga2d_last_transaction_status;
static int32_t desktop_svga2d_last_copy_status;
static int32_t desktop_svga2d_last_mark_status;
static uint32_t desktop_low_latency_menu_feedback;

static void desktop_svga2d_forget_endpoint(void) {
    if (desktop_svga2d_endpoint != X86OS_IPC_INVALID_HANDLE)
        (void)x86os_ipc_release(desktop_svga2d_endpoint);
    desktop_svga2d_endpoint = X86OS_IPC_INVALID_HANDLE;
    desktop_svga2d_capabilities = 0U;
}

static int desktop_svga2d_decode_response(
        const x86os_ipc_message_t *ipc,
        reist_svga2d_message_t *response) {
    if (ipc == 0 || response == 0 ||
        ipc->version != X86OS_IPC_MESSAGE_VERSION ||
        ipc->struct_size != sizeof(*ipc) ||
        ipc->length != sizeof(*response)) return -84;
    for (uint32_t index = 0U; index < sizeof(*response); ++index)
        ((uint8_t *)response)[index] = ipc->payload[index];
    if (response->version != REIST_SVGA2D_ABI_VERSION ||
        response->struct_size != sizeof(*response) ||
        response->request_id == 0U ||
        response->operation < REIST_SVGA2D_ACTIVATE ||
        response->operation > REIST_SVGA2D_INFO ||
        response->flags != REIST_SVGA2D_FLAG_RESPONSE)
        return -84;
    return 0;
}

static uint32_t desktop_svga2d_response_is_stale(uint32_t response_id,
                                                 uint32_t request_id) {
    uint32_t age = request_id - response_id;
    return age != 0U && age <= 0x7FFFFFFFU;
}

static int desktop_svga2d_drain_stale_responses(uint32_t request_id) {
    for (uint32_t stale = 0U; stale < X86OS_IPC_QUEUE_DEPTH; ++stale) {
        x86os_ipc_message_t ipc;
        ipc.version = X86OS_IPC_MESSAGE_VERSION;
        ipc.struct_size = sizeof(ipc);
        ipc.length = 0U;
        int status = x86os_ipc_receive_timeout(
            desktop_svga2d_endpoint, &ipc, 0U);
        if (status == -11) return 0;
        if (status != 0) return status;
        reist_svga2d_message_t stale_response;
        status = desktop_svga2d_decode_response(&ipc, &stale_response);
        if (status != 0) return status;
        if (!desktop_svga2d_response_is_stale(
                stale_response.request_id, request_id)) return -84;
    }
    return 0;
}

static int desktop_svga2d_receive_response(
        uint32_t request_id, uint32_t operation,
        reist_svga2d_message_t *wire, uint32_t timeout_ms) {
    uint64_t started_ms = 0U;
    if (timeout_ms == 0U || x86os_monotonic_ms(&started_ms) != 0 ||
        started_ms > UINT64_MAX - timeout_ms) return -84;
    uint64_t deadline_ms = started_ms + timeout_ms;
    for (uint32_t reply = 0U;
         reply < X86OS_IPC_QUEUE_DEPTH + 1U; ++reply) {
        uint64_t now_ms = 0U;
        if (x86os_monotonic_ms(&now_ms) != 0 || now_ms < started_ms)
            return -84;
        if (now_ms >= deadline_ms) return -110;
        x86os_ipc_message_t ipc;
        ipc.version = X86OS_IPC_MESSAGE_VERSION;
        ipc.struct_size = sizeof(ipc);
        ipc.length = 0U;
        int status = x86os_ipc_receive_timeout(
            desktop_svga2d_endpoint, &ipc,
            (uint32_t)(deadline_ms - now_ms));
        if (status != 0) return status;
        reist_svga2d_message_t response;
        status = desktop_svga2d_decode_response(&ipc, &response);
        if (status != 0) return status;
        if (response.request_id == request_id) {
            if (response.operation != operation) return -84;
            *wire = response;
            desktop_svga2d_capabilities = response.capabilities;
            desktop_svga2d_observed_capabilities |= response.capabilities;
            return response.status;
        }
        if (!desktop_svga2d_response_is_stale(
                response.request_id, request_id)) return -84;
        continue;
    }
    return -110;
}

static int desktop_svga2d_transact(reist_svga2d_message_t *wire) {
    if (wire == 0 || desktop_svga2d_endpoint == X86OS_IPC_INVALID_HANDLE)
        return -19;
    if (++desktop_svga2d_request_id == 0U) desktop_svga2d_request_id = 1U;
    uint32_t request_id = desktop_svga2d_request_id;
    uint32_t operation = wire->operation;
    wire->version = REIST_SVGA2D_ABI_VERSION;
    wire->struct_size = sizeof(*wire);
    wire->request_id = request_id;
    x86os_ipc_message_t ipc = {
        .version = X86OS_IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc),
        .length = sizeof(*wire),
    };
    for (uint32_t index = 0U; index < sizeof(*wire); ++index)
        ipc.payload[index] = ((const uint8_t *)wire)[index];
    int status = desktop_svga2d_drain_stale_responses(request_id);
    if (status != 0) {
        desktop_svga2d_forget_endpoint();
        return status;
    }
    status = x86os_ipc_send_timeout(desktop_svga2d_endpoint, &ipc, 50U);
    if (status != 0) {
        desktop_svga2d_forget_endpoint();
        return status;
    }
    uint32_t reply_timeout_ms =
        operation == REIST_SVGA2D_ACTIVATE ||
        operation == REIST_SVGA2D_DEACTIVATE
            ? DESKTOP_SVGA2D_ACTIVATE_REPLY_MS : DESKTOP_SVGA2D_REPLY_MS;
    status = desktop_svga2d_receive_response(
        request_id, operation, wire, reply_timeout_ms);
    if (status == -84 || status == -110 || status == -32 || status == -9) {
        desktop_svga2d_forget_endpoint();
    }
    return status;
}

static int desktop_svga2d_connect(uint32_t activate, uint32_t report_error) {
    if (desktop_svga2d_endpoint == X86OS_IPC_INVALID_HANDLE) {
        int status = x86os_service_connect(
            X86OS_SERVICE_DISPLAY_DRIVER, &desktop_svga2d_endpoint);
        desktop_svga2d_last_service_status = status;
        if (status != 0) {
            desktop_svga2d_last_connect_status = status;
            if (report_error != 0U) {
                x86os_puts("desktop: Beschleunigungsdienst status=");
                x86os_print_number(status);
                x86os_putchar('\n');
            }
            return status;
        }
    }
    reist_svga2d_message_t request = {0};
    request.operation = activate != 0U
        ? REIST_SVGA2D_ACTIVATE : REIST_SVGA2D_INFO;
    int status = desktop_svga2d_transact(&request);
    desktop_svga2d_last_transaction_status = status;
    desktop_svga2d_last_connect_status = status;
    if (status != 0 && activate != 0U && report_error != 0U) {
        x86os_puts("desktop: SVGA2D-Transaktion status=");
        x86os_print_number(status);
        x86os_putchar('\n');
    }
    return status;
}

static int desktop_svga2d_activate_bounded(void) {
    int status = -19;
    for (uint32_t attempt = 0U;
         attempt < DESKTOP_SVGA2D_CONNECT_ATTEMPTS; ++attempt) {
        status = desktop_svga2d_connect(1U, 1U);
        if (status == 0) return 0;
        desktop_svga2d_forget_endpoint();
        if (attempt + 1U < DESKTOP_SVGA2D_CONNECT_ATTEMPTS)
            (void)x86os_sleep_ms(DESKTOP_SVGA2D_RETRY_MS);
    }
    return status;
}

static int desktop_activate_with_fallback(void) {
    desktop_low_latency_menu_feedback = 0U;
    int driver_status = desktop_svga2d_activate_bounded();
    if (driver_status == 0) {
        desktop_low_latency_menu_feedback =
            (desktop_svga2d_capabilities & REIST_SVGA2D_CAP_RECT_COPY) != 0U;
        x86os_puts("DESKTOP_ACCELERATION_READY caps=");
        x86os_print_number((int)desktop_svga2d_capabilities);
        x86os_putchar('\n');
        return 0;
    }

    /* The supervised endpoint is an optional acceleration path.  Its absence
     * must never suppress the validated VBE/software desktop. */
    desktop_svga2d_forget_endpoint();
    int fallback_status = x86os_display_activate();
    if (fallback_status == 0) {
        x86os_puts("desktop: DISPLAY_SOFTWARE_FALLBACK\n");
        return 0;
    }
    x86os_puts("desktop: Display-Aktivierung fehlgeschlagen driver=");
    x86os_print_number(driver_status);
    x86os_puts(" fallback=");
    x86os_print_number(fallback_status);
    x86os_putchar('\n');
    return fallback_status;
}

static int desktop_svga2d_activate_until_ready(uint32_t deadline_ms) {
    if (deadline_ms == 0U) return -22;
    uint64_t started = 0U;
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&started) != 0) return -5;
    int status = -19;
    for (;;) {
        status = desktop_svga2d_connect(1U, 0U);
        if (status == 0) return 0;
        desktop_svga2d_forget_endpoint();
        if (x86os_monotonic_ms(&now) != 0 || now < started ||
            now - started >= deadline_ms) return status;
        uint64_t remaining = deadline_ms - (now - started);
        uint32_t delay = remaining < DESKTOP_SVGA2D_RETRY_MS
            ? (uint32_t)remaining : DESKTOP_SVGA2D_RETRY_MS;
        if (delay == 0U || x86os_sleep_ms(delay) != 0) return status;
    }
}

static int desktop_svga2d_reconnect_if_ready(void) {
    if ((desktop_svga2d_capabilities & REIST_SVGA2D_CAP_RECT_COPY) != 0U)
        return 0;

    uint64_t now = 0U;
    if (x86os_monotonic_ms(&now) != 0) return -5;
    if (desktop_svga2d_next_reconnect_ms != 0U &&
        now < desktop_svga2d_next_reconnect_ms)
        return desktop_svga2d_last_connect_status != 0
            ? desktop_svga2d_last_connect_status : -95;

    desktop_svga2d_next_reconnect_ms = now + DESKTOP_SVGA2D_RECONNECT_MS;
    if (desktop_svga2d_next_reconnect_ms < now)
        desktop_svga2d_next_reconnect_ms = ~(uint64_t)0U;
    if (desktop_svga2d_reconnect_attempts != 0xFFFFFFFFU)
        ++desktop_svga2d_reconnect_attempts;
    int status = desktop_svga2d_connect(1U, 0U);
    if (status != 0) return status;
    if ((desktop_svga2d_capabilities & REIST_SVGA2D_CAP_RECT_COPY) == 0U)
        return -95;
    desktop_svga2d_next_reconnect_ms = 0U;
    if (desktop_svga2d_reconnects != 0xFFFFFFFFU)
        ++desktop_svga2d_reconnects;
    return 0;
}

static int desktop_svga2d_rect_copy(uint32_t source_x, uint32_t source_y,
                                    uint32_t destination_x,
                                    uint32_t destination_y,
                                    uint32_t width, uint32_t height) {
    int status = desktop_svga2d_reconnect_if_ready();
    if (status != 0) {
        desktop_svga2d_last_copy_status = status;
        return status;
    }
    reist_svga2d_message_t request = {0};
    request.operation = REIST_SVGA2D_RECT_COPY;
    request.source_x = source_x;
    request.source_y = source_y;
    request.destination_x = destination_x;
    request.destination_y = destination_y;
    request.width = width;
    request.height = height;
    status = desktop_svga2d_transact(&request);
    desktop_svga2d_last_copy_status = status;
    return status;
}

static int desktop_display_deactivate(void) {
    if (desktop_svga2d_endpoint != X86OS_IPC_INVALID_HANDLE) {
        reist_svga2d_message_t request = {0};
        request.operation = REIST_SVGA2D_DEACTIVATE;
        int status = desktop_svga2d_transact(&request);
        if (status == 0) return 0;
        desktop_svga2d_forget_endpoint();
    }
    return x86os_display_deactivate();
}

enum {
    DESKTOP_MENU_START = 0U
};

enum {
    DESKTOP_MENU_ACTION_NONE = 0U,
    DESKTOP_MENU_ACTION_ABOUT,
    DESKTOP_MENU_ACTION_EXIT,
    DESKTOP_MENU_ACTION_OPEN_ROOT,
    DESKTOP_MENU_ACTION_CLOSE_ALL,
    DESKTOP_MENU_ACTION_HELP,
    DESKTOP_MENU_ACTION_OPEN_CONTROL_PANEL,
    DESKTOP_MENU_ACTION_OPEN_BROWSER
};

enum {
    DESKTOP_TRASH_MENU_ACTION_OPEN = 1U,
    DESKTOP_TRASH_MENU_ACTION_EMPTY
};

enum {
    DESKTOP_EXPLORER_MENU_ACTION_CREATE_SHORTCUT = 1U
};

enum {
    DESKTOP_SHORTCUT_MENU_ACTION_OPEN = 1U,
    DESKTOP_SHORTCUT_MENU_ACTION_REMOVE
};

enum {
    DESKTOP_DIALOG_NONE = 0U,
    DESKTOP_DIALOG_HELP,
    DESKTOP_DIALOG_ABOUT,
    DESKTOP_DIALOG_ERROR,
    DESKTOP_DIALOG_EMPTY_TRASH
};

enum {
    DESKTOP_UI_ACTION_NONE = 0U,
    DESKTOP_UI_ACTION_EXIT,
    DESKTOP_UI_ACTION_OPEN_ROOT,
    DESKTOP_UI_ACTION_CLOSE_ALL,
    DESKTOP_UI_ACTION_OPEN_CONTROL_PANEL,
    DESKTOP_UI_ACTION_OPEN_BROWSER,
    DESKTOP_UI_ACTION_OPEN_TRASH,
    DESKTOP_UI_ACTION_EMPTY_TRASH,
    DESKTOP_UI_ACTION_CREATE_SHORTCUT,
    DESKTOP_UI_ACTION_OPEN_SHORTCUT,
    DESKTOP_UI_ACTION_REMOVE_SHORTCUT
};

enum {
    DESKTOP_MOVE_CACHE_NONE = 0U,
    DESKTOP_MOVE_CACHE_WINDOW,
    DESKTOP_MOVE_CACHE_DIALOG,
    DESKTOP_MOVE_CACHE_RESIZE
};

/* Application policy stays outside libreistgui: the library returns these
 * opaque IDs while this compositor translates them into typed WM actions. */
static const reist_gui_menu_item_t start_menu_items[] = {
    {"Computer oeffnen", DESKTOP_MENU_ACTION_OPEN_ROOT, 0U, 0U, 0U},
    {"Webbrowser", DESKTOP_MENU_ACTION_OPEN_BROWSER, 0U, 0U, 0U},
    {"Systemsteuerung", DESKTOP_MENU_ACTION_OPEN_CONTROL_PANEL,
     0U, 0U, 0U},
    {"Alle Fenster schliessen", DESKTOP_MENU_ACTION_CLOSE_ALL, 0U, 0U, 0U},
    {"Desktop-Hilfe", DESKTOP_MENU_ACTION_HELP, 0U, 0U, 0U},
    {"Ueber REIST Workspace", DESKTOP_MENU_ACTION_ABOUT, 0U, 0U, 0U},
    {"Desktop beenden", DESKTOP_MENU_ACTION_EXIT, 0U, 0U, 0U},
};

static const reist_gui_menu_t desktop_menus[DESKTOP_MENU_COUNT] = {
    {"Start", start_menu_items,
     sizeof(start_menu_items) / sizeof(start_menu_items[0]), 0U, 0U},
};

static const reist_gui_menu_model_t desktop_menu_model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = desktop_menus,
    .menu_count = DESKTOP_MENU_COUNT,
};

static const reist_gui_menu_item_t trash_context_items[] = {
    {"Oeffnen", DESKTOP_TRASH_MENU_ACTION_OPEN, 0U, 0U, 0U},
    {"Papierkorb leeren", DESKTOP_TRASH_MENU_ACTION_EMPTY, 0U, 0U, 0U},
};

static const reist_gui_menu_item_t trash_context_empty_items[] = {
    {"Oeffnen", DESKTOP_TRASH_MENU_ACTION_OPEN, 0U, 0U, 0U},
    {"Papierkorb leeren", DESKTOP_TRASH_MENU_ACTION_EMPTY, 0U,
     REIST_GUI_MENU_ITEM_DISABLED, 0U},
};

static const reist_gui_menu_t trash_context_menus[] = {
    {"Papierkorb", trash_context_items, 2U, 0U, 0U},
};

static const reist_gui_menu_t trash_context_empty_menus[] = {
    {"Papierkorb", trash_context_empty_items, 2U, 0U, 0U},
};

static const reist_gui_menu_model_t trash_context_menu_model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = trash_context_menus,
    .menu_count = 1U,
};

static const reist_gui_menu_model_t trash_context_empty_menu_model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = trash_context_empty_menus,
    .menu_count = 1U,
};

static const reist_gui_menu_item_t explorer_context_items[] = {
    {"Verknuepfung erstellen",
     DESKTOP_EXPLORER_MENU_ACTION_CREATE_SHORTCUT, 0U, 0U, 0U},
};

static const reist_gui_menu_t explorer_context_menus[] = {
    {"Datei", explorer_context_items, 1U, 0U, 0U},
};

static const reist_gui_menu_model_t explorer_context_menu_model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = explorer_context_menus,
    .menu_count = 1U,
};

static const reist_gui_menu_item_t shortcut_context_items[] = {
    {"Oeffnen", DESKTOP_SHORTCUT_MENU_ACTION_OPEN, 0U, 0U, 0U},
    {"In Papierkorb verschieben", DESKTOP_SHORTCUT_MENU_ACTION_REMOVE,
     0U, 0U, 0U},
};

static const reist_gui_menu_t shortcut_context_menus[] = {
    {"Datei", shortcut_context_items, 2U, 0U, 0U},
};

static const reist_gui_menu_model_t shortcut_context_menu_model = {
    .version = REIST_GUI_MENU_API_VERSION,
    .struct_size = sizeof(reist_gui_menu_model_t),
    .menus = shortcut_context_menus,
    .menu_count = 1U,
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

static const reist_gui_dialog_button_t empty_trash_dialog_buttons[] = {
    {"Ja, leeren", REIST_GUI_DIALOG_RESPONSE_YES,
     REIST_GUI_DIALOG_ROLE_DESTRUCTIVE, 0U, 0U},
    {"Nein", REIST_GUI_DIALOG_RESPONSE_NO,
     REIST_GUI_DIALOG_ROLE_REJECT, 0U, 0U},
};

/* Help is intentionally modeless so its outside events can continue to the
 * desktop. About is application-modal and makes every underlying target
 * inert. Both use the same public asynchronous controller. */
static const reist_gui_dialog_model_t help_dialog_model = {
    .version = REIST_GUI_DIALOG_API_VERSION,
    .struct_size = sizeof(reist_gui_dialog_model_t),
    .title = "Desktop-Hilfe",
    .message = "Startmenue: Klick, Pfeile und Enter",
    .detail = "ESC: Menue, Dialog oder Ziehen abbrechen",
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

static const reist_gui_dialog_model_t empty_trash_dialog_model = {
    .version = REIST_GUI_DIALOG_API_VERSION,
    .struct_size = sizeof(reist_gui_dialog_model_t),
    .title = "Papierkorb leeren",
    .message = "Alle Dateien endgueltig loeschen?",
    .detail = "Dieser Vorgang kann nicht rueckgaengig gemacht werden.",
    .buttons = empty_trash_dialog_buttons,
    .button_count = 2U,
    .modality = REIST_GUI_DIALOG_APPLICATION_MODAL,
    .default_response = REIST_GUI_DIALOG_RESPONSE_NO,
    .cancel_response = REIST_GUI_DIALOG_RESPONSE_NO,
    .owner_id = REIST_GUI_DIALOG_NO_OWNER,
    .flags = REIST_GUI_DIALOG_MOVABLE | REIST_GUI_DIALOG_CLOSE_BUTTON,
};

_Static_assert(sizeof(start_menu_items) / sizeof(start_menu_items[0]) <=
                   REIST_GUI_MENU_MAX_ITEMS,
               "Start menu exceeds fixed item capacity");
_Static_assert(sizeof(start_menu_items) / sizeof(start_menu_items[0]) ==
                   DESKTOP_HOVER_PROBE_ITEMS,
               "hover probe must cover every Start-menu item");

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
static desktop_file_icon_cache_entry_t
    desktop_trash_icon_cache[DESKTOP_TRASH_ICON_COUNT];
static uint8_t desktop_file_icon_encoded[DESKTOP_FILE_ICON_ENCODED_CAPACITY];
static uint32_t desktop_file_icon_decoded[DESKTOP_FILE_ICON_PIXELS];
static reist_gui_font_t desktop_font;
typedef union desktop_startup_workspace {
    reist_gui_font_mapping_t font_mappings[DESKTOP_FONT_MAPPING_CAPACITY];
} desktop_startup_workspace_t;
typedef union desktop_font_file_storage {
    uint8_t bytes[DESKTOP_FONT_FILE_CAPACITY];
    uint32_t alignment;
} desktop_font_file_storage_t;
static desktop_startup_workspace_t desktop_startup_workspace;
static desktop_font_file_storage_t desktop_font_file;
static uint32_t desktop_splash_strip[DESKTOP_SPLASH_STRIP_PIXELS];
extern const uint8_t reist_desktop_splash_bmp[];
extern const uint32_t reist_desktop_splash_bmp_size;
static uint32_t desktop_font_pixels[
    REIST_GUI_FONT_MAX_WIDTH * REIST_GUI_FONT_MAX_HEIGHT];
static uint32_t desktop_font_ready;
static uint32_t desktop_font_attempted;
typedef struct desktop_editor_font_slot {
    reist_gui_font_t font;
    reist_gui_font_mapping_t mappings[
        DESKTOP_EDITOR_FONT_MAPPING_CAPACITY];
    uint8_t bytes[DESKTOP_EDITOR_FONT_FILE_CAPACITY];
    uint32_t ready;
} desktop_editor_font_slot_t;
static desktop_editor_font_slot_t desktop_editor_fonts[
    REIST_GUI_FONT_FAMILY_COUNT - 1U][REIST_GUI_FONT_SIZE_COUNT];
typedef struct desktop_font_glyph_cache_entry {
    uint32_t valid;
    uint32_t family;
    uint32_t pixel_height;
    uint32_t scalar;
    uint32_t foreground;
    uint32_t background;
    uint32_t width;
    uint32_t pixels[REIST_GUI_FONT_MAX_WIDTH * REIST_GUI_FONT_MAX_HEIGHT];
} desktop_font_glyph_cache_entry_t;
static desktop_font_glyph_cache_entry_t desktop_font_glyph_cache[
    DESKTOP_FONT_GLYPH_CACHE_CAPACITY];
static uint32_t desktop_font_glyph_cache_next;
static const char *const desktop_file_icon_paths[
    DESKTOP_EXPLORER_ICON_COUNT] = {
        "/usr/share/icons/folder-empty.ico",
        "/usr/share/icons/folder-full.ico",
        "/usr/share/icons/program.ico",
        "/usr/share/icons/text.ico",
        "/usr/share/icons/audio.ico",
        "/usr/share/icons/image.ico",
        "/usr/share/icons/settings.ico",
        "/usr/share/icons/shortcut.ico",
        "/usr/share/icons/unknown.ico",
};
static const char *const desktop_trash_icon_paths[DESKTOP_TRASH_ICON_COUNT] = {
    "/usr/share/icons/trash-empty.ico",
    "/usr/share/icons/trash-full.ico",
};

_Static_assert(sizeof(desktop_file_icon_paths) /
                   sizeof(desktop_file_icon_paths[0]) ==
                   DESKTOP_EXPLORER_ICON_COUNT,
               "file icon path table is incomplete");
_Static_assert(DESKTOP_SPLASH_HEIGHT % DESKTOP_SPLASH_STRIP_ROWS == 0U,
               "splash strips must cover the image exactly");

typedef struct {
    const x86os_display_info_t *display;
    desktop_rect_t clip;
    uint32_t omitted_kind;
    uint32_t omitted_window;
} desktop_render_context_t;

static void draw_shortcut_icon_fallback(
    const desktop_render_context_t *context, desktop_rect_t symbol);
static desktop_rect_t desktop_explorer_content_rect(
    const desktop_wm_t *manager, const desktop_explorer_t *explorer,
    uint32_t window_index);
static int desktop_icon_at_position(
    const x86os_display_info_t *display,
    const desktop_explorer_t *explorer, int32_t x, int32_t y);
static desktop_rect_t desktop_layout_cell_rect(desktop_layout_cell_t cell);

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
    uint32_t accelerated_frames;
    uint32_t acceleration_fallbacks;
    uint32_t damage_regions;
    uint32_t damage_max;
    uint32_t clock_errors;
    uint32_t probe_errors;
} desktop_render_metrics_t;

typedef struct {
    uint32_t enabled;
    uint32_t visited_mask;
    uint32_t last_hot;
    uint32_t hover_frames;
    uint32_t hover_full_frames;
    uint32_t hover_total_ms;
    uint32_t hover_max_ms;
    uint32_t hover_damage_max;
    uint32_t mouse_reports;
    uint32_t mouse_batch_max_ms;
    uint32_t mouse_batch_max_reports;
    uint32_t pointer_frames;
    uint32_t pointer_max_gap_ms;
    uint32_t pointer_latency_max_ms;
    uint32_t pointer_call_max_ms;
    uint32_t pointer_failures;
    uint32_t order_errors;
    uint32_t clock_errors;
    uint32_t complete;
    uint32_t success;
    uint64_t pointer_last_present_ms;
} desktop_hover_probe_t;

typedef struct {
    int status;
    uint32_t clock_valid;
    uint64_t started_ms;
    uint64_t finished_ms;
} desktop_pointer_present_result_t;

typedef struct {
    reist_gui_menu_state_t menu;
    reist_gui_menu_state_t trash_menu;
    reist_gui_menu_state_t explorer_menu;
    reist_gui_menu_state_t shortcut_menu;
    reist_gui_dialog_state_t dialog;
    uint32_t taskbar_capture_slot;
    uint32_t dialog_kind;
    uint32_t trash_menu_can_empty;
    uint32_t error_sequence;
    uint32_t notification_sequence;
    uint32_t trash_drop_sequence;
    uint32_t trash_empty_sequence;
    int32_t trash_menu_x;
    int32_t trash_menu_y;
    int32_t explorer_menu_x;
    int32_t explorer_menu_y;
    int32_t shortcut_menu_x;
    int32_t shortcut_menu_y;
    desktop_drag_object_t explorer_menu_object;
    uint32_t shortcut_menu_generation;
    uint32_t shortcut_menu_index;
    reist_gui_dialog_model_t error_model;
    char error_detail[REIST_GUI_DIALOG_TEXT_LIMIT];
} desktop_ui_state_t;

typedef struct {
    char text[DESKTOP_CLOCK_TEXT_CAPACITY];
    uint64_t next_poll_ms;
    uint32_t fallback_polls;
    uint32_t initialized;
} desktop_clock_state_t;

static desktop_clock_state_t desktop_clock;

typedef struct {
    uint32_t consumed;
    uint32_t action;
    uint32_t target;
} desktop_ui_result_t;

typedef struct {
    desktop_rect_t source;
    desktop_rect_t destination;
    desktop_rect_t cleanup;
    desktop_rect_t redraw;
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

static uint32_t unicode_text_measure(const char *text, size_t maximum_bytes,
                                     size_t *byte_length,
                                     size_t *scalar_count) {
    if (text == 0 || byte_length == 0 || scalar_count == 0) return 0U;
    size_t length = bounded_text_length(text, maximum_bytes);
    size_t count = 0U;
    if (!reist_utf8_scan(text, length, &count)) return 0U;
    *byte_length = length;
    *scalar_count = count;
    return 1U;
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

static int desktop_lifecycle_publish_progress(
    uint32_t supervised, uint32_t *sequence, uint64_t *heartbeat_ms);

static int read_file_bounded_progress(
        const char *path, uint8_t *bytes, size_t capacity, size_t *size_out,
        uint32_t lifecycle_supervised, uint32_t *lifecycle_sequence,
        uint64_t *lifecycle_heartbeat_ms) {
    if (path == 0 || bytes == 0 || capacity == 0U || size_out == 0) return -22;
    *size_out = 0U;
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = reist_vfs_file_open(
        path, DESKTOP_FILE_READ_TIMEOUT_MS, &handle);
    if (status != 0) return status;
    x86os_file_info_t info;
    status = reist_vfs_file_fstat(handle, &info);
    if (status != 0 || info.type != X86OS_FILE || info.size == 0U ||
        info.size > capacity) {
        (void)reist_vfs_file_close(handle);
        return status != 0 ? status : -75;
    }
    size_t used = 0U;
    while (used < info.size) {
        size_t request = info.size - used;
        if (request > X86OS_STORAGE_BULK_MAX_BYTES)
            request = X86OS_STORAGE_BULK_MAX_BYTES;
        int amount = reist_vfs_file_read_bulk(handle, bytes + used, request);
        if (amount <= 0 || (size_t)amount > request) {
            (void)reist_vfs_file_close(handle);
            return -5;
        }
        used += (size_t)amount;
        if (desktop_lifecycle_publish_progress(
                lifecycle_supervised, lifecycle_sequence,
                lifecycle_heartbeat_ms) != 0) {
            (void)reist_vfs_file_close(handle);
            return -1;
        }
    }
    int close_status = reist_vfs_file_close(handle);
    if (close_status != 0) return close_status;
    *size_out = used;
    return 0;
}

static void desktop_copy_bytes(void *destination, const void *source,
                               size_t size) {
    volatile uint8_t *output = (volatile uint8_t *)destination;
    const volatile uint8_t *input =
        (const volatile uint8_t *)source;
    for (size_t index = 0U; index < size; ++index)
        output[index] = input[index];
}

static void desktop_clear_bytes(void *destination, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)destination;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static int read_file_bounded(const char *path, uint8_t *bytes,
                             size_t capacity, size_t *size_out) {
    return read_file_bounded_progress(
        path, bytes, capacity, size_out, 0U, 0, 0);
}

static void desktop_startup_phase_metric(const char *phase,
                                         uint64_t started_ms) {
    uint64_t finished_ms = 0U;
    if (phase == 0 || x86os_monotonic_ms(&finished_ms) != 0 ||
        finished_ms < started_ms || finished_ms - started_ms > INT32_MAX)
        return;
    x86os_puts("DESKTOP_STARTUP_PHASE name=");
    x86os_puts(phase);
    x86os_puts(" ms=");
    x86os_print_number((int)(finished_ms - started_ms));
    x86os_putchar('\n');
}

static int32_t desktop_splash_center_x(
    const x86os_display_info_t *display, size_t text_length) {
    uint64_t width = (uint64_t)text_length * display->font_width;
    return width < display->width
        ? (int32_t)((display->width - (uint32_t)width) / 2U) : 0;
}

static int desktop_splash_show(
        const x86os_display_info_t *display, uint32_t lifecycle_supervised,
        uint32_t *lifecycle_sequence, uint64_t *lifecycle_heartbeat_ms) {
    static const char title[] = "REIST OS";
    static const char loading[] = "System wird geladen ...";
    if (display == 0) return -22;

    /* Publish a useful fallback first. The real splash is linked into this
     * process so desktop startup never waits for the storage service. */
    uint32_t fallback_serial = 0U;
    uint32_t fallback_frame =
        x86os_display_frame_begin(&fallback_serial) == 0 ? 1U : 0U;
    (void)x86os_fill_rect(
        0, 0, display->width, display->height, DESKTOP_SPLASH_BACKGROUND);
    int32_t fallback_y = (int32_t)((display->height - display->font_height) /
                                   2U);
    (void)x86os_draw_text_pixels(
        desktop_splash_center_x(display, sizeof(title) - 1U), fallback_y,
        title, sizeof(title) - 1U,
        DESKTOP_SPLASH_FOREGROUND, DESKTOP_SPLASH_BACKGROUND);
    if (fallback_frame != 0U &&
        x86os_display_frame_commit(fallback_serial) != 0)
        (void)x86os_display_frame_cancel(fallback_serial);
    if (desktop_lifecycle_publish_progress(
            lifecycle_supervised, lifecycle_sequence,
            lifecycle_heartbeat_ms) != 0)
        return -1;

    uint32_t text_gap = display->font_height + 12U;
    uint32_t group_height = DESKTOP_SPLASH_HEIGHT + text_gap +
                            display->font_height * 2U;
    uint32_t expected_size = DESKTOP_SPLASH_BMP_HEADER_SIZE +
        DESKTOP_SPLASH_WIDTH * DESKTOP_SPLASH_HEIGHT *
        DESKTOP_SPLASH_BYTES_PER_PIXEL;
    if (reist_desktop_splash_bmp_size != expected_size ||
        reist_desktop_splash_bmp[0] != 'B' ||
        reist_desktop_splash_bmp[1] != 'M' ||
        display->width < DESKTOP_SPLASH_WIDTH ||
        display->height < group_height) {
        x86os_puts("DESKTOP_SPLASH_FALLBACK status=-84\n");
        return 0;
    }

    int32_t image_x = (int32_t)((display->width - DESKTOP_SPLASH_WIDTH) / 2U);
    int32_t image_y = (int32_t)((display->height - group_height) / 2U);
    (void)x86os_fill_rect(
        0, 0, display->width, display->height, DESKTOP_SPLASH_BACKGROUND);
    for (uint32_t strip_y = 0U; strip_y < DESKTOP_SPLASH_HEIGHT;
         strip_y += DESKTOP_SPLASH_STRIP_ROWS) {
        for (uint32_t row = 0U; row < DESKTOP_SPLASH_STRIP_ROWS; ++row) {
            uint32_t source_y = DESKTOP_SPLASH_HEIGHT - 1U - strip_y - row;
            const uint8_t *source = reist_desktop_splash_bmp +
                DESKTOP_SPLASH_BMP_HEADER_SIZE +
                source_y * DESKTOP_SPLASH_WIDTH *
                    DESKTOP_SPLASH_BYTES_PER_PIXEL;
            uint32_t *target = desktop_splash_strip +
                row * DESKTOP_SPLASH_WIDTH;
            for (uint32_t column = 0U; column < DESKTOP_SPLASH_WIDTH;
                 ++column) {
                uint32_t blue = source[column * 3U];
                uint32_t green = source[column * 3U + 1U];
                uint32_t red = source[column * 3U + 2U];
                target[column] = (red << 16U) | (green << 8U) | blue;
            }
        }
        int status = x86os_draw_pixels(
            image_x, image_y + (int32_t)strip_y,
            DESKTOP_SPLASH_WIDTH, DESKTOP_SPLASH_STRIP_ROWS,
            desktop_splash_strip, DESKTOP_SPLASH_WIDTH);
        if (status != 0) {
            x86os_puts("DESKTOP_SPLASH_FALLBACK status=");
            x86os_print_number(status);
            x86os_putchar('\n');
            return 0;
        }
        if (desktop_lifecycle_publish_progress(
                lifecycle_supervised, lifecycle_sequence,
                lifecycle_heartbeat_ms) != 0)
            return -1;
    }
    int32_t title_y = image_y + (int32_t)DESKTOP_SPLASH_HEIGHT + 12;
    (void)x86os_draw_text_pixels(
        desktop_splash_center_x(display, sizeof(title) - 1U), title_y,
        title, sizeof(title) - 1U,
        DESKTOP_SPLASH_FOREGROUND, DESKTOP_SPLASH_BACKGROUND);
    (void)x86os_draw_text_pixels(
        desktop_splash_center_x(display, sizeof(loading) - 1U),
        title_y + (int32_t)display->font_height,
        loading, sizeof(loading) - 1U,
        DESKTOP_SPLASH_FOREGROUND, DESKTOP_SPLASH_BACKGROUND);
    x86os_puts("DESKTOP_SPLASH_READY\n");
    return desktop_lifecycle_publish_progress(
        lifecycle_supervised, lifecycle_sequence,
        lifecycle_heartbeat_ms);
}

static int desktop_font_load_progress(
        const x86os_display_info_t *display,
        uint32_t lifecycle_supervised, uint32_t *lifecycle_sequence,
        uint64_t *lifecycle_heartbeat_ms) {
    desktop_font_ready = 0U;
    desktop_font_attempted = 1U;
    if (display == 0) return -22;
    uint64_t phase_started = 0U;
    (void)x86os_monotonic_ms(&phase_started);
    size_t size = 0U;
    int status = read_file_bounded_progress(
        DESKTOP_FONT_PATH, desktop_font_file.bytes,
        sizeof(desktop_font_file.bytes), &size, lifecycle_supervised,
        lifecycle_sequence, lifecycle_heartbeat_ms);
    desktop_startup_phase_metric("font-io", phase_started);
    if (status != 0) return status;
    (void)x86os_monotonic_ms(&phase_started);
    reist_gui_font_t candidate = {0};
    status = reist_gui_font_open_psf2(
        &candidate, desktop_font_file.bytes, size,
        desktop_startup_workspace.font_mappings,
        DESKTOP_FONT_MAPPING_CAPACITY, 0x25A0U);
    desktop_startup_phase_metric("font-parse", phase_started);
    if (status != 0 || candidate.width != display->font_width ||
        candidate.height != display->font_height) return status != 0
            ? status : -84;
    desktop_font = candidate;
    desktop_font_ready = 1U;
    return 0;
}

static int desktop_font_load(const x86os_display_info_t *display) {
    return desktop_font_load_progress(display, 0U, 0, 0);
}

static int desktop_editor_font_catalog_load(
        const x86os_display_info_t *display,
        uint32_t lifecycle_supervised, uint32_t *lifecycle_sequence,
        uint64_t *lifecycle_heartbeat_ms) {
    for (uint32_t index = 0U;
         index < DESKTOP_FONT_GLYPH_CACHE_CAPACITY; ++index)
        desktop_font_glyph_cache[index].valid = 0U;
    desktop_font_glyph_cache_next = 0U;
    int fallback_status = desktop_font_load_progress(
        display, lifecycle_supervised, lifecycle_sequence,
        lifecycle_heartbeat_ms);
    if (fallback_status != 0) return fallback_status;
    for (uint32_t family = REIST_GUI_FONT_FAMILY_JETBRAINS_MONO;
         family <= REIST_GUI_FONT_FAMILY_FIRA_CODE; ++family) {
        for (uint32_t size_index = 0U;
             size_index < REIST_GUI_FONT_SIZE_COUNT; ++size_index) {
            uint32_t pixel_height =
                reist_gui_font_catalog_height(size_index);
            desktop_editor_font_slot_t *slot = &desktop_editor_fonts[
                family - REIST_GUI_FONT_FAMILY_JETBRAINS_MONO][size_index];
            slot->ready = 0U;
            const reist_gui_font_catalog_asset_t *asset =
                reist_gui_font_catalog_asset(family, pixel_height);
            size_t size = 0U;
            int status = asset != 0
                ? read_file_bounded_progress(
                    asset->path, slot->bytes, sizeof(slot->bytes), &size,
                    lifecycle_supervised, lifecycle_sequence,
                    lifecycle_heartbeat_ms)
                : -22;
            reist_gui_font_t candidate = {0};
            if (status == 0)
                status = reist_gui_font_open_psf2(
                    &candidate, slot->bytes, size, slot->mappings,
                    DESKTOP_EDITOR_FONT_MAPPING_CAPACITY, 0x25A0U);
            if (status == 0 &&
                (candidate.width != asset->cell_width ||
                 candidate.height != asset->cell_height)) status = -84;
            if (status == 0) {
                slot->font = candidate;
                slot->ready = 1U;
            } else {
                x86os_puts("DESKTOP_EDITOR_FONT_FALLBACK family=");
                x86os_print_number((int)family);
                x86os_puts(" height=");
                x86os_print_number((int)pixel_height);
                x86os_puts(" status=");
                x86os_print_number(status);
                x86os_putchar('\n');
            }
        }
    }
    x86os_puts("DESKTOP_EDITOR_FONT_CATALOG_READY families=5 sizes=8\n");
    return 0;
}

static const reist_gui_font_t *desktop_editor_font(uint32_t family,
                                                   uint32_t pixel_height) {
    if (family >= REIST_GUI_FONT_FAMILY_JETBRAINS_MONO &&
        family <= REIST_GUI_FONT_FAMILY_FIRA_CODE) {
        uint32_t size_index = 0U;
        if (reist_gui_font_catalog_size_index(
                pixel_height, &size_index) != 0) return 0;
        const desktop_editor_font_slot_t *slot =
            &desktop_editor_fonts[family -
                REIST_GUI_FONT_FAMILY_JETBRAINS_MONO][size_index];
        if (slot->ready) return &slot->font;
    }
    return desktop_font_ready ? &desktop_font : 0;
}

static desktop_font_glyph_cache_entry_t *desktop_font_glyph(
        uint32_t family, uint32_t pixel_height, uint32_t scalar,
        uint32_t foreground, uint32_t background) {
    uint32_t width = 0U;
    uint32_t height = 0U;
    if (!desktop_font_ready ||
        reist_gui_font_catalog_metrics(
            family, pixel_height, &width, &height) != 0) return 0;
    for (uint32_t index = 0U;
         index < DESKTOP_FONT_GLYPH_CACHE_CAPACITY; ++index) {
        desktop_font_glyph_cache_entry_t *cached =
            &desktop_font_glyph_cache[index];
        if (cached->valid && cached->family == family &&
            cached->pixel_height == pixel_height &&
            cached->scalar == scalar && cached->foreground == foreground &&
            cached->background == background) return cached;
    }
    const reist_gui_font_t *font = desktop_editor_font(family, pixel_height);
    if (font == 0) return 0;
    uint32_t glyph = 0U;
    int mapped = reist_gui_font_lookup(font, scalar, &glyph);
    if (mapped < 0) return 0;
    if (family != REIST_GUI_FONT_FAMILY_UNIFONT && mapped == 0) {
        font = &desktop_font;
        if (reist_gui_font_lookup(font, scalar, &glyph) < 0) return 0;
    }
    desktop_font_glyph_cache_entry_t *cached =
        &desktop_font_glyph_cache[desktop_font_glyph_cache_next];
    desktop_font_glyph_cache_next =
        (desktop_font_glyph_cache_next + 1U) %
        DESKTOP_FONT_GLYPH_CACHE_CAPACITY;
    cached->valid = 0U;
    int raster_status = font->width == width && font->height == height
        ? reist_gui_font_raster_xrgb(
            font, glyph, foreground, background, cached->pixels, width,
            sizeof(cached->pixels) / sizeof(cached->pixels[0]))
        : reist_gui_font_raster_scaled_xrgb(
            font, glyph, width, height, foreground, background,
            cached->pixels, width,
            sizeof(cached->pixels) / sizeof(cached->pixels[0]));
    if (raster_status != 0)
        return 0;
    cached->family = family;
    cached->pixel_height = pixel_height;
    cached->scalar = scalar;
    cached->foreground = foreground;
    cached->background = background;
    cached->width = width;
    cached->valid = 1U;
    return cached;
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

static void desktop_icon_cache_load(desktop_file_icon_cache_entry_t *entry,
                                    const char *path) {
    if (entry == 0 || path == 0) return;
    entry->valid = 0U;
    size_t encoded_size = 0U;
    if (read_file_bounded(
            path, desktop_file_icon_encoded,
            sizeof(desktop_file_icon_encoded), &encoded_size) != 0)
        return;
    reist_image_info_t info;
    if (reist_image_decode_ico(
            desktop_file_icon_encoded, encoded_size,
            desktop_file_icon_decoded, DESKTOP_FILE_ICON_PIXELS,
            &info) != 0 || info.width != DESKTOP_FILE_ICON_SIZE ||
        info.height != DESKTOP_FILE_ICON_SIZE ||
        info.stride_pixels != DESKTOP_FILE_ICON_SIZE ||
        info.format != REIST_IMAGE_FORMAT_ICO ||
        (info.flags & REIST_IMAGE_FLAG_ALPHA) == 0U)
        return;
    for (uint32_t pixel = 0U; pixel < DESKTOP_FILE_ICON_PIXELS; ++pixel) {
        uint32_t argb = desktop_file_icon_decoded[pixel];
        entry->client[pixel] = compose_icon_pixel(argb, color_client);
        entry->selected[pixel] = compose_icon_pixel(argb, color_face);
        entry->desktop[pixel] = compose_icon_pixel(argb, color_desktop);
    }
    entry->valid = 1U;
}

static int desktop_file_icon_cache_initialize(
        uint32_t lifecycle_supervised, uint32_t *lifecycle_sequence,
        uint64_t *lifecycle_heartbeat_ms) {
    for (uint32_t kind = 0U; kind < DESKTOP_EXPLORER_ICON_COUNT; ++kind) {
        desktop_icon_cache_load(
            &desktop_file_icon_cache[kind], desktop_file_icon_paths[kind]);
        if (desktop_lifecycle_publish_progress(
                lifecycle_supervised, lifecycle_sequence,
                lifecycle_heartbeat_ms) != 0)
            return -1;
    }
    for (uint32_t kind = 0U; kind < DESKTOP_TRASH_ICON_COUNT; ++kind) {
        desktop_icon_cache_load(
            &desktop_trash_icon_cache[kind], desktop_trash_icon_paths[kind]);
        if (desktop_lifecycle_publish_progress(
                lifecycle_supervised, lifecycle_sequence,
                lifecycle_heartbeat_ms) != 0)
            return -1;
    }
    return 0;
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

static uint32_t draw_cached_trash_icon(
    const desktop_render_context_t *context, desktop_rect_t bounds,
    uint32_t full, uint32_t selected) {
    uint32_t kind = full ? 1U : 0U;
    if (context == 0 || bounds.width != DESKTOP_FILE_ICON_SIZE ||
        bounds.height != DESKTOP_FILE_ICON_SIZE ||
        !desktop_trash_icon_cache[kind].valid) return 0U;
    desktop_rect_t clipped;
    if (!intersect_rects(bounds, context->clip, &clipped)) return 1U;
    const uint32_t *pixels = selected
        ? desktop_trash_icon_cache[kind].selected
        : desktop_trash_icon_cache[kind].desktop;
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

static int desktop_font_overlay_extensions(
    const desktop_render_context_t *context, int32_t x, int32_t y,
    const char *text, size_t length, uint32_t foreground,
    uint32_t background) {
    if (context == 0 || context->display == 0 || text == 0) return 0;
    if (!desktop_font_ready) {
        size_t scan = 0U;
        uint32_t extension_needed = 0U;
        while (scan < length) {
            size_t consumed = 0U;
            uint32_t scalar = 0U;
            if (!reist_utf8_decode_one(text + scan, length - scan,
                                       &consumed, &scalar)) return -84;
            if (!reist_unicode_vga_has_glyph(scalar)) extension_needed = 1U;
            scan += consumed;
        }
        if (!extension_needed) return 0;
        if (desktop_font_attempted) return 0;
        int load_status = desktop_font_load(context->display);
        if (load_status != 0) {
            x86os_puts("DESKTOP_FONT_LAZY_FALLBACK status=");
            x86os_print_number(load_status);
            x86os_putchar('\n');
            return 0;
        }
        x86os_puts("DESKTOP_FONT_LAZY_READY\n");
    }
    size_t source = 0U;
    size_t cell = 0U;
    int overlays = 0;
    desktop_rect_t physical = {
        0, 0, context->display->width, context->display->height
    };
    while (source < length) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(
                text + source, length - source, &consumed, &scalar))
            return -84;
        uint32_t glyph_index = 0U;
        int mapped = reist_gui_font_lookup(
            &desktop_font, scalar, &glyph_index);
        if (mapped < 0) return mapped;
        if (mapped == 1 && !reist_unicode_vga_has_glyph(scalar)) {
            int64_t glyph_x64 = (int64_t)x +
                (int64_t)cell * desktop_font.width;
            if (glyph_x64 >= INT32_MIN && glyph_x64 <= INT32_MAX) {
                desktop_rect_t glyph = {
                    (int32_t)glyph_x64, y,
                    desktop_font.width, desktop_font.height
                };
                desktop_rect_t clipped;
                if (intersect_rects(glyph, context->clip, &clipped) &&
                    intersect_rects(clipped, physical, &clipped)) {
                    uint32_t source_x = (uint32_t)(clipped.x - glyph.x);
                    uint32_t source_y = (uint32_t)(clipped.y - glyph.y);
                    int raster = reist_gui_font_raster_xrgb_region(
                        &desktop_font, glyph_index, source_x, source_y,
                        clipped.width, clipped.height, foreground, background,
                        desktop_font_pixels, clipped.width,
                        sizeof(desktop_font_pixels) /
                            sizeof(desktop_font_pixels[0]));
                    if (raster != 0) return raster;
                    int draw = x86os_draw_pixels(
                        clipped.x, clipped.y, clipped.width, clipped.height,
                        desktop_font_pixels, clipped.width);
                    if (draw != 0) return draw;
                    ++overlays;
                }
            }
        }
        source += consumed;
        ++cell;
    }
    return overlays;
}

static void draw_text_clipped(const desktop_render_context_t *context,
                              int32_t x, int32_t y, const char *text,
                              uint32_t maximum_width, uint32_t foreground,
                              uint32_t background) {
    if (context == 0 || context->display == 0 || text == 0 ||
        context->display->font_width == 0U ||
        context->display->font_height == 0U) return;
    const x86os_display_info_t *display = context->display;
    int64_t clip_top = context->clip.y;
    int64_t clip_bottom = clip_top + context->clip.height;
    int64_t text_top = y;
    int64_t text_bottom = text_top + display->font_height;
    if (text_top >= clip_bottom || text_bottom <= clip_top) return;
    size_t length = bounded_text_length(text, X86OS_DISPLAY_MAX_TEXT);
    size_t prefix_bytes = 0U;
    size_t prefix_scalars = 0U;
    size_t maximum_scalars = maximum_width / display->font_width;
    if (reist_utf8_prefix(text, length, maximum_scalars,
                          &prefix_bytes, &prefix_scalars) &&
        prefix_scalars != 0U) {
        (void)x86os_draw_text_pixels_clipped(
            x, y, text, prefix_bytes, foreground, background,
            context->clip.x, context->clip.y,
            context->clip.width, context->clip.height);
        (void)desktop_font_overlay_extensions(
            context, x, y, text, prefix_bytes, foreground, background);
    }
}

static void draw_font_text_clipped(
        const desktop_render_context_t *context, int32_t x, int32_t y,
        const char *text, size_t length, uint32_t maximum_width,
        uint32_t foreground, uint32_t background,
        uint32_t family, uint32_t pixel_height) {
    size_t scalar_count = 0U;
    uint32_t cell_width = 0U;
    uint32_t cell_height = 0U;
    if (context == 0 || context->display == 0 || text == 0 || length == 0U ||
        !reist_utf8_scan(text, length, &scalar_count) || scalar_count == 0U ||
        reist_gui_font_catalog_metrics(
            family, pixel_height, &cell_width, &cell_height) != 0 ||
        maximum_width < cell_width) return;
    desktop_rect_t physical = {
        0, 0, context->display->width, context->display->height
    };
    uint32_t maximum_cells = maximum_width / cell_width;
    size_t source = 0U;
    uint32_t cell = 0U;
    while (source < length && cell < maximum_cells) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(
                text + source, length - source, &consumed, &scalar)) return;
        desktop_font_glyph_cache_entry_t *cached = desktop_font_glyph(
            family, pixel_height, scalar, foreground, background);
        if (cached == 0 || cached->width != cell_width) return;
        int64_t glyph_x = (int64_t)x + (uint64_t)cell * cell_width;
        if (glyph_x >= INT32_MIN && glyph_x <= INT32_MAX) {
            desktop_rect_t glyph = {
                (int32_t)glyph_x, y, cell_width, cell_height
            };
            desktop_rect_t clipped;
            if (intersect_rects(glyph, context->clip, &clipped) &&
                intersect_rects(clipped, physical, &clipped)) {
                uint32_t source_x = (uint32_t)(clipped.x - glyph.x);
                uint32_t source_y = (uint32_t)(clipped.y - glyph.y);
                (void)x86os_draw_pixels(
                    clipped.x, clipped.y, clipped.width, clipped.height,
                    cached->pixels + (size_t)source_y * cell_width + source_x,
                    cell_width);
            }
        }
        source += consumed;
        ++cell;
    }
}

static uint32_t menu_height(const x86os_display_info_t *display) {
    return max_u32(display->font_height + 12U, 30U);
}

static uint32_t taskbar_height(const x86os_display_info_t *display) {
    uint64_t preferred = (uint64_t)display->font_height + 12U;
    uint32_t height = preferred > UINT32_MAX
        ? UINT32_MAX : (uint32_t)preferred;
    height = max_u32(height, 30U);
    return min_u32(height, display->height);
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
    uint32_t height = taskbar_height(display);
    uint64_t preferred_width = (uint64_t)display->font_width * 5U + 32U;
    uint32_t button_width = preferred_width > display->width
        ? display->width : (uint32_t)preferred_width;
    return (reist_gui_menu_layout_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(reist_gui_menu_layout_t),
        .surface_width = display->width,
        .surface_height = display->height,
        .bar = {
            4,
            (int32_t)(display->height - height + 3U),
            button_width,
            height > 6U ? height - 6U : height,
        },
        .font_width = display->font_width,
        .font_height = display->font_height,
        .title_padding_x = 16U,
        .item_padding_x = 8U,
        .item_padding_y = 4U,
        .damage_margin = 6U,
        .popup_direction = REIST_GUI_MENU_POPUP_ABOVE,
    };
}

static const reist_gui_menu_model_t *trash_context_model(
    const desktop_ui_state_t *ui) {
    return ui != 0 && ui->trash_menu_can_empty
        ? &trash_context_menu_model : &trash_context_empty_menu_model;
}

static reist_gui_menu_layout_t desktop_context_layout(
    const x86os_display_info_t *display, int32_t requested_x,
    int32_t requested_y, uint32_t title_characters,
    uint32_t item_count) {
    uint32_t title_padding = 4U;
    uint32_t item_padding_y = 4U;
    uint32_t bar_height = max_u32(display->font_height, 1U);
    uint64_t preferred_width =
        (uint64_t)display->font_width * title_characters +
        title_padding * 2U;
    uint32_t title_width = preferred_width > UINT32_MAX
        ? UINT32_MAX : (uint32_t)preferred_width;
    if (title_width > display->width) title_width = display->width;
    uint32_t popup_height = 4U + item_count *
        (display->font_height + item_padding_y * 2U);
    int32_t anchor_x = requested_x;
    int32_t anchor_y = requested_y;
    if (anchor_x < 0) anchor_x = 0;
    if ((uint64_t)(uint32_t)anchor_x + title_width > display->width)
        anchor_x = (int32_t)(display->width - title_width);
    if (anchor_y < 0) anchor_y = 0;
    if ((uint32_t)anchor_y > display->height)
        anchor_y = (int32_t)display->height;
    uint32_t below = (uint64_t)(uint32_t)anchor_y + popup_height <=
        display->height;
    int32_t bar_y;
    uint32_t direction;
    if (below) {
        bar_y = anchor_y >= (int32_t)bar_height
            ? anchor_y - (int32_t)bar_height : 0;
        direction = REIST_GUI_MENU_POPUP_BELOW;
    } else {
        uint32_t maximum_y = display->height > bar_height
            ? display->height - bar_height : 0U;
        bar_y = anchor_y > (int32_t)maximum_y
            ? (int32_t)maximum_y : anchor_y;
        if (bar_y < (int32_t)popup_height)
            bar_y = (int32_t)popup_height;
        direction = REIST_GUI_MENU_POPUP_ABOVE;
    }
    return (reist_gui_menu_layout_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(reist_gui_menu_layout_t),
        .surface_width = display->width,
        .surface_height = display->height,
        .bar = {anchor_x, bar_y, title_width, bar_height},
        .font_width = display->font_width,
        .font_height = display->font_height,
        .title_padding_x = title_padding,
        .item_padding_x = 8U,
        .item_padding_y = item_padding_y,
        .damage_margin = 6U,
        .popup_direction = direction,
    };
}

static reist_gui_menu_layout_t trash_context_layout(
    const desktop_ui_state_t *ui, const x86os_display_info_t *display) {
    return desktop_context_layout(
        display, ui != 0 ? ui->trash_menu_x : 0,
        ui != 0 ? ui->trash_menu_y : 0, 10U, 2U);
}

static reist_gui_menu_layout_t explorer_context_layout(
    const desktop_ui_state_t *ui, const x86os_display_info_t *display) {
    return desktop_context_layout(
        display, ui != 0 ? ui->explorer_menu_x : 0,
        ui != 0 ? ui->explorer_menu_y : 0, 5U, 1U);
}

static reist_gui_menu_layout_t shortcut_context_layout(
    const desktop_ui_state_t *ui, const x86os_display_info_t *display) {
    return desktop_context_layout(
        display, ui != 0 ? ui->shortcut_menu_x : 0,
        ui != 0 ? ui->shortcut_menu_y : 0, 12U, 2U);
}

static void desktop_shortcut_probe_publish_menu_point(
    const char *action, const reist_gui_menu_model_t *model,
    const reist_gui_menu_layout_t *layout, uint32_t item_index) {
    if (!desktop_shortcut_probe_enabled || action == 0 || model == 0 ||
        layout == 0) return;
    reist_gui_rect_t item;
    if (reist_gui_menu_item_rect(
            model, layout, 0U, item_index, &item) != 0 ||
        item.width == 0U || item.height == 0U) return;
    x86os_puts("DESKTOP_SHORTCUT_PROBE_MENU action=");
    x86os_puts(action);
    x86os_puts(" x=");
    x86os_print_number(item.x + (int)(item.width / 2U));
    x86os_puts(" y=");
    x86os_print_number(item.y + (int)(item.height / 2U));
    x86os_putchar('\n');
}

static desktop_rect_t desktop_taskbar_rect(
    const x86os_display_info_t *display) {
    uint32_t height = taskbar_height(display);
    return (desktop_rect_t){
        0, (int32_t)(display->height - height), display->width, height
    };
}

static desktop_rect_t desktop_clock_rect(
    const x86os_display_info_t *display) {
    desktop_rect_t taskbar = desktop_taskbar_rect(display);
    uint64_t preferred_width =
        (uint64_t)(DESKTOP_CLOCK_TEXT_CAPACITY - 1U) *
            display->font_width + 12U;
    uint32_t maximum = display->width / 2U;
    uint32_t width = preferred_width > maximum
        ? maximum : (uint32_t)preferred_width;
    return (desktop_rect_t){
        (int32_t)(display->width - width - 4U), taskbar.y + 3,
        width, taskbar.height > 6U ? taskbar.height - 6U : taskbar.height,
    };
}

static uint32_t desktop_partition_offset(
    uint32_t width, uint32_t part, uint32_t count) {
    if (count == 0U || count > DESKTOP_WM_CAPACITY || part > count)
        return width;
    uint32_t quotient = width / count;
    uint32_t remainder = width % count;
    /* remainder and part are both bounded by DESKTOP_WM_CAPACITY, while
     * quotient * part cannot exceed width. This preserves floor(width *
     * part / count) without a 64-bit compiler-runtime division helper. */
    return quotient * part + remainder * part / count;
}

static desktop_rect_t desktop_task_button_rect(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    uint32_t window_index) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (display == 0 || manager == 0 ||
        window_index >= DESKTOP_WM_CAPACITY ||
        !manager->windows[window_index].visible) return empty;
    reist_gui_menu_layout_t menu = desktop_menu_layout(display);
    desktop_rect_t clock = desktop_clock_rect(display);
    uint32_t visible = 0U;
    uint32_t ordinal = 0U;
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        if (!manager->windows[index].visible) continue;
        if (index == window_index) ordinal = visible;
        ++visible;
    }
    int32_t left = menu.bar.x + (int32_t)menu.bar.width + 4;
    int32_t right = clock.x - 4;
    if (visible == 0U || right <= left) return empty;
    uint32_t available = (uint32_t)(right - left);
    uint32_t x0 = (uint32_t)left +
        desktop_partition_offset(available, ordinal, visible);
    uint32_t x1 = (uint32_t)left +
        desktop_partition_offset(available, ordinal + 1U, visible);
    if (x1 <= x0 + 2U) return empty;
    desktop_rect_t taskbar = desktop_taskbar_rect(display);
    return (desktop_rect_t){
        (int32_t)x0, taskbar.y + 3, x1 - x0 - 2U,
        taskbar.height > 6U ? taskbar.height - 6U : taskbar.height,
    };
}

static uint32_t desktop_taskbar_window_at(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    int32_t x, int32_t y) {
    if (display == 0 || manager == 0) return DESKTOP_WM_NO_TARGET;
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        desktop_rect_t button = desktop_task_button_rect(
            display, manager, index);
        if (button.width != 0U && point_in_rect(button, x, y)) return index;
    }
    return DESKTOP_WM_NO_TARGET;
}

static uint32_t desktop_clock_days_in_month(uint32_t year, uint32_t month) {
    static const uint8_t days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month == 0U || month > 12U) return 0U;
    uint32_t result = days[month - 1U];
    if (month == 2U &&
        ((year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U))
        ++result;
    return result;
}

static uint32_t desktop_clock_fields_valid(uint32_t year, uint32_t month,
                                           uint32_t day, uint32_t hour,
                                           uint32_t minute,
                                           uint32_t second) {
    uint32_t month_days = desktop_clock_days_in_month(year, month);
    return year >= 1970U && year <= 9999U && month_days != 0U &&
        day != 0U && day <= month_days && hour < 24U && minute < 60U &&
        second < 60U;
}

static void desktop_clock_two(char *text, uint32_t offset, uint32_t value) {
    text[offset] = (char)('0' + (value / 10U) % 10U);
    text[offset + 1U] = (char)('0' + value % 10U);
}

static void desktop_clock_format(char *text, uint32_t date, uint32_t time) {
    uint32_t year = date >> 16U;
    uint32_t month = (date >> 8U) & 0xFFU;
    uint32_t day = date & 0xFFU;
    uint32_t hour = (time >> 16U) & 0xFFU;
    uint32_t minute = (time >> 8U) & 0xFFU;
    uint32_t second = time & 0xFFU;
    if (!desktop_clock_fields_valid(
            year, month, day, hour, minute, second)) {
        static const char invalid[DESKTOP_CLOCK_TEXT_CAPACITY] =
            "---- -- -- --:--";
        for (uint32_t index = 0U;
             index < DESKTOP_CLOCK_TEXT_CAPACITY; ++index)
            text[index] = invalid[index];
        return;
    }
    text[0] = (char)('0' + (year / 1000U) % 10U);
    text[1] = (char)('0' + (year / 100U) % 10U);
    text[2] = (char)('0' + (year / 10U) % 10U);
    text[3] = (char)('0' + year % 10U);
    text[4] = '-';
    desktop_clock_two(text, 5U, month);
    text[7] = '-';
    desktop_clock_two(text, 8U, day);
    text[10] = ' ';
    desktop_clock_two(text, 11U, hour);
    text[13] = ':';
    desktop_clock_two(text, 14U, minute);
    text[16] = '\0';
}

static void desktop_clock_refresh(
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    uint32_t force) {
    uint64_t now_ms = 0U;
    if (x86os_monotonic_ms(&now_ms) == 0) {
        if (!force && desktop_clock.initialized &&
            now_ms < desktop_clock.next_poll_ms) return;
        desktop_clock.next_poll_ms = now_ms > UINT64_MAX -
                DESKTOP_CLOCK_POLL_MS
            ? UINT64_MAX : now_ms + DESKTOP_CLOCK_POLL_MS;
        desktop_clock.fallback_polls = 0U;
    } else if (!force && desktop_clock.initialized) {
        if (++desktop_clock.fallback_polls < DESKTOP_CLOCK_FALLBACK_POLLS)
            return;
        desktop_clock.fallback_polls = 0U;
    }
    char next[DESKTOP_CLOCK_TEXT_CAPACITY];
    desktop_clock_format(next, x86os_get_date(), x86os_get_time());
    uint32_t changed = !desktop_clock.initialized;
    for (uint32_t index = 0U; index < DESKTOP_CLOCK_TEXT_CAPACITY; ++index) {
        if (desktop_clock.text[index] != next[index]) changed = 1U;
        desktop_clock.text[index] = next[index];
    }
    desktop_clock.initialized = 1U;
    if (changed && display != 0 && dirty != 0)
        desktop_dirty_add(dirty, desktop_clock_rect(display));
}

static void desktop_ui_initialize(desktop_ui_state_t *ui) {
    if (ui == 0) return;
    reist_gui_menu_state_initialize(&ui->menu);
    reist_gui_menu_state_initialize(&ui->trash_menu);
    reist_gui_menu_state_initialize(&ui->explorer_menu);
    reist_gui_menu_state_initialize(&ui->shortcut_menu);
    reist_gui_dialog_state_initialize(&ui->dialog);
    ui->taskbar_capture_slot = DESKTOP_WM_NO_TARGET;
    ui->dialog_kind = DESKTOP_DIALOG_NONE;
    ui->trash_menu_can_empty = 0U;
    ui->error_sequence = 0U;
    ui->notification_sequence = 0U;
    ui->trash_drop_sequence = 0U;
    ui->trash_empty_sequence = 0U;
    ui->trash_menu_x = 0;
    ui->trash_menu_y = 0;
    ui->explorer_menu_x = 0;
    ui->explorer_menu_y = 0;
    ui->shortcut_menu_x = 0;
    ui->shortcut_menu_y = 0;
    desktop_drag_object_initialize(&ui->explorer_menu_object);
    ui->shortcut_menu_generation = 0U;
    ui->shortcut_menu_index = UINT32_MAX;
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
    if (kind == DESKTOP_DIALOG_EMPTY_TRASH)
        return &empty_trash_dialog_model;
    return 0;
}

static reist_gui_dialog_layout_t desktop_dialog_layout(
    const x86os_display_info_t *display) {
    uint32_t top = 12U;
    uint32_t bottom = display->height > taskbar_height(display) + 12U
        ? display->height - taskbar_height(display) - 12U : top + 1U;
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

static uint32_t desktop_start_menu_item_damage(
    const x86os_display_info_t *display, desktop_rect_t candidate) {
    if (display == 0 || candidate.width == 0U || candidate.height == 0U)
        return 0U;
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    uint32_t item_count = desktop_menu_model.menus[DESKTOP_MENU_START]
        .item_count;
    for (uint32_t item = 0U; item < item_count; ++item) {
        reist_gui_rect_t gui_item;
        if (reist_gui_menu_item_rect(
                &desktop_menu_model, &layout, DESKTOP_MENU_START, item,
                &gui_item) != 0)
            return 0U;
        desktop_rect_t row = desktop_rect_from_gui(gui_item);
        if (candidate.x == row.x && candidate.y == row.y &&
            candidate.width == row.width && candidate.height == row.height)
            return 1U;
    }
    return 0U;
}

static void collect_menu_damage(
    const desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_result_t *menu_result) {
    if (dirty == 0 || menu_result == 0) return;
    if (menu_result->full_redraw) {
        desktop_dirty_full(dirty);
        return;
    }
    /* The append-only clipped-text ABI now clips every glyph pixel against
     * the same update rectangle as fills and bevels.  Preserve the menu
     * controller's fixed old/new item damage instead of repainting the scene. */
    for (uint32_t index = 0U; index < menu_result->damage_count; ++index) {
        desktop_rect_t damage =
            desktop_rect_from_gui(menu_result->damage[index]);
        /* VMware's SVGA-II device exposes rectangle copy but no rectangle
         * fill.  A complete 95-style blue row would therefore be a slow
         * uncached BAR write.  Keep the controller's exact row contract, but
         * render the accelerated desktop feedback as a narrow left-hand band.
         * The shape change is enabled only with the copy-capable service and
         * only for an already open Start-menu item; all other damage remains
         * byte-for-byte the normal compositor path. */
        if (desktop_low_latency_menu_feedback != 0U && ui != 0 &&
            ui->menu.open_menu == DESKTOP_MENU_START &&
            desktop_start_menu_item_damage(display, damage)) {
            if (damage.width > DESKTOP_MENU_FAST_FEEDBACK_WIDTH)
                damage.width = DESKTOP_MENU_FAST_FEEDBACK_WIDTH;
        }
        desktop_dirty_add(dirty, damage);
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
    if (kind != DESKTOP_DIALOG_ERROR && kind != DESKTOP_DIALOG_EMPTY_TRASH)
        saturating_increment(&ui->notification_sequence);
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
    if (ui->dialog.visible && ui->dialog_kind == DESKTOP_DIALOG_ERROR)
        saturating_increment(&ui->error_sequence);
}

static void desktop_ui_open_trash_context(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t x, int32_t y,
    uint32_t can_empty) {
    if (ui == 0 || display == 0 || dirty == 0 || ui->dialog.visible) return;
    reist_gui_menu_state_initialize(&ui->menu);
    reist_gui_menu_state_initialize(&ui->trash_menu);
    reist_gui_menu_state_initialize(&ui->explorer_menu);
    reist_gui_menu_state_initialize(&ui->shortcut_menu);
    ui->trash_menu_can_empty = can_empty != 0U;
    ui->trash_menu_x = x;
    ui->trash_menu_y = y;
    ui->trash_menu.open_menu = 0U;
    reist_gui_menu_layout_t layout = trash_context_layout(ui, display);
    if (reist_gui_menu_validate(
            trash_context_model(ui), &layout, &ui->trash_menu) != 0) {
        reist_gui_menu_state_initialize(&ui->trash_menu);
        return;
    }
    desktop_dirty_full(dirty);
}

static void desktop_ui_open_explorer_context(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t x, int32_t y,
    const desktop_drag_object_t *object) {
    if (ui == 0 || display == 0 || dirty == 0 || object == 0 ||
        ui->dialog.visible ||
        desktop_drag_validate_object(object) != DESKTOP_DRAG_OK) return;
    reist_gui_menu_state_initialize(&ui->menu);
    reist_gui_menu_state_initialize(&ui->trash_menu);
    reist_gui_menu_state_initialize(&ui->explorer_menu);
    reist_gui_menu_state_initialize(&ui->shortcut_menu);
    desktop_copy_bytes(
        &ui->explorer_menu_object, object,
        sizeof(ui->explorer_menu_object));
    ui->explorer_menu_x = x;
    ui->explorer_menu_y = y;
    ui->explorer_menu.open_menu = 0U;
    reist_gui_menu_layout_t layout = explorer_context_layout(ui, display);
    if (reist_gui_menu_validate(
            &explorer_context_menu_model, &layout,
            &ui->explorer_menu) != 0) {
        reist_gui_menu_state_initialize(&ui->explorer_menu);
        desktop_drag_object_initialize(&ui->explorer_menu_object);
        return;
    }
    desktop_shortcut_probe_publish_menu_point(
        "create", &explorer_context_menu_model, &layout, 0U);
    desktop_dirty_full(dirty);
}

static void desktop_ui_open_shortcut_context(
    const desktop_explorer_t *explorer, desktop_ui_state_t *ui,
    const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t x, int32_t y,
    uint32_t generation, uint32_t entry_index) {
    if (explorer == 0 || ui == 0 || display == 0 || dirty == 0 ||
        ui->dialog.visible || generation == 0U ||
        generation !=
            explorer->desktop_directory.snapshot_generation ||
        entry_index >= explorer->desktop_directory.entry_count ||
        explorer->desktop_directory.entries[entry_index].type != X86OS_FILE)
        return;
    reist_gui_menu_state_initialize(&ui->menu);
    reist_gui_menu_state_initialize(&ui->trash_menu);
    reist_gui_menu_state_initialize(&ui->explorer_menu);
    reist_gui_menu_state_initialize(&ui->shortcut_menu);
    ui->shortcut_menu_generation = generation;
    ui->shortcut_menu_index = entry_index;
    ui->shortcut_menu_x = x;
    ui->shortcut_menu_y = y;
    ui->shortcut_menu.open_menu = 0U;
    reist_gui_menu_layout_t layout = shortcut_context_layout(ui, display);
    if (reist_gui_menu_validate(
            &shortcut_context_menu_model, &layout,
            &ui->shortcut_menu) != 0) {
        reist_gui_menu_state_initialize(&ui->shortcut_menu);
        ui->shortcut_menu_generation = 0U;
        ui->shortcut_menu_index = UINT32_MAX;
        return;
    }
    desktop_shortcut_probe_publish_menu_point(
        "open", &shortcut_context_menu_model, &layout, 0U);
    desktop_shortcut_probe_publish_menu_point(
        "remove", &shortcut_context_menu_model, &layout, 1U);
    desktop_dirty_full(dirty);
}

static desktop_ui_result_t desktop_ui_apply_dialog_result(
    desktop_ui_state_t *ui, desktop_dirty_region_t *dirty,
    const reist_gui_dialog_result_t *dialog_result) {
    desktop_ui_result_t result = desktop_ui_result_none();
    result.consumed = dialog_result->consumed;
    collect_dialog_damage(dirty, dialog_result);
    if (dialog_result->completed) {
        uint32_t response = REIST_GUI_DIALOG_RESPONSE_NONE;
        if (ui->dialog_kind == DESKTOP_DIALOG_EMPTY_TRASH &&
            reist_gui_dialog_response(&ui->dialog, &response) == 0 &&
            response == REIST_GUI_DIALOG_RESPONSE_YES)
            result.action = DESKTOP_UI_ACTION_EMPTY_TRASH;
        ui->dialog_kind = DESKTOP_DIALOG_NONE;
    }
    return result;
}

static desktop_ui_result_t desktop_ui_apply_trash_menu_result(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_result_t *menu_result) {
    desktop_ui_result_t result = desktop_ui_result_none();
    result.consumed = menu_result->consumed;
    collect_menu_damage(ui, display, dirty, menu_result);
    if (!menu_result->activated) return result;
    if (menu_result->action == DESKTOP_TRASH_MENU_ACTION_OPEN) {
        result.action = DESKTOP_UI_ACTION_OPEN_TRASH;
    } else if (menu_result->action == DESKTOP_TRASH_MENU_ACTION_EMPTY &&
               ui->trash_menu_can_empty) {
        desktop_ui_open_dialog(
            ui, display, dirty, DESKTOP_DIALOG_EMPTY_TRASH);
    }
    return result;
}

static desktop_ui_result_t desktop_ui_dispatch_trash_menu(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_event_t *event) {
    desktop_ui_result_t result = desktop_ui_result_none();
    reist_gui_menu_layout_t layout = trash_context_layout(ui, display);
    reist_gui_menu_result_t menu_result;
    reist_gui_menu_result_initialize(&menu_result);
    if (reist_gui_menu_dispatch(
            trash_context_model(ui), &layout, &ui->trash_menu,
            event, &menu_result) != 0) {
        result.consumed = 1U;
        reist_gui_menu_state_initialize(&ui->trash_menu);
        desktop_dirty_full(dirty);
        return result;
    }
    return desktop_ui_apply_trash_menu_result(
        ui, display, dirty, &menu_result);
}

static desktop_ui_result_t desktop_ui_dispatch_explorer_menu(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_event_t *event) {
    desktop_ui_result_t result = desktop_ui_result_none();
    reist_gui_menu_layout_t layout = explorer_context_layout(ui, display);
    reist_gui_menu_result_t menu_result;
    reist_gui_menu_result_initialize(&menu_result);
    if (reist_gui_menu_dispatch(
            &explorer_context_menu_model, &layout, &ui->explorer_menu,
            event, &menu_result) != 0) {
        result.consumed = 1U;
        reist_gui_menu_state_initialize(&ui->explorer_menu);
        desktop_drag_object_initialize(&ui->explorer_menu_object);
        desktop_dirty_full(dirty);
        return result;
    }
    result.consumed = menu_result.consumed;
    collect_menu_damage(ui, display, dirty, &menu_result);
    if (menu_result.activated &&
        menu_result.action == DESKTOP_EXPLORER_MENU_ACTION_CREATE_SHORTCUT)
        result.action = DESKTOP_UI_ACTION_CREATE_SHORTCUT;
    return result;
}

static desktop_ui_result_t desktop_ui_dispatch_shortcut_menu(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_event_t *event) {
    desktop_ui_result_t result = desktop_ui_result_none();
    reist_gui_menu_layout_t layout = shortcut_context_layout(ui, display);
    reist_gui_menu_result_t menu_result;
    reist_gui_menu_result_initialize(&menu_result);
    if (reist_gui_menu_dispatch(
            &shortcut_context_menu_model, &layout, &ui->shortcut_menu,
            event, &menu_result) != 0) {
        result.consumed = 1U;
        reist_gui_menu_state_initialize(&ui->shortcut_menu);
        ui->shortcut_menu_generation = 0U;
        ui->shortcut_menu_index = UINT32_MAX;
        desktop_dirty_full(dirty);
        return result;
    }
    result.consumed = menu_result.consumed;
    collect_menu_damage(ui, display, dirty, &menu_result);
    if (!menu_result.activated) return result;
    if (menu_result.action == DESKTOP_SHORTCUT_MENU_ACTION_OPEN)
        result.action = DESKTOP_UI_ACTION_OPEN_SHORTCUT;
    else if (menu_result.action == DESKTOP_SHORTCUT_MENU_ACTION_REMOVE)
        result.action = DESKTOP_UI_ACTION_REMOVE_SHORTCUT;
    result.target = ui->shortcut_menu_index;
    return result;
}

static desktop_ui_result_t desktop_ui_apply_menu_result(
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty,
    const reist_gui_menu_result_t *menu_result) {
    desktop_ui_result_t result = desktop_ui_result_none();
    result.consumed = menu_result->consumed;
    collect_menu_damage(ui, display, dirty, menu_result);
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
    } else if (menu_result->action ==
               DESKTOP_MENU_ACTION_OPEN_CONTROL_PANEL) {
        result.action = DESKTOP_UI_ACTION_OPEN_CONTROL_PANEL;
    } else if (menu_result->action == DESKTOP_MENU_ACTION_OPEN_BROWSER) {
        result.action = DESKTOP_UI_ACTION_OPEN_BROWSER;
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
    if (ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->explorer_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE) {
        desktop_ui_result_t context_result =
            desktop_ui_dispatch_explorer_menu(
                ui, display, dirty, &event);
        if (context_result.consumed) return context_result;
    }
    if (ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->shortcut_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE) {
        desktop_ui_result_t context_result =
            desktop_ui_dispatch_shortcut_menu(
                ui, display, dirty, &event);
        if (context_result.consumed) return context_result;
    }
    if (ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->trash_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE) {
        desktop_ui_result_t context_result =
            desktop_ui_dispatch_trash_menu(
                ui, display, dirty, &event);
        if (context_result.consumed) return context_result;
    }
    return desktop_ui_dispatch_menu(ui, display, dirty, &event);
}

static uint32_t desktop_ui_owns_pointer(const desktop_ui_state_t *ui) {
    return (ui->dialog.visible &&
            (ui->dialog.capture_kind != REIST_GUI_DIALOG_CAPTURE_NONE ||
             ui->dialog.modality != REIST_GUI_DIALOG_MODELESS)) ||
           ui->menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
           ui->menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE ||
           ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
           ui->trash_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE ||
           ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
           ui->explorer_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE ||
           ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
           ui->shortcut_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE ||
           ui->taskbar_capture_slot != DESKTOP_WM_NO_TARGET;
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
    uint32_t menu_key = desktop_menu_key_from_input(key);
    if (ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX) {
        if (menu_key == 0U) {
            result.consumed = 1U;
            return result;
        }
        reist_gui_menu_event_t context_event;
        reist_gui_menu_event_initialize(&context_event);
        context_event.type = REIST_GUI_MENU_EVENT_KEYBOARD;
        context_event.key = menu_key;
        return desktop_ui_dispatch_explorer_menu(
            ui, display, dirty, &context_event);
    }
    if (ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX) {
        if (menu_key == 0U) {
            result.consumed = 1U;
            return result;
        }
        reist_gui_menu_event_t context_event;
        reist_gui_menu_event_initialize(&context_event);
        context_event.type = REIST_GUI_MENU_EVENT_KEYBOARD;
        context_event.key = menu_key;
        return desktop_ui_dispatch_shortcut_menu(
            ui, display, dirty, &context_event);
    }
    if (ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX) {
        if (menu_key == 0U) {
            result.consumed = 1U;
            return result;
        }
        reist_gui_menu_event_t context_event;
        reist_gui_menu_event_initialize(&context_event);
        context_event.type = REIST_GUI_MENU_EVENT_KEYBOARD;
        context_event.key = menu_key;
        return desktop_ui_dispatch_trash_menu(
            ui, display, dirty, &context_event);
    }
    if (ui->menu.open_menu == REIST_GUI_MENU_NO_INDEX) return result;
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

static uint32_t desktop_dynamic_icon_count(
    const desktop_explorer_t *explorer) {
    return explorer != 0 && explorer->desktop_directory.active
        ? explorer->desktop_directory.entry_count : 0U;
}

typedef struct desktop_layout_io_budget {
    uint64_t deadline_ms;
} desktop_layout_io_budget_t;

static int desktop_layout_budget_start(desktop_layout_io_budget_t *budget) {
    uint64_t now_ms = 0U;
    if (budget == 0 || x86os_monotonic_ms(&now_ms) != 0)
        return DESKTOP_LAYOUT_EIO;
    budget->deadline_ms = UINT64_MAX - now_ms < DESKTOP_LAYOUT_IO_TIMEOUT_MS
        ? UINT64_MAX : now_ms + DESKTOP_LAYOUT_IO_TIMEOUT_MS;
    return DESKTOP_LAYOUT_OK;
}

static int desktop_layout_budget_remaining(
    const desktop_layout_io_budget_t *budget, uint32_t *remaining_out) {
    uint64_t now_ms = 0U;
    if (budget == 0 || remaining_out == 0 ||
        x86os_monotonic_ms(&now_ms) != 0 || now_ms >= budget->deadline_ms)
        return DESKTOP_LAYOUT_EIO;
    uint64_t remaining = budget->deadline_ms - now_ms;
    *remaining_out = remaining > UINT32_MAX ? UINT32_MAX
                                            : (uint32_t)remaining;
    if (*remaining_out == 0U) *remaining_out = 1U;
    return DESKTOP_LAYOUT_OK;
}

static void desktop_layout_close_cleanup(reist_vfs_file_handle_t handle) {
    if (handle == REIST_VFS_FILE_INVALID_HANDLE) return;
    (void)reist_vfs_file_set_timeout(handle, 1U);
    (void)reist_vfs_file_close(handle);
}

static int desktop_layout_load(desktop_layout_document_t *document) {
    if (document == 0) return DESKTOP_LAYOUT_EINVAL;
    desktop_layout_io_budget_t budget;
    if (desktop_layout_budget_start(&budget) != DESKTOP_LAYOUT_OK)
        return DESKTOP_LAYOUT_EIO;
    uint32_t timeout = 0U;
    if (desktop_layout_budget_remaining(&budget, &timeout) !=
        DESKTOP_LAYOUT_OK) return DESKTOP_LAYOUT_EIO;
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = reist_vfs_file_open_flags(
        DESKTOP_LAYOUT_PATH, timeout,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT,
        X86OS_O_NOFOLLOW, &handle);
    if (status != 0)
        return status == DESKTOP_LAYOUT_ENOENT ? DESKTOP_LAYOUT_ENOENT
                                              : DESKTOP_LAYOUT_EIO;
    x86os_file_info_t info;
    desktop_clear_bytes(&info, sizeof(info));
    if (desktop_layout_budget_remaining(&budget, &timeout) !=
            DESKTOP_LAYOUT_OK ||
        reist_vfs_file_set_timeout(handle, timeout) != 0 ||
        reist_vfs_file_fstat(handle, &info) != 0 ||
        info.type != X86OS_FILE || info.size == 0U ||
        info.size > DESKTOP_LAYOUT_FILE_CAPACITY) {
        desktop_layout_close_cleanup(handle);
        return info.size > DESKTOP_LAYOUT_FILE_CAPACITY
            ? DESKTOP_LAYOUT_ECAPACITY : DESKTOP_LAYOUT_EIO;
    }
    uint32_t used = 0U;
    uint32_t calls = 0U;
    while (used < info.size && calls < DESKTOP_LAYOUT_IO_CALL_CAPACITY) {
        if (desktop_layout_budget_remaining(&budget, &timeout) !=
                DESKTOP_LAYOUT_OK ||
            reist_vfs_file_set_timeout(handle, timeout) != 0) {
            desktop_layout_close_cleanup(handle);
            return DESKTOP_LAYOUT_EIO;
        }
        int amount = reist_vfs_file_read_bulk(
            handle, desktop_layout_file_bytes + used, info.size - used);
        if (amount <= 0 || (uint32_t)amount > info.size - used) {
            desktop_layout_close_cleanup(handle);
            return DESKTOP_LAYOUT_EIO;
        }
        used += (uint32_t)amount;
        ++calls;
    }
    uint8_t trailing = 0U;
    if (used != info.size ||
        reist_vfs_file_read_bulk(handle, &trailing, 1U) != 0 ||
        desktop_layout_budget_remaining(&budget, &timeout) !=
            DESKTOP_LAYOUT_OK ||
        reist_vfs_file_set_timeout(handle, timeout) != 0 ||
        reist_vfs_file_close(handle) != 0) {
        desktop_layout_close_cleanup(handle);
        return used == info.size ? DESKTOP_LAYOUT_EIO
                                 : DESKTOP_LAYOUT_ECAPACITY;
    }
    return desktop_layout_parse(desktop_layout_file_bytes, used, document);
}

static int desktop_layout_store(
    const desktop_layout_document_t *requested_candidate) {
    if (requested_candidate == 0) return DESKTOP_LAYOUT_EINVAL;
    const desktop_layout_document_t *candidate = requested_candidate;
    uint32_t size = 0U;
    int status = desktop_layout_serialize(
        candidate, desktop_layout_file_bytes,
        DESKTOP_LAYOUT_FILE_CAPACITY, &size);
    if (status != DESKTOP_LAYOUT_OK) return status;
    desktop_layout_io_budget_t budget;
    if (desktop_layout_budget_start(&budget) != DESKTOP_LAYOUT_OK)
        return DESKTOP_LAYOUT_EIO;
    int unlink_status = x86os_unlink(DESKTOP_LAYOUT_TEMP_PATH);
    if (unlink_status != 0 && unlink_status != DESKTOP_LAYOUT_ENOENT)
        return DESKTOP_LAYOUT_EIO;
    int descriptor = x86os_create(DESKTOP_LAYOUT_TEMP_PATH);
    if (descriptor < 0) return DESKTOP_LAYOUT_EIO;
    uint32_t used = 0U;
    uint32_t calls = 0U;
    while (used < size && calls < DESKTOP_LAYOUT_IO_CALL_CAPACITY) {
        uint32_t remaining = 0U;
        if (desktop_layout_budget_remaining(&budget, &remaining) !=
            DESKTOP_LAYOUT_OK) {
            status = DESKTOP_LAYOUT_EIO;
            break;
        }
        (void)remaining;
        int amount = x86os_write(
            descriptor, desktop_layout_file_bytes + used, size - used);
        if (amount <= 0 || (uint32_t)amount > size - used) {
            status = DESKTOP_LAYOUT_EIO;
            break;
        }
        used += (uint32_t)amount;
        ++calls;
    }
    if (used != size && status == DESKTOP_LAYOUT_OK)
        status = DESKTOP_LAYOUT_ECAPACITY;
    int sync_status = status == DESKTOP_LAYOUT_OK
        ? x86os_fsync(descriptor) : -1;
    int close_status = x86os_close(descriptor);
    if (status != DESKTOP_LAYOUT_OK || sync_status != 0 || close_status != 0 ||
        x86os_rename(DESKTOP_LAYOUT_TEMP_PATH, DESKTOP_LAYOUT_PATH) != 0) {
        (void)x86os_unlink(DESKTOP_LAYOUT_TEMP_PATH);
        return status != DESKTOP_LAYOUT_OK ? status : DESKTOP_LAYOUT_EIO;
    }
    desktop_copy_bytes(
        &desktop_layout_document, candidate,
        sizeof(desktop_layout_document));
    return DESKTOP_LAYOUT_OK;
}

static int desktop_layout_rebuild(
    const x86os_display_info_t *display,
    const desktop_explorer_t *explorer) {
    if (display == 0 || explorer == 0) return DESKTOP_LAYOUT_EINVAL;
    uint32_t icon_count = DESKTOP_BUILTIN_ICON_COUNT +
        desktop_dynamic_icon_count(explorer);
    if (icon_count > DESKTOP_LAYOUT_ENTRY_CAPACITY)
        return DESKTOP_LAYOUT_ECAPACITY;
    for (uint32_t index = 0U; index < DESKTOP_BUILTIN_ICON_COUNT; ++index)
        if (desktop_layout_identity_builtin(
                &desktop_layout_identities[index], index) !=
            DESKTOP_LAYOUT_OK) return DESKTOP_LAYOUT_EINVAL;
    for (uint32_t index = 0U;
         index < desktop_dynamic_icon_count(explorer); ++index)
        if (desktop_layout_identity_file(
                &desktop_layout_identities[
                    DESKTOP_BUILTIN_ICON_COUNT + index],
                explorer->desktop_directory.entries[index].name) !=
            DESKTOP_LAYOUT_OK) return DESKTOP_LAYOUT_EINVAL;
    uint32_t work_bottom = display->height > taskbar_height(display)
        ? display->height - taskbar_height(display) : display->height;
    if (display->width <= 16U || work_bottom <= 16U)
        return DESKTOP_LAYOUT_EINVAL;
    desktop_rect_t work_area = {
        8, 8, display->width - 16U, work_bottom - 16U
    };
    uint32_t generation = desktop_layout_view.generation + 1U;
    if (generation == 0U) generation = 1U;
    uint32_t cell_height = max_u32(display->font_height + 42U, 68U);
    int status = desktop_layout_resolve(
        &desktop_layout_document, desktop_layout_identities, icon_count,
        work_area, cell_height, generation, &desktop_layout_view);
    if (status == DESKTOP_LAYOUT_OK) return status;
    desktop_layout_document_initialize(&desktop_layout_candidate);
    return desktop_layout_resolve(
        &desktop_layout_candidate, desktop_layout_identities, icon_count,
        work_area, cell_height, generation, &desktop_layout_view);
}

static uint32_t desktop_layout_documents_equal(
    const desktop_layout_document_t *left,
    const desktop_layout_document_t *right) {
    if (left == 0 || right == 0 || left->entry_count != right->entry_count)
        return 0U;
    for (uint32_t index = 0U; index < left->entry_count; ++index) {
        const desktop_layout_entry_t *entry = &left->entries[index];
        uint32_t found = 0U;
        for (uint32_t other = 0U; other < right->entry_count; ++other) {
            const desktop_layout_entry_t *candidate = &right->entries[other];
            uint32_t same = 1U;
            for (uint32_t byte = 0U;
                 byte < DESKTOP_LAYOUT_IDENTITY_CAPACITY; ++byte) {
                if (entry->identity.value[byte] !=
                    candidate->identity.value[byte]) {
                    same = 0U;
                    break;
                }
                if (entry->identity.value[byte] == '\0') break;
            }
            if (same && entry->column == candidate->column &&
                entry->row == candidate->row) {
                found = 1U;
                break;
            }
        }
        if (!found) return 0U;
    }
    return 1U;
}

static void desktop_layout_probe_publish_geometry(
    const desktop_explorer_t *explorer) {
    if (!desktop_layout_probe_enabled || explorer == 0) return;
    x86os_puts("DESKTOP_ICON_LAYOUT_READY width=");
    x86os_print_number((int)desktop_layout_view.work_area.width);
    x86os_puts(" height=");
    x86os_print_number((int)desktop_layout_view.work_area.height);
    x86os_puts(" count=");
    x86os_print_number((int)desktop_layout_view.entry_count);
    x86os_puts(" columns=");
    x86os_print_number((int)desktop_layout_view.columns);
    x86os_puts(" rows=");
    x86os_print_number((int)desktop_layout_view.rows);
    x86os_putchar('\n');
    for (uint32_t index = 0U;
         index < desktop_layout_view.entry_count; ++index) {
        const char *kind = index == 0U ? "computer"
            : index == 1U ? "control"
            : index == 2U ? "trash"
            : desktop_shortcut_is_filename(
                explorer->desktop_directory.entries[
                    index - DESKTOP_BUILTIN_ICON_COUNT].name)
                ? "shortcut" : "file";
        desktop_rect_t source = desktop_layout_view_rect(
            &desktop_layout_view, index);
        x86os_puts("DESKTOP_ICON_LAYOUT_ICON index=");
        x86os_print_number((int)index);
        x86os_puts(" kind=");
        x86os_puts(kind);
        x86os_puts(" x=");
        x86os_print_number(source.x + (int)(source.width / 2U));
        x86os_puts(" y=");
        x86os_print_number(source.y + (int)(source.height / 2U));
        x86os_putchar('\n');
        desktop_layout_cell_t desired = {
            desktop_layout_view.entries[index].column <
                    desktop_layout_view.columns / 2U
                ? desktop_layout_view.columns - 1U : 0U,
            desktop_layout_view.entries[index].row <
                    desktop_layout_view.rows / 2U
                ? desktop_layout_view.rows - 1U : 0U
        };
        desktop_rect_t desired_rect = desktop_layout_cell_rect(desired);
        desktop_layout_cell_t target;
        if (desired_rect.width != 0U && desired_rect.height != 0U &&
            desktop_layout_drop(
                &desktop_layout_view, index,
                desired_rect.x + (int32_t)(desired_rect.width / 2U),
                desired_rect.y + (int32_t)(desired_rect.height / 2U),
                &target) == DESKTOP_LAYOUT_OK) {
            desktop_rect_t target_rect = desktop_layout_cell_rect(target);
            x86os_puts("DESKTOP_ICON_LAYOUT_DROP_TARGET index=");
            x86os_print_number((int)index);
            x86os_puts(" x=");
            x86os_print_number(
                target_rect.x + (int)(target_rect.width / 2U));
            x86os_puts(" y=");
            x86os_print_number(
                target_rect.y + (int)(target_rect.height / 2U));
            x86os_putchar('\n');
        }
    }
}

static int desktop_layout_probe_resize(void) {
    if (!desktop_layout_probe_enabled ||
        desktop_layout_view.work_area.width < 2U ||
        desktop_layout_view.work_area.height < 2U)
        return DESKTOP_LAYOUT_EINVAL;
    desktop_rect_t smaller = desktop_layout_view.work_area;
    smaller.width /= 2U;
    smaller.height /= 2U;
    uint32_t cell_height = desktop_layout_view.cell_height;
    if (cell_height > smaller.height) cell_height = smaller.height;
    int status = desktop_layout_resolve(
        &desktop_layout_document, desktop_layout_identities,
        desktop_layout_view.entry_count, smaller, cell_height,
        desktop_layout_view.generation + 1U,
        &desktop_layout_probe_view);
    if (status != DESKTOP_LAYOUT_OK) return status;
    for (uint32_t index = 0U;
         index < desktop_layout_probe_view.entry_count; ++index) {
        desktop_rect_t rect = desktop_layout_view_rect(
            &desktop_layout_probe_view, index);
        if (rect.width == 0U || rect.height == 0U ||
            rect.x < smaller.x || rect.y < smaller.y ||
            (uint64_t)(uint32_t)(rect.x - smaller.x) + rect.width >
                smaller.width ||
            (uint64_t)(uint32_t)(rect.y - smaller.y) + rect.height >
                smaller.height)
            return DESKTOP_LAYOUT_EINVAL;
    }
    x86os_puts("DESKTOP_ICON_LAYOUT_RESIZE_OK width=");
    x86os_print_number((int)smaller.width);
    x86os_puts(" height=");
    x86os_print_number((int)smaller.height);
    x86os_putchar('\n');
    return DESKTOP_LAYOUT_OK;
}

static uint32_t desktop_layout_probe_copy_text(
    char *destination, uint32_t capacity, const char *source) {
    if (destination == 0 || capacity == 0U || source == 0) return 0U;
    uint32_t index = 0U;
    while (index < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    if (index == capacity) return 0U;
    destination[index] = '\0';
    return 1U;
}

static int desktop_layout_probe_prepare_shortcut(
    desktop_explorer_t *explorer,
    const x86os_file_info_t *desktop_directory_identity) {
    if (!desktop_layout_probe_enabled || explorer == 0 ||
        desktop_directory_identity == 0)
        return DESKTOP_LAYOUT_EINVAL;
    for (uint32_t index = 0U;
         index < explorer->desktop_directory.entry_count; ++index)
        if (desktop_shortcut_is_filename(
                explorer->desktop_directory.entries[index].name))
            return DESKTOP_LAYOUT_OK;
    desktop_shortcut_create_request_t request;
    desktop_shortcut_create_request_initialize(&request);
    if (!desktop_layout_probe_copy_text(
            request.directory_path, sizeof(request.directory_path),
            DESKTOP_SHORTCUT_DIRECTORY) ||
        !desktop_layout_probe_copy_text(
            request.display_name, sizeof(request.display_name),
            "Layout Probe") ||
        !desktop_layout_probe_copy_text(
            request.target_path, sizeof(request.target_path),
            "/htdocs/readme.txt"))
        return DESKTOP_LAYOUT_ECAPACITY;
    request.target_kind = DESKTOP_SHORTCUT_TARGET_FILE;
    desktop_copy_bytes(
        &request.directory_identity, desktop_directory_identity,
        sizeof(request.directory_identity));
    if (x86os_stat(request.target_path, &request.target_identity) != 0 ||
        request.target_identity.type != X86OS_FILE)
        return DESKTOP_LAYOUT_EIO;
    desktop_shortcut_create_result_t result;
    int status = desktop_shortcut_create(&request, &result);
    if (status != DESKTOP_SHORTCUT_OK || !result.created)
        return DESKTOP_LAYOUT_EIO;
    return desktop_explorer_desktop_refresh(explorer) == DESKTOP_EXPLORER_OK
        ? DESKTOP_LAYOUT_OK : DESKTOP_LAYOUT_EIO;
}

static desktop_rect_t desktop_icon_rect(const x86os_display_info_t *display,
                                        const desktop_explorer_t *explorer,
                                        uint32_t index) {
    (void)display;
    (void)explorer;
    return desktop_layout_view_rect(&desktop_layout_view, index);
}

static void desktop_shortcut_probe_publish_point(
    const char *marker, const char *kind, desktop_rect_t rect) {
    if (!desktop_shortcut_probe_enabled || marker == 0 || kind == 0 ||
        rect.width == 0U || rect.height == 0U) return;
    x86os_puts("DESKTOP_SHORTCUT_PROBE_");
    x86os_puts(marker);
    x86os_puts(" kind=");
    x86os_puts(kind);
    x86os_puts(" x=");
    x86os_print_number(rect.x + (int)(rect.width / 2U));
    x86os_puts(" y=");
    x86os_print_number(rect.y + (int)(rect.height / 2U));
    x86os_putchar('\n');
}

static void desktop_shortcut_probe_publish_geometry(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer) {
    if (!desktop_shortcut_probe_enabled || display == 0 || manager == 0 ||
        explorer == 0) return;
    for (uint32_t window_index = 0U;
         window_index < DESKTOP_WM_CAPACITY; ++window_index) {
        const desktop_explorer_window_t *window =
            &explorer->windows[window_index];
        if (!window->active) continue;
        if (!path_equal_ascii_case(window->path, "/htdocs")) continue;
        for (uint32_t entry_index = 0U;
             entry_index < window->entry_count; ++entry_index) {
            const char *marker = 0;
            const char *kind = 0;
            if (path_equal_ascii_case(
                    window->entries[entry_index].name, "readme.txt")) {
                marker = "TARGET";
                kind = "file";
            } else if (desktop_shortcut_is_filename(
                           window->entries[entry_index].name)) {
                marker = "SIBLING";
                kind = "shortcut";
            }
            if (marker != 0)
                desktop_shortcut_probe_publish_point(
                    marker, kind,
                    desktop_explorer_entry_rect(
                        window,
                        desktop_explorer_content_rect(
                            manager, explorer, window_index),
                        entry_index));
        }
        desktop_rect_t content = desktop_explorer_content_rect(
            manager, explorer, window_index);
        if (content.width > DESKTOP_EXPLORER_SCROLLBAR_EXTENT + 8U &&
            content.height > 8U)
            desktop_shortcut_probe_publish_point(
                "DROP", "source",
                (desktop_rect_t){
                    content.x + (int32_t)content.width -
                        (int32_t)DESKTOP_EXPLORER_SCROLLBAR_EXTENT - 6,
                    content.y + (int32_t)content.height - 6,
                    1U, 1U
                });
    }
    for (uint32_t entry_index = 0U;
         entry_index < explorer->desktop_directory.entry_count;
         ++entry_index) {
        desktop_shortcut_probe_publish_point(
            "ICON",
            desktop_shortcut_is_filename(
                explorer->desktop_directory.entries[entry_index].name)
                ? "shortcut" : "file",
            desktop_icon_rect(
                display, explorer,
                DESKTOP_BUILTIN_ICON_COUNT + entry_index));
    }
    desktop_rect_t taskbar = desktop_taskbar_rect(display);
    for (int32_t y = 8; y + 8 < taskbar.y; y += 24) {
        for (int32_t x = 8; x + 8 < (int32_t)display->width; x += 24) {
            if (desktop_wm_window_at(manager, x, y) ==
                    DESKTOP_WM_NO_WINDOW &&
                desktop_icon_at_position(display, explorer, x, y) ==
                    DESKTOP_WM_NO_WINDOW) {
                desktop_shortcut_probe_publish_point(
                    "DROP", "desktop",
                    (desktop_rect_t){x, y, 1U, 1U});
                return;
            }
        }
    }
}

static int desktop_icon_at_position(
    const x86os_display_info_t *display,
    const desktop_explorer_t *explorer, int32_t x, int32_t y) {
    uint32_t icon_count = DESKTOP_BUILTIN_ICON_COUNT +
        desktop_dynamic_icon_count(explorer);
    for (uint32_t index = 0U; index < icon_count; ++index) {
        if (point_in_rect(
                desktop_icon_rect(display, explorer, index), x, y))
            return (int)index;
    }
    return DESKTOP_WM_NO_WINDOW;
}

static desktop_rect_t desktop_drag_feedback_rect(void) {
    if (desktop_drag.phase != DESKTOP_DRAG_PHASE_DRAGGING)
        return (desktop_rect_t){0, 0, 0U, 0U};
    return (desktop_rect_t){
        desktop_drag.current_x + 6, desktop_drag.current_y + 6,
        DESKTOP_DRAG_FEEDBACK_SIZE, DESKTOP_DRAG_FEEDBACK_SIZE
    };
}

static desktop_rect_t desktop_layout_cell_rect(desktop_layout_cell_t cell) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (cell.column >= desktop_layout_view.columns ||
        cell.row >= desktop_layout_view.rows ||
        desktop_layout_view.cell_width == 0U ||
        desktop_layout_view.cell_height == 0U)
        return empty;
    uint64_t x = (uint64_t)(uint32_t)desktop_layout_view.work_area.x +
        (uint64_t)cell.column * desktop_layout_view.cell_width;
    uint64_t y = (uint64_t)(uint32_t)desktop_layout_view.work_area.y +
        (uint64_t)cell.row * desktop_layout_view.cell_height;
    if (x > INT32_MAX || y > INT32_MAX) return empty;
    return (desktop_rect_t){
        (int32_t)x, (int32_t)y,
        desktop_layout_view.cell_width,
        desktop_layout_view.cell_height
    };
}

static int desktop_layout_arm_icon(uint32_t index, uint32_t kind,
                                   int32_t x, int32_t y) {
    if (index >= desktop_layout_view.entry_count ||
        (kind != DESKTOP_DRAG_OBJECT_FILE &&
         kind != DESKTOP_DRAG_OBJECT_APPLICATION) ||
        desktop_layout_view.generation == 0U)
        return DESKTOP_LAYOUT_EINVAL;
    desktop_drag_object_t object;
    desktop_drag_object_initialize(&object);
    object.kind = kind;
    object.operations = DESKTOP_DRAG_OPERATION_LAYOUT;
    object.source_id = DESKTOP_LAYOUT_SOURCE_ID;
    object.source_generation = desktop_layout_view.generation;
    object.data_size = sizeof(index);
    desktop_copy_bytes(object.data, &index, sizeof(index));
    int status = desktop_drag_arm(&desktop_drag, &object, x, y);
    if (status == DESKTOP_DRAG_OK)
        desktop_layout_drag_source_index = index;
    return status;
}

static uint32_t desktop_layout_drop_target(
    const desktop_wm_t *manager, const desktop_ui_state_t *ui,
    int32_t x, int32_t y, desktop_drag_target_t *target) {
    desktop_layout_hover_valid = 0U;
    if (manager == 0 || ui == 0 || target == 0 ||
        desktop_layout_drag_source_index >= desktop_layout_view.entry_count ||
        desktop_layout_view.generation == 0U || ui->dialog.visible ||
        ui->menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->taskbar_capture_slot != DESKTOP_WM_NO_TARGET ||
        desktop_wm_window_at(manager, x, y) != DESKTOP_WM_NO_WINDOW ||
        desktop_layout_drop(
            &desktop_layout_view, desktop_layout_drag_source_index,
            x, y, &desktop_layout_hover_cell) != DESKTOP_LAYOUT_OK)
        return 0U;
    desktop_drag_target_initialize(target);
    target->bounds = desktop_layout_view.work_area;
    target->accepted_kinds =
        DESKTOP_DRAG_KIND_MASK(DESKTOP_DRAG_OBJECT_FILE) |
        DESKTOP_DRAG_KIND_MASK(DESKTOP_DRAG_OBJECT_APPLICATION);
    target->operations = DESKTOP_DRAG_OPERATION_LAYOUT;
    target->target_id = DESKTOP_LAYOUT_TARGET_ID;
    target->target_generation = desktop_layout_view.generation;
    desktop_layout_hover_valid = 1U;
    return 1U;
}

static uint32_t desktop_trash_drop_target(
    const desktop_wm_t *manager, const desktop_ui_state_t *ui,
    const desktop_explorer_t *explorer,
    const x86os_display_info_t *display, int32_t x, int32_t y,
    desktop_drag_target_t *target) {
    if (manager == 0 || ui == 0 || display == 0 || target == 0 ||
        !desktop_trash.available || ui->dialog.visible ||
        ui->menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->taskbar_capture_slot != DESKTOP_WM_NO_TARGET ||
        (desktop_drag.object.operations & DESKTOP_DRAG_OPERATION_MOVE) == 0U ||
        desktop_wm_window_at(manager, x, y) != DESKTOP_WM_NO_WINDOW ||
        desktop_icon_at_position(display, explorer, x, y) != 2)
        return 0U;
    desktop_drag_target_initialize(target);
    target->bounds = desktop_icon_rect(display, explorer, 2U);
    target->accepted_kinds =
        DESKTOP_DRAG_KIND_MASK(DESKTOP_DRAG_OBJECT_FILE);
    target->operations = DESKTOP_DRAG_OPERATION_MOVE;
    target->target_id = DESKTOP_TRASH_TARGET_ID;
    target->target_generation = desktop_trash.generation;
    return 1U;
}

typedef struct desktop_directory_destination {
    char path[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    x86os_file_info_t identity;
    desktop_rect_t bounds;
    uint32_t target_id;
    uint32_t generation;
} desktop_directory_destination_t;

static uint32_t desktop_copy_path(
    char destination[DESKTOP_FILE_MOVE_PATH_CAPACITY],
    const char *source) {
    size_t length = bounded_text_length(
        source, DESKTOP_FILE_MOVE_PATH_CAPACITY);
    if (length == 0U || length == DESKTOP_FILE_MOVE_PATH_CAPACITY)
        return 0U;
    for (size_t index = 0U; index <= length; ++index)
        destination[index] = source[index];
    return 1U;
}

static uint32_t desktop_directory_destination_window(
    const desktop_explorer_t *explorer, uint32_t window_index,
    uint32_t entry_index, desktop_rect_t bounds,
    desktop_directory_destination_t *destination) {
    if (explorer == 0 || destination == 0 ||
        window_index >= DESKTOP_WM_CAPACITY ||
        !explorer->windows[window_index].active) return 0U;
    const desktop_explorer_window_t *window =
        &explorer->windows[window_index];
    if (entry_index == DESKTOP_EXPLORER_NO_ENTRY) {
        if (!desktop_copy_path(destination->path, window->path))
            return 0U;
        desktop_copy_bytes(
            &destination->identity, &window->directory_identity,
            sizeof(destination->identity));
        destination->target_id =
            DESKTOP_DIRECTORY_TARGET_WINDOW_BASE + window_index;
    } else {
        if (entry_index >= window->entry_count ||
            window->entries[entry_index].type != X86OS_DIRECTORY ||
            desktop_explorer_child_path(
                window, entry_index, destination->path,
                sizeof(destination->path)) != DESKTOP_EXPLORER_OK)
            return 0U;
        desktop_copy_bytes(
            &destination->identity, &window->entries[entry_index],
            sizeof(destination->identity));
        destination->target_id = DESKTOP_DIRECTORY_TARGET_CHILD_BASE +
            window_index * DESKTOP_EXPLORER_ENTRY_CAPACITY + entry_index;
    }
    destination->generation = window->snapshot_generation;
    destination->bounds = bounds;
    return desktop_file_move_destination_allowed(destination->path);
}

static uint32_t desktop_directory_destination_desktop(
    const desktop_explorer_t *explorer, uint32_t entry_index,
    desktop_rect_t bounds,
    desktop_directory_destination_t *destination) {
    if (explorer == 0 || destination == 0 ||
        !explorer->desktop_directory.active) return 0U;
    const desktop_explorer_window_t *window =
        &explorer->desktop_directory;
    if (entry_index == DESKTOP_EXPLORER_NO_ENTRY) {
        if (window->truncated ||
            window->entry_count >= DESKTOP_EXPLORER_ENTRY_CAPACITY)
            return 0U;
        if (!desktop_copy_path(destination->path, window->path))
            return 0U;
        desktop_copy_bytes(
            &destination->identity, &window->directory_identity,
            sizeof(destination->identity));
        destination->target_id = DESKTOP_DIRECTORY_TARGET_DESKTOP;
    } else {
        if (entry_index >= window->entry_count ||
            window->entries[entry_index].type != X86OS_DIRECTORY ||
            desktop_explorer_child_path(
                window, entry_index, destination->path,
                sizeof(destination->path)) != DESKTOP_EXPLORER_OK)
            return 0U;
        desktop_copy_bytes(
            &destination->identity, &window->entries[entry_index],
            sizeof(destination->identity));
        destination->target_id =
            DESKTOP_DIRECTORY_TARGET_DESKTOP_CHILD_BASE + entry_index;
    }
    destination->generation = window->snapshot_generation;
    destination->bounds = bounds;
    return desktop_file_move_destination_allowed(destination->path);
}

static uint32_t desktop_directory_destination_at(
    const desktop_wm_t *manager, const desktop_explorer_t *explorer,
    const x86os_display_info_t *display, int32_t x, int32_t y,
    desktop_directory_destination_t *destination) {
    if (manager == 0 || explorer == 0 || display == 0 ||
        destination == 0) return 0U;
    int window_hit = desktop_wm_window_at(manager, x, y);
    if (window_hit >= 0 && window_hit < (int)DESKTOP_WM_CAPACITY) {
        uint32_t window_index = (uint32_t)window_hit;
        desktop_rect_t content = desktop_explorer_content_rect(
            manager, explorer, window_index);
        if (!point_in_rect(content, x, y)) return 0U;
        uint32_t entry_index = desktop_explorer_entry_at(
            &explorer->windows[window_index], content, x, y);
        desktop_rect_t bounds = content;
        if (entry_index != DESKTOP_EXPLORER_NO_ENTRY) {
            bounds = desktop_explorer_entry_rect(
                &explorer->windows[window_index], content, entry_index);
        }
        return desktop_directory_destination_window(
            explorer, window_index, entry_index, bounds, destination);
    }
    if (window_hit != DESKTOP_WM_NO_WINDOW ||
        y < 0 || y >= desktop_taskbar_rect(display).y) return 0U;
    int icon = desktop_icon_at_position(display, explorer, x, y);
    if (icon >= (int)DESKTOP_BUILTIN_ICON_COUNT) {
        uint32_t entry_index =
            (uint32_t)icon - DESKTOP_BUILTIN_ICON_COUNT;
        return desktop_directory_destination_desktop(
            explorer, entry_index,
            desktop_icon_rect(display, explorer, (uint32_t)icon),
            destination);
    }
    if (icon != DESKTOP_WM_NO_WINDOW) return 0U;
    desktop_rect_t work = {
        0, 0, display->width,
        (uint32_t)desktop_taskbar_rect(display).y
    };
    return desktop_directory_destination_desktop(
        explorer, DESKTOP_EXPLORER_NO_ENTRY, work, destination);
}

static uint32_t desktop_directory_drop_target(
    const desktop_wm_t *manager, const desktop_explorer_t *explorer,
    const desktop_ui_state_t *ui,
    const x86os_display_info_t *display, int32_t x, int32_t y,
    desktop_drag_target_t *target) {
    if (manager == 0 || explorer == 0 || ui == 0 || display == 0 ||
        target == 0 || ui->dialog.visible ||
        ui->menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->taskbar_capture_slot != DESKTOP_WM_NO_TARGET)
        return 0U;
    desktop_explorer_drag_file_t file;
    char source_directory[DESKTOP_EXPLORER_PATH_CAPACITY];
    x86os_file_info_t source_directory_identity;
    if (desktop_explorer_drag_validate(
            explorer, &desktop_drag.object, &file) != DESKTOP_EXPLORER_OK ||
        file.identity.type != X86OS_FILE ||
        !desktop_file_move_source_allowed(file.path) ||
        desktop_explorer_drag_source_directory(
            explorer, &desktop_drag.object, source_directory,
            &source_directory_identity) != DESKTOP_EXPLORER_OK)
        return 0U;
    desktop_directory_destination_t destination;
    if (!desktop_directory_destination_at(
            manager, explorer, display, x, y, &destination) ||
        path_equal_ascii_case(source_directory, destination.path))
        return 0U;
    desktop_drag_target_initialize(target);
    target->bounds = destination.bounds;
    target->accepted_kinds =
        DESKTOP_DRAG_KIND_MASK(DESKTOP_DRAG_OBJECT_FILE);
    target->operations = DESKTOP_DRAG_OPERATION_MOVE;
    target->target_id = destination.target_id;
    target->target_generation = destination.generation;
    return 1U;
}

static uint32_t desktop_directory_destination_from_target(
    const desktop_explorer_t *explorer, uint32_t target_id,
    uint32_t generation,
    desktop_directory_destination_t *destination) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (target_id == DESKTOP_DIRECTORY_TARGET_DESKTOP) {
        if (!desktop_directory_destination_desktop(
                explorer, DESKTOP_EXPLORER_NO_ENTRY,
                empty, destination)) return 0U;
    } else if (target_id >= DESKTOP_DIRECTORY_TARGET_WINDOW_BASE &&
               target_id < DESKTOP_DIRECTORY_TARGET_WINDOW_BASE +
                   DESKTOP_WM_CAPACITY) {
        if (!desktop_directory_destination_window(
                explorer,
                target_id - DESKTOP_DIRECTORY_TARGET_WINDOW_BASE,
                DESKTOP_EXPLORER_NO_ENTRY, empty, destination))
            return 0U;
    } else if (target_id >= DESKTOP_DIRECTORY_TARGET_CHILD_BASE &&
               target_id < DESKTOP_DIRECTORY_TARGET_DESKTOP_CHILD_BASE) {
        uint32_t offset =
            target_id - DESKTOP_DIRECTORY_TARGET_CHILD_BASE;
        uint32_t window_index =
            offset / DESKTOP_EXPLORER_ENTRY_CAPACITY;
        uint32_t entry_index =
            offset % DESKTOP_EXPLORER_ENTRY_CAPACITY;
        if (!desktop_directory_destination_window(
                explorer, window_index, entry_index, empty, destination))
            return 0U;
    } else if (target_id >=
                   DESKTOP_DIRECTORY_TARGET_DESKTOP_CHILD_BASE &&
               target_id < DESKTOP_DIRECTORY_TARGET_DESKTOP_CHILD_BASE +
                   DESKTOP_EXPLORER_ENTRY_CAPACITY) {
        if (!desktop_directory_destination_desktop(
                explorer,
                target_id - DESKTOP_DIRECTORY_TARGET_DESKTOP_CHILD_BASE,
                empty, destination)) return 0U;
    } else {
        return 0U;
    }
    return destination->target_id == target_id &&
        destination->generation == generation;
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
        0x00513A8CU, 0x002B8A68U, 0x00808A96U, 0x002868B0U,
        0x0096A0ACU,
    };
    if (kind >= DESKTOP_EXPLORER_ICON_COUNT) kind =
        DESKTOP_EXPLORER_ICON_UNKNOWN;
    if (kind == DESKTOP_EXPLORER_ICON_SHORTCUT) {
        draw_shortcut_icon_fallback(context, symbol);
        return;
    }
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

static void draw_trash_icon_fallback(
    const desktop_render_context_t *context, desktop_rect_t symbol,
    uint32_t full) {
    if (symbol.width < 18U || symbol.height < 18U) {
        draw_bevel(context, symbol, 0x0090A0A8U, 1U);
        return;
    }
    uint32_t body_width = symbol.width - 12U;
    uint32_t body_height = symbol.height - 12U;
    desktop_rect_t body = {
        symbol.x + 6, symbol.y + 9, body_width, body_height
    };
    fill_rect_clipped(context, body, 0x00B8C2C8U);
    fill_rect_clipped(context,
        (desktop_rect_t){body.x, body.y, body.width, 2U}, color_light);
    fill_rect_clipped(context,
        (desktop_rect_t){body.x, body.y, 2U, body.height}, color_light);
    fill_rect_clipped(context,
        (desktop_rect_t){body.x + (int32_t)body.width - 2, body.y,
                         2U, body.height}, color_shadow);
    fill_rect_clipped(context,
        (desktop_rect_t){symbol.x + 3, symbol.y + 6,
                         symbol.width - 6U, 3U}, 0x00808C94U);
    fill_rect_clipped(context,
        (desktop_rect_t){symbol.x + 11, symbol.y + 3,
                         symbol.width - 22U, 3U}, 0x00808C94U);
    for (uint32_t line = 0U; line < 3U; ++line)
        fill_rect_clipped(context,
            (desktop_rect_t){body.x + 4 + (int32_t)(line * 5U), body.y + 4,
                             2U, body.height > 8U ? body.height - 8U : 1U},
            0x00748088U);
    if (full) {
        fill_rect_clipped(context,
            (desktop_rect_t){symbol.x + 8, symbol.y + 2, 7U, 9U},
            0x00F0E8D0U);
        fill_rect_clipped(context,
            (desktop_rect_t){symbol.x + 17, symbol.y + 1, 7U, 10U},
            0x00D8E8F0U);
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
    size_t byte_length = 0U;
    size_t scalar_count = 0U;
    if (!unicode_text_measure(text, X86OS_DISPLAY_MAX_TEXT,
                              &byte_length, &scalar_count) ||
        scalar_count > maximum_chars)
        return bounds.x + (int32_t)horizontal_padding;
    uint32_t text_width = (uint32_t)scalar_count * display->font_width;
    return bounds.x + (int32_t)horizontal_padding +
        (int32_t)((usable - text_width) / 2U);
}

static void render_icon(const desktop_render_context_t *context,
                        const desktop_explorer_t *explorer, uint32_t index) {
    const x86os_display_info_t *display = context->display;
    desktop_rect_t rect = desktop_icon_rect(display, explorer, index);
    if (!intersect_rects(rect, context->clip, 0)) return;
    uint32_t shortcut_index = index >= DESKTOP_BUILTIN_ICON_COUNT
        ? index - DESKTOP_BUILTIN_ICON_COUNT : UINT32_MAX;
    if (shortcut_index != UINT32_MAX &&
        (explorer == 0 ||
         shortcut_index >= explorer->desktop_directory.entry_count)) return;
    uint32_t target_hot = index == 2U &&
        desktop_drag.phase == DESKTOP_DRAG_PHASE_DRAGGING &&
        desktop_drag.feedback == DESKTOP_DRAG_FEEDBACK_VALID &&
        desktop_drag.target_id == DESKTOP_TRASH_TARGET_ID;
    uint32_t selected = shortcut_index != UINT32_MAX
        ? desktop_shortcut_selected == shortcut_index
        : index == 0U
            ? explorer != 0 && explorer->desktop_selected
            : index == 1U ? control_panel_selected : trash_selected;
    uint32_t icon_size = min_u32(
        min_u32(rect.height > display->font_height + 6U
            ? rect.height - display->font_height - 6U : 12U,
            DESKTOP_FILE_ICON_SIZE),
        rect.width);
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
    if (selected || target_hot)
        draw_bevel(context, focus,
                   target_hot ? 0x0068B878U : color_face, 0U);
    if (shortcut_index != UINT32_MAX) {
        const x86os_file_info_t *entry =
            &explorer->desktop_directory.entries[shortcut_index];
        uint32_t kind = desktop_explorer_icon_kind(
            entry,
            explorer->desktop_directory
                .directory_nonempty[shortcut_index]);
        if (!draw_cached_file_icon(
                context, symbol, kind, selected, 1U)) {
            if (kind == DESKTOP_EXPLORER_ICON_SHORTCUT)
                draw_shortcut_icon_fallback(context, symbol);
            else
                draw_file_icon_fallback(context, symbol, kind);
        }
    } else if (index == 2U) {
        if (!draw_cached_trash_icon(
                context, symbol, desktop_trash.full,
                selected || target_hot))
            draw_trash_icon_fallback(context, symbol, desktop_trash.full);
    } else {
        uint32_t kind = index == 0U ? DESKTOP_EXPLORER_ICON_FOLDER_FULL
                                    : DESKTOP_EXPLORER_ICON_SETTINGS;
        if (!draw_cached_file_icon(context, symbol, kind, selected, 1U))
            draw_file_icon_fallback(context, symbol, kind);
    }
    /* Anchor the caption to the visible symbol, not to the bottom of the
     * deliberately tall hit cell.  This keeps icon and title recognizable as
     * one desktop object while preserving the generous input target. */
    uint32_t text_y_offset = 3U + symbol.height + 3U;
    if (text_y_offset + display->font_height > rect.height) {
        text_y_offset = rect.height > display->font_height
            ? rect.height - display->font_height : 0U;
    }
    const char *title = shortcut_index != UINT32_MAX
        ? explorer->desktop_directory.entries[shortcut_index].name
        : desktop_icons[index].title;
    draw_text_clipped(
        context, centered_text_x(display, rect, title, 4U),
        rect.y + (int32_t)text_y_offset, title,
        rect.width > 8U ? rect.width - 8U : 1U, color_title_text,
        selected || target_hot ? color_active : color_desktop);
}

static void render_drag_feedback(
    const desktop_render_context_t *context) {
    if (context == 0 ||
        desktop_drag.phase != DESKTOP_DRAG_PHASE_DRAGGING) return;
    if (desktop_layout_hover_valid &&
        desktop_drag.feedback == DESKTOP_DRAG_FEEDBACK_VALID &&
        desktop_drag.target_id == DESKTOP_LAYOUT_TARGET_ID) {
        desktop_rect_t target = desktop_layout_cell_rect(
            desktop_layout_hover_cell);
        draw_bevel(context, target, 0x0068B878U, 0U);
    }
    desktop_rect_t feedback = desktop_drag_feedback_rect();
    desktop_rect_t symbol = {
        feedback.x + 2, feedback.y + 2,
        DESKTOP_FILE_ICON_SIZE, DESKTOP_FILE_ICON_SIZE
    };
    if (!draw_cached_file_icon(
            context, symbol, DESKTOP_EXPLORER_ICON_UNKNOWN, 0U, 1U))
        draw_file_icon_fallback(
            context, symbol, DESKTOP_EXPLORER_ICON_UNKNOWN);
    desktop_rect_t badge = {
        feedback.x, feedback.y,
        12U, 12U
    };
    uint32_t valid =
        desktop_drag.feedback == DESKTOP_DRAG_FEEDBACK_VALID;
    fill_rect_clipped(context, badge,
                      valid ? 0x00009030U : 0x00B02020U);
    if (valid) {
        fill_rect_clipped(context,
            (desktop_rect_t){badge.x + 3, badge.y + 6, 2U, 3U}, color_light);
        fill_rect_clipped(context,
            (desktop_rect_t){badge.x + 5, badge.y + 8, 2U, 2U}, color_light);
        fill_rect_clipped(context,
            (desktop_rect_t){badge.x + 7, badge.y + 4, 2U, 5U}, color_light);
    } else {
        for (uint32_t offset = 0U; offset < 6U; ++offset) {
            fill_rect_clipped(context,
                (desktop_rect_t){badge.x + 3 + (int32_t)offset,
                                 badge.y + 3 + (int32_t)offset, 1U, 1U},
                color_light);
            fill_rect_clipped(context,
                (desktop_rect_t){badge.x + 8 - (int32_t)offset,
                                 badge.y + 3 + (int32_t)offset, 1U, 1U},
                color_light);
        }
    }
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

static uint32_t desktop_window_is_trash(
    const desktop_explorer_t *explorer, uint32_t window_index) {
    return explorer != 0 && window_index < DESKTOP_WM_CAPACITY &&
        explorer->windows[window_index].active &&
        path_equal_ascii_case(
            explorer->windows[window_index].path,
            DESKTOP_TRASH_FILES_PATH);
}

static desktop_rect_t desktop_explorer_toolbar_button(
    desktop_rect_t toolbar, uint32_t *offset, uint32_t requested_width) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (offset == 0 || toolbar.width <= DESKTOP_EXPLORER_BUTTON_GAP * 2U ||
        toolbar.height <= 6U || *offset >= toolbar.width) return empty;
    uint32_t remaining = toolbar.width - *offset;
    uint32_t width = requested_width < remaining ? requested_width : remaining;
    desktop_rect_t result = {
        toolbar.x + (int32_t)*offset, toolbar.y + 3,
        width, toolbar.height - 6U};
    uint32_t advance = width;
    if (advance <= UINT32_MAX - DESKTOP_EXPLORER_BUTTON_GAP)
        advance += DESKTOP_EXPLORER_BUTTON_GAP;
    *offset = advance > toolbar.width - *offset
        ? toolbar.width : *offset + advance;
    return result;
}

static desktop_explorer_chrome_t desktop_explorer_chrome(
    const desktop_wm_t *manager, const desktop_explorer_t *explorer,
    uint32_t window_index) {
    desktop_explorer_chrome_t chrome = {0};
    desktop_rect_t client = desktop_window_client_rect(manager, window_index);
    if (desktop_window_is_trash(explorer, window_index)) {
        uint32_t reserved = min_u32(client.height,
                                    DESKTOP_TRASH_ACTION_HEIGHT);
        client.height -= reserved;
    }
    uint32_t toolbar_height = min_u32(
        client.height, DESKTOP_EXPLORER_TOOLBAR_HEIGHT);
    chrome.toolbar = (desktop_rect_t){
        client.x, client.y, client.width, toolbar_height};
    client.y += (int32_t)toolbar_height;
    client.height -= toolbar_height;
    uint32_t address_height = min_u32(
        client.height, DESKTOP_EXPLORER_ADDRESS_HEIGHT);
    chrome.address = (desktop_rect_t){
        client.x, client.y, client.width, address_height};
    client.y += (int32_t)address_height;
    client.height -= address_height;
    uint32_t status_height = min_u32(
        client.height, DESKTOP_EXPLORER_STATUS_HEIGHT);
    chrome.content = (desktop_rect_t){
        client.x, client.y, client.width, client.height - status_height};
    chrome.status = (desktop_rect_t){
        client.x,
        client.y + (int32_t)(client.height - status_height),
        client.width, status_height};

    uint32_t offset = DESKTOP_EXPLORER_BUTTON_GAP;
    chrome.back = desktop_explorer_toolbar_button(
        chrome.toolbar, &offset, DESKTOP_EXPLORER_BACK_WIDTH);
    chrome.forward = desktop_explorer_toolbar_button(
        chrome.toolbar, &offset, DESKTOP_EXPLORER_SMALL_BUTTON_WIDTH);
    chrome.up = desktop_explorer_toolbar_button(
        chrome.toolbar, &offset, DESKTOP_EXPLORER_SMALL_BUTTON_WIDTH);
    chrome.refresh = desktop_explorer_toolbar_button(
        chrome.toolbar, &offset, DESKTOP_EXPLORER_REFRESH_WIDTH);
    chrome.view = desktop_explorer_toolbar_button(
        chrome.toolbar, &offset, DESKTOP_EXPLORER_VIEW_WIDTH);
    return chrome;
}

static desktop_rect_t desktop_explorer_content_rect(
    const desktop_wm_t *manager, const desktop_explorer_t *explorer,
    uint32_t window_index) {
    return desktop_explorer_chrome(
        manager, explorer, window_index).content;
}

static desktop_rect_t desktop_trash_restore_rect(
    const desktop_wm_t *manager, const desktop_explorer_t *explorer,
    uint32_t window_index) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (!desktop_window_is_trash(explorer, window_index)) return empty;
    desktop_rect_t client = desktop_window_client_rect(manager, window_index);
    if (client.height < DESKTOP_TRASH_ACTION_HEIGHT) return empty;
    uint32_t width = min_u32(client.width > 16U ? client.width - 16U : 1U,
                             190U);
    uint32_t height = DESKTOP_TRASH_ACTION_HEIGHT > 8U
        ? DESKTOP_TRASH_ACTION_HEIGHT - 8U : 1U;
    return (desktop_rect_t){
        client.x + (int32_t)((client.width - width) / 2U),
        client.y + (int32_t)client.height -
            (int32_t)DESKTOP_TRASH_ACTION_HEIGHT + 4,
        width, height,
    };
}

static desktop_explorer_detail_columns_t desktop_explorer_detail_columns(
    desktop_rect_t row) {
    desktop_explorer_detail_columns_t columns = {0};
    uint32_t name_width = row.width / 2U;
    uint32_t remaining = row.width - name_width;
    uint32_t type_width = remaining / 3U;
    remaining -= type_width;
    uint32_t size_width = remaining / 3U;
    uint32_t modified_width = remaining - size_width;
    columns.name = (desktop_rect_t){
        row.x, row.y, name_width, row.height};
    columns.type = (desktop_rect_t){
        row.x + (int32_t)name_width, row.y, type_width, row.height};
    columns.size = (desktop_rect_t){
        columns.type.x + (int32_t)type_width,
        row.y, size_width, row.height};
    columns.modified = (desktop_rect_t){
        columns.size.x + (int32_t)size_width,
        row.y, modified_width, row.height};
    return columns;
}

static void render_explorer_details_header_cell(
    const desktop_render_context_t *context, desktop_rect_t cell,
    const char *label) {
    if (cell.width == 0U || cell.height == 0U) return;
    draw_bevel(context, cell, color_face, 1U);
    const x86os_display_info_t *display = context->display;
    int32_t y = cell.y + (int32_t)((cell.height > display->font_height
        ? cell.height - display->font_height : 0U) / 2U);
    draw_text_clipped(
        context, cell.x + 4, y, label,
        cell.width > 8U ? cell.width - 8U : 1U,
        color_text, color_face);
}

static void render_explorer_details_header(
    const desktop_render_context_t *context,
    const desktop_explorer_window_t *explorer_window,
    desktop_rect_t client) {
    if (context == 0 || explorer_window == 0 ||
        explorer_window->view != DESKTOP_EXPLORER_VIEW_DETAILS) return;
    desktop_explorer_layout_t layout =
        desktop_explorer_layout(explorer_window, client);
    if (layout.header.width == 0U || layout.header.height == 0U) return;
    desktop_explorer_detail_columns_t columns =
        desktop_explorer_detail_columns(layout.header);
    render_explorer_details_header_cell(context, columns.name, "Name");
    render_explorer_details_header_cell(context, columns.type, "Typ");
    render_explorer_details_header_cell(context, columns.size, "Groesse");
    render_explorer_details_header_cell(
        context, columns.modified, "Geaendert UTC");
    desktop_rect_t corner = {
        layout.scrollbar.x, layout.header.y,
        layout.scrollbar.width, layout.header.height,
    };
    draw_bevel(context, corner, color_face, 1U);
}

static void render_explorer_details_entry(
    const desktop_render_context_t *context,
    const desktop_explorer_window_t *explorer_window,
    desktop_rect_t cell, uint32_t entry_index) {
    const x86os_display_info_t *display = context->display;
    const x86os_file_info_t *entry = &explorer_window->entries[entry_index];
    uint32_t selected = explorer_window->selected == entry_index;
    uint32_t background = selected ? color_active : color_client;
    uint32_t foreground = selected ? color_title_text : color_text;
    fill_rect_clipped(context, cell, background);
    desktop_explorer_detail_columns_t columns =
        desktop_explorer_detail_columns(cell);
    uint32_t symbol_size = min_u32(
        DESKTOP_EXPLORER_DETAILS_ICON_SIZE,
        cell.height > 4U ? cell.height - 4U : 1U);
    if (symbol_size > columns.name.width)
        symbol_size = columns.name.width;
    desktop_rect_t symbol = {
        columns.name.x + 2,
        columns.name.y + (int32_t)((columns.name.height - symbol_size) / 2U),
        symbol_size, symbol_size,
    };
    uint32_t kind = desktop_explorer_icon_kind(
        entry, explorer_window->directory_nonempty[entry_index]);
    if (symbol.width != 0U && symbol.height != 0U &&
        !draw_cached_file_icon(context, symbol, kind, selected, 0U))
        draw_file_icon_fallback(context, symbol, kind);
    int32_t text_y = cell.y + (int32_t)((cell.height > display->font_height
        ? cell.height - display->font_height : 0U) / 2U);
    uint32_t name_offset = symbol_size + 6U;
    draw_text_clipped(
        context, columns.name.x + (int32_t)name_offset, text_y,
        entry->name,
        columns.name.width > name_offset + 4U
            ? columns.name.width - name_offset - 4U : 1U,
        foreground, background);
    draw_text_clipped(
        context, columns.type.x + 4, text_y,
        desktop_explorer_type_text(
            entry, explorer_window->directory_nonempty[entry_index]),
        columns.type.width > 8U ? columns.type.width - 8U : 1U,
        foreground, background);

    char size[DESKTOP_EXPLORER_SIZE_TEXT_CAPACITY];
    const char *size_text = "-";
    if (entry->type != X86OS_DIRECTORY &&
        desktop_explorer_format_size(
            entry->size, size, sizeof(size)) == DESKTOP_EXPLORER_OK)
        size_text = size;
    size_t size_length = bounded_text_length(
        size_text, DESKTOP_EXPLORER_SIZE_TEXT_CAPACITY);
    uint32_t size_pixels = size_length != 0U &&
        display->font_width > UINT32_MAX / size_length
        ? UINT32_MAX : (uint32_t)size_length * display->font_width;
    int32_t size_x = columns.size.x + 4;
    if (size_pixels != UINT32_MAX && columns.size.width > size_pixels &&
        columns.size.width - size_pixels > 8U)
        size_x = columns.size.x +
            (int32_t)(columns.size.width - size_pixels - 4U);
    draw_text_clipped(
        context, size_x, text_y, size_text,
        columns.size.width > 8U ? columns.size.width - 8U : 1U,
        foreground, background);

    char modified[DESKTOP_EXPLORER_MODIFIED_TEXT_CAPACITY];
    if (desktop_explorer_format_modified_utc(
            entry->modify_time, modified, sizeof(modified)) !=
        DESKTOP_EXPLORER_OK) modified[0] = '\0';
    draw_text_clipped(
        context, columns.modified.x + 4, text_y, modified,
        columns.modified.width > 8U ? columns.modified.width - 8U : 1U,
        foreground, background);
}

static void render_explorer_entry(
    const desktop_render_context_t *context,
    const desktop_explorer_window_t *explorer_window,
    desktop_rect_t client, uint32_t entry_index) {
    desktop_rect_t cell = desktop_explorer_entry_rect(
        explorer_window, client, entry_index);
    if (cell.width == 0U || cell.height == 0U ||
        !intersect_rects(cell, context->clip, 0)) return;
    if (explorer_window->view == DESKTOP_EXPLORER_VIEW_DETAILS) {
        render_explorer_details_entry(
            context, explorer_window, cell, entry_index);
        return;
    }
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

static void render_explorer_scrollbar(
    const desktop_render_context_t *context,
    const desktop_explorer_window_t *explorer_window,
    desktop_rect_t client) {
    if (context == 0 || explorer_window == 0) return;
    desktop_explorer_layout_t layout =
        desktop_explorer_layout(explorer_window, client);
    if (layout.scrollbar.width == 0U || layout.scrollbar.height == 0U)
        return;
    const x86os_display_info_t *display = context->display;
    fill_rect_clipped(context, layout.track, color_light);
    draw_bevel(context, layout.decrement, color_face,
               explorer_window->scroll_capture !=
                   DESKTOP_EXPLORER_SCROLL_DECREMENT);
    draw_bevel(context, layout.increment, color_face,
               explorer_window->scroll_capture !=
                   DESKTOP_EXPLORER_SCROLL_INCREMENT);
    draw_bevel(context, layout.thumb, color_face,
               explorer_window->scroll_capture !=
                   DESKTOP_EXPLORER_SCROLL_THUMB);
    uint32_t foreground = layout.enabled ? color_text : color_shadow;
    int32_t decrement_x = layout.decrement.x +
        (int32_t)((layout.decrement.width > display->font_width
            ? layout.decrement.width - display->font_width : 0U) / 2U);
    int32_t decrement_y = layout.decrement.y +
        (int32_t)((layout.decrement.height > display->font_height
            ? layout.decrement.height - display->font_height : 0U) / 2U);
    int32_t increment_x = layout.increment.x +
        (int32_t)((layout.increment.width > display->font_width
            ? layout.increment.width - display->font_width : 0U) / 2U);
    int32_t increment_y = layout.increment.y +
        (int32_t)((layout.increment.height > display->font_height
            ? layout.increment.height - display->font_height : 0U) / 2U);
    draw_text_clipped(context, decrement_x, decrement_y, "^",
                      layout.decrement.width, foreground, color_face);
    draw_text_clipped(context, increment_x, increment_y, "v",
                      layout.increment.width, foreground, color_face);
}

static uint32_t explorer_navigation_enabled(
    const desktop_explorer_window_t *window, uint32_t action) {
    if (window == 0 || !window->active) return 0U;
    if (action == DESKTOP_EXPLORER_NAVIGATION_BACK)
        return desktop_explorer_can_back(window);
    if (action == DESKTOP_EXPLORER_NAVIGATION_FORWARD)
        return desktop_explorer_can_forward(window);
    if (action == DESKTOP_EXPLORER_NAVIGATION_UP)
        return desktop_explorer_can_up(window);
    if (action == DESKTOP_EXPLORER_NAVIGATION_REFRESH) return 1U;
    return action == DESKTOP_EXPLORER_NAVIGATION_VIEW &&
        window->view < DESKTOP_EXPLORER_VIEW_COUNT;
}

static void render_explorer_navigation_button(
    const desktop_render_context_t *context,
    const desktop_explorer_window_t *window, uint32_t window_index,
    desktop_rect_t button, uint32_t action, const char *label) {
    if (button.width == 0U || button.height == 0U) return;
    uint32_t enabled = explorer_navigation_enabled(window, action);
    uint32_t pressed = enabled &&
        explorer_navigation_pressed_window == window_index &&
        explorer_navigation_pressed_action == action;
    draw_bevel(context, button, color_face, !pressed);
    const x86os_display_info_t *display = context->display;
    int32_t y = button.y + (int32_t)((button.height > display->font_height
        ? button.height - display->font_height : 0U) / 2U);
    draw_text_clipped(
        context, button.x + 4, y, label,
        button.width > 8U ? button.width - 8U : 1U,
        enabled ? color_text : color_shadow, color_face);
}

static uint32_t unsigned_decimal(uint32_t value, char output[11]) {
    char reverse[10];
    uint32_t count = 0U;
    do {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(reverse));
    for (uint32_t index = 0U; index < count; ++index)
        output[index] = reverse[count - index - 1U];
    output[count] = '\0';
    return count;
}

static void render_explorer_chrome(
    const desktop_render_context_t *context,
    const desktop_wm_t *manager,
    const desktop_explorer_t *explorer, uint32_t window_index) {
    const desktop_explorer_window_t *window =
        &explorer->windows[window_index];
    desktop_explorer_chrome_t chrome = desktop_explorer_chrome(
        manager, explorer, window_index);
    fill_rect_clipped(context, chrome.toolbar, color_face);
    render_explorer_navigation_button(
        context, window, window_index, chrome.back,
        DESKTOP_EXPLORER_NAVIGATION_BACK, "< Zurueck");
    render_explorer_navigation_button(
        context, window, window_index, chrome.forward,
        DESKTOP_EXPLORER_NAVIGATION_FORWARD, ">");
    render_explorer_navigation_button(
        context, window, window_index, chrome.up,
        DESKTOP_EXPLORER_NAVIGATION_UP, "^");
    render_explorer_navigation_button(
        context, window, window_index, chrome.refresh,
        DESKTOP_EXPLORER_NAVIGATION_REFRESH, "Aktual.");
    render_explorer_navigation_button(
        context, window, window_index, chrome.view,
        DESKTOP_EXPLORER_NAVIGATION_VIEW,
        window->view == DESKTOP_EXPLORER_VIEW_DETAILS
            ? "Symbole" : "Details");

    if (chrome.address.width != 0U && chrome.address.height != 0U) {
        fill_rect_clipped(context, chrome.address, color_face);
        desktop_rect_t field = chrome.address;
        if (field.width > 8U) {
            field.x += 4;
            field.width -= 8U;
        }
        if (field.height > 4U) {
            field.y += 2;
            field.height -= 4U;
        }
        draw_bevel(context, field, color_client, 0U);
        const x86os_display_info_t *display = context->display;
        int32_t text_y = field.y +
            (int32_t)((field.height > display->font_height
                ? field.height - display->font_height : 0U) / 2U);
        uint32_t label_width = display->font_width * 9U;
        draw_text_clipped(
            context, field.x + 4, text_y, "Adresse:", label_width,
            color_text, color_client);
        if (field.width > label_width + 8U)
            draw_text_clipped(
                context, field.x + 4 + (int32_t)label_width, text_y,
                window->path, field.width - label_width - 8U,
                color_text, color_client);
    }

    if (chrome.status.width != 0U && chrome.status.height != 0U) {
        draw_bevel(context, chrome.status, color_face, 0U);
        const x86os_display_info_t *display = context->display;
        int32_t text_y = chrome.status.y +
            (int32_t)((chrome.status.height > display->font_height
                ? chrome.status.height - display->font_height : 0U) / 2U);
        char number[11];
        uint32_t digits = unsigned_decimal(window->entry_count, number);
        draw_text_clipped(
            context, chrome.status.x + 4, text_y, number,
            digits * display->font_width, color_text, color_face);
        int32_t suffix_x = chrome.status.x + 4 +
            (int32_t)(digits * display->font_width);
        const char *suffix = window->truncated ? "+ Objekte" : " Objekte";
        uint32_t left_width = chrome.status.width / 3U;
        draw_text_clipped(
            context, suffix_x, text_y, suffix,
            left_width > digits * display->font_width
                ? left_width - digits * display->font_width : 1U,
            color_text, color_face);
        if (window->selected < window->entry_count &&
            chrome.status.width > left_width + 8U)
            draw_text_clipped(
                context, chrome.status.x + (int32_t)left_width, text_y,
                window->entries[window->selected].name,
                chrome.status.width - left_width - 4U,
                color_text, color_face);
    }
}

static void draw_shortcut_icon_fallback(
    const desktop_render_context_t *context, desktop_rect_t symbol) {
    draw_file_icon_fallback(
        context, symbol, DESKTOP_EXPLORER_ICON_PROGRAM);
    if (symbol.width < 18U || symbol.height < 18U) return;
    desktop_rect_t badge = {
        symbol.x, symbol.y + (int32_t)symbol.height - 13,
        14U, 13U
    };
    draw_bevel(context, badge, color_light, 1U);
    fill_rect_clipped(
        context,
        (desktop_rect_t){badge.x + 3, badge.y + 6, 7U, 2U},
        color_active);
    fill_rect_clipped(
        context,
        (desktop_rect_t){badge.x + 3, badge.y + 4, 2U, 6U},
        color_active);
    fill_rect_clipped(
        context,
        (desktop_rect_t){badge.x + 5, badge.y + 3, 2U, 2U},
        color_active);
    fill_rect_clipped(
        context,
        (desktop_rect_t){badge.x + 5, badge.y + 9, 2U, 2U},
        color_active);
}

static void render_surface_paint_list(
    const desktop_render_context_t *context, desktop_rect_t client,
    const desktop_surface_paint_command_t *commands, uint32_t count) {
    if (context == 0 || commands == 0) return;
    for (uint32_t index = 0U; index < count; ++index) {
        const desktop_surface_paint_command_t *command = &commands[index];
        desktop_rect_t bounds = {
            client.x + command->rect.x,
            client.y + command->rect.y,
            command->rect.width, command->rect.height,
        };
        if (command->type == DESKTOP_SURFACE_PAINT_FILL)
            fill_rect_clipped(context, bounds, command->foreground);
        else if (command->type == DESKTOP_SURFACE_PAINT_TEXT)
            draw_text_clipped(
                context, bounds.x, bounds.y, command->text,
                bounds.width, command->foreground, command->background);
        else if (command->type == DESKTOP_SURFACE_PAINT_FONT_TEXT)
            draw_font_text_clipped(
                context, bounds.x, bounds.y, command->text,
                command->text_length, bounds.width,
                command->foreground, command->background,
                command->font_family, command->font_height);
    }
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
    desktop_rect_t explorer_client = desktop_explorer_content_rect(
        manager, explorer, window_index);
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
        desktop_rect_t committed_bounds = {
            client.x, client.y,
            client.width < surface->width ? client.width : surface->width,
            client.height < surface->height ? client.height : surface->height,
        };
        desktop_rect_t buffer_clip = {0, 0, 0U, 0U};
        if (intersect_rects(
                committed_bounds, context->clip, &buffer_clip))
            (void)x86os_display_surface_buffer_draw(
                (int)surface->owner.pid,
                surface->owner.process_generation,
                surface->committed_buffer,
                surface->committed_buffer_generation,
                (uint32_t)(buffer_clip.x - client.x),
                (uint32_t)(buffer_clip.y - client.y),
                buffer_clip.x, buffer_clip.y,
                buffer_clip.width, buffer_clip.height);
    }
    if (surface_visible) {
        render_surface_paint_list(
            &surface_context, client, surface->committed_paint,
            surface->committed_paint_count);
        render_surface_paint_list(
            &surface_context, client, surface->committed_dynamic_paint,
            surface->committed_dynamic_paint_count);
        render_surface_paint_list(
            &surface_context, client, surface->committed_overlay_paint,
            surface->committed_overlay_paint_count);
        render_surface_paint_list(
            &surface_context, client, surface->committed_hover_paint,
            surface->committed_hover_paint_count);
    }
    if (explorer_window != 0)
        render_explorer_chrome(
            context, manager, explorer, window_index);
    if (explorer_window != 0)
        render_explorer_details_header(
            context, explorer_window, explorer_client);
    if (explorer_window != 0)
        for (uint32_t entry = 0U; entry < explorer_window->entry_count; ++entry)
            render_explorer_entry(
                context, explorer_window, explorer_client, entry);
    if (explorer_window != 0)
        render_explorer_scrollbar(context, explorer_window, explorer_client);
    if (explorer_window != 0 && explorer_window->truncated &&
        explorer_client.height > display->font_height + 4U) {
        desktop_explorer_layout_t explorer_layout =
            desktop_explorer_layout(explorer_window, explorer_client);
        draw_text_clipped(
            context, explorer_layout.viewport.x + 4,
            explorer_layout.viewport.y +
                (int32_t)explorer_layout.viewport.height -
                (int32_t)display->font_height - 3,
            "Weitere Eintraege nicht geladen",
            explorer_layout.viewport.width > 8U
                ? explorer_layout.viewport.width - 8U : 1U,
            color_shadow, color_client);
    }
    if (explorer_window != 0 &&
        desktop_window_is_trash(explorer, window_index)) {
        desktop_rect_t action = desktop_trash_restore_rect(
            manager, explorer, window_index);
        uint32_t enabled = explorer_window->selected <
            explorer_window->entry_count;
        uint32_t pressed = enabled &&
            trash_restore_pressed_window == window_index;
        draw_bevel(context, action, color_face, !pressed);
        const char *label = "Wiederherstellen";
        size_t label_bytes = 0U;
        size_t label_scalars = 0U;
        (void)unicode_text_measure(
            label, 32U, &label_bytes, &label_scalars);
        uint32_t label_width = (uint32_t)label_scalars * display->font_width;
        int32_t label_x = action.x + (int32_t)((action.width > label_width
            ? action.width - label_width : 0U) / 2U);
        int32_t label_y = action.y + (int32_t)((action.height >
            display->font_height ? action.height - display->font_height
                                 : 0U) / 2U);
        draw_text_clipped(
            context, label_x, label_y, label, action.width,
            enabled ? color_text : color_shadow, color_face);
    }
    render_resize_grip(context, window);
}

static const char *desktop_task_title(
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces, uint32_t window_index) {
    if (explorer != 0 && window_index < DESKTOP_EXPLORER_WINDOW_CAPACITY &&
        explorer->windows[window_index].active) {
        if (explorer->windows[window_index].path[0] == '/' &&
            explorer->windows[window_index].path[1] == '\0') return "Computer";
        return explorer->windows[window_index].path;
    }
    if (surfaces != 0)
        for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index)
            if (surfaces->slots[index].active &&
                surfaces->slots[index].window_index == window_index)
                return surfaces->slots[index].title;
    return "Fenster";
}

static void render_taskbar(
    const desktop_render_context_t *context, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces,
    const desktop_ui_state_t *ui) {
    const x86os_display_info_t *display = context->display;
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    desktop_rect_t taskbar = desktop_taskbar_rect(display);
    draw_bevel(context, taskbar, color_face, 1U);

    reist_gui_rect_t gui_start;
    if (reist_gui_menu_title_rect(
            &desktop_menu_model, &layout, DESKTOP_MENU_START,
            &gui_start) == 0) {
        desktop_rect_t start = desktop_rect_from_gui(gui_start);
        uint32_t active = ui != 0 &&
            ui->menu.open_menu == DESKTOP_MENU_START;
        draw_bevel(context, start, color_face, active ? 0U : 1U);
        int32_t logo_x = start.x + 5;
        int32_t logo_y = start.y +
            (int32_t)((start.height > 10U ? start.height - 10U : 0U) / 2U);
        fill_rect_clipped(context,
            (desktop_rect_t){logo_x, logo_y, 4U, 4U}, 0x0000479DU);
        fill_rect_clipped(context,
            (desktop_rect_t){logo_x + 5, logo_y, 4U, 4U}, 0x00806020U);
        fill_rect_clipped(context,
            (desktop_rect_t){logo_x, logo_y + 5, 4U, 4U}, 0x00C09000U);
        fill_rect_clipped(context,
            (desktop_rect_t){logo_x + 5, logo_y + 5, 4U, 4U}, 0x00800080U);
        uint32_t text_y = start.height > display->font_height
            ? (start.height - display->font_height) / 2U : 0U;
        draw_text_clipped(
            context, start.x + 18, start.y + (int32_t)text_y, "Start",
            start.width > 22U ? start.width - 22U : 1U,
            color_text, color_face);
    }

    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        desktop_rect_t button = desktop_task_button_rect(
            display, manager, index);
        if (button.width == 0U) continue;
        uint32_t active = manager->keyboard_focus == (int32_t)index ||
            (ui != 0 && ui->taskbar_capture_slot == index);
        uint32_t background = active ? color_active : color_face;
        uint32_t foreground = active ? color_title_text : color_text;
        draw_bevel(context, button, background, active ? 0U : 1U);
        uint32_t text_y = button.height > display->font_height
            ? (button.height - display->font_height) / 2U : 0U;
        draw_text_clipped(
            context, button.x + 5, button.y + (int32_t)text_y,
            desktop_task_title(explorer, surfaces, index),
            button.width > 10U ? button.width - 10U : 1U,
            foreground, background);
    }

    desktop_rect_t clock = desktop_clock_rect(display);
    draw_bevel(context, clock, color_face, 0U);
    uint32_t clock_y = clock.height > display->font_height
        ? (clock.height - display->font_height) / 2U : 0U;
    draw_text_clipped(
        context, clock.x + 6, clock.y + (int32_t)clock_y,
        desktop_clock.text, clock.width > 12U ? clock.width - 12U : 1U,
        color_text, color_face);
}

static void render_menu_item_model(
    const desktop_render_context_t *context,
    const reist_gui_menu_model_t *model,
    const reist_gui_menu_layout_t *layout,
    const reist_gui_menu_state_t *state, uint32_t menu_index,
    uint32_t item_index) {
    if (context == 0 || model == 0 || layout == 0 || state == 0 ||
        menu_index >= model->menu_count ||
        item_index >= model->menus[menu_index].item_count)
        return;
    reist_gui_rect_t gui_item;
    if (reist_gui_menu_item_rect(
            model, layout, menu_index, item_index, &gui_item) != 0)
        return;
    const x86os_display_info_t *display = context->display;
    desktop_rect_t item = desktop_rect_from_gui(gui_item);
    const reist_gui_menu_item_t *model_item =
        &model->menus[menu_index].items[item_index];
    uint32_t disabled =
        (model_item->flags & REIST_GUI_MENU_ITEM_DISABLED) != 0U;
    uint32_t hot = !disabled && state->hot_item == item_index;
    uint32_t pressed = hot &&
        state->capture_kind == REIST_GUI_MENU_CAPTURE_ITEM &&
        state->capture_menu == menu_index &&
        state->capture_item == item_index;
    uint32_t fast_feedback = desktop_low_latency_menu_feedback != 0U &&
        model == &desktop_menu_model && menu_index == DESKTOP_MENU_START &&
        (hot || pressed);
    uint32_t background = fast_feedback ? color_face :
        (hot ? color_active : color_face);
    uint32_t foreground = disabled
        ? color_shadow : (hot && !fast_feedback ? color_title_text : color_text);
    fill_rect_clipped(context, item, background);
    if (fast_feedback) {
        uint32_t feedback_width = item.width > DESKTOP_MENU_FAST_FEEDBACK_WIDTH
            ? DESKTOP_MENU_FAST_FEEDBACK_WIDTH : item.width;
        fill_rect_clipped(
            context,
            (desktop_rect_t){item.x, item.y, feedback_width, item.height},
            pressed ? color_dark : color_active);
    } else if (pressed) {
        draw_bevel(context, item, background, 0U);
    }
    uint32_t text_y = item.height > display->font_height
        ? (item.height - display->font_height) / 2U : 0U;
    int32_t marker_x = item.x + (int32_t)layout->item_padding_x;
    int32_t label_x = marker_x + (int32_t)(display->font_width * 2U);
    uint32_t used = layout->item_padding_x * 2U +
                    display->font_width * 2U;
    draw_text_clipped(
        context, label_x, item.y + (int32_t)text_y,
        model_item->label,
        item.width > used ? item.width - used : 1U,
        foreground, background);
}

static void render_menu_popup_model(
    const desktop_render_context_t *context,
    const reist_gui_menu_model_t *model,
    const reist_gui_menu_layout_t *layout,
    const reist_gui_menu_state_t *state) {
    if (model == 0 || layout == 0 || state == 0 ||
        state->open_menu == REIST_GUI_MENU_NO_INDEX ||
        state->open_menu >= model->menu_count) return;
    uint32_t menu_index = state->open_menu;
    reist_gui_rect_t gui_popup;
    if (reist_gui_menu_popup_rect(
            model, layout, menu_index, &gui_popup) != 0)
        return;
    desktop_rect_t popup = desktop_rect_from_gui(gui_popup);
    /* Popup and shadow are composed after all ordinary windows. */
    fill_rect_clipped(
        context,
        (desktop_rect_t){popup.x + 4, popup.y + 4,
                         popup.width, popup.height},
        color_dark);
    draw_bevel(context, popup, color_face, 1U);

    const reist_gui_menu_t *menu_model = &model->menus[menu_index];
    for (uint32_t item_index = 0U;
         item_index < menu_model->item_count; ++item_index)
        render_menu_item_model(
            context, model, layout, state, menu_index, item_index);
}

static void render_menu_popup(const desktop_render_context_t *context,
                              const desktop_ui_state_t *ui) {
    if (ui == 0) return;
    reist_gui_menu_layout_t layout = desktop_menu_layout(context->display);
    render_menu_popup_model(
        context, &desktop_menu_model, &layout, &ui->menu);
}

static void render_trash_context_popup(
    const desktop_render_context_t *context,
    const desktop_ui_state_t *ui) {
    if (ui == 0) return;
    reist_gui_menu_layout_t layout =
        trash_context_layout(ui, context->display);
    render_menu_popup_model(
        context, trash_context_model(ui), &layout, &ui->trash_menu);
}

static void render_explorer_context_popup(
    const desktop_render_context_t *context,
    const desktop_ui_state_t *ui) {
    if (ui == 0) return;
    reist_gui_menu_layout_t layout =
        explorer_context_layout(ui, context->display);
    render_menu_popup_model(
        context, &explorer_context_menu_model, &layout,
        &ui->explorer_menu);
}

static void render_shortcut_context_popup(
    const desktop_render_context_t *context,
    const desktop_ui_state_t *ui) {
    if (ui == 0) return;
    reist_gui_menu_layout_t layout =
        shortcut_context_layout(ui, context->display);
    render_menu_popup_model(
        context, &shortcut_context_menu_model, &layout,
        &ui->shortcut_menu);
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
        size_t label_bytes = 0U;
        size_t label_scalars = 0U;
        (void)unicode_text_measure(
            model->buttons[index].label, REIST_GUI_DIALOG_LABEL_LIMIT,
            &label_bytes, &label_scalars);
        uint64_t measured = (uint64_t)label_scalars * display->font_width;
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

#define DESKTOP_VISIBLE_REGION_CAPACITY 128U

typedef struct desktop_visible_region {
    desktop_rect_t rects[DESKTOP_VISIBLE_REGION_CAPACITY];
    uint32_t count;
} desktop_visible_region_t;

static uint32_t visible_region_append(desktop_visible_region_t *region,
                                      desktop_rect_t rect) {
    if (rect.width == 0U || rect.height == 0U) return 1U;
    if (region == 0 || region->count >= DESKTOP_VISIBLE_REGION_CAPACITY)
        return 0U;
    region->rects[region->count++] = rect;
    return 1U;
}

/* Subtract one opaque rectangle without allocation.  On capacity exhaustion
 * the caller discards the partial result and renders the original clip, so
 * optimization pressure can never create missing pixels. */
static uint32_t visible_region_subtract(desktop_visible_region_t *region,
                                        desktop_rect_t opaque) {
    if (region == 0 || opaque.width == 0U || opaque.height == 0U) return 1U;
    desktop_visible_region_t next;
    next.count = 0U;
    for (uint32_t index = 0U; index < region->count; ++index) {
        desktop_rect_t source = region->rects[index];
        desktop_rect_t overlap;
        if (!intersect_rects(source, opaque, &overlap)) {
            if (!visible_region_append(&next, source)) return 0U;
            continue;
        }
        int32_t source_right = source.x + (int32_t)source.width;
        int32_t source_bottom = source.y + (int32_t)source.height;
        int32_t overlap_right = overlap.x + (int32_t)overlap.width;
        int32_t overlap_bottom = overlap.y + (int32_t)overlap.height;
        if (!visible_region_append(&next, (desktop_rect_t){
                source.x, source.y, source.width,
                (uint32_t)(overlap.y - source.y)}) ||
            !visible_region_append(&next, (desktop_rect_t){
                source.x, overlap_bottom, source.width,
                (uint32_t)(source_bottom - overlap_bottom)}) ||
            !visible_region_append(&next, (desktop_rect_t){
                source.x, overlap.y,
                (uint32_t)(overlap.x - source.x), overlap.height}) ||
            !visible_region_append(&next, (desktop_rect_t){
                overlap_right, overlap.y,
                (uint32_t)(source_right - overlap_right), overlap.height}))
            return 0U;
    }
    region->count = next.count;
    for (uint32_t index = 0U; index < next.count; ++index)
        region->rects[index] = next.rects[index];
    return 1U;
}

static desktop_rect_t window_visual_bounds(const desktop_wm_t *manager,
                                           uint32_t window_index) {
    return desktop_wm_window_bounds(manager, window_index);
}

static uint32_t visible_region_subtract_popup(
        desktop_visible_region_t *region,
        const reist_gui_menu_model_t *model,
        const reist_gui_menu_layout_t *layout,
        const reist_gui_menu_state_t *state) {
    if (model == 0 || layout == 0 || state == 0 ||
        state->open_menu == REIST_GUI_MENU_NO_INDEX ||
        state->open_menu >= model->menu_count) return 1U;
    reist_gui_rect_t popup;
    if (reist_gui_menu_popup_rect(
            model, layout, state->open_menu, &popup) != 0) return 1U;
    desktop_rect_t opaque = desktop_rect_from_gui(popup);
    if (opaque.width <= UINT32_MAX - 4U) opaque.width += 4U;
    if (opaque.height <= UINT32_MAX - 4U) opaque.height += 4U;
    return visible_region_subtract(region, opaque);
}

static uint32_t visible_region_subtract_system_ui(
        desktop_visible_region_t *region,
        const x86os_display_info_t *display,
        const desktop_ui_state_t *ui) {
    if (region == 0 || display == 0 || ui == 0) return 1U;
    if (!visible_region_subtract(region, desktop_taskbar_rect(display)))
        return 0U;
    reist_gui_menu_layout_t desktop_layout = desktop_menu_layout(display);
    if (!visible_region_subtract_popup(
            region, &desktop_menu_model, &desktop_layout, &ui->menu))
        return 0U;
    reist_gui_menu_layout_t trash_layout = trash_context_layout(ui, display);
    if (!visible_region_subtract_popup(
            region, trash_context_model(ui), &trash_layout, &ui->trash_menu))
        return 0U;
    reist_gui_menu_layout_t explorer_layout =
        explorer_context_layout(ui, display);
    if (!visible_region_subtract_popup(
            region, &explorer_context_menu_model, &explorer_layout,
            &ui->explorer_menu)) return 0U;
    reist_gui_menu_layout_t shortcut_layout =
        shortcut_context_layout(ui, display);
    if (!visible_region_subtract_popup(
            region, &shortcut_context_menu_model, &shortcut_layout,
            &ui->shortcut_menu)) return 0U;
    if (!ui->dialog.visible) return 1U;
    const reist_gui_dialog_model_t *model =
        desktop_dialog_model(ui, ui->dialog_kind);
    reist_gui_dialog_layout_t layout = desktop_dialog_layout(display);
    reist_gui_rect_t dialog;
    if (model == 0 || reist_gui_dialog_frame_rect(
            model, &layout, &ui->dialog, &dialog) != 0) return 1U;
    desktop_rect_t opaque = desktop_rect_from_gui(dialog);
    if (opaque.width <= UINT32_MAX - 4U) opaque.width += 4U;
    if (opaque.height <= UINT32_MAX - 4U) opaque.height += 4U;
    return visible_region_subtract(region, opaque);
}

static void render_desktop_background(
        const desktop_render_context_t *context,
        const desktop_explorer_t *explorer) {
    fill_rect_clipped(
        context,
        (desktop_rect_t){0, 0, context->display->width,
                         context->display->height},
        color_desktop);
    uint32_t icon_count = DESKTOP_BUILTIN_ICON_COUNT +
        desktop_dynamic_icon_count(explorer);
    for (uint32_t index = 0U; index < icon_count; ++index)
        render_icon(context, explorer, index);
}

static void render_desktop_clip(const desktop_render_context_t *context,
                                const desktop_wm_t *manager,
                                const desktop_explorer_t *explorer,
                                const desktop_surface_manager_t *surfaces,
                                const desktop_ui_state_t *ui) {
    const x86os_display_info_t *display = context->display;

    desktop_visible_region_t background;
    background.count = 0U;
    (void)visible_region_append(&background, context->clip);
    uint32_t background_culled = visible_region_subtract_system_ui(
        &background, display, ui);
    for (uint32_t position = 0U;
         background_culled && position < DESKTOP_WM_CAPACITY; ++position) {
        uint32_t window_index = manager->z_order[position];
        if (window_index >= DESKTOP_WM_CAPACITY ||
            !manager->windows[window_index].visible ||
            (context->omitted_kind == DESKTOP_MOVE_CACHE_WINDOW &&
             context->omitted_window == window_index))
            continue;
        background_culled = visible_region_subtract(
            &background, window_visual_bounds(manager, window_index));
    }
    if (!background_culled) {
        render_desktop_background(context, explorer);
    } else {
        for (uint32_t visible = 0U; visible < background.count; ++visible) {
            desktop_render_context_t clipped = *context;
            clipped.clip = background.rects[visible];
            render_desktop_background(&clipped, explorer);
        }
    }

    for (uint32_t position = 0U; position < DESKTOP_WM_CAPACITY; ++position) {
        uint32_t window_index = manager->z_order[position];
        if (window_index >= DESKTOP_WM_CAPACITY ||
            (context->omitted_kind == DESKTOP_MOVE_CACHE_WINDOW &&
             context->omitted_window == window_index))
            continue;
        desktop_visible_region_t visible;
        visible.count = 0U;
        (void)visible_region_append(&visible, context->clip);
        uint32_t culled = visible_region_subtract_system_ui(
            &visible, display, ui);
        for (uint32_t higher = position + 1U;
             culled && higher < DESKTOP_WM_CAPACITY; ++higher) {
            uint32_t higher_window = manager->z_order[higher];
            if (higher_window >= DESKTOP_WM_CAPACITY ||
                !manager->windows[higher_window].visible ||
                (context->omitted_kind == DESKTOP_MOVE_CACHE_WINDOW &&
                 context->omitted_window == higher_window))
                continue;
            culled = visible_region_subtract(
                &visible, window_visual_bounds(manager, higher_window));
        }
        if (!culled) {
            render_window(
                context, manager, explorer, surfaces, window_index);
            continue;
        }
        for (uint32_t region = 0U; region < visible.count; ++region) {
            desktop_render_context_t clipped = *context;
            clipped.clip = visible.rects[region];
            render_window(
                &clipped, manager, explorer, surfaces, window_index);
        }
    }

    render_taskbar(context, manager, explorer, surfaces, ui);
    render_menu_popup(context, ui);
    render_trash_context_popup(context, ui);
    render_explorer_context_popup(context, ui);
    render_shortcut_context_popup(context, ui);
    if (context->omitted_kind != DESKTOP_MOVE_CACHE_DIALOG)
        render_system_dialog(context, ui);
    render_drag_feedback(context);
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

static uint32_t rect_contains(desktop_rect_t outer, desktop_rect_t inner) {
    int64_t outer_right = (int64_t)outer.x + outer.width;
    int64_t outer_bottom = (int64_t)outer.y + outer.height;
    int64_t inner_right = (int64_t)inner.x + inner.width;
    int64_t inner_bottom = (int64_t)inner.y + inner.height;
    return inner.width != 0U && inner.height != 0U &&
        inner.x >= outer.x && inner.y >= outer.y &&
        inner_right <= outer_right && inner_bottom <= outer_bottom;
}

static uint32_t start_menu_damage_bounds(
    const x86os_display_info_t *display, desktop_rect_t *bounds) {
    if (display == 0 || bounds == 0 || display->width == 0U ||
        display->height == 0U) return 0U;
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    reist_gui_rect_t gui_popup;
    reist_gui_rect_t gui_title;
    if (reist_gui_menu_popup_rect(
            &desktop_menu_model, &layout, DESKTOP_MENU_START,
            &gui_popup) != 0 ||
        reist_gui_menu_title_rect(
            &desktop_menu_model, &layout, DESKTOP_MENU_START,
            &gui_title) != 0)
        return 0U;
    int64_t left = gui_popup.x < gui_title.x
        ? gui_popup.x : gui_title.x;
    int64_t top = gui_popup.y < gui_title.y
        ? gui_popup.y : gui_title.y;
    int64_t popup_right = (int64_t)gui_popup.x + gui_popup.width;
    int64_t title_right = (int64_t)gui_title.x + gui_title.width;
    int64_t right = popup_right > title_right ? popup_right : title_right;
    int64_t popup_bottom = (int64_t)gui_popup.y + gui_popup.height;
    int64_t title_bottom = (int64_t)gui_title.y + gui_title.height;
    int64_t bottom = popup_bottom > title_bottom
        ? popup_bottom : title_bottom;
    left -= layout.damage_margin;
    top -= layout.damage_margin;
    right += layout.damage_margin;
    bottom += layout.damage_margin;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > display->width) right = display->width;
    if (bottom > display->height) bottom = display->height;
    if (left >= right || top >= bottom) return 0U;
    *bounds = (desktop_rect_t){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top),
    };
    return 1U;
}

/* Start-menu damage is confined to one fixed popup/title envelope.  The
 * optimized composer below still reconstructs every exposed margin pixel;
 * this classifier therefore remains correct even if another local invalid
 * rectangle merges with menu damage.  Mixed or ambiguous frames fail closed
 * to the complete scene renderer. */
static uint32_t menu_overlay_local_damage(
    const x86os_display_info_t *display, const desktop_ui_state_t *ui,
    const desktop_dirty_region_t *dirty) {
    if (display == 0 || ui == 0 || dirty == 0 || dirty->full ||
        dirty->count == 0U ||
        ui->menu.open_menu != DESKTOP_MENU_START || ui->dialog.visible ||
        ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        desktop_drag.phase != DESKTOP_DRAG_PHASE_IDLE)
        return 0U;
    desktop_rect_t bounds;
    if (!start_menu_damage_bounds(display, &bounds)) return 0U;
    for (uint32_t index = 0U; index < dirty->count; ++index)
        if (!rect_contains(bounds, dirty->rects[index])) return 0U;
    return 1U;
}

static void render_menu_overlay_damage(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces,
    const desktop_ui_state_t *ui,
    const desktop_dirty_region_t *dirty) {
    if (display == 0 || manager == 0 || ui == 0 || dirty == 0) return;
    reist_gui_menu_layout_t layout = desktop_menu_layout(display);
    for (uint32_t index = 0U; index < dirty->count; ++index) {
        desktop_render_context_t context = {
            .display = display,
            .clip = dirty->rects[index],
            .omitted_kind = DESKTOP_MOVE_CACHE_NONE,
            .omitted_window = DESKTOP_WM_NO_TARGET,
        };

        /* A hover transition damages complete item rows on the ordinary
         * renderer, or a narrow item-column feedback strip on VMware's
         * copy-capable path.  Popup borders, shadow, taskbar and lower scene
         * are unchanged and must not consume the compositor's bounded service
         * window. Adjacent old/new rows may already be merged by the damage
         * accumulator, so classify either fixed item-column envelope. */
        reist_gui_rect_t first_gui_item;
        reist_gui_rect_t last_gui_item;
        uint32_t item_count = desktop_menu_model.menus[DESKTOP_MENU_START]
            .item_count;
        if (item_count != 0U &&
            reist_gui_menu_item_rect(
                &desktop_menu_model, &layout, DESKTOP_MENU_START, 0U,
                &first_gui_item) == 0 &&
            reist_gui_menu_item_rect(
                &desktop_menu_model, &layout, DESKTOP_MENU_START,
                item_count - 1U, &last_gui_item) == 0) {
            desktop_rect_t first = desktop_rect_from_gui(first_gui_item);
            desktop_rect_t last = desktop_rect_from_gui(last_gui_item);
            int64_t clip_bottom =
                (int64_t)context.clip.y + context.clip.height;
            int64_t item_bottom = (int64_t)last.y + last.height;
            uint32_t complete_item_column = context.clip.x == first.x &&
                context.clip.width == first.width &&
                context.clip.y >= first.y && clip_bottom <= item_bottom;
            uint32_t feedback_item_column =
                desktop_low_latency_menu_feedback != 0U &&
                context.clip.x == first.x &&
                context.clip.width <= DESKTOP_MENU_FAST_FEEDBACK_WIDTH &&
                context.clip.y >= first.y && clip_bottom <= item_bottom;
            uint32_t item_column = complete_item_column ||
                feedback_item_column;
            if (item_column) {
                for (uint32_t item = 0U; item < item_count; ++item)
                    render_menu_item_model(
                        &context, &desktop_menu_model, &layout, &ui->menu,
                        DESKTOP_MENU_START, item);
                continue;
            }
        }

        /* Opening damage includes the popup shadow margin and Start-button
         * margin. Recompose only those fixed strips which are not hidden by
         * the opaque popup or taskbar; a capacity failure falls back to the
         * original complete clip before any overlay-only shortcut is used. */
        desktop_visible_region_t exposed;
        exposed.count = 0U;
        uint32_t culled = visible_region_append(&exposed, context.clip) &&
            visible_region_subtract(
                &exposed, desktop_taskbar_rect(display)) &&
            visible_region_subtract_popup(
                &exposed, &desktop_menu_model, &layout, &ui->menu);
        if (!culled) {
            render_desktop_clip(
                &context, manager, explorer, surfaces, ui);
            continue;
        }
        for (uint32_t region = 0U; region < exposed.count; ++region) {
            desktop_render_context_t lower = context;
            lower.clip = exposed.rects[region];
            render_desktop_clip(
                &lower, manager, explorer, surfaces, ui);
        }
        render_taskbar(&context, manager, explorer, surfaces, ui);
        render_menu_popup(&context, ui);
    }
}

static void render_desktop_rect(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces,
    const desktop_ui_state_t *ui, desktop_rect_t rect,
    uint32_t omitted_kind, uint32_t omitted_window) {
    if (rect.width == 0U || rect.height == 0U) return;
    desktop_dirty_region_t dirty;
    desktop_dirty_initialize(&dirty, display->width, display->height);
    desktop_dirty_add(&dirty, rect);
    render_dirty_regions(
        display, manager, explorer, surfaces, ui, &dirty,
        omitted_kind, omitted_window);
}

/* Render outer minus excluded as at most four disjoint strips.  Keeping the
 * strips separate avoids merging them back into a near-full window redraw. */
static void render_desktop_rect_difference(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces,
    const desktop_ui_state_t *ui, desktop_rect_t outer,
    desktop_rect_t excluded, uint32_t omitted_kind,
    uint32_t omitted_window) {
    desktop_rect_t overlap;
    if (!intersect_rects(outer, excluded, &overlap)) {
        render_desktop_rect(
            display, manager, explorer, surfaces, ui, outer,
            omitted_kind, omitted_window);
        return;
    }
    int32_t outer_right = outer.x + (int32_t)outer.width;
    int32_t outer_bottom = outer.y + (int32_t)outer.height;
    int32_t overlap_right = overlap.x + (int32_t)overlap.width;
    int32_t overlap_bottom = overlap.y + (int32_t)overlap.height;
    render_desktop_rect(
        display, manager, explorer, surfaces, ui,
        (desktop_rect_t){outer.x, outer.y, outer.width,
                         (uint32_t)(overlap.y - outer.y)},
        omitted_kind, omitted_window);
    render_desktop_rect(
        display, manager, explorer, surfaces, ui,
        (desktop_rect_t){outer.x, overlap_bottom, outer.width,
                         (uint32_t)(outer_bottom - overlap_bottom)},
        omitted_kind, omitted_window);
    render_desktop_rect(
        display, manager, explorer, surfaces, ui,
        (desktop_rect_t){outer.x, overlap.y,
                         (uint32_t)(overlap.x - outer.x), overlap.height},
        omitted_kind, omitted_window);
    render_desktop_rect(
        display, manager, explorer, surfaces, ui,
        (desktop_rect_t){overlap_right, overlap.y,
                         (uint32_t)(outer_right - overlap_right),
                         overlap.height},
        omitted_kind, omitted_window);
}

static uint32_t render_desktop_frame(const x86os_display_info_t *display,
                                     const desktop_wm_t *manager,
                                     const desktop_explorer_t *explorer,
                                     const desktop_surface_manager_t *surfaces,
                                     const desktop_ui_state_t *ui,
                                     const desktop_dirty_region_t *dirty) {
    if (dirty == 0 || dirty->count == 0U) return 0U;
    uint32_t menu_local = menu_overlay_local_damage(display, ui, dirty);
    uint32_t serial = 0U;
    int begin = x86os_display_frame_begin(&serial);
    if (begin != 0) {
        /* Oversized/direct framebuffers retain the compatible immediate path. */
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return DESKTOP_RENDER_FALLBACK;
    }
    if (menu_local)
        render_menu_overlay_damage(
            display, manager, explorer, surfaces, ui, dirty);
    else
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
    int commit = x86os_display_frame_commit(serial);
    if (commit != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return DESKTOP_RENDER_FALLBACK;
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
        ((move->kind != DESKTOP_MOVE_CACHE_WINDOW &&
          move->kind != DESKTOP_MOVE_CACHE_RESIZE) ||
         move->window_index >= DESKTOP_WM_CAPACITY))
        return render_desktop_frame(display, manager, explorer, surfaces, ui, dirty);

    uint32_t outcome = 0U;
    uint32_t serial = 0U;
    int begin = x86os_display_frame_begin(&serial);
    if (begin != 0) {
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return DESKTOP_RENDER_FALLBACK |
            DESKTOP_RENDER_ACCELERATION_FALLBACK;
    }
    int staged = x86os_display_frame_stage_blit(
        serial, (uint32_t)move->source.x, (uint32_t)move->source.y,
        (uint32_t)move->destination.x,
        (uint32_t)move->destination.y,
        move->source.width, move->source.height);
    if (staged != 0) {
        outcome |= DESKTOP_RENDER_ACCELERATION_FALLBACK;
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
    } else {
        /* VMware's asynchronous RECT_COPY is safe for equal-sized window
         * moves, but resize changes the surrounding geometry concurrently.
         * Keep resize on the staged shadow copy: commit publishes the copied
         * destination atomically without trusting an in-flight device copy. */
        int copy_status = move->kind == DESKTOP_MOVE_CACHE_RESIZE
            ? -95
            : desktop_svga2d_rect_copy(
                (uint32_t)move->source.x, (uint32_t)move->source.y,
                (uint32_t)move->destination.x,
                (uint32_t)move->destination.y,
                move->source.width, move->source.height);
        if (copy_status == 0) {
            desktop_svga2d_last_mark_status =
                x86os_display_frame_mark_accelerated(serial);
            if (desktop_svga2d_last_mark_status == 0)
                outcome |= DESKTOP_RENDER_ACCELERATED;
            else
                outcome |= DESKTOP_RENDER_ACCELERATION_FALLBACK;
        } else {
            outcome |= DESKTOP_RENDER_ACCELERATION_FALLBACK;
        }
        uint32_t omitted_kind = move->kind == DESKTOP_MOVE_CACHE_DIALOG
            ? DESKTOP_MOVE_CACHE_DIALOG : DESKTOP_MOVE_CACHE_WINDOW;
        render_desktop_rect_difference(
            display, manager, explorer, surfaces, ui,
            move->cleanup, move->destination,
            omitted_kind, move->window_index);
        if (move->kind == DESKTOP_MOVE_CACHE_RESIZE)
            render_desktop_rect_difference(
                display, manager, explorer, surfaces, ui,
                move->redraw, move->destination,
                DESKTOP_MOVE_CACHE_NONE, DESKTOP_WM_NO_TARGET);
    }
    if (x86os_display_frame_commit(serial) != 0) {
        (void)x86os_display_frame_cancel(serial);
        render_desktop(display, manager, explorer, surfaces, ui, dirty);
        return outcome | DESKTOP_RENDER_FALLBACK;
    }
    return outcome;
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
    if ((fallback & DESKTOP_RENDER_FALLBACK) != 0U)
        saturating_increment(&metrics->fallback_frames);
    if ((fallback & DESKTOP_RENDER_ACCELERATED) != 0U)
        saturating_increment(&metrics->accelerated_frames);
    if ((fallback & DESKTOP_RENDER_ACCELERATION_FALLBACK) != 0U)
        saturating_increment(&metrics->acceleration_fallbacks);

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

static uint32_t render_desktop_measured(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    const desktop_explorer_t *explorer,
    const desktop_surface_manager_t *surfaces,
    const desktop_ui_state_t *ui,
    const desktop_dirty_region_t *dirty,
    const desktop_move_cache_t *move_cache,
    uint32_t drag, uint32_t resize, desktop_render_metrics_t *metrics) {
    if (dirty == 0 || dirty->count == 0U) return 0U;
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
    return fallback;
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

    /* Consume a complete ANSI CSI sequence. A bare Escape is a local cancel. */
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

static uint32_t surface_window_is_live_resizing(
    const desktop_wm_t *manager, uint32_t window_index) {
    return manager != 0 &&
        manager->capture_kind == DESKTOP_WM_CAPTURE_RESIZE &&
        manager->capture_window == (int32_t)window_index;
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
    uint32_t pressed, uint32_t allow_outside, int *enqueue_status) {
    if (enqueue_status != 0) *enqueue_status = 0;
    if (manager == 0 || surfaces == 0 || window_index < 0 ||
        window_index >= (int32_t)DESKTOP_WM_CAPACITY) {
        if (enqueue_status != 0) *enqueue_status = -22;
        return 0U;
    }
    desktop_surface_slot_t *surface = surface_for_window(
        surfaces, (uint32_t)window_index);
    if (surface == 0) {
        if (enqueue_status != 0) *enqueue_status = -2;
        return 0U;
    }
    desktop_rect_t client = desktop_window_client_rect(
        manager, (uint32_t)window_index);
    if (!allow_outside &&
        (pointer_x < client.x || pointer_y < client.y ||
        pointer_x >= client.x + (int32_t)client.width ||
        pointer_y >= client.y + (int32_t)client.height))
        {
            if (enqueue_status != 0) *enqueue_status = -34;
            return 0U;
        }
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
    int status = desktop_surface_input_enqueue(
        surfaces, surface->owner, surface->handle, &event);
    if (enqueue_status != 0) *enqueue_status = status;
    return status == 0;
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
            window->flags = 0U;
            desktop_dirty_full(dirty);
            if (desktop_shortcut_probe_enabled)
                x86os_puts("DESKTOP_SHORTCUT_CLIENT_CLOSED\n");
        }
    }
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index) {
        desktop_surface_slot_t *surface = &surfaces->slots[index];
        if (!surface->active) continue;
        if (surface->window_index != DESKTOP_SURFACE_NO_SLOT) {
            if (surface->role == REIST_GUI_SURFACE_ROLE_DIALOG &&
                surface->window_index < DESKTOP_WM_CAPACITY &&
                manager->windows[surface->window_index].visible &&
                manager->keyboard_focus != (int32_t)surface->window_index) {
                (void)desktop_wm_select(manager, surface->window_index);
                desktop_dirty_full(dirty);
            }
            if (surface->window_index < DESKTOP_WM_CAPACITY &&
                manager->windows[surface->window_index].visible &&
                !surface->close_sent) {
                desktop_rect_t client = desktop_window_client_rect(
                    manager, surface->window_index);
                uint32_t live_resize = surface_window_is_live_resizing(
                    manager, surface->window_index);
                if (!live_resize &&
                    surface->acknowledged_serial ==
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
                        surface->configured_serial,
                        surface->pending_width != 0U
                            ? surface->pending_width : surface->width,
                        surface->pending_height != 0U
                            ? surface->pending_height : surface->height,
                        0U, 0U,
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
                reist_gui_rect_t local_damage;
                int damage_status = desktop_surface_present_damage_take(
                    surfaces, surface->owner, surface->handle,
                    &local_damage);
                if (damage_status == DESKTOP_SURFACE_OK) {
                    desktop_rect_t client = desktop_window_client_rect(
                        manager, surface->window_index);
                    desktop_rect_t presentation_damage = {
                        client.x + local_damage.x,
                        client.y + local_damage.y,
                        local_damage.width,
                        local_damage.height,
                    };
                    desktop_dirty_add(dirty, presentation_damage);
                } else if (damage_status != DESKTOP_SURFACE_ESTATE) {
                    desktop_rect_t presentation_damage =
                        desktop_wm_window_bounds(
                            manager, surface->window_index);
                    desktop_dirty_add(dirty, presentation_damage);
                }
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
        if (surface->role == REIST_GUI_SURFACE_ROLE_DIALOG &&
            surface->parent.id != 0U &&
            surface->parent.id <= DESKTOP_SURFACE_CAPACITY) {
            desktop_surface_slot_t *parent =
                &surfaces->slots[surface->parent.id - 1U];
            if (parent->active &&
                parent->handle.generation == surface->parent.generation &&
                parent->window_index < DESKTOP_WM_CAPACITY) {
                desktop_window_t *parent_window =
                    &manager->windows[parent->window_index];
                window->x = parent_window->x +
                    (int32_t)((parent_window->width > decorated_width
                        ? parent_window->width - decorated_width : 0U) / 2U);
                window->y = parent_window->y +
                    (int32_t)((parent_window->height > decorated_height
                        ? parent_window->height - decorated_height : 0U) / 2U);
            } else {
                window->x = manager->work_left +
                    (int32_t)((available_width - decorated_width) / 2U);
                window->y = manager->work_top +
                    (int32_t)((available_height - decorated_height) / 2U);
            }
        } else {
            window->x = manager->work_left +
                (int32_t)((available_width - decorated_width) / 2U);
            window->y = manager->work_top +
                (int32_t)((available_height - decorated_height) / 2U);
        }
        window->content_id = DESKTOP_SURFACE_CONTENT_TAG | surface->handle.id;
        window->flags = DESKTOP_WM_WINDOW_RETAINED_RESIZE;
        surface->window_index = chosen;
        (void)desktop_wm_open(manager, chosen);
        if (desktop_shortcut_probe_enabled &&
            text_equal(surface->title, "REIST Editor")) {
            desktop_shortcut_probe_publish_point(
                "CLOSE", "client",
                desktop_wm_close_rect(manager, chosen));
        }
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

static desktop_pointer_present_result_t desktop_pointer_present(
        int32_t x, int32_t y, uint32_t visible) {
    desktop_pointer_present_result_t result = {0};
    result.clock_valid = x86os_monotonic_ms(&result.started_ms) == 0;
    result.status = x86os_pointer_update(x, y, visible);
    if (!result.clock_valid ||
        x86os_monotonic_ms(&result.finished_ms) != 0 ||
        result.finished_ms < result.started_ms)
        result.clock_valid = 0U;
    return result;
}

static void hover_probe_initialize(desktop_hover_probe_t *probe,
                                   uint32_t enabled) {
    if (probe == 0) return;
    *probe = (desktop_hover_probe_t){
        .enabled = enabled != 0U,
        .last_hot = REIST_GUI_MENU_NO_INDEX,
    };
}

static void hover_probe_record_pointer_present(
    desktop_hover_probe_t *probe, uint32_t pending_clock_valid,
    uint64_t pending_since_ms,
    const desktop_pointer_present_result_t *result) {
    if (probe == 0 || !probe->enabled || probe->complete || result == 0)
        return;
    if (result->status != 0) {
        /* Display lifecycle contention is deliberately nonblocking. The
         * pending position remains queued and is retried next turn; only a
         * hard service failure invalidates the probe. */
        if (result->status != -11)
            saturating_increment(&probe->pointer_failures);
        return;
    }
    saturating_increment(&probe->pointer_frames);
    if (!pending_clock_valid || !result->clock_valid ||
        result->finished_ms < pending_since_ms) {
        saturating_increment(&probe->clock_errors);
        return;
    }

    uint64_t call = result->finished_ms - result->started_ms;
    uint32_t call_ms = call > UINT32_MAX ? UINT32_MAX : (uint32_t)call;
    if (call_ms > probe->pointer_call_max_ms)
        probe->pointer_call_max_ms = call_ms;
    uint64_t latency = result->finished_ms - pending_since_ms;
    uint32_t latency_ms = latency > UINT32_MAX
        ? UINT32_MAX : (uint32_t)latency;
    if (latency_ms > probe->pointer_latency_max_ms)
        probe->pointer_latency_max_ms = latency_ms;

    if (probe->pointer_last_present_ms != 0U) {
        if (result->finished_ms < probe->pointer_last_present_ms ||
            pending_since_ms < probe->pointer_last_present_ms) {
            saturating_increment(&probe->clock_errors);
        } else {
            uint64_t continuous_deadline =
                probe->pointer_last_present_ms >
                    UINT64_MAX - DESKTOP_POINTER_CONTINUOUS_INPUT_INTERVAL_MS
                ? UINT64_MAX
                : probe->pointer_last_present_ms +
                    DESKTOP_POINTER_CONTINUOUS_INPUT_INTERVAL_MS;
            if (pending_since_ms <= continuous_deadline) {
                uint64_t gap = result->finished_ms -
                    probe->pointer_last_present_ms;
                uint32_t gap_ms = gap > UINT32_MAX
                    ? UINT32_MAX : (uint32_t)gap;
                if (gap_ms > probe->pointer_max_gap_ms)
                    probe->pointer_max_gap_ms = gap_ms;
            }
        }
    }
    probe->pointer_last_present_ms = result->finished_ms;
}

static uint32_t desktop_pointer_present_completed(
    desktop_hover_probe_t *probe, uint32_t pending_clock_valid,
    uint64_t pending_since_ms,
    const desktop_pointer_present_result_t *result) {
    hover_probe_record_pointer_present(
        probe, pending_clock_valid, pending_since_ms, result);
    return result != 0 && result->status == 0;
}

static void print_hover_probe_metrics(const desktop_hover_probe_t *probe) {
    if (probe == 0) return;
    x86os_puts("DESKTOP_HOVER_METRICS");
    print_metric("version", DESKTOP_HOVER_PROBE_VERSION);
    print_metric("items", DESKTOP_HOVER_PROBE_ITEMS);
    print_metric("frames", probe->hover_frames);
    print_metric("full_frames", probe->hover_full_frames);
    print_metric("total_ms", probe->hover_total_ms);
    print_metric("max_ms", probe->hover_max_ms);
    print_metric("damage_max", probe->hover_damage_max);
    print_metric("mouse_reports", probe->mouse_reports);
    print_metric("mouse_batch_max_ms", probe->mouse_batch_max_ms);
    print_metric("mouse_batch_max_reports", probe->mouse_batch_max_reports);
    print_metric("pointer_frames", probe->pointer_frames);
    print_metric("pointer_max_gap_ms", probe->pointer_max_gap_ms);
    print_metric("pointer_latency_max_ms", probe->pointer_latency_max_ms);
    print_metric("pointer_call_max_ms", probe->pointer_call_max_ms);
    print_metric("pointer_failures", probe->pointer_failures);
    print_metric("order_errors", probe->order_errors);
    print_metric("clock_errors", probe->clock_errors);
    x86os_putchar('\n');
    x86os_puts(probe->success
        ? "DESKTOP_HOVER_OK\n" : "DESKTOP_HOVER_FAIL\n");
}

static void hover_probe_record_transition(
    desktop_hover_probe_t *probe, const desktop_ui_state_t *ui,
    const desktop_dirty_region_t *dirty,
    const desktop_render_metrics_t *metrics,
    uint32_t full_frames_before, uint32_t full_total_before,
    uint32_t dirty_frames_before, uint32_t dirty_total_before,
    uint32_t clock_errors_before) {
    if (probe == 0 || !probe->enabled || probe->complete || ui == 0 ||
        dirty == 0 || metrics == 0) return;
    uint32_t hot = ui->menu.open_menu == DESKTOP_MENU_START
        ? ui->menu.hot_item : REIST_GUI_MENU_NO_INDEX;
    if (hot == probe->last_hot) return;
    probe->last_hot = hot;
    if (hot == REIST_GUI_MENU_NO_INDEX) return;
    if (hot >= DESKTOP_HOVER_PROBE_ITEMS) {
        saturating_increment(&probe->order_errors);
        return;
    }
    uint32_t bit = 1U << hot;
    if ((probe->visited_mask & bit) != 0U) return;
    /* The virtual HID path may coalesce consecutive, replaceable motion
     * reports before this outer render iteration observes the selected row.
     * Coverage is therefore established by every distinct row receiving its
     * own bounded repaint, rather than by requiring an implementation-specific
     * observation order between those independent iterations. */
    probe->visited_mask |= bit;
    saturating_increment(&probe->hover_frames);
    if (dirty->count == 0U) saturating_increment(&probe->order_errors);
    if (dirty->count > probe->hover_damage_max)
        probe->hover_damage_max = dirty->count;

    uint32_t elapsed_ms = 0U;
    uint32_t rendered_frames = 0U;
    if (dirty->full) {
        saturating_increment(&probe->hover_full_frames);
        rendered_frames = metrics->full_frames - full_frames_before;
        elapsed_ms = metrics->full_total_ms - full_total_before;
    } else {
        rendered_frames = metrics->dirty_frames - dirty_frames_before;
        elapsed_ms = metrics->dirty_total_ms - dirty_total_before;
    }
    if (rendered_frames != 1U)
        saturating_increment(&probe->order_errors);
    if (metrics->clock_errors > clock_errors_before)
        probe->clock_errors = saturating_add_u32(
            probe->clock_errors,
            metrics->clock_errors - clock_errors_before);
    probe->hover_total_ms = saturating_add_u32(
        probe->hover_total_ms, elapsed_ms);
    if (elapsed_ms > probe->hover_max_ms)
        probe->hover_max_ms = elapsed_ms;
    uint32_t all_items = (1U << DESKTOP_HOVER_PROBE_ITEMS) - 1U;
    if (probe->visited_mask != all_items) return;
    probe->complete = 1U;
    probe->success = probe->hover_frames == DESKTOP_HOVER_PROBE_ITEMS &&
        probe->hover_full_frames == 0U && probe->hover_damage_max <= 2U &&
        probe->mouse_reports >= DESKTOP_HOVER_PROBE_ITEMS &&
        probe->pointer_frames >= 2U && probe->order_errors == 0U &&
        probe->clock_errors == 0U;
    print_hover_probe_metrics(probe);
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
    x86os_puts("DESKTOP_ACCELERATION");
    print_metric("accelerated_frames", metrics->accelerated_frames);
    print_metric("fallbacks", metrics->acceleration_fallbacks);
    print_metric("observed_caps", desktop_svga2d_observed_capabilities);
    print_metric("reconnects", desktop_svga2d_reconnects);
    print_metric("reconnect_attempts", desktop_svga2d_reconnect_attempts);
    x86os_puts(" connect_status=");
    x86os_print_number(desktop_svga2d_last_connect_status);
    x86os_puts(" service_status=");
    x86os_print_number(desktop_svga2d_last_service_status);
    x86os_puts(" transaction_status=");
    x86os_print_number(desktop_svga2d_last_transaction_status);
    x86os_puts(" copy_status=");
    x86os_print_number(desktop_svga2d_last_copy_status);
    x86os_puts(" mark_status=");
    x86os_print_number(desktop_svga2d_last_mark_status);
    x86os_putchar('\n');
}

static uint32_t desktop_try_exit(
    int32_t pointer_x, int32_t pointer_y, uint32_t runtime_activated,
    const desktop_render_metrics_t *metrics) {
    /* A supervised interactive session announces intentional shutdown before
     * relinquishing display publication. Direct diagnostic launches are
     * compatibility processes; their rejected report is deliberately benign. */
    (void)x86os_reist_report(
        X86OS_REIST_REPORT_DIAGNOSTIC, 0x434D5053U);
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    if (runtime_activated && desktop_display_deactivate() != 0) {
        x86os_puts("desktop: VGA-Rueckkehr fehlgeschlagen\n");
        (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        return 0U;
    }
    if (!runtime_activated) x86os_clear();
    print_render_metrics(metrics);
    x86os_puts("DESKTOP_EXIT_OK\n");
    return 1U;
}

static int desktop_explorer_scroll_probe_run(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    const x86os_display_info_t *display) {
    if (manager == 0 || explorer == 0 || display == 0 ||
        !explorer->windows[0].active ||
        explorer->windows[0].entry_count < 8U) return -1;
    desktop_explorer_window_t *window = &explorer->windows[0];
    if (desktop_explorer_up(explorer, 0U) != DESKTOP_EXPLORER_OK ||
        !text_equal(window->path, "/usr/share") ||
        desktop_explorer_navigate(
            explorer, 0U, "/usr/share/fonts") != DESKTOP_EXPLORER_OK ||
        !text_equal(window->path, "/usr/share/fonts") ||
        desktop_explorer_back(explorer, 0U) != DESKTOP_EXPLORER_OK ||
        !text_equal(window->path, "/usr/share") ||
        desktop_explorer_forward(explorer, 0U) != DESKTOP_EXPLORER_OK ||
        !text_equal(window->path, "/usr/share/fonts")) return -2;
    uint32_t refreshed_generation = window->snapshot_generation;
    if (desktop_explorer_refresh(explorer, 0U) != DESKTOP_EXPLORER_OK ||
        window->snapshot_generation == refreshed_generation ||
        !text_equal(window->path, "/usr/share/fonts")) return -3;
    desktop_rect_t actual = desktop_explorer_content_rect(
        manager, explorer, 0U);
    desktop_rect_t client = actual;
    uint32_t probe_height = DESKTOP_EXPLORER_ICON_HEIGHT * 2U;
    if (client.height > probe_height) client.height = probe_height;
    desktop_explorer_layout_t layout =
        desktop_explorer_layout(window, client);
    if (!layout.enabled || layout.maximum_first_row == 0U ||
        layout.scrollbar.x != client.x + (int32_t)client.width -
            (int32_t)layout.scrollbar.width) return -4;

    desktop_explorer_result_t result;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_press(
            explorer, 0U, client, layout.increment.x + 1,
            layout.increment.y + 1, &result) != DESKTOP_EXPLORER_OK ||
        !result.viewport_changed || window->first_row != 1U) return -5;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_release(
            explorer, 0U, client, layout.increment.x + 1,
            layout.increment.y + 1, 1U, &result) != DESKTOP_EXPLORER_OK ||
        window->scroll_capture != DESKTOP_EXPLORER_SCROLL_NONE) return -6;

    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_wheel(
            explorer, 0U, client, -1, &result) != DESKTOP_EXPLORER_OK ||
        !result.viewport_changed || window->first_row <= 1U) return -7;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_wheel(
            explorer, 0U, client, INT32_MAX, &result) !=
            DESKTOP_EXPLORER_OK || window->first_row != 0U) return -8;

    desktop_explorer_layout_t thumb_layout =
        desktop_explorer_layout(window, client);
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_press(
            explorer, 0U, client, thumb_layout.thumb.x + 1,
            thumb_layout.thumb.y + 1, &result) != DESKTOP_EXPLORER_OK ||
        window->scroll_capture != DESKTOP_EXPLORER_SCROLL_THUMB) return -9;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_motion(
            explorer, 0U, client, thumb_layout.thumb.x + 1,
            thumb_layout.track.y + (int32_t)thumb_layout.track.height + 1,
            &result) != DESKTOP_EXPLORER_OK ||
        window->first_row != thumb_layout.maximum_first_row) return -10;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_release(
            explorer, 0U, client, thumb_layout.thumb.x + 1,
            thumb_layout.track.y + 1, 2U, &result) != DESKTOP_EXPLORER_OK)
        return -11;

    window->first_row = 0U;
    window->selected = window->entry_count - 1U;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_keyboard(
            explorer, 0U, client, DESKTOP_EXPLORER_KEY_LEFT,
            &result) != DESKTOP_EXPLORER_OK ||
        !result.viewport_changed || window->first_row == 0U) return -12;

    desktop_rect_t resized = client;
    if (resized.width > DESKTOP_EXPLORER_SCROLLBAR_EXTENT + 80U)
        resized.width -= 80U;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_resize(
            explorer, 0U, resized, &result) != DESKTOP_EXPLORER_OK)
        return -13;
    desktop_explorer_layout_t resized_layout =
        desktop_explorer_layout(window, resized);
    if (resized_layout.scrollbar.x !=
            resized.x + (int32_t)resized.width -
                (int32_t)resized_layout.scrollbar.width ||
        resized_layout.scrollbar.y != resized.y ||
        resized_layout.scrollbar.height != resized.height) return -14;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_resize(
            explorer, 0U, actual, &result) != DESKTOP_EXPLORER_OK)
        return -15;
    return 0;
}

static int desktop_explorer_views_probe_run(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    const x86os_display_info_t *display) {
    if (manager == 0 || explorer == 0 || display == 0 ||
        !explorer->windows[0].active ||
        explorer->windows[0].entry_count < 8U) return -1;
    desktop_explorer_window_t *window = &explorer->windows[0];
    if (window->view != DESKTOP_EXPLORER_VIEW_ICONS ||
        !text_equal(window->path, "/usr/share/fonts")) return -2;
    desktop_rect_t actual = desktop_explorer_content_rect(
        manager, explorer, 0U);
    desktop_rect_t client = actual;
    uint32_t probe_height = DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT +
        DESKTOP_EXPLORER_DETAILS_ROW_HEIGHT * 5U;
    if (client.height > probe_height) client.height = probe_height;
    uint32_t generation = window->snapshot_generation;
    window->selected = window->entry_count - 1U;
    window->first_row = 0U;

    desktop_explorer_result_t result;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_toggle_view(
            explorer, 0U, client, &result) != DESKTOP_EXPLORER_OK ||
        window->view != DESKTOP_EXPLORER_VIEW_DETAILS ||
        window->snapshot_generation != generation ||
        window->selected != window->entry_count - 1U ||
        !result.viewport_changed) return -3;
    desktop_explorer_layout_t layout =
        desktop_explorer_layout(window, client);
    uint32_t expected_visible_rows =
        (client.height - layout.header.height) /
            DESKTOP_EXPLORER_DETAILS_ROW_HEIGHT;
    if (expected_visible_rows == 0U) expected_visible_rows = 1U;
    if (layout.header.height != DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT ||
        layout.viewport.y != layout.header.y +
            (int32_t)layout.header.height ||
        layout.scrollbar.y != layout.viewport.y ||
        layout.scrollbar.height != layout.viewport.height ||
        layout.columns != 1U ||
        layout.visible_rows != expected_visible_rows ||
        !layout.enabled || window->first_row != layout.maximum_first_row ||
        desktop_explorer_entry_at(
            window, client, layout.header.x + 1,
            layout.header.y + 1) != DESKTOP_EXPLORER_NO_ENTRY) return -4;

    desktop_rect_t selected = desktop_explorer_entry_rect(
        window, client, window->selected);
    if (selected.width != layout.viewport.width ||
        selected.height != DESKTOP_EXPLORER_DETAILS_ROW_HEIGHT) return -5;
    int32_t hit_x = selected.x + 2;
    int32_t hit_y = selected.y + 2;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_press(
            explorer, 0U, client, hit_x, hit_y, &result) !=
        DESKTOP_EXPLORER_OK) return -6;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_release(
            explorer, 0U, client, hit_x, hit_y, 100U, &result) !=
            DESKTOP_EXPLORER_OK || result.activated) return -7;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_press(
            explorer, 0U, client, hit_x, hit_y, &result) !=
        DESKTOP_EXPLORER_OK) return -8;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_release(
            explorer, 0U, client, hit_x, hit_y, 300U, &result) !=
            DESKTOP_EXPLORER_OK || !result.activated) return -9;

    desktop_explorer_result_initialize(&result);
    uint32_t bottom_row = window->first_row;
    if (desktop_explorer_wheel(
            explorer, 0U, client, 1, &result) != DESKTOP_EXPLORER_OK ||
        !result.viewport_changed || window->first_row >= bottom_row)
        return -10;
    desktop_explorer_layout_t thumb_layout =
        desktop_explorer_layout(window, client);
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_press(
            explorer, 0U, client, thumb_layout.thumb.x + 1,
            thumb_layout.thumb.y + 1, &result) != DESKTOP_EXPLORER_OK ||
        window->scroll_capture != DESKTOP_EXPLORER_SCROLL_THUMB) return -11;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_motion(
            explorer, 0U, client, thumb_layout.thumb.x + 1,
            thumb_layout.track.y + (int32_t)thumb_layout.track.height + 1,
            &result) != DESKTOP_EXPLORER_OK ||
        window->first_row != thumb_layout.maximum_first_row) return -12;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_pointer_release(
            explorer, 0U, client, thumb_layout.thumb.x + 1,
            thumb_layout.track.y + 1, 400U, &result) != DESKTOP_EXPLORER_OK)
        return -13;

    desktop_rect_t resized = client;
    if (resized.width > DESKTOP_EXPLORER_SCROLLBAR_EXTENT + 80U)
        resized.width -= 80U;
    resized.height = DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT +
        DESKTOP_EXPLORER_DETAILS_ROW_HEIGHT * 3U;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_resize(
            explorer, 0U, resized, &result) != DESKTOP_EXPLORER_OK)
        return -14;
    desktop_explorer_layout_t resized_layout =
        desktop_explorer_layout(window, resized);
    if (resized_layout.scrollbar.x !=
            resized.x + (int32_t)resized.width -
                (int32_t)resized_layout.scrollbar.width ||
        resized_layout.scrollbar.y != resized.y +
            (int32_t)DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT ||
        resized_layout.scrollbar.height !=
            resized.height - DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT ||
        resized_layout.visible_rows != 3U ||
        window->first_row != resized_layout.maximum_first_row) return -15;

    if (desktop_explorer_up(explorer, 0U) != DESKTOP_EXPLORER_OK ||
        window->view != DESKTOP_EXPLORER_VIEW_DETAILS ||
        !text_equal(window->path, "/usr/share") ||
        desktop_explorer_back(explorer, 0U) != DESKTOP_EXPLORER_OK ||
        window->view != DESKTOP_EXPLORER_VIEW_DETAILS ||
        !text_equal(window->path, "/usr/share/fonts")) return -16;
    uint32_t refreshed_generation = window->snapshot_generation;
    if (desktop_explorer_refresh(explorer, 0U) != DESKTOP_EXPLORER_OK ||
        window->view != DESKTOP_EXPLORER_VIEW_DETAILS ||
        window->snapshot_generation == refreshed_generation) return -17;

    generation = window->snapshot_generation;
    window->selected = 0U;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_toggle_view(
            explorer, 0U, actual, &result) != DESKTOP_EXPLORER_OK ||
        window->view != DESKTOP_EXPLORER_VIEW_ICONS ||
        window->snapshot_generation != generation ||
        window->selected != 0U) return -18;
    desktop_explorer_layout_t icon_layout =
        desktop_explorer_layout(window, actual);
    if (icon_layout.header.width != 0U || icon_layout.header.height != 0U ||
        icon_layout.columns == 0U) return -19;
    desktop_explorer_result_initialize(&result);
    if (desktop_explorer_toggle_view(
            explorer, 0U, actual, &result) != DESKTOP_EXPLORER_OK ||
        window->view != DESKTOP_EXPLORER_VIEW_DETAILS ||
        window->snapshot_generation != generation ||
        window->selected != 0U) return -20;
    desktop_explorer_layout_t details_layout =
        desktop_explorer_layout(window, actual);
    if (details_layout.header.height !=
            DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT ||
        details_layout.columns != 1U) return -21;
    return 0;
}

static char filetypes_config[DESKTOP_FILETYPES_CONFIG_CAPACITY + 1U];
static char system_sound_config[REIST_CONFIG_FILE_CAPACITY + 1U];
static reist_config_document_t system_sound_document;
static desktop_system_sound_state_t system_sound_candidate;
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

static uint32_t desktop_system_sound_path_valid(const char *path) {
    static const char prefix[] = "/usr/share/sounds/";
    if (path == 0) return 0U;
    size_t length = bounded_text_length(path, REIST_CONFIG_VALUE_CAPACITY);
    if (length >= REIST_CONFIG_VALUE_CAPACITY ||
        length <= sizeof(prefix) - 1U + 4U) return 0U;
    for (size_t index = 0U; index < sizeof(prefix) - 1U; ++index)
        if (path[index] != prefix[index]) return 0U;
    for (size_t index = sizeof(prefix) - 1U; index < length; ++index) {
        unsigned char character = (unsigned char)path[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '-' || character == '_' || character == '.'))
            return 0U;
    }
    return path[length - 4U] == '.' && path[length - 3U] == 'w' &&
        path[length - 2U] == 'a' && path[length - 1U] == 'v';
}

static void desktop_system_sound_state_reset(
    desktop_system_sound_state_t *state) {
    if (state == 0) return;
    volatile uint8_t *bytes = (volatile uint8_t *)state;
    for (size_t index = 0U; index < sizeof(*state); ++index)
        bytes[index] = 0U;
    state->pending_event = DESKTOP_SYSTEM_SOUND_NONE;
}

static int load_system_sounds(desktop_system_sound_state_t *state) {
    if (state == 0) return -22;
    desktop_system_sound_state_reset(&system_sound_candidate);
    size_t used = 0U;
    if (read_file_bounded(
            DESKTOP_SYSTEM_SOUND_CONFIG_PATH,
            (uint8_t *)system_sound_config,
            REIST_CONFIG_FILE_CAPACITY, &used) != 0 ||
        reist_config_parse(
            system_sound_config, used, DESKTOP_SYSTEM_SOUND_SCHEMA,
            &system_sound_document) != 0) return -1;
    const char *enabled = reist_config_get(
        &system_sound_document, "enabled");
    if (!text_equal(enabled, "true") && !text_equal(enabled, "false"))
        return -1;
    system_sound_candidate.enabled = text_equal(enabled, "true");
    for (uint32_t event = 0U;
         event < DESKTOP_SYSTEM_SOUND_EVENT_COUNT; ++event) {
        const char *path = reist_config_get(
            &system_sound_document, desktop_system_sound_keys[event]);
        if (path == 0) return -1;
        if (text_equal(path, "none")) continue;
        if (!desktop_system_sound_path_valid(path) ||
            copy_launch_text(
                system_sound_candidate.paths[event],
                sizeof(system_sound_candidate.paths[event]), path) != 0)
            return -1;
    }
    /* Keep the final publication explicit: freestanding GUI programs do not
     * link a compiler-generated memcpy for this comparatively large table. */
    state->enabled = system_sound_candidate.enabled;
    state->pending_event = DESKTOP_SYSTEM_SOUND_NONE;
    for (uint32_t slot = 0U;
         slot < DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY; ++slot) {
        state->children[slot].pid = 0;
        state->children[slot].process_generation = 0U;
    }
    for (uint32_t event = 0U;
         event < DESKTOP_SYSTEM_SOUND_EVENT_COUNT; ++event)
        if (copy_launch_text(
                state->paths[event], sizeof(state->paths[event]),
                system_sound_candidate.paths[event]) != 0) return -1;
    return 0;
}

static uint32_t desktop_system_sound_priority(uint32_t event) {
    if (event == DESKTOP_SYSTEM_SOUND_SHUTDOWN) return 7U;
    if (event == DESKTOP_SYSTEM_SOUND_ERROR) return 6U;
    if (event == DESKTOP_SYSTEM_SOUND_TRASH_EMPTY) return 5U;
    if (event == DESKTOP_SYSTEM_SOUND_TRASH_DROP) return 4U;
    if (event == DESKTOP_SYSTEM_SOUND_NOTIFICATION) return 3U;
    if (event == DESKTOP_SYSTEM_SOUND_STARTUP) return 2U;
    return 0U;
}

static uint32_t desktop_system_sound_active_count(
        const desktop_system_sound_state_t *state) {
    uint32_t count = 0U;
    if (state == 0) return 0U;
    for (uint32_t slot = 0U;
         slot < DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY; ++slot)
        if (state->children[slot].pid > 0) ++count;
    return count;
}

static int desktop_system_sound_launch(
        desktop_system_sound_state_t *state, uint32_t event) {
    if (state == 0 || !state->enabled ||
        event >= DESKTOP_SYSTEM_SOUND_EVENT_COUNT ||
        state->paths[event][0] == '\0') return -22;
    uint32_t slot = DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY;
    for (uint32_t index = 0U;
         index < DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY; ++index) {
        if (state->children[index].pid <= 0) {
            slot = index;
            break;
        }
    }
    if (slot == DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY) return -28;
    const char *arguments[] = {
        DESKTOP_SYSTEM_SOUND_PLAYER, "--quiet", state->paths[event],
    };
    int pid = x86os_spawnv(DESKTOP_SYSTEM_SOUND_PLAYER, 3, arguments);
    if (pid <= 0) return pid < 0 ? pid : -5;
    x86os_process_identity_t identity;
    int identity_status = x86os_process_identity_of(pid, &identity);
    if (identity_status != 0 || identity.version != 1U ||
        identity.struct_size != sizeof(identity) || identity.pid != pid ||
        identity.generation == 0U) {
        /* This PID was returned by our still-unreaped spawn transaction, so
         * it cannot have been recycled. Terminate before waiting: malformed
         * identity metadata must never turn GUI startup into a live wait. */
        (void)x86os_kill(pid);
        int child_status = 0;
        (void)x86os_wait(pid, &child_status);
        return identity_status != 0 ? identity_status : -84;
    }
    state->children[slot] = (desktop_system_sound_child_t){
        .pid = pid,
        .process_generation = identity.generation,
    };
    return 0;
}

static void desktop_system_sound_poll(desktop_system_sound_state_t *state) {
    if (state == 0) return;
    for (uint32_t slot = 0U;
         slot < DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY; ++slot) {
        desktop_system_sound_child_t *child = &state->children[slot];
        if (child->pid <= 0) continue;
        x86os_process_identity_t identity;
        int identity_status = x86os_process_identity_of(
            child->pid, &identity);
        if (identity_status == 0 && identity.version == 1U &&
            identity.struct_size == sizeof(identity) &&
            identity.pid == child->pid &&
            identity.generation == child->process_generation)
            continue;
        /* A successful but foreign/malformed identity is never evidence that
         * our exact child can be reaped. Keep the fixed slot quarantined
         * instead of waiting on or clearing a possibly live process. */
        if (identity_status == 0) continue;
        int child_status = 0;
        if (x86os_wait(child->pid, &child_status) == child->pid)
            *child = (desktop_system_sound_child_t){0};
    }
    if (desktop_system_sound_active_count(state) == 0U &&
        state->pending_event < DESKTOP_SYSTEM_SOUND_EVENT_COUNT) {
        uint32_t pending = state->pending_event;
        state->pending_event = DESKTOP_SYSTEM_SOUND_NONE;
        (void)desktop_system_sound_launch(state, pending);
    }
}

static void desktop_system_sound_request(
        desktop_system_sound_state_t *state, uint32_t event) {
    if (state == 0 || !state->enabled ||
        event >= DESKTOP_SYSTEM_SOUND_EVENT_COUNT ||
        state->paths[event][0] == '\0') return;
    uint32_t active = desktop_system_sound_active_count(state);
    if (active == 0U ||
        (event == DESKTOP_SYSTEM_SOUND_SHUTDOWN &&
         active < DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY)) {
        (void)desktop_system_sound_launch(state, event);
        return;
    }
    if (state->pending_event >= DESKTOP_SYSTEM_SOUND_EVENT_COUNT ||
        desktop_system_sound_priority(event) >
            desktop_system_sound_priority(state->pending_event))
        state->pending_event = event;
}

static int load_filetypes(desktop_filetypes_t *filetypes) {
    desktop_filetypes_initialize(filetypes);
    size_t used = 0U;
    if (read_file_bounded(
            "/etc/reist/filetypes.conf", (uint8_t *)filetypes_config,
            DESKTOP_FILETYPES_CONFIG_CAPACITY, &used) != 0) return -1;
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
        path_equal_ascii_case(program, "/usr/gui/bin/guidemo.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/notepad.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/imageviewer.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/soundplayer.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/control.prg") ||
        path_equal_ascii_case(program, "/usr/gui/bin/browser.prg");
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

static uint32_t same_surface_owner(
        reist_gui_surface_owner_t left, reist_gui_surface_owner_t right) {
    return left.pid == right.pid &&
        left.process_generation == right.process_generation;
}

static uint32_t committed_surface_owned_by(
        const desktop_surface_manager_t *surfaces,
        reist_gui_surface_owner_t owner) {
    if (surfaces == 0 || owner.pid <= 0 ||
        owner.process_generation == 0U) return 0U;
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index) {
        if (surfaces->slots[index].active &&
            (surfaces->slots[index].committed ||
             surfaces->slots[index].paint_generation != 0U) &&
            same_surface_owner(surfaces->slots[index].owner, owner))
            return 1U;
    }
    return 0U;
}

static int enqueue_guidemo_interaction_probe(
        desktop_surface_manager_t *surfaces) {
    if (surfaces == 0) return -22;
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index) {
        desktop_surface_slot_t *surface = &surfaces->slots[index];
        if (!surface->active ||
            !text_equal(surface->title, "REIST GUI Control Gallery"))
            continue;
        const reist_gui_surface_input_t events[3] = {
            {REIST_GUI_SURFACE_INPUT_POINTER_MOTION,
             next_surface_input_serial(), 300, 45, 0, 0, 0U, 0U, 0U, 0U},
            {REIST_GUI_SURFACE_INPUT_POINTER_BUTTON,
             next_surface_input_serial(), 300, 45, 0, 0, 1U, 1U, 0U, 0U},
            {REIST_GUI_SURFACE_INPUT_POINTER_BUTTON,
             next_surface_input_serial(), 300, 45, 0, 0, 1U, 0U, 0U, 0U},
        };
        for (uint32_t event = 0U; event < 3U; ++event) {
            int status = desktop_surface_input_enqueue(
                surfaces, surface->owner, surface->handle, &events[event]);
            if (status != 0) return status;
        }
        return 0;
    }
    return -2;
}

static int launch_surface_probe_client(
    desktop_surface_runtime_t *runtime,
    desktop_surface_manager_t *surfaces,
    const char *program, const char *argument,
    uint32_t lifecycle_supervised,
    uint32_t *lifecycle_sequence, uint64_t *lifecycle_heartbeat_ms) {
    if (runtime == 0 || surfaces == 0 ||
        (lifecycle_supervised != 0U &&
         (lifecycle_sequence == 0 || lifecycle_heartbeat_ms == 0)))
        return -22;
    reist_gui_surface_owner_t prior_owners[DESKTOP_SURFACE_RUNTIME_CAPACITY];
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        prior_owners[i] = (reist_gui_surface_owner_t){0};
        if (runtime->clients[i].active == DESKTOP_SURFACE_RUNTIME_BOUND)
            prior_owners[i] = runtime->clients[i].owner;
    }
    int result = launch_program(runtime, program, argument);
    if (result != 0) return result;
    reist_gui_surface_owner_t launched_owner = {0};
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        const desktop_surface_runtime_client_t *candidate =
            &runtime->clients[i];
        if (candidate->active != DESKTOP_SURFACE_RUNTIME_BOUND) continue;
        uint32_t known = 0U;
        for (uint32_t prior = 0U;
             prior < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++prior) {
            if (prior_owners[prior].process_generation != 0U &&
                same_surface_owner(candidate->owner, prior_owners[prior])) {
                known = 1U;
                break;
            }
        }
        if (known) continue;
        if (launched_owner.process_generation != 0U) return -84;
        launched_owner = candidate->owner;
    }
    if (launched_owner.pid <= 0 || launched_owner.process_generation == 0U)
        return -84;
    /* The probe starts several clients without user input between launches.
     * Service each new endpoint before launching the next process so a client
     * cannot exhaust its bounded configure timeout behind later spawns. Wait
     * for this generation's own committed Surface: an earlier short-lived
     * client may retire without making a later successful binding look like a
     * timeout, and a later spawn cannot compete with an unfinished first
     * frame. */
    for (uint32_t attempt = 0U;
         attempt < DESKTOP_SURFACE_PROBE_READY_ATTEMPTS; ++attempt) {
        uint64_t now_ms = 0U;
        if (lifecycle_supervised != 0U &&
            (x86os_monotonic_ms(&now_ms) != 0 ||
             now_ms < *lifecycle_heartbeat_ms ||
             (now_ms - *lifecycle_heartbeat_ms >= 500U &&
              desktop_lifecycle_publish_progress(
                  lifecycle_supervised, lifecycle_sequence,
                  lifecycle_heartbeat_ms) != 0)))
            return -1;
        result = desktop_surface_runtime_poll(runtime, surfaces);
        if (result != 0) return result;
        if (committed_surface_owned_by(surfaces, launched_owner))
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
    if ((result.flags & DESKTOP_WM_RESULT_REDRAW) != 0U &&
        (event->type == DESKTOP_WM_EVENT_OPEN ||
         event->type == DESKTOP_WM_EVENT_CLOSE ||
         event->type == DESKTOP_WM_EVENT_SELECT ||
         event->type == DESKTOP_WM_EVENT_POINTER_BUTTON))
        desktop_dirty_add(dirty, desktop_taskbar_rect(display));
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

static uint32_t parent_path_of(const char *path, char *parent) {
    size_t length = bounded_text_length(
        path, DESKTOP_EXPLORER_PATH_CAPACITY);
    if (length < 2U || length == DESKTOP_EXPLORER_PATH_CAPACITY)
        return 0U;
    size_t slash = length;
    while (slash != 0U && path[slash - 1U] != '/') --slash;
    if (slash == 0U) return 0U;
    size_t parent_length = slash == 1U ? 1U : slash - 1U;
    for (size_t index = 0U; index < parent_length; ++index)
        parent[index] = path[index];
    parent[parent_length] = '\0';
    return 1U;
}

static uint32_t apply_trash_restore(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, uint32_t window_index,
    uint32_t entry_index) {
    if (manager == 0 || explorer == 0 || ui == 0 || display == 0 ||
        dirty == 0 || window_index >= DESKTOP_WM_CAPACITY ||
        !desktop_window_is_trash(explorer, window_index) ||
        entry_index >= explorer->windows[window_index].entry_count)
        return 0U;
    desktop_trash_restore_request_t request;
    desktop_trash_restore_request_initialize(&request);
    if (desktop_explorer_child_path(
            &explorer->windows[window_index], entry_index,
            request.catalog_path, sizeof(request.catalog_path)) !=
        DESKTOP_EXPLORER_OK) {
        desktop_ui_open_error(
            ui, display, dirty, "Papierkorbpfad ist ungueltig.",
            DESKTOP_TRASH_FILES_PATH);
        return 0U;
    }
    const x86os_file_info_t *selected =
        &explorer->windows[window_index].entries[entry_index];
    size_t name_length = bounded_text_length(
        selected->name, sizeof(request.identity.name));
    if (name_length == sizeof(request.identity.name)) {
        desktop_ui_open_error(
            ui, display, dirty, "Papierkorbeintrag ist ungueltig.",
            request.catalog_path);
        return 0U;
    }
    for (size_t index = 0U; index <= name_length; ++index)
        request.identity.name[index] = selected->name[index];
    request.identity.type = selected->type;
    request.identity.size = selected->size;
    request.identity.create_time = selected->create_time;
    request.identity.modify_time = selected->modify_time;
    request.identity.access_time = selected->access_time;
    desktop_trash_restore_result_t result;
    desktop_trash_restore_result_initialize(&result);
    int status = desktop_trash_restore(&desktop_trash, &request, &result);
    desktop_dirty_add(
        dirty, desktop_icon_rect(display, explorer, 2U));
    if (status != DESKTOP_TRASH_OK && !result.restored) {
        const char *message = status == DESKTOP_TRASH_ECOLLISION
            ? "Am urspruenglichen Ort existiert bereits ein Eintrag."
            : status == DESKTOP_TRASH_ESTALE
                ? "Papierkorbeintrag wurde inzwischen veraendert."
                : status == DESKTOP_TRASH_EINVAL
                    ? "Wiederherstellungsdaten sind ungueltig."
                    : "Datei konnte nicht wiederhergestellt werden.";
        desktop_ui_open_error(
            ui, display, dirty, message, request.catalog_path);
        return 0U;
    }

    char original_parent[DESKTOP_EXPLORER_PATH_CAPACITY];
    uint32_t parent_valid = parent_path_of(
        result.original_path, original_parent);
    uint32_t refreshed = 1U;
    if (parent_valid && path_equal_ascii_case(
            original_parent, DESKTOP_SHORTCUT_DIRECTORY)) {
        if (desktop_explorer_desktop_refresh(explorer) !=
            DESKTOP_EXPLORER_OK)
            refreshed = 0U;
        else
            (void)desktop_layout_rebuild(display, explorer);
        desktop_shortcut_selection_reset();
        desktop_dirty_full(dirty);
    }
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        if (!explorer->windows[index].active ||
            (!path_equal_ascii_case(
                 explorer->windows[index].path,
                 DESKTOP_TRASH_FILES_PATH) &&
             (!parent_valid || !path_equal_ascii_case(
                 explorer->windows[index].path, original_parent))))
            continue;
        if (desktop_explorer_refresh(explorer, index) !=
            DESKTOP_EXPLORER_OK) refreshed = 0U;
        desktop_dirty_add(
            dirty, desktop_wm_window_bounds(manager, index));
    }
    if (status != DESKTOP_TRASH_OK || !result.cleanup_complete)
        desktop_ui_open_error(
            ui, display, dirty,
            "Datei ist wiederhergestellt; Katalogbereinigung ist fehlgeschlagen.",
            result.original_path);
    else if (!refreshed)
        desktop_ui_open_error(
            ui, display, dirty,
            "Datei ist wiederhergestellt; Ansicht ist veraltet.",
            result.original_path);
    return 0U;
}

static const char *desktop_launch_error_message(int launch_status) {
    if (launch_status == -2)
        return "Programmdatei nicht gefunden oder ungueltig.";
    if (launch_status == -11)
        return "Kein freier Prozessplatz verfuegbar.";
    if (launch_status == -12)
        return "Nicht genug Speicher fuer das Programm.";
    if (launch_status == -14)
        return "Programmargumente konnten nicht uebergeben werden.";
    if (launch_status == -16)
        return "Kein freier Scheduler-Task verfuegbar.";
    if (launch_status == -28)
        return "Keine freie IPC-Ressource verfuegbar.";
    if (launch_status == -75)
        return "Surface-Clientkapazitaet ist erschoepft.";
    if (launch_status == -3)
        return "Programm endete vor der Surface-Bindung.";
    if (launch_status == -9)
        return "Surface-Endpunkt ist beim Besitzer ungueltig (-9).";
    if (launch_status == -13)
        return "Surface-Delegation wurde verweigert (-13).";
    if (launch_status == -22 || launch_status == -36)
        return "Programmpfad ist ungueltig oder zu lang.";
    return "Programm konnte nicht gestartet werden.";
}

static int apply_path_activation(
    const desktop_filetypes_t *filetypes,
    desktop_surface_runtime_t *surface_runtime,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const char *path, uint32_t target_kind,
    int32_t pointer_x, int32_t pointer_y) {
    if (filetypes == 0 || surface_runtime == 0 || ui == 0 || display == 0 ||
        dirty == 0 || path == 0 ||
        (target_kind != DESKTOP_SHORTCUT_TARGET_PROGRAM &&
         target_kind != DESKTOP_SHORTCUT_TARGET_FILE)) return -22;
    const char *program = path;
    const char *document = 0;
    if (target_kind == DESKTOP_SHORTCUT_TARGET_FILE) {
        if (desktop_filetypes_lookup(filetypes, path, &program) != 0) {
            desktop_ui_open_error(
                ui, display, dirty, "Keine Dateizuordnung vorhanden.", path);
            return -2;
        }
        document = path;
    }
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    int launch_status = launch_program(surface_runtime, program, document);
    desktop_dirty_full(dirty);
    if (launch_status != 0)
        desktop_ui_open_error(
            ui, display, dirty,
            desktop_launch_error_message(launch_status), path);
    return launch_status;
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
    if (desktop_window_is_trash(explorer, activation->window_index))
        return apply_trash_restore(
            manager, explorer, ui, display, dirty,
            activation->window_index, activation->entry_index);
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    if (desktop_explorer_child_path(
            window, activation->entry_index, path, sizeof(path)) !=
        DESKTOP_EXPLORER_OK) return 0U;
    if (window->entries[activation->entry_index].type == X86OS_DIRECTORY) {
        int navigation_status = desktop_explorer_navigate(
            explorer, activation->window_index, path);
        if (navigation_status != DESKTOP_EXPLORER_OK) {
            desktop_ui_open_error(
                ui, display, dirty,
                "Ordner kann nicht geoeffnet werden.", path);
        }
        desktop_dirty_add(
            dirty, desktop_wm_window_bounds(
                manager, activation->window_index));
        return 0U;
    }
    if (desktop_shortcut_is_filename(
            window->entries[activation->entry_index].name)) {
        desktop_shortcut_resolve_result_t resolved;
        int shortcut_status = desktop_shortcut_resolve(
            path, &window->entries[activation->entry_index], &resolved);
        if (shortcut_status != DESKTOP_SHORTCUT_OK) {
            desktop_ui_open_error(
                ui, display, dirty,
                "Verknuepfungsziel fehlt oder wurde veraendert.", path);
            return 0U;
        }
        if (apply_path_activation(
            filetypes, surface_runtime, ui, display, dirty,
            resolved.target_path, resolved.target_kind,
            pointer_x, pointer_y) == 0) {
            x86os_puts("DESKTOP_SHORTCUT_ACTIVATED kind=");
            x86os_puts(resolved.target_kind ==
                DESKTOP_SHORTCUT_TARGET_PROGRAM ? "program" : "file");
            x86os_puts(" path=");
            x86os_puts(resolved.target_path);
            x86os_putchar('\n');
        }
        return 0U;
    }
    (void)apply_path_activation(
        filetypes, surface_runtime, ui, display, dirty, path,
        has_program_extension(path) ? DESKTOP_SHORTCUT_TARGET_PROGRAM
                                    : DESKTOP_SHORTCUT_TARGET_FILE,
        pointer_x, pointer_y);
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

static void apply_browser_activation(
    desktop_surface_runtime_t *surface_runtime, desktop_ui_state_t *ui,
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    int32_t pointer_x, int32_t pointer_y) {
    static const char path[] = "/usr/gui/bin/browser.prg";
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    int launch_status = launch_program(surface_runtime, path, 0);
    desktop_dirty_full(dirty);
    if (launch_status != 0)
        desktop_ui_open_error(
            ui, display, dirty,
            "Webbrowser konnte nicht gestartet werden.", path);
}

static uint32_t desktop_shortcut_display_name(
    const desktop_explorer_drag_file_t *file,
    char output[DESKTOP_SHORTCUT_DISPLAY_NAME_CAPACITY]) {
    if (file == 0 || output == 0) return 0U;
    size_t length = bounded_text_length(
        file->identity.name, sizeof(file->identity.name));
    if (length == 0U || length == sizeof(file->identity.name)) return 0U;
    if (has_program_extension(file->identity.name) && length > 4U)
        length -= 4U;
    size_t source = 0U;
    size_t used = 0U;
    while (source < length) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(
                file->identity.name + source, length - source,
                &consumed, &scalar) || scalar < 0x20U || scalar == 0x7FU)
            return 0U;
        if (consumed >= DESKTOP_SHORTCUT_DISPLAY_NAME_CAPACITY - used)
            break;
        for (size_t byte = 0U; byte < consumed; ++byte)
            output[used++] = file->identity.name[source + byte];
        source += consumed;
    }
    if (used == 0U) return 0U;
    output[used] = '\0';
    return 1U;
}

static void refresh_directory_views(
    desktop_explorer_t *explorer, const desktop_wm_t *manager,
    const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const char *path) {
    if (explorer == 0 || manager == 0 || display == 0 || dirty == 0 ||
        path == 0) return;
    if (path_equal_ascii_case(path, DESKTOP_SHORTCUT_DIRECTORY)) {
        if (desktop_explorer_desktop_refresh(explorer) ==
            DESKTOP_EXPLORER_OK)
            (void)desktop_layout_rebuild(display, explorer);
        desktop_shortcut_selection_reset();
        desktop_dirty_full(dirty);
    }
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        if (!explorer->windows[index].active ||
            !path_equal_ascii_case(
                explorer->windows[index].path, path)) continue;
        (void)desktop_explorer_refresh(explorer, index);
        desktop_dirty_add(
            dirty, desktop_wm_window_bounds(manager, index));
    }
}

static int desktop_shortcut_probe_start_storage_restart(void) {
    static const char program[] = "/sbin/svcctl.prg";
    const char *arguments[] = {program, "restart", "5"};
    if (!desktop_shortcut_probe_enabled ||
        desktop_shortcut_probe_restart_phase !=
            DESKTOP_SHORTCUT_PROBE_RESTART_IDLE)
        return -22;
    int pid = x86os_spawnv(program, 3, arguments);
    if (pid <= 0) return pid < 0 ? pid : -5;
    x86os_process_identity_t identity;
    int identity_status = x86os_process_identity_of(pid, &identity);
    uint64_t now_ms = 0U;
    if (identity_status != 0 || identity.version != 1U ||
        identity.struct_size != sizeof(identity) || identity.pid != pid ||
        identity.generation == 0U || x86os_monotonic_ms(&now_ms) != 0) {
        (void)x86os_kill(pid);
        int child_status = 0;
        (void)x86os_wait(pid, &child_status);
        return identity_status != 0 ? identity_status : -5;
    }
    desktop_shortcut_probe_restart_pid = pid;
    desktop_shortcut_probe_restart_generation = identity.generation;
    desktop_shortcut_probe_restart_deadline_ms =
        UINT64_MAX - now_ms < DESKTOP_SHORTCUT_PROBE_RESTART_TIMEOUT_MS
            ? UINT64_MAX
            : now_ms + DESKTOP_SHORTCUT_PROBE_RESTART_TIMEOUT_MS;
    desktop_shortcut_probe_restart_phase =
        DESKTOP_SHORTCUT_PROBE_RESTART_RUNNING;
    x86os_puts("DESKTOP_SHORTCUT_STORAGE_RESTART_REQUESTED\n");
    return 0;
}

static void desktop_shortcut_probe_fail_storage_restart(int status) {
    desktop_shortcut_probe_restart_phase =
        DESKTOP_SHORTCUT_PROBE_RESTART_FAILED;
    desktop_shortcut_probe_restart_pid = 0;
    desktop_shortcut_probe_restart_generation = 0U;
    x86os_puts("DESKTOP_SHORTCUT_PROBE_FAIL storage-restart status=");
    x86os_print_number(status);
    x86os_putchar('\n');
}

static void desktop_shortcut_probe_cancel_storage_restart(void) {
    if (desktop_shortcut_probe_restart_phase !=
            DESKTOP_SHORTCUT_PROBE_RESTART_RUNNING ||
        desktop_shortcut_probe_restart_pid <= 0) return;
    int pid = desktop_shortcut_probe_restart_pid;
    x86os_process_identity_t identity;
    int identity_status = x86os_process_identity_of(pid, &identity);
    uint32_t exact = identity_status == 0 && identity.version == 1U &&
        identity.struct_size == sizeof(identity) && identity.pid == pid &&
        identity.generation ==
            desktop_shortcut_probe_restart_generation;
    if (exact) (void)x86os_kill(pid);
    if (exact || identity_status != 0) {
        int child_status = 0;
        (void)x86os_wait(pid, &child_status);
    }
    desktop_shortcut_probe_restart_pid = 0;
    desktop_shortcut_probe_restart_generation = 0U;
    desktop_shortcut_probe_restart_phase =
        DESKTOP_SHORTCUT_PROBE_RESTART_FAILED;
}

static void desktop_shortcut_probe_poll_storage_restart(
    const x86os_display_info_t *display, const desktop_wm_t *manager,
    desktop_explorer_t *explorer, desktop_dirty_region_t *dirty) {
    if (desktop_shortcut_probe_restart_phase !=
            DESKTOP_SHORTCUT_PROBE_RESTART_RUNNING ||
        desktop_shortcut_probe_restart_pid <= 0) return;
    x86os_process_identity_t identity;
    int identity_status = x86os_process_identity_of(
        desktop_shortcut_probe_restart_pid, &identity);
    if (identity_status == 0 && identity.version == 1U &&
        identity.struct_size == sizeof(identity) &&
        identity.pid == desktop_shortcut_probe_restart_pid &&
        identity.generation ==
            desktop_shortcut_probe_restart_generation) {
        uint64_t now_ms = 0U;
        if (x86os_monotonic_ms(&now_ms) == 0 &&
            now_ms < desktop_shortcut_probe_restart_deadline_ms)
            return;
        int pid = desktop_shortcut_probe_restart_pid;
        (void)x86os_kill(pid);
        int child_status = 0;
        (void)x86os_wait(pid, &child_status);
        desktop_shortcut_probe_fail_storage_restart(
            DESKTOP_SHORTCUT_ETIMEDOUT);
        return;
    }
    if (identity_status == 0) {
        desktop_shortcut_probe_fail_storage_restart(-84);
        return;
    }
    int pid = desktop_shortcut_probe_restart_pid;
    int child_status = 0;
    if (x86os_wait(pid, &child_status) != pid || child_status != 0) {
        desktop_shortcut_probe_fail_storage_restart(
            child_status != 0 ? child_status : -5);
        return;
    }
    desktop_shortcut_probe_restart_pid = 0;
    desktop_shortcut_probe_restart_generation = 0U;
    int refresh_status =
        desktop_explorer_desktop_refresh(explorer);
    if (refresh_status != DESKTOP_EXPLORER_OK) {
        desktop_shortcut_probe_fail_storage_restart(
            refresh_status);
        return;
    }
    (void)desktop_layout_rebuild(display, explorer);
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index)
        if (explorer->windows[index].active)
            (void)desktop_explorer_refresh(explorer, index);
    desktop_shortcut_probe_restart_phase =
        DESKTOP_SHORTCUT_PROBE_RESTART_COMPLETE;
    desktop_shortcut_selection_reset();
    desktop_dirty_full(dirty);
    x86os_puts("DESKTOP_SHORTCUT_STORAGE_RELOAD_OK count=");
    x86os_print_number(
        (int)explorer->desktop_directory.entry_count);
    x86os_putchar('\n');
    desktop_shortcut_probe_publish_geometry(display, manager, explorer);
}

static void apply_explorer_shortcut_create(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty) {
    desktop_drag_object_t source_object;
    desktop_copy_bytes(
        &source_object, &ui->explorer_menu_object,
        sizeof(source_object));
    desktop_explorer_drag_file_t file;
    int validation = desktop_explorer_drag_validate(
        explorer, &source_object, &file);
    desktop_drag_object_initialize(&ui->explorer_menu_object);
    if (validation != DESKTOP_EXPLORER_OK ||
        file.identity.type != X86OS_FILE ||
        desktop_shortcut_is_filename(file.identity.name)) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Datei wurde inzwischen veraendert.",
            "Verknuepfung wurde nicht erstellt.");
        return;
    }
    desktop_shortcut_create_request_t request;
    desktop_shortcut_create_request_initialize(&request);
    if (desktop_explorer_drag_source_directory(
            explorer, &source_object,
            request.directory_path,
            &request.directory_identity) != DESKTOP_EXPLORER_OK) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Quellordner wurde inzwischen veraendert.",
            "Verknuepfung wurde nicht erstellt.");
        return;
    }
    if (!desktop_shortcut_display_name(&file, request.display_name)) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Dateiname ist fuer eine Verknuepfung ungueltig.", file.path);
        return;
    }
    size_t path_length = bounded_text_length(
        file.path, sizeof(request.target_path));
    if (path_length == 0U || path_length == sizeof(request.target_path)) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Dateipfad ist fuer eine Verknuepfung ungueltig.", file.path);
        return;
    }
    for (size_t index = 0U; index <= path_length; ++index)
        request.target_path[index] = file.path[index];
    request.target_kind = has_program_extension(file.path)
        ? DESKTOP_SHORTCUT_TARGET_PROGRAM
        : DESKTOP_SHORTCUT_TARGET_FILE;
    desktop_copy_bytes(
        &request.target_identity, &file.identity,
        sizeof(request.target_identity));

    uint32_t desktop_count_before =
        explorer->desktop_directory.entry_count;
    desktop_shortcut_create_result_t result;
    int status = desktop_shortcut_create(&request, &result);
    desktop_shortcut_selection_reset();
    refresh_directory_views(
        explorer, manager, display, dirty, request.directory_path);
    if (status != DESKTOP_SHORTCUT_OK || !result.created) {
        const char *message = status == DESKTOP_SHORTCUT_EEXIST
            ? "Der Verknuepfungsname ist inzwischen belegt."
            : status == DESKTOP_SHORTCUT_ECAPACITY
                ? "Kein freier 8.3-Verknuepfungsname verfuegbar."
                : status == DESKTOP_SHORTCUT_ESTALE
                    ? "Datei wurde inzwischen veraendert."
                    : "Verknuepfung konnte nicht erstellt werden.";
        desktop_ui_open_error(ui, display, dirty, message, file.path);
        return;
    }
    x86os_puts("DESKTOP_SHORTCUT_CREATED kind=");
    x86os_puts(request.target_kind == DESKTOP_SHORTCUT_TARGET_PROGRAM
        ? "program" : "file");
    x86os_puts(" path=");
    x86os_puts(request.target_path);
    x86os_puts(" shortcut=");
    x86os_puts(result.shortcut_path);
    x86os_putchar('\n');
    x86os_puts("DESKTOP_SHORTCUT_SIBLING_OK path=");
    x86os_puts(result.shortcut_path);
    x86os_putchar('\n');
    if (!path_equal_ascii_case(
            request.directory_path, DESKTOP_SHORTCUT_DIRECTORY) &&
        desktop_count_before == explorer->desktop_directory.entry_count)
        x86os_puts("DESKTOP_SHORTCUT_DESKTOP_UNCHANGED\n");
    desktop_shortcut_probe_publish_geometry(
        display, manager, explorer);
    if (desktop_shortcut_probe_enabled &&
        desktop_shortcut_probe_restart_phase ==
            DESKTOP_SHORTCUT_PROBE_RESTART_IDLE) {
        int restart_status =
            desktop_shortcut_probe_start_storage_restart();
        if (restart_status != 0)
            desktop_shortcut_probe_fail_storage_restart(restart_status);
    }
}

static void apply_desktop_shortcut_activation(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    const desktop_filetypes_t *filetypes,
    desktop_surface_runtime_t *surface_runtime,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, uint32_t generation,
    uint32_t entry_index, uint32_t *target,
    int32_t pointer_x, int32_t pointer_y) {
    if (explorer == 0 || generation == 0U ||
        generation != explorer->desktop_directory.snapshot_generation ||
        entry_index >= explorer->desktop_directory.entry_count) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Desktop-Eintrag wurde inzwischen veraendert.",
            DESKTOP_SHORTCUT_DIRECTORY);
        return;
    }
    const x86os_file_info_t *entry =
        &explorer->desktop_directory.entries[entry_index];
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    if (desktop_explorer_child_path(
            &explorer->desktop_directory, entry_index,
            path, sizeof(path)) != DESKTOP_EXPLORER_OK) return;
    if (entry->type == X86OS_DIRECTORY) {
        (void)open_explorer_path(
            manager, explorer, ui, display, dirty, path, target);
        return;
    }
    if (entry->type != X86OS_FILE) return;
    if (!desktop_shortcut_is_filename(entry->name)) {
        (void)apply_path_activation(
            filetypes, surface_runtime, ui, display, dirty, path,
            has_program_extension(path)
                ? DESKTOP_SHORTCUT_TARGET_PROGRAM
                : DESKTOP_SHORTCUT_TARGET_FILE,
            pointer_x, pointer_y);
        return;
    }
    desktop_shortcut_resolve_result_t resolved;
    int status = desktop_shortcut_resolve(path, entry, &resolved);
    if (status != DESKTOP_SHORTCUT_OK) {
        if (status == DESKTOP_SHORTCUT_ESTALE ||
            status == DESKTOP_SHORTCUT_ENOENT) {
            if (desktop_explorer_desktop_refresh(explorer) ==
                DESKTOP_EXPLORER_OK)
                (void)desktop_layout_rebuild(display, explorer);
            desktop_shortcut_selection_reset();
            desktop_dirty_full(dirty);
        }
        desktop_ui_open_error(
            ui, display, dirty,
            "Verknuepfungsziel fehlt oder wurde veraendert.", path);
        return;
    }
    if (apply_path_activation(
            filetypes, surface_runtime, ui, display, dirty,
            resolved.target_path, resolved.target_kind,
            pointer_x, pointer_y) == 0) {
        x86os_puts("DESKTOP_SHORTCUT_ACTIVATED kind=");
        x86os_puts(resolved.target_kind ==
            DESKTOP_SHORTCUT_TARGET_PROGRAM ? "program" : "file");
        x86os_puts(" path=");
        x86os_puts(resolved.target_path);
        x86os_putchar('\n');
        if (desktop_layout_probe_enabled)
            x86os_puts("DESKTOP_ICON_LAYOUT_ACTIVATED\n");
    }
}

static void apply_desktop_shortcut_remove(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, uint32_t generation,
    uint32_t entry_index) {
    if (explorer == 0 || generation == 0U ||
        generation != explorer->desktop_directory.snapshot_generation ||
        entry_index >= explorer->desktop_directory.entry_count ||
        explorer->desktop_directory.entries[entry_index].type != X86OS_FILE) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Desktop-Eintrag wurde inzwischen veraendert.",
            DESKTOP_SHORTCUT_DIRECTORY);
        return;
    }
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    if (desktop_explorer_child_path(
            &explorer->desktop_directory, entry_index,
            path, sizeof(path)) != DESKTOP_EXPLORER_OK) return;
    desktop_trash_request_t request;
    desktop_trash_request_initialize(&request);
    size_t length = bounded_text_length(path, sizeof(request.source_path));
    if (length == 0U || length == sizeof(request.source_path)) return;
    for (size_t index = 0U; index <= length; ++index)
        request.source_path[index] = path[index];
    desktop_copy_bytes(
        &request.identity,
        &explorer->desktop_directory.entries[entry_index],
        sizeof(request.identity));
    desktop_trash_result_t result;
    int status = desktop_trash_move(&desktop_trash, &request, &result);
    desktop_shortcut_selection_reset();
    refresh_directory_views(
        explorer, manager, display, dirty, DESKTOP_SHORTCUT_DIRECTORY);
    if (status != DESKTOP_TRASH_OK || !result.moved) {
        desktop_ui_open_error(
            ui, display, dirty,
            status == DESKTOP_TRASH_ESTALE
                ? "Datei wurde inzwischen veraendert."
                : "Datei konnte nicht in den Papierkorb verschoben werden.",
            path);
        return;
    }
    x86os_puts("DESKTOP_FILE_TRASHED remaining=");
    x86os_print_number(
        (int)explorer->desktop_directory.entry_count);
    x86os_putchar('\n');
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
    if (ui_result->action == DESKTOP_UI_ACTION_OPEN_CONTROL_PANEL)
        return DESKTOP_ACTION_OPEN_CONTROL_PANEL;
    if (ui_result->action == DESKTOP_UI_ACTION_OPEN_BROWSER)
        return DESKTOP_ACTION_OPEN_BROWSER;
    if (ui_result->action == DESKTOP_UI_ACTION_OPEN_TRASH)
        return DESKTOP_ACTION_OPEN_TRASH;
    if (ui_result->action == DESKTOP_UI_ACTION_EMPTY_TRASH)
        return DESKTOP_ACTION_EMPTY_TRASH;
    if (ui_result->action == DESKTOP_UI_ACTION_CREATE_SHORTCUT)
        return DESKTOP_ACTION_CREATE_SHORTCUT;
    if (ui_result->action == DESKTOP_UI_ACTION_OPEN_SHORTCUT)
        return DESKTOP_ACTION_OPEN_SHORTCUT;
    if (ui_result->action == DESKTOP_UI_ACTION_REMOVE_SHORTCUT)
        return DESKTOP_ACTION_REMOVE_SHORTCUT;
    return 0U;
}

static uint32_t desktop_taskbar_pointer_button(
    desktop_wm_t *manager, desktop_ui_state_t *ui,
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    int32_t x, int32_t y, uint32_t pressed, uint32_t *actions,
    uint32_t *target) {
    if (manager == 0 || ui == 0 || display == 0 || dirty == 0 ||
        actions == 0) return 0U;
    if (pressed) {
        if (!point_in_rect(desktop_taskbar_rect(display), x, y)) return 0U;
        uint32_t slot = desktop_taskbar_window_at(display, manager, x, y);
        ui->taskbar_capture_slot = slot == DESKTOP_WM_NO_TARGET
            ? DESKTOP_TASKBAR_CAPTURE_BACKGROUND : slot;
        if (slot != DESKTOP_WM_NO_TARGET)
            desktop_dirty_add(
                dirty, desktop_task_button_rect(display, manager, slot));
        return 1U;
    }
    if (ui->taskbar_capture_slot == DESKTOP_WM_NO_TARGET) return 0U;
    uint32_t captured = ui->taskbar_capture_slot;
    ui->taskbar_capture_slot = DESKTOP_WM_NO_TARGET;
    uint32_t released = desktop_taskbar_window_at(display, manager, x, y);
    desktop_dirty_add(dirty, desktop_taskbar_rect(display));
    if (released == captured && captured < DESKTOP_WM_CAPACITY &&
        manager->windows[captured].visible) {
        desktop_wm_event_t select = {
            .type = DESKTOP_WM_EVENT_SELECT,
            .target = captured,
        };
        *actions |= dispatch_desktop_event(
            manager, display, dirty, &select, target);
    }
    return 1U;
}

static uint32_t desktop_explorer_navigation_action_at(
    const desktop_wm_t *manager, const desktop_explorer_t *explorer,
    uint32_t window_index, int32_t x, int32_t y) {
    if (manager == 0 || explorer == 0 ||
        window_index >= DESKTOP_WM_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_NAVIGATION_NONE;
    desktop_explorer_chrome_t chrome = desktop_explorer_chrome(
        manager, explorer, window_index);
    if (point_in_rect(chrome.back, x, y))
        return DESKTOP_EXPLORER_NAVIGATION_BACK;
    if (point_in_rect(chrome.forward, x, y))
        return DESKTOP_EXPLORER_NAVIGATION_FORWARD;
    if (point_in_rect(chrome.up, x, y))
        return DESKTOP_EXPLORER_NAVIGATION_UP;
    if (point_in_rect(chrome.refresh, x, y))
        return DESKTOP_EXPLORER_NAVIGATION_REFRESH;
    if (point_in_rect(chrome.view, x, y))
        return DESKTOP_EXPLORER_NAVIGATION_VIEW;
    return DESKTOP_EXPLORER_NAVIGATION_NONE;
}

static int desktop_explorer_apply_navigation(
    const desktop_wm_t *manager, desktop_explorer_t *explorer,
    uint32_t window_index, uint32_t action) {
    if (action == DESKTOP_EXPLORER_NAVIGATION_BACK)
        return desktop_explorer_back(explorer, window_index);
    if (action == DESKTOP_EXPLORER_NAVIGATION_FORWARD)
        return desktop_explorer_forward(explorer, window_index);
    if (action == DESKTOP_EXPLORER_NAVIGATION_UP)
        return desktop_explorer_up(explorer, window_index);
    if (action == DESKTOP_EXPLORER_NAVIGATION_REFRESH)
        return desktop_explorer_refresh(explorer, window_index);
    if (action == DESKTOP_EXPLORER_NAVIGATION_VIEW) {
        desktop_explorer_result_t result;
        desktop_explorer_result_initialize(&result);
        return desktop_explorer_toggle_view(
            explorer, window_index,
            desktop_explorer_content_rect(manager, explorer, window_index),
            &result);
    }
    return DESKTOP_EXPLORER_EINVAL;
}

static uint32_t desktop_explorer_navigation_pointer_button(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t x, int32_t y,
    uint32_t pressed, uint32_t *actions, uint32_t *target) {
    if (manager == 0 || explorer == 0 || ui == 0 || display == 0 ||
        dirty == 0 || actions == 0) return 0U;
    if (pressed) {
        int window = desktop_wm_window_at(manager, x, y);
        if (window < 0 || window >= (int32_t)DESKTOP_WM_CAPACITY)
            return 0U;
        uint32_t window_index = (uint32_t)window;
        uint32_t action = desktop_explorer_navigation_action_at(
            manager, explorer, window_index, x, y);
        if (action == DESKTOP_EXPLORER_NAVIGATION_NONE) return 0U;
        explorer_navigation_pressed_window = window_index;
        explorer_navigation_pressed_action = action;
        desktop_wm_event_t select = {
            .type = DESKTOP_WM_EVENT_SELECT,
            .target = window_index,
        };
        *actions |= dispatch_desktop_event(
            manager, display, dirty, &select, target);
        desktop_dirty_add(
            dirty, desktop_wm_window_bounds(manager, window_index));
        return 1U;
    }
    if (explorer_navigation_pressed_window == DESKTOP_WM_NO_TARGET)
        return 0U;
    uint32_t window_index = explorer_navigation_pressed_window;
    uint32_t captured_action = explorer_navigation_pressed_action;
    explorer_navigation_pressed_window = DESKTOP_WM_NO_TARGET;
    explorer_navigation_pressed_action = DESKTOP_EXPLORER_NAVIGATION_NONE;
    if (window_index >= DESKTOP_WM_CAPACITY ||
        !explorer->windows[window_index].active) return 1U;
    uint32_t released_action = desktop_explorer_navigation_action_at(
        manager, explorer, window_index, x, y);
    if (released_action == captured_action &&
        explorer_navigation_enabled(
            &explorer->windows[window_index], captured_action)) {
        int status = desktop_explorer_apply_navigation(
            manager, explorer, window_index, captured_action);
        if (status != DESKTOP_EXPLORER_OK)
            desktop_ui_open_error(
                ui, display, dirty,
                "Explorer-Aktion ist fehlgeschlagen.",
                explorer->windows[window_index].path);
    }
    desktop_dirty_add(
        dirty, desktop_wm_window_bounds(manager, window_index));
    return 1U;
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
    if ((manager->capture_kind != DESKTOP_WM_CAPTURE_MOVE &&
         manager->capture_kind != DESKTOP_WM_CAPTURE_RESIZE) ||
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
    if (manager->capture_kind == DESKTOP_WM_CAPTURE_RESIZE &&
        (manager->windows[index].flags &
         DESKTOP_WM_WINDOW_RETAINED_RESIZE) == 0U)
        return 0U;
    *kind = manager->capture_kind == DESKTOP_WM_CAPTURE_RESIZE
        ? DESKTOP_MOVE_CACHE_RESIZE : DESKTOP_MOVE_CACHE_WINDOW;
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
        destination.width == 0U || destination.height == 0U) return;
    desktop_rect_t copy_source = source;
    desktop_rect_t copy_destination = destination;
    if (kind == DESKTOP_MOVE_CACHE_RESIZE) {
        copy_source.width = min_u32(source.width, destination.width);
        copy_source.height = min_u32(source.height, destination.height);
        copy_destination.width = copy_source.width;
        copy_destination.height = copy_source.height;
    } else if (source.width != destination.width ||
               source.height != destination.height) {
        return;
    }
    if (copy_source.x == copy_destination.x &&
        copy_source.y == copy_destination.y) return;
    move->source = copy_source;
    move->destination = copy_destination;
    move->cleanup = source;
    move->redraw = destination;
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
    if (desktop_drag.phase != DESKTOP_DRAG_PHASE_IDLE) {
        desktop_rect_t old_feedback = desktop_drag_feedback_rect();
        desktop_rect_t old_layout_feedback = desktop_layout_hover_valid
            ? desktop_layout_cell_rect(desktop_layout_hover_cell)
            : (desktop_rect_t){0, 0, 0U, 0U};
        uint32_t old_feedback_kind = desktop_drag.feedback;
        uint32_t old_target = desktop_drag.target_id;
        desktop_drag_target_t trash_target;
        desktop_drag_target_t directory_target;
        desktop_drag_target_t layout_target;
        const desktop_drag_target_t *drop_target = 0;
        desktop_layout_hover_valid = 0U;
        if (desktop_trash_drop_target(
                manager, ui, explorer, display,
                *pointer_x, *pointer_y,
                &trash_target))
            drop_target = &trash_target;
        else if (desktop_directory_drop_target(
                manager, explorer, ui, display,
                *pointer_x, *pointer_y, &directory_target))
            drop_target = &directory_target;
        else if (desktop_layout_drop_target(
                manager, ui, *pointer_x, *pointer_y, &layout_target))
            drop_target = &layout_target;
        else
            desktop_layout_hover_valid = 0U;
        uint32_t requested_operation = drop_target == &layout_target
            ? DESKTOP_DRAG_OPERATION_LAYOUT : DESKTOP_DRAG_OPERATION_MOVE;
        (void)desktop_drag_motion(
            &desktop_drag, *pointer_x, *pointer_y, drop_target,
            requested_operation);
        desktop_rect_t new_feedback = desktop_drag_feedback_rect();
        desktop_rect_t new_layout_feedback = desktop_layout_hover_valid
            ? desktop_layout_cell_rect(desktop_layout_hover_cell)
            : (desktop_rect_t){0, 0, 0U, 0U};
        desktop_dirty_add(dirty, old_feedback);
        desktop_dirty_add(dirty, new_feedback);
        desktop_dirty_add(dirty, old_layout_feedback);
        desktop_dirty_add(dirty, new_layout_feedback);
        if (old_feedback_kind != desktop_drag.feedback ||
            old_target != desktop_drag.target_id)
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, explorer, 2U));
        if (desktop_drag.phase == DESKTOP_DRAG_PHASE_DRAGGING)
            return actions;
    }
    uint32_t ui_motion_consumed = 0U;
    if (manager->capture_kind == DESKTOP_WM_CAPTURE_NONE) {
        desktop_ui_result_t ui_motion = desktop_ui_pointer_event(
            ui, display, dirty, *pointer_x, *pointer_y, 0U, 0U);
        ui_motion_consumed = ui_motion.consumed;
        actions |= apply_desktop_ui_result(
            manager, explorer, ui, display, dirty, &ui_motion, target);
        if (ui->taskbar_capture_slot != DESKTOP_WM_NO_TARGET)
            ui_motion_consumed = 1U;
    }
    if (!ui_motion_consumed) {
        desktop_wm_event_t motion = {
            .type = DESKTOP_WM_EVENT_POINTER_MOTION,
            .x = *pointer_x,
            .y = *pointer_y,
        };
        actions |= dispatch_desktop_event(
            manager, display, dirty, &motion, target);
        if (manager->capture_kind == DESKTOP_WM_CAPTURE_CLIENT &&
            manager->capture_window >= 0 &&
            manager->capture_window < (int32_t)DESKTOP_WM_CAPACITY) {
            uint32_t window_index = (uint32_t)manager->capture_window;
            desktop_explorer_result_t scroll_result;
            desktop_explorer_result_initialize(&scroll_result);
            (void)desktop_explorer_pointer_motion(
                explorer, window_index,
                desktop_explorer_content_rect(
                    manager, explorer, window_index),
                *pointer_x, *pointer_y, &scroll_result);
            if (scroll_result.viewport_changed)
                desktop_dirty_add(
                    dirty, desktop_wm_window_bounds(manager, window_index));
        } else if (manager->capture_kind == DESKTOP_WM_CAPTURE_RESIZE &&
                   manager->capture_window >= 0 &&
                   manager->capture_window < (int32_t)DESKTOP_WM_CAPACITY) {
            uint32_t window_index = (uint32_t)manager->capture_window;
            desktop_explorer_result_t resize_result;
            desktop_explorer_result_initialize(&resize_result);
            (void)desktop_explorer_resize(
                explorer, window_index,
                desktop_explorer_content_rect(
                    manager, explorer, window_index),
                &resize_result);
        }
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
    if (result->selection_changed || result->viewport_changed) {
        if (root) {
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, 0, 0U));
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
        display, explorer, pointer_x, pointer_y) == 1;
    if (left_down && !left_was_down) {
        uint32_t old_selected = control_panel_selected;
        uint32_t old_trash_selected = trash_selected;
        control_panel_selected = hit;
        control_panel_pressed = hit;
        if (hit) {
            trash_selected = 0U;
            if (explorer != 0) explorer->desktop_selected = 0U;
        }
        if (old_selected != control_panel_selected || hit)
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, explorer, 1U));
        if (old_trash_selected != trash_selected)
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, explorer, 2U));
        if (hit && desktop_drag.phase == DESKTOP_DRAG_PHASE_IDLE)
            (void)desktop_layout_arm_icon(
                1U, DESKTOP_DRAG_OBJECT_APPLICATION,
                pointer_x, pointer_y);
        return 0U;
    }
    if (!left_down && left_was_down) {
        uint32_t activate = 0U;
        uint32_t dragging_layout =
            desktop_drag.phase == DESKTOP_DRAG_PHASE_DRAGGING &&
            desktop_layout_drag_source_index == 1U;
        if (control_panel_pressed && hit && !dragging_layout) {
            uint64_t now_ms = 0U;
            if (x86os_monotonic_ms(&now_ms) == 0 &&
                control_panel_last_click_ms != 0U &&
                now_ms >= control_panel_last_click_ms &&
                now_ms - control_panel_last_click_ms <=
                    DESKTOP_EXPLORER_DOUBLE_CLICK_MS)
                activate = 1U;
            control_panel_last_click_ms = now_ms;
        }
        if (dragging_layout) control_panel_last_click_ms = 0U;
        control_panel_pressed = 0U;
        return activate;
    }
    return 0U;
}

static uint32_t trash_pointer_button(
    desktop_explorer_t *explorer, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t pointer_x, int32_t pointer_y,
    uint32_t buttons, uint32_t previous_buttons) {
    uint32_t left_down =
        (buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    uint32_t left_was_down =
        (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    uint32_t hit = desktop_icon_at_position(
        display, explorer, pointer_x, pointer_y) == 2;
    if (left_down && !left_was_down) {
        uint32_t old_selected = trash_selected;
        uint32_t old_control_selected = control_panel_selected;
        trash_selected = hit;
        trash_pressed = hit;
        if (hit) {
            control_panel_selected = 0U;
            if (explorer != 0) explorer->desktop_selected = 0U;
        }
        if (old_selected != trash_selected || hit)
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, explorer, 2U));
        if (old_control_selected != control_panel_selected)
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, explorer, 1U));
        if (hit && desktop_drag.phase == DESKTOP_DRAG_PHASE_IDLE)
            (void)desktop_layout_arm_icon(
                2U, DESKTOP_DRAG_OBJECT_APPLICATION,
                pointer_x, pointer_y);
        return 0U;
    }
    if (!left_down && left_was_down) {
        uint32_t activate = 0U;
        uint32_t dragging_layout =
            desktop_drag.phase == DESKTOP_DRAG_PHASE_DRAGGING &&
            desktop_layout_drag_source_index == 2U;
        if (trash_pressed && hit && !dragging_layout) {
            uint64_t now_ms = 0U;
            if (x86os_monotonic_ms(&now_ms) == 0 &&
                trash_last_click_ms != 0U &&
                now_ms >= trash_last_click_ms &&
                now_ms - trash_last_click_ms <=
                    DESKTOP_EXPLORER_DOUBLE_CLICK_MS)
                activate = 1U;
            trash_last_click_ms = now_ms;
        }
        if (dragging_layout) trash_last_click_ms = 0U;
        trash_pressed = 0U;
        return activate;
    }
    return 0U;
}

static void desktop_shortcut_selection_reset(void) {
    desktop_shortcut_selected = UINT32_MAX;
    desktop_shortcut_pressed = UINT32_MAX;
    desktop_shortcut_pressed_generation = 0U;
    desktop_shortcut_last_click = UINT32_MAX;
    desktop_shortcut_last_click_generation = 0U;
    desktop_shortcut_last_click_ms = 0U;
}

static uint32_t desktop_shortcut_pointer_button(
    desktop_explorer_t *explorer, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t pointer_x, int32_t pointer_y,
    uint32_t buttons, uint32_t previous_buttons,
    uint32_t *activation_index, uint32_t *activation_generation) {
    uint32_t left_down =
        (buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    uint32_t left_was_down =
        (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
    if (explorer == 0 || !explorer->desktop_directory.active) return 0U;
    int hit_icon = desktop_icon_at_position(
        display, explorer, pointer_x, pointer_y);
    uint32_t hit = hit_icon >= (int)DESKTOP_BUILTIN_ICON_COUNT
        ? (uint32_t)hit_icon - DESKTOP_BUILTIN_ICON_COUNT : UINT32_MAX;
    if (hit >= explorer->desktop_directory.entry_count)
        hit = UINT32_MAX;
    if (left_down && !left_was_down) {
        uint32_t old_selected = desktop_shortcut_selected;
        desktop_shortcut_selected = hit;
        desktop_shortcut_pressed = hit;
        desktop_shortcut_pressed_generation = hit != UINT32_MAX
            ? explorer->desktop_directory.snapshot_generation : 0U;
        if (hit != UINT32_MAX) {
            control_panel_selected = 0U;
            trash_selected = 0U;
            if (explorer != 0) explorer->desktop_selected = 0U;
        }
        if (old_selected != UINT32_MAX)
            desktop_dirty_add(
                dirty, desktop_icon_rect(
                    display, explorer,
                    DESKTOP_BUILTIN_ICON_COUNT + old_selected));
        if (hit != UINT32_MAX)
            desktop_dirty_add(
                dirty, desktop_icon_rect(
                    display, explorer,
                    DESKTOP_BUILTIN_ICON_COUNT + hit));
        if (hit != UINT32_MAX) {
            uint32_t armed = 0U;
            desktop_drag_object_t object;
            desktop_explorer_drag_file_t file;
            if (explorer->desktop_directory.entries[hit].type == X86OS_FILE &&
                desktop_explorer_desktop_drag_object(
                    explorer, hit, &object) == DESKTOP_EXPLORER_OK &&
                desktop_explorer_drag_validate(
                    explorer, &object, &file) == DESKTOP_EXPLORER_OK &&
                desktop_file_move_source_allowed(file.path)) {
                object.operations |= DESKTOP_DRAG_OPERATION_LAYOUT;
                if (desktop_drag_arm(
                        &desktop_drag, &object, pointer_x, pointer_y) ==
                    DESKTOP_DRAG_OK)
                    desktop_layout_drag_source_index =
                        DESKTOP_BUILTIN_ICON_COUNT + hit;
                armed = desktop_layout_drag_source_index ==
                    DESKTOP_BUILTIN_ICON_COUNT + hit;
            }
            if (!armed)
                (void)desktop_layout_arm_icon(
                    DESKTOP_BUILTIN_ICON_COUNT + hit,
                    DESKTOP_DRAG_OBJECT_FILE, pointer_x, pointer_y);
        }
        return 0U;
    }
    if (!left_down && left_was_down) {
        uint32_t dragging_desktop_file =
            desktop_drag.phase == DESKTOP_DRAG_PHASE_DRAGGING &&
            desktop_layout_drag_source_index ==
                DESKTOP_BUILTIN_ICON_COUNT + hit;
        uint32_t clicked = hit != UINT32_MAX &&
            !dragging_desktop_file &&
            desktop_shortcut_pressed == hit &&
            desktop_shortcut_pressed_generation ==
                explorer->desktop_directory.snapshot_generation;
        desktop_shortcut_pressed = UINT32_MAX;
        desktop_shortcut_pressed_generation = 0U;
        if (!clicked) {
            desktop_shortcut_last_click = UINT32_MAX;
            desktop_shortcut_last_click_generation = 0U;
            desktop_shortcut_last_click_ms = 0U;
            return 0U;
        }
        uint64_t now_ms = 0U;
        if (x86os_monotonic_ms(&now_ms) == 0 &&
            desktop_shortcut_last_click == hit &&
            desktop_shortcut_last_click_generation ==
                explorer->desktop_directory.snapshot_generation &&
            desktop_shortcut_last_click_ms != 0U &&
            now_ms >= desktop_shortcut_last_click_ms &&
            now_ms - desktop_shortcut_last_click_ms <=
                DESKTOP_EXPLORER_DOUBLE_CLICK_MS) {
            if (activation_index != 0 && activation_generation != 0) {
                *activation_index = hit;
                *activation_generation =
                    explorer->desktop_directory.snapshot_generation;
            }
            desktop_shortcut_last_click = UINT32_MAX;
            desktop_shortcut_last_click_generation = 0U;
            desktop_shortcut_last_click_ms = 0U;
            return 1U;
        }
        desktop_shortcut_last_click = hit;
        desktop_shortcut_last_click_generation =
            explorer->desktop_directory.snapshot_generation;
        desktop_shortcut_last_click_ms = now_ms;
    }
    return 0U;
}

static uint32_t refresh_explorer_snapshot(
    desktop_explorer_t *explorer, const desktop_wm_t *manager,
    desktop_dirty_region_t *dirty, uint32_t window_index) {
    if (explorer == 0 || manager == 0 || dirty == 0 ||
        window_index >= DESKTOP_WM_CAPACITY ||
        !explorer->windows[window_index].active) return 0U;
    if (desktop_explorer_refresh(explorer, window_index) !=
        DESKTOP_EXPLORER_OK) return 0U;
    desktop_dirty_add(
        dirty, desktop_wm_window_bounds(manager, window_index));
    return 1U;
}

static void apply_trash_drop(
    desktop_explorer_t *explorer, desktop_wm_t *manager,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const desktop_drag_result_t *drop) {
    if (explorer == 0 || manager == 0 || ui == 0 || display == 0 ||
        dirty == 0 || drop == 0 || !drop->accepted ||
        drop->operation != DESKTOP_DRAG_OPERATION_MOVE ||
        drop->target_id != DESKTOP_TRASH_TARGET_ID ||
        drop->target_generation != desktop_trash.generation)
        return;
    desktop_explorer_drag_file_t file;
    int validation = desktop_explorer_drag_validate(
        explorer, &drop->object, &file);
    char source_directory[DESKTOP_EXPLORER_PATH_CAPACITY];
    x86os_file_info_t source_directory_identity;
    if (validation != DESKTOP_EXPLORER_OK ||
        file.identity.type != X86OS_FILE ||
        desktop_explorer_drag_source_directory(
            explorer, &drop->object, source_directory,
            &source_directory_identity) != DESKTOP_EXPLORER_OK) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Datei wurde inzwischen veraendert.", "Drop abgebrochen");
        return;
    }
    desktop_trash_request_t request;
    desktop_trash_request_initialize(&request);
    size_t length = bounded_text_length(file.path, sizeof(request.source_path));
    if (length == sizeof(request.source_path)) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Dateipfad ist zu lang.", "Drop abgebrochen");
        return;
    }
    for (size_t index = 0U; index <= length; ++index)
        request.source_path[index] = file.path[index];
    size_t name_length = bounded_text_length(
        file.identity.name, sizeof(request.identity.name));
    if (name_length == sizeof(request.identity.name)) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Dateiname ist ungueltig.", "Drop abgebrochen");
        return;
    }
    for (size_t index = 0U; index <= name_length; ++index)
        request.identity.name[index] = file.identity.name[index];
    request.identity.type = file.identity.type;
    request.identity.size = file.identity.size;
    request.identity.create_time = file.identity.create_time;
    request.identity.modify_time = file.identity.modify_time;
    request.identity.access_time = file.identity.access_time;
    desktop_trash_result_t trash_result;
    int status = desktop_trash_move(
        &desktop_trash, &request, &trash_result);
    desktop_dirty_add(
        dirty, desktop_icon_rect(display, explorer, 2U));
    if (status != DESKTOP_TRASH_OK || !trash_result.moved) {
        const char *message = status == DESKTOP_TRASH_ESTALE
            ? "Datei wurde inzwischen veraendert."
            : status == DESKTOP_TRASH_EPROTECTED
                ? "Systemdatei darf nicht verschoben werden."
                : "Datei konnte nicht in den Papierkorb verschoben werden.";
        desktop_ui_open_error(ui, display, dirty, message, file.path);
        return;
    }
    saturating_increment(&ui->trash_drop_sequence);

    refresh_directory_views(
        explorer, manager, display, dirty, source_directory);
    uint32_t refreshed = 1U;
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        if (!explorer->windows[index].active ||
            !path_equal_ascii_case(
                explorer->windows[index].path, DESKTOP_TRASH_FILES_PATH))
            continue;
        if (!refresh_explorer_snapshot(explorer, manager, dirty, index))
            refreshed = 0U;
    }
    if (!refreshed)
        desktop_ui_open_error(
            ui, display, dirty,
            "Papierkorb wurde aktualisiert; Ansicht ist veraltet.",
            trash_result.stored_path);
}

static void apply_directory_drop(
    desktop_explorer_t *explorer, desktop_wm_t *manager,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, const desktop_drag_result_t *drop) {
    if (explorer == 0 || manager == 0 || ui == 0 || display == 0 ||
        dirty == 0 || drop == 0 || !drop->accepted ||
        drop->operation != DESKTOP_DRAG_OPERATION_MOVE ||
        drop->target_id == DESKTOP_TRASH_TARGET_ID) return;
    desktop_directory_destination_t destination;
    if (!desktop_directory_destination_from_target(
            explorer, drop->target_id, drop->target_generation,
            &destination)) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Zielordner wurde inzwischen veraendert.",
            "Drop abgebrochen");
        return;
    }
    desktop_explorer_drag_file_t file;
    desktop_file_move_request_t request;
    desktop_file_move_request_initialize(&request);
    if (desktop_explorer_drag_validate(
            explorer, &drop->object, &file) != DESKTOP_EXPLORER_OK ||
        file.identity.type != X86OS_FILE ||
        desktop_explorer_drag_source_directory(
            explorer, &drop->object, request.source_directory_path,
            &request.source_directory_identity) != DESKTOP_EXPLORER_OK ||
        !desktop_copy_path(request.source_path, file.path) ||
        !desktop_copy_path(
            request.destination_directory_path, destination.path)) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Datei wurde inzwischen veraendert.", "Drop abgebrochen");
        return;
    }
    desktop_copy_bytes(
        &request.source_identity, &file.identity,
        sizeof(request.source_identity));
    desktop_copy_bytes(
        &request.destination_directory_identity, &destination.identity,
        sizeof(request.destination_directory_identity));
    desktop_file_move_result_t result;
    int status = desktop_file_move_execute(&request, &result);
    if (result.destination_published || result.source_removed ||
        status == DESKTOP_FILE_MOVE_OK) {
        refresh_directory_views(
            explorer, manager, display, dirty,
            request.source_directory_path);
        refresh_directory_views(
            explorer, manager, display, dirty,
            request.destination_directory_path);
    }
    if (status != DESKTOP_FILE_MOVE_OK ||
        !result.destination_published || !result.source_removed) {
        const char *message = status == DESKTOP_FILE_MOVE_EEXIST
            ? "Im Zielordner existiert bereits eine Datei dieses Namens."
            : status == DESKTOP_FILE_MOVE_ESTALE
                ? "Datei oder Zielordner wurde inzwischen veraendert."
                : status == DESKTOP_FILE_MOVE_ECAPACITY
                    ? "Datei ist zu gross oder Sicherheitsgrenze erreicht."
                    : status == DESKTOP_FILE_MOVE_EPARTIAL &&
                        result.duplicate_retained
                        ? "Kopie verifiziert; Quelldatei konnte nicht "
                          "entfernt werden."
                        : "Datei konnte nicht verschoben werden.";
        desktop_ui_open_error(
            ui, display, dirty, message,
            result.destination_published
                ? result.destination_path : file.path);
        return;
    }
    x86os_puts("DESKTOP_FILE_MOVE_OK source=");
    x86os_puts(file.path);
    x86os_puts(" destination=");
    x86os_puts(result.destination_path);
    x86os_putchar('\n');
    if (desktop_shortcut_probe_enabled) {
        x86os_puts("DESKTOP_DIRECTORY_RELOAD count=");
        x86os_print_number(
            (int)explorer->desktop_directory.entry_count);
        x86os_putchar('\n');
    }
    desktop_shortcut_probe_publish_geometry(
        display, manager, explorer);
}

static void apply_desktop_layout_drop(
    desktop_explorer_t *explorer, desktop_ui_state_t *ui,
    const x86os_display_info_t *display, desktop_dirty_region_t *dirty,
    const desktop_drag_result_t *drop, int32_t x, int32_t y) {
    if (explorer == 0 || ui == 0 || display == 0 || dirty == 0 ||
        drop == 0 || !drop->accepted ||
        drop->operation != DESKTOP_DRAG_OPERATION_LAYOUT ||
        drop->target_id != DESKTOP_LAYOUT_TARGET_ID ||
        drop->target_generation != desktop_layout_view.generation ||
        desktop_layout_drag_source_index >= desktop_layout_view.entry_count)
        return;
    desktop_layout_cell_t cell;
    int status = desktop_layout_drop(
        &desktop_layout_view, desktop_layout_drag_source_index, x, y, &cell);
    if (status == DESKTOP_LAYOUT_OK)
        status = desktop_layout_move_document(
            &desktop_layout_view, desktop_layout_drag_source_index,
            cell, &desktop_layout_candidate);
    if (status == DESKTOP_LAYOUT_OK)
        status = desktop_layout_store(&desktop_layout_candidate);
    if (status == DESKTOP_LAYOUT_OK)
        status = desktop_layout_rebuild(display, explorer);
    if (status != DESKTOP_LAYOUT_OK) {
        desktop_ui_open_error(
            ui, display, dirty,
            "Desktop-Anordnung konnte nicht gespeichert werden.",
            DESKTOP_LAYOUT_PATH);
        return;
    }
    desktop_dirty_full(dirty);
    x86os_puts("DESKTOP_ICON_LAYOUT_DROP_OK index=");
    x86os_print_number((int)desktop_layout_drag_source_index);
    x86os_puts(" column=");
    x86os_print_number((int)cell.column);
    x86os_puts(" row=");
    x86os_print_number((int)cell.row);
    x86os_putchar('\n');
    if (desktop_layout_probe_enabled) {
        int reload_status = desktop_layout_load(&desktop_layout_candidate);
        if (reload_status == DESKTOP_LAYOUT_OK &&
            desktop_layout_documents_equal(
                &desktop_layout_document, &desktop_layout_candidate))
            x86os_puts("DESKTOP_ICON_LAYOUT_RELOAD_OK\n");
        else {
            x86os_puts("DESKTOP_ICON_LAYOUT_PROBE_FAIL reload status=");
            x86os_print_number(reload_status);
            x86os_putchar('\n');
        }
        desktop_layout_probe_publish_geometry(explorer);
    }
}

static void apply_trash_empty(
    desktop_explorer_t *explorer, desktop_wm_t *manager,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty) {
    if (explorer == 0 || manager == 0 || ui == 0 || display == 0 ||
        dirty == 0) return;
    desktop_trash_empty_result_t result;
    desktop_trash_empty_result_initialize(&result);
    int status = desktop_trash_empty(&desktop_trash, &result);
    desktop_dirty_add(
        dirty, desktop_icon_rect(display, explorer, 2U));
    uint32_t refreshed = 1U;
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        if (!desktop_window_is_trash(explorer, index)) continue;
        if (desktop_explorer_refresh(explorer, index) !=
            DESKTOP_EXPLORER_OK) refreshed = 0U;
        desktop_dirty_add(
            dirty, desktop_wm_window_bounds(manager, index));
    }
    if (status != DESKTOP_TRASH_OK || result.incomplete) {
        desktop_ui_open_error(
            ui, display, dirty,
            result.removed_count != 0U
                ? "Papierkorb wurde nur teilweise geleert."
                : "Papierkorb konnte nicht geleert werden.",
            status == DESKTOP_TRASH_ECAPACITY
                ? "Sicherheitsgrenze erreicht; Vorgang erneut ausfuehren."
                : DESKTOP_TRASH_FILES_PATH);
    } else {
        saturating_increment(&ui->trash_empty_sequence);
        if (!refreshed)
            desktop_ui_open_error(
                ui, display, dirty,
                "Papierkorb wurde geleert; Ansicht ist veraltet.",
                DESKTOP_TRASH_FILES_PATH);
    }
}

static uint32_t desktop_ui_close_menus(desktop_ui_state_t *ui) {
    if (ui == 0) return 0U;
    uint32_t was_open =
        ui->menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->trash_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->explorer_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->shortcut_menu.open_menu != REIST_GUI_MENU_NO_INDEX ||
        ui->menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE ||
        ui->trash_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE ||
        ui->explorer_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE ||
        ui->shortcut_menu.capture_kind != REIST_GUI_MENU_CAPTURE_NONE;
    reist_gui_menu_state_initialize(&ui->menu);
    reist_gui_menu_state_initialize(&ui->trash_menu);
    reist_gui_menu_state_initialize(&ui->explorer_menu);
    reist_gui_menu_state_initialize(&ui->shortcut_menu);
    return was_open;
}

static uint32_t desktop_open_pointer_context(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, int32_t pointer_x, int32_t pointer_y,
    uint32_t *actions, uint32_t *target) {
    if (manager == 0 || explorer == 0 || ui == 0 || display == 0 ||
        dirty == 0 || actions == 0 || ui->dialog.visible ||
        manager->capture_kind != DESKTOP_WM_CAPTURE_NONE ||
        desktop_drag.phase != DESKTOP_DRAG_PHASE_IDLE)
        return 0U;
    int window = desktop_wm_window_at(manager, pointer_x, pointer_y);
    if (window >= 0 && window < (int)DESKTOP_WM_CAPACITY) {
        uint32_t window_index = (uint32_t)window;
        desktop_rect_t content = desktop_explorer_content_rect(
            manager, explorer, window_index);
        uint32_t entry_index = desktop_explorer_entry_at(
            &explorer->windows[window_index], content,
            pointer_x, pointer_y);
        desktop_drag_object_t object;
        desktop_explorer_drag_file_t file;
        if (entry_index != DESKTOP_EXPLORER_NO_ENTRY &&
            desktop_explorer_drag_object(
                explorer, window_index, entry_index, &object) ==
                DESKTOP_EXPLORER_OK &&
            desktop_explorer_drag_validate(explorer, &object, &file) ==
                DESKTOP_EXPLORER_OK && file.identity.type == X86OS_FILE &&
            !desktop_shortcut_is_filename(file.identity.name)) {
            desktop_wm_event_t select = {
                .type = DESKTOP_WM_EVENT_SELECT,
                .target = window_index,
            };
            *actions |= dispatch_desktop_event(
                manager, display, dirty, &select, target);
            explorer->windows[window_index].selected = entry_index;
            explorer->windows[window_index].pressed =
                DESKTOP_EXPLORER_NO_ENTRY;
            explorer->desktop_selected = 0U;
            control_panel_selected = 0U;
            trash_selected = 0U;
            desktop_shortcut_selected = UINT32_MAX;
            desktop_dirty_add(
                dirty, desktop_wm_window_bounds(manager, window_index));
            desktop_ui_open_explorer_context(
                ui, display, dirty, pointer_x, pointer_y, &object);
            return 1U;
        }
    } else if (window == DESKTOP_WM_NO_WINDOW &&
               pointer_y < desktop_taskbar_rect(display).y) {
        int icon = desktop_icon_at_position(
            display, explorer, pointer_x, pointer_y);
        if (icon == 2) {
            trash_selected = 1U;
            control_panel_selected = 0U;
            explorer->desktop_selected = 0U;
            desktop_shortcut_selected = UINT32_MAX;
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, explorer, 2U));
            desktop_ui_open_trash_context(
                ui, display, dirty, pointer_x, pointer_y,
                desktop_trash.full);
            return 1U;
        }
        if (icon >= (int)DESKTOP_BUILTIN_ICON_COUNT) {
            uint32_t entry_index =
                (uint32_t)icon - DESKTOP_BUILTIN_ICON_COUNT;
            if (entry_index <
                    explorer->desktop_directory.entry_count &&
                explorer->desktop_directory.entries[entry_index].type ==
                    X86OS_FILE) {
                desktop_shortcut_selected = entry_index;
                trash_selected = 0U;
                control_panel_selected = 0U;
                explorer->desktop_selected = 0U;
                desktop_dirty_add(
                    dirty, desktop_icon_rect(
                        display, explorer, (uint32_t)icon));
                desktop_ui_open_shortcut_context(
                    explorer, ui, display, dirty, pointer_x, pointer_y,
                    explorer->desktop_directory.snapshot_generation,
                    entry_index);
                return 1U;
            }
        }
    }
    if (desktop_ui_close_menus(ui)) desktop_dirty_full(dirty);
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
            if (!ui_press_consumed)
                ui_press_consumed = desktop_taskbar_pointer_button(
                    manager, ui, display, dirty, pointer_x, pointer_y,
                    1U, &actions, target);
        }
        if (!ui_press_consumed)
            ui_press_consumed = desktop_explorer_navigation_pointer_button(
                manager, explorer, ui, display, dirty,
                pointer_x, pointer_y, 1U, &actions, target);
        if (!ui_press_consumed) {
            int restore_window = desktop_wm_window_at(
                manager, pointer_x, pointer_y);
            if (restore_window >= 0 &&
                restore_window < (int32_t)DESKTOP_WM_CAPACITY &&
                point_in_rect(
                    desktop_trash_restore_rect(
                        manager, explorer, (uint32_t)restore_window),
                    pointer_x, pointer_y)) {
                desktop_wm_event_t select = {
                    .type = DESKTOP_WM_EVENT_SELECT,
                    .target = (uint32_t)restore_window,
                };
                actions |= dispatch_desktop_event(
                    manager, display, dirty, &select, target);
                trash_restore_pressed_window = (uint32_t)restore_window;
                desktop_dirty_add(
                    dirty, desktop_wm_window_bounds(
                        manager, (uint32_t)restore_window));
                ui_press_consumed = 1U;
            }
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
                int explorer_status = desktop_explorer_pointer_press(
                    explorer, (uint32_t)window,
                    desktop_explorer_content_rect(
                        manager, explorer, (uint32_t)window),
                    pointer_x, pointer_y, &explorer_result);
                collect_explorer_pointer_result(
                    display, manager, dirty, &explorer_result, 0U,
                    activation);
                if (explorer_status == DESKTOP_EXPLORER_OK &&
                    explorer_result.entry_index !=
                        DESKTOP_EXPLORER_NO_ENTRY) {
                    desktop_drag_object_t object;
                    desktop_explorer_drag_file_t file;
                    if (desktop_explorer_drag_object(
                            explorer, (uint32_t)window,
                            explorer_result.entry_index, &object) ==
                            DESKTOP_EXPLORER_OK &&
                        desktop_explorer_drag_validate(
                            explorer, &object, &file) ==
                            DESKTOP_EXPLORER_OK &&
                        file.identity.type == X86OS_FILE &&
                        desktop_file_move_source_allowed(file.path)) {
                        if (desktop_drag_arm(
                                &desktop_drag, &object,
                                pointer_x, pointer_y) == DESKTOP_DRAG_OK)
                            desktop_layout_drag_source_index = UINT32_MAX;
                    }
                }
            } else if (window == DESKTOP_WM_NO_WINDOW) {
                uint32_t hit = desktop_icon_at_position(
                    display, explorer, pointer_x, pointer_y) == 0;
                desktop_explorer_desktop_press(
                    explorer, hit, &explorer_result);
                collect_explorer_pointer_result(
                    display, manager, dirty, &explorer_result, 1U,
                    activation);
                if (hit && desktop_drag.phase == DESKTOP_DRAG_PHASE_IDLE)
                    (void)desktop_layout_arm_icon(
                        0U, DESKTOP_DRAG_OBJECT_APPLICATION,
                        pointer_x, pointer_y);
            }
        }
    } else if (!left_down && left_was_down) {
        if (explorer_navigation_pressed_window != DESKTOP_WM_NO_TARGET) {
            (void)desktop_explorer_navigation_pointer_button(
                manager, explorer, ui, display, dirty,
                pointer_x, pointer_y, 0U, &actions, target);
            return actions;
        }
        if (trash_restore_pressed_window != DESKTOP_WM_NO_TARGET) {
            uint32_t restore_window = trash_restore_pressed_window;
            trash_restore_pressed_window = DESKTOP_WM_NO_TARGET;
            if (restore_window < DESKTOP_WM_CAPACITY) {
                desktop_dirty_add(
                    dirty, desktop_wm_window_bounds(
                        manager, restore_window));
                desktop_explorer_window_t *restore_explorer =
                    &explorer->windows[restore_window];
                if (point_in_rect(
                        desktop_trash_restore_rect(
                            manager, explorer, restore_window),
                        pointer_x, pointer_y) &&
                    restore_explorer->active &&
                    restore_explorer->selected <
                        restore_explorer->entry_count &&
                    activation != 0 && !activation->valid) {
                    activation->valid = 1U;
                    activation->root = 0U;
                    activation->window_index = restore_window;
                    activation->entry_index = restore_explorer->selected;
                }
            }
            return actions;
        }
        uint32_t captured_kind = manager->capture_kind;
        int32_t captured_window = manager->capture_window;
        if (desktop_drag.phase == DESKTOP_DRAG_PHASE_DRAGGING) {
            desktop_rect_t old_feedback = desktop_drag_feedback_rect();
            desktop_rect_t old_layout_feedback = desktop_layout_hover_valid
                ? desktop_layout_cell_rect(desktop_layout_hover_cell)
                : (desktop_rect_t){0, 0, 0U, 0U};
            uint32_t source_window = desktop_drag.object.source_id;
            desktop_drag_target_t trash_target;
            desktop_drag_target_t directory_target;
            desktop_drag_target_t layout_target;
            const desktop_drag_target_t *drop_target = 0;
            desktop_layout_hover_valid = 0U;
            if (desktop_trash_drop_target(
                    manager, ui, explorer, display,
                    pointer_x, pointer_y,
                    &trash_target))
                drop_target = &trash_target;
            else if (desktop_directory_drop_target(
                    manager, explorer, ui, display,
                    pointer_x, pointer_y, &directory_target))
                drop_target = &directory_target;
            else if (desktop_layout_drop_target(
                    manager, ui, pointer_x, pointer_y, &layout_target))
                drop_target = &layout_target;
            else
                desktop_layout_hover_valid = 0U;
            uint32_t requested_operation = drop_target == &layout_target
                ? DESKTOP_DRAG_OPERATION_LAYOUT
                : DESKTOP_DRAG_OPERATION_MOVE;
            desktop_drag_result_t drop;
            desktop_drag_result_initialize(&drop);
            (void)desktop_drag_drop(
                &desktop_drag, pointer_x, pointer_y, drop_target,
                requested_operation, &drop);
            desktop_dirty_add(dirty, old_feedback);
            desktop_dirty_add(dirty, old_layout_feedback);
            desktop_dirty_add(
                dirty, desktop_icon_rect(display, explorer, 2U));
            if (source_window < DESKTOP_WM_CAPACITY)
                desktop_explorer_pointer_cancel(explorer, source_window);
            desktop_wm_event_t release = {
                .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
                .x = pointer_x,
                .y = pointer_y,
                .button = DESKTOP_WM_BUTTON_LEFT,
                .pressed = 0U,
            };
            actions |= dispatch_desktop_event(
                manager, display, dirty, &release, target);
            if (drop.operation == DESKTOP_DRAG_OPERATION_LAYOUT)
                apply_desktop_layout_drop(
                    explorer, ui, display, dirty, &drop,
                    pointer_x, pointer_y);
            else if (drop.target_id == DESKTOP_TRASH_TARGET_ID)
                apply_trash_drop(
                    explorer, manager, ui, display, dirty, &drop);
            else
                apply_directory_drop(
                    explorer, manager, ui, display, dirty, &drop);
            desktop_layout_drag_source_index = UINT32_MAX;
            desktop_layout_hover_valid = 0U;
            return actions;
        }
        if (desktop_drag.phase == DESKTOP_DRAG_PHASE_ARMED) {
            desktop_drag_cancel(&desktop_drag);
            desktop_layout_drag_source_index = UINT32_MAX;
            desktop_layout_hover_valid = 0U;
        }
        uint32_t ui_release_consumed = 0U;
        if (manager->capture_kind == DESKTOP_WM_CAPTURE_NONE) {
            desktop_ui_result_t ui_release = desktop_ui_pointer_event(
                ui, display, dirty, pointer_x, pointer_y, 1U, 0U);
            ui_release_consumed = ui_release.consumed;
            actions |= apply_desktop_ui_result(
                manager, explorer, ui, display, dirty, &ui_release, target);
            if (!ui_release_consumed)
                ui_release_consumed = desktop_taskbar_pointer_button(
                    manager, ui, display, dirty, pointer_x, pointer_y,
                    0U, &actions, target);
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
                    desktop_explorer_content_rect(
                        manager, explorer, (uint32_t)captured_window),
                    pointer_x, pointer_y, now_ms, &explorer_result);
                collect_explorer_pointer_result(
                    display, manager, dirty, &explorer_result, 0U,
                    activation);
            } else if (captured_kind == DESKTOP_WM_CAPTURE_NONE) {
                uint32_t hit = desktop_icon_at_position(
                    display, explorer, pointer_x, pointer_y) == 0;
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
    uint32_t help_index = 0U;
    const uint32_t item_count = sizeof(start_menu_items) /
                                sizeof(start_menu_items[0]);
    for (; help_index < item_count; ++help_index)
        if (start_menu_items[help_index].action == DESKTOP_MENU_ACTION_HELP)
            break;
    if (help_index == item_count || reist_gui_menu_validate(
            &desktop_menu_model, &layout, &ui->menu) != 0 ||
        reist_gui_menu_title_rect(
            &desktop_menu_model, &layout, DESKTOP_MENU_START,
            &help_title) != 0 ||
        reist_gui_menu_item_rect(
            &desktop_menu_model, &layout, DESKTOP_MENU_START, help_index,
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
    if (!result.consumed || ui->menu.open_menu != DESKTOP_MENU_START)
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
    const int32_t initial_window_x = window->x;
    const int32_t initial_window_y = window->y;
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
    const int32_t anchor_x = *pointer_x - initial_window_x;
    const int32_t anchor_y = *pointer_y - initial_window_y;
    if (manager->capture_kind != DESKTOP_WM_CAPTURE_MOVE ||
        window->x != initial_window_x || window->y != initial_window_y)
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
        if (*pointer_x - window->x != anchor_x ||
            *pointer_y - window->y != anchor_y)
            render_probe_error(metrics);
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

/* Documentation probes run only in the QEMU runner's immutable -snapshot
 * guest.  They still build their visible state through the production trash
 * adapter, so the captured catalog entry is restorable rather than painted
 * test data. */
static int prepare_trash_documentation_probe(
    desktop_wm_t *manager, desktop_explorer_t *explorer,
    desktop_ui_state_t *ui, const x86os_display_info_t *display,
    desktop_dirty_region_t *dirty, uint32_t show_confirmation,
    uint32_t restore_immediately, uint32_t *target) {
    static const char sample_path[] = "/trash-demo.txt";
    static const char sample_text[] =
        "REIST Workspace Papierkorb-Dokumentationsprobe\n";
    x86os_file_info_t existing;
    if (desktop_trash.available == 0U ||
        x86os_stat(sample_path, &existing) != DESKTOP_TRASH_ENOENT)
        return -1;

    int descriptor = x86os_create(sample_path);
    if (descriptor < 0) return -1;
    uint32_t written = 0U;
    while (written + 1U < sizeof(sample_text)) {
        int amount = x86os_write(
            descriptor, sample_text + written,
            sizeof(sample_text) - 1U - written);
        if (amount <= 0 ||
            (uint32_t)amount > sizeof(sample_text) - 1U - written) {
            (void)x86os_close(descriptor);
            (void)x86os_unlink(sample_path);
            return -1;
        }
        written += (uint32_t)amount;
    }
    int sync_status = x86os_fsync(descriptor);
    int close_status = x86os_close(descriptor);
    if (sync_status != 0 || close_status != 0) {
        (void)x86os_unlink(sample_path);
        return -1;
    }

    desktop_trash_request_t request;
    desktop_trash_request_initialize(&request);
    for (uint32_t index = 0U; index < sizeof(sample_path); ++index)
        request.source_path[index] = sample_path[index];
    if (x86os_stat(sample_path, &request.identity) != 0) {
        (void)x86os_unlink(sample_path);
        return -1;
    }
    desktop_trash_result_t result;
    desktop_trash_result_initialize(&result);
    if (desktop_trash_move(&desktop_trash, &request, &result) !=
            DESKTOP_TRASH_OK ||
        result.moved == 0U)
        return -1;

    if (restore_immediately) {
        desktop_trash_restore_request_t restore_request;
        desktop_trash_restore_request_initialize(&restore_request);
        uint32_t index = 0U;
        while (index + 1U < sizeof(restore_request.catalog_path) &&
               result.catalog_path[index] != '\0') {
            restore_request.catalog_path[index] = result.catalog_path[index];
            ++index;
        }
        if (result.catalog_path[index] != '\0' ||
            x86os_stat(result.catalog_path, &restore_request.identity) != 0)
            return -1;
        desktop_trash_restore_result_t restore_result;
        desktop_trash_restore_result_initialize(&restore_result);
        if (desktop_trash_restore(
                &desktop_trash, &restore_request, &restore_result) !=
                DESKTOP_TRASH_OK || restore_result.restored == 0U ||
            restore_result.cleanup_complete == 0U ||
            x86os_stat(sample_path, &existing) != 0 ||
            existing.type != X86OS_FILE)
            return -1;
        x86os_puts("DESKTOP_TRASH_RESTORE_READY\n");
        return 0;
    }

    uint32_t trash_window = desktop_explorer_free_window(explorer);
    if (trash_window >= DESKTOP_WM_CAPACITY) return -1;
    (void)open_explorer_path(
        manager, explorer, ui, display, dirty,
        DESKTOP_TRASH_FILES_PATH, target);
    if (!explorer->windows[trash_window].active ||
        explorer->windows[trash_window].entry_count == 0U)
        return -1;
    explorer->windows[trash_window].selected = 0U;
    explorer->desktop_selected = 0U;
    control_panel_selected = 0U;
    trash_selected = 1U;
    if (show_confirmation) {
        desktop_ui_open_dialog(
            ui, display, dirty, DESKTOP_DIALOG_EMPTY_TRASH);
    } else {
        desktop_ui_open_trash_context(
            ui, display, dirty,
            (int32_t)(display->width * 3U / 4U),
            (int32_t)(display->height / 3U), 1U);
    }
    desktop_dirty_full(dirty);
    return 0;
}

static int desktop_lifecycle_publish_progress(
        uint32_t supervised, uint32_t *sequence, uint64_t *heartbeat_ms) {
    if (supervised == 0U) return 0;
    if (sequence == 0 || heartbeat_ms == 0 || *sequence == 0U) return -22;
    uint64_t now_ms = 0U;
    if (x86os_monotonic_ms(&now_ms) != 0 ||
        x86os_reist_report(
            X86OS_REIST_REPORT_PROGRESS, *sequence) != 0)
        return -1;
    if (*sequence != UINT32_MAX) ++*sequence;
    *heartbeat_ms = now_ms;
    return 0;
}

int main(int argc, char **argv) {
    x86os_display_info_t display;
    desktop_wm_t manager;
    static desktop_surface_manager_t surfaces;
    desktop_surface_runtime_t surface_runtime;
    static desktop_explorer_t explorer;
    static desktop_filetypes_t filetypes;
    static desktop_system_sound_state_t system_sounds;
    desktop_ui_state_t ui;
    desktop_render_metrics_t metrics = {0};
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t previous_buttons = 0U;
    unsigned int runtime_activated = 0U;
    uint32_t render_probe = 0U;
    uint32_t hover_probe = 0U;
    uint32_t surface_probe = 0U;
    uint32_t notepad_probe = 0U;
    uint32_t notepad_font_probe = 0U;
    uint32_t control_probe = 0U;
    uint32_t browser_probe = 0U;
    uint32_t guidemo_probe = 0U;
    uint32_t sound_probe = 0U;
    uint32_t trash_context_probe = 0U;
    uint32_t trash_confirm_probe = 0U;
    uint32_t trash_restore_probe = 0U;
    uint32_t explorer_scroll_probe = 0U;
    uint32_t explorer_views_probe = 0U;
    uint32_t shortcut_probe = 0U;
    uint32_t icon_layout_probe = 0U;
    uint32_t unicode_probe = 0U;
    uint32_t surface_probe_reported = 0U;
    uint32_t surface_probe_created_reported = 0U;
    uint32_t surface_resize_requested = 0U;
    uint32_t sound_probe_reported = 0U;
    uint64_t sound_probe_started_ms = 0U;
    uint32_t pointer_present_pending = 0U;
    uint64_t pointer_pending_since_ms = 0U;
    uint32_t pointer_pending_clock_valid = 0U;
    uint32_t pointer_overlay_active = 0U;
    desktop_hover_probe_t hover_probe_state;
    uint32_t hover_menu_ready_reported = 0U;
    uint32_t hover_menu_ready_pending = 0U;
    uint64_t startup_started_ms = 0U;
    uint32_t startup_clock_valid =
        x86os_monotonic_ms(&startup_started_ms) == 0;
    uint32_t online_cpu_count = 1U;
    x86os_cpu_topology_t topology;
    if (x86os_cpu_topology(&topology) == 0 &&
        topology.version == X86OS_CPU_TOPOLOGY_VERSION &&
        topology.struct_size == sizeof(topology) &&
        topology.online_cpu_count > 0U &&
        topology.online_cpu_count <= X86OS_CPU_TOPOLOGY_MAX_CPUS)
        online_cpu_count = topology.online_cpu_count;

    if (argc == 2 && argv != 0 && text_equal(argv[1], "--render-probe")) {
        render_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--hover-probe")) {
        hover_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--surface-probe")) {
        surface_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--notepad-probe")) {
        notepad_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--notepad-font-probe")) {
        notepad_font_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--control-probe")) {
        control_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--browser-probe")) {
        browser_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--guidemo-probe")) {
        guidemo_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--sound-probe")) {
        sound_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--trash-context-probe")) {
        trash_context_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--trash-confirm-probe")) {
        trash_confirm_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--trash-restore-probe")) {
        trash_restore_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--unicode-probe")) {
        unicode_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--explorer-scroll-probe")) {
        explorer_scroll_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--explorer-views-probe")) {
        explorer_views_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--shortcut-probe")) {
        shortcut_probe = 1U;
    } else if (argc == 2 && argv != 0 &&
               text_equal(argv[1], "--icon-layout-probe")) {
        icon_layout_probe = 1U;
    } else if (argc != 1) {
        x86os_puts(
            "Usage: desktop [--render-probe|--hover-probe|--surface-probe|"
            "--notepad-probe|--notepad-font-probe|--control-probe|"
            "--browser-probe|"
            "--guidemo-probe|--sound-probe|"
            "--trash-context-probe|"
            "--trash-confirm-probe|--trash-restore-probe|--unicode-probe|"
            "--explorer-scroll-probe|--explorer-views-probe|"
            "--shortcut-probe|--icon-layout-probe]\n");
        return 2;
    }
    desktop_shortcut_probe_enabled = shortcut_probe;
    desktop_shortcut_probe_restart_pid = 0;
    desktop_shortcut_probe_restart_generation = 0U;
    desktop_shortcut_probe_restart_phase =
        DESKTOP_SHORTCUT_PROBE_RESTART_IDLE;
    desktop_shortcut_probe_restart_deadline_ms = 0U;
    desktop_layout_probe_enabled = icon_layout_probe;
    hover_probe_initialize(&hover_probe_state, hover_probe);

    int activation_status = desktop_activate_with_fallback();
    if (activation_status == 0) runtime_activated = 1U;
    int display_status = x86os_display_info(&display);
    if (activation_status != 0 || display_status != 0 ||
        display.version != X86OS_DISPLAY_ABI_VERSION ||
        display.struct_size < sizeof(display) ||
        display.width < 320U || display.height < 240U ||
        display.font_width == 0U || display.font_height == 0U) {
        x86os_puts("desktop: Grafikmodus nicht verfuegbar\n");
        return 1;
    }
    /* Diagnostic modes are timing probes, not interactive desktop sessions;
     * keep their established runtime envelope free of presentation I/O. */
    uint64_t phase_started_ms = 0U;
    (void)x86os_monotonic_ms(&phase_started_ms);
    if (argc != 1) desktop_startup_phase_metric("splash", phase_started_ms);
    (void)desktop_svga2d_connect(0U, 0U);
    desktop_startup_phase_metric("accel-info", phase_started_ms);
    (void)x86os_monotonic_ms(&phase_started_ms);
    int font_status = unicode_probe ? desktop_font_load(&display) : 0;
    desktop_startup_phase_metric("font", phase_started_ms);
    if (unicode_probe) {
        static const char valid[] =
            "A\xC3\x84\xE2\x82\xAC"
            "\xD0\x96\xD7\x90\xD8\xA7"
            "\xE0\xA4\x95\xE4\xB8\xAD\xEA\xB0\x80"
            "\xF0\x9F\x9A\x80";
        static const char malformed[] = "\xF0\x28\x8C\x28";
        int probe_activate_status = desktop_svga2d_activate_until_ready(
            DESKTOP_SVGA2D_PROBE_READY_DEADLINE_MS);
        if (probe_activate_status == 0) runtime_activated = 1U;
        int valid_status = probe_activate_status == 0
            ? x86os_draw_text_pixels(
                (int32_t)(display.width - display.font_width), 0,
                valid, sizeof(valid) - 1U, 0x00FFFFFFU, 0x00000000U)
            : probe_activate_status;
        desktop_render_context_t probe_context = {
            .display = &display,
            .clip = {0, 0, display.width, display.height},
        };
        int font_overlay_status = probe_activate_status == 0
            ? desktop_font_overlay_extensions(
                &probe_context, 0, 0, valid, sizeof(valid) - 1U,
                0x00FFFFFFU, 0x00000000U)
            : probe_activate_status;
        uint32_t fallback_glyph = 0U;
        int fallback_lookup_status = font_status == 0
            ? reist_gui_font_lookup(
                &desktop_font, 0x10FFFDU, &fallback_glyph)
            : font_status;
        int malformed_status = probe_activate_status == 0
            ? x86os_draw_text_pixels(
                0, 0, malformed, sizeof(malformed) - 1U,
                0x00FFFFFFU, 0x00000000U)
            : probe_activate_status;
        int acceleration_copy_status = 0;
        if (probe_activate_status == 0 &&
            (desktop_svga2d_capabilities & REIST_SVGA2D_CAP_RECT_COPY) != 0U) {
            /* Exercise the negotiated command path without depending on a
             * synthetic drag gesture.  A one-pixel copy is bounded, visible
             * only for this disposable probe frame, and proves FIFO command
             * submission before the driver is deactivated. */
            acceleration_copy_status = desktop_svga2d_rect_copy(
                0U, 0U, 1U, 1U, 1U, 1U);
        }
        desktop_explorer_initialize(&explorer);
        int explorer_status = desktop_explorer_open(&explorer, 0U, "/");
        uint32_t explorer_htdocs = 0U;
        if (explorer_status == DESKTOP_EXPLORER_OK) {
            const desktop_explorer_window_t *root = &explorer.windows[0];
            for (uint32_t index = 0U; index < root->entry_count; ++index) {
                if (root->entries[index].type == X86OS_DIRECTORY &&
                    text_equal(root->entries[index].name, "htdocs"))
                    explorer_htdocs = 1U;
            }
        }
        int deactivate_status = runtime_activated
            ? desktop_display_deactivate() : 0;
        if (valid_status != (int)(sizeof(valid) - 1U) || font_status != 0 ||
            font_overlay_status != 8 || fallback_lookup_status != 0 ||
            fallback_glyph != desktop_font.fallback_glyph ||
            malformed_status != -22 || explorer_status != 0 ||
            explorer.windows[0].active == 0U || explorer_htdocs == 0U ||
            acceleration_copy_status != 0 ||
            deactivate_status != 0) {
            x86os_puts("DESKTOP_UNICODE_FAIL valid=");
            x86os_print_number(valid_status);
            x86os_puts(" malformed=");
            x86os_print_number(malformed_status);
            x86os_puts(" font=");
            x86os_print_number(font_status);
            x86os_puts(" fallback=");
            x86os_print_number(fallback_lookup_status);
            x86os_puts(" overlay=");
            x86os_print_number(font_overlay_status);
            x86os_puts(" explorer=");
            x86os_print_number(explorer_status);
            x86os_puts(" activate=");
            x86os_print_number(probe_activate_status);
            x86os_puts(" copy=");
            x86os_print_number(acceleration_copy_status);
            x86os_puts(" deactivate=");
            x86os_print_number(deactivate_status);
            x86os_putchar('\n');
            return 1;
        }
        x86os_puts("DESKTOP_FONT_BMP_OK\n");
        x86os_puts("DESKTOP_FONT_SUPPLEMENTARY_OK\n");
        x86os_puts("DESKTOP_FONT_FALLBACK_OK\n");
        x86os_puts("DESKTOP_EXPLORER_VFS_OK\n");
        x86os_puts("DESKTOP_UNICODE_OK\n");
        return 0;
    }

    uint32_t taskbar = taskbar_height(&display);
    desktop_ui_initialize(&ui);
    reist_gui_menu_layout_t menu_layout = desktop_menu_layout(&display);
    reist_gui_dialog_layout_t dialog_layout =
        desktop_dialog_layout(&display);
    reist_gui_menu_layout_t trash_menu_layout =
        trash_context_layout(&ui, &display);
    reist_gui_menu_layout_t explorer_menu_layout =
        explorer_context_layout(&ui, &display);
    reist_gui_menu_layout_t shortcut_menu_layout =
        shortcut_context_layout(&ui, &display);
    if (reist_gui_menu_validate(
            &desktop_menu_model, &menu_layout, &ui.menu) != 0 ||
        reist_gui_menu_validate(
            trash_context_model(&ui), &trash_menu_layout,
            &ui.trash_menu) != 0 ||
        reist_gui_menu_validate(
            &explorer_context_menu_model, &explorer_menu_layout,
            &ui.explorer_menu) != 0 ||
        reist_gui_menu_validate(
            &shortcut_context_menu_model, &shortcut_menu_layout,
            &ui.shortcut_menu) != 0 ||
        reist_gui_dialog_validate(
            &help_dialog_model, &dialog_layout, &ui.dialog) != 0 ||
        reist_gui_dialog_validate(
            &about_dialog_model, &dialog_layout, &ui.dialog) != 0 ||
        reist_gui_dialog_validate(
            &empty_trash_dialog_model, &dialog_layout, &ui.dialog) != 0 ||
        reist_gui_dialog_validate(
            &ui.error_model, &dialog_layout, &ui.dialog) != 0) {
        if (runtime_activated) (void)desktop_display_deactivate();
        x86os_puts("desktop: GUI-API/Layout nicht kompatibel\n");
        return 1;
    }
    desktop_wm_initialize(&manager, display.width, display.height,
                          4,
                          (int32_t)(display.height - taskbar - 4U),
                          max_u32(display.font_height + 8U, 24U));
    desktop_surface_initialize(&surfaces);
    if (desktop_surface_runtime_initialize(&surface_runtime) != 0) {
        if (runtime_activated) (void)desktop_display_deactivate();
        x86os_puts("desktop: Surface-IPC konnte nicht gestartet werden\n");
        return 1;
    }
    int lifecycle_self_test = x86os_reist_report(
        X86OS_REIST_REPORT_SELF_TEST, 1U);
    uint32_t lifecycle_supervised = lifecycle_self_test == 0;
    uint32_t lifecycle_sequence = 1U;
    uint64_t lifecycle_heartbeat_ms = 0U;
    if ((lifecycle_self_test != 0 && lifecycle_self_test != -1) ||
        desktop_lifecycle_publish_progress(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        x86os_puts("desktop: Supervisor-Lifecycle nicht verfuegbar\n");
        return 1;
    }
    if (surface_probe || notepad_probe || notepad_font_probe || browser_probe ||
        shortcut_probe || icon_layout_probe || argc == 1) {
        (void)x86os_monotonic_ms(&phase_started_ms);
        font_status = desktop_editor_font_catalog_load(&display,
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms);
        desktop_startup_phase_metric("font-catalog", phase_started_ms);
    }
    /* SERVICE_READY remains withheld until all fixed startup work and the
     * first complete desktop frame have finished on the BSP.  Keep the
     * existing two-second healthy deadline alive between bounded strips and
     * asset reads instead of widening it for a slow virtual display. */
    if (argc == 1) {
        (void)x86os_monotonic_ms(&phase_started_ms);
        if (desktop_splash_show(
                &display, lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms) != 0) {
            desktop_surface_runtime_shutdown(&surface_runtime);
            if (runtime_activated) (void)desktop_display_deactivate();
            return 1;
        }
        desktop_startup_phase_metric("splash", phase_started_ms);
    }
    desktop_explorer_initialize(&explorer);
    desktop_drag_state_initialize(&desktop_drag);
    desktop_layout_document_initialize(&desktop_layout_document);
    desktop_layout_view_initialize(&desktop_layout_view);
    desktop_layout_drag_source_index = UINT32_MAX;
    desktop_layout_hover_valid = 0U;
    desktop_trash_state_initialize(&desktop_trash);
    int trash_status = desktop_trash_prepare(&desktop_trash);
    if (desktop_lifecycle_publish_progress(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        return 1;
    }
    desktop_shortcut_selection_reset();
    x86os_file_info_t desktop_directory_identity;
    int shortcut_status =
        desktop_shortcut_prepare_directory(&desktop_directory_identity);
    if (shortcut_status == DESKTOP_SHORTCUT_OK &&
        desktop_explorer_desktop_open(
            &explorer, DESKTOP_SHORTCUT_DIRECTORY) !=
            DESKTOP_EXPLORER_OK)
        shortcut_status = DESKTOP_SHORTCUT_EIO;
    if (shortcut_status == DESKTOP_SHORTCUT_OK && icon_layout_probe &&
        desktop_layout_probe_prepare_shortcut(
            &explorer, &desktop_directory_identity) != DESKTOP_LAYOUT_OK)
        shortcut_status = DESKTOP_SHORTCUT_EIO;
    int desktop_layout_status = desktop_layout_load(
        &desktop_layout_document);
    if (desktop_layout_status != DESKTOP_LAYOUT_OK)
        desktop_layout_document_initialize(&desktop_layout_document);
    int desktop_layout_view_status = desktop_layout_rebuild(
        &display, &explorer);
    if (desktop_layout_status != DESKTOP_LAYOUT_OK)
        x86os_puts(
            "desktop: Desktop-Anordnung fehlt oder ist ungueltig; "
            "Standardanordnung aktiv\n");
    if (desktop_layout_view_status != DESKTOP_LAYOUT_OK) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        x86os_puts("desktop: Desktop-Anordnung konnte nicht berechnet werden\n");
        return 1;
    }
    if (desktop_lifecycle_publish_progress(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        return 1;
    }
    /* Optional assets are read and decoded exactly once before the first
     * frame. Missing or malformed files leave fixed-cost vector fallbacks. */
    (void)x86os_monotonic_ms(&phase_started_ms);
    if (desktop_file_icon_cache_initialize(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        return 1;
    }
    desktop_startup_phase_metric("icons", phase_started_ms);
    (void)x86os_monotonic_ms(&phase_started_ms);
    int filetypes_status = load_filetypes(&filetypes);
    desktop_startup_phase_metric("filetypes", phase_started_ms);
    if (desktop_lifecycle_publish_progress(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        return 1;
    }
    (void)x86os_monotonic_ms(&phase_started_ms);
    int system_sound_status = load_system_sounds(&system_sounds);
    desktop_startup_phase_metric("sounds", phase_started_ms);
    if (render_probe || hover_probe || surface_probe || notepad_probe ||
        notepad_font_probe ||
        control_probe || browser_probe ||
        guidemo_probe || sound_probe || trash_context_probe ||
        trash_confirm_probe || trash_restore_probe ||
        unicode_probe || explorer_scroll_probe || explorer_views_probe ||
        shortcut_probe || icon_layout_probe)
        system_sounds.enabled = 0U;
    if (system_sound_status != 0)
        x86os_puts("desktop: Systemklangkonfiguration ungueltig\n");
    if (desktop_lifecycle_publish_progress(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        return 1;
    }
    pointer_x = (int32_t)(display.width / 2U);
    pointer_y = (int32_t)(display.height / 2U);
    desktop_dirty_region_t initial_dirty;
    desktop_dirty_initialize(&initial_dirty, display.width, display.height);
    uint32_t initial_target = DESKTOP_WM_NO_TARGET;
    if (!icon_layout_probe && open_explorer_path(
            &manager, &explorer, &ui, &display,
            &initial_dirty,
            (explorer_scroll_probe || explorer_views_probe)
                ? "/usr/share/fonts"
                : shortcut_probe ? "/htdocs" : "/",
            &initial_target) != 0U)
        x86os_puts("DESKTOP_EXPLORER_OK\n");
    if (shortcut_probe)
        x86os_puts("DESKTOP_SHORTCUT_PROBE_EXPLORERS_OK\n");
    if (filetypes_status != 0)
        desktop_ui_open_error(
            &ui, &display, &initial_dirty,
            "Dateizuordnungen sind ungueltig.",
            "/etc/reist/filetypes.conf");
    if (trash_status != DESKTOP_TRASH_OK)
        desktop_ui_open_error(
            &ui, &display, &initial_dirty,
            "Papierkorb ist nicht verfuegbar.", DESKTOP_TRASH_ROOT_PATH);
    if (shortcut_status != DESKTOP_SHORTCUT_OK)
        desktop_ui_open_error(
            &ui, &display, &initial_dirty,
            "Desktop-Verzeichnis ist nicht verfuegbar.",
            DESKTOP_SHORTCUT_DIRECTORY);
    if ((trash_context_probe || trash_confirm_probe || trash_restore_probe) &&
        prepare_trash_documentation_probe(
            &manager, &explorer, &ui, &display, &initial_dirty,
            trash_confirm_probe, trash_restore_probe, &initial_target) != 0) {
        x86os_puts("DESKTOP_TRASH_PROBE_FAIL setup\n");
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        return 1;
    }
    if (desktop_lifecycle_publish_progress(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        return 1;
    }
    desktop_clock_refresh(&display, &initial_dirty, 1U);
    desktop_dirty_full(&initial_dirty);
    uint32_t initial_render_outcome = render_desktop_measured(
        &display, &manager, &explorer, &surfaces, &ui,
        &initial_dirty, 0, 0U, 0U, &metrics);
    pointer_overlay_active =
        (initial_render_outcome & DESKTOP_RENDER_FALLBACK) == 0U;
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
    if (desktop_lifecycle_publish_progress(
            lifecycle_supervised, &lifecycle_sequence,
            &lifecycle_heartbeat_ms) != 0 ||
        (lifecycle_supervised != 0U &&
         x86os_reist_report(
             X86OS_REIST_REPORT_SERVICE_READY, 1U) != 0)) {
        desktop_surface_runtime_shutdown(&surface_runtime);
        if (runtime_activated) (void)desktop_display_deactivate();
        x86os_puts("desktop: Supervisor-Lifecycle nicht verfuegbar\n");
        return 1;
    }
    uint64_t startup_ready_ms = 0U;
    if (startup_clock_valid &&
        x86os_monotonic_ms(&startup_ready_ms) == 0 &&
        startup_ready_ms >= startup_started_ms &&
        startup_ready_ms - startup_started_ms <= INT32_MAX) {
        x86os_puts("DESKTOP_STARTUP_MS value=");
        x86os_print_number((int)(startup_ready_ms - startup_started_ms));
        x86os_putchar('\n');
    }
    x86os_puts("DESKTOP_OK\n");
    if (shortcut_status == DESKTOP_SHORTCUT_OK) {
        x86os_puts("DESKTOP_SHORTCUTS_READY count=");
        x86os_print_number(
            (int)explorer.desktop_directory.entry_count);
        x86os_putchar('\n');
    }
    desktop_shortcut_probe_publish_geometry(
        &display, &manager, &explorer);
    if (icon_layout_probe) {
        if (desktop_layout_status == DESKTOP_LAYOUT_OK)
            x86os_puts("DESKTOP_ICON_LAYOUT_RELOAD_OK\n");
        if (desktop_layout_probe_resize() != DESKTOP_LAYOUT_OK)
            x86os_puts("DESKTOP_ICON_LAYOUT_PROBE_FAIL resize\n");
        desktop_layout_probe_publish_geometry(&explorer);
    }
    if (explorer_scroll_probe) {
        int probe_status = desktop_explorer_scroll_probe_run(
            &manager, &explorer, &display);
        if (probe_status == 0) {
            desktop_dirty_region_t probe_dirty;
            desktop_dirty_initialize(
                &probe_dirty, display.width, display.height);
            desktop_dirty_full(&probe_dirty);
            (void)render_desktop_frame(
                &display, &manager, &explorer, &surfaces, &ui,
                &probe_dirty);
            x86os_puts("DESKTOP_EXPLORER_SCROLL_OK\n");
        } else {
            x86os_puts("DESKTOP_EXPLORER_SCROLL_FAIL status=");
            x86os_print_number(probe_status);
            x86os_putchar('\n');
        }
        uint32_t exited = desktop_try_exit(
            pointer_x, pointer_y, runtime_activated, &metrics);
        desktop_surface_runtime_shutdown(&surface_runtime);
        return probe_status == 0 && exited ? 0 : 1;
    }
    if (explorer_views_probe) {
        int probe_status = desktop_explorer_views_probe_run(
            &manager, &explorer, &display);
        if (probe_status == 0) {
            desktop_dirty_region_t probe_dirty;
            desktop_dirty_initialize(
                &probe_dirty, display.width, display.height);
            desktop_dirty_full(&probe_dirty);
            (void)render_desktop_frame(
                &display, &manager, &explorer, &surfaces, &ui,
                &probe_dirty);
            x86os_puts("DESKTOP_EXPLORER_VIEWS_OK\n");
        } else {
            x86os_puts("DESKTOP_EXPLORER_VIEWS_FAIL status=");
            x86os_print_number(probe_status);
            x86os_putchar('\n');
        }
        uint32_t exited = desktop_try_exit(
            pointer_x, pointer_y, runtime_activated, &metrics);
        desktop_surface_runtime_shutdown(&surface_runtime);
        return probe_status == 0 && exited ? 0 : 1;
    }
    if (hover_probe) x86os_puts("DESKTOP_HOVER_PROBE_READY\n");
    desktop_system_sound_request(
        &system_sounds, ui.error_sequence != 0U
            ? DESKTOP_SYSTEM_SOUND_ERROR
            : DESKTOP_SYSTEM_SOUND_STARTUP);
    if (trash_context_probe)
        x86os_puts("DESKTOP_TRASH_CONTEXT_READY\n");
    if (trash_confirm_probe)
        x86os_puts("DESKTOP_TRASH_CONFIRM_READY\n");
    if (surface_probe || notepad_probe || notepad_font_probe ||
        control_probe || browser_probe || guidemo_probe || sound_probe) {
        /* Each client spawn is bounded, but two consecutive image loads can
         * legitimately cross one heartbeat interval.  Reset the deadline at
         * the phase boundary and, for the audio probe, between both clients. */
        if (desktop_lifecycle_publish_progress(
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms) != 0) {
            desktop_surface_runtime_shutdown(&surface_runtime);
            if (runtime_activated) (void)desktop_display_deactivate();
            return 1;
        }
        int probe_status;
        if (sound_probe) {
            probe_status = launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/SOUNDPLAYER.PRG",
                "/USR/SHARE/SOUNDS/STARTUP.WAV",
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms);
        } else if (guidemo_probe) {
            probe_status = launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/GUIDEMO.PRG", "--interaction-probe",
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms);
        } else if (control_probe) {
            probe_status = launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/CONTROL.PRG", 0,
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms);
        } else if (browser_probe) {
            probe_status = launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/BROWSER.PRG", "--browser-probe",
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms);
        } else if (notepad_probe || notepad_font_probe) {
            probe_status = launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/NOTEPAD.PRG",
                notepad_font_probe ? "--font-probe" :
                    "--large-document-probe",
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms);
        } else {
            probe_status = launch_surface_probe_client(
                &surface_runtime, &surfaces,
                "/USR/GUI/BIN/NOTEPAD.PRG", "/README.TXT",
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms);
        }
        if (sound_probe && probe_status == 0) {
            x86os_puts("DESKTOP_AUDIO_STAGE sound-bound\n");
            probe_status = desktop_lifecycle_publish_progress(
                lifecycle_supervised, &lifecycle_sequence,
                &lifecycle_heartbeat_ms);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/GUIDEMO.PRG", "--interaction-probe",
                    lifecycle_supervised, &lifecycle_sequence,
                    &lifecycle_heartbeat_ms);
            if (probe_status == 0)
                probe_status = enqueue_guidemo_interaction_probe(&surfaces);
        }
        if (surface_probe) {
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/NOTEPAD.PRG", "--menu-probe",
                    lifecycle_supervised, &lifecycle_sequence,
                    &lifecycle_heartbeat_ms);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/NOTEPAD.PRG", "--file-dialog-probe",
                    lifecycle_supervised, &lifecycle_sequence,
                    &lifecycle_heartbeat_ms);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/NOTEPAD.PRG", "--hover-probe",
                    lifecycle_supervised, &lifecycle_sequence,
                    &lifecycle_heartbeat_ms);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/NOTEPAD.PRG", "--dialog-probe",
                    lifecycle_supervised, &lifecycle_sequence,
                    &lifecycle_heartbeat_ms);
            if (probe_status == 0)
                probe_status = launch_surface_probe_client(
                    &surface_runtime, &surfaces,
                    "/USR/GUI/BIN/IMAGEVIEWER.PRG",
                    "/USR/SHARE/IMAGES/DEMO-COLORS.GIF",
                    lifecycle_supervised, &lifecycle_sequence,
                    &lifecycle_heartbeat_ms);
        }
        if (probe_status != 0) {
            x86os_puts(sound_probe
                ? "DESKTOP_AUDIO_FAIL launch status="
                : guidemo_probe
                ? "DESKTOP_GUIDEMO_FAIL launch status="
                : control_probe
                ? "DESKTOP_CONTROL_FAIL launch status="
                : browser_probe
                ? "DESKTOP_BROWSER_FAIL launch status="
                : (surface_probe
                    ? "DESKTOP_SURFACE_FAIL launch status="
                    : "DESKTOP_NOTEPAD_FAIL launch status="));
            x86os_print_number(probe_status);
            x86os_putchar('\n');
            desktop_surface_runtime_shutdown(&surface_runtime);
            if (runtime_activated) (void)desktop_display_deactivate();
            return 1;
        }
        if (sound_probe) (void)x86os_monotonic_ms(&sound_probe_started_ms);
        x86os_puts(sound_probe
            ? "DESKTOP_AUDIO_STAGE client-bound\n"
            : guidemo_probe
            ? "DESKTOP_GUIDEMO_STAGE client-bound\n"
            : control_probe
            ? "DESKTOP_CONTROL_STAGE client-bound\n"
            : browser_probe
            ? "DESKTOP_BROWSER_STAGE client-bound\n"
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
        desktop_system_sound_poll(&system_sounds);
        uint64_t lifecycle_now_ms = 0U;
        int lifecycle_clock_status = x86os_monotonic_ms(&lifecycle_now_ms);
        if (lifecycle_supervised &&
            (lifecycle_clock_status != 0 ||
            lifecycle_now_ms < lifecycle_heartbeat_ms ||
            lifecycle_now_ms - lifecycle_heartbeat_ms >= 500U)) {
            if (x86os_reist_report(
                    X86OS_REIST_REPORT_PROGRESS, lifecycle_sequence) != 0) {
                desktop_surface_runtime_shutdown(&surface_runtime);
                if (runtime_activated) (void)desktop_display_deactivate();
                return 1;
            }
            if (lifecycle_sequence != UINT32_MAX) ++lifecycle_sequence;
            lifecycle_heartbeat_ms = lifecycle_now_ms;
        }
        /* The paired runtime also requires SOUNDPLAYER_PLAYBACK_OK from the
         * client.  Crossing the supervisor's two-second deadline here proves
         * that delegated playback never blocked compositor progress. */
        if (sound_probe && !sound_probe_reported &&
            lifecycle_clock_status == 0 &&
            sound_probe_started_ms != 0U &&
            lifecycle_now_ms >= sound_probe_started_ms &&
            lifecycle_now_ms - sound_probe_started_ms >= 2500U &&
            active_surface_count(&surfaces) != 0U) {
            x86os_puts("DESKTOP_AUDIO_HEARTBEAT_OK\n");
            sound_probe_reported = 1U;
        }
        int surface_poll_status = desktop_surface_runtime_poll(
            &surface_runtime, &surfaces);
        if ((surface_probe || sound_probe || notepad_probe ||
             notepad_font_probe || browser_probe) && surface_poll_status != 0) {
            x86os_puts(sound_probe
                ? "DESKTOP_AUDIO_FAIL protocol status="
                : (notepad_probe || notepad_font_probe)
                ? "DESKTOP_NOTEPAD_FAIL protocol status="
                : browser_probe
                ? "DESKTOP_BROWSER_FAIL protocol status="
                : "DESKTOP_SURFACE_FAIL protocol status=");
            x86os_print_number(surface_poll_status);
            x86os_putchar('\n');
            desktop_surface_runtime_shutdown(&surface_runtime);
            if (runtime_activated) (void)desktop_display_deactivate();
            return 1;
        }
        /* Driver construction is independent of desktop presentation. Poll
         * its supervised generation at the helper's monotonic one-second
         * bound so readiness is adopted even without a later drag gesture. */
        (void)desktop_svga2d_reconnect_if_ready();
        int key = read_key();
        desktop_dirty_region_t dirty;
        desktop_dirty_initialize(&dirty, display.width, display.height);
        desktop_shortcut_probe_poll_storage_restart(
            &display, &manager, &explorer, &dirty);
        uint32_t hover_full_frames_before = metrics.full_frames;
        uint32_t hover_full_total_before = metrics.full_total_ms;
        uint32_t hover_dirty_frames_before = metrics.dirty_frames;
        uint32_t hover_dirty_total_before = metrics.dirty_total_ms;
        uint32_t hover_clock_errors_before = metrics.clock_errors;
        desktop_clock_refresh(&display, &dirty, 0U);
        sync_surface_windows(
            &manager, &explorer, &surfaces, &surface_runtime, &dirty);
        if (surface_probe && !surface_resize_requested) {
            for (uint32_t surface_index = 0U;
                 surface_index < DESKTOP_SURFACE_CAPACITY; ++surface_index) {
                desktop_surface_slot_t *surface = &surfaces.slots[surface_index];
                if (!surface->active ||
                    surface->role != REIST_GUI_SURFACE_ROLE_TOPLEVEL ||
                    surface->window_index >= DESKTOP_WM_CAPACITY) continue;
                desktop_window_t *window =
                    &manager.windows[surface->window_index];
                if (window->width + 16U <=
                    (uint32_t)(manager.work_right - window->x)) {
                    window->width += 16U;
                    desktop_dirty_add(
                        &dirty, desktop_wm_window_bounds(
                            &manager, surface->window_index));
                    surface_resize_requested = 1U;
                }
                break;
            }
        }
        if ((surface_probe || control_probe || browser_probe || guidemo_probe) &&
            !surface_probe_reported) {
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
                    x86os_puts(guidemo_probe
                        ? "DESKTOP_GUIDEMO_OK\n"
                        : control_probe
                        ? "DESKTOP_CONTROL_OK\n"
                        : browser_probe
                        ? "DESKTOP_BROWSER_OK\n"
                        : "DESKTOP_SURFACE_OK\n");
                    surface_probe_reported = 1U;
                    break;
                }
            }
        }
        uint32_t actions = 0U;
        uint32_t error_sequence_before = ui.error_sequence;
        uint32_t notification_sequence_before = ui.notification_sequence;
        uint32_t trash_drop_sequence_before = ui.trash_drop_sequence;
        uint32_t trash_empty_sequence_before = ui.trash_empty_sequence;
        uint32_t action_target = DESKTOP_WM_NO_TARGET;
        uint32_t drag_render = 0U;
        uint32_t resize_render = 0U;
        desktop_move_cache_t move_cache = {0};
        desktop_activation_t activation = {
            .window_index = DESKTOP_WM_NO_TARGET,
            .entry_index = DESKTOP_EXPLORER_NO_ENTRY,
        };
        uint32_t control_panel_activate = 0U;
        uint32_t browser_activate = 0U;
        uint32_t trash_activate = 0U;
        uint32_t shortcut_activate_index = UINT32_MAX;
        uint32_t shortcut_activate_generation = 0U;
        int32_t pending_delta_x = 0;
        int32_t pending_delta_y = 0;
        unsigned int mouse_events = 0U;
        uint64_t mouse_batch_started_ms = 0U;
        uint32_t mouse_batch_clock_valid = 0U;
        uint32_t surface_input_queued = 0U;
        for (; mouse_events < DESKTOP_MOUSE_BATCH_LIMIT; ++mouse_events) {
            x86os_mouse_event_t mouse;
            uint32_t hover_transition = 0U;
            if (x86os_mouse_event(&mouse) != 0) break;
            static uint32_t mouse_ready_reported;
            if (!mouse_ready_reported) {
                x86os_puts("DESKTOP_MOUSE_OK\n");
                mouse_ready_reported = 1U;
            }
            if (hover_probe_state.enabled) {
                if (mouse_events == 0U)
                    mouse_batch_clock_valid = x86os_monotonic_ms(
                        &mouse_batch_started_ms) == 0;
                saturating_increment(&hover_probe_state.mouse_reports);
            }
            if ((mouse.delta_x != 0 || mouse.delta_y != 0) &&
                !pointer_present_pending) {
                pointer_present_pending = 1U;
                pointer_pending_clock_valid =
                    x86os_monotonic_ms(&pointer_pending_since_ms) == 0;
            }
            accumulate_mouse_delta(&pending_delta_x, mouse.delta_x);
            accumulate_mouse_delta(&pending_delta_y, mouse.delta_y);
            if (hover_probe_state.enabled &&
                (mouse.delta_x != 0 || mouse.delta_y != 0)) {
                uint32_t menu_before = ui.menu.open_menu;
                uint32_t hot_before = ui.menu.hot_item;
                actions |= dispatch_pointer_motion(
                    &manager, &explorer, &ui, &display, &dirty,
                    &pointer_x, &pointer_y,
                    pending_delta_x, pending_delta_y, &action_target,
                    &drag_render, &resize_render, &move_cache);
                pending_delta_x = 0;
                pending_delta_y = 0;
                hover_transition = menu_before != ui.menu.open_menu ||
                    hot_before != ui.menu.hot_item;
            }
            uint32_t left_down =
                (mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t left_was_down =
                (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t right_down =
                (mouse.buttons & X86OS_MOUSE_BUTTON_RIGHT) != 0U;
            uint32_t right_was_down =
                (previous_buttons & X86OS_MOUSE_BUTTON_RIGHT) != 0U;
            if (left_down != left_was_down ||
                right_down != right_was_down) {
                actions |= dispatch_pointer_motion(
                    &manager, &explorer, &ui, &display, &dirty,
                    &pointer_x, &pointer_y,
                    pending_delta_x, pending_delta_y, &action_target,
                    &drag_render, &resize_render, &move_cache);
                pending_delta_x = 0;
                pending_delta_y = 0;
                if (right_down && !right_was_down)
                    (void)desktop_open_pointer_context(
                        &manager, &explorer, &ui, &display, &dirty,
                        pointer_x, pointer_y, &actions, &action_target);
                if (left_down != left_was_down) {
                    if (!desktop_ui_owns_pointer(&ui) &&
                        manager.capture_kind == DESKTOP_WM_CAPTURE_NONE &&
                        pointer_y < desktop_taskbar_rect(&display).y &&
                        desktop_wm_window_at(
                            &manager, pointer_x, pointer_y) ==
                                DESKTOP_WM_NO_WINDOW)
                        control_panel_activate |= control_panel_pointer_button(
                            &explorer, &display, &dirty,
                            pointer_x, pointer_y, mouse.buttons,
                            previous_buttons);
                    if (!desktop_ui_owns_pointer(&ui) &&
                        manager.capture_kind == DESKTOP_WM_CAPTURE_NONE &&
                        pointer_y < desktop_taskbar_rect(&display).y &&
                        desktop_wm_window_at(
                            &manager, pointer_x, pointer_y) ==
                                DESKTOP_WM_NO_WINDOW)
                        trash_activate |= trash_pointer_button(
                            &explorer, &display, &dirty,
                            pointer_x, pointer_y, mouse.buttons,
                            previous_buttons);
                    if (!desktop_ui_owns_pointer(&ui) &&
                        manager.capture_kind == DESKTOP_WM_CAPTURE_NONE &&
                        pointer_y < desktop_taskbar_rect(&display).y &&
                        desktop_wm_window_at(
                            &manager, pointer_x, pointer_y) ==
                                DESKTOP_WM_NO_WINDOW)
                        (void)desktop_shortcut_pointer_button(
                            &explorer, &display, &dirty,
                            pointer_x, pointer_y, mouse.buttons,
                            previous_buttons, &shortcut_activate_index,
                            &shortcut_activate_generation);
                    int32_t captured_surface_window = manager.capture_window;
                    uint32_t captured_surface_kind = manager.capture_kind;
                    actions |= dispatch_pointer_button(
                        &manager, &explorer, &ui, &display, &dirty,
                        pointer_x, pointer_y, mouse.buttons,
                        previous_buttons, &action_target, &activation);
                    /* The RFB cadence probe must not begin its row sweep
                     * until the Start-menu click has completely reached the
                     * compositor. Defer its probe-only acknowledgement until
                     * this turn has committed the opening damage as well, so
                     * a delayed release or its SVGA update cannot share the
                     * first hover batch. */
                    if (hover_probe_state.enabled &&
                        !hover_menu_ready_reported &&
                        !left_down && left_was_down &&
                        ui.menu.open_menu == DESKTOP_MENU_START)
                        hover_menu_ready_pending = 1U;
                    uint32_t surface_capture_kind = left_down
                        ? manager.capture_kind : captured_surface_kind;
                    int32_t surface_button_window =
                        surface_capture_kind == DESKTOP_WM_CAPTURE_CLIENT
                            ? (left_down ? manager.capture_window
                                         : captured_surface_window)
                            : DESKTOP_WM_NO_WINDOW;
                    int surface_button_status = 0;
                    uint32_t surface_button_queued = enqueue_surface_pointer(
                        &manager, &surfaces, surface_button_window,
                        REIST_GUI_SURFACE_INPUT_POINTER_BUTTON,
                        pointer_x, pointer_y, 0, 0, left_down,
                        !left_down || surface_capture_kind ==
                            DESKTOP_WM_CAPTURE_CLIENT,
                        &surface_button_status);
                    surface_input_queued |= surface_button_queued;
                    if (!surface_button_queued &&
                        surface_button_status == DESKTOP_SURFACE_ECAPACITY &&
                        surface_button_window >= 0) {
                        desktop_surface_slot_t *failed_surface =
                            surface_for_window(
                                &surfaces, (uint32_t)surface_button_window);
                        if (left_down &&
                            manager.capture_kind ==
                                DESKTOP_WM_CAPTURE_CLIENT &&
                            manager.capture_window == surface_button_window)
                            (void)desktop_wm_pointer_release(
                                &manager, pointer_x, pointer_y);
                        if (failed_surface != 0) {
                            reist_gui_surface_owner_t failed_owner =
                                failed_surface->owner;
                            reist_gui_surface_handle_t failed_handle =
                                failed_surface->handle;
                            (void)desktop_surface_destroy(
                                &surfaces, failed_owner, failed_handle);
                            desktop_dirty_full(&dirty);
                        }
                        x86os_puts(
                            "DESKTOP_SURFACE_INPUT_FENCED status=-75\n");
                    }
                    if (guidemo_probe) {
                        x86os_puts("DESKTOP_GUIDEMO_POINTER edge=");
                        x86os_puts(left_down ? "down" : "up");
                        x86os_puts(" x=");
                        x86os_print_number(pointer_x);
                        x86os_puts(" y=");
                        x86os_print_number(pointer_y);
                        x86os_puts(" capture=");
                        x86os_print_number((int)surface_capture_kind);
                        x86os_puts(" window=");
                        x86os_print_number(surface_button_window);
                        x86os_puts(" queued=");
                        x86os_print_number((int)surface_button_queued);
                        x86os_puts(" status=");
                        x86os_print_number(surface_button_status);
                        x86os_putchar('\n');
                    }
                }
            }
            if (mouse.wheel != 0 && !desktop_ui_owns_pointer(&ui) &&
                desktop_drag.phase == DESKTOP_DRAG_PHASE_IDLE) {
                int scroll_window = desktop_wm_window_at(
                    &manager, pointer_x, pointer_y);
                if (scroll_window >= 0 &&
                    scroll_window < (int32_t)DESKTOP_WM_CAPACITY) {
                    uint32_t window_index = (uint32_t)scroll_window;
                    desktop_rect_t client = desktop_explorer_content_rect(
                        &manager, &explorer, window_index);
                    if (point_in_rect(client, pointer_x, pointer_y)) {
                        desktop_explorer_result_t scroll_result;
                        desktop_explorer_result_initialize(&scroll_result);
                        if (desktop_explorer_wheel(
                                &explorer, window_index, client, mouse.wheel,
                                &scroll_result) == DESKTOP_EXPLORER_OK)
                            collect_explorer_pointer_result(
                                &display, &manager, &dirty, &scroll_result,
                                0U, &activation);
                    }
                }
            }
            previous_buttons = mouse.buttons;
            if (hover_transition) {
                ++mouse_events;
                break;
            }
        }
        if (hover_probe_state.enabled && mouse_events != 0U) {
            uint64_t mouse_batch_finished_ms = 0U;
            if (!mouse_batch_clock_valid ||
                x86os_monotonic_ms(&mouse_batch_finished_ms) != 0 ||
                mouse_batch_finished_ms < mouse_batch_started_ms) {
                saturating_increment(&hover_probe_state.clock_errors);
            } else {
                uint64_t elapsed =
                    mouse_batch_finished_ms - mouse_batch_started_ms;
                uint32_t elapsed_ms = elapsed > UINT32_MAX
                    ? UINT32_MAX : (uint32_t)elapsed;
                if (elapsed_ms > hover_probe_state.mouse_batch_max_ms)
                    hover_probe_state.mouse_batch_max_ms = elapsed_ms;
                if (mouse_events >
                    hover_probe_state.mouse_batch_max_reports)
                    hover_probe_state.mouse_batch_max_reports = mouse_events;
            }
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
            surface_input_queued |= enqueue_surface_pointer(
                &manager, &surfaces, surface_motion_window,
                REIST_GUI_SURFACE_INPUT_POINTER_MOTION,
                pointer_x, pointer_y, pending_delta_x, pending_delta_y, 0U,
                manager.capture_kind == DESKTOP_WM_CAPTURE_CLIENT, 0);
        }

        uint32_t drag_key_consumed = 0U;
        if (key == DESKTOP_KEY_ESCAPE &&
            desktop_drag.phase != DESKTOP_DRAG_PHASE_IDLE) {
            desktop_dirty_add(&dirty, desktop_drag_feedback_rect());
            desktop_dirty_add(
                &dirty, desktop_icon_rect(&display, &explorer, 2U));
            if (desktop_drag.object.source_id < DESKTOP_WM_CAPACITY)
                desktop_explorer_pointer_cancel(
                    &explorer, desktop_drag.object.source_id);
            desktop_drag_cancel(&desktop_drag);
            desktop_layout_drag_source_index = UINT32_MAX;
            desktop_layout_hover_valid = 0U;
            drag_key_consumed = 1U;
        }
        desktop_ui_result_t ui_key = drag_key_consumed
            ? desktop_ui_result_none()
            : desktop_ui_keyboard_event(&ui, &display, &dirty, key);
        if (drag_key_consumed) ui_key.consumed = 1U;
        actions |= apply_desktop_ui_result(
            &manager, &explorer, &ui, &display, &dirty,
            &ui_key, &action_target);
        if (!ui_key.consumed) {
            uint32_t navigation_key_consumed = 0U;
            if ((key == '\b' || key == 0x7F) &&
                manager.keyboard_focus >= 0 &&
                manager.keyboard_focus < (int32_t)DESKTOP_WM_CAPACITY) {
                uint32_t focused = (uint32_t)manager.keyboard_focus;
                if (explorer.windows[focused].active) {
                    navigation_key_consumed = 1U;
                    if (desktop_explorer_can_back(
                            &explorer.windows[focused])) {
                        int navigation_status = desktop_explorer_back(
                            &explorer, focused);
                        if (navigation_status != DESKTOP_EXPLORER_OK)
                            desktop_ui_open_error(
                                &ui, &display, &dirty,
                                "Zurueck-Navigation ist fehlgeschlagen.",
                                explorer.windows[focused].path);
                        desktop_dirty_add(
                            &dirty, desktop_wm_window_bounds(
                                &manager, focused));
                    }
                }
            }
            uint32_t surface_key_consumed = navigation_key_consumed ? 0U :
                enqueue_surface_keyboard(&manager, &surfaces, key);
            surface_input_queued |= surface_key_consumed;
            uint32_t explorer_key = explorer_key_from_input(key);
            if (!navigation_key_consumed && !surface_key_consumed &&
                explorer_key != 0U &&
                manager.keyboard_focus >= 0 &&
                manager.keyboard_focus < (int32_t)DESKTOP_WM_CAPACITY) {
                uint32_t focused = (uint32_t)manager.keyboard_focus;
                desktop_rect_t client = desktop_explorer_content_rect(
                    &manager, &explorer, focused);
                desktop_explorer_result_t explorer_result;
                desktop_explorer_result_initialize(&explorer_result);
                (void)desktop_explorer_keyboard(
                    &explorer, focused, client, explorer_key,
                    &explorer_result);
                collect_explorer_pointer_result(
                    &display, &manager, &dirty, &explorer_result, 0U,
                    &activation);
            }
        }

        if ((actions & DESKTOP_ACTION_OPEN_CONTROL_PANEL) != 0U) {
            control_panel_activate = 1U;
            actions &= ~DESKTOP_ACTION_OPEN_CONTROL_PANEL;
        }
        if ((actions & DESKTOP_ACTION_OPEN_BROWSER) != 0U) {
            browser_activate = 1U;
            actions &= ~DESKTOP_ACTION_OPEN_BROWSER;
        }
        if ((actions & DESKTOP_ACTION_OPEN_TRASH) != 0U) {
            trash_activate = 1U;
            actions &= ~DESKTOP_ACTION_OPEN_TRASH;
        }
        if ((actions & DESKTOP_ACTION_EMPTY_TRASH) != 0U) {
            actions &= ~DESKTOP_ACTION_EMPTY_TRASH;
            apply_trash_empty(
                &explorer, &manager, &ui, &display, &dirty);
        }
        if ((actions & DESKTOP_ACTION_CREATE_SHORTCUT) != 0U) {
            actions &= ~DESKTOP_ACTION_CREATE_SHORTCUT;
            apply_explorer_shortcut_create(
                &manager, &explorer, &ui, &display, &dirty);
        }
        if ((actions & DESKTOP_ACTION_OPEN_SHORTCUT) != 0U) {
            actions &= ~DESKTOP_ACTION_OPEN_SHORTCUT;
            shortcut_activate_index = ui.shortcut_menu_index;
            shortcut_activate_generation = ui.shortcut_menu_generation;
            ui.shortcut_menu_index = UINT32_MAX;
            ui.shortcut_menu_generation = 0U;
        }
        if ((actions & DESKTOP_ACTION_REMOVE_SHORTCUT) != 0U) {
            actions &= ~DESKTOP_ACTION_REMOVE_SHORTCUT;
            apply_desktop_shortcut_remove(
                &manager, &explorer, &ui, &display, &dirty,
                ui.shortcut_menu_generation,
                ui.shortcut_menu_index);
            ui.shortcut_menu_index = UINT32_MAX;
            ui.shortcut_menu_generation = 0U;
        }

        if ((actions & DESKTOP_WM_RESULT_EXIT) != 0U) {
            desktop_system_sound_request(
                &system_sounds, DESKTOP_SYSTEM_SOUND_SHUTDOWN);
            desktop_shortcut_probe_cancel_storage_restart();
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
        if (shortcut_activate_index != UINT32_MAX)
            apply_desktop_shortcut_activation(
                &manager, &explorer, &filetypes, &surface_runtime,
                &ui, &display, &dirty,
                shortcut_activate_generation, shortcut_activate_index,
                &action_target, pointer_x, pointer_y);
        if (control_panel_activate)
            apply_control_panel_activation(
                &surface_runtime, &ui, &display, &dirty,
                pointer_x, pointer_y);
        if (browser_activate)
            apply_browser_activation(
                &surface_runtime, &ui, &display, &dirty,
                pointer_x, pointer_y);
        if (trash_activate)
            actions |= open_explorer_path(
                &manager, &explorer, &ui, &display, &dirty,
                DESKTOP_TRASH_FILES_PATH, &action_target);
        if (surface_input_queued) {
            /* One CPU needs an explicit bounded handoff so the addressed
             * client can paint. On SMP it can run concurrently; yielding the
             * compositor there only adds global scheduler contention. */
            if (online_cpu_count == 1U) (void)x86os_yield();
            (void)desktop_surface_runtime_poll(
                &surface_runtime, &surfaces);
        }
        sync_surface_windows(
            &manager, &explorer, &surfaces, &surface_runtime, &dirty);

        if (ui.error_sequence != error_sequence_before)
            desktop_system_sound_request(
                &system_sounds, DESKTOP_SYSTEM_SOUND_ERROR);
        else if (ui.trash_empty_sequence != trash_empty_sequence_before)
            desktop_system_sound_request(
                &system_sounds, DESKTOP_SYSTEM_SOUND_TRASH_EMPTY);
        else if (ui.trash_drop_sequence != trash_drop_sequence_before)
            desktop_system_sound_request(
                &system_sounds, DESKTOP_SYSTEM_SOUND_TRASH_DROP);
        else if (ui.notification_sequence != notification_sequence_before)
            desktop_system_sound_request(
                &system_sounds, DESKTOP_SYSTEM_SOUND_NOTIFICATION);

        /* The cached path represents exactly one unobscured move or retained
         * left/top resize. Concurrent state changes use normal composition. */
        uint32_t cached_interaction = move_cache.kind ==
                DESKTOP_MOVE_CACHE_RESIZE
            ? resize_render &&
                manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE
            : drag_render && !resize_render;
        if (move_cache.valid &&
            (!cached_interaction || dirty.full ||
             key != DESKTOP_KEY_NONE ||
             (actions & (DESKTOP_WM_RESULT_LAUNCH |
                         DESKTOP_WM_RESULT_EXIT)) != 0U))
            move_cache.valid = 0U;

        if (dirty.count != 0U) {
            uint32_t motion_present = pointer_present_pending;
            if (pointer_overlay_active && !move_cache.valid) {
                /* Publish the newest pointer before raster work. The kernel
                 * keeps it out of the scene shadow and reapplies it after an
                 * intersecting commit, so hover no longer emits hide/show
                 * frames or makes cursor latency depend on GUI rendering. */
                if (motion_present) {
                    desktop_pointer_present_result_t pointer_result =
                        desktop_pointer_present(
                            pointer_x, pointer_y, 1U);
                    if (desktop_pointer_present_completed(
                            &hover_probe_state,
                            pointer_pending_clock_valid,
                            pointer_pending_since_ms, &pointer_result)) {
                        pointer_present_pending = 0U;
                        pointer_pending_clock_valid = 0U;
                    }
                }
                (void)render_desktop_measured(
                    &display, &manager, &explorer, &surfaces, &ui, &dirty,
                    0, drag_render, resize_render, &metrics);
            } else {
                (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
                (void)render_desktop_measured(
                    &display, &manager, &explorer, &surfaces, &ui, &dirty,
                    move_cache.valid ? &move_cache : 0,
                    drag_render, resize_render, &metrics);
                desktop_pointer_present_result_t pointer_result =
                    desktop_pointer_present(pointer_x, pointer_y, 1U);
                if (motion_present && desktop_pointer_present_completed(
                        &hover_probe_state, pointer_pending_clock_valid,
                        pointer_pending_since_ms, &pointer_result)) {
                    pointer_present_pending = 0U;
                    pointer_pending_clock_valid = 0U;
                } else if (!motion_present && pointer_result.status != 0) {
                    pointer_present_pending = 1U;
                    pointer_pending_clock_valid =
                        x86os_monotonic_ms(&pointer_pending_since_ms) == 0;
                }
            }
        } else if (pointer_present_pending) {
            /* Pure motion has no scene damage. Publish its newest coalesced
             * position in this desktop turn, then block for exactly the
             * existing one-millisecond poll interval. */
            desktop_pointer_present_result_t pointer_result =
                desktop_pointer_present(pointer_x, pointer_y, 1U);
            if (desktop_pointer_present_completed(
                    &hover_probe_state, pointer_pending_clock_valid,
                    pointer_pending_since_ms, &pointer_result)) {
                pointer_present_pending = 0U;
                pointer_pending_clock_valid = 0U;
            }
            (void)x86os_sleep_ms(DESKTOP_IDLE_POLL_MS);
        } else {
            (void)x86os_sleep_ms(DESKTOP_IDLE_POLL_MS);
        }
        hover_probe_record_transition(
            &hover_probe_state, &ui, &dirty, &metrics,
            hover_full_frames_before, hover_full_total_before,
            hover_dirty_frames_before, hover_dirty_total_before,
            hover_clock_errors_before);
        if (hover_menu_ready_pending && !hover_menu_ready_reported &&
            ui.menu.open_menu == DESKTOP_MENU_START) {
            x86os_puts("DESKTOP_HOVER_MENU_READY\n");
            hover_menu_ready_reported = 1U;
            hover_menu_ready_pending = 0U;
        }
        if (hover_probe_state.complete) {
            uint32_t probe_success = hover_probe_state.success;
            uint32_t exited = desktop_try_exit(
                pointer_x, pointer_y, runtime_activated, &metrics);
            desktop_surface_runtime_shutdown(&surface_runtime);
            return exited && probe_success ? 0 : 1;
        }
    }
}
