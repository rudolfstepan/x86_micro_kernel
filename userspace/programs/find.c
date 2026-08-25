/** @file userspace/programs/find.c @brief Begrenzte Dateisuche nach Namen. */
#include "fs_walk.h"
#include "reist/vfs_read_client.h"
#include "reist/vfs_stat_client.h"

#define FIND_DEADLINE_MS 5000U
#define FIND_REQUEST_TIMEOUT_MS 1000U

static int find_remaining_timeout(uint64_t deadline, uint32_t *timeout) {
    uint64_t now = 0U;
    if (timeout == 0 || x86os_monotonic_ms(&now) != 0 || now >= deadline)
        return -1;
    uint64_t remaining = deadline - now;
    *timeout = remaining < FIND_REQUEST_TIMEOUT_MS
        ? (uint32_t)remaining : FIND_REQUEST_TIMEOUT_MS;
    return *timeout != 0U ? 0 : -1;
}

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
                      unsigned *nodes, uint64_t deadline) {
    if (*nodes >= FS_TOOL_MAX_ENTRIES) return -2;
    uint32_t timeout = 0U;
    if (find_remaining_timeout(deadline, &timeout) != 0) return -3;
    x86os_file_info_t info;
    int status = reist_vfs_stat(path, &info, timeout);
    if (status != 0) return status == -110 ? -3 : -1;
    ++*nodes;
    if (find_match(info.name, pattern)) { x86os_puts(path); x86os_putchar('\n'); }
    if (info.type != X86OS_DIRECTORY || depth >= FS_TOOL_MAX_DEPTH) return 0;
    uint32_t index = 0U;
    unsigned entries_seen = 0U;
    for (;;) {
        if (find_remaining_timeout(deadline, &timeout) != 0) return -3;
        x86os_file_info_t entry;
        int present = reist_vfs_readdir_at(path, index, &entry, timeout);
        if (present < 0) return present == -110 ? -3 : -1;
        if (present == 0) break;
        if (++entries_seen > FS_TOOL_MAX_ENTRIES) return -2;
        char child[FS_TOOL_PATH_CAPACITY];
        if (fs_tool_join(child, sizeof(child), path, entry.name) < 0)
            return -2;
        int result = find_visit(
            child, pattern, depth + 1U, nodes, deadline);
        if (result != 0) return result;
        ++index;
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
    uint64_t started = 0U;
    if (x86os_monotonic_ms(&started) != 0) {
        x86os_puts("find: monotonic clock unavailable\n");
        return 1;
    }
    uint64_t deadline = UINT64_MAX - started < FIND_DEADLINE_MS
        ? UINT64_MAX : started + FIND_DEADLINE_MS;
    unsigned nodes = 0U;
    int result = find_visit(path, pattern, 0U, &nodes, deadline);
    if (result == -2) x86os_puts("find: traversal limit reached\n");
    else if (result == -3) x86os_puts("find: traversal deadline reached\n");
    else if (result != 0) x86os_puts("find: read error\n");
    return result == 0 ? 0 : 1;
}
