#include "desktop_explorer.h"
#include "desktop_trash.h"
#include "reist/vfs_read_client.h"
#include "reist/vfs_stat_client.h"

_Static_assert(sizeof(desktop_explorer_drag_file_t) <=
                   DESKTOP_DRAG_DATA_CAPACITY,
               "Explorer drag file exceeds generic drag data capacity");

static void clear_bytes(void *value, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static uint32_t text_length(const char *text, uint32_t capacity,
                            uint32_t *length_out) {
    if (text == 0 || length_out == 0) return 0U;
    for (uint32_t length = 0U; length < capacity; ++length) {
        if (text[length] == '\0') {
            *length_out = length;
            return 1U;
        }
    }
    return 0U;
}

static void copy_name(char *destination, const char *source) {
    uint32_t index = 0U;
    while (index + 1U < sizeof(((x86os_file_info_t *)0)->name) &&
           source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index++] = '\0';
    while (index < sizeof(((x86os_file_info_t *)0)->name))
        destination[index++] = '\0';
}

static void copy_entry(x86os_file_info_t *destination,
                       const x86os_file_info_t *source) {
    copy_name(destination->name, source->name);
    destination->type = source->type;
    destination->size = source->size;
    destination->create_time = source->create_time;
    destination->modify_time = source->modify_time;
    destination->access_time = source->access_time;
}

static uint32_t entries_equal(const x86os_file_info_t *left,
                              const x86os_file_info_t *right) {
    if (left == 0 || right == 0 || left->type != right->type ||
        left->size != right->size ||
        left->create_time != right->create_time ||
        left->modify_time != right->modify_time ||
        left->access_time != right->access_time) return 0U;
    for (uint32_t index = 0U; index < sizeof(left->name); ++index) {
        if (left->name[index] != right->name[index]) return 0U;
        if (left->name[index] == '\0') return 1U;
    }
    return 0U;
}

static uint32_t path_equal(const char *left, const char *right) {
    for (uint32_t index = 0U;
         index < DESKTOP_EXPLORER_PATH_CAPACITY; ++index) {
        if (left[index] != right[index]) return 0U;
        if (left[index] == '\0') return 1U;
    }
    return 0U;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t size) {
    for (uint32_t index = 0U; index < size; ++index)
        destination[index] = source[index];
}

static uint8_t fold_ascii(uint8_t value) {
    return value >= 'A' && value <= 'Z' ? (uint8_t)(value + ('a' - 'A'))
                                        : value;
}

static uint32_t entry_is_dot_name(const x86os_file_info_t *entry) {
    return entry != 0 && entry->name[0] == '.' &&
        (entry->name[1] == '\0' ||
         (entry->name[1] == '.' && entry->name[2] == '\0'));
}

static uint32_t explorer_hex_digit(uint8_t value) {
    value = fold_ascii(value);
    return (value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f');
}

static uint32_t entry_is_trash_storage_name(
    const x86os_file_info_t *entry) {
    if (entry == 0 || fold_ascii((uint8_t)entry->name[0]) != 'r' ||
        fold_ascii((uint8_t)entry->name[1]) != 't') return 0U;
    for (uint32_t index = 2U; index < 8U; ++index)
        if (!explorer_hex_digit((uint8_t)entry->name[index])) return 0U;
    return entry->name[8] == '.' &&
        fold_ascii((uint8_t)entry->name[9]) == 't' &&
        fold_ascii((uint8_t)entry->name[10]) == 'r' &&
        fold_ascii((uint8_t)entry->name[11]) == 's' &&
        entry->name[DESKTOP_TRASH_STORAGE_NAME_LENGTH] == '\0';
}

static uint32_t entry_is_hidden_name(const x86os_file_info_t *entry) {
    return entry_is_dot_name(entry) || entry_is_trash_storage_name(entry);
}

static uint32_t extension_equal(const char *extension, const char *expected) {
    if (extension == 0 || expected == 0) return 0U;
    for (uint32_t index = 0U;
         index < sizeof(((x86os_file_info_t *)0)->name); ++index) {
        uint8_t left = fold_ascii((uint8_t)extension[index]);
        uint8_t right = (uint8_t)expected[index];
        if (left != right) return 0U;
        if (left == 0U) return 1U;
    }
    return 0U;
}

uint32_t desktop_explorer_icon_kind(const x86os_file_info_t *entry,
                                    uint32_t directory_nonempty) {
    if (entry == 0) return DESKTOP_EXPLORER_ICON_UNKNOWN;
    if (entry->type == X86OS_DIRECTORY)
        return directory_nonempty != 0U
            ? DESKTOP_EXPLORER_ICON_FOLDER_FULL
            : DESKTOP_EXPLORER_ICON_FOLDER_EMPTY;
    if (entry->type != X86OS_FILE) return DESKTOP_EXPLORER_ICON_UNKNOWN;
    uint32_t name_length = 0U;
    if (!text_length(entry->name, sizeof(entry->name), &name_length))
        return DESKTOP_EXPLORER_ICON_UNKNOWN;
    const char *extension = 0;
    for (uint32_t index = 0U; index + 1U < name_length; ++index) {
        if (entry->name[index] == '.')
            extension = &entry->name[index + 1U];
    }
    if (extension == 0) return DESKTOP_EXPLORER_ICON_UNKNOWN;
    if (extension_equal(extension, "prg"))
        return DESKTOP_EXPLORER_ICON_PROGRAM;
    if (extension_equal(extension, "txt") ||
        extension_equal(extension, "md") ||
        extension_equal(extension, "log") ||
        extension_equal(extension, "c") ||
        extension_equal(extension, "h"))
        return DESKTOP_EXPLORER_ICON_TEXT;
    if (extension_equal(extension, "conf") ||
        extension_equal(extension, "cfg") ||
        extension_equal(extension, "ini"))
        return DESKTOP_EXPLORER_ICON_SETTINGS;
    if (extension_equal(extension, "wav"))
        return DESKTOP_EXPLORER_ICON_AUDIO;
    if (extension_equal(extension, "bmp") ||
        extension_equal(extension, "gif") ||
        extension_equal(extension, "ico"))
        return DESKTOP_EXPLORER_ICON_IMAGE;
    return DESKTOP_EXPLORER_ICON_UNKNOWN;
}

static int compare_entries(const x86os_file_info_t *left,
                           const x86os_file_info_t *right) {
    if (left->type == X86OS_DIRECTORY && right->type != X86OS_DIRECTORY)
        return -1;
    if (left->type != X86OS_DIRECTORY && right->type == X86OS_DIRECTORY)
        return 1;
    for (uint32_t index = 0U; index < sizeof(left->name); ++index) {
        uint8_t a = fold_ascii((uint8_t)left->name[index]);
        uint8_t b = fold_ascii((uint8_t)right->name[index]);
        if (a < b) return -1;
        if (a > b) return 1;
        if (a == 0U) return 0;
    }
    return 0;
}

static void sort_staging(desktop_explorer_t *explorer) {
    /* Stable bounded insertion sort keeps directories first and avoids a
     * freestanding qsort dependency for at most 128 entries. */
    for (uint32_t index = 1U; index < explorer->staging_count; ++index) {
        uint32_t position = index;
        while (position != 0U && compare_entries(
                &explorer->staging[position],
                &explorer->staging[position - 1U]) < 0) {
            x86os_file_info_t temporary;
            copy_entry(&temporary, &explorer->staging[position - 1U]);
            uint8_t temporary_nonempty =
                explorer->staging_directory_nonempty[position - 1U];
            copy_entry(&explorer->staging[position - 1U],
                       &explorer->staging[position]);
            explorer->staging_directory_nonempty[position - 1U] =
                explorer->staging_directory_nonempty[position];
            copy_entry(&explorer->staging[position], &temporary);
            explorer->staging_directory_nonempty[position] =
                temporary_nonempty;
            --position;
        }
    }
}

static uint32_t entry_name_valid(const x86os_file_info_t *entry) {
    uint32_t length = 0U;
    return entry != 0 &&
           text_length(entry->name, sizeof(entry->name), &length) &&
           length != 0U && entry->type >= X86OS_FILE &&
           entry->type <= X86OS_DIRECTORY;
}

static int build_child_path(const char *parent, const char *name,
                            char *path, uint32_t capacity) {
    uint32_t parent_length = 0U;
    uint32_t name_length = 0U;
    if (path == 0 || capacity == 0U ||
        !text_length(parent, DESKTOP_EXPLORER_PATH_CAPACITY, &parent_length) ||
        !text_length(name, sizeof(((x86os_file_info_t *)0)->name),
                     &name_length)) return DESKTOP_EXPLORER_EINVAL;
    uint32_t separator = parent_length != 0U &&
        parent[parent_length - 1U] != '/' ? 1U : 0U;
    uint64_t required = (uint64_t)parent_length + separator + name_length + 1U;
    if (required > capacity) return DESKTOP_EXPLORER_ECAPACITY;
    uint32_t offset = 0U;
    for (; offset < parent_length; ++offset) path[offset] = parent[offset];
    if (separator) path[offset++] = '/';
    for (uint32_t index = 0U; index < name_length; ++index)
        path[offset++] = name[index];
    path[offset] = '\0';
    return DESKTOP_EXPLORER_OK;
}

static int snapshot_remaining_timeout(uint64_t deadline,
                                      uint32_t *timeout) {
    uint64_t now = 0U;
    if (timeout == 0 || x86os_monotonic_ms(&now) != 0)
        return DESKTOP_EXPLORER_EIO;
    if (now >= deadline) return DESKTOP_EXPLORER_ETIMEDOUT;
    uint64_t remaining = deadline - now;
    *timeout = remaining < DESKTOP_EXPLORER_REQUEST_TIMEOUT_MS
        ? (uint32_t)remaining : DESKTOP_EXPLORER_REQUEST_TIMEOUT_MS;
    return *timeout != 0U ? DESKTOP_EXPLORER_OK
                          : DESKTOP_EXPLORER_ETIMEDOUT;
}

static uint8_t probe_directory_nonempty(const char *parent,
                                        const x86os_file_info_t *entry,
                                        uint64_t deadline) {
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    if (entry == 0 || entry->type != X86OS_DIRECTORY ||
        build_child_path(parent, entry->name, path, sizeof(path)) !=
            DESKTOP_EXPLORER_OK) return 1U;
    for (uint32_t index = 0U;
         index < DESKTOP_EXPLORER_DIRECTORY_PROBE_ENTRIES; ++index) {
        uint32_t timeout = 0U;
        if (snapshot_remaining_timeout(deadline, &timeout) !=
            DESKTOP_EXPLORER_OK) return 1U;
        x86os_file_info_t child;
        int present = reist_vfs_readdir_at(path, index, &child, timeout);
        if (present < 0) return 1U;
        if (present == 0) return 0U;
        if (!entry_name_valid(&child)) return 1U;
        if (!entry_is_hidden_name(&child)) return 1U;
    }
    return 1U;
}

void desktop_explorer_initialize(desktop_explorer_t *explorer) {
    if (explorer == 0) return;
    clear_bytes(explorer, sizeof(*explorer));
    for (uint32_t index = 0U;
         index < DESKTOP_EXPLORER_WINDOW_CAPACITY; ++index) {
        explorer->windows[index].selected = DESKTOP_EXPLORER_NO_ENTRY;
        explorer->windows[index].pressed = DESKTOP_EXPLORER_NO_ENTRY;
        explorer->windows[index].last_click = DESKTOP_EXPLORER_NO_ENTRY;
    }
}

void desktop_explorer_result_initialize(desktop_explorer_result_t *result) {
    if (result == 0) return;
    clear_bytes(result, sizeof(*result));
    result->window_index = DESKTOP_WM_NO_TARGET;
    result->entry_index = DESKTOP_EXPLORER_NO_ENTRY;
}

void desktop_explorer_desktop_press(desktop_explorer_t *explorer,
                                    uint32_t hit,
                                    desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0) return;
    explorer->desktop_pressed = hit != 0U;
    if (explorer->desktop_selected != (hit != 0U)) {
        explorer->desktop_selected = hit != 0U;
        result->selection_changed = 1U;
    }
    result->consumed = hit != 0U;
}

void desktop_explorer_desktop_release(desktop_explorer_t *explorer,
                                      uint32_t hit, uint64_t now_ms,
                                      desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0) return;
    uint32_t clicked = hit != 0U && explorer->desktop_pressed;
    explorer->desktop_pressed = 0U;
    result->consumed = clicked;
    if (!clicked) {
        explorer->desktop_last_click_ms = 0U;
        return;
    }
    if (explorer->desktop_last_click_ms != 0U &&
        now_ms >= explorer->desktop_last_click_ms &&
        now_ms - explorer->desktop_last_click_ms <=
            DESKTOP_EXPLORER_DOUBLE_CLICK_MS) {
        result->activated = 1U;
        explorer->desktop_last_click_ms = 0U;
    } else explorer->desktop_last_click_ms = now_ms;
}

static int stage_directory(desktop_explorer_t *explorer, const char *path) {
    uint32_t path_length = 0U;
    if (explorer == 0 || !text_length(
            path, DESKTOP_EXPLORER_PATH_CAPACITY, &path_length) ||
        path_length == 0U) return DESKTOP_EXPLORER_EINVAL;
    uint64_t started = 0U;
    if (x86os_monotonic_ms(&started) != 0) return DESKTOP_EXPLORER_EIO;
    uint64_t deadline = UINT64_MAX - started <
        DESKTOP_EXPLORER_SNAPSHOT_TIMEOUT_MS
        ? UINT64_MAX : started + DESKTOP_EXPLORER_SNAPSHOT_TIMEOUT_MS;
    uint32_t timeout = 0U;
    int remaining_status = snapshot_remaining_timeout(deadline, &timeout);
    if (remaining_status != DESKTOP_EXPLORER_OK) return remaining_status;
    x86os_file_info_t target;
    int stat_status = reist_vfs_stat(path, &target, timeout);
    if (stat_status == -110) return DESKTOP_EXPLORER_ETIMEDOUT;
    if (stat_status == -2) return DESKTOP_EXPLORER_ENOENT;
    if (stat_status != 0) return DESKTOP_EXPLORER_EIO;
    if (target.type != X86OS_DIRECTORY) return DESKTOP_EXPLORER_ENOTDIR;

    explorer->staging_count = 0U;
    explorer->staging_truncated = 0U;
    uint32_t index = 0U;
    uint32_t scanned = 0U;
    for (;;) {
        if (scanned >= DESKTOP_EXPLORER_SCAN_CAPACITY) {
            explorer->staging_truncated = 1U;
            break;
        }
        remaining_status = snapshot_remaining_timeout(deadline, &timeout);
        if (remaining_status != DESKTOP_EXPLORER_OK) return remaining_status;
        x86os_file_info_t entry;
        int present = reist_vfs_readdir_at(path, index, &entry, timeout);
        if (present == -110) return DESKTOP_EXPLORER_ETIMEDOUT;
        if (present < 0) return DESKTOP_EXPLORER_EIO;
        if (present == 0) break;
        ++scanned;
        ++index;
        if (!entry_name_valid(&entry)) return DESKTOP_EXPLORER_EIO;
        if (entry_is_hidden_name(&entry)) continue;
        if (explorer->staging_count >= DESKTOP_EXPLORER_ENTRY_CAPACITY) {
            explorer->staging_truncated = 1U;
            break;
        }
        uint32_t staging_index = explorer->staging_count;
        copy_entry(&explorer->staging[staging_index], &entry);
        explorer->staging_directory_nonempty[staging_index] =
            entry.type == X86OS_DIRECTORY
                ? probe_directory_nonempty(path, &entry, deadline) : 0U;
        ++explorer->staging_count;
    }
    for (uint32_t index = 0U; index <= path_length; ++index)
        explorer->staging_path[index] = path[index];
    sort_staging(explorer);
    return DESKTOP_EXPLORER_OK;
}

static void publish_staging(desktop_explorer_t *explorer,
                            desktop_explorer_window_t *window,
                            uint32_t reset_history) {
    if (reset_history) {
        clear_bytes(window, sizeof(*window));
        window->view = DESKTOP_EXPLORER_VIEW_ICONS;
    }
    window->active = 1U;
    if (++explorer->next_snapshot_generation == 0U)
        explorer->next_snapshot_generation = 1U;
    window->snapshot_generation = explorer->next_snapshot_generation;
    window->entry_count = explorer->staging_count;
    window->truncated = explorer->staging_truncated;
    window->selected = DESKTOP_EXPLORER_NO_ENTRY;
    window->pressed = DESKTOP_EXPLORER_NO_ENTRY;
    window->last_click = DESKTOP_EXPLORER_NO_ENTRY;
    window->last_click_ms = 0U;
    window->first_row = 0U;
    window->scroll_capture = DESKTOP_EXPLORER_SCROLL_NONE;
    window->scroll_drag_offset = 0U;
    uint32_t path_length = 0U;
    (void)text_length(explorer->staging_path,
                      DESKTOP_EXPLORER_PATH_CAPACITY, &path_length);
    for (uint32_t index = 0U; index <= path_length; ++index)
        window->path[index] = explorer->staging_path[index];
    for (uint32_t index = 0U; index < window->entry_count; ++index) {
        copy_entry(&window->entries[index], &explorer->staging[index]);
        window->directory_nonempty[index] =
            explorer->staging_directory_nonempty[index];
    }
}

static void copy_path(char *destination, const char *source) {
    uint32_t length = 0U;
    if (!text_length(source, DESKTOP_EXPLORER_PATH_CAPACITY, &length)) {
        destination[0] = '\0';
        return;
    }
    for (uint32_t index = 0U; index <= length; ++index)
        destination[index] = source[index];
}

static void push_history(
    char history[DESKTOP_EXPLORER_HISTORY_CAPACITY]
                [DESKTOP_EXPLORER_PATH_CAPACITY],
    uint32_t *count, const char *path) {
    if (*count == DESKTOP_EXPLORER_HISTORY_CAPACITY) {
        for (uint32_t index = 1U;
             index < DESKTOP_EXPLORER_HISTORY_CAPACITY; ++index)
            copy_path(history[index - 1U], history[index]);
        *count = DESKTOP_EXPLORER_HISTORY_CAPACITY - 1U;
    }
    copy_path(history[*count], path);
    ++*count;
}

int desktop_explorer_open(desktop_explorer_t *explorer,
                          uint32_t window_index, const char *path) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY)
        return DESKTOP_EXPLORER_EINVAL;
    int result = stage_directory(explorer, path);
    if (result != DESKTOP_EXPLORER_OK) return result;
    publish_staging(explorer, &explorer->windows[window_index], 1U);
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_navigate(desktop_explorer_t *explorer,
                              uint32_t window_index, const char *path) {
    if (explorer == 0 || path == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    int result = stage_directory(explorer, path);
    if (result != DESKTOP_EXPLORER_OK) return result;
    push_history(window->back_paths, &window->back_count, window->path);
    window->forward_count = 0U;
    publish_staging(explorer, window, 0U);
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_back(desktop_explorer_t *explorer,
                          uint32_t window_index) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    if (window->back_count == 0U) return DESKTOP_EXPLORER_ENOENT;
    const char *target = window->back_paths[window->back_count - 1U];
    int result = stage_directory(explorer, target);
    if (result != DESKTOP_EXPLORER_OK) return result;
    push_history(window->forward_paths, &window->forward_count, window->path);
    --window->back_count;
    publish_staging(explorer, window, 0U);
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_forward(desktop_explorer_t *explorer,
                             uint32_t window_index) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    if (window->forward_count == 0U) return DESKTOP_EXPLORER_ENOENT;
    const char *target = window->forward_paths[window->forward_count - 1U];
    int result = stage_directory(explorer, target);
    if (result != DESKTOP_EXPLORER_OK) return result;
    push_history(window->back_paths, &window->back_count, window->path);
    --window->forward_count;
    publish_staging(explorer, window, 0U);
    return DESKTOP_EXPLORER_OK;
}

static int parent_path(const char *path,
                       char parent[DESKTOP_EXPLORER_PATH_CAPACITY]) {
    uint32_t length = 0U;
    if (!text_length(path, DESKTOP_EXPLORER_PATH_CAPACITY, &length) ||
        length == 0U || path[0] != '/') return DESKTOP_EXPLORER_EINVAL;
    while (length > 1U && path[length - 1U] == '/') --length;
    if (length == 1U) return DESKTOP_EXPLORER_ENOENT;
    uint32_t slash = length;
    while (slash > 1U && path[slash - 1U] != '/') --slash;
    uint32_t parent_length = slash == 1U ? 1U : slash - 1U;
    for (uint32_t index = 0U; index < parent_length; ++index)
        parent[index] = path[index];
    parent[parent_length] = '\0';
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_up(desktop_explorer_t *explorer,
                        uint32_t window_index) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    char parent[DESKTOP_EXPLORER_PATH_CAPACITY];
    int result = parent_path(explorer->windows[window_index].path, parent);
    if (result != DESKTOP_EXPLORER_OK) return result;
    return desktop_explorer_navigate(explorer, window_index, parent);
}

int desktop_explorer_refresh(desktop_explorer_t *explorer,
                             uint32_t window_index) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    int result = stage_directory(explorer, window->path);
    if (result != DESKTOP_EXPLORER_OK) return result;
    publish_staging(explorer, window, 0U);
    return DESKTOP_EXPLORER_OK;
}

uint32_t desktop_explorer_can_back(const desktop_explorer_window_t *window) {
    return window != 0 && window->active && window->back_count != 0U;
}

uint32_t desktop_explorer_can_forward(
    const desktop_explorer_window_t *window) {
    return window != 0 && window->active && window->forward_count != 0U;
}

uint32_t desktop_explorer_can_up(const desktop_explorer_window_t *window) {
    char parent[DESKTOP_EXPLORER_PATH_CAPACITY];
    return window != 0 && window->active &&
        parent_path(window->path, parent) == DESKTOP_EXPLORER_OK;
}

void desktop_explorer_close(desktop_explorer_t *explorer,
                            uint32_t window_index) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY)
        return;
    clear_bytes(&explorer->windows[window_index],
                sizeof(explorer->windows[window_index]));
    explorer->windows[window_index].selected = DESKTOP_EXPLORER_NO_ENTRY;
    explorer->windows[window_index].pressed = DESKTOP_EXPLORER_NO_ENTRY;
    explorer->windows[window_index].last_click = DESKTOP_EXPLORER_NO_ENTRY;
}

uint32_t desktop_explorer_free_window(const desktop_explorer_t *explorer) {
    if (explorer == 0) return DESKTOP_WM_NO_TARGET;
    for (uint32_t index = 0U;
         index < DESKTOP_EXPLORER_WINDOW_CAPACITY; ++index)
        if (!explorer->windows[index].active) return index;
    return DESKTOP_WM_NO_TARGET;
}

int desktop_explorer_child_path(const desktop_explorer_window_t *window,
                                uint32_t entry_index,
                                char *path, uint32_t capacity) {
    if (window == 0 || !window->active || path == 0 || capacity == 0U ||
        entry_index >= window->entry_count)
        return DESKTOP_EXPLORER_EINVAL;
    return build_child_path(window->path, window->entries[entry_index].name,
                            path, capacity);
}

static uint32_t divide_round_up(uint32_t value, uint32_t divisor) {
    return divisor != 0U ? value / divisor + (value % divisor != 0U) : 0U;
}

const char *desktop_explorer_type_text(const x86os_file_info_t *entry,
                                       uint32_t directory_nonempty) {
    uint32_t kind = desktop_explorer_icon_kind(entry, directory_nonempty);
    if (kind == DESKTOP_EXPLORER_ICON_FOLDER_EMPTY ||
        kind == DESKTOP_EXPLORER_ICON_FOLDER_FULL) return "Ordner";
    if (kind == DESKTOP_EXPLORER_ICON_PROGRAM) return "Programm";
    if (kind == DESKTOP_EXPLORER_ICON_TEXT) return "Textdokument";
    if (kind == DESKTOP_EXPLORER_ICON_AUDIO) return "Audio";
    if (kind == DESKTOP_EXPLORER_ICON_IMAGE) return "Bild";
    if (kind == DESKTOP_EXPLORER_ICON_SETTINGS) return "Konfiguration";
    return "Datei";
}

int desktop_explorer_format_size(uint32_t bytes, char *output,
                                 uint32_t capacity) {
    if (output == 0 || capacity < DESKTOP_EXPLORER_SIZE_TEXT_CAPACITY)
        return DESKTOP_EXPLORER_EINVAL;
    char reverse[10];
    uint32_t count = 0U;
    do {
        reverse[count++] = (char)('0' + bytes % 10U);
        bytes /= 10U;
    } while (bytes != 0U && count < sizeof(reverse));
    for (uint32_t index = 0U; index < count; ++index)
        output[index] = reverse[count - index - 1U];
    output[count] = '\0';
    return DESKTOP_EXPLORER_OK;
}

static uint32_t explorer_year_is_leap(uint32_t year) {
    return year % 4U == 0U &&
        (year % 100U != 0U || year % 400U == 0U);
}

static uint32_t explorer_month_days(uint32_t year, uint32_t month) {
    static const uint8_t days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month == 0U || month > 12U) return 0U;
    if (month == 2U && explorer_year_is_leap(year)) return 29U;
    return days[month - 1U];
}

static void explorer_write_two_digits(char *output, uint32_t value) {
    output[0] = (char)('0' + (value / 10U) % 10U);
    output[1] = (char)('0' + value % 10U);
}

int desktop_explorer_format_modified_utc(uint32_t unix_seconds,
                                         char *output, uint32_t capacity) {
    static const char missing[] = "---- -- -- --:--";
    if (output == 0 || capacity < DESKTOP_EXPLORER_MODIFIED_TEXT_CAPACITY)
        return DESKTOP_EXPLORER_EINVAL;
    if (unix_seconds == 0U) {
        for (uint32_t index = 0U; index < sizeof(missing); ++index)
            output[index] = missing[index];
        return DESKTOP_EXPLORER_OK;
    }

    uint32_t days = unix_seconds / 86400U;
    uint32_t seconds_of_day = unix_seconds % 86400U;
    uint32_t year = 1970U;
    while (year <= 2106U) {
        uint32_t year_days = explorer_year_is_leap(year) ? 366U : 365U;
        if (days < year_days) break;
        days -= year_days;
        ++year;
    }
    uint32_t month = 1U;
    while (month <= 12U) {
        uint32_t month_days = explorer_month_days(year, month);
        if (days < month_days) break;
        days -= month_days;
        ++month;
    }
    if (year > 2106U || month > 12U) {
        for (uint32_t index = 0U; index < sizeof(missing); ++index)
            output[index] = missing[index];
        return DESKTOP_EXPLORER_OK;
    }
    output[0] = (char)('0' + (year / 1000U) % 10U);
    output[1] = (char)('0' + (year / 100U) % 10U);
    output[2] = (char)('0' + (year / 10U) % 10U);
    output[3] = (char)('0' + year % 10U);
    output[4] = '-';
    explorer_write_two_digits(&output[5], month);
    output[7] = '-';
    explorer_write_two_digits(&output[8], days + 1U);
    output[10] = ' ';
    explorer_write_two_digits(&output[11], seconds_of_day / 3600U);
    output[13] = ':';
    explorer_write_two_digits(&output[14],
                              (seconds_of_day % 3600U) / 60U);
    output[16] = '\0';
    return DESKTOP_EXPLORER_OK;
}

/* Freestanding i386 does not provide the compiler's 64-bit division helper.
 * Keep scrollbar interpolation exact and bounded with fixed-step long
 * division instead of allowing the compiler to emit __udivdi3. */
static uint32_t multiply_divide_u32(uint32_t left, uint32_t right,
                                    uint32_t divisor) {
    uint64_t product = (uint64_t)left * right;
    uint64_t quotient = 0U;
    uint64_t remainder = 0U;
    if (divisor == 0U) return UINT32_MAX;
    for (uint32_t remaining = 64U; remaining != 0U; --remaining) {
        uint32_t bit = remaining - 1U;
        remainder = (remainder << 1U) | ((product >> bit) & 1U);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (uint64_t)1U << bit;
        }
    }
    return quotient > UINT32_MAX ? UINT32_MAX : (uint32_t)quotient;
}

static uint32_t point_in_rect(desktop_rect_t rect, int32_t x, int32_t y) {
    return rect.width != 0U && rect.height != 0U && x >= rect.x && y >= rect.y &&
           (uint64_t)(uint32_t)(x - rect.x) < rect.width &&
           (uint64_t)(uint32_t)(y - rect.y) < rect.height;
}

desktop_explorer_layout_t desktop_explorer_layout(
    const desktop_explorer_window_t *window, desktop_rect_t client) {
    desktop_explorer_layout_t layout;
    clear_bytes(&layout, sizeof(layout));
    if (window == 0 || !window->active ||
        window->view >= DESKTOP_EXPLORER_VIEW_COUNT ||
        client.width == 0U || client.height == 0U ||
        client.x < 0 || client.y < 0) return layout;

    uint32_t scrollbar_width = client.width < DESKTOP_EXPLORER_SCROLLBAR_EXTENT
        ? client.width : DESKTOP_EXPLORER_SCROLLBAR_EXTENT;
    uint32_t viewport_width = client.width - scrollbar_width;
    uint32_t header_height = window->view == DESKTOP_EXPLORER_VIEW_DETAILS
        ? (client.height < DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT
            ? client.height : DESKTOP_EXPLORER_DETAILS_HEADER_HEIGHT)
        : 0U;
    uint32_t viewport_height = client.height - header_height;
    if (header_height != 0U)
        layout.header = (desktop_rect_t){
            client.x, client.y, viewport_width, header_height};
    layout.viewport = (desktop_rect_t){
        client.x, client.y + (int32_t)header_height,
        viewport_width, viewport_height};
    layout.scrollbar = (desktop_rect_t){
        client.x + (int32_t)(client.width - scrollbar_width),
        client.y + (int32_t)header_height,
        scrollbar_width, viewport_height};
    layout.columns = window->view == DESKTOP_EXPLORER_VIEW_DETAILS
        ? 1U : viewport_width / DESKTOP_EXPLORER_ICON_WIDTH;
    if (layout.columns == 0U) layout.columns = 1U;
    uint32_t row_height = window->view == DESKTOP_EXPLORER_VIEW_DETAILS
        ? DESKTOP_EXPLORER_DETAILS_ROW_HEIGHT
        : DESKTOP_EXPLORER_ICON_HEIGHT;
    layout.visible_rows = viewport_height / row_height;
    if (layout.visible_rows == 0U) layout.visible_rows = 1U;
    layout.total_rows = divide_round_up(window->entry_count, layout.columns);
    layout.maximum_first_row = layout.total_rows > layout.visible_rows
        ? layout.total_rows - layout.visible_rows : 0U;
    layout.first_row = window->first_row < layout.maximum_first_row
        ? window->first_row : layout.maximum_first_row;
    layout.enabled = layout.maximum_first_row != 0U;

    uint32_t button = scrollbar_width;
    if (button * 2U > viewport_height) button = viewport_height / 2U;
    uint32_t track_height = viewport_height > button * 2U
        ? viewport_height - button * 2U : 0U;
    layout.decrement = (desktop_rect_t){
        layout.scrollbar.x, layout.scrollbar.y, scrollbar_width, button};
    layout.increment = (desktop_rect_t){
        layout.scrollbar.x,
        layout.scrollbar.y + (int32_t)viewport_height - (int32_t)button,
        scrollbar_width, button};
    layout.track = (desktop_rect_t){
        layout.scrollbar.x, layout.scrollbar.y + (int32_t)button,
        scrollbar_width, track_height};
    if (track_height == 0U) return layout;

    uint32_t thumb_height = track_height;
    if (layout.enabled && layout.total_rows != 0U) {
        thumb_height = multiply_divide_u32(
            track_height, layout.visible_rows, layout.total_rows);
        if (thumb_height < DESKTOP_EXPLORER_SCROLLBAR_MIN_THUMB)
            thumb_height = DESKTOP_EXPLORER_SCROLLBAR_MIN_THUMB;
        if (thumb_height > track_height) thumb_height = track_height;
    }
    uint32_t travel = track_height - thumb_height;
    uint32_t offset = layout.enabled && travel != 0U
        ? multiply_divide_u32(
              travel, layout.first_row, layout.maximum_first_row) : 0U;
    layout.thumb = (desktop_rect_t){
        layout.track.x, layout.track.y + (int32_t)offset,
        layout.track.width, thumb_height};
    return layout;
}

static void set_first_row(desktop_explorer_window_t *window,
                          uint32_t first_row, uint32_t maximum,
                          desktop_explorer_result_t *result) {
    if (first_row > maximum) first_row = maximum;
    if (window->first_row != first_row) {
        window->first_row = first_row;
        result->viewport_changed = 1U;
    }
}

static void scroll_rows(desktop_explorer_window_t *window,
                        const desktop_explorer_layout_t *layout,
                        uint32_t amount, uint32_t decrement,
                        desktop_explorer_result_t *result) {
    uint32_t current = layout->first_row;
    uint32_t next;
    if (decrement)
        next = amount > current ? 0U : current - amount;
    else
        next = amount > layout->maximum_first_row - current
            ? layout->maximum_first_row : current + amount;
    set_first_row(window, next, layout->maximum_first_row, result);
}

static void reveal_selection(
    desktop_explorer_window_t *window,
    const desktop_explorer_layout_t *layout,
    desktop_explorer_result_t *result) {
    if (window->selected >= window->entry_count || layout->columns == 0U ||
        layout->visible_rows == 0U) return;
    uint32_t selected_row = window->selected / layout->columns;
    if (selected_row < layout->first_row)
        set_first_row(window, selected_row,
                      layout->maximum_first_row, result);
    else if (selected_row >= layout->first_row + layout->visible_rows)
        set_first_row(window, selected_row - layout->visible_rows + 1U,
                      layout->maximum_first_row, result);
}

desktop_rect_t desktop_explorer_entry_rect(
    const desktop_explorer_window_t *window, desktop_rect_t client,
    uint32_t entry_index) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (window == 0 || !window->active || entry_index >= window->entry_count)
        return empty;
    desktop_explorer_layout_t layout = desktop_explorer_layout(window, client);
    if (layout.viewport.width == 0U || layout.viewport.height == 0U)
        return empty;
    uint32_t column = entry_index % layout.columns;
    uint32_t row = entry_index / layout.columns;
    if (row < layout.first_row) return empty;
    uint32_t visible_row = row - layout.first_row;
    uint32_t row_height = window->view == DESKTOP_EXPLORER_VIEW_DETAILS
        ? DESKTOP_EXPLORER_DETAILS_ROW_HEIGHT
        : DESKTOP_EXPLORER_ICON_HEIGHT;
    uint64_t y = (uint64_t)(uint32_t)layout.viewport.y +
                 (uint64_t)visible_row * row_height;
    if (y >= (uint64_t)(uint32_t)layout.viewport.y + layout.viewport.height)
        return empty;
    desktop_rect_t result = {
        window->view == DESKTOP_EXPLORER_VIEW_DETAILS
            ? layout.viewport.x
            : layout.viewport.x +
                (int32_t)(column * DESKTOP_EXPLORER_ICON_WIDTH),
        (int32_t)y,
        window->view == DESKTOP_EXPLORER_VIEW_DETAILS
            ? layout.viewport.width : DESKTOP_EXPLORER_ICON_WIDTH,
        row_height,
    };
    if ((uint64_t)(uint32_t)result.x + result.width >
        (uint64_t)(uint32_t)layout.viewport.x + layout.viewport.width)
        result.width = (uint32_t)((uint64_t)(uint32_t)layout.viewport.x +
                                 layout.viewport.width - (uint32_t)result.x);
    if ((uint64_t)(uint32_t)result.y + result.height >
        (uint64_t)(uint32_t)layout.viewport.y + layout.viewport.height)
        result.height = (uint32_t)((uint64_t)(uint32_t)layout.viewport.y +
                                  layout.viewport.height - (uint32_t)result.y);
    return result;
}

uint32_t desktop_explorer_entry_at(
    const desktop_explorer_window_t *window, desktop_rect_t client,
    int32_t x, int32_t y) {
    if (window == 0 || !window->active || !point_in_rect(client, x, y))
        return DESKTOP_EXPLORER_NO_ENTRY;
    for (uint32_t index = 0U; index < window->entry_count; ++index) {
        if (point_in_rect(
                desktop_explorer_entry_rect(window, client, index), x, y))
            return index;
    }
    return DESKTOP_EXPLORER_NO_ENTRY;
}

int desktop_explorer_pointer_press(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t x, int32_t y,
    desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    desktop_explorer_layout_t layout = desktop_explorer_layout(window, client);
    if (layout.columns == 0U || layout.visible_rows == 0U)
        return DESKTOP_EXPLORER_EINVAL;
    result->window_index = window_index;
    if (window->view == DESKTOP_EXPLORER_VIEW_DETAILS &&
        y >= client.y && y < layout.viewport.y &&
        x >= client.x &&
        (uint64_t)(uint32_t)(x - client.x) < client.width) {
        result->consumed = 1U;
        window->pressed = DESKTOP_EXPLORER_NO_ENTRY;
        window->last_click = DESKTOP_EXPLORER_NO_ENTRY;
        window->last_click_ms = 0U;
        return DESKTOP_EXPLORER_OK;
    }
    if (point_in_rect(layout.scrollbar, x, y)) {
        result->consumed = 1U;
        window->pressed = DESKTOP_EXPLORER_NO_ENTRY;
        window->last_click = DESKTOP_EXPLORER_NO_ENTRY;
        if (!layout.enabled) return DESKTOP_EXPLORER_OK;
        if (point_in_rect(layout.decrement, x, y)) {
            window->scroll_capture = DESKTOP_EXPLORER_SCROLL_DECREMENT;
            scroll_rows(window, &layout, 1U, 1U, result);
        } else if (point_in_rect(layout.increment, x, y)) {
            window->scroll_capture = DESKTOP_EXPLORER_SCROLL_INCREMENT;
            scroll_rows(window, &layout, 1U, 0U, result);
        } else if (point_in_rect(layout.thumb, x, y)) {
            window->scroll_capture = DESKTOP_EXPLORER_SCROLL_THUMB;
            window->scroll_drag_offset = (uint32_t)(y - layout.thumb.y);
        } else if (y < layout.thumb.y) {
            window->scroll_capture = DESKTOP_EXPLORER_SCROLL_PAGE_DECREMENT;
            scroll_rows(window, &layout, layout.visible_rows, 1U, result);
        } else {
            window->scroll_capture = DESKTOP_EXPLORER_SCROLL_PAGE_INCREMENT;
            scroll_rows(window, &layout, layout.visible_rows, 0U, result);
        }
        return DESKTOP_EXPLORER_OK;
    }
    uint32_t entry = desktop_explorer_entry_at(window, client, x, y);
    result->consumed = point_in_rect(client, x, y);
    result->entry_index = entry;
    window->pressed = entry;
    if (window->selected != entry) {
        window->selected = entry;
        result->selection_changed = 1U;
    }
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_pointer_motion(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t x, int32_t y,
    desktop_explorer_result_t *result) {
    (void)x;
    if (explorer == 0 || result == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    result->window_index = window_index;
    if (window->scroll_capture != DESKTOP_EXPLORER_SCROLL_THUMB)
        return DESKTOP_EXPLORER_OK;
    result->consumed = 1U;
    desktop_explorer_layout_t layout = desktop_explorer_layout(window, client);
    uint32_t travel = layout.track.height > layout.thumb.height
        ? layout.track.height - layout.thumb.height : 0U;
    if (!layout.enabled || travel == 0U) {
        window->scroll_capture = DESKTOP_EXPLORER_SCROLL_NONE;
        window->scroll_drag_offset = 0U;
        set_first_row(window, 0U, layout.maximum_first_row, result);
        return DESKTOP_EXPLORER_OK;
    }
    int64_t offset = (int64_t)y - layout.track.y -
        (int64_t)window->scroll_drag_offset;
    if (offset < 0) offset = 0;
    if ((uint64_t)offset > travel) offset = travel;
    uint32_t row = multiply_divide_u32(
        (uint32_t)offset, layout.maximum_first_row, travel);
    set_first_row(window, row, layout.maximum_first_row, result);
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_pointer_release(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t x, int32_t y, uint64_t now_ms,
    desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    if (window->scroll_capture != DESKTOP_EXPLORER_SCROLL_NONE) {
        window->scroll_capture = DESKTOP_EXPLORER_SCROLL_NONE;
        window->scroll_drag_offset = 0U;
        window->pressed = DESKTOP_EXPLORER_NO_ENTRY;
        result->consumed = 1U;
        result->window_index = window_index;
        result->entry_index = DESKTOP_EXPLORER_NO_ENTRY;
        return DESKTOP_EXPLORER_OK;
    }
    uint32_t released = desktop_explorer_entry_at(window, client, x, y);
    uint32_t pressed = window->pressed;
    window->pressed = DESKTOP_EXPLORER_NO_ENTRY;
    result->consumed = 1U;
    result->window_index = window_index;
    result->entry_index = released;
    if (released == DESKTOP_EXPLORER_NO_ENTRY || released != pressed) {
        window->last_click = DESKTOP_EXPLORER_NO_ENTRY;
        return DESKTOP_EXPLORER_OK;
    }
    uint32_t double_click = window->last_click == released &&
        now_ms >= window->last_click_ms &&
        now_ms - window->last_click_ms <= DESKTOP_EXPLORER_DOUBLE_CLICK_MS;
    if (double_click) {
        result->activated = 1U;
        window->last_click = DESKTOP_EXPLORER_NO_ENTRY;
        window->last_click_ms = 0U;
    } else {
        window->last_click = released;
        window->last_click_ms = now_ms;
    }
    return DESKTOP_EXPLORER_OK;
}

void desktop_explorer_pointer_cancel(desktop_explorer_t *explorer,
                                     uint32_t window_index) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active) return;
    explorer->windows[window_index].pressed = DESKTOP_EXPLORER_NO_ENTRY;
    explorer->windows[window_index].last_click = DESKTOP_EXPLORER_NO_ENTRY;
    explorer->windows[window_index].last_click_ms = 0U;
    explorer->windows[window_index].scroll_capture =
        DESKTOP_EXPLORER_SCROLL_NONE;
    explorer->windows[window_index].scroll_drag_offset = 0U;
}

int desktop_explorer_wheel(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, int32_t wheel,
    desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0 || wheel == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    desktop_explorer_layout_t layout = desktop_explorer_layout(window, client);
    result->consumed = 1U;
    result->window_index = window_index;
    uint64_t magnitude = wheel < 0 ? (uint64_t)(-(int64_t)wheel)
                                   : (uint64_t)wheel;
    uint64_t scaled = magnitude * DESKTOP_EXPLORER_WHEEL_ROWS;
    uint32_t amount = scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
    scroll_rows(window, &layout, amount, wheel > 0, result);
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_resize(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    desktop_explorer_layout_t layout = desktop_explorer_layout(window, client);
    if (layout.columns == 0U || layout.visible_rows == 0U)
        return DESKTOP_EXPLORER_EINVAL;
    result->window_index = window_index;
    set_first_row(window, layout.first_row, layout.maximum_first_row, result);
    reveal_selection(window, &layout, result);
    window->scroll_capture = DESKTOP_EXPLORER_SCROLL_NONE;
    window->scroll_drag_offset = 0U;
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_toggle_view(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0 || client.width == 0U ||
        client.height == 0U || client.x < 0 || client.y < 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active ||
        explorer->windows[window_index].view >= DESKTOP_EXPLORER_VIEW_COUNT)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    window->view = window->view == DESKTOP_EXPLORER_VIEW_ICONS
        ? DESKTOP_EXPLORER_VIEW_DETAILS : DESKTOP_EXPLORER_VIEW_ICONS;
    window->pressed = DESKTOP_EXPLORER_NO_ENTRY;
    window->last_click = DESKTOP_EXPLORER_NO_ENTRY;
    window->last_click_ms = 0U;
    window->scroll_capture = DESKTOP_EXPLORER_SCROLL_NONE;
    window->scroll_drag_offset = 0U;
    desktop_explorer_layout_t layout = desktop_explorer_layout(window, client);
    set_first_row(window, layout.first_row, layout.maximum_first_row, result);
    reveal_selection(window, &layout, result);
    result->consumed = 1U;
    result->viewport_changed = 1U;
    result->window_index = window_index;
    result->entry_index = window->selected;
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_drag_object(const desktop_explorer_t *explorer,
                                 uint32_t window_index, uint32_t entry_index,
                                 desktop_drag_object_t *object) {
    if (explorer == 0 || object == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY)
        return DESKTOP_EXPLORER_EINVAL;
    const desktop_explorer_window_t *window =
        &explorer->windows[window_index];
    if (!window->active || window->snapshot_generation == 0U ||
        entry_index >= window->entry_count)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_drag_file_t file;
    clear_bytes(&file, sizeof(file));
    file.version = DESKTOP_EXPLORER_DRAG_FILE_VERSION;
    file.struct_size = sizeof(file);
    file.window_index = window_index;
    file.entry_index = entry_index;
    file.snapshot_generation = window->snapshot_generation;
    copy_entry(&file.identity, &window->entries[entry_index]);
    if (desktop_explorer_child_path(
            window, entry_index, file.path, sizeof(file.path)) !=
        DESKTOP_EXPLORER_OK) return DESKTOP_EXPLORER_ECAPACITY;
    desktop_drag_object_initialize(object);
    object->kind = DESKTOP_DRAG_OBJECT_FILE;
    object->operations = DESKTOP_DRAG_OPERATION_ALL;
    object->source_id = window_index;
    object->source_generation = window->snapshot_generation;
    object->data_size = sizeof(file);
    copy_bytes(object->data, (const uint8_t *)&file, sizeof(file));
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_drag_validate(
    const desktop_explorer_t *explorer, const desktop_drag_object_t *object,
    desktop_explorer_drag_file_t *file) {
    if (explorer == 0 || file == 0 ||
        desktop_drag_validate_object(object) != DESKTOP_DRAG_OK ||
        object->kind != DESKTOP_DRAG_OBJECT_FILE ||
        object->data_size != sizeof(*file)) return DESKTOP_EXPLORER_EINVAL;
    copy_bytes((uint8_t *)file, object->data, sizeof(*file));
    if (file->version != DESKTOP_EXPLORER_DRAG_FILE_VERSION ||
        file->struct_size != sizeof(*file) ||
        file->window_index != object->source_id ||
        file->snapshot_generation != object->source_generation ||
        file->window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        file->reserved[0] != 0U || file->reserved[1] != 0U ||
        file->reserved[2] != 0U)
        return DESKTOP_EXPLORER_EINVAL;
    const desktop_explorer_window_t *window =
        &explorer->windows[file->window_index];
    if (!window->active ||
        window->snapshot_generation != file->snapshot_generation ||
        file->entry_index >= window->entry_count ||
        !entries_equal(&window->entries[file->entry_index], &file->identity))
        return DESKTOP_EXPLORER_ESTALE;
    char current_path[DESKTOP_EXPLORER_PATH_CAPACITY];
    if (desktop_explorer_child_path(
            window, file->entry_index, current_path,
            sizeof(current_path)) != DESKTOP_EXPLORER_OK ||
        !path_equal(current_path, file->path))
        return DESKTOP_EXPLORER_ESTALE;
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_keyboard(
    desktop_explorer_t *explorer, uint32_t window_index,
    desktop_rect_t client, uint32_t key, desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0 ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    desktop_explorer_layout_t layout = desktop_explorer_layout(window, client);
    uint32_t columns = layout.columns;
    if (columns == 0U || layout.visible_rows == 0U)
        return DESKTOP_EXPLORER_EINVAL;
    result->consumed = 1U;
    result->window_index = window_index;
    result->entry_index = window->selected;
    if (window->entry_count == 0U) return DESKTOP_EXPLORER_OK;
    if (key == DESKTOP_EXPLORER_KEY_ENTER) {
        if (window->selected < window->entry_count) {
            result->activated = 1U;
            result->entry_index = window->selected;
        }
        return DESKTOP_EXPLORER_OK;
    }

    uint32_t had_selection = window->selected < window->entry_count;
    uint32_t selected = had_selection ? window->selected : 0U;
    uint32_t next = selected;
    /* The first navigation key establishes a deterministic first selection;
     * only subsequent keys apply a directional delta. */
    if (!had_selection) {
        next = 0U;
    } else if (key == DESKTOP_EXPLORER_KEY_LEFT) {
        if (selected != 0U) next = selected - 1U;
    } else if (key == DESKTOP_EXPLORER_KEY_RIGHT) {
        if (selected + 1U < window->entry_count) next = selected + 1U;
    } else if (key == DESKTOP_EXPLORER_KEY_UP) {
        if (selected >= columns) next = selected - columns;
    } else if (key == DESKTOP_EXPLORER_KEY_DOWN) {
        if (selected <= UINT32_MAX - columns &&
            selected + columns < window->entry_count)
            next = selected + columns;
    } else {
        return DESKTOP_EXPLORER_EINVAL;
    }
    if (window->selected != next) {
        window->selected = next;
        result->selection_changed = 1U;
    }
    reveal_selection(window, &layout, result);
    result->entry_index = window->selected;
    return DESKTOP_EXPLORER_OK;
}
