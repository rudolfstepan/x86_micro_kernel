#include "include/kernel/handover_serial_backend.h"

/* The frame-only host test does not exercise port I/O. */
uint64_t pit_monotonic_ms(void) { return 0U; }

int main(void) {
    handover_serial_frame_t frame;
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
    return 0;
}
