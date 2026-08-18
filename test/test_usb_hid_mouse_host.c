/** @file test/test_usb_hid_mouse_host.c @brief HID-Boot-Maus Hosttest. */
#include "drivers/usb/hid_mouse.h"

#include <assert.h>
#include <stdint.h>

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
    return 0;
}
