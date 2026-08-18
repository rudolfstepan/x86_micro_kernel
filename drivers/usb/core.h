/**
 * @file drivers/usb/core.h
 * @brief USB-Core-Initialisierungsvertrag.
 *
 * Layer: Ring-0 USB driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: Geräte werden erst nach erfolgreicher Hostcontrollerinitialisierung sichtbar.
 */
#ifndef USB_CORE_H
#define USB_CORE_H

#include "drivers/bus/pci.h"
#include <stdint.h>

/* Forward declarations */
int xhci_probe(pci_device_t *dev);
void usb_init(void);

#endif /* USB_CORE_H */
