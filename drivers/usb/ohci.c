/**
 * @file drivers/usb/ohci.c
 * @brief Begrenzter OHCI-Host fuer USB-1.1-HID-Boot-Tastatur und -Maus.
 *
 * Der Controller wird fail-closed in den operativen Zustand gebracht, ein
 * Geraet an den verbundenen Root-Ports wird ueber Control-Transfers
 * enumeriert und sein Interrupt-IN-Boot-Endpoint periodisch gepollt.  Alle
 * DMA-Objekte sind statisch, 32-Bit-adressierbar und ausgerichtet; jede
 * Wartezeit ist an eine monotone Deadline oder eine feste Iterationsschranke
 * gebunden.  Reports werden an die gemeinsame HID-Klassenschicht uebergeben.
 */
#include "ohci.h"
#include "hid_kb.h"
#include "hid_mouse.h"
#include "arch/x86/include/interrupt.h"
#include "drivers/char/kb.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OHCI_MMIO_SIZE 0x1000U
#define OHCI_MAX_PORTS 15U
#define OHCI_HANDOFF_TIMEOUT_MS 100U
#define OHCI_RESET_TIMEOUT_MS 100U
#define OHCI_PORT_POWER_SETTLE_MS 100U
#define OHCI_PORT_RESET_TIMEOUT_MS 100U
#define OHCI_CONTROL_TIMEOUT_MS 200U
#define OHCI_SET_ADDRESS_SETTLE_MS 10U
#define OHCI_HID_SLOT_COUNT 2U
#define OHCI_HID_SLOT_KEYBOARD 0U
#define OHCI_HID_SLOT_MOUSE 1U

/* Operational register offsets (byte offsets into BAR0 MMIO). */
#define OHCI_HC_REVISION 0x00U
#define OHCI_HC_CONTROL 0x04U
#define OHCI_HC_COMMAND_STATUS 0x08U
#define OHCI_HC_INTERRUPT_STATUS 0x0CU
#define OHCI_HC_INTERRUPT_ENABLE 0x10U
#define OHCI_HC_INTERRUPT_DISABLE 0x14U
#define OHCI_HC_HCCA 0x18U
#define OHCI_HC_CONTROL_HEAD_ED 0x20U
#define OHCI_HC_CONTROL_CURRENT_ED 0x24U
#define OHCI_HC_BULK_HEAD_ED 0x28U
#define OHCI_HC_DONE_HEAD 0x30U
#define OHCI_HC_FM_INTERVAL 0x34U
#define OHCI_HC_PERIODIC_START 0x40U
#define OHCI_HC_RH_DESCRIPTOR_A 0x48U
#define OHCI_HC_RH_DESCRIPTOR_B 0x4CU
#define OHCI_HC_RH_STATUS 0x50U
#define OHCI_HC_RH_PORT_STATUS 0x54U

/* HcControl. */
#define OHCI_CTRL_CBSR_MASK 0x00000003U
#define OHCI_CTRL_PLE (1U << 2U)
#define OHCI_CTRL_IE (1U << 3U)
#define OHCI_CTRL_CLE (1U << 4U)
#define OHCI_CTRL_BLE (1U << 5U)
#define OHCI_CTRL_HCFS_SHIFT 6U
#define OHCI_CTRL_HCFS_MASK (3U << OHCI_CTRL_HCFS_SHIFT)
#define OHCI_CTRL_HCFS_OPERATIONAL (2U << OHCI_CTRL_HCFS_SHIFT)
#define OHCI_CTRL_IR (1U << 8U)

/* HcCommandStatus. */
#define OHCI_CMD_HCR (1U << 0U)
#define OHCI_CMD_CLF (1U << 1U)
#define OHCI_CMD_BLF (1U << 2U)
#define OHCI_CMD_OCR (1U << 3U)

/* HcInterruptStatus / Enable / Disable. */
#define OHCI_INT_WDH (1U << 1U)
#define OHCI_INT_RHSC (1U << 6U)
#define OHCI_INT_MIE (1U << 31U)
#define OHCI_INT_ALL 0xC000007FU

/* HcFmInterval. */
#define OHCI_FM_INTERVAL_FI_MASK 0x00003FFFU
#define OHCI_FM_INTERVAL_FIT (1U << 31U)
#define OHCI_FM_INTERVAL_FSMPS_SHIFT 16U

/* HcRhDescriptorA. */
#define OHCI_RH_NDP_MASK 0x000000FFU
#define OHCI_RH_PSM (1U << 8U)
#define OHCI_RH_NPS (1U << 9U)
#define OHCI_RH_POTPGT_SHIFT 24U

/* HcRhStatus. */
#define OHCI_RH_STATUS_LPSC (1U << 16U)

/* HcRhPortStatus. */
#define OHCI_PORT_CCS (1U << 0U)
#define OHCI_PORT_PES (1U << 1U)
#define OHCI_PORT_PRS (1U << 4U)
#define OHCI_PORT_PPS (1U << 8U)
#define OHCI_PORT_LSDA (1U << 9U)
#define OHCI_PORT_CSC (1U << 16U)
#define OHCI_PORT_PRSC (1U << 20U)

/* Endpoint descriptor control fields. */
#define OHCI_ED_EN_SHIFT 7U
#define OHCI_ED_DIR_TD (0U << 11U)
#define OHCI_ED_DIR_IN (2U << 11U)
#define OHCI_ED_SPEED_LOW (1U << 13U)
#define OHCI_ED_SKIP (1U << 14U)
#define OHCI_ED_MPS_SHIFT 16U
#define OHCI_ED_HALTED 1U
#define OHCI_ED_HEAD_CARRY 2U
#define OHCI_ED_HEAD_MASK 0xFFFFFFF0U

/* Transfer descriptor flag fields. */
#define OHCI_TD_R (1U << 18U)
#define OHCI_TD_DP_SETUP (0U << 19U)
#define OHCI_TD_DP_OUT (1U << 19U)
#define OHCI_TD_DP_IN (2U << 19U)
#define OHCI_TD_DI_NONE (7U << 21U)
#define OHCI_TD_T_DATA0 (2U << 24U)
#define OHCI_TD_T_DATA1 (3U << 24U)
#define OHCI_TD_CC_SHIFT 28U
#define OHCI_TD_CC_MASK (0xFU << OHCI_TD_CC_SHIFT)
#define OHCI_TD_CC_NO_ERROR 0x0U
#define OHCI_TD_CC_DATA_UNDERRUN 0x9U

typedef struct {
    volatile uint32_t control;
    volatile uint32_t tail;
    volatile uint32_t head;
    volatile uint32_t next;
} ohci_ed_t;

typedef struct {
    volatile uint32_t flags;
    volatile uint32_t cbp;
    volatile uint32_t next;
    volatile uint32_t be;
} ohci_td_t;

/* Every object below is handed to hardware only after validation. */
static uint8_t hcca[256] __attribute__((aligned(256)));
static ohci_ed_t control_ed __attribute__((aligned(16)));
static ohci_ed_t interrupt_eds[OHCI_HID_SLOT_COUNT]
    __attribute__((aligned(16)));
static ohci_td_t control_tds[4] __attribute__((aligned(16)));
static ohci_td_t interrupt_tds[OHCI_HID_SLOT_COUNT][2]
    __attribute__((aligned(16)));
static uint8_t setup_buffer[8] __attribute__((aligned(16)));
static uint8_t data_buffer[256] __attribute__((aligned(16)));
static uint8_t report_buffers[OHCI_HID_SLOT_COUNT][8]
    __attribute__((aligned(16)));

typedef struct {
    bool online;
    uint8_t address;
    uint8_t protocol;
    uint8_t endpoint;
    uint8_t interface;
    uint8_t report_size;
    uint8_t speed_low;
    uint16_t ep0_max;
    uint32_t generation;
    uint32_t root_port;
} ohci_hid_t;

typedef struct {
    volatile uint32_t *mmio;
    pci_device_t *device;
    uint32_t port_count;
    uint32_t selected_port;
    bool operational;
} ohci_state_t;

static ohci_state_t controller;
static ohci_hid_t enumerating_hid;
static ohci_hid_t hid_devices[OHCI_HID_SLOT_COUNT];
static uint32_t generation_counter;
static ohci_diagnostics_t diagnostics = {
    .version = OHCI_DIAGNOSTICS_VERSION,
    .struct_size = sizeof(ohci_diagnostics_t),
    .state = OHCI_DIAG_NOT_PROBED
};

static uint32_t ohci_dma32(const void *address) {
    uintptr_t value = (uintptr_t)address;
    if (value > UINT32_MAX) return 0U;
    return (uint32_t)value;
}

static bool ohci_dma_valid(const void *address, uint32_t alignment) {
    uint32_t value = ohci_dma32(address);
    return value != 0U && (value & (alignment - 1U)) == 0U;
}

static bool ohci_all_dma_valid(void) {
    return ohci_dma_valid(hcca, 256U) &&
        ohci_dma_valid(&control_ed, 16U) &&
        ohci_dma_valid(interrupt_eds, 16U) &&
        ohci_dma_valid(control_tds, 16U) &&
        ohci_dma_valid(interrupt_tds, 16U) &&
        ohci_dma_valid(setup_buffer, 16U) &&
        ohci_dma_valid(data_buffer, 16U) &&
        ohci_dma_valid(report_buffers, 16U);
}

static uint32_t ohci_read(uint32_t offset) {
    return controller.mmio[offset >> 2U];
}

static void ohci_write(uint32_t offset, uint32_t value) {
    controller.mmio[offset >> 2U] = value;
}

static uint32_t ohci_port_offset(uint32_t port) {
    return OHCI_HC_RH_PORT_STATUS + (port - 1U) * 4U;
}

static bool ohci_deadline_expired(uint64_t start, uint32_t timeout_ms) {
    uint64_t now = pit_monotonic_ms();
    return now < start || now - start >= timeout_ms;
}

static bool ohci_wait_next_tick(uint64_t start, uint32_t timeout_ms) {
    if (ohci_deadline_expired(start, timeout_ms) || !irq_enabled() ||
        irq_in_context())
        return false;
    cpu_halt();
    return true;
}

static bool ohci_settle(uint32_t timeout_ms) {
    uint64_t start = pit_monotonic_ms();
    while (!ohci_deadline_expired(start, timeout_ms)) {
        if (!ohci_wait_next_tick(start, timeout_ms)) break;
    }
    return ohci_deadline_expired(start, timeout_ms);
}

/* Reclaim the controller from BIOS/SMM ownership before reprogramming it. */
static bool ohci_acquire_ownership(void) {
    if ((ohci_read(OHCI_HC_CONTROL) & OHCI_CTRL_IR) == 0U) return true;
    ohci_write(OHCI_HC_COMMAND_STATUS, OHCI_CMD_OCR);
    uint64_t start = pit_monotonic_ms();
    while ((ohci_read(OHCI_HC_CONTROL) & OHCI_CTRL_IR) != 0U) {
        if (ohci_deadline_expired(start, OHCI_HANDOFF_TIMEOUT_MS)) return false;
        if (!ohci_wait_next_tick(start, OHCI_HANDOFF_TIMEOUT_MS)) return false;
    }
    return true;
}

static bool ohci_reset_controller(void) {
    const uint32_t fm_interval = ohci_read(OHCI_HC_FM_INTERVAL);
    ohci_write(OHCI_HC_INTERRUPT_DISABLE, OHCI_INT_MIE);

    ohci_write(OHCI_HC_COMMAND_STATUS, OHCI_CMD_HCR);
    bool cleared = false;
    uint64_t reset_start = pit_monotonic_ms();
    while (!ohci_deadline_expired(reset_start, OHCI_RESET_TIMEOUT_MS)) {
        if ((ohci_read(OHCI_HC_COMMAND_STATUS) & OHCI_CMD_HCR) == 0U) {
            cleared = true;
            break;
        }
        if (!ohci_wait_next_tick(reset_start, OHCI_RESET_TIMEOUT_MS)) break;
    }
    if (!cleared) return false;

    /* SUSPENDED after reset; publish the validated HCCA and empty control,
     * bulk and periodic schedules, then reach operational within 2ms. */
    ohci_write(OHCI_HC_HCCA, ohci_dma32(hcca));
    ohci_write(OHCI_HC_CONTROL_HEAD_ED, 0U);
    ohci_write(OHCI_HC_CONTROL_CURRENT_ED, 0U);
    ohci_write(OHCI_HC_BULK_HEAD_ED, 0U);

    uint32_t fi = fm_interval & OHCI_FM_INTERVAL_FI_MASK;
    if (fi < 210U) fi = 0x2EDFU;
    uint32_t fsmps = ((fi - 210U) * 6U) / 7U;
    uint32_t restored = (fm_interval & OHCI_FM_INTERVAL_FIT) ^
        OHCI_FM_INTERVAL_FIT;
    restored |= (fsmps << OHCI_FM_INTERVAL_FSMPS_SHIFT) | fi;
    ohci_write(OHCI_HC_FM_INTERVAL, restored);
    ohci_write(OHCI_HC_PERIODIC_START, (fi * 9U) / 10U);
    ohci_write(OHCI_HC_INTERRUPT_STATUS, OHCI_INT_ALL);

    uint32_t control = ohci_read(OHCI_HC_CONTROL);
    control &= ~(OHCI_CTRL_HCFS_MASK | OHCI_CTRL_CBSR_MASK);
    control |= OHCI_CTRL_HCFS_OPERATIONAL | OHCI_CTRL_PLE | OHCI_CTRL_CLE;
    ohci_write(OHCI_HC_CONTROL, control);
    return (ohci_read(OHCI_HC_CONTROL) & OHCI_CTRL_HCFS_MASK) ==
        OHCI_CTRL_HCFS_OPERATIONAL;
}

static bool ohci_power_root_hub(void) {
    uint32_t descriptor_a = ohci_read(OHCI_HC_RH_DESCRIPTOR_A);
    uint32_t ports = descriptor_a & OHCI_RH_NDP_MASK;
    if (ports > OHCI_MAX_PORTS) ports = OHCI_MAX_PORTS;
    controller.port_count = ports;
    diagnostics.port_count = ports;
    if (ports == 0U || (descriptor_a & OHCI_RH_NPS) != 0U) return ports != 0U;

    if ((descriptor_a & OHCI_RH_PSM) != 0U) {
        uint32_t controllable = ohci_read(OHCI_HC_RH_DESCRIPTOR_B) >> 16U;
        for (uint32_t port = 1U; port <= ports; ++port) {
            if ((controllable & (1U << (port - 1U))) != 0U)
                ohci_write(ohci_port_offset(port), OHCI_PORT_PPS);
        }
    } else {
        ohci_write(OHCI_HC_RH_STATUS, OHCI_RH_STATUS_LPSC);
    }
    uint32_t settle = ((descriptor_a >> OHCI_RH_POTPGT_SHIFT) & 0xFFU) * 2U;
    if (settle < OHCI_PORT_POWER_SETTLE_MS) settle = OHCI_PORT_POWER_SETTLE_MS;
    return ohci_settle(settle);
}

static bool ohci_reset_port(uint32_t port) {
    diagnostics.reset_attempts++;
    ohci_write(ohci_port_offset(port), OHCI_PORT_PRS);
    uint64_t start = pit_monotonic_ms();
    while (!ohci_deadline_expired(start, OHCI_PORT_RESET_TIMEOUT_MS)) {
        uint32_t status = ohci_read(ohci_port_offset(port));
        if ((status & OHCI_PORT_PRSC) != 0U) {
            ohci_write(ohci_port_offset(port), OHCI_PORT_PRSC);
            return (ohci_read(ohci_port_offset(port)) & OHCI_PORT_PES) != 0U;
        }
        if (!ohci_wait_next_tick(start, OHCI_PORT_RESET_TIMEOUT_MS)) break;
    }
    return false;
}

static void ohci_detect_ports(void) {
    controller.selected_port = 0U;
    diagnostics.connected_ports = 0U;
    for (uint32_t port = 1U; port <= controller.port_count; ++port) {
        if ((ohci_read(ohci_port_offset(port)) & OHCI_PORT_CCS) == 0U)
            continue;
        diagnostics.connected_ports |= 1U << (port - 1U);
    }
    diagnostics.selected_port = 0U;
}

static void ohci_td_init(ohci_td_t *td, uint32_t flags, uint32_t cbp,
                         uint32_t length, uint32_t next) {
    td->flags = flags | OHCI_TD_CC_MASK;
    td->cbp = cbp;
    td->be = cbp != 0U ? cbp + length - 1U : 0U;
    td->next = next;
}

static bool ohci_wait_ed_done(ohci_ed_t *ed) {
    uint64_t start = pit_monotonic_ms();
    for (;;) {
        __sync_synchronize();
        uint32_t head = ed->head;
        if ((head & OHCI_ED_HALTED) != 0U) return false;
        if ((head & OHCI_ED_HEAD_MASK) == (ed->tail & OHCI_ED_HEAD_MASK))
            return true;
        if (ohci_deadline_expired(start, OHCI_CONTROL_TIMEOUT_MS)) return false;
        if (!ohci_wait_next_tick(start, OHCI_CONTROL_TIMEOUT_MS)) return false;
    }
}

static bool ohci_td_ok(const ohci_td_t *td) {
    uint32_t cc = (td->flags & OHCI_TD_CC_MASK) >> OHCI_TD_CC_SHIFT;
    return cc == OHCI_TD_CC_NO_ERROR || cc == OHCI_TD_CC_DATA_UNDERRUN;
}

/* Run one USB control transfer through the control ED and wait bounded. */
static bool ohci_control_transfer(const uint8_t *setup, uint8_t *data,
                                  uint16_t length, bool in) {
    if (length > sizeof(data_buffer)) return false;
    memcpy(setup_buffer, setup, 8U);
    ohci_td_t *setup_td = &control_tds[0];
    ohci_td_t *data_td = &control_tds[1];
    ohci_td_t *status_td = &control_tds[2];
    ohci_td_t *dummy_td = &control_tds[3];

    if (length > 0U && !in) memcpy(data_buffer, data, length);
    else if (length > 0U) memset(data_buffer, 0, length);

    ohci_td_init(dummy_td, 0U, 0U, 0U, 0U);
    ohci_td_init(status_td,
                 (in ? OHCI_TD_DP_OUT : OHCI_TD_DP_IN) | OHCI_TD_T_DATA1 |
                     OHCI_TD_DI_NONE,
                 0U, 0U, ohci_dma32(dummy_td));
    if (length > 0U) {
        ohci_td_init(data_td,
                     (in ? (OHCI_TD_DP_IN | OHCI_TD_R) : OHCI_TD_DP_OUT) |
                         OHCI_TD_T_DATA1 | OHCI_TD_DI_NONE,
                     ohci_dma32(data_buffer), length, ohci_dma32(status_td));
        ohci_td_init(setup_td, OHCI_TD_DP_SETUP | OHCI_TD_T_DATA0 |
                         OHCI_TD_DI_NONE,
                     ohci_dma32(setup_buffer), 8U, ohci_dma32(data_td));
    } else {
        ohci_td_init(setup_td, OHCI_TD_DP_SETUP | OHCI_TD_T_DATA0 |
                         OHCI_TD_DI_NONE,
                     ohci_dma32(setup_buffer), 8U, ohci_dma32(status_td));
    }

    control_ed.control = (uint32_t)enumerating_hid.address | OHCI_ED_DIR_TD |
        (enumerating_hid.speed_low ? OHCI_ED_SPEED_LOW : 0U) |
        ((uint32_t)enumerating_hid.ep0_max << OHCI_ED_MPS_SHIFT);
    control_ed.tail = ohci_dma32(dummy_td);
    control_ed.head = ohci_dma32(setup_td);
    control_ed.next = 0U;
    __sync_synchronize();

    ohci_write(OHCI_HC_CONTROL_CURRENT_ED, 0U);
    ohci_write(OHCI_HC_CONTROL_HEAD_ED, ohci_dma32(&control_ed));
    ohci_write(OHCI_HC_COMMAND_STATUS, OHCI_CMD_CLF);

    bool ok = ohci_wait_ed_done(&control_ed);
    ohci_write(OHCI_HC_CONTROL_HEAD_ED, 0U);
    ohci_write(OHCI_HC_CONTROL_CURRENT_ED, 0U);
    if (!ok) return false;
    if (!ohci_td_ok(setup_td) || !ohci_td_ok(status_td) ||
        (length > 0U && !ohci_td_ok(data_td)))
        return false;
    if (in && length > 0U && data != NULL) memcpy(data, data_buffer, length);
    return true;
}

/* Locate the boot HID interface and its interrupt IN endpoint. */
static bool ohci_parse_boot_hid(uint16_t total) {
    uint8_t current_protocol = 0U;
    uint8_t current_interface = 0U;
    for (uint32_t offset = 0U; offset + 2U <= total;) {
        uint8_t blen = data_buffer[offset];
        uint8_t btype = data_buffer[offset + 1U];
        if (blen < 2U || offset + blen > total) break;
        if (btype == 4U && blen >= 9U) {
            uint8_t cls = data_buffer[offset + 5U];
            uint8_t sub = data_buffer[offset + 6U];
            uint8_t proto = data_buffer[offset + 7U];
            if (cls == 3U && sub == 1U && (proto == 1U || proto == 2U)) {
                current_protocol = proto;
                current_interface = data_buffer[offset + 2U];
            } else {
                current_protocol = 0U;
            }
        } else if (btype == 5U && blen >= 7U && current_protocol != 0U) {
            uint8_t address = data_buffer[offset + 2U];
            uint8_t attributes = data_buffer[offset + 3U];
            uint16_t packet = (uint16_t)data_buffer[offset + 4U] |
                ((uint16_t)data_buffer[offset + 5U] << 8U);
            if ((attributes & 0x03U) == 0x03U && (address & 0x80U) != 0U) {
                enumerating_hid.protocol = current_protocol;
                enumerating_hid.interface = current_interface;
                enumerating_hid.endpoint = address & 0x0FU;
                enumerating_hid.report_size = packet == 0U || packet > 8U
                    ? 8U : (uint8_t)packet;
                return true;
            }
        }
        offset += blen;
    }
    return false;
}

/* Queue one HID interrupt-IN transfer, preserving its data-toggle carry. */
static void ohci_arm_interrupt(uint32_t slot, uint32_t carry) {
    ohci_hid_t *device = &hid_devices[slot];
    ohci_ed_t *ed = &interrupt_eds[slot];
    ohci_td_t *td = &interrupt_tds[slot][0];
    ohci_td_t *dummy = &interrupt_tds[slot][1];
    uint8_t *report = report_buffers[slot];
    memset(report, 0, sizeof(report_buffers[slot]));
    ohci_td_init(dummy, 0U, 0U, 0U, 0U);
    ohci_td_init(td, OHCI_TD_DP_IN | OHCI_TD_R | OHCI_TD_DI_NONE,
                 ohci_dma32(report), device->report_size,
                 ohci_dma32(dummy));

    ed->control = (uint32_t)device->address |
        ((uint32_t)device->endpoint << OHCI_ED_EN_SHIFT) | OHCI_ED_DIR_IN |
        (device->speed_low ? OHCI_ED_SPEED_LOW : 0U) |
        ((uint32_t)device->report_size << OHCI_ED_MPS_SHIFT);
    ed->tail = ohci_dma32(dummy);
    __sync_synchronize();
    ed->head = ohci_dma32(td) | (carry & OHCI_ED_HEAD_CARRY);
    __sync_synchronize();
}

/* Build one bounded periodic chain containing every active boot HID. */
static void ohci_link_interrupt_eds(void) {
    volatile uint32_t *interrupt_table = (volatile uint32_t *)hcca;
    uint32_t chain = 0U;
    for (uint32_t cursor = OHCI_HID_SLOT_COUNT; cursor > 0U; --cursor) {
        uint32_t slot = cursor - 1U;
        if (!hid_devices[slot].online) continue;
        interrupt_eds[slot].next = chain;
        chain = ohci_dma32(&interrupt_eds[slot]);
    }
    for (uint32_t slot = 0U; slot < 32U; ++slot)
        interrupt_table[slot] = chain;
    __sync_synchronize();
}

static void ohci_update_ready_diagnostics(void) {
    const ohci_hid_t *keyboard = &hid_devices[OHCI_HID_SLOT_KEYBOARD];
    const ohci_hid_t *mouse = &hid_devices[OHCI_HID_SLOT_MOUSE];
    if (keyboard->online && mouse->online)
        diagnostics.state = OHCI_DIAG_KEYBOARD_MOUSE_READY;
    else if (keyboard->online)
        diagnostics.state = OHCI_DIAG_KEYBOARD_READY;
    else if (mouse->online)
        diagnostics.state = OHCI_DIAG_MOUSE_READY;
    else
        diagnostics.state = OHCI_DIAG_DISCONNECTED;

    const ohci_hid_t *selected = keyboard->online ? keyboard :
        (mouse->online ? mouse : NULL);
    diagnostics.selected_port = selected != NULL ? selected->root_port : 0U;
    diagnostics.hid_protocol = selected != NULL ? selected->protocol : 0U;
    diagnostics.endpoint = selected != NULL ? selected->endpoint : 0U;
    diagnostics.report_size = selected != NULL ? selected->report_size : 0U;
    diagnostics.keyboard_port = keyboard->online ? keyboard->root_port : 0U;
    diagnostics.keyboard_endpoint = keyboard->online
        ? keyboard->endpoint : 0U;
    diagnostics.mouse_port = mouse->online ? mouse->root_port : 0U;
    diagnostics.mouse_endpoint = mouse->online ? mouse->endpoint : 0U;
}

static bool ohci_hid_enumerate(uint32_t port) {
    memset(&enumerating_hid, 0, sizeof(enumerating_hid));
    enumerating_hid.root_port = port;
    enumerating_hid.ep0_max = 8U;
    enumerating_hid.speed_low =
        (ohci_read(ohci_port_offset(port)) & OHCI_PORT_LSDA) != 0U ? 1U : 0U;

    static const uint8_t get_device8[8] =
        {0x80U, 0x06U, 0x00U, 0x01U, 0x00U, 0x00U, 0x08U, 0x00U};
    if (!ohci_control_transfer(get_device8, data_buffer, 8U, true))
        return false;
    if (data_buffer[7] != 0U) enumerating_hid.ep0_max = data_buffer[7];

    /* Use a distinct legal USB address for every tested root port.  A device
     * that turns out not to be a boot HID can therefore not collide with a
     * later candidate while the bounded port scan continues. */
    const uint8_t set_address[8] =
        {0x00U, 0x05U, (uint8_t)port, 0x00U, 0x00U, 0x00U,
         0x00U, 0x00U};
    if (!ohci_control_transfer(set_address, NULL, 0U, false)) return false;
    enumerating_hid.address = (uint8_t)port;
    if (!ohci_settle(OHCI_SET_ADDRESS_SETTLE_MS)) return false;

    static const uint8_t get_config9[8] =
        {0x80U, 0x06U, 0x00U, 0x02U, 0x00U, 0x00U, 0x09U, 0x00U};
    if (!ohci_control_transfer(get_config9, data_buffer, 9U, true))
        return false;
    uint16_t total = (uint16_t)data_buffer[2] |
        ((uint16_t)data_buffer[3] << 8U);
    if (total < 9U) return false;
    if (total > sizeof(data_buffer)) total = (uint16_t)sizeof(data_buffer);

    const uint8_t get_config[8] =
        {0x80U, 0x06U, 0x00U, 0x02U, 0x00U, 0x00U,
         (uint8_t)(total & 0xFFU), (uint8_t)(total >> 8U)};
    if (!ohci_control_transfer(get_config, data_buffer, total, true))
        return false;
    uint8_t configuration_value = data_buffer[5];
    if (!ohci_parse_boot_hid(total)) return false;
    uint32_t slot = enumerating_hid.protocol == 1U
        ? OHCI_HID_SLOT_KEYBOARD : OHCI_HID_SLOT_MOUSE;
    if (hid_devices[slot].online) return false;

    const uint8_t set_configuration[8] =
        {0x00U, 0x09U, configuration_value, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    if (!ohci_control_transfer(set_configuration, NULL, 0U, false))
        return false;
    const uint8_t set_protocol[8] =
        {0x21U, 0x0BU, 0x00U, 0x00U, enumerating_hid.interface,
         0x00U, 0x00U, 0x00U};
    if (!ohci_control_transfer(set_protocol, NULL, 0U, false)) return false;
    const uint8_t set_idle[8] =
        {0x21U, 0x0AU, 0x00U, 0x00U, enumerating_hid.interface,
         0x00U, 0x00U, 0x00U};
    (void)ohci_control_transfer(set_idle, NULL, 0U, false);

    enumerating_hid.generation = ++generation_counter;
    enumerating_hid.online = true;
    hid_devices[slot] = enumerating_hid;
    if (enumerating_hid.protocol == 1U)
        hid_keyboard_attach(enumerating_hid.generation);
    else
        hid_mouse_attach(enumerating_hid.generation);
    ohci_arm_interrupt(slot, 0U);
    ohci_link_interrupt_eds();
    ohci_update_ready_diagnostics();
    return true;
}

static void ohci_detach_hid(uint32_t slot) {
    ohci_hid_t *device = &hid_devices[slot];
    if (!device->online) return;
    if (device->protocol == 1U) hid_keyboard_detach(device->generation);
    else hid_mouse_detach(device->generation);
    device->online = false;
    ohci_link_interrupt_eds();
    ohci_update_ready_diagnostics();
}

static void ohci_release_controller(void) {
    if (controller.mmio != NULL) {
        uint32_t control = ohci_read(OHCI_HC_CONTROL);
        control &= ~(OHCI_CTRL_PLE | OHCI_CTRL_IE | OHCI_CTRL_CLE |
                     OHCI_CTRL_BLE);
        ohci_write(OHCI_HC_CONTROL, control);
        ohci_write(OHCI_HC_CONTROL_HEAD_ED, 0U);
        ohci_write(OHCI_HC_BULK_HEAD_ED, 0U);
        ohci_write(OHCI_HC_INTERRUPT_DISABLE, OHCI_INT_ALL);
    }
    if (controller.device != NULL)
        (void)pci_set_bus_master_verified(controller.device, false);
    controller.operational = false;
    controller.device = NULL;
}

int ohci_probe(pci_device_t *dev) {
    if (dev == NULL || dev->prog_if != 0x10U) return -1;

    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.version = OHCI_DIAGNOSTICS_VERSION;
    diagnostics.struct_size = sizeof(diagnostics);
    diagnostics.state = OHCI_DIAG_PROBING;
    diagnostics.bus = dev->bus;
    diagnostics.slot = dev->slot;
    diagnostics.function = dev->function;
    diagnostics.vendor_id = dev->vendor_id;
    diagnostics.device_id = dev->device_id;

    uint32_t bar = pci_read_bar(dev, 0U);
    if (bar == 0U || bar == UINT32_MAX || (bar & 1U) != 0U) {
        printf("USB: OHCI invalid BAR0=%08X\n", bar);
        diagnostics.state = OHCI_DIAG_INVALID_BAR;
        return -1;
    }
    pci_enable_device(dev);
    memset(&controller, 0, sizeof(controller));
    memset(&enumerating_hid, 0, sizeof(enumerating_hid));
    memset(hid_devices, 0, sizeof(hid_devices));
    memset(interrupt_eds, 0, sizeof(interrupt_eds));
    memset(interrupt_tds, 0, sizeof(interrupt_tds));
    memset(report_buffers, 0, sizeof(report_buffers));
    controller.device = dev;
    controller.mmio = map_mmio_region((uint64_t)(bar & ~0x0FU),
                                       OHCI_MMIO_SIZE);
    if (controller.mmio == NULL) {
        printf("USB: OHCI MMIO mapping failed BAR0=%08X\n", bar);
        diagnostics.state = OHCI_DIAG_MMIO_FAILED;
        return -1;
    }

    uint32_t revision = ohci_read(OHCI_HC_REVISION) & 0xFFU;
    diagnostics.revision = revision;
    if (revision != 0x10U) {
        printf("USB: OHCI unsupported revision 0x%02X\n", (unsigned)revision);
        diagnostics.state = OHCI_DIAG_UNSUPPORTED_REVISION;
        return -1;
    }
    if (!ohci_all_dma_valid()) {
        printf("USB: OHCI DMA alignment rejected\n");
        diagnostics.state = OHCI_DIAG_DMA_REJECTED;
        return -1;
    }
    memset(hcca, 0, sizeof(hcca));

    if (!ohci_acquire_ownership()) {
        printf("USB: OHCI SMM ownership handoff failed\n");
        diagnostics.state = OHCI_DIAG_HANDOFF_FAILED;
        return -1;
    }
    if (!pci_set_intx_disabled_verified(dev, true)) {
        printf("USB: OHCI PCI INTx disable failed\n");
        diagnostics.state = OHCI_DIAG_DMA_REJECTED;
        ohci_release_controller();
        return -1;
    }
    if (!pci_set_bus_master_verified(dev, true)) {
        printf("USB: OHCI PCI command enable failed\n");
        diagnostics.state = OHCI_DIAG_DMA_REJECTED;
        ohci_release_controller();
        return -1;
    }
    if (!ohci_reset_controller()) {
        printf("USB: OHCI reset/start failed\n");
        ohci_release_controller();
        diagnostics.state = OHCI_DIAG_RESET_FAILED;
        return -1;
    }
    controller.operational = true;

    if (!ohci_power_root_hub()) {
        printf("USB: OHCI root-hub power transition failed\n");
        diagnostics.state = OHCI_DIAG_NO_ROOT_PORTS;
        ohci_release_controller();
        return -1;
    }
    if (controller.port_count == 0U) {
        printf("USB: OHCI reports no root-hub ports\n");
        diagnostics.state = OHCI_DIAG_NO_ROOT_PORTS;
        ohci_release_controller();
        return -1;
    }
    ohci_detect_ports();
    printf("USB: OHCI operational ports=%u connected=%u\n",
           (unsigned)controller.port_count,
           (unsigned)diagnostics.connected_ports);
    if (diagnostics.connected_ports == 0U) {
        diagnostics.state = OHCI_DIAG_NO_CONNECTED_PORT;
        ohci_release_controller();
        return OHCI_PROBE_NO_SUPPORTED_DEVICE;
    }
    bool reset_succeeded = false;
    bool hid_ready = false;
    for (uint32_t port = 1U; port <= controller.port_count; ++port) {
        if ((diagnostics.connected_ports & (1U << (port - 1U))) == 0U)
            continue;
        controller.selected_port = port;
        diagnostics.selected_port = port;
        if (!ohci_reset_port(port)) {
            printf("USB: OHCI port %u reset failed\n", (unsigned)port);
            continue;
        }
        reset_succeeded = true;
        if (!ohci_settle(OHCI_SET_ADDRESS_SETTLE_MS)) break;
        if (ohci_hid_enumerate(port)) {
            hid_ready = true;
            if (hid_devices[OHCI_HID_SLOT_KEYBOARD].online &&
                hid_devices[OHCI_HID_SLOT_MOUSE].online)
                break;
            continue;
        }
        printf("USB: OHCI no supported boot HID on port %u\n",
               (unsigned)port);
    }
    if (!hid_ready) {
        diagnostics.state = reset_succeeded
            ? OHCI_DIAG_OPERATIONAL : OHCI_DIAG_PORT_RESET_FAILED;
        ohci_release_controller();
        return OHCI_PROBE_NO_SUPPORTED_DEVICE;
    }
    ohci_update_ready_diagnostics();
    for (uint32_t slot = 0U; slot < OHCI_HID_SLOT_COUNT; ++slot) {
        const ohci_hid_t *device = &hid_devices[slot];
        if (!device->online) continue;
        printf("USB: OHCI %s ready port=%u endpoint=%u report=%u\n",
               device->protocol == 1U ? "keyboard" : "mouse",
               (unsigned)device->root_port, (unsigned)device->endpoint,
               (unsigned)device->report_size);
    }
    return OHCI_PROBE_READY;
}

void ohci_poll(void) {
    if (!controller.operational) return;

    for (uint32_t slot = 0U; slot < OHCI_HID_SLOT_COUNT; ++slot) {
        ohci_hid_t *device = &hid_devices[slot];
        if (!device->online) continue;
        if ((ohci_read(ohci_port_offset(device->root_port)) &
             OHCI_PORT_CCS) == 0U) {
            ohci_detach_hid(slot);
            continue;
        }

        __sync_synchronize();
        ohci_ed_t *ed = &interrupt_eds[slot];
        uint32_t head = ed->head;
        if ((head & OHCI_ED_HALTED) != 0U) {
            /* Recover a stalled endpoint while preserving data-toggle carry. */
            ohci_arm_interrupt(slot, head & OHCI_ED_HEAD_CARRY);
            continue;
        }
        if ((head & OHCI_ED_HEAD_MASK) != (ed->tail & OHCI_ED_HEAD_MASK))
            continue;

        ohci_td_t *td = &interrupt_tds[slot][0];
        uint8_t *report = report_buffers[slot];
        if (ohci_td_ok(td)) {
            uint32_t cbp = td->cbp;
            size_t actual = cbp == 0U
                ? device->report_size
                : (size_t)(cbp - ohci_dma32(report));
            if (actual > device->report_size) actual = device->report_size;
            if (device->protocol == 1U) {
                if (hid_keyboard_report(device->generation, report, actual))
                    diagnostics.keyboard_reports++;
                else
                    diagnostics.rejected_keyboard_reports++;
            } else if (device->protocol == 2U) {
                if (hid_mouse_report(device->generation, report, actual))
                    diagnostics.mouse_reports++;
                else
                    diagnostics.rejected_mouse_reports++;
            }
        }
        ohci_arm_interrupt(slot, head & OHCI_ED_HEAD_CARRY);
    }
}

bool ohci_get_diagnostics(ohci_diagnostics_t *diagnostics_out) {
    if (diagnostics_out == NULL) return false;
    *diagnostics_out = diagnostics;
    return diagnostics.version == OHCI_DIAGNOSTICS_VERSION &&
        diagnostics.struct_size == sizeof(ohci_diagnostics_t);
}
