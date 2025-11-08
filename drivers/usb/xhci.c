#include "xhci.h"
#include "lib/libc/stdio.h"

/* Minimal xHCI probe stub: enable device, print BAR/IRQ and enable bus mastering.
 * Real implementation must:
 *  - pci_enable_device(dev)
 *  - pci_set_bus_master(dev->bus, dev->slot, 1)
 *  - map MMIO BAR (64-bit BAR handling), allocate aligned structures,
 *  - initialize xHCI command/event rings, configure interrupts
 */
static int xhci_probe(pci_device_t *dev) {
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
