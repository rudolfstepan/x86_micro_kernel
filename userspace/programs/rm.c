/** @file userspace/programs/rm.c @brief Löschen mit explizitem --recursive. */
#include "fs_walk.h"
#include "reist/vfs_namespace_client.h"
#include "reist/vfs_stat_client.h"

static int remove_leaf(const char *path) {
    int status = reist_vfs_unlink(
        path, REIST_VFS_NAMESPACE_DEFAULT_TIMEOUT_MS);
    return status == -95 ? x86os_unlink(path) : status;
}

static int remove_tree(const char *path, unsigned depth, unsigned *nodes) {
    x86os_file_info_t info;
    if (reist_vfs_lstat(
            path, &info, REIST_VFS_STAT_DEFAULT_TIMEOUT_MS) < 0) return -1;
    if (*nodes >= FS_TOOL_MAX_ENTRIES || depth > FS_TOOL_MAX_DEPTH) return -2;
    ++*nodes;
    if (info.type != X86OS_DIRECTORY) return remove_leaf(path) < 0 ? -1 : 0;
    uint32_t index = 0U;
    unsigned entries_seen = 0U;
    for (;;) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, index, entries);
        if (count < 0) return -1;
        if (count == 0) break;
        for (int item = 0; item < count; ++item) {
            char child[FS_TOOL_PATH_CAPACITY];
            if (fs_tool_join(child, sizeof(child), path, entries[item].name) < 0)
                return -2;
            int result = remove_tree(child, depth + 1U, nodes);
            if (result != 0) return result;
        }
        entries_seen += (unsigned)count;
        index += (uint32_t)count;
        if (entries_seen >= FS_TOOL_MAX_ENTRIES) return -2;
    }
    return x86os_rmdir(path) < 0 ? -1 : 0;
}

int main(int argc, char **argv) {
    int recursive = argc == 3 && fs_tool_equal(argv[1], "--recursive");
    const char *path = recursive ? argv[2] : argc == 2 ? argv[1] : 0;
    if (path == 0) {
        x86os_puts("Usage: rm [--recursive] <path>\n");
        return 2;
    }
    if (fs_tool_reserved_root(path)) {
        x86os_puts("rm: refusing to remove a root path\n");
        return 1;
    }
    x86os_file_info_t info;
    if (reist_vfs_lstat(
            path, &info, REIST_VFS_STAT_DEFAULT_TIMEOUT_MS) < 0) {
        x86os_puts("rm: path not found\n");
        return 1;
    }
    if (info.type == X86OS_DIRECTORY && !recursive) {
        x86os_puts("rm: directory requires --recursive\n");
        return 1;
    }
    unsigned nodes = 0U;
    int result = recursive ? remove_tree(path, 0U, &nodes) :
        (remove_leaf(path) < 0 ? -1 : 0);
    if (result == -2) x86os_puts("rm: traversal limit reached\n");
    else if (result != 0) x86os_puts("rm: remove failed\n");
    return result == 0 ? 0 : 1;
}
