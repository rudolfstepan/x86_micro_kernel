/** @file test_edit_vfs_host.c @brief Host checks for bounded EDIT loading. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "x86os.h"
#include "reist/vfs_file_client.h"

int reist_edit_test_load(const char *path, int *exists);
unsigned int reist_edit_test_line_count(void);
const char *reist_edit_test_line(unsigned int index);

enum mock_mode {
    MOCK_SUCCESS, MOCK_MISSING, MOCK_PARTIAL_READ, MOCK_PREMATURE_EOF,
    MOCK_FSTAT_FAILURE, MOCK_DEADLINE, MOCK_CLOSE_FAILURE, MOCK_TOO_LARGE
};

static const char document[] = "alpha\r\nbeta\n";
static enum mock_mode mode;
static uint64_t now_ms;
static uint32_t offset;
static uint32_t opens, fstats, reads, closes, marker_count;
static uint32_t rights, last_timeout;

static void reset_mock(enum mock_mode next) {
    mode = next; now_ms = 100U; offset = 0U;
    opens = fstats = reads = closes = marker_count = 0U;
    rights = last_timeout = 0U;
}

int x86os_monotonic_ms(uint64_t *value) {
    assert(value != NULL);
    *value = now_ms;
    if (mode == MOCK_DEADLINE && opens != 0U) now_ms = 60100U;
    return 0;
}

int reist_vfs_file_open_rights(const char *path, uint32_t timeout,
                               uint32_t requested_rights,
                               reist_vfs_file_handle_t *handle) {
    assert(strcmp(path, "/edit.txt") == 0 && timeout > 0U &&
           timeout <= 60000U && handle != NULL);
    ++opens; rights = requested_rights;
    if (mode == MOCK_MISSING) return -2;
    *handle = 0x201U;
    return 0;
}

int reist_vfs_file_set_timeout(reist_vfs_file_handle_t handle,
                               uint32_t timeout) {
    assert(handle == 0x201U && timeout > 0U && timeout <= 60000U);
    last_timeout = timeout;
    return 0;
}

int reist_vfs_file_fstat(reist_vfs_file_handle_t handle,
                         x86os_file_info_t *info) {
    assert(handle == 0x201U && info != NULL); ++fstats;
    if (mode == MOCK_FSTAT_FAILURE) return -5;
    memset(info, 0, sizeof(*info)); info->type = X86OS_FILE;
    info->size = mode == MOCK_PREMATURE_EOF
        ? (uint32_t)sizeof(document) + 3U : mode == MOCK_TOO_LARGE
        ? 51201U : (uint32_t)sizeof(document) - 1U;
    return 0;
}

int reist_vfs_file_read_bulk(reist_vfs_file_handle_t handle, void *data,
                             size_t capacity) {
    assert(handle == 0x201U && data != NULL && capacity > 0U && capacity <= 256U);
    ++reads;
    if (mode == MOCK_PARTIAL_READ && offset == 0U) {
        memcpy(data, document, 2U); offset = 2U; return 2;
    }
    uint32_t available = (uint32_t)sizeof(document) - 1U - offset;
    if (available == 0U) return 0;
    uint32_t amount = capacity < available ? (uint32_t)capacity : available;
    memcpy(data, &document[offset], amount); offset += amount; return (int)amount;
}

int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    assert(handle == 0x201U); ++closes;
    return mode == MOCK_CLOSE_FAILURE ? -5 : 0;
}

void x86os_puts(const char *text) {
    if (strcmp(text, "EDIT_VFS_LOAD_OK\n") == 0) ++marker_count;
}

/* Unused editor UI/save dependencies retained in the host-linked object. */
void x86os_clear(void) {}
void x86os_set_cursor(unsigned int column, unsigned int row) {(void)column;(void)row;}
void x86os_draw_text(unsigned int column, unsigned int row, const char *text,
                     size_t length) {(void)column;(void)row;(void)text;(void)length;}
int x86os_getchar(void) { return 0; }
int x86os_open(const char *path) {(void)path;return -1;}
int x86os_read(int descriptor, void *data, size_t size) {(void)descriptor;(void)data;(void)size;return -1;}
int x86os_close(int descriptor) {(void)descriptor;return -1;}
int x86os_stat(const char *path, x86os_file_info_t *info) {(void)path;(void)info;return -1;}
int x86os_create(const char *path) {(void)path;return -1;}
int x86os_write(int descriptor, const void *data, size_t size) {(void)descriptor;(void)data;(void)size;return -1;}
int x86os_fsync(int descriptor) {(void)descriptor;return -1;}
int x86os_unlink(const char *path) {(void)path;return -1;}
int x86os_rename(const char *old_path, const char *new_path) {(void)old_path;(void)new_path;return -1;}
int x86os_getpid(void) { return 1; }

static void test_success(void) {
    reset_mock(MOCK_SUCCESS); int exists = 0;
    assert(reist_edit_test_load("/edit.txt", &exists) == 0 && exists == 1);
    assert(opens == 1U && fstats == 1U && closes == 1U && reads == 2U);
    assert(rights == (REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT));
    assert(marker_count == 1U && reist_edit_test_line_count() == 3U);
    assert(strcmp(reist_edit_test_line(0U), "alpha") == 0);
    assert(strcmp(reist_edit_test_line(1U), "beta") == 0);
}

static void test_missing(void) {
    reset_mock(MOCK_MISSING); int exists = 1;
    assert(reist_edit_test_load("/edit.txt", &exists) == 0 && exists == 0);
    assert(opens == 1U && closes == 0U && marker_count == 0U);
}

static void test_partial_read(void) {
    reset_mock(MOCK_PARTIAL_READ); int exists = 0;
    assert(reist_edit_test_load("/edit.txt", &exists) == 0 && exists == 1);
    assert(reads == 3U && closes == 1U && marker_count == 1U);
}

static void test_failure(enum mock_mode failure) {
    reset_mock(failure); int exists = 0;
    assert(reist_edit_test_load("/edit.txt", &exists) != 0);
    assert(opens == 1U && closes == 1U && marker_count == 0U);
    if (failure != MOCK_CLOSE_FAILURE) assert(last_timeout == 1U);
    assert(reist_edit_test_line_count() == 3U);
    assert(strcmp(reist_edit_test_line(0U), "alpha") == 0);
}

int main(void) {
    test_success(); test_missing(); test_partial_read();
    test_failure(MOCK_PREMATURE_EOF); test_failure(MOCK_FSTAT_FAILURE);
    test_failure(MOCK_DEADLINE); test_failure(MOCK_CLOSE_FAILURE);
    test_failure(MOCK_TOO_LARGE);
    return 0;
}
