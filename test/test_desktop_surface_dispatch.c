#include <assert.h>
#include "userspace/gui/compositor/desktop_surface.h"

int main(void) {
    desktop_surface_manager_t manager;
    desktop_surface_initialize(&manager);
    const reist_gui_surface_owner_t owner = {9U, 3U};
    reist_gui_surface_message_t request = {
        .protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION,
        .message_size = sizeof(request),
        .type = REIST_GUI_SURFACE_CREATE,
        .flags = REIST_GUI_SURFACE_ROLE_TOPLEVEL,
        .width = 200U,
        .height = 120U,
    };
    reist_gui_surface_message_t response;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    assert(response.type == REIST_GUI_SURFACE_CONFIGURE);
    request.type = REIST_GUI_SURFACE_ACK_CONFIGURE;
    request.surface = response.surface;
    request.serial = response.serial;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_BEGIN;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_FILL;
    request.damage = (reist_gui_rect_t){0, 0, 200U, 120U};
    request.flags = 0x00c8c8c8U;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_COMMIT;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    assert(response.type == REIST_GUI_SURFACE_PAINT_COMMIT);
    request.type = REIST_GUI_SURFACE_BUFFER_CREATE;
    request.buffer_id = 1U;
    request.buffer_generation = 1U;
    request.width = 200U;
    request.height = 120U;
    request.stride_bytes = 200U * 4U;
    request.format = REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888;
    request.byte_size = 200U * 120U * 4U;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_ATTACH;
    request.buffer_id = 1U;
    request.buffer_generation = 1U;
    request.width = 200U;
    request.height = 120U;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_DAMAGE;
    request.damage = (reist_gui_rect_t){0, 0, 200U, 120U};
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_COMMIT;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    assert(response.type == REIST_GUI_SURFACE_BUFFER_RELEASE);
    assert(response.buffer_id == 0U && response.buffer_generation == 0U);
    request.type = REIST_GUI_SURFACE_DESTROY;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_BUFFER_DESTROY;
    request.buffer_id = 1U;
    request.buffer_generation = 1U;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    return 0;
}
