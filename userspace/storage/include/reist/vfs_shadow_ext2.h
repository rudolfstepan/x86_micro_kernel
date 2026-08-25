/**
 * @file userspace/storage/include/reist/vfs_shadow_ext2.h
 * @brief Bounded read-only EXT2 metadata parser for the storage service.
 */
#ifndef REIST_VFS_SHADOW_EXT2_H
#define REIST_VFS_SHADOW_EXT2_H

#include "vfs_shadow_fat32.h"

#define REIST_VFS_SHADOW_EXT2_MAX_COMPONENTS 16U
#define REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS 32U
#define REIST_VFS_SHADOW_EXT2_MAX_SECTOR_READS 192U
#define REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE 4096U

/** Returns the public SYS_STAT error convention: 0, -2, -5, -22 or -110. */
int reist_vfs_shadow_ext2_stat(const reist_vfs_shadow_io_t *io,
                               const char *absolute_path,
                               uint32_t path_length,
                               x86os_file_info_t *info);

int reist_vfs_shadow_ext2_read(const reist_vfs_shadow_io_t *io,
                               const char *absolute_path,
                               uint32_t path_length, uint32_t offset,
                               uint8_t *data, uint32_t capacity,
                               uint32_t *transferred);

int reist_vfs_shadow_ext2_readdir(const reist_vfs_shadow_io_t *io,
                                  const char *absolute_path,
                                  uint32_t path_length, uint32_t index,
                                  x86os_file_info_t *info);

int reist_vfs_shadow_ext2_object_open(
    const reist_vfs_shadow_io_t *io, const char *absolute_path,
    uint32_t path_length, reist_vfs_shadow_object_t *object,
    x86os_file_info_t *info);
int reist_vfs_shadow_ext2_object_stat(
    const reist_vfs_shadow_io_t *io,
    const reist_vfs_shadow_object_t *object, x86os_file_info_t *info);
int reist_vfs_shadow_ext2_object_read(
    const reist_vfs_shadow_io_t *io,
    const reist_vfs_shadow_object_t *object, uint32_t offset, uint8_t *data,
    uint32_t capacity, uint32_t *transferred);

#endif
