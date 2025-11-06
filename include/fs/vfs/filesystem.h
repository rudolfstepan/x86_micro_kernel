#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "drivers/bus/drives.h"


#pragma pack(push, 1) // Ensure no padding between struct fields
typedef struct {
    uint8_t  jump_code[3];           // Jump instruction
    char     oem_name[8];            // OEM identifier
    uint16_t bytes_per_sector;       // Bytes per sector
    uint8_t  sectors_per_cluster;    // Sectors per cluster
    uint16_t reserved_sector_count;  // Number of reserved sectors
    uint8_t  num_fats;               // Number of FATs
    uint16_t max_root_entries;       // Max entries in the root directory (FAT12/16)
    uint16_t total_sectors_16;       // Total sectors (if less than 65536)
    uint8_t  media_descriptor;       // Media descriptor
    uint16_t sectors_per_fat_16;     // Sectors per FAT (FAT12/16)
    uint16_t sectors_per_track;      // Sectors per track (for CHS addressing)
    uint16_t num_heads;              // Number of heads (for CHS addressing)
    uint32_t hidden_sectors;         // Number of hidden sectors
    uint32_t total_sectors_32;       // Total sectors (if more than 65535)

    // FAT32-specific fields
    uint32_t sectors_per_fat_32;     // Sectors per FAT (FAT32 only)
    uint16_t extended_flags;         // Flags (FAT32 only)
    uint16_t file_system_version;    // Version (FAT32 only)
    uint32_t root_cluster;           // Root directory start cluster (FAT32 only)
    uint16_t fs_info_sector;         // File system info sector (FAT32 only)
    uint16_t backup_boot_sector;     // Backup boot sector location (FAT32 only)
    uint8_t  reserved[12];           // Reserved for future expansion

    uint8_t  drive_number;           // Drive number (BIOS)
    uint8_t  reserved1;              // Reserved
    uint8_t  boot_signature;         // Extended boot signature (0x29)
    uint32_t volume_id;              // Volume ID (serial number)
    char     volume_label[11];       // Volume label
    char     file_system_type[8];    // File system type (e.g., "FAT12   ", "FAT16   ", "FAT32   ", "NTFS    ")
} boot_sector_t;
#pragma pack(pop)


void init_fs(drive_t* drive);


#endif // FILESYSTEM_H