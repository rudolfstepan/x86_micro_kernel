#ifndef USB_XHCI_H
#define USB_XHCI_H

#include "drivers/bus/pci.h"
#include <stdint.h>

/* Returns failure until command/event rings and controller start-up exist. */
int xhci_probe(pci_device_t *dev);

#endif /* USB_XHCI_H */
