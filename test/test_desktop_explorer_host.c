#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "desktop_explorer.h"

static const x86os_file_info_t root_entries[] = {
    {".", X86OS_DIRECTORY, 0U, 0U, 0U, 0U},
    {"..", X86OS_DIRECTORY, 0U, 0U, 0U, 0U},
    {"ZETA.TXT", X86OS_FILE, 10U, 0U, 0U, 0U},
    {"BIN", X86OS_DIRECTORY, 0U, 0U, 0U, 0U},
    {"alpha.prg", X86OS_FILE, 20U, 0U, 0U, 0U},
    {"Docs", X86OS_DIRECTORY, 0U, 0U, 0U, 0U},
    {"RT12AB34.TRS", X86OS_FILE, 42U, 0U, 0U, 0U},
};

static uint64_t monotonic_now = 100U;
static uint32_t monotonic_calls;
static uint32_t expire_call;

int x86os_monotonic_ms(uint64_t *value) {
    assert(value != NULL);
    ++monotonic_calls;
    if (monotonic_calls == expire_call) monotonic_now += 6000U;
    *value = monotonic_now++;
    return 0;
}

int reist_vfs_stat(const char *path, x86os_file_info_t *info,
                   uint32_t timeout_ms) {
    assert(timeout_ms != 0U && timeout_ms <= 1000U);
    if (strcmp(path, "/") != 0 && strcmp(path, "/Docs") != 0 &&
        strcmp(path, "/BIN") != 0) return -2;
    memset(info, 0, sizeof(*info));
    strcpy(info->name, path);
    info->type = X86OS_DIRECTORY;
    return 0;
}

int reist_vfs_readdir_at(const char *path, uint32_t index,
                         x86os_file_info_t *entry, uint32_t timeout_ms) {
    assert(timeout_ms != 0U && timeout_ms <= 1000U);
    if (strcmp(path, "/Docs") == 0) return 0;
    if (strcmp(path, "/BIN") == 0) {
        if (index != 0U) return 0;
        *entry = (x86os_file_info_t){
            "TOOL.PRG", X86OS_FILE, 1U, 0U, 0U, 0U};
        return 1;
    }
    if (strcmp(path, "/") != 0 || index >= 7U) return 0;
    *entry = root_entries[index];
    return 1;
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
    assert(window->directory_nonempty[0] == 1U);
    assert(window->directory_nonempty[1] == 0U);
    assert(desktop_explorer_icon_kind(&window->entries[0],
        window->directory_nonempty[0]) == DESKTOP_EXPLORER_ICON_FOLDER_FULL);
    assert(desktop_explorer_icon_kind(&window->entries[1],
        window->directory_nonempty[1]) == DESKTOP_EXPLORER_ICON_FOLDER_EMPTY);
    assert(desktop_explorer_open(&explorer, 0U, "/missing") ==
           DESKTOP_EXPLORER_ENOENT);
    assert(strcmp(explorer.windows[0].path, "/") == 0);
}

static void test_deadline_failure_keeps_published_snapshot(void) {
    desktop_explorer_t explorer;
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    uint32_t generation = explorer.windows[0].snapshot_generation;
    expire_call = monotonic_calls + 2U;
    assert(desktop_explorer_open(&explorer, 0U, "/") ==
           DESKTOP_EXPLORER_ETIMEDOUT);
    expire_call = 0U;
    assert(explorer.windows[0].snapshot_generation == generation);
    assert(strcmp(explorer.windows[0].path, "/") == 0);
    assert(explorer.windows[0].entry_count == 4U);
}

static void test_extension_icons_are_case_insensitive(void) {
    static const struct {
        const char *name;
        uint32_t expected;
    } cases[] = {
        {"APP.PRG", DESKTOP_EXPLORER_ICON_PROGRAM},
        {"notes.Txt", DESKTOP_EXPLORER_ICON_TEXT},
        {"system.CONF", DESKTOP_EXPLORER_ICON_SETTINGS},
        {"sound.WAV", DESKTOP_EXPLORER_ICON_AUDIO},
        {"photo.BMP", DESKTOP_EXPLORER_ICON_IMAGE},
        {"anim.gif", DESKTOP_EXPLORER_ICON_IMAGE},
        {"native.ico", DESKTOP_EXPLORER_ICON_IMAGE},
        {"archive.bin", DESKTOP_EXPLORER_ICON_UNKNOWN},
        {"no-extension", DESKTOP_EXPLORER_ICON_UNKNOWN},
    };
    x86os_file_info_t entry = {0};
    entry.type = X86OS_FILE;
    for (uint32_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        memset(entry.name, 0, sizeof(entry.name));
        strcpy(entry.name, cases[index].name);
        assert(desktop_explorer_icon_kind(&entry, 0U) == cases[index].expected);
    }
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

static void test_drag_object_is_bound_to_snapshot_generation(void) {
    desktop_explorer_t explorer;
    desktop_drag_object_t object;
    desktop_explorer_drag_file_t file;
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    uint32_t generation = explorer.windows[0].snapshot_generation;
    assert(generation != 0U);
    assert(desktop_explorer_drag_object(
               &explorer, 0U, 2U, &object) == DESKTOP_EXPLORER_OK);
    assert(object.kind == DESKTOP_DRAG_OBJECT_FILE);
    assert(object.operations == (DESKTOP_DRAG_OPERATION_MOVE |
                                  DESKTOP_DRAG_OPERATION_COPY |
                                  DESKTOP_DRAG_OPERATION_LINK));
    assert(object.source_generation == generation);
    assert(desktop_explorer_drag_validate(
               &explorer, &object, &file) == DESKTOP_EXPLORER_OK);
    assert(strcmp(file.path, "/alpha.prg") == 0);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    assert(explorer.windows[0].snapshot_generation != generation);
    assert(desktop_explorer_drag_validate(
               &explorer, &object, &file) == DESKTOP_EXPLORER_ESTALE);
}

int main(void) {
    test_directory_snapshot_is_sorted_and_atomic();
    test_deadline_failure_keeps_published_snapshot();
    test_extension_icons_are_case_insensitive();
    test_child_path_and_window_capacity();
    test_same_item_double_click_activates_once();
    test_keyboard_grid_navigation_and_activation();
    test_drag_object_is_bound_to_snapshot_generation();
    return 0;
}
