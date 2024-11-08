#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "drivers/drives.h"


#pragma pack(push, 1) // Ensure no padding between struct fields

typedef struct {
    uint8_t  jumpCode[3];           // Jump instruction
    char     oemName[8];            // OEM identifier
    uint16_t bytesPerSector;        // Bytes per sector
    uint8_t  sectorsPerCluster;     // Sectors per cluster
    uint16_t reservedSectors;       // Number of reserved sectors
    uint8_t  fatCount;              // Number of FATs
    uint16_t rootEntryCount;        // Max entries in the root directory (FAT12/16)
    uint16_t totalSectors16;        // Total sectors (if less than 65536)
    uint8_t  mediaDescriptor;       // Media descriptor
    uint16_t sectorsPerFAT16;       // Sectors per FAT (FAT12/16)
    uint16_t sectorsPerTrack;       // Sectors per track (for CHS addressing)
    uint16_t heads;                 // Number of heads (for CHS addressing)
    uint32_t hiddenSectors;         // Number of hidden sectors
    uint32_t totalSectors32;        // Total sectors (if more than 65535)

    // FAT32-specific fields
    uint32_t sectorsPerFAT32;       // Sectors per FAT (FAT32 only)
    uint16_t extFlags;              // Flags (FAT32 only)
    uint16_t fsVersion;             // Version (FAT32 only)
    uint32_t rootCluster;           // Root directory start cluster (FAT32 only)
    uint16_t fsInfo;                // File system info sector (FAT32 only)
    uint16_t backupBootSector;      // Backup boot sector location (FAT32 only)
    uint8_t  reserved[12];          // Reserved for future expansion

    uint8_t  driveNumber;           // Drive number (BIOS)
    uint8_t  reserved1;             // Reserved
    uint8_t  bootSignature;         // Extended boot signature (0x29)
    uint32_t volumeID;              // Volume ID (serial number)
    char     volumeLabel[11];       // Volume label
    char     fsType[8];             // File system type (e.g., "FAT12   ", "FAT16   ", "FAT32   ", "NTFS    ")
} BootSector;

#pragma pack(pop)

void init_fs(drive_t* drive);

#endif // FILESYSTEM_H