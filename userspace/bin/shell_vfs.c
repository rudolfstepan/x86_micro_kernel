/**
 * @file userspace/bin/shell_vfs.c
 * @brief Fixed-storage adapter from shell namespace work to Ring-3 VFS.
 */
#include "shell_vfs.h"

#include "reist/vfs_read_client.h"
#include "reist/vfs_stat_client.h"

static int remaining_timeout(const shell_vfs_budget_t *budget,
                             uint32_t *timeout_ms) {
    uint64_t now = 0U;
    if (budget == 0 || timeout_ms == 0 ||
        x86os_monotonic_ms(&now) != 0) return -5;
    if (now >= budget->deadline_ms) return SHELL_VFS_ETIMEDOUT;
    uint64_t remaining = budget->deadline_ms - now;
    *timeout_ms = remaining < SHELL_VFS_REQUEST_TIMEOUT_MS
        ? (uint32_t)remaining : SHELL_VFS_REQUEST_TIMEOUT_MS;
    return *timeout_ms != 0U ? 0 : SHELL_VFS_ETIMEDOUT;
}

int shell_vfs_budget_begin(shell_vfs_budget_t *budget) {
    uint64_t now = 0U;
    if (budget == 0 || x86os_monotonic_ms(&now) != 0) return -5;
    budget->deadline_ms = UINT64_MAX - now < SHELL_VFS_OPERATION_TIMEOUT_MS
        ? UINT64_MAX : now + SHELL_VFS_OPERATION_TIMEOUT_MS;
    budget->accepted_entries = 0U;
    return 0;
}

int shell_vfs_executable(shell_vfs_budget_t *budget, const char *path) {
    uint32_t timeout_ms = 0U;
    int status = remaining_timeout(budget, &timeout_ms);
    if (status != 0) return status;
    x86os_file_info_t info;
    status = reist_vfs_stat(path, &info, timeout_ms);
    if (status == -2) return 0;
    if (status != 0) return status;
    return info.type == X86OS_FILE ? 1 : 0;
}

int shell_vfs_readdir(shell_vfs_budget_t *budget, const char *path,
                      uint32_t index, x86os_file_info_t *info) {
    if (budget == 0 || info == 0) return -22;
    uint32_t timeout_ms = 0U;
    int status = remaining_timeout(budget, &timeout_ms);
    if (status != 0) return status;
    int present = reist_vfs_readdir_at(path, index, info, timeout_ms);
    if (present <= 0) return present;
    if (budget->accepted_entries >= SHELL_VFS_DIRECTORY_ENTRY_CAPACITY)
        return SHELL_VFS_ECAPACITY;
    ++budget->accepted_entries;
    return 1;
}
