/** Host contract for the generation-scoped namespace mutation client. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_namespace_client.h"
#include "x86os.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static x86os_storage_submit_t submitted;
static x86os_vfs_namespace_frame_t service_frame;
static uint64_t now_ms;
static uint32_t submits;
static uint8_t recover_once;
static uint8_t corrupt_reply;
static uint8_t pending_forever;
static uint8_t clock_failure;
static uint32_t cancels;

int x86os_getcwd(char *buffer, size_t size) {
    static const char cwd[] = "/mnt/ext2";
    if (buffer == 0 || size < sizeof(cwd)) return -22;
    memcpy(buffer, cwd, sizeof(cwd));
    return 0;
}

int x86os_drive_info(uint32_t index, x86os_drive_info_t *info) {
    (void)index;
    (void)info;
    return 0;
}

int x86os_storage_submit(const x86os_storage_submit_t *request,
                         const void *data, x86os_storage_handle_t *handle) {
    if (request == 0 || data == 0 || handle == 0) return -22;
    submitted = *request;
    memcpy(&service_frame, data, sizeof(service_frame));
    ++submits;
    *handle = 0x20001U;
    return 0;
}

int x86os_storage_collect(x86os_storage_handle_t handle, int32_t *result,
                          void *data) {
    if (handle != 0x20001U || result == 0 || data == 0) return -22;
    if (pending_forever != 0U) return -11;
    service_frame.result = recover_once != 0U && submits == 1U ? -11 : 0;
    if (corrupt_reply != 0U) service_frame.source[1U] ^= 1;
    memcpy(data, &service_frame, sizeof(service_frame));
    *result = 0;
    return 0;
}

int x86os_storage_cancel(x86os_storage_handle_t handle) {
    if (handle != 0x20001U) return -22;
    ++cancels;
    return 0;
}

int x86os_monotonic_ms(uint64_t *value) {
    if (value == 0) return -22;
    if (clock_failure != 0U) return -5;
    *value = now_ms++;
    return 0;
}

int x86os_sleep_ms(uint32_t milliseconds) {
    (void)milliseconds;
    return 0;
}

int x86os_yield(void) {
    return 0;
}

int main(void) {
    now_ms = 1U;
    CHECK(reist_vfs_unlink(
        "link", REIST_VFS_NAMESPACE_DEFAULT_TIMEOUT_MS) == 0);
    CHECK(submitted.operation == X86OS_STORAGE_VFS_NAMESPACE);
    CHECK(submitted.length == X86OS_STORAGE_BLOCK_SIZE);
    CHECK(service_frame.operation == X86OS_VFS_SHADOW_FS_UNLINK);
    CHECK(strcmp(service_frame.source, "/mnt/ext2/link") == 0);
    CHECK(service_frame.destination_length == 0U);
    CHECK(service_frame.destination[0U] == '\0');

    CHECK(reist_vfs_rename(
        "old-link", "new-link",
        REIST_VFS_NAMESPACE_DEFAULT_TIMEOUT_MS) == 0);
    CHECK(service_frame.operation == X86OS_VFS_SHADOW_FS_RENAME);
    CHECK(strcmp(service_frame.source, "/mnt/ext2/old-link") == 0);
    CHECK(strcmp(service_frame.destination, "/mnt/ext2/new-link") == 0);

    submits = 0U;
    recover_once = 1U;
    CHECK(reist_vfs_unlink("retry-link", 100U) == 0);
    CHECK(submits == 2U);
    recover_once = 0U;

    corrupt_reply = 1U;
    CHECK(reist_vfs_unlink("bad-reply", 100U) == -84);
    corrupt_reply = 0U;
    CHECK(reist_vfs_rename("same", "same", 100U) == -22);
    CHECK(reist_vfs_unlink(0, 100U) == -22);
    CHECK(reist_vfs_rename("old", 0, 100U) == -22);
    CHECK(reist_vfs_unlink("old", 0U) == -22);

    submits = 0U;
    cancels = 0U;
    pending_forever = 1U;
    now_ms = 1U;
    CHECK(reist_vfs_unlink("timeout", 3U) == -110);
    CHECK(submits == 1U);
    CHECK(cancels == 1U);
    pending_forever = 0U;

    submits = 0U;
    clock_failure = 1U;
    CHECK(reist_vfs_unlink("clock", 100U) == -5);
    CHECK(submits == 0U);
    clock_failure = 0U;
    return 0;
}
