/*
 * Minimal USB core + xHCI probe skeleton for x86_micro_kernel
 *
 * Purpose: provide a small, non-invasive entrypoint that scans already-discovered
 * PCI devices and calls an xHCI probe stub when a USB host controller is found.
 *
 * Notes:
 * - This file is intentionally small: it only logs detection and BAR/IRQ info.
 * - Next steps: split into core.h/core.c, add xhci.c implementing MMIO init,
 *   and call usb_init() from kernel/init/driver_init() after pci_init().
 */

#include "drivers/bus/pci.h"
#include "lib/libc/stdio.h"
#include <stdint.h>

/* Forward declarations for more complete HCDs */
int xhci_probe(pci_device_t *dev);

/* Scan pci_devices[] (filled by pci_init()) and call probes for USB HCI */
void usb_init(void) {
    extern pci_device_t pci_devices[];
    extern size_t pci_device_count;

    printf("USB: Scanning PCI devices for USB host controllers (%u devices)\n", (unsigned)pci_device_count);
    for (size_t i = 0; i < pci_device_count; i++) {
        pci_device_t *dev = &pci_devices[i];

        // Class 0x0C = Serial Bus Controller, Subclass 0x03 = USB controller
        if (dev->class_code == 0x0C && dev->subclass_code == 0x03) {
            printf("USB: Found USB controller at bus %u slot %u func %u (vendor=0x%04X device=0x%04X prog_if=0x%02X)\n",
                   dev->bus, dev->slot, dev->function, dev->vendor_id, dev->device_id, dev->prog_if);

            // Probe by programming interface if possible
            if (dev->prog_if == 0x30) {
                // xHCI
                if (xhci_probe(dev) == 0) {
                    printf("USB: xHCI probe succeeded\n");
                } else {
                    printf("USB: xHCI probe failed\n");
                }
            } else {
                printf("USB: Unsupported USB prog-if 0x%02X (EHCI/OHCI/UHCI not handled yet)\n", dev->prog_if);
            }
        }
    }
}

/* Hint for integration:
 *
 * Call usb_init() from kernel/init/driver_init() after pci_init():
 *
 *   pci_init();
 *   usb_init();   // <-- add this line to enable USB probe at boot
 *
 * After the HCD skeleton is implemented, create:
 *  - drivers/usb/xhci.c  + xhci.h
 *  - drivers/usb/core.c  + core.h
 *  - drivers/usb/hub.c
 *  - drivers/usb/hid_kb.c
 *  - include/usb/usb.h
 *
 * And update the Makefile to compile all USB driver C sources.
 */
