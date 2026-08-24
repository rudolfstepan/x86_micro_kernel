/**
 * @file userspace/storage/include/reist/vfs_stat_client.h
 * @brief Controlled client adapter for the Ring-3 VFS stat migration path.
 */
#ifndef REIST_VFS_STAT_CLIENT_H
#define REIST_VFS_STAT_CLIENT_H

#include <stdint.h>

#include "x86os.h"

#define REIST_VFS_STAT_DEFAULT_TIMEOUT_MS 1000U
#define REIST_VFS_STAT_MAX_DRIVES 22U

/* This adapter is initially restricted to short-lived command clients. A
 * timeout requires the caller to terminate so process cleanup revokes the
 * outstanding generation-scoped request. */
int reist_vfs_stat(const char *path, x86os_file_info_t *info,
                   uint32_t timeout_ms);

#endif
