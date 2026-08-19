#include "userspace/programs/desktop_wm.h"

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
    manager->focused = 1;
}

int main(void) {
    desktop_wm_t manager;
    desktop_wm_initialize(&manager, 1024U, 768U, 36, 736, 24U);

    assert(DESKTOP_WM_CAPACITY == 4U);
    assert(manager.windows[0].visible == 1U);
    assert(manager.windows[1].visible == 1U);
    assert(manager.windows[2].visible == 0U);
    assert(manager.focused == 0);

    arrange_overlap(&manager);
    assert(desktop_wm_window_at(&manager, 200, 160) == 1);
    assert(desktop_wm_window_at(&manager, 120, 125) == 0);
    assert(desktop_wm_window_at(&manager, 20, 80) == DESKTOP_WM_NO_WINDOW);

    assert(desktop_wm_pointer_press(&manager, 160, 110) != 0U);
    assert(manager.focused == 0);
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
    assert(manager.focused == 1);

    assert(desktop_wm_open(&manager, 0U) != 0U);
    assert(manager.windows[0].visible == 1U);
    assert(manager.focused == 0);
    assert(manager.selected == 0U);
    assert(manager.z_order[DESKTOP_WM_CAPACITY - 1U] == 0U);
    return 0;
}
