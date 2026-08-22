#include "desktop_explorer.h"

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
     * freestanding qsort dependency for at most 32 entries. */
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

static uint8_t probe_directory_nonempty(const char *parent,
                                        const x86os_file_info_t *entry) {
    char path[DESKTOP_EXPLORER_PATH_CAPACITY];
    if (entry == 0 || entry->type != X86OS_DIRECTORY ||
        build_child_path(parent, entry->name, path, sizeof(path)) !=
            DESKTOP_EXPLORER_OK) return 1U;
    uint32_t offset = 0U;
    for (uint32_t attempt = 0U;
         attempt < DESKTOP_EXPLORER_DIRECTORY_PROBE_BATCHES; ++attempt) {
        x86os_file_info_t batch[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, offset, batch);
        if (count < 0 || (uint32_t)count > X86OS_READDIR_BATCH_CAPACITY)
            return 1U;
        if (count == 0) return 0U;
        for (int index = 0; index < count; ++index) {
            if (!entry_name_valid(&batch[index])) return 1U;
            if (!entry_is_dot_name(&batch[index])) return 1U;
        }
        if (offset > UINT32_MAX - (uint32_t)count) return 1U;
        offset += (uint32_t)count;
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
    x86os_file_info_t target;
    if (x86os_stat(path, &target) != 0) return DESKTOP_EXPLORER_ENOENT;
    if (target.type != X86OS_DIRECTORY) return DESKTOP_EXPLORER_ENOTDIR;

    explorer->staging_count = 0U;
    explorer->staging_truncated = 0U;
    uint32_t offset = 0U;
    for (;;) {
        x86os_file_info_t batch[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, offset, batch);
        if (count < 0) return DESKTOP_EXPLORER_EIO;
        if (count == 0) break;
        if ((uint32_t)count > X86OS_READDIR_BATCH_CAPACITY)
            return DESKTOP_EXPLORER_EIO;
        for (int index = 0; index < count; ++index) {
            if (!entry_name_valid(&batch[index]))
                return DESKTOP_EXPLORER_EIO;
            if (entry_is_dot_name(&batch[index])) continue;
            if (explorer->staging_count < DESKTOP_EXPLORER_ENTRY_CAPACITY) {
                uint32_t staging_index = explorer->staging_count;
                copy_entry(&explorer->staging[staging_index], &batch[index]);
                explorer->staging_directory_nonempty[staging_index] =
                    batch[index].type == X86OS_DIRECTORY
                        ? probe_directory_nonempty(path, &batch[index]) : 0U;
                ++explorer->staging_count;
            } else explorer->staging_truncated = 1U;
        }
        offset += (uint32_t)count;
        /* One batch beyond capacity is sufficient to report truncation; do
         * not walk an arbitrarily large or hostile directory in one event. */
        if (explorer->staging_truncated) break;
        if (offset > UINT32_MAX - X86OS_READDIR_BATCH_CAPACITY)
            return DESKTOP_EXPLORER_ECAPACITY;
    }
    for (uint32_t index = 0U; index <= path_length; ++index)
        explorer->staging_path[index] = path[index];
    sort_staging(explorer);
    return DESKTOP_EXPLORER_OK;
}

int desktop_explorer_open(desktop_explorer_t *explorer,
                          uint32_t window_index, const char *path) {
    if (explorer == 0 || window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY)
        return DESKTOP_EXPLORER_EINVAL;
    int result = stage_directory(explorer, path);
    if (result != DESKTOP_EXPLORER_OK) return result;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
    clear_bytes(window, sizeof(*window));
    window->active = 1U;
    if (++explorer->next_snapshot_generation == 0U)
        explorer->next_snapshot_generation = 1U;
    window->snapshot_generation = explorer->next_snapshot_generation;
    window->entry_count = explorer->staging_count;
    window->truncated = explorer->staging_truncated;
    window->selected = DESKTOP_EXPLORER_NO_ENTRY;
    window->pressed = DESKTOP_EXPLORER_NO_ENTRY;
    window->last_click = DESKTOP_EXPLORER_NO_ENTRY;
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
    return DESKTOP_EXPLORER_OK;
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

desktop_rect_t desktop_explorer_entry_rect(
    const desktop_explorer_window_t *window, desktop_rect_t client,
    uint32_t entry_index) {
    desktop_rect_t empty = {0, 0, 0U, 0U};
    if (window == 0 || !window->active || entry_index >= window->entry_count ||
        client.x < 0 || client.y < 0 ||
        client.width < DESKTOP_EXPLORER_ICON_WIDTH ||
        client.height < DESKTOP_EXPLORER_ICON_HEIGHT) return empty;
    uint32_t columns = client.width / DESKTOP_EXPLORER_ICON_WIDTH;
    if (columns == 0U) return empty;
    uint32_t column = entry_index % columns;
    uint32_t row = entry_index / columns;
    uint64_t y = (uint64_t)(uint32_t)client.y +
                 (uint64_t)row * DESKTOP_EXPLORER_ICON_HEIGHT;
    if (y >= (uint64_t)(uint32_t)client.y + client.height) return empty;
    desktop_rect_t result = {
        client.x + (int32_t)(column * DESKTOP_EXPLORER_ICON_WIDTH),
        (int32_t)y,
        DESKTOP_EXPLORER_ICON_WIDTH,
        DESKTOP_EXPLORER_ICON_HEIGHT,
    };
    if ((uint64_t)(uint32_t)result.x + result.width >
        (uint64_t)(uint32_t)client.x + client.width)
        result.width = (uint32_t)((uint64_t)(uint32_t)client.x +
                                 client.width - (uint32_t)result.x);
    if ((uint64_t)(uint32_t)result.y + result.height >
        (uint64_t)(uint32_t)client.y + client.height)
        result.height = (uint32_t)((uint64_t)(uint32_t)client.y +
                                  client.height - (uint32_t)result.y);
    return result;
}

static uint32_t point_in_rect(desktop_rect_t rect, int32_t x, int32_t y) {
    return rect.width != 0U && rect.height != 0U && x >= rect.x && y >= rect.y &&
           (uint64_t)(uint32_t)(x - rect.x) < rect.width &&
           (uint64_t)(uint32_t)(y - rect.y) < rect.height;
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
    uint32_t entry = desktop_explorer_entry_at(window, client, x, y);
    result->consumed = point_in_rect(client, x, y);
    result->window_index = window_index;
    result->entry_index = entry;
    window->pressed = entry;
    if (window->selected != entry) {
        window->selected = entry;
        result->selection_changed = 1U;
    }
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
    uint32_t columns, uint32_t key, desktop_explorer_result_t *result) {
    if (explorer == 0 || result == 0 || columns == 0U ||
        window_index >= DESKTOP_EXPLORER_WINDOW_CAPACITY ||
        !explorer->windows[window_index].active)
        return DESKTOP_EXPLORER_EINVAL;
    desktop_explorer_window_t *window = &explorer->windows[window_index];
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
    result->entry_index = window->selected;
    return DESKTOP_EXPLORER_OK;
}
