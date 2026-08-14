#ifndef KERNEL_HANDOVER_H
#define KERNEL_HANDOVER_H

#include <stdbool.h>
#include <stdint.h>

#define HANDOVER_PROTOCOL_VERSION 1U
#define HANDOVER_MIN_LEASE_MS 10U
#define HANDOVER_MAX_LEASE_MS 10000U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t active_node;
    uint32_t standby_node;
    uint32_t lease_ms;
    uint32_t reserved;
    uint64_t epoch;
    uint64_t lease_deadline_ms;
    uint64_t fenced_epoch;
    uint64_t transition_sequence;
} handover_status_t;

typedef struct {
    bool (*request_fence)(void *context, uint32_t active_node,
                          uint64_t epoch);
    bool (*fence_confirmed)(void *context, uint32_t active_node,
                            uint64_t epoch);
    void *context;
} handover_fence_backend_t;

int handover_attach_fence_backend(const handover_fence_backend_t *backend);
int handover_init(uint32_t active_node, uint32_t standby_node,
                  uint32_t lease_ms, uint64_t now_ms);
int handover_snapshot(handover_status_t *status_out);
int handover_renew(uint32_t node, uint64_t expected_epoch, uint64_t now_ms);
int handover_request_fence(uint64_t expected_epoch, uint64_t now_ms);
int handover_confirm_fenced(uint64_t expected_epoch, uint64_t now_ms);
int handover_takeover(uint32_t candidate_node, uint64_t expected_epoch,
                      uint64_t now_ms);

#ifdef REIST_HOST_TEST
int handover_test_corrupt(bool both_copies);
#endif

#endif
