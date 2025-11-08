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

/* Minimal xHCI probe stub: enable device, print BAR/IRQ and enable bus mastering.
 * Real implementation must:
 *  - pci_enable_device(dev)
 *  - pci_set_bus_master(dev->bus, dev->slot, 1)
 *  - map MMIO BAR (64-bit BAR handling), allocate aligned structures,
 *  - initialize xHCI command/event rings, configure interrupts
 */
int xhci_probe(pci_device_t *dev) {
    if (!dev) return -1;

    printf("xhci_probe: vendor=0x%04X device=0x%04X\n", dev->vendor_id, dev->device_id);

    // Enable device memory/io and bus mastering
    pci_enable_device(dev);
    pci_set_bus_master(dev->bus, dev->slot, 1);

    // Print BAR0 for debugging (MMIO)
    uint32_t bar0 = pci_read_bar(dev, 0);
    printf("xhci_probe: BAR0 = 0x%08X\n", bar0);

    // IRQ configuration
    uint8_t irq = pci_configure_irq(dev);
    printf("xhci_probe: IRQ = %u\n", irq);

    // TODO: map MMIO, parse capability registers, init xHCI structures
    // For now return success so higher layers know a controller exists.
    return 0;
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
 * And update Makefile to compile drivers/usb/*.c
 */
