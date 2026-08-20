/**
 * @file reist/audio_wave.h
 * @brief Bounded RIFF/WAVE loader shared by REIST audio applications.
 *
 * Version 1 accepts uncompressed 48 kHz, 16-bit little-endian PCM with one or
 * two channels.  Callers provide all sample storage; the implementation uses
 * no heap and scans only a fixed-size file prefix and a bounded chunk count.
 */
#ifndef REIST_AUDIO_WAVE_H
#define REIST_AUDIO_WAVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_AUDIO_WAVE_API_VERSION 1U
#define REIST_AUDIO_WAVE_HEADER_CAPACITY 512U
#define REIST_AUDIO_WAVE_CHUNK_LIMIT 16U

typedef struct reist_audio_wave_info {
    uint32_t version;
    uint32_t struct_size;
    uint32_t sample_rate;
    uint32_t source_frames;
    uint32_t loaded_frames;
    uint16_t source_channels;
    uint16_t bits_per_sample;
    uint32_t reserved[4];
} reist_audio_wave_info_t;

#ifdef __cplusplus
static_assert(sizeof(reist_audio_wave_info_t) == 40U,
              "audio wave info ABI changed");
#else
_Static_assert(sizeof(reist_audio_wave_info_t) == 40U,
               "audio wave info ABI changed");
#endif

/**
 * Validate a WAV file and load at most capacity_frames as stereo S16_LE PCM.
 * Mono input is duplicated to both output channels.  Output objects are not
 * published on failure.
 */
int reist_audio_wave_load_preview(const char *path, int16_t *stereo_samples,
                                  uint32_t capacity_frames,
                                  reist_audio_wave_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
