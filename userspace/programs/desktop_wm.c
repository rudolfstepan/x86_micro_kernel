/**
 * @file userspace/programs/desktop_wm.c
 * @brief Bounded Z-order, focus and pointer-capture state transitions.
 */
#include "desktop_wm.h"

static uint32_t point_in_rect(desktop_rect_t rect, int32_t x, int32_t y) {
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return x >= rect.x && y >= rect.y &&
           (int64_t)x < right && (int64_t)y < bottom;
}

static desktop_rect_t window_rect(const desktop_window_t *window) {
    desktop_rect_t rect = {0, 0, 0U, 0U};
    if (window != 0) {
        rect.x = window->x;
        rect.y = window->y;
        rect.width = window->width;
        rect.height = window->height;
    }
    return rect;
}

static desktop_rect_t title_rect(const desktop_wm_t *manager,
                                 uint32_t window_index) {
    desktop_rect_t rect = {0, 0, 0U, 0U};
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY) return rect;
    const desktop_window_t *window = &manager->windows[window_index];
    uint32_t border = manager->frame_border;
    if (window->width <= border * 2U) return rect;
    rect.x = window->x + (int32_t)border;
    rect.y = window->y + (int32_t)border;
    rect.width = window->width - border * 2U;
    rect.height = manager->title_height;
    return rect;
}

desktop_rect_t desktop_wm_close_rect(const desktop_wm_t *manager,
                                     uint32_t window_index) {
    desktop_rect_t title = title_rect(manager, window_index);
    desktop_rect_t close = {0, 0, 0U, 0U};
    if (title.width == 0U || title.height < 8U) return close;
    uint32_t size = title.height - 6U;
    if (size > title.width) size = title.width;
    close.x = title.x + 3;
    close.y = title.y + 3;
    close.width = size;
    close.height = size;
    return close;
}

static void clamp_window(desktop_wm_t *manager, desktop_window_t *window) {
    int32_t maximum_x = manager->work_right - (int32_t)window->width;
    int32_t maximum_y = manager->work_bottom - (int32_t)window->height;
    if (maximum_x < manager->work_left) maximum_x = manager->work_left;
    if (maximum_y < manager->work_top) maximum_y = manager->work_top;
    if (window->x < manager->work_left) window->x = manager->work_left;
    if (window->y < manager->work_top) window->y = manager->work_top;
    if (window->x > maximum_x) window->x = maximum_x;
    if (window->y > maximum_y) window->y = maximum_y;
}

static uint32_t raise_window(desktop_wm_t *manager, uint32_t window_index) {
    uint32_t position = DESKTOP_WM_CAPACITY;
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        if (manager->z_order[index] == window_index) {
            position = index;
            break;
        }
    }
    if (position >= DESKTOP_WM_CAPACITY ||
        position == DESKTOP_WM_CAPACITY - 1U) return 0U;
    for (uint32_t index = position; index + 1U < DESKTOP_WM_CAPACITY; ++index)
        manager->z_order[index] = manager->z_order[index + 1U];
    manager->z_order[DESKTOP_WM_CAPACITY - 1U] = window_index;
    return 1U;
}

static uint32_t focus_window(desktop_wm_t *manager, uint32_t window_index) {
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY ||
        manager->windows[window_index].visible == 0U) return 0U;
    uint32_t changed = manager->focused != (int32_t)window_index ||
                       manager->selected != window_index;
    manager->focused = (int32_t)window_index;
    manager->selected = window_index;
    return changed | raise_window(manager, window_index);
}

static void focus_top_visible(desktop_wm_t *manager) {
    manager->focused = DESKTOP_WM_NO_WINDOW;
    for (uint32_t position = DESKTOP_WM_CAPACITY; position > 0U; --position) {
        uint32_t index = manager->z_order[position - 1U];
        if (index < DESKTOP_WM_CAPACITY && manager->windows[index].visible) {
            manager->focused = (int32_t)index;
            manager->selected = index;
            return;
        }
    }
}

void desktop_wm_initialize(desktop_wm_t *manager, uint32_t screen_width,
                           uint32_t screen_height, int32_t work_top,
                           int32_t work_bottom, uint32_t title_height) {
    if (manager == 0) return;
    manager->work_left = 4;
    manager->work_top = work_top;
    manager->work_right = screen_width > 8U ? (int32_t)screen_width - 4 : 4;
    manager->work_bottom = work_bottom;
    if (manager->work_top < 0) manager->work_top = 0;
    if (manager->work_bottom > (int32_t)screen_height)
        manager->work_bottom = (int32_t)screen_height;
    if (manager->work_bottom <= manager->work_top)
        manager->work_bottom = (int32_t)screen_height;
    manager->title_height = title_height < 18U ? 18U : title_height;
    manager->frame_border = 3U;
    manager->focused = DESKTOP_WM_NO_WINDOW;
    manager->selected = 0U;
    manager->capture_kind = DESKTOP_WM_CAPTURE_NONE;
    manager->capture_window = DESKTOP_WM_NO_WINDOW;
    manager->drag_offset_x = 0;
    manager->drag_offset_y = 0;

    uint32_t available_width = manager->work_right > manager->work_left
        ? (uint32_t)(manager->work_right - manager->work_left) : 1U;
    uint32_t available_height = manager->work_bottom > manager->work_top
        ? (uint32_t)(manager->work_bottom - manager->work_top) : 1U;
    uint32_t window_width = (available_width / 5U) * 3U;
    uint32_t window_height = (available_height / 5U) * 3U;
    uint32_t minimum_height = manager->title_height + 64U;
    if (window_width < 160U) window_width = available_width;
    if (window_height < minimum_height) window_height = available_height;
    if (window_width > available_width) window_width = available_width;
    if (window_height > available_height) window_height = available_height;

    int32_t base_x = manager->work_left + (int32_t)(available_width / 5U);
    int32_t base_y = manager->work_top + 12;
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        desktop_window_t *window = &manager->windows[index];
        window->x = base_x + (int32_t)(index * 36U);
        window->y = base_y + (int32_t)(index * 28U);
        window->width = window_width;
        window->height = window_height;
        window->app_index = index;
        window->visible = index < 2U ? 1U : 0U;
        manager->z_order[index] = index;
        clamp_window(manager, window);
    }
    manager->focused = 0;
    (void)raise_window(manager, 0U);
}

int desktop_wm_window_at(const desktop_wm_t *manager, int32_t x, int32_t y) {
    if (manager == 0) return DESKTOP_WM_NO_WINDOW;
    for (uint32_t position = DESKTOP_WM_CAPACITY; position > 0U; --position) {
        uint32_t index = manager->z_order[position - 1U];
        if (index >= DESKTOP_WM_CAPACITY ||
            manager->windows[index].visible == 0U) continue;
        if (point_in_rect(window_rect(&manager->windows[index]), x, y))
            return (int)index;
    }
    return DESKTOP_WM_NO_WINDOW;
}

uint32_t desktop_wm_open(desktop_wm_t *manager, uint32_t window_index) {
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY) return 0U;
    uint32_t changed = manager->windows[window_index].visible == 0U;
    manager->windows[window_index].visible = 1U;
    return changed | focus_window(manager, window_index);
}

uint32_t desktop_wm_select(desktop_wm_t *manager, uint32_t window_index) {
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY) return 0U;
    uint32_t changed = manager->selected != window_index;
    manager->selected = window_index;
    if (manager->windows[window_index].visible)
        changed |= focus_window(manager, window_index);
    return changed;
}

uint32_t desktop_wm_pointer_press(desktop_wm_t *manager,
                                  int32_t x, int32_t y) {
    if (manager == 0 || manager->capture_kind != DESKTOP_WM_CAPTURE_NONE)
        return 0U;
    int window_index = desktop_wm_window_at(manager, x, y);
    if (window_index == DESKTOP_WM_NO_WINDOW) return 0U;
    uint32_t index = (uint32_t)window_index;
    uint32_t changed = focus_window(manager, index);
    if (point_in_rect(desktop_wm_close_rect(manager, index), x, y)) {
        manager->capture_kind = DESKTOP_WM_CAPTURE_CLOSE;
        manager->capture_window = window_index;
    } else if (point_in_rect(title_rect(manager, index), x, y)) {
        manager->capture_kind = DESKTOP_WM_CAPTURE_MOVE;
        manager->capture_window = window_index;
        manager->drag_offset_x = x - manager->windows[index].x;
        manager->drag_offset_y = y - manager->windows[index].y;
    }
    return changed;
}

uint32_t desktop_wm_pointer_motion(desktop_wm_t *manager,
                                   int32_t x, int32_t y) {
    if (manager == 0 || manager->capture_kind != DESKTOP_WM_CAPTURE_MOVE ||
        manager->capture_window < 0 ||
        manager->capture_window >= (int32_t)DESKTOP_WM_CAPACITY) return 0U;
    desktop_window_t *window = &manager->windows[manager->capture_window];
    int64_t proposed_x = (int64_t)x - manager->drag_offset_x;
    int64_t proposed_y = (int64_t)y - manager->drag_offset_y;
    int32_t old_x = window->x;
    int32_t old_y = window->y;
    window->x = proposed_x < INT32_MIN ? INT32_MIN :
                (proposed_x > INT32_MAX ? INT32_MAX : (int32_t)proposed_x);
    window->y = proposed_y < INT32_MIN ? INT32_MIN :
                (proposed_y > INT32_MAX ? INT32_MAX : (int32_t)proposed_y);
    clamp_window(manager, window);
    return old_x != window->x || old_y != window->y;
}

uint32_t desktop_wm_pointer_release(desktop_wm_t *manager,
                                    int32_t x, int32_t y) {
    if (manager == 0) return 0U;
    uint32_t changed = 0U;
    int32_t captured = manager->capture_window;
    uint32_t capture_kind = manager->capture_kind;
    manager->capture_kind = DESKTOP_WM_CAPTURE_NONE;
    manager->capture_window = DESKTOP_WM_NO_WINDOW;
    if (capture_kind == DESKTOP_WM_CAPTURE_CLOSE && captured >= 0 &&
        captured < (int32_t)DESKTOP_WM_CAPACITY &&
        manager->windows[captured].visible &&
        point_in_rect(desktop_wm_close_rect(manager, (uint32_t)captured),
                      x, y)) {
        manager->windows[captured].visible = 0U;
        changed = 1U;
        if (manager->focused == captured) focus_top_visible(manager);
    }
    return changed;
}
