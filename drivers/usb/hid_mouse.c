/**
 * @file drivers/usb/hid_mouse.c
 * @brief USB-HID-Boot-Mausreports als feste Ringqueue.
 *
 * Reports werden nur für die aktive Gerätegeneration akzeptiert. Die Queue
 * ist fest begrenzt; bei Überlauf wird das älteste Bewegungsereignis ersetzt,
 * der aktuelle Tastenstand bleibt dabei erhalten.
 */
#include "hid_mouse.h"
#include "hid_sync.h"

#include "lib/libc/string.h"

#define HID_MOUSE_QUEUE_CAPACITY 32U

static hid_mouse_event_t events[HID_MOUSE_QUEUE_CAPACITY];
static volatile uint32_t event_head;
static volatile uint32_t event_tail;
static uint32_t active_generation;
static uint32_t current_buttons;
static bool attached;
static hid_sync_lock_t mouse_state_lock = HID_SYNC_LOCK_INIT;

void hid_mouse_attach(uint32_t generation) {
    uint32_t flags = hid_sync_lock_acquire(&mouse_state_lock);
    memset(events, 0, sizeof(events));
    event_head = event_tail = 0U;
    active_generation = generation;
    current_buttons = 0U;
    attached = generation != 0U;
    hid_sync_lock_release(&mouse_state_lock, flags);
}

void hid_mouse_detach(uint32_t generation) {
    uint32_t flags = hid_sync_lock_acquire(&mouse_state_lock);
    if (attached && generation == active_generation) {
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
    uint32_t next = (event_head + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    if (next == event_tail)
        event_tail = (event_tail + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    current_buttons = report[0] & 0x07U;
    events[event_head] = (hid_mouse_event_t){
        .version = HID_MOUSE_EVENT_VERSION,
        .struct_size = sizeof(hid_mouse_event_t),
        .delta_x = (int8_t)report[1],
        .delta_y = (int8_t)report[2],
        .wheel = length == 4U ? (int8_t)report[3] : 0,
        .buttons = current_buttons,
        .generation = active_generation,
        .reserved = 0U
    };
    event_head = next;
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
    event_tail = (event_tail + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    hid_sync_lock_release(&mouse_state_lock, flags);
    return 0;
}
