/**
 * @file userspace/programs/usbinfo.c
 * @brief Zeigt den persistenten USB-HID-Diagnose-Snapshot.
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
        case X86OS_USB_STATE_PORT_ROUTING_FAILED:
            return "intel-port-routing-failed";
        case X86OS_USB_STATE_KEYBOARD_MOUSE_READY:
            return "keyboard-mouse-ready";
        default: return "unknown";
    }
}

static const char *failure_name(uint32_t stage) {
    switch (stage) {
        case X86OS_USB_FAILURE_NONE: return "none";
        case X86OS_USB_FAILURE_PORT_RESET: return "port-reset";
        case X86OS_USB_FAILURE_ADDRESS_DEVICE: return "address-device";
        case X86OS_USB_FAILURE_DEVICE_DESCRIPTOR_8: return "device-desc-8";
        case X86OS_USB_FAILURE_EP0_DESCRIPTOR: return "ep0-desc";
        case X86OS_USB_FAILURE_DEVICE_DESCRIPTOR: return "device-desc";
        case X86OS_USB_FAILURE_CONFIG_HEADER: return "config-header";
        case X86OS_USB_FAILURE_CONFIG_LENGTH: return "config-length";
        case X86OS_USB_FAILURE_CONFIG_DESCRIPTOR: return "config-desc";
        case X86OS_USB_FAILURE_NO_BOOT_HID: return "no-boot-hid";
        case X86OS_USB_FAILURE_CONFIGURE_ENDPOINT:
            return "configure-endpoint";
        case X86OS_USB_FAILURE_SET_CONFIGURATION:
            return "set-configuration";
        case X86OS_USB_FAILURE_RELEASE_SLOT: return "release-slot";
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
    x86os_usb_diagnostics_t status;
    if (x86os_usb_diagnostics(&status) != 0 ||
        status.version != X86OS_USB_DIAGNOSTICS_VERSION ||
        status.struct_size != sizeof(status)) {
        x86os_puts("USB diagnostics unavailable.\n");
        return 1;
    }

    print_controller_counts(&status);
    x86os_puts(status.backend == X86OS_USB_BACKEND_OHCI
        ? "OHCI state=" : status.backend == X86OS_USB_BACKEND_XHCI
        ? "xHCI state=" : "USB state=");
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

    x86os_puts("     failure=");
    x86os_puts(failure_name(status.failure_stage));
    x86os_puts(" candidate-port=");
    print_unsigned(status.candidate_port);
    x86os_puts(" speed=");
    print_unsigned(status.candidate_speed);
    x86os_puts(" class=");
    print_unsigned(status.device_class);
    x86os_putchar('/');
    print_unsigned(status.device_subclass);
    x86os_putchar('/');
    print_unsigned(status.device_protocol);
    x86os_puts(" config-len=");
    print_unsigned(status.configuration_length);
    x86os_putchar('\n');

    x86os_puts("     control type=");
    print_hex32(status.control_request_type);
    x86os_puts(" request=");
    print_unsigned(status.control_request);
    x86os_puts(" value=");
    print_hex32(status.control_value);
    x86os_puts(" index=");
    print_unsigned(status.control_index);
    x86os_puts(" length=");
    print_unsigned(status.control_length);
    x86os_puts(" cc=");
    print_unsigned(status.control_completion);
    x86os_puts(" residual=");
    print_unsigned(status.control_residual);
    x86os_puts(" stage=");
    print_unsigned(status.control_event_stage);
    x86os_puts(" flags=");
    print_hex32(status.control_flags);
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

    x86os_puts("     keyboard port=");
    print_unsigned(status.keyboard_port);
    x86os_puts(" slot=");
    print_unsigned(status.keyboard_slot);
    x86os_puts(" endpoint=");
    print_unsigned(status.keyboard_endpoint);
    x86os_puts(" mouse port=");
    print_unsigned(status.mouse_port);
    x86os_puts(" slot=");
    print_unsigned(status.mouse_slot);
    x86os_puts(" endpoint=");
    print_unsigned(status.mouse_endpoint);
    x86os_putchar('\n');

    x86os_puts("     transfers=");
    print_unsigned(status.transfer_events);
    x86os_puts(" keyboard=");
    print_unsigned(status.keyboard_reports);
    x86os_puts(" key-rejected=");
    print_unsigned(status.rejected_keyboard_reports);
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

    x86os_puts("     pci=");
    print_hex32(status.vendor_id);
    x86os_putchar(':');
    print_hex32(status.device_id);
    x86os_puts(" route-flags=");
    print_hex32(status.intel_routing_flags);
    x86os_putchar('\n');
    x86os_puts("     usb2-mask=");
    print_hex32(status.usb2_routing_mask);
    x86os_puts(" routed=");
    print_hex32(status.usb2_routing);
    x86os_puts(" usb3-mask=");
    print_hex32(status.usb3_routing_mask);
    x86os_puts(" enabled=");
    print_hex32(status.usb3_routing);
    x86os_putchar('\n');

    if (status.state == X86OS_USB_STATE_KEYBOARD_MOUSE_READY &&
        status.mouse_reports == 0U) {
        x86os_puts("Result: keyboard and mouse configured; no mouse reports yet.\n");
    } else if (status.state == X86OS_USB_STATE_KEYBOARD_MOUSE_READY) {
        x86os_puts("Result: USB keyboard and mouse are ready.\n");
    } else if (status.state == X86OS_USB_STATE_MOUSE_READY &&
        status.mouse_reports == 0U) {
        x86os_puts("Result: mouse configured, no interrupt reports received.\n");
    } else if (status.state == X86OS_USB_STATE_MOUSE_READY) {
        x86os_puts("Result: USB mouse reports reach the kernel.\n");
    } else if (status.xhci_controllers == 0U &&
               status.backend == X86OS_USB_BACKEND_NONE &&
               (status.ehci_controllers != 0U ||
                status.ohci_controllers != 0U ||
                status.uhci_controllers != 0U)) {
        x86os_puts("Result: no supported boot HID is ready on legacy USB.\n");
    } else if (status.state == X86OS_USB_STATE_KEYBOARD_READY) {
        x86os_puts("Result: boot keyboard selected, no root-port mouse.\n");
    } else {
        x86os_puts("Result: selected USB backend is not ready; state is failure stage.\n");
    }
    return 0;
}
