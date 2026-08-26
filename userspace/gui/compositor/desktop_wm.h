/**
 * @file userspace/gui/compositor/desktop_wm.h
 * @brief Fixed-capacity state and event contract of the session window manager.
 *
 * This is a compositor-internal API, not an installed GUI-client header.
 * Coordinates are global only inside the trusted session compositor. The
 * module owns no framebuffer and performs no allocation, I/O or process
 * operation. One compositor thread serializes calls and renders the bounded
 * damage returned by desktop_wm_dispatch().
 */
#ifndef USERSPACE_DESKTOP_WM_H
#define USERSPACE_DESKTOP_WM_H

#include <stdint.h>

/** Maximum number of top-level windows in the current desktop session. */
#define DESKTOP_WM_CAPACITY 8U
/** Maximum damage rectangles before controlled full-screen fallback. */
#define DESKTOP_WM_DIRTY_CAPACITY 8U
/** Sentinel returned by signed window hit tests. */
#define DESKTOP_WM_NO_WINDOW (-1)
/** Sentinel used when an event has no unsigned action target. */
#define DESKTOP_WM_NO_TARGET UINT32_MAX

/** Owner of the current implicit pointer sequence. */
enum desktop_wm_capture_kind {
    DESKTOP_WM_CAPTURE_NONE = 0U, /**< No button sequence is active. */
    DESKTOP_WM_CAPTURE_MOVE,      /**< Move a top-level window. */
    DESKTOP_WM_CAPTURE_CLOSE,     /**< Close-button press. */
    DESKTOP_WM_CAPTURE_CLIENT,    /**< Client-area press. */
    DESKTOP_WM_CAPTURE_RESIZE     /**< Resize from captured edges. */
};

/** Resize-edge bit mask; corner resizes combine one horizontal and vertical. */
#define DESKTOP_WM_RESIZE_LEFT (1U << 0)
#define DESKTOP_WM_RESIZE_RIGHT (1U << 1)
#define DESKTOP_WM_RESIZE_TOP (1U << 2)
#define DESKTOP_WM_RESIZE_BOTTOM (1U << 3)

/** Client pixels remain at the same origin during right/bottom live resize. */
#define DESKTOP_WM_WINDOW_RETAINED_RESIZE (1U << 0)

/** Normalized event kinds accepted by desktop_wm_dispatch(). */
enum desktop_wm_event_type {
    DESKTOP_WM_EVENT_POINTER_MOTION = 1U, /**< Global pointer position. */
    DESKTOP_WM_EVENT_POINTER_BUTTON,     /**< Pointer button edge. */
    DESKTOP_WM_EVENT_KEYBOARD,           /**< Focused keyboard action. */
    DESKTOP_WM_EVENT_OPEN,               /**< Show one indexed window. */
    DESKTOP_WM_EVENT_SELECT,             /**< Select one indexed window. */
    DESKTOP_WM_EVENT_CLOSE               /**< Hide one indexed window. */
};

/** Renderer-neutral keyboard actions understood by the desktop policy. */
enum desktop_wm_key {
    DESKTOP_WM_KEY_TAB = 1U, /**< Select next icon/window. */
    DESKTOP_WM_KEY_UP,       /**< Select item in the previous row. */
    DESKTOP_WM_KEY_DOWN,     /**< Select item in the next row. */
    DESKTOP_WM_KEY_LEFT,     /**< Select previous item. */
    DESKTOP_WM_KEY_RIGHT,    /**< Select next item. */
    DESKTOP_WM_KEY_ENTER,    /**< Open and launch selected item. */
    DESKTOP_WM_KEY_ESCAPE    /**< Request session exit. */
};

/** The only pointer button handled by this window-manager version. */
#define DESKTOP_WM_BUTTON_LEFT 1U

/** Dispatcher result includes visible state damage. */
#define DESKTOP_WM_RESULT_REDRAW (1U << 0)
/** Dispatcher requests launching the target application. */
#define DESKTOP_WM_RESULT_LAUNCH (1U << 1)
/** Dispatcher requests leaving the desktop session. */
#define DESKTOP_WM_RESULT_EXIT (1U << 2)
/** Desktop icon selection changed and requires icon redraw. */
#define DESKTOP_WM_RESULT_SELECTION_CHANGED (1U << 3)

/** Half-open compositor-global pixel rectangle. */
typedef struct desktop_rect {
    int32_t x;       /**< Left edge in pixels. */
    int32_t y;       /**< Top edge in pixels. */
    uint32_t width;  /**< Width in pixels. */
    uint32_t height; /**< Height in pixels. */
} desktop_rect_t;

/** Fixed-capacity set of clipped regions that require recomposition. */
typedef struct desktop_dirty_region {
    desktop_rect_t rects[DESKTOP_WM_DIRTY_CAPACITY]; /**< Valid regions. */
    uint32_t count;         /**< Number of valid rects entries. */
    uint32_t full;          /**< Nonzero after full-screen fallback. */
    uint32_t screen_width;  /**< Clip width fixed at initialization. */
    uint32_t screen_height; /**< Clip height fixed at initialization. */
} desktop_dirty_region_t;

/** One normalized, immutable state-transition request. */
typedef struct desktop_wm_event {
    uint32_t type;    /**< enum desktop_wm_event_type. */
    int32_t x;        /**< Pointer X for pointer events. */
    int32_t y;        /**< Pointer Y for pointer events. */
    uint32_t button;  /**< DESKTOP_WM_BUTTON_* for button events. */
    uint32_t pressed; /**< One for button-down, zero for button-up. */
    uint32_t key;     /**< enum desktop_wm_key for keyboard events. */
    uint32_t target;  /**< Window/application index for typed actions. */
} desktop_wm_event_t;

/** Bounded output from one dispatched event. */
typedef struct desktop_wm_dispatch_result {
    desktop_dirty_region_t dirty; /**< Regions changed by this event. */
    uint32_t flags;               /**< DESKTOP_WM_RESULT_* bit mask. */
    uint32_t target;              /**< Launch target or NO_TARGET. */
    uint32_t previous_selected;   /**< Selection before the event. */
    uint32_t selected;            /**< Selection after the event. */
} desktop_wm_dispatch_result_t;

/** Server-owned geometry and visibility of one top-level window. */
typedef struct desktop_window {
    int32_t x;         /**< Global left edge. */
    int32_t y;         /**< Global top edge. */
    uint32_t width;    /**< Decorated width. */
    uint32_t height;   /**< Decorated height. */
    uint32_t content_id; /**< Compositor-owned content-slot identity. */
    uint32_t flags;    /**< DESKTOP_WM_WINDOW_* rendering capabilities. */
    uint32_t visible;  /**< Nonzero while included in composition. */
} desktop_window_t;

/**
 * Complete fixed-capacity compositor policy state.
 *
 * Callers initialize with desktop_wm_initialize() and thereafter mutate the
 * object only through this module. Focus and capture indices are signed so
 * DESKTOP_WM_NO_WINDOW can represent no owner.
 */
typedef struct desktop_wm {
    desktop_window_t windows[DESKTOP_WM_CAPACITY]; /**< Window slots. */
    uint32_t z_order[DESKTOP_WM_CAPACITY]; /**< Back-to-front slot order. */
    int32_t keyboard_focus; /**< Window receiving keyboard input. */
    int32_t pointer_focus;  /**< Window currently below the pointer. */
    uint32_t selected;      /**< Selected desktop application index. */
    uint32_t capture_kind;  /**< enum desktop_wm_capture_kind. */
    int32_t capture_window; /**< Captured slot or NO_WINDOW. */
    int32_t drag_offset_x;  /**< Pointer-to-window X offset for moves. */
    int32_t drag_offset_y;  /**< Pointer-to-window Y offset for moves. */
    uint32_t resize_edges;  /**< Captured DESKTOP_WM_RESIZE_* mask. */
    int32_t resize_start_x; /**< Pointer X at resize button-down. */
    int32_t resize_start_y; /**< Pointer Y at resize button-down. */
    int32_t resize_window_x; /**< Window X snapshot at resize start. */
    int32_t resize_window_y; /**< Window Y snapshot at resize start. */
    uint32_t resize_window_width;  /**< Width snapshot at resize start. */
    uint32_t resize_window_height; /**< Height snapshot at resize start. */
    int32_t work_left;   /**< Inclusive left work-area edge. */
    int32_t work_top;    /**< Inclusive top work-area edge. */
    int32_t work_right;  /**< Exclusive right work-area edge. */
    int32_t work_bottom; /**< Exclusive bottom work-area edge. */
    uint32_t title_height;  /**< Server decoration title height. */
    uint32_t frame_border;  /**< Server decoration border width. */
    uint32_t resize_margin; /**< Edge hit-test width. */
    uint32_t minimum_width;  /**< Smallest decorated window width. */
    uint32_t minimum_height; /**< Smallest decorated window height. */
    uint32_t screen_width;   /**< Composition surface width. */
    uint32_t screen_height;  /**< Composition surface height. */
} desktop_wm_t;

/** Initialize an empty bounded damage set for one screen geometry. */
void desktop_dirty_initialize(desktop_dirty_region_t *dirty,
                              uint32_t screen_width,
                              uint32_t screen_height);
/** Add, clip and merge one region; overflow becomes one full-screen region. */
void desktop_dirty_add(desktop_dirty_region_t *dirty, desktop_rect_t rect);
/** Merge a bounded source set into an initialized destination set. */
void desktop_dirty_add_regions(desktop_dirty_region_t *destination,
                               const desktop_dirty_region_t *source);
/** Replace an initialized set with one full-screen damage rectangle. */
void desktop_dirty_full(desktop_dirty_region_t *dirty);

/**
 * Initialize deterministic window geometry, Z-order, focus and policy limits.
 *
 * @param work_top Inclusive first Y coordinate available to windows.
 * @param work_bottom Exclusive Y coordinate below the window work area.
 */
void desktop_wm_initialize(desktop_wm_t *manager, uint32_t screen_width,
                           uint32_t screen_height, int32_t work_top,
                           int32_t work_bottom, uint32_t title_height);
/** Return the frontmost visible window at a point, or NO_WINDOW. */
int desktop_wm_window_at(const desktop_wm_t *manager, int32_t x, int32_t y);
/** Return local compositor bounds of one server-side close button. */
desktop_rect_t desktop_wm_close_rect(const desktop_wm_t *manager,
                                     uint32_t window_index);
/** Return decorated bounds including the fixed drop shadow. */
desktop_rect_t desktop_wm_window_bounds(const desktop_wm_t *manager,
                                        uint32_t window_index);
/** Return a DESKTOP_WM_RESIZE_* edge mask for one point. */
uint32_t desktop_wm_resize_edges_at(const desktop_wm_t *manager,
                                    uint32_t window_index,
                                    int32_t x, int32_t y);

/* Low-level transitions retained for focused host tests; runtime input uses
 * desktop_wm_dispatch() so state mutation and damage collection stay atomic. */
uint32_t desktop_wm_open(desktop_wm_t *manager, uint32_t window_index);
/** Hide one window and transfer focus to the highest visible window. */
uint32_t desktop_wm_close(desktop_wm_t *manager, uint32_t window_index);
uint32_t desktop_wm_select(desktop_wm_t *manager, uint32_t window_index);
uint32_t desktop_wm_pointer_press(desktop_wm_t *manager,
                                  int32_t x, int32_t y);
uint32_t desktop_wm_pointer_motion(desktop_wm_t *manager,
                                   int32_t x, int32_t y);
uint32_t desktop_wm_pointer_release(desktop_wm_t *manager,
                                    int32_t x, int32_t y);

/**
 * Apply one validated event and derive all resulting damage and actions.
 *
 * Invalid pointers, event kinds, buttons, edges or targets return `-22` and
 * never publish a partial state transition. Work is bounded by
 * DESKTOP_WM_CAPACITY and DESKTOP_WM_DIRTY_CAPACITY.
 *
 * @return Zero on success or `-22` for an invalid request.
 */
int desktop_wm_dispatch(desktop_wm_t *manager,
                        const desktop_wm_event_t *event,
                        desktop_wm_dispatch_result_t *result);

#endif
