/**
 * @file reist/gui/menu.h
 * @brief Versioned, fixed-capacity menu-controller API for Ring-3 programs.
 *
 * The controller is renderer-independent: it calculates geometry, owns a
 * caller-supplied interaction state, dispatches bounded input events and
 * reports invalidated rectangles plus opaque actions. It never accesses a
 * framebuffer, input device, process, allocator or global desktop state.
 *
 * All coordinates use the caller-owned surface coordinate system and all
 * rectangles are half-open: `[x, x + width)` by `[y, y + height)`. Model
 * strings and arrays remain owned by the caller and must outlive every API
 * call that references them. This is an in-process C API, not the future
 * inter-process Surface protocol. Immutable models may be shared, but calls
 * that reference the same mutable state/result must be serialized by the
 * caller; the library creates no threads and takes no locks.
 *
 * @defgroup reist_gui_menu REIST GUI menu controller
 * @{
 */
#ifndef REIST_GUI_MENU_H
#define REIST_GUI_MENU_H

#include <stdint.h>
#include <reist/gui/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Exact API version accepted by this implementation. */
#define REIST_GUI_MENU_API_VERSION 1U
/** Maximum number of top-level menus in one model. */
#define REIST_GUI_MENU_MAX_MENUS 8U
/** Maximum number of items in one top-level menu. */
#define REIST_GUI_MENU_MAX_ITEMS 16U
/** Maximum label length including the required terminating NUL byte. */
#define REIST_GUI_MENU_LABEL_LIMIT 48U
/** Maximum number of damage rectangles returned by one dispatch. */
#define REIST_GUI_MENU_DAMAGE_CAPACITY 4U
/** Sentinel used when no menu or item is selected. */
#define REIST_GUI_MENU_NO_INDEX UINT32_MAX
/** Byte size of the original version-1 layout prefix. */
#define REIST_GUI_MENU_LAYOUT_V1_SIZE 72U
/** Default popup direction retained by old and zero-initialized callers. */
#define REIST_GUI_MENU_POPUP_BELOW 0U
/** Place the popup immediately above the menu title/bar. */
#define REIST_GUI_MENU_POPUP_ABOVE 1U

/** Successful operation. */
#define REIST_GUI_MENU_OK 0
/** Invalid pointer, version, field, model, state or event (`EINVAL`). */
#define REIST_GUI_MENU_EINVAL (-22)
/** Valid input whose geometry cannot fit the configured surface. */
#define REIST_GUI_MENU_EOVERFLOW (-75)

/** The only pointer button interpreted by version 1. */
#define REIST_GUI_MENU_BUTTON_LEFT 1U
/** Item flag: display the item but never focus or activate it. */
#define REIST_GUI_MENU_ITEM_DISABLED (1U << 0)

/** Input event kinds accepted by reist_gui_menu_dispatch(). */
enum reist_gui_menu_event_type {
    REIST_GUI_MENU_EVENT_POINTER_MOTION = 1U, /**< Pointer position changed. */
    REIST_GUI_MENU_EVENT_POINTER_BUTTON,     /**< Pointer button edge. */
    REIST_GUI_MENU_EVENT_KEYBOARD            /**< Menu-navigation key. */
};

/** Renderer-neutral menu navigation keys. */
enum reist_gui_menu_key {
    REIST_GUI_MENU_KEY_LEFT = 1U, /**< Select the previous menu. */
    REIST_GUI_MENU_KEY_RIGHT,     /**< Select the next menu. */
    REIST_GUI_MENU_KEY_UP,        /**< Select the previous enabled item. */
    REIST_GUI_MENU_KEY_DOWN,      /**< Select the next enabled item. */
    REIST_GUI_MENU_KEY_ENTER,     /**< Activate the selected item. */
    REIST_GUI_MENU_KEY_ESCAPE     /**< Close without activation. */
};

/**
 * Pointer-capture state maintained by the controller.
 *
 * A button-down sequence remains bound to exactly one capture kind until its
 * matching button-up event. Callers must continue routing that sequence to
 * the controller even when the pointer leaves the original rectangle.
 */
enum reist_gui_menu_capture_kind {
    REIST_GUI_MENU_CAPTURE_NONE = 0U, /**< No active button sequence. */
    REIST_GUI_MENU_CAPTURE_TITLE,     /**< Sequence began on a menu title. */
    REIST_GUI_MENU_CAPTURE_ITEM,      /**< Sequence began on a menu item. */
    REIST_GUI_MENU_CAPTURE_DISMISS    /**< Outside press dismisses a popup. */
};

/** Immutable description of one menu item. */
typedef struct reist_gui_menu_item {
    const char *label; /**< Caller-owned, NUL-terminated display label. */
    uint32_t action;   /**< Opaque action ID returned on activation. */
    uint32_t target;   /**< Opaque action argument returned unchanged. */
    uint32_t flags;    /**< REIST_GUI_MENU_ITEM_* bit mask. */
    uint32_t reserved; /**< Must be zero. */
} reist_gui_menu_item_t;

/** Immutable description of one top-level menu and its items. */
typedef struct reist_gui_menu {
    const char *label; /**< Caller-owned title label. */
    const reist_gui_menu_item_t *items; /**< Caller-owned item array. */
    uint32_t item_count; /**< Number of entries in items. */
    uint32_t flags;      /**< Reserved for version 1; zero. */
    uint32_t reserved;   /**< Must be zero. */
} reist_gui_menu_t;

/** Versioned root object for an immutable menu model. */
typedef struct reist_gui_menu_model {
    uint32_t version;     /**< REIST_GUI_MENU_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_menu_model_t). */
    const reist_gui_menu_t *menus; /**< Caller-owned top-level menu array. */
    uint32_t menu_count;  /**< Number of entries in menus. */
    uint32_t reserved[4]; /**< Must be initialized to zero. */
} reist_gui_menu_model_t;

/**
 * Versioned layout metrics supplied by the renderer.
 *
 * The controller does not know fonts or colors. The caller supplies measured
 * fixed-width font dimensions and renders the rectangles returned by the
 * geometry functions.
 */
typedef struct reist_gui_menu_layout {
    uint32_t version;      /**< REIST_GUI_MENU_API_VERSION. */
    uint32_t struct_size;  /**< sizeof(reist_gui_menu_layout_t). */
    uint32_t surface_width;  /**< Width of the local drawing surface. */
    uint32_t surface_height; /**< Height of the local drawing surface. */
    reist_gui_rect_t bar;  /**< Menu-bar bounds inside the local surface. */
    uint32_t font_width;   /**< Fixed glyph width in pixels. */
    uint32_t font_height;  /**< Fixed glyph height in pixels. */
    uint32_t title_padding_x; /**< Horizontal title padding per side. */
    uint32_t item_padding_x;  /**< Horizontal item padding per side. */
    uint32_t item_padding_y;  /**< Vertical item padding per side. */
    uint32_t damage_margin;   /**< Extra redraw margin, at most 16 pixels. */
    uint32_t reserved[4];     /**< Must be initialized to zero. */
    /**
     * Appended direction field. Read only when struct_size reaches this
     * member; a version-1-sized layout behaves as POPUP_BELOW.
     */
    uint32_t popup_direction; /**< REIST_GUI_MENU_POPUP_* value. */
} reist_gui_menu_layout_t;

/** Caller-owned mutable interaction state; initialize before first use. */
typedef struct reist_gui_menu_state {
    uint32_t version;      /**< REIST_GUI_MENU_API_VERSION. */
    uint32_t struct_size;  /**< sizeof(reist_gui_menu_state_t). */
    uint32_t open_menu;    /**< Open menu index or REIST_GUI_MENU_NO_INDEX. */
    uint32_t hot_item;     /**< Highlighted item or REIST_GUI_MENU_NO_INDEX. */
    uint32_t capture_kind; /**< enum reist_gui_menu_capture_kind. */
    uint32_t capture_menu; /**< Captured menu or REIST_GUI_MENU_NO_INDEX. */
    uint32_t capture_item; /**< Captured item or REIST_GUI_MENU_NO_INDEX. */
    uint32_t reserved[4];  /**< Must remain zero. */
} reist_gui_menu_state_t;

/** One normalized input event supplied to the controller. */
typedef struct reist_gui_menu_event {
    uint32_t version;      /**< REIST_GUI_MENU_API_VERSION. */
    uint32_t struct_size;  /**< sizeof(reist_gui_menu_event_t). */
    uint32_t type;         /**< enum reist_gui_menu_event_type. */
    int32_t x;             /**< Pointer X for pointer events. */
    int32_t y;             /**< Pointer Y for pointer events. */
    uint32_t button;       /**< REIST_GUI_MENU_BUTTON_* for button events. */
    uint32_t pressed;      /**< One on button-down, zero on button-up. */
    uint32_t key;          /**< enum reist_gui_menu_key for keyboard events. */
    uint32_t reserved[4];  /**< Must remain zero. */
} reist_gui_menu_event_t;

/** Bounded output of one dispatched event. */
typedef struct reist_gui_menu_result {
    uint32_t version;      /**< REIST_GUI_MENU_API_VERSION. */
    uint32_t struct_size;  /**< sizeof(reist_gui_menu_result_t). */
    uint32_t consumed;     /**< Nonzero when the event belongs to the menu. */
    uint32_t activated;    /**< Nonzero when action and target are valid. */
    uint32_t action;       /**< Opaque action ID copied from the model. */
    uint32_t target;       /**< Opaque action argument copied from the model. */
    reist_gui_rect_t damage[REIST_GUI_MENU_DAMAGE_CAPACITY];
                            /**< Local regions that must be recomposed. */
    uint32_t damage_count; /**< Valid entries in damage. */
    uint32_t full_redraw;  /**< Damage overflow requested full-surface redraw. */
    uint32_t reserved[4];  /**< Must remain zero. */
} reist_gui_menu_result_t;

/**
 * Initialize mutable state to a closed, uncaptured menu.
 *
 * @param[out] state State object, or NULL for a no-op.
 */
void reist_gui_menu_state_initialize(reist_gui_menu_state_t *state);

/**
 * Initialize an event header and clear all event fields.
 *
 * @param[out] event Event object, or NULL for a no-op.
 */
void reist_gui_menu_event_initialize(reist_gui_menu_event_t *event);

/**
 * Initialize a result header before calling reist_gui_menu_dispatch().
 *
 * @param[out] result Result object, or NULL for a no-op.
 */
void reist_gui_menu_result_initialize(reist_gui_menu_result_t *result);

/**
 * Validate model, layout and current state without changing them.
 *
 * @param[in] model Immutable caller-owned model.
 * @param[in] layout Immutable local-surface layout.
 * @param[in] state Current initialized interaction state.
 * @return REIST_GUI_MENU_OK, REIST_GUI_MENU_EINVAL or
 *         REIST_GUI_MENU_EOVERFLOW.
 */
int reist_gui_menu_validate(const reist_gui_menu_model_t *model,
                            const reist_gui_menu_layout_t *layout,
                            const reist_gui_menu_state_t *state);

/**
 * Query one title rectangle.
 *
 * @param[in] model Valid immutable model.
 * @param[in] layout Valid renderer metrics.
 * @param[in] menu_index Index smaller than model->menu_count.
 * @param[out] rect Receives local half-open bounds on success.
 * @return REIST_GUI_MENU_OK or a documented negative status.
 */
int reist_gui_menu_title_rect(const reist_gui_menu_model_t *model,
                              const reist_gui_menu_layout_t *layout,
                              uint32_t menu_index,
                              reist_gui_rect_t *rect);

/**
 * Query the complete popup rectangle for one menu.
 *
 * @param[in] model Valid immutable model.
 * @param[in] layout Valid renderer metrics.
 * @param[in] menu_index Index smaller than model->menu_count.
 * @param[out] rect Receives local half-open bounds on success.
 * @return REIST_GUI_MENU_OK or a documented negative status.
 */
int reist_gui_menu_popup_rect(const reist_gui_menu_model_t *model,
                              const reist_gui_menu_layout_t *layout,
                              uint32_t menu_index,
                              reist_gui_rect_t *rect);

/**
 * Query one item row inside a popup.
 *
 * @param[in] model Valid immutable model.
 * @param[in] layout Valid renderer metrics.
 * @param[in] menu_index Index smaller than model->menu_count.
 * @param[in] item_index Index smaller than the menu's item_count.
 * @param[out] rect Receives local half-open bounds on success.
 * @return REIST_GUI_MENU_OK or a documented negative status.
 */
int reist_gui_menu_item_rect(const reist_gui_menu_model_t *model,
                             const reist_gui_menu_layout_t *layout,
                             uint32_t menu_index, uint32_t item_index,
                             reist_gui_rect_t *rect);

/**
 * Validate and dispatch exactly one normalized input event.
 *
 * The operation performs bounded work (`MAX_MENUS * MAX_ITEMS` worst case),
 * never allocates and changes only state and result. On validation failure no
 * model or layout memory is changed; callers should fail closed and reset an
 * untrusted state with reist_gui_menu_state_initialize().
 *
 * @param[in] model Immutable caller-owned model.
 * @param[in] layout Immutable local-surface layout.
 * @param[in,out] state Mutable state initialized by the API.
 * @param[in] event Event initialized by the API and then populated.
 * @param[in,out] result Result initialized by the API before this call.
 * @return REIST_GUI_MENU_OK or a documented negative status.
 */
int reist_gui_menu_dispatch(const reist_gui_menu_model_t *model,
                            const reist_gui_menu_layout_t *layout,
                            reist_gui_menu_state_t *state,
                            const reist_gui_menu_event_t *event,
                            reist_gui_menu_result_t *result);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
