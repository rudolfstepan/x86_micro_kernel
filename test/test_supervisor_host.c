#include "include/kernel/supervisor.h"

static bool fence_applied;
static bool fence_verified = true;
static uint32_t fence_apply_count;

static bool apply_fence(void *context) {
    (void)context;
    fence_applied = true;
    ++fence_apply_count;
    return true;
}

static bool verify_fence(void *context) {
    (void)context;
    return fence_applied && fence_verified;
}

int main(void) {
    supervisor_config_t config = {
        .heartbeat_timeout_ms = 100U,
        .recovery_timeout_ms = 50U,
        .restart_budget = 1U,
    };
    supervisor_handle_t handle;
    supervisor_fence_ops_t fence_ops = {
        .apply = apply_fence, .verify = verify_fence, .context = 0,
    };
    supervisor_init();
    if (supervisor_register("storage", &config, &fence_ops, 1000U, &handle) != 0) return 1;
    if (supervisor_output_allowed(handle)) return 2;
    if (supervisor_report_progress(handle, 1U, 1010U) != 0) return 3;
    if (!supervisor_output_allowed(handle)) return 4;
    if (supervisor_report_progress(handle, 1U, 1020U) == 0) return 5;
    if (supervisor_poll(1109U).type != SUPERVISOR_EVENT_NONE) return 6;

    supervisor_clock_tick(1110U);
    supervisor_event_t fence = supervisor_poll(1110U);
    if (fence.type != SUPERVISOR_EVENT_FENCE_REQUIRED) return 7;
    if (supervisor_poll(1111U).type != SUPERVISOR_EVENT_FENCE_REQUIRED) return 25;
    if (supervisor_output_allowed(handle)) return 8;
    supervisor_event_t restart = supervisor_apply_fence(fence.handle, 1120U);
    if (restart.type != SUPERVISOR_EVENT_RESTART_REQUIRED) return 9;
    if (supervisor_poll(1120U).type != SUPERVISOR_EVENT_RESTART_REQUIRED)
        return 26;
    if (fence_apply_count != 1U) return 23;
    if (supervisor_apply_fence(fence.handle, 1120U).type !=
        SUPERVISOR_EVENT_NONE || fence_apply_count != 1U) return 24;
    if (restart.handle.epoch == handle.epoch) return 10;
    if (supervisor_report_progress(handle, 2U, 1121U) == 0) return 11;
    if (supervisor_report_self_test(restart.handle, true, 1130U) != 0) return 12;
    if (supervisor_report_progress(restart.handle, 1U, 1131U) != 0) return 13;
    if (!supervisor_output_allowed(restart.handle)) return 14;

    fence = supervisor_poll(1231U);
    if (fence.type != SUPERVISOR_EVENT_FENCE_REQUIRED) return 15;
    supervisor_event_t safe = supervisor_apply_fence(fence.handle, 1240U);
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) return 16;
    if (supervisor_poll(1240U).type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED)
        return 27;
    if (supervisor_output_allowed(safe.handle)) return 17;

    supervisor_handle_t failed_handle;
    fence_applied = false;
    fence_verified = false;
    if (supervisor_register("actuator", &config, &fence_ops, 2000U,
                            &failed_handle) != 0) return 18;
    if (supervisor_report_progress(failed_handle, 1U, 2001U) != 0) return 19;
    safe = supervisor_service_one(2101U);
    if (!fence_applied) return 20;
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) return 21;
    if (supervisor_output_allowed(failed_handle)) return 22;

    supervisor_init();
    config.restart_budget = 0U;
    fence_applied = false;
    fence_verified = true;
    if (supervisor_register("idle-domain", &config, &fence_ops, 3000U,
                            &handle) != 0) return 28;
    if (supervisor_report_progress(handle, 1U, 3001U) != 0 ||
        supervisor_report_idle(handle) != 0 ||
        !supervisor_output_allowed(handle)) return 29;
    supervisor_clock_tick(UINT64_MAX);
    if (supervisor_poll(UINT64_MAX).type != SUPERVISOR_EVENT_NONE) return 30;
    if (supervisor_report_progress(handle, 2U, 4000U) != 0) return 31;
    supervisor_clock_tick(4100U);
    safe = supervisor_service_one(4100U);
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) return 32;

    supervisor_init();
    config.restart_budget = 1U;
    fence_applied = false;
    if (supervisor_register("corrected-ops", &config, &fence_ops, 5000U,
                            &handle) != 0 ||
        supervisor_report_progress(handle, 1U, 5001U) != 0 ||
        supervisor_test_corrupt_fence_ops(handle, false) != 0) return 33;
    supervisor_clock_tick(5101U);
    restart = supervisor_service_one(5101U);
    if (restart.type != SUPERVISOR_EVENT_RESTART_REQUIRED || !fence_applied)
        return 34;

    supervisor_init();
    fence_applied = false;
    if (supervisor_register("broken-ops", &config, &fence_ops, 6000U,
                            &handle) != 0 ||
        supervisor_report_progress(handle, 1U, 6001U) != 0 ||
        supervisor_test_corrupt_fence_ops(handle, true) != 0) return 35;
    supervisor_clock_tick(6101U);
    safe = supervisor_service_one(6101U);
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED || fence_applied)
        return 36;

    supervisor_init();
    if (supervisor_register("corrected-slot", &config, &fence_ops, 7000U,
                            &handle) != 0 ||
        supervisor_test_corrupt_descriptor(handle, false) != 0 ||
        supervisor_report_progress(handle, 1U, 7001U) != 0) return 37;
    if (!supervisor_output_allowed(handle)) return 38;

    supervisor_init();
    if (supervisor_register("broken-slot", &config, &fence_ops, 8000U,
                            &handle) != 0 ||
        supervisor_test_corrupt_descriptor(handle, true) != 0) return 39;
    if (supervisor_poll(8001U).type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED)
        return 40;
    return 0;
}
