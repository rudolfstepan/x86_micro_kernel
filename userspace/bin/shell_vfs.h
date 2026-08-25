/**
 * @file userspace/bin/shell_vfs.h
 * @brief Bounded authoritative namespace access for the userspace shell.
 */
#ifndef REIST_SHELL_VFS_H
#define REIST_SHELL_VFS_H

#include <stdint.h>

#include "x86os.h"

#define SHELL_VFS_OPERATION_TIMEOUT_MS 5000U
#define SHELL_VFS_REQUEST_TIMEOUT_MS 1000U
#define SHELL_VFS_DIRECTORY_ENTRY_CAPACITY 128U

#define SHELL_VFS_ECAPACITY (-75)
#define SHELL_VFS_ETIMEDOUT (-110)

typedef struct {
    uint64_t deadline_ms;
    uint32_t accepted_entries;
} shell_vfs_budget_t;

/** Starts one absolute budget shared by a complete lookup or completion. */
int shell_vfs_budget_begin(shell_vfs_budget_t *budget);

/** Returns one for a regular file, zero for absent/non-file, or negative errno. */
int shell_vfs_executable(shell_vfs_budget_t *budget, const char *path);

/**
 * Returns one entry, zero at EOF, or negative errno. No more than 128 entries
 * are accepted across all directories sharing one budget.
 */
int shell_vfs_readdir(shell_vfs_budget_t *budget, const char *path,
                      uint32_t index, x86os_file_info_t *info);

#endif
