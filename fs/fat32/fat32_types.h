#ifndef FAT32_TYPES_H
#define FAT32_TYPES_H

#include <stdint.h>

// FAT32 Directory Entry Structure (32 bytes)
#pragma pack(push, 1)
struct FAT32DirEntry {
    uint8_t name[11];             // Short name (8.3 format)
    uint8_t attr;                 // File attributes
    uint8_t ntRes;                // Reserved for use by Windows NT
    uint8_t crtTimeTenth;         // Millisecond stamp at file creation time
    uint16_t crtTime;             // Time file was created
    uint16_t crtDate;             // Date file was created
    uint16_t lastAccessDate;      // Last access date
    uint16_t firstClusterHigh;    // High word of the first data cluster number
    uint16_t writeTime;           // Time of last write
    uint16_t writeDate;           // Date of last write
    uint16_t firstClusterLow;     // Low word of the first data cluster number
    uint32_t fileSize;            // File size in bytes
};
#pragma pack(pop)

// FAT32 Boot Sector Structure (512 bytes)
#pragma pack(push, 1)
struct Fat32BootSector {
    uint8_t jumpBoot[3];               // Jump instruction to boot code
    uint8_t OEMName[8];                // OEM Name and version
    uint16_t bytesPerSector;           // Bytes per sector
    uint8_t sectorsPerCluster;         // Sectors per cluster
    uint16_t reservedSectorCount;      // Reserved sector count (including boot sector)
    uint8_t numberOfFATs;              // Number of FATs
    uint16_t rootEntryCount;           // (Not used in FAT32)
    uint16_t totalSectors16;           // Total sectors (16-bit), if 0, use totalSectors32
    uint8_t mediaType;                 // Media type
    uint16_t FATSize16;                // (Not used in FAT32)
    uint16_t sectorsPerTrack;          // Sectors per track (for media formatting)
    uint16_t numberOfHeads;            // Number of heads (for media formatting)
    uint32_t hiddenSectors;            // Hidden sectors (preceding the partition)
    uint32_t totalSectors32;           // Total sectors (32-bit)
    uint32_t FATSize32;                // Sectors per FAT
    uint16_t flags;                    // Flags
    uint16_t version;                  // FAT32 version
    uint32_t rootCluster;              // Root directory start cluster
    uint16_t FSInfo;                   // FSInfo sector
    uint16_t backupBootSector;         // Backup boot sector
    uint8_t reserved[12];              // Reserved bytes
    uint8_t driveNumber;               // Drive number
    uint8_t reserved1;                 // Reserved byte
    uint8_t bootSignature;             // Boot signature (indicates next three fields are present)
    uint32_t volumeID;                 // Volume ID serial number
    uint8_t volumeLabel[11];           // Volume label
    uint8_t fileSystemType[8];         // File system type label
    uint8_t bootCode[420];             // Boot code (padded to make structure exactly 512 bytes)
    uint16_t bootSectorSignature;      // 0xAA55 at offset 510-511
};
#pragma pack(pop)

#endif /* FAT32_TYPES_H */
