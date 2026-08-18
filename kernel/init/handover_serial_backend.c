/**
 * @file kernel/init/handover_serial_backend.c
 * @brief Transportiert Handover-Frames über einen begrenzten seriellen Kanal.
 *
 * Layer: Ring-0 platform backend.
 * Contract: I/O folgt festen Frameformaten und monotonen Deadlines.
 * Safety: Teilübertragung oder ungültige CRC erteilt keine Takeover-Freigabe.
 */
#include "include/kernel/handover_serial_backend.h"

#include <stddef.h>

#include "drivers/char/io.h"
#include "drivers/char/serial.h"
#include "kernel/time/pit.h"

#define UART_DATA 0U
#define UART_INTERRUPT_ENABLE 1U
#define UART_FIFO_CONTROL 2U
#define UART_LINE_CONTROL 3U
#define UART_MODEM_CONTROL 4U
#define UART_LINE_STATUS 5U
#define UART_DATA_READY 0x01U
#define UART_TRANSMIT_EMPTY 0x20U
#define UART_IO_TIMEOUT_MS 1000U
#define UART_MAX_POLLS 10000000U

typedef struct {
    bool initialized;
    bool request_pending;
    uint32_t requested_node;
    uint64_t requested_epoch;
} serial_fence_context_t;

static serial_fence_context_t serial_context;

_Static_assert(sizeof(handover_serial_frame_t) == HANDOVER_SERIAL_FRAME_SIZE,
               "handover serial frame ABI drift");
_Static_assert(sizeof(handover_serial_state_frame_t) ==
               HANDOVER_SERIAL_STATE_FRAME_SIZE,
               "handover serial state frame ABI drift");

static uint32_t frame_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFU;
}

bool handover_serial_frame_build(handover_serial_frame_t *frame,
                                 uint8_t type, uint32_t active_node,
                                 uint64_t epoch) {
    if (frame == NULL ||
        (type != HANDOVER_SERIAL_REQUEST && type != HANDOVER_SERIAL_ACK &&
         type != HANDOVER_SERIAL_REPLICA && type != HANDOVER_SERIAL_READY) ||
        active_node == 0U || epoch == 0U) return false;
    *frame = (handover_serial_frame_t) {
        .magic = HANDOVER_SERIAL_MAGIC,
        .version = HANDOVER_SERIAL_VERSION,
        .type = type,
        .frame_size = sizeof(*frame),
        .active_node = active_node,
        .epoch = epoch,
    };
    frame->crc32 = frame_crc32((const uint8_t *)frame,
                               offsetof(handover_serial_frame_t, crc32));
    return true;
}

bool handover_serial_frame_valid(const handover_serial_frame_t *frame,
                                 uint8_t expected_type,
                                 uint32_t expected_active_node,
                                 uint64_t expected_epoch) {
    if (frame == NULL || frame->magic != HANDOVER_SERIAL_MAGIC ||
        frame->version != HANDOVER_SERIAL_VERSION ||
        frame->type != expected_type || frame->frame_size != sizeof(*frame) ||
        frame->active_node != expected_active_node ||
        frame->epoch != expected_epoch) return false;
    return frame->crc32 == frame_crc32((const uint8_t *)frame,
                                  offsetof(handover_serial_frame_t, crc32));
}

bool handover_serial_state_frame_build(handover_serial_state_frame_t *frame,
                                       const handover_replica_state_t *state) {
    if (frame == NULL || state == NULL ||
        state->version != HANDOVER_REPLICA_VERSION ||
        state->struct_size != sizeof(*state) || state->source_node == 0U ||
        state->service_id == 0U || state->epoch == 0U ||
        state->sequence == 0U || state->reserved != 0U) return false;
    *frame = (handover_serial_state_frame_t) {
        .magic = HANDOVER_SERIAL_MAGIC,
        .version = HANDOVER_SERIAL_VERSION,
        .type = HANDOVER_SERIAL_STATE,
        .frame_size = sizeof(*frame),
        .state_version = state->version,
        .state_size = state->struct_size,
        .source_node = state->source_node,
        .service_id = state->service_id,
        .epoch = state->epoch,
        .sequence = state->sequence,
        .value = state->value,
        .reserved = state->reserved,
    };
    frame->crc32 = frame_crc32((const uint8_t *)frame,
        offsetof(handover_serial_state_frame_t, crc32));
    return true;
}

bool handover_serial_state_frame_valid(
    const handover_serial_state_frame_t *frame,
    handover_replica_state_t *state_out) {
    if (frame == NULL || state_out == NULL ||
        frame->magic != HANDOVER_SERIAL_MAGIC ||
        frame->version != HANDOVER_SERIAL_VERSION ||
        frame->type != HANDOVER_SERIAL_STATE ||
        frame->frame_size != sizeof(*frame) ||
        frame->state_version != HANDOVER_REPLICA_VERSION ||
        frame->state_size != sizeof(*state_out) || frame->source_node == 0U ||
        frame->service_id == 0U || frame->epoch == 0U ||
        frame->sequence == 0U || frame->reserved != 0U ||
        frame->crc32 != frame_crc32((const uint8_t *)frame,
            offsetof(handover_serial_state_frame_t, crc32))) return false;
    *state_out = (handover_replica_state_t) {
        .version = frame->state_version,
        .struct_size = frame->state_size,
        .source_node = frame->source_node,
        .service_id = frame->service_id,
        .epoch = frame->epoch,
        .sequence = frame->sequence,
        .value = frame->value,
        .reserved = frame->reserved,
    };
    return true;
}

static uint64_t io_deadline(void) {
    uint64_t now = pit_monotonic_ms();
    return UINT64_MAX - now < UART_IO_TIMEOUT_MS
        ? UINT64_MAX : now + UART_IO_TIMEOUT_MS;
}

static bool write_bytes(const uint8_t *bytes, size_t length) {
    uint64_t deadline = io_deadline();
    uint32_t polls_remaining = UART_MAX_POLLS;
    for (size_t index = 0U; index < length; ++index) {
        while ((inb(SERIAL_COM2 + UART_LINE_STATUS) &
                UART_TRANSMIT_EMPTY) == 0U) {
            if (polls_remaining-- == 0U ||
                pit_monotonic_ms() >= deadline) return false;
        }
        outb(SERIAL_COM2 + UART_DATA, bytes[index]);
    }
    return true;
}

static bool read_bytes(uint8_t *bytes, size_t length) {
    uint64_t deadline = io_deadline();
    uint32_t polls_remaining = UART_MAX_POLLS;
    for (size_t index = 0U; index < length; ++index) {
        while ((inb(SERIAL_COM2 + UART_LINE_STATUS) &
                UART_DATA_READY) == 0U) {
            if (polls_remaining-- == 0U ||
                pit_monotonic_ms() >= deadline) return false;
        }
        bytes[index] = inb(SERIAL_COM2 + UART_DATA);
    }
    return true;
}

static bool request_fence(void *opaque, uint32_t active_node,
                          uint64_t epoch) {
    serial_fence_context_t *context = opaque;
    handover_serial_frame_t request;
    if (context == NULL || !context->initialized ||
        !handover_serial_frame_build(&request, HANDOVER_SERIAL_REQUEST,
                                     active_node, epoch) ||
        !write_bytes((const uint8_t *)&request, sizeof(request))) return false;
    context->requested_node = active_node;
    context->requested_epoch = epoch;
    context->request_pending = true;
    return true;
}

static bool fence_confirmed(void *opaque, uint32_t active_node,
                            uint64_t epoch) {
    serial_fence_context_t *context = opaque;
    handover_serial_frame_t response;
    if (context == NULL || !context->initialized ||
        !context->request_pending || context->requested_node != active_node ||
        context->requested_epoch != epoch ||
        !read_bytes((uint8_t *)&response, sizeof(response)) ||
        !handover_serial_frame_valid(&response, HANDOVER_SERIAL_ACK,
                                     active_node, epoch)) return false;
    context->request_pending = false;
    return true;
}

static handover_fence_backend_t backend = {
    .request_fence = request_fence,
    .fence_confirmed = fence_confirmed,
    .context = &serial_context,
};

bool handover_serial_backend_init(void) {
    if (serial_context.initialized) return false;
    /* Dedicated COM2, 115200 8N1, FIFO enabled, polling only. */
    outb(SERIAL_COM2 + UART_INTERRUPT_ENABLE, 0x00U);
    outb(SERIAL_COM2 + UART_LINE_CONTROL, 0x80U);
    outb(SERIAL_COM2 + UART_DATA, 0x01U);
    outb(SERIAL_COM2 + UART_INTERRUPT_ENABLE, 0x00U);
    outb(SERIAL_COM2 + UART_LINE_CONTROL, 0x03U);
    outb(SERIAL_COM2 + UART_FIFO_CONTROL, 0xC7U);
    outb(SERIAL_COM2 + UART_MODEM_CONTROL, 0x0BU);
    serial_context.initialized = true;
    return true;
}

const handover_fence_backend_t *handover_serial_backend(void) {
    return serial_context.initialized ? &backend : NULL;
}

bool handover_serial_send_replica(uint32_t active_node, uint64_t epoch) {
    handover_serial_frame_t frame;
    return serial_context.initialized &&
        handover_serial_frame_build(&frame, HANDOVER_SERIAL_REPLICA,
                                    active_node, epoch) &&
        write_bytes((const uint8_t *)&frame, sizeof(frame));
}

bool handover_serial_send_ready(uint32_t standby_node, uint64_t epoch) {
    handover_serial_frame_t frame;
    return serial_context.initialized &&
        handover_serial_frame_build(&frame, HANDOVER_SERIAL_READY,
                                    standby_node, epoch) &&
        write_bytes((const uint8_t *)&frame, sizeof(frame));
}

bool handover_serial_receive_replica(uint32_t *active_node_out,
                                     uint64_t *epoch_out) {
    handover_serial_frame_t frame;
    if (!serial_context.initialized || active_node_out == NULL ||
        epoch_out == NULL || !read_bytes((uint8_t *)&frame, sizeof(frame)) ||
        !handover_serial_frame_valid(&frame, HANDOVER_SERIAL_REPLICA,
                                     frame.active_node, frame.epoch))
        return false;
    *active_node_out = frame.active_node;
    *epoch_out = frame.epoch;
    return true;
}

bool handover_serial_send_state(const handover_replica_state_t *state) {
    handover_serial_state_frame_t frame;
    return serial_context.initialized &&
        handover_serial_state_frame_build(&frame, state) &&
        write_bytes((const uint8_t *)&frame, sizeof(frame));
}

bool handover_serial_receive_state(handover_replica_state_t *state_out) {
    handover_serial_state_frame_t frame;
    return serial_context.initialized && state_out != NULL &&
        read_bytes((uint8_t *)&frame, sizeof(frame)) &&
        handover_serial_state_frame_valid(&frame, state_out);
}
