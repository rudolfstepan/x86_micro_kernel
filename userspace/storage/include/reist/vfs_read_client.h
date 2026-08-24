/**
 * @file userspace/storage/include/reist/vfs_read_client.h
 * @brief Bounded path-based read-only VFS client operations.
 */
#ifndef REIST_VFS_READ_CLIENT_H
#define REIST_VFS_READ_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "x86os.h"

#define REIST_VFS_READ_DEFAULT_TIMEOUT_MS 1000U

/** Returns bytes read, zero at EOF, or a negative errno-compatible value. */
int reist_vfs_read_at(const char *path, uint32_t offset, void *data,
                      size_t capacity, uint32_t timeout_ms);

/** Returns one for an entry, zero at end, or a negative errno-compatible value. */
int reist_vfs_readdir_at(const char *path, uint32_t index,
                         x86os_file_info_t *info, uint32_t timeout_ms);

#endif
