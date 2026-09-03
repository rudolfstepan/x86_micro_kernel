/** @file userspace/programs/readlink.c @brief Prints one symlink target. */
#include "x86os.h"
#include "reist/vfs_symlink_client.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        x86os_puts("Usage: readlink <link-path>\n");
        return 2;
    }
    char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
    int length = reist_vfs_readlink(
        argv[1], target, sizeof(target),
        REIST_VFS_SYMLINK_DEFAULT_TIMEOUT_MS);
    if (length < 0) {
        x86os_puts("readlink: path is not a readable symbolic link\n");
        return 1;
    }
    for (int index = 0; index < length; ++index)
        x86os_putchar(target[index]);
    x86os_putchar('\n');
    return 0;
}
