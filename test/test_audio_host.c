/** @file test_audio_host.c @brief Host checks for pure HDA decoding rules. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "hda_driver.h"

uint32_t reist_audiotest_buffer_frames(void);
int16_t reist_audiotest_sample_for_phase(uint32_t phase);
int reist_audio_wave_parse_header(const uint8_t *bytes, uint32_t available,
                                  uint32_t file_size, uint32_t *data_offset,
                                  uint32_t *data_bytes, uint16_t *channels);

static void test_bounded_wave_header_parser(void) {
    static const uint8_t valid_wave[48] = {
        'R', 'I', 'F', 'F', 40, 0, 0, 0,
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0,
        1, 0, 1, 0, 0x80, 0xBB, 0, 0,
        0x00, 0x77, 0x01, 0, 2, 0, 16, 0,
        'd', 'a', 't', 'a', 4, 0, 0, 0,
        0, 0, 0, 0,
    };
    uint32_t data_offset = 0U;
    uint32_t data_bytes = 0U;
    uint16_t channels = 0U;
    assert(reist_audio_wave_parse_header(
        valid_wave, 44U, sizeof(valid_wave), &data_offset, &data_bytes,
        &channels) == 0);
    assert(data_offset == 44U);
    assert(data_bytes == 4U);
    assert(channels == 1U);

    uint8_t malformed[44];
    memcpy(malformed, valid_wave, sizeof(malformed));
    malformed[24] = 0x44U; /* Unsupported 44100-Hz rate. */
    malformed[25] = 0xACU;
    assert(reist_audio_wave_parse_header(
        malformed, sizeof(malformed), sizeof(valid_wave), 0, 0, 0) != 0);

    memcpy(malformed, valid_wave, sizeof(malformed));
    malformed[40] = 3U; /* Data is not aligned to a complete mono frame. */
    assert(reist_audio_wave_parse_header(
        malformed, sizeof(malformed), sizeof(valid_wave), 0, 0, 0) != 0);
}

static void test_phase_exact_440hz_tone(void) {
    const uint32_t frames = reist_audiotest_buffer_frames();
    assert(frames == 2400U);
    uint32_t phase = 0U;
    int16_t previous = reist_audiotest_sample_for_phase(phase);
    int16_t peak = previous;
    int16_t trough = previous;
    uint32_t rising_crossings = 0U;
    int32_t largest_step = 0;
    for (uint32_t frame = 1U; frame < frames; ++frame) {
        phase += 440U;
        if (phase >= 48000U) phase -= 48000U;
        int16_t sample = reist_audiotest_sample_for_phase(phase);
        if (previous <= 0 && sample > 0) ++rising_crossings;
        int32_t step = (int32_t)sample - previous;
        if (step < 0) step = -step;
        if (step > largest_step) largest_step = step;
        if (sample > peak) peak = sample;
        if (sample < trough) trough = sample;
        previous = sample;
    }
    phase += 440U;
    if (phase >= 48000U) phase -= 48000U;
    assert(phase == 0U);
    assert(rising_crossings == 22U);
    assert(peak >= 5900 && trough <= -5900);
    assert(largest_step <= 220);
    assert(reist_audiotest_sample_for_phase(phase) == 0);
}

int main(void) {
    test_bounded_wave_header_parser();
    test_phase_exact_440hz_tone();
    uint8_t start = 0U;
    uint8_t count = 0U;
    assert(reist_hda_decode_nodes(0x00020002U, &start, &count));
    assert(start == 2U && count == 2U);
    assert(!reist_hda_decode_nodes(0U, &start, &count));
    assert(!reist_hda_decode_nodes(0x00FF0002U, &start, &count));
    assert(!reist_hda_decode_nodes(0x00020002U, 0, &count));

    uint8_t gain = 0U;
    /* QEMU HDA: Offset 74 is the codec-declared 0-dB step. */
    assert(reist_hda_amp_0db_gain(0x80034A4AU, &gain));
    assert(gain == 0x4AU);
    assert(reist_hda_amp_0db_gain(0U, &gain));
    assert(gain == 0U);
    assert(!reist_hda_amp_0db_gain(0x00000102U, &gain));
    assert(!reist_hda_amp_0db_gain(0x00000101U, 0));
    /* QEMU exposes no positive range, so playback remains exactly at 0 dB. */
    assert(reist_hda_amp_playback_gain(0x80034A4AU, &gain));
    assert(gain == 0x4AU);
    /* Offset 10, max step 30 and 1-dB steps permit exactly +6 dB. */
    assert(reist_hda_amp_playback_gain(0x00031E0AU, &gain));
    assert(gain == 16U);
    assert(!reist_hda_amp_playback_gain(0x00000102U, &gain));
    assert(!reist_hda_amp_playback_gain(0U, 0));
    return 0;
}
