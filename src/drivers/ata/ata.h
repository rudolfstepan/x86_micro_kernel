#ifndef ATA_H
#define ATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define ATA_READ_SECTORS      0x20             // Read sectors command
#define ATA_WRITE_SECTORS     0x30             // Write sectors command
#define ATA_IDENTIFY          0xEC             // Identify command

#define ATA_PRIMARY_IO      0x1F0  // Base I/O port for the primary ATA bus
#define ATA_SECONDARY_IO    0x170  // Base I/O port for the secondary ATA bus
#define ATA_MASTER          0xA0  // Master drive selection
#define ATA_SLAVE           0xB0  // Slave drive selection


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

#define MAX_DRIVES          4      // Max of 4 ATA drives (primary/master, primary/slave, secondary/master, secondary/slave)

#define SECTOR_SIZE 512

typedef struct {
    uint16_t base;          // Base I/O port
    uint8_t drive;          // 0 = Master, 1 = Slave
    bool exists;            // True if a drive is detected
    bool is_master;         // True if drive is master, false if slave
    char model[41];         // Drive model string (from IDENTIFY command)
    uint32_t sectors;       // Total sectors (from IDENTIFY command)
    char name[8];           // Drive name (e.g., "hdd1")
} ata_drive_t;


// Forward declaration of functions
bool ata_identify_drive(uint16_t base, uint8_t drive, ata_drive_t *drive_info);
void init_fs(ata_drive_t* drive);

ata_drive_t* ata_get_drive(unsigned short drive_index);
void ata_detect_drives();
void list_detected_drives();
ata_drive_t* get_drive_by_name(const char* name);
bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master);
bool ata_write_sector(unsigned short base, unsigned int lba, const void* buffer, bool is_master);


#endif