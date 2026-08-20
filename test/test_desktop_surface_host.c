#include <assert.h>
#include "userspace/gui/compositor/desktop_surface.h"

int main(void) {
    desktop_surface_manager_t manager;
    desktop_surface_initialize(&manager);
    reist_gui_surface_owner_t owner = {42U, 7U};
    reist_gui_surface_owner_t other_owner = {43U, 1U};
    reist_gui_surface_handle_t handle;
    reist_gui_surface_configure_t configure;
    assert(desktop_surface_create(&manager, owner,
        REIST_GUI_SURFACE_ROLE_TOPLEVEL, 320U, 200U,
        &handle, &configure) == 0);
    assert(desktop_surface_ack_configure(&manager, owner, handle,
        configure.serial) == 0);
    assert(desktop_surface_set_title(
        &manager, owner, handle, "Editor", 6U) == 0);
    assert(desktop_surface_paint_begin(&manager, owner, handle) == 0);
    assert(desktop_surface_paint_fill(
        &manager, owner, handle,
        (reist_gui_rect_t){0, 0, 320U, 200U}, 0x00ffffffU) == 0);
    assert(desktop_surface_paint_text(
        &manager, owner, handle,
        (reist_gui_rect_t){8, 8, 120U, 1U}, 0U, 0x00ffffffU,
        "Document", 8U) == 0);
    assert(desktop_surface_paint_commit(&manager, owner, handle) == 0);
    assert(manager.slots[handle.id - 1U].committed_paint_count == 2U);
    assert(manager.slots[handle.id - 1U].paint_generation != 0U);
    reist_gui_surface_input_t input = {
        REIST_GUI_SURFACE_INPUT_POINTER_MOTION, 1U, 12, 8,
        1, -1, 0U, 0U, 0U, 0U};
    assert(desktop_surface_input_enqueue(
        &manager, owner, handle, &input) == 0);
    assert(desktop_surface_input_dequeue(
        &manager, other_owner, handle, &input) < 0);
    reist_gui_surface_input_t received_input;
    assert(desktop_surface_input_dequeue(
        &manager, owner, handle, &received_input) == 0);
    assert(received_input.serial == 1U && received_input.x == 12);
    input.type = REIST_GUI_SURFACE_INPUT_KEYBOARD;
    input.key = 'a';
    for (uint32_t event_index = 0U;
         event_index < REIST_GUI_SURFACE_MAX_PENDING_EVENTS; ++event_index) {
        input.serial = event_index + 2U;
        assert(desktop_surface_input_enqueue(
            &manager, owner, handle, &input) == 0);
    }
    assert(desktop_surface_input_enqueue(
        &manager, owner, handle, &input) == DESKTOP_SURFACE_ECAPACITY);
    while (desktop_surface_input_dequeue(
        &manager, owner, handle, &received_input) == 0) {}
    reist_gui_surface_buffer_t buffer = {
        REIST_GUI_SURFACE_BUFFER_API_VERSION, sizeof(buffer), 1U, 1U,
        320U, 200U, 320U * 4U,
        REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888, 320U * 200U * 4U, 0U};
    assert(desktop_surface_buffer_create(&manager, owner, &buffer) == 0);
    assert(desktop_surface_attach(&manager, owner, handle,
        1U, 1U, 320U, 200U) == 0);
    assert(desktop_surface_buffer_create(&manager, other_owner, &buffer) == 0);
    assert(desktop_surface_buffer_destroy(
        &manager, other_owner, 1U, 1U) == 0);
    assert(desktop_surface_damage(&manager, owner, handle,
        (reist_gui_rect_t){0, 0, 320U, 200U}) == 0);
    desktop_surface_commit_result_t result;
    assert(desktop_surface_commit(&manager, owner, handle, &result) == 0);
    assert(result.committed == 1U && result.damage.count == 1U);
    assert(desktop_surface_buffer_destroy(&manager, owner, 1U, 1U) == 0);
    reist_gui_surface_configure_t resized;
    assert(desktop_surface_reconfigure(
        &manager, owner, handle, 400U, 260U, &resized) == 0);
    assert(resized.width == 400U && resized.height == 260U);
    assert(manager.slots[handle.id - 1U].configure_sent == 0U);
    assert(desktop_surface_ack_configure(
        &manager, owner, handle, resized.serial) == 0);
    assert(desktop_surface_ack_configure(&manager, owner, handle,
        resized.serial + 1U) < 0);
    assert(desktop_surface_destroy(&manager, owner, handle) == 0);
    assert(desktop_surface_destroy(&manager, owner, handle) < 0);

    reist_gui_surface_handle_t bounded_handle;
    reist_gui_surface_configure_t bounded_configure;
    assert(desktop_surface_create(&manager, owner,
        REIST_GUI_SURFACE_ROLE_TOPLEVEL, REIST_GUI_SURFACE_MAX_WIDTH,
        REIST_GUI_SURFACE_MAX_HEIGHT, &bounded_handle,
        &bounded_configure) == 0);
    assert(desktop_surface_ack_configure(&manager, owner, bounded_handle,
        bounded_configure.serial) == 0);
    buffer = (reist_gui_surface_buffer_t){
        REIST_GUI_SURFACE_BUFFER_API_VERSION, sizeof(buffer), 2U, 1U,
        REIST_GUI_SURFACE_MAX_WIDTH, REIST_GUI_SURFACE_MAX_HEIGHT,
        REIST_GUI_SURFACE_MAX_WIDTH * 4U,
        REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888,
        REIST_GUI_SURFACE_MAX_BUFFER_BYTES, 0U};
    assert(desktop_surface_buffer_create(&manager, owner, &buffer) == 0);
    assert(desktop_surface_attach(&manager, owner, bounded_handle,
        2U, 1U, REIST_GUI_SURFACE_MAX_WIDTH,
        REIST_GUI_SURFACE_MAX_HEIGHT) == 0);
    assert(desktop_surface_create(&manager, owner,
        REIST_GUI_SURFACE_ROLE_TOPLEVEL, REIST_GUI_SURFACE_MAX_WIDTH + 1U,
        1U, &bounded_handle, &bounded_configure) < 0);
    desktop_surface_revoke_owner(&manager, owner);
    assert(desktop_surface_buffer_destroy(&manager, owner, 2U, 1U) < 0);
    desktop_surface_revoke_owner(&manager, owner);
    return 0;
}
