/** @file test/test_usb_hid_mouse_host.c @brief HID-Boot-Maus Hosttest. */
#include "drivers/usb/hid_mouse.h"

#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE test_thread_t;
#define TEST_THREAD_RETURN DWORD WINAPI
static test_thread_t test_thread_start(TEST_THREAD_RETURN (*entry)(void *),
                                       void *context) {
    HANDLE thread = CreateThread(NULL, 0U, entry, context, 0U, NULL);
    assert(thread != NULL);
    return thread;
}
static void test_thread_join(test_thread_t thread) {
    assert(WaitForSingleObject(thread, 5000U) == WAIT_OBJECT_0);
    assert(CloseHandle(thread));
}
#else
#include <pthread.h>
typedef pthread_t test_thread_t;
#define TEST_THREAD_RETURN void *
static test_thread_t test_thread_start(TEST_THREAD_RETURN (*entry)(void *),
                                       void *context) {
    pthread_t thread;
    assert(pthread_create(&thread, NULL, entry, context) == 0);
    return thread;
}
static void test_thread_join(test_thread_t thread) {
    assert(pthread_join(thread, NULL) == 0);
}
#endif

#define PARALLEL_REPORTS 2000U

typedef struct {
    uint8_t report[4];
    atomic_uint *remaining;
} producer_context_t;

typedef struct {
    atomic_uint *remaining;
    atomic_uint consumed;
} reader_context_t;

static TEST_THREAD_RETURN publish_reports(void *opaque) {
    producer_context_t *context = opaque;
    for (uint32_t index = 0U; index < PARALLEL_REPORTS; ++index)
        assert(hid_mouse_report(19U, context->report, 4U));
    atomic_fetch_sub_explicit(context->remaining, 1U, memory_order_release);
    return 0;
}

static TEST_THREAD_RETURN consume_reports(void *opaque) {
    reader_context_t *context = opaque;
    for (;;) {
        hid_mouse_event_t event;
        int result = hid_mouse_read_event(&event);
        if (result == 0) {
            assert(event.version == HID_MOUSE_EVENT_VERSION);
            assert(event.struct_size == sizeof(event));
            assert(event.generation == 19U);
            assert(event.buttons <= 7U);
            atomic_fetch_add_explicit(
                &context->consumed, 1U, memory_order_relaxed);
            continue;
        }
        assert(result == -11);
        if (atomic_load_explicit(
                context->remaining, memory_order_acquire) == 0U)
            break;
    }
    return 0;
}

int main(void) {
    hid_mouse_event_t event;
    const uint8_t move[3] = {1U, 7U, (uint8_t)-4};
    const uint8_t wheel[4] = {0U, (uint8_t)-2, 3U, (uint8_t)-1};
    const uint8_t invalid[3] = {0x80U, 0U, 0U};

    assert(hid_mouse_read_event(&event) == -19);
    hid_mouse_attach(9U);
    assert(!hid_mouse_report(8U, move, sizeof(move)));
    assert(!hid_mouse_report(9U, invalid, sizeof(invalid)));
    assert(hid_mouse_report(9U, move, sizeof(move)));
    assert(hid_mouse_read_event(&event) == 0);
    assert(event.version == HID_MOUSE_EVENT_VERSION);
    assert(event.delta_x == 7 && event.delta_y == -4);
    assert(event.buttons == 1U && event.wheel == 0);
    assert(hid_mouse_read_event(&event) == -11);
    assert(hid_mouse_report(9U, wheel, sizeof(wheel)));
    assert(hid_mouse_read_event(&event) == 0);
    assert(event.delta_x == -2 && event.delta_y == 3 && event.wheel == -1);
    hid_mouse_detach(8U);
    assert(hid_mouse_report(9U, move, sizeof(move)));
    hid_mouse_detach(9U);
    assert(hid_mouse_read_event(&event) == -19);
    assert(!hid_mouse_report(9U, move, sizeof(move)));

    const uint8_t pure_motion[3] = {0U, 2U, (uint8_t)-1};
    hid_mouse_attach(10U);
    for (uint32_t index = 0U; index < 100U; ++index)
        assert(hid_mouse_report(10U, pure_motion, sizeof(pure_motion)));
    assert(hid_mouse_read_event(&event) == 0);
    assert(event.delta_x == 200 && event.delta_y == -100);
    assert(event.buttons == 0U && event.wheel == 0);
    assert(hid_mouse_read_event(&event) == -11);
    hid_mouse_detach(10U);

    /* Fill the queue with alternating edges and replaceable drag motion.
     * A final edge must evict only motion and retain every button transition. */
    hid_mouse_attach(11U);
    uint8_t accepted_buttons = 0U;
    for (uint32_t index = 0U; index < 15U; ++index) {
        accepted_buttons ^= 1U;
        uint8_t edge[3] = {accepted_buttons, 0U, 0U};
        uint8_t drag[3] = {accepted_buttons, 1U, 1U};
        assert(hid_mouse_report(11U, edge, sizeof(edge)));
        assert(hid_mouse_report(11U, drag, sizeof(drag)));
    }
    uint8_t retained_wheel[4] = {accepted_buttons, 0U, 0U, 1U};
    assert(hid_mouse_report(11U, retained_wheel, sizeof(retained_wheel)));
    accepted_buttons ^= 1U;
    uint8_t final_edge[3] = {accepted_buttons, 0U, 0U};
    assert(hid_mouse_report(11U, final_edge, sizeof(final_edge)));

    uint32_t edge_count = 0U;
    uint32_t wheel_count = 0U;
    uint32_t event_count = 0U;
    uint32_t prior_buttons = 0U;
    while (hid_mouse_read_event(&event) == 0) {
        event_count++;
        if (event.buttons != prior_buttons) {
            uint32_t expected = (edge_count & 1U) == 0U ? 1U : 0U;
            assert(event.buttons == expected);
            prior_buttons = event.buttons;
            edge_count++;
        }
        if (event.wheel != 0) {
            assert(event.wheel == 1);
            wheel_count++;
        }
    }
    assert(event_count == 31U);
    assert(edge_count == 16U && wheel_count == 1U);
    assert(prior_buttons == 0U);
    hid_mouse_detach(11U);

    /* A queue containing only nonreplaceable edges fails closed. The rejected
     * state is not accepted, so the same report remains an edge after drain. */
    hid_mouse_attach(12U);
    accepted_buttons = 0U;
    for (uint32_t index = 0U; index < 31U; ++index) {
        accepted_buttons ^= 1U;
        uint8_t edge[3] = {accepted_buttons, 0U, 0U};
        assert(hid_mouse_report(12U, edge, sizeof(edge)));
    }
    accepted_buttons ^= 1U;
    uint8_t rejected_edge[3] = {accepted_buttons, 0U, 0U};
    assert(!hid_mouse_report(12U, rejected_edge, sizeof(rejected_edge)));
    edge_count = 0U;
    prior_buttons = 0U;
    while (hid_mouse_read_event(&event) == 0) {
        assert(event.buttons != prior_buttons);
        prior_buttons = event.buttons;
        edge_count++;
    }
    assert(edge_count == 31U && prior_buttons == 1U);
    assert(hid_mouse_report(12U, rejected_edge, sizeof(rejected_edge)));
    assert(hid_mouse_read_event(&event) == 0 && event.buttons == 0U);
    assert(hid_mouse_read_event(&event) == -11);
    hid_mouse_detach(12U);

    atomic_uint remaining = 2U;
    producer_context_t first = {{0U, 1U, 1U, 0U}, &remaining};
    producer_context_t second = {{0U, (uint8_t)-1, 2U, 0U}, &remaining};
    reader_context_t reader = {&remaining, ATOMIC_VAR_INIT(0U)};
    hid_mouse_attach(19U);
    test_thread_t consumer = test_thread_start(consume_reports, &reader);
    test_thread_t producer_a = test_thread_start(publish_reports, &first);
    test_thread_t producer_b = test_thread_start(publish_reports, &second);
    test_thread_join(producer_a);
    test_thread_join(producer_b);
    test_thread_join(consumer);
    assert(atomic_load_explicit(&reader.consumed, memory_order_relaxed) > 0U);
    hid_mouse_detach(19U);
    return 0;
}
