#include "core.h"
#include "lib/libc/stdio.h"

/* Scan pci_devices[] (filled by pci_init()) and call probes for USB HCI */
static void usb_init(void) {
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
