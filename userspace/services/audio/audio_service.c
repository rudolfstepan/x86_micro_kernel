/**
 * @file audio_service.c
 * @brief Supervised Ring-3 PCM policy and session service.
 *
 * Applications connect to this endpoint, never to the HDA driver.  The
 * service validates fixed-format PCM requests, assigns generation-scoped
 * stream identities and relays only bounded protocol messages to the driver.
 */
#include <stddef.h>
#include <stdint.h>

#include "reist/audio.h"
#include "x86os.h"

#define AUDIO_SERVICE_CONNECT_ATTEMPTS 200U
#define AUDIO_SERVICE_CONNECT_DELAY_MS 10U
#define AUDIO_SERVICE_RECEIVE_MS 40U
#define AUDIO_SERVICE_SEND_MS 100U
#define AUDIO_SERVICE_DRIVER_MS 500U
#define AUDIO_SERVICE_HEARTBEAT_MS 500U
#define AUDIO_DIAGNOSTIC_STAGE_CONNECT 1U
#define AUDIO_DIAGNOSTIC_STAGE_DRIVER_SELF_TEST 2U
#define AUDIO_DIAGNOSTIC_STAGE_CLIENT_ENDPOINT 3U
#define AUDIO_DIAGNOSTIC_STAGE_SELF_TEST_REPORT 4U
#define AUDIO_DIAGNOSTIC_STAGE_PROGRESS_REPORT 5U
#define AUDIO_DIAGNOSTIC_STAGE_READY_REPORT 6U

typedef struct {
    x86os_ipc_handle_t client_endpoint;
    x86os_ipc_handle_t driver_endpoint;
    uint32_t next_generation;
    uint32_t stream_id;
    uint32_t stream_generation;
    uint32_t buffered_frames;
    uint32_t backend_state;
    uint32_t progress;
    uint32_t driver_failed;
} audio_service_t;

static void bytes_zero(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static int service_failure(uint32_t stage, int status) {
    uint32_t error = status < 0 ? (uint32_t)(-status) : (uint32_t)status;
    uint32_t diagnostic = (stage << 24U) | (error & 0x00FFFFFFU);
    (void)x86os_reist_report(X86OS_REIST_REPORT_DIAGNOSTIC, diagnostic);
    return status;
}

static int ipc_decode(const x86os_ipc_message_t *ipc,
                      reist_audio_message_t *wire) {
    if (ipc == NULL || wire == NULL ||
        ipc->version != X86OS_IPC_MESSAGE_VERSION ||
        ipc->struct_size != sizeof(*ipc) || ipc->length != sizeof(*wire))
        return -84;
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        ((uint8_t *)wire)[index] = ipc->payload[index];
    return 0;
}

static void ipc_encode(x86os_ipc_message_t *ipc,
                       const reist_audio_message_t *wire) {
    bytes_zero(ipc, sizeof(*ipc));
    ipc->version = X86OS_IPC_MESSAGE_VERSION;
    ipc->struct_size = sizeof(*ipc);
    ipc->length = sizeof(*wire);
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        ipc->payload[index] = ((const uint8_t *)wire)[index];
}

static void ipc_receive_prepare(x86os_ipc_message_t *ipc) {
    bytes_zero(ipc, sizeof(*ipc));
    ipc->version = X86OS_IPC_MESSAGE_VERSION;
    ipc->struct_size = sizeof(*ipc);
}

static void response_prepare(reist_audio_message_t *response,
                             const reist_audio_message_t *request,
                             int status) {
    bytes_zero(response, sizeof(*response));
    response->version = REIST_AUDIO_PROTOCOL_VERSION;
    response->struct_size = sizeof(*response);
    response->command = request->command | REIST_AUDIO_RESPONSE_FLAG;
    response->request_id = request->request_id;
    response->stream_id = request->stream_id;
    response->stream_generation = request->stream_generation;
    response->status = status;
}

static int driver_transact(audio_service_t *service,
                           reist_audio_message_t *wire) {
    uint32_t expected_command = wire->command;
    uint32_t expected_request_id = wire->request_id;
    x86os_ipc_message_t ipc;
    ipc_encode(&ipc, wire);
    int result = x86os_ipc_send_timeout(
        service->driver_endpoint, &ipc, AUDIO_SERVICE_DRIVER_MS);
    if (result == 0)
        result = x86os_ipc_receive_timeout(
            service->driver_endpoint, &ipc, AUDIO_SERVICE_DRIVER_MS);
    if (result != 0 || ipc_decode(&ipc, wire) != 0) {
        service->driver_failed = 1U;
        return result != 0 ? result : -84;
    }
    if (wire->version != REIST_AUDIO_PROTOCOL_VERSION ||
        wire->struct_size != sizeof(*wire) ||
        wire->command != (expected_command | REIST_AUDIO_RESPONSE_FLAG) ||
        wire->request_id != expected_request_id)
        service->driver_failed = 1U;
    return service->driver_failed != 0U ? -84 : wire->status;
}

static int stream_matches(const audio_service_t *service,
                          const reist_audio_message_t *request) {
    return service->stream_id != 0U &&
        request->stream_id == service->stream_id &&
        request->stream_generation == service->stream_generation;
}

static int service_handle(audio_service_t *service,
                          const reist_audio_message_t *request,
                          reist_audio_message_t *response) {
    response_prepare(response, request, -22);
    if (request->version != REIST_AUDIO_PROTOCOL_VERSION ||
        request->struct_size != sizeof(*request) || request->request_id == 0U ||
        (request->command & REIST_AUDIO_RESPONSE_FLAG) != 0U) return -22;

    if (request->command == REIST_AUDIO_COMMAND_INFO) {
        *response = *request;
        int result = driver_transact(service, response);
        if (result != 0) response_prepare(response, request, result);
        return result;
    }

    if (request->command == REIST_AUDIO_COMMAND_OPEN) {
        if (service->stream_id != 0U) return -16;
        if (request->payload.words[0] != REIST_AUDIO_SAMPLE_RATE ||
            request->payload.words[1] != REIST_AUDIO_CHANNELS ||
            request->payload.words[2] != REIST_AUDIO_FORMAT_S16_LE)
            return -22;
        if (service->next_generation == UINT32_MAX) return -75;
        reist_audio_message_t relay = *request;
        relay.stream_id = 1U;
        relay.stream_generation = service->next_generation;
        int result = driver_transact(service, &relay);
        if (result == 0) {
            service->stream_id = relay.stream_id;
            service->stream_generation = relay.stream_generation;
            ++service->next_generation;
            service->buffered_frames = 0U;
            service->backend_state = REIST_AUDIO_BACKEND_BUFFERING;
            *response = relay;
        } else {
            response_prepare(response, request, result);
        }
        return result;
    }

    if (!stream_matches(service, request)) return -9;
    reist_audio_message_t relay = *request;
    int result = -22;
    if (request->command == REIST_AUDIO_COMMAND_WRITE) {
        if (service->backend_state != REIST_AUDIO_BACKEND_BUFFERING ||
            request->frame_count == 0U ||
            request->frame_count > REIST_AUDIO_MESSAGE_FRAMES) return -22;
        if (service->buffered_frames > REIST_AUDIO_MAX_STREAM_FRAMES -
                request->frame_count) return -11;
        result = driver_transact(service, &relay);
        if (result == 0) service->buffered_frames += request->frame_count;
    } else if (request->command == REIST_AUDIO_COMMAND_START) {
        if (service->backend_state != REIST_AUDIO_BACKEND_BUFFERING ||
            service->buffered_frames == 0U) return -16;
        result = driver_transact(service, &relay);
        if (result == 0) service->backend_state = REIST_AUDIO_BACKEND_RUNNING;
    } else if (request->command == REIST_AUDIO_COMMAND_STOP) {
        if (service->backend_state != REIST_AUDIO_BACKEND_RUNNING) return -16;
        result = driver_transact(service, &relay);
        if (result == 0) service->backend_state = REIST_AUDIO_BACKEND_READY;
    } else if (request->command == REIST_AUDIO_COMMAND_CLOSE) {
        if (service->backend_state == REIST_AUDIO_BACKEND_RUNNING) return -16;
        result = driver_transact(service, &relay);
        if (result == 0) {
            service->stream_id = 0U;
            service->stream_generation = 0U;
            service->buffered_frames = 0U;
            service->backend_state = REIST_AUDIO_BACKEND_READY;
        }
    }
    if (result == 0) *response = relay;
    else response_prepare(response, request, result);
    return result;
}

static int connect_driver(audio_service_t *service) {
    for (uint32_t attempt = 0U; attempt < AUDIO_SERVICE_CONNECT_ATTEMPTS;
         ++attempt) {
        x86os_ipc_handle_t handle = X86OS_IPC_INVALID_HANDLE;
        int result = x86os_service_connect(
            X86OS_SERVICE_AUDIO_DRIVER_INTERNAL, &handle);
        if (result == 0) {
            service->driver_endpoint = handle;
            return 0;
        }
        if (x86os_sleep_ms(AUDIO_SERVICE_CONNECT_DELAY_MS) != 0)
            (void)x86os_yield();
    }
    return -110;
}

static int driver_self_test(audio_service_t *service) {
    reist_audio_message_t request;
    bytes_zero(&request, sizeof(request));
    request.version = REIST_AUDIO_PROTOCOL_VERSION;
    request.struct_size = sizeof(request);
    request.command = REIST_AUDIO_COMMAND_INFO;
    request.request_id = 1U;
    int result = driver_transact(service, &request);
    if (result != 0) return result;
    return request.payload.words[0] == REIST_AUDIO_SAMPLE_RATE &&
        request.payload.words[1] == REIST_AUDIO_CHANNELS &&
        request.payload.words[2] == REIST_AUDIO_FORMAT_S16_LE ? 0 : -84;
}

static void abandon_client_stream(audio_service_t *service) {
    if (service->stream_id == 0U || service->driver_failed != 0U) return;
    reist_audio_message_t request;
    bytes_zero(&request, sizeof(request));
    request.version = REIST_AUDIO_PROTOCOL_VERSION;
    request.struct_size = sizeof(request);
    request.request_id = 0xFFFFFFFEU;
    request.stream_id = service->stream_id;
    request.stream_generation = service->stream_generation;
    if (service->backend_state == REIST_AUDIO_BACKEND_RUNNING) {
        request.command = REIST_AUDIO_COMMAND_STOP;
        (void)driver_transact(service, &request);
    }
    if (service->driver_failed == 0U) {
        request.command = REIST_AUDIO_COMMAND_CLOSE;
        (void)driver_transact(service, &request);
    }
    service->stream_id = 0U;
    service->stream_generation = 0U;
    service->buffered_frames = 0U;
    service->backend_state = REIST_AUDIO_BACKEND_READY;
}

int main(void) {
    audio_service_t service;
    bytes_zero(&service, sizeof(service));
    service.next_generation = 1U;
    service.backend_state = REIST_AUDIO_BACKEND_READY;
    service.progress = 2U;
    int status = connect_driver(&service);
    if (status != 0)
        return service_failure(AUDIO_DIAGNOSTIC_STAGE_CONNECT, status);
    status = driver_self_test(&service);
    if (status != 0)
        return service_failure(
            AUDIO_DIAGNOSTIC_STAGE_DRIVER_SELF_TEST, status);
    status = x86os_ipc_create(&service.client_endpoint);
    if (status != 0)
        return service_failure(
            AUDIO_DIAGNOSTIC_STAGE_CLIENT_ENDPOINT, status);
    status = x86os_reist_report(
        X86OS_REIST_REPORT_SELF_TEST, service.client_endpoint);
    if (status != 0)
        return service_failure(
            AUDIO_DIAGNOSTIC_STAGE_SELF_TEST_REPORT, status);
    status = x86os_reist_report(X86OS_REIST_REPORT_PROGRESS, 1U);
    if (status != 0)
        return service_failure(
            AUDIO_DIAGNOSTIC_STAGE_PROGRESS_REPORT, status);
    status = x86os_reist_report(X86OS_REIST_REPORT_SERVICE_READY, 1U);
    if (status != 0)
        return service_failure(
            AUDIO_DIAGNOSTIC_STAGE_READY_REPORT, status);

    uint64_t last_report = 0U;
    (void)x86os_monotonic_ms(&last_report);
    for (;;) {
        x86os_ipc_message_t ipc;
        ipc_receive_prepare(&ipc);
        int received = x86os_ipc_receive_timeout(
            service.client_endpoint, &ipc, AUDIO_SERVICE_RECEIVE_MS);
        if (received == 0) {
            reist_audio_message_t request;
            reist_audio_message_t response;
            if (ipc_decode(&ipc, &request) == 0) {
                int status = service_handle(&service, &request, &response);
                response.status = status;
                if (response.version == 0U)
                    response_prepare(&response, &request, status);
                ipc_encode(&ipc, &response);
                (void)x86os_ipc_send_timeout(
                    service.client_endpoint, &ipc, AUDIO_SERVICE_SEND_MS);
            }
        } else if (received == -32) {
            abandon_client_stream(&service);
            if (x86os_sleep_ms(1U) != 0) (void)x86os_yield();
        }
        if (service.driver_failed != 0U) return 2;
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) == 0 &&
            now - last_report >= AUDIO_SERVICE_HEARTBEAT_MS) {
            if (service.progress == 0U) service.progress = 1U;
            if (x86os_reist_report(
                    X86OS_REIST_REPORT_PROGRESS, service.progress++) != 0)
                return 3;
            last_report = now;
        }
    }
}
