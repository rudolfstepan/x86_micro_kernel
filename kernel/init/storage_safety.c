#include "include/kernel/storage_safety.h"

#include "drivers/block/ata.h"
#include "drivers/block/fdd.h"
#include "include/kernel/supervisor.h"

#define STORAGE_WRITE_DEADLINE_MS 10000U

static volatile bool storage_write_fenced;
static bool storage_supervised;
static uint64_t storage_progress_marker;
static supervisor_handle_t storage_supervisor_handle;

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
    storage_progress_marker = 1U;
    storage_supervised = true;
    return true;
}

bool storage_write_begin(uint64_t now_ms) {
    if (storage_write_fenced) return false;
    if (!storage_supervised) return true;
    return supervisor_report_progress(storage_supervisor_handle,
                                      ++storage_progress_marker, now_ms) == 0;
}

bool storage_write_end(void) {
    if (!storage_supervised) return !storage_write_fenced;
    return supervisor_report_idle(storage_supervisor_handle) == 0 &&
           !storage_write_fenced;
}

void storage_fence_writes(void) {
    storage_write_fenced = true;
    __asm__ volatile("" ::: "memory");
    ata_fence_writes();
    fdd_fence_writes();
}

bool storage_writes_fenced(void) {
    return storage_write_fenced && ata_writes_quiescent() &&
           fdd_writes_quiescent();
}
