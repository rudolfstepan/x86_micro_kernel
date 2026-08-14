#ifndef KERNEL_SUPERVISOR_H
#define KERNEL_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

struct Process;

#define SUPERVISOR_MAX_DOMAINS 8U
#define SUPERVISOR_NAME_CAPACITY 16U
#define SUPERVISOR_STATE_VERSION 1U
#define SUPERVISOR_FENCE_OPS_VERSION 1U
#define SUPERVISOR_DESCRIPTOR_VERSION 1U
#define REIST_REPORT_SELF_TEST 1U
#define REIST_REPORT_PROGRESS 2U
#define REIST_REPORT_INVALID 3U
#define REIST_REPORT_NETWORK_HEADER 4U
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
#endif

#endif
