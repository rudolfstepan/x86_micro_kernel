/**
 * @file reist/gui/dialog.h
 * @brief Versioned asynchronous dialog-controller API for Ring-3 programs.
 *
 * The controller implements dialog lifetime, modal/modeless input routing,
 * button focus, completion responses and bounded title-bar dragging. It is
 * renderer-independent and never accesses a framebuffer, device, process,
 * allocator or global window-manager state.
 *
 * The API deliberately uses asynchronous open/dispatch/complete semantics.
 * It never creates a nested event loop. The caller owns every model string,
 * button array, layout, state and result object and must keep model data alive
 * while the dialog is visible. Calls that share mutable state must be
 * serialized by the caller. This in-process API is not the future Surface IPC
 * protocol.
 *
 * Coordinates are local to the caller-owned surface; rectangles are half-open.
 * Owner identifiers are opaque to this component. The host must validate and
 * enforce their process/surface generation when routing a window-modal dialog.
 *
 * @defgroup reist_gui_dialog REIST GUI dialog controller
 * @{
 */
#ifndef REIST_GUI_DIALOG_H
#define REIST_GUI_DIALOG_H

#include <stdint.h>
#include <reist/gui/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Exact API version accepted by this implementation. */
#define REIST_GUI_DIALOG_API_VERSION 1U
/** Maximum number of action buttons in one dialog. */
#define REIST_GUI_DIALOG_MAX_BUTTONS 4U
/** Maximum title or button label length including terminating NUL. */
#define REIST_GUI_DIALOG_LABEL_LIMIT 64U
/** Maximum message/detail length including terminating NUL. */
#define REIST_GUI_DIALOG_TEXT_LIMIT 256U
/** Maximum number of damage rectangles returned by one operation. */
#define REIST_GUI_DIALOG_DAMAGE_CAPACITY 4U
/** Sentinel for no button index. */
#define REIST_GUI_DIALOG_NO_INDEX UINT32_MAX
/** Sentinel for a dialog without a top-level owner. */
#define REIST_GUI_DIALOG_NO_OWNER UINT32_MAX

/** Successful operation. */
#define REIST_GUI_DIALOG_OK 0
/** Invalid pointer, version, field, model, state or event (`EINVAL`). */
#define REIST_GUI_DIALOG_EINVAL (-22)
/** A dialog is already visible (`EBUSY`). */
#define REIST_GUI_DIALOG_EBUSY (-16)
/** The dialog has not completed, so no response is available (`EAGAIN`). */
#define REIST_GUI_DIALOG_EAGAIN (-11)
/** Valid dimensions or text cannot fit the configured surface. */
#define REIST_GUI_DIALOG_EOVERFLOW (-75)

/** The only pointer button interpreted by version 1. */
#define REIST_GUI_DIALOG_BUTTON_LEFT 1U
/** Model flag: allow title-bar dragging inside the work area. */
#define REIST_GUI_DIALOG_MOVABLE (1U << 0)
/** Model flag: expose a close control using the cancel response. */
#define REIST_GUI_DIALOG_CLOSE_BUTTON (1U << 1)
/** Button flag: render the button but exclude it from focus/activation. */
#define REIST_GUI_DIALOG_BUTTON_DISABLED (1U << 0)

/** Input modality enforced by the host while a dialog is visible. */
enum reist_gui_dialog_modality {
    /** Outside input is not consumed and may continue to another target. */
    REIST_GUI_DIALOG_MODELESS = 0U,
    /** The generations-bound owner is inert; unrelated windows may continue. */
    REIST_GUI_DIALOG_WINDOW_MODAL,
    /** All ordinary targets in the application/session are inert. */
    REIST_GUI_DIALOG_APPLICATION_MODAL
};

/** Semantic action role, independent of localized button text and response. */
enum reist_gui_dialog_button_role {
    REIST_GUI_DIALOG_ROLE_ACCEPT = 1U,      /**< Confirm or continue. */
    REIST_GUI_DIALOG_ROLE_REJECT,           /**< Cancel or close safely. */
    REIST_GUI_DIALOG_ROLE_DESTRUCTIVE,      /**< Irreversible operation. */
    REIST_GUI_DIALOG_ROLE_APPLY,            /**< Apply without semantic close. */
    REIST_GUI_DIALOG_ROLE_RESET,            /**< Restore/reset values. */
    REIST_GUI_DIALOG_ROLE_HELP,             /**< Request contextual help. */
    REIST_GUI_DIALOG_ROLE_ACTION            /**< Application-specific action. */
};

/** Stable standard responses. Applications may use values >= APPLICATION. */
enum reist_gui_dialog_response {
    REIST_GUI_DIALOG_RESPONSE_NONE = 0U,
    REIST_GUI_DIALOG_RESPONSE_OK,
    REIST_GUI_DIALOG_RESPONSE_CANCEL,
    REIST_GUI_DIALOG_RESPONSE_YES,
    REIST_GUI_DIALOG_RESPONSE_NO,
    REIST_GUI_DIALOG_RESPONSE_RETRY,
    REIST_GUI_DIALOG_RESPONSE_CLOSE,
    REIST_GUI_DIALOG_RESPONSE_APPLY,
    REIST_GUI_DIALOG_RESPONSE_RESET,
    REIST_GUI_DIALOG_RESPONSE_HELP,
    REIST_GUI_DIALOG_RESPONSE_SAVE,
    REIST_GUI_DIALOG_RESPONSE_DISCARD,
    REIST_GUI_DIALOG_RESPONSE_APPLICATION = 0x00010000U
};

/** Normalized input event kinds accepted by reist_gui_dialog_dispatch(). */
enum reist_gui_dialog_event_type {
    REIST_GUI_DIALOG_EVENT_POINTER_MOTION = 1U,
    REIST_GUI_DIALOG_EVENT_POINTER_BUTTON,
    REIST_GUI_DIALOG_EVENT_KEYBOARD
};

/** Renderer-neutral dialog navigation keys. */
enum reist_gui_dialog_key {
    REIST_GUI_DIALOG_KEY_PREVIOUS = 1U,
    REIST_GUI_DIALOG_KEY_NEXT,
    REIST_GUI_DIALOG_KEY_ENTER,
    REIST_GUI_DIALOG_KEY_ESCAPE
};

/** Implicit pointer capture held from button-down through button-up. */
enum reist_gui_dialog_capture_kind {
    REIST_GUI_DIALOG_CAPTURE_NONE = 0U,
    REIST_GUI_DIALOG_CAPTURE_MOVE,
    REIST_GUI_DIALOG_CAPTURE_BUTTON,
    REIST_GUI_DIALOG_CAPTURE_CLOSE,
    REIST_GUI_DIALOG_CAPTURE_BODY
};

/** Immutable description of one localized action button. */
typedef struct reist_gui_dialog_button {
    const char *label; /**< Caller-owned NUL-terminated localized label. */
    uint32_t response; /**< Nonzero stable response returned on activation. */
    uint32_t role;     /**< enum reist_gui_dialog_button_role. */
    uint32_t flags;    /**< REIST_GUI_DIALOG_BUTTON_* bit mask. */
    uint32_t reserved; /**< Must be zero. */
} reist_gui_dialog_button_t;

/** Versioned immutable content and policy for one dialog. */
typedef struct reist_gui_dialog_model {
    uint32_t version;     /**< REIST_GUI_DIALOG_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_dialog_model_t). */
    const char *title;    /**< Caller-owned nonempty localized title. */
    const char *message;  /**< Caller-owned nonempty primary message. */
    const char *detail;   /**< Optional caller-owned detail, or NULL. */
    const reist_gui_dialog_button_t *buttons; /**< Caller-owned button array. */
    uint32_t button_count; /**< Entries in buttons; one through four. */
    uint32_t modality;     /**< enum reist_gui_dialog_modality. */
    uint32_t default_response; /**< Enter response present in buttons. */
    uint32_t cancel_response;  /**< Escape/close response, or NONE. */
    uint32_t owner_id;         /**< Opaque owner or NO_OWNER. */
    uint32_t owner_generation; /**< Nonzero iff owner_id is present. */
    uint32_t flags;            /**< REIST_GUI_DIALOG_* bit mask. */
    uint32_t reserved[4];      /**< Must be initialized to zero. */
} reist_gui_dialog_model_t;

/**
 * Versioned renderer metrics and initial placement.
 *
 * The work area and initial bounds must fit inside the local surface. Button
 * widths are derived from fixed-width label metrics, horizontal padding and
 * button_min_width; buttons are laid out in model order from left to right.
 */
typedef struct reist_gui_dialog_layout {
    uint32_t version;     /**< REIST_GUI_DIALOG_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_dialog_layout_t). */
    uint32_t surface_width;  /**< Local surface width in pixels. */
    uint32_t surface_height; /**< Local surface height in pixels. */
    reist_gui_rect_t work_area;     /**< Bounds allowed for the complete frame. */
    reist_gui_rect_t initial_bounds;/**< Initial complete dialog frame. */
    uint32_t title_height;    /**< Server-decoration title height. */
    uint32_t border_width;    /**< Frame border, one through 16 pixels. */
    uint32_t font_width;      /**< Fixed glyph width in pixels. */
    uint32_t font_height;     /**< Fixed glyph height in pixels. */
    uint32_t button_min_width;/**< Minimum action-button width. */
    uint32_t button_height;   /**< Uniform action-button height. */
    uint32_t button_gap;      /**< Horizontal gap between buttons. */
    uint32_t button_padding_x;/**< Label padding on each horizontal side. */
    uint32_t content_padding; /**< Content/button edge inset. */
    uint32_t damage_margin;   /**< Shadow/redraw expansion, at most 16. */
    uint32_t reserved[4];     /**< Must be initialized to zero. */
} reist_gui_dialog_layout_t;

/** Caller-owned mutable state; initialize once before open or dispatch. */
typedef struct reist_gui_dialog_state {
    uint32_t version;     /**< REIST_GUI_DIALOG_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_dialog_state_t). */
    uint32_t visible;     /**< Nonzero while input/rendering is active. */
    uint32_t active;      /**< Nonzero while this dialog owns keyboard focus. */
    uint32_t completed;   /**< Nonzero after exactly one completion response. */
    uint32_t response;    /**< Valid when completed is nonzero. */
    uint32_t generation;  /**< Nonzero instance generation after first open. */
    uint32_t modality;    /**< Snapshot of the opened model modality. */
    uint32_t owner_id;    /**< Snapshot of the opened owner ID. */
    uint32_t owner_generation; /**< Snapshot of the opened owner generation. */
    reist_gui_rect_t bounds; /**< Current complete frame in local coordinates. */
    uint32_t focused_button; /**< Index or REIST_GUI_DIALOG_NO_INDEX. */
    uint32_t hot_button;     /**< Index or REIST_GUI_DIALOG_NO_INDEX. */
    uint32_t capture_kind;   /**< enum reist_gui_dialog_capture_kind. */
    uint32_t capture_button; /**< Captured index or NO_INDEX. */
    int32_t drag_offset_x;   /**< Pointer offset from frame left during move. */
    int32_t drag_offset_y;   /**< Pointer offset from frame top during move. */
    uint32_t reserved[4];    /**< Must remain zero. */
} reist_gui_dialog_state_t;

/** One normalized event supplied by the host event loop. */
typedef struct reist_gui_dialog_event {
    uint32_t version;     /**< REIST_GUI_DIALOG_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_dialog_event_t). */
    uint32_t type;        /**< enum reist_gui_dialog_event_type. */
    int32_t x;            /**< Pointer X for pointer events. */
    int32_t y;            /**< Pointer Y for pointer events. */
    uint32_t button;      /**< REIST_GUI_DIALOG_BUTTON_* for button events. */
    uint32_t pressed;     /**< One on button-down, zero on button-up. */
    uint32_t key;         /**< enum reist_gui_dialog_key for keyboard events. */
    uint32_t reserved[4]; /**< Must remain zero. */
} reist_gui_dialog_event_t;

/** Bounded output of open, dispatch or programmatic completion. */
typedef struct reist_gui_dialog_result {
    uint32_t version;     /**< REIST_GUI_DIALOG_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_dialog_result_t). */
    uint32_t consumed;    /**< Event belongs to this dialog or its modality. */
    uint32_t completed;   /**< response became final during this operation. */
    uint32_t response;    /**< Final response when completed is nonzero. */
    uint32_t generation;  /**< Dialog instance associated with this result. */
    reist_gui_rect_t damage[REIST_GUI_DIALOG_DAMAGE_CAPACITY];
                            /**< Local regions requiring recomposition. */
    uint32_t damage_count; /**< Valid entries in damage. */
    uint32_t full_redraw;  /**< Damage overflow requests full-surface redraw. */
    uint32_t reserved[4];  /**< Must remain zero. */
} reist_gui_dialog_result_t;

/** Initialize state to a closed, inactive, response-free dialog. */
void reist_gui_dialog_state_initialize(reist_gui_dialog_state_t *state);

/** Initialize an empty versioned input event. */
void reist_gui_dialog_event_initialize(reist_gui_dialog_event_t *event);

/** Initialize a versioned operation result. */
void reist_gui_dialog_result_initialize(reist_gui_dialog_result_t *result);

/**
 * Validate model, layout and current state without mutation.
 *
 * @param[in] model Caller-owned immutable model.
 * @param[in] layout Caller-owned immutable renderer metrics.
 * @param[in] state Initialized mutable-state snapshot.
 * @return REIST_GUI_DIALOG_OK or a documented negative status.
 */
int reist_gui_dialog_validate(const reist_gui_dialog_model_t *model,
                              const reist_gui_dialog_layout_t *layout,
                              const reist_gui_dialog_state_t *state);

/**
 * Open a new asynchronous dialog instance.
 *
 * A visible state returns EBUSY without mutation. On success generation is
 * advanced, response is cleared, focus enters the default/first enabled
 * button and initial_bounds is damaged.
 *
 * @param[in] model Valid model that outlives the visible instance.
 * @param[in] layout Valid local layout.
 * @param[in,out] state Initialized caller-owned state.
 * @param[in,out] result Initialized result receiving initial damage.
 * @return REIST_GUI_DIALOG_OK or a documented negative status.
 */
int reist_gui_dialog_open(const reist_gui_dialog_model_t *model,
                          const reist_gui_dialog_layout_t *layout,
                          reist_gui_dialog_state_t *state,
                          reist_gui_dialog_result_t *result);

/**
 * Dispatch one event through the bounded dialog state machine.
 *
 * Outside pointer input is consumed for modal dialogs and passed through for
 * modeless dialogs without active capture. A matching button-up, Enter,
 * Escape or reist_gui_dialog_complete() may publish exactly one response.
 *
 * @param[in] model Same immutable model used to open this instance.
 * @param[in] layout Current validated surface metrics.
 * @param[in,out] state Mutable caller-owned dialog state.
 * @param[in] event Initialized normalized event.
 * @param[in,out] result Initialized bounded output.
 * @return REIST_GUI_DIALOG_OK or a documented negative status.
 */
int reist_gui_dialog_dispatch(const reist_gui_dialog_model_t *model,
                              const reist_gui_dialog_layout_t *layout,
                              reist_gui_dialog_state_t *state,
                              const reist_gui_dialog_event_t *event,
                              reist_gui_dialog_result_t *result);

/**
 * Complete a visible dialog programmatically with a configured response.
 *
 * @param[in] model Same immutable model used to open this instance.
 * @param[in] layout Current validated surface metrics.
 * @param[in,out] state Mutable visible state.
 * @param[in] response Response present in model->buttons.
 * @param[in,out] result Initialized bounded output.
 * @return REIST_GUI_DIALOG_OK or a documented negative status.
 */
int reist_gui_dialog_complete(const reist_gui_dialog_model_t *model,
                              const reist_gui_dialog_layout_t *layout,
                              reist_gui_dialog_state_t *state,
                              uint32_t response,
                              reist_gui_dialog_result_t *result);

/** Return the completed response, or EAGAIN while none is available. */
int reist_gui_dialog_response(const reist_gui_dialog_state_t *state,
                              uint32_t *response);

/** Query the current complete frame rectangle. */
int reist_gui_dialog_frame_rect(const reist_gui_dialog_model_t *model,
                                const reist_gui_dialog_layout_t *layout,
                                const reist_gui_dialog_state_t *state,
                                reist_gui_rect_t *rect);

/** Query the draggable title rectangle inside the frame. */
int reist_gui_dialog_title_rect(const reist_gui_dialog_model_t *model,
                                const reist_gui_dialog_layout_t *layout,
                                const reist_gui_dialog_state_t *state,
                                reist_gui_rect_t *rect);

/** Query the close-control rectangle; EINVAL if no close control exists. */
int reist_gui_dialog_close_rect(const reist_gui_dialog_model_t *model,
                                const reist_gui_dialog_layout_t *layout,
                                const reist_gui_dialog_state_t *state,
                                reist_gui_rect_t *rect);

/** Query one action-button rectangle by model index. */
int reist_gui_dialog_button_rect(const reist_gui_dialog_model_t *model,
                                 const reist_gui_dialog_layout_t *layout,
                                 const reist_gui_dialog_state_t *state,
                                 uint32_t button_index,
                                 reist_gui_rect_t *rect);

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_DIALOG_H */
/** @} */
