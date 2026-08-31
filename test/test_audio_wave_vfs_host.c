/** @file test_audio_wave_vfs_host.c
 * @brief Host behavior checks for generation-scoped WAV object loading.
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "reist/audio_wave.h"
#include "reist/vfs_file_client.h"

enum mock_mode {
    MOCK_SUCCESS = 0,
    MOCK_SHORT_READ,
    MOCK_FSTAT_FAILURE,
    MOCK_DEADLINE,
    MOCK_CLOSE_FAILURE,
};

static const uint8_t valid_wave[48] = {
    'R', 'I', 'F', 'F', 40, 0, 0, 0,
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ', 16, 0, 0, 0,
    1, 0, 1, 0, 0x80, 0xBB, 0, 0,
    0x00, 0x77, 0x01, 0, 2, 0, 16, 0,
    'd', 'a', 't', 'a', 4, 0, 0, 0,
    0x34, 0x12, 0xCC, 0xED,
};

static enum mock_mode mode;
static uint64_t now_ms;
static uint32_t read_offset;
static uint32_t open_calls;
static uint32_t close_calls;
static uint32_t fstat_calls;
static uint32_t read_calls;
static uint32_t timeout_calls;
static uint32_t last_timeout_ms;
static uint32_t observed_rights;

static void reset_mock(enum mock_mode next_mode) {
    mode = next_mode;
    now_ms = 100U;
    read_offset = 0U;
    open_calls = 0U;
    close_calls = 0U;
    fstat_calls = 0U;
    read_calls = 0U;
    timeout_calls = 0U;
    last_timeout_ms = 0U;
    observed_rights = 0U;
}

int x86os_monotonic_ms(uint64_t *value) {
    assert(value != NULL);
    *value = now_ms;
    if (mode == MOCK_DEADLINE && open_calls != 0U) now_ms = 60100U;
    return 0;
}

int reist_vfs_file_open_rights(const char *path, uint32_t timeout_ms,
                               uint32_t rights,
                               reist_vfs_file_handle_t *handle) {
    assert(path != NULL && strcmp(path, "/tone.wav") == 0);
    assert(timeout_ms > 0U && timeout_ms <= 60000U);
    assert(handle != NULL);
    ++open_calls;
    observed_rights = rights;
    *handle = 0x101U;
    return 0;
}

int reist_vfs_file_set_timeout(reist_vfs_file_handle_t handle,
                               uint32_t timeout_ms) {
    assert(handle == 0x101U);
    assert(timeout_ms > 0U && timeout_ms <= 60000U);
    ++timeout_calls;
    last_timeout_ms = timeout_ms;
    return 0;
}

int reist_vfs_file_fstat(reist_vfs_file_handle_t handle,
                         x86os_file_info_t *info) {
    assert(handle == 0x101U && info != NULL);
    ++fstat_calls;
    if (mode == MOCK_FSTAT_FAILURE) return -5;
    memset(info, 0, sizeof(*info));
    info->type = X86OS_FILE;
    info->size = sizeof(valid_wave);
    return 0;
}

int reist_vfs_file_read_bulk(reist_vfs_file_handle_t handle, void *data,
                             size_t capacity) {
    assert(handle == 0x101U && data != NULL && capacity > 0U &&
           capacity <= 512U);
    ++read_calls;
    if (mode == MOCK_SHORT_READ) return 0;
    uint32_t remaining = (uint32_t)sizeof(valid_wave) - read_offset;
    uint32_t amount = capacity < remaining ? (uint32_t)capacity : remaining;
    memcpy(data, &valid_wave[read_offset], amount);
    read_offset += amount;
    return (int)amount;
}

int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    assert(handle == 0x101U);
    ++close_calls;
    return mode == MOCK_CLOSE_FAILURE ? -5 : 0;
}

static void assert_unpublished(const reist_audio_wave_info_t *info) {
    const uint8_t *bytes = (const uint8_t *)info;
    for (size_t index = 0U; index < sizeof(*info); ++index)
        assert(bytes[index] == 0xA5U);
}

static void test_success(void) {
    reset_mock(MOCK_SUCCESS);
    int16_t samples[8] = {0};
    reist_audio_wave_info_t info;
    memset(&info, 0xA5, sizeof(info));
    assert(reist_audio_wave_load_preview("/tone.wav", samples, 4U, &info) == 0);
    assert(open_calls == 1U && close_calls == 1U && fstat_calls == 1U);
    assert(read_calls == 1U && timeout_calls >= 3U);
    assert(observed_rights ==
           (REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT));
    assert(info.source_frames == 2U && info.loaded_frames == 2U);
    assert(info.source_channels == 1U && info.bits_per_sample == 16U);
    assert(samples[0] == 0x1234 && samples[1] == 0x1234);
    assert(samples[2] == (int16_t)-0x1234 && samples[3] == (int16_t)-0x1234);
}

static void test_failure(enum mock_mode failure) {
    reset_mock(failure);
    int16_t samples[8] = {0};
    reist_audio_wave_info_t info;
    memset(&info, 0xA5, sizeof(info));
    assert(reist_audio_wave_load_preview("/tone.wav", samples, 4U, &info) != 0);
    assert(open_calls == 1U && close_calls == 1U);
    if (failure != MOCK_CLOSE_FAILURE) assert(last_timeout_ms == 1U);
    assert_unpublished(&info);
}

int main(void) {
    test_success();
    test_failure(MOCK_SHORT_READ);
    test_failure(MOCK_FSTAT_FAILURE);
    test_failure(MOCK_DEADLINE);
    test_failure(MOCK_CLOSE_FAILURE);
    return 0;
}
