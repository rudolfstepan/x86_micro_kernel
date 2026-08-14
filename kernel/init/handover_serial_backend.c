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
        (type != HANDOVER_SERIAL_REQUEST && type != HANDOVER_SERIAL_ACK) ||
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
