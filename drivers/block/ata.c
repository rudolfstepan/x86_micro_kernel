// ATA driver
#include "ata.h"
#include "ahci.h"
#include "../char/io.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/fdd.h"
#include "drivers/bus/pci.h"
#include "include/kernel/panic.h"
#include "include/kernel/storage_safety.h"
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

#define ATA_CHANNEL_COUNT 2U
#define ATA_PRIMARY_CONTROL 0x3F6U
#define ATA_SECONDARY_CONTROL 0x376U
#define ATA_PCI_CLASS_STORAGE 0x01U
#define ATA_PCI_SUBCLASS_IDE 0x01U
#define ATA_PCI_PRIMARY_NATIVE 0x01U
#define ATA_PCI_SECONDARY_NATIVE 0x04U
#define ATA_DEVICE_CONTROL_NIEN 0x02U
#define ATA_DEVICE_CONTROL_SRST 0x04U

typedef struct {
    uint16_t command_base;
    uint16_t control_port;
    bool valid;
    bool native_mode;
} ata_channel_t;

static ata_channel_t ata_channels[ATA_CHANNEL_COUNT] = {
    {ATA_PRIMARY_IO, ATA_PRIMARY_CONTROL, true, false},
    {ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, true, false},
};

static int ata_channel_index(uint16_t base) {
    for (uint32_t index = 0U; index < ATA_CHANNEL_COUNT; ++index) {
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

static bool ata_configure_native_channel(const pci_device_t *device,
                                         uint32_t channel,
                                         uint32_t command_bar,
                                         uint32_t control_bar) {
    uint16_t command_base, control_base;
    if (device == NULL || channel >= ATA_CHANNEL_COUNT ||
        !ata_pci_io_bar(device->bar[command_bar], 8U, &command_base) ||
        !ata_pci_io_bar(device->bar[control_bar], 4U, &control_base) ||
        control_base > UINT16_MAX - 2U) {
        ata_channels[channel].valid = false;
        return false;
    }
    ata_channels[channel].command_base = command_base;
    ata_channels[channel].control_port = (uint16_t)(control_base + 2U);
    ata_channels[channel].valid = true;
    ata_channels[channel].native_mode = true;
    return true;
}

static void ata_configure_channels(void) {
    ata_channels[0] = (ata_channel_t){
        ATA_PRIMARY_IO, ATA_PRIMARY_CONTROL, true, false
    };
    ata_channels[1] = (ata_channel_t){
        ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, true, false
    };

    const pci_device_t *controller = NULL;
    for (size_t index = 0U; index < pci_device_count; ++index) {
        pci_device_t *candidate = &pci_devices[index];
        if (candidate->class_code == ATA_PCI_CLASS_STORAGE &&
            candidate->subclass_code == ATA_PCI_SUBCLASS_IDE) {
            controller = candidate;
            break;
        }
    }
    if (controller == NULL) {
        printf("ATA: no PCI IDE function; probing legacy channels\n");
        return;
    }

    uint16_t command = pci_read_config_word(
        controller->bus, controller->slot, controller->function, PCI_COMMAND);
    if ((command & PCI_COMMAND_IO) == 0U) {
        pci_write_config_word(controller->bus, controller->slot,
                              controller->function, PCI_COMMAND,
                              (uint16_t)(command | PCI_COMMAND_IO));
        command = pci_read_config_word(controller->bus, controller->slot,
                                       controller->function, PCI_COMMAND);
    }
    if ((command & PCI_COMMAND_IO) == 0U) {
        ata_channels[0].valid = false;
        ata_channels[1].valid = false;
        printf("ATA: PCI IDE I/O decoding could not be enabled\n");
        return;
    }

    if ((controller->prog_if & ATA_PCI_PRIMARY_NATIVE) != 0U) {
        (void)ata_configure_native_channel(controller, 0U, 0U, 1U);
    }
    if ((controller->prog_if & ATA_PCI_SECONDARY_NATIVE) != 0U) {
        (void)ata_configure_native_channel(controller, 1U, 2U, 3U);
    }
    printf("ATA: PCI IDE %04X:%04X at %u:%u.%u prog-if=%02X\n",
           controller->vendor_id, controller->device_id, controller->bus,
           controller->slot, controller->function, controller->prog_if);
    for (uint32_t index = 0U; index < ATA_CHANNEL_COUNT; ++index) {
        if (!ata_channels[index].valid) {
            printf("ATA: channel %u rejected (invalid native I/O BARs)\n",
                   index);
            continue;
        }
        printf("ATA: channel %u command=%X control=%X mode=%s\n", index,
               ata_channels[index].command_base,
               ata_channels[index].control_port,
               ata_channels[index].native_mode ? "native" : "compat");
    }
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

#define ATA_JOURNAL_MAGIC 0x4A545352U /* "RSTJ" */
#define ATA_JOURNAL_VERSION 2U
#define ATA_JOURNAL_CLEAN 0U
#define ATA_JOURNAL_ACTIVE 1U
#define ATA_JOURNAL_HEADER_OFFSET 8U
#define ATA_JOURNAL_DATA_OFFSET 9U
#define ATA_JOURNAL_MAX_ENTRIES 20U
#define ATA_JOURNAL_MIRROR_OFFSET 31U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t state;
    uint32_t sequence;
    uint32_t entry_count;
    uint32_t header_crc32;
    struct {
        uint32_t target_lba;
        uint32_t data_crc32;
    } entries[ATA_JOURNAL_MAX_ENTRIES];
    uint8_t reserved[SECTOR_SIZE - 24U - ATA_JOURNAL_MAX_ENTRIES * 8U];
} ata_journal_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic, version, state, target_lba, data_crc32, sequence;
    uint32_t header_crc32;
    uint8_t reserved[SECTOR_SIZE - 28U];
} ata_journal_v1_record_t;

static struct {
    bool enabled;
    unsigned short base;
    bool is_master;
    uint32_t header_lba;
    uint32_t mirror_lba;
    uint32_t data_lba;
    uint32_t volume_start_lba;
    uint32_t volume_end_lba;
    uint32_t sequence;
    uint32_t entry_count;
    uint32_t transaction_depth;
    struct {
        uint32_t target_lba;
        uint32_t data_crc32;
    } entries[ATA_JOURNAL_MAX_ENTRIES];
} ata_journal;

static uint32_t ata_journal_crc32(const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static bool ata_journal_record_valid(const ata_journal_record_t *record) {
    ata_journal_record_t copy = *record;
    uint32_t expected_crc = copy.header_crc32;
    copy.header_crc32 = 0U;
    return copy.magic == ATA_JOURNAL_MAGIC &&
           record->version == ATA_JOURNAL_VERSION &&
           record->state <= ATA_JOURNAL_ACTIVE &&
           record->entry_count <= ATA_JOURNAL_MAX_ENTRIES &&
           (record->state == ATA_JOURNAL_ACTIVE || record->entry_count == 0U) &&
           expected_crc == ata_journal_crc32(&copy, sizeof(copy));
}

static void ata_journal_seal(ata_journal_record_t *record) {
    record->header_crc32 = 0U;
    record->header_crc32 = ata_journal_crc32(record, sizeof(*record));
}

static bool ata_journal_v1_valid(const ata_journal_v1_record_t *record) {
    return record->magic == ATA_JOURNAL_MAGIC && record->version == 1U &&
           record->state <= ATA_JOURNAL_ACTIVE &&
           record->header_crc32 == ata_journal_crc32(record, 24U);
}

/* ATA PIO is synchronous and uses controller-global task-file registers.  On
 * this single-core kernel a nestable preemption guard serializes complete
 * transactions while leaving hardware interrupts enabled. */
static void ata_transaction_begin(void) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    scheduler_preempt_disable();
}

static void ata_transaction_end(void) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    KASSERT(scheduler_preempt_is_disabled());
    scheduler_preempt_enable();
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
    static bool first_read_attempted[2] = {false, false};  // Track per controller
    int controller_idx = ata_channel_index(base);
    if (controller_idx < 0) return false;
    bool selected = ata_select_target(base, drive_head,
                                      ATA_WAIT_TIMEOUT_MS);
    if (!selected && !first_read_attempted[controller_idx]) {
        ata_soft_reset(base, is_master);
        selected = ata_select_target(base, drive_head, ATA_WAIT_TIMEOUT_MS);
    }
    first_read_attempted[controller_idx] = true;
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
        if (parent == NULL || count == 0U ||
            count > partition->sectors - lba) return false;
        ata_transaction_begin();
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
    ata_transaction_begin();
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
            return ahci_read_sector(parent, absolute, buffer);
        base = parent->base;
        lba = absolute;
        is_master = parent->is_master;
    } else {
        drive_t *ahci_drive = ata_compat_ahci_drive(base);
        if (ahci_drive != NULL)
            return ahci_read_sector(ahci_drive, lba, buffer);
    }
    ata_transaction_begin();
    bool result = ata_read_sector_impl(base, lba, buffer, is_master);
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
        ata_transaction_begin();
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
    ata_transaction_begin();
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
static bool ata_write_sector_impl(unsigned short base, unsigned int lba,
                                  void* buffer, bool is_master) {
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
    
    // Flush cache to ensure data is written (critical for filesystem integrity)
    // This prevents data loss on power failure or disk removal
    outb(ATA_COMMAND(base), use_lba48 ? ATA_FLUSH_CACHE_EXT : 0xE7U);
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        printf("Warning: Cache flush timeout\n");
        return false;
    }

    return true;
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

static bool ata_journal_write_record(const ata_journal_record_t *record) {
    if (!ata_write_sector_impl(ata_journal.base, ata_journal.header_lba,
                               (void *)record, ata_journal.is_master))
        return false;
    return ata_journal.mirror_lba == 0U ||
           ata_write_sector_impl(ata_journal.base, ata_journal.mirror_lba,
                                 (void *)record, ata_journal.is_master);
}

static bool ata_journal_clear(void) {
    ata_journal_record_t clean;
    memset(&clean, 0, sizeof(clean));
    clean.magic = ATA_JOURNAL_MAGIC;
    clean.version = ATA_JOURNAL_VERSION;
    clean.state = ATA_JOURNAL_CLEAN;
    clean.sequence = ata_journal.sequence;
    ata_journal_seal(&clean);
    return ata_journal_write_record(&clean);
}

static bool ata_journal_write_active(void) {
    ata_journal_record_t active;
    memset(&active, 0, sizeof(active));
    active.magic = ATA_JOURNAL_MAGIC;
    active.version = ATA_JOURNAL_VERSION;
    active.state = ATA_JOURNAL_ACTIVE;
    active.sequence = ata_journal.sequence;
    active.entry_count = ata_journal.entry_count;
    for (uint32_t i = 0; i < ata_journal.entry_count; ++i) {
        active.entries[i].target_lba = ata_journal.entries[i].target_lba;
        active.entries[i].data_crc32 = ata_journal.entries[i].data_crc32;
    }
    ata_journal_seal(&active);
    return ata_journal_write_record(&active);
}

bool ata_journal_transaction_begin(void) {
    if (!ata_journal.enabled) return true;
    if (ata_journal.transaction_depth++ == 0U) ata_journal.entry_count = 0U;
    return ata_journal.transaction_depth != 0U;
}

bool ata_journal_transaction_end(bool commit) {
    if (!ata_journal.enabled) return true;
    if (ata_journal.transaction_depth == 0U) return false;
    if (--ata_journal.transaction_depth != 0U) return true;
    bool result = true;
    if (ata_journal.entry_count != 0U && commit) result = ata_journal_clear();
    ata_journal.entry_count = 0U;
    return result;
}

static bool ata_write_sector_journaled(unsigned short base, unsigned int lba,
                                       void *buffer, bool is_master) {
    if (!ata_journal.enabled || base != ata_journal.base ||
        is_master != ata_journal.is_master ||
        lba < ata_journal.volume_start_lba || lba >= ata_journal.volume_end_lba) {
        return ata_write_sector_impl(base, lba, buffer, is_master);
    }
    if (lba >= ata_journal.header_lba &&
        lba < ata_journal.data_lba + ATA_JOURNAL_MAX_ENTRIES)
        return false;
    if (lba == ata_journal.mirror_lba) return false;

    bool automatic = ata_journal.transaction_depth == 0U;
    if (automatic && !ata_journal_transaction_begin()) return false;
    for (uint32_t i = 0; i < ata_journal.entry_count; ++i) {
        if (ata_journal.entries[i].target_lba == lba) {
            bool result = ata_write_sector_impl(base, lba, buffer, is_master);
            return automatic ? (ata_journal_transaction_end(result) && result)
                             : result;
        }
    }
    if (ata_journal.entry_count >= ATA_JOURNAL_MAX_ENTRIES) {
        if (automatic) (void)ata_journal_transaction_end(false);
        return false;
    }

    uint8_t old_data[SECTOR_SIZE];
    uint32_t slot = ata_journal.entry_count;
    bool result = ata_read_sector_impl(base, lba, old_data, is_master) &&
        ata_write_sector_impl(base, ata_journal.data_lba + slot, old_data,
                              is_master);
    if (result) {
        ata_journal.entries[slot].target_lba = lba;
        ata_journal.entries[slot].data_crc32 =
            ata_journal_crc32(old_data, sizeof(old_data));
        ata_journal.entry_count++;
        if (slot == 0U && ++ata_journal.sequence == 0U) result = false;
    }
    if (result) result = ata_journal_write_active();
    if (result) result = ata_write_sector_impl(base, lba, buffer, is_master);
    if (automatic) result = ata_journal_transaction_end(result) && result;
    return result;
}

static bool ata_journal_attach_impl(unsigned short base, bool is_master,
                                    uint32_t partition_lba,
                                    uint32_t volume_sectors,
                                    uint16_t reserved_sectors) {
    ata_transaction_begin();
    bool result = true;
    ata_journal.enabled = false;
    if (reserved_sectors <= ATA_JOURNAL_DATA_OFFSET || volume_sectors == 0U ||
        partition_lba > UINT32_MAX - ATA_JOURNAL_DATA_OFFSET ||
        volume_sectors > UINT32_MAX - partition_lba) goto done;

    uint32_t header_lba = partition_lba + ATA_JOURNAL_HEADER_OFFSET;
    uint32_t data_lba = partition_lba + ATA_JOURNAL_DATA_OFFSET;
    uint32_t mirror_lba = reserved_sectors > ATA_JOURNAL_MIRROR_OFFSET
        ? partition_lba + ATA_JOURNAL_MIRROR_OFFSET : 0U;
    ata_journal_record_t primary, mirror, record;
    bool primary_read = ata_read_sector_impl(base, header_lba, &primary,
                                             is_master);
    bool mirror_read = mirror_lba != 0U &&
        ata_read_sector_impl(base, mirror_lba, &mirror, is_master);
    /* Only images explicitly provisioned by our builder opt in. */
    bool primary_marked = primary_read && primary.magic == ATA_JOURNAL_MAGIC;
    bool mirror_marked = mirror_read && mirror.magic == ATA_JOURNAL_MAGIC;
    if (!primary_marked && !mirror_marked) goto done;
    ata_journal.base = base;
    ata_journal.is_master = is_master;
    ata_journal.header_lba = header_lba;
    ata_journal.mirror_lba = mirror_lba;
    ata_journal.data_lba = data_lba;
    ata_journal.volume_start_lba = partition_lba;
    ata_journal.volume_end_lba = partition_lba + volume_sectors;
    ata_journal.entry_count = 0U;
    ata_journal.transaction_depth = 0U;
    bool primary_valid = primary_marked &&
        (primary.version == 1U
            ? ata_journal_v1_valid((ata_journal_v1_record_t *)&primary)
            : ata_journal_record_valid(&primary));
    bool mirror_valid = mirror_marked && mirror.version == ATA_JOURNAL_VERSION &&
                        ata_journal_record_valid(&mirror);
    bool repair_headers = !primary_valid || (mirror_lba != 0U && !mirror_valid);
    if (!primary_valid && !mirror_valid) {
        result = false;
        goto done;
    }
    if (primary_valid && mirror_valid && primary.version == ATA_JOURNAL_VERSION) {
        if (primary.sequence > mirror.sequence) {
            record = primary;
            repair_headers = true;
        } else if (mirror.sequence > primary.sequence) {
            record = mirror;
            repair_headers = true;
        }
        else if (primary.state != mirror.state) {
            record = primary.state == ATA_JOURNAL_ACTIVE ? primary : mirror;
            repair_headers = true;
        } else if (memcmp(&primary, &mirror, sizeof(primary)) != 0) {
            result = false;
            goto done;
        } else record = primary;
    } else if (mirror_valid &&
               (!primary_valid || primary.version != ATA_JOURNAL_VERSION)) {
        record = mirror;
        repair_headers = true;
    } else {
        record = primary;
        repair_headers = true;
    }
    if (record.version == 1U) {
        ata_journal_v1_record_t *old = (ata_journal_v1_record_t *)&record;
        ata_journal.sequence = old->sequence;
        if (result && old->state == ATA_JOURNAL_ACTIVE) {
            uint8_t data[SECTOR_SIZE];
            result = old->target_lba >= ata_journal.volume_start_lba &&
                old->target_lba < ata_journal.volume_end_lba &&
                ata_read_sector_impl(base, data_lba, data, is_master) &&
                ata_journal_crc32(data, sizeof(data)) == old->data_crc32 &&
                ata_write_sector_impl(base, old->target_lba, data, is_master);
        }
        if (result) result = ata_journal_clear();
    } else {
        result = reserved_sectors >
                     ATA_JOURNAL_DATA_OFFSET + ATA_JOURNAL_MAX_ENTRIES - 1U &&
                 ata_journal_record_valid(&record);
        ata_journal.sequence = record.sequence;
        for (uint32_t i = record.entry_count; result && i > 0U; --i) {
            uint32_t index = i - 1U;
            uint32_t target = record.entries[index].target_lba;
            uint8_t data[SECTOR_SIZE];
            result = target >= ata_journal.volume_start_lba &&
                target < ata_journal.volume_end_lba &&
                !(target >= header_lba &&
                  target < data_lba + ATA_JOURNAL_MAX_ENTRIES) &&
                ata_read_sector_impl(base, data_lba + index, data, is_master) &&
                ata_journal_crc32(data, sizeof(data)) ==
                    record.entries[index].data_crc32 &&
                ata_write_sector_impl(base, target, data, is_master);
        }
        if (result && record.state == ATA_JOURNAL_ACTIVE)
            result = ata_journal_clear();
        else if (result && repair_headers)
            result = ata_journal_clear();
    }
    if (!result) goto done;
    ata_journal.enabled = true;
done:
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
            bool result = armed && ahci_write_sector(parent, absolute, buffer) &&
                ahci_flush(parent);
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
            bool result = armed && ahci_write_sector(ahci_drive, lba, buffer) &&
                          ahci_flush(ahci_drive);
            if (armed && !storage_write_end(result)) result = false;
            return result;
        }
    }
    ata_transaction_begin();
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
        for (uint32_t index = 0U; result && index < count; ++index)
            result = ahci_write_sector(ahci_drive, lba + index,
                                       bytes + index * SECTOR_SIZE);
        if (armed && !storage_write_end(result)) result = false;
        return result;
    }
    ata_transaction_begin();
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
        ata_transaction_begin();
        int resource = ata_resource_index(parent->base, parent->is_master);
        bool armed = !ata_write_fenced && resource >= 0 &&
            storage_write_begin((uint32_t)resource, pit_monotonic_ms());
        bool result = false;
        if (armed) {
            drive_t *drive = parent;
            if (ata_select_target(parent->base,
                                  parent->is_master ? 0xE0U : 0xF0U,
                                  ATA_WAIT_TIMEOUT_MS)) {
                outb(ATA_COMMAND(parent->base), drive->lba48_supported ?
                    ATA_FLUSH_CACHE_EXT : 0xE7U);
                result = wait_for_drive_ready(parent->base,
                                              ATA_WAIT_TIMEOUT_MS);
            }
        }
        if (armed && !storage_write_end(result)) result = false;
        ata_transaction_end();
        return result;
    }
    drive_t *ahci_drive = ata_compat_ahci_drive(base);
    if (ahci_drive != NULL) return ahci_flush(ahci_drive);
    ata_transaction_begin();
    int resource = ata_resource_index(base, is_master);
    bool armed = !ata_write_fenced && resource >= 0 &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    bool result = false;
    if (armed) {
        drive_t *drive = &detected_drives[resource];
        if (ata_select_target(base, is_master ? 0xE0U : 0xF0U,
                              ATA_WAIT_TIMEOUT_MS)) {
            outb(ATA_COMMAND(base), drive->lba48_supported ?
                                      ATA_FLUSH_CACHE_EXT : 0xE7U);
            result = wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS);
        }
    }
    if (armed && !storage_write_end(result)) result = false;
    ata_transaction_end();
    return result;
}

void ata_fence_writes(void) {
    ata_write_fenced = true;
    __asm__ volatile("" ::: "memory");
}

bool ata_writes_quiescent(void) {
    if (!ata_write_fenced) return false;
    for (short i = 0; i < drive_count; ++i) {
        if (detected_drives[i].type != DRIVE_TYPE_ATA) continue;
        uint8_t status = inb(ATA_ALT_STATUS(detected_drives[i].base));
        if ((status & (0x80U | 0x08U)) != 0U) return false;
    }
    return true;
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
    for (uint32_t bus = 0U; bus < ATA_CHANNEL_COUNT; bus++) {
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
    ata_transaction_begin();
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
    ata_transaction_begin();
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
