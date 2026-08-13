#include "include/kernel/filesystem_safety.h"

#include "include/kernel/supervisor.h"

#define FILESYSTEM_MUTATION_DEADLINE_MS 15000U

static volatile bool filesystem_read_only;
static bool filesystem_supervised;
static uint64_t filesystem_progress_marker;
static supervisor_handle_t filesystem_supervisor_handle;

static bool filesystem_apply_fence(void *context) {
    (void)context;
    filesystem_fence_mutations();
    return true;
}

static bool filesystem_verify_fence(void *context) {
    (void)context;
    return filesystem_is_read_only();
}

bool filesystem_safety_init(uint64_t now_ms) {
    if (filesystem_supervised) return true;
    supervisor_config_t config = {
        .heartbeat_timeout_ms = FILESYSTEM_MUTATION_DEADLINE_MS,
        .recovery_timeout_ms = FILESYSTEM_MUTATION_DEADLINE_MS,
        .restart_budget = 0,
    };
    supervisor_fence_ops_t fence_ops = {
        .apply = filesystem_apply_fence,
        .verify = filesystem_verify_fence,
        .context = 0,
    };
    if (supervisor_register("filesystem-write", &config, &fence_ops, now_ms,
                            &filesystem_supervisor_handle) != 0 ||
        supervisor_report_progress(filesystem_supervisor_handle, 1U, now_ms) != 0 ||
        supervisor_report_idle(filesystem_supervisor_handle) != 0) return false;
    filesystem_progress_marker = 1U;
    filesystem_supervised = true;
    return true;
}

bool filesystem_mutation_begin(uint64_t now_ms) {
    if (filesystem_read_only) return false;
    if (!filesystem_supervised) return true;
    return supervisor_report_progress(filesystem_supervisor_handle,
                                      ++filesystem_progress_marker, now_ms) == 0;
}

bool filesystem_mutation_end(void) {
    if (!filesystem_supervised) return !filesystem_read_only;
    return supervisor_report_idle(filesystem_supervisor_handle) == 0 &&
           !filesystem_read_only;
}

void filesystem_fence_mutations(void) {
    filesystem_read_only = true;
    __asm__ volatile("" ::: "memory");
}

bool filesystem_is_read_only(void) {
    return filesystem_read_only;
}
