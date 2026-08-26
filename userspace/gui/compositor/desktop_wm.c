/**
 * @file userspace/gui/compositor/desktop_wm.c
 * @brief Bounded Z-order, focus and pointer-capture state transitions.
 *
 * Input policy and geometry are independent of rendering. The dispatcher
 * snapshots the small fixed state, applies one transition and derives damage
 * from the before/after states, so callers never guess which covered pixels
 * became visible.
 */
#include "desktop_wm.h"

static uint32_t rects_intersect(desktop_rect_t left, desktop_rect_t right) {
    int64_t left_right = (int64_t)left.x + left.width;
    int64_t left_bottom = (int64_t)left.y + left.height;
    int64_t right_right = (int64_t)right.x + right.width;
    int64_t right_bottom = (int64_t)right.y + right.height;
    return (int64_t)left.x < right_right &&
           (int64_t)right.x < left_right &&
           (int64_t)left.y < right_bottom &&
           (int64_t)right.y < left_bottom;
}

static desktop_rect_t rect_union(desktop_rect_t left, desktop_rect_t right) {
    int32_t x = left.x < right.x ? left.x : right.x;
    int32_t y = left.y < right.y ? left.y : right.y;
    int64_t left_right = (int64_t)left.x + left.width;
    int64_t right_right = (int64_t)right.x + right.width;
    int64_t left_bottom = (int64_t)left.y + left.height;
    int64_t right_bottom = (int64_t)right.y + right.height;
    int64_t maximum_x = left_right > right_right ? left_right : right_right;
    int64_t maximum_y = left_bottom > right_bottom
        ? left_bottom : right_bottom;
    desktop_rect_t result = {
        x, y, (uint32_t)(maximum_x - x), (uint32_t)(maximum_y - y)
    };
    return result;
}

static uint32_t rect_merge_efficient(desktop_rect_t left,
                                     desktop_rect_t right,
                                     desktop_rect_t combined) {
    uint64_t left_area = (uint64_t)left.width * left.height;
    uint64_t right_area = (uint64_t)right.width * right.height;
    uint64_t combined_area = (uint64_t)combined.width * combined.height;
    if (UINT64_MAX - left_area < right_area) return 1U;
    uint64_t separate_area = left_area + right_area;
    if (separate_area > UINT64_MAX / 2U) return 1U;
    /* A bounded amount of overdraw is cheaper than another region. Thin
     * perpendicular edges, however, must not inflate into a full window. */
    return combined_area <= separate_area * 2U;
}

void desktop_dirty_initialize(desktop_dirty_region_t *dirty,
                              uint32_t screen_width,
                              uint32_t screen_height) {
    if (dirty == 0) return;
    dirty->count = 0U;
    dirty->full = 0U;
    dirty->screen_width = screen_width;
    dirty->screen_height = screen_height;
}

void desktop_dirty_full(desktop_dirty_region_t *dirty) {
    if (dirty == 0 || dirty->screen_width == 0U ||
        dirty->screen_height == 0U) return;
    dirty->rects[0] = (desktop_rect_t){
        0, 0, dirty->screen_width, dirty->screen_height
    };
    dirty->count = 1U;
    dirty->full = 1U;
}

void desktop_dirty_add(desktop_dirty_region_t *dirty, desktop_rect_t rect) {
    if (dirty == 0 || dirty->full || rect.width == 0U || rect.height == 0U ||
        dirty->screen_width == 0U || dirty->screen_height == 0U) return;
    int64_t left = rect.x;
    int64_t top = rect.y;
    int64_t right = left + rect.width;
    int64_t bottom = top + rect.height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int64_t)dirty->screen_width) right = dirty->screen_width;
    if (bottom > (int64_t)dirty->screen_height)
        bottom = dirty->screen_height;
    if (left >= right || top >= bottom) return;
    rect = (desktop_rect_t){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top)
    };

    for (uint32_t index = 0U; index < dirty->count;) {
        if (!rects_intersect(rect, dirty->rects[index])) {
            ++index;
            continue;
        }
        desktop_rect_t combined = rect_union(rect, dirty->rects[index]);
        if (!rect_merge_efficient(rect, dirty->rects[index], combined)) {
            ++index;
            continue;
        }
        rect = combined;
        --dirty->count;
        dirty->rects[index] = dirty->rects[dirty->count];
        /* The enlarged union can overlap regions examined earlier. */
        index = 0U;
    }
    if (dirty->count == DESKTOP_WM_DIRTY_CAPACITY) {
        desktop_dirty_full(dirty);
        return;
    }
    dirty->rects[dirty->count++] = rect;
}

void desktop_dirty_add_regions(desktop_dirty_region_t *destination,
                               const desktop_dirty_region_t *source) {
    if (destination == 0 || source == 0) return;
    if (source->full) {
        desktop_dirty_full(destination);
        return;
    }
    for (uint32_t index = 0U; index < source->count; ++index)
        desktop_dirty_add(destination, source->rects[index]);
}

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

desktop_rect_t desktop_wm_window_bounds(const desktop_wm_t *manager,
                                        uint32_t window_index) {
    desktop_rect_t rect = {0, 0, 0U, 0U};
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY) return rect;
    rect = window_rect(&manager->windows[window_index]);
    if (rect.width <= UINT32_MAX - 4U) rect.width += 4U;
    if (rect.height <= UINT32_MAX - 4U) rect.height += 4U;
    return rect;
}

uint32_t desktop_wm_resize_edges_at(const desktop_wm_t *manager,
                                    uint32_t window_index,
                                    int32_t x, int32_t y) {
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY ||
        manager->windows[window_index].visible == 0U) return 0U;
    const desktop_window_t *window = &manager->windows[window_index];
    desktop_rect_t bounds = window_rect(window);
    if (!point_in_rect(bounds, x, y)) return 0U;
    int64_t left = window->x;
    int64_t top = window->y;
    int64_t right = left + window->width;
    int64_t bottom = top + window->height;
    uint32_t margin = manager->resize_margin;
    uint32_t edges = 0U;
    if ((int64_t)x < left + margin)
        edges |= DESKTOP_WM_RESIZE_LEFT;
    else if ((int64_t)x >= right - margin)
        edges |= DESKTOP_WM_RESIZE_RIGHT;
    if ((int64_t)y < top + margin)
        edges |= DESKTOP_WM_RESIZE_TOP;
    else if ((int64_t)y >= bottom - margin)
        edges |= DESKTOP_WM_RESIZE_BOTTOM;
    return edges;
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

static uint32_t resize_window(desktop_wm_t *manager,
                              desktop_window_t *window,
                              int32_t pointer_x, int32_t pointer_y) {
    int64_t left = manager->resize_window_x;
    int64_t top = manager->resize_window_y;
    int64_t right = left + manager->resize_window_width;
    int64_t bottom = top + manager->resize_window_height;
    int64_t delta_x = (int64_t)pointer_x - manager->resize_start_x;
    int64_t delta_y = (int64_t)pointer_y - manager->resize_start_y;
    if (manager->resize_edges & DESKTOP_WM_RESIZE_LEFT) left += delta_x;
    if (manager->resize_edges & DESKTOP_WM_RESIZE_RIGHT) right += delta_x;
    if (manager->resize_edges & DESKTOP_WM_RESIZE_TOP) top += delta_y;
    if (manager->resize_edges & DESKTOP_WM_RESIZE_BOTTOM) bottom += delta_y;

    /* Clamp only the dragged edge; its opposite edge remains invariant. */
    if (manager->resize_edges & DESKTOP_WM_RESIZE_LEFT) {
        int64_t maximum_left = right - manager->minimum_width;
        if (left < manager->work_left) left = manager->work_left;
        if (left > maximum_left) left = maximum_left;
    } else if (manager->resize_edges & DESKTOP_WM_RESIZE_RIGHT) {
        int64_t minimum_right = left + manager->minimum_width;
        if (right > manager->work_right) right = manager->work_right;
        if (right < minimum_right) right = minimum_right;
    }
    if (manager->resize_edges & DESKTOP_WM_RESIZE_TOP) {
        int64_t maximum_top = bottom - manager->minimum_height;
        if (top < manager->work_top) top = manager->work_top;
        if (top > maximum_top) top = maximum_top;
    } else if (manager->resize_edges & DESKTOP_WM_RESIZE_BOTTOM) {
        int64_t minimum_bottom = top + manager->minimum_height;
        if (bottom > manager->work_bottom) bottom = manager->work_bottom;
        if (bottom < minimum_bottom) bottom = minimum_bottom;
    }

    int32_t old_x = window->x;
    int32_t old_y = window->y;
    uint32_t old_width = window->width;
    uint32_t old_height = window->height;
    window->x = (int32_t)left;
    window->y = (int32_t)top;
    window->width = (uint32_t)(right - left);
    window->height = (uint32_t)(bottom - top);
    return old_x != window->x || old_y != window->y ||
           old_width != window->width || old_height != window->height;
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
    uint32_t changed = manager->keyboard_focus != (int32_t)window_index ||
                       manager->selected != window_index;
    manager->keyboard_focus = (int32_t)window_index;
    manager->selected = window_index;
    return changed | raise_window(manager, window_index);
}

static void focus_top_visible(desktop_wm_t *manager) {
    manager->keyboard_focus = DESKTOP_WM_NO_WINDOW;
    for (uint32_t position = DESKTOP_WM_CAPACITY; position > 0U; --position) {
        uint32_t index = manager->z_order[position - 1U];
        if (index < DESKTOP_WM_CAPACITY && manager->windows[index].visible) {
            manager->keyboard_focus = (int32_t)index;
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
    manager->screen_width = screen_width;
    manager->screen_height = screen_height;
    manager->keyboard_focus = DESKTOP_WM_NO_WINDOW;
    manager->pointer_focus = DESKTOP_WM_NO_WINDOW;
    manager->selected = 0U;
    manager->capture_kind = DESKTOP_WM_CAPTURE_NONE;
    manager->capture_window = DESKTOP_WM_NO_WINDOW;
    manager->drag_offset_x = 0;
    manager->drag_offset_y = 0;
    manager->resize_edges = 0U;
    manager->resize_start_x = 0;
    manager->resize_start_y = 0;
    manager->resize_window_x = 0;
    manager->resize_window_y = 0;
    manager->resize_window_width = 0U;
    manager->resize_window_height = 0U;
    manager->resize_margin = 6U;

    uint32_t available_width = manager->work_right > manager->work_left
        ? (uint32_t)(manager->work_right - manager->work_left) : 1U;
    uint32_t available_height = manager->work_bottom > manager->work_top
        ? (uint32_t)(manager->work_bottom - manager->work_top) : 1U;
    manager->minimum_width = available_width < 160U
        ? available_width : 160U;
    uint32_t desired_minimum_height =
        manager->title_height > UINT32_MAX - 64U
            ? UINT32_MAX : manager->title_height + 64U;
    manager->minimum_height = available_height < desired_minimum_height
        ? available_height : desired_minimum_height;
    uint32_t window_width = (available_width / 5U) * 3U;
    uint32_t window_height = (available_height / 5U) * 3U;
    if (window_width < manager->minimum_width) window_width = available_width;
    if (window_height < manager->minimum_height)
        window_height = available_height;
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
        window->content_id = index;
        window->flags = 0U;
        /* Slots are geometry only until a compositor client publishes valid
         * content and explicitly opens one.  This prevents placeholder
         * windows from becoming visible without a backing application. */
        window->visible = 0U;
        manager->z_order[index] = index;
        clamp_window(manager, window);
    }
    manager->keyboard_focus = DESKTOP_WM_NO_WINDOW;
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

uint32_t desktop_wm_close(desktop_wm_t *manager, uint32_t window_index) {
    if (manager == 0 || window_index >= DESKTOP_WM_CAPACITY ||
        manager->windows[window_index].visible == 0U) return 0U;
    manager->windows[window_index].visible = 0U;
    if (manager->keyboard_focus == (int32_t)window_index)
        focus_top_visible(manager);
    if (manager->pointer_focus == (int32_t)window_index)
        manager->pointer_focus = DESKTOP_WM_NO_WINDOW;
    return 1U;
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
    manager->pointer_focus = window_index;
    if (window_index == DESKTOP_WM_NO_WINDOW) return 0U;
    uint32_t index = (uint32_t)window_index;
    uint32_t changed = focus_window(manager, index);
    /* Hit-test precedence is close, resize edge, title, then client area. */
    if (point_in_rect(desktop_wm_close_rect(manager, index), x, y)) {
        manager->capture_kind = DESKTOP_WM_CAPTURE_CLOSE;
        manager->capture_window = window_index;
    } else if ((manager->resize_edges = desktop_wm_resize_edges_at(
                    manager, index, x, y)) != 0U) {
        const desktop_window_t *window = &manager->windows[index];
        manager->capture_kind = DESKTOP_WM_CAPTURE_RESIZE;
        manager->capture_window = window_index;
        manager->resize_start_x = x;
        manager->resize_start_y = y;
        manager->resize_window_x = window->x;
        manager->resize_window_y = window->y;
        manager->resize_window_width = window->width;
        manager->resize_window_height = window->height;
    } else if (point_in_rect(title_rect(manager, index), x, y)) {
        manager->capture_kind = DESKTOP_WM_CAPTURE_MOVE;
        manager->capture_window = window_index;
        manager->drag_offset_x = x - manager->windows[index].x;
        manager->drag_offset_y = y - manager->windows[index].y;
    } else {
        /* Keep an implicit grab on ordinary client presses as established
         * desktop protocols do; motion cannot retarget until Button-Up. */
        manager->capture_kind = DESKTOP_WM_CAPTURE_CLIENT;
        manager->capture_window = window_index;
    }
    return changed;
}

uint32_t desktop_wm_pointer_motion(desktop_wm_t *manager,
                                   int32_t x, int32_t y) {
    if (manager == 0) return 0U;
    if (manager->capture_kind == DESKTOP_WM_CAPTURE_NONE) {
        manager->pointer_focus = desktop_wm_window_at(manager, x, y);
        return 0U;
    }
    manager->pointer_focus = manager->capture_window;
    if (manager->capture_window < 0 ||
        manager->capture_window >= (int32_t)DESKTOP_WM_CAPACITY) return 0U;
    desktop_window_t *window = &manager->windows[manager->capture_window];
    if (manager->capture_kind == DESKTOP_WM_CAPTURE_RESIZE)
        return resize_window(manager, window, x, y);
    if (manager->capture_kind != DESKTOP_WM_CAPTURE_MOVE) return 0U;
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
    /* Release belongs to the button-down owner, never the current hover. */
    int32_t captured = manager->capture_window;
    uint32_t capture_kind = manager->capture_kind;
    manager->capture_kind = DESKTOP_WM_CAPTURE_NONE;
    manager->capture_window = DESKTOP_WM_NO_WINDOW;
    manager->resize_edges = 0U;
    if (capture_kind == DESKTOP_WM_CAPTURE_CLOSE && captured >= 0 &&
        captured < (int32_t)DESKTOP_WM_CAPACITY &&
        manager->windows[captured].visible &&
        point_in_rect(desktop_wm_close_rect(manager, (uint32_t)captured),
                      x, y)) {
        changed = desktop_wm_close(manager, (uint32_t)captured);
    }
    manager->pointer_focus = desktop_wm_window_at(manager, x, y);
    return changed;
}

typedef struct {
    desktop_window_t windows[DESKTOP_WM_CAPACITY];
    uint32_t z_order[DESKTOP_WM_CAPACITY];
    int32_t keyboard_focus;
    uint32_t selected;
} desktop_wm_snapshot_t;

static void take_snapshot(const desktop_wm_t *manager,
                          desktop_wm_snapshot_t *snapshot) {
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        snapshot->windows[index] = manager->windows[index];
        snapshot->z_order[index] = manager->z_order[index];
    }
    snapshot->keyboard_focus = manager->keyboard_focus;
    snapshot->selected = manager->selected;
}

static uint32_t z_position(const uint32_t *z_order, uint32_t window_index) {
    for (uint32_t position = 0U; position < DESKTOP_WM_CAPACITY; ++position) {
        if (z_order[position] == window_index) return position;
    }
    return DESKTOP_WM_CAPACITY;
}

static uint32_t window_geometry_changed(const desktop_window_t *before,
                                        const desktop_window_t *after) {
    return before->x != after->x || before->y != after->y ||
           before->width != after->width ||
           before->height != after->height ||
           before->visible != after->visible;
}

static desktop_rect_t snapshot_window_bounds(const desktop_window_t *window) {
    desktop_rect_t rect = window_rect(window);
    if (rect.width <= UINT32_MAX - 4U) rect.width += 4U;
    if (rect.height <= UINT32_MAX - 4U) rect.height += 4U;
    return rect;
}

static void collect_right_bottom_resize_damage(
    const desktop_wm_t *manager, const desktop_window_t *before,
    const desktop_window_t *after, desktop_dirty_region_t *dirty) {
    desktop_rect_t old_bounds = snapshot_window_bounds(before);
    desktop_rect_t new_bounds = snapshot_window_bounds(after);
    int64_t old_right = (int64_t)before->x + before->width;
    int64_t new_right = (int64_t)after->x + after->width;
    int64_t old_bottom = (int64_t)before->y + before->height;
    int64_t new_bottom = (int64_t)after->y + after->height;
    int64_t maximum_right = (int64_t)old_bounds.x + old_bounds.width;
    int64_t new_bounds_right = (int64_t)new_bounds.x + new_bounds.width;
    int64_t maximum_bottom = (int64_t)old_bounds.y + old_bounds.height;
    int64_t new_bounds_bottom = (int64_t)new_bounds.y + new_bounds.height;
    int32_t minimum_y = old_bounds.y < new_bounds.y
        ? old_bounds.y : new_bounds.y;
    int32_t minimum_x = old_bounds.x < new_bounds.x
        ? old_bounds.x : new_bounds.x;
    uint32_t damage_inset = manager->frame_border;
    if (manager->resize_margin <= UINT32_MAX / 2U &&
        manager->resize_margin * 2U > damage_inset)
        damage_inset = manager->resize_margin * 2U;
    if (new_bounds_right > maximum_right) maximum_right = new_bounds_right;
    if (new_bounds_bottom > maximum_bottom)
        maximum_bottom = new_bounds_bottom;

    if ((manager->resize_edges & DESKTOP_WM_RESIZE_RIGHT) != 0U) {
        int64_t start = old_right < new_right ? old_right : new_right;
        if (start >= (int64_t)damage_inset)
            start -= damage_inset;
        desktop_dirty_add(dirty, (desktop_rect_t){
            (int32_t)start, minimum_y,
            (uint32_t)(maximum_right - start),
            (uint32_t)(maximum_bottom - minimum_y),
        });
    }
    if ((manager->resize_edges & DESKTOP_WM_RESIZE_BOTTOM) != 0U) {
        int64_t start = old_bottom < new_bottom ? old_bottom : new_bottom;
        if (start >= (int64_t)damage_inset)
            start -= damage_inset;
        desktop_dirty_add(dirty, (desktop_rect_t){
            minimum_x, (int32_t)start,
            (uint32_t)(maximum_right - minimum_x),
            (uint32_t)(maximum_bottom - start),
        });
    }
}

static void collect_state_damage(const desktop_wm_t *manager,
                                 const desktop_wm_snapshot_t *before,
                                 desktop_wm_dispatch_result_t *result) {
    for (uint32_t index = 0U; index < DESKTOP_WM_CAPACITY; ++index) {
        const desktop_window_t *old_window = &before->windows[index];
        const desktop_window_t *new_window = &manager->windows[index];
        uint32_t geometry_changed =
            window_geometry_changed(old_window, new_window);
        uint32_t order_changed =
            z_position(before->z_order, index) !=
            z_position(manager->z_order, index);
        uint32_t focus_changed =
            (before->keyboard_focus == (int32_t)index) !=
            (manager->keyboard_focus == (int32_t)index);
        uint32_t edge_resize = geometry_changed && !order_changed &&
            !focus_changed && old_window->visible && new_window->visible &&
            (new_window->flags & DESKTOP_WM_WINDOW_RETAINED_RESIZE) != 0U &&
            manager->capture_kind == DESKTOP_WM_CAPTURE_RESIZE &&
            manager->capture_window == (int32_t)index &&
            (manager->resize_edges &
                (DESKTOP_WM_RESIZE_LEFT | DESKTOP_WM_RESIZE_TOP)) == 0U &&
            (manager->resize_edges &
                (DESKTOP_WM_RESIZE_RIGHT | DESKTOP_WM_RESIZE_BOTTOM)) != 0U;
        /* Recompose both sides of geometry, order and focus transitions. */
        if (old_window->visible &&
            (geometry_changed || order_changed || focus_changed) &&
            !edge_resize) {
            desktop_dirty_add(&result->dirty,
                              snapshot_window_bounds(old_window));
        }
        if (new_window->visible &&
            (geometry_changed || order_changed || focus_changed) &&
            !edge_resize) {
            desktop_dirty_add(&result->dirty,
                              desktop_wm_window_bounds(manager, index));
        }
        /* Right/bottom resizing leaves existing client pixels in place.
         * Redraw only swept frame/shadow bands. Left/top resizing moves the
         * client origin and therefore retains the safe full damage above. */
        if (edge_resize)
            collect_right_bottom_resize_damage(
                manager, old_window, new_window, &result->dirty);
    }
    if (before->selected != manager->selected) {
        result->flags |= DESKTOP_WM_RESULT_SELECTION_CHANGED;
        result->previous_selected = before->selected;
        result->selected = manager->selected;
    }
    if (result->dirty.count != 0U ||
        (result->flags & DESKTOP_WM_RESULT_SELECTION_CHANGED) != 0U)
        result->flags |= DESKTOP_WM_RESULT_REDRAW;
}

int desktop_wm_dispatch(desktop_wm_t *manager,
                        const desktop_wm_event_t *event,
                        desktop_wm_dispatch_result_t *result) {
    if (manager == 0 || event == 0 || result == 0) return -22;
    desktop_dirty_initialize(&result->dirty, manager->screen_width,
                             manager->screen_height);
    result->flags = 0U;
    result->target = DESKTOP_WM_NO_TARGET;
    result->previous_selected = manager->selected;
    result->selected = manager->selected;

    desktop_wm_snapshot_t before;
    take_snapshot(manager, &before);
    if (event->type == DESKTOP_WM_EVENT_POINTER_MOTION) {
        (void)desktop_wm_pointer_motion(manager, event->x, event->y);
    } else if (event->type == DESKTOP_WM_EVENT_POINTER_BUTTON) {
        if (event->button != DESKTOP_WM_BUTTON_LEFT || event->pressed > 1U)
            return -22;
        if (event->pressed)
            (void)desktop_wm_pointer_press(manager, event->x, event->y);
        else
            (void)desktop_wm_pointer_release(manager, event->x, event->y);
    } else if (event->type == DESKTOP_WM_EVENT_OPEN) {
        if (event->target >= DESKTOP_WM_CAPACITY) return -22;
        (void)desktop_wm_open(manager, event->target);
    } else if (event->type == DESKTOP_WM_EVENT_SELECT) {
        if (event->target >= DESKTOP_WM_CAPACITY) return -22;
        (void)desktop_wm_select(manager, event->target);
    } else if (event->type == DESKTOP_WM_EVENT_CLOSE) {
        if (event->target >= DESKTOP_WM_CAPACITY) return -22;
        (void)desktop_wm_close(manager, event->target);
    } else if (event->type == DESKTOP_WM_EVENT_KEYBOARD) {
        uint32_t next = manager->selected;
        if (event->key == DESKTOP_WM_KEY_TAB ||
            event->key == DESKTOP_WM_KEY_RIGHT) {
            next = (manager->selected + 1U) % DESKTOP_WM_CAPACITY;
            (void)desktop_wm_select(manager, next);
        } else if (event->key == DESKTOP_WM_KEY_LEFT) {
            next = (manager->selected + DESKTOP_WM_CAPACITY - 1U) %
                   DESKTOP_WM_CAPACITY;
            (void)desktop_wm_select(manager, next);
        } else if (event->key == DESKTOP_WM_KEY_UP) {
            next = (manager->selected + DESKTOP_WM_CAPACITY - 2U) %
                   DESKTOP_WM_CAPACITY;
            (void)desktop_wm_select(manager, next);
        } else if (event->key == DESKTOP_WM_KEY_DOWN) {
            next = (manager->selected + 2U) % DESKTOP_WM_CAPACITY;
            (void)desktop_wm_select(manager, next);
        } else if (event->key == DESKTOP_WM_KEY_ENTER) {
            (void)desktop_wm_open(manager, manager->selected);
            result->flags |= DESKTOP_WM_RESULT_LAUNCH;
            result->target = manager->selected;
        } else if (event->key == DESKTOP_WM_KEY_ESCAPE) {
            result->flags |= DESKTOP_WM_RESULT_EXIT;
        } else {
            return -22;
        }
    } else {
        return -22;
    }

    collect_state_damage(manager, &before, result);
    result->selected = manager->selected;
    return 0;
}
