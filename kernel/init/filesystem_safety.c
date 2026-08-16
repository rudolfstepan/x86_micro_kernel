#include "include/kernel/filesystem_safety.h"

#include "include/kernel/critical_object.h"
#include "include/kernel/supervisor.h"
#include "drivers/block/ata.h"

#define FILESYSTEM_MUTATION_DEADLINE_MS 15000U

#define FILESYSTEM_CONTROL_VERSION 1U

typedef struct {
    uint64_t progress_marker;
    uint64_t mutation_deadline_ms;
    uint32_t read_only;
    uint32_t mutation_active;
} filesystem_control_t;

static critical_object_t filesystem_control;
static volatile bool filesystem_integrity_failed;
static volatile bool filesystem_force_read_only;
static bool filesystem_supervised;
static supervisor_handle_t filesystem_supervisor_handle;

static bool filesystem_control_valid(const void *payload, size_t length) {
    if (length != sizeof(filesystem_control_t)) return false;
    const filesystem_control_t *state = (const filesystem_control_t *)payload;
    return state->progress_marker != 0U && state->read_only <= 1U &&
           state->mutation_active <= 1U &&
           ((state->mutation_active == 0U &&
             state->mutation_deadline_ms == 0U) ||
            (state->mutation_active != 0U &&
             state->mutation_deadline_ms != 0U));
}

static bool filesystem_control_read(filesystem_control_t *state) {
    size_t length = 0;
    if (filesystem_integrity_failed ||
        critical_object_read(&filesystem_control, FILESYSTEM_CONTROL_VERSION,
                             state, sizeof(*state), &length,
                             filesystem_control_valid) < 0 ||
        length != sizeof(*state)) {
        filesystem_integrity_failed = true;
        return false;
    }
    return true;
}

static bool filesystem_control_write(const filesystem_control_t *state) {
    if (filesystem_integrity_failed ||
        critical_object_update(&filesystem_control, FILESYSTEM_CONTROL_VERSION,
                               state, sizeof(*state),
                               filesystem_control_valid) != 0) {
        filesystem_integrity_failed = true;
        return false;
    }
    return true;
}

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
    filesystem_control_t state = {
        .progress_marker = 1U,
        .mutation_deadline_ms = 0U,
        .read_only = 0U,
        .mutation_active = 0U,
    };
    if (critical_object_init(&filesystem_control, FILESYSTEM_CONTROL_VERSION,
                             &state, sizeof(state)) != 0) return false;
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
    filesystem_supervised = true;
    return true;
}

bool filesystem_mutation_begin(uint64_t now_ms) {
    filesystem_control_t state;
    if (!filesystem_supervised)
        return !filesystem_integrity_failed && !filesystem_force_read_only;
    if (filesystem_force_read_only) return false;
    if (!filesystem_control_read(&state) || state.read_only != 0U ||
        state.mutation_active != 0U) return false;
    if (++state.progress_marker == 0U) {
        filesystem_fence_mutations();
        return false;
    }
    state.mutation_active = 1U;
    state.mutation_deadline_ms =
        UINT64_MAX - now_ms < FILESYSTEM_MUTATION_DEADLINE_MS
            ? UINT64_MAX : now_ms + FILESYSTEM_MUTATION_DEADLINE_MS;
    if (!filesystem_control_write(&state)) {
        filesystem_fence_mutations();
        return false;
    }
    if (supervisor_report_progress(filesystem_supervisor_handle,
                                   state.progress_marker, now_ms) != 0 ||
        !ata_journal_transaction_begin()) {
        filesystem_fence_mutations();
        return false;
    }
    return true;
}

bool filesystem_mutation_end(bool commit) {
    filesystem_control_t state;
    if (!filesystem_supervised) return !filesystem_integrity_failed;
    if (!ata_journal_transaction_end(commit)) {
        filesystem_fence_mutations();
        return false;
    }
    if (!filesystem_control_read(&state) || state.read_only != 0U ||
        state.mutation_active == 0U) return false;
    state.mutation_active = 0U;
    state.mutation_deadline_ms = 0U;
    if (!filesystem_control_write(&state) ||
        supervisor_report_idle(filesystem_supervisor_handle) != 0) {
        filesystem_fence_mutations();
        return false;
    }
    return !filesystem_integrity_failed;
}

void filesystem_fence_mutations(void) {
    filesystem_force_read_only = true;
    __asm__ volatile("" ::: "memory");
    filesystem_control_t state;
    if (filesystem_control_read(&state)) {
        state.read_only = 1U;
        (void)filesystem_control_write(&state);
    }
}

bool filesystem_restore_mutations_after_recovery(void) {
    filesystem_control_t state;
    if (!filesystem_supervised || filesystem_integrity_failed ||
        !filesystem_control_read(&state)) return false;
    state.read_only = 0U;
    state.mutation_active = 0U;
    state.mutation_deadline_ms = 0U;
    if (!filesystem_control_write(&state) ||
        supervisor_report_idle(filesystem_supervisor_handle) != 0) {
        filesystem_fence_mutations();
        return false;
    }
    __asm__ volatile("" ::: "memory");
    filesystem_force_read_only = false;
    return true;
}

bool filesystem_is_read_only(void) {
    if (filesystem_integrity_failed || filesystem_force_read_only) return true;
    filesystem_control_t state;
    return !filesystem_control_read(&state) || state.read_only != 0U;
}
