// ATA driver
#include "ata.h"
#include "drivers/char/io.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/fdd.h"
#include "kernel/time/pit.h"  // For pit_delay() in kernel context
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Build configuration
#ifdef QEMU_BUILD
    #define ATA_WAIT_TIMEOUT_MS 500      // QEMU is fast, shorter timeout
    #define ATA_POLL_DELAY_MS 1          // QEMU: poll every 1ms
    #define ATA_DETECTION_TIMEOUT_MS 100 // QEMU detection is quick
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


bool wait_for_drive_ready(unsigned short base, unsigned int timeout_ms) {
    unsigned int elapsed_time = 0;
    
#ifdef QEMU_BUILD
    // QEMU: Wait for BSY to clear AND RDY to be set
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
    
#ifdef QEMU_BUILD
    // QEMU: First wait for BSY to clear, then wait for DRQ
    printf("      wait_for_drive_data_ready: Step A - waiting for BSY clear\n");
    // Step 1: Wait for BSY to clear
    while (inb(ATA_STATUS(base)) & 0x80) {  // Wait while BSY bit is set
        if (elapsed_time >= timeout_ms) {
            printf("      ERROR: BSY still set after %u ms\n", elapsed_time);
            return false;
        }
        pit_delay(ATA_POLL_DELAY_MS);
        elapsed_time += ATA_POLL_DELAY_MS;
        
        if (elapsed_time % 50 == 0) {  // Debug every 50ms
            uint8_t status = inb(ATA_STATUS(base));
            printf("      [%u ms] status=0x%02X (BSY still set)\n", elapsed_time, status);
        }
    }
    printf("      wait_for_drive_data_ready: Step A OK - BSY cleared after %u ms\n", elapsed_time);
    
    // Step 2: Wait for DRQ to be set
    printf("      wait_for_drive_data_ready: Step B - waiting for DRQ set\n");
    while (!(inb(ATA_STATUS(base)) & 0x08)) {  // Wait for DRQ bit to set
        if (elapsed_time >= timeout_ms) {
            uint8_t status = inb(ATA_STATUS(base));
            printf("      ERROR: DRQ not set. Final status=0x%02X after %u ms\n", status, elapsed_time);
            return false;
        }
        pit_delay(ATA_POLL_DELAY_MS);
        elapsed_time += ATA_POLL_DELAY_MS;
        
        if (elapsed_time % 50 == 0) {  // Debug every 50ms
            uint8_t status = inb(ATA_STATUS(base));
            printf("      [%u ms] status=0x%02X (waiting for DRQ)\n", elapsed_time, status);
        }
    }
    printf("      wait_for_drive_data_ready: Step B OK - DRQ set after %u ms\n", elapsed_time);
    
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

/*
    * Reads a sector from the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to read.
    * @param buffer The buffer to read the sector into.
    * @return True if the sector was read successfully, false otherwise.
*/
bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master) {
    printf("ata_read_sector: base=0x%X, lba=%u, is_master=%d\n", base, lba, is_master);
    
    // Wait for the drive to be ready
    printf("  Step 1: Waiting for drive ready...\n");
    if (!wait_for_drive_ready(base, 1000)) {  // 1000 ms timeout
        printf("  ERROR: Drive not ready (timeout)\n");
        return false;  // Drive not ready within the timeout
    }
    printf("  Step 1: Drive ready OK\n");

    // Set up sector count and LBA
    printf("  Step 2: Setting up LBA registers...\n");
    outb(ATA_SECTOR_CNT(base), 1); // Read 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));

    // Set the drive/head register for LBA mode
    unsigned char drive_head = 0xE0 | ((lba >> 24) & 0x0F); // LBA mode with upper LBA bits
    drive_head |= is_master ? 0x00 : 0x10; // 0x00 for master, 0x10 for slave
    outb(ATA_DRIVE_HEAD(base), drive_head);
    printf("  Step 2: LBA registers set OK\n");

    // Send the read command
    printf("  Step 3: Sending READ command...\n");
    outb(ATA_COMMAND(base), ATA_READ_SECTORS);
    
    // Small delay after sending command (required by ATA spec)
    printf("  Step 3: Delay after command...\n");
    for (volatile int i = 0; i < 4; i++) {
        inb(ATA_ALT_STATUS(base));  // Read alternate status 4 times for 400ns delay
    }
    printf("  Step 3: Command sent OK\n");

    // Wait for the drive to be ready to transfer data
    printf("  Step 4: Waiting for data ready...\n");
    if (!wait_for_drive_data_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        printf("  ERROR: Data not ready (timeout)\n");
        return false;  // Drive data not ready within the timeout
    }
    printf("  Step 4: Data ready OK\n");

    // Read the data
    printf("  Step 5: Reading data...\n");
    insw(ATA_DATA(base), buffer, SECTOR_SIZE / 2);
    printf("  Step 5: Data read OK\n");

#ifdef REAL_HARDWARE
    // Real hardware: Wait for command completion
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        return false;
    }
#endif

    printf("ata_read_sector: SUCCESS\n");
    return true;
}

/*
    * Writes a sector to the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to write.
    * @param buffer The buffer to write to the sector.
    * @return True if the sector was written successfully, false otherwise.
*/
bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master) {
    if (buffer == NULL) {
        return false; // Error: Buffer is null
    }

    // Wait for the drive to be ready
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        return false;  // Drive not ready within the timeout
    }

    // Set up the sector count, LBA (Logical Block Addressing), and drive/head
    outb(ATA_SECTOR_CNT(base), 1); // Write 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));

    // Set the drive/head register
    unsigned char drive_head = 0xE0 | ((lba >> 24) & 0x0F);  // LBA mode with upper LBA bits
    drive_head |= is_master ? 0x00 : 0x10;  // 0x00 for master, 0x10 for slave
    outb(ATA_DRIVE_HEAD(base), drive_head);

    // Send the write command
    outb(ATA_COMMAND(base), ATA_WRITE_SECTORS);

    // Wait for the drive to signal that it's ready to receive data
    if (!wait_for_drive_data_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        return false;  // Drive data not ready within the timeout
    }

    // Write the data
    outsw(ATA_DATA(base), buffer, SECTOR_SIZE / 2);

#ifdef REAL_HARDWARE
    // Real hardware: Wait for write completion and flush
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        return false;
    }
    
    // Flush cache to ensure data is written
    outb(ATA_COMMAND(base), 0xE7);  // FLUSH CACHE command
    if (!wait_for_drive_ready(base, ATA_WAIT_TIMEOUT_MS)) {
        printf("Warning: Cache flush timeout\n");
    }
#endif

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
    printf("ata_get_first_hdd: searching for first HDD...\n");
    printf("  drive_count: %d\n", drive_count);
    
    if (drive_count <= 0 || drive_count > MAX_DRIVES) {
        printf("  ERROR: Invalid drive_count: %d\n", drive_count);
        return NULL;
    }
    
    // Search for first ATA drive
    for (int i = 0; i < drive_count; i++) {
        printf("  Checking drive[%d]: name='%s', type=%d\n", 
               i, detected_drives[i].name, detected_drives[i].type);
        
        if (detected_drives[i].type == DRIVE_TYPE_ATA) {
            printf("  FOUND: First HDD is '%s' at index %d\n", 
                   detected_drives[i].name, i);
            return &detected_drives[i];
        }
    }
    
    printf("  NOT FOUND: No ATA HDD in detected drives\n");
    return NULL;
}

// Function to detect all ATA drives on the primary and secondary buses
void ata_detect_drives() {
    uint16_t bases[2] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO };
    uint8_t drives[2] = { ATA_MASTER, ATA_SLAVE };
    int drive_name_index = 0;  // For generating names like "hdd1", "hdd2", etc.

    drive_count = 0;  // Reset drive count before detection
    
    printf("Starting ATA drive detection...\n");

    // Detect ATA drives
    for (int bus = 0; bus < 2; bus++) {
        for (int drive = 0; drive < 2; drive++) {
            if (drive_count >= MAX_DRIVES) {
                printf("Maximum number of drives reached.\n");
                return;
            }

            drive_t* ata_drive_info = &detected_drives[drive_count];
            ata_drive_info->base = bases[bus];
            ata_drive_info->is_master = (drive == 0);  // 0 for master, 1 for slave

            // Attempt to identify the drive
            if (ata_identify_drive(bases[bus], drives[drive], ata_drive_info)) {

                // Trim trailing spaces from the model name
                trim_trailing_spaces(ata_drive_info->model);

                ata_drive_info->type = DRIVE_TYPE_ATA;
                snprintf(ata_drive_info->name, sizeof(ata_drive_info->name), "hdd%d", drive_name_index++);
                printf("ATA drive %s detected: %s, Sectors: %u\n", ata_drive_info->name, ata_drive_info->model, ata_drive_info->sectors);

                // Initialize the file system for the detected drive
                //init_fs(ata_drive_info);

                // Increment the global drive count after successfully adding a drive
                drive_count++;
            }
        }
    }
    
    printf("ATA detection complete. Total ATA drives: %d\n", drive_count);
}

bool ata_identify_drive(uint16_t base, uint8_t drive, drive_t *drive_info) {
    // Select the drive (master or slave)
    outb(base + 6, drive);

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

drive_t* get_drive_by_name(const char* name) {
    printf("get_drive_by_name: searching for '%s'\n", name);
    printf("  drive_count address: %p, value: %d\n", &drive_count, drive_count);
    printf("  detected_drives address: %p\n", detected_drives);
    
    if (drive_count < 0 || drive_count > MAX_DRIVES) {
        printf("  ERROR: Invalid drive_count value: %d\n", drive_count);
        return NULL;
    }
    
    for (int i = 0; i < drive_count; i++) {
        printf("  Checking drive[%d]: '%s' vs '%s'\n", i, detected_drives[i].name, name);
        if (strcmp(detected_drives[i].name, name) == 0) {
            printf("  FOUND: drive '%s' at index %d\n", name, i);
            return &detected_drives[i];
        }
    }
    
    printf("  NOT FOUND: drive '%s' not in list\n", name);
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
