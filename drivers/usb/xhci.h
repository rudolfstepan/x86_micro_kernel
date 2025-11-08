#ifndef USB_XHCI_H
#define USB_XHCI_H

#include "drivers/bus/pci.h"
#include <stdint.h>

/* xHCI probe function */
static int xhci_probe(pci_device_t *dev);

#endif /* USB_XHCI_H */
