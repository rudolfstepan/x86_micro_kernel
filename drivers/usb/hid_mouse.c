/**
 * @file drivers/usb/hid_mouse.c
 * @brief USB-HID-Boot-Mausreports als feste Ringqueue.
 *
 * Reports werden nur für die aktive Gerätegeneration akzeptiert. Die Queue
 * ist fest begrenzt. Aufeinanderfolgende reine Relativbewegung wird summiert;
 * Tasten- und Radereignisse bleiben geordnet und werden nie still ersetzt.
 */
#include "hid_mouse.h"
#include "hid_sync.h"

#include "lib/libc/string.h"

#define HID_MOUSE_QUEUE_CAPACITY 32U

static hid_mouse_event_t events[HID_MOUSE_QUEUE_CAPACITY];
static bool event_replaceable[HID_MOUSE_QUEUE_CAPACITY];
static volatile uint32_t event_head;
static volatile uint32_t event_tail;
static uint32_t active_generation;
static uint32_t current_buttons;
static bool attached;
static hid_sync_lock_t mouse_state_lock = HID_SYNC_LOCK_INIT;

static uint32_t hid_mouse_previous_slot(uint32_t slot) {
    return slot == 0U ? HID_MOUSE_QUEUE_CAPACITY - 1U : slot - 1U;
}

static bool hid_mouse_queue_full(void) {
    return (event_head + 1U) % HID_MOUSE_QUEUE_CAPACITY == event_tail;
}

static int32_t hid_mouse_accumulate_delta(int32_t current, int32_t delta) {
    int64_t sum = (int64_t)current + (int64_t)delta;
    if (sum > INT32_MAX) return INT32_MAX;
    if (sum < INT32_MIN) return INT32_MIN;
    return (int32_t)sum;
}

static bool hid_mouse_coalesce_latest(int32_t delta_x, int32_t delta_y,
                                      uint32_t buttons) {
    if (event_head == event_tail) return false;
    uint32_t latest = hid_mouse_previous_slot(event_head);
    if (!event_replaceable[latest] || events[latest].wheel != 0 ||
        events[latest].buttons != buttons ||
        events[latest].generation != active_generation) return false;
    events[latest].delta_x = hid_mouse_accumulate_delta(
        events[latest].delta_x, delta_x);
    events[latest].delta_y = hid_mouse_accumulate_delta(
        events[latest].delta_y, delta_y);
    return true;
}

/* Fixed O(capacity) compaction is used only when a nonreplaceable report
 * arrives at a full queue. Order is retained and only old pure motion may be
 * sacrificed. */
static bool hid_mouse_discard_oldest_motion(void) {
    uint32_t slot = event_tail;
    while (slot != event_head && !event_replaceable[slot])
        slot = (slot + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    if (slot == event_head) return false;

    uint32_t source = (slot + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    while (source != event_head) {
        events[slot] = events[source];
        event_replaceable[slot] = event_replaceable[source];
        slot = source;
        source = (source + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    }
    event_head = hid_mouse_previous_slot(event_head);
    memset(&events[event_head], 0, sizeof(events[event_head]));
    event_replaceable[event_head] = false;
    return true;
}

void hid_mouse_attach(uint32_t generation) {
    uint32_t flags = hid_sync_lock_acquire(&mouse_state_lock);
    memset(events, 0, sizeof(events));
    memset(event_replaceable, 0, sizeof(event_replaceable));
    event_head = event_tail = 0U;
    active_generation = generation;
    current_buttons = 0U;
    attached = generation != 0U;
    hid_sync_lock_release(&mouse_state_lock, flags);
}

void hid_mouse_detach(uint32_t generation) {
    uint32_t flags = hid_sync_lock_acquire(&mouse_state_lock);
    if (attached && generation == active_generation) {
        memset(events, 0, sizeof(events));
        memset(event_replaceable, 0, sizeof(event_replaceable));
        event_head = event_tail = 0U;
        active_generation = 0U;
        current_buttons = 0U;
        attached = false;
    }
    hid_sync_lock_release(&mouse_state_lock, flags);
}

bool hid_mouse_report(uint32_t generation, const uint8_t *report,
                      size_t length) {
    if (!report || (length != 3U && length != 4U)) return false;
    uint32_t flags = hid_sync_lock_acquire(&mouse_state_lock);
    if (!attached || generation != active_generation ||
        (report[0] & 0xF8U) != 0U) {
        hid_sync_lock_release(&mouse_state_lock, flags);
        return false;
    }
    uint32_t buttons = report[0] & 0x07U;
    int32_t delta_x = (int8_t)report[1];
    int32_t delta_y = (int8_t)report[2];
    int32_t wheel = length == 4U ? (int8_t)report[3] : 0;
    bool button_edge = buttons != current_buttons;
    bool replaceable = !button_edge && wheel == 0;

    if (replaceable && delta_x == 0 && delta_y == 0) {
        hid_sync_lock_release(&mouse_state_lock, flags);
        return true;
    }
    if (replaceable && hid_mouse_coalesce_latest(
            delta_x, delta_y, buttons)) {
        hid_sync_lock_release(&mouse_state_lock, flags);
        return true;
    }
    if (hid_mouse_queue_full()) {
        if (replaceable) {
            /* A pure-motion report after a retained edge cannot be moved in
             * front of that edge. It may be dropped under bounded overload. */
            hid_sync_lock_release(&mouse_state_lock, flags);
            return true;
        }
        if (!hid_mouse_discard_oldest_motion()) {
            /* The controller diagnostics count this rejected edge. Keep the
             * accepted state unchanged so a repeated report remains an edge. */
            hid_sync_lock_release(&mouse_state_lock, flags);
            return false;
        }
    }
    uint32_t next = (event_head + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    events[event_head] = (hid_mouse_event_t){
        .version = HID_MOUSE_EVENT_VERSION,
        .struct_size = sizeof(hid_mouse_event_t),
        .delta_x = delta_x,
        .delta_y = delta_y,
        .wheel = wheel,
        .buttons = buttons,
        .generation = active_generation,
        .reserved = 0U
    };
    event_replaceable[event_head] = replaceable;
    event_head = next;
    current_buttons = buttons;
    hid_sync_lock_release(&mouse_state_lock, flags);
    return true;
}

int hid_mouse_read_event(hid_mouse_event_t *event) {
    if (!event) return -22;
    uint32_t flags = hid_sync_lock_acquire(&mouse_state_lock);
    if (!attached) {
        hid_sync_lock_release(&mouse_state_lock, flags);
        return -19;
    }
    if (event_tail == event_head) {
        hid_sync_lock_release(&mouse_state_lock, flags);
        return -11;
    }
    *event = events[event_tail];
    event_replaceable[event_tail] = false;
    event_tail = (event_tail + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    hid_sync_lock_release(&mouse_state_lock, flags);
    return 0;
}
