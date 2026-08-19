/**
 * @file userspace/programs/desktop_wm.h
 * @brief Fixed-capacity state model for the Ring-3 desktop window manager.
 */
#ifndef USERSPACE_DESKTOP_WM_H
#define USERSPACE_DESKTOP_WM_H

#include <stdint.h>

#define DESKTOP_WM_CAPACITY 4U
#define DESKTOP_WM_NO_WINDOW (-1)

enum {
    DESKTOP_WM_CAPTURE_NONE = 0U,
    DESKTOP_WM_CAPTURE_MOVE,
    DESKTOP_WM_CAPTURE_CLOSE
};

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} desktop_rect_t;

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
    int32_t focused;
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
} desktop_wm_t;

void desktop_wm_initialize(desktop_wm_t *manager, uint32_t screen_width,
                           uint32_t screen_height, int32_t work_top,
                           int32_t work_bottom, uint32_t title_height);
int desktop_wm_window_at(const desktop_wm_t *manager, int32_t x, int32_t y);
desktop_rect_t desktop_wm_close_rect(const desktop_wm_t *manager,
                                     uint32_t window_index);
uint32_t desktop_wm_open(desktop_wm_t *manager, uint32_t window_index);
uint32_t desktop_wm_select(desktop_wm_t *manager, uint32_t window_index);
uint32_t desktop_wm_pointer_press(desktop_wm_t *manager,
                                  int32_t x, int32_t y);
uint32_t desktop_wm_pointer_motion(desktop_wm_t *manager,
                                   int32_t x, int32_t y);
uint32_t desktop_wm_pointer_release(desktop_wm_t *manager,
                                    int32_t x, int32_t y);

#endif
