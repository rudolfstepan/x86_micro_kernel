/**
 * @file userspace/programs/del.c
 * @brief Löscht eine Datei.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"
#include "reist/vfs_namespace_client.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        x86os_puts("Usage: del <file>\n");
        return 2;
    }
    int status = reist_vfs_unlink(
        argv[1], REIST_VFS_NAMESPACE_DEFAULT_TIMEOUT_MS);
    if (status == -95) status = x86os_unlink(argv[1]);
    if (status < 0) {
        x86os_puts("del: file not found or cannot be removed\n");
        return 1;
    }
    return 0;
}
