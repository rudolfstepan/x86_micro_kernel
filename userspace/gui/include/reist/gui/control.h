/**
 * @file reist/gui/control.h
 * @brief Basic renderer-independent controls for Ring-3 GUI programs.
 *
 * Labels, push buttons, checkboxes and radio buttons use one bounded input
 * and focus controller. The library does not draw, allocate, access devices
 * or own an event loop. The caller owns the immutable model, mutable state and
 * rendering. Model strings and arrays must outlive every call that uses them.
 *
 * Coordinates are local to the caller-owned surface and rectangles are
 * half-open. Calls using the same mutable state must be serialized by the
 * caller. A pointer press accepted by an interactive control establishes an
 * implicit grab until the matching release or an explicit cancel event.
 *
 * @defgroup reist_gui_control REIST basic controls
 * @{
 */
#ifndef REIST_GUI_CONTROL_H
#define REIST_GUI_CONTROL_H

#include <stdint.h>
#include <reist/gui/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_GUI_CONTROL_API_VERSION 1U
#define REIST_GUI_CONTROL_CAPACITY 16U
#define REIST_GUI_CONTROL_LABEL_LIMIT 64U
#define REIST_GUI_CONTROL_DAMAGE_CAPACITY 8U
#define REIST_GUI_CONTROL_NO_INDEX UINT32_MAX
#define REIST_GUI_CONTROL_NO_ID 0U

#define REIST_GUI_CONTROL_OK 0
#define REIST_GUI_CONTROL_EINVAL (-22)
#define REIST_GUI_CONTROL_EOVERFLOW (-75)

#define REIST_GUI_CONTROL_BUTTON_LEFT 1U

/** Descriptor flags. Visible and enabled are explicit rather than implicit. */
#define REIST_GUI_CONTROL_VISIBLE (1U << 0)
#define REIST_GUI_CONTROL_ENABLED (1U << 1)
#define REIST_GUI_CONTROL_DEFAULT (1U << 2)
#define REIST_GUI_CONTROL_TRISTATE (1U << 3)

enum reist_gui_control_role {
    REIST_GUI_CONTROL_ROLE_LABEL = 1U,
    REIST_GUI_CONTROL_ROLE_PUSH_BUTTON,
    REIST_GUI_CONTROL_ROLE_CHECKBOX,
    REIST_GUI_CONTROL_ROLE_RADIO_BUTTON
};

/** Semantic check state exposed to renderers and future accessibility code. */
enum reist_gui_control_check_state {
    REIST_GUI_CONTROL_UNCHECKED = 0U,
    REIST_GUI_CONTROL_CHECKED = 1U,
    REIST_GUI_CONTROL_MIXED = 2U
};

enum reist_gui_control_event_type {
    REIST_GUI_CONTROL_EVENT_POINTER_MOTION = 1U,
    REIST_GUI_CONTROL_EVENT_POINTER_BUTTON,
    REIST_GUI_CONTROL_EVENT_KEYBOARD,
    REIST_GUI_CONTROL_EVENT_CANCEL
};

/** Renderer-neutral keys. NEXT/PREVIOUS normally represent Tab/Shift-Tab. */
enum reist_gui_control_key {
    REIST_GUI_CONTROL_KEY_NEXT = 1U,
    REIST_GUI_CONTROL_KEY_PREVIOUS,
    REIST_GUI_CONTROL_KEY_SPACE,
    REIST_GUI_CONTROL_KEY_ENTER,
    REIST_GUI_CONTROL_KEY_LEFT,
    REIST_GUI_CONTROL_KEY_RIGHT,
    REIST_GUI_CONTROL_KEY_UP,
    REIST_GUI_CONTROL_KEY_DOWN
};

/** Why keyboard focus changed; useful for focus-ring policy and diagnostics. */
enum reist_gui_control_focus_reason {
    REIST_GUI_CONTROL_FOCUS_NONE = 0U,
    REIST_GUI_CONTROL_FOCUS_POINTER,
    REIST_GUI_CONTROL_FOCUS_KEYBOARD,
    REIST_GUI_CONTROL_FOCUS_PROGRAMMATIC
};

/** Immutable semantic and geometric description of one control. */
typedef struct reist_gui_control {
    uint32_t id;          /**< Stable nonzero ID returned in results. */
    uint32_t role;        /**< enum reist_gui_control_role. */
    const char *label;    /**< Caller-owned, NUL-terminated accessible name. */
    reist_gui_rect_t bounds; /**< Local hit-test and paint rectangle. */
    uint32_t action;      /**< Opaque action ID returned on activation. */
    uint32_t group;       /**< Nonzero exclusive group for radio buttons. */
    uint32_t flags;       /**< REIST_GUI_CONTROL_* bit mask. */
    uint32_t initial_check; /**< enum reist_gui_control_check_state. */
    uint32_t reserved[2]; /**< Must be zero. */
} reist_gui_control_t;

/** Versioned immutable root model and surface geometry. */
typedef struct reist_gui_control_model {
    uint32_t version;     /**< REIST_GUI_CONTROL_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_control_model_t). */
    const reist_gui_control_t *controls; /**< Caller-owned descriptor array. */
    uint32_t control_count; /**< One through REIST_GUI_CONTROL_CAPACITY. */
    uint32_t surface_width; /**< Local surface width in pixels. */
    uint32_t surface_height; /**< Local surface height in pixels. */
    uint32_t damage_margin; /**< Extra redraw margin, at most 16 pixels. */
    uint32_t reserved[4];   /**< Must be zero. */
} reist_gui_control_model_t;

/** Caller-owned state. Initialize, then configure from exactly one model. */
typedef struct reist_gui_control_state {
    uint32_t version;     /**< REIST_GUI_CONTROL_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_control_state_t). */
    uint32_t configured;  /**< Nonzero after successful configure. */
    uint32_t control_count; /**< Model count captured by configure. */
    uint32_t focused;     /**< Focused index or REIST_GUI_CONTROL_NO_INDEX. */
    uint32_t hovered;     /**< Pointer-hot index or NO_INDEX. */
    uint32_t captured;    /**< Pointer-grab index or NO_INDEX. */
    uint32_t armed;       /**< Captured pointer is currently inside. */
    uint32_t focus_reason; /**< enum reist_gui_control_focus_reason. */
    uint32_t check[REIST_GUI_CONTROL_CAPACITY]; /**< Semantic check states. */
    uint32_t reserved[4]; /**< Must remain zero. */
} reist_gui_control_state_t;

/** One normalized input event. Pointer coordinates are ignored for keys. */
typedef struct reist_gui_control_event {
    uint32_t version;
    uint32_t struct_size;
    uint32_t type;        /**< enum reist_gui_control_event_type. */
    int32_t x;
    int32_t y;
    uint32_t button;      /**< LEFT for pointer-button events. */
    uint32_t pressed;     /**< One for down and zero for up. */
    uint32_t key;         /**< enum reist_gui_control_key. */
    uint32_t reserved[4]; /**< Must be zero. */
} reist_gui_control_event_t;

/** Bounded semantic output and local invalidation of one operation. */
typedef struct reist_gui_control_result {
    uint32_t version;
    uint32_t struct_size;
    uint32_t consumed;
    uint32_t activated;
    uint32_t control_id;
    uint32_t action;
    uint32_t value_changed;
    uint32_t check_state;
    uint32_t focus_changed;
    uint32_t focused_id;
    uint32_t focus_reason;
    reist_gui_rect_t damage[REIST_GUI_CONTROL_DAMAGE_CAPACITY];
    uint32_t damage_count;
    uint32_t full_redraw;
    uint32_t reserved[4];
} reist_gui_control_result_t;

/**
 * Initialize state to unconfigured, with no focus or pointer grab.
 * @param[out] state State to initialize; NULL is a no-op.
 */
void reist_gui_control_state_initialize(reist_gui_control_state_t *state);
/**
 * Initialize an input event to the exact version-1 empty form.
 * @param[out] event Event to initialize; NULL is a no-op.
 */
void reist_gui_control_event_initialize(reist_gui_control_event_t *event);
/**
 * Initialize an operation result to the exact version-1 empty form.
 * @param[out] result Result to initialize; NULL is a no-op.
 */
void reist_gui_control_result_initialize(reist_gui_control_result_t *result);

/**
 * Validate model and state without changing either object.
 *
 * Validation rejects duplicate IDs, invalid geometry/strings, more than one
 * initially checked radio per group, unsupported flags and all nonzero
 * reserved fields. A configured state must belong to an equal-size model and
 * contain valid indices and check states.
 *
 * @param[in] model Immutable model to validate.
 * @param[in] state Caller-owned initialized or configured state.
 * @return OK, EINVAL for invalid data, or EOVERFLOW for geometry.
 */
int reist_gui_control_validate(const reist_gui_control_model_t *model,
                               const reist_gui_control_state_t *state);

/**
 * Copy validated initial check values into state and invalidate the surface.
 * Existing focus and capture are discarded. No state changes occur on error.
 *
 * @param[in] model Immutable model retained by the caller.
 * @param[in,out] state Version-initialized caller-owned state.
 * @param[in,out] result Freshly initialized bounded result.
 * @return OK, EINVAL for an invalid object, or EOVERFLOW for geometry.
 */
int reist_gui_control_configure(const reist_gui_control_model_t *model,
                                reist_gui_control_state_t *state,
                                reist_gui_control_result_t *result);

/**
 * Dispatch one bounded pointer, keyboard or cancellation event.
 *
 * @param[in] model The immutable model used to configure state.
 * @param[in,out] state Configured caller-owned interaction state.
 * @param[in] event Fresh normalized event; reserved fields must be zero.
 * @param[in,out] result Freshly initialized bounded result.
 * @return OK on dispatch or EINVAL before any mutation.
 */
int reist_gui_control_dispatch(const reist_gui_control_model_t *model,
                               reist_gui_control_state_t *state,
                               const reist_gui_control_event_t *event,
                               reist_gui_control_result_t *result);

/**
 * Move focus to a non-label enabled visible control by stable ID.
 *
 * @param[in] model The immutable model used to configure state.
 * @param[in,out] state Configured caller-owned interaction state.
 * @param[in] control_id Stable nonzero ID of the destination.
 * @param[in] reason POINTER, KEYBOARD or PROGRAMMATIC.
 * @param[in,out] result Freshly initialized bounded result.
 * @return OK or EINVAL without changing state.
 */
int reist_gui_control_focus(const reist_gui_control_model_t *model,
                            reist_gui_control_state_t *state,
                            uint32_t control_id, uint32_t reason,
                            reist_gui_control_result_t *result);

/**
 * Set checkbox/radio state programmatically while preserving radio exclusion.
 * A programmatic radio request for UNCHECKED is valid; user activation never
 * unchecks the selected radio without selecting another member.
 *
 * @param[in] model The immutable model used to configure state.
 * @param[in,out] state Configured caller-owned interaction state.
 * @param[in] control_id Stable ID of a checkbox or radio button.
 * @param[in] check_state UNCHECKED, CHECKED or permitted MIXED.
 * @param[in,out] result Freshly initialized bounded result.
 * @return OK or EINVAL without changing state.
 */
int reist_gui_control_set_check(const reist_gui_control_model_t *model,
                                reist_gui_control_state_t *state,
                                uint32_t control_id, uint32_t check_state,
                                reist_gui_control_result_t *result);

/**
 * Return a descriptor index for a stable ID.
 * @param[in] model Immutable model to search, bounded by CAPACITY.
 * @param[in] control_id Stable nonzero ID to locate.
 * @param[out] index_out Descriptor index; unchanged on failure.
 * @return OK or EINVAL when the model or ID is invalid.
 */
int reist_gui_control_index(const reist_gui_control_model_t *model,
                            uint32_t control_id, uint32_t *index_out);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_CONTROL_H */
