/**
 * @file include/kernel/handover_replica.h
 * @brief Versionierte Replikation des minimalen Zustands für System-Handover.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Frames tragen Quelle, Dienst, Epoche, Sequenz und Integritätsschutz.
 * Safety: Nur neuere valide Zustände fester Größe werden übernommen.
 */
#ifndef KERNEL_HANDOVER_REPLICA_H
#define KERNEL_HANDOVER_REPLICA_H

#include <stdbool.h>
#include <stdint.h>

#define HANDOVER_REPLICA_VERSION 1U
#define HANDOVER_REPLICA_SERVICE_TEST 1U
#define HANDOVER_REPLICA_SERVICE_STORAGE 2U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t source_node;
    uint32_t service_id;
    uint64_t epoch;
    uint64_t sequence;
    uint32_t value;
    uint32_t reserved;
} handover_replica_state_t;

int handover_replica_init(const handover_replica_state_t *initial);
int handover_replica_apply(const handover_replica_state_t *next);
int handover_replica_promote(uint32_t new_source_node, uint64_t new_epoch,
                             uint32_t value);
int handover_replica_snapshot(handover_replica_state_t *state_out);

#ifdef REIST_HOST_TEST
int handover_replica_test_corrupt(bool both_copies);
#endif

#endif
