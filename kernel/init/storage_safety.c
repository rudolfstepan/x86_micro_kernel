#include "include/kernel/storage_safety.h"

#include "drivers/block/ata.h"
#include "drivers/block/fdd.h"
#include "include/kernel/critical_object.h"
#include "include/kernel/supervisor.h"

#define STORAGE_WRITE_DEADLINE_MS 10000U

#define STORAGE_CONTROL_VERSION 1U

typedef struct {
    uint64_t progress_marker;
    uint32_t write_fenced;
    uint32_t reserved;
} storage_control_t;

static critical_object_t storage_control;
static volatile bool storage_integrity_failed;
static volatile bool storage_force_fenced;
static bool storage_supervised;
static supervisor_handle_t storage_supervisor_handle;

static bool storage_control_valid(const void *payload, size_t length) {
    if (length != sizeof(storage_control_t)) return false;
    const storage_control_t *state = (const storage_control_t *)payload;
    return state->progress_marker != 0U && state->write_fenced <= 1U &&
           state->reserved == 0U;
}

static bool storage_control_read(storage_control_t *state) {
    size_t length = 0;
    if (storage_integrity_failed ||
        critical_object_read(&storage_control, STORAGE_CONTROL_VERSION, state,
                             sizeof(*state), &length,
                             storage_control_valid) < 0 ||
        length != sizeof(*state)) {
        storage_integrity_failed = true;
        ata_fence_writes();
        fdd_fence_writes();
        return false;
    }
    return true;
}

static bool storage_control_write(const storage_control_t *state) {
    if (storage_integrity_failed ||
        critical_object_update(&storage_control, STORAGE_CONTROL_VERSION,
                               state, sizeof(*state),
                               storage_control_valid) != 0) {
        storage_integrity_failed = true;
        ata_fence_writes();
        fdd_fence_writes();
        return false;
    }
    return true;
}

static bool storage_apply_supervisor_fence(void *context) {
    (void)context;
    storage_fence_writes();
    return true;
}

static bool storage_verify_supervisor_fence(void *context) {
    (void)context;
    return storage_writes_fenced();
}

bool storage_safety_init(uint64_t now_ms) {
    if (storage_supervised) return true;
    storage_control_t state = {
        .progress_marker = 1U,
        .write_fenced = 0U,
        .reserved = 0U,
    };
    if (critical_object_init(&storage_control, STORAGE_CONTROL_VERSION, &state,
                             sizeof(state)) != 0) return false;
    supervisor_config_t config = {
        .heartbeat_timeout_ms = STORAGE_WRITE_DEADLINE_MS,
        .recovery_timeout_ms = STORAGE_WRITE_DEADLINE_MS,
        .restart_budget = 0,
    };
    supervisor_fence_ops_t fence_ops = {
        .apply = storage_apply_supervisor_fence,
        .verify = storage_verify_supervisor_fence,
        .context = 0,
    };
    if (supervisor_register("storage-write", &config, &fence_ops, now_ms,
                            &storage_supervisor_handle) != 0 ||
        supervisor_report_progress(storage_supervisor_handle, 1U, now_ms) != 0 ||
        supervisor_report_idle(storage_supervisor_handle) != 0) return false;
    storage_supervised = true;
    return true;
}

bool storage_write_begin(uint64_t now_ms) {
    storage_control_t state;
    if (!storage_supervised)
        return !storage_integrity_failed && !storage_force_fenced;
    if (storage_force_fenced) return false;
    if (!storage_control_read(&state) || state.write_fenced != 0U) return false;
    if (++state.progress_marker == 0U || !storage_control_write(&state))
        return false;
    return supervisor_report_progress(storage_supervisor_handle,
                                      state.progress_marker, now_ms) == 0;
}

bool storage_write_end(void) {
    storage_control_t state;
    if (!storage_supervised) return !storage_integrity_failed;
    if (!storage_control_read(&state) || state.write_fenced != 0U) return false;
    return supervisor_report_idle(storage_supervisor_handle) == 0 &&
           !storage_integrity_failed;
}

void storage_fence_writes(void) {
    storage_force_fenced = true;
    __asm__ volatile("" ::: "memory");
    storage_control_t state;
    if (storage_control_read(&state)) {
        state.write_fenced = 1U;
        (void)storage_control_write(&state);
    }
    ata_fence_writes();
    fdd_fence_writes();
}

bool storage_writes_fenced(void) {
    storage_control_t state;
    bool logical = storage_force_fenced || storage_integrity_failed;
    if (storage_control_read(&state)) logical |= state.write_fenced != 0U;
    return logical && ata_writes_quiescent() && fdd_writes_quiescent();
}
