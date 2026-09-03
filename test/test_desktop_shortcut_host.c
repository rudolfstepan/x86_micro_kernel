#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "userspace/gui/compositor/desktop_shortcut.h"
#include "reist/vfs_file_client.h"

#define FAKE_NODE_CAPACITY 96U
#define FAKE_DESCRIPTOR_BASE 100

typedef struct fake_node {
    uint32_t active;
    char path[DESKTOP_SHORTCUT_PATH_CAPACITY];
    x86os_file_info_t info;
    uint8_t data[DESKTOP_SHORTCUT_FILE_CAPACITY];
} fake_node_t;

static fake_node_t nodes[FAKE_NODE_CAPACITY];
static uint32_t handle_offsets[FAKE_NODE_CAPACITY];
static uint8_t handle_open[FAKE_NODE_CAPACITY];
static uint64_t fake_clock;
static uint32_t mkdir_calls;
static uint32_t write_calls;
static uint32_t fsync_calls;
static uint32_t rename_calls;
static uint32_t partial_write_limit;
static uint32_t fail_write;
static uint32_t fail_vfs_close;
static uint32_t mutate_target_on_fsync;
static uint32_t mutate_directory_on_fsync;
static uint32_t forced_final_collisions;

static void copy_string(char *destination, size_t capacity,
                        const char *source) {
    size_t length = strlen(source);
    assert(length < capacity);
    memcpy(destination, source, length + 1U);
}

static const char *leaf_name(const char *path) {
    const char *leaf = path;
    for (const char *cursor = path; *cursor != '\0'; ++cursor)
        if (*cursor == '/') leaf = cursor + 1;
    return leaf;
}

static int find_node(const char *path) {
    for (uint32_t index = 0U; index < FAKE_NODE_CAPACITY; ++index)
        if (nodes[index].active && strcmp(nodes[index].path, path) == 0)
            return (int)index;
    return -1;
}

static int allocate_node(const char *path, uint32_t type) {
    assert(find_node(path) < 0);
    for (uint32_t index = 0U; index < FAKE_NODE_CAPACITY; ++index) {
        if (nodes[index].active) continue;
        memset(&nodes[index], 0, sizeof(nodes[index]));
        nodes[index].active = 1U;
        copy_string(nodes[index].path, sizeof(nodes[index].path), path);
        copy_string(nodes[index].info.name, sizeof(nodes[index].info.name),
                    leaf_name(path));
        nodes[index].info.type = type;
        nodes[index].info.create_time = 100U + index;
        nodes[index].info.modify_time = 200U + index;
        nodes[index].info.access_time = 300U + index;
        return (int)index;
    }
    return -1;
}

static int add_file_bytes(const char *path, const void *data, size_t size) {
    int index = allocate_node(path, X86OS_FILE);
    assert(index >= 0 && size <= sizeof(nodes[index].data));
    memcpy(nodes[index].data, data, size);
    nodes[index].info.size = (uint32_t)size;
    return index;
}

static int add_file(const char *path, const char *data) {
    return add_file_bytes(path, data, strlen(data));
}

static int add_directory(const char *path) {
    return allocate_node(path, X86OS_DIRECTORY);
}

static uint32_t has_suffix(const char *text, const char *suffix) {
    size_t length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return length >= suffix_length &&
        strcmp(text + length - suffix_length, suffix) == 0;
}

static uint32_t direct_child(const char *path, const char *directory) {
    size_t length = strlen(directory);
    if (strncmp(path, directory, length) != 0 || path[length] != '/')
        return 0U;
    const char *relative = path + length + 1U;
    return relative[0] != '\0' && strchr(relative, '/') == NULL;
}

static uint32_t count_children_with_suffix(
    const char *directory, const char *suffix) {
    uint32_t count = 0U;
    for (uint32_t index = 0U; index < FAKE_NODE_CAPACITY; ++index)
        if (nodes[index].active &&
            direct_child(nodes[index].path, directory) &&
            has_suffix(nodes[index].path, suffix))
            ++count;
    return count;
}

static void reset_fake_fs(void) {
    memset(nodes, 0, sizeof(nodes));
    memset(handle_offsets, 0, sizeof(handle_offsets));
    memset(handle_open, 0, sizeof(handle_open));
    fake_clock = 1U;
    mkdir_calls = write_calls = fsync_calls = rename_calls = 0U;
    partial_write_limit = fail_write = fail_vfs_close = 0U;
    mutate_target_on_fsync = mutate_directory_on_fsync = 0U;
    forced_final_collisions = 0U;
}

static x86os_file_info_t identity_of(int node) {
    assert(node >= 0);
    return nodes[node].info;
}

static void fill_request(desktop_shortcut_create_request_t *request,
                         int directory, int target,
                         const char *directory_path,
                         const char *target_path,
                         const char *display,
                         uint32_t kind) {
    desktop_shortcut_create_request_initialize(request);
    copy_string(request->directory_path, sizeof(request->directory_path),
                directory_path);
    copy_string(request->target_path, sizeof(request->target_path),
                target_path);
    copy_string(request->display_name, sizeof(request->display_name),
                display);
    request->target_kind = kind;
    request->directory_identity = identity_of(directory);
    request->target_identity = identity_of(target);
}

int x86os_monotonic_ms(uint64_t *value) {
    if (value == NULL) return -22;
    *value = fake_clock++;
    return 0;
}

int reist_vfs_lstat(const char *path, x86os_file_info_t *info,
                    uint32_t timeout_ms) {
    assert(timeout_ms > 0U &&
           timeout_ms <= DESKTOP_SHORTCUT_REQUEST_TIMEOUT_MS);
    if (forced_final_collisions != 0U && has_suffix(path, ".LNK") &&
        find_node(path) < 0) {
        --forced_final_collisions;
        memset(info, 0, sizeof(*info));
        info->type = X86OS_FILE;
        return 0;
    }
    int index = find_node(path);
    if (index < 0) return -2;
    *info = nodes[index].info;
    return 0;
}

int reist_vfs_file_open_flags(const char *path, uint32_t timeout_ms,
                              uint32_t rights, uint32_t flags,
                              reist_vfs_file_handle_t *handle) {
    assert(timeout_ms > 0U);
    assert(rights == (REIST_VFS_FILE_RIGHT_READ |
                      REIST_VFS_FILE_RIGHT_STAT));
    assert(flags == X86OS_O_NOFOLLOW);
    int index = find_node(path);
    if (index < 0) return -2;
    if (nodes[index].info.type != X86OS_FILE) return -40;
    handle_open[index] = 1U;
    handle_offsets[index] = 0U;
    *handle = (reist_vfs_file_handle_t)index + 1U;
    return 0;
}

int reist_vfs_file_set_timeout(reist_vfs_file_handle_t handle,
                               uint32_t timeout_ms) {
    assert(handle > 0U && handle <= FAKE_NODE_CAPACITY && timeout_ms > 0U);
    return 0;
}

int reist_vfs_file_fstat(reist_vfs_file_handle_t handle,
                         x86os_file_info_t *info) {
    uint32_t index = handle - 1U;
    assert(index < FAKE_NODE_CAPACITY && handle_open[index]);
    *info = nodes[index].info;
    return 0;
}

int reist_vfs_file_read_bulk(reist_vfs_file_handle_t handle, void *data,
                             size_t capacity) {
    uint32_t index = handle - 1U;
    assert(index < FAKE_NODE_CAPACITY && handle_open[index]);
    uint32_t offset = handle_offsets[index];
    if (offset >= nodes[index].info.size) return 0;
    size_t amount = nodes[index].info.size - offset;
    if (amount > capacity) amount = capacity;
    memcpy(data, nodes[index].data + offset, amount);
    handle_offsets[index] += (uint32_t)amount;
    return (int)amount;
}

int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    uint32_t index = handle - 1U;
    assert(index < FAKE_NODE_CAPACITY && handle_open[index]);
    handle_open[index] = 0U;
    if (fail_vfs_close) {
        fail_vfs_close = 0U;
        return -5;
    }
    return 0;
}

int x86os_mkdir(const char *path) {
    ++mkdir_calls;
    return add_directory(path) >= 0 ? 0 : -17;
}

int x86os_create(const char *path) {
    if (find_node(path) >= 0) return -17;
    int index = allocate_node(path, X86OS_FILE);
    return index >= 0 ? FAKE_DESCRIPTOR_BASE + index : -5;
}

int x86os_write(int descriptor, const void *buffer, size_t size) {
    ++write_calls;
    if (fail_write) return -5;
    int index = descriptor - FAKE_DESCRIPTOR_BASE;
    assert(index >= 0 && index < (int)FAKE_NODE_CAPACITY);
    size_t amount = size;
    if (partial_write_limit != 0U && amount > partial_write_limit)
        amount = partial_write_limit;
    assert(nodes[index].info.size + amount <= sizeof(nodes[index].data));
    memcpy(nodes[index].data + nodes[index].info.size, buffer, amount);
    nodes[index].info.size += (uint32_t)amount;
    return (int)amount;
}

int x86os_fsync(int descriptor) {
    assert(descriptor >= FAKE_DESCRIPTOR_BASE);
    ++fsync_calls;
    if (mutate_target_on_fsync) {
        int target = find_node("/docs/readme.txt");
        assert(target >= 0);
        ++nodes[target].info.modify_time;
        mutate_target_on_fsync = 0U;
    }
    if (mutate_directory_on_fsync) {
        int directory = find_node("/docs");
        assert(directory >= 0);
        ++nodes[directory].info.create_time;
        mutate_directory_on_fsync = 0U;
    }
    return 0;
}

int x86os_close(int descriptor) {
    assert(descriptor >= FAKE_DESCRIPTOR_BASE);
    return 0;
}

int x86os_unlink(const char *path) {
    int index = find_node(path);
    if (index < 0) return -2;
    nodes[index].active = 0U;
    return 0;
}

int x86os_rename(const char *old_path, const char *new_path) {
    ++rename_calls;
    int old_index = find_node(old_path);
    if (old_index < 0 || find_node(new_path) >= 0) return -17;
    copy_string(nodes[old_index].path, sizeof(nodes[old_index].path),
                new_path);
    copy_string(nodes[old_index].info.name,
                sizeof(nodes[old_index].info.name), leaf_name(new_path));
    return 0;
}

static void test_prepare_directory_is_real_and_fail_closed(void) {
    reset_fake_fs();
    x86os_file_info_t identity;
    assert(desktop_shortcut_prepare_directory(&identity) ==
           DESKTOP_SHORTCUT_OK);
    assert(mkdir_calls == 1U && identity.type == X86OS_DIRECTORY);
    assert(find_node(DESKTOP_SHORTCUT_DIRECTORY) >= 0);

    reset_fake_fs();
    assert(allocate_node(DESKTOP_SHORTCUT_DIRECTORY, X86OS_SYMLINK) >= 0);
    assert(desktop_shortcut_prepare_directory(&identity) ==
           DESKTOP_SHORTCUT_ENOTDIR);
    assert(mkdir_calls == 0U);
}

static void test_create_is_sibling_and_resolve_is_generation_bound(void) {
    reset_fake_fs();
    int desktop = add_directory(DESKTOP_SHORTCUT_DIRECTORY);
    int docs = add_directory("/docs");
    int target = add_file("/docs/readme.txt", "document");
    (void)desktop;
    (void)add_file("/docs/README.LNK", "occupied");
    desktop_shortcut_create_request_t request;
    fill_request(&request, docs, target, "/docs", "/docs/readme.txt",
                 "Readme", DESKTOP_SHORTCUT_TARGET_FILE);
    partial_write_limit = 7U;
    desktop_shortcut_create_result_t created;
    assert(desktop_shortcut_create(&request, &created) ==
           DESKTOP_SHORTCUT_OK);
    assert(created.created);
    assert(strcmp(created.shortcut_path, "/docs/README01.LNK") == 0);
    assert(strcmp(created.filename, "README01.LNK") == 0);
    assert(count_children_with_suffix("/desktop", ".LNK") == 0U);
    assert(count_children_with_suffix("/docs", ".TMP") == 0U);
    assert(count_children_with_suffix("/docs", ".LNK") == 2U);
    assert(write_calls > 1U && fsync_calls == 1U && rename_calls == 1U);

    desktop_shortcut_resolve_result_t resolved;
    assert(desktop_shortcut_resolve(
               created.shortcut_path, &created.shortcut_identity,
               &resolved) == DESKTOP_SHORTCUT_OK);
    assert(resolved.target_kind == DESKTOP_SHORTCUT_TARGET_FILE);
    assert(strcmp(resolved.display_name, "Readme") == 0);
    assert(strcmp(resolved.target_path, "/docs/readme.txt") == 0);
    assert(resolved.target_identity.size == strlen("document"));

    int shortcut = find_node(created.shortcut_path);
    assert(shortcut >= 0);
    ++nodes[shortcut].info.modify_time;
    assert(desktop_shortcut_resolve(
               created.shortcut_path, &created.shortcut_identity,
               &resolved) == DESKTOP_SHORTCUT_ESTALE);
    --nodes[shortcut].info.modify_time;
    nodes[shortcut].data[0] = 'x';
    assert(desktop_shortcut_resolve(
               created.shortcut_path, &created.shortcut_identity,
               &resolved) == DESKTOP_SHORTCUT_EINVAL);
    nodes[shortcut].data[0] = 's';
    nodes[target].active = 0U;
    assert(desktop_shortcut_resolve(
               created.shortcut_path, &created.shortcut_identity,
               &resolved) == DESKTOP_SHORTCUT_ENOENT);
}

static void test_creation_failures_leave_no_partial_sibling(void) {
    desktop_shortcut_create_request_t request;
    desktop_shortcut_create_result_t result;

    reset_fake_fs();
    (void)add_directory(DESKTOP_SHORTCUT_DIRECTORY);
    int docs = add_directory("/docs");
    int target = add_file("/docs/readme.txt", "document");
    fill_request(&request, docs, target, "/docs", "/docs/readme.txt",
                 "Readme", DESKTOP_SHORTCUT_TARGET_FILE);
    fail_write = 1U;
    assert(desktop_shortcut_create(&request, &result) ==
           DESKTOP_SHORTCUT_EIO);
    assert(count_children_with_suffix("/docs", ".TMP") == 0U);
    assert(count_children_with_suffix("/docs", ".LNK") == 0U);

    fail_write = 0U;
    mutate_target_on_fsync = 1U;
    assert(desktop_shortcut_create(&request, &result) ==
           DESKTOP_SHORTCUT_ESTALE);
    assert(count_children_with_suffix("/docs", ".TMP") == 0U);
    assert(count_children_with_suffix("/docs", ".LNK") == 0U);

    request.target_identity = identity_of(target);
    mutate_directory_on_fsync = 1U;
    assert(desktop_shortcut_create(&request, &result) ==
           DESKTOP_SHORTCUT_ESTALE);
    assert(count_children_with_suffix("/docs", ".TMP") == 0U);
    assert(count_children_with_suffix("/docs", ".LNK") == 0U);

    request.directory_identity = identity_of(docs);
    forced_final_collisions = DESKTOP_SHORTCUT_COLLISION_LIMIT;
    assert(desktop_shortcut_create(&request, &result) ==
           DESKTOP_SHORTCUT_ECAPACITY);
    assert(count_children_with_suffix("/docs", ".TMP") == 0U);
    assert(count_children_with_suffix("/docs", ".LNK") == 0U);
}

static void test_malformed_inputs_and_close_failure_are_rejected(void) {
    reset_fake_fs();
    (void)add_directory(DESKTOP_SHORTCUT_DIRECTORY);
    int docs = add_directory("/docs");
    int target = add_file("/docs/tool.prg", "program");
    desktop_shortcut_create_request_t request;
    fill_request(&request, docs, target, "/docs", "/docs/tool.prg",
                 "Tool", DESKTOP_SHORTCUT_TARGET_FILE);
    desktop_shortcut_create_result_t result;
    assert(desktop_shortcut_create(&request, &result) ==
           DESKTOP_SHORTCUT_EINVAL);
    request.target_kind = DESKTOP_SHORTCUT_TARGET_PROGRAM;
    assert(desktop_shortcut_create(&request, &result) ==
           DESKTOP_SHORTCUT_OK);
    fail_vfs_close = 1U;
    desktop_shortcut_resolve_result_t resolved;
    assert(desktop_shortcut_resolve(
               result.shortcut_path, &result.shortcut_identity,
               &resolved) == DESKTOP_SHORTCUT_EIO);
    assert(desktop_shortcut_is_filename("TOOL.LNK"));
    assert(!desktop_shortcut_is_filename("TOOL.URL"));
    assert(!desktop_shortcut_is_filename("TOOL TOO.LNK"));
}

int main(void) {
    test_prepare_directory_is_real_and_fail_closed();
    test_create_is_sibling_and_resolve_is_generation_bound();
    test_creation_failures_leave_no_partial_sibling();
    test_malformed_inputs_and_close_failure_are_rejected();
    return 0;
}
