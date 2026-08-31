/** @file audio_wave.c @brief Bounded RIFF/WAVE validation and loading. */
#include <stddef.h>
#include <stdint.h>

#include "reist/audio.h"
#include "reist/audio_wave.h"
#include "reist/vfs_file_client.h"

#define WAVE_FORMAT_PCM 1U
#define WAVE_LOAD_DEADLINE_MS 60000U

typedef struct wave_description {
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wave_description_t;

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static int tag_equal(const uint8_t *bytes, const char tag[4]) {
    return bytes[0] == (uint8_t)tag[0] && bytes[1] == (uint8_t)tag[1] &&
        bytes[2] == (uint8_t)tag[2] && bytes[3] == (uint8_t)tag[3];
}

static int parse_header(const uint8_t *bytes, uint32_t available,
                        uint32_t file_size, wave_description_t *description) {
    if (bytes == NULL || description == NULL || available < 20U ||
        available > REIST_AUDIO_WAVE_HEADER_CAPACITY || file_size < available ||
        !tag_equal(bytes, "RIFF") || !tag_equal(&bytes[8], "WAVE"))
        return -84;
    uint32_t riff_bytes = read_le32(&bytes[4]);
    if (riff_bytes < 4U || riff_bytes > file_size - 8U) return -84;
    uint32_t container_end = riff_bytes + 8U;
    uint32_t offset = 12U;
    int have_format = 0;
    wave_description_t parsed = {0};
    for (uint32_t chunk = 0U; chunk < REIST_AUDIO_WAVE_CHUNK_LIMIT; ++chunk) {
        if (offset > available || available - offset < 8U) return -95;
        const uint8_t *header = &bytes[offset];
        uint32_t chunk_bytes = read_le32(&header[4]);
        uint64_t payload = (uint64_t)offset + 8U;
        uint64_t end = payload + chunk_bytes;
        uint64_t padded_end = end + (chunk_bytes & 1U);
        if (end > container_end || padded_end > container_end ||
            padded_end > UINT32_MAX) return -84;
        if (tag_equal(header, "fmt ")) {
            if (have_format || chunk_bytes < 16U || chunk_bytes > 64U ||
                end > available) return -84;
            const uint8_t *format = &bytes[(uint32_t)payload];
            parsed.channels = read_le16(&format[2]);
            parsed.sample_rate = read_le32(&format[4]);
            parsed.byte_rate = read_le32(&format[8]);
            parsed.block_align = read_le16(&format[12]);
            parsed.bits_per_sample = read_le16(&format[14]);
            uint32_t expected_align = (uint32_t)parsed.channels * 2U;
            if (read_le16(format) != WAVE_FORMAT_PCM ||
                (parsed.channels != 1U && parsed.channels != 2U) ||
                parsed.sample_rate != REIST_AUDIO_SAMPLE_RATE ||
                parsed.bits_per_sample != 16U ||
                parsed.block_align != expected_align ||
                parsed.byte_rate != parsed.sample_rate * expected_align)
                return -95;
            have_format = 1;
        } else if (tag_equal(header, "data")) {
            if (!have_format || chunk_bytes == 0U ||
                chunk_bytes % parsed.block_align != 0U) return -84;
            parsed.data_offset = (uint32_t)payload;
            parsed.data_bytes = chunk_bytes;
            *description = parsed;
            return 0;
        }
        if (padded_end > available) return -95;
        offset = (uint32_t)padded_end;
    }
    return -95;
}

#ifdef REIST_AUDIO_WAVE_HELPERS_ONLY
int reist_audio_wave_parse_header(const uint8_t *bytes, uint32_t available,
                                  uint32_t file_size, uint32_t *data_offset,
                                  uint32_t *data_bytes, uint16_t *channels) {
    wave_description_t wave;
    int result = parse_header(bytes, available, file_size, &wave);
    if (result == 0) {
        if (data_offset != NULL) *data_offset = wave.data_offset;
        if (data_bytes != NULL) *data_bytes = wave.data_bytes;
        if (channels != NULL) *channels = wave.channels;
    }
    return result;
}
#else
typedef struct wave_load_budget {
    uint64_t start_ms;
    uint64_t deadline_ms;
} wave_load_budget_t;

static int load_budget_start(wave_load_budget_t *budget) {
    if (budget == NULL || x86os_monotonic_ms(&budget->start_ms) != 0)
        return -5;
    budget->deadline_ms = UINT64_MAX - budget->start_ms <
        WAVE_LOAD_DEADLINE_MS ? UINT64_MAX :
        budget->start_ms + WAVE_LOAD_DEADLINE_MS;
    return 0;
}

static int load_budget_remaining(const wave_load_budget_t *budget,
                                 uint32_t *remaining_ms) {
    if (budget == NULL || remaining_ms == NULL) return -22;
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&now) != 0 || now < budget->start_ms) return -5;
    if (now >= budget->deadline_ms) return -110;
    uint64_t remaining = budget->deadline_ms - now;
    if (remaining > WAVE_LOAD_DEADLINE_MS) remaining = WAVE_LOAD_DEADLINE_MS;
    *remaining_ms = (uint32_t)remaining;
    return *remaining_ms == 0U ? -110 : 0;
}

static int prepare_request(reist_vfs_file_handle_t handle,
                           const wave_load_budget_t *budget) {
    uint32_t remaining_ms = 0U;
    int result = load_budget_remaining(budget, &remaining_ms);
    if (result != 0) return result;
    return reist_vfs_file_set_timeout(handle, remaining_ms);
}

static int read_exact(reist_vfs_file_handle_t handle,
                      const wave_load_budget_t *budget,
                      uint8_t *buffer, uint32_t amount) {
    uint32_t completed = 0U;
    while (completed < amount) {
        int result = prepare_request(handle, budget);
        if (result != 0) return result;
        result = reist_vfs_file_read_bulk(
            handle, &buffer[completed], amount - completed);
        if (result <= 0) return result < 0 ? result : -5;
        if ((uint32_t)result > amount - completed) return -84;
        completed += (uint32_t)result;
    }
    return 0;
}

static int close_object(reist_vfs_file_handle_t handle,
                        const wave_load_budget_t *budget, int result) {
    if (result != 0) {
        /* Failure cleanup does not consume the remaining load budget. */
        (void)reist_vfs_file_set_timeout(handle, 1U);
        (void)reist_vfs_file_close(handle);
        return result;
    }
    uint32_t remaining_ms = 0U;
    int budget_status = load_budget_remaining(budget, &remaining_ms);
    if (budget_status == 0) {
        int timeout_status = reist_vfs_file_set_timeout(handle, remaining_ms);
        if (timeout_status != 0) result = timeout_status;
    } else {
        /* Cleanup is still bounded after an exhausted load deadline. */
        (void)reist_vfs_file_set_timeout(handle, 1U);
        result = budget_status;
    }
    int close_status = reist_vfs_file_close(handle);
    if (result == 0 && close_status != 0) result = close_status;
    if (result == 0 && load_budget_remaining(budget, &remaining_ms) != 0)
        result = -110;
    return result;
}

static int16_t decode_sample(const uint8_t *source) {
    uint16_t raw = read_le16(source);
    return (int16_t)(raw >= 0x8000U ? (int32_t)raw - 0x10000 : raw);
}

static void decode_frames(const uint8_t *input, uint32_t frame_count,
                          const wave_description_t *wave, int16_t *samples,
                          uint32_t output_offset) {
    for (uint32_t index = 0U; index < frame_count; ++index) {
        const uint8_t *source = &input[index * wave->block_align];
        int16_t left = decode_sample(source);
        int16_t right = wave->channels == 1U
            ? left : decode_sample(&source[2]);
        samples[(output_offset + index) * REIST_AUDIO_CHANNELS] = left;
        samples[(output_offset + index) * REIST_AUDIO_CHANNELS + 1U] = right;
    }
}

int reist_audio_wave_load_preview(const char *path, int16_t *samples,
                                  uint32_t capacity_frames,
                                  reist_audio_wave_info_t *info) {
    if (path == NULL || samples == NULL || info == NULL ||
        capacity_frames == 0U || capacity_frames > REIST_AUDIO_MAX_STREAM_FRAMES)
        return -22;
    wave_load_budget_t budget;
    int result = load_budget_start(&budget);
    if (result != 0) return result;
    uint32_t open_timeout_ms = 0U;
    result = load_budget_remaining(&budget, &open_timeout_ms);
    if (result != 0) return result;
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    result = reist_vfs_file_open_rights(
        path, open_timeout_ms,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT, &handle);
    if (result != 0) return result;
    x86os_file_info_t file;
    result = prepare_request(handle, &budget);
    if (result == 0) result = reist_vfs_file_fstat(handle, &file);
    if (result == 0 && (file.type != X86OS_FILE || file.size < 20U))
        result = -2;
    uint8_t header[REIST_AUDIO_WAVE_HEADER_CAPACITY];
    uint32_t header_bytes = 0U;
    if (result == 0)
        header_bytes = file.size < sizeof(header) ? file.size : sizeof(header);
    if (result == 0) result = read_exact(handle, &budget, header, header_bytes);
    wave_description_t wave;
    uint32_t source_frames = 0U;
    uint32_t frames = 0U;
    if (result == 0)
        result = parse_header(header, header_bytes, file.size, &wave);
    if (result == 0) {
        source_frames = wave.data_bytes / wave.block_align;
        frames = source_frames < capacity_frames
            ? source_frames : capacity_frames;
        if (frames == 0U) result = -84;
    }

    uint8_t input[512];
    uint32_t completed = 0U;
    uint32_t target_bytes = result == 0 ? frames * wave.block_align : 0U;
    uint32_t prefix_bytes = 0U;
    if (result == 0 && header_bytes > wave.data_offset) {
        prefix_bytes = header_bytes - wave.data_offset;
        if (prefix_bytes > target_bytes) prefix_bytes = target_bytes;
        uint32_t prefix_frames = prefix_bytes / wave.block_align;
        decode_frames(&header[wave.data_offset], prefix_frames, &wave,
                      samples, 0U);
        completed = prefix_frames;
    }

    uint32_t consumed_bytes = prefix_bytes;
    uint32_t pending_bytes = result == 0
        ? prefix_bytes % wave.block_align : 0U;
    if (result == 0 && pending_bytes != 0U) {
        for (uint32_t index = 0U; index < pending_bytes; ++index)
            input[index] = header[wave.data_offset +
                prefix_bytes - pending_bytes + index];
        uint32_t needed = wave.block_align - pending_bytes;
        result = read_exact(handle, &budget, &input[pending_bytes], needed);
        if (result == 0) {
            decode_frames(input, 1U, &wave, samples, completed);
            ++completed;
            consumed_bytes += needed;
        }
    }

    while (result == 0 && consumed_bytes < target_bytes) {
        uint32_t remaining_bytes = target_bytes - consumed_bytes;
        uint32_t chunk = frames - completed;
        uint32_t chunk_capacity = sizeof(input) / wave.block_align;
        if (chunk > chunk_capacity) chunk = chunk_capacity;
        if (chunk * wave.block_align > remaining_bytes)
            chunk = remaining_bytes / wave.block_align;
        if (chunk == 0U) {
            result = -84;
            break;
        }
        result = read_exact(handle, &budget, input, chunk * wave.block_align);
        if (result == 0) {
            decode_frames(input, chunk, &wave, samples, completed);
            completed += chunk;
            consumed_bytes += chunk * wave.block_align;
        }
    }
    result = close_object(handle, &budget, result);
    if (result != 0) return result;
    reist_audio_wave_info_t published = {
        REIST_AUDIO_WAVE_API_VERSION, sizeof(reist_audio_wave_info_t),
        wave.sample_rate, source_frames, completed, wave.channels,
        wave.bits_per_sample, {0U, 0U, 0U, 0U}};
    *info = published;
    return 0;
}
#endif
