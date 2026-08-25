/**
 * @file userspace/storage/include/reist/vfs_shadow_fat32.h
 * @brief Bounded read-only FAT12/FAT32 metadata parser for the storage service.
 */
#ifndef REIST_VFS_SHADOW_FAT32_H
#define REIST_VFS_SHADOW_FAT32_H

#include <stdint.h>

#include "x86os.h"

#define REIST_VFS_SHADOW_MAX_RESOURCES 22U
#define REIST_VFS_SHADOW_MAX_COMPONENTS 32U
#define REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS 128U
#define REIST_VFS_SHADOW_MAX_SECTOR_READS 320U
#define REIST_VFS_SHADOW_OBJECT_VERSION 1U
#define REIST_VFS_SHADOW_OBJECT_FAT 1U
#define REIST_VFS_SHADOW_OBJECT_EXT2 2U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t filesystem;
    uint32_t resource;
    uint32_t volume_signature;
    uint32_t locator_a;
    uint32_t locator_b;
    uint32_t locator_c;
    uint32_t object_generation;
} reist_vfs_shadow_object_t;

typedef int (*reist_vfs_shadow_drive_info_fn)(
    void *context, uint32_t resource, x86os_drive_info_t *info);
typedef int (*reist_vfs_shadow_read_sector_fn)(
    void *context, uint32_t resource, uint32_t sector,
    uint8_t data[X86OS_STORAGE_BLOCK_SIZE]);

typedef struct {
    void *context;
    reist_vfs_shadow_drive_info_fn drive_info;
    reist_vfs_shadow_read_sector_fn read_sector;
} reist_vfs_shadow_io_t;

/** Returns the public SYS_STAT error convention: 0, -2, -5, -22 or -110. */
int reist_vfs_shadow_fat32_stat(const reist_vfs_shadow_io_t *io,
                                const char *absolute_path,
                                uint32_t path_length,
                                x86os_file_info_t *info);

/** Auto-detects Microsoft FAT12 or FAT32; FAT16 is rejected. */
int reist_vfs_shadow_fat_stat(const reist_vfs_shadow_io_t *io,
                              const char *absolute_path,
                              uint32_t path_length,
                              x86os_file_info_t *info);

int reist_vfs_shadow_fat_read(const reist_vfs_shadow_io_t *io,
                              const char *absolute_path,
                              uint32_t path_length, uint32_t offset,
                              uint8_t *data, uint32_t capacity,
                              uint32_t *transferred);

int reist_vfs_shadow_fat_readdir(const reist_vfs_shadow_io_t *io,
                                 const char *absolute_path,
                                 uint32_t path_length, uint32_t index,
                                 x86os_file_info_t *info);

int reist_vfs_shadow_fat_object_open(
    const reist_vfs_shadow_io_t *io, const char *absolute_path,
    uint32_t path_length, reist_vfs_shadow_object_t *object,
    x86os_file_info_t *info);
int reist_vfs_shadow_fat_object_stat(
    const reist_vfs_shadow_io_t *io,
    const reist_vfs_shadow_object_t *object, x86os_file_info_t *info);
int reist_vfs_shadow_fat_object_read(
    const reist_vfs_shadow_io_t *io,
    const reist_vfs_shadow_object_t *object, uint32_t offset, uint8_t *data,
    uint32_t capacity, uint32_t *transferred);

#endif
