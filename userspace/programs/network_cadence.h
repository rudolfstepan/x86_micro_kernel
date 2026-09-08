/* Private Ring-3 service policy, not a scheduler or public ABI. */
#ifndef REIST_NETWORK_CADENCE_H
#define REIST_NETWORK_CADENCE_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint64_t last_ms, active_until_ms; } network_cadence_t;
static inline uint32_t network_control_wait(network_cadence_t *state,
                                             uint64_t now, bool received) {
    if (now < state->last_ms) {
        state->active_until_ms = 0;
        state->last_ms = now;
        return 40U;
    }
    state->last_ms = now;
    if (received)
        state->active_until_ms = UINT64_MAX - now < 100U ? UINT64_MAX : now + 100U;
    return now < state->active_until_ms ? 1U : 40U;
}
#endif
