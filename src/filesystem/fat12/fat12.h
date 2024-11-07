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
    uint16_t      lastWriteTime;     // Last write time
    uint16_t      lastWriteDate;     // Last write date
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
    uint8_t* fat;           // Pointer to the FAT table in memory
} FAT12;
#pragma pack(pop)

typedef struct {
    unsigned char* base;           // Base address of the file data in memory (optional, if preloaded)
    unsigned char* ptr;            // Current read/write position within the file
    unsigned int startCluster;     // Start cluster of the file (first cluster in the cluster chain)
    const char* mode;              // Mode the file was opened with (e.g., "r", "w")
    unsigned char name[13];                 // Name of the file in 8.3 format (FILENAME.EXT\0)
    size_t size;                   // Size of the file in bytes
    size_t position;               // Current position within the file (offset from base)
    struct FAT12* fat12Instance;   // Pointer to the FAT12 structure (for accessing FAT, boot sector, etc.)
} Fat12File;


bool fat12_init_fs(uint8_t drive);
bool fat12_read_dir(const char* path);
int fat12_read_dir_entries(DirectoryEntry* dir);
bool fat12_change_directory(const char* relativePath);

// file operations
Fat12File* fat12_open_file(const char* filename, const char* mode);
int fat12_read_file(Fat12File* file, void* buffer, size_t size);
void print_file_content(Fat12File* file);

#endif // FAT12_H
