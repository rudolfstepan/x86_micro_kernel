/** @file userspace/programs/tree.c @brief Begrenzte Verzeichnisbaum-Ausgabe. */
#include "fs_walk.h"
#include "reist/vfs_read_client.h"
#include "reist/vfs_stat_client.h"

#define TREE_DEADLINE_MS 5000U
#define TREE_REQUEST_TIMEOUT_MS 1000U

static int tree_remaining_timeout(uint64_t deadline, uint32_t *timeout) {
    uint64_t now = 0U;
    if (timeout == 0 || x86os_monotonic_ms(&now) != 0 || now >= deadline)
        return -1;
    uint64_t remaining = deadline - now;
    *timeout = remaining < TREE_REQUEST_TIMEOUT_MS
        ? (uint32_t)remaining : TREE_REQUEST_TIMEOUT_MS;
    return *timeout != 0U ? 0 : -1;
}

static int tree_visit(const char *path, unsigned depth, unsigned *nodes,
                      uint64_t deadline) {
    if (*nodes >= FS_TOOL_MAX_ENTRIES) return -2;
    uint32_t timeout = 0U;
    if (tree_remaining_timeout(deadline, &timeout) != 0) return -3;
    x86os_file_info_t info;
    int status = reist_vfs_stat(path, &info, timeout);
    if (status != 0) return status == -110 ? -3 : -1;
    ++*nodes;
    if (depth == 0U) { x86os_puts(path); x86os_putchar('\n'); }
    if (info.type != X86OS_DIRECTORY) return 0;
    if (depth >= FS_TOOL_MAX_DEPTH) {
        fs_tool_print_indent(depth + 1U);
        x86os_puts("[depth limit]\n");
        return 0;
    }
    uint32_t index = 0U;
    unsigned entries_seen = 0U;
    for (;;) {
        if (tree_remaining_timeout(deadline, &timeout) != 0) return -3;
        x86os_file_info_t entry;
        int present = reist_vfs_readdir_at(path, index, &entry, timeout);
        if (present < 0) return present == -110 ? -3 : -1;
        if (present == 0) break;
        if (++entries_seen > FS_TOOL_MAX_ENTRIES) return -2;
        char child[FS_TOOL_PATH_CAPACITY];
        if (fs_tool_join(child, sizeof(child), path, entry.name) < 0)
            return -2;
        fs_tool_print_indent(depth + 1U);
        x86os_puts("|-- "); x86os_puts(entry.name);
        if (entry.type == X86OS_DIRECTORY) x86os_putchar('/');
        x86os_putchar('\n');
        int result = tree_visit(child, depth + 1U, nodes, deadline);
        if (result != 0) return result;
        ++index;
        if (entries_seen >= FS_TOOL_MAX_ENTRIES) return -2;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 2) { x86os_puts("Usage: tree [path]\n"); return 2; }
    const char *path = argc == 2 ? argv[1] : ".";
    uint64_t started = 0U;
    if (x86os_monotonic_ms(&started) != 0) {
        x86os_puts("tree: monotonic clock unavailable\n");
        return 1;
    }
    uint64_t deadline = UINT64_MAX - started < TREE_DEADLINE_MS
        ? UINT64_MAX : started + TREE_DEADLINE_MS;
    unsigned nodes = 0U;
    int result = tree_visit(path, 0U, &nodes, deadline);
    if (result == -2) x86os_puts("tree: traversal limit reached\n");
    else if (result == -3) x86os_puts("tree: traversal deadline reached\n");
    else if (result != 0) x86os_puts("tree: read error\n");
    return result == 0 ? 0 : 1;
}
