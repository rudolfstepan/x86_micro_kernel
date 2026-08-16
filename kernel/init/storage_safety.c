#include "include/kernel/storage_safety.h"

#include "drivers/block/ata.h"
#include "drivers/block/ahci.h"
#include "drivers/block/fdd.h"
#include "include/kernel/critical_object.h"
#include "include/kernel/supervisor.h"
#include "include/kernel/storage_request_pool.h"
#include "include/kernel/storage_handover.h"
#include "include/kernel/filesystem_safety.h"
#include "include/kernel/storage_service.h"

#define STORAGE_WRITE_DEADLINE_MS 10000U

#define STORAGE_CONTROL_VERSION 1U

typedef struct {
    uint64_t progress_marker;
    uint64_t operation_deadline_ms;
    uint32_t write_fenced;
    uint32_t operation_active;
    uint32_t active_resource;
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
           state->operation_active <= 1U &&
           ((state->operation_active == 0U &&
             state->operation_deadline_ms == 0U &&
             state->active_resource == UINT32_MAX) ||
            (state->operation_active != 0U &&
             state->operation_deadline_ms != 0U &&
             state->active_resource < MAX_DRIVES));
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
        ahci_fence_writes();
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
        ahci_fence_writes();
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
    if (storage_request_pool_init() != 0 || !storage_handover_init())
        return false;
    storage_control_t state = {
        .progress_marker = 1U,
        .operation_deadline_ms = 0U,
        .write_fenced = 0U,
        .operation_active = 0U,
        .active_resource = UINT32_MAX,
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

bool storage_write_begin(uint32_t resource, uint64_t now_ms) {
    storage_control_t state;
    if (resource >= (uint32_t)drive_count || resource >= MAX_DRIVES ||
        storage_handover_is_held() ||
        !storage_service_resource_available(resource) ||
        storage_service_resource_read_only(resource)) return false;
    if (!storage_supervised)
        return !storage_integrity_failed && !storage_force_fenced;
    if (storage_force_fenced) return false;
    if (!storage_control_read(&state) || state.write_fenced != 0U ||
        state.operation_active != 0U) return false;
    if (++state.progress_marker == 0U) {
        storage_fence_writes();
        return false;
    }
    state.operation_active = 1U;
    state.active_resource = resource;
    state.operation_deadline_ms = UINT64_MAX - now_ms < STORAGE_WRITE_DEADLINE_MS
        ? UINT64_MAX : now_ms + STORAGE_WRITE_DEADLINE_MS;
    if (!storage_control_write(&state) ||
        supervisor_report_progress(storage_supervisor_handle,
                                   state.progress_marker, now_ms) != 0) {
        storage_fence_writes();
        return false;
    }
    return true;
}

bool storage_write_end(bool durable_commit) {
    storage_control_t state;
    if (!storage_supervised) return !storage_integrity_failed;
    if (!storage_control_read(&state) || state.operation_active == 0U)
        return false;
    uint32_t resource = state.active_resource;
    bool committed = durable_commit && state.write_fenced == 0U &&
                     !storage_force_fenced;
    state.operation_active = 0U;
    state.operation_deadline_ms = 0U;
    state.active_resource = UINT32_MAX;
    if (!committed) state.write_fenced = 1U;
    if (!storage_control_write(&state) ||
        supervisor_report_idle(storage_supervisor_handle) != 0) {
        storage_fence_writes();
        return false;
    }
    if (!committed) {
        (void)storage_service_report_media_failure(resource, true);
        filesystem_fence_mutations();
        storage_fence_writes();
        return false;
    }
    return !storage_integrity_failed;
}

void storage_fence_writes(void) {
    storage_force_fenced = true;
    ahci_fence_writes();
    __asm__ volatile("" ::: "memory");
    storage_control_t state;
    if (storage_control_read(&state)) {
        state.write_fenced = 1U;
        (void)storage_control_write(&state);
    }
    ata_fence_writes();
    fdd_fence_writes();
}

bool storage_restore_writes_after_recovery(uint32_t resource) {
    storage_control_t state;
    if (resource >= (uint32_t)drive_count || resource >= MAX_DRIVES ||
        !storage_supervised || storage_integrity_failed ||
        storage_handover_is_held() || !storage_control_read(&state) ||
        (state.operation_active != 0U &&
         state.active_resource != resource)) return false;
    /* Journal recovery has already resolved uncertain media effects.  It is
     * now safe to retire only the matching interrupted operation before the
     * global and driver fences are released. */
    state.operation_active = 0U;
    state.operation_deadline_ms = 0U;
    state.active_resource = UINT32_MAX;
    state.write_fenced = 0U;
    if (!storage_control_write(&state) ||
        supervisor_report_idle(storage_supervisor_handle) != 0) {
        storage_fence_writes();
        return false;
    }
    ata_restore_writes_after_recovery();
    ahci_restore_writes_after_recovery();
    fdd_restore_writes_after_recovery();
    __asm__ volatile("" ::: "memory");
    storage_force_fenced = false;
    return true;
}

bool storage_writes_fenced(void) {
    storage_control_t state;
    bool logical = storage_force_fenced || storage_integrity_failed;
    if (storage_control_read(&state)) logical |= state.write_fenced != 0U;
    return logical && ata_writes_quiescent() && fdd_writes_quiescent();
}
