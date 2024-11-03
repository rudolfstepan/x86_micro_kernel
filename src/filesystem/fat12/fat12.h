#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stdbool.h>

#include "toolchain/strings.h"
#include "toolchain/stdlib.h"
#include "drivers/fdd/fdd.h"

#pragma pack(push, 1)

typedef struct {
    uint8_t  jumpCode[3];
    char     oemName[8];
    uint16_t bytesPerSector;
    uint8_t  sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t  fatCount;
    uint16_t rootEntryCount;
    uint16_t totalSectors;
    uint8_t  mediaDescriptor;
    uint16_t sectorsPerFAT;
    uint16_t sectorsPerTrack;
    uint16_t heads;
    uint32_t hiddenSectors;
    uint32_t totalSectorsLarge;
    uint8_t  driveNumber;
    uint8_t  reserved;
    uint8_t  bootSignature;
    uint32_t volumeID;
    char     volumeLabel[11];
    char     fsType[8];
} BootSector;

typedef struct {
    signed char filename[8];  // Define as signed to allow negative values like 0xE5
    char     extension[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  createTimeTenths;
    uint16_t createTime;
    uint16_t createDate;
    uint16_t lastAccessDate;
    uint16_t firstClusterHigh;
    uint16_t writeTime;
    uint16_t writeDate;
    uint16_t firstClusterLow;
    uint32_t fileSize;
} DirectoryEntry;

#pragma pack(pop)

// FAT12 structure definition
typedef struct {
    BootSector bootSector;
    int fatStart;           // Start sector of the FAT table
    int rootDirStart;       // Start sector of the Root Directory
    int dataStart;          // Start sector of the Data region
} FAT12;

bool fat12_init_fs();
bool fat12_read_dir(const char* path, char* buffer, unsigned int* size);

#endif // FAT12_H
