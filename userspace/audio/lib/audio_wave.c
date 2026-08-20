/** @file audio_wave.c @brief Bounded RIFF/WAVE validation and loading. */
#include <stddef.h>
#include <stdint.h>

#include "reist/audio.h"
#include "reist/audio_wave.h"

#define WAVE_FORMAT_PCM 1U

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
static int read_exact(int descriptor, uint8_t *buffer, uint32_t amount) {
    uint32_t completed = 0U;
    while (completed < amount) {
        int result = x86os_read(descriptor, &buffer[completed], amount - completed);
        if (result <= 0) return result < 0 ? result : -5;
        if ((uint32_t)result > amount - completed) return -84;
        completed += (uint32_t)result;
    }
    return 0;
}

static int skip_exact(int descriptor, uint32_t amount) {
    uint8_t discard[256];
    while (amount != 0U) {
        uint32_t chunk = amount < sizeof(discard) ? amount : sizeof(discard);
        int result = read_exact(descriptor, discard, chunk);
        if (result != 0) return result;
        amount -= chunk;
    }
    return 0;
}

static int16_t decode_sample(const uint8_t *source) {
    uint16_t raw = read_le16(source);
    return (int16_t)(raw >= 0x8000U ? (int32_t)raw - 0x10000 : raw);
}

int reist_audio_wave_load_preview(const char *path, int16_t *samples,
                                  uint32_t capacity_frames,
                                  reist_audio_wave_info_t *info) {
    if (path == NULL || samples == NULL || info == NULL ||
        capacity_frames == 0U || capacity_frames > REIST_AUDIO_MAX_STREAM_FRAMES)
        return -22;
    x86os_file_info_t file;
    if (x86os_stat(path, &file) != 0 || file.type != X86OS_FILE || file.size < 20U)
        return -2;
    int descriptor = x86os_open(path);
    if (descriptor < 0) return descriptor;
    uint8_t header[REIST_AUDIO_WAVE_HEADER_CAPACITY];
    uint32_t header_bytes = file.size < sizeof(header) ? file.size : sizeof(header);
    int result = read_exact(descriptor, header, header_bytes);
    int closed = x86os_close(descriptor);
    if (result == 0 && closed != 0) result = closed;
    if (result != 0) return result;
    wave_description_t wave;
    result = parse_header(header, header_bytes, file.size, &wave);
    if (result != 0) return result;
    uint32_t source_frames = wave.data_bytes / wave.block_align;
    uint32_t frames = source_frames < capacity_frames ? source_frames : capacity_frames;
    if (frames == 0U) return -84;
    descriptor = x86os_open(path);
    if (descriptor < 0) return descriptor;
    result = skip_exact(descriptor, wave.data_offset);
    uint8_t input[512];
    uint32_t completed = 0U;
    while (result == 0 && completed < frames) {
        uint32_t chunk = frames - completed;
        uint32_t chunk_capacity = sizeof(input) / wave.block_align;
        if (chunk > chunk_capacity) chunk = chunk_capacity;
        result = read_exact(descriptor, input, chunk * wave.block_align);
        for (uint32_t index = 0U; result == 0 && index < chunk; ++index) {
            const uint8_t *source = &input[index * wave.block_align];
            int16_t left = decode_sample(source);
            int16_t right = wave.channels == 1U ? left : decode_sample(&source[2]);
            samples[(completed + index) * 2U] = left;
            samples[(completed + index) * 2U + 1U] = right;
        }
        if (result == 0) completed += chunk;
    }
    closed = x86os_close(descriptor);
    if (result == 0 && closed != 0) result = closed;
    if (result != 0) return result;
    reist_audio_wave_info_t published = {
        REIST_AUDIO_WAVE_API_VERSION, sizeof(reist_audio_wave_info_t),
        wave.sample_rate, source_frames, completed, wave.channels,
        wave.bits_per_sample, {0U, 0U, 0U, 0U}};
    *info = published;
    return 0;
}
#endif
