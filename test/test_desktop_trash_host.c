#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "userspace/gui/compositor/desktop_trash.h"
#include "reist/vfs_file_client.h"

static uint32_t root_exists;
static uint32_t files_exists;
static uint32_t info_exists;
static uint32_t source_exists;
static uint32_t storage_exists;
static uint32_t catalog_exists;
static uint32_t metadata_exists;
static uint32_t fail_rename;
static uint32_t fail_catalog_create;
static uint32_t rename_calls;
static uint32_t metadata_read_offset;
static char metadata[DESKTOP_TRASH_METADATA_CAPACITY];
static uint32_t metadata_size;
static uint32_t metadata_advertised_size;
static uint32_t metadata_type;
static uint32_t vfs_open_calls;
static uint32_t vfs_fstat_calls;
static uint32_t vfs_read_calls;
static uint32_t vfs_close_calls;
static uint32_t vfs_open_rights;
static uint32_t vfs_partial_limit;
static uint32_t vfs_fail_fstat;
static uint32_t vfs_fail_close;
static uint32_t vfs_fail_read;
static uint32_t vfs_clock_calls;
static uint32_t vfs_fail_clock_call;
static uint32_t vfs_expire_clock_call;
static uint32_t vfs_cleanup_timeout_seen;
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
    else if (equal_text(path, "/readme.txt")) {
        exists = source_exists;
        type = X86OS_FILE;
    } else if (starts_with(path, "/RT") &&
               starts_with(path + 9U, ".TRS")) {
        exists = storage_exists;
        type = X86OS_FILE;
    } else if (starts_with(path, DESKTOP_TRASH_FILES_PATH "/")) {
        exists = catalog_exists;
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
    if (starts_with(path, DESKTOP_TRASH_INFO_PATH "/")) {
        if (metadata_exists) return -1;
        metadata_exists = 1U;
        metadata_size = 0U;
        return 4;
    }
    if (starts_with(path, DESKTOP_TRASH_FILES_PATH "/")) {
        if (catalog_exists || fail_catalog_create) return -1;
        catalog_exists = 1U;
        return 5;
    }
    return -1;
}

int x86os_write(int descriptor, const void *buffer, size_t size) {
    if (descriptor != 4 || size > sizeof(metadata) - metadata_size) return -1;
    const char *bytes = (const char *)buffer;
    for (size_t index = 0U; index < size; ++index)
        metadata[metadata_size++] = bytes[index];
    metadata_advertised_size = metadata_size;
    return (int)size;
}

int reist_vfs_file_open_rights(const char *path, uint32_t timeout_ms,
                               uint32_t rights,
                               reist_vfs_file_handle_t *handle) {
    ++vfs_open_calls;
    vfs_open_rights = rights;
    if (timeout_ms == 0U || handle == 0 ||
        !starts_with(path, DESKTOP_TRASH_INFO_PATH "/") || !metadata_exists)
        return -2;
    metadata_read_offset = 0U;
    *handle = 7U;
    return 0;
}

int reist_vfs_file_set_timeout(reist_vfs_file_handle_t handle,
                               uint32_t timeout_ms) {
    if (handle != 7U || timeout_ms == 0U) return -1;
    if (timeout_ms == 1U) vfs_cleanup_timeout_seen = 1U;
    return 0;
}

int reist_vfs_file_fstat(reist_vfs_file_handle_t handle,
                         x86os_file_info_t *info) {
    ++vfs_fstat_calls;
    if (handle != 7U || info == 0 || vfs_fail_fstat) return -1;
    *info = (x86os_file_info_t){0};
    info->type = metadata_type;
    info->size = metadata_advertised_size;
    return 0;
}

int reist_vfs_file_read_bulk(reist_vfs_file_handle_t handle, void *buffer,
                             size_t size) {
    ++vfs_read_calls;
    if (handle != 7U || vfs_fail_read) return -1;
    uint32_t remaining = metadata_size - metadata_read_offset;
    uint32_t count = size < remaining ? (uint32_t)size : remaining;
    if (vfs_partial_limit != 0U && count > vfs_partial_limit)
        count = vfs_partial_limit;
    char *bytes = (char *)buffer;
    for (uint32_t index = 0U; index < count; ++index)
        bytes[index] = metadata[metadata_read_offset + index];
    metadata_read_offset += count;
    return (int)count;
}

int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    ++vfs_close_calls;
    return handle == 7U && !vfs_fail_close ? 0 : -1;
}

int x86os_monotonic_ms(uint64_t *milliseconds) {
    ++vfs_clock_calls;
    if (milliseconds == 0 ||
        (vfs_fail_clock_call != 0U &&
         vfs_clock_calls == vfs_fail_clock_call)) return -1;
    *milliseconds = vfs_expire_clock_call == vfs_clock_calls
        ? 6000U : vfs_clock_calls;
    return 0;
}

void x86os_puts(const char *text) { (void)text; }

int x86os_fsync(int descriptor) {
    return descriptor == 4 || descriptor == 5 ? 0 : -1;
}
int x86os_close(int descriptor) {
    return descriptor == 4 || descriptor == 5 ? 0 : -1;
}

int x86os_unlink(const char *path) {
    if (starts_with(path, DESKTOP_TRASH_INFO_PATH "/")) {
        metadata_exists = 0U;
        metadata_size = 0U;
        return 0;
    }
    if (starts_with(path, DESKTOP_TRASH_FILES_PATH "/")) {
        catalog_exists = 0U;
        return 0;
    }
    if (starts_with(path, "/RT") && starts_with(path + 9U, ".TRS")) {
        storage_exists = 0U;
        return 0;
    }
    return -1;
}

int x86os_rmdir(const char *path) {
    (void)path;
    return -1;
}

int x86os_rename(const char *old_path, const char *new_path) {
    ++rename_calls;
    if (fail_rename) return -1;
    if (equal_text(old_path, "/readme.txt") &&
        starts_with(new_path, "/RT") &&
        starts_with(new_path + 9U, ".TRS") && new_path[13] == '\0') {
        source_exists = 0U;
        storage_exists = 1U;
        return 0;
    }
    if (starts_with(old_path, "/RT") &&
        starts_with(old_path + 9U, ".TRS") && old_path[13] == '\0' &&
        equal_text(new_path, "/readme.txt")) {
        storage_exists = 0U;
        source_exists = 1U;
        return 0;
    }
    return -1;
}

int x86os_readdir_batch(const char *path, uint32_t index,
                        x86os_file_info_t *entries) {
    if (!equal_text(path, DESKTOP_TRASH_FILES_PATH)) return -1;
    if (!catalog_exists || index != 0U) return 0;
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
    storage_exists = catalog_exists = metadata_exists = 0U;
    fail_rename = fail_catalog_create = rename_calls = metadata_size = 0U;
    metadata_read_offset = 0U;
    metadata_advertised_size = 0U;
    metadata_type = X86OS_FILE;
    vfs_open_calls = vfs_fstat_calls = vfs_read_calls = vfs_close_calls = 0U;
    vfs_open_rights = vfs_partial_limit = vfs_fail_fstat = 0U;
    vfs_fail_close = vfs_fail_read = vfs_clock_calls = 0U;
    vfs_fail_clock_call = vfs_expire_clock_call = 0U;
    vfs_cleanup_timeout_seen = 0U;
    source_info = (x86os_file_info_t){0};
    source_info.type = X86OS_FILE;
    source_info.size = 42U;
    source_info.create_time = 1U;
    source_info.modify_time = 2U;
    source_info.access_time = 3U;
}

static void stage_restore(desktop_trash_state_t *state,
                          desktop_trash_request_t *request,
                          desktop_trash_result_t *result,
                          desktop_trash_restore_request_t *restore_request) {
    static const char path[] = "/readme.txt";
    reset_fake_fs();
    desktop_trash_state_initialize(state);
    assert(desktop_trash_prepare(state) == DESKTOP_TRASH_OK);
    desktop_trash_request_initialize(request);
    request->identity = source_info;
    for (uint32_t index = 0U; index < sizeof(path); ++index)
        request->source_path[index] = path[index];
    desktop_trash_result_initialize(result);
    assert(desktop_trash_move(state, request, result) == DESKTOP_TRASH_OK);
    desktop_trash_restore_request_initialize(restore_request);
    for (uint32_t index = 0U; result->catalog_path[index] != '\0'; ++index)
        restore_request->catalog_path[index] = result->catalog_path[index];
    assert(x86os_stat(result->catalog_path, &restore_request->identity) == 0);
}

static void assert_restore_load_failure(
    desktop_trash_state_t *state,
    desktop_trash_restore_request_t *restore_request, int expected_status) {
    desktop_trash_restore_result_t restore_result;
    desktop_trash_restore_result_initialize(&restore_result);
    assert(desktop_trash_restore(state, restore_request, &restore_result) ==
           expected_status);
    assert(restore_result.restored == 0U);
    assert(source_exists == 0U && storage_exists == 1U &&
           catalog_exists == 1U && metadata_exists == 1U);
    assert(rename_calls == 1U && vfs_close_calls == 1U);
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
    const char path[] = "/readme.txt";
    for (uint32_t index = 0U; index < sizeof(path); ++index)
        request.source_path[index] = path[index];
    desktop_trash_result_t result;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) == DESKTOP_TRASH_OK);
    assert(result.moved == 1U && source_exists == 0U && storage_exists == 1U);
    assert(catalog_exists == 1U);
    assert(starts_with(result.stored_path, "/RT"));
    assert(starts_with(result.catalog_path, DESKTOP_TRASH_FILES_PATH "/"));
    assert(state.full == 1U && rename_calls == 1U);
    assert(metadata_contains("[Trash Info]\n"));
    assert(metadata_contains("Version=2\n"));
    assert(metadata_contains("Path=/readme.txt\n"));
    assert(metadata_contains("StoragePath=/RT"));
    assert(metadata_contains("DeletionDate=2026-08-22T19:45:07\n"));

    desktop_trash_restore_request_t restore_request;
    desktop_trash_restore_request_initialize(&restore_request);
    for (uint32_t index = 0U;
         result.catalog_path[index] != '\0'; ++index)
        restore_request.catalog_path[index] = result.catalog_path[index];
    assert(x86os_stat(result.catalog_path, &restore_request.identity) == 0);
    desktop_trash_restore_result_t restore_result;
    desktop_trash_restore_result_initialize(&restore_result);
    vfs_partial_limit = 7U;
    assert(desktop_trash_restore(
               &state, &restore_request, &restore_result) ==
           DESKTOP_TRASH_OK);
    assert(restore_result.restored == 1U &&
           restore_result.cleanup_complete == 1U);
    assert(source_exists == 1U && storage_exists == 0U &&
           catalog_exists == 0U && metadata_exists == 0U);
    assert(state.full == 0U);
    assert(vfs_open_calls == 1U && vfs_fstat_calls == 1U &&
           vfs_read_calls > 2U && vfs_close_calls == 1U);
    assert(vfs_open_rights ==
           (REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT));
    assert(vfs_cleanup_timeout_seen == 0U);

    stage_restore(&state, &request, &result, &restore_request);
    metadata_advertised_size = metadata_size + 1U;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_EIO);
    assert(vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    metadata_type = X86OS_DIRECTORY;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_EINVAL);
    assert(vfs_read_calls == 0U && vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    metadata_advertised_size = DESKTOP_TRASH_METADATA_CAPACITY;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_ECAPACITY);
    assert(vfs_read_calls == 0U && vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    vfs_fail_fstat = 1U;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_EIO);
    assert(vfs_read_calls == 0U && vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    vfs_fail_read = 1U;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_EIO);
    assert(vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    vfs_fail_clock_call = 3U;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_EIO);
    assert(vfs_read_calls == 0U && vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    vfs_expire_clock_call = 3U;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_EIO);
    assert(vfs_read_calls == 0U && vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    metadata_advertised_size = metadata_size - 1U;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_ECAPACITY);
    assert(vfs_cleanup_timeout_seen == 1U);

    stage_restore(&state, &request, &result, &restore_request);
    vfs_fail_close = 1U;
    assert_restore_load_failure(&state, &restore_request,
                                DESKTOP_TRASH_EIO);

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) == DESKTOP_TRASH_OK);
    desktop_trash_restore_request_initialize(&restore_request);
    for (uint32_t index = 0U;
         result.catalog_path[index] != '\0'; ++index)
        restore_request.catalog_path[index] = result.catalog_path[index];
    assert(x86os_stat(result.catalog_path, &restore_request.identity) == 0);
    source_exists = 1U;
    desktop_trash_restore_result_initialize(&restore_result);
    assert(desktop_trash_restore(
               &state, &restore_request, &restore_result) ==
           DESKTOP_TRASH_ECOLLISION);
    assert(restore_result.restored == 0U && storage_exists == 1U &&
           catalog_exists == 1U && metadata_exists == 1U);

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) == DESKTOP_TRASH_OK);
    desktop_trash_restore_request_initialize(&restore_request);
    for (uint32_t index = 0U;
         result.catalog_path[index] != '\0'; ++index)
        restore_request.catalog_path[index] = result.catalog_path[index];
    assert(x86os_stat(result.catalog_path, &restore_request.identity) == 0);
    for (uint32_t index = 0U; index + 9U < metadata_size; ++index) {
        if (starts_with(&metadata[index], "Version=2")) {
            metadata[index + 8U] = '9';
            break;
        }
    }
    desktop_trash_restore_result_initialize(&restore_result);
    assert(desktop_trash_restore(
               &state, &restore_request, &restore_result) ==
           DESKTOP_TRASH_EINVAL);
    assert(source_exists == 0U && storage_exists == 1U &&
           catalog_exists == 1U && metadata_exists == 1U);

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) == DESKTOP_TRASH_OK);
    desktop_trash_restore_request_initialize(&restore_request);
    for (uint32_t index = 0U;
         result.catalog_path[index] != '\0'; ++index)
        restore_request.catalog_path[index] = result.catalog_path[index];
    assert(x86os_stat(result.catalog_path, &restore_request.identity) == 0);
    fail_rename = 1U;
    desktop_trash_restore_result_initialize(&restore_result);
    assert(desktop_trash_restore(
               &state, &restore_request, &restore_result) ==
           DESKTOP_TRASH_ERENAME);
    assert(source_exists == 0U && storage_exists == 1U &&
           catalog_exists == 1U && metadata_exists == 1U);

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) == DESKTOP_TRASH_OK);
    desktop_trash_empty_result_t empty_result;
    desktop_trash_empty_result_initialize(&empty_result);
    assert(desktop_trash_empty(&state, &empty_result) == DESKTOP_TRASH_OK);
    assert(empty_result.removed_count == 1U && empty_result.incomplete == 0U);
    assert(source_exists == 0U && storage_exists == 0U &&
           catalog_exists == 0U && metadata_exists == 0U &&
           state.full == 0U);

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    fail_rename = 1U;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) ==
           DESKTOP_TRASH_ERENAME);
    assert(source_exists == 1U && metadata_exists == 0U &&
           catalog_exists == 0U && storage_exists == 0U);

    reset_fake_fs();
    desktop_trash_state_initialize(&state);
    request.identity = source_info;
    fail_catalog_create = 1U;
    desktop_trash_result_initialize(&result);
    assert(desktop_trash_move(&state, &request, &result) ==
           DESKTOP_TRASH_EIO);
    assert(source_exists == 1U && metadata_exists == 0U &&
           catalog_exists == 0U && rename_calls == 0U);

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
