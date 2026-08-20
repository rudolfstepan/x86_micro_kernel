/** @file test_audio_sdk_host.c @brief Host behavior tests for libreistaudio. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "reist/audio.h"

static x86os_ipc_message_t last_message;
static uint32_t send_count;
static uint32_t receive_count;
static uint32_t release_count;
static uint32_t fail_send_at;
static uint32_t corrupt_request_id;
static uint32_t connect_attempts;
static uint32_t connect_eagain_remaining;
static uint32_t sleep_count;

int x86os_service_connect(uint32_t service_id,
                          x86os_ipc_handle_t *handle) {
    assert(service_id == X86OS_SERVICE_AUDIO);
    assert(handle != NULL);
    ++connect_attempts;
    if (connect_eagain_remaining != 0U) {
        --connect_eagain_remaining;
        return -11;
    }
    *handle = 7U;
    return 0;
}

int x86os_sleep_ms(uint32_t milliseconds) {
    assert(milliseconds == REIST_AUDIO_CONNECT_DELAY_MS);
    ++sleep_count;
    return 0;
}

int x86os_yield(void) {
    return 0;
}

int x86os_ipc_send_timeout(x86os_ipc_handle_t handle,
                           const x86os_ipc_message_t *message,
                           uint32_t timeout_ms) {
    assert(handle == 7U);
    assert(message != NULL);
    assert(timeout_ms > 0U && timeout_ms <= REIST_AUDIO_MAX_TIMEOUT_MS);
    ++send_count;
    if (fail_send_at != 0U && send_count == fail_send_at) return -11;
    last_message = *message;
    return 0;
}

int x86os_ipc_receive_timeout(x86os_ipc_handle_t handle,
                              x86os_ipc_message_t *message,
                              uint32_t timeout_ms) {
    assert(handle == 7U);
    assert(message != NULL);
    assert(timeout_ms > 0U && timeout_ms <= REIST_AUDIO_MAX_TIMEOUT_MS);
    ++receive_count;
    reist_audio_message_t response;
    memcpy(&response, last_message.payload, sizeof(response));
    response.command |= REIST_AUDIO_RESPONSE_FLAG;
    response.status = 0;
    if (response.command ==
            (REIST_AUDIO_COMMAND_INFO | REIST_AUDIO_RESPONSE_FLAG)) {
        response.payload.words[0] = REIST_AUDIO_SAMPLE_RATE;
        response.payload.words[1] = REIST_AUDIO_CHANNELS;
        response.payload.words[2] = REIST_AUDIO_FORMAT_S16_LE;
        response.payload.words[3] = REIST_AUDIO_MESSAGE_FRAMES;
        response.payload.words[4] = REIST_AUDIO_MAX_STREAM_FRAMES;
        response.payload.words[5] = REIST_AUDIO_BACKEND_READY;
    } else if (response.command ==
               (REIST_AUDIO_COMMAND_OPEN | REIST_AUDIO_RESPONSE_FLAG)) {
        response.stream_id = 1U;
        response.stream_generation = 9U;
    }
    if (corrupt_request_id != 0U) ++response.request_id;
    message->version = X86OS_IPC_MESSAGE_VERSION;
    message->struct_size = sizeof(*message);
    message->length = sizeof(response);
    memcpy(message->payload, &response, sizeof(response));
    return 0;
}

int x86os_ipc_release(x86os_ipc_handle_t handle) {
    assert(handle == 7U);
    ++release_count;
    return 0;
}

int main(void) {
    reist_audio_context_t context;
    assert(reist_audio_init(NULL) == -22);
    connect_eagain_remaining = 2U;
    assert(reist_audio_init(&context) == 0);
    assert(connect_attempts == 3U && sleep_count == 2U);
    assert(context.version == REIST_AUDIO_API_VERSION);
    assert(context.endpoint == 7U);
    assert(reist_audio_set_timeout(&context, 0U) == -22);
    assert(reist_audio_set_timeout(
        &context, REIST_AUDIO_MAX_TIMEOUT_MS + 1U) == -22);
    assert(reist_audio_set_timeout(&context, 25U) == 0);

    reist_audio_info_t info;
    assert(reist_audio_get_info(&context, &info) == 0);
    assert(info.preferred_format.sample_rate == REIST_AUDIO_SAMPLE_RATE);
    assert(info.preferred_format.channels == REIST_AUDIO_CHANNELS);
    assert(info.preferred_format.format == REIST_AUDIO_FORMAT_S16_LE);

    reist_audio_stream_t stream = {0};
    reist_audio_format_t bad = {44100U, 2U, REIST_AUDIO_FORMAT_S16_LE};
    uint32_t before = send_count;
    assert(reist_audio_open(&context, &bad, &stream) == -22);
    assert(send_count == before);
    const reist_audio_format_t format = {
        REIST_AUDIO_SAMPLE_RATE, REIST_AUDIO_CHANNELS,
        REIST_AUDIO_FORMAT_S16_LE,
    };
    assert(reist_audio_open(&context, &format, &stream) == 0);
    assert(stream.id == 1U && stream.generation == 9U);

    int16_t samples[REIST_AUDIO_MESSAGE_FRAMES * 2U * REIST_AUDIO_CHANNELS];
    for (size_t index = 0U; index < sizeof(samples) / sizeof(samples[0]);
         ++index) samples[index] = (int16_t)(index + 1U);
    before = send_count;
    assert(reist_audio_write(
        &context, stream, samples, REIST_AUDIO_MESSAGE_FRAMES * 2U) ==
        (int)(REIST_AUDIO_MESSAGE_FRAMES * 2U));
    assert(send_count == before + 2U);

    /* A later block failure is surfaced as an explicit POSIX-style short
     * write; already accepted frames are never hidden behind an errno. */
    before = send_count;
    fail_send_at = send_count + 2U;
    assert(reist_audio_write(
        &context, stream, samples, REIST_AUDIO_MESSAGE_FRAMES * 2U) ==
        (int)REIST_AUDIO_MESSAGE_FRAMES);
    assert(send_count == before + 2U);
    fail_send_at = 0U;

    assert(reist_audio_start(&context, stream) == 0);
    assert(reist_audio_stop(&context, stream) == 0);
    assert(reist_audio_close(&context, &stream) == 0);
    assert(stream.id == 0U && stream.generation == 0U);

    corrupt_request_id = 1U;
    assert(reist_audio_get_info(&context, &info) == -84);
    corrupt_request_id = 0U;
    reist_audio_shutdown(&context);
    reist_audio_shutdown(&context);
    assert(release_count == 1U);
    assert(receive_count > 0U);
    return 0;
}
