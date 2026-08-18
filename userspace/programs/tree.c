/** @file userspace/programs/tree.c @brief Begrenzte Verzeichnisbaum-Ausgabe. */
#include "fs_walk.h"

static int tree_visit(const char *path, unsigned depth, unsigned *nodes) {
    x86os_file_info_t info;
    if (x86os_stat(path, &info) < 0) return -1;
    if (*nodes >= FS_TOOL_MAX_ENTRIES) return -2;
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
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, index, entries);
        if (count < 0) return -1;
        if (count == 0) break;
        if (entries_seen + (unsigned)count > FS_TOOL_MAX_ENTRIES) return -2;
        for (int item = 0; item < count; ++item) {
            char child[FS_TOOL_PATH_CAPACITY];
            if (fs_tool_join(child, sizeof(child), path, entries[item].name) < 0)
                return -2;
            fs_tool_print_indent(depth + 1U);
            x86os_puts("|-- "); x86os_puts(entries[item].name);
            if (entries[item].type == X86OS_DIRECTORY) x86os_putchar('/');
            x86os_putchar('\n');
            int result = tree_visit(child, depth + 1U, nodes);
            if (result != 0) return result;
        }
        entries_seen += (unsigned)count;
        index += (uint32_t)count;
        if (entries_seen >= FS_TOOL_MAX_ENTRIES) return -2;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 2) { x86os_puts("Usage: tree [path]\n"); return 2; }
    const char *path = argc == 2 ? argv[1] : ".";
    unsigned nodes = 0U;
    int result = tree_visit(path, 0U, &nodes);
    if (result == -2) x86os_puts("tree: traversal limit reached\n");
    else if (result != 0) x86os_puts("tree: read error\n");
    return result == 0 ? 0 : 1;
}
