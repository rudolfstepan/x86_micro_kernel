/**
 * @file userspace/programs/copy.c
 * @brief Kopiert eine Datei mit begrenzten Puffern.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"
#include "reist/vfs_file_client.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        x86os_puts("Usage: copy <source> <destination>\n");
        return 2;
    }
    reist_vfs_file_handle_t source = REIST_VFS_FILE_INVALID_HANDLE;
    int source_status = reist_vfs_file_open_rights(
        argv[1], REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT, &source);
    if (source_status != 0) {
        x86os_puts("copy: source file not found\n");
        return 1;
    }
    x86os_file_info_t source_info;
    if (reist_vfs_file_fstat(source, &source_info) != 0 ||
        source_info.type != X86OS_FILE) {
        (void)reist_vfs_file_close(source);
        x86os_puts("copy: source file not found\n");
        return 1;
    }
    int destination = x86os_create(argv[2]);
    if (destination < 0) {
        (void)reist_vfs_file_close(source);
        x86os_puts("copy: destination already exists or cannot be created\n");
        return 1;
    }

    char buffer[512];
    int failed = 0;
    for (;;) {
        int amount = reist_vfs_file_read_bulk(source, buffer, sizeof(buffer));
        if (amount < 0) { failed = 1; break; }
        if (amount == 0) break;
        int offset = 0;
        while (offset < amount) {
            int written = x86os_write(destination, buffer + offset,
                                      (size_t)(amount - offset));
            if (written <= 0) { failed = 1; break; }
            offset += written;
        }
        if (failed) break;
    }
    if (reist_vfs_file_close(source) < 0) failed = 1;
    if (x86os_close(destination) < 0) failed = 1;
    if (failed) {
        x86os_puts("copy: I/O error\n");
        return 1;
    }
    x86os_puts("        1 file(s) copied.\n");
    return 0;
}
