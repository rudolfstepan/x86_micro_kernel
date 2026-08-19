/**
 * @file userspace/programs/usbinfo.c
 * @brief Zeigt den persistenten USB/xHCI-Mausdiagnose-Snapshot.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Nur die versionierte, vom Kernel kopierte Diagnose-ABI wird gelesen.
 * Safety: Keine Gerätezugriffe aus Ring 3; Ausgabe und Arbeit sind fest begrenzt.
 */
#include "x86os.h"

static void print_unsigned(uint32_t value) {
    char digits[10];
    unsigned count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        x86os_putchar(digits[(value >> (uint32_t)shift) & 0x0FU]);
}

static const char *state_name(uint32_t state) {
    switch (state) {
        case X86OS_USB_STATE_NOT_PROBED: return "not-probed";
        case X86OS_USB_STATE_PROBING: return "probing";
        case X86OS_USB_STATE_INVALID_BAR: return "invalid-bar";
        case X86OS_USB_STATE_MMIO_FAILED: return "mmio-failed";
        case X86OS_USB_STATE_CAPABILITIES_REJECTED:
            return "capabilities-rejected";
        case X86OS_USB_STATE_HANDOFF_FAILED: return "bios-handoff-failed";
        case X86OS_USB_STATE_DMA_REJECTED: return "dma-rejected";
        case X86OS_USB_STATE_START_FAILED: return "controller-start-failed";
        case X86OS_USB_STATE_NO_CONNECTED_PORT:
            return "no-connected-root-port";
        case X86OS_USB_STATE_NO_SUPPORTED_HID:
            return "no-supported-boot-hid";
        case X86OS_USB_STATE_IRQ_FAILED: return "irq-failed";
        case X86OS_USB_STATE_KEYBOARD_READY: return "keyboard-ready";
        case X86OS_USB_STATE_MOUSE_READY: return "mouse-ready";
        case X86OS_USB_STATE_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

static void print_controller_counts(const x86os_usb_diagnostics_t *status) {
    x86os_puts("USB host controllers: xHCI=");
    print_unsigned(status->xhci_controllers);
    x86os_puts(" EHCI=");
    print_unsigned(status->ehci_controllers);
    x86os_puts(" OHCI=");
    print_unsigned(status->ohci_controllers);
    x86os_puts(" UHCI=");
    print_unsigned(status->uhci_controllers);
    x86os_puts(" other=");
    print_unsigned(status->other_controllers);
    x86os_putchar('\n');
}

int main(void) {
    x86os_usb_diagnostics_t status = {0};
    if (x86os_usb_diagnostics(&status) != 0 ||
        status.version != X86OS_USB_DIAGNOSTICS_VERSION ||
        status.struct_size != sizeof(status)) {
        x86os_puts("USB diagnostics unavailable.\n");
        return 1;
    }

    print_controller_counts(&status);
    x86os_puts("xHCI state=");
    x86os_puts(state_name(status.state));
    x86os_puts(" bdf=");
    print_unsigned(status.bus);
    x86os_putchar(':');
    print_unsigned(status.slot);
    x86os_putchar('.');
    print_unsigned(status.function);
    x86os_puts(" ports=");
    print_unsigned(status.port_count);
    x86os_puts(" connected=");
    print_hex32(status.connected_ports);
    x86os_puts(" attempts=");
    print_unsigned(status.attempts);
    x86os_putchar('\n');

    x86os_puts("     selected=");
    print_unsigned(status.selected_port);
    x86os_puts(" protocol=");
    print_unsigned(status.hid_protocol);
    x86os_puts(" endpoint=");
    print_unsigned(status.endpoint_id);
    x86os_puts(" report=");
    print_unsigned(status.report_size);
    x86os_puts(" irq=");
    print_unsigned(status.irq);
    x86os_putchar('\n');

    x86os_puts("     transfers=");
    print_unsigned(status.transfer_events);
    x86os_puts(" mouse=");
    print_unsigned(status.mouse_reports);
    x86os_puts(" rejected=");
    print_unsigned(status.rejected_mouse_reports);
    x86os_puts(" last-cc=");
    print_unsigned(status.last_completion);
    x86os_puts(" last-len=");
    print_unsigned(status.last_actual_length);
    x86os_putchar('\n');

    x86os_puts("     caplen=");
    print_unsigned(status.cap_length);
    x86os_puts(" slots=");
    print_unsigned(status.max_slots);
    x86os_puts(" scratch=");
    print_unsigned(status.scratchpad_count);
    x86os_puts(" db=");
    print_hex32(status.doorbell_offset);
    x86os_puts(" rt=");
    print_hex32(status.runtime_offset);
    x86os_puts(" reject=");
    print_hex32(status.capability_rejections);
    x86os_putchar('\n');

    if (status.state == X86OS_USB_STATE_MOUSE_READY &&
        status.mouse_reports == 0U) {
        x86os_puts("Result: mouse configured, no interrupt reports received.\n");
    } else if (status.state == X86OS_USB_STATE_MOUSE_READY) {
        x86os_puts("Result: xHCI mouse reports reach the kernel.\n");
    } else if (status.xhci_controllers == 0U &&
               (status.ehci_controllers != 0U ||
                status.ohci_controllers != 0U ||
                status.uhci_controllers != 0U)) {
        x86os_puts("Result: only unsupported legacy USB controllers found.\n");
    } else if (status.state == X86OS_USB_STATE_KEYBOARD_READY) {
        x86os_puts("Result: boot keyboard selected, no root-port mouse.\n");
    } else {
        x86os_puts("Result: xHCI mouse not ready; state is failure stage.\n");
    }
    return 0;
}
