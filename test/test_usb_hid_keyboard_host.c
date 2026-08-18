/** @file test/test_usb_hid_keyboard_host.c @brief HID-Boot-Keyboard host test. */
#include "drivers/usb/hid_kb.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t scan;
    int extended;
    int released;
} event_t;

static event_t events[32];
static size_t event_count;

void kb_submit_key_event(uint8_t scan, bool extended, bool released) {
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = (event_t){scan, extended ? 1 : 0,
                                      released ? 1 : 0};
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
    return 0;
}
