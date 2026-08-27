/** @file test/test_usb_hid_keyboard_host.c @brief HID-Boot-Keyboard host test. */
#include "drivers/usb/hid_kb.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

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

typedef struct {
    uint8_t scan;
    int extended;
    int released;
} event_t;

static event_t events[32];
static size_t event_count;
static int record_events = 1;
static size_t parallel_event_count;

void kb_submit_key_event(uint8_t scan, bool extended, bool released) {
    if (!record_events) {
        assert(scan != 0U);
        (void)extended;
        (void)released;
        parallel_event_count++;
        return;
    }
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = (event_t){scan, extended ? 1 : 0,
                                      released ? 1 : 0};
}

static TEST_THREAD_RETURN publish_keyboard_reports(void *opaque) {
    const uint8_t *report = opaque;
    const uint8_t empty[8] = {0};
    for (uint32_t index = 0U; index < 2000U; ++index) {
        assert(hid_keyboard_report(17U, report, 8U));
        assert(hid_keyboard_report(17U, empty, sizeof(empty)));
    }
    return 0;
}

static void clear_events(void) {
    event_count = 0U;
}

int main(void) {
    const uint8_t empty[8] = {0};
    const uint8_t a[8] = {0, 0, 4, 0, 0, 0, 0, 0};
    const uint8_t shift_a[8] = {2, 0, 4, 0, 0, 0, 0, 0};
    const uint8_t rollover[8] = {0, 0, 1, 0, 0, 0, 0, 0};

    hid_keyboard_attach(7U);
    assert(hid_keyboard_report(7U, a, sizeof(a)));
    assert(event_count == 1U && events[0].scan == 0x1EU &&
           events[0].released == 0);
    clear_events();
    assert(hid_keyboard_report(7U, a, sizeof(a)));
    assert(event_count == 0U);

    assert(hid_keyboard_report(7U, shift_a, sizeof(shift_a)));
    assert(event_count == 1U && events[0].scan == 0x2AU &&
           events[0].released == 0);
    clear_events();
    assert(hid_keyboard_report(7U, empty, sizeof(empty)));
    assert(event_count == 2U && events[0].scan == 0x1EU &&
           events[0].released != 0 && events[1].scan == 0x2AU &&
           events[1].released != 0);

    clear_events();
    assert(!hid_keyboard_report(6U, a, sizeof(a)));
    assert(!hid_keyboard_report(7U, rollover, sizeof(rollover)));
    assert(event_count == 0U);

    assert(hid_keyboard_report(7U, shift_a, sizeof(shift_a)));
    clear_events();
    hid_keyboard_detach(7U);
    assert(event_count == 2U && events[0].scan == 0x2AU &&
           events[0].released != 0 && events[1].scan == 0x1EU &&
           events[1].released != 0);
    hid_keyboard_detach(7U);
    assert(event_count == 2U);

    record_events = 0;
    parallel_event_count = 0U;
    hid_keyboard_attach(17U);
    test_thread_t first = test_thread_start(publish_keyboard_reports,
                                            (void *)a);
    test_thread_t second = test_thread_start(publish_keyboard_reports,
                                             (void *)shift_a);
    test_thread_join(first);
    test_thread_join(second);
    hid_keyboard_detach(17U);
    assert(parallel_event_count > 0U);
    return 0;
}
