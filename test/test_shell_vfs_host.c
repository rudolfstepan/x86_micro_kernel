#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "shell_vfs.h"

static uint64_t mock_now;
static uint32_t last_timeout;
static int stat_result;
static uint32_t stat_type = X86OS_FILE;
static int readdir_result = 1;
static uint32_t readdir_calls;

int x86os_monotonic_ms(uint64_t *value) {
    assert(value != NULL);
    *value = mock_now;
    return 0;
}

int reist_vfs_stat(const char *path, x86os_file_info_t *info,
                   uint32_t timeout_ms) {
    assert(path != NULL && info != NULL);
    last_timeout = timeout_ms;
    memset(info, 0, sizeof(*info));
    info->type = stat_type;
    return stat_result;
}

int reist_vfs_readdir_at(const char *path, uint32_t index,
                         x86os_file_info_t *info, uint32_t timeout_ms) {
    assert(path != NULL && info != NULL);
    (void)index;
    ++readdir_calls;
    last_timeout = timeout_ms;
    memset(info, 0, sizeof(*info));
    strcpy(info->name, "ENTRY.PRG");
    info->type = X86OS_FILE;
    return readdir_result;
}

static void reset_mocks(void) {
    mock_now = 100U;
    last_timeout = 0U;
    stat_result = 0;
    stat_type = X86OS_FILE;
    readdir_result = 1;
    readdir_calls = 0U;
}

static void test_one_absolute_deadline_is_shared(void) {
    reset_mocks();
    shell_vfs_budget_t budget;
    assert(shell_vfs_budget_begin(&budget) == 0);
    assert(budget.deadline_ms == 5100U && budget.accepted_entries == 0U);
    assert(shell_vfs_executable(&budget, "/bin/tool.prg") == 1);
    assert(last_timeout == 1000U);

    stat_type = X86OS_DIRECTORY;
    assert(shell_vfs_executable(&budget, "/bin") == 0);
    stat_result = -2;
    assert(shell_vfs_executable(&budget, "/missing.prg") == 0);

    mock_now = 5099U;
    stat_result = 0;
    stat_type = X86OS_FILE;
    assert(shell_vfs_executable(&budget, "/bin/tool.prg") == 1);
    assert(last_timeout == 1U);
    mock_now = 5100U;
    assert(shell_vfs_executable(&budget, "/bin/tool.prg") ==
           SHELL_VFS_ETIMEDOUT);
}

static void test_protocol_errors_are_not_hidden(void) {
    reset_mocks();
    shell_vfs_budget_t budget;
    assert(shell_vfs_budget_begin(&budget) == 0);
    stat_result = -84;
    assert(shell_vfs_executable(&budget, "/bin/tool.prg") == -84);
    readdir_result = -5;
    x86os_file_info_t info;
    assert(shell_vfs_readdir(&budget, "/bin", 0U, &info) == -5);
    assert(budget.accepted_entries == 0U);
}

static void test_directory_acceptance_is_fixed_capacity(void) {
    reset_mocks();
    shell_vfs_budget_t budget;
    assert(shell_vfs_budget_begin(&budget) == 0);
    x86os_file_info_t info;
    for (uint32_t index = 0U;
         index < SHELL_VFS_DIRECTORY_ENTRY_CAPACITY; ++index) {
        assert(shell_vfs_readdir(&budget, "/bin", index, &info) == 1);
    }
    assert(budget.accepted_entries == SHELL_VFS_DIRECTORY_ENTRY_CAPACITY);
    assert(shell_vfs_readdir(&budget, "/bin",
                             SHELL_VFS_DIRECTORY_ENTRY_CAPACITY,
                             &info) == SHELL_VFS_ECAPACITY);
    assert(readdir_calls == SHELL_VFS_DIRECTORY_ENTRY_CAPACITY + 1U);

    reset_mocks();
    assert(shell_vfs_budget_begin(&budget) == 0);
    readdir_result = 0;
    assert(shell_vfs_readdir(&budget, "/empty", 0U, &info) == 0);
    assert(budget.accepted_entries == 0U);
}

int main(void) {
    test_one_absolute_deadline_is_shared();
    test_protocol_errors_are_not_hidden();
    test_directory_acceptance_is_fixed_capacity();
    return 0;
}
