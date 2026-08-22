/**
 * @file userspace/gui/compositor/desktop_trash.h
 * @brief Bounded recoverable trash adapter for the Ring-3 desktop.
 *
 * REIST currently has one local user and one root-filesystem trash namespace.
 * Entries are moved atomically into /trash/files and receive restoration
 * metadata in /trash/info. Cross-filesystem copy/delete is never attempted.
 */
#ifndef USERSPACE_DESKTOP_TRASH_H
#define USERSPACE_DESKTOP_TRASH_H

#include <stdint.h>

#include "x86os.h"

#define DESKTOP_TRASH_ROOT_PATH "/trash"
#define DESKTOP_TRASH_FILES_PATH "/trash/files"
#define DESKTOP_TRASH_INFO_PATH "/trash/info"
#define DESKTOP_TRASH_PATH_CAPACITY 256U
#define DESKTOP_TRASH_METADATA_CAPACITY 384U
#define DESKTOP_TRASH_COLLISION_LIMIT 32U
#define DESKTOP_TRASH_SCAN_BATCHES 2U

enum desktop_trash_status {
    DESKTOP_TRASH_OK = 0,
    DESKTOP_TRASH_ENOENT = -2,
    DESKTOP_TRASH_EIO = -5,
    DESKTOP_TRASH_EPROTECTED = -13,
    DESKTOP_TRASH_ERENAME = -18,
    DESKTOP_TRASH_EINVAL = -22,
    DESKTOP_TRASH_ECAPACITY = -75,
    DESKTOP_TRASH_ESTALE = -116
};

typedef struct desktop_trash_state {
    uint32_t available;
    uint32_t full;
    uint32_t generation;
} desktop_trash_state_t;

typedef struct desktop_trash_request {
    char source_path[DESKTOP_TRASH_PATH_CAPACITY];
    x86os_file_info_t identity;
} desktop_trash_request_t;

typedef struct desktop_trash_result {
    uint32_t moved;
    char stored_path[DESKTOP_TRASH_PATH_CAPACITY];
    char info_path[DESKTOP_TRASH_PATH_CAPACITY];
} desktop_trash_result_t;

void desktop_trash_state_initialize(desktop_trash_state_t *state);
void desktop_trash_request_initialize(desktop_trash_request_t *request);
void desktop_trash_result_initialize(desktop_trash_result_t *result);
/** Return one only for canonical, non-system, non-trash source paths. */
uint32_t desktop_trash_source_allowed(const char *path);
/** Ensure and validate the fixed trash directories, then refresh state. */
int desktop_trash_prepare(desktop_trash_state_t *state);
/** Refresh empty/full state with a fixed number of directory batches. */
int desktop_trash_refresh(desktop_trash_state_t *state);
/** Persist restoration metadata, then atomically rename one current source. */
int desktop_trash_move(desktop_trash_state_t *state,
                       const desktop_trash_request_t *request,
                       desktop_trash_result_t *result);

#endif
