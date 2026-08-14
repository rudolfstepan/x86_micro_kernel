#include "include/kernel/storage_service.h"

#include "arch/x86/include/interrupt.h"
#include "drivers/block/ata.h"
#include "include/kernel/critical_object.h"
#include "include/kernel/filesystem_safety.h"
#include "include/kernel/storage_request_pool.h"
#include "include/kernel/storage_safety.h"
#include "include/kernel/supervisor.h"
#include "kernel/proc/process.h"
#include "lib/libc/stdio.h"

#define STORAGE_SERVICE_CONTROL_VERSION 1U
#define STORAGE_SERVICE_START_TIMEOUT_MS 1000U
#define STORAGE_SERVICE_RESTART_BUDGET 3U

typedef struct {
    int32_t pid;
    uint32_t generation;
    uint32_t launch_count;
    uint32_t healthy;
    uint32_t quarantined_resources;
    uint64_t start_deadline_ms;
} storage_service_control_t;

static critical_object_t protected_control;
static bool initialized;
static volatile bool service_starting;
static volatile bool service_started;

_Static_assert(sizeof(storage_service_control_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "storage service control exceeds protected payload");
_Static_assert(MAX_DRIVES > 0 && MAX_DRIVES < 32,
               "storage quarantine mask requires 1..31 resources");

static bool control_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(storage_service_control_t))
        return false;
    const storage_service_control_t *control = payload;
    if (control->healthy > 1U ||
        (control->quarantined_resources & ~((1U << MAX_DRIVES) - 1U)) != 0U ||
        control->launch_count > STORAGE_SERVICE_RESTART_BUDGET + 1U)
        return false;
    if (control->pid == 0)
        return control->generation == 0U && control->healthy == 0U &&
               control->start_deadline_ms == 0U;
    return control->pid > 0 && control->generation != 0U &&
           (control->healthy != 0U || control->start_deadline_ms != 0U);
}

static int control_read(storage_service_control_t *control) {
    size_t length = 0U;
    uint32_t flags = irq_save();
    int result = critical_object_read(&protected_control,
        STORAGE_SERVICE_CONTROL_VERSION, control, sizeof(*control), &length,
        control_valid) < 0 || length != sizeof(*control) ? -84 : 0;
    irq_restore(flags);
    return result;
}

static int control_write(const storage_service_control_t *control) {
    uint32_t flags = irq_save();
    int result = critical_object_update(&protected_control,
        STORAGE_SERVICE_CONTROL_VERSION, control, sizeof(*control),
        control_valid) == 0 ? 0 : -84;
    irq_restore(flags);
    return result;
}

static uint64_t deadline_after(uint64_t now_ms, uint32_t interval_ms) {
    return UINT64_MAX - now_ms < interval_ms
        ? UINT64_MAX : now_ms + interval_ms;
}

static bool spawn_service(storage_service_control_t *control,
                          uint64_t now_ms) {
    const char *arguments[] = {"STORAGE.PRG"};
    int pid = supervisor_spawn_service("/STORAGE.PRG", 1, arguments,
                                       PROCESS_DOMAIN_STORAGE);
    uint32_t generation = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) return false;
    control->pid = pid;
    control->generation = generation;
    control->healthy = 0U;
    ++control->launch_count;
    control->start_deadline_ms = deadline_after(
        now_ms, STORAGE_SERVICE_START_TIMEOUT_MS);
    if (control_write(control) != 0) {
        (void)process_terminate(pid);
        return false;
    }
    return true;
}

bool storage_service_init(void) {
    storage_service_control_t control = {0};
    if (critical_object_init(&protected_control,
            STORAGE_SERVICE_CONTROL_VERSION, &control,
            sizeof(control)) != 0) return false;
    initialized = true;
    return true;
}

bool storage_service_start(uint64_t now_ms) {
    if (!initialized || service_starting || service_started) return false;
    service_starting = true;
    storage_service_control_t control;
    bool result = control_read(&control) == 0 && control.pid == 0 &&
                  spawn_service(&control, now_ms);
    service_started = result;
    service_starting = false;
    return result;
}

int storage_service_bind(int pid, uint32_t generation) {
    storage_service_control_t control;
    if (!initialized || control_read(&control) != 0 || control.pid != pid ||
        control.generation != generation || control.healthy != 0U ||
        !process_identity_alive(pid, generation)) return -13;
    int result = storage_request_bind_service(pid, generation);
    if (result != 0) return result;
    control.healthy = 1U;
    control.start_deadline_ms = 0U;
    if (control_write(&control) != 0) {
        storage_request_unbind_service(pid, generation);
        return -84;
    }
    printf("REIST_STORAGE SERVICE_READY\n");
    return 0;
}

bool storage_service_authorized(int pid, uint32_t generation) {
    storage_service_control_t control;
    return initialized && control_read(&control) == 0 &&
           control.healthy != 0U && control.pid == pid &&
           control.generation == generation &&
           process_identity_alive(pid, generation);
}

bool storage_service_resource_available(uint32_t resource) {
    storage_service_control_t control;
    if (resource >= MAX_DRIVES || !initialized ||
        control_read(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return false;
    }
    return (control.quarantined_resources & (1U << resource)) == 0U;
}

bool storage_service_report_io_failure(uint32_t resource) {
    storage_service_control_t control;
    if (resource >= MAX_DRIVES || !initialized ||
        control_read(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return false;
    }
    uint32_t mask = 1U << resource;
    if ((control.quarantined_resources & mask) == 0U) {
        control.quarantined_resources |= mask;
        if (control_write(&control) != 0) {
            storage_fence_writes();
            filesystem_fence_mutations();
            return false;
        }
        printf("REIST_STORAGE RESOURCE_QUARANTINED %u\n", resource);
    }
    return true;
}

void storage_service_poll(uint64_t now_ms) {
    if (!initialized || !service_started || service_starting) return;
    storage_service_control_t control;
    if (control_read(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return;
    }
    if (control.pid != 0 && process_identity_alive(
            control.pid, control.generation) &&
        (control.healthy != 0U || now_ms < control.start_deadline_ms)) return;

    if (control.pid != 0) {
        storage_request_unbind_service(control.pid, control.generation);
        if (process_identity_alive(control.pid, control.generation))
            (void)process_terminate(control.pid);
        printf("REIST_STORAGE SERVICE_FAILURE_DETECTED\n");
    }
    control.pid = 0;
    control.generation = 0U;
    control.healthy = 0U;
    control.start_deadline_ms = 0U;
    if (control.launch_count > STORAGE_SERVICE_RESTART_BUDGET ||
        !spawn_service(&control, now_ms)) {
        storage_fence_writes();
        filesystem_fence_mutations();
        (void)control_write(&control);
        printf("REIST_STORAGE SERVICE_DEGRADED\n");
    } else if (control.launch_count > 1U) {
        printf("REIST_STORAGE SERVICE_RESTARTED\n");
    }
}
