// ATA driver
#include "ata.h"
#include "../char/io.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/fdd.h"
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
        if (drive->type == DRIVE_TYPE_ATA && drive->base == base &&
            drive->is_master == is_master) return index;
    }
    return -1;
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

static ata_cache_entry_t* ata_cache_slot(unsigned short base,
                                         unsigned int lba, bool is_master) {
    unsigned int drive = is_master ? 0U : 1U;
    unsigned int controller = base == ATA_PRIMARY_IO ? 0U : 2U;
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
    for (volatile int i = 0; i < 4; i++) {
        inb(ATA_ALT_STATUS(base));
    }
    pit_delay(10);  // Extra delay for drive selection
    
    // Set SRST (Software Reset) bit in Device Control register
    outb(ATA_CONTROL(base), 0x04);
    pit_delay(10);  // Wait 10ms (increased)
    
    // Clear SRST bit (enable interrupts again)
    outb(ATA_CONTROL(base), 0x00);
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
    if (buffer == NULL || lba >= ATA_LBA28_LIMIT ||
        (base != ATA_PRIMARY_IO && base != ATA_SECONDARY_IO)) {
        return false;
    }
    ata_cache_entry_t* cached = ata_cache_slot(base, lba, is_master);
    if (cached->valid && cached->base == base && cached->lba == lba &&
        cached->is_master == is_master) {
        memcpy(buffer, cached->data, SECTOR_SIZE);
        return true;
    }
    //printf("ata_read_sector: base=0x%X, lba=%u, is_master=%d\n", base, lba, is_master);
    
    // On first read attempt, try a soft reset if drive isn't responding
    static bool first_read_attempted[2] = {false, false};  // Track per controller
    int controller_idx = (base == ATA_PRIMARY_IO) ? 0 : 1;
    
    if (!first_read_attempted[controller_idx]) {
        uint8_t status = inb(ATA_STATUS(base));
        if (status == 0x00 || status == 0xFF) {
            printf("  Drive status invalid (0x%02X), attempting soft reset...\n", status);
            ata_soft_reset(base, is_master);
        }
        first_read_attempted[controller_idx] = true;
    }
    
    // Wait for the drive to be ready
    //printf("  Step 1: Waiting for drive ready...\n");
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        //printf("  ERROR: Drive not ready (timeout)\n");
        consecutive_read_failures++;
        return false;  // Drive not ready within the timeout
    }
    //printf("  Step 1: Drive ready OK\n");

    // Set the drive/head register for LBA mode FIRST (before other registers)
    unsigned char drive_head = 0xE0 | ((lba >> 24) & 0x0F); // LBA mode with upper LBA bits
    drive_head |= is_master ? 0x00 : 0x10; // 0x00 for master, 0x10 for slave
    //printf("  Step 2: Selecting drive (drive_head=0x%02X, is_master=%d)...\n", drive_head, is_master);
    outb(ATA_DRIVE_HEAD(base), drive_head);
    
    // Wait 400ns after drive selection (ATA spec requirement)
    for (volatile int i = 0; i < 4; i++) {
        inb(ATA_ALT_STATUS(base));  // Read alternate status 4 times for 400ns delay
    }
    
    // Wait for drive to acknowledge selection
    //printf("  Step 2: Waiting for drive to acknowledge selection...\n");
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        //printf("  ERROR: Drive not ready after selection\n");
        consecutive_read_failures++;
        return false;
    }
    
    // Set up sector count and LBA registers
    //printf("  Step 2: Setting up LBA registers...\n");
    outb(ATA_SECTOR_CNT(base), 1); // Read 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));
    //printf("  Step 2: LBA registers set OK\n");

    // Send the read command
    //printf("  Step 3: Sending READ command...\n");
    outb(ATA_COMMAND(base), ATA_READ_SECTORS);
    
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

bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer,
                     bool is_master) {
    ata_transaction_begin();
    bool result = ata_read_sector_impl(base, lba, buffer, is_master);
    ata_transaction_end();
    return result;
}

bool ata_read_sector_fresh(unsigned short base, unsigned int lba, void *buffer,
                           bool is_master) {
    ata_transaction_begin();
    if (buffer == NULL || lba >= ATA_LBA28_LIMIT ||
        (base != ATA_PRIMARY_IO && base != ATA_SECONDARY_IO)) {
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
    if (buffer == NULL || lba >= ATA_LBA28_LIMIT ||
        (base != ATA_PRIMARY_IO && base != ATA_SECONDARY_IO)) {
        return false; // Error: Buffer is null
    }
    /* Never serve data cached before a write attempt.  Invalidating first is
     * conservative when hardware reports a partial or uncertain failure. */
    ata_cache_entry_t* cached = ata_cache_slot(base, lba, is_master);
    if (cached->valid && cached->base == base && cached->lba == lba &&
        cached->is_master == is_master) {
        cached->valid = false;
    }

    // Wait for the drive to be ready
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        return false;  // Drive not ready within the timeout
    }

    // Select the target before programming its task-file registers.
    unsigned char drive_head = 0xE0 | ((lba >> 24) & 0x0F);  // LBA mode with upper LBA bits
    drive_head |= is_master ? 0x00 : 0x10;  // 0x00 for master, 0x10 for slave
    outb(ATA_DRIVE_HEAD(base), drive_head);
    for (volatile int i = 0; i < 4; ++i) inb(ATA_ALT_STATUS(base));
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) return false;

    // Program the selected device's task-file registers.
    outb(ATA_SECTOR_CNT(base), 1); // Write 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));

    // Send the write command
    outb(ATA_COMMAND(base), ATA_WRITE_SECTORS);

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
    outb(ATA_COMMAND(base), 0xE7);  // FLUSH CACHE command
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        printf("Warning: Cache flush timeout\n");
        return false;
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

bool ata_journal_attach(unsigned short base, bool is_master,
                        uint32_t partition_lba, uint32_t volume_sectors,
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

bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer,
                      bool is_master) {
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

bool ata_flush_cache(unsigned short base, bool is_master) {
    ata_transaction_begin();
    int resource = ata_resource_index(base, is_master);
    bool armed = !ata_write_fenced && resource >= 0 &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    bool result = false;
    if (armed) {
        outb(ATA_DRIVE_HEAD(base), is_master ? 0xE0U : 0xF0U);
        for (volatile int i = 0; i < 4; ++i) (void)inb(ATA_ALT_STATUS(base));
        if (wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
            outb(ATA_COMMAND(base), 0xE7U);
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
static void ata_detect_drives_impl(void) {
    uint16_t bases[2] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO };
    uint8_t drives[2] = { ATA_MASTER, ATA_SLAVE };
    int drive_name_index = 0;  // For generating names like "hdd1", "hdd2", etc.

    drive_count = 0;  // Reset drive count before detection
    
    //printf("Starting ATA drive detection...\n");

    // Detect ATA drives
    for (int bus = 0; bus < 2; bus++) {
        for (int drive = 0; drive < 2; drive++) {
            if (drive_count >= MAX_ATA_DRIVES) {
               // printf("Maximum number of drives reached.\n");
                return;
            }

            // Use a temporary structure to avoid corrupting detected_drives on failure
            drive_t temp_drive;
            temp_drive.base = bases[bus];
            temp_drive.is_master = (drive == 0);  // 0 for master, 1 for slave

            // Attempt to identify the drive
            if (ata_identify_drive(bases[bus], drives[drive], &temp_drive)) {

                // Trim trailing spaces from the model name
                trim_trailing_spaces(temp_drive.model);

                temp_drive.type = DRIVE_TYPE_ATA;
                snprintf(temp_drive.name, sizeof(temp_drive.name), "hdd%d", drive_name_index++);
                
                // Initialize mount_point to empty
                temp_drive.mount_point[0] = '\0';
                
                //printf("ATA drive %s detected: %s, Sectors: %u\n", temp_drive.name, temp_drive.model, temp_drive.sectors);

                // Copy the successfully identified drive to the detected_drives array
                detected_drives[drive_count] = temp_drive;

                // Increment the global drive count after successfully adding a drive
                drive_count++;
            }
        }
    }

    //printf("ATA detection complete. Total ATA drives: %d\n", drive_count);
}

void ata_detect_drives(void) {
    ata_transaction_begin();
    ata_detect_drives_impl();
    ata_transaction_end();
}

static bool ata_identify_drive_impl(uint16_t base, uint8_t drive,
                                    drive_t *drive_info) {
    if (!drive_info || (base != ATA_PRIMARY_IO && base != ATA_SECONDARY_IO)) return false;

    // Select the drive (master or slave)
    outb(base + 6, drive);
    for (volatile int i = 0; i < 4; ++i) inb(ATA_ALT_STATUS(base));

    // IDENTIFY requires the task-file address/count registers to be zero.
    outb(ATA_SECTOR_CNT(base), 0);
    outb(ATA_LBA_LOW(base), 0);
    outb(ATA_LBA_MID(base), 0);
    outb(ATA_LBA_HIGH(base), 0);

    // Send the IDENTIFY command
    outb(base + 7, ATA_IDENTIFY);

    // Wait a bit for the drive to respond
    if (inb(base + 7) == 0) {
        return false;  // No drive present
    }

    // Wait until BSY clears and DRQ sets with timeout
    if (!wait_for_drive_ready(base, ATA_DETECTION_TIMEOUT_MS)) {
        return false;  // Timeout waiting for drive
    }
    
    if (!wait_for_drive_data_ready(base, ATA_DETECTION_TIMEOUT_MS)) {
        return false;  // DRQ not set, not an ATA device
    }

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

    // Get the total sector count (LBA28; words 60-61)
    drive_info->sectors = identify_data[60] | (identify_data[61] << 16);

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
