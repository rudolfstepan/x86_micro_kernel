#ifndef KERNEL_STORAGE_HANDOVER_H
#define KERNEL_STORAGE_HANDOVER_H

#include <stdbool.h>
#include <stdint.h>

#include "include/kernel/handover_replica.h"

bool storage_handover_init(void);
bool storage_handover_hold(void);
bool storage_handover_is_held(void);
bool storage_handover_snapshot(uint32_t source_node, uint64_t epoch,
                               uint64_t sequence,
                               handover_replica_state_t *state_out);
bool storage_handover_validate(const handover_replica_state_t *state);
bool storage_handover_release(const handover_replica_state_t *state);

#endif
