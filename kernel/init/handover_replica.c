/**
 * @file kernel/init/handover_replica.c
 * @brief Validiert und speichert replizierten Minimalzustand für Handover.
 *
 * Layer: Ring-0 replication service.
 * Contract: Nur passende Versionen, Quellen und steigende Sequenzen werden publiziert.
 * Safety: CRC- oder Replayfehler verändern den aktiven Zustand nicht.
 */
#include "include/kernel/handover_replica.h"

#include <stddef.h>

#include "include/kernel/critical_object.h"

#ifndef REIST_HOST_TEST
#include "arch/x86/include/interrupt.h"
#include "include/lib/spinlock.h"
static spinlock_t replica_state_lock = SPINLOCK_INIT;
#endif

#define REPLICA_EINVAL (-22)
#define REPLICA_ESTALE (-116)
#define REPLICA_EINTEGRITY (-117)
#define REPLICA_EOVERFLOW (-75)

static critical_object_t protected_replica;
static bool initialized;

_Static_assert(sizeof(handover_replica_state_t) <=
               CRITICAL_OBJECT_MAX_PAYLOAD,
               "handover replica exceeds protected payload");

static uint32_t replica_lock(void) {
#ifdef REIST_HOST_TEST
    return 0U;
#else
    return spinlock_acquire_irq(&replica_state_lock);
#endif
}

static void replica_unlock(uint32_t flags) {
#ifdef REIST_HOST_TEST
    (void)flags;
#else
    spinlock_release_irq(&replica_state_lock, flags);
#endif
}

static bool replica_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(handover_replica_state_t))
        return false;
    const handover_replica_state_t *state = payload;
    return state->version == HANDOVER_REPLICA_VERSION &&
        state->struct_size == sizeof(*state) && state->source_node != 0U &&
        state->service_id != 0U && state->epoch != 0U &&
        state->sequence != 0U && state->reserved == 0U;
}

static int read_state(handover_replica_state_t *state) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(&protected_replica,
        HANDOVER_REPLICA_VERSION, state, sizeof(*state), &length,
        replica_valid);
    return result >= CRITICAL_READ_OK && length == sizeof(*state)
        ? 0 : REPLICA_EINTEGRITY;
}

static int write_state(const handover_replica_state_t *state) {
    return critical_object_update(&protected_replica,
        HANDOVER_REPLICA_VERSION, state, sizeof(*state), replica_valid) == 0
        ? 0 : REPLICA_EINTEGRITY;
}

int handover_replica_init(const handover_replica_state_t *initial) {
    if (initialized || !replica_valid(initial, sizeof(*initial)))
        return REPLICA_EINVAL;
    uint32_t flags = replica_lock();
    int result = critical_object_init(&protected_replica,
        HANDOVER_REPLICA_VERSION, initial, sizeof(*initial)) == 0
        ? 0 : REPLICA_EINTEGRITY;
    if (result == 0) initialized = true;
    replica_unlock(flags);
    return result;
}

int handover_replica_apply(const handover_replica_state_t *next) {
    if (!initialized || !replica_valid(next, sizeof(*next)))
        return REPLICA_EINVAL;
    uint32_t flags = replica_lock();
    handover_replica_state_t current;
    int result = read_state(&current);
    if (result == 0 && (next->source_node != current.source_node ||
                       next->service_id != current.service_id ||
                       next->epoch != current.epoch ||
                       current.sequence == UINT64_MAX ||
                       next->sequence != current.sequence + 1U))
        result = REPLICA_ESTALE;
    if (result == 0) result = write_state(next);
    replica_unlock(flags);
    return result;
}

int handover_replica_promote(uint32_t new_source_node, uint64_t new_epoch,
                             uint32_t value) {
    if (!initialized || new_source_node == 0U || new_epoch == 0U)
        return REPLICA_EINVAL;
    uint32_t flags = replica_lock();
    handover_replica_state_t state;
    int result = read_state(&state);
    if (result == 0 && (state.epoch == UINT64_MAX ||
                       state.sequence == UINT64_MAX))
        result = REPLICA_EOVERFLOW;
    if (result == 0 && (new_source_node == state.source_node ||
                       new_epoch != state.epoch + 1U))
        result = REPLICA_ESTALE;
    if (result == 0) {
        state.source_node = new_source_node;
        state.epoch = new_epoch;
        ++state.sequence;
        state.value = value;
        result = write_state(&state);
    }
    replica_unlock(flags);
    return result;
}

int handover_replica_snapshot(handover_replica_state_t *state_out) {
    if (!initialized || state_out == NULL) return REPLICA_EINVAL;
    uint32_t flags = replica_lock();
    int result = read_state(state_out);
    replica_unlock(flags);
    return result;
}

#ifdef REIST_HOST_TEST
int handover_replica_test_corrupt(bool both_copies) {
    if (!initialized) return REPLICA_EINVAL;
    protected_replica.primary.crc32 ^= 1U;
    if (both_copies) protected_replica.shadow.crc32 ^= 2U;
    return 0;
}
#endif
