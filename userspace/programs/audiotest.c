/** @file audiotest.c @brief Play a bounded 440 Hz PCM test tone. */
#include <stdint.h>

#include "reist/audio.h"

#define TEST_TONE_FRAMES 2400U
#define TEST_TONE_HZ 440U
#define TEST_TONE_AMPLITUDE 6000
#define TEST_TONE_DURATION_MS 1000U
#define TEST_TONE_QUARTER_PHASE (REIST_AUDIO_SAMPLE_RATE / 4U)

#ifndef REIST_AUDIOTEST_HELPERS_ONLY
static int16_t samples[TEST_TONE_FRAMES * REIST_AUDIO_CHANNELS];
#endif

/** Return one bounded triangle-wave sample for a 48-kHz phase position.
 *
 * A triangle wave avoids the broadband edges of the former square wave while
 * retaining an integer-only implementation suitable for the freestanding
 * userspace runtime.  The 2400-frame DMA ring contains exactly 22 periods at
 * 440 Hz, so wrapping the HDA cyclic buffer introduces no phase discontinuity.
 */
static int16_t tone_sample(uint32_t phase) {
    if (phase >= REIST_AUDIO_SAMPLE_RATE) return 0;
    int32_t sample;
    if (phase < TEST_TONE_QUARTER_PHASE) {
        sample = (TEST_TONE_AMPLITUDE * (int32_t)phase) /
            (int32_t)TEST_TONE_QUARTER_PHASE;
    } else if (phase < 3U * TEST_TONE_QUARTER_PHASE) {
        sample = TEST_TONE_AMPLITUDE -
            (TEST_TONE_AMPLITUDE *
             (int32_t)(phase - TEST_TONE_QUARTER_PHASE)) /
                (int32_t)TEST_TONE_QUARTER_PHASE;
    } else {
        sample = -TEST_TONE_AMPLITUDE +
            (TEST_TONE_AMPLITUDE *
             (int32_t)(phase - 3U * TEST_TONE_QUARTER_PHASE)) /
                (int32_t)TEST_TONE_QUARTER_PHASE;
    }
    return (int16_t)sample;
}

#ifdef REIST_AUDIOTEST_HELPERS_ONLY
uint32_t reist_audiotest_buffer_frames(void) {
    return TEST_TONE_FRAMES;
}

int16_t reist_audiotest_sample_for_phase(uint32_t phase) {
    return tone_sample(phase);
}
#else

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
        int16_t sample = tone_sample(phase);
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
#endif
