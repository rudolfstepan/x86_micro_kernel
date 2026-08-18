/**
 * @file drivers/usb/xhci.c
 * @brief Begrenzter xHCI-Host für USB-HID-Boot-Tastatur oder -Maus.
 *
 * Der Treiber implementiert bewusst nur einen Root-Port und ein HID-
 * Boot-HID-Gerät. Alle DMA-Objekte sind statisch, 32-Bit-adressierbar und
 * ausgerichtet. Enumeration und Control-Transfers laufen mit monotonen
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
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XHCI_MMIO_SIZE          0x10000U
#define XHCI_TIMEOUT_MS         1000U
#define XHCI_POLL_LIMIT         1000000U
#define XHCI_MAX_PORTS          8U
#define XHCI_COMMAND_RING_TRBS  64U
#define XHCI_EVENT_RING_TRBS    128U
#define XHCI_ENDPOINT_RING_TRBS 32U
#define XHCI_CONTEXT_BYTES      64U
#define XHCI_MAX_SCRATCHPADS     8U
#define XHCI_CONTROL_BYTES      256U

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
#define XHCI_PORT_PR            (1U << 4)
#define XHCI_PORT_PP            (1U << 9)
#define XHCI_PORT_SPEED_MASK    (0xFU << 10)
#define XHCI_PORT_CSC           (1U << 17)
#define XHCI_PORT_PRC           (1U << 21)
#define XHCI_IMAN_IP             (1U << 0)
#define XHCI_IMAN_IE             (1U << 1)

#define TRB_CYCLE                (1U << 0)
#define TRB_ENT                  (1U << 1)
#define TRB_IOC                  (1U << 5)
#define TRB_CHAIN                (1U << 4)
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
#define TRB_ADDRESS_DEVICE       11U
#define TRB_CONFIGURE_ENDPOINT   12U
#define TRB_EVENT_TRANSFER       32U
#define TRB_EVENT_COMMAND        33U
#define TRB_EVENT_PORT           34U
#define XHCI_COMPLETION_SUCCESS  1U
#define XHCI_COMPLETION_SHORT_PACKET 13U

typedef struct {
    uint32_t d[4];
} xhci_trb_t;

typedef struct {
    uint64_t ring_segment;
    uint32_t ring_size;
    uint32_t reserved;
} xhci_erst_entry_t;

typedef struct {
    volatile uint32_t *mmio;
    uint32_t op_base;
    uint32_t runtime_base;
    uint32_t doorbell_base;
    uint32_t context_size;
    uint32_t port_count;
    uint32_t scratchpad_count;
    uint32_t root_port;
    uint8_t irq;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint8_t interface_number;
    uint8_t configuration_value;
    uint8_t hid_protocol;
    uint8_t report_size;
    uint16_t max_packet;
    uint32_t generation;
    uint32_t command_index;
    uint32_t event_index;
    uint32_t endpoint_index;
    bool command_cycle;
    bool event_cycle;
    bool endpoint_cycle;
    bool online;
    bool port_change_pending;
} xhci_state_t;

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
static xhci_trb_t endpoint0_ring[XHCI_ENDPOINT_RING_TRBS]
    __attribute__((aligned(4096)));
static xhci_trb_t interrupt_ring[XHCI_ENDPOINT_RING_TRBS]
    __attribute__((aligned(4096)));
static uint8_t input_context[33 * XHCI_CONTEXT_BYTES]
    __attribute__((aligned(4096)));
static uint8_t device_context[32 * XHCI_CONTEXT_BYTES]
    __attribute__((aligned(4096)));
static uint8_t control_buffer[XHCI_CONTROL_BYTES]
    __attribute__((aligned(64)));
static uint8_t hid_reports[XHCI_ENDPOINT_RING_TRBS][8]
    __attribute__((aligned(64)));
static xhci_state_t controller;
static uint32_t root_port_status;

static uint32_t xhci_dma32(const void *address) {
    uintptr_t value = (uintptr_t)address;
    if (value > UINT32_MAX) return 0U;
    return (uint32_t)value;
}

static bool xhci_dma_valid(const void *address, uint32_t alignment) {
    uint32_t value = xhci_dma32(address);
    return value != 0U && (value & (alignment - 1U)) == 0U;
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
        uint32_t header = xhci_read(extended);
        uint8_t id = (uint8_t)header;
        uint8_t next = (uint8_t)(header >> 8U);
        if (id == 1U) {
            uint32_t legacy = xhci_read(extended + 4U);
            if ((legacy & (1U << 24U)) == 0U) {
                xhci_write(extended + 4U, legacy | (1U << 24U));
                if (!xhci_wait_until(extended + 4U, 1U << 16U, 0U,
                                     XHCI_TIMEOUT_MS)) return false;
            }
            return true;
        }
        extended = (uint32_t)next << 2U;
    }
    return true;
}

static bool xhci_reset_controller(void) {
    uint32_t command = xhci_read(controller.op_base + XHCI_USBCMD);
    xhci_write(controller.op_base + XHCI_USBCMD, command & ~XHCI_CMD_RUN);
    if (!xhci_wait_until(controller.op_base + XHCI_USBSTS, XHCI_STS_HCH,
                         XHCI_STS_HCH, XHCI_TIMEOUT_MS)) return false;
    xhci_write(controller.op_base + XHCI_USBCMD, XHCI_CMD_RESET);
    if (!xhci_wait_until(controller.op_base + XHCI_USBCMD, XHCI_CMD_RESET,
                         0U, XHCI_TIMEOUT_MS)) return false;
    return xhci_wait_until(controller.op_base + XHCI_USBSTS, XHCI_STS_CNR,
                           0U, XHCI_TIMEOUT_MS);
}

static void xhci_prepare_rings(void) {
    memset(dcbaa, 0, sizeof(dcbaa));
    memset(command_ring, 0, sizeof(command_ring));
    memset(event_ring, 0, sizeof(event_ring));
    memset(endpoint0_ring, 0, sizeof(endpoint0_ring));
    memset(interrupt_ring, 0, sizeof(interrupt_ring));
    memset(input_context, 0, sizeof(input_context));
    memset(device_context, 0, sizeof(device_context));
    for (uint32_t index = 0U; index < XHCI_MAX_SCRATCHPADS; ++index)
        scratchpad_array[index] = xhci_dma32(scratchpad_pages[index]);
    command_ring[XHCI_COMMAND_RING_TRBS - 1U].d[0] = xhci_dma32(command_ring);
    command_ring[XHCI_COMMAND_RING_TRBS - 1U].d[3] =
        TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
    endpoint0_ring[XHCI_ENDPOINT_RING_TRBS - 1U].d[0] =
        xhci_dma32(endpoint0_ring);
    endpoint0_ring[XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
        TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
    interrupt_ring[XHCI_ENDPOINT_RING_TRBS - 1U].d[0] =
        xhci_dma32(interrupt_ring);
    interrupt_ring[XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
        TRB_TYPE(TRB_LINK) | TRB_CYCLE | TRB_ENT;
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
    controller.endpoint_index = 0U;
    controller.command_cycle = true;
    controller.event_cycle = true;
    controller.endpoint_cycle = true;
}

static void xhci_ring_doorbell(uint8_t slot, uint8_t endpoint) {
    xhci_write(controller.doorbell_base + (uint32_t)slot * 4U, endpoint);
}

static void xhci_queue_interrupt_report(void) {
    uint32_t index = controller.endpoint_index;
    xhci_trb_t *trb = &interrupt_ring[index];
    memset(trb, 0, sizeof(*trb));
    trb->d[0] = xhci_dma32(hid_reports[index]);
    trb->d[2] = controller.report_size;
    trb->d[3] = TRB_TYPE(TRB_TRANSFER) | TRB_IOC |
        (controller.endpoint_cycle ? TRB_CYCLE : 0U);
    controller.endpoint_index++;
    if (controller.endpoint_index >= XHCI_ENDPOINT_RING_TRBS - 1U) {
        interrupt_ring[XHCI_ENDPOINT_RING_TRBS - 1U].d[3] =
            TRB_TYPE(TRB_LINK) | TRB_ENT |
            (controller.endpoint_cycle ? TRB_CYCLE : 0U);
        controller.endpoint_index = 0U;
        controller.endpoint_cycle = !controller.endpoint_cycle;
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

static bool xhci_drain_events(uint32_t limit, uint32_t expected,
                              uint8_t *slot_out, uint32_t *status_out) {
    bool found = false;
    for (uint32_t count = 0U; count < limit; ++count) {
        xhci_trb_t *event = &event_ring[controller.event_index];
        if ((event->d[3] & 1U) != (controller.event_cycle ? 1U : 0U)) break;
        uint32_t type = (event->d[3] >> 10U) & 0x3FU;
        uint32_t pointer = event->d[0];
        uint32_t status = event->d[2];
        if (type == TRB_EVENT_COMMAND && pointer == expected) {
            if (slot_out != NULL) *slot_out = (uint8_t)(event->d[3] >> 24U);
            if (status_out != NULL) *status_out = (status >> 24U) & 0xFFU;
            found = true;
        } else if (type == TRB_EVENT_TRANSFER) {
            if (pointer == expected) {
                if (status_out != NULL) *status_out = (status >> 24U) & 0xFFU;
                found = true;
            }
            if (!controller.online) goto event_done;
            uint32_t first = xhci_dma32(&interrupt_ring[0]);
            uint32_t last = xhci_dma32(
                &interrupt_ring[XHCI_ENDPOINT_RING_TRBS - 2U]);
            uint32_t completion = (status >> 24U) & 0xFFU;
            if (pointer >= first && pointer <= last &&
                ((pointer - first) % sizeof(interrupt_ring[0])) == 0U &&
                (completion == XHCI_COMPLETION_SUCCESS ||
                 completion == XHCI_COMPLETION_SHORT_PACKET)) {
                uint32_t index = (pointer - first) / sizeof(interrupt_ring[0]);
                uint32_t residual = status & 0x00FFFFFFU;
                size_t actual = residual <= controller.report_size
                    ? controller.report_size - residual : 0U;
                if (controller.hid_protocol == 1U)
                    (void)hid_keyboard_report(controller.generation,
                                               hid_reports[index], actual);
                else if (controller.hid_protocol == 2U)
                    (void)hid_mouse_report(controller.generation,
                                            hid_reports[index], actual);
                xhci_queue_interrupt_report();
                xhci_ring_doorbell(controller.slot_id, controller.endpoint_id);
            }
        } else if (type == TRB_EVENT_PORT) {
            controller.port_change_pending = true;
            uint32_t port_offset = controller.op_base + XHCI_PORTSC_BASE +
                (controller.root_port - 1U) * XHCI_PORTSC_STRIDE;
            root_port_status = xhci_read(port_offset);
            if ((root_port_status & XHCI_PORT_CCS) == 0U && controller.online) {
                if (controller.hid_protocol == 1U)
                    hid_keyboard_detach(controller.generation);
                else if (controller.hid_protocol == 2U)
                    hid_mouse_detach(controller.generation);
                controller.online = false;
            }
        }
event_done:
        memset(event, 0, sizeof(*event));
        controller.event_index++;
        if (controller.event_index >= XHCI_EVENT_RING_TRBS) {
            controller.event_index = 0U;
            controller.event_cycle = !controller.event_cycle;
        }
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
        if (xhci_drain_events(32U, xhci_dma32(command), slot_out, &status))
            return status == XHCI_COMPLETION_SUCCESS;
    }
    return false;
}

static bool xhci_wait_transfer(xhci_trb_t *status_trb,
                               uint32_t *completion_out) {
    uint64_t start = pit_monotonic_ms();
    uint32_t expected = xhci_dma32(status_trb);
    while (1) {
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= XHCI_TIMEOUT_MS) break;
        uint32_t status = 0U;
        if (xhci_drain_events(32U, expected, NULL, &status)) {
            if (completion_out != NULL) *completion_out = status;
            return status == XHCI_COMPLETION_SUCCESS;
        }
    }
    if (completion_out != NULL) *completion_out = 0U;
    return false;
}

static bool xhci_control(uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         bool direction_in, uint8_t *buffer) {
    if (length > XHCI_CONTROL_BYTES || (length != 0U && buffer == NULL))
        return false;
    xhci_trb_t *setup = &endpoint0_ring[controller.endpoint_index];
    xhci_trb_t *data = &endpoint0_ring[(controller.endpoint_index + 1U) %
                                       (XHCI_ENDPOINT_RING_TRBS - 1U)];
    xhci_trb_t *status = &endpoint0_ring[(controller.endpoint_index +
                                          (length != 0U ? 2U : 1U)) %
                                         (XHCI_ENDPOINT_RING_TRBS - 1U)];
    uint8_t setup_packet[8] = {request_type, request, (uint8_t)value,
                               (uint8_t)(value >> 8U), (uint8_t)index,
                               (uint8_t)(index >> 8U), (uint8_t)length,
                               (uint8_t)(length >> 8U)};
    memset(setup, 0, sizeof(*setup));
    memcpy(setup->d, setup_packet, sizeof(setup_packet));
    uint32_t transfer_type = length == 0U ? 0U :
                             (direction_in ? TRB_TRT_IN : TRB_TRT_OUT);
    setup->d[2] = 8U;
    setup->d[3] = TRB_TYPE(TRB_SETUP) | TRB_IDT | TRB_CHAIN |
        transfer_type |
        (controller.endpoint_cycle ? 1U : 0U);
    if (length != 0U) {
        memset(data, 0, sizeof(*data));
        data->d[0] = xhci_dma32(buffer);
        data->d[2] = length;
        data->d[3] = TRB_TYPE(TRB_DATA) | TRB_CHAIN |
            (direction_in ? TRB_DIR_IN : 0U) |
            (controller.endpoint_cycle ? 1U : 0U);
    }
    memset(status, 0, sizeof(*status));
    status->d[3] = TRB_TYPE(TRB_STATUS) | TRB_IOC |
        (direction_in ? 0U : TRB_DIR_IN) |
        (controller.endpoint_cycle ? 1U : 0U);
    xhci_ring_doorbell(controller.slot_id, 1U);
    uint32_t completion = 0U;
    bool result = xhci_wait_transfer(status, &completion);
    if (!result) {
        printf("USB: xHCI control failed request=%u type=%02X length=%u cc=%u\n",
               (unsigned)request, (unsigned)request_type, (unsigned)length,
               (unsigned)completion);
    }
    controller.endpoint_index = (uint32_t)((status - endpoint0_ring) + 1U);
    if (controller.endpoint_index >= XHCI_ENDPOINT_RING_TRBS - 1U) {
        controller.endpoint_index = 0U;
        controller.endpoint_cycle = !controller.endpoint_cycle;
    }
    return result;
}

static bool xhci_address_device(uint8_t port_speed) {
    xhci_trb_t *command = xhci_command(TRB_ENABLE_SLOT, 0U, 0U, 0U);
    uint8_t slot = 0U;
    if (!xhci_wait_command(command, &slot) || slot == 0U || slot > 31U)
        return false;
    controller.slot_id = slot;
    memset(input_context, 0, sizeof(input_context));
    memset(device_context, 0, sizeof(device_context));
    dcbaa[slot] = xhci_dma32(device_context);
    uint32_t *control = (uint32_t *)input_context;
    uint32_t *slot_context = (uint32_t *)(input_context +
                                          controller.context_size);
    uint32_t *ep0 = (uint32_t *)(input_context +
                                  2U * controller.context_size);
    control[1] = (1U << 0U) | (1U << 1U);
    slot_context[0] = ((uint32_t)port_speed << 20U) | (1U << 27U);
    slot_context[1] = controller.root_port << 16U;
    ep0[1] = (3U << 1U) | (4U << 3U) | ((uint32_t)controller.max_packet << 16U);
    ep0[2] = xhci_dma32(endpoint0_ring) | 1U;
    ep0[3] = 0U;
    ep0[4] = 8U;
    xhci_trb_t *address = xhci_command(TRB_ADDRESS_DEVICE,
                                        xhci_dma32(input_context), 0U,
                                        (uint32_t)slot << 24U);
    uint8_t completed_slot = 0U;
    return xhci_wait_command(address, &completed_slot) &&
           completed_slot == slot;
}

static bool xhci_find_boot_hid(uint8_t *config, uint16_t length,
                               uint8_t *configuration, uint8_t *interface,
                               uint8_t *protocol, uint8_t *endpoint,
                               uint16_t *packet, uint8_t *interval) {
    uint16_t offset = 0U;
    bool boot_hid = false;
    while (offset + 2U <= length) {
        uint8_t item_length = config[offset];
        uint8_t item_type = config[offset + 1U];
        if (item_length < 2U || offset + item_length > length) return false;
        if (item_type == 2U && item_length >= 6U) *configuration = config[offset + 5U];
        if (item_type == 4U && item_length >= 9U) {
            boot_hid = config[offset + 5U] == 3U &&
                       config[offset + 6U] == 1U &&
                       (config[offset + 7U] == 1U ||
                        config[offset + 7U] == 2U);
            if (boot_hid) {
                *interface = config[offset + 2U];
                *protocol = config[offset + 7U];
            }
        } else if (item_type == 5U && boot_hid && item_length >= 7U &&
                   (config[offset + 2U] & 0x80U) != 0U &&
                   (config[offset + 3U] & 3U) == 3U) {
            uint16_t max_packet = (uint16_t)config[offset + 4U] |
                                  ((uint16_t)config[offset + 5U] << 8U);
            if (max_packet == 0U || max_packet > 8U) return false;
            *endpoint = config[offset + 2U] & 0x0FU;
            *packet = max_packet;
            *interval = config[offset + 6U] == 0U ? 1U : config[offset + 6U];
            return true;
        }
        offset = (uint16_t)(offset + item_length);
    }
    return false;
}

static bool xhci_configure_boot_hid(void) {
    memset(control_buffer, 0, sizeof(control_buffer));
    if (!xhci_control(0x80U, 6U, 0x0100U, 0U, 8U, true, control_buffer)) {
        printf("USB: xHCI GET_DESCRIPTOR device-8 failed\n");
        return false;
    }
    if (controller.max_packet >= 512U && control_buffer[7] > 15U) {
        printf("USB: xHCI invalid EP0 packet exponent=%u\n",
               (unsigned)control_buffer[7]);
        return false;
    }
    uint16_t descriptor_packet = controller.max_packet >= 512U
        ? (uint16_t)(1U << control_buffer[7]) : control_buffer[7];
    if (control_buffer[0] < 8U || control_buffer[7] == 0U ||
        descriptor_packet != controller.max_packet) {
        printf("USB: xHCI invalid EP0 packet descriptor=%u context=%u\n",
               (unsigned)descriptor_packet, (unsigned)controller.max_packet);
        return false;
    }
    if (!xhci_control(0x80U, 6U, 0x0100U, 0U, 18U, true, control_buffer)) {
        printf("USB: xHCI GET_DESCRIPTOR device failed\n");
        return false;
    }
    if (!xhci_control(0x80U, 6U, 0x0200U, 0U, 9U, true, control_buffer)) {
        printf("USB: xHCI GET_DESCRIPTOR config-9 failed\n");
        return false;
    }
    uint16_t total = (uint16_t)control_buffer[2] |
                     ((uint16_t)control_buffer[3] << 8U);
    if (total < 9U || total > XHCI_CONTROL_BYTES) {
        printf("USB: xHCI invalid configuration length=%u\n", (unsigned)total);
        return false;
    }
    if (!xhci_control(0x80U, 6U, 0x0200U, 0U, total, true, control_buffer)) {
        printf("USB: xHCI GET_DESCRIPTOR config failed\n");
        return false;
    }
    uint8_t configuration = 0U, interface = 0U, protocol = 0U;
    uint8_t endpoint = 0U, interval = 0U;
    uint16_t packet = 0U;
    if (!xhci_find_boot_hid(control_buffer, total, &configuration, &interface,
                            &protocol, &endpoint, &packet, &interval) ||
        endpoint == 0U || configuration == 0U) {
        printf("USB: xHCI no HID boot interface\n");
        return false;
    }
    controller.endpoint_id = (uint8_t)(endpoint * 2U + 1U);
    controller.interface_number = interface;
    controller.configuration_value = configuration;
    controller.max_packet = packet;
    controller.hid_protocol = protocol;
    controller.report_size = protocol == 1U ? 8U :
                             (packet >= 4U ? 4U : 3U);
    if (packet < controller.report_size) return false;
    uint8_t saved_slot[XHCI_CONTEXT_BYTES];
    memset(saved_slot, 0, sizeof(saved_slot));
    memcpy(saved_slot, input_context + controller.context_size,
           controller.context_size);
    memset(input_context, 0, sizeof(input_context));
    uint32_t *control = (uint32_t *)input_context;
    uint32_t *slot = (uint32_t *)(input_context + controller.context_size);
    uint32_t *ep = (uint32_t *)(input_context +
                                ((uint32_t)controller.endpoint_id + 1U) *
                                controller.context_size);
    control[1] = (1U << 0U) | (1U << controller.endpoint_id);
    memcpy(slot, saved_slot, controller.context_size);
    ep[0] = (uint32_t)interval << 16U;
    ep[1] = (3U << 1U) | (7U << 3U) | ((uint32_t)packet << 16U);
    ep[2] = xhci_dma32(interrupt_ring) | 1U;
    ep[4] = controller.report_size;
    xhci_trb_t *configure = xhci_command(TRB_CONFIGURE_ENDPOINT,
                                           xhci_dma32(input_context), 0U,
                                           (uint32_t)controller.slot_id << 24U);
    if (!xhci_wait_command(configure, NULL)) {
        printf("USB: xHCI CONFIGURE_ENDPOINT failed\n");
        return false;
    }
    if (!xhci_control(0x00U, 9U, configuration, 0U, 0U, false, NULL) ||
        !xhci_control(0x21U, 0x0BU, 0U, interface, 0U, false, NULL)) {
        printf("USB: xHCI SET_CONFIGURATION/PROTOCOL failed\n");
        return false;
    }
    controller.endpoint_index = 0U;
    controller.endpoint_cycle = true;
    memset(hid_reports, 0, sizeof(hid_reports));
    xhci_queue_interrupt_report();
    xhci_ring_doorbell(controller.slot_id, controller.endpoint_id);
    return true;
}

static bool xhci_enumerate_root_hid(void) {
    uint32_t port = 0U;
    uint32_t port_offset = 0U;
    controller.root_port = 0U;
    for (uint32_t candidate = 1U; candidate <= controller.port_count;
         ++candidate) {
        uint32_t offset = controller.op_base + XHCI_PORTSC_BASE +
            (candidate - 1U) * XHCI_PORTSC_STRIDE;
        uint32_t status = xhci_read(offset);
        if ((status & XHCI_PORT_CCS) != 0U) {
            controller.root_port = candidate;
            port_offset = offset;
            port = status;
            break;
        }
    }
    if (controller.root_port == 0U) {
        printf("USB: xHCI no connected root-port HID\n");
        return false;
    }
    xhci_write(port_offset,
               (port & ~(XHCI_PORT_CSC | XHCI_PORT_PRC)) | XHCI_PORT_PP);
    xhci_write(port_offset,
               (port & ~(XHCI_PORT_CSC | XHCI_PORT_PRC)) | XHCI_PORT_PP |
               XHCI_PORT_PR);
    uint64_t start = pit_monotonic_ms();
    while (1) {
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= XHCI_TIMEOUT_MS) break;
        port = xhci_read(port_offset);
        if ((port & XHCI_PORT_PED) != 0U) break;
    }
    if ((port & XHCI_PORT_PED) == 0U) return false;
    uint8_t port_speed = (uint8_t)((port & XHCI_PORT_SPEED_MASK) >> 10U);
    controller.max_packet = port_speed >= 4U ? 512U :
                            (port_speed == 3U ? 64U : 8U);
    if (!xhci_address_device(port_speed)) {
        printf("USB: xHCI address-device failed\n");
        return false;
    }
    if (!xhci_configure_boot_hid()) {
        printf("USB: xHCI HID configuration failed\n");
        return false;
    }
    controller.generation++;
    if (controller.generation == 0U) controller.generation = 1U;
    if (controller.hid_protocol == 1U)
        hid_keyboard_attach(controller.generation);
    else if (controller.hid_protocol == 2U)
        hid_mouse_attach(controller.generation);
    else return false;
    controller.online = true;
    return true;
}

static void xhci_irq_handler(void *opaque) {
    (void)opaque;
    if (!controller.online || controller.mmio == NULL) return;
    uint32_t status = xhci_read(controller.op_base + XHCI_USBSTS);
    uint32_t iman = xhci_read(controller.runtime_base + XHCI_RT_IMAN);
    if ((status & XHCI_STS_EINT) == 0U && (iman & XHCI_IMAN_IP) == 0U) return;
    (void)xhci_drain_events(32U, 0U, NULL, NULL);
    /* IMAN.IP is RW1C.  Leaving it asserted keeps the legacy PCI interrupt
     * active even after USBSTS.EINT was acknowledged and can starve the
     * remainder of early boot as soon as the mouse produces reports. */
    xhci_write(controller.runtime_base + XHCI_RT_IMAN,
               XHCI_IMAN_IP | XHCI_IMAN_IE);
    xhci_write(controller.op_base + XHCI_USBSTS, XHCI_STS_EINT);
}

int xhci_probe(pci_device_t *dev) {
    if (dev == NULL || dev->prog_if != 0x30U) return -1;
    uint32_t bar = pci_read_bar(dev, 0U);
    if (bar == 0U || bar == UINT32_MAX || (bar & 1U) != 0U ||
        ((bar & 6U) == 4U && dev->bar[1] != 0U)) {
        printf("USB: xHCI invalid BAR0=%08X BAR1=%08X\n", bar, dev->bar[1]);
        return -1;
    }
    pci_enable_device(dev);
    memset(&controller, 0, sizeof(controller));
    controller.mmio = map_mmio_region((uint64_t)(bar & ~0x0FU), XHCI_MMIO_SIZE);
    if (controller.mmio == NULL) {
        printf("USB: xHCI MMIO mapping failed BAR0=%08X\n", bar);
        return -1;
    }
    uint32_t caplength = xhci_read(0U) & 0xFFU;
    uint32_t hcs1 = xhci_read(4U);
    uint32_t hcs2 = xhci_read(8U);
    uint32_t hcc = xhci_read(0x10U);
    controller.op_base = caplength;
    controller.doorbell_base = xhci_read(0x14U);
    controller.runtime_base = xhci_read(0x18U);
    controller.port_count = (hcs1 >> 24U) & 0xFFU;
    controller.scratchpad_count = ((hcs2 >> 21U) & 0x1FU) |
                                  (((hcs2 >> 27U) & 0x1FU) << 5U);
    if (controller.port_count == 0U || controller.port_count > XHCI_MAX_PORTS ||
        controller.scratchpad_count > XHCI_MAX_SCRATCHPADS ||
        controller.doorbell_base >= XHCI_MMIO_SIZE ||
        controller.runtime_base >= XHCI_MMIO_SIZE ||
        controller.runtime_base >= XHCI_MMIO_SIZE) {
        printf("USB: xHCI capabilities rejected ports=%u scratch=%u db=%X rt=%X\n",
               (unsigned)controller.port_count,
               (unsigned)controller.scratchpad_count,
               (unsigned)controller.doorbell_base,
               (unsigned)controller.runtime_base);
        return -1;
    }
    controller.context_size = (hcc & (1U << 2U)) != 0U ? 64U : 32U;
    if (!xhci_legacy_handoff()) {
        printf("USB: xHCI legacy handoff failed\n");
        return -1;
    }
    if (!xhci_reset_controller()) {
        printf("USB: xHCI controller reset failed\n");
        return -1;
    }
    xhci_prepare_rings();
    if (!xhci_dma_valid(dcbaa, 64U) || !xhci_dma_valid(command_ring, 64U) ||
        !xhci_dma_valid(event_ring, 64U) ||
        !xhci_dma_valid(input_context, 4096U)) {
        printf("USB: xHCI DMA alignment rejected\n");
        return -1;
    }
    pci_set_bus_master(dev->bus, dev->slot, dev->function, 1U);
    uint32_t max_slots = hcs1 & 0xFFU;
    if (max_slots == 0U) max_slots = 1U;
    xhci_write(controller.op_base + XHCI_CONFIG, max_slots > 31U ? 31U : max_slots);
    xhci_write(controller.runtime_base + XHCI_RT_IMAN, 0U);
    xhci_write(controller.op_base + XHCI_USBCMD,
               xhci_read(controller.op_base + XHCI_USBCMD) | XHCI_CMD_RUN);
    if (!xhci_wait_until(controller.op_base + XHCI_USBSTS, XHCI_STS_HCH,
                         0U, XHCI_TIMEOUT_MS)) {
        printf("USB: xHCI controller did not enter run state\n");
        pci_set_bus_master(dev->bus, dev->slot, dev->function, 0U);
        return -1;
    }
    if (!xhci_enumerate_root_hid()) {
        pci_set_bus_master(dev->bus, dev->slot, dev->function, 0U);
        return -1;
    }
    controller.irq = pci_configure_irq(dev);
    if (!pci_irq_is_valid(controller.irq) ||
        register_interrupt_handler(controller.irq, (void *)xhci_irq_handler) != 0) {
        if (controller.hid_protocol == 1U)
            hid_keyboard_detach(controller.generation);
        else if (controller.hid_protocol == 2U)
            hid_mouse_detach(controller.generation);
        controller.online = false;
        xhci_write(controller.op_base + XHCI_USBCMD,
                   xhci_read(controller.op_base + XHCI_USBCMD) &
                   ~(XHCI_CMD_RUN | XHCI_CMD_INTE));
        pci_set_bus_master(dev->bus, dev->slot, dev->function, 0U);
        printf("USB: xHCI IRQ setup failed irq=%u\n", (unsigned)controller.irq);
        return -1;
    }
    /* Clear enumeration-time pending state before unmasking the interrupter. */
    xhci_write(controller.runtime_base + XHCI_RT_IMAN,
               XHCI_IMAN_IP | XHCI_IMAN_IE);
    xhci_write(controller.op_base + XHCI_USBCMD,
               xhci_read(controller.op_base + XHCI_USBCMD) |
               XHCI_CMD_INTE | XHCI_CMD_RUN);
    printf("USB: xHCI HID %s ready port=%u irq=%u\n",
           controller.hid_protocol == 2U ? "mouse" : "keyboard",
           (unsigned)controller.root_port, (unsigned)controller.irq);
    return 0;
}

/* Called from task-context console polling.  IRQ context only records changes. */
void xhci_poll(void) {
    if (controller.mmio == NULL || irq_in_context()) return;
    uint32_t flags = irq_save();
    if (controller.online) (void)xhci_drain_events(16U, 0U, NULL, NULL);
    irq_restore(flags);
}
