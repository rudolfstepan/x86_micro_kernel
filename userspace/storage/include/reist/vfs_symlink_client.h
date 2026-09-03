/**
 * @file userspace/storage/include/reist/vfs_symlink_client.h
 * @brief Bounded POSIX-shaped symbolic-link client operations.
 */
#ifndef REIST_VFS_SYMLINK_CLIENT_H
#define REIST_VFS_SYMLINK_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "x86os.h"

#define REIST_VFS_SYMLINK_DEFAULT_TIMEOUT_MS 5000U
#define REIST_VFS_SYMLINK_MAX_RECOVERY_RETRIES 1U

/** Creates exactly one native symbolic link; hard links are not implied. */
int reist_vfs_symlink(const char *target, const char *link_path,
                      uint32_t timeout_ms);

/** POSIX-shaped readlink: copies at most capacity bytes and adds no NUL.
 * Returns the number of bytes copied or a negative errno-compatible value. */
int reist_vfs_readlink(const char *path, char *target, size_t capacity,
                       uint32_t timeout_ms);

#endif
