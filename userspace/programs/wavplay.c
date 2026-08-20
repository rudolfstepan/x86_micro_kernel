/**
 * @file wavplay.c
 * @brief Bounded RIFF/WAVE PCM preview player for REIST Ring 3.
 *
 * The version-1 audio ABI exposes one finite cyclic DMA buffer rather than a
 * streaming queue.  This program therefore validates a file, copies at most
 * 2400 source frames into fixed storage and plays that preview for two
 * seconds.  Mono PCM is duplicated to stereo; compressed, floating-point,
 * malformed and differently sampled inputs fail closed.
 */
#include <stddef.h>
#include <stdint.h>

#include "reist/audio.h"

#define WAVPLAY_DEFAULT_PATH "/usr/share/sounds/440hz.wav"
#define WAVPLAY_HEADER_CAPACITY 512U
#define WAVPLAY_CHUNK_LIMIT 16U
#define WAVPLAY_PREVIEW_FRAMES 2400U
#define WAVPLAY_DURATION_MS 2000U
#define WAVPLAY_FORMAT_PCM 1U

typedef struct {
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wavplay_description_t;

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static int tag_equal(const uint8_t *bytes, const char tag[4]) {
    return bytes[0] == (uint8_t)tag[0] &&
        bytes[1] == (uint8_t)tag[1] &&
        bytes[2] == (uint8_t)tag[2] &&
        bytes[3] == (uint8_t)tag[3];
}

/** Parse only the bounded prefix needed to locate a conventional data chunk. */
static int parse_header(const uint8_t *bytes, uint32_t available,
                        uint32_t file_size,
                        wavplay_description_t *description) {
    if (bytes == NULL || description == NULL || available < 20U ||
        available > WAVPLAY_HEADER_CAPACITY || file_size < available ||
        !tag_equal(bytes, "RIFF") || !tag_equal(&bytes[8], "WAVE"))
        return -84;

    uint32_t riff_bytes = read_le32(&bytes[4]);
    if (riff_bytes < 4U || riff_bytes > file_size - 8U) return -84;
    uint32_t container_end = riff_bytes + 8U;
    uint32_t offset = 12U;
    int have_format = 0;
    wavplay_description_t parsed = {0};

    for (uint32_t chunk = 0U; chunk < WAVPLAY_CHUNK_LIMIT; ++chunk) {
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
            uint16_t encoding = read_le16(format);
            parsed.channels = read_le16(&format[2]);
            parsed.sample_rate = read_le32(&format[4]);
            parsed.byte_rate = read_le32(&format[8]);
            parsed.block_align = read_le16(&format[12]);
            parsed.bits_per_sample = read_le16(&format[14]);
            uint32_t expected_align = (uint32_t)parsed.channels * 2U;
            if (encoding != WAVPLAY_FORMAT_PCM ||
                (parsed.channels != 1U && parsed.channels != 2U) ||
                parsed.sample_rate != REIST_AUDIO_SAMPLE_RATE ||
                parsed.bits_per_sample != 16U ||
                parsed.block_align != expected_align ||
                parsed.byte_rate != parsed.sample_rate * expected_align)
                return -95;
            have_format = 1;
        } else if (tag_equal(header, "data")) {
            if (!have_format || chunk_bytes == 0U ||
                chunk_bytes % parsed.block_align != 0U)
                return -84;
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

#ifdef REIST_WAVPLAY_HELPERS_ONLY
int reist_wavplay_parse_header(const uint8_t *bytes, uint32_t available,
                               uint32_t file_size, uint32_t *data_offset,
                               uint32_t *data_bytes, uint16_t *channels) {
    wavplay_description_t description;
    int result = parse_header(bytes, available, file_size, &description);
    if (result == 0) {
        if (data_offset != NULL) *data_offset = description.data_offset;
        if (data_bytes != NULL) *data_bytes = description.data_bytes;
        if (channels != NULL) *channels = description.channels;
    }
    return result;
}
#else

static int16_t samples[WAVPLAY_PREVIEW_FRAMES * REIST_AUDIO_CHANNELS];

static void print_number(uint32_t value) {
    char digits[12];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_error(const char *operation, int result) {
    x86os_puts("wavplay: ");
    x86os_puts(operation);
    x86os_puts(" failed (");
    if (result < 0) {
        x86os_putchar('-');
        result = (int)(-(int64_t)result);
    }
    print_number((uint32_t)result);
    x86os_puts(")\n");
}

/** Read exactly a finite byte count or report malformed/truncated input. */
static int read_exact(int descriptor, uint8_t *buffer, uint32_t amount) {
    uint32_t completed = 0U;
    while (completed < amount) {
        int result = x86os_read(
            descriptor, &buffer[completed], amount - completed);
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
    int32_t value = raw >= 0x8000U ? (int32_t)raw - 0x10000 : raw;
    return (int16_t)value;
}

/** Validate the file and copy a fixed-capacity preview into stereo storage. */
static int load_preview(const char *path, uint32_t *frames_out,
                        uint32_t *source_frames_out, uint16_t *channels_out) {
    x86os_file_info_t info;
    if (x86os_stat(path, &info) != 0 || info.type != X86OS_FILE ||
        info.size < 20U) return -2;

    int descriptor = x86os_open(path);
    if (descriptor < 0) return descriptor;
    uint8_t header[WAVPLAY_HEADER_CAPACITY];
    uint32_t header_bytes = info.size < sizeof(header)
        ? info.size : sizeof(header);
    int result = read_exact(descriptor, header, header_bytes);
    int close_result = x86os_close(descriptor);
    if (result == 0 && close_result != 0) result = close_result;
    if (result != 0) return result;

    wavplay_description_t wave;
    result = parse_header(header, header_bytes, info.size, &wave);
    if (result != 0) return result;
    uint32_t source_frames = wave.data_bytes / wave.block_align;
    uint32_t frames = source_frames < WAVPLAY_PREVIEW_FRAMES
        ? source_frames : WAVPLAY_PREVIEW_FRAMES;
    if (frames == 0U) return -84;

    descriptor = x86os_open(path);
    if (descriptor < 0) return descriptor;
    result = skip_exact(descriptor, wave.data_offset);
    uint8_t input[512];
    uint32_t completed = 0U;
    while (result == 0 && completed < frames) {
        uint32_t chunk = frames - completed;
        uint32_t capacity = sizeof(input) / wave.block_align;
        if (chunk > capacity) chunk = capacity;
        result = read_exact(descriptor, input, chunk * wave.block_align);
        for (uint32_t index = 0U; result == 0 && index < chunk; ++index) {
            const uint8_t *source = &input[index * wave.block_align];
            int16_t left = decode_sample(source);
            int16_t right = wave.channels == 1U
                ? left : decode_sample(&source[2]);
            samples[(completed + index) * 2U] = left;
            samples[(completed + index) * 2U + 1U] = right;
        }
        completed += result == 0 ? chunk : 0U;
    }
    close_result = x86os_close(descriptor);
    if (result == 0 && close_result != 0) result = close_result;
    if (result != 0) return result;
    *frames_out = completed;
    *source_frames_out = source_frames;
    *channels_out = wave.channels;
    return 0;
}

static int play_preview(uint32_t frames) {
    reist_audio_context_t audio;
    reist_audio_stream_t stream = {0};
    int started = 0;
    int result = reist_audio_init(&audio);
    if (result != 0) return result;
    const reist_audio_format_t format = {
        .sample_rate = REIST_AUDIO_SAMPLE_RATE,
        .channels = REIST_AUDIO_CHANNELS,
        .format = REIST_AUDIO_FORMAT_S16_LE,
    };
    result = reist_audio_open(&audio, &format, &stream);
    if (result == 0) {
        int written = reist_audio_write(&audio, stream, samples, frames);
        result = written == (int)frames ? 0 : written < 0 ? written : -5;
    }
    if (result == 0) {
        result = reist_audio_start(&audio, stream);
        started = result == 0;
    }
    if (result == 0 && x86os_sleep_ms(WAVPLAY_DURATION_MS) != 0) result = -5;
    if (started) {
        int stopped = reist_audio_stop(&audio, stream);
        if (result == 0) result = stopped;
    }
    if (stream.id != 0U && !started) {
        int closed = reist_audio_close(&audio, &stream);
        if (result == 0) result = closed;
    } else if (stream.id != 0U && result == 0) {
        result = reist_audio_close(&audio, &stream);
    }
    reist_audio_shutdown(&audio);
    return result;
}

int main(int argc, char **argv) {
    if (argc > 2) {
        x86os_puts("Usage: wavplay [pcm-wave-file]\n");
        return 2;
    }
    const char *path = argc == 2 ? argv[1] : WAVPLAY_DEFAULT_PATH;
    uint32_t frames = 0U;
    uint32_t source_frames = 0U;
    uint16_t channels = 0U;
    int result = load_preview(path, &frames, &source_frames, &channels);
    if (result != 0) {
        print_error("WAV validation", result);
        return 1;
    }
    x86os_puts("WAV PCM: 48000 Hz, 16 bit, ");
    x86os_puts(channels == 1U ? "mono -> stereo, " : "stereo, ");
    print_number(source_frames);
    x86os_puts(" source frames\nPlaying ");
    print_number(frames);
    x86os_puts("-frame bounded preview for 2000 ms...\n");
    result = play_preview(frames);
    if (result != 0) {
        print_error("playback", result);
        return 1;
    }
    x86os_puts("WAV playback complete.\n");
    return 0;
}
#endif
