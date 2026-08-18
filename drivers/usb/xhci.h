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

/* Initialise one bounded xHCI root-port HID boot keyboard or mouse. */
int xhci_probe(pci_device_t *dev);

/* Drain deferred port/report work from task context, never from an IRQ. */
void xhci_poll(void);

#endif /* USB_XHCI_H */
