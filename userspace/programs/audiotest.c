/** @file audiotest.c @brief Play a bounded 440 Hz PCM test tone. */
#include <stdint.h>

#include "reist/audio.h"

#define TEST_TONE_FRAMES 480U
#define TEST_TONE_HZ 440U
#define TEST_TONE_AMPLITUDE 6000
#define TEST_TONE_DURATION_MS 1000U

static int16_t samples[TEST_TONE_FRAMES * REIST_AUDIO_CHANNELS];

static void put_result(const char *prefix, int result) {
    x86os_puts(prefix);
    if (result < 0) {
        x86os_putchar('-');
        result = (int)(-(int64_t)result);
    }
    char digits[12];
    uint32_t count = 0U;
    uint32_t value = (uint32_t)result;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
    x86os_putchar('\n');
}

static void make_test_tone(void) {
    uint32_t phase = 0U;
    for (uint32_t frame = 0U; frame < TEST_TONE_FRAMES; ++frame) {
        int16_t sample = phase < REIST_AUDIO_SAMPLE_RATE / 2U
            ? TEST_TONE_AMPLITUDE : -TEST_TONE_AMPLITUDE;
        samples[frame * REIST_AUDIO_CHANNELS] = sample;
        samples[frame * REIST_AUDIO_CHANNELS + 1U] = sample;
        phase += TEST_TONE_HZ;
        if (phase >= REIST_AUDIO_SAMPLE_RATE) phase -= REIST_AUDIO_SAMPLE_RATE;
    }
}

int main(void) {
    reist_audio_context_t audio;
    reist_audio_stream_t stream = {0};
    int result = reist_audio_init(&audio);
    if (result != 0) {
        put_result("Audio service unavailable: ", result);
        return 1;
    }
    const reist_audio_format_t format = {
        .sample_rate = REIST_AUDIO_SAMPLE_RATE,
        .channels = REIST_AUDIO_CHANNELS,
        .format = REIST_AUDIO_FORMAT_S16_LE,
    };
    result = reist_audio_open(&audio, &format, &stream);
    if (result == 0) {
        make_test_tone();
        result = reist_audio_write(
            &audio, stream, samples, TEST_TONE_FRAMES);
        if (result == (int)TEST_TONE_FRAMES)
            result = reist_audio_start(&audio, stream);
        else if (result >= 0)
            result = -5;
    }
    if (result == 0) {
        x86os_puts("Playing 440 Hz test tone...\n");
        if (x86os_sleep_ms(TEST_TONE_DURATION_MS) != 0) result = -5;
    }
    if (stream.id != 0U) {
        int stop = result == 0 ? reist_audio_stop(&audio, stream) : 0;
        int close = stop == 0 ? reist_audio_close(&audio, &stream) : stop;
        if (result == 0 && close != 0) result = close;
    }
    reist_audio_shutdown(&audio);
    if (result != 0) {
        put_result("Audio test failed: ", result);
        return 2;
    }
    x86os_puts("Audio test complete.\n");
    return 0;
}
