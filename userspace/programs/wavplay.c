/** @file wavplay.c @brief Bounded command-line WAV preview player. */
#include <stdint.h>

#include "x86os.h"
#include "reist/audio.h"
#include "reist/audio_wave.h"

#define WAVPLAY_DEFAULT_PATH "/usr/share/sounds/440hz.wav"
#define WAVPLAY_PREVIEW_FRAMES 2400U
#define WAVPLAY_DURATION_MS 2000U

static int16_t samples[WAVPLAY_PREVIEW_FRAMES * REIST_AUDIO_CHANNELS];

static void print_number(uint32_t value) {
    char digits[12];
    uint32_t count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_error(const char *operation, int result) {
    x86os_puts("wavplay: "); x86os_puts(operation); x86os_puts(" failed (");
    if (result < 0) { x86os_putchar('-'); result = (int)(-(int64_t)result); }
    print_number((uint32_t)result); x86os_puts(")\n");
}

static int play_preview(uint32_t frames) {
    reist_audio_context_t audio;
    reist_audio_stream_t stream = {0};
    int started = 0;
    int result = reist_audio_init(&audio);
    if (result != 0) return result;
    const reist_audio_format_t format = {
        REIST_AUDIO_SAMPLE_RATE, REIST_AUDIO_CHANNELS, REIST_AUDIO_FORMAT_S16_LE};
    result = reist_audio_open(&audio, &format, &stream);
    if (result == 0) {
        int written = reist_audio_write(&audio, stream, samples, frames);
        result = written == (int)frames ? 0 : written < 0 ? written : -5;
    }
    if (result == 0) { result = reist_audio_start(&audio, stream); started = result == 0; }
    if (result == 0 && x86os_sleep_ms(WAVPLAY_DURATION_MS) != 0) result = -5;
    if (started) { int stopped = reist_audio_stop(&audio, stream); if (result == 0) result = stopped; }
    if (stream.id != 0U) { int closed = reist_audio_close(&audio, &stream); if (result == 0) result = closed; }
    reist_audio_shutdown(&audio);
    return result;
}

int main(int argc, char **argv) {
    if (argc > 2) { x86os_puts("Usage: wavplay [pcm-wave-file]\n"); return 2; }
    const char *path = argc == 2 ? argv[1] : WAVPLAY_DEFAULT_PATH;
    reist_audio_wave_info_t wave;
    int result = reist_audio_wave_load_preview(path, samples, WAVPLAY_PREVIEW_FRAMES, &wave);
    if (result != 0) { print_error("WAV validation", result); return 1; }
    x86os_puts("WAV PCM: 48000 Hz, 16 bit, ");
    x86os_puts(wave.source_channels == 1U ? "mono -> stereo, " : "stereo, ");
    print_number(wave.source_frames); x86os_puts(" source frames\nPlaying ");
    print_number(wave.loaded_frames); x86os_puts("-frame bounded preview for 2000 ms...\n");
    result = play_preview(wave.loaded_frames);
    if (result != 0) { print_error("playback", result); return 1; }
    x86os_puts("WAV playback complete.\n");
    return 0;
}
