/**
 * @file userspace/storage/include/reist/vfs_file_client.h
 * @brief Fixed-capacity Ring-3 sessions over service-owned VFS objects.
 */
#ifndef REIST_VFS_FILE_CLIENT_H
#define REIST_VFS_FILE_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "x86os.h"

#define REIST_VFS_FILE_CAPACITY 4U
#define REIST_VFS_FILE_INVALID_HANDLE 0U
#define REIST_VFS_FILE_DEFAULT_TIMEOUT_MS 1000U
#define REIST_VFS_SEEK_SET 0U
#define REIST_VFS_SEEK_CUR 1U
#define REIST_VFS_SEEK_END 2U

typedef uint32_t reist_vfs_file_handle_t;

/* The path is resolved only by open. Follow-up operations carry an owner-bound
 * service token and service generation, never path authority. Handles remain
 * process-local and are not cross-process descriptors. */

int reist_vfs_file_open(const char *path, uint32_t timeout_ms,
                        reist_vfs_file_handle_t *handle);
int reist_vfs_file_read(reist_vfs_file_handle_t handle, void *data,
                        size_t capacity);
int reist_vfs_file_seek(reist_vfs_file_handle_t handle, int64_t offset,
                        uint32_t whence, uint32_t *new_offset);
int reist_vfs_file_fstat(reist_vfs_file_handle_t handle,
                         x86os_file_info_t *info);
int reist_vfs_file_close(reist_vfs_file_handle_t handle);

#endif
