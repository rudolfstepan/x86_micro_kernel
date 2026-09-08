#include "userspace/gui/compositor/desktop_wm.h"

#include <assert.h>

static void arrange_overlap(desktop_wm_t *manager) {
    manager->windows[0].x = 100;
    manager->windows[0].y = 100;
    manager->windows[0].width = 300U;
    manager->windows[0].height = 220U;
    manager->windows[0].visible = 1U;
    manager->windows[1].x = 180;
    manager->windows[1].y = 140;
    manager->windows[1].width = 300U;
    manager->windows[1].height = 220U;
    manager->windows[1].visible = 1U;
    manager->z_order[0] = 0U;
    manager->z_order[1] = 1U;
    manager->z_order[2] = 2U;
    manager->z_order[3] = 3U;
    manager->keyboard_focus = 1;
}

static void test_dirty_regions_and_event_dispatch(void) {
    desktop_dirty_region_t dirty;
    desktop_dirty_initialize(&dirty, 1024U, 768U);
    desktop_dirty_add(&dirty, (desktop_rect_t){100, 100, 500U, 3U});
    desktop_dirty_add(&dirty, (desktop_rect_t){100, 100, 3U, 300U});
    assert(dirty.count == 2U);

    desktop_dirty_initialize(&dirty, 1024U, 768U);
    desktop_dirty_add(&dirty, (desktop_rect_t){10, 10, 20U, 20U});
    desktop_dirty_add(&dirty, (desktop_rect_t){25, 20, 20U, 20U});
    assert(dirty.count == 1U);
    assert(dirty.rects[0].x == 10 && dirty.rects[0].y == 10);
    assert(dirty.rects[0].width == 35U && dirty.rects[0].height == 30U);

    desktop_dirty_initialize(&dirty, 1024U, 768U);
    desktop_dirty_add(&dirty, (desktop_rect_t){40, 40, 30U, 20U});
    desktop_dirty_add(&dirty, (desktop_rect_t){70, 40, 30U, 20U});
    assert(dirty.count == 1U);
    assert(dirty.rects[0].x == 40 && dirty.rects[0].y == 40);
    assert(dirty.rects[0].width == 60U && dirty.rects[0].height == 20U);

    desktop_dirty_initialize(&dirty, 1024U, 768U);
    desktop_dirty_add(&dirty, (desktop_rect_t){40, 40, 30U, 20U});
    desktop_dirty_add(&dirty, (desktop_rect_t){70, 60, 30U, 20U});
    assert(dirty.count == 2U);
    for (uint32_t index = 0U;
         index < DESKTOP_WM_DIRTY_CAPACITY + 1U; ++index) {
        desktop_dirty_add(
            &dirty, (desktop_rect_t){(int32_t)(index * 100U),
                                     (int32_t)(index * 60U), 8U, 8U});
    }
    assert(dirty.count == 1U && dirty.full != 0U);
    assert(dirty.rects[0].width == 1024U);
    assert(dirty.rects[0].height == 768U);

    desktop_wm_t manager;
    desktop_wm_initialize(&manager, 1024U, 768U, 36, 736, 24U);
    arrange_overlap(&manager);
    manager.keyboard_focus = 1;
    manager.pointer_focus = DESKTOP_WM_NO_WINDOW;

    desktop_wm_event_t client_press = {
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = 300,
        .y = 260,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 1U,
    };
    desktop_wm_dispatch_result_t result;
    assert(desktop_wm_dispatch(&manager, &client_press, &result) == 0);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_CLIENT);
    assert(manager.capture_window == 1);
    desktop_wm_event_t client_drag = {
        .type = DESKTOP_WM_EVENT_POINTER_MOTION,
        .x = 20,
        .y = 70,
    };
    assert(desktop_wm_dispatch(&manager, &client_drag, &result) == 0);
    assert(manager.pointer_focus == 1);
    desktop_wm_event_t client_release = {
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = 20,
        .y = 70,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 0U,
    };
    assert(desktop_wm_dispatch(&manager, &client_release, &result) == 0);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_NONE);

    desktop_wm_event_t motion = {
        .type = DESKTOP_WM_EVENT_POINTER_MOTION,
        .x = 120,
        .y = 125,
    };
    assert(desktop_wm_dispatch(&manager, &motion, &result) == 0);
    assert(manager.pointer_focus == 0);
    assert(manager.keyboard_focus == 1);
    assert(result.dirty.count == 0U);

    desktop_wm_event_t press = {
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = 160,
        .y = 110,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 1U,
    };
    assert(desktop_wm_dispatch(&manager, &press, &result) == 0);
    assert(manager.pointer_focus == 0);
    assert(manager.keyboard_focus == 0);
    assert(manager.capture_window == 0);

    /* Capture preserves the exact title-bar pixel under the cursor, even
     * after raising a background window. Neither press nor zero motion jumps. */
    assert(manager.windows[0].x == 100 && manager.windows[0].y == 100);
    assert(manager.drag_offset_x == 60 && manager.drag_offset_y == 10);
    assert(desktop_wm_pointer_motion(&manager, 160, 110) == 0U);
    assert(manager.windows[0].x == 100 && manager.windows[0].y == 100);
    assert(desktop_wm_pointer_motion(&manager, 300, 240) != 0U);
    assert(manager.windows[0].x == 240 && manager.windows[0].y == 230);
    assert(desktop_wm_pointer_motion(&manager, 160, 110) != 0U);
    assert(manager.windows[0].x == 100 && manager.windows[0].y == 100);
    assert(result.flags & DESKTOP_WM_RESULT_REDRAW);
    assert(result.dirty.count != 0U);

    desktop_wm_event_t drag = {
        .type = DESKTOP_WM_EVENT_POINTER_MOTION,
        .x = 300,
        .y = 240,
    };
    assert(desktop_wm_dispatch(&manager, &drag, &result) == 0);
    assert(result.flags & DESKTOP_WM_RESULT_REDRAW);
    assert(result.dirty.count != 0U);

    desktop_wm_event_t key = {
        .type = DESKTOP_WM_EVENT_KEYBOARD,
        .key = DESKTOP_WM_KEY_ENTER,
    };
    assert(desktop_wm_dispatch(&manager, &key, &result) == 0);
    assert(result.flags & DESKTOP_WM_RESULT_LAUNCH);
    assert(result.target == manager.selected);
}

static void test_edge_and_corner_resize(void) {
    desktop_wm_t manager;
    desktop_wm_initialize(&manager, 1024U, 768U, 36, 736, 24U);
    for (uint32_t index = 1U; index < DESKTOP_WM_CAPACITY; ++index)
        manager.windows[index].visible = 0U;
    desktop_window_t *window = &manager.windows[0];
    window->visible = 1U;
    window->x = 200;
    window->y = 150;
    window->width = 300U;
    window->height = 220U;

    int32_t right = window->x + (int32_t)window->width - 1;
    int32_t bottom = window->y + (int32_t)window->height - 1;
    int32_t center_x = window->x + (int32_t)(window->width / 2U);
    int32_t center_y = window->y + (int32_t)(window->height / 2U);
    const struct {
        int32_t x;
        int32_t y;
        uint32_t edges;
    } hit_cases[] = {
        {window->x, center_y, DESKTOP_WM_RESIZE_LEFT},
        {right, center_y, DESKTOP_WM_RESIZE_RIGHT},
        {center_x, window->y, DESKTOP_WM_RESIZE_TOP},
        {center_x, bottom, DESKTOP_WM_RESIZE_BOTTOM},
        {window->x, window->y,
         DESKTOP_WM_RESIZE_LEFT | DESKTOP_WM_RESIZE_TOP},
        {right, window->y,
         DESKTOP_WM_RESIZE_RIGHT | DESKTOP_WM_RESIZE_TOP},
        {window->x, bottom,
         DESKTOP_WM_RESIZE_LEFT | DESKTOP_WM_RESIZE_BOTTOM},
        {right, bottom,
         DESKTOP_WM_RESIZE_RIGHT | DESKTOP_WM_RESIZE_BOTTOM},
    };
    for (uint32_t index = 0U;
         index < sizeof(hit_cases) / sizeof(hit_cases[0]); ++index) {
        assert(desktop_wm_resize_edges_at(
                   &manager, 0U, hit_cases[index].x, hit_cases[index].y) ==
               hit_cases[index].edges);
    }
    assert(desktop_wm_resize_edges_at(&manager, 0U, right, bottom) ==
           (DESKTOP_WM_RESIZE_RIGHT | DESKTOP_WM_RESIZE_BOTTOM));
    desktop_wm_event_t press = {
        .type = DESKTOP_WM_EVENT_POINTER_BUTTON,
        .x = right,
        .y = bottom,
        .button = DESKTOP_WM_BUTTON_LEFT,
        .pressed = 1U,
    };
    desktop_wm_dispatch_result_t result;
    assert(desktop_wm_dispatch(&manager, &press, &result) == 0);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE);
    assert(manager.resize_edges ==
           (DESKTOP_WM_RESIZE_RIGHT | DESKTOP_WM_RESIZE_BOTTOM));
    desktop_wm_event_t grow = {
        .type = DESKTOP_WM_EVENT_POINTER_MOTION,
        .x = 2000,
        .y = 2000,
    };
    assert(desktop_wm_dispatch(&manager, &grow, &result) == 0);
    assert(result.flags & DESKTOP_WM_RESULT_REDRAW);
    assert(result.dirty.count != 0U);
    assert(window->x + (int32_t)window->width == manager.work_right);
    assert(window->y + (int32_t)window->height == manager.work_bottom);
    assert(desktop_wm_pointer_release(&manager, 2000, 2000) == 0U);

    window->x = 200;
    window->y = 150;
    window->width = 300U;
    window->height = 220U;
    int32_t fixed_right = window->x + (int32_t)window->width;
    int32_t fixed_bottom = window->y + (int32_t)window->height;
    assert(desktop_wm_resize_edges_at(&manager, 0U,
                                      window->x, window->y) ==
           (DESKTOP_WM_RESIZE_LEFT | DESKTOP_WM_RESIZE_TOP));
    assert(desktop_wm_pointer_press(&manager, window->x, window->y) == 0U);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE);
    assert(desktop_wm_pointer_motion(&manager, 2000, 2000) != 0U);
    assert(window->width == manager.minimum_width);
    assert(window->height == manager.minimum_height);
    assert(window->x + (int32_t)window->width == fixed_right);
    assert(window->y + (int32_t)window->height == fixed_bottom);
    assert(desktop_wm_pointer_release(&manager, 2000, 2000) == 0U);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_NONE);
    assert(manager.resize_edges == 0U);
}

static void arrange_single(desktop_wm_t *manager) {
    desktop_wm_initialize(manager, 1024U, 768U, 36, 736, 24U);
    manager->windows[0] = (desktop_window_t){
        .x = 200, .y = 150, .width = 300U, .height = 220U, .visible = 1U};
    (void)desktop_wm_open(manager, 0U);
}

static void test_complete_corner_pixels(void) {
    desktop_wm_t manager;
    arrange_single(&manager);
    desktop_window_t *window = &manager.windows[0];
    const int32_t origins[][2] = {
        {200, 150}, {-400, -300}, {INT32_MIN, INT32_MIN},
        {INT32_MAX - 299, INT32_MAX - 219}};
    /* Literal 16 freezes the user-visible target independently of production
     * constants. Include each neighbouring pixel, edge strip and shadow. */
    for (uint32_t origin = 0; origin < sizeof(origins) / sizeof(origins[0]); ++origin) {
        window->x = origins[origin][0]; window->y = origins[origin][1];
        for (uint32_t corner = 0; corner < 4U; ++corner) {
            uint32_t horizontal = corner & 1U ? DESKTOP_WM_RESIZE_RIGHT : DESKTOP_WM_RESIZE_LEFT;
            uint32_t vertical = corner & 2U ? DESKTOP_WM_RESIZE_BOTTOM : DESKTOP_WM_RESIZE_TOP;
            for (int32_t dx = -1; dx <= 17; ++dx) {
                for (int32_t dy = -1; dy <= 17; ++dy) {
                    int64_t x = (int64_t)window->x + (corner & 1U ? 299 - dx : dx);
                    int64_t y = (int64_t)window->y + (corner & 2U ? 219 - dy : dy);
                    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) continue;
                    uint32_t expected = 0U;
                    if (dx >= 0 && dy >= 0) {
                        if (dx < 16 && dy < 16) expected = horizontal | vertical;
                        else expected = (dx < 6 ? horizontal : 0U) | (dy < 6 ? vertical : 0U);
                    }
                    manager.resize_margin = 6U;
                    assert(desktop_wm_resize_edges_at(&manager, 0U, (int32_t)x, (int32_t)y) == expected);
                    manager.resize_margin = 0U;
                    assert(desktop_wm_resize_edges_at(&manager, 0U, (int32_t)x, (int32_t)y) == 0U);
                }
            }
        }
    }
    manager.resize_margin = 6U;
    /* right/bottom may exceed INT32_MAX; no wrapped opposite-edge hits. */
    window->x = INT32_MAX - 100; window->y = INT32_MAX - 100;
    window->width = UINT32_MAX; window->height = UINT32_MAX;
    assert(desktop_wm_resize_edges_at(&manager, 0U, INT32_MAX, INT32_MAX) == 0U);
    assert(desktop_wm_resize_edges_at(&manager, 0U, window->x + 15, window->y + 15) ==
           (DESKTOP_WM_RESIZE_LEFT | DESKTOP_WM_RESIZE_TOP));
    assert(desktop_wm_resize_edges_at(&manager, 0U, INT32_MIN, INT32_MIN) == 0U);
    window->visible = 0U;
    assert(desktop_wm_resize_edges_at(&manager, 0U, window->x, window->y) == 0U);
    assert(desktop_wm_resize_edges_at(0, 0U, 0, 0) == 0U);
    assert(desktop_wm_resize_edges_at(&manager, DESKTOP_WM_CAPACITY, 0, 0) == 0U);
    assert(desktop_wm_resize_edges_at(&manager, UINT32_MAX, 0, 0) == 0U);
}

static void test_tiny_corner_partition(void) {
    desktop_wm_t manager;
    arrange_single(&manager);
    desktop_window_t *window = &manager.windows[0];
    manager.resize_margin = 1U;
    /* Odd dimensions have a centre gap, not overlapping opposite corners.
     * Margin 1 distinguishes the clipped corners from ordinary edge strips. */
    for (uint32_t width = 0; width <= 33U; ++width) {
        for (uint32_t height = 0; height <= 33U; ++height) {
            window->width = width; window->height = height;
            uint32_t cx = width < 32U ? width / 2U : 16U;
            uint32_t cy = height < 32U ? height / 2U : 16U;
            for (uint32_t x = 0; x <= width; ++x) {
                for (uint32_t y = 0; y <= height; ++y) {
                    uint32_t expected = 0U;
                    if (x < width && y < height) {
                        uint32_t h = x < cx ? DESKTOP_WM_RESIZE_LEFT :
                            (x >= width - cx ? DESKTOP_WM_RESIZE_RIGHT : 0U);
                        uint32_t v = y < cy ? DESKTOP_WM_RESIZE_TOP :
                            (y >= height - cy ? DESKTOP_WM_RESIZE_BOTTOM : 0U);
                        if (h && v) expected = h | v;
                        else expected = (x == 0U ? DESKTOP_WM_RESIZE_LEFT :
                            (x == width - 1U ? DESKTOP_WM_RESIZE_RIGHT : 0U)) |
                            (y == 0U ? DESKTOP_WM_RESIZE_TOP :
                            (y == height - 1U ? DESKTOP_WM_RESIZE_BOTTOM : 0U));
                    }
                    uint32_t actual = desktop_wm_resize_edges_at(
                        &manager, 0U, window->x + (int32_t)x, window->y + (int32_t)y);
                    assert(actual == expected);
                    assert((actual & (DESKTOP_WM_RESIZE_LEFT | DESKTOP_WM_RESIZE_RIGHT)) !=
                           (DESKTOP_WM_RESIZE_LEFT | DESKTOP_WM_RESIZE_RIGHT));
                    assert((actual & (DESKTOP_WM_RESIZE_TOP | DESKTOP_WM_RESIZE_BOTTOM)) !=
                           (DESKTOP_WM_RESIZE_TOP | DESKTOP_WM_RESIZE_BOTTOM));
                }
            }
        }
    }
}

static void test_corner_capture_and_precedence(void) {
    desktop_wm_t manager;
    for (uint32_t corner = 0; corner < 4U; ++corner) {
        for (int32_t dx = 0; dx < 16; ++dx) {
            for (int32_t dy = 0; dy < 16; ++dy) {
                arrange_single(&manager);
                desktop_window_t *window = &manager.windows[0];
                int32_t x = window->x + (corner & 1U ? 299 - dx : dx);
                int32_t y = window->y + (corner & 2U ? 219 - dy : dy);
                desktop_wm_event_t press = {
                    .type = DESKTOP_WM_EVENT_POINTER_BUTTON, .x = x, .y = y,
                    .button = DESKTOP_WM_BUTTON_LEFT, .pressed = 1U};
                desktop_wm_dispatch_result_t result;
                assert(desktop_wm_dispatch(&manager, &press, &result) == 0);
                assert(manager.capture_window == 0);
                assert(window->x == 200 && window->y == 150);
                assert(window->width == 300U && window->height == 220U);
                desktop_rect_t close = desktop_wm_close_rect(&manager, 0U);
                if (x >= close.x && x < close.x + (int32_t)close.width &&
                    y >= close.y && y < close.y + (int32_t)close.height) {
                    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_CLOSE);
                    (void)desktop_wm_pointer_release(&manager, 0, 0);
                    assert(window->visible); /* Cancel a close, never resize. */
                    continue;
                }
                uint32_t edges = (corner & 1U ? DESKTOP_WM_RESIZE_RIGHT : DESKTOP_WM_RESIZE_LEFT) |
                                 (corner & 2U ? DESKTOP_WM_RESIZE_BOTTOM : DESKTOP_WM_RESIZE_TOP);
                assert(manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE);
                assert(manager.resize_edges == edges);
                assert(manager.resize_start_x == x && manager.resize_start_y == y);
                desktop_wm_event_t motion = {.type = DESKTOP_WM_EVENT_POINTER_MOTION, .x = x, .y = y};
                assert(desktop_wm_dispatch(&manager, &motion, &result) == 0);
                assert(result.flags == 0U && result.dirty.count == 0U);
                motion.x += 17; motion.y += 11;
                assert(desktop_wm_dispatch(&manager, &motion, &result) == 0);
                assert(window->x == (corner & 1U ? 200 : 217));
                assert(window->y == (corner & 2U ? 150 : 161));
                assert(window->width == (corner & 1U ? 317U : 283U));
                assert(window->height == (corner & 2U ? 231U : 209U));
                assert(result.flags & DESKTOP_WM_RESULT_REDRAW);
                assert(desktop_wm_pointer_press(&manager, 350, 260) == 0U);
                assert(manager.resize_edges == edges); /* No recapture. */
                assert(desktop_wm_pointer_motion(&manager, x, y) != 0U);
                assert(window->x == 200 && window->y == 150);
                assert(window->width == 300U && window->height == 220U);
                press.pressed = 0U; press.x = 0; press.y = 0;
                assert(desktop_wm_dispatch(&manager, &press, &result) == 0);
                assert(manager.capture_kind == DESKTOP_WM_CAPTURE_NONE);
                assert(manager.capture_window == DESKTOP_WM_NO_WINDOW && manager.resize_edges == 0U);
                assert(desktop_wm_pointer_motion(&manager, x + 17, y + 11) == 0U);
            }
        }
    }
    arrange_single(&manager);
    (void)desktop_wm_pointer_press(&manager, 483, 353); /* One pixel beyond corner. */
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_CLIENT);
    (void)desktop_wm_pointer_release(&manager, 483, 353);
    /* An overlying client owns the press, not the covered resize corner. */
    manager.windows[1] = (desktop_window_t){
        .x = 450, .y = 300, .width = 300U, .height = 220U, .visible = 1U};
    (void)desktop_wm_open(&manager, 1U);
    (void)desktop_wm_pointer_press(&manager, 484, 354);
    assert(manager.capture_window == 1 && manager.capture_kind == DESKTOP_WM_CAPTURE_CLIENT);
    (void)desktop_wm_pointer_release(&manager, 0, 0);
    (void)desktop_wm_close(&manager, 1U);
    (void)desktop_wm_pointer_press(&manager, 484, 354);
    assert(manager.capture_window == 0 && manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE);
    (void)desktop_wm_pointer_release(&manager, 0, 0);
    (void)desktop_wm_close(&manager, 0U);
    (void)desktop_wm_pointer_press(&manager, 484, 354);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_NONE);
}

static uint32_t dirty_contains(const desktop_dirty_region_t *dirty,
                               int32_t x, int32_t y) {
    for (uint32_t index = 0U; index < dirty->count; ++index) {
        const desktop_rect_t rect = dirty->rects[index];
        if (x >= rect.x && y >= rect.y &&
            (int64_t)x < (int64_t)rect.x + rect.width &&
            (int64_t)y < (int64_t)rect.y + rect.height) return 1U;
    }
    return 0U;
}

static void test_shrink_invalidates_only_current_resize_sweep(void) {
    desktop_wm_t manager;
    desktop_wm_initialize(&manager, 1024U, 768U, 32, 744, 24U);
    assert(desktop_wm_open(&manager, 0U) != 0U);
    desktop_window_t *window = &manager.windows[0];
    window->flags = DESKTOP_WM_WINDOW_RETAINED_RESIZE;
    int32_t original_right = window->x + (int32_t)window->width - 1;
    int32_t original_bottom = window->y + (int32_t)window->height - 1;
    desktop_wm_event_t press = {
        DESKTOP_WM_EVENT_POINTER_BUTTON, original_right, original_bottom,
        DESKTOP_WM_BUTTON_LEFT, 1U, 0U, 0U};
    desktop_wm_dispatch_result_t result;
    assert(desktop_wm_dispatch(&manager, &press, &result) == 0);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE);
    desktop_wm_event_t shrink = {
        DESKTOP_WM_EVENT_POINTER_MOTION,
        original_right - 180, original_bottom - 120, 0U, 0U, 0U, 0U};
    assert(desktop_wm_dispatch(&manager, &shrink, &result) == 0);
    assert(dirty_contains(&result.dirty, original_right, original_bottom));
    assert(dirty_contains(
        &result.dirty, original_right - 10, original_bottom - 10));
    int32_t previous_right = window->x + (int32_t)window->width - 1;
    int32_t previous_bottom = window->y + (int32_t)window->height - 1;
    shrink.x -= 80;
    shrink.y -= 60;
    assert(desktop_wm_dispatch(&manager, &shrink, &result) == 0);
    assert(dirty_contains(&result.dirty, previous_right, previous_bottom));
    assert(!dirty_contains(&result.dirty, original_right, original_bottom));
    uint64_t damage_area = 0U;
    for (uint32_t index = 0U; index < result.dirty.count; ++index)
        damage_area += (uint64_t)result.dirty.rects[index].width *
            result.dirty.rects[index].height;
    assert(damage_area <
           (uint64_t)window->width * (uint64_t)window->height);
}

static void test_layout_dependent_content_gets_full_resize_damage(void) {
    desktop_wm_t manager;
    desktop_wm_initialize(&manager, 1024U, 768U, 32, 744, 24U);
    assert(desktop_wm_open(&manager, 0U) != 0U);
    desktop_window_t *window = &manager.windows[0];
    assert(window->flags == 0U);
    int32_t right = window->x + (int32_t)window->width - 1;
    int32_t bottom = window->y + (int32_t)window->height - 1;
    desktop_wm_event_t press = {
        DESKTOP_WM_EVENT_POINTER_BUTTON, right, bottom,
        DESKTOP_WM_BUTTON_LEFT, 1U, 0U, 0U};
    desktop_wm_dispatch_result_t result;
    assert(desktop_wm_dispatch(&manager, &press, &result) == 0);
    desktop_wm_event_t shrink = {
        DESKTOP_WM_EVENT_POINTER_MOTION,
        right - 80, bottom - 60, 0U, 0U, 0U, 0U};
    assert(desktop_wm_dispatch(&manager, &shrink, &result) == 0);
    assert(dirty_contains(
        &result.dirty, window->x + 24, window->y + 48));
}

int main(void) {
    test_dirty_regions_and_event_dispatch();
    test_edge_and_corner_resize();
    test_complete_corner_pixels();
    test_tiny_corner_partition();
    test_corner_capture_and_precedence();
    test_shrink_invalidates_only_current_resize_sweep();
    test_layout_dependent_content_gets_full_resize_damage();
    desktop_wm_t manager;
    desktop_wm_initialize(&manager, 1024U, 768U, 36, 736, 24U);

    assert(DESKTOP_WM_CAPACITY == 8U);
    assert(manager.windows[0].visible == 0U);
    assert(manager.windows[1].visible == 0U);
    assert(manager.windows[2].visible == 0U);
    assert(manager.keyboard_focus == DESKTOP_WM_NO_WINDOW);

    arrange_overlap(&manager);
    assert(desktop_wm_window_at(&manager, 200, 160) == 1);
    assert(desktop_wm_window_at(&manager, 120, 125) == 0);
    assert(desktop_wm_window_at(&manager, 20, 80) == DESKTOP_WM_NO_WINDOW);

    assert(desktop_wm_pointer_press(&manager, 160, 110) != 0U);
    assert(manager.keyboard_focus == 0);
    assert(manager.z_order[DESKTOP_WM_CAPACITY - 1U] == 0U);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_MOVE);
    assert(manager.capture_window == 0);

    assert(desktop_wm_pointer_motion(&manager, 2000, 2000) != 0U);
    assert(manager.windows[0].x >= manager.work_left);
    assert(manager.windows[0].y >= manager.work_top);
    assert(manager.windows[0].x + (int32_t)manager.windows[0].width <=
           manager.work_right);
    assert(manager.windows[0].y + (int32_t)manager.windows[0].height <=
           manager.work_bottom);
    assert(desktop_wm_pointer_release(&manager, 2000, 2000) == 0U);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_NONE);

    desktop_rect_t close = desktop_wm_close_rect(&manager, 0U);
    int32_t close_x = close.x + (int32_t)(close.width / 2U);
    int32_t close_y = close.y + (int32_t)(close.height / 2U);
    assert(desktop_wm_pointer_press(&manager, close_x, close_y) == 0U);
    assert(manager.capture_kind == DESKTOP_WM_CAPTURE_CLOSE);
    assert(desktop_wm_pointer_release(&manager, 10, 70) == 0U);
    assert(manager.windows[0].visible == 1U);

    assert(desktop_wm_pointer_press(&manager, close_x, close_y) == 0U);
    assert(desktop_wm_pointer_release(&manager, close_x, close_y) != 0U);
    assert(manager.windows[0].visible == 0U);
    assert(manager.keyboard_focus == 1);

    assert(desktop_wm_open(&manager, 0U) != 0U);
    assert(manager.windows[0].visible == 1U);
    assert(manager.keyboard_focus == 0);
    assert(manager.selected == 0U);
    assert(manager.z_order[DESKTOP_WM_CAPACITY - 1U] == 0U);
    return 0;
}
