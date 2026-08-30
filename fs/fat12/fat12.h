/**
 * @file fs/fat12/fat12.h
 * @brief FAT12-On-Disk-, Geometrie- und Operationsvertrag.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: 12-Bit-FAT-Zugriffe beachten Sektorgrenzen und feste Mediengeometrie.
 */
#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stdbool.h>

#include "../../lib/libc/string.h"
#include "../../lib/libc/stdlib.h"
#include "../../drivers/block/fdd.h"
#include "fat12_journal.h"
#include "fat12_remap.h"
#include "fat12_replica.h"
#include "fat12_critical.h"

// FAT12 Constants
#define FAT12_SECTOR_SIZE           512
#define FAT12_ROOT_ENTRY_SIZE       32
#define FAT12_MAX_ROOT_ENTRIES      224
#define FAT12_ROOT_DIR_SECTORS      14

// FAT12 Cluster Markers
#define FAT12_MIN_CLUSTER           0x002
#define FAT12_MAX_CLUSTER           0xFEF
#define FAT12_BAD_CLUSTER           0xFF7
#define FAT12_EOC_MIN               0xFF8  // End of chain minimum
#define FAT12_EOC_MAX               0xFFF  // End of chain maximum
#define FAT12_FREE_CLUSTER          0x000
#define FAT12_RESERVED_CLUSTER      0x001
#define FAT12_DEFECT_CONFIRM_READS  3U
#define FAT12_SAFETY_RESERVED_SECTORS \
    (2U + FAT12_JOURNAL_MAX_ENTRIES * 2U + 3U + \
     FAT12_REMAP_SPARE_COUNT + FAT12_REPLICA_RESERVED_SECTORS)
#define FAT12_MIN_REIST_RESERVED_SECTORS \
    (1U + FAT12_SAFETY_RESERVED_SECTORS)

// File Attributes
#define FILE_ATTR_READONLY          0x01
#define FILE_ATTR_HIDDEN            0x02
#define FILE_ATTR_SYSTEM            0x04
#define FILE_ATTR_VOLUME_LABEL      0x08
#define FILE_ATTR_DIRECTORY         0x10
#define FILE_ATTR_ARCHIVE           0x20
#define FILE_ATTR_LONG_NAME         0x0F

// Boot Sector Signature
#define FAT12_BOOT_SIGNATURE        0xAA55

// Floppy Disk Geometry (Standard 1.44MB)
#define FAT12_DEFAULT_SPT           18    // Sectors per track
#define FAT12_DEFAULT_HEADS         2     // Number of heads
#define FAT12_DEFAULT_BPS           512   // Bytes per sector

// Error codes
typedef enum {
    FAT12_SUCCESS = 0,
    FAT12_ERROR_NOT_INITIALIZED,
    FAT12_ERROR_INVALID_PARAMETER,
    FAT12_ERROR_IO_FAILURE,
    FAT12_ERROR_OUT_OF_MEMORY,
    FAT12_ERROR_NOT_FOUND,
    FAT12_ERROR_CORRUPT_FILESYSTEM,
    FAT12_ERROR_INVALID_CLUSTER,
    FAT12_ERROR_INVALID_BOOT_SECTOR
} fat12_error_t;

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
    bool journal_enabled;
    fat12_journal_t journal;
    bool remap_enabled;
    fat12_remap_table_t remap;
    bool replica_enabled;
    fat12_replica_t replicas[FAT12_REPLICA_FILE_COUNT];
    bool transaction_active;
    bool write_fenced;
} fat12_t;
#pragma pack(pop)

typedef struct {
    unsigned char* base;             // Base address of the file data in memory (optional, if preloaded)
    unsigned char* ptr;              // Current read/write position within the file
    unsigned int start_cluster;      // Start cluster of the file (first cluster in the cluster chain)
    char mode[4];                    // Stable open-mode copy (e.g., "r", "w")
    unsigned char name[13];          // Name of the file in 8.3 format (FILENAME.EXT\0)
    size_t size;                     // Size of the file in bytes
    size_t position;                 // Current position within the file (offset from base)
    fat12_t* fat12_instance;         // Pointer to the FAT12 structure (for accessing FAT, boot sector, etc.)
    uint32_t directory_sector;       // Logical sector containing the 8.3 entry
    uint16_t directory_slot;         // Entry index within directory_sector
    uint8_t attributes;
} fat12_file;


// Forward declaration for FAT12
extern bool fat12_init_fs(uint8_t drive);

// Initialization and cleanup
bool fat12_init_fs(uint8_t drive);
void fat12_cleanup(void);

// Directory operations
bool fat12_read_dir(const char* path);
int fat12_read_dir_entries(directory_entry* dir);
bool fat12_change_directory(const char* relative_path);

// File operations
fat12_file* fat12_open_file(const char* filename, const char* mode);
int fat12_read_file(fat12_file* file, void* buffer, unsigned int buffer_size, unsigned int bytes_to_read);
void fat12_close_file(fat12_file* file);
void print_file_content(fat12_file* file);

// Validation and utility functions
bool is_valid_cluster_fat12(int cluster);
bool validate_fat12_boot_sector(fat12_boot_sector* bs);
uint32_t fat12_cluster_count(void);
uint16_t fat12_get_fat_entry(uint16_t cluster);
bool fat12_write_supported(void);
/* Fixed FAT12 scratch is serialized independently of VFS callback nesting.
 * The mutex is recursive because metadata helpers call journaled I/O helpers
 * while retaining distinct workspace owner slots. */
bool fat12_operation_workspace_begin(void);
void fat12_operation_workspace_end(void);
bool fat12_set_fat_entry(uint16_t cluster, uint16_t value);
bool fat12_sync_fat(void);
bool fat12_read_logical_sectors(uint32_t logical_sector, uint32_t count,
                                void* output);
bool fat12_write_logical_sectors(uint32_t logical_sector, uint32_t count,
                                 const void* input);
bool fat12_quarantine_data_cluster(uint16_t cluster);
bool fat12_install_sector_remap(uint32_t bad_sector,
                                const void *recovered_sector);
bool fat12_publish_critical_replica(const char *name, const void *data,
                                    size_t length);
bool fat12_read_critical_replica(const char *name, void *output,
                                 size_t capacity, size_t *length_out);
bool fat12_transaction_begin(uint32_t maximum_unique_sectors);
bool fat12_transaction_commit(void);
void fat12_transaction_fail(void);

#endif // FAT12_H
