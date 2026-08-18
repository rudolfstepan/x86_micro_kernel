/**
 * @file drivers/net/vmxnet3.c
 * @brief VMware-VMXNET3-Erkennung und Pakettransport.
 *
 * Layer: Ring-0 network driver and mediation.
 * Contract: Frames und Gerätezustand werden vor Veröffentlichung vollständig validiert.
 * Safety: Nur vollständig konfigurierte Ringe werden aktiviert; Reset widerruft Ownership.
 */
#include "vmxnet3.h"

#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "drivers/bus/pci.h"

#include <stddef.h>
#include <stdint.h>

#define VMXNET3_VENDOR_ID 0x15AD
#define VMXNET3_DEVICE_ID 0x07B0

/*
 * VMXNET3 cannot be driven by poking a producer index into a PCI BAR.  The
 * device first needs a DMA-backed shared-data structure, queue descriptors,
 * command/register BAR separation and ACTIVATE_DEV.  The old implementation
 * skipped all of that and used offsets outside BAR0, which could corrupt an
 * unrelated MMIO mapping.  Keep discovery explicit, but do not claim support
 * until the complete device contract is implemented.
 */
vmxnet3_device_t vmxnet3_device;

void vmxnet3_init(vmxnet3_device_t *dev) {
    if (dev) {
        dev->initialized = false;
        dev->mmio_base = NULL;
    }
}

static int vmxnet3_probe(pci_device_t *pci_dev) {
    if (!pci_dev || pci_dev->vendor_id != VMXNET3_VENDOR_ID ||
        pci_dev->device_id != VMXNET3_DEVICE_ID) {
        return -1;
    }

    vmxnet3_init(&vmxnet3_device);
    printf("VMXNET3 detected, but the driver is not implemented safely; device disabled\n");
    return -1;
}

void vmxnet3_register_driver(void) {
    pci_register_driver_named(VMXNET3_VENDOR_ID, VMXNET3_DEVICE_ID,
                              "VMware VMXNET3", vmxnet3_probe);
}

void vmxnet3_setup(void) {
    vmxnet3_register_driver();
}

void vmxnet3_transmit_packet(vmxnet3_device_t *dev, const uint8_t *data,
                             uint16_t length) {
    (void)dev;
    (void)data;
    (void)length;
    printf("VMXNET3 transmit unavailable: driver disabled\n");
}

void vmxnet3_receive_packet(vmxnet3_device_t *dev) {
    (void)dev;
}

void vmxnet3_send_packet(const uint8_t *data, uint16_t length) {
    vmxnet3_transmit_packet(&vmxnet3_device, data, length);
}

void vmxnet3_get_mac_address(vmxnet3_device_t *dev, uint8_t *mac) {
    (void)dev;
    if (mac) {
        memset(mac, 0, 6);
    }
}

void test_vmxnet3(void) {
    printf("VMXNET3 self-test skipped: driver disabled\n");
}
