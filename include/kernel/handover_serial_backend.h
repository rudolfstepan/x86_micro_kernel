#ifndef KERNEL_HANDOVER_SERIAL_BACKEND_H
#define KERNEL_HANDOVER_SERIAL_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "include/kernel/handover.h"

#define HANDOVER_SERIAL_MAGIC 0x54464952U
#define HANDOVER_SERIAL_VERSION 1U
#define HANDOVER_SERIAL_REQUEST 1U
#define HANDOVER_SERIAL_ACK 2U
#define HANDOVER_SERIAL_FRAME_SIZE 24U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t frame_size;
    uint32_t active_node;
    uint64_t epoch;
    uint32_t crc32;
} handover_serial_frame_t;

bool handover_serial_frame_build(handover_serial_frame_t *frame,
                                 uint8_t type, uint32_t active_node,
                                 uint64_t epoch);
bool handover_serial_frame_valid(const handover_serial_frame_t *frame,
                                 uint8_t expected_type,
                                 uint32_t expected_active_node,
                                 uint64_t expected_epoch);
bool handover_serial_backend_init(void);
const handover_fence_backend_t *handover_serial_backend(void);

#endif
