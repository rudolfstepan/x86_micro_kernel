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

    atomic_uint remaining = 2U;
    producer_context_t first = {{1U, 1U, 1U, 0U}, &remaining};
    producer_context_t second = {{2U, (uint8_t)-1, 2U, 1U}, &remaining};
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
