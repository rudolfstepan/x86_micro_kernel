#ifndef KERNEL_SUPERVISOR_H
#define KERNEL_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

#define SUPERVISOR_MAX_DOMAINS 8U
#define SUPERVISOR_NAME_CAPACITY 16U
#define SUPERVISOR_STATE_VERSION 1U

typedef enum {
    SUPERVISOR_STARTING = 1,
    SUPERVISOR_HEALTHY = 2,
    SUPERVISOR_DEGRADED = 3,
    SUPERVISOR_RECOVERING = 4,
    SUPERVISOR_FAILED = 5,
    SUPERVISOR_ISOLATED = 6,
    SUPERVISOR_SAFE_STATE = 7,
    SUPERVISOR_FENCING = 8,
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
int supervisor_register(const char *name, const supervisor_config_t *config,
                        const supervisor_fence_ops_t *fence_ops,
                        uint64_t now_ms, supervisor_handle_t *handle_out);
int supervisor_report_progress(supervisor_handle_t handle,
                               uint32_t progress_marker, uint64_t now_ms);
supervisor_event_t supervisor_poll(uint64_t now_ms);
supervisor_event_t supervisor_service_one(uint64_t now_ms);
supervisor_event_t supervisor_apply_fence(supervisor_handle_t handle,
                                          uint64_t now_ms);
int supervisor_report_self_test(supervisor_handle_t handle, bool passed,
                                uint64_t now_ms);
bool supervisor_output_allowed(supervisor_handle_t handle);

#endif
