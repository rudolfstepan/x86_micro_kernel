/**
 * @file drivers/usb/xhci.h
 * @brief xHCI-Controller-Schnittstelle.
 *
 * Layer: Ring-0 USB driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: MMIO-, Ring- und Kontextadressen müssen ausgerichtet und begrenzt sein.
 */
#ifndef USB_XHCI_H
#define USB_XHCI_H

#include "drivers/bus/pci.h"
#include <stdint.h>

/* Returns failure until command/event rings and controller start-up exist. */
int xhci_probe(pci_device_t *dev);

#endif /* USB_XHCI_H */
