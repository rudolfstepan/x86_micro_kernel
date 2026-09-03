/**
 * @file userspace/gui/compositor/desktop_shortcut.h
 * @brief Bounded sibling shortcut persistence for the REIST desktop.
 *
 * An 8.3 .LNK file is an ordinary file in the directory in which the user
 * creates it. The local reist.shortcut/1 text format is intentionally not
 * Microsoft Shell Link compatible. It stores data only: a display name, a
 * typed canonical target path and no shell text, arguments, environment,
 * working-directory or embedded-icon authority.
 */
#ifndef USERSPACE_DESKTOP_SHORTCUT_H
#define USERSPACE_DESKTOP_SHORTCUT_H

#include <stdint.h>

#include "x86os.h"

#define DESKTOP_SHORTCUT_SCHEMA "reist.shortcut/1"
#define DESKTOP_SHORTCUT_DIRECTORY "/desktop"
#define DESKTOP_SHORTCUT_DISPLAY_NAME_CAPACITY 64U
#define DESKTOP_SHORTCUT_PATH_CAPACITY 256U
#define DESKTOP_SHORTCUT_FILENAME_CAPACITY 13U
#define DESKTOP_SHORTCUT_FILE_CAPACITY 512U
#define DESKTOP_SHORTCUT_COLLISION_LIMIT 32U
#define DESKTOP_SHORTCUT_OPERATION_TIMEOUT_MS 10000U
#define DESKTOP_SHORTCUT_REQUEST_TIMEOUT_MS 1000U

enum desktop_shortcut_status {
    DESKTOP_SHORTCUT_OK = 0,
    DESKTOP_SHORTCUT_ENOENT = -2,
    DESKTOP_SHORTCUT_EIO = -5,
    DESKTOP_SHORTCUT_EEXIST = -17,
    DESKTOP_SHORTCUT_ENOTDIR = -20,
    DESKTOP_SHORTCUT_EINVAL = -22,
    DESKTOP_SHORTCUT_ECAPACITY = -75,
    DESKTOP_SHORTCUT_ETIMEDOUT = -110,
    DESKTOP_SHORTCUT_ESTALE = -116
};

enum desktop_shortcut_target_kind {
    DESKTOP_SHORTCUT_TARGET_PROGRAM = 1U,
    DESKTOP_SHORTCUT_TARGET_FILE = 2U
};

typedef struct desktop_shortcut_create_request {
    char directory_path[DESKTOP_SHORTCUT_PATH_CAPACITY];
    char display_name[DESKTOP_SHORTCUT_DISPLAY_NAME_CAPACITY];
    char target_path[DESKTOP_SHORTCUT_PATH_CAPACITY];
    uint32_t target_kind;
    x86os_file_info_t directory_identity;
    x86os_file_info_t target_identity;
} desktop_shortcut_create_request_t;

typedef struct desktop_shortcut_create_result {
    uint32_t created;
    char filename[DESKTOP_SHORTCUT_FILENAME_CAPACITY];
    char shortcut_path[DESKTOP_SHORTCUT_PATH_CAPACITY];
    x86os_file_info_t shortcut_identity;
} desktop_shortcut_create_result_t;

typedef struct desktop_shortcut_resolve_result {
    uint32_t target_kind;
    char display_name[DESKTOP_SHORTCUT_DISPLAY_NAME_CAPACITY];
    char target_path[DESKTOP_SHORTCUT_PATH_CAPACITY];
    x86os_file_info_t target_identity;
} desktop_shortcut_resolve_result_t;

void desktop_shortcut_create_request_initialize(
    desktop_shortcut_create_request_t *request);
void desktop_shortcut_create_result_initialize(
    desktop_shortcut_create_result_t *result);
void desktop_shortcut_resolve_result_initialize(
    desktop_shortcut_resolve_result_t *result);

/** Return non-zero only for one valid bounded 8.3 .LNK leaf name. */
uint32_t desktop_shortcut_is_filename(const char *name);

/** Ensure the fixed /desktop directory exists and is not a symlink. */
int desktop_shortcut_prepare_directory(x86os_file_info_t *identity_out);

/**
 * Create one complete sibling .LNK in request.directory_path. The source
 * directory and target identities must still match the Explorer snapshot.
 */
int desktop_shortcut_create(
    const desktop_shortcut_create_request_t *request,
    desktop_shortcut_create_result_t *result);

/** Re-read one exact generation-bound .LNK and freshly validate its target. */
int desktop_shortcut_resolve(
    const char *shortcut_path,
    const x86os_file_info_t *shortcut_identity,
    desktop_shortcut_resolve_result_t *result);

#endif
