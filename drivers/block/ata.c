/**
 * @file drivers/block/ata.c
 * @brief ATA-PIO-Erkennung, Transfer, Reset und persistentes Undo-Journal.
 *
 * Layer: Ring-0 block and bus driver.
 * Contract: Ressourcen, LBA-Bereiche und Backendbesitz werden vor jedem Seiteneffekt validiert.
 * Safety: Alle Polls sind begrenzt; unklarer Write-Abschluss wird nicht blind wiederholt.
 */
// ATA driver
#include "ata.h"
#include "ahci.h"
#include "ata_journal.h"
#include "../char/io.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/fdd.h"
#include "drivers/bus/pci.h"
#include "arch/x86/include/cpu_local.h"
#include "include/kernel/panic.h"
#include "include/kernel/storage_safety.h"
#include "kernel/sched/mutex.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"  // For pit_delay() in kernel context
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Build configuration
#ifdef QEMU_BUILD
    #define ATA_WAIT_TIMEOUT_MS 500      // QEMU is fast, shorter timeout
    #define ATA_POLL_DELAY_MS 1          // QEMU: poll every 1ms
    #define ATA_DETECTION_TIMEOUT_MS 100 // QEMU detection is quick
#elif defined(VMWARE_BUILD)
    #define ATA_WAIT_TIMEOUT_MS 5000     // VMware: use same as real hardware
    #define ATA_POLL_DELAY_MS 10         // VMware: longer polling interval
    #define ATA_DETECTION_TIMEOUT_MS 2000 // VMware needs much longer detection
#elif defined(REAL_HARDWARE)
    #define ATA_WAIT_TIMEOUT_MS 5000     // Real hardware needs more time
    #define ATA_POLL_DELAY_MS 10         // Real HW: poll every 10ms to avoid bus hogging
    #define ATA_DETECTION_TIMEOUT_MS 500 // Real hardware can be slow
#else
    #define ATA_WAIT_TIMEOUT_MS 1000     // Default: moderate timeout
    #define ATA_POLL_DELAY_MS 5
    #define ATA_DETECTION_TIMEOUT_MS 200
#endif


drive_t* current_drive = NULL;// = {0};  // Current drive (global variable)
drive_t detected_drives[MAX_DRIVES];  // Global array of detected drives
short drive_count = 0;  // Number of detected drives

#define ATA_MAX_PCI_CONTROLLERS 4U
#define ATA_CHANNEL_CAPACITY (ATA_MAX_PCI_CONTROLLERS * 2U)
#define ATA_PRIMARY_CONTROL 0x3F6U
#define ATA_SECONDARY_CONTROL 0x376U
#define ATA_PCI_CLASS_STORAGE 0x01U
#define ATA_PCI_SUBCLASS_IDE 0x01U
#define ATA_PCI_PRIMARY_NATIVE 0x01U
#define ATA_PCI_SECONDARY_NATIVE 0x04U
#define ATA_DEVICE_CONTROL_NIEN 0x02U
#define ATA_DEVICE_CONTROL_SRST 0x04U
#define ATA_PROBE_FLAG_IO_DECODE_FAILED (1U << 0U)
#define ATA_PROBE_FLAG_INVALID_BAR (1U << 1U)
#define ATA_PROBE_FLAG_CHANNEL_TRUNCATED (1U << 2U)
#define ATA_PROBE_FLAG_CONTROLLER_TRUNCATED (1U << 3U)

typedef struct {
    uint16_t command_base;
    uint16_t control_port;
    bool valid;
    bool native_mode;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
} ata_channel_t;

static ata_channel_t ata_channels[ATA_CHANNEL_CAPACITY] = {
    {ATA_PRIMARY_IO, ATA_PRIMARY_CONTROL, true, false, 0U, 0U, 0U},
    {ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, true, false, 0U, 0U, 0U},
};
static uint32_t ata_channel_count = 2U;
static uint32_t ata_pci_storage_function_count;
static uint32_t ata_pci_ide_function_count;
static uint32_t ata_identified_drive_count;
static uint32_t ata_probe_flags;
static bool ata_first_read_attempted[ATA_CHANNEL_CAPACITY];

static int ata_channel_index(uint16_t base) {
    for (uint32_t index = 0U; index < ata_channel_count; ++index) {
        if (ata_channels[index].valid &&
            ata_channels[index].command_base == base) return (int)index;
    }
    return -1;
}

static bool ata_command_base_valid(uint16_t base) {
    return ata_channel_index(base) >= 0;
}

uint16_t ata_control_port_for_base(uint16_t base) {
    int index = ata_channel_index(base);
    if (index >= 0) return ata_channels[index].control_port;
    return base == ATA_SECONDARY_IO ? ATA_SECONDARY_CONTROL :
                                      ATA_PRIMARY_CONTROL;
}

static bool ata_pci_io_bar(uint32_t raw, uint32_t span,
                           uint16_t *base) {
    if (base == NULL || span == 0U || raw == 0U || raw == UINT32_MAX ||
        (raw & 1U) == 0U) return false;
    uint32_t port = raw & ~3U;
    if (port == 0U || port > UINT16_MAX || span - 1U > UINT16_MAX - port ||
        (port & (span - 1U)) != 0U) return false;
    *base = (uint16_t)port;
    return true;
}

static bool ata_append_channel(uint16_t command_base, uint16_t control_port,
                               bool native_mode,
                               const pci_device_t *device) {
    if (device == NULL || command_base == 0U || control_port == 0U)
        return false;
    for (uint32_t index = 0U; index < ata_channel_count; ++index) {
        if (ata_channels[index].command_base == command_base) {
            if (ata_channels[index].control_port == control_port) {
                printf("ATA: duplicate channel %X/%X at %u:%u.%u ignored\n",
                       command_base, control_port, device->bus, device->slot,
                       device->function);
            } else {
                ata_probe_flags |= ATA_PROBE_FLAG_INVALID_BAR;
                printf("ATA: conflicting control port for command %X at %u:%u.%u\n",
                       command_base, device->bus, device->slot,
                       device->function);
            }
            return true;
        }
    }
    if (ata_channel_count >= ATA_CHANNEL_CAPACITY) {
        ata_probe_flags |= ATA_PROBE_FLAG_CHANNEL_TRUNCATED;
        return false;
    }
    ata_channels[ata_channel_count++] = (ata_channel_t){
        command_base, control_port, true, native_mode,
        device->bus, device->slot, device->function
    };
    return true;
}

static bool ata_configure_controller_channel(const pci_device_t *device,
                                             uint32_t channel) {
    if (device == NULL || channel >= 2U) return false;
    uint8_t native_bit = channel == 0U ? ATA_PCI_PRIMARY_NATIVE :
                                         ATA_PCI_SECONDARY_NATIVE;
    if ((device->prog_if & native_bit) == 0U) {
        return ata_append_channel(
            channel == 0U ? ATA_PRIMARY_IO : ATA_SECONDARY_IO,
            channel == 0U ? ATA_PRIMARY_CONTROL : ATA_SECONDARY_CONTROL,
            false, device);
    }

    uint32_t command_bar = channel * 2U;
    uint32_t control_bar = command_bar + 1U;
    uint16_t command_base, control_base;
    if (!ata_pci_io_bar(device->bar[command_bar], 8U, &command_base) ||
        !ata_pci_io_bar(device->bar[control_bar], 4U, &control_base) ||
        control_base > UINT16_MAX - 2U) {
        ata_probe_flags |= ATA_PROBE_FLAG_INVALID_BAR;
        printf("ATA: PCI IDE %u:%u.%u channel %u has invalid native BARs\n",
               device->bus, device->slot, device->function, channel);
        return false;
    }
    return ata_append_channel(command_base, (uint16_t)(control_base + 2U),
                              true, device);
}

static void ata_configure_channels(void) {
    memset(ata_channels, 0, sizeof(ata_channels));
    memset(ata_first_read_attempted, 0, sizeof(ata_first_read_attempted));
    ata_channel_count = 0U;
    ata_pci_storage_function_count = 0U;
    ata_pci_ide_function_count = 0U;
    ata_identified_drive_count = 0U;
    ata_probe_flags = 0U;

    uint32_t configured_controllers = 0U;
    for (size_t index = 0U; index < pci_device_count; ++index) {
        pci_device_t *controller = &pci_devices[index];
        if (controller->class_code != ATA_PCI_CLASS_STORAGE) continue;
        if (ata_pci_storage_function_count != UINT32_MAX)
            ++ata_pci_storage_function_count;
        if (controller->subclass_code != ATA_PCI_SUBCLASS_IDE) continue;
        if (ata_pci_ide_function_count != UINT32_MAX)
            ++ata_pci_ide_function_count;
        printf("ATA: PCI IDE %04X:%04X at %u:%u.%u prog-if=%02X\n",
               controller->vendor_id, controller->device_id, controller->bus,
               controller->slot, controller->function, controller->prog_if);
        if (configured_controllers >= ATA_MAX_PCI_CONTROLLERS) {
            ata_probe_flags |= ATA_PROBE_FLAG_CONTROLLER_TRUNCATED;
            continue;
        }
        ++configured_controllers;

        uint16_t command = pci_read_config_word(
            controller->bus, controller->slot, controller->function,
            PCI_COMMAND);
        if ((command & PCI_COMMAND_IO) == 0U) {
            pci_write_config_word(controller->bus, controller->slot,
                                  controller->function, PCI_COMMAND,
                                  (uint16_t)(command | PCI_COMMAND_IO));
            command = pci_read_config_word(controller->bus, controller->slot,
                                           controller->function, PCI_COMMAND);
        }
        if ((command & PCI_COMMAND_IO) == 0U) {
            ata_probe_flags |= ATA_PROBE_FLAG_IO_DECODE_FAILED;
            printf("ATA: PCI IDE %u:%u.%u I/O decoding unavailable\n",
                   controller->bus, controller->slot, controller->function);
            continue;
        }
        (void)ata_configure_controller_channel(controller, 0U);
        (void)ata_configure_controller_channel(controller, 1U);
    }
    if (ata_pci_ide_function_count == 0U) {
        pci_device_t legacy = {0};
        printf("ATA: no PCI IDE function; probing legacy channels\n");
        (void)ata_append_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CONTROL, false,
                                 &legacy);
        (void)ata_append_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL,
                                 false, &legacy);
    }

    for (uint32_t index = 0U; index < ata_channel_count; ++index) {
        printf("ATA: channel %u command=%X control=%X mode=%s owner=%u:%u.%u\n",
               index,
               ata_channels[index].command_base,
               ata_channels[index].control_port,
               ata_channels[index].native_mode ? "native" : "compat",
               ata_channels[index].bus, ata_channels[index].slot,
               ata_channels[index].function);
    }
}

uint32_t ata_probe_diagnostics(void) {
    uint32_t functions = ata_pci_ide_function_count > 0xFFU ? 0xFFU :
                         ata_pci_ide_function_count;
    uint32_t channels = ata_channel_count > 0xFFU ? 0xFFU :
                        ata_channel_count;
    uint32_t drives = ata_identified_drive_count > 0xFFU ? 0xFFU :
                      ata_identified_drive_count;
    uint32_t storage = ata_pci_storage_function_count > 0x0FU ? 0x0FU :
                       ata_pci_storage_function_count;
    return functions | (channels << 8U) | (drives << 16U) |
           (((ata_probe_flags & 0x0FU) | (storage << 4U)) << 24U);
}

// Track consecutive failures to prevent infinite loops
static unsigned int consecutive_read_failures = 0;
#define MAX_CONSECUTIVE_FAILURES 5

#define ATA_READ_CACHE_ENTRIES 32
typedef struct {
    unsigned short base;
    unsigned int lba;
    bool is_master;
    bool valid;
    uint8_t data[SECTOR_SIZE];
} ata_cache_entry_t;

static ata_cache_entry_t ata_read_cache[ATA_READ_CACHE_ENTRIES];
static volatile bool ata_write_fenced;

static int ata_resource_index(unsigned short base, bool is_master) {
    for (short index = 0; index < drive_count; ++index) {
        drive_t *drive = &detected_drives[index];
        if (((drive->type == DRIVE_TYPE_ATA &&
              drive->is_master == is_master) ||
             drive->type == DRIVE_TYPE_AHCI) && drive->base == base)
            return index;
    }
    return -1;
}

static drive_t *ata_compat_ahci_drive(unsigned short base) {
    if ((base & 0xFFE0U) != AHCI_VIRTUAL_BASE) return NULL;
    for (short index = 0; index < drive_count; ++index) {
        drive_t *drive = &detected_drives[index];
        if (drive->type == DRIVE_TYPE_AHCI && drive->base == base)
            return drive;
    }
    return NULL;
}

static drive_t *ata_compat_partition_drive(unsigned short base) {
    if ((base & 0xFFE0U) != 0xB000U) return NULL;
    for (short index = 0; index < drive_count; ++index) {
        drive_t *drive = &detected_drives[index];
        if (drive->type == DRIVE_TYPE_PARTITION && drive->base == base)
            return drive;
    }
    return NULL;
}

static drive_t *ata_partition_translate(drive_t *partition, uint32_t lba,
                                        uint32_t *absolute_lba) {
    if (partition == NULL || absolute_lba == NULL ||
        partition->type != DRIVE_TYPE_PARTITION ||
        partition->parent_resource >= (uint32_t)drive_count ||
        lba >= partition->sectors || lba > UINT32_MAX - partition->lba_offset)
        return NULL;
    drive_t *parent = &detected_drives[partition->parent_resource];
    uint32_t absolute = partition->lba_offset + lba;
    if (parent->type == DRIVE_TYPE_PARTITION || absolute >= parent->sectors)
        return NULL;
    *absolute_lba = absolute;
    return parent;
}

static ata_undo_journal_t ata_journal;
static bool ata_journal_read_view(unsigned short base, uint32_t lba,
                                  void *buffer, bool is_master);

/* ATA PIO uses controller-global task-file registers. A recursive sleepable
 * mutex serializes complete transactions without pinning another CPU in a
 * spin loop; public entry points fail closed when its deadline expires. */
#define ATA_TRANSACTION_LOCK_TIMEOUT_MS 10000U
static kernel_mutex_t ata_transaction_mutex = KERNEL_MUTEX_INIT;

static bool ata_transaction_begin(void) {
    if (irq_in_context()) {
        x86_cpu_local_t *local = x86_cpu_local_current();
        panic_context_set_result(
            -35, local != NULL ? local->cpu_index : UINT32_MAX,
            local != NULL
                ? (local->irq_context_depth << 16U) |
                      (irq_context_vector() & 0xFFFFU)
                : x86_cpu_initial_apic_id());
    }
    KASSERT_NOT_IRQ();
    if (kernel_mutex_lock_for(&ata_transaction_mutex,
                              ATA_TRANSACTION_LOCK_TIMEOUT_MS) != 0)
        return false;
    return true;
}

static void ata_transaction_end(void) {
    KASSERT_NOT_IRQ();
    kernel_mutex_unlock(&ata_transaction_mutex);
}

static void ata_selection_delay(uint16_t base) {
    for (volatile int delay = 0; delay < 4; ++delay)
        (void)inb(ATA_ALT_STATUS(base));
}

/*
 * Device selection is channel-global.  IDENTIFY probing commonly leaves an
 * absent slave selected with ERR latched.  That ERR belongs to the completed
 * probe and must not reject a later command for the detected master.  Select
 * the target first, wait for BSY and DRQ to clear, and let the status after the
 * new command report any real operation failure.
 */
static bool ata_select_target(uint16_t base, uint8_t drive_head,
                              uint32_t timeout_ms) {
    if (!ata_command_base_valid(base)) return false;
    outb(ATA_DRIVE_HEAD(base), drive_head);
    ata_selection_delay(base);

    uint32_t elapsed = 0U;
    while (elapsed <= timeout_ms) {
        uint8_t status = inb(ATA_STATUS(base));
        if (status == 0U || status == 0xFFU) return false;
        if ((status & (0x80U | 0x08U)) == 0U) return true;
        if (elapsed == timeout_ms) break;
        uint32_t delay = ATA_POLL_DELAY_MS;
        if (delay > timeout_ms - elapsed) delay = timeout_ms - elapsed;
        pit_delay(delay);
        elapsed += delay;
    }
    return false;
}

static ata_cache_entry_t* ata_cache_slot(unsigned short base,
                                         unsigned int lba, bool is_master) {
    unsigned int drive = is_master ? 0U : 1U;
    int channel = ata_channel_index(base);
    unsigned int controller = channel >= 0 ? (unsigned int)channel * 2U : 0U;
    unsigned int index = (lba ^ (drive << 4) ^ (controller << 3)) %
                         ATA_READ_CACHE_ENTRIES;
    return &ata_read_cache[index];
}

bool wait_for_drive_ready(unsigned short base, unsigned int timeout_ms) {
    unsigned int elapsed_time = 0;
    
#if defined(QEMU_BUILD) || defined(VMWARE_BUILD)
    // QEMU/VMware: Wait for BSY to clear AND RDY to be set
    while (1) {
        uint8_t status = inb(ATA_STATUS(base));
        
        // Check if BSY is clear AND RDY is set
        if (!(status & 0x80) && (status & 0x40)) {
            break;  // Drive is ready (BSY clear, RDY set)
        }
        
        if (elapsed_time >= timeout_ms) {
            printf("Timeout: Drive not ready (status=0x%02X) after %u ms\n", status, elapsed_time);
            return false;  // Timeout reached
        }
        pit_delay(ATA_POLL_DELAY_MS);
        elapsed_time += ATA_POLL_DELAY_MS;
    }
#else
    // Real hardware: Strict status checking
    while (1) {
        uint8_t status = inb(ATA_STATUS(base));
        
        // Check for errors
        if (status & 0x01) {  // ERR bit set
            printf("ATA Error: Drive returned error status\n");
            return false;
        }
        
        // Check if BSY is clear
        if (!(status & 0x80)) {
            break;  // Drive is ready
        }
        
        if (elapsed_time >= timeout_ms) {
            printf("Timeout: Drive not ready within %u ms.\n", timeout_ms);
            return false;
        }
        
        pit_delay(ATA_POLL_DELAY_MS);
        elapsed_time += ATA_POLL_DELAY_MS;
    }
#endif
    
    return true;  // Drive is ready
}

bool wait_for_drive_data_ready(unsigned short base, unsigned int timeout_ms) {
    unsigned int elapsed_time = 0;
    
#if defined(QEMU_BUILD) || defined(VMWARE_BUILD)
    // Emulated IDE: first wait for BSY to clear, then wait for DRQ.  Missing
    // slots return ERR during IDENTIFY and are a normal negative probe, so
    // leave user-facing error reporting to actual read/write operations.
    //printf("      wait_for_drive_data_ready: Step A - waiting for BSY clear\n");
    // Step 1: Wait for BSY to clear
    while (1) {
        uint8_t status = inb(ATA_STATUS(base));
        
        // Immediate debug on first read
        if (elapsed_time == 0) {
            //printf("      Initial status=0x%02X (BSY=%d, DRQ=%d, ERR=%d)\n",
            //       status, !!(status & 0x80), !!(status & 0x08), !!(status & 0x01));
        }
        
        // Check for floating bus (0xFF means no drive)
        if (status == 0xFF) {
            //printf("      ERROR: Drive not present (status=0xFF)\n");
            return false;
        }
        
        // Check for error
        if (status & 0x01) {
            uint8_t error = inb(ATA_ERROR(base));
            //printf("      ERROR: Drive returned error status (ERR=0x%02X)\n", error);
            return false;
        }
        
        // Check if BSY cleared
        if (!(status & 0x80)) {
            break;  // BSY cleared
        }
        
        if (elapsed_time >= timeout_ms) {
            //printf("      ERROR: BSY still set after %u ms (final status=0x%02X)\n", elapsed_time, status);
            return false;
        }
        pit_delay(ATA_POLL_DELAY_MS);
        elapsed_time += ATA_POLL_DELAY_MS;
        
        if (elapsed_time % 50 == 0) {  // Debug every 50ms
            //printf("      [%u ms] status=0x%02X (BSY still set)\n", elapsed_time, status);
        }
    }
    //printf("      wait_for_drive_data_ready: Step A OK - BSY cleared after %u ms\n", elapsed_time);
    
    // Step 2: Wait for DRQ to be set
    //printf("      wait_for_drive_data_ready: Step B - waiting for DRQ set\n");
    while (!(inb(ATA_STATUS(base)) & 0x08)) {  // Wait for DRQ bit to set
        if (elapsed_time >= timeout_ms) {
            uint8_t status = inb(ATA_STATUS(base));
            //printf("      ERROR: DRQ not set. Final status=0x%02X after %u ms\n", status, elapsed_time);
            return false;
        }
        pit_delay(ATA_POLL_DELAY_MS);
        elapsed_time += ATA_POLL_DELAY_MS;
        
        if (elapsed_time % 50 == 0) {  // Debug every 50ms
            uint8_t status = inb(ATA_STATUS(base));
            //printf("      [%u ms] status=0x%02X (waiting for DRQ)\n", elapsed_time, status);
        }
    }
    //printf("      wait_for_drive_data_ready: Step B OK - DRQ set after %u ms\n", elapsed_time);

    return true;  // Data is ready
#else
    // Real hardware: Strict DRQ and error checking
    while (1) {
        uint8_t status = inb(ATA_STATUS(base));
        
        // Check for errors first
        if (status & 0x01) {  // ERR bit
            printf("ATA Error: Drive error during data transfer\n");
            return false;
        }
        
        // Check for device fault
        if (status & 0x20) {  // DF bit
            printf("ATA Error: Device fault\n");
            return false;
        }
        
        // Check if DRQ is set and BSY is clear
        if ((status & 0x08) && !(status & 0x80)) {
            break;  // Data is ready
        }
        
        if (elapsed_time >= timeout_ms) {
            printf("Timeout: Drive data not ready within %u ms.\n", timeout_ms);
            return false;
        }
        
        pit_delay(ATA_POLL_DELAY_MS);
        elapsed_time += ATA_POLL_DELAY_MS;
    }
    
    return true;  // Data is ready
#endif
}

// Software reset for ATA controller
static void ata_soft_reset(unsigned short base, bool is_master) {
    printf("  Performing ATA software reset (base=0x%X, master=%d)...\n", base, is_master);
    
    // First, try to select the drive
    uint8_t drive_select = is_master ? 0xA0 : 0xB0;
    outb(ATA_DRIVE_HEAD(base), drive_select);
    
    // Wait 400ns for drive selection
    ata_selection_delay(base);
    pit_delay(10);  // Extra delay for drive selection
    
    // Set SRST (Software Reset) bit in Device Control register
    outb(ATA_CONTROL(base), ATA_DEVICE_CONTROL_NIEN |
                            ATA_DEVICE_CONTROL_SRST);
    pit_delay(10);  // Wait 10ms (increased)
    
    // Clear SRST while retaining polling-only operation (nIEN).
    outb(ATA_CONTROL(base), ATA_DEVICE_CONTROL_NIEN);
    pit_delay(5);  // Wait 5ms for reset to complete
    
    // Poll for drive ready (don't use wait_for_drive_ready as it may timeout)
    //printf("  Polling for drive ready after reset...\n");
    for (int i = 0; i < 100; i++) {
        uint8_t status = inb(ATA_STATUS(base));
        //printf("    Poll %d: status=0x%02X\n", i, status);

        if (status != 0x00 && status != 0xFF && !(status & 0x80)) {
            printf("  Reset complete, drive ready (status=0x%02X)\n", status);
            return;
        }
        pit_delay(10);
    }
    
    printf("  Warning: Drive may not be ready after reset\n");
}

/*
    * Reads a sector from the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to read.
    * @param buffer The buffer to read the sector into.
    * @return True if the sector was read successfully, false otherwise.
*/
static bool ata_read_sector_impl(unsigned short base, unsigned int lba,
                                 void* buffer, bool is_master) {
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    bool use_lba48 = lba >= ATA_LBA28_LIMIT;
    if (buffer == NULL ||
        (use_lba48 && (drive == NULL || !drive->lba48_supported)) ||
        (drive != NULL && lba >= drive->sectors) ||
        !ata_command_base_valid(base)) {
        return false;
    }
    ata_cache_entry_t* cached = ata_cache_slot(base, lba, is_master);
    if (cached->valid && cached->base == base && cached->lba == lba &&
        cached->is_master == is_master) {
        memcpy(buffer, cached->data, SECTOR_SIZE);
        return true;
    }
    //printf("ata_read_sector: base=0x%X, lba=%u, is_master=%d\n", base, lba, is_master);
    
    unsigned char drive_head = use_lba48 ? 0x40U :
        (unsigned char)(0xE0U | ((lba >> 24) & 0x0FU));
    drive_head |= is_master ? 0x00U : 0x10U;

    // Select the actual target before inspecting channel-global status.  A
    // failed empty-slot IDENTIFY may otherwise leave its ERR bit latched.
    int controller_idx = ata_channel_index(base);
    if (controller_idx < 0) return false;
    bool selected = ata_select_target(base, drive_head,
                                      ATA_WAIT_TIMEOUT_MS);
    if (!selected && !ata_first_read_attempted[controller_idx]) {
        ata_soft_reset(base, is_master);
        selected = ata_select_target(base, drive_head, ATA_WAIT_TIMEOUT_MS);
    }
    ata_first_read_attempted[controller_idx] = true;
    if (!selected) {
        consecutive_read_failures++;
        return false;
    }
    
    // Set up sector count and LBA registers
    //printf("  Step 2: Setting up LBA registers...\n");
    if (use_lba48) {
        outb(ATA_SECTOR_CNT(base), 0U);
        outb(ATA_LBA_LOW(base), (unsigned char)(lba >> 24U));
        outb(ATA_LBA_MID(base), 0U);
        outb(ATA_LBA_HIGH(base), 0U);
    }
    outb(ATA_SECTOR_CNT(base), 1); // Read 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));
    //printf("  Step 2: LBA registers set OK\n");

    // Send the read command
    //printf("  Step 3: Sending READ command...\n");
    outb(ATA_COMMAND(base), use_lba48 ? ATA_READ_SECTORS_EXT :
                                       ATA_READ_SECTORS);
    
    // Small delay after sending command (required by ATA spec)
    //printf("  Step 3: Delay after command...\n");
    for (volatile int i = 0; i < 4; i++) {
        inb(ATA_ALT_STATUS(base));  // Read alternate status 4 times for 400ns delay
    }
    //printf("  Step 3: Command sent OK\n");

    // Wait for the drive to be ready to transfer data
    //printf("  Step 4: Waiting for data ready...\n");
    if (!wait_for_drive_data_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        //printf("  ERROR: Data not ready (timeout)\n");
        consecutive_read_failures++;
        //printf("  Consecutive failures: %u/%u\n", consecutive_read_failures, MAX_CONSECUTIVE_FAILURES);

        // Add delay before returning to prevent rapid retry loops
        pit_delay(100);  // 100ms delay on failure
        return false;  // Drive data not ready within the timeout
    }
    //printf("  Step 4: Data ready OK\n");

    // Read the data
    //printf("  Step 5: Reading data...\n");
    insw(ATA_DATA(base), buffer, SECTOR_SIZE / 2);
    //printf("  Step 5: Data read OK\n");

#ifdef REAL_HARDWARE
    // Real hardware: Wait for command completion
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        consecutive_read_failures++;
        return false;
    }
#endif

    // Success - reset failure counter
    consecutive_read_failures = 0;

    cached->base = base;
    cached->lba = lba;
    cached->is_master = is_master;
    cached->valid = true;
    memcpy(cached->data, buffer, SECTOR_SIZE);

    //printf("ata_read_sector: SUCCESS\n");
    return true;
}

static bool ata_pio_range_valid(drive_t *drive, uint32_t lba,
                                uint32_t count) {
    return drive != NULL && count != 0U && count <= ATA_PIO_MAX_SECTORS &&
           lba < drive->sectors && count <= drive->sectors - lba;
}

static bool ata_program_pio_batch(unsigned short base, uint32_t lba,
                                  uint32_t count, bool is_master,
                                  bool write, bool use_lba48) {
    uint8_t head = use_lba48 ? 0x40U :
        (uint8_t)(0xE0U | ((lba >> 24U) & 0x0FU));
    head |= is_master ? 0U : 0x10U;
    if (!ata_select_target(base, head, ATA_WAIT_TIMEOUT_MS)) return false;
    if (use_lba48) {
        outb(ATA_SECTOR_CNT(base), 0U);
        outb(ATA_LBA_LOW(base), (uint8_t)(lba >> 24U));
        outb(ATA_LBA_MID(base), 0U);
        outb(ATA_LBA_HIGH(base), 0U);
    }
    outb(ATA_SECTOR_CNT(base), (uint8_t)count);
    outb(ATA_LBA_LOW(base), (uint8_t)lba);
    outb(ATA_LBA_MID(base), (uint8_t)(lba >> 8U));
    outb(ATA_LBA_HIGH(base), (uint8_t)(lba >> 16U));
    outb(ATA_COMMAND(base), use_lba48
        ? (write ? ATA_WRITE_SECTORS_EXT : ATA_READ_SECTORS_EXT)
        : (write ? ATA_WRITE_SECTORS : ATA_READ_SECTORS));
    for (volatile int delay = 0; delay < 4; ++delay)
        (void)inb(ATA_ALT_STATUS(base));
    return true;
}

static bool ata_read_sectors_pio_impl(unsigned short base, uint32_t lba,
                                      uint32_t count, void *buffer,
                                      bool is_master) {
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    if (buffer == NULL || !ata_pio_range_valid(drive, lba, count))
        return false;
    uint32_t last = lba + count - 1U;
    bool use_lba48 = last >= ATA_LBA28_LIMIT;
    if (use_lba48 && !drive->lba48_supported) return false;
    if (!ata_program_pio_batch(base, lba, count, is_master, false,
                               use_lba48)) return false;
    uint8_t *bytes = buffer;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!wait_for_drive_data_ready(base, ATA_WAIT_TIMEOUT_MS)) return false;
        insw(ATA_DATA(base), bytes + index * SECTOR_SIZE, SECTOR_SIZE / 2U);
        ata_cache_entry_t *cached =
            ata_cache_slot(base, lba + index, is_master);
        cached->base = base;
        cached->lba = lba + index;
        cached->is_master = is_master;
        cached->valid = true;
        memcpy(cached->data, bytes + index * SECTOR_SIZE, SECTOR_SIZE);
    }
    consecutive_read_failures = 0U;
    return wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS);
}

bool ata_read_sectors(unsigned short base, uint32_t lba, uint32_t count,
                      void *buffer, bool is_master) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, lba, &absolute);
        if (parent == NULL || buffer == NULL || count == 0U ||
            count > ATA_PIO_MAX_SECTORS ||
            count > partition->sectors - lba) return false;
        if (parent->type == DRIVE_TYPE_AHCI) {
            uint8_t *bytes = buffer;
            for (uint32_t index = 0U; index < count; ++index) {
                if (!ahci_read_sector(parent, absolute + index,
                        bytes + index * SECTOR_SIZE)) return false;
            }
            return true;
        }
        if (!ata_transaction_begin()) return false;
        bool result = ata_read_sectors_pio_impl(parent->base, absolute,
            count, buffer, parent->is_master);
        ata_transaction_end();
        return result;
    }
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    if (ahci_drive != NULL) {
        if (buffer == NULL || count == 0U || count > ATA_PIO_MAX_SECTORS ||
            lba >= ahci_drive->sectors || count > ahci_drive->sectors - lba)
            return false;
        uint8_t *bytes = buffer;
        for (uint32_t index = 0U; index < count; ++index) {
            if (!ahci_read_sector(ahci_drive, lba + index,
                                  bytes + index * SECTOR_SIZE)) return false;
        }
        return true;
    }
    if (!ata_transaction_begin()) return false;
    bool result = ata_read_sectors_pio_impl(base, lba, count, buffer,
                                            is_master);
    ata_transaction_end();
    return result;
}

bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer,
                     bool is_master) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, lba, &absolute);
        if (parent == NULL) return false;
        if (parent->type == DRIVE_TYPE_AHCI)
            return ata_journal_read_view(parent->base, absolute, buffer,
                                         parent->is_master);
        base = parent->base;
        lba = absolute;
        is_master = parent->is_master;
    } else {
        drive_t *ahci_drive = ata_compat_ahci_drive(base);
        if (ahci_drive != NULL)
            return ata_journal_read_view(base, lba, buffer, is_master);
    }
    if (!ata_transaction_begin()) return false;
    bool result = ata_journal_read_view(base, lba, buffer, is_master);
    ata_transaction_end();
    return result;
}

bool ata_read_sector_fresh(unsigned short base, unsigned int lba, void *buffer,
                           bool is_master) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, lba, &absolute);
        if (parent == NULL) return false;
        if (parent->type == DRIVE_TYPE_AHCI)
            return ahci_read_sector(parent, absolute, buffer);
        if (!ata_transaction_begin()) return false;
        ata_cache_entry_t *cached = ata_cache_slot(parent->base, absolute,
                                                    parent->is_master);
        cached->valid = false;
        bool result = ata_read_sector_impl(parent->base, absolute, buffer,
                                           parent->is_master);
        ata_transaction_end();
        return result;
    }
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    if (ahci_drive != NULL) return ahci_read_sector(ahci_drive, lba, buffer);
    if (!ata_transaction_begin()) return false;
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    if (buffer == NULL ||
        (lba >= ATA_LBA28_LIMIT &&
         (drive == NULL || !drive->lba48_supported)) ||
        (drive != NULL && lba >= drive->sectors) ||
        !ata_command_base_valid(base)) {
        ata_transaction_end();
        return false;
    }
    ata_cache_entry_t *cached = ata_cache_slot(base, lba, is_master);
    if (cached->valid && cached->base == base && cached->lba == lba &&
        cached->is_master == is_master) cached->valid = false;
    bool result = ata_read_sector_impl(base, lba, buffer, is_master);
    ata_transaction_end();
    return result;
}

// Reset the consecutive failure counter (useful after system idle or manual intervention)
void ata_reset_error_counter() {
    //printf("ata_reset_error_counter: Resetting failure counter (was %u)\n", consecutive_read_failures);
    consecutive_read_failures = 0;
}

/*
    * Writes a sector to the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to write.
    * @param buffer The buffer to write to the sector.
    * @return True if the sector was written successfully, false otherwise.
*/
static bool ata_flush_cache_impl(unsigned short base, bool is_master,
                                 bool use_lba48) {
    unsigned char drive_head = is_master ? 0xE0U : 0xF0U;
    if (!ata_select_target(base, drive_head, ATA_WAIT_TIMEOUT_MS)) return false;
    outb(ATA_COMMAND(base), use_lba48 ? ATA_FLUSH_CACHE_EXT : 0xE7U);
    return wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS);
}

static bool ata_write_sector_impl(unsigned short base, unsigned int lba,
                                  void* buffer, bool is_master,
                                  bool flush_cache) {
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    bool use_lba48 = lba >= ATA_LBA28_LIMIT;
    if (buffer == NULL ||
        (use_lba48 && (drive == NULL || !drive->lba48_supported)) ||
        (drive != NULL && lba >= drive->sectors) ||
        !ata_command_base_valid(base)) {
        return false; // Error: Buffer is null
    }
    /* Never serve data cached before a write attempt.  Invalidating first is
     * conservative when hardware reports a partial or uncertain failure. */
    ata_cache_entry_t* cached = ata_cache_slot(base, lba, is_master);
    if (cached->valid && cached->base == base && cached->lba == lba &&
        cached->is_master == is_master) {
        cached->valid = false;
    }

    // Select the target before programming its task-file registers.
    unsigned char drive_head = use_lba48 ? 0x40U :
        (unsigned char)(0xE0U | ((lba >> 24) & 0x0FU));
    drive_head |= is_master ? 0x00U : 0x10U;
    if (!ata_select_target(base, drive_head, ATA_WAIT_TIMEOUT_MS)) return false;

    // Program the selected device's task-file registers.
    if (use_lba48) {
        outb(ATA_SECTOR_CNT(base), 0U);
        outb(ATA_LBA_LOW(base), (unsigned char)(lba >> 24U));
        outb(ATA_LBA_MID(base), 0U);
        outb(ATA_LBA_HIGH(base), 0U);
    }
    outb(ATA_SECTOR_CNT(base), 1); // Write 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));

    // Send the write command
    outb(ATA_COMMAND(base), use_lba48 ? ATA_WRITE_SECTORS_EXT :
                                       ATA_WRITE_SECTORS);

    // Wait for the drive to signal that it's ready to receive data
    if (!wait_for_drive_data_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        return false;  // Drive data not ready within the timeout
    }

    // Write the data
    outsw(ATA_DATA(base), buffer, SECTOR_SIZE / 2);

    // Wait for write completion
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        return false;
    }
    
    // Journal staging may defer this barrier, but ordinary writes remain
    // durable before returning.
    if (flush_cache &&
        !ata_flush_cache_impl(base, is_master, use_lba48)) {
        printf("Warning: Cache flush timeout\n");
        return false;
    }

    return true;
}

static bool ata_journal_read_transport(unsigned short base, uint32_t lba,
                                       void *buffer, bool is_master) {
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    return ahci_drive != NULL
        ? ahci_read_sector(ahci_drive, lba, buffer)
        : ata_read_sector_impl(base, lba, buffer, is_master);
}

static bool ata_journal_write_transport(unsigned short base, uint32_t lba,
                                        const void *buffer, bool is_master) {
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    return ahci_drive != NULL
        ? ahci_write_sector_recovery(ahci_drive, lba, buffer)
        : ata_write_sector_impl(base, lba, (void *)buffer, is_master, true);
}

static bool ata_journal_write_deferred_transport(unsigned short base,
                                                 uint32_t lba,
                                                 const void *buffer,
                                                 bool is_master) {
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    /* AHCI currently exposes only a durable recovery write. Keeping that
     * stronger primitive preserves ordering; PIO gains the flush coalescing. */
    return ahci_drive != NULL
        ? ahci_write_sector_recovery(ahci_drive, lba, buffer)
        : ata_write_sector_impl(base, lba, (void *)buffer, is_master, false);
}

static bool ata_write_sectors_pio_deferred_impl(unsigned short base,
                                                uint32_t lba,
                                                uint32_t count,
                                                const void *buffer,
                                                bool is_master) {
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    if (buffer == NULL || !ata_pio_range_valid(drive, lba, count))
        return false;
    uint32_t last = lba + count - 1U;
    bool use_lba48 = last >= ATA_LBA28_LIMIT;
    if (use_lba48 && !drive->lba48_supported) return false;
    for (uint32_t index = 0U; index < count; ++index)
        ata_cache_slot(base, lba + index, is_master)->valid = false;
    if (!ata_program_pio_batch(base, lba, count, is_master, true,
                               use_lba48)) return false;
    const uint8_t *bytes = buffer;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!wait_for_drive_data_ready(base, ATA_WAIT_TIMEOUT_MS)) return false;
        outsw(ATA_DATA(base), bytes + index * SECTOR_SIZE,
              SECTOR_SIZE / 2U);
    }
    return wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS);
}

static bool ata_journal_write_sectors_deferred_transport(
        unsigned short base, uint32_t lba, uint32_t count,
        const void *buffer, bool is_master) {
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    if (ahci_drive == NULL)
        return ata_write_sectors_pio_deferred_impl(
            base, lba, count, buffer, is_master);
    const uint8_t *bytes = buffer;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!ahci_write_sector_recovery(
                ahci_drive, lba + index, bytes + index * SECTOR_SIZE))
            return false;
    }
    return true;
}

static bool ata_journal_flush_transport(unsigned short base,
                                        bool is_master) {
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    if (ahci_drive != NULL) return ahci_flush(ahci_drive);
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    return drive != NULL && drive->type == DRIVE_TYPE_ATA &&
        ata_flush_cache_impl(base, is_master, drive->lba48_supported);
}

static uint8_t ata_batch_verify[SECTOR_SIZE];

static bool ata_write_sectors_pio_impl(unsigned short base, uint32_t lba,
                                       uint32_t count, const void *buffer,
                                       bool is_master) {
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    if (buffer == NULL || !ata_pio_range_valid(drive, lba, count))
        return false;
    uint32_t last = lba + count - 1U;
    bool use_lba48 = last >= ATA_LBA28_LIMIT;
    if (use_lba48 && !drive->lba48_supported) return false;
    for (uint32_t index = 0U; index < count; ++index) {
        ata_cache_entry_t *cached =
            ata_cache_slot(base, lba + index, is_master);
        cached->valid = false;
    }
    if (!ata_program_pio_batch(base, lba, count, is_master, true,
                               use_lba48)) return false;
    const uint8_t *bytes = buffer;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!wait_for_drive_data_ready(base, ATA_WAIT_TIMEOUT_MS)) return false;
        outsw(ATA_DATA(base), bytes + index * SECTOR_SIZE, SECTOR_SIZE / 2U);
    }
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) return false;
    outb(ATA_COMMAND(base), use_lba48 ? ATA_FLUSH_CACHE_EXT : 0xE7U);
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) return false;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!ata_read_sector_impl(base, lba + index, ata_batch_verify,
                                  is_master) ||
            memcmp(ata_batch_verify, bytes + index * SECTOR_SIZE,
                   SECTOR_SIZE) != 0) return false;
    }
    return true;
}

static bool ata_journal_core_read(void *context, unsigned short base,
                                  uint32_t lba, void *buffer,
                                  bool is_master) {
    (void)context;
    return ata_journal_read_transport(base, lba, buffer, is_master);
}

static bool ata_journal_core_write(void *context, unsigned short base,
                                   uint32_t lba, const void *buffer,
                                   bool is_master) {
    (void)context;
    return ata_journal_write_transport(base, lba, buffer, is_master);
}

static bool ata_journal_core_write_deferred(void *context,
                                            unsigned short base,
                                            uint32_t lba,
                                            const void *buffer,
                                            bool is_master) {
    (void)context;
    return ata_journal_write_deferred_transport(base, lba, buffer,
                                                is_master);
}

static bool ata_journal_core_write_sectors_deferred(
        void *context, unsigned short base, uint32_t lba, uint32_t count,
        const void *buffer, bool is_master) {
    (void)context;
    return ata_journal_write_sectors_deferred_transport(
        base, lba, count, buffer, is_master);
}

static bool ata_journal_core_flush(void *context, unsigned short base,
                                   bool is_master) {
    (void)context;
    return ata_journal_flush_transport(base, is_master);
}

static bool ata_journal_core_commit_begin(void *context,
                                          unsigned short base,
                                          bool is_master) {
    (void)context;
    int resource = ata_resource_index(base, is_master);
    bool armed = !ata_write_fenced && resource >= 0 &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    return armed;
}

static bool ata_journal_core_commit_write_deferred(
        void *context, unsigned short base, uint32_t lba,
        const void *buffer, bool is_master) {
    (void)context;
    return ata_journal_write_deferred_transport(base, lba, buffer,
                                                is_master);
}

static bool ata_journal_core_commit_end(void *context, unsigned short base,
                                        bool is_master, bool commit) {
    (void)context;
    bool flushed = ata_journal_flush_transport(base, is_master);
    bool durable = commit && flushed;
    bool result = storage_write_end(durable) && durable;
    return result;
}

static bool ata_journal_core_commit_write(void *context, unsigned short base,
                                          uint32_t lba, const void *buffer,
                                          bool is_master) {
    (void)context;
    int resource = ata_resource_index(base, is_master);
    bool armed = !ata_write_fenced && resource >= 0 &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    bool result = armed &&
        ata_journal_write_transport(base, lba, buffer, is_master);
    if (armed && !storage_write_end(result)) result = false;
    return result;
}

static const ata_journal_transport_t ata_journal_transport = {
    .read = ata_journal_core_read,
    .write = ata_journal_core_write,
    .commit_write = ata_journal_core_commit_write,
    .write_deferred = ata_journal_core_write_deferred,
    .write_sectors_deferred = ata_journal_core_write_sectors_deferred,
    .flush = ata_journal_core_flush,
    .commit_begin = ata_journal_core_commit_begin,
    .commit_write_deferred = ata_journal_core_commit_write_deferred,
    .commit_end = ata_journal_core_commit_end,
};
static bool ata_journal_initialized;

static void ata_journal_ensure_initialized(void) {
    if (ata_journal_initialized) return;
    ata_undo_journal_init(&ata_journal, &ata_journal_transport, NULL);
    ata_journal_initialized = true;
}

static bool ata_journal_read_view(unsigned short base, uint32_t lba,
                                  void *buffer, bool is_master) {
    ata_journal_ensure_initialized();
    return ata_undo_journal_read_sector(&ata_journal, base, lba, buffer,
                                        is_master);
}

bool ata_journal_transaction_begin(void) {
    ata_journal_ensure_initialized();
    return ata_undo_journal_transaction_begin(&ata_journal);
}

bool ata_journal_transaction_end(bool commit) {
    ata_journal_ensure_initialized();
    /* The core's deferred callbacks intentionally assume this transaction
     * lock. Automatic one-sector transactions already execute below a public
     * ATA entry point holding the same recursive mutex. */
    if (!ata_transaction_begin()) return false;
    bool result = ata_undo_journal_transaction_end(&ata_journal, commit);
    ata_transaction_end();
    return result;
}

static bool ata_write_sector_journaled(unsigned short base, unsigned int lba,
                                       void *buffer, bool is_master) {
    ata_journal_ensure_initialized();
    return ata_undo_journal_write_sector(&ata_journal, base, lba, buffer,
                                         is_master);
}

static bool ata_journal_attach_impl(unsigned short base, bool is_master,
                                    uint32_t partition_lba,
                                    uint32_t volume_sectors,
                                    uint16_t reserved_sectors) {
    if (!ata_transaction_begin()) return false;
    ata_journal_ensure_initialized();
    bool result = ata_undo_journal_attach(&ata_journal, base, is_master,
        partition_lba, volume_sectors, reserved_sectors);
    if (!result) ata_fence_writes();
    ata_transaction_end();
    return result;
}

bool ata_journal_attach(unsigned short base, bool is_master,
                        uint32_t partition_lba, uint32_t volume_sectors,
                        uint16_t reserved_sectors) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, partition_lba,
                                                  &absolute);
        return parent != NULL && volume_sectors != 0U &&
            volume_sectors <= partition->sectors - partition_lba &&
            ata_journal_attach_impl(parent->base, parent->is_master, absolute,
                                    volume_sectors, reserved_sectors);
    }
    return ata_journal_attach_impl(base, is_master, partition_lba,
                                   volume_sectors, reserved_sectors);
}

static bool ata_journal_is_attached_impl(unsigned short base, bool is_master,
                                         uint32_t partition_lba,
                                         uint32_t volume_sectors) {
    ata_journal_ensure_initialized();
    return ata_undo_journal_is_attached(&ata_journal, base, is_master,
        partition_lba, volume_sectors);
}

bool ata_journal_is_attached(unsigned short base, bool is_master,
                             uint32_t partition_lba,
                             uint32_t volume_sectors) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, partition_lba,
                                                  &absolute);
        return parent != NULL && volume_sectors != 0U &&
            volume_sectors <= partition->sectors - partition_lba &&
            ata_journal_is_attached_impl(parent->base, parent->is_master,
                                         absolute, volume_sectors);
    }
    return ata_journal_is_attached_impl(base, is_master, partition_lba,
                                        volume_sectors);
}

bool ata_journal_recover_resource(uint32_t resource) {
    if (!ata_journal.enabled || ata_journal.transaction_depth != 0U ||
        resource >= (uint32_t)drive_count || resource >= MAX_DRIVES ||
        ata_journal.mirror_lba <= ata_journal.volume_start_lba ||
        ata_journal.volume_end_lba <= ata_journal.volume_start_lba)
        return false;
    drive_t *drive = &detected_drives[resource];
    if (drive->type == DRIVE_TYPE_PARTITION) {
        if (drive->parent_resource >= (uint32_t)drive_count) return false;
        drive = &detected_drives[drive->parent_resource];
    }
    if ((drive->type != DRIVE_TYPE_ATA && drive->type != DRIVE_TYPE_AHCI) ||
        drive->base != ata_journal.base ||
        (drive->type == DRIVE_TYPE_ATA &&
         drive->is_master != ata_journal.is_master)) return false;
    uint32_t reserved = ata_journal.mirror_lba -
                        ata_journal.volume_start_lba + 1U;
    if (reserved > UINT16_MAX) return false;
    return ata_journal_attach_impl(ata_journal.base, ata_journal.is_master,
        ata_journal.volume_start_lba,
        ata_journal.volume_end_lba - ata_journal.volume_start_lba,
        (uint16_t)reserved);
}

bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer,
                      bool is_master) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, lba, &absolute);
        if (parent == NULL) return false;
        if (parent->type == DRIVE_TYPE_AHCI) {
            int resource = ata_resource_index(parent->base, parent->is_master);
            bool armed = !ata_write_fenced && resource >= 0 &&
                storage_write_begin((uint32_t)resource, pit_monotonic_ms());
            bool result = armed && ata_write_sector_journaled(parent->base,
                absolute, buffer, parent->is_master);
            if (armed && !storage_write_end(result)) result = false;
            return result;
        }
        base = parent->base;
        lba = absolute;
        is_master = parent->is_master;
    } else {
        drive_t *ahci_drive = ata_compat_ahci_drive(base);
        if (ahci_drive != NULL) {
            int resource = ata_resource_index(base, is_master);
            bool armed = !ata_write_fenced && resource >= 0 &&
                storage_write_begin((uint32_t)resource, pit_monotonic_ms());
            bool result = armed && ata_write_sector_journaled(base, lba,
                                                               buffer,
                                                               is_master);
            if (armed && !storage_write_end(result)) result = false;
            return result;
        }
    }
    if (!ata_transaction_begin()) return false;
    int resource = ata_resource_index(base, is_master);
    bool armed = !ata_write_fenced && resource >= 0 &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    bool result = armed &&
                  ata_write_sector_journaled(base, lba, buffer, is_master);
    if (armed && !storage_write_end(result)) result = false;
    ata_transaction_end();
    return result;
}

bool ata_write_sectors(unsigned short base, uint32_t lba, uint32_t count,
                       const void *buffer, bool is_master) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, lba, &absolute);
        if (parent == NULL || count == 0U ||
            count > partition->sectors - lba) return false;
        const uint8_t *bytes = buffer;
        for (uint32_t index = 0U; index < count; ++index) {
            if (!ata_write_sector(parent->base, absolute + index,
                                  (void *)(bytes + index * SECTOR_SIZE),
                                  parent->is_master)) return false;
        }
        return true;
    }
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    if (ahci_drive != NULL) {
        int resource = ata_resource_index(base, is_master);
        bool valid = buffer != NULL && count != 0U &&
            count <= ATA_PIO_MAX_SECTORS && lba < ahci_drive->sectors &&
            count <= ahci_drive->sectors - lba;
        bool armed = valid && !ata_write_fenced && resource >= 0 &&
            storage_write_begin((uint32_t)resource, pit_monotonic_ms());
        bool result = armed;
        const uint8_t *bytes = buffer;
        bool journaled = armed && ata_journal.enabled &&
            base == ata_journal.base && is_master == ata_journal.is_master &&
            lba >= ata_journal.volume_start_lba &&
            lba < ata_journal.volume_end_lba &&
            count <= ata_journal.volume_end_lba - lba;
        if (journaled) result = ata_journal_transaction_begin();
        for (uint32_t index = 0U; result && index < count; ++index) {
            result = journaled
                ? ata_write_sector_journaled(base, lba + index,
                    (void *)(bytes + index * SECTOR_SIZE), is_master)
                : ahci_write_sector(ahci_drive, lba + index,
                                    bytes + index * SECTOR_SIZE);
        }
        if (journaled)
            result = ata_journal_transaction_end(result) && result;
        if (armed && !storage_write_end(result)) result = false;
        return result;
    }
    if (!ata_transaction_begin()) return false;
    int resource = ata_resource_index(base, is_master);
    drive_t *drive = resource >= 0 ? &detected_drives[resource] : NULL;
    bool valid = buffer != NULL && ata_pio_range_valid(drive, lba, count);
    bool armed = valid && !ata_write_fenced &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    bool result = false;
    if (armed && ata_journal.enabled && base == ata_journal.base &&
        is_master == ata_journal.is_master &&
        lba >= ata_journal.volume_start_lba &&
        lba < ata_journal.volume_end_lba &&
        count <= ata_journal.volume_end_lba - lba) {
        result = ata_journal_transaction_begin();
        const uint8_t *bytes = buffer;
        for (uint32_t index = 0U; result && index < count; ++index)
            result = ata_write_sector_journaled(base, lba + index,
                (void *)(bytes + index * SECTOR_SIZE), is_master);
        result = ata_journal_transaction_end(result) && result;
    } else if (armed) {
        result = ata_write_sectors_pio_impl(base, lba, count, buffer,
                                            is_master);
    }
    if (armed && !storage_write_end(result)) result = false;
    ata_transaction_end();
    return result;
}

bool ata_flush_cache(unsigned short base, bool is_master) {
    drive_t *partition = ata_compat_partition_drive(base);
    if (partition != NULL) {
        uint32_t absolute;
        drive_t *parent = ata_partition_translate(partition, 0U, &absolute);
        (void)absolute;
        if (parent == NULL) return false;
        if (parent->type == DRIVE_TYPE_AHCI) return ahci_flush(parent);
        if (!ata_transaction_begin()) return false;
        int resource = ata_resource_index(parent->base, parent->is_master);
        bool armed = !ata_write_fenced && resource >= 0 &&
            storage_write_begin((uint32_t)resource, pit_monotonic_ms());
        bool result = false;
        if (armed) {
            drive_t *drive = parent;
            result = ata_flush_cache_impl(parent->base, parent->is_master,
                                          drive->lba48_supported);
        }
        if (armed && !storage_write_end(result)) result = false;
        ata_transaction_end();
        return result;
    }
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    if (ahci_drive != NULL) return ahci_flush(ahci_drive);
    if (!ata_transaction_begin()) return false;
    int resource = ata_resource_index(base, is_master);
    bool armed = !ata_write_fenced && resource >= 0 &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    bool result = false;
    if (armed) {
        drive_t *drive = &detected_drives[resource];
        result = ata_flush_cache_impl(base, is_master,
                                      drive->lba48_supported);
    }
    if (armed && !storage_write_end(result)) result = false;
    ata_transaction_end();
    return result;
}

void ata_fence_writes(void) {
    ata_write_fenced = true;
    __asm__ volatile("" ::: "memory");
    if (!ata_transaction_begin()) return;
    ata_transaction_end();
}

void ata_restore_writes_after_recovery(void) {
    if (!ata_transaction_begin()) return;
    ata_write_fenced = false;
    __asm__ volatile("" ::: "memory");
    ata_transaction_end();
}

bool ata_writes_quiescent(void) {
    if (!ata_transaction_begin()) return false;
    bool result = ata_write_fenced;
    for (short i = 0; i < drive_count; ++i) {
        if (detected_drives[i].type != DRIVE_TYPE_ATA) continue;
        uint8_t status = inb(ATA_ALT_STATUS(detected_drives[i].base));
        if ((status & (0x80U | 0x08U)) != 0U) {
            result = false;
            break;
        }
    }
    ata_transaction_end();
    return result;
}

drive_t* ata_get_drive(unsigned short drive_index) {
    if (drive_index >= drive_count) {
        return NULL;  // Return NULL if the index is out of bounds
    }
    return &detected_drives[drive_index];
}

// Function to get the first ATA HDD (automatically finds first HDD)
drive_t* ata_get_first_hdd() {
    //printf("ata_get_first_hdd: searching for first HDD...\n");
    //printf("  drive_count: %d\n", drive_count);

    if (drive_count <= 0 || drive_count > MAX_DRIVES) {
        //printf("  ERROR: Invalid drive_count: %d\n", drive_count);
        return NULL;
    }
    
    // Search for first ATA drive
    for (int i = 0; i < drive_count; i++) {
        //printf("  Checking drive[%d]: name='%s', type=%d\n", 
         //      i, detected_drives[i].name, detected_drives[i].type);
        
        if (detected_drives[i].type == DRIVE_TYPE_ATA) {
            //printf("  FOUND: First HDD is '%s' at index %d\n", 
            //       detected_drives[i].name, i);
            return &detected_drives[i];
        }
    }

    //printf("  NOT FOUND: No ATA HDD in detected drives\n");
    return NULL;
}

// Function to detect all ATA drives on the primary and secondary buses
static void ata_probe_reset_channel(uint16_t base) {
    if (!ata_command_base_valid(base)) return;
    uint16_t control = ATA_CONTROL(base);
    outb(control, ATA_DEVICE_CONTROL_NIEN | ATA_DEVICE_CONTROL_SRST);
    pit_delay(5U);
    outb(control, ATA_DEVICE_CONTROL_NIEN);
    pit_delay(5U);
    for (uint32_t elapsed = 0U; elapsed < ATA_DETECTION_TIMEOUT_MS;
         elapsed += ATA_POLL_DELAY_MS) {
        uint8_t status = inb(ATA_ALT_STATUS(base));
        if (status == 0U || status == 0xFFU || (status & 0x80U) == 0U) return;
        pit_delay(ATA_POLL_DELAY_MS);
    }
}

static void ata_detect_drives_impl(void) {
    uint8_t drives[2] = { ATA_MASTER, ATA_SLAVE };
    int drive_name_index = 0;  // For generating names like "hdd1", "hdd2", etc.

    drive_count = 0;  // Reset drive count before detection
    ata_configure_channels();
    
    //printf("Starting ATA drive detection...\n");

    // Detect ATA drives
    for (uint32_t bus = 0U; bus < ata_channel_count; bus++) {
        if (!ata_channels[bus].valid) continue;
        uint16_t base = ata_channels[bus].command_base;
        ata_probe_reset_channel(base);
        for (int drive = 0; drive < 2; drive++) {
            if (drive_count >= MAX_ATA_DRIVES) {
               // printf("Maximum number of drives reached.\n");
                return;
            }

            // Use a temporary structure to avoid corrupting detected_drives on failure
            drive_t temp_drive;
            memset(&temp_drive, 0, sizeof(temp_drive));
            temp_drive.base = base;
            temp_drive.is_master = (drive == 0);  // 0 for master, 1 for slave

            // Attempt to identify the drive
            if (ata_identify_drive(base, drives[drive], &temp_drive)) {

                // Trim trailing spaces from the model name
                trim_trailing_spaces(temp_drive.model);

                temp_drive.type = DRIVE_TYPE_ATA;
                snprintf(temp_drive.name, sizeof(temp_drive.name), "hdd%d", drive_name_index++);
                
                // Initialize mount_point to empty
                temp_drive.mount_point[0] = '\0';
                
                //printf("ATA drive %s detected: %s, Sectors: %u\n", temp_drive.name, temp_drive.model, temp_drive.sectors);

                // Copy the successfully identified drive to the detected_drives array
                detected_drives[drive_count] = temp_drive;

                printf("ATA: resource %d %s model='%s' sectors=%u command=%X control=%X\n",
                       drive_count, temp_drive.is_master ? "master" : "slave",
                       temp_drive.model, temp_drive.sectors, temp_drive.base,
                       ATA_CONTROL(temp_drive.base));

                // Increment the global drive count after successfully adding a drive
                drive_count++;
                ata_identified_drive_count++;
            }
        }
        // Leave a deterministic selection behind.  Every operation still
        // selects its own target before reading channel-global status.
        outb(ATA_DRIVE_HEAD(base), ATA_MASTER);
        ata_selection_delay(base);
    }

    printf("ATA: detected %d PIO drive(s)\n", drive_count);
}

void ata_detect_drives(void) {
    if (!ata_transaction_begin()) return;
    ata_detect_drives_impl();
    ata_transaction_end();
}

static bool ata_wait_identify_data(uint16_t base, uint32_t timeout_ms) {
    uint32_t elapsed = 0U;
    while (elapsed <= timeout_ms) {
        uint8_t status = inb(ATA_STATUS(base));
        if (status == 0U || status == 0xFFU) return false;
        if ((status & 0x80U) == 0U) {
            if ((status & (0x01U | 0x20U)) != 0U) return false;
            if ((status & 0x08U) != 0U) return true;
        }
        if (elapsed == timeout_ms) break;
        uint32_t delay = ATA_POLL_DELAY_MS;
        if (delay > timeout_ms - elapsed) delay = timeout_ms - elapsed;
        pit_delay(delay);
        elapsed += delay;
    }
    return false;
}

static bool ata_identify_drive_impl(uint16_t base, uint8_t drive,
                                    drive_t *drive_info) {
    if (!drive_info || !ata_command_base_valid(base)) return false;

    // Select the drive (master or slave)
    outb(base + 6, drive);
    ata_selection_delay(base);

    // IDENTIFY requires the task-file address/count registers to be zero.
    outb(ATA_SECTOR_CNT(base), 0);
    outb(ATA_LBA_LOW(base), 0);
    outb(ATA_LBA_MID(base), 0);
    outb(ATA_LBA_HIGH(base), 0);

    // Send the IDENTIFY command
    outb(base + 7, ATA_IDENTIFY);
    ata_selection_delay(base);
    if (!ata_wait_identify_data(base, ATA_DETECTION_TIMEOUT_MS)) return false;

    // Read the IDENTIFY data
    uint16_t identify_data[256];
    insw(base, identify_data, 256);

    // Sanity checks for valid IDENTIFY response
    if (identify_data[0] == 0 || identify_data[0] == 0xFFFF) {
        return false;  // Invalid or non-ATA device
    }

    // Extract the model number (bytes 54-93 in IDENTIFY data)
    for (int i = 0; i < 20; i++) {
        drive_info->model[i * 2] = identify_data[27 + i] >> 8;
        drive_info->model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
    }
    drive_info->model[40] = '\0';

    // Validate the model name (check for printable characters)
    for (int i = 0; i < 40; i++) {
        if (drive_info->model[i] != '\0' && (drive_info->model[i] < 32 || drive_info->model[i] > 126)) {
            return false;  // Invalid model name, likely corrupted data
        }
    }

    uint64_t sectors = (uint32_t)identify_data[60] |
                       ((uint32_t)identify_data[61] << 16U);
    drive_info->lba48_supported =
        (identify_data[83] & (1U << 10U)) != 0U;
    if (drive_info->lba48_supported) {
        uint64_t lba48 = (uint64_t)identify_data[100] |
            ((uint64_t)identify_data[101] << 16U) |
            ((uint64_t)identify_data[102] << 32U) |
            ((uint64_t)identify_data[103] << 48U);
        if (lba48 != 0U) sectors = lba48;
        else drive_info->lba48_supported = false;
    }
    drive_info->sectors = sectors > UINT32_MAX ? UINT32_MAX :
                          (uint32_t)sectors;

    // Validate sector count
    if (drive_info->sectors <= 0) {
        return false;  // Invalid sector count, likely corrupted data
    }

    return true;
}

bool ata_identify_drive(uint16_t base, uint8_t drive, drive_t *drive_info) {
    if (!ata_transaction_begin()) return false;
    bool result = ata_identify_drive_impl(base, drive, drive_info);
    ata_transaction_end();
    return result;
}

drive_t* get_drive_by_name(const char* name) {
    //printf("get_drive_by_name: searching for '%s'\n", name);
    //printf("  drive_count address: %p, value: %d\n", &drive_count, drive_count);
    //printf("  detected_drives address: %p\n", detected_drives);

    if (drive_count < 0 || drive_count > MAX_DRIVES) {
        //printf("  ERROR: Invalid drive_count value: %d\n", drive_count);
        return NULL;
    }
    
    for (int i = 0; i < drive_count; i++) {
        //printf("  Checking drive[%d]: '%s' vs '%s'\n", i, detected_drives[i].name, name);
        if (strcmp(detected_drives[i].name, name) == 0) {
            //printf("  FOUND: drive '%s' at index %d\n", name, i);
            return &detected_drives[i];
        }
    }

    //printf("  NOT FOUND: drive '%s' not in list\n", name);
    return NULL;  // Return NULL if no drive with the specified name is found
}

void list_detected_drives() {
    printf("=== Drive List Debug ===\n");
    printf("drive_count variable address: %p, value: %d\n", &drive_count, drive_count);
    printf("detected_drives array address: %p\n", detected_drives);
    printf("Total drives detected: %d\n", drive_count);
    
    for (int i = 0; i < drive_count; i++) {
        drive_t* drive = &detected_drives[i];
        printf("  [%d] ", i);
        if (drive->type == DRIVE_TYPE_ATA) {
            printf("%s: %s, Sectors: %u\n", drive->name, drive->model, drive->sectors);
        } else if (drive->type == DRIVE_TYPE_FDD) {
            printf("%s: Floppy Drive (CHS: %u/%u/%u)\n", 
                   drive->name, drive->cylinder, drive->head, drive->sector);
        } else {
            printf("%s: Unknown type %d\n", drive->name, drive->type);
        }
    }
    printf("======================\n");
}
