#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "userspace/gui/compositor/desktop_surface_runtime.h"
static uint32_t generation=8;
int x86os_process_identity_of(int pid,x86os_process_identity_t *out) {
    *out=(x86os_process_identity_t){1,sizeof(*out),pid,generation}; return 0;
}
/* PRODUCTION */
static desktop_surface_runtime_t runtime;
static desktop_surface_manager_t surfaces;
int main(void) {
    desktop_surface_runtime_client_t *c=&runtime.clients[0];
    c->active=DESKTOP_SURFACE_RUNTIME_BOUND; c->owner=(reist_gui_surface_owner_t){42,8};
    surfaces.slots[0].active=1; surfaces.slots[0].owner=c->owner;
    surfaces.slots[0].handle=(reist_gui_surface_handle_t){1,2};
    reist_gui_surface_message_t request={0},response;
    request.protocol_version=6; request.message_size=sizeof(request);
    request.type=REIST_GUI_SURFACE_OPEN_MOUSE; request.surface=surfaces.slots[0].handle;
    assert(REIST_GUI_SURFACE_OPEN_DISPLAY==22 && REIST_GUI_SURFACE_OPEN_MOUSE==23);
    assert(queue_mouse_applet(c,&surfaces,&request,&response)==-13);
    assert(!desktop_surface_runtime_allow_mouse(&runtime,42));
    for (unsigned i=0;i<sizeof(request);++i) {
        reist_gui_surface_message_t bad=request; ((unsigned char *)&bad)[i]^=1;
        assert(queue_mouse_applet(c,&surfaces,&bad,&response)<0);
        assert(!c->mouse_applet_pending.id);
    }
    assert(!queue_mouse_applet(c,&surfaces,&request,&response));
    assert(!queue_mouse_applet(c,&surfaces,&request,&response));
    assert(desktop_surface_runtime_take_mouse(&runtime,&surfaces)==1);
    assert(!desktop_surface_runtime_take_mouse(&runtime,&surfaces));
    assert(!queue_mouse_applet(c,&surfaces,&request,&response)); generation=9;
    assert(!desktop_surface_runtime_take_mouse(&runtime,&surfaces)); generation=8;
    assert(!queue_mouse_applet(c,&surfaces,&request,&response)); surfaces.slots[0].active=0;
    assert(!desktop_surface_runtime_take_mouse(&runtime,&surfaces)); surfaces.slots[0].active=1;
    assert(!queue_mouse_applet(c,&surfaces,&request,&response)); c->active=DESKTOP_SURFACE_RUNTIME_RETIRING;
    assert(!desktop_surface_runtime_take_mouse(&runtime,&surfaces));
    assert(queue_mouse_applet(c,&surfaces,&request,&response)==-13);
    assert(!desktop_surface_runtime_take_mouse(0,&surfaces));
    puts("MOUSE_TEST_OK"); return 0;
}
