/**
 * @file userspace/storage/include/reist/vfs_shadow_ext2.h
 * @brief Bounded EXT2 parser and native Ring-3 namespace transaction engine.
 */
#ifndef REIST_VFS_SHADOW_EXT2_H
#define REIST_VFS_SHADOW_EXT2_H

#include "vfs_shadow_fat32.h"

#define REIST_VFS_SHADOW_EXT2_MAX_COMPONENTS 16U
#define REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS 32U
#define REIST_VFS_SHADOW_EXT2_MAX_SECTOR_READS 192U
#define REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE 4096U
#define REIST_VFS_SHADOW_EXT2_MAX_LINK_DEPTH 8U
#define REIST_VFS_SHADOW_EXT2_MAX_WALK_COMPONENTS 64U
#define REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS 384U
#define REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_WRITES 64U
#define REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_FLUSHES 8U
#define REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS 26U
#define REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES 24U
#define REIST_VFS_SHADOW_EXT2_MAX_ALLOCATION_GROUPS 32U
#define REIST_VFS_SHADOW_EXT2_MAX_UNLINK_BLOCKS 64U
#define REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL (1U << 0U)

typedef int (*reist_vfs_shadow_ext2_write_sector_fn)(
    void *context, uint32_t resource, uint32_t sector,
    const uint8_t data[X86OS_STORAGE_BLOCK_SIZE]);
typedef int (*reist_vfs_shadow_ext2_flush_fn)(
    void *context, uint32_t resource);
typedef int (*reist_vfs_shadow_ext2_monotonic_fn)(
    void *context, uint64_t *milliseconds);

typedef struct {
    void *context;
    reist_vfs_shadow_drive_info_fn drive_info;
    reist_vfs_shadow_read_sector_fn read_sector;
    reist_vfs_shadow_ext2_write_sector_fn write_sector;
    reist_vfs_shadow_ext2_flush_fn flush;
    reist_vfs_shadow_ext2_monotonic_fn monotonic_ms;
} reist_vfs_shadow_ext2_io_t;

/* Explicit host adapter for the shared lifetime contract. The six-callback
 * legacy IO layout stays unchanged. The parser never invokes OS syscalls;
 * only this trusted host callback may mediate its fixed guard requests. */
typedef int (*reist_vfs_shadow_ext2_guard_fn)(void *context,
    reist_file_object_guard_request_t *request);
typedef struct {
    reist_vfs_shadow_ext2_io_t io;
    void *guard_context;
    reist_vfs_shadow_ext2_guard_fn guard;
} reist_vfs_shadow_ext2_guarded_io_t;

/** Replaceable, non-authoritative continuation hint for EXT2 readdir. */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t next_index;
    uint32_t resource;
    uint32_t volume_signature;
    uint32_t directory_inode;
    uint32_t directory_generation;
    uint32_t directory_signature;
    uint32_t directory_size;
    uint32_t logical_block;
    uint32_t entry_offset;
    uint32_t active;
} reist_vfs_shadow_ext2_readdir_cursor_t;

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

int reist_vfs_shadow_ext2_readdir_continue(
    const reist_vfs_shadow_io_t *io, const char *absolute_path,
    uint32_t path_length, uint32_t index,
    reist_vfs_shadow_ext2_readdir_cursor_t *cursor,
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

int reist_vfs_shadow_ext2_stat_bounded(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, uint32_t resolve_flags, uint64_t deadline_ms,
    x86os_file_info_t *info);
int reist_vfs_shadow_ext2_read_bounded(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, uint32_t offset, uint8_t *data, uint32_t capacity,
    uint64_t deadline_ms, uint32_t *transferred);
int reist_vfs_shadow_ext2_readdir_bounded(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, uint32_t index,
    reist_vfs_shadow_ext2_readdir_cursor_t *cursor,
    uint64_t deadline_ms, x86os_file_info_t *info);
int reist_vfs_shadow_ext2_object_open_bounded(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, uint32_t open_flags, uint64_t deadline_ms,
    reist_vfs_shadow_object_t *object, x86os_file_info_t *info);
int reist_vfs_shadow_ext2_object_stat_bounded(
    const reist_vfs_shadow_ext2_io_t *io,
    const reist_vfs_shadow_object_t *object, uint64_t deadline_ms,
    x86os_file_info_t *info);
int reist_vfs_shadow_ext2_object_read_bounded(
    const reist_vfs_shadow_ext2_io_t *io,
    const reist_vfs_shadow_object_t *object, uint32_t offset, uint8_t *data,
    uint32_t capacity, uint64_t deadline_ms, uint32_t *transferred);
int reist_vfs_shadow_ext2_readlink(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY],
    uint32_t *target_length, uint64_t deadline_ms);
int reist_vfs_shadow_ext2_symlink(
    const reist_vfs_shadow_ext2_io_t *io, const char *target,
    uint32_t target_length, const char *absolute_link_path,
    uint32_t link_path_length, uint64_t deadline_ms);
int reist_vfs_shadow_ext2_unlink_symlink(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, uint64_t deadline_ms);
int reist_vfs_shadow_ext2_unlink(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, uint64_t deadline_ms);
int reist_vfs_shadow_ext2_rename(
    const reist_vfs_shadow_ext2_io_t *io, const char *source_path,
    uint32_t source_length, const char *destination_path,
    uint32_t destination_length, uint64_t deadline_ms);
int reist_vfs_shadow_ext2_rename_symlink(
    const reist_vfs_shadow_ext2_io_t *io, const char *source_path,
    uint32_t source_length, const char *destination_path,
    uint32_t destination_length, uint64_t deadline_ms);
int reist_vfs_shadow_ext2_recover_path(
    const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
    uint32_t path_length, uint64_t deadline_ms);
int reist_vfs_shadow_ext2_recover_object(
    const reist_vfs_shadow_ext2_io_t *io, uint32_t resource,
    uint64_t deadline_ms);

/* Same bounded Linux EXT2 subset and errno semantics. On error, output is not
 * publishable. No implicit authority is added to the compatibility API. */
int reist_vfs_shadow_ext2_recover_path_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        uint64_t deadline_ms);

int reist_vfs_shadow_ext2_recover_object_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        uint32_t resource,
        uint64_t deadline_ms);

int reist_vfs_shadow_ext2_stat_bounded_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        uint32_t resolve_flags,
        uint64_t deadline_ms,
        x86os_file_info_t *info);

int reist_vfs_shadow_ext2_read_bounded_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        uint32_t offset,
        uint8_t *data,
        uint32_t capacity,
        uint64_t deadline_ms,
        uint32_t *transferred);

int reist_vfs_shadow_ext2_readdir_bounded_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        uint32_t index,
        reist_vfs_shadow_ext2_readdir_cursor_t *cursor,
        uint64_t deadline_ms,
        x86os_file_info_t *info);

int reist_vfs_shadow_ext2_object_open_bounded_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        uint32_t open_flags,
        uint64_t deadline_ms,
        reist_vfs_shadow_object_t *object,
        x86os_file_info_t *info);

int reist_vfs_shadow_ext2_object_stat_bounded_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const reist_vfs_shadow_object_t *object,
        uint64_t deadline_ms,
        x86os_file_info_t *info);

int reist_vfs_shadow_ext2_object_read_bounded_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const reist_vfs_shadow_object_t *object,
        uint32_t offset,
        uint8_t *data,
        uint32_t capacity,
        uint64_t deadline_ms,
        uint32_t *transferred);

int reist_vfs_shadow_ext2_readlink_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY],
        uint32_t *target_length,
        uint64_t deadline_ms);

int reist_vfs_shadow_ext2_symlink_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *target,
        uint32_t target_length,
        const char *absolute_link_path,
        uint32_t link_path_length,
        uint64_t deadline_ms);

int reist_vfs_shadow_ext2_unlink_symlink_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        uint64_t deadline_ms);

int reist_vfs_shadow_ext2_unlink_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *absolute_path,
        uint32_t path_length,
        uint64_t deadline_ms);

int reist_vfs_shadow_ext2_rename_symlink_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *source_path,
        uint32_t source_length,
        const char *destination_path,
        uint32_t destination_length,
        uint64_t deadline_ms);

int reist_vfs_shadow_ext2_rename_guarded(const reist_vfs_shadow_ext2_guarded_io_t *io,
        const char *source_path,
        uint32_t source_length,
        const char *destination_path,
        uint32_t destination_length,
        uint64_t deadline_ms);

#endif
