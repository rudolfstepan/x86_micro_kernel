/**
 * @file include/kernel/handover_serial_backend.h
 * @brief Begrenztes serielles Transportbackend für Fence- und Handover-Nachweise.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Frames besitzen feste Version, Größe, Typ und CRC; I/O folgt Deadlines.
 * Safety: Teilframes, Timeout und Integritätsfehler erteilen keine Takeover-Freigabe.
 */
#ifndef KERNEL_HANDOVER_SERIAL_BACKEND_H
#define KERNEL_HANDOVER_SERIAL_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "include/kernel/handover.h"
#include "include/kernel/handover_replica.h"

#define HANDOVER_SERIAL_MAGIC 0x54464952U
#define HANDOVER_SERIAL_VERSION 1U
#define HANDOVER_SERIAL_REQUEST 1U
#define HANDOVER_SERIAL_ACK 2U
#define HANDOVER_SERIAL_REPLICA 3U
#define HANDOVER_SERIAL_READY 4U
#define HANDOVER_SERIAL_STATE 5U
#define HANDOVER_SERIAL_FRAME_SIZE 24U
#define HANDOVER_SERIAL_STATE_FRAME_SIZE 52U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t frame_size;
    uint32_t active_node;
    uint64_t epoch;
    uint32_t crc32;
} handover_serial_frame_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t frame_size;
    uint32_t state_version;
    uint32_t state_size;
    uint32_t source_node;
    uint32_t service_id;
    uint64_t epoch;
    uint64_t sequence;
    uint32_t value;
    uint32_t reserved;
    uint32_t crc32;
} handover_serial_state_frame_t;

bool handover_serial_frame_build(handover_serial_frame_t *frame,
                                 uint8_t type, uint32_t active_node,
                                 uint64_t epoch);
bool handover_serial_frame_valid(const handover_serial_frame_t *frame,
                                 uint8_t expected_type,
                                 uint32_t expected_active_node,
                                 uint64_t expected_epoch);
bool handover_serial_backend_init(void);
const handover_fence_backend_t *handover_serial_backend(void);
bool handover_serial_send_replica(uint32_t active_node, uint64_t epoch);
bool handover_serial_send_ready(uint32_t standby_node, uint64_t epoch);
bool handover_serial_receive_replica(uint32_t *active_node_out,
                                     uint64_t *epoch_out);
bool handover_serial_send_state(const handover_replica_state_t *state);
bool handover_serial_receive_state(handover_replica_state_t *state_out);
bool handover_serial_state_frame_build(handover_serial_state_frame_t *frame,
                                       const handover_replica_state_t *state);
bool handover_serial_state_frame_valid(
    const handover_serial_state_frame_t *frame,
    handover_replica_state_t *state_out);

#endif
