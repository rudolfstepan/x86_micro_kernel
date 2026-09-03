#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "userspace/gui/compositor/desktop_file_move.h"
#include "reist/vfs_file_client.h"

#define FAKE_NODE_CAPACITY 64U
#define FAKE_DATA_CAPACITY 8192U
#define FAKE_DESCRIPTOR_BASE 100

typedef struct fake_node {
    uint32_t active;
    char path[DESKTOP_FILE_MOVE_PATH_CAPACITY];
    x86os_file_info_t info;
    uint8_t data[FAKE_DATA_CAPACITY];
} fake_node_t;

static fake_node_t nodes[FAKE_NODE_CAPACITY];
static uint8_t handles[FAKE_NODE_CAPACITY];
static uint32_t offsets[FAKE_NODE_CAPACITY];
static uint64_t fake_clock;
static uint32_t partial_read_limit;
static uint32_t partial_write_limit;
static uint32_t fail_write;
static uint32_t fail_source_unlink;
static uint32_t corrupt_temp_on_fsync;
static uint32_t mutate_source_on_fsync;
static uint32_t rename_calls;
static uint32_t source_unlink_calls;

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
        nodes[index].info.create_time = 1000U + index;
        nodes[index].info.modify_time = 2000U + index;
        nodes[index].info.access_time = 3000U + index;
        return (int)index;
    }
    return -1;
}

static int add_directory(const char *path) {
    return allocate_node(path, X86OS_DIRECTORY);
}

static int add_file_bytes(const char *path, uint32_t size) {
    assert(size <= FAKE_DATA_CAPACITY);
    int index = allocate_node(path, X86OS_FILE);
    assert(index >= 0);
    for (uint32_t byte = 0U; byte < size; ++byte)
        nodes[index].data[byte] = (uint8_t)(byte * 17U + 3U);
    nodes[index].info.size = size;
    return index;
}

static void reset_fake_fs(void) {
    memset(nodes, 0, sizeof(nodes));
    memset(handles, 0, sizeof(handles));
    memset(offsets, 0, sizeof(offsets));
    fake_clock = 1U;
    partial_read_limit = partial_write_limit = 0U;
    fail_write = fail_source_unlink = 0U;
    corrupt_temp_on_fsync = mutate_source_on_fsync = 0U;
    rename_calls = source_unlink_calls = 0U;
}

static x86os_file_info_t identity_of(int node) {
    assert(node >= 0);
    return nodes[node].info;
}

static void fill_request(desktop_file_move_request_t *request,
                         int source, int source_directory,
                         int destination_directory,
                         const char *source_path,
                         const char *source_directory_path,
                         const char *destination_directory_path) {
    desktop_file_move_request_initialize(request);
    copy_string(request->source_path, sizeof(request->source_path),
                source_path);
    copy_string(request->source_directory_path,
                sizeof(request->source_directory_path),
                source_directory_path);
    copy_string(request->destination_directory_path,
                sizeof(request->destination_directory_path),
                destination_directory_path);
    request->source_identity = identity_of(source);
    request->source_directory_identity = identity_of(source_directory);
    request->destination_directory_identity =
        identity_of(destination_directory);
}

static uint32_t suffix(const char *path, const char *ending) {
    size_t length = strlen(path);
    size_t ending_length = strlen(ending);
    return length >= ending_length &&
        strcmp(path + length - ending_length, ending) == 0;
}

static uint32_t temp_count(void) {
    uint32_t count = 0U;
    for (uint32_t index = 0U; index < FAKE_NODE_CAPACITY; ++index)
        if (nodes[index].active && suffix(nodes[index].path, ".TMP"))
            ++count;
    return count;
}

int x86os_monotonic_ms(uint64_t *value) {
    if (value == NULL) return -22;
    *value = fake_clock++;
    return 0;
}

int reist_vfs_lstat(const char *path, x86os_file_info_t *info,
                    uint32_t timeout_ms) {
    assert(timeout_ms > 0U &&
           timeout_ms <= DESKTOP_FILE_MOVE_REQUEST_TIMEOUT_MS);
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
    handles[index] = 1U;
    offsets[index] = 0U;
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
    assert(index < FAKE_NODE_CAPACITY && handles[index]);
    *info = nodes[index].info;
    return 0;
}

int reist_vfs_file_read_bulk(reist_vfs_file_handle_t handle, void *buffer,
                             size_t capacity) {
    uint32_t index = handle - 1U;
    assert(index < FAKE_NODE_CAPACITY && handles[index]);
    uint32_t offset = offsets[index];
    if (offset >= nodes[index].info.size) return 0;
    size_t amount = nodes[index].info.size - offset;
    if (amount > capacity) amount = capacity;
    if (partial_read_limit != 0U && amount > partial_read_limit)
        amount = partial_read_limit;
    memcpy(buffer, nodes[index].data + offset, amount);
    offsets[index] += (uint32_t)amount;
    return (int)amount;
}

int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    uint32_t index = handle - 1U;
    assert(index < FAKE_NODE_CAPACITY && handles[index]);
    handles[index] = 0U;
    return 0;
}

int x86os_create(const char *path) {
    if (find_node(path) >= 0) return -17;
    int index = allocate_node(path, X86OS_FILE);
    return index >= 0 ? FAKE_DESCRIPTOR_BASE + index : -5;
}

int x86os_write(int descriptor, const void *buffer, size_t size) {
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
    int index = descriptor - FAKE_DESCRIPTOR_BASE;
    assert(index >= 0 && index < (int)FAKE_NODE_CAPACITY);
    if (corrupt_temp_on_fsync && nodes[index].info.size != 0U) {
        nodes[index].data[0] ^= 0xFFU;
        corrupt_temp_on_fsync = 0U;
    }
    if (mutate_source_on_fsync) {
        int source = find_node("/docs/readme.txt");
        assert(source >= 0);
        ++nodes[source].info.modify_time;
        mutate_source_on_fsync = 0U;
    }
    return 0;
}

int x86os_close(int descriptor) {
    assert(descriptor >= FAKE_DESCRIPTOR_BASE);
    return 0;
}

int x86os_rename(const char *old_path, const char *new_path) {
    ++rename_calls;
    int old_node = find_node(old_path);
    if (old_node < 0 || find_node(new_path) >= 0) return -17;
    copy_string(nodes[old_node].path, sizeof(nodes[old_node].path),
                new_path);
    copy_string(nodes[old_node].info.name,
                sizeof(nodes[old_node].info.name), leaf_name(new_path));
    return 0;
}

int x86os_unlink(const char *path) {
    int index = find_node(path);
    if (index < 0) return -2;
    if (strcmp(path, "/docs/readme.txt") == 0) {
        ++source_unlink_calls;
        if (fail_source_unlink) return -5;
    }
    nodes[index].active = 0U;
    return 0;
}

static void test_verified_move_publishes_then_removes_source(void) {
    reset_fake_fs();
    int docs = add_directory("/docs");
    int desktop = add_directory("/desktop");
    int source = add_file_bytes("/docs/readme.txt", 4097U);
    desktop_file_move_request_t request;
    fill_request(&request, source, docs, desktop,
                 "/docs/readme.txt", "/docs", "/desktop");
    partial_read_limit = 137U;
    partial_write_limit = 29U;
    desktop_file_move_result_t result;
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_OK);
    assert(result.destination_published && result.source_removed);
    assert(!result.duplicate_retained && result.bytes_copied == 4097U);
    assert(strcmp(result.destination_path, "/desktop/readme.txt") == 0);
    assert(find_node("/docs/readme.txt") < 0);
    int destination = find_node("/desktop/readme.txt");
    assert(destination >= 0 && nodes[destination].info.size == 4097U);
    for (uint32_t byte = 0U; byte < 4097U; ++byte)
        assert(nodes[destination].data[byte] ==
               (uint8_t)(byte * 17U + 3U));
    assert(rename_calls == 1U && source_unlink_calls == 1U);
    assert(temp_count() == 0U);
}

static void test_prepublication_failures_preserve_source(void) {
    desktop_file_move_request_t request;
    desktop_file_move_result_t result;

    reset_fake_fs();
    int docs = add_directory("/docs");
    int desktop = add_directory("/desktop");
    int source = add_file_bytes("/docs/readme.txt", 2048U);
    fill_request(&request, source, docs, desktop,
                 "/docs/readme.txt", "/docs", "/desktop");
    fail_write = 1U;
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_EIO);
    assert(find_node("/docs/readme.txt") >= 0);
    assert(find_node("/desktop/readme.txt") < 0 && temp_count() == 0U);
    assert(source_unlink_calls == 0U && rename_calls == 0U);

    fail_write = 0U;
    corrupt_temp_on_fsync = 1U;
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_EIO);
    assert(find_node("/docs/readme.txt") >= 0);
    assert(find_node("/desktop/readme.txt") < 0 && temp_count() == 0U);
    assert(source_unlink_calls == 0U && rename_calls == 0U);

    mutate_source_on_fsync = 1U;
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_ESTALE);
    assert(find_node("/docs/readme.txt") >= 0);
    assert(find_node("/desktop/readme.txt") < 0 && temp_count() == 0U);
    assert(source_unlink_calls == 0U && rename_calls == 0U);
}

static void test_collision_and_invalid_authority_fail_closed(void) {
    reset_fake_fs();
    int docs = add_directory("/docs");
    int desktop = add_directory("/desktop");
    int source = add_file_bytes("/docs/readme.txt", 32U);
    (void)add_file_bytes("/desktop/readme.txt", 2U);
    desktop_file_move_request_t request;
    fill_request(&request, source, docs, desktop,
                 "/docs/readme.txt", "/docs", "/desktop");
    desktop_file_move_result_t result;
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_EEXIST);
    assert(find_node("/docs/readme.txt") >= 0 && rename_calls == 0U);

    nodes[desktop].info.type = X86OS_SYMLINK;
    request.destination_directory_identity = identity_of(desktop);
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_EINVAL);
    assert(!desktop_file_move_source_allowed("/usr/bin/tool.prg"));
    assert(!desktop_file_move_source_allowed("/trash/files/item.txt"));
    assert(desktop_file_move_source_allowed("/docs/readme.txt"));
    assert(!desktop_file_move_source_allowed("/docs/RM000000.TMP"));
    assert(desktop_file_move_destination_allowed("/desktop"));
    assert(!desktop_file_move_destination_allowed("/"));
    assert(!desktop_file_move_destination_allowed("/etc"));

    request.source_identity.size = DESKTOP_FILE_MOVE_MAX_BYTES + 1U;
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_ECAPACITY);
}

static void test_source_delete_failure_reports_verified_duplicate(void) {
    reset_fake_fs();
    int docs = add_directory("/docs");
    int desktop = add_directory("/desktop");
    int source = add_file_bytes("/docs/readme.txt", 300U);
    desktop_file_move_request_t request;
    fill_request(&request, source, docs, desktop,
                 "/docs/readme.txt", "/docs", "/desktop");
    fail_source_unlink = 1U;
    desktop_file_move_result_t result;
    assert(desktop_file_move_execute(&request, &result) ==
           DESKTOP_FILE_MOVE_EPARTIAL);
    assert(result.destination_published && !result.source_removed);
    assert(result.duplicate_retained);
    assert(find_node("/docs/readme.txt") >= 0);
    assert(find_node("/desktop/readme.txt") >= 0);
    assert(rename_calls == 1U && source_unlink_calls == 1U);
}

int main(void) {
    test_verified_move_publishes_then_removes_source();
    test_prepublication_failures_preserve_source();
    test_collision_and_invalid_authority_fail_closed();
    test_source_delete_failure_reports_verified_duplicate();
    return 0;
}
