/**
 * @file drivers/usb/hid_mouse.c
 * @brief USB-HID-Boot-Mausreports als feste Ringqueue.
 *
 * Reports werden nur für die aktive Gerätegeneration akzeptiert. Die Queue
 * ist fest begrenzt; bei Überlauf wird das älteste Bewegungsereignis ersetzt,
 * der aktuelle Tastenstand bleibt dabei erhalten.
 */
#include "hid_mouse.h"

#include "lib/libc/string.h"

#ifdef HID_MOUSE_HOST_TEST
static uint32_t irq_save(void) { return 0U; }
static void irq_restore(uint32_t flags) { (void)flags; }
#else
#include "arch/x86/include/interrupt.h"
#endif

#define HID_MOUSE_QUEUE_CAPACITY 32U

static hid_mouse_event_t events[HID_MOUSE_QUEUE_CAPACITY];
static volatile uint32_t event_head;
static volatile uint32_t event_tail;
static uint32_t active_generation;
static uint32_t current_buttons;
static bool attached;

void hid_mouse_attach(uint32_t generation) {
    uint32_t flags = irq_save();
    memset(events, 0, sizeof(events));
    event_head = event_tail = 0U;
    active_generation = generation;
    current_buttons = 0U;
    attached = generation != 0U;
    irq_restore(flags);
}

void hid_mouse_detach(uint32_t generation) {
    uint32_t flags = irq_save();
    if (attached && generation == active_generation) {
        event_head = event_tail = 0U;
        active_generation = 0U;
        current_buttons = 0U;
        attached = false;
    }
    irq_restore(flags);
}

bool hid_mouse_report(uint32_t generation, const uint8_t *report,
                      size_t length) {
    if (!report || (length != 3U && length != 4U)) return false;
    uint32_t flags = irq_save();
    if (!attached || generation != active_generation ||
        (report[0] & 0xF8U) != 0U) {
        irq_restore(flags);
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
    irq_restore(flags);
    return true;
}

int hid_mouse_read_event(hid_mouse_event_t *event) {
    if (!event) return -22;
    uint32_t flags = irq_save();
    if (!attached) {
        irq_restore(flags);
        return -19;
    }
    if (event_tail == event_head) {
        irq_restore(flags);
        return -11;
    }
    *event = events[event_tail];
    event_tail = (event_tail + 1U) % HID_MOUSE_QUEUE_CAPACITY;
    irq_restore(flags);
    return 0;
}
