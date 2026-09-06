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
    reist_gui_surface_handle_t parent = response.surface;
    request.type = REIST_GUI_SURFACE_ACK_CONFIGURE;
    request.surface = response.surface;
    request.serial = response.serial;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    /* Legacy clients must never receive an unnegotiated new input type. */
    reist_gui_surface_input_t scroll={.type=REIST_GUI_SURFACE_INPUT_POINTER_SCROLL,
        .serial=1,.x=10,.y=10,.delta_y=120}, delivered;
    assert(desktop_surface_input_enqueue(&manager,owner,parent,&scroll)<0);
    request.type=REIST_GUI_SURFACE_ENABLE_SCROLL;
    request.flags=0; request.serial=REIST_GUI_SURFACE_SCROLL_VERSION+1;
    assert(desktop_surface_dispatch_message(&manager,owner,&request,&response)<0);
    request.serial=REIST_GUI_SURFACE_SCROLL_VERSION;
    reist_gui_surface_owner_t stale=owner; ++stale.process_generation;
    assert(desktop_surface_dispatch_message(&manager,stale,&request,&response)<0);
    assert(desktop_surface_dispatch_message(&manager,owner,&request,&response)==0);
    assert(response.serial==REIST_GUI_SURFACE_SCROLL_VERSION);
    /* No coalescing of opposite directions or wheel/button edges. */
    for (unsigned i=0;i<REIST_GUI_SURFACE_MAX_PENDING_EVENTS;++i) {
        scroll.serial=i+1; scroll.delta_y=(i&1) ? -120 : 120;
        assert(!desktop_surface_input_enqueue(&manager,owner,parent,&scroll));
    }
    assert(desktop_surface_input_enqueue(&manager,owner,parent,&scroll)==DESKTOP_SURFACE_ECAPACITY);
    for (unsigned i=0;i<REIST_GUI_SURFACE_MAX_PENDING_EVENTS;++i) {
        assert(!desktop_surface_input_dequeue(&manager,owner,parent,&delivered));
        assert(delivered.serial==i+1 && delivered.delta_y==((i&1) ? -120 : 120));
    }
    scroll.key=1; assert(desktop_surface_input_enqueue(&manager,owner,parent,&scroll)<0);
    scroll.key=0; scroll.delta_y=0;
    assert(desktop_surface_input_enqueue(&manager,owner,parent,&scroll)<0);
    scroll.delta_y=INT32_MIN; scroll.x=-1;
    assert(desktop_surface_input_enqueue(&manager,owner,parent,&scroll)<0);
    scroll.x=10; assert(!desktop_surface_input_enqueue(&manager,owner,parent,&scroll));
    assert(!desktop_surface_input_dequeue(&manager,owner,parent,&delivered));
    assert(delivered.delta_y==INT32_MIN);
    request = (reist_gui_surface_message_t){
        .protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION,
        .message_size = sizeof(request),
        .type = REIST_GUI_SURFACE_CREATE,
        .flags = REIST_GUI_SURFACE_ROLE_DIALOG,
        .parent_surface = parent,
        .width = 300U,
        .height = 160U,
    };
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    assert(response.type == REIST_GUI_SURFACE_CONFIGURE);
    reist_gui_surface_handle_t dialog = response.surface;
    request.type = REIST_GUI_SURFACE_DESTROY;
    request.surface = dialog;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.surface = parent;
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
    request.type = REIST_GUI_SURFACE_PAINT_OVERLAY_BEGIN;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    assert(response.type == REIST_GUI_SURFACE_PAINT_OVERLAY_BEGIN);
    request.type = REIST_GUI_SURFACE_PAINT_FILL;
    request.damage = (reist_gui_rect_t){8, 8, 80U, 20U};
    request.flags = 0x00000080U;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_OVERLAY_COMMIT;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    assert(response.type == REIST_GUI_SURFACE_PAINT_OVERLAY_COMMIT);
    assert(manager.slots[parent.id - 1U].committed_paint_count == 1U);
    assert(manager.slots[parent.id - 1U].committed_overlay_paint_count == 1U);
    request.type = REIST_GUI_SURFACE_PAINT_DYNAMIC_BEGIN;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_FILL;
    request.damage = (reist_gui_rect_t){8, 32, 80U, 20U};
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_DYNAMIC_COMMIT;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_HOVER_BEGIN;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_FILL;
    request.damage = (reist_gui_rect_t){8, 56, 80U, 20U};
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    request.type = REIST_GUI_SURFACE_PAINT_HOVER_COMMIT;
    assert(desktop_surface_dispatch_message(
        &manager, owner, &request, &response) == 0);
    assert(manager.slots[parent.id - 1U].committed_dynamic_paint_count == 1U);
    assert(manager.slots[parent.id - 1U].committed_hover_paint_count == 1U);
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
