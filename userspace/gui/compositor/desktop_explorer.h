/**
 * @file userspace/gui/compositor/desktop_explorer.h
 * @brief Fixed-capacity folder-window state for the trusted desktop process.
 *
 * This is a compositor-internal VFS adapter, not a public GUI-client API. It
 * owns bounded path and directory snapshots, icon selection and double-click
 * recognition. Window geometry, Z-order and drawing remain in desktop_wm and
 * desktop.c. One compositor thread serializes all calls.
 */
#ifndef USERSPACE_DESKTOP_EXPLORER_H
#define USERSPACE_DESKTOP_EXPLORER_H

#include <stdint.h>

#include "desktop_drag.h"
#include "desktop_wm.h"
#include "x86os.h"

#define DESKTOP_EXPLORER_WINDOW_CAPACITY 8U
#define DESKTOP_EXPLORER_ENTRY_CAPACITY 128U
#define DESKTOP_EXPLORER_PATH_CAPACITY 256U
#define DESKTOP_EXPLORER_NO_ENTRY UINT32_MAX
#define DESKTOP_EXPLORER_DOUBLE_CLICK_MS 500U
#define DESKTOP_EXPLORER_ICON_WIDTH 104U
#define DESKTOP_EXPLORER_ICON_HEIGHT 76U
#define DESKTOP_EXPLORER_SCAN_CAPACITY 129U
#define DESKTOP_EXPLORER_SCROLLBAR_EXTENT 18U
#define DESKTOP_EXPLORER_SCROLLBAR_MIN_THUMB 8U
#define DESKTOP_EXPLORER_WHEEL_ROWS 3U
#define DESKTOP_EXPLORER_HISTORY_CAPACITY 16U
#define DESKTOP_EXPLORER_DIRECTORY_PROBE_ENTRIES 8U
#define DESKTOP_EXPLORER_SNAPSHOT_TIMEOUT_MS 10000U
#define DESKTOP_EXPLORER_REQUEST_TIMEOUT_MS 1000U

enum desktop_explorer_status {
    DESKTOP_EXPLORER_OK = 0,
    DESKTOP_EXPLORER_EINVAL = -22,
    DESKTOP_EXPLORER_ENOENT = -2,
    DESKTOP_EXPLORER_ENOTDIR = -20,
    DESKTOP_EXPLORER_EIO = -5,
    DESKTOP_EXPLORER_ETIMEDOUT = -110,
    DESKTOP_EXPLORER_ECAPACITY = -75,
    DESKTOP_EXPLORER_ESTALE = -116
};

enum desktop_explorer_key {
    DESKTOP_EXPLORER_KEY_LEFT = 1U,
    DESKTOP_EXPLORER_KEY_RIGHT,
    DESKTOP_EXPLORER_KEY_UP,
    DESKTOP_EXPLORER_KEY_DOWN,
    DESKTOP_EXPLORER_KEY_ENTER
};

enum desktop_explorer_icon {
    DESKTOP_EXPLORER_ICON_FOLDER_EMPTY = 0U,
    DESKTOP_EXPLORER_ICON_FOLDER_FULL,
    DESKTOP_EXPLORER_ICON_PROGRAM,
    DESKTOP_EXPLORER_ICON_TEXT,
    DESKTOP_EXPLORER_ICON_AUDIO,
    DESKTOP_EXPLORER_ICON_IMAGE,
    DESKTOP_EXPLORER_ICON_SETTINGS,
    DESKTOP_EXPLORER_ICON_UNKNOWN,
    DESKTOP_EXPLORER_ICON_COUNT
};

enum desktop_explorer_scroll_capture {
    DESKTOP_EXPLORER_SCROLL_NONE = 0U,
    DESKTOP_EXPLORER_SCROLL_DECREMENT,
    DESKTOP_EXPLORER_SCROLL_INCREMENT,
    DESKTOP_EXPLORER_SCROLL_PAGE_DECREMENT,
    DESKTOP_EXPLORER_SCROLL_PAGE_INCREMENT,
    DESKTOP_EXPLORER_SCROLL_THUMB
};

typedef struct desktop_explorer_window {
    uint32_t active;
    uint32_t snapshot_generation;
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    x86os_file_info_t entries[DESKTOP_EXPLORER_ENTRY_CAPACITY];
    uint8_t directory_nonempty[DESKTOP_EXPLORER_ENTRY_CAPACITY];
    uint32_t entry_count;
    uint32_t truncated;
    uint32_t selected;
    uint32_t pressed;
    uint32_t last_click;
    uint64_t last_click_ms;
    uint32_t first_row;
    uint32_t scroll_capture;
    uint32_t scroll_drag_offset;
    char back_paths[DESKTOP_EXPLORER_HISTORY_CAPACITY]
                   [DESKTOP_EXPLORER_PATH_CAPACITY];
    char forward_paths[DESKTOP_EXPLORER_HISTORY_CAPACITY]
                      [DESKTOP_EXPLORER_PATH_CAPACITY];
    uint32_t back_count;
    uint32_t forward_count;
} desktop_explorer_window_t;

typedef struct desktop_explorer_layout {
    desktop_rect_t viewport;
    desktop_rect_t scrollbar;
    desktop_rect_t decrement;
    desktop_rect_t increment;
    desktop_rect_t track;
    desktop_rect_t thumb;
    uint32_t columns;
    uint32_t visible_rows;
    uint32_t total_rows;
    uint32_t maximum_first_row;
    uint32_t first_row;
    uint32_t enabled;
} desktop_explorer_layout_t;

typedef struct desktop_explorer {
    desktop_explorer_window_t windows[DESKTOP_EXPLORER_WINDOW_CAPACITY];
    x86os_file_info_t staging[DESKTOP_EXPLORER_ENTRY_CAPACITY];
    uint8_t staging_directory_nonempty[DESKTOP_EXPLORER_ENTRY_CAPACITY];
    char staging_path[DESKTOP_EXPLORER_PATH_CAPACITY];
    uint32_t staging_count;
    uint32_t staging_truncated;
    uint32_t next_snapshot_generation;
    uint32_t desktop_selected;
    uint32_t desktop_pressed;
    uint64_t desktop_last_click_ms;
} desktop_explorer_t;

typedef struct desktop_explorer_drag_file {
    uint32_t version;
    uint32_t struct_size;
    uint32_t window_index;
    uint32_t entry_index;
    uint32_t snapshot_generation;
    uint32_t reserved[3];
    x86os_file_info_t identity;
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
} desktop_explorer_drag_file_t;

#define DESKTOP_EXPLORER_DRAG_FILE_VERSION 1U

typedef struct desktop_explorer_result {
    uint32_t consumed;
    uint32_t selection_changed;
    uint32_t viewport_changed;
    uint32_t activated;
    uint32_t window_index;
    uint32_t entry_index;
} desktop_explorer_result_t;

void desktop_explorer_initialize(desktop_explorer_t *explorer);
void desktop_explorer_result_initialize(desktop_explorer_result_t *result);
/** Classify one snapshot entry with a bounded case-insensitive extension. */
uint32_t desktop_explorer_icon_kind(const x86os_file_info_t *entry,
                                    uint32_t directory_nonempty);
/** Track the single bounded root-volume desktop icon. */
void desktop_explorer_desktop_press(desktop_explorer_t *explorer,
                                    uint32_t hit,
                                    desktop_explorer_result_t *result);
void desktop_explorer_desktop_release(desktop_explorer_t *explorer,
                                      uint32_t hit, uint64_t now_ms,
                                      desktop_explorer_result_t *result);

/** Read and atomically publish one bounded directory snapshot. */
int desktop_explorer_open(desktop_explorer_t *explorer,
                          uint32_t window_index, const char *path);
/** Navigate one existing window and push its old path onto Back history. */
int desktop_explorer_navigate(desktop_explorer_t *explorer,
                              uint32_t window_index, const char *path);
/** Atomically publish the previous or next bounded history snapshot. */
int desktop_explorer_back(desktop_explorer_t *explorer,
                          uint32_t window_index);
int desktop_explorer_forward(desktop_explorer_t *explorer,
                             uint32_t window_index);
/** Navigate to the canonical parent, or ENOENT at the VFS root. */
int desktop_explorer_up(desktop_explorer_t *explorer,
                        uint32_t window_index);
/** Reload the current path without changing Back or Forward history. */
int desktop_explorer_refresh(desktop_explorer_t *explorer,
                             uint32_t window_index);
uint32_t desktop_explorer_can_back(const desktop_explorer_window_t *window);
uint32_t desktop_explorer_can_forward(const desktop_explorer_window_t *window);
uint32_t desktop_explorer_can_up(const desktop_explorer_window_t *window);
/** Drop one window snapshot after the WM closes its corresponding slot. */
void desktop_explorer_close(desktop_explorer_t *explorer,
                            uint32_t window_index);
/** Return the first inactive folder-window slot or UINT32_MAX. */
uint32_t desktop_explorer_free_window(const desktop_explorer_t *explorer);

/** Build a child path without truncation or mutation on failure. */
int desktop_explorer_child_path(const desktop_explorer_window_t *window,
                                uint32_t entry_index,
                                char *path, uint32_t capacity);

/** Derive the bounded icon viewport and vertical scrollbar from a client. */
desktop_explorer_layout_t desktop_explorer_layout(
    const desktop_explorer_window_t *window, desktop_rect_t client);

/** Compute one icon-cell rectangle in a window client rectangle. */
desktop_rect_t desktop_explorer_entry_rect(
    const desktop_explorer_window_t *window, desktop_rect_t client,
    uint32_t entry_index);
/** Hit-test a visible icon cell or return DESKTOP_EXPLORER_NO_ENTRY. */
uint32_t desktop_explorer_entry_at(
    const desktop_explorer_window_t *window, desktop_rect_t client,
    int32_t x, int32_t y);

/** Begin an implicit client-area click and update selection. */
int desktop_explorer_pointer_press(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t x, int32_t y,
    desktop_explorer_result_t *result);
/** Update a captured scrollbar thumb without changing icon press state. */
int desktop_explorer_pointer_motion(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t x, int32_t y,
    desktop_explorer_result_t *result);
/** Finish the captured click and recognize a bounded same-item double click. */
int desktop_explorer_pointer_release(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t x, int32_t y, uint64_t now_ms,
    desktop_explorer_result_t *result);
/** Cancel an entry press after the drag controller owns button release. */
void desktop_explorer_pointer_cancel(desktop_explorer_t *explorer,
                                     uint32_t window_index);
/** Scroll the Explorer under the pointer; positive wheel values move up. */
int desktop_explorer_wheel(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t wheel,
    desktop_explorer_result_t *result);
/** Clamp row state and cancel scrollbar capture after client resize. */
int desktop_explorer_resize(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, desktop_explorer_result_t *result);
/** Publish one generation-bound Explorer entry as a generic file object. */
int desktop_explorer_drag_object(const desktop_explorer_t *explorer,
                                 uint32_t window_index, uint32_t entry_index,
                                 desktop_drag_object_t *object);
/** Validate and decode a file object against the current Explorer snapshot. */
int desktop_explorer_drag_validate(
    const desktop_explorer_t *explorer, const desktop_drag_object_t *object,
    desktop_explorer_drag_file_t *file);
/** Move selection in the icon grid, reveal it, or activate it with Enter. */
int desktop_explorer_keyboard(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, uint32_t key, desktop_explorer_result_t *result);

#endif
