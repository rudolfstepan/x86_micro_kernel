/**
 * @file userspace/gui/compositor/desktop_file_move.h
 * @brief Bounded verified Ring-3 regular-file move between directories.
 *
 * The legacy FAT rename operation is same-parent only. This adapter therefore
 * copies into a private destination sibling, durably closes and reads it back,
 * atomically publishes it within the destination directory and only then
 * removes the freshly revalidated source. It never overwrites a destination.
 */
#ifndef USERSPACE_DESKTOP_FILE_MOVE_H
#define USERSPACE_DESKTOP_FILE_MOVE_H

#include <stdint.h>

#include "x86os.h"

#define DESKTOP_FILE_MOVE_PATH_CAPACITY 256U
#define DESKTOP_FILE_MOVE_CHUNK_CAPACITY 1024U
#define DESKTOP_FILE_MOVE_TEMP_ATTEMPTS 32U
#define DESKTOP_FILE_MOVE_MAX_BYTES (64U * 1024U * 1024U)
#define DESKTOP_FILE_MOVE_TIMEOUT_MS 30000U
#define DESKTOP_FILE_MOVE_REQUEST_TIMEOUT_MS 1000U

enum desktop_file_move_status {
    DESKTOP_FILE_MOVE_OK = 0,
    DESKTOP_FILE_MOVE_ENOENT = -2,
    DESKTOP_FILE_MOVE_EIO = -5,
    DESKTOP_FILE_MOVE_EEXIST = -17,
    DESKTOP_FILE_MOVE_ENOTDIR = -20,
    DESKTOP_FILE_MOVE_EINVAL = -22,
    DESKTOP_FILE_MOVE_ECAPACITY = -75,
    DESKTOP_FILE_MOVE_ETIMEDOUT = -110,
    DESKTOP_FILE_MOVE_ESTALE = -116,
    DESKTOP_FILE_MOVE_EPARTIAL = -117
};

typedef struct desktop_file_move_request {
    char source_path[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    char source_directory_path[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    char destination_directory_path[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    x86os_file_info_t source_identity;
    x86os_file_info_t source_directory_identity;
    x86os_file_info_t destination_directory_identity;
} desktop_file_move_request_t;

typedef struct desktop_file_move_result {
    uint32_t destination_published;
    uint32_t source_removed;
    uint32_t duplicate_retained;
    uint32_t bytes_copied;
    char destination_path[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    char temporary_path[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    x86os_file_info_t destination_identity;
} desktop_file_move_result_t;

void desktop_file_move_request_initialize(
    desktop_file_move_request_t *request);
void desktop_file_move_result_initialize(
    desktop_file_move_result_t *result);

/** Reject immutable/system namespaces and private transaction names. */
uint32_t desktop_file_move_source_allowed(const char *path);
/** Reject protected destinations while allowing ordinary user directories. */
uint32_t desktop_file_move_destination_allowed(const char *path);

/** Execute one bounded verified MOVE without cross-parent kernel rename. */
int desktop_file_move_execute(
    const desktop_file_move_request_t *request,
    desktop_file_move_result_t *result);

#endif
