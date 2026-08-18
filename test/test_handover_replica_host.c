/**
 * @file test/test_handover_replica_host.c
 * @brief Hostseitiger Regressionstest für handover replica.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "include/kernel/handover_replica.h"

int main(void) {
    handover_replica_state_t state = {
        .version = HANDOVER_REPLICA_VERSION,
        .struct_size = sizeof(state),
        .source_node = 1U,
        .service_id = HANDOVER_REPLICA_SERVICE_TEST,
        .epoch = 1U,
        .sequence = 1U,
        .value = 10U,
    };
    if (handover_replica_init(&state) != 0) return 1;
    if (handover_replica_snapshot(&state) != 0 || state.value != 10U)
        return 2;
    state.sequence = 3U;
    if (handover_replica_apply(&state) >= 0) return 3;
    state.sequence = 2U;
    state.value = 20U;
    if (handover_replica_apply(&state) != 0) return 4;
    if (handover_replica_apply(&state) >= 0) return 5;
    state.sequence = 3U;
    state.epoch = 2U;
    if (handover_replica_apply(&state) >= 0) return 6;
    if (handover_replica_promote(1U, 2U, 30U) >= 0) return 7;
    if (handover_replica_promote(2U, 2U, 30U) != 0) return 8;
    if (handover_replica_snapshot(&state) != 0 || state.source_node != 2U ||
        state.epoch != 2U || state.sequence != 3U || state.value != 30U)
        return 9;
    if (handover_replica_test_corrupt(false) != 0 ||
        handover_replica_snapshot(&state) != 0) return 10;
    if (handover_replica_test_corrupt(true) != 0 ||
        handover_replica_snapshot(&state) >= 0) return 11;
    return 0;
}
