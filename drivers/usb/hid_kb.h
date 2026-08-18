/**
 * @file drivers/usb/hid_kb.h
 * @brief Begrenzter USB-HID-Boot-Keyboard-Vertrag.
 *
 * Layer: Ring-0 USB class driver.
 * Contract: Nur validierte acht Byte lange Boot-Reports werden veröffentlicht.
 * Safety: Rollover, unbekannte Usages und Reports nach Disconnect bleiben ohne Seiteneffekt.
 */
#ifndef USB_HID_KB_H
#define USB_HID_KB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void hid_keyboard_attach(uint32_t generation);
void hid_keyboard_detach(uint32_t generation);
bool hid_keyboard_report(uint32_t generation, const uint8_t *report,
                         size_t length);

#endif
