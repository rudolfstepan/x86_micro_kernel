#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "desktop_explorer.h"

static const x86os_file_info_t root_entries[] = {
    {"ZETA.TXT", X86OS_FILE, 10U, 0U, 0U, 0U},
    {"BIN", X86OS_DIRECTORY, 0U, 0U, 0U, 0U},
    {"alpha.prg", X86OS_FILE, 20U, 0U, 0U, 0U},
    {"Docs", X86OS_DIRECTORY, 0U, 0U, 0U, 0U},
};

int x86os_stat(const char *path, x86os_file_info_t *info) {
    if (strcmp(path, "/") != 0 && strcmp(path, "/Docs") != 0) return -1;
    memset(info, 0, sizeof(*info));
    strcpy(info->name, path);
    info->type = X86OS_DIRECTORY;
    return 0;
}

int x86os_readdir_batch(const char *path, uint32_t index,
                        x86os_file_info_t *entries) {
    if (strcmp(path, "/Docs") == 0) return 0;
    if (strcmp(path, "/") != 0 || index >= 4U) return 0;
    uint32_t count = 4U - index;
    if (count > X86OS_READDIR_BATCH_CAPACITY)
        count = X86OS_READDIR_BATCH_CAPACITY;
    for (uint32_t item = 0U; item < count; ++item)
        entries[item] = root_entries[index + item];
    return (int)count;
}

static void test_directory_snapshot_is_sorted_and_atomic(void) {
    desktop_explorer_t explorer;
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    const desktop_explorer_window_t *window = &explorer.windows[0];
    assert(window->active && window->entry_count == 4U);
    assert(strcmp(window->entries[0].name, "BIN") == 0);
    assert(strcmp(window->entries[1].name, "Docs") == 0);
    assert(strcmp(window->entries[2].name, "alpha.prg") == 0);
    assert(strcmp(window->entries[3].name, "ZETA.TXT") == 0);
    assert(desktop_explorer_open(&explorer, 0U, "/missing") ==
           DESKTOP_EXPLORER_ENOENT);
    assert(strcmp(explorer.windows[0].path, "/") == 0);
}

static void test_child_path_and_window_capacity(void) {
    desktop_explorer_t explorer;
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    assert(desktop_explorer_child_path(
        &explorer.windows[0], 1U, path, sizeof(path)) == 0);
    assert(strcmp(path, "/Docs") == 0);
    assert(desktop_explorer_free_window(&explorer) == 1U);
    desktop_explorer_close(&explorer, 0U);
    assert(desktop_explorer_free_window(&explorer) == 0U);
}

static void test_same_item_double_click_activates_once(void) {
    desktop_explorer_t explorer;
    desktop_explorer_result_t result;
    desktop_rect_t client = {20, 40, 320U, 180U};
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    desktop_rect_t icon = desktop_explorer_entry_rect(
        &explorer.windows[0], client, 1U);
    int32_t x = icon.x + 4;
    int32_t y = icon.y + 4;

    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_press(
        &explorer, 0U, client, x, y, &result) == 0);
    assert(result.selection_changed && result.entry_index == 1U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_release(
        &explorer, 0U, client, x, y, 1000U, &result) == 0);
    assert(!result.activated);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_press(
        &explorer, 0U, client, x, y, &result) == 0);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_release(
        &explorer, 0U, client, x, y, 1300U, &result) == 0);
    assert(result.activated && result.entry_index == 1U);
}

static void test_keyboard_grid_navigation_and_activation(void) {
    desktop_explorer_t explorer;
    desktop_explorer_result_t result;
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, 2U, DESKTOP_EXPLORER_KEY_RIGHT, &result) == 0);
    assert(result.selection_changed && result.entry_index == 0U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, 2U, DESKTOP_EXPLORER_KEY_DOWN, &result) == 0);
    assert(result.selection_changed && result.entry_index == 2U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, 2U, DESKTOP_EXPLORER_KEY_ENTER, &result) == 0);
    assert(result.activated && result.entry_index == 2U);
}

int main(void) {
    test_directory_snapshot_is_sorted_and_atomic();
    test_child_path_and_window_capacity();
    test_same_item_double_click_activates_once();
    test_keyboard_grid_navigation_and_activation();
    return 0;
}
