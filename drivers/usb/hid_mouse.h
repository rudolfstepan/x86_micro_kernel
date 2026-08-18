/**
 * @file drivers/usb/hid_mouse.h
 * @brief Begrenzte USB-HID-Boot-Maus und Ereignisqueue.
 */
#ifndef USB_HID_MOUSE_H
#define USB_HID_MOUSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HID_MOUSE_EVENT_VERSION 1U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t delta_x;
    int32_t delta_y;
    int32_t wheel;
    uint32_t buttons;
    uint32_t generation;
    uint32_t reserved;
} hid_mouse_event_t;

void hid_mouse_attach(uint32_t generation);
void hid_mouse_detach(uint32_t generation);
bool hid_mouse_report(uint32_t generation, const uint8_t *report,
                      size_t length);
int hid_mouse_read_event(hid_mouse_event_t *event);

#endif
