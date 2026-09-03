/** @file userspace/programs/ln.c @brief Creates bounded native symlinks. */
#include "x86os.h"
#include "reist/vfs_symlink_client.h"

static int text_equal(const char *left, const char *right) {
    if (left == 0 || right == 0) return 0;
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return *left == *right;
}

int main(int argc, char **argv) {
    if (argc != 4 || !text_equal(argv[1], "-s")) {
        x86os_puts("Usage: ln -s <target> <link-path>\n");
        return 2;
    }
    int status = reist_vfs_symlink(
        argv[2], argv[3], REIST_VFS_SYMLINK_DEFAULT_TIMEOUT_MS);
    if (status != 0) {
        x86os_puts(status == -95
            ? "ln: filesystem does not support symbolic links\n"
            : status == -17
                ? "ln: link path already exists\n"
                : "ln: unable to create symbolic link\n");
        return 1;
    }
    return 0;
}
