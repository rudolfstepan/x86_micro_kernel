/* Fixed limits shared by tree, find and rm --recursive. */
#ifndef REIST_FS_WALK_H
#define REIST_FS_WALK_H

#include "x86os.h"

#define FS_TOOL_PATH_CAPACITY 256U
#define FS_TOOL_MAX_DEPTH 16U
#define FS_TOOL_MAX_ENTRIES 512U

static unsigned fs_tool_length(const char *text) {
    unsigned length = 0U;
    while (text[length] != '\0' && length < FS_TOOL_PATH_CAPACITY) ++length;
    return length;
}

static int fs_tool_equal(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static int fs_tool_join(char *output, unsigned capacity,
                        const char *parent, const char *name) {
    unsigned parent_length = fs_tool_length(parent);
    unsigned name_length = fs_tool_length(name);
    unsigned separator = parent_length != 0U && parent[parent_length - 1U] != '/';
    if (parent_length + separator + name_length + 1U > capacity) return -1;
    unsigned offset = 0U;
    for (unsigned index = 0U; index < parent_length; ++index)
        output[offset++] = parent[index];
    if (separator != 0U) output[offset++] = '/';
    for (unsigned index = 0U; index < name_length; ++index)
        output[offset++] = name[index];
    output[offset] = '\0';
    return 0;
}

static void fs_tool_print_indent(unsigned depth) {
    for (unsigned index = 0U; index < depth * 2U; ++index)
        x86os_putchar(' ');
}

static int fs_tool_reserved_root(const char *path) {
    return fs_tool_equal(path, "/") || fs_tool_equal(path, ".") ||
           fs_tool_equal(path, "C:") || fs_tool_equal(path, "C:/");
}

#endif
