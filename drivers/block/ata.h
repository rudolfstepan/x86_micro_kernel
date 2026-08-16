#ifndef ATA_H
#define ATA_H

#include "../bus/drives.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define ATA_READ_SECTORS    0x20               // Read sectors command
#define ATA_WRITE_SECTORS   0x30               // Write sectors command
#define ATA_READ_SECTORS_EXT 0x24              // 48-bit PIO read
#define ATA_WRITE_SECTORS_EXT 0x34             // 48-bit PIO write
#define ATA_FLUSH_CACHE_EXT  0xEA
#define ATA_IDENTIFY        0xEC               // Identify command
#define ATA_PRIMARY_IO      0x1F0              // Base I/O port for the primary ATA bus
#define ATA_SECONDARY_IO    0x170              // Base I/O port for the secondary ATA bus
#define ATA_MASTER          0xA0               // Master drive selection
#define ATA_SLAVE           0xB0               // Slave drive selection

// Macros to access ATA registers, given a base I/O address (e.g., ATA_PRIMARY_IO or ATA_SECONDARY)
#define ATA_DATA(base)        (base + 0)       // Data register
#define ATA_ERROR(base)       (base + 1)       // Error register
#define ATA_SECTOR_CNT(base)  (base + 2)       // Sector count register
#define ATA_LBA_LOW(base)     (base + 3)       // LBA low register
#define ATA_LBA_MID(base)     (base + 4)       // LBA mid register
#define ATA_LBA_HIGH(base)    (base + 5)       // LBA high register
#define ATA_DRIVE_HEAD(base)  (base + 6)       // Drive/head register
#define ATA_STATUS(base)      (base + 7)       // Status register
#define ATA_COMMAND(base)     (base + 7)       // Command register

// Control registers
#define ATA_ALT_STATUS(base)  ((base) + 0x206) // Alternate status register
#define ATA_DEV_CTRL(base)    ((base) + 0x206) // Device control register
#define ATA_CONTROL(base)     ((base) + 0x206) // Device control register (alias)

#define MAX_ATA_DRIVES      4      // primary/secondary, master/slave
#define MAX_FDD_DRIVES      2
#define MAX_PARTITION_DRIVES 16
#define MAX_DRIVES          (MAX_ATA_DRIVES + MAX_FDD_DRIVES + MAX_PARTITION_DRIVES)
#define ATA_LBA28_LIMIT     0x10000000u
#define ATA_PIO_MAX_SECTORS 20U
#define SECTOR_SIZE 512

// External declarations
extern short drive_count;
extern drive_t detected_drives[MAX_DRIVES];

bool ata_identify_drive(uint16_t base, uint8_t drive, drive_t *drive_info);

drive_t* ata_get_drive(unsigned short drive_index);
drive_t* ata_get_first_hdd();
drive_t* get_drive_by_name(const char* name);

void ata_detect_drives();
void list_detected_drives();
void ata_reset_error_counter();  // Reset consecutive failure counter

bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master);
bool ata_read_sector_fresh(unsigned short base, unsigned int lba, void* buffer,
                           bool is_master);
bool ata_read_sectors(unsigned short base, uint32_t lba, uint32_t count,
                      void *buffer, bool is_master);
bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master);
bool ata_write_sectors(unsigned short base, uint32_t lba, uint32_t count,
                       const void *buffer, bool is_master);
bool ata_flush_cache(unsigned short base, bool is_master);
bool ata_journal_attach(unsigned short base, bool is_master,
                        uint32_t partition_lba, uint32_t volume_sectors,
                        uint16_t reserved_sectors);
bool ata_journal_transaction_begin(void);
bool ata_journal_transaction_end(bool commit);
void ata_fence_writes(void);
bool ata_writes_quiescent(void);


#endif
