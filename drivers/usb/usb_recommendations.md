# USB Core Recommendations

## File: drivers/usb/usb_core.c

### General Structure
- Maintain current clear documentation and structure.
- Ensure all necessary headers are included.

### usb_init()
```c
void usb_init(void) {
    extern pci_device_t pci_devices[];
    extern size_t pci_device_count;

    // Add error handling for NULL or empty pci_devices
    if (!pci_devices || !pci_device_count) {
        printf("USB: No PCI devices to scan\n");
        return;
    }

    printf("USB: Scanning PCI devices for USB host controllers (%u devices)\n", (unsigned)pci_device_count);
    // ... rest of the function ...
}
```

### xhci_probe()
```c
int xhci_probe(pci_device_t *dev) {
    if (!dev) return -1;

    printf("xhci_probe: vendor=0x%04X device=0x%04X\n", dev->vendor_id, dev->device_id);

    // Add error handling for pci functions
    if (pci_enable_device(dev) != 0) {
        printf("USB: Failed to enable USB controller\n");
        return -1;
    }

    // ... rest of the function ...
}
```

### Integration Steps
1. Verify call order in driver initialization:
```c
// In kernel/init/driver_init.c (or equivalent)
void driver_init() {
    pci_init();
    usb_init();  // Add this line after pci_init()
    // ... other drivers ...
}
```

2. Future work tracking:
- Create tasks for implementing missing components:
  - xhci.c
  - core.h/core.c
  - hub.c
  - hid_kb.c
  - usb.h

### Additional Recommendations
1. Consider adding logging levels (info/warning/error)
2. Implement proper error codes instead of returning generic -1
3. Document expected PCI device states for each function
```