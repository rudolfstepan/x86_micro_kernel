#include "ahci.h"

#include "arch/x86/mm/paging.h"
#include "drivers/block/ata.h"
#include "lib/libc/string.h"
#include "kernel/time/pit.h"

#define AHCI_MAX_CONTROLLERS 4U
#define AHCI_MMIO_SIZE 0x1100U
#define AHCI_RESET_TIMEOUT_MS 1000U
#define AHCI_RESET_MAX_POLLS 1000000U
#define AHCI_GHC_HR (1U << 0)
#define AHCI_GHC_AE (1U << 31)
#define AHCI_PORT_BASE 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PORT_SIG 0x24U
#define AHCI_PORT_SSTS 0x28U
#define AHCI_SIG_ATA 0x00000101U
#define AHCI_PORT_CLB 0x00U
#define AHCI_PORT_FB 0x08U
#define AHCI_PORT_CMD 0x18U
#define AHCI_PORT_CMD_ST (1U << 0)
#define AHCI_PORT_CMD_FRE (1U << 4)
#define AHCI_PORT_CMD_CR (1U << 15)
#define AHCI_PORT_CMD_FR (1U << 14)
#define AHCI_PORT_IS 0x10U
#define AHCI_PORT_TFD 0x20U
#define AHCI_PORT_CI 0x38U
#define AHCI_PORT_IS_TFES (1U << 30)
#define AHCI_PORT_TFD_BSY (1U << 7)
#define AHCI_PORT_TFD_ERR (1U << 0)
#define AHCI_COMMAND_TIMEOUT_MS 2000U

/* These pools are deliberately fixed and identity-mapped. They are only
 * published after address/alignment validation and remain one-slot-per-port
 * until a bounded command scheduler is implemented. */
static uint8_t command_lists[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS]
    [AHCI_COMMAND_LIST_SIZE] __attribute__((aligned(1024)));
static uint8_t received_fis[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS]
    [AHCI_RECEIVED_FIS_SIZE] __attribute__((aligned(256)));
static uint8_t command_tables[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS]
    [AHCI_COMMAND_TABLE_SIZE] __attribute__((aligned(128)));
static uint8_t identify_buffers[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS][512]
    __attribute__((aligned(2)));

static ahci_controller_info_t controllers[AHCI_MAX_CONTROLLERS];
static size_t controller_count;

static int ahci_bar5(const pci_device_t *device, uint64_t *address) {
    if (device == NULL || address == NULL) return -22;
    uint32_t low = device->bar[5];
    /* ABAR is BAR5 in a type-0 header, so there is no following BAR available
     * as the high half of a 64-bit address. Accept only a 32-bit memory BAR. */
    if (low == 0U || low == 0xFFFFFFFFU || (low & 1U) != 0U ||
        (low & 0x6U) != 0U) return -95;
    *address = (uint64_t)(low & ~0xFU);
    return *address == 0U ? -95 : 0;
}

static uint32_t ahci_read(volatile uint32_t *mmio, uint32_t offset) {
    return mmio[offset / sizeof(uint32_t)];
}

static void ahci_write(volatile uint32_t *mmio, uint32_t offset,
                       uint32_t value) {
    mmio[offset / sizeof(uint32_t)] = value;
}

static bool ahci_reset(volatile uint32_t *mmio) {
    uint64_t start = pit_monotonic_ms();
    ahci_write(mmio, 0x04U, ahci_read(mmio, 0x04U) | AHCI_GHC_HR);
    for (uint32_t poll = 0U; poll < AHCI_RESET_MAX_POLLS; ++poll) {
        if ((ahci_read(mmio, 0x04U) & AHCI_GHC_HR) == 0U) return true;
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= AHCI_RESET_TIMEOUT_MS) break;
    }
    return false;
}

static bool ahci_port_is_sata(volatile uint32_t *mmio, uint32_t port) {
    uint32_t base = AHCI_PORT_BASE + port * AHCI_PORT_STRIDE;
    uint32_t status = ahci_read(mmio, base + AHCI_PORT_SSTS);
    uint32_t det = status & 0x0FU;
    uint32_t ipm = (status >> 8U) & 0x0FU;
    return det == 3U && ipm == 1U &&
           ahci_read(mmio, base + AHCI_PORT_SIG) == AHCI_SIG_ATA;
}

static bool ahci_dma_address_valid(const void *address, size_t length,
                                   size_t alignment) {
    uintptr_t value = (uintptr_t)address;
    return address != NULL && length != 0U &&
           (value & (alignment - 1U)) == 0U &&
           value <= UINT32_MAX && length - 1U <= UINT32_MAX - value;
}

static bool ahci_stop_port(volatile uint32_t *mmio, uint32_t port) {
    uint32_t base = AHCI_PORT_BASE + port * AHCI_PORT_STRIDE;
    uint32_t command = ahci_read(mmio, base + AHCI_PORT_CMD);
    ahci_write(mmio, base + AHCI_PORT_CMD,
               command & ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE));
    uint64_t start = pit_monotonic_ms();
    for (uint32_t poll = 0U; poll < AHCI_RESET_MAX_POLLS; ++poll) {
        uint32_t state = ahci_read(mmio, base + AHCI_PORT_CMD);
        if ((state & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) == 0U)
            return true;
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= AHCI_RESET_TIMEOUT_MS) break;
    }
    return false;
}

static bool ahci_prepare_port(ahci_controller_info_t *controller,
                              size_t controller_index, uint32_t port) {
    if (controller == NULL || controller->mmio == NULL ||
        controller_index >= AHCI_MAX_CONTROLLERS || port >= AHCI_MAX_PORTS ||
        !ahci_stop_port(controller->mmio, port)) return false;
    if (!ahci_dma_address_valid(command_lists[controller_index][port],
            AHCI_COMMAND_LIST_SIZE, 1024U) ||
        !ahci_dma_address_valid(received_fis[controller_index][port],
            AHCI_RECEIVED_FIS_SIZE, 256U) ||
        !ahci_dma_address_valid(command_tables[controller_index][port],
            AHCI_COMMAND_TABLE_SIZE, 128U)) return false;
    memset(command_lists[controller_index][port], 0,
           AHCI_COMMAND_LIST_SIZE);
    memset(received_fis[controller_index][port], 0,
           AHCI_RECEIVED_FIS_SIZE);
    memset(command_tables[controller_index][port], 0,
           AHCI_COMMAND_TABLE_SIZE);
    uint32_t base = AHCI_PORT_BASE + port * AHCI_PORT_STRIDE;
    ahci_write(controller->mmio, base + AHCI_PORT_CLB,
               (uint32_t)(uintptr_t)command_lists[controller_index][port]);
    ahci_write(controller->mmio, base + AHCI_PORT_CLB + 4U, 0U);
    ahci_write(controller->mmio, base + AHCI_PORT_FB,
               (uint32_t)(uintptr_t)received_fis[controller_index][port]);
    ahci_write(controller->mmio, base + AHCI_PORT_FB + 4U, 0U);
    return true;
}

static bool ahci_build_identify_command(ahci_controller_info_t *controller,
                                        size_t controller_index,
                                        uint32_t port) {
    if (controller == NULL || controller->mmio == NULL ||
        controller_index >= AHCI_MAX_CONTROLLERS || port >= AHCI_MAX_PORTS ||
        !ahci_dma_address_valid(identify_buffers[controller_index][port],
                                512U, 2U)) return false;
    ahci_command_header_t *header = (ahci_command_header_t *)
        command_lists[controller_index][port];
    ahci_command_table_t *table = (ahci_command_table_t *)
        command_tables[controller_index][port];
    ahci_fis_reg_h2d_t *fis = (ahci_fis_reg_h2d_t *)table->fis;
    memset(header, 0, sizeof(*header));
    memset(table, 0, sizeof(*table));
    memset(identify_buffers[controller_index][port], 0, 512U);
    header->flags = 5U; /* 20-byte H2D FIS, read direction */
    header->prdt_length = 1U;
    header->command_table_base =
        (uint32_t)(uintptr_t)command_tables[controller_index][port];
    fis->fis_type = 0x27U;
    fis->flags = 0x80U; /* command FIS */
    fis->command = 0xECU; /* IDENTIFY DEVICE */
    fis->device = 0xA0U;
    table->prdt[0].data_base =
        (uint32_t)(uintptr_t)identify_buffers[controller_index][port];
    table->prdt[0].byte_count_and_interrupt = 511U | (1U << 31U);
    return true;
}

static uint16_t ahci_identify_word(const uint8_t *identify, uint32_t word) {
    return (uint16_t)identify[word * 2U] |
           ((uint16_t)identify[word * 2U + 1U] << 8U);
}

static bool ahci_parse_identify(ahci_controller_info_t *controller,
                                size_t controller_index, uint32_t port) {
    if (controller == NULL || controller_index >= AHCI_MAX_CONTROLLERS ||
        port >= AHCI_MAX_PORTS) return false;
    const uint8_t *identify = identify_buffers[controller_index][port];
    uint64_t sectors = (uint64_t)ahci_identify_word(identify, 60U) |
        ((uint64_t)ahci_identify_word(identify, 61U) << 16U);
    uint64_t lba48 = (uint64_t)ahci_identify_word(identify, 100U) |
        ((uint64_t)ahci_identify_word(identify, 101U) << 16U) |
        ((uint64_t)ahci_identify_word(identify, 102U) << 32U) |
        ((uint64_t)ahci_identify_word(identify, 103U) << 48U);
    if ((ahci_identify_word(identify, 83U) & (1U << 10U)) != 0U)
        sectors = lba48;
    if (sectors == 0U || sectors > (UINT64_MAX / 512U)) return false;
    uint32_t sector_size = 512U;
    uint16_t sector_info = ahci_identify_word(identify, 106U);
    if ((sector_info & (1U << 12U)) != 0U) {
        uint32_t words = (uint32_t)ahci_identify_word(identify, 117U) |
                         ((uint32_t)ahci_identify_word(identify, 118U) << 16U);
        if (words != 256U) return false;
    }
    for (uint32_t index = 0U; index < 40U; index += 2U) {
        controller->model[port][index] = (char)identify[54U + index + 1U];
        controller->model[port][index + 1U] = (char)identify[54U + index];
    }
    controller->model[port][40] = '\0';
    controller->sector_count[port] = sectors;
    controller->sector_size[port] = sector_size;
    controller->identify_valid_ports |= 1U << port;
    return true;
}

static void ahci_publish_drives(void) {
    for (size_t index = 0U; index < controller_count; ++index) {
        ahci_controller_info_t *controller = &controllers[index];
        for (uint32_t port = 0U; port < AHCI_MAX_PORTS; ++port) {
            uint32_t bit = 1U << port;
            if ((controller->identify_valid_ports & bit) == 0U ||
                drive_count < 0 || drive_count >= MAX_DRIVES) continue;
            drive_t *drive = &detected_drives[drive_count++];
            memset(drive, 0, sizeof(*drive));
            drive->type = DRIVE_TYPE_AHCI;
            drive->sectors = controller->sector_count[port] > UINT32_MAX ?
                             UINT32_MAX : (uint32_t)controller->sector_count[port];
            memcpy(drive->model, controller->model[port], sizeof(drive->model));
            snprintf(drive->name, sizeof(drive->name), "sata%u",
                     (unsigned)(drive_count - 1));
        }
    }
}

static bool ahci_execute_identify(ahci_controller_info_t *controller,
                                  size_t controller_index, uint32_t port) {
    if (controller == NULL || controller->mmio == NULL ||
        controller_index >= AHCI_MAX_CONTROLLERS || port >= AHCI_MAX_PORTS)
        return false;
    uint32_t base = AHCI_PORT_BASE + port * AHCI_PORT_STRIDE;
    uint32_t command = ahci_read(controller->mmio, base + AHCI_PORT_CMD);
    if ((command & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) != 0U ||
        (ahci_read(controller->mmio, base + AHCI_PORT_CI) & 1U) != 0U)
        return false;
    ahci_write(controller->mmio, base + AHCI_PORT_IS, 0xFFFFFFFFU);
    ahci_write(controller->mmio, base + AHCI_PORT_CMD,
               command | AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_ST);
    ahci_write(controller->mmio, base + AHCI_PORT_CI, 1U);
    uint64_t start = pit_monotonic_ms();
    bool completed = false;
    for (uint32_t poll = 0U; poll < AHCI_RESET_MAX_POLLS; ++poll) {
        uint32_t interrupt_status = ahci_read(controller->mmio,
                                               base + AHCI_PORT_IS);
        uint32_t task_status = ahci_read(controller->mmio, base + AHCI_PORT_TFD);
        if ((interrupt_status & AHCI_PORT_IS_TFES) != 0U ||
            (task_status & AHCI_PORT_TFD_ERR) != 0U) break;
        if ((ahci_read(controller->mmio, base + AHCI_PORT_CI) & 1U) == 0U) {
            completed = (task_status & AHCI_PORT_TFD_BSY) == 0U;
            break;
        }
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= AHCI_COMMAND_TIMEOUT_MS) break;
    }
    bool stopped = ahci_stop_port(controller->mmio, port);
    if (!stopped || !completed) return false;
    const uint8_t *identify = identify_buffers[controller_index][port];
    return identify[0] != 0U || identify[1] != 0U;
}

static bool ahci_initialize_controller(ahci_controller_info_t *controller,
                                       pci_device_t *device,
                                       size_t controller_index) {
    if (controller == NULL || device == NULL || controller->abar > UINT32_MAX)
        return false;
    pci_enable_device(device);
    volatile uint32_t *mmio = map_mmio_region(controller->abar,
                                               AHCI_MMIO_SIZE);
    if (mmio == NULL || !ahci_reset(mmio)) return false;
    ahci_write(mmio, 0x04U, ahci_read(mmio, 0x04U) | AHCI_GHC_AE);
    uint32_t capability = ahci_read(mmio, 0x00U);
    uint32_t port_limit = (capability & 0x1FU) + 1U;
    uint32_t implemented = ahci_read(mmio, 0x0CU);
    uint32_t sata_ports = 0U;
    for (uint32_t port = 0U; port < port_limit && port < 32U; ++port) {
        uint32_t bit = 1U << port;
        if ((implemented & bit) != 0U && ahci_port_is_sata(mmio, port))
            sata_ports |= bit;
    }
    controller->mmio = mmio;
    controller->capability = capability;
    controller->implemented_ports = implemented;
    controller->sata_ports = sata_ports;
    controller->version = ahci_read(mmio, 0x10U);
    controller->port_count = (uint8_t)port_limit;
    controller->dma_ready_ports = 0U;
    controller->identify_valid_ports = 0U;
    uint32_t identify_ports = 0U;
    for (uint32_t port = 0U; port < port_limit && port < AHCI_MAX_PORTS;
         ++port) {
        uint32_t bit = 1U << port;
        if ((sata_ports & bit) != 0U &&
            ahci_prepare_port(controller, controller_index, port) &&
            ahci_build_identify_command(controller, controller_index, port) &&
            ahci_execute_identify(controller, controller_index, port) &&
            ahci_parse_identify(controller, controller_index, port))
            controller->dma_ready_ports |= bit;
        if ((controller->dma_ready_ports & bit) != 0U) identify_ports |= bit;
    }
    controller->dma_ready_ports = identify_ports;
    controller->valid = 1U;
    return true;
}

size_t ahci_probe_controllers(ahci_controller_info_t *output,
                              size_t capacity) {
    size_t found = 0U;
    if (output == NULL && capacity != 0U) return 0U;
    for (size_t index = 0U; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        uint64_t abar = 0U;
        if (device->class_code != AHCI_PCI_CLASS ||
            device->subclass_code != AHCI_PCI_SUBCLASS ||
            device->prog_if != AHCI_PCI_PROG_IF ||
            ahci_bar5(device, &abar) != 0) continue;
        if (found < capacity) {
            output[found] = (ahci_controller_info_t){
                .abar = abar, .bus = device->bus, .slot = device->slot,
                .function = device->function, .port_count = 0U, .valid = 0U
            };
        }
        ++found;
    }
    return found;
}

void ahci_init(void) {
    memset(controllers, 0, sizeof(controllers));
    controller_count = ahci_probe_controllers(controllers,
                                               AHCI_MAX_CONTROLLERS);
    if (controller_count > AHCI_MAX_CONTROLLERS)
        controller_count = AHCI_MAX_CONTROLLERS;
    for (size_t index = 0U; index < controller_count; ++index) {
        for (size_t device_index = 0U; device_index < pci_device_count;
             ++device_index) {
            pci_device_t *device = &pci_devices[device_index];
            if (device->bus == controllers[index].bus &&
                device->slot == controllers[index].slot &&
                device->function == controllers[index].function) {
                (void)ahci_initialize_controller(&controllers[index], device,
                                                  index);
                break;
            }
        }
    }
    ahci_publish_drives();
}
