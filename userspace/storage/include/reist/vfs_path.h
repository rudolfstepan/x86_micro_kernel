/**
 * @file userspace/storage/include/reist/vfs_path.h
 * @brief Canonical path resolution shared by Ring-3 VFS clients.
 */
#ifndef REIST_VFS_PATH_H
#define REIST_VFS_PATH_H

#include <stdint.h>

#include "x86os.h"

#define REIST_VFS_PATH_MAX_DRIVES 22U

int reist_vfs_resolve_path(const char *path,
                           char output[X86OS_VFS_SHADOW_PATH_CAPACITY],
                           uint32_t *length);

#endif
