#ifndef USB_CORE_H
#define USB_CORE_H

#include "drivers/bus/pci.h"
#include <stdint.h>

/* Forward declarations */
int xhci_probe(pci_device_t *dev);
static void usb_init(void);

#endif /* USB_CORE_H */
