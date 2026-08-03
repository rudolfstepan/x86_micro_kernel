#include "xhci.h"
#include "lib/libc/stdio.h"

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

    // Report what was found, but do not enable DMA before rings exist.
    uint32_t bar0 = pci_read_bar(dev, 0);
    printf("xhci_probe: BAR0 = 0x%08X\n", bar0);

    // IRQ configuration
    uint8_t irq = pci_configure_irq(dev);
    printf("xhci_probe: IRQ = %u\n", irq);

    printf("xhci_probe: controller initialization is not implemented; leaving device disabled\n");
    return -1;
}
