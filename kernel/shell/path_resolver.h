/**
 * @file kernel/shell/path_resolver.h
 * @brief Shell-Pfadauflösungsvertrag.
 *
 * Layer: Ring-0 rescue support.
 * Contract: Erfolg liefert einen nullterminierten Pfad im Aufruferpuffer.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#ifndef KERNEL_SHELL_PATH_RESOLVER_H
#define KERNEL_SHELL_PATH_RESOLVER_H

#include <stddef.h>

#define SHELL_PATH_MAX 256
#define SHELL_DRIVE_SELECTOR_MAX 8

typedef enum {
    SHELL_PATH_OK = 0,
    SHELL_PATH_INVALID = -1,
    SHELL_PATH_TOO_LONG = -2
} shell_path_result_t;

/*
 * Split an optional DOS/legacy drive prefix from a path.
 *
 * Accepted forms include C:\\DIR, hdd0:/DIR and /hdd0/DIR.  The returned
 * remainder points into input and is never NULL on success.  An empty
 * selector means that the current drive is used.
 */
shell_path_result_t shell_path_split_drive(
    const char* input,
    char selector[SHELL_DRIVE_SELECTOR_MAX],
    const char** remainder);

/* Build a canonical, drive-relative absolute path using '/' internally. */
shell_path_result_t shell_path_normalize(
    const char* current_path,
    const char* input,
    char output[SHELL_PATH_MAX]);

/* Prefix a canonical drive-relative path with its canonical VFS mount. */
shell_path_result_t shell_path_join_mount(
    const char* mount_point,
    const char* drive_path,
    char output[SHELL_PATH_MAX]);

/* Convert an internal '/' path into the DOS-style '\\' representation. */
shell_path_result_t shell_path_to_dos(
    const char* drive_path,
    char output[SHELL_PATH_MAX]);

#endif
