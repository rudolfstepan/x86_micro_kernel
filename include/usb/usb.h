#ifndef USB_H
#define USB_H

#include <stdint.h>
#include "drivers/bus/pci.h"

/* Basic USB descriptor types and constants (minimal set for enumeration) */
#define USB_CLASS_PER_INTERFACE      0x00
#define USB_CLASS_HID                0x03
#define USB_CLASS_MASS_STORAGE       0x08

#define USB_DT_DEVICE                0x01
#define USB_DT_CONFIG                0x02
#define USB_DT_STRING                0x03
#define USB_DT_INTERFACE             0x04
#define USB_DT_ENDPOINT              0x05

/* Standard USB request codes */
#define USB_REQ_GET_DESCRIPTOR       0x06
#define USB_REQ_SET_ADDRESS          0x05
#define USB_REQ_SET_CONFIGURATION    0x09

/* Prototype for high-level USB initialization (called after pci_init) */
void usb_init(void);

/* Host controller probe hook (implemented by HCD) */
int xhci_probe(pci_device_t *dev);

#endif /* USB_H */
