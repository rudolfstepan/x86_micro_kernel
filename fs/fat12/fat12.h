#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stdbool.h>

#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/fdd.h"

#pragma pack(push, 1)
typedef struct {
    uint8_t  jump_code[3];            // Jump instruction to the bootstrap code
    char     oem_name[8];             // OEM Name
    uint16_t bytes_per_sector;        // Bytes per sector
    uint8_t  sectors_per_cluster;     // Sectors per cluster
    uint16_t reserved_sectors;        // Number of reserved sectors
    uint8_t  fat_count;               // Number of FATs
    uint16_t root_entry_count;        // Maximum number of root directory entries
    uint16_t total_sectors;           // Total sectors (if < 65536)
    uint8_t  media_descriptor;        // Media descriptor
    uint16_t sectors_per_fat;         // Sectors per FAT
    uint16_t sectors_per_track;       // Sectors per track (geometry info)
    uint16_t heads;                   // Number of heads (geometry info)
    uint32_t hidden_sectors;          // Hidden sectors
    uint32_t total_sectors_large;     // Total sectors (if >= 65536)
    uint8_t  drive_number;            // Drive number
    uint8_t  reserved0;               // Reserved
    uint8_t  boot_signature;          // Boot signature
    uint32_t volume_id;               // Volume serial number
    char     volume_label[11];        // Volume label
    char     fs_type[8];              // File system type
    uint8_t  boot_code[448];          // Bootstrap code
    uint16_t boot_sector_signature;   // End of sector signature (should be 0xAA55)
} fat12_boot_sector;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    unsigned char filename[8];          // 0xE5 indicates deleted file
    char          extension[3];
    uint8_t       attributes;           // File attributes
    uint8_t       reserved;             // Reserved for system use
    uint8_t       create_time_tenths;   // Creation time in tenths of a second
    uint16_t      create_time;          // Creation time
    uint16_t      create_date;          // Creation date
    uint16_t      last_access_date;     // Last access date
    uint16_t      first_cluster_high;   // High word of first cluster (FAT16/FAT32)
    uint16_t      last_write_time;      // Last write time
    uint16_t      last_write_date;      // Last write date
    uint16_t      first_cluster_low;    // Low word of first cluster
    uint32_t      file_size;            // File size in bytes
} directory_entry;
#pragma pack(pop)

// FAT12 structure definition
#pragma pack(push, 1)
typedef struct {
    fat12_boot_sector boot_sector;
    int fat_start;           // Start sector of the FAT
    int root_dir_start;      // Start sector of the Root Directory
    int data_start;          // Start sector of the Data region
    uint8_t* fat;            // Pointer to the FAT table in memory
} fat12_t;
#pragma pack(pop)

typedef struct {
    unsigned char* base;             // Base address of the file data in memory (optional, if preloaded)
    unsigned char* ptr;              // Current read/write position within the file
    unsigned int start_cluster;      // Start cluster of the file (first cluster in the cluster chain)
    const char* mode;                // Mode the file was opened with (e.g., "r", "w")
    unsigned char name[13];          // Name of the file in 8.3 format (FILENAME.EXT\0)
    size_t size;                     // Size of the file in bytes
    size_t position;                 // Current position within the file (offset from base)
    fat12_t* fat12_instance;         // Pointer to the FAT12 structure (for accessing FAT, boot sector, etc.)
} fat12_file;


bool fat12_init_fs(uint8_t drive);
bool fat12_read_dir(const char* path);
int fat12_read_dir_entries(directory_entry* dir);
bool fat12_change_directory(const char* relative_path);

// file operations
fat12_file* fat12_open_file(const char* filename, const char* mode);
int fat12_read_file(fat12_file* file, void* buffer, unsigned int buffer_size, unsigned int bytes_to_read);
void print_file_content(fat12_file* file);

#endif // FAT12_H
