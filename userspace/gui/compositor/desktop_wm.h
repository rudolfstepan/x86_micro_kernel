/**
 * @file userspace/gui/compositor/desktop_wm.h
 * @brief Fixed-capacity state model for the Ring-3 desktop window manager.
 */
#ifndef USERSPACE_DESKTOP_WM_H
#define USERSPACE_DESKTOP_WM_H

#include <stdint.h>

#define DESKTOP_WM_CAPACITY 4U
#define DESKTOP_WM_DIRTY_CAPACITY 8U
#define DESKTOP_WM_NO_WINDOW (-1)
#define DESKTOP_WM_NO_TARGET UINT32_MAX

enum {
    DESKTOP_WM_CAPTURE_NONE = 0U,
    DESKTOP_WM_CAPTURE_MOVE,
    DESKTOP_WM_CAPTURE_CLOSE,
    DESKTOP_WM_CAPTURE_CLIENT
};

enum {
    DESKTOP_WM_EVENT_POINTER_MOTION = 1U,
    DESKTOP_WM_EVENT_POINTER_BUTTON,
    DESKTOP_WM_EVENT_KEYBOARD,
    DESKTOP_WM_EVENT_OPEN,
    DESKTOP_WM_EVENT_SELECT
};

enum {
    DESKTOP_WM_KEY_TAB = 1U,
    DESKTOP_WM_KEY_UP,
    DESKTOP_WM_KEY_DOWN,
    DESKTOP_WM_KEY_LEFT,
    DESKTOP_WM_KEY_RIGHT,
    DESKTOP_WM_KEY_ENTER,
    DESKTOP_WM_KEY_ESCAPE
};

#define DESKTOP_WM_BUTTON_LEFT 1U

#define DESKTOP_WM_RESULT_REDRAW (1U << 0)
#define DESKTOP_WM_RESULT_LAUNCH (1U << 1)
#define DESKTOP_WM_RESULT_EXIT (1U << 2)
#define DESKTOP_WM_RESULT_SELECTION_CHANGED (1U << 3)

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} desktop_rect_t;

typedef struct {
    desktop_rect_t rects[DESKTOP_WM_DIRTY_CAPACITY];
    uint32_t count;
    uint32_t full;
    uint32_t screen_width;
    uint32_t screen_height;
} desktop_dirty_region_t;

typedef struct {
    uint32_t type;
    int32_t x;
    int32_t y;
    uint32_t button;
    uint32_t pressed;
    uint32_t key;
    uint32_t target;
} desktop_wm_event_t;

typedef struct {
    desktop_dirty_region_t dirty;
    uint32_t flags;
    uint32_t target;
    uint32_t previous_selected;
    uint32_t selected;
} desktop_wm_dispatch_result_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t app_index;
    uint32_t visible;
} desktop_window_t;

typedef struct {
    desktop_window_t windows[DESKTOP_WM_CAPACITY];
    uint32_t z_order[DESKTOP_WM_CAPACITY];
    int32_t keyboard_focus;
    int32_t pointer_focus;
    uint32_t selected;
    uint32_t capture_kind;
    int32_t capture_window;
    int32_t drag_offset_x;
    int32_t drag_offset_y;
    int32_t work_left;
    int32_t work_top;
    int32_t work_right;
    int32_t work_bottom;
    uint32_t title_height;
    uint32_t frame_border;
    uint32_t screen_width;
    uint32_t screen_height;
} desktop_wm_t;

void desktop_dirty_initialize(desktop_dirty_region_t *dirty,
                              uint32_t screen_width,
                              uint32_t screen_height);
void desktop_dirty_add(desktop_dirty_region_t *dirty, desktop_rect_t rect);
void desktop_dirty_add_regions(desktop_dirty_region_t *destination,
                               const desktop_dirty_region_t *source);
void desktop_dirty_full(desktop_dirty_region_t *dirty);

void desktop_wm_initialize(desktop_wm_t *manager, uint32_t screen_width,
                           uint32_t screen_height, int32_t work_top,
                           int32_t work_bottom, uint32_t title_height);
int desktop_wm_window_at(const desktop_wm_t *manager, int32_t x, int32_t y);
desktop_rect_t desktop_wm_close_rect(const desktop_wm_t *manager,
                                     uint32_t window_index);
desktop_rect_t desktop_wm_window_bounds(const desktop_wm_t *manager,
                                        uint32_t window_index);
uint32_t desktop_wm_open(desktop_wm_t *manager, uint32_t window_index);
uint32_t desktop_wm_select(desktop_wm_t *manager, uint32_t window_index);
uint32_t desktop_wm_pointer_press(desktop_wm_t *manager,
                                  int32_t x, int32_t y);
uint32_t desktop_wm_pointer_motion(desktop_wm_t *manager,
                                   int32_t x, int32_t y);
uint32_t desktop_wm_pointer_release(desktop_wm_t *manager,
                                    int32_t x, int32_t y);
int desktop_wm_dispatch(desktop_wm_t *manager,
                        const desktop_wm_event_t *event,
                        desktop_wm_dispatch_result_t *result);

#endif
