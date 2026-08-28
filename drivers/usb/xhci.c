/**
 * @file drivers/usb/xhci.c
 * @brief Begrenzter xHCI-Host für USB-HID-Boot-Tastatur und -Maus.
 *
 * Der Treiber verwaltet höchstens eine Boot-Tastatur und eine Boot-Maus an
 * getrennten Root-Ports. Alle DMA-Objekte sind statisch, 32-Bit-adressierbar
 * und ausgerichtet. Enumeration und Control-Transfers laufen mit monotonen
 * Deadlines; ein unbekanntes Gerät wird ohne Veröffentlichung abgewiesen.
 */
#include "xhci.h"
#include "hid_kb.h"
#include "hid_mouse.h"
#include "drivers/char/kb.h"
#include "drivers/char/io.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/include/interrupt.h"
#include "kernel/time/pit.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XHCI_MMIO_SIZE          0x10000U
#define XHCI_TIMEOUT_MS         1000U
#define XHCI_POLL_LIMIT         1000000U
#define XHCI_MAX_PORTS          32U
#define XHCI_MAX_HID_CANDIDATES 8U
#define XHCI_MAX_HID_DEVICES    2U
#define XHCI_COMMAND_RING_TRBS  64U
#define XHCI_EVENT_RING_TRBS    128U
#define XHCI_ENDPOINT_RING_TRBS 32U
#define XHCI_CONTEXT_BYTES      64U
#define XHCI_MAX_SCRATCHPADS    32U
#define XHCI_CONTROL_BYTES      256U
#define XHCI_PORT_SETTLE_MS     500U
#define XHCI_PORT_POWER_SETTLE_MS 20U
#define XHCI_PORT_RESET_RECOVERY_MS 10U
#define XHCI_SET_ADDRESS_RECOVERY_MS 2U
#define XHCI_PORT_MAX_RECOVERY_MS XHCI_PORT_POWER_SETTLE_MS

#define XHCI_INTEL_VENDOR_ID       0x8086U
#define XHCI_SONY_VENDOR_ID        0x104DU
#define XHCI_SONY_VAIO_SUBDEVICE   0x90A8U
#define XHCI_PCI_SUBSYSTEM_IDS     0x2CU
#define XHCI_INTEL_XUSB2PR         0xD0U
#define XHCI_INTEL_USB2PRM         0xD4U
#define XHCI_INTEL_USB3_PSSEN      0xD8U
#define XHCI_INTEL_USB3PRM         0xDCU

#define XHCI_USBCMD             0x00U
#define XHCI_USBSTS             0x04U
#define XHCI_CRCR              0x18U
#define XHCI_DCBAAP            0x30U
#define XHCI_CONFIG             0x38U
#define XHCI_PORTSC_BASE       0x400U
#define XHCI_PORTSC_STRIDE      0x10U
#define XHCI_RT_IMAN            0x20U
#define XHCI_RT_IMOD            0x24U
#define XHCI_RT_ERSTSZ         0x28U
#define XHCI_RT_ERSTBA         0x30U
#define XHCI_RT_ERDP           0x38U

#define XHCI_CMD_RUN            (1U << 0)
#define XHCI_CMD_RESET          (1U << 1)
#define XHCI_CMD_INTE           (1U << 2)
#define XHCI_STS_HCH            (1U << 0)
#define XHCI_STS_EINT           (1U << 3)
#define XHCI_STS_CNR            (1U << 11)
#define XHCI_PORT_CCS           (1U << 0)
#define XHCI_PORT_PED           (1U << 1)
#define XHCI_PORT_OCA           (1U << 3)
#define XHCI_PORT_PR            (1U << 4)
#define XHCI_PORT_PLS_MASK      (0xFU << 5)
#define XHCI_PORT_PP            (1U << 9)
#define XHCI_PORT_SPEED_MASK    (0xFU << 10)
#define XHCI_PORT_CSC           (1U << 17)
#define XHCI_PORT_PEC           (1U << 18)
#define XHCI_PORT_WRC           (1U << 19)
#define XHCI_PORT_OCC           (1U << 20)
#define XHCI_PORT_PRC           (1U << 21)
#define XHCI_PORT_PLC           (1U << 22)
#define XHCI_PORT_CEC           (1U << 23)
#define XHCI_PORT_WAKE_BITS     (7U << 25)
#define XHCI_PORT_CHANGE_BITS   (XHCI_PORT_CSC | XHCI_PORT_PEC | \
                                  XHCI_PORT_WRC | XHCI_PORT_OCC | \
                                  XHCI_PORT_PRC | XHCI_PORT_PLC | \
                                  XHCI_PORT_CEC)
#define XHCI_PORT_RO_BITS       (XHCI_PORT_CCS | XHCI_PORT_OCA | \
                                  XHCI_PORT_SPEED_MASK)
#define XHCI_PORT_RWS_BITS      (XHCI_PORT_PLS_MASK | XHCI_PORT_PP | \
                                  XHCI_PORT_WAKE_BITS)
#define XHCI_IMAN_IP             (1U << 0)
#define XHCI_IMAN_IE             (1U << 1)
#define XHCI_LEGACY_BIOS_OWNED   (1U << 16U)
#define XHCI_LEGACY_OS_OWNED     (1U << 24U)
#define XHCI_LEGACY_SMI_ENABLES  ((1U << 0U) | (1U << 4U) | (7U << 13U))
#define XHCI_LEGACY_SMI_PRESERVE ((7U << 1U) | (0xFFU << 5U) | (7U << 17U))
#define XHCI_LEGACY_SMI_EVENTS   (7U << 29U)

#define TRB_CYCLE                (1U << 0)
#define TRB_ENT                  (1U << 1)
#define TRB_ISP                  (1U << 2)
#define TRB_IOC                  (1U << 5)
#define TRB_IDT                  (1U << 6)
#define TRB_TYPE(type)           ((uint32_t)(type) << 10)
#define TRB_DIR_IN               (1U << 16)
#define TRB_TRT_OUT              (2U << 16)
#define TRB_TRT_IN               (3U << 16)
#define TRB_LINK                 6U
#define TRB_TRANSFER             1U
#define TRB_SETUP                2U
#define TRB_DATA                 3U
#define TRB_STATUS               4U
#define TRB_ENABLE_SLOT          9U
#define TRB_DISABLE_SLOT         10U
#define TRB_ADDRESS_DEVICE       11U
#define TRB_CONFIGURE_ENDPOINT   12U
#define TRB_EVALUATE_CONTEXT     13U
#define TRB_EVENT_TRANSFER       32U
#define TRB_EVENT_COMMAND        33U
#define TRB_EVENT_PORT           34U
#define XHCI_COMPLETION_SUCCESS  1U
#define XHCI_COMPLETION_SHORT_PACKET 13U
#define XHCI_HID_KEYBOARD_MASK  (1U << 1U)
#define XHCI_HID_MOUSE_MASK     (1U << 2U)

typedef struct {
    uint32_t d[4];
} xhci_trb_t;

typedef struct {
    uint64_t ring_segment;
    uint32_t ring_size;
    uint32_t reserved;
} xhci_erst_entry_t;

typedef struct {
    uint32_t root_port;
    uint32_t generation;
    uint32_t endpoint_index;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint8_t interface_number;
    uint8_t configuration_value;
    uint8_t hid_protocol;
    uint8_t report_size;
    uint8_t port_speed;
    uint16_t max_packet;
    bool endpoint_cycle;
    bool online;
} xhci_hid_device_t;

typedef struct {
    volatile uint32_t *mmio;
    uint32_t op_base;
    uint32_t runtime_base;
    uint32_t doorbell_base;
    uint32_t context_size;
    uint32_t port_count;
    uint32_t scratchpad_count;
    uint8_t irq;
    uint32_t command_index;
    uint32_t event_index;
    uint32_t start_attempts;
    bool command_cycle;
    bool event_cycle;
    bool port_change_pending;
    bool runtime_published;
} xhci_state_t;

typedef struct {
    uint32_t request_type;
    uint32_t request;
    uint32_t value;
    uint32_t index;
    uint32_t length;
    uint32_t completion;
    uint32_t residual;
    uint32_t event_stage;
    uint32_t flags;
} xhci_control_diagnostic_t;

/* These objects are never handed to hardware before xHCI validation. */
static uint64_t dcbaa[33] __attribute__((aligned(4096)));
static uint64_t scratchpad_array[XHCI_MAX_SCRATCHPADS]
    __attribute__((aligned(64)));
static uint8_t scratchpad_pages[XHCI_MAX_SCRATCHPADS][4096]
    __attribute__((aligned(4096)));
static xhci_trb_t command_ring[XHCI_COMMAND_RING_TRBS]
    __attribute__((aligned(4096)));
static xhci_trb_t event_ring[XHCI_EVENT_RING_TRBS]
    __attribute__((aligned(4096)));
static xhci_erst_entry_t erst[1] __attribute__((aligned(64)));
static xhci_trb_t endpoint0_rings[XHCI_MAX_HID_DEVICES]
                                      [XHCI_ENDPOINT_RING_TRBS]
    __attribute__((aligned(4096)));
static xhci_trb_t interrupt_rings[XHCI_MAX_HID_DEVICES]
                                      [XHCI_ENDPOINT_RING_TRBS]
    __attribute__((aligned(4096)));
static uint8_t input_context[33 * XHCI_CONTEXT_BYTES]
    __attribute__((aligned(4096)));
static uint8_t device_contexts[XHCI_MAX_HID_DEVICES]
                              [32 * XHCI_CONTEXT_BYTES]
    __attribute__((aligned(4096)));
static uint8_t control_buffer[XHCI_CONTROL_BYTES]
    __attribute__((aligned(64)));
static uint8_t hid_reports[XHCI_MAX_HID_DEVICES]
                          [XHCI_ENDPOINT_RING_TRBS][8]
    __attribute__((aligned(64)));
static xhci_hid_device_t hid_devices[XHCI_MAX_HID_DEVICES];
static xhci_state_t controller;
/* Published event-consumer and diagnostics transaction. Enumeration remains
 * BSP-only and completes before runtime_published becomes visible. */
static spinlock_t xhci_runtime_lock = SPINLOCK_INIT;
static xhci_diagnostics_t diagnostics = {
    .version = XHCI_DIAGNOSTICS_VERSION,
    .struct_size = sizeof(xhci_diagnostics_t),
    .state = XHCI_DIAG_NOT_PROBED
};

_Static_assert(offsetof(xhci_diagnostics_t, control_request_type) == 188U,
               "xHCI diagnostics v5 prefix changed");

static xhci_control_diagnostic_t xhci_control_diagnostics_snapshot(void) {
    xhci_control_diagnostic_t snapshot = {
        .request_type = diagnostics.control_request_type,
        .request = diagnostics.control_request,
        .value = diagnostics.control_value,
        .index = diagnostics.control_index,
        .length = diagnostics.control_length,
        .completion = diagnostics.control_completion,
        .residual = diagnostics.control_residual,
        .event_stage = diagnostics.control_event_stage,
        .flags = diagnostics.control_flags
    };
    return snapshot;
}

static void xhci_control_diagnostics_restore(
        const xhci_control_diagnostic_t *snapshot) {
    if (snapshot == NULL) return;
    diagnostics.control_request_type = snapshot->request_type;
    diagnostics.control_request = snapshot->request;
    diagnostics.control_value = snapshot->value;
    diagnostics.control_index = snapshot->index;
    diagnostics.control_length = snapshot->length;
    diagnostics.control_completion = snapshot->completion;
    diagnostics.control_residual = snapshot->residual;
    diagnostics.control_event_stage = snapshot->event_stage;
    diagnostics.control_flags = snapshot->flags;
}

static xhci_control_diagnostic_t xhci_control_diagnostics_begin(
        uint8_t request_type, uint8_t request, uint16_t value,
        uint16_t index, uint16_t length) {
    xhci_control_diagnostic_t previous =
        xhci_control_diagnostics_snapshot();
    diagnostics.control_request_type = request_type;
    diagnostics.control_request = request;
    diagnostics.control_value = value;
    diagnostics.control_index = index;
    diagnostics.control_length = length;
    diagnostics.control_completion = 0U;
    diagnostics.control_residual = 0U;
    diagnostics.control_event_stage = XHCI_CONTROL_EVENT_NONE;
    diagnostics.control_flags = 0U;
    return previous;
}

static uint32_t xhci_dma32(const void *address) {
    uintptr_t value = (uintptr_t)address;
    if (value > UINT32_MAX) return 0U;
    return (uint32_t)value;
}

static bool xhci_dma_valid(const void *address, uint32_t alignment) {
    uint32_t value = xhci_dma32(address);
    return value != 0U && (value & (alignment - 1U)) == 0U;
}

static uint32_t xhci_hid_index(const xhci_hid_device_t *hid) {
    return (uint32_t)(hid - &hid_devices[0]);
}

static xhci_trb_t *xhci_endpoint0_ring(xhci_hid_device_t *hid) {
    return endpoint0_rings[xhci_hid_index(hid)];
}

static xhci_trb_t *xhci_interrupt_ring(xhci_hid_device_t *hid) {
    return interrupt_rings[xhci_hid_index(hid)];
}

static uint8_t *xhci_device_context(xhci_hid_device_t *hid) {
    return device_contexts[xhci_hid_index(hid)];
}

static uint8_t (*xhci_hid_report_buffers(xhci_hid_device_t *hid))[8] {
    return hid_reports[xhci_hid_index(hid)];
}

static bool xhci_hid_dma_valid(void) {
    for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index) {
        if (!xhci_dma_valid(endpoint0_rings[index], 64U) ||
            !xhci_dma_valid(interrupt_rings[index], 64U) ||
            !xhci_dma_valid(device_contexts[index], 64U) ||
            !xhci_dma_valid(hid_reports[index], 64U))
            return false;
    }
    return true;
}

static uint32_t xhci_scratchpad_count(uint32_t hcs2) {
    /* xHCI HCSPARAMS2 stores bits 9:5 in 25:21 and bits 4:0 in
     * 31:27. Keeping the halves in their specified order matters on Intel
     * controllers: swapping them turns four scratchpads into 128. */
    uint32_t high = (hcs2 >> 21U) & 0x1FU;
    uint32_t low = (hcs2 >> 27U) & 0x1FU;
    return (high << 5U) | low;
}

static uint32_t xhci_read(uint32_t offset) {
    return controller.mmio[offset >> 2U];
}

static void xhci_write(uint32_t offset, uint32_t value) {
    controller.mmio[offset >> 2U] = value;
}

static void xhci_write64(uint32_t offset, uint64_t value) {
    xhci_write(offset, (uint32_t)value);
    xhci_write(offset + 4U, (uint32_t)(value >> 32U));
}

/* x86 keeps coherent DMA stores ordered; the compiler must still not move
 * ring writes across the ownership bit or an MMIO doorbell access. */
static void xhci_dma_write_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

static void xhci_dma_read_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

static bool xhci_wait_until(uint32_t offset, uint32_t mask, uint32_t value,
                            uint32_t timeout_ms) {
    uint64_t start = pit_monotonic_ms();
    for (uint32_t poll = 0U; poll < XHCI_POLL_LIMIT; ++poll) {
        if ((xhci_read(offset) & mask) == value) return true;
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= timeout_ms) return false;
        if ((poll & 0x3FFU) == 0U) __asm__ __volatile__("pause");
    }
    return false;
}

static bool xhci_legacy_handoff(void) {
    uint32_t hcc = xhci_read(0x10U);
    uint32_t extended = (hcc >> 16U) << 2U;
    uint32_t visited = 0U;
    while (extended != 0U && visited++ < 32U) {
        if (extended > XHCI_MMIO_SIZE - 8U) return false;
        uint32_t header = xhci_read(extended);
        if (header == UINT32_MAX) return false;
        uint8_t id = (uint8_t)header;
        uint8_t next = (uint8_t)(header >> 8U);
        if (id == 1U) {
            /* The BIOS/OS ownership semaphores are in the capability header;
             * the following dword is the legacy SMI control/status register. */
            if ((header & XHCI_LEGACY_BIOS_OWNED) != 0U) {
                xhci_write(extended, header | XHCI_LEGACY_OS_OWNED);
                if (!xhci_wait_until(extended, XHCI_LEGACY_BIOS_OWNED, 0U,
                                     XHCI_TIMEOUT_MS)) return false;
            }
            uint32_t smi_before = xhci_read(extended + 4U);
            uint32_t smi_disable =
                (smi_before & XHCI_LEGACY_SMI_PRESERVE) |
                XHCI_LEGACY_SMI_EVENTS;
            xhci_write(extended + 4U, smi_disable);
            uint32_t smi_after = xhci_read(extended + 4U);
            printf("USB: xHCI legacy SMI before=%08X after=%08X\n",
                   (unsigned)smi_before, (unsigned)smi_after);
            return (smi_after & XHCI_LEGACY_SMI_ENABLES) == 0U;
        }
        if (next == 0U) return true;
        uint32_t increment = (uint32_t)next << 2U;
        if (increment > UINT32_MAX - extended) return false;
        extended += increment;
    }
    return extended == 0U;
}

static bool xhci_has_intel_ehci(void) {
    for (size_t index = 0U; index < pci_device_count; ++index) {
        const pci_device_t *candidate = &pci_devices[index];
        if (candidate->vendor_id == XHCI_INTEL_VENDOR_ID &&
            candidate->class_code == 0x0CU &&
            candidate->subclass_code == 0x03U &&
            candidate->prog_if == 0x20U) return true;
    }
    return false;
}

static void xhci_pci_write32(const pci_device_t *dev, uint8_t offset,
                             uint32_t value) {
    pci_write(dev->bus, dev->slot, dev->function, offset, 4U, value);
}

static bool xhci_route_intel_ports(const pci_device_t *dev) {
    if (dev->vendor_id != XHCI_INTEL_VENDOR_ID || !xhci_has_intel_ehci())
        return true;

    diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_EHCI_FOUND;
    uint32_t subsystem = pci_read_config_dword(dev->bus, dev->slot,
                                               dev->function,
                                               XHCI_PCI_SUBSYSTEM_IDS);
    if ((subsystem & 0xFFFFU) == XHCI_SONY_VENDOR_ID &&
        (subsystem >> 16U) == XHCI_SONY_VAIO_SUBDEVICE) {
        diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_SKIPPED_QUIRK;
        return true;
    }

    uint32_t original_usb2 = pci_read_config_dword(
        dev->bus, dev->slot, dev->function, XHCI_INTEL_XUSB2PR);
    uint32_t original_usb3 = pci_read_config_dword(
        dev->bus, dev->slot, dev->function, XHCI_INTEL_USB3_PSSEN);
    uint32_t usb2_mask = pci_read_config_dword(
        dev->bus, dev->slot, dev->function, XHCI_INTEL_USB2PRM);
    uint32_t usb3_mask = pci_read_config_dword(
        dev->bus, dev->slot, dev->function, XHCI_INTEL_USB3PRM);
    diagnostics.usb2_routing_mask = usb2_mask;
    diagnostics.usb2_routing = original_usb2;
    diagnostics.usb3_routing_mask = usb3_mask;
    diagnostics.usb3_routing = original_usb3;
    if (original_usb2 == UINT32_MAX || original_usb3 == UINT32_MAX ||
        usb2_mask == UINT32_MAX || usb3_mask == UINT32_MAX) {
        diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_INVALID_CONFIG;
        return false;
    }
    if ((usb2_mask | usb3_mask) == 0U) {
        diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_NO_MASK;
        return true;
    }

    diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_ATTEMPTED;
    /* Enable SuperSpeed terminations before moving the USB2 data wires. */
    xhci_pci_write32(dev, XHCI_INTEL_USB3_PSSEN, usb3_mask);
    uint32_t usb3_routing = pci_read_config_dword(
        dev->bus, dev->slot, dev->function, XHCI_INTEL_USB3_PSSEN);
    diagnostics.usb3_routing = usb3_routing;
    if ((usb3_routing & usb3_mask) != usb3_mask) {
        diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_VERIFY_FAILED;
        xhci_pci_write32(dev, XHCI_INTEL_USB3_PSSEN, original_usb3);
        return false;
    }
    diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_USB3_VERIFIED;

    xhci_pci_write32(dev, XHCI_INTEL_XUSB2PR, usb2_mask);
    uint32_t usb2_routing = pci_read_config_dword(
        dev->bus, dev->slot, dev->function, XHCI_INTEL_XUSB2PR);
    diagnostics.usb2_routing = usb2_routing;
    if ((usb2_routing & usb2_mask) != usb2_mask) {
        diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_VERIFY_FAILED;
        xhci_pci_write32(dev, XHCI_INTEL_XUSB2PR, original_usb2);
        xhci_pci_write32(dev, XHCI_INTEL_USB3_PSSEN, original_usb3);
        return false;
    }
    diagnostics.intel_routing_flags |= XHCI_INTEL_ROUTE_USB2_VERIFIED;
    return true;
}

static bool xhci_reset_controller(bool trace) {
    uint32_t command = xhci_read(controller.op_base + XHCI_USBCMD);
    if (trace)
        printf("USB: xHCI reset stage=halt-request cmd=%08X sts=%08X\n",
               (unsigned)command,
               (unsigned)xhci_read(controller.op_base + XHCI_USBSTS));
    xhci_write(controller.op_base + XHCI_USBCMD, command & ~XHCI_CMD_RUN);
    if (!xhci_wait_until(controller.op_base + XHCI_USBSTS, XHCI_STS_HCH,
                         XHCI_STS_HCH, XHCI_TIMEOUT_MS)) {
        if (trace)
            printf("USB: xHCI reset stage=halt-timeout sts=%08X\n",
                   (unsigned)xhci_read(controller.op_base + XHCI_USBSTS));
        return false;
    }
    if (trace) printf("USB: xHCI reset stage=host-reset-request\n");
    xhci_write(controller.op_base + XHCI_USBCMD, XHCI_CMD_RESET);
    if (!xhci_wait_until(controller.op_base + XHCI_USBCMD, XHCI_CMD_RESET,
                         0U, XHCI_TIMEOUT_MS)) {
        if (trace)
            printf("USB: xHCI reset stage=host-reset-timeout cmd=%08X\n",
                   (unsigned)xhci_read(controller.op_base + XHCI_USBCMD));
        return false;
    }
    if (trace) printf("USB: xHCI reset stage=controller-ready-wait\n");
    bool ready = xhci_wait_until(controller.op_base + XHCI_USBSTS,
                                 XHCI_STS_CNR, 0U, XHCI_TIMEOUT_MS);
    if (trace)
        printf("USB: xHCI reset stage=%s\n",
               ready ? "complete" : "controller-ready-timeout");
    return ready;
}

static void xhci_prepare_rings(void) {
    memset(dcbaa, 0, sizeof(dcbaa));
    memset(command_ring, 0, sizeof(command_ring));
    memset(event_ring, 0, sizeof(event_ring));
    memset(endpoint0_rings, 0, sizeof(endpoint0_rings));
    memset(interrupt_rings, 0, sizeof(interrupt_rings));
    memset(input_context, 0, sizeof(input_context));
    memset(device_contexts, 0, sizeof(device_contexts));
    memset(hid_reports, 0, sizeof(hid_reports));
    for (uint32_t index = 0U; index < XHCI_MAX_SCRATCHPADS; ++index)
        scratchpad_array[index] = xhci_dma32(scratchpad_pages[index]);
    command_ring[XHCI_COMMAND_RING_TRBS - 1U].d[0] = xhci_dma32(command_ring);
    command_ring[XHCI_COMMAND_RING_TRBS - 1U].d[3] =
        TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
    for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index) {
        endpoint0_rings[index][XHCI_ENDPOINT_RING_TRBS - 1U].d[0] =
            xhci_dma32(endpoint0_rings[index]);
        endpoint0_rings[index][XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
            TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
        interrupt_rings[index][XHCI_ENDPOINT_RING_TRBS - 1U].d[0] =
            xhci_dma32(interrupt_rings[index]);
        interrupt_rings[index][XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
            TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
        hid_devices[index].endpoint_index = 0U;
        hid_devices[index].endpoint_cycle = true;
    }
    erst[0].ring_segment = xhci_dma32(event_ring);
    erst[0].ring_size = XHCI_EVENT_RING_TRBS;
    erst[0].reserved = 0U;
    dcbaa[0] = controller.scratchpad_count == 0U
        ? 0U : xhci_dma32(scratchpad_array);
    if (xhci_dma_valid(dcbaa, 64U) && xhci_dma_valid(command_ring, 64U))
        xhci_write64(controller.op_base + XHCI_DCBAAP, xhci_dma32(dcbaa));
    xhci_write64(controller.op_base + XHCI_CRCR,
                 (uint64_t)xhci_dma32(command_ring) | 1U);
    uint32_t runtime = controller.runtime_base;
    xhci_write(runtime + XHCI_RT_ERSTSZ, 1U);
    xhci_write64(runtime + XHCI_RT_ERSTBA, xhci_dma32(erst));
    xhci_write64(runtime + XHCI_RT_ERDP, xhci_dma32(event_ring));
    xhci_write(runtime + XHCI_RT_IMOD, 0U);
    xhci_write(runtime + XHCI_RT_IMAN, XHCI_IMAN_IE);
    controller.command_index = 0U;
    controller.event_index = 0U;
    controller.command_cycle = true;
    controller.event_cycle = true;
}

static void xhci_ring_doorbell(uint8_t slot, uint8_t endpoint) {
    uint32_t offset = controller.doorbell_base + (uint32_t)slot * 4U;
    xhci_dma_write_barrier();
    xhci_write(offset, endpoint);
    (void)xhci_read(offset);
}

static void xhci_queue_interrupt_report(xhci_hid_device_t *hid) {
    xhci_trb_t *ring = xhci_interrupt_ring(hid);
    uint8_t (*reports)[8] = xhci_hid_report_buffers(hid);
    uint32_t index = hid->endpoint_index;
    xhci_trb_t *trb = &ring[index];
    memset(trb, 0, sizeof(*trb));
    trb->d[0] = xhci_dma32(reports[index]);
    trb->d[2] = hid->report_size;
    trb->d[3] = TRB_TYPE(TRB_TRANSFER) | TRB_ISP | TRB_IOC |
        (hid->endpoint_cycle ? TRB_CYCLE : 0U);
    hid->endpoint_index++;
    if (hid->endpoint_index >= XHCI_ENDPOINT_RING_TRBS - 1U) {
        ring[XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
            TRB_TYPE(TRB_LINK) | TRB_ENT |
            (hid->endpoint_cycle ? TRB_CYCLE : 0U);
        hid->endpoint_index = 0U;
        hid->endpoint_cycle = !hid->endpoint_cycle;
    }
}

static xhci_trb_t *xhci_command(uint32_t type, uint32_t d0, uint32_t d1,
                                uint32_t control) {
    uint32_t index = controller.command_index;
    xhci_trb_t *command = &command_ring[index];
    memset(command, 0, sizeof(*command));
    command->d[0] = d0;
    command->d[1] = d1;
    command->d[2] = 0U;
    command->d[3] = TRB_TYPE(type) | control |
        (controller.command_cycle ? TRB_CYCLE : 0U);
    controller.command_index++;
    if (controller.command_index >= XHCI_COMMAND_RING_TRBS - 1U) {
        controller.command_index = 0U;
        controller.command_cycle = !controller.command_cycle;
    }
    xhci_ring_doorbell(0U, 0U);
    return command;
}

static bool xhci_any_hid_online(void) {
    for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index)
        if (hid_devices[index].online) return true;
    return false;
}

static xhci_hid_device_t *xhci_hid_for_transfer(uint32_t pointer,
                                                 uint32_t *index_out) {
    for (uint32_t device_index = 0U;
         device_index < XHCI_MAX_HID_DEVICES; ++device_index) {
        xhci_hid_device_t *hid = &hid_devices[device_index];
        if (!hid->online) continue;
        xhci_trb_t *ring = xhci_interrupt_ring(hid);
        uint32_t first = xhci_dma32(&ring[0]);
        uint32_t last = xhci_dma32(&ring[XHCI_ENDPOINT_RING_TRBS - 2U]);
        if (pointer < first || pointer > last ||
            ((pointer - first) % sizeof(ring[0])) != 0U) continue;
        if (index_out != NULL)
            *index_out = (pointer - first) / sizeof(ring[0]);
        return hid;
    }
    return NULL;
}

static xhci_hid_device_t *xhci_hid_by_protocol(uint8_t protocol) {
    for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index) {
        if (hid_devices[index].online &&
            hid_devices[index].hid_protocol == protocol)
            return &hid_devices[index];
    }
    return NULL;
}

static xhci_hid_device_t *xhci_free_hid_resource(void) {
    for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index) {
        if (!hid_devices[index].online && hid_devices[index].slot_id == 0U)
            return &hid_devices[index];
    }
    return NULL;
}

static void xhci_refresh_ready_state(void) {
    xhci_hid_device_t *keyboard = xhci_hid_by_protocol(1U);
    xhci_hid_device_t *mouse = xhci_hid_by_protocol(2U);
    xhci_hid_device_t *selected = mouse != NULL ? mouse : keyboard;
    diagnostics.selected_port = selected != NULL ? selected->root_port : 0U;
    diagnostics.hid_protocol = selected != NULL ? selected->hid_protocol : 0U;
    diagnostics.endpoint_id = selected != NULL ? selected->endpoint_id : 0U;
    diagnostics.report_size = selected != NULL ? selected->report_size : 0U;
    diagnostics.keyboard_port = keyboard != NULL ? keyboard->root_port : 0U;
    diagnostics.keyboard_slot = keyboard != NULL ? keyboard->slot_id : 0U;
    diagnostics.keyboard_endpoint = keyboard != NULL
        ? keyboard->endpoint_id : 0U;
    diagnostics.mouse_port = mouse != NULL ? mouse->root_port : 0U;
    diagnostics.mouse_slot = mouse != NULL ? mouse->slot_id : 0U;
    diagnostics.mouse_endpoint = mouse != NULL ? mouse->endpoint_id : 0U;
    if (keyboard != NULL && mouse != NULL)
        diagnostics.state = XHCI_DIAG_KEYBOARD_MOUSE_READY;
    else if (mouse != NULL)
        diagnostics.state = XHCI_DIAG_MOUSE_READY;
    else if (keyboard != NULL)
        diagnostics.state = XHCI_DIAG_KEYBOARD_READY;
    else
        diagnostics.state = XHCI_DIAG_DISCONNECTED;
}

static void xhci_detach_hid(xhci_hid_device_t *hid) {
    if (hid == NULL || !hid->online) return;
    if (hid->hid_protocol == 1U)
        hid_keyboard_detach(hid->generation);
    else if (hid->hid_protocol == 2U)
        hid_mouse_detach(hid->generation);
    hid->online = false;
    xhci_refresh_ready_state();
}

static bool xhci_drain_events(uint32_t limit, uint32_t expected,
                              uint32_t alternate, uint8_t *slot_out,
                              uint32_t *status_out, uint32_t *residual_out,
                              uint32_t *pointer_out) {
    bool found = false;
    for (uint32_t count = 0U; count < limit; ++count) {
        xhci_trb_t *event = &event_ring[controller.event_index];
        xhci_dma_read_barrier();
        if ((event->d[3] & 1U) != (controller.event_cycle ? 1U : 0U)) break;
        xhci_dma_read_barrier();
        uint32_t type = (event->d[3] >> 10U) & 0x3FU;
        uint32_t pointer = event->d[0];
        uint32_t status = event->d[2];
        if (type == TRB_EVENT_COMMAND && pointer == expected) {
            if (slot_out != NULL) *slot_out = (uint8_t)(event->d[3] >> 24U);
            if (status_out != NULL) *status_out = (status >> 24U) & 0xFFU;
            if (residual_out != NULL) *residual_out = 0U;
            if (pointer_out != NULL) *pointer_out = pointer;
            found = true;
        } else if (type == TRB_EVENT_TRANSFER) {
            if (pointer == expected ||
                (alternate != 0U && pointer == alternate)) {
                if (status_out != NULL) *status_out = (status >> 24U) & 0xFFU;
                if (residual_out != NULL)
                    *residual_out = status & 0x00FFFFFFU;
                if (pointer_out != NULL) *pointer_out = pointer;
                found = true;
            }
            uint32_t completion = (status >> 24U) & 0xFFU;
            uint32_t index = 0U;
            xhci_hid_device_t *hid = xhci_hid_for_transfer(pointer, &index);
            if (hid != NULL &&
                (completion == XHCI_COMPLETION_SUCCESS ||
                 completion == XHCI_COMPLETION_SHORT_PACKET)) {
                uint32_t residual = status & 0x00FFFFFFU;
                size_t actual = residual <= hid->report_size
                    ? hid->report_size - residual : 0U;
                uint8_t (*reports)[8] = xhci_hid_report_buffers(hid);
                diagnostics.transfer_events++;
                diagnostics.last_completion = completion;
                diagnostics.last_actual_length = (uint32_t)actual;
                if (hid->hid_protocol == 1U) {
                    if (hid_keyboard_report(hid->generation,
                                            reports[index], actual))
                        diagnostics.keyboard_reports++;
                    else
                        diagnostics.rejected_keyboard_reports++;
                } else if (hid->hid_protocol == 2U) {
                    if (hid_mouse_report(hid->generation,
                                         reports[index], actual))
                        diagnostics.mouse_reports++;
                    else
                        diagnostics.rejected_mouse_reports++;
                }
                xhci_queue_interrupt_report(hid);
                xhci_ring_doorbell(hid->slot_id, hid->endpoint_id);
            }
        } else if (type == TRB_EVENT_PORT) {
            controller.port_change_pending = true;
            uint32_t changed_port = event->d[0] >> 24U;
            for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index) {
                xhci_hid_device_t *hid = &hid_devices[index];
                if (!hid->online || (changed_port != 0U &&
                    hid->root_port != changed_port)) continue;
                uint32_t port_offset = controller.op_base + XHCI_PORTSC_BASE +
                    (hid->root_port - 1U) * XHCI_PORTSC_STRIDE;
                if ((xhci_read(port_offset) & XHCI_PORT_CCS) == 0U)
                    xhci_detach_hid(hid);
            }
        }
        /* Do not clear consumed event TRBs.  Their old cycle bit must remain
         * visible so that, after the consumer-cycle toggle at ring wrap, an
         * entry is not mistaken for a newly produced event before hardware
         * has written it with the new producer cycle. */
        controller.event_index++;
        if (controller.event_index >= XHCI_EVENT_RING_TRBS) {
            controller.event_index = 0U;
            controller.event_cycle = !controller.event_cycle;
        }
        if (found) break;
    }
    xhci_write64(controller.runtime_base + XHCI_RT_ERDP,
                 (uint64_t)xhci_dma32(&event_ring[controller.event_index]) | 8U);
    return found;
}

static bool xhci_wait_command(xhci_trb_t *command, uint8_t *slot_out) {
    uint64_t start = pit_monotonic_ms();
    uint32_t status = 0U;
    while (1) {
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= XHCI_TIMEOUT_MS) break;
        if (xhci_drain_events(32U, xhci_dma32(command), 0U, slot_out,
                              &status, NULL, NULL)) {
            diagnostics.last_completion = status;
            return status == XHCI_COMPLETION_SUCCESS;
        }
    }
    diagnostics.last_completion = 0U;
    return false;
}

static bool xhci_wait_control_transfer(xhci_trb_t *data_trb,
                                       xhci_trb_t *status_trb,
                                       uint32_t requested_length,
                                       bool direction_in,
                                       uint32_t *completion_out) {
    if (status_trb == NULL ||
        (requested_length != 0U && data_trb == NULL))
        return false;
    uint64_t start = pit_monotonic_ms();
    uint32_t data_pointer = data_trb != NULL ? xhci_dma32(data_trb) : 0U;
    uint32_t status_pointer = xhci_dma32(status_trb);
    bool saw_data_short = false;
    uint32_t actual = requested_length;
    if (completion_out != NULL) *completion_out = 0U;
    for (uint32_t poll = 0U; poll < XHCI_POLL_LIMIT; ++poll) {
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= XHCI_TIMEOUT_MS) break;
        uint32_t completion = 0U;
        uint32_t residual = 0U;
        uint32_t pointer = 0U;
        if (!xhci_drain_events(32U, status_pointer, data_pointer, NULL,
                               &completion, &residual, &pointer)) {
            if ((poll & 0x3FFU) == 0U) __asm__ __volatile__("pause");
            continue;
        }
        diagnostics.last_completion = completion;
        diagnostics.control_completion = completion;
        diagnostics.control_residual = residual;
        if (completion_out != NULL) *completion_out = completion;

        if (pointer == data_pointer && data_pointer != 0U) {
            diagnostics.control_event_stage = XHCI_CONTROL_EVENT_DATA;
            diagnostics.control_flags |= XHCI_CONTROL_DATA_EVENT;
            if (!direction_in ||
                completion != XHCI_COMPLETION_SHORT_PACKET ||
                residual > requested_length) {
                diagnostics.last_actual_length = 0U;
                return false;
            }
            actual = requested_length - residual;
            diagnostics.last_actual_length = actual;
            saw_data_short = true;
            continue;
        }

        if (pointer == status_pointer) {
            diagnostics.control_event_stage = XHCI_CONTROL_EVENT_STATUS;
            diagnostics.control_flags |= XHCI_CONTROL_STATUS_EVENT;
            if (completion == XHCI_COMPLETION_SUCCESS) {
                xhci_dma_read_barrier();
                diagnostics.last_actual_length = actual;
                if (saw_data_short) {
                    diagnostics.last_completion =
                        XHCI_COMPLETION_SHORT_PACKET;
                    diagnostics.control_completion =
                        XHCI_COMPLETION_SHORT_PACKET;
                    diagnostics.control_residual = requested_length - actual;
                    diagnostics.control_event_stage = XHCI_CONTROL_EVENT_DATA;
                    if (completion_out != NULL)
                        *completion_out = XHCI_COMPLETION_SHORT_PACKET;
                    return false;
                }
                return actual == requested_length;
            }
            /* xHCI 1.2 permits software to treat a short event as TD-complete
             * only when its pointer references the final TRB.  For a
             * host-to-device request with wLength=0 that final TRB is the
             * zero-length Status Stage itself; no data was requested and a
             * zero residue proves that the terminal event is complete. */
            if (!direction_in && requested_length == 0U &&
                data_pointer == 0U &&
                completion == XHCI_COMPLETION_SHORT_PACKET &&
                residual == 0U) {
                xhci_dma_read_barrier();
                diagnostics.last_actual_length = 0U;
                diagnostics.control_flags |= XHCI_CONTROL_SHORT_ACCEPTED;
                printf("USB: xHCI accepted terminal zero-length Status Stage short"
                       " type=%02X request=%u value=%04X index=%u\n",
                       (unsigned)diagnostics.control_request_type,
                       (unsigned)diagnostics.control_request,
                       (unsigned)diagnostics.control_value,
                       (unsigned)diagnostics.control_index);
                return true;
            }
            return false;
        }
    }
    diagnostics.last_completion = saw_data_short
        ? XHCI_COMPLETION_SHORT_PACKET : 0U;
    diagnostics.last_actual_length = saw_data_short ? actual : 0U;
    diagnostics.control_completion = diagnostics.last_completion;
    diagnostics.control_residual = saw_data_short
        ? requested_length - actual : requested_length;
    diagnostics.control_flags |= XHCI_CONTROL_TIMEOUT;
    if (completion_out != NULL)
        *completion_out = diagnostics.last_completion;
    return false;
}

static bool xhci_control(xhci_hid_device_t *hid,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         bool direction_in, uint8_t *buffer) {
    if (hid == NULL || length > XHCI_CONTROL_BYTES ||
        (length != 0U && buffer == NULL))
        return false;
    if (direction_in && length != 0U) memset(buffer, 0, length);
    xhci_trb_t *ring = xhci_endpoint0_ring(hid);
    uint32_t trb_count = length != 0U ? 3U : 2U;
    if (hid->endpoint_index + trb_count > XHCI_ENDPOINT_RING_TRBS - 1U)
        return false;
    xhci_trb_t *setup = &ring[hid->endpoint_index];
    xhci_trb_t *data_trb = length != 0U
        ? &ring[hid->endpoint_index + 1U] : NULL;
    xhci_trb_t *status = &ring[hid->endpoint_index + trb_count - 1U];
    xhci_control_diagnostic_t previous_control =
        xhci_control_diagnostics_begin(request_type, request, value, index,
                                       length);
    uint8_t setup_packet[8] = {request_type, request, (uint8_t)value,
                               (uint8_t)(value >> 8U), (uint8_t)index,
                               (uint8_t)(index >> 8U), (uint8_t)length,
                               (uint8_t)(length >> 8U)};
    memset(setup, 0, sizeof(*setup));
    memcpy(setup->d, setup_packet, sizeof(setup_packet));
    uint32_t transfer_type = length == 0U ? 0U :
                             (direction_in ? TRB_TRT_IN : TRB_TRT_OUT);
    uint32_t cycle = hid->endpoint_cycle ? TRB_CYCLE : 0U;
    uint32_t setup_control = TRB_TYPE(TRB_SETUP) | TRB_IDT |
        transfer_type | cycle;
    setup->d[2] = 8U;
    /* Keep the first TRB software-owned until the complete Control TD is
     * visible.  Publishing Setup last prevents an active EP0 ring from
     * fetching a partially initialized Data or Status Stage. */
    setup->d[3] = setup_control ^ TRB_CYCLE;
    if (length != 0U) {
        memset(data_trb, 0, sizeof(*data_trb));
        data_trb->d[0] = xhci_dma32(buffer);
        data_trb->d[2] = length;
        data_trb->d[3] = TRB_TYPE(TRB_DATA) |
            (direction_in ? TRB_DIR_IN | TRB_ISP : 0U) | cycle;
    }
    memset(status, 0, sizeof(*status));
    status->d[3] = TRB_TYPE(TRB_STATUS) | TRB_IOC |
        (direction_in ? 0U : TRB_DIR_IN) | cycle;
    xhci_dma_write_barrier();
    setup->d[3] = setup_control;
    xhci_ring_doorbell(hid->slot_id, 1U);
    uint32_t completion = 0U;
    bool result = xhci_wait_control_transfer(data_trb, status, length,
                                             direction_in, &completion);
    if (!result) {
        diagnostics.control_flags |= XHCI_CONTROL_FAILED;
        printf("USB: xHCI control failed type=%02X request=%u value=%04X"
               " index=%u length=%u cc=%u residual=%u stage=%u flags=%X\n",
               (unsigned)request_type, (unsigned)request, (unsigned)value,
               (unsigned)index, (unsigned)length, (unsigned)completion,
               (unsigned)diagnostics.control_residual,
               (unsigned)diagnostics.control_event_stage,
               (unsigned)diagnostics.control_flags);
    }
    hid->endpoint_index = (uint32_t)((status - ring) + 1U);
    if (hid->endpoint_index >= XHCI_ENDPOINT_RING_TRBS - 1U) {
        hid->endpoint_index = 0U;
        hid->endpoint_cycle = !hid->endpoint_cycle;
    }
    if (result && (diagnostics.control_flags &
                   XHCI_CONTROL_SHORT_ACCEPTED) == 0U)
        xhci_control_diagnostics_restore(&previous_control);
    return result;
}

static void xhci_wait_address_recovery(void) {
    pit_delay(XHCI_SET_ADDRESS_RECOVERY_MS);
}

static bool xhci_address_device(xhci_hid_device_t *hid, uint8_t port_speed) {
    if (hid == NULL) return false;
    xhci_trb_t *command = xhci_command(TRB_ENABLE_SLOT, 0U, 0U, 0U);
    uint8_t slot = 0U;
    if (!xhci_wait_command(command, &slot) || slot == 0U || slot > 31U)
        return false;
    hid->slot_id = slot;
    memset(input_context, 0, sizeof(input_context));
    uint8_t *device_context = xhci_device_context(hid);
    xhci_trb_t *endpoint0_ring = xhci_endpoint0_ring(hid);
    memset(device_context, 0, 32U * XHCI_CONTEXT_BYTES);
    dcbaa[slot] = xhci_dma32(device_context);
    uint32_t *control = (uint32_t *)input_context;
    uint32_t *slot_context = (uint32_t *)(input_context +
                                          controller.context_size);
    uint32_t *ep0 = (uint32_t *)(input_context +
                                  2U * controller.context_size);
    control[1] = (1U << 0U) | (1U << 1U);
    slot_context[0] = ((uint32_t)port_speed << 20U) | (1U << 27U);
    slot_context[1] = hid->root_port << 16U;
    ep0[1] = (3U << 1U) | (4U << 3U) |
             ((uint32_t)hid->max_packet << 16U);
    ep0[2] = xhci_dma32(endpoint0_ring) | 1U;
    ep0[3] = 0U;
    ep0[4] = 8U;
    xhci_trb_t *address = xhci_command(TRB_ADDRESS_DEVICE,
                                        xhci_dma32(input_context), 0U,
                                        (uint32_t)slot << 24U);
    uint8_t completed_slot = 0U;
    if (!xhci_wait_command(address, &completed_slot) ||
        completed_slot != slot) return false;
    /* USB 2.0 section 9.2.6.3 requires at least 2 ms recovery after the
     * SET_ADDRESS status stage.  xHCI performs that request inside Address
     * Device, so its successful command completion must precede this fixed
     * delay and the first EP0 descriptor doorbell. */
    xhci_wait_address_recovery();
    return true;
}

static bool xhci_ep0_packet_valid(uint8_t port_speed, uint16_t max_packet) {
    if (port_speed == 1U)
        return max_packet == 8U || max_packet == 16U ||
               max_packet == 32U || max_packet == 64U;
    if (port_speed == 2U) return max_packet == 8U;
    if (port_speed == 3U) return max_packet == 64U;
    if (port_speed >= 4U) return max_packet == 512U;
    return false;
}

static bool xhci_update_ep0_max_packet(xhci_hid_device_t *hid,
                                        uint16_t max_packet) {
    if (hid == NULL || hid->slot_id == 0U ||
        !xhci_ep0_packet_valid(hid->port_speed, max_packet))
        return false;
    if (max_packet == hid->max_packet) return true;

    uint8_t saved_ep0[XHCI_CONTEXT_BYTES];
    memset(saved_ep0, 0, sizeof(saved_ep0));
    memcpy(saved_ep0,
           xhci_device_context(hid) + controller.context_size,
           controller.context_size);
    memset(input_context, 0, sizeof(input_context));
    uint32_t *control = (uint32_t *)input_context;
    uint32_t *ep0 = (uint32_t *)(input_context +
                                  2U * controller.context_size);
    control[1] = (1U << 1U);
    memcpy(ep0, saved_ep0, controller.context_size);
    /* An input endpoint context must not carry the controller-owned state
     * from the output context.  Preserve the dequeue state and update only
     * Max Packet Size before asking xHCI to evaluate EP0. */
    ep0[0] &= ~0x7U;
    ep0[1] = (ep0[1] & ~(0xFFFFU << 16U)) |
             ((uint32_t)max_packet << 16U);
    xhci_trb_t *evaluate = xhci_command(TRB_EVALUATE_CONTEXT,
                                         xhci_dma32(input_context), 0U,
                                         (uint32_t)hid->slot_id << 24U);
    uint8_t completed_slot = 0U;
    if (!xhci_wait_command(evaluate, &completed_slot) ||
        completed_slot != hid->slot_id)
        return false;
    hid->max_packet = max_packet;
    return true;
}

static bool xhci_release_candidate(xhci_hid_device_t *hid) {
    if (hid == NULL || hid->online) return false;
    uint8_t slot = hid->slot_id;
    if (slot != 0U) {
        uint32_t failure_completion = diagnostics.last_completion;
        xhci_trb_t *disable = xhci_command(
            TRB_DISABLE_SLOT, 0U, 0U, (uint32_t)slot << 24U);
        if (!xhci_wait_command(disable, NULL)) return false;
        /* Successful cleanup must not hide the completion code of the
         * enumeration command or transfer that actually failed. */
        if (diagnostics.failure_stage != XHCI_FAILURE_NONE)
            diagnostics.last_completion = failure_completion;
        dcbaa[slot] = 0U;
    }
    uint32_t resource = xhci_hid_index(hid);
    memset(endpoint0_rings[resource], 0, sizeof(endpoint0_rings[resource]));
    memset(interrupt_rings[resource], 0, sizeof(interrupt_rings[resource]));
    memset(device_contexts[resource], 0, sizeof(device_contexts[resource]));
    memset(hid_reports[resource], 0, sizeof(hid_reports[resource]));
    endpoint0_rings[resource][XHCI_ENDPOINT_RING_TRBS - 1U].d[0] =
        xhci_dma32(endpoint0_rings[resource]);
    endpoint0_rings[resource][XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
        TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
    interrupt_rings[resource][XHCI_ENDPOINT_RING_TRBS - 1U].d[0] =
        xhci_dma32(interrupt_rings[resource]);
    interrupt_rings[resource][XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
        TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
    memset(hid, 0, sizeof(*hid));
    hid->endpoint_cycle = true;
    return true;
}

static bool xhci_find_boot_hid(uint8_t *config, uint16_t length,
                               uint32_t protocol_mask,
                               uint8_t *configuration, uint8_t *interface,
                               uint8_t *protocol, uint8_t *endpoint,
                               uint16_t *packet, uint8_t *interval) {
    uint16_t offset = 0U;
    bool boot_hid = false;
    bool keyboard_found = false;
    uint8_t configuration_value = 0U;
    uint8_t current_interface = 0U;
    uint8_t current_protocol = 0U;
    uint8_t keyboard_interface = 0U;
    uint8_t keyboard_endpoint = 0U;
    uint8_t keyboard_interval = 0U;
    uint16_t keyboard_packet = 0U;
    bool mouse_found = false;
    uint8_t mouse_interface = 0U;
    uint8_t mouse_endpoint = 0U;
    uint8_t mouse_interval = 0U;
    uint16_t mouse_packet = 0U;
    while (offset + 2U <= length) {
        uint8_t item_length = config[offset];
        uint8_t item_type = config[offset + 1U];
        if (item_length < 2U || offset + item_length > length) return false;
        if (item_type == 2U && item_length >= 6U)
            configuration_value = config[offset + 5U];
        if (item_type == 4U && item_length >= 9U) {
            boot_hid = config[offset + 5U] == 3U &&
                       config[offset + 6U] == 1U &&
                       (config[offset + 7U] == 1U ||
                        config[offset + 7U] == 2U);
            if (boot_hid) {
                current_interface = config[offset + 2U];
                current_protocol = config[offset + 7U];
            }
        } else if (item_type == 5U && boot_hid && item_length >= 7U &&
                   (config[offset + 2U] & 0x80U) != 0U &&
                   (config[offset + 3U] & 3U) == 3U) {
            uint16_t max_packet = (uint16_t)config[offset + 4U] |
                                  ((uint16_t)config[offset + 5U] << 8U);
            /* Boot protocol limits the consumed report to 8 bytes, but a
             * real interrupt endpoint may advertise a larger packet. */
            if (max_packet == 0U || max_packet > 64U) return false;
            uint8_t endpoint_number = config[offset + 2U] & 0x0FU;
            uint8_t endpoint_interval =
                config[offset + 6U] == 0U ? 1U : config[offset + 6U];
            if (current_protocol == 2U && !mouse_found &&
                (protocol_mask & XHCI_HID_MOUSE_MASK) != 0U) {
                mouse_found = true;
                mouse_interface = current_interface;
                mouse_endpoint = endpoint_number;
                mouse_packet = max_packet;
                mouse_interval = endpoint_interval;
            }
            if (current_protocol == 1U && !keyboard_found &&
                (protocol_mask & XHCI_HID_KEYBOARD_MASK) != 0U) {
                keyboard_found = true;
                keyboard_interface = current_interface;
                keyboard_endpoint = endpoint_number;
                keyboard_packet = max_packet;
                keyboard_interval = endpoint_interval;
            }
        }
        offset = (uint16_t)(offset + item_length);
    }
    if (configuration_value == 0U) return false;
    /* A composite mouse can expose a later boot-keyboard interface for
     * auxiliary buttons.  Select the lowest-numbered boot interface while
     * both fixed protocol resources are free, then let a separate root-port
     * candidate fill the remaining resource. */
    if (mouse_found && (!keyboard_found || mouse_interface < keyboard_interface)) {
        *configuration = configuration_value;
        *interface = mouse_interface;
        *protocol = 2U;
        *endpoint = mouse_endpoint;
        *packet = mouse_packet;
        *interval = mouse_interval;
        return true;
    }
    if (!keyboard_found) return false;
    *configuration = configuration_value;
    *interface = keyboard_interface;
    *protocol = 1U;
    *endpoint = keyboard_endpoint;
    *packet = keyboard_packet;
    *interval = keyboard_interval;
    return true;
}

static uint8_t xhci_periodic_interval(uint8_t port_speed,
                                      uint8_t descriptor_interval) {
    if (port_speed >= 3U) {
        uint8_t bounded = descriptor_interval;
        if (bounded < 1U) bounded = 1U;
        if (bounded > 16U) bounded = 16U;
        return (uint8_t)(bounded - 1U);
    }

    /* Full-/Low-Speed bInterval is expressed in 1-ms frames.  The xHCI
     * endpoint context uses an exponent of 125-us microframes, so encode
     * floor(log2(bInterval * 8)) and keep the range required for FS/LS. */
    uint32_t microframes = (uint32_t)descriptor_interval * 8U;
    uint8_t exponent = 0U;
    while (microframes > 1U && exponent < 15U) {
        microframes >>= 1U;
        exponent++;
    }
    if (exponent < 3U) exponent = 3U;
    if (exponent > 10U) exponent = 10U;
    return exponent;
}

static bool xhci_configure_boot_hid(xhci_hid_device_t *hid,
                                    uint32_t protocol_mask) {
    if (hid == NULL || protocol_mask == 0U) return false;
    memset(control_buffer, 0, sizeof(control_buffer));
    if (!xhci_control(hid, 0x80U, 6U, 0x0100U, 0U, 8U, true,
                      control_buffer)) {
        diagnostics.failure_stage = XHCI_FAILURE_DEVICE_DESCRIPTOR_8;
        printf("USB: xHCI GET_DESCRIPTOR device-8 failed\n");
        return false;
    }
    if (hid->port_speed >= 4U && control_buffer[7] > 15U) {
        diagnostics.failure_stage = XHCI_FAILURE_EP0_DESCRIPTOR;
        printf("USB: xHCI invalid EP0 packet exponent=%u\n",
               (unsigned)control_buffer[7]);
        return false;
    }
    uint16_t descriptor_packet = hid->port_speed >= 4U
        ? (uint16_t)(1U << control_buffer[7]) : control_buffer[7];
    if (control_buffer[0] < 8U || control_buffer[7] == 0U ||
        !xhci_ep0_packet_valid(hid->port_speed, descriptor_packet)) {
        diagnostics.failure_stage = XHCI_FAILURE_EP0_DESCRIPTOR;
        printf("USB: xHCI invalid EP0 packet descriptor=%u context=%u\n",
               (unsigned)descriptor_packet, (unsigned)hid->max_packet);
        return false;
    }
    uint16_t addressed_packet = hid->max_packet;
    if (!xhci_update_ep0_max_packet(hid, descriptor_packet)) {
        diagnostics.failure_stage = XHCI_FAILURE_EP0_DESCRIPTOR;
        printf("USB: xHCI EVALUATE_CONTEXT EP0 failed packet=%u cc=%u\n",
               (unsigned)descriptor_packet,
               (unsigned)diagnostics.last_completion);
        return false;
    }
    if (descriptor_packet != addressed_packet)
        printf("USB: xHCI EP0 packet updated %u->%u\n",
               (unsigned)addressed_packet, (unsigned)descriptor_packet);
    if (!xhci_control(hid, 0x80U, 6U, 0x0100U, 0U, 18U, true,
                      control_buffer)) {
        diagnostics.failure_stage = XHCI_FAILURE_DEVICE_DESCRIPTOR;
        printf("USB: xHCI GET_DESCRIPTOR device failed\n");
        return false;
    }
    diagnostics.device_class = control_buffer[4];
    diagnostics.device_subclass = control_buffer[5];
    diagnostics.device_protocol = control_buffer[6];
    if (!xhci_control(hid, 0x80U, 6U, 0x0200U, 0U, 9U, true,
                      control_buffer)) {
        diagnostics.failure_stage = XHCI_FAILURE_CONFIG_HEADER;
        printf("USB: xHCI GET_DESCRIPTOR config-9 failed\n");
        return false;
    }
    uint16_t total = (uint16_t)control_buffer[2] |
                     ((uint16_t)control_buffer[3] << 8U);
    diagnostics.configuration_length = total;
    if (total < 9U || total > XHCI_CONTROL_BYTES) {
        diagnostics.failure_stage = XHCI_FAILURE_CONFIG_LENGTH;
        printf("USB: xHCI invalid configuration length=%u\n", (unsigned)total);
        return false;
    }
    if (!xhci_control(hid, 0x80U, 6U, 0x0200U, 0U, total, true,
                      control_buffer)) {
        diagnostics.failure_stage = XHCI_FAILURE_CONFIG_DESCRIPTOR;
        printf("USB: xHCI GET_DESCRIPTOR config failed\n");
        return false;
    }
    uint8_t configuration = 0U, interface = 0U, protocol = 0U;
    uint8_t endpoint = 0U, interval = 0U;
    uint16_t packet = 0U;
    if (!xhci_find_boot_hid(control_buffer, total, protocol_mask,
                            &configuration, &interface, &protocol, &endpoint,
                            &packet, &interval) ||
        endpoint == 0U || configuration == 0U) {
        diagnostics.failure_stage = XHCI_FAILURE_NO_BOOT_HID;
        printf("USB: xHCI no HID boot interface\n");
        return false;
    }
    hid->endpoint_id = (uint8_t)(endpoint * 2U + 1U);
    hid->interface_number = interface;
    hid->configuration_value = configuration;
    hid->max_packet = packet;
    hid->hid_protocol = protocol;
    hid->report_size = protocol == 1U ? 8U : (packet >= 4U ? 4U : 3U);
    if (packet < hid->report_size) {
        diagnostics.failure_stage = XHCI_FAILURE_NO_BOOT_HID;
        return false;
    }
    uint8_t saved_slot[XHCI_CONTEXT_BYTES];
    memset(saved_slot, 0, sizeof(saved_slot));
    /* Address Device publishes the authoritative Slot Context in the output
     * Device Context.  Configure Endpoint must start from that controller-
     * updated state, not from the stale Address Device input copy. */
    memcpy(saved_slot, xhci_device_context(hid), controller.context_size);
    memset(input_context, 0, sizeof(input_context));
    uint32_t *control = (uint32_t *)input_context;
    uint32_t *slot = (uint32_t *)(input_context + controller.context_size);
    uint32_t *ep = (uint32_t *)(input_context +
                                ((uint32_t)hid->endpoint_id + 1U) *
                                controller.context_size);
    control[1] = (1U << 0U) | (1U << hid->endpoint_id);
    memcpy(slot, saved_slot, controller.context_size);
    /* Context Entries is the highest valid Device Context Index.  Leaving it
     * at one makes a strict controller ignore the newly added HID endpoint. */
    slot[0] = (slot[0] & ~(0x1FU << 27U)) |
              ((uint32_t)hid->endpoint_id << 27U);
    ep[0] = ((uint32_t)xhci_periodic_interval(hid->port_speed, interval)
             << 16U) | (((uint32_t)packet & 0xFFU) << 24U);
    ep[1] = (3U << 1U) | (7U << 3U) | ((uint32_t)packet << 16U);
    ep[2] = xhci_dma32(xhci_interrupt_ring(hid)) | 1U;
    /* Interrupt endpoints are periodic: Max ESIT Payload must describe the
     * bytes available per service interval.  Average TRB Length uses the same
     * bounded packet size, matching the reference xHCI endpoint setup. */
    ep[4] = (uint32_t)packet | (((uint32_t)packet >> 8U) << 16U);
    xhci_trb_t *configure = xhci_command(TRB_CONFIGURE_ENDPOINT,
                                           xhci_dma32(input_context), 0U,
                                           (uint32_t)hid->slot_id << 24U);
    if (!xhci_wait_command(configure, NULL)) {
        diagnostics.failure_stage = XHCI_FAILURE_CONFIGURE_ENDPOINT;
        printf("USB: xHCI CONFIGURE_ENDPOINT failed\n");
        return false;
    }
    if (!xhci_control(hid, 0x00U, 9U, configuration, 0U, 0U, false, NULL) ||
        !xhci_control(hid, 0x21U, 0x0BU, 0U, interface, 0U, false, NULL) ||
        (protocol == 1U &&
         !xhci_control(hid, 0x21U, 0x0AU, 10U << 8U, interface, 0U,
                       false, NULL))) {
        diagnostics.failure_stage = XHCI_FAILURE_SET_CONFIGURATION;
        printf("USB: xHCI SET_CONFIGURATION/PROTOCOL/IDLE failed\n");
        return false;
    }
    hid->endpoint_index = 0U;
    hid->endpoint_cycle = true;
    memset(xhci_hid_report_buffers(hid), 0,
           sizeof(hid_reports[xhci_hid_index(hid)]));
    xhci_queue_interrupt_report(hid);
    xhci_ring_doorbell(hid->slot_id, hid->endpoint_id);
    diagnostics.failure_stage = XHCI_FAILURE_NONE;
    return true;
}

static bool xhci_start_controller(uint32_t max_slots) {
    bool trace = controller.start_attempts++ == 0U;
    if (!xhci_reset_controller(trace)) return false;
    xhci_prepare_rings();
    xhci_write(controller.op_base + XHCI_CONFIG,
               max_slots > 31U ? 31U : max_slots);
    xhci_write(controller.runtime_base + XHCI_RT_IMAN, 0U);
    xhci_write(controller.op_base + XHCI_USBCMD,
               xhci_read(controller.op_base + XHCI_USBCMD) | XHCI_CMD_RUN);
    return xhci_wait_until(controller.op_base + XHCI_USBSTS, XHCI_STS_HCH,
                           0U, XHCI_TIMEOUT_MS);
}

static uint32_t xhci_connected_ports(void) {
    uint32_t connected = 0U;
    for (uint32_t candidate = 1U; candidate <= controller.port_count;
         ++candidate) {
        uint32_t offset = controller.op_base + XHCI_PORTSC_BASE +
            (candidate - 1U) * XHCI_PORTSC_STRIDE;
        if ((xhci_read(offset) & XHCI_PORT_CCS) != 0U)
            connected |= 1U << (candidate - 1U);
    }
    return connected;
}

static uint32_t xhci_wait_connected_ports(uint32_t timeout_ms) {
    uint64_t start = pit_monotonic_ms();
    uint32_t observed = 0U;
    for (uint32_t poll = 0U; poll < XHCI_POLL_LIMIT; ++poll) {
        observed |= xhci_connected_ports();
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= timeout_ms) return observed;
        if ((poll & 0x3FFU) == 0U) __asm__ __volatile__("pause");
    }
    return observed;
}

/* PORTSC contains several RW1C/RW1S fields.  A raw read-modify-write would
 * write PED back as one and can disable an already enabled port.  Keep only
 * read-only and read/write-sticky fields when composing a control write. */
static uint32_t xhci_port_neutral(uint32_t port) {
    return port & (XHCI_PORT_RO_BITS | XHCI_PORT_RWS_BITS);
}

static bool xhci_wait_port_recovery(uint32_t delay_ms) {
    if (delay_ms > XHCI_PORT_MAX_RECOVERY_MS) return false;
    pit_delay(delay_ms);
    return true;
}

static bool xhci_reset_root_port(uint32_t root_port, uint32_t port_offset,
                                 uint32_t *status) {
    if (status == NULL) return false;
    uint32_t port = xhci_read(port_offset);
    *status = port;
    if ((port & XHCI_PORT_CCS) == 0U) return false;

    if ((port & XHCI_PORT_PP) == 0U) {
        xhci_write(port_offset, xhci_port_neutral(port) | XHCI_PORT_PP);
        (void)xhci_read(port_offset);
        if (!xhci_wait_port_recovery(XHCI_PORT_POWER_SETTLE_MS)) return false;
        port = xhci_read(port_offset);
        *status = port;
        if ((port & XHCI_PORT_CCS) == 0U) return false;
    }

    /* A one clears only change bits which were actually observed.  In
     * particular, do not write the RW1CS PED bit back from the snapshot. */
    xhci_write(port_offset, xhci_port_neutral(port) |
               (port & XHCI_PORT_CHANGE_BITS));
    (void)xhci_read(port_offset);
    port = xhci_read(port_offset);
    xhci_write(port_offset,
               xhci_port_neutral(port) | XHCI_PORT_PP | XHCI_PORT_PR);
    (void)xhci_read(port_offset);

    bool complete = false;
    uint64_t start = pit_monotonic_ms();
    for (uint32_t poll = 0U; poll < XHCI_POLL_LIMIT; ++poll) {
        port = xhci_read(port_offset);
        if ((port & XHCI_PORT_CCS) == 0U) break;
        if ((port & XHCI_PORT_PR) == 0U &&
            (port & XHCI_PORT_PRC) != 0U &&
            (port & XHCI_PORT_PED) != 0U) {
            complete = true;
            break;
        }
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= XHCI_TIMEOUT_MS) break;
        if ((poll & 0x3FFU) == 0U) __asm__ __volatile__("pause");
    }
    *status = port;
    if (!complete) {
        printf("USB: xHCI port=%u reset incomplete status=%08X\n",
               (unsigned)root_port, (unsigned)port);
        return false;
    }

    xhci_write(port_offset, xhci_port_neutral(port) |
               (port & XHCI_PORT_CHANGE_BITS));
    (void)xhci_read(port_offset);
    if (!xhci_wait_port_recovery(XHCI_PORT_RESET_RECOVERY_MS)) return false;

    port = xhci_read(port_offset);
    *status = port;
    if ((port & (XHCI_PORT_CCS | XHCI_PORT_PED | XHCI_PORT_PR)) !=
        (XHCI_PORT_CCS | XHCI_PORT_PED)) {
        printf("USB: xHCI port=%u unstable after reset status=%08X\n",
               (unsigned)root_port, (unsigned)port);
        return false;
    }
    return true;
}

static bool xhci_enumerate_root_hid(xhci_hid_device_t *hid,
                                    uint32_t root_port,
                                    uint32_t protocol_mask) {
    if (hid == NULL || root_port == 0U ||
        root_port > controller.port_count || protocol_mask == 0U)
        return false;
    diagnostics.failure_stage = XHCI_FAILURE_NONE;
    diagnostics.candidate_port = root_port;
    diagnostics.candidate_speed = 0U;
    diagnostics.device_class = 0U;
    diagnostics.device_subclass = 0U;
    diagnostics.device_protocol = 0U;
    diagnostics.configuration_length = 0U;
    hid->root_port = root_port;
    uint32_t port_offset = controller.op_base + XHCI_PORTSC_BASE +
        (root_port - 1U) * XHCI_PORTSC_STRIDE;
    uint32_t port = 0U;
    if (!xhci_reset_root_port(root_port, port_offset, &port)) {
        diagnostics.failure_stage = XHCI_FAILURE_PORT_RESET;
        return false;
    }
    uint8_t port_speed = (uint8_t)((port & XHCI_PORT_SPEED_MASK) >> 10U);
    diagnostics.candidate_speed = port_speed;
    hid->port_speed = port_speed;
    hid->max_packet = port_speed >= 4U ? 512U :
                      (port_speed == 3U ? 64U : 8U);
    if (!xhci_address_device(hid, port_speed)) {
        diagnostics.failure_stage = XHCI_FAILURE_ADDRESS_DEVICE;
        printf("USB: xHCI address-device failed\n");
        return false;
    }
    if (!xhci_configure_boot_hid(hid, protocol_mask)) {
        printf("USB: xHCI port=%u HID configuration failed\n",
               (unsigned)root_port);
        return false;
    }
    printf("USB: xHCI port=%u HID %s candidate\n", (unsigned)root_port,
           hid->hid_protocol == 2U ? "mouse" : "keyboard");
    return hid->hid_protocol == 1U || hid->hid_protocol == 2U;
}

static bool xhci_publish_hid(xhci_hid_device_t *hid) {
    if (hid == NULL || hid->online ||
        (hid->hid_protocol != 1U && hid->hid_protocol != 2U) ||
        xhci_hid_by_protocol(hid->hid_protocol) != NULL)
        return false;
    hid->generation++;
    if (hid->generation == 0U) hid->generation = 1U;
    if (hid->hid_protocol == 1U)
        hid_keyboard_attach(hid->generation);
    else
        hid_mouse_attach(hid->generation);
    hid->online = true;
    xhci_refresh_ready_state();
    return true;
}

static void xhci_irq_handler(void *opaque) {
    (void)opaque;
    uint32_t flags = spinlock_acquire_irq(&xhci_runtime_lock);
    if (!controller.runtime_published || controller.mmio == NULL ||
        !xhci_any_hid_online()) {
        spinlock_release_irq(&xhci_runtime_lock, flags);
        return;
    }
    uint32_t status = xhci_read(controller.op_base + XHCI_USBSTS);
    uint32_t iman = xhci_read(controller.runtime_base + XHCI_RT_IMAN);
    if ((status & XHCI_STS_EINT) == 0U && (iman & XHCI_IMAN_IP) == 0U) {
        spinlock_release_irq(&xhci_runtime_lock, flags);
        return;
    }
    (void)xhci_drain_events(32U, 0U, 0U, NULL, NULL, NULL, NULL);
    /* IMAN.IP is RW1C.  Leaving it asserted keeps the legacy PCI interrupt
     * active even after USBSTS.EINT was acknowledged and can starve the
     * remainder of early boot as soon as the mouse produces reports. */
    xhci_write(controller.runtime_base + XHCI_RT_IMAN,
               XHCI_IMAN_IP | XHCI_IMAN_IE);
    xhci_write(controller.op_base + XHCI_USBSTS, XHCI_STS_EINT);
    spinlock_release_irq(&xhci_runtime_lock, flags);
}

int xhci_probe(pci_device_t *dev) {
    if (dev == NULL || dev->prog_if != 0x30U) return -1;
    if (xhci_any_hid_online()) {
        printf("USB: xHCI HID already active; preserving controller\n");
        return 0;
    }
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.version = XHCI_DIAGNOSTICS_VERSION;
    diagnostics.struct_size = sizeof(diagnostics);
    diagnostics.state = XHCI_DIAG_PROBING;
    diagnostics.bus = dev->bus;
    diagnostics.slot = dev->slot;
    diagnostics.function = dev->function;
    diagnostics.vendor_id = dev->vendor_id;
    diagnostics.device_id = dev->device_id;
    uint32_t bar = pci_read_bar(dev, 0U);
    if (bar == 0U || bar == UINT32_MAX || (bar & 1U) != 0U ||
        ((bar & 6U) == 4U && dev->bar[1] != 0U)) {
        printf("USB: xHCI invalid BAR0=%08X BAR1=%08X\n", bar, dev->bar[1]);
        diagnostics.state = XHCI_DIAG_INVALID_BAR;
        return -1;
    }
    pci_enable_device(dev);
    memset(&controller, 0, sizeof(controller));
    memset(hid_devices, 0, sizeof(hid_devices));
    controller.mmio = map_mmio_region((uint64_t)(bar & ~0x0FU), XHCI_MMIO_SIZE);
    if (controller.mmio == NULL) {
        printf("USB: xHCI MMIO mapping failed BAR0=%08X\n", bar);
        diagnostics.state = XHCI_DIAG_MMIO_FAILED;
        return -1;
    }
    uint32_t caplength = xhci_read(0U) & 0xFFU;
    uint32_t hcs1 = xhci_read(4U);
    uint32_t hcs2 = xhci_read(8U);
    uint32_t hcc = xhci_read(0x10U);
    controller.op_base = caplength;
    controller.doorbell_base = xhci_read(0x14U) & ~0x3U;
    controller.runtime_base = xhci_read(0x18U) & ~0x1FU;
    controller.port_count = (hcs1 >> 24U) & 0xFFU;
    diagnostics.port_count = controller.port_count;
    controller.scratchpad_count = xhci_scratchpad_count(hcs2);
    diagnostics.cap_length = caplength;
    diagnostics.max_slots = hcs1 & 0xFFU;
    diagnostics.scratchpad_count = controller.scratchpad_count;
    diagnostics.doorbell_offset = controller.doorbell_base;
    diagnostics.runtime_offset = controller.runtime_base;
    uint32_t capability_rejections = 0U;
    if (controller.port_count == 0U || controller.port_count > XHCI_MAX_PORTS)
        capability_rejections |= XHCI_CAP_REJECT_PORT_COUNT;
    if (controller.scratchpad_count > XHCI_MAX_SCRATCHPADS)
        capability_rejections |= XHCI_CAP_REJECT_SCRATCHPADS;
    if (controller.op_base < 0x20U)
        capability_rejections |= XHCI_CAP_REJECT_CAPLENGTH;
    if (controller.op_base + XHCI_PORTSC_BASE +
            controller.port_count * XHCI_PORTSC_STRIDE > XHCI_MMIO_SIZE)
        capability_rejections |= XHCI_CAP_REJECT_PORT_RANGE;
    if (controller.doorbell_base >= XHCI_MMIO_SIZE)
        capability_rejections |= XHCI_CAP_REJECT_DOORBELL_RANGE;
    if (controller.runtime_base >= XHCI_MMIO_SIZE)
        capability_rejections |= XHCI_CAP_REJECT_RUNTIME_RANGE;
    diagnostics.capability_rejections = capability_rejections;
    if (capability_rejections != 0U) {
        printf("USB: xHCI capabilities rejected ports=%u scratch=%u db=%X rt=%X\n",
               (unsigned)controller.port_count,
               (unsigned)controller.scratchpad_count,
               (unsigned)controller.doorbell_base,
               (unsigned)controller.runtime_base);
        diagnostics.state = XHCI_DIAG_CAPABILITIES_REJECTED;
        return -1;
    }
    controller.context_size = (hcc & (1U << 2U)) != 0U ? 64U : 32U;
    if (!xhci_legacy_handoff()) {
        printf("USB: xHCI legacy handoff failed\n");
        diagnostics.state = XHCI_DIAG_HANDOFF_FAILED;
        return -1;
    }
    if (!xhci_route_intel_ports(dev)) {
        printf("USB: Intel xHCI port routing failed flags=%X\n",
               (unsigned)diagnostics.intel_routing_flags);
        diagnostics.state = XHCI_DIAG_PORT_ROUTING_FAILED;
        return -1;
    }
    if ((diagnostics.intel_routing_flags &
         XHCI_INTEL_ROUTE_ATTEMPTED) != 0U) {
        printf("USB: Intel xHCI routing usb2=%08X/%08X usb3=%08X/%08X flags=%X\n",
               (unsigned)diagnostics.usb2_routing_mask,
               (unsigned)diagnostics.usb2_routing,
               (unsigned)diagnostics.usb3_routing_mask,
               (unsigned)diagnostics.usb3_routing,
               (unsigned)diagnostics.intel_routing_flags);
    }
    if (!xhci_dma_valid(dcbaa, 64U) || !xhci_dma_valid(command_ring, 64U) ||
        !xhci_dma_valid(event_ring, 64U) ||
        !xhci_dma_valid(input_context, 4096U) || !xhci_hid_dma_valid()) {
        printf("USB: xHCI DMA alignment rejected\n");
        diagnostics.state = XHCI_DIAG_DMA_REJECTED;
        return -1;
    }
    pci_set_bus_master(dev->bus, dev->slot, dev->function, 1U);
    uint32_t max_slots = diagnostics.max_slots;
    if (max_slots == 0U) max_slots = 1U;
    if (!xhci_start_controller(max_slots)) {
        printf("USB: xHCI controller did not enter run state\n");
        diagnostics.state = XHCI_DIAG_START_FAILED;
        pci_set_bus_master(dev->bus, dev->slot, dev->function, 0U);
        return -1;
    }

    /* Controller reset and Run/Stop publication are asynchronous to root-port
     * connection visibility on physical hosts.  Applying the finite discovery
     * window only after Intel companion routing made successful enumeration
     * depend on unrelated early-boot work (observably, i8042/PS/2 setup) on
     * other xHCI implementations.  Use the same bounded window for every
     * controller and retain every port observed during it. */
    uint32_t connected_ports =
        xhci_wait_connected_ports(XHCI_PORT_SETTLE_MS);
    diagnostics.connected_ports = connected_ports;
    printf("USB: xHCI root ports=%u connected=%08X\n",
           (unsigned)controller.port_count, (unsigned)connected_ports);
    uint32_t attempts = 0U;
    bool release_failed = false;
    for (uint32_t root_port = 1U;
         root_port <= controller.port_count &&
         attempts < XHCI_MAX_HID_CANDIDATES;
         ++root_port) {
        if ((connected_ports & (1U << (root_port - 1U))) == 0U) continue;
        uint32_t protocol_mask = 0U;
        if (xhci_hid_by_protocol(1U) == NULL)
            protocol_mask |= XHCI_HID_KEYBOARD_MASK;
        if (xhci_hid_by_protocol(2U) == NULL)
            protocol_mask |= XHCI_HID_MOUSE_MASK;
        if (protocol_mask == 0U) break;
        xhci_hid_device_t *hid = xhci_free_hid_resource();
        if (hid == NULL) break;
        attempts++;
        diagnostics.attempts = attempts;
        if (!xhci_enumerate_root_hid(hid, root_port, protocol_mask)) {
            if (!xhci_release_candidate(hid)) {
                diagnostics.failure_stage = XHCI_FAILURE_RELEASE_SLOT;
                release_failed = true;
                printf("USB: xHCI failed to release HID candidate port=%u\n",
                       (unsigned)root_port);
                break;
            }
            continue;
        }
        if (!xhci_publish_hid(hid)) {
            if (!xhci_release_candidate(hid)) {
                diagnostics.failure_stage = XHCI_FAILURE_RELEASE_SLOT;
                release_failed = true;
            }
            break;
        }
    }
    if (release_failed || !xhci_any_hid_online()) {
        for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index)
            xhci_detach_hid(&hid_devices[index]);
        printf("USB: xHCI no supported root-port boot HID after %u attempts\n",
               (unsigned)attempts);
        diagnostics.state = release_failed ? XHCI_DIAG_START_FAILED :
            (connected_ports == 0U ? XHCI_DIAG_NO_CONNECTED_PORT :
                                     XHCI_DIAG_NO_SUPPORTED_HID);
        xhci_write(controller.op_base + XHCI_USBCMD,
                   xhci_read(controller.op_base + XHCI_USBCMD) &
                   ~(XHCI_CMD_RUN | XHCI_CMD_INTE));
        pci_set_bus_master(dev->bus, dev->slot, dev->function, 0U);
        return -1;
    }
    controller.irq = pci_configure_irq(dev);
    if (!pci_irq_is_valid(controller.irq) ||
        register_interrupt_handler(controller.irq, (void *)xhci_irq_handler) != 0) {
        for (uint32_t index = 0U; index < XHCI_MAX_HID_DEVICES; ++index)
            xhci_detach_hid(&hid_devices[index]);
        xhci_write(controller.op_base + XHCI_USBCMD,
                   xhci_read(controller.op_base + XHCI_USBCMD) &
                   ~(XHCI_CMD_RUN | XHCI_CMD_INTE));
        pci_set_bus_master(dev->bus, dev->slot, dev->function, 0U);
        printf("USB: xHCI IRQ setup failed irq=%u\n", (unsigned)controller.irq);
        diagnostics.irq = controller.irq;
        diagnostics.state = XHCI_DIAG_IRQ_FAILED;
        return -1;
    }
    /* Publish the fully enumerated runtime atomically with interrupt enable.
     * No deadline-bound construction or control wait is inside this lock. */
    uint32_t runtime_flags = spinlock_acquire_irq(&xhci_runtime_lock);
    controller.runtime_published = true;
    diagnostics.irq = controller.irq;
    xhci_refresh_ready_state();
    /* Clear enumeration-time pending state before unmasking the interrupter. */
    xhci_write(controller.runtime_base + XHCI_RT_IMAN,
               XHCI_IMAN_IP | XHCI_IMAN_IE);
    xhci_write(controller.op_base + XHCI_USBCMD,
               xhci_read(controller.op_base + XHCI_USBCMD) |
               XHCI_CMD_INTE | XHCI_CMD_RUN);
    uint32_t keyboard_port = diagnostics.keyboard_port;
    uint32_t mouse_port = diagnostics.mouse_port;
    uint32_t runtime_irq = controller.irq;
    spinlock_release_irq(&xhci_runtime_lock, runtime_flags);
    printf("USB: xHCI HID ready keyboard-port=%u mouse-port=%u irq=%u\n",
           (unsigned)keyboard_port, (unsigned)mouse_port,
           (unsigned)runtime_irq);
    return 0;
}

/* Called from task-context console polling.  IRQ context only records changes. */
void xhci_poll(void) {
    if (irq_in_context()) return;
    uint32_t flags = spinlock_acquire_irq(&xhci_runtime_lock);
    if (controller.runtime_published && controller.mmio != NULL &&
        xhci_any_hid_online())
        (void)xhci_drain_events(16U, 0U, 0U, NULL, NULL, NULL, NULL);
    spinlock_release_irq(&xhci_runtime_lock, flags);
}

bool xhci_get_diagnostics(xhci_diagnostics_t *snapshot) {
    if (snapshot == NULL) return false;
    uint32_t flags = spinlock_acquire_irq(&xhci_runtime_lock);
    *snapshot = diagnostics;
    spinlock_release_irq(&xhci_runtime_lock, flags);
    return snapshot->version == XHCI_DIAGNOSTICS_VERSION &&
           snapshot->struct_size == sizeof(*snapshot);
}
