/**
 * @file desktop_filetypes.h
 * @brief Bounded desktop file-association policy loaded from /etc/reist.
 *
 * This compositor-private adapter parses a fixed key/value file into a
 * caller-owned table. It performs no allocation, filesystem access, process
 * activation or fallback guessing. Extensions are matched case-insensitively
 * while configured program paths must already be canonical absolute PRG
 * paths.
 */
#ifndef REIST_DESKTOP_FILETYPES_H
#define REIST_DESKTOP_FILETYPES_H

#include <stddef.h>
#include <stdint.h>

#define DESKTOP_FILETYPES_VERSION 1U
#define DESKTOP_FILETYPES_CAPACITY 16U
#define DESKTOP_FILETYPES_EXTENSION_CAPACITY 12U
#define DESKTOP_FILETYPES_PROGRAM_CAPACITY 256U
#define DESKTOP_FILETYPES_CONFIG_CAPACITY 4096U

enum desktop_filetypes_status {
    DESKTOP_FILETYPES_OK = 0,
    DESKTOP_FILETYPES_EINVAL = -1,
    DESKTOP_FILETYPES_ECAPACITY = -2,
    DESKTOP_FILETYPES_ENOTFOUND = -3
};

typedef struct desktop_filetype_entry {
    char extension[DESKTOP_FILETYPES_EXTENSION_CAPACITY];
    char program[DESKTOP_FILETYPES_PROGRAM_CAPACITY];
} desktop_filetype_entry_t;

typedef struct desktop_filetypes {
    uint32_t version;
    uint32_t entry_count;
    desktop_filetype_entry_t entries[DESKTOP_FILETYPES_CAPACITY];
} desktop_filetypes_t;

/** Initialize an empty versioned table. NULL is accepted. */
void desktop_filetypes_initialize(desktop_filetypes_t *table);

/**
 * Parse one complete `reist.filetypes/1` configuration atomically.
 *
 * The first effective line must be `schema=reist.filetypes/1`. Remaining
 * lines map one lowercase extension such as `.txt` to one canonical absolute
 * `.prg` path. Blank lines and lines beginning with `#` are ignored. Unknown
 * syntax, duplicate extensions, overlong lines and capacities fail before the
 * destination table is changed.
 */
int desktop_filetypes_parse(desktop_filetypes_t *table,
                            const char *data, size_t size);

/** Return the configured program for a path, or ENOTFOUND. */
int desktop_filetypes_lookup(const desktop_filetypes_t *table,
                             const char *path, const char **program_out);

#endif
