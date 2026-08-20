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

#include "desktop_wm.h"
#include "x86os.h"

#define DESKTOP_EXPLORER_WINDOW_CAPACITY 8U
#define DESKTOP_EXPLORER_ENTRY_CAPACITY 32U
#define DESKTOP_EXPLORER_PATH_CAPACITY 256U
#define DESKTOP_EXPLORER_NO_ENTRY UINT32_MAX
#define DESKTOP_EXPLORER_DOUBLE_CLICK_MS 500U
#define DESKTOP_EXPLORER_ICON_WIDTH 104U
#define DESKTOP_EXPLORER_ICON_HEIGHT 76U

enum desktop_explorer_status {
    DESKTOP_EXPLORER_OK = 0,
    DESKTOP_EXPLORER_EINVAL = -22,
    DESKTOP_EXPLORER_ENOENT = -2,
    DESKTOP_EXPLORER_ENOTDIR = -20,
    DESKTOP_EXPLORER_EIO = -5,
    DESKTOP_EXPLORER_ECAPACITY = -75
};

enum desktop_explorer_key {
    DESKTOP_EXPLORER_KEY_LEFT = 1U,
    DESKTOP_EXPLORER_KEY_RIGHT,
    DESKTOP_EXPLORER_KEY_UP,
    DESKTOP_EXPLORER_KEY_DOWN,
    DESKTOP_EXPLORER_KEY_ENTER
};

typedef struct desktop_explorer_window {
    uint32_t active;
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    x86os_file_info_t entries[DESKTOP_EXPLORER_ENTRY_CAPACITY];
    uint32_t entry_count;
    uint32_t truncated;
    uint32_t selected;
    uint32_t pressed;
    uint32_t last_click;
    uint64_t last_click_ms;
} desktop_explorer_window_t;

typedef struct desktop_explorer {
    desktop_explorer_window_t windows[DESKTOP_EXPLORER_WINDOW_CAPACITY];
    x86os_file_info_t staging[DESKTOP_EXPLORER_ENTRY_CAPACITY];
    char staging_path[DESKTOP_EXPLORER_PATH_CAPACITY];
    uint32_t staging_count;
    uint32_t staging_truncated;
    uint32_t desktop_selected;
    uint32_t desktop_pressed;
    uint64_t desktop_last_click_ms;
} desktop_explorer_t;

typedef struct desktop_explorer_result {
    uint32_t consumed;
    uint32_t selection_changed;
    uint32_t activated;
    uint32_t window_index;
    uint32_t entry_index;
} desktop_explorer_result_t;

void desktop_explorer_initialize(desktop_explorer_t *explorer);
void desktop_explorer_result_initialize(desktop_explorer_result_t *result);
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
/** Drop one window snapshot after the WM closes its corresponding slot. */
void desktop_explorer_close(desktop_explorer_t *explorer,
                            uint32_t window_index);
/** Return the first inactive folder-window slot or UINT32_MAX. */
uint32_t desktop_explorer_free_window(const desktop_explorer_t *explorer);

/** Build a child path without truncation or mutation on failure. */
int desktop_explorer_child_path(const desktop_explorer_window_t *window,
                                uint32_t entry_index,
                                char *path, uint32_t capacity);

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
/** Finish the captured click and recognize a bounded same-item double click. */
int desktop_explorer_pointer_release(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t x, int32_t y, uint64_t now_ms,
    desktop_explorer_result_t *result);
/** Move selection in the visible icon grid or activate it with Enter. */
int desktop_explorer_keyboard(
    desktop_explorer_t *explorer, uint32_t window_index,
    uint32_t columns, uint32_t key, desktop_explorer_result_t *result);

#endif
