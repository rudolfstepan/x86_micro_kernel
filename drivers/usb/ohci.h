/**
 * @file drivers/usb/ohci.h
 * @brief OHCI-Controller-Schnittstelle fuer USB-HID-Boot-Geraete.
 *
 * Layer: Ring-0 USB driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren
 * Seiteneffekten geprueft.
 * Safety: MMIO-, HCCA- und Deskriptoradressen muessen ausgerichtet und
 * 32-Bit-adressierbar sein; alle Wartezeiten sind an monotone Deadlines
 * gebunden.
 */
#ifndef USB_OHCI_H
#define USB_OHCI_H

#include "drivers/bus/pci.h"

#include <stdbool.h>
#include <stdint.h>

#define OHCI_DIAGNOSTICS_VERSION 2U

enum {
    OHCI_DIAG_NOT_PROBED = 0U,
    OHCI_DIAG_PROBING,
    OHCI_DIAG_INVALID_BAR,
    OHCI_DIAG_MMIO_FAILED,
    OHCI_DIAG_UNSUPPORTED_REVISION,
    OHCI_DIAG_DMA_REJECTED,
    OHCI_DIAG_HANDOFF_FAILED,
    OHCI_DIAG_RESET_FAILED,
    OHCI_DIAG_NO_ROOT_PORTS,
    OHCI_DIAG_NO_CONNECTED_PORT,
    OHCI_DIAG_PORT_RESET_FAILED,
    OHCI_DIAG_OPERATIONAL,
    OHCI_DIAG_KEYBOARD_READY,
    OHCI_DIAG_MOUSE_READY,
    OHCI_DIAG_KEYBOARD_MOUSE_READY,
    OHCI_DIAG_DISCONNECTED
};

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t state;
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t revision;
    uint32_t port_count;
    uint32_t connected_ports;
    uint32_t selected_port;
    uint32_t reset_attempts;
    uint32_t hid_protocol;
    uint32_t endpoint;
    uint32_t report_size;
    uint32_t keyboard_reports;
    uint32_t rejected_keyboard_reports;
    uint32_t mouse_reports;
    uint32_t rejected_mouse_reports;
    uint32_t keyboard_port;
    uint32_t keyboard_endpoint;
    uint32_t mouse_port;
    uint32_t mouse_endpoint;
} ohci_diagnostics_t;

enum {
    OHCI_PROBE_READY = 0,
    OHCI_PROBE_NO_SUPPORTED_DEVICE = 1,
};

/* Bring one OHCI controller to the operational state and detect connected
 * root-hub ports.  Returns 0 when the controller is operational. */
int ohci_probe(pci_device_t *dev);

/* Service attached OHCI HID endpoints.  Bounded and safe to call when no
 * controller is present. */
void ohci_poll(void);

/* Copy the bounded persistent controller snapshot. */
bool ohci_get_diagnostics(ohci_diagnostics_t *diagnostics_out);

#endif /* USB_OHCI_H */
