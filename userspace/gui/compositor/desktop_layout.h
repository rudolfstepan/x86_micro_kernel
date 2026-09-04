/**
 * @file userspace/gui/compositor/desktop_layout.h
 * @brief Fixed-capacity persistent desktop icon grid.
 *
 * The module is a pure bounded layout and reist.desktop-layout/1 codec.  It
 * performs no filesystem operations; the compositor owns durable loading and
 * publication outside render and input hit-test paths.
 */
#ifndef USERSPACE_DESKTOP_LAYOUT_H
#define USERSPACE_DESKTOP_LAYOUT_H

#include <stdint.h>

#include "desktop_wm.h"

#define DESKTOP_LAYOUT_SCHEMA "reist.desktop-layout/1"
#define DESKTOP_LAYOUT_PATH "/etc/reist/desktop-layout.conf"
#define DESKTOP_LAYOUT_TEMP_PATH "/etc/reist/desktop-layout.tmp"
#define DESKTOP_LAYOUT_ENTRY_CAPACITY 131U
#define DESKTOP_LAYOUT_IDENTITY_CAPACITY 272U
#define DESKTOP_LAYOUT_FILE_CAPACITY 73728U
#define DESKTOP_LAYOUT_GRID_AXIS_CAPACITY 256U
#define DESKTOP_LAYOUT_SEARCH_CAPACITY 4096U
#define DESKTOP_LAYOUT_PREFERRED_CELL_WIDTH 176U

enum desktop_layout_status {
    DESKTOP_LAYOUT_OK = 0,
    DESKTOP_LAYOUT_EINVAL = -22,
    DESKTOP_LAYOUT_ENOENT = -2,
    DESKTOP_LAYOUT_EIO = -5,
    DESKTOP_LAYOUT_ECAPACITY = -75,
    DESKTOP_LAYOUT_EMALFORMED = -84,
    DESKTOP_LAYOUT_EOCCUPIED = -17
};

enum desktop_layout_builtin {
    DESKTOP_LAYOUT_BUILTIN_COMPUTER = 0U,
    DESKTOP_LAYOUT_BUILTIN_CONTROL,
    DESKTOP_LAYOUT_BUILTIN_TRASH,
    DESKTOP_LAYOUT_BUILTIN_COUNT
};

typedef struct desktop_layout_identity {
    char value[DESKTOP_LAYOUT_IDENTITY_CAPACITY];
} desktop_layout_identity_t;

typedef struct desktop_layout_cell {
    uint32_t column;
    uint32_t row;
} desktop_layout_cell_t;

typedef struct desktop_layout_entry {
    desktop_layout_identity_t identity;
    uint32_t column;
    uint32_t row;
} desktop_layout_entry_t;

typedef struct desktop_layout_document {
    desktop_layout_entry_t entries[DESKTOP_LAYOUT_ENTRY_CAPACITY];
    uint32_t entry_count;
} desktop_layout_document_t;

typedef struct desktop_layout_view {
    desktop_layout_entry_t entries[DESKTOP_LAYOUT_ENTRY_CAPACITY];
    uint32_t entry_count;
    uint32_t generation;
    desktop_rect_t work_area;
    uint32_t columns;
    uint32_t rows;
    uint32_t cell_width;
    uint32_t cell_height;
} desktop_layout_view_t;

void desktop_layout_document_initialize(desktop_layout_document_t *document);
void desktop_layout_view_initialize(desktop_layout_view_t *view);
int desktop_layout_identity_builtin(desktop_layout_identity_t *identity,
                                    uint32_t builtin_index);
int desktop_layout_identity_file(desktop_layout_identity_t *identity,
                                 const char *filename);

/** Parse and publish only a complete canonical document. */
int desktop_layout_parse(const uint8_t *bytes, uint32_t size,
                         desktop_layout_document_t *document);
/** Serialize entries in stable identity order. */
int desktop_layout_serialize(const desktop_layout_document_t *document,
                             uint8_t *bytes, uint32_t capacity,
                             uint32_t *size_out);

/** Resolve stored/default cells into one temporary collision-free view. */
int desktop_layout_resolve(
    const desktop_layout_document_t *document,
    const desktop_layout_identity_t *identities, uint32_t identity_count,
    desktop_rect_t work_area, uint32_t cell_height, uint32_t generation,
    desktop_layout_view_t *view);
desktop_rect_t desktop_layout_view_rect(const desktop_layout_view_t *view,
                                        uint32_t index);
/** Resolve a pointer drop to the nearest unoccupied bounded grid cell. */
int desktop_layout_drop(const desktop_layout_view_t *view,
                        uint32_t source_index, int32_t x, int32_t y,
                        desktop_layout_cell_t *cell);
/** Build a complete candidate from the current temporary view and one move. */
int desktop_layout_move_document(const desktop_layout_view_t *view,
                                 uint32_t source_index,
                                 desktop_layout_cell_t cell,
                                 desktop_layout_document_t *candidate);

#endif
