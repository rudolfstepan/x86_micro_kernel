#include "include/kernel/handover_serial_backend.h"

/* The frame-only host test does not exercise port I/O. */
uint64_t pit_monotonic_ms(void) { return 0U; }

int main(void) {
    handover_serial_frame_t frame;
    handover_serial_state_frame_t state_frame;
    handover_replica_state_t state = {
        .version = HANDOVER_REPLICA_VERSION,
        .struct_size = sizeof(state),
        .source_node = 1U,
        .service_id = HANDOVER_REPLICA_SERVICE_TEST,
        .epoch = 2U,
        .sequence = 9U,
        .value = 42U,
    };
    handover_replica_state_t decoded;
    if (!handover_serial_frame_build(&frame, HANDOVER_SERIAL_REQUEST,
                                     7U, 0x1122334455667788ULL)) return 1;
    if (!handover_serial_frame_valid(&frame, HANDOVER_SERIAL_REQUEST,
                                     7U, 0x1122334455667788ULL)) return 2;
    if (handover_serial_frame_valid(&frame, HANDOVER_SERIAL_ACK,
                                    7U, 0x1122334455667788ULL)) return 3;
    frame.epoch ^= 1U;
    if (handover_serial_frame_valid(&frame, HANDOVER_SERIAL_REQUEST,
                                    7U, 0x1122334455667788ULL)) return 4;
    if (!handover_serial_frame_build(&frame, HANDOVER_SERIAL_ACK, 7U, 9U))
        return 5;
    frame.crc32 ^= 1U;
    if (handover_serial_frame_valid(&frame, HANDOVER_SERIAL_ACK, 7U, 9U))
        return 6;
    if (handover_serial_frame_build(&frame, 99U, 7U, 9U)) return 7;
    if (handover_serial_frame_build(&frame, HANDOVER_SERIAL_ACK, 0U, 9U))
        return 8;
    if (!handover_serial_frame_build(&frame, HANDOVER_SERIAL_REPLICA,
                                     1U, 2U) ||
        !handover_serial_frame_valid(&frame, HANDOVER_SERIAL_REPLICA,
                                     1U, 2U)) return 9;
    if (!handover_serial_frame_build(&frame, HANDOVER_SERIAL_READY, 2U, 2U) ||
        !handover_serial_frame_valid(&frame, HANDOVER_SERIAL_READY, 2U, 2U))
        return 10;
    if (!handover_serial_state_frame_build(&state_frame, &state) ||
        !handover_serial_state_frame_valid(&state_frame, &decoded) ||
        decoded.source_node != 1U || decoded.epoch != 2U ||
        decoded.sequence != 9U || decoded.value != 42U) return 11;
    state_frame.crc32 ^= 1U;
    if (handover_serial_state_frame_valid(&state_frame, &decoded)) return 12;
    return 0;
}
