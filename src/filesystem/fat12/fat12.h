#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stdbool.h>

#include "toolchain/strings.h"
#include "toolchain/stdlib.h"
#include "drivers/fdd/fdd.h"

#pragma pack(push, 1)
typedef struct {
    uint8_t  jumpCode[3];         // Jump instruction to the bootstrap code
    char     oemName[8];          // OEM Name
    uint16_t bytesPerSector;      // Bytes per sector
    uint8_t  sectorsPerCluster;   // Sectors per cluster
    uint16_t reservedSectors;     // Number of reserved sectors
    uint8_t  fatCount;            // Number of FATs
    uint16_t rootEntryCount;      // Maximum number of root directory entries
    uint16_t totalSectors;        // Total sectors (if < 65536)
    uint8_t  mediaDescriptor;     // Media descriptor
    uint16_t sectorsPerFAT;       // Sectors per FAT
    uint16_t sectorsPerTrack;     // Sectors per track (geometry info)
    uint16_t heads;               // Number of heads (geometry info)
    uint32_t hiddenSectors;       // Hidden sectors
    uint32_t totalSectorsLarge;   // Total sectors (if >= 65536)
    uint8_t  driveNumber;         // Drive number
    uint8_t  reserved0;           // Reserved
    uint8_t  bootSignature;       // Boot signature
    uint32_t volumeID;            // Volume serial number
    char     volumeLabel[11];     // Volume label
    char     fsType[8];           // File system type
    uint8_t  bootCode[448];       // Bootstrap code
    uint16_t bootSectorSignature; // End of sector signature (should be 0xAA55)
} Fat12BootSector;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    unsigned char filename[8];       // 0xE5 indicates deleted file
    char          extension[3];
    uint8_t       attributes;        // File attributes
    uint8_t       reserved;          // Reserved for system use
    uint8_t       createTimeTenths;  // Creation time in tenths of a second
    uint16_t      createTime;        // Creation time
    uint16_t      createDate;        // Creation date
    uint16_t      lastAccessDate;    // Last access date
    uint16_t      firstClusterHigh;  // High word of first cluster (FAT16/FAT32)
    uint16_t      writeTime;         // Last write time
    uint16_t      writeDate;         // Last write date
    uint16_t      firstClusterLow;   // Low word of first cluster
    uint32_t      fileSize;          // File size in bytes
} DirectoryEntry;
#pragma pack(pop)

// FAT12 structure definition
#pragma pack(push, 1)
typedef struct {
    Fat12BootSector bootSector;
    int fatStart;           // Start sector of the FAT
    int rootDirStart;       // Start sector of the Root Directory
    int dataStart;          // Start sector of the Data region
} FAT12;
#pragma pack(pop)

bool fat12_init_fs();
bool fat12_read_dir(const char* path, char* buffer, unsigned int* size);

#endif // FAT12_H
