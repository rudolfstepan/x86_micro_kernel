#ifndef KERNEL_SUPERVISOR_H
#define KERNEL_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>
#include "include/kernel/critical_object.h"

struct Process;

#define SUPERVISOR_MAX_DOMAINS 8U
#define SUPERVISOR_NAME_CAPACITY 16U
#define SUPERVISOR_STATE_VERSION 1U
#define SUPERVISOR_FENCE_OPS_VERSION 1U
#define SUPERVISOR_DESCRIPTOR_VERSION 1U
#define SUPERVISOR_NETWORK_DEGRADATION_VERSION 1U
#define SUPERVISOR_PROBE_AUTHORITY_VERSION 1U
#define SUPERVISOR_NETWORK_CONTEXT_VERSION 1U
#define SUPERVISOR_EINTEGRITY (-84)
#define REIST_REPORT_SELF_TEST 1U
#define REIST_REPORT_PROGRESS 2U
#define REIST_REPORT_INVALID 3U
#define REIST_REPORT_NETWORK_HEADER 4U
#define REIST_REPORT_NETWORK_PROBE_ID 5U
#define REIST_REPORT_NETWORK_DEGRADED 6U
#define REIST_SERVICE_DIAGNOSTIC 1U

typedef enum {
    SUPERVISOR_STARTING = 1,
    SUPERVISOR_HEALTHY = 2,
    SUPERVISOR_DEGRADED = 3,
    SUPERVISOR_RECOVERING = 4,
    SUPERVISOR_FAILED = 5,
    SUPERVISOR_ISOLATED = 6,
    SUPERVISOR_SAFE_STATE = 7,
    SUPERVISOR_FENCING = 8,
    SUPERVISOR_IDLE = 9,
} supervisor_health_state_t;

typedef enum {
    SUPERVISOR_EVENT_NONE = 0,
    SUPERVISOR_EVENT_FENCE_REQUIRED = 1,
    SUPERVISOR_EVENT_RESTART_REQUIRED = 2,
    SUPERVISOR_EVENT_SAFE_STATE_REQUIRED = 3,
} supervisor_event_type_t;

typedef struct {
    uint32_t slot;
    uint32_t generation;
    uint32_t epoch;
} supervisor_handle_t;

typedef struct {
    uint32_t heartbeat_timeout_ms;
    uint32_t recovery_timeout_ms;
    uint32_t restart_budget;
} supervisor_config_t;

typedef bool (*supervisor_fence_fn_t)(void *context);

typedef struct {
    supervisor_fence_fn_t apply;
    supervisor_fence_fn_t verify;
    void *context;
} supervisor_fence_ops_t;

typedef struct {
    supervisor_event_type_t type;
    supervisor_handle_t handle;
} supervisor_event_t;

typedef struct {
    uint64_t next_id;
    uint64_t deadline_ms;
    uint32_t active_id;
} supervisor_probe_authority_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_probe_authority_t;

typedef struct {
    uint32_t delivered_id;
    uint32_t gateway;
    uint32_t local_ip;
    uint8_t local_mac[6];
    uint8_t reserved[2];
} supervisor_network_probe_context_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_network_context_t;

typedef enum {
    SUPERVISOR_NETWORK_DEGRADED_EXPIRED = 1,
    SUPERVISOR_NETWORK_DEGRADED_QUEUE = 2,
    SUPERVISOR_NETWORK_DEGRADED_SEMANTIC = 3,
} supervisor_network_degradation_reason_t;

typedef struct {
    uint32_t expired;
    uint32_t queue_fallback;
    uint32_t semantic_reject;
} supervisor_network_degradation_stats_t;

void supervisor_network_degradation_init(
    supervisor_network_degradation_stats_t *stats);
void supervisor_network_degradation_record(
    supervisor_network_degradation_stats_t *stats,
    supervisor_network_degradation_reason_t reason);
int supervisor_network_degradation_snapshot(
    supervisor_network_degradation_stats_t *stats_out);

void supervisor_probe_authority_init(supervisor_probe_authority_t *authority);
int supervisor_probe_authority_begin(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms, uint32_t timeout_ms,
                                     uint32_t *probe_id_out);
bool supervisor_probe_authority_take(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms, uint32_t *probe_id_out);
bool supervisor_probe_authority_expire(supervisor_probe_authority_t *authority,
                                       uint64_t now_ms);
void supervisor_probe_authority_cancel(supervisor_probe_authority_t *authority);
int supervisor_protected_probe_authority_init(
    supervisor_protected_probe_authority_t *authority);
int supervisor_protected_probe_authority_begin(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms,
    uint32_t timeout_ms, uint32_t *probe_id_out);
int supervisor_protected_probe_authority_take(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms,
    uint32_t *probe_id_out);
int supervisor_protected_probe_authority_expire(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms);
int supervisor_protected_probe_authority_cancel(
    supervisor_protected_probe_authority_t *authority);
int supervisor_protected_network_context_init(
    supervisor_protected_network_context_t *context);
int supervisor_protected_network_context_prepare(
    supervisor_protected_network_context_t *context, uint32_t gateway,
    uint32_t local_ip, const uint8_t local_mac[6]);
int supervisor_protected_network_context_snapshot(
    supervisor_protected_network_context_t *context,
    supervisor_network_probe_context_t *snapshot_out);
int supervisor_protected_network_context_publish(
    supervisor_protected_network_context_t *context, uint32_t probe_id);
int supervisor_protected_network_context_consume(
    supervisor_protected_network_context_t *context, uint32_t probe_id);
int supervisor_protected_network_context_clear(
    supervisor_protected_network_context_t *context);

void supervisor_init(void);
void supervisor_clock_tick(uint64_t now_ms);
bool supervisor_start_worker(void);
bool supervisor_start_probe(uint64_t now_ms);
int supervisor_probe_report(int pid, uint32_t generation,
                            uint32_t report_type, uint32_t value,
                            uint64_t now_ms);
int supervisor_service_connect(struct Process *client, uint32_t service_id,
                               uint32_t *handle_out);
bool supervisor_network_submit_header(const uint8_t *frame, uint16_t length);
int supervisor_network_probe_request(int pid, uint32_t generation,
                                     uint64_t now_ms);
int supervisor_network_probe_request_id(int pid, uint32_t generation,
                                        uint64_t now_ms,
                                        uint32_t *probe_id_out);
int supervisor_spawn_service(const char *path, int argc,
                             const char *const *argv, uint32_t domain_kind);
int supervisor_register(const char *name, const supervisor_config_t *config,
                        const supervisor_fence_ops_t *fence_ops,
                        uint64_t now_ms, supervisor_handle_t *handle_out);
int supervisor_report_progress(supervisor_handle_t handle,
                               uint64_t progress_marker, uint64_t now_ms);
int supervisor_report_idle(supervisor_handle_t handle);
supervisor_event_t supervisor_poll(uint64_t now_ms);
supervisor_event_t supervisor_service_one(uint64_t now_ms);
supervisor_event_t supervisor_apply_fence(supervisor_handle_t handle,
                                          uint64_t now_ms);
int supervisor_report_self_test(supervisor_handle_t handle, bool passed,
                                uint64_t now_ms);
bool supervisor_output_allowed(supervisor_handle_t handle);

#ifdef REIST_HOST_TEST
int supervisor_test_corrupt_fence_ops(supervisor_handle_t handle,
                                      bool corrupt_both_copies);
int supervisor_test_corrupt_descriptor(supervisor_handle_t handle,
                                       bool corrupt_both_copies);
int supervisor_test_corrupt_network_degradation(bool corrupt_both_copies);
int supervisor_test_record_network_degradation(
    supervisor_network_degradation_reason_t reason);
int supervisor_test_corrupt_probe_authority(
    supervisor_protected_probe_authority_t *authority,
    bool corrupt_both_copies);
int supervisor_test_corrupt_network_context(
    supervisor_protected_network_context_t *context,
    bool corrupt_both_copies);
#endif

#endif
