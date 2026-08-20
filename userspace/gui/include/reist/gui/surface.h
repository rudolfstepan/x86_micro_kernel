/**
 * @file reist/gui/surface.h
 * @brief Versionierter Surface-/Event-Vertrag für getrennte GUI-Prozesse.
 *
 * Der Vertrag folgt den Zustandsübergängen von wl_surface/xdg_toplevel,
 * bleibt aber ein REIST-eigenes, fest begrenztes IPC-Format. Alle Koordinaten
 * einer Clientfläche sind lokal. Globale Position, Dekoration, Z-Order und
 * Fokus verbleiben beim Session-Compositor.
 */
#ifndef REIST_GUI_SURFACE_H
#define REIST_GUI_SURFACE_H

#include <stdint.h>
#include <reist/gui/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_GUI_SURFACE_API_VERSION 2U
#define REIST_GUI_SURFACE_PROTOCOL_VERSION 3U
#define REIST_GUI_SURFACE_MAX_DAMAGE 8U
#define REIST_GUI_SURFACE_MAX_PENDING_EVENTS 16U
#define REIST_GUI_SURFACE_MAX_CLIENTS 8U
#define REIST_GUI_SURFACE_MAX_SURFACES 8U
#define REIST_GUI_SURFACE_MAX_PAINT_COMMANDS 192U
#define REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY 40U
#define REIST_GUI_SURFACE_MAX_WIDTH 1024U
#define REIST_GUI_SURFACE_MAX_HEIGHT 768U
#define REIST_GUI_SURFACE_MAX_BUFFER_BYTES (1024U * 768U * 4U)
#define REIST_GUI_SURFACE_BUFFER_API_VERSION 1U

enum reist_gui_surface_buffer_format {
    REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888 = 1U
};

enum reist_gui_surface_message_type {
    REIST_GUI_SURFACE_CREATE = 1U,
    REIST_GUI_SURFACE_DESTROY,
    REIST_GUI_SURFACE_ATTACH,
    REIST_GUI_SURFACE_DAMAGE,
    REIST_GUI_SURFACE_COMMIT,
    REIST_GUI_SURFACE_ACK_CONFIGURE,
    REIST_GUI_SURFACE_BUFFER_CREATE,
    REIST_GUI_SURFACE_BUFFER_DESTROY,
    REIST_GUI_SURFACE_SET_TITLE,
    REIST_GUI_SURFACE_PAINT_BEGIN,
    REIST_GUI_SURFACE_PAINT_FILL,
    REIST_GUI_SURFACE_PAINT_TEXT,
    REIST_GUI_SURFACE_PAINT_COMMIT,
    REIST_GUI_SURFACE_CONFIGURE = 0x80U,
    REIST_GUI_SURFACE_INPUT,
    REIST_GUI_SURFACE_CLOSE,
    REIST_GUI_SURFACE_BUFFER_RELEASE
};

enum reist_gui_surface_role {
    REIST_GUI_SURFACE_ROLE_NONE = 0U,
    REIST_GUI_SURFACE_ROLE_TOPLEVEL = 1U
};

enum reist_gui_surface_input_type {
    REIST_GUI_SURFACE_INPUT_POINTER_MOTION = 1U,
    REIST_GUI_SURFACE_INPUT_POINTER_BUTTON,
    REIST_GUI_SURFACE_INPUT_KEYBOARD
};

/** Stable owner identity. A recycled PID with another generation is invalid. */
typedef struct reist_gui_surface_owner {
    uint32_t pid;
    uint32_t process_generation;
} reist_gui_surface_owner_t;

/** Capability-like compositor identity; zero is never a valid handle. */
typedef struct reist_gui_surface_handle {
    uint32_t id;
    uint32_t generation;
} reist_gui_surface_handle_t;

/** Metadata for a compositor-controlled, generation-scoped pixel buffer. */
typedef struct reist_gui_surface_buffer {
    uint32_t version;
    uint32_t struct_size;
    uint32_t capability_id;
    uint32_t capability_generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t format;
    uint32_t byte_size;
    uint32_t reserved;
} reist_gui_surface_buffer_t;

/** Fixed damage set submitted in one commit. */
typedef struct reist_gui_surface_damage {
    uint32_t count;
    uint32_t reserved;
    reist_gui_rect_t rects[REIST_GUI_SURFACE_MAX_DAMAGE];
} reist_gui_surface_damage_t;

/** Client-local input event; global window coordinates never cross the ABI. */
typedef struct reist_gui_surface_input {
    uint32_t type;
    uint32_t serial;
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;
    uint32_t button;
    uint32_t pressed;
    uint32_t key;
    uint32_t reserved;
} reist_gui_surface_input_t;

/** Configure serial and local size proposed by the compositor. */
typedef struct reist_gui_surface_configure {
    uint32_t serial;
    uint32_t width;
    uint32_t height;
    uint32_t states;
    uint32_t reserved;
} reist_gui_surface_configure_t;

/** Fixed wire envelope; payload remains below the existing IPC limit. */
typedef struct reist_gui_surface_message {
    uint32_t protocol_version;
    uint32_t message_size;
    uint32_t type;
    uint32_t flags;
    reist_gui_surface_handle_t surface;
    uint32_t serial;
    uint32_t buffer_id;
    uint32_t buffer_generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t format;
    uint32_t byte_size;
    uint32_t reserved;
    /* One damage rectangle per wire message; the compositor aggregates at
     * most REIST_GUI_SURFACE_MAX_DAMAGE rectangles per pending commit. */
    reist_gui_rect_t damage;
    reist_gui_surface_input_t input;
} reist_gui_surface_message_t;

/**
 * Paint-command wire mapping used by the public client wrapper.
 *
 * The fixed envelope is deliberately reused instead of exposing a global
 * framebuffer mapping. For PAINT_FILL and PAINT_TEXT, ``damage`` contains
 * client-local geometry, ``flags`` the foreground/fill color and
 * ``buffer_id`` the text background color. PAINT_TEXT stores at most
 * REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY bytes in ``input`` and records the
 * exact byte count in ``byte_size``. SET_TITLE uses the same bounded byte
 * storage. Applications must use surface_client.h rather than constructing
 * this representation themselves.
 */

#ifdef __cplusplus
static_assert(sizeof(reist_gui_surface_message_t) <= 128U,
              "surface message exceeds IPC payload");
#else
_Static_assert(sizeof(reist_gui_surface_message_t) <= 128U,
               "surface message exceeds IPC payload");
_Static_assert(sizeof(reist_gui_surface_buffer_t) == 40U,
               "surface buffer ABI size changed");
#endif

/**
 * Invariants: configure serials must be acknowledged before commit; attach
 * and commit are separate; committed buffers remain immutable until release;
 * damage is clipped and limited to eight rectangles; owner, handle and
 * generation are checked on every request; invalid or stale messages have no
 * visible side effect.
 */

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_SURFACE_H */
