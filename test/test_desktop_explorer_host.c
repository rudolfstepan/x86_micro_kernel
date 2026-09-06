#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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
    {"RM12AB34.TMP", X86OS_FILE, 42U, 0U, 0U, 0U},
};

static const x86os_file_info_t desktop_entries[] = {
    {"readme.txt", X86OS_FILE, 11U, 4U, 5U, 6U},
    {"LINK.LNK", X86OS_FILE, 72U, 7U, 8U, 9U},
};

static uint64_t monotonic_now = 100U;
static uint32_t desktop_path_type = X86OS_DIRECTORY;
static uint32_t monotonic_calls;
static uint32_t expire_call;
static uint32_t many_entry_count = 50U;

int x86os_monotonic_ms(uint64_t *value) {
    assert(value != NULL);
    ++monotonic_calls;
    if (monotonic_calls == expire_call)
        monotonic_now += DESKTOP_EXPLORER_SNAPSHOT_TIMEOUT_MS + 1000U;
    *value = monotonic_now++;
    return 0;
}

int reist_vfs_stat(const char *path, x86os_file_info_t *info,
                   uint32_t timeout_ms) {
    assert(timeout_ms != 0U && timeout_ms <= 1000U);
    if (strcmp(path, "/") != 0 && strcmp(path, "/Docs") != 0 &&
        strcmp(path, "/Docs/Sub") != 0 &&
        strcmp(path, "/desktop") != 0 &&
        strcmp(path, "/Many") != 0 &&
        strcmp(path, "/BIN") != 0) return -2;
    memset(info, 0, sizeof(*info));
    strcpy(info->name, path);
    info->type = strcmp(path, "/desktop") == 0
        ? desktop_path_type : X86OS_DIRECTORY;
    info->create_time = strcmp(path, "/desktop") == 0 ? 77U : 11U;
    return 0;
}

int reist_vfs_lstat(const char *path, x86os_file_info_t *info,
                    uint32_t timeout_ms) {
    return reist_vfs_stat(path, info, timeout_ms);
}

int reist_vfs_readdir_at(const char *path, uint32_t index,
                         x86os_file_info_t *entry, uint32_t timeout_ms) {
    assert(timeout_ms != 0U && timeout_ms <= 1000U);
    if (strcmp(path, "/Docs") == 0) {
        if (index != 0U) return 0;
        *entry = (x86os_file_info_t){
            "Sub", X86OS_DIRECTORY, 0U, 0U, 0U, 0U};
        return 1;
    }
    if (strcmp(path, "/Docs/Sub") == 0) return 0;
    if (strcmp(path, "/desktop") == 0) {
        if (index >= sizeof(desktop_entries) /
                sizeof(desktop_entries[0])) return 0;
        *entry = desktop_entries[index];
        return 1;
    }
    if (strcmp(path, "/Many") == 0) {
        if (index >= many_entry_count) return 0;
        memset(entry, 0, sizeof(*entry));
        (void)snprintf(entry->name, sizeof(entry->name),
                       "ITEM%03u.TXT", index);
        entry->type = X86OS_FILE;
        entry->size = index;
        return 1;
    }
    if (strcmp(path, "/BIN") == 0) {
        if (index != 0U) return 0;
        *entry = (x86os_file_info_t){
            "TOOL.PRG", X86OS_FILE, 1U, 0U, 0U, 0U};
        return 1;
    }
    if (strcmp(path, "/") != 0 || index >= 8U) return 0;
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
    assert(window->directory_nonempty[1] == 1U);
    assert(desktop_explorer_icon_kind(&window->entries[0],
        window->directory_nonempty[0]) == DESKTOP_EXPLORER_ICON_FOLDER_FULL);
    assert(desktop_explorer_icon_kind(&window->entries[1],
        window->directory_nonempty[1]) == DESKTOP_EXPLORER_ICON_FOLDER_FULL);
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
        {"document.LNK", DESKTOP_EXPLORER_ICON_SHORTCUT},
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

static void test_same_window_navigation_history_and_atomic_failure(void) {
    desktop_explorer_t explorer;
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    desktop_explorer_window_t *window = &explorer.windows[0];
    uint32_t initial_generation = window->snapshot_generation;

    assert(desktop_explorer_navigate(&explorer, 0U, "/Docs") == 0);
    assert(strcmp(window->path, "/Docs") == 0);
    assert(window->snapshot_generation != initial_generation);
    assert(window->back_count == 1U && window->forward_count == 0U);
    assert(desktop_explorer_can_back(window));
    assert(!desktop_explorer_can_forward(window));
    assert(desktop_explorer_can_up(window));

    assert(desktop_explorer_back(&explorer, 0U) == 0);
    assert(strcmp(window->path, "/") == 0);
    assert(window->back_count == 0U && window->forward_count == 1U);
    assert(!desktop_explorer_can_up(window));
    assert(desktop_explorer_forward(&explorer, 0U) == 0);
    assert(strcmp(window->path, "/Docs") == 0);
    assert(window->back_count == 1U && window->forward_count == 0U);
    assert(desktop_explorer_navigate(
        &explorer, 0U, "/Docs/Sub") == 0);
    assert(desktop_explorer_up(&explorer, 0U) == 0);
    assert(strcmp(window->path, "/Docs") == 0);

    uint32_t back_count = window->back_count;
    uint32_t forward_count = window->forward_count;
    uint32_t generation = window->snapshot_generation;
    uint32_t entry_count = window->entry_count;
    assert(desktop_explorer_navigate(
        &explorer, 0U, "/missing") == DESKTOP_EXPLORER_ENOENT);
    assert(strcmp(window->path, "/Docs") == 0);
    assert(window->back_count == back_count);
    assert(window->forward_count == forward_count);
    assert(window->snapshot_generation == generation);
    assert(window->entry_count == entry_count);

    assert(desktop_explorer_refresh(&explorer, 0U) == 0);
    assert(strcmp(window->path, "/Docs") == 0);
    assert(window->back_count == back_count);
    assert(window->forward_count == forward_count);
    assert(window->snapshot_generation != generation);
}

static void test_navigation_history_rolls_over_at_fixed_capacity(void) {
    desktop_explorer_t explorer;
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    desktop_explorer_window_t *window = &explorer.windows[0];
    for (uint32_t index = 0U;
         index < DESKTOP_EXPLORER_HISTORY_CAPACITY + 4U; ++index) {
        const char *path = (index & 1U) != 0U ? "/Docs" : "/BIN";
        assert(desktop_explorer_navigate(&explorer, 0U, path) == 0);
    }
    assert(window->back_count == DESKTOP_EXPLORER_HISTORY_CAPACITY);
    for (uint32_t index = 0U;
         index < DESKTOP_EXPLORER_HISTORY_CAPACITY; ++index)
        assert(desktop_explorer_back(&explorer, 0U) == 0);
    assert(window->back_count == 0U);
    assert(window->forward_count == DESKTOP_EXPLORER_HISTORY_CAPACITY);
    assert(desktop_explorer_back(&explorer, 0U) ==
           DESKTOP_EXPLORER_ENOENT);
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
    desktop_rect_t client = {20, 40, 226U, 152U};
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, client, DESKTOP_EXPLORER_KEY_RIGHT, &result) == 0);
    assert(result.selection_changed && result.entry_index == 0U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, client, DESKTOP_EXPLORER_KEY_DOWN, &result) == 0);
    assert(result.selection_changed && result.entry_index == 2U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, client, DESKTOP_EXPLORER_KEY_ENTER, &result) == 0);
    assert(result.activated && result.entry_index == 2U);
}

static void test_scrollbar_geometry_input_and_resize_are_bounded(void) {
    desktop_explorer_t explorer;
    desktop_explorer_result_t result;
    desktop_rect_t client = {20, 40, 330U, 152U};
    desktop_explorer_initialize(&explorer);
    many_entry_count = 50U;
    assert(desktop_explorer_open(&explorer, 0U, "/Many") == 0);
    desktop_explorer_window_t *window = &explorer.windows[0];
    assert(window->entry_count == 50U && !window->truncated);

    desktop_explorer_layout_t layout =
        desktop_explorer_layout(window, client);
    assert(layout.scrollbar.x == client.x + (int32_t)client.width -
                                  (int32_t)DESKTOP_EXPLORER_SCROLLBAR_EXTENT);
    assert(layout.scrollbar.y == client.y);
    assert(layout.scrollbar.height == client.height);
    assert(layout.viewport.width + layout.scrollbar.width == client.width);
    assert(layout.columns == 3U && layout.visible_rows == 2U);
    assert(layout.total_rows == 17U && layout.maximum_first_row == 15U);
    assert(layout.enabled && layout.first_row == 0U);

    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_press(
        &explorer, 0U, client,
        layout.increment.x + 1,
        layout.increment.y + 1, &result) == 0);
    assert(result.consumed && result.viewport_changed);
    assert(window->first_row == 1U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_release(
        &explorer, 0U, client, layout.increment.x + 1,
        layout.increment.y + 1, 1000U, &result) == 0);
    assert(window->scroll_capture == DESKTOP_EXPLORER_SCROLL_NONE);

    layout = desktop_explorer_layout(window, client);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_press(
        &explorer, 0U, client, layout.thumb.x + 1,
        layout.thumb.y + 1, &result) == 0);
    assert(window->scroll_capture == DESKTOP_EXPLORER_SCROLL_THUMB);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_motion(
        &explorer, 0U, client, layout.thumb.x + 1,
        layout.track.y + (int32_t)layout.track.height + 50,
        &result) == 0);
    assert(result.viewport_changed);
    assert(window->first_row == layout.maximum_first_row);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_release(
        &explorer, 0U, client, layout.thumb.x + 1,
        layout.track.y + 1, 1100U, &result) == 0);

    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_wheel(
        &explorer, 0U, client, 1, &result) == 0);
    assert(result.viewport_changed && window->first_row == 12U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_wheel(
        &explorer, 0U, client, -1, &result) == 0);
    assert(result.viewport_changed && window->first_row == 15U);

    desktop_rect_t enlarged = {20, 40, 330U, 2000U};
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_resize(
        &explorer, 0U, enlarged, &result) == 0);
    assert(result.viewport_changed && window->first_row == 0U);
    layout = desktop_explorer_layout(window, enlarged);
    assert(layout.scrollbar.x == enlarged.x + (int32_t)enlarged.width -
                                  (int32_t)layout.scrollbar.width);
    assert(!layout.enabled && layout.first_row == 0U);
}

static void test_scrolled_hit_test_keyboard_reveal_and_capacity(void) {
    desktop_explorer_t explorer;
    desktop_explorer_result_t result;
    desktop_rect_t client = {10, 10, 226U, 152U};
    desktop_explorer_initialize(&explorer);
    many_entry_count = 50U;
    assert(desktop_explorer_open(&explorer, 0U, "/Many") == 0);
    desktop_explorer_window_t *window = &explorer.windows[0];

    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_wheel(
        &explorer, 0U, client, -1, &result) == 0);
    assert(window->first_row == 3U);
    desktop_rect_t visible = desktop_explorer_entry_rect(window, client, 6U);
    assert(visible.width != 0U && visible.y == client.y);
    assert(desktop_explorer_entry_rect(window, client, 0U).width == 0U);
    assert(desktop_explorer_entry_at(
        window, client, visible.x + 1, visible.y + 1) == 6U);

    window->selected = 6U;
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, client, DESKTOP_EXPLORER_KEY_UP, &result) == 0);
    assert(window->selected == 4U && window->first_row == 2U);
    window->selected = 48U;
    window->first_row = 0U;
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_keyboard(
        &explorer, 0U, client, DESKTOP_EXPLORER_KEY_RIGHT, &result) == 0);
    assert(window->selected == 49U && result.viewport_changed);
    assert(window->first_row == 23U);

    many_entry_count = DESKTOP_EXPLORER_ENTRY_CAPACITY + 1U;
    assert(desktop_explorer_open(&explorer, 0U, "/Many") == 0);
    assert(window->entry_count == DESKTOP_EXPLORER_ENTRY_CAPACITY);
    assert(window->truncated);
    many_entry_count = 50U;
}

static void test_details_view_toggle_layout_and_state_are_bounded(void) {
    desktop_explorer_t explorer;
    desktop_explorer_result_t result;
    desktop_rect_t client = {20, 40, 330U, 176U};
    desktop_explorer_initialize(&explorer);
    many_entry_count = 50U;
    assert(desktop_explorer_open(&explorer, 0U, "/Many") == 0);
    desktop_explorer_window_t *window = &explorer.windows[0];
    assert(window->view == DESKTOP_EXPLORER_VIEW_ICONS);
    uint32_t generation = window->snapshot_generation;
    window->selected = 17U;
    window->first_row = 4U;

    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_toggle_view(
        &explorer, 0U, client, &result) == DESKTOP_EXPLORER_OK);
    assert(result.consumed && result.viewport_changed);
    assert(window->view == DESKTOP_EXPLORER_VIEW_DETAILS);
    assert(window->snapshot_generation == generation);
    assert(window->entry_count == 50U && window->selected == 17U);
    assert(strcmp(window->entries[17].name, "ITEM017.TXT") == 0);

    desktop_explorer_layout_t layout =
        desktop_explorer_layout(window, client);
    assert(layout.header.x == client.x && layout.header.y == client.y);
    assert(layout.header.width == layout.viewport.width);
    assert(layout.header.height == DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT);
    assert(layout.viewport.y ==
           client.y + (int32_t)DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT);
    assert(layout.viewport.height ==
           client.height - DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT);
    assert(layout.scrollbar.y == layout.viewport.y);
    assert(layout.scrollbar.height == layout.viewport.height);
    assert(layout.columns == 1U && layout.visible_rows == 6U);
    assert(layout.total_rows == 50U && layout.maximum_first_row == 44U);
    assert(layout.first_row == 12U && window->first_row == 12U);

    desktop_rect_t first = desktop_explorer_entry_rect(window, client, 12U);
    assert(first.x == layout.viewport.x && first.y == layout.viewport.y);
    assert(first.width == layout.viewport.width);
    assert(first.height == DESKTOP_EXPLORER_DETAILS_ROW_HEIGHT);
    assert(desktop_explorer_entry_rect(window, client, 11U).width == 0U);
    assert(desktop_explorer_entry_at(
        window, client, layout.header.x + 2, layout.header.y + 2) ==
        DESKTOP_EXPLORER_NO_ENTRY);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_pointer_press(
        &explorer, 0U, client, layout.header.x + 2,
        layout.header.y + 2, &result) == DESKTOP_EXPLORER_OK);
    assert(result.consumed && window->selected == 17U);
    assert(desktop_explorer_entry_at(
        window, client, first.x + 2, first.y + 2) == 12U);

    desktop_rect_t tiny = {20, 40, 12U, 12U};
    desktop_explorer_layout_t tiny_layout =
        desktop_explorer_layout(window, tiny);
    assert(tiny_layout.header.height == tiny.height);
    assert(tiny_layout.header.width == 0U);
    assert(tiny_layout.viewport.width == 0U &&
           tiny_layout.viewport.height == 0U);
    assert(tiny_layout.scrollbar.x == tiny.x &&
           tiny_layout.scrollbar.y == tiny.y + (int32_t)tiny.height);
    assert(desktop_explorer_entry_rect(window, tiny, 17U).width == 0U);

    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_wheel(
        &explorer, 0U, client, -1, &result) == DESKTOP_EXPLORER_OK);
    assert(result.viewport_changed && window->first_row == 15U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_toggle_view(
        &explorer, 0U, client, &result) == DESKTOP_EXPLORER_OK);
    assert(window->view == DESKTOP_EXPLORER_VIEW_ICONS);
    assert(window->snapshot_generation == generation && window->selected == 17U);
    layout = desktop_explorer_layout(window, client);
    assert(layout.header.width == 0U && layout.header.height == 0U);
    assert(layout.columns == 3U && window->first_row == 5U);

    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_toggle_view(
        &explorer, 0U, client, &result) == DESKTOP_EXPLORER_OK);
    assert(desktop_explorer_navigate(
        &explorer, 0U, "/Docs") == DESKTOP_EXPLORER_OK);
    assert(window->view == DESKTOP_EXPLORER_VIEW_DETAILS);
    assert(desktop_explorer_refresh(
        &explorer, 0U) == DESKTOP_EXPLORER_OK);
    assert(window->view == DESKTOP_EXPLORER_VIEW_DETAILS);
    assert(desktop_explorer_back(
        &explorer, 0U) == DESKTOP_EXPLORER_OK);
    assert(window->view == DESKTOP_EXPLORER_VIEW_DETAILS);

    assert(desktop_explorer_open(&explorer, 0U, "/") ==
           DESKTOP_EXPLORER_OK);
    assert(window->view == DESKTOP_EXPLORER_VIEW_ICONS);
    window->view = DESKTOP_EXPLORER_VIEW_COUNT;
    layout = desktop_explorer_layout(window, client);
    assert(layout.viewport.width == 0U && layout.scrollbar.width == 0U);
    desktop_explorer_result_initialize(&result);
    assert(desktop_explorer_toggle_view(
        &explorer, 0U, client, &result) == DESKTOP_EXPLORER_EINVAL);
    assert(window->view == DESKTOP_EXPLORER_VIEW_COUNT);
    many_entry_count = 50U;
}

static void test_details_metadata_formatting_is_deterministic(void) {
    char size[DESKTOP_EXPLORER_SIZE_TEXT_CAPACITY];
    char modified[DESKTOP_EXPLORER_MODIFIED_TEXT_CAPACITY];
    char too_small[3] = "ok";
    assert(desktop_explorer_format_size(
        UINT32_MAX, size, sizeof(size)) == DESKTOP_EXPLORER_OK);
    assert(strcmp(size, "4294967295") == 0);
    assert(desktop_explorer_format_modified_utc(
        0U, modified, sizeof(modified)) == DESKTOP_EXPLORER_OK);
    assert(strcmp(modified, "---- -- -- --:--") == 0);
    assert(desktop_explorer_format_modified_utc(
        1U, modified, sizeof(modified)) == DESKTOP_EXPLORER_OK);
    assert(strcmp(modified, "1970-01-01 00:00") == 0);
    assert(desktop_explorer_format_modified_utc(
        951827696U, modified, sizeof(modified)) == DESKTOP_EXPLORER_OK);
    assert(strcmp(modified, "2000-02-29 12:34") == 0);
    assert(desktop_explorer_format_modified_utc(
        UINT32_MAX, modified, sizeof(modified)) == DESKTOP_EXPLORER_OK);
    assert(strcmp(modified, "2106-02-07 06:28") == 0);
    assert(desktop_explorer_format_size(
        1U, too_small, sizeof(too_small)) == DESKTOP_EXPLORER_EINVAL);
    assert(strcmp(too_small, "ok") == 0);

    x86os_file_info_t entry = {0};
    entry.type = X86OS_DIRECTORY;
    assert(strcmp(desktop_explorer_type_text(&entry, 0U), "Ordner") == 0);
    entry.type = X86OS_FILE;
    strcpy(entry.name, "SETUP.PRG");
    assert(strcmp(desktop_explorer_type_text(&entry, 0U), "Programm") == 0);
    strcpy(entry.name, "README.TXT");
    assert(strcmp(desktop_explorer_type_text(&entry, 0U),
                  "Textdokument") == 0);
    strcpy(entry.name, "DATA.BIN");
    assert(strcmp(desktop_explorer_type_text(&entry, 0U), "Datei") == 0);
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
                                  DESKTOP_DRAG_OPERATION_LINK |
                                  DESKTOP_DRAG_OPERATION_LAYOUT));
    assert(object.source_generation == generation);
    assert(desktop_explorer_drag_validate(
               &explorer, &object, &file) == DESKTOP_EXPLORER_OK);
    assert(strcmp(file.path, "/alpha.prg") == 0);
    assert(desktop_explorer_open(&explorer, 0U, "/") == 0);
    assert(explorer.windows[0].snapshot_generation != generation);
    assert(desktop_explorer_drag_validate(
               &explorer, &object, &file) == DESKTOP_EXPLORER_ESTALE);
}

static void test_desktop_directory_snapshot_and_drag_identity(void) {
    desktop_explorer_t explorer;
    desktop_drag_object_t object;
    desktop_explorer_drag_file_t file;
    char source_directory[DESKTOP_EXPLORER_PATH_CAPACITY];
    x86os_file_info_t directory_identity;
    desktop_explorer_initialize(&explorer);
    assert(desktop_explorer_desktop_open(
               &explorer, "/desktop") == DESKTOP_EXPLORER_OK);
    assert(explorer.desktop_directory.active);
    assert(explorer.desktop_directory.entry_count == 2U);
    assert(explorer.desktop_directory.directory_identity.type ==
           X86OS_DIRECTORY);
    assert(explorer.desktop_directory.directory_identity.create_time == 77U);
    assert(strcmp(explorer.desktop_directory.entries[0].name,
                  "LINK.LNK") == 0);
    assert(desktop_explorer_desktop_drag_object(
               &explorer, 0U, &object) == DESKTOP_EXPLORER_OK);
    assert(object.source_id == DESKTOP_EXPLORER_DESKTOP_SOURCE_ID);
    assert(desktop_explorer_drag_validate(
               &explorer, &object, &file) == DESKTOP_EXPLORER_OK);
    assert(strcmp(file.path, "/desktop/LINK.LNK") == 0);
    assert(desktop_explorer_drag_source_directory(
               &explorer, &object, source_directory,
               &directory_identity) == DESKTOP_EXPLORER_OK);
    assert(strcmp(source_directory, "/desktop") == 0);
    assert(directory_identity.create_time == 77U);
    uint32_t generation =
        explorer.desktop_directory.snapshot_generation;
    assert(desktop_explorer_desktop_refresh(&explorer) ==
           DESKTOP_EXPLORER_OK);
    assert(explorer.desktop_directory.snapshot_generation != generation);
    assert(desktop_explorer_drag_validate(
               &explorer, &object, &file) == DESKTOP_EXPLORER_ESTALE);

    generation = explorer.desktop_directory.snapshot_generation;
    desktop_path_type = X86OS_SYMLINK;
    assert(desktop_explorer_desktop_refresh(&explorer) ==
           DESKTOP_EXPLORER_ENOTDIR);
    assert(explorer.desktop_directory.snapshot_generation == generation);
    assert(explorer.desktop_directory.active);
    desktop_path_type = X86OS_DIRECTORY;
}

int main(void) {
    test_directory_snapshot_is_sorted_and_atomic();
    test_deadline_failure_keeps_published_snapshot();
    test_extension_icons_are_case_insensitive();
    test_child_path_and_window_capacity();
    test_same_window_navigation_history_and_atomic_failure();
    test_navigation_history_rolls_over_at_fixed_capacity();
    test_same_item_double_click_activates_once();
    test_keyboard_grid_navigation_and_activation();
    test_scrollbar_geometry_input_and_resize_are_bounded();
    test_scrolled_hit_test_keyboard_reveal_and_capacity();
    test_details_view_toggle_layout_and_state_are_bounded();
    test_details_metadata_formatting_is_deterministic();
    test_drag_object_is_bound_to_snapshot_generation();
    test_desktop_directory_snapshot_and_drag_identity();
    return 0;
}
