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
#include <stdbool.h>
#include <stdint.h>

#define XHCI_DIAGNOSTICS_VERSION 2U

#define XHCI_CAP_REJECT_PORT_COUNT       (1U << 0U)
#define XHCI_CAP_REJECT_SCRATCHPADS      (1U << 1U)
#define XHCI_CAP_REJECT_CAPLENGTH        (1U << 2U)
#define XHCI_CAP_REJECT_PORT_RANGE       (1U << 3U)
#define XHCI_CAP_REJECT_DOORBELL_RANGE   (1U << 4U)
#define XHCI_CAP_REJECT_RUNTIME_RANGE    (1U << 5U)

enum {
    XHCI_DIAG_NOT_PROBED = 0U,
    XHCI_DIAG_PROBING,
    XHCI_DIAG_INVALID_BAR,
    XHCI_DIAG_MMIO_FAILED,
    XHCI_DIAG_CAPABILITIES_REJECTED,
    XHCI_DIAG_HANDOFF_FAILED,
    XHCI_DIAG_DMA_REJECTED,
    XHCI_DIAG_START_FAILED,
    XHCI_DIAG_NO_CONNECTED_PORT,
    XHCI_DIAG_NO_SUPPORTED_HID,
    XHCI_DIAG_IRQ_FAILED,
    XHCI_DIAG_KEYBOARD_READY,
    XHCI_DIAG_MOUSE_READY,
    XHCI_DIAG_DISCONNECTED
};

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t state;
    uint32_t port_count;
    uint32_t connected_ports;
    uint32_t attempts;
    uint32_t selected_port;
    uint32_t hid_protocol;
    uint32_t endpoint_id;
    uint32_t report_size;
    uint32_t irq;
    uint32_t transfer_events;
    uint32_t mouse_reports;
    uint32_t rejected_mouse_reports;
    uint32_t last_completion;
    uint32_t last_actual_length;
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    uint32_t cap_length;
    uint32_t max_slots;
    uint32_t scratchpad_count;
    uint32_t doorbell_offset;
    uint32_t runtime_offset;
    uint32_t capability_rejections;
} xhci_diagnostics_t;

/* Initialise one bounded xHCI root-port HID boot keyboard or mouse. */
int xhci_probe(pci_device_t *dev);

/* Drain deferred port/report work from task context, never from an IRQ. */
void xhci_poll(void);

/* Return one fixed snapshot suitable for a non-IRQ diagnostic command. */
bool xhci_get_diagnostics(xhci_diagnostics_t *diagnostics);

#endif /* USB_XHCI_H */
