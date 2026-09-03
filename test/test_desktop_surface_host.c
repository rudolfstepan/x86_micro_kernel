#include <assert.h>
#include "userspace/gui/compositor/desktop_surface.h"
#include "reist/gui/font_catalog.h"

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
    reist_gui_surface_handle_t dialog;
    reist_gui_surface_configure_t dialog_configure;
    assert(desktop_surface_create_dialog(&manager, owner, handle,
        300U, 160U, &dialog, &dialog_configure) == 0);
    assert(manager.slots[dialog.id - 1U].role ==
        REIST_GUI_SURFACE_ROLE_DIALOG);
    assert(manager.slots[dialog.id - 1U].parent.id == handle.id);
    assert(desktop_surface_create_dialog(&manager, owner, handle,
        300U, 160U, &dialog, &dialog_configure) == DESKTOP_SURFACE_ESTATE);
    assert(desktop_surface_destroy(&manager, owner, handle) ==
        DESKTOP_SURFACE_ESTATE);
    assert(desktop_surface_ack_configure(&manager, owner, dialog,
        dialog_configure.serial) == 0);
    assert(desktop_surface_destroy(&manager, owner, dialog) == 0);
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
    assert(desktop_surface_paint_font_text(
        &manager, owner, handle,
        (reist_gui_rect_t){8, 24, 120U, 16U}, 0U, 0x00ffffffU,
        "Font", 4U, REIST_GUI_FONT_FAMILY_UNIFONT, 11U) ==
        DESKTOP_SURFACE_EINVAL);
    assert(desktop_surface_paint_font_text(
        &manager, owner, handle,
        (reist_gui_rect_t){8, 24, 120U, 16U}, 0U, 0x00ffffffU,
        "Font", 4U, 99U, 16U) == DESKTOP_SURFACE_EINVAL);
    assert(desktop_surface_paint_font_text(
        &manager, owner, handle,
        (reist_gui_rect_t){8, 24, 120U, 16U}, 0U, 0x00ffffffU,
        "Font", 4U, REIST_GUI_FONT_FAMILY_UNIFONT, 16U) == 0);
    assert(desktop_surface_paint_commit(&manager, owner, handle) == 0);
    desktop_surface_slot_t *painted = &manager.slots[handle.id - 1U];
    assert(painted->committed_paint_count == 3U);
    assert(painted->committed_paint[2].type ==
           DESKTOP_SURFACE_PAINT_FONT_TEXT);
    assert(painted->committed_paint[2].font_family ==
           REIST_GUI_FONT_FAMILY_UNIFONT);
    assert(painted->committed_paint[2].font_height == 16U);
    assert(painted->paint_generation != 0U);
    reist_gui_rect_t present_damage;
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == 0);
    assert(present_damage.x == 0 && present_damage.y == 0 &&
           present_damage.width == 320U && present_damage.height == 200U);
    uint32_t base_generation = painted->paint_generation;
    assert(desktop_surface_paint_begin_layer(
        &manager, owner, handle, 4U) == DESKTOP_SURFACE_EINVAL);
    assert(!painted->paint_active &&
           painted->paint_generation == base_generation);
    assert(desktop_surface_paint_begin_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY) == 0);
    assert(desktop_surface_paint_begin(
        &manager, owner, handle) == DESKTOP_SURFACE_ESTATE);
    assert(desktop_surface_paint_fill(
        &manager, owner, handle,
        (reist_gui_rect_t){4, 4, 80U, 24U}, 0x00000080U) == 0);
    assert(desktop_surface_paint_commit(
        &manager, owner, handle) == DESKTOP_SURFACE_ESTATE);
    assert(desktop_surface_paint_commit_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY) == 0);
    assert(painted->committed_paint_count == 3U);
    assert(painted->committed_overlay_paint_count == 1U);
    assert(painted->committed_overlay_paint[0].foreground == 0x00000080U);
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == 0);
    assert(present_damage.x == 4 && present_damage.y == 4 &&
           present_damage.width == 80U && present_damage.height == 24U);
    uint32_t overlay_generation = painted->paint_generation;
    assert(desktop_surface_paint_begin_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC) == 0);
    assert(desktop_surface_paint_text(
        &manager, owner, handle,
        (reist_gui_rect_t){8, 40, 120U, 1U}, 0U, 0x00ffffffU,
        "Dynamic", 7U) == 0);
    assert(desktop_surface_paint_commit_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC) == 0);
    assert(painted->committed_dynamic_paint_count == 1U);
    assert(desktop_surface_paint_begin_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_HOVER) == 0);
    for (uint32_t hover = 0U;
         hover < REIST_GUI_SURFACE_MAX_HOVER_PAINT_COMMANDS; ++hover)
        assert(desktop_surface_paint_fill(
            &manager, owner, handle,
            (reist_gui_rect_t){(int32_t)(4U + hover), 4, 1U, 1U},
            hover) == 0);
    assert(desktop_surface_paint_fill(
        &manager, owner, handle,
        (reist_gui_rect_t){12, 4, 1U, 1U}, 0U) ==
        DESKTOP_SURFACE_ECAPACITY);
    assert(desktop_surface_paint_commit_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_HOVER) == 0);
    assert(painted->committed_hover_paint_count ==
           REIST_GUI_SURFACE_MAX_HOVER_PAINT_COMMANDS);
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == 0);
    overlay_generation = painted->paint_generation;
    assert(desktop_surface_paint_begin_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY) == 0);
    assert(desktop_surface_paint_fill(
        &manager, owner, handle,
        (reist_gui_rect_t){4, 4, 80U, 24U}, 0x00000080U) == 0);
    assert(desktop_surface_paint_commit_layer(
        &manager, owner, handle,
        REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY) == 0);
    assert(painted->paint_generation == overlay_generation);
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == DESKTOP_SURFACE_ESTATE);
    assert(desktop_surface_paint_begin(&manager, owner, handle) == 0);
    assert(desktop_surface_paint_fill(
        &manager, owner, handle,
        (reist_gui_rect_t){0, 0, 320U, 200U}, 0x00ffffffU) == 0);
    assert(desktop_surface_paint_commit(&manager, owner, handle) == 0);
    assert(painted->committed_paint_count == 1U);
    assert(painted->committed_overlay_paint_count == 1U);
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == 0);
    assert(present_damage.x == 8 && present_damage.y == 8 &&
           present_damage.width == 120U && present_damage.height == 32U);
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
    uint32_t discarded_motion_serial = 0U;
    for (uint32_t event_index = 0U;
         event_index < REIST_GUI_SURFACE_MAX_PENDING_EVENTS; ++event_index) {
        input.type = event_index == 5U
            ? REIST_GUI_SURFACE_INPUT_POINTER_MOTION
            : REIST_GUI_SURFACE_INPUT_KEYBOARD;
        input.serial = 100U + event_index;
        input.x = 12;
        input.y = 8;
        if (event_index == 5U) discarded_motion_serial = input.serial;
        assert(desktop_surface_input_enqueue(
            &manager, owner, handle, &input) == 0);
    }
    input.type = REIST_GUI_SURFACE_INPUT_POINTER_BUTTON;
    input.serial = 200U;
    input.button = 1U;
    input.pressed = 1U;
    assert(desktop_surface_input_enqueue(
        &manager, owner, handle, &input) == 0);
    for (uint32_t event_index = 0U;
         event_index < REIST_GUI_SURFACE_MAX_PENDING_EVENTS; ++event_index) {
        assert(desktop_surface_input_dequeue(
            &manager, owner, handle, &received_input) == 0);
        assert(received_input.serial != discarded_motion_serial);
        if (event_index + 1U == REIST_GUI_SURFACE_MAX_PENDING_EVENTS) {
            assert(received_input.type ==
                REIST_GUI_SURFACE_INPUT_POINTER_BUTTON);
            assert(received_input.serial == 200U);
        }
    }
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
    assert(result.released_buffer_id == 0U);
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == 0);
    assert(present_damage.x == 0 && present_damage.y == 0 &&
           present_damage.width == 320U && present_damage.height == 200U);
    assert(desktop_surface_buffer_destroy(&manager, owner, 1U, 1U) < 0);
    buffer.capability_id = 3U;
    assert(desktop_surface_buffer_create(&manager, owner, &buffer) == 0);
    assert(desktop_surface_attach(&manager, owner, handle,
        3U, 1U, 320U, 200U) == 0);
    assert(desktop_surface_damage(&manager, owner, handle,
        (reist_gui_rect_t){10, 20, 30U, 40U}) == 0);
    assert(desktop_surface_commit(&manager, owner, handle, &result) == 0);
    assert(result.released_buffer_id == 1U &&
           result.released_buffer_generation == 1U);
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == 0);
    assert(present_damage.x == 10 && present_damage.y == 20 &&
           present_damage.width == 30U && present_damage.height == 40U);
    assert(desktop_surface_buffer_destroy(&manager, owner, 1U, 1U) == 0);
    reist_gui_surface_configure_t resized;
    assert(desktop_surface_reconfigure(
        &manager, owner, handle, 400U, 260U, &resized) == 0);
    assert(resized.width == 400U && resized.height == 260U);
    desktop_surface_slot_t *resizing = &manager.slots[handle.id - 1U];
    assert(resizing->configure_sent == 0U);
    assert(resizing->width == 320U && resizing->height == 200U);
    assert(resizing->pending_width == 400U &&
           resizing->pending_height == 260U);
    assert(desktop_surface_paint_begin(&manager, owner, handle) == 0);
    assert(desktop_surface_paint_fill(&manager, owner, handle,
        (reist_gui_rect_t){0, 0, 320U, 200U}, 0U) == 0);
    assert(desktop_surface_paint_commit(&manager, owner, handle) == 0);
    assert(desktop_surface_ack_configure(
        &manager, owner, handle, resized.serial) == 0);
    assert(resizing->width == 400U && resizing->height == 260U);
    assert(resizing->pending_width == 0U &&
           resizing->pending_height == 0U);
    assert(desktop_surface_present_damage_take(
        &manager, owner, handle, &present_damage) == 0);
    assert(present_damage.x == 0 && present_damage.y == 0 &&
           present_damage.width == 400U && present_damage.height == 260U);
    assert(desktop_surface_ack_configure(&manager, owner, handle,
        resized.serial + 1U) < 0);
    assert(desktop_surface_destroy(&manager, owner, handle) == 0);
    assert(desktop_surface_buffer_destroy(&manager, owner, 3U, 1U) == 0);
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
