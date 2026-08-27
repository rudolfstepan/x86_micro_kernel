/**
 * @file reist/audio.h
 * @brief Public bounded PCM playback API for REIST Ring-3 programs.
 *
 * This source API follows familiar PCM concepts without claiming binary
 * compatibility with ALSA, OSS, CoreAudio or WASAPI.  Version 1 supports one
 * interleaved S16_LE stereo stream at 48 kHz.  Calls are synchronous but
 * bounded by the context timeout and never grant device authority.
 */
#ifndef REIST_AUDIO_H
#define REIST_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "x86os.h"

#define REIST_AUDIO_API_VERSION 1U
#define REIST_AUDIO_PROTOCOL_VERSION 1U
#define REIST_AUDIO_RESPONSE_FLAG 0x80000000U
#define REIST_AUDIO_SAMPLE_RATE 48000U
#define REIST_AUDIO_CHANNELS 2U
#define REIST_AUDIO_FORMAT_S16_LE 1U
#define REIST_AUDIO_MESSAGE_SAMPLE_BYTES 96U
#define REIST_AUDIO_MESSAGE_FRAMES 24U
#define REIST_AUDIO_BULK_SAMPLE_BYTES 2016U
#define REIST_AUDIO_BULK_FRAMES 504U
#define REIST_AUDIO_MAX_STREAM_FRAMES 15360U
#define REIST_AUDIO_DEFAULT_TIMEOUT_MS 500U
#define REIST_AUDIO_MAX_TIMEOUT_MS 5000U
#define REIST_AUDIO_CONNECT_ATTEMPTS 200U
#define REIST_AUDIO_CONNECT_DELAY_MS 10U

enum reist_audio_command {
    REIST_AUDIO_COMMAND_INFO = 1U,
    REIST_AUDIO_COMMAND_OPEN = 2U,
    REIST_AUDIO_COMMAND_WRITE = 3U,
    REIST_AUDIO_COMMAND_START = 4U,
    REIST_AUDIO_COMMAND_STOP = 5U,
    REIST_AUDIO_COMMAND_CLOSE = 6U,
    /* One-way final session message. A valid RELEASE is never answered, so
     * no stale response can remain when the peer capability disappears. */
    REIST_AUDIO_COMMAND_RELEASE = 7U,
    /** Version-2 IPC payload carrying at most 504 interleaved frames. */
    REIST_AUDIO_COMMAND_WRITE_BULK = 8U,
};

enum reist_audio_backend_state {
    REIST_AUDIO_BACKEND_READY = 1U,
    REIST_AUDIO_BACKEND_BUFFERING = 2U,
    REIST_AUDIO_BACKEND_RUNNING = 3U,
};

/** Exactly one versioned x86os IPC payload (128 bytes). */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t command;
    uint32_t request_id;
    uint32_t stream_id;
    uint32_t stream_generation;
    uint32_t frame_count;
    int32_t status;
    union {
        int16_t samples[REIST_AUDIO_MESSAGE_FRAMES * REIST_AUDIO_CHANNELS];
        uint32_t words[REIST_AUDIO_MESSAGE_SAMPLE_BYTES / sizeof(uint32_t)];
        uint8_t bytes[REIST_AUDIO_MESSAGE_SAMPLE_BYTES];
    } payload;
} reist_audio_message_t;

_Static_assert(sizeof(reist_audio_message_t) == 128U,
               "audio protocol message ABI changed");

/** Exactly one version-2 x86os IPC payload (2048 bytes). */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t command;
    uint32_t request_id;
    uint32_t stream_id;
    uint32_t stream_generation;
    uint32_t frame_count;
    int32_t status;
    union {
        int16_t samples[REIST_AUDIO_BULK_FRAMES * REIST_AUDIO_CHANNELS];
        uint32_t words[REIST_AUDIO_BULK_SAMPLE_BYTES / sizeof(uint32_t)];
        uint8_t bytes[REIST_AUDIO_BULK_SAMPLE_BYTES];
    } payload;
} reist_audio_bulk_message_t;

_Static_assert(sizeof(reist_audio_bulk_message_t) == 2048U,
               "audio bulk protocol message ABI changed");

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t format;
} reist_audio_format_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    reist_audio_format_t preferred_format;
    uint32_t max_message_frames;
    uint32_t max_stream_frames;
    uint32_t backend_state;
    uint32_t reserved;
} reist_audio_info_t;

typedef struct {
    uint32_t id;
    uint32_t generation;
} reist_audio_stream_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_ipc_handle_t endpoint;
    uint32_t next_request_id;
    uint32_t timeout_ms;
    uint32_t connected;
    uint32_t reserved[2];
} reist_audio_context_t;

_Static_assert(sizeof(reist_audio_info_t) == 32U,
               "audio info ABI changed");
_Static_assert(sizeof(reist_audio_context_t) == 32U,
               "audio context ABI changed");

/** Connect to the supervised PCM service within a finite two-second budget. */
int reist_audio_init(reist_audio_context_t *context);
/**
 * Request graceful session release, then drop the IPC capability.
 * The request is one-way and best effort; failure falls back to supervised
 * endpoint rotation on the next connection. Safe to call more than once.
 */
void reist_audio_shutdown(reist_audio_context_t *context);
/** Configure the finite transaction timeout (1..5000 ms). */
int reist_audio_set_timeout(reist_audio_context_t *context,
                            uint32_t timeout_ms);
/** Query the negotiated backend capabilities. */
int reist_audio_get_info(reist_audio_context_t *context,
                         reist_audio_info_t *info);
/** Open one fixed-format PCM stream. */
int reist_audio_open(reist_audio_context_t *context,
                     const reist_audio_format_t *format,
                     reist_audio_stream_t *stream);
/**
 * Copy interleaved frames into the bounded service buffer.
 * Like POSIX write, an explicitly reported short positive count may be
 * returned if a later bounded IPC block fails; no accepted frames are hidden.
 * @return accepted frame count, or a negative error if none were accepted.
 */
int reist_audio_write(reist_audio_context_t *context,
                      reist_audio_stream_t stream,
                      const int16_t *interleaved_samples,
                      uint32_t frame_count);
/** Start cyclic playback of all frames written to the stream. */
int reist_audio_start(reist_audio_context_t *context,
                      reist_audio_stream_t stream);
/** Stop hardware playback within the configured deadline. */
int reist_audio_stop(reist_audio_context_t *context,
                     reist_audio_stream_t stream);
/** Close the generation-scoped stream handle. */
int reist_audio_close(reist_audio_context_t *context,
                      reist_audio_stream_t *stream);

#endif
