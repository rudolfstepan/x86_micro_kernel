/**
 * @file drivers/usb/ehci_companion.h
 * @brief Bounded EHCI-to-companion port handoff.
 *
 * EHCI owns USB-2 root ports after firmware on many AMD southbridges.  REIST
 * currently has no EHCI transfer backend, so those ports must be returned to
 * their OHCI/UHCI companions before the companion controller is probed.
 */
#ifndef USB_EHCI_COMPANION_H
#define USB_EHCI_COMPANION_H

#include <stdbool.h>
#include <stdint.h>

#define EHCI_COMPANION_DIAGNOSTICS_VERSION 1U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t controllers;
    uint32_t routed;
    uint32_t handoff_failures;
    uint32_t halt_failures;
    uint32_t invalid_bars;
} ehci_companion_diagnostics_t;

/* Route all discovered EHCI ports to legacy companions.  The operation is
 * idempotent, uses monotonic deadlines and leaves a controller bus-master
 * disabled after a successful handoff. */
bool ehci_route_ports_to_companions(void);

bool ehci_companion_get_diagnostics(
    ehci_companion_diagnostics_t *diagnostics_out);

#endif
