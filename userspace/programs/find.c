/** @file userspace/programs/find.c @brief Begrenzte Dateisuche nach Namen. */
#include "fs_walk.h"

static int find_match(const char *name, const char *pattern) {
    /* Exact names and the two useful bounded forms: *.ext and prefix*. */
    unsigned name_length = fs_tool_length(name);
    unsigned pattern_length = fs_tool_length(pattern);
    if (fs_tool_equal(pattern, "*")) return 1;
    if (pattern_length != 0U && pattern[0] == '*') {
        unsigned suffix = pattern_length - 1U;
        if (suffix > name_length) return 0;
        for (unsigned index = 0U; index < suffix; ++index)
            if (name[name_length - suffix + index] != pattern[index + 1U]) return 0;
        return 1;
    }
    if (pattern_length != 0U && pattern[pattern_length - 1U] == '*') {
        unsigned prefix = pattern_length - 1U;
        if (prefix > name_length) return 0;
        for (unsigned index = 0U; index < prefix; ++index)
            if (name[index] != pattern[index]) return 0;
        return 1;
    }
    return fs_tool_equal(name, pattern);
}

static int find_visit(const char *path, const char *pattern, unsigned depth,
                      unsigned *nodes) {
    x86os_file_info_t info;
    if (x86os_stat(path, &info) < 0) return -1;
    if (*nodes >= FS_TOOL_MAX_ENTRIES) return -2;
    ++*nodes;
    if (find_match(info.name, pattern)) { x86os_puts(path); x86os_putchar('\n'); }
    if (info.type != X86OS_DIRECTORY || depth >= FS_TOOL_MAX_DEPTH) return 0;
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
            int result = find_visit(child, pattern, depth + 1U, nodes);
            if (result != 0) return result;
        }
        entries_seen += (unsigned)count;
        index += (uint32_t)count;
        if (entries_seen >= FS_TOOL_MAX_ENTRIES) return -2;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        x86os_puts("Usage: find [path] <name>\n");
        return 2;
    }
    const char *path = argc == 3 ? argv[1] : ".";
    const char *pattern = argc == 3 ? argv[2] : argv[1];
    unsigned nodes = 0U;
    int result = find_visit(path, pattern, 0U, &nodes);
    if (result == -2) x86os_puts("find: traversal limit reached\n");
    else if (result != 0) x86os_puts("find: read error\n");
    return result == 0 ? 0 : 1;
}
