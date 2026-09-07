/* Real Surface client; only the OS IPC/clock boundary is simulated. The
 * four-slot queue is shared by both directions, as in kernel/ipc/ipc.c. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reist/gui/surface_client.h"

static x86os_ipc_message_t queue[X86OS_IPC_QUEUE_DEPTH];
static unsigned count, outbound_full, refill, drain_after_sleeps;
static unsigned sends, receives, published, sleeps, clock_calls, blocking_sends;
static unsigned yields, handoff_drains;
static int yield_error;
static uint64_t now_ms;
static int send_error, clock_error, sleep_error, frozen_clock, backwards_clock;
static reist_gui_surface_client_t owner, dialog;

static void reset(void) {
    memset(queue, 0, sizeof(queue));
    count = outbound_full = refill = drain_after_sleeps = 0;
    sends = receives = published = sleeps = clock_calls = blocking_sends = 0;
    yields = handoff_drains = 0; yield_error = 0;
    send_error = clock_error = sleep_error = frozen_clock = backwards_clock = 0;
    now_ms = 5000;
    assert(reist_gui_surface_client_init(&owner, 17) == 0);
    owner.surface = (reist_gui_surface_handle_t){1, 2};
    assert(reist_gui_surface_client_init_shared(&dialog, &owner) == 0);
    dialog.surface = (reist_gui_surface_handle_t){3, 4};
}
static void enqueue(uint32_t type, unsigned serial, int error) {
    assert(count < X86OS_IPC_QUEUE_DEPTH);
    reist_gui_surface_message_t message = {0};
    message.protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION;
    message.message_size = sizeof(message);
    message.surface = owner.surface; message.type = type;
    message.flags = (uint32_t)error;
    message.input.serial = serial;
    message.input.type = REIST_GUI_SURFACE_INPUT_POINTER_MOTION;
    queue[count].version = X86OS_IPC_MESSAGE_VERSION;
    queue[count].struct_size = sizeof(queue[count]);
    queue[count].length = sizeof(message);
    memcpy(queue[count++].payload, &message, sizeof(message));
}
static void full_input(void) {
    for (unsigned i = 0; i < X86OS_IPC_QUEUE_DEPTH; ++i)
        enqueue(REIST_GUI_SURFACE_INPUT, i + 1, 0);
}
int x86os_ipc_send_timeout(x86os_ipc_handle_t endpoint,
                           const x86os_ipc_message_t *message, uint32_t timeout) {
    assert(endpoint == 17 && message && count <= X86OS_IPC_QUEUE_DEPTH);
    ++sends;
    if (send_error) return send_error;
    if (count == X86OS_IPC_QUEUE_DEPTH) {
        if (timeout) ++blocking_sends;
        return timeout ? -110 : -11;
    }
    /* The broker consumes the request and replies, without further input. */
    ++published;
    queue[count++] = *message;
    return 0;
}
int x86os_ipc_receive_timeout(x86os_ipc_handle_t endpoint,
                              x86os_ipc_message_t *message, uint32_t timeout) {
    assert(endpoint == 17 && message);
    ++receives;
    if (!count || outbound_full) return timeout ? -110 : -11;
    *message = queue[0];
    memmove(queue, queue + 1, --count * sizeof(queue[0]));
    if (refill) enqueue(REIST_GUI_SURFACE_INPUT, receives + 4, 0);
    return 0;
}
int x86os_monotonic_ms(uint64_t *value) {
    assert(value); ++clock_calls;
    *value = backwards_clock && clock_calls > 1 ? now_ms - 1 : now_ms;
    return clock_error;
}
int x86os_sleep_ms(uint32_t duration) {
    assert(duration == 1); ++sleeps;
    if (sleep_error) return sleep_error;
    if (!frozen_clock) now_ms += duration;
    if (drain_after_sleeps && sleeps == drain_after_sleeps) {
        count = 0; outbound_full = 0;
    }
    return 0;
}
int x86os_yield(void) {
    ++yields;
    if (yield_error) return yield_error;
    if (handoff_drains) { count=0; outbound_full=0; }
    return 0;
}
static int unregister(reist_gui_surface_client_t *client) {
    return reist_gui_surface_client_buffer_destroy(client, 7, 8);
}
static void expect_events(unsigned amount) {
    for (unsigned i = 0; i < amount; ++i) {
        reist_gui_surface_message_t message;
        assert(reist_gui_surface_client_receive(&owner, &message, 0) == 0);
        assert(message.type == REIST_GUI_SURFACE_INPUT && message.input.serial == i + 1);
        assert(message.surface.id == owner.surface.id &&
               message.surface.generation == owner.surface.generation);
    }
    assert(owner.deferred_count == 0);
}
int main(void) {
    /* Queue holds our requests and a ready broker needs a CPU turn, not an
     * idle timer. A non-progressing peer gets only one such handoff. */
    reset(); full_input(); outbound_full=1; handoff_drains=1;
    assert(unregister(&owner)==0 && yields==1 && !sleeps && published==1 && now_ms==5000);
    reset(); full_input(); outbound_full=1; yield_error=-5;
    assert(unregister(&owner)==-5 && yields==1 && !sleeps && !published);
    _Static_assert(X86OS_IPC_QUEUE_DEPTH == 4, "recheck shared IPC contract");
    _Static_assert(REIST_GUI_SURFACE_PROTOCOL_VERSION == 6 &&
        REIST_GUI_SURFACE_INPUT_KEYBOARD == 3 && REIST_GUI_SURFACE_PAINT_FONT_TEXT == 20,
        "scroll extension must preserve old ABI");
    reset(); owner.acknowledged_serial=1;
    assert(!reist_gui_surface_client_enable_scroll(&owner));
    reset(); full_input();
    reist_gui_surface_message_t wheel_message;
    memcpy(&wheel_message,queue[0].payload,sizeof(wheel_message));
    wheel_message.input=(reist_gui_surface_input_t){.type=REIST_GUI_SURFACE_INPUT_POINTER_SCROLL,
        .serial=1,.x=10,.y=80,.delta_y=-120};
    memcpy(queue[0].payload,&wheel_message,sizeof(wheel_message));
    assert(!unregister(&owner));
    reist_gui_surface_input_t wheel_event;
    assert(!reist_gui_surface_client_receive_input(&owner,&wheel_event,0));
    assert(wheel_event.delta_y==-120 && wheel_event.type==REIST_GUI_SURFACE_INPUT_POINTER_SCROLL);
    reset(); enqueue(REIST_GUI_SURFACE_INPUT,1,0);
    wheel_message.input.key=1; /* Fail before dispatch/defer of malformed wheel data. */
    memcpy(queue[0].payload,&wheel_message,sizeof(wheel_message));
    assert(reist_gui_surface_client_receive_input(&owner,&wheel_event,0)==-84);
    reset();
    assert(unregister(&owner) == 0 && published == 1 && !sleeps && !clock_calls);

    reset(); full_input();
    int result = unregister(&owner);
    printf("SURFACE_INPUT_BACKPRESSURE result=%d published=%u deferred=%u blocking_sends=%u\n",
           result, published, owner.deferred_count, blocking_sends);
    fflush(stdout);
    assert(result == 0 && published == 1 && !blocking_sends && !sleeps);
    expect_events(4);

    reset(); full_input();
    assert(unregister(&dialog) == 0 && dialog.deferred_count == 0);
    expect_events(4); /* Other Surface's events remain in the shared owner. */

    reset(); full_input(); outbound_full = 1; drain_after_sleeps = 3;
    assert(unregister(&owner) == 0 && sleeps == 3 && !blocking_sends);
    assert(published == 1 && now_ms == 5003);

    reset(); full_input(); outbound_full = 1;
    assert(unregister(&owner) == -110 && !published && !blocking_sends);
    assert(yields==1 && sleeps > 0 && sleeps <= 500 && sends <= 630 && now_ms <= 5500);

    reset(); full_input(); outbound_full = 1; drain_after_sleeps = 500;
    assert(unregister(&owner) == -110 && !published && sleeps == 500);
    reset(); full_input(); outbound_full = 1; frozen_clock = 1;
    assert(unregister(&owner) == -110 && !published && sleeps <= 630);
    reset(); full_input(); outbound_full = 1; sleep_error = -5;
    assert(unregister(&owner) == -5 && !published && sleeps == 1);
    reset(); full_input(); backwards_clock = 1;
    assert(unregister(&owner) == -5 && !published && !receives);

    reset(); full_input();
    owner.deferred_count = REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
    assert(unregister(&owner) == -75 && !published && !receives && count == 4);

    reset(); full_input(); refill = 1;
    assert(unregister(&owner) == -75 && !published && !blocking_sends);
    assert(owner.deferred_count == REIST_GUI_SURFACE_MAX_PENDING_EVENTS);
    assert(receives == REIST_GUI_SURFACE_MAX_PENDING_EVENTS && count == 4);
    refill = 0; expect_events(REIST_GUI_SURFACE_MAX_PENDING_EVENTS);

    reset(); full_input(); queue[0].version = 0;
    assert(unregister(&owner) == -84 && !published && !owner.deferred_count);
    reset(); full_input();
    memset(queue[0].payload, 0, sizeof(reist_gui_surface_message_t));
    assert(unregister(&owner) == -84 && !published);
    reset(); enqueue(0xffffffffU, 1, 0);
    for (unsigned i = 1; i < 4; ++i) enqueue(REIST_GUI_SURFACE_INPUT, i + 1, 0);
    assert(unregister(&owner) == -84 && !published);

    reset(); enqueue(REIST_GUI_SURFACE_PAINT_FONT_TEXT, 1, -22);
    for (unsigned i = 1; i < 4; ++i) enqueue(REIST_GUI_SURFACE_INPUT, i + 1, 0);
    assert(unregister(&owner) == -22 && !published);

    reset(); enqueue(REIST_GUI_SURFACE_CLOSE, 1, 0);
    for (unsigned i = 1; i < 4; ++i) enqueue(REIST_GUI_SURFACE_INPUT, i + 1, 0);
    assert(unregister(&owner) == 0);
    reist_gui_surface_message_t event;
    assert(reist_gui_surface_client_receive(&owner, &event, 0) == 0);
    assert(event.type == REIST_GUI_SURFACE_CLOSE); /* Never discard close. */

    reset(); send_error = -32;
    assert(unregister(&owner) == -32 && sends == 1 && !receives && !published);
    reset(); full_input(); clock_error = -5;
    assert(unregister(&owner) == -5 && !published && !receives);
    puts("SURFACE_CLIENT_BACKPRESSURE_HOST_OK");
    return 0;
}
