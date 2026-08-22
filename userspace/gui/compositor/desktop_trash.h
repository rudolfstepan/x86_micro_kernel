/**
 * @file userspace/gui/compositor/desktop_trash.h
 * @brief Bounded recoverable trash adapter for the Ring-3 desktop.
 *
 * REIST currently has one local user and one root-filesystem trash namespace.
 * Payloads receive a reserved 8.3 name in their existing parent so the native
 * FAT32 journal can commit one same-directory rename. /trash/files stores
 * visible catalog markers and /trash/info stores restoration metadata.
 * Cross-directory/cross-filesystem copy-delete is never attempted.
 */
#ifndef USERSPACE_DESKTOP_TRASH_H
#define USERSPACE_DESKTOP_TRASH_H

#include <stdint.h>

#include "x86os.h"

#define DESKTOP_TRASH_ROOT_PATH "/trash"
#define DESKTOP_TRASH_FILES_PATH "/trash/files"
#define DESKTOP_TRASH_INFO_PATH "/trash/info"
#define DESKTOP_TRASH_STORAGE_PREFIX "RT"
#define DESKTOP_TRASH_STORAGE_SUFFIX ".TRS"
#define DESKTOP_TRASH_STORAGE_NAME_LENGTH 12U
#define DESKTOP_TRASH_PATH_CAPACITY 256U
#define DESKTOP_TRASH_METADATA_CAPACITY 640U
#define DESKTOP_TRASH_COLLISION_LIMIT 32U
#define DESKTOP_TRASH_SCAN_BATCHES 2U
#define DESKTOP_TRASH_DELETE_DEPTH_LIMIT 8U
#define DESKTOP_TRASH_DELETE_ENTRY_LIMIT 128U
#define DESKTOP_TRASH_EMPTY_CATALOG_LIMIT 64U

enum desktop_trash_status {
    DESKTOP_TRASH_OK = 0,
    DESKTOP_TRASH_ENOENT = -2,
    DESKTOP_TRASH_EIO = -5,
    DESKTOP_TRASH_EPROTECTED = -13,
    DESKTOP_TRASH_ECOLLISION = -17,
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
    char catalog_path[DESKTOP_TRASH_PATH_CAPACITY];
    char info_path[DESKTOP_TRASH_PATH_CAPACITY];
} desktop_trash_result_t;

typedef struct desktop_trash_restore_request {
    char catalog_path[DESKTOP_TRASH_PATH_CAPACITY];
    x86os_file_info_t identity;
} desktop_trash_restore_request_t;

typedef struct desktop_trash_restore_result {
    uint32_t restored;
    uint32_t cleanup_complete;
    char original_path[DESKTOP_TRASH_PATH_CAPACITY];
    char stored_path[DESKTOP_TRASH_PATH_CAPACITY];
    char info_path[DESKTOP_TRASH_PATH_CAPACITY];
} desktop_trash_restore_result_t;

typedef struct desktop_trash_empty_result {
    uint32_t removed_count;
    uint32_t incomplete;
} desktop_trash_empty_result_t;

void desktop_trash_state_initialize(desktop_trash_state_t *state);
void desktop_trash_request_initialize(desktop_trash_request_t *request);
void desktop_trash_result_initialize(desktop_trash_result_t *result);
void desktop_trash_restore_request_initialize(
    desktop_trash_restore_request_t *request);
void desktop_trash_restore_result_initialize(
    desktop_trash_restore_result_t *result);
void desktop_trash_empty_result_initialize(
    desktop_trash_empty_result_t *result);
/** Return one only for canonical, non-system, non-trash source paths. */
uint32_t desktop_trash_source_allowed(const char *path);
/** Ensure and validate the fixed trash directories, then refresh state. */
int desktop_trash_prepare(desktop_trash_state_t *state);
/** Refresh empty/full state with a fixed number of directory batches. */
int desktop_trash_refresh(desktop_trash_state_t *state);
/** Publish catalog metadata, then atomically rename within the source parent. */
int desktop_trash_move(desktop_trash_state_t *state,
                       const desktop_trash_request_t *request,
                       desktop_trash_result_t *result);
/** Restore one current catalog entry without replacing its original target. */
int desktop_trash_restore(desktop_trash_state_t *state,
                          const desktop_trash_restore_request_t *request,
                          desktop_trash_restore_result_t *result);
/** Permanently remove metadata-bound entries under fixed traversal budgets. */
int desktop_trash_empty(desktop_trash_state_t *state,
                        desktop_trash_empty_result_t *result);

#endif
