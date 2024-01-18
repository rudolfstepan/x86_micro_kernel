#include <stdbool.h>

#ifndef ATA_H
#define ATA_H


#define ATA_PRIMARY_IO 0x1F0
#define ATA_DATA       (ATA_PRIMARY_IO + 0)   // Data register
#define ATA_SECTOR_CNT (ATA_PRIMARY_IO + 2)   // Sector count register
#define ATA_LBA_LOW    (ATA_PRIMARY_IO + 3)   // LBA low register
#define ATA_LBA_MID    (ATA_PRIMARY_IO + 4)   // LBA mid register
#define ATA_LBA_HIGH   (ATA_PRIMARY_IO + 5)   // LBA high register
#define ATA_DRIVE_HEAD (ATA_PRIMARY_IO + 6)   // Drive/head register
#define ATA_STATUS     (ATA_PRIMARY_IO + 7)   // Status register
#define ATA_COMMAND    (ATA_PRIMARY_IO + 7)   // Command register
#define ATA_READ_SECTORS 0x20                 // Read sectors command
#define ATA_WRITE_SECTORS 0x30                // Write sectors command

#define SECTOR_SIZE 512

bool read_sector(unsigned int lba, void* buffer);
bool write_sector(unsigned int lba, const void* buffer);

#endif
