#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "userspace/gui/compositor/desktop_trash.h"

static uint32_t root_exists;
static uint32_t files_exists;
static uint32_t info_exists;
static uint32_t source_exists;
static uint32_t stored_exists;
static uint32_t metadata_exists;
static uint32_t fail_rename;
static uint32_t rename_calls;
static char metadata[DESKTOP_TRASH_METADATA_CAPACITY];
static uint32_t metadata_size;
static x86os_file_info_t source_info;

static uint32_t equal_text(const char *left, const char *right) {
    for (uint32_t index = 0U; index < DESKTOP_TRASH_PATH_CAPACITY; ++index) {
        if (left[index] != right[index]) return 0U;
        if (left[index] == '\0') return 1U;
    }
    return 0U;
}

static uint32_t starts_with(const char *text, const char *prefix) {
    for (uint32_t index = 0U; prefix[index] != '\0'; ++index)
        if (text[index] != prefix[index]) return 0U;
    return 1U;
}

int x86os_stat(const char *path, x86os_file_info_t *info) {
    uint32_t exists = 0U;
    uint32_t type = X86OS_DIRECTORY;
    if (equal_text(path, DESKTOP_TRASH_ROOT_PATH)) exists = root_exists;
    else if (equal_text(path, DESKTOP_TRASH_FILES_PATH)) exists = files_exists;
    else if (equal_text(path, DESKTOP_TRASH_INFO_PATH)) exists = info_exists;
    else if (equal_text(path, "/htdocs/about.txt")) {
        exists = source_exists;
        type = X86OS_FILE;
    } else if (starts_with(path, DESKTOP_TRASH_FILES_PATH "/")) {
        exists = stored_exists;
        type = X86OS_FILE;
    } else if (starts_with(path, DESKTOP_TRASH_INFO_PATH "/")) {
        exists = metadata_exists;
        type = X86OS_FILE;
    }
    if (!exists) return -2;
    if (info != 0) {
        *info = type == X86OS_FILE ? source_info : (x86os_file_info_t){0};
        info->type = type;
    }
    return 0;
}

int x86os_mkdir(const char *path) {
    if (equal_text(path, DESKTOP_TRASH_ROOT_PATH)) root_exists = 1U;
    else if (equal_text(path, DESKTOP_TRASH_FILES_PATH)) files_exists = 1U;
    else if (equal_text(path, DESKTOP_TRASH_INFO_PATH)) info_exists = 1U;
    else return -22;
    return 0;
}

int x86os_create(const char *path) {
    if (!starts_with(path, DESKTOP_TRASH_INFO_PATH "/") || metadata_exists)
        return -1;
    metadata_exists = 1U;
    metadata_size = 0U;
    return 4;
}

int x86os_write(int descriptor, const void *buffer, size_t size) {
    if (descriptor != 4 || size > sizeof(metadata) - metadata_size) return -1;
    const char *bytes = (const char *)buffer;
    for (size_t index = 0U; index < size; ++index)
        metadata[metadata_size++] = bytes[index];
    return (int)size;
}

int x86os_fsync(int descriptor) { return descriptor == 4 ? 0 : -1; }
int x86os_close(int descriptor) { return descriptor == 4 ? 0 : -1; }

int x86os_unlink(const char *path) {
    if (!starts_with(path, DESKTOP_TRASH_INFO_PATH "/")) return -1;
    metadata_exists = 0U;
    metadata_size = 0U;
    return 0;
}

int x86os_rename(const char *old_path, const char *new_path) {
    ++rename_calls;
    if (fail_rename || !equal_text(old_path, "/htdocs/about.txt") ||
        !starts_with(new_path, DESKTOP_TRASH_FILES_PATH "/")) return -1;
    source_exists = 0U;
    stored_exists = 1U;
    return 0;
}

int x86os_readdir_batch(const char *path, uint32_t index,
                        x86os_file_info_t *entries) {
    if (!equal_text(path, DESKTOP_TRASH_FILES_PATH)) return -1;
    if (!stored_exists || index != 0U) return 0;
    entries[0] = source_info;
    entries[0].name[0] = 'a';
    entries[0].name[1] = '\0';
    return 1;
}

uint32_t x86os_get_date(void) { return (2026U << 16U) | (8U << 8U) | 22U; }
uint32_t x86os_get_time(void) { return (19U << 16U) | (45U << 8U) | 7U; }

static uint32_t metadata_contains(const char *needle) {
    for (uint32_t start = 0U; start < metadata_size; ++start) {
        uint32_t index = 0U;
        while (needle[index] != '\0' && start + index < metadata_size &&
               metadata[start + index] == needle[index]) ++index;
        if (needle[index] == '\0') return 1U;
    }
    return 0U;
}

static void reset_fake_fs(void) {
    root_exists = files_exists = info_exists = 0U;
    source_exists = 1U;
    stored_exists = metadata_exists = 0U;
    fail_rename = rename_calls = metadata_size = 0U;
    source_info = (x86os_file_info_t){0};
    source_info.type = X86OS_FILE;
    source_info.size = 42U;
    source_info.create_time = 1U;
    source_info.modify_time = 2U;
    source_info.access_time = 3U;
}

int main(void) {
    reset_fake_fs();
    desktop_trash_state_t state;
    desktop_trash_state_initialize(&state);
    assert(desktop_trash_prepare(&state) == DESKTOP_TRASH_OK);
    assert(state.available == 1U && state.full == 0U);

    desktop_trash_request_t request;
    desktop_trash_request_initialize(&request);
    request.identity = source_info;
    const char path[] = "/htdocs/about.txt";
    for (uint32_t index = 0U; index < sizeof(path); ++index)
        request.source_path[index] = path[index];
    desktop_trash_result_t result;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) == DESKTOP_TRASH_OK);
    assert(result.moved == 1U && source_exists == 0U && stored_exists == 1U);
    assert(state.full == 1U && rename_calls == 1U);
    assert(metadata_contains("[Trash Info]\n"));
    assert(metadata_contains("Version=1\n"));
    assert(metadata_contains("Path=/htdocs/about.txt\n"));
    assert(metadata_contains("DeletionDate=2026-08-22T19:45:07\n"));

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    fail_rename = 1U;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) ==
           DESKTOP_TRASH_ERENAME);
    assert(source_exists == 1U && metadata_exists == 0U);

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    request.identity.size = 99U;
    assert(desktop_trash_move(&state, &request, &result) ==
           DESKTOP_TRASH_ESTALE);
    assert(rename_calls == 0U && source_exists == 1U);
    assert(desktop_trash_source_allowed("/bin/shell.prg") == 0U);
    assert(desktop_trash_source_allowed("/trash/files/a.txt") == 0U);
    assert(desktop_trash_source_allowed("/mnt/usb/a.txt") == 0U);
    assert(desktop_trash_source_allowed("/htdocs/../bin/shell.prg") == 0U);
    assert(desktop_trash_source_allowed("/htdocs/about.txt") == 1U);
    return 0;
}
