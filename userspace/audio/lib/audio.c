/** @file audio.c @brief Public PCM client implementation over bounded IPC. */
#include "reist/audio.h"

static void audio_zero(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static uint32_t audio_request_id(reist_audio_context_t *context) {
    uint32_t result = context->next_request_id++;
    if (result == 0U) result = context->next_request_id++;
    if (context->next_request_id == 0U) context->next_request_id = 1U;
    return result;
}

static int audio_context_valid(const reist_audio_context_t *context) {
    return context != NULL && context->version == REIST_AUDIO_API_VERSION &&
        context->struct_size == sizeof(*context) && context->connected == 1U &&
        context->endpoint != X86OS_IPC_INVALID_HANDLE &&
        context->timeout_ms > 0U &&
        context->timeout_ms <= REIST_AUDIO_MAX_TIMEOUT_MS;
}

static void audio_message_init(reist_audio_message_t *wire,
                               uint32_t command, uint32_t request_id,
                               reist_audio_stream_t stream) {
    audio_zero(wire, sizeof(*wire));
    wire->version = REIST_AUDIO_PROTOCOL_VERSION;
    wire->struct_size = sizeof(*wire);
    wire->command = command;
    wire->request_id = request_id;
    wire->stream_id = stream.id;
    wire->stream_generation = stream.generation;
}

static int audio_transact(reist_audio_context_t *context,
                          reist_audio_message_t *wire) {
    if (!audio_context_valid(context) || wire == NULL) return -22;
    x86os_ipc_message_t message;
    audio_zero(&message, sizeof(message));
    message.version = X86OS_IPC_MESSAGE_VERSION;
    message.struct_size = sizeof(message);
    message.length = sizeof(*wire);
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        message.payload[index] = ((const uint8_t *)wire)[index];
    int result = x86os_ipc_send_timeout(
        context->endpoint, &message, context->timeout_ms);
    if (result != 0) return result;
    result = x86os_ipc_receive_timeout(
        context->endpoint, &message, context->timeout_ms);
    if (result != 0) return result;
    if (message.version != X86OS_IPC_MESSAGE_VERSION ||
        message.struct_size != sizeof(message) ||
        message.length != sizeof(*wire)) return -84;
    reist_audio_message_t response;
    for (size_t index = 0U; index < sizeof(response); ++index)
        ((uint8_t *)&response)[index] = message.payload[index];
    if (response.version != REIST_AUDIO_PROTOCOL_VERSION ||
        response.struct_size != sizeof(response) ||
        response.command != (wire->command | REIST_AUDIO_RESPONSE_FLAG) ||
        response.request_id != wire->request_id) return -84;
    *wire = response;
    return response.status;
}

static int audio_bulk_transact(reist_audio_context_t *context,
                               reist_audio_bulk_message_t *wire) {
    if (!audio_context_valid(context) || wire == NULL) return -22;
    x86os_ipc_bulk_message_t message;
    audio_zero(&message, sizeof(message));
    message.version = X86OS_IPC_BULK_MESSAGE_VERSION;
    message.struct_size = sizeof(message);
    message.length = sizeof(*wire);
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        message.payload[index] = ((const uint8_t *)wire)[index];
    int result = x86os_ipc_send_bulk_timeout(
        context->endpoint, &message, context->timeout_ms);
    if (result != 0) return result;

    audio_zero(&message, sizeof(message));
    message.version = X86OS_IPC_BULK_MESSAGE_VERSION;
    message.struct_size = sizeof(message);
    result = x86os_ipc_receive_bulk_timeout(
        context->endpoint, &message, context->timeout_ms);
    if (result != 0) return result;
    if (message.version != X86OS_IPC_MESSAGE_VERSION ||
        message.struct_size != sizeof(x86os_ipc_message_t) ||
        message.length != sizeof(reist_audio_message_t)) return -84;
    reist_audio_message_t response;
    for (size_t index = 0U; index < sizeof(response); ++index)
        ((uint8_t *)&response)[index] = message.payload[index];
    if (response.version != REIST_AUDIO_PROTOCOL_VERSION ||
        response.struct_size != sizeof(response) ||
        response.command != (wire->command | REIST_AUDIO_RESPONSE_FLAG) ||
        response.request_id != wire->request_id) return -84;
    return response.status;
}

int reist_audio_init(reist_audio_context_t *context) {
    if (context == NULL) return -22;
    audio_zero(context, sizeof(*context));
    x86os_ipc_handle_t endpoint = X86OS_IPC_INVALID_HANDLE;
    int result = -11;
    for (uint32_t attempt = 0U; attempt < REIST_AUDIO_CONNECT_ATTEMPTS;
         ++attempt) {
        result = x86os_service_connect(X86OS_SERVICE_AUDIO, &endpoint);
        if (result != -11 && result != -16) break;
        if (attempt + 1U < REIST_AUDIO_CONNECT_ATTEMPTS &&
            x86os_sleep_ms(REIST_AUDIO_CONNECT_DELAY_MS) != 0)
            (void)x86os_yield();
    }
    if (result != 0) return result;
    *context = (reist_audio_context_t){
        .version = REIST_AUDIO_API_VERSION,
        .struct_size = sizeof(*context),
        .endpoint = endpoint,
        .next_request_id = 1U,
        .timeout_ms = REIST_AUDIO_DEFAULT_TIMEOUT_MS,
        .connected = 1U,
    };
    return 0;
}

void reist_audio_shutdown(reist_audio_context_t *context) {
    if (context == NULL) return;
    if (audio_context_valid(context)) {
        reist_audio_message_t wire;
        audio_message_init(&wire, REIST_AUDIO_COMMAND_RELEASE,
                           audio_request_id(context),
                           (reist_audio_stream_t){0});
        x86os_ipc_message_t message;
        audio_zero(&message, sizeof(message));
        message.version = X86OS_IPC_MESSAGE_VERSION;
        message.struct_size = sizeof(message);
        message.length = sizeof(wire);
        for (size_t index = 0U; index < sizeof(wire); ++index)
            message.payload[index] = ((const uint8_t *)&wire)[index];
        /* RELEASE has deliberately no response. Once enqueue succeeds, the
         * following capability drop lets the service prove an armed EPIPE
         * without leaving a reply for a future client. */
        (void)x86os_ipc_send_timeout(
            context->endpoint, &message, context->timeout_ms);
        (void)x86os_ipc_release(context->endpoint);
    } else if (context->connected == 1U &&
               context->endpoint != X86OS_IPC_INVALID_HANDLE) {
        /* Corrupt local context still relinquishes its endpoint, but cannot
         * authorize reusable-session cleanup. */
        (void)x86os_ipc_release(context->endpoint);
    }
    audio_zero(context, sizeof(*context));
}

int reist_audio_set_timeout(reist_audio_context_t *context,
                            uint32_t timeout_ms) {
    if (!audio_context_valid(context) || timeout_ms == 0U ||
        timeout_ms > REIST_AUDIO_MAX_TIMEOUT_MS) return -22;
    context->timeout_ms = timeout_ms;
    return 0;
}

int reist_audio_get_info(reist_audio_context_t *context,
                         reist_audio_info_t *info) {
    if (!audio_context_valid(context) || info == NULL) return -22;
    reist_audio_message_t wire;
    audio_message_init(&wire, REIST_AUDIO_COMMAND_INFO,
                       audio_request_id(context), (reist_audio_stream_t){0});
    int result = audio_transact(context, &wire);
    if (result != 0) return result;
    *info = (reist_audio_info_t){
        .version = REIST_AUDIO_API_VERSION,
        .struct_size = sizeof(*info),
        .preferred_format = {
            .sample_rate = wire.payload.words[0],
            .channels = (uint16_t)wire.payload.words[1],
            .format = (uint16_t)wire.payload.words[2],
        },
        .max_message_frames = wire.payload.words[3],
        .max_stream_frames = wire.payload.words[4],
        .backend_state = wire.payload.words[5],
    };
    return 0;
}

int reist_audio_open(reist_audio_context_t *context,
                     const reist_audio_format_t *format,
                     reist_audio_stream_t *stream) {
    if (!audio_context_valid(context) || format == NULL || stream == NULL ||
        format->sample_rate != REIST_AUDIO_SAMPLE_RATE ||
        format->channels != REIST_AUDIO_CHANNELS ||
        format->format != REIST_AUDIO_FORMAT_S16_LE) return -22;
    reist_audio_message_t wire;
    audio_message_init(&wire, REIST_AUDIO_COMMAND_OPEN,
                       audio_request_id(context), (reist_audio_stream_t){0});
    wire.payload.words[0] = format->sample_rate;
    wire.payload.words[1] = format->channels;
    wire.payload.words[2] = format->format;
    int result = audio_transact(context, &wire);
    if (result != 0) return result;
    if (wire.stream_id == 0U || wire.stream_generation == 0U) return -84;
    *stream = (reist_audio_stream_t){
        .id = wire.stream_id,
        .generation = wire.stream_generation,
    };
    return 0;
}

int reist_audio_write(reist_audio_context_t *context,
                      reist_audio_stream_t stream,
                      const int16_t *interleaved_samples,
                      uint32_t frame_count) {
    if (!audio_context_valid(context) || stream.id == 0U ||
        stream.generation == 0U || interleaved_samples == NULL ||
        frame_count == 0U || frame_count > REIST_AUDIO_MAX_STREAM_FRAMES)
        return -22;
    uint32_t completed = 0U;
    while (completed < frame_count) {
        uint32_t chunk = frame_count - completed;
        if (chunk > REIST_AUDIO_BULK_FRAMES)
            chunk = REIST_AUDIO_BULK_FRAMES;
        reist_audio_bulk_message_t wire;
        audio_zero(&wire, sizeof(wire));
        wire.version = REIST_AUDIO_PROTOCOL_VERSION;
        wire.struct_size = sizeof(wire);
        wire.command = REIST_AUDIO_COMMAND_WRITE_BULK;
        wire.request_id = audio_request_id(context);
        wire.stream_id = stream.id;
        wire.stream_generation = stream.generation;
        wire.frame_count = chunk;
        for (uint32_t index = 0U; index < chunk * REIST_AUDIO_CHANNELS;
             ++index)
            wire.payload.samples[index] =
                interleaved_samples[completed * REIST_AUDIO_CHANNELS + index];
        int result = audio_bulk_transact(context, &wire);
        if (result != 0)
            return completed != 0U ? (int)completed : result;
        completed += chunk;
    }
    return (int)completed;
}

static int audio_stream_command(reist_audio_context_t *context,
                                reist_audio_stream_t stream,
                                uint32_t command) {
    if (!audio_context_valid(context) || stream.id == 0U ||
        stream.generation == 0U) return -22;
    reist_audio_message_t wire;
    audio_message_init(&wire, command, audio_request_id(context), stream);
    return audio_transact(context, &wire);
}

int reist_audio_start(reist_audio_context_t *context,
                      reist_audio_stream_t stream) {
    return audio_stream_command(context, stream, REIST_AUDIO_COMMAND_START);
}

int reist_audio_stop(reist_audio_context_t *context,
                     reist_audio_stream_t stream) {
    return audio_stream_command(context, stream, REIST_AUDIO_COMMAND_STOP);
}

int reist_audio_close(reist_audio_context_t *context,
                      reist_audio_stream_t *stream) {
    if (stream == NULL) return -22;
    int result = audio_stream_command(
        context, *stream, REIST_AUDIO_COMMAND_CLOSE);
    if (result == 0) *stream = (reist_audio_stream_t){0};
    return result;
}
