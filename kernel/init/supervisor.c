#include "include/kernel/supervisor.h"

#include "include/kernel/critical_object.h"
#ifndef REIST_HOST_TEST
#include "arch/x86/include/interrupt.h"
#include "include/kernel/panic.h"
#include "include/kernel/output_fence.h"
#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"
#endif

typedef struct {
    uint32_t generation;
    uint32_t state;
    uint32_t epoch;
    uint64_t progress_marker;
    uint32_t restart_count;
    uint32_t heartbeat_timeout_ms;
    uint32_t recovery_timeout_ms;
    uint32_t restart_budget;
    uint64_t deadline_ms;
} supervisor_state_t;

typedef struct {
    uint32_t active;
    uint32_t generation;
    char name[SUPERVISOR_NAME_CAPACITY];
} supervisor_descriptor_t;

typedef struct {
    critical_object_t protected_descriptor;
    critical_object_t protected_fence_ops;
    critical_object_t protected_state;
} supervisor_slot_t;

static supervisor_slot_t slots[SUPERVISOR_MAX_DOMAINS];
static uint32_t next_generation = 1U;
static uint64_t last_deadline_check_ms;
static uint32_t next_poll_slot;

#define SUPERVISOR_CHECK_INTERVAL_MS 10U

#ifndef REIST_HOST_TEST
typedef struct {
    bool active;
    bool fenced;
    supervisor_handle_t handle;
    int pid;
    uint32_t process_generation;
    uint32_t launch_count;
    uint32_t endpoint_handle;
} supervisor_probe_runtime_t;

static supervisor_probe_runtime_t probe_runtime;
#endif

static uint32_t supervisor_lock(void) {
#ifdef REIST_HOST_TEST
    return 0;
#else
    return irq_save();
#endif
}

static void supervisor_unlock(uint32_t flags) {
#ifndef REIST_HOST_TEST
    irq_restore(flags);
#else
    (void)flags;
#endif
}

static uint64_t deadline_after(uint64_t now, uint32_t interval) {
    return UINT64_MAX - now < interval ? UINT64_MAX : now + interval;
}

static bool state_valid(const void *payload, size_t length) {
    if (length != sizeof(supervisor_state_t)) return false;
    const supervisor_state_t *state = (const supervisor_state_t *)payload;
    return state->generation != 0 && state->state >= SUPERVISOR_STARTING &&
           state->state <= SUPERVISOR_IDLE &&
           state->heartbeat_timeout_ms != 0 &&
           state->recovery_timeout_ms != 0 &&
           state->restart_count <= state->restart_budget;
}

static bool fence_ops_valid(const void *payload, size_t length) {
    if (length != sizeof(supervisor_fence_ops_t)) return false;
    const supervisor_fence_ops_t *ops =
        (const supervisor_fence_ops_t *)payload;
    return ops->apply != 0 && ops->verify != 0;
}

static bool descriptor_valid(const void *payload, size_t length) {
    if (length != sizeof(supervisor_descriptor_t)) return false;
    const supervisor_descriptor_t *descriptor =
        (const supervisor_descriptor_t *)payload;
    if (descriptor->active > 1U) return false;
    if (descriptor->active == 0U)
        return descriptor->generation == 0U && descriptor->name[0] == '\0';
    return descriptor->generation != 0U && descriptor->name[0] != '\0' &&
           descriptor->name[SUPERVISOR_NAME_CAPACITY - 1U] == '\0';
}

static int descriptor_read(uint32_t slot, supervisor_descriptor_t *descriptor) {
    size_t length = 0;
    critical_read_result_t result = critical_object_read(
        &slots[slot].protected_descriptor, SUPERVISOR_DESCRIPTOR_VERSION,
        descriptor, sizeof(*descriptor), &length, descriptor_valid);
    return result < 0 ? -1 : 0;
}

static int fence_ops_read(uint32_t slot, supervisor_fence_ops_t *ops) {
    size_t length = 0;
    critical_read_result_t result = critical_object_read(
        &slots[slot].protected_fence_ops, SUPERVISOR_FENCE_OPS_VERSION, ops,
        sizeof(*ops), &length, fence_ops_valid);
    return result < 0 ? -1 : 0;
}

static int state_read(uint32_t slot, supervisor_state_t *state) {
    size_t length = 0;
    critical_read_result_t result = critical_object_read(
        &slots[slot].protected_state, SUPERVISOR_STATE_VERSION, state,
        sizeof(*state), &length, state_valid);
    return result < 0 ? -1 : 0;
}

static int state_write(uint32_t slot, const supervisor_state_t *state) {
    return critical_object_update(&slots[slot].protected_state,
                                  SUPERVISOR_STATE_VERSION, state,
                                  sizeof(*state), state_valid);
}

static int resolve(supervisor_handle_t handle, supervisor_state_t *state) {
    supervisor_descriptor_t descriptor;
    if (handle.slot >= SUPERVISOR_MAX_DOMAINS ||
        descriptor_read(handle.slot, &descriptor) != 0 ||
        descriptor.active == 0U || descriptor.generation != handle.generation ||
        state_read(handle.slot, state) != 0 ||
        state->generation != handle.generation || state->epoch != handle.epoch) return -1;
    return 0;
}

static supervisor_event_t event(supervisor_event_type_t type, uint32_t slot,
                                const supervisor_state_t *state) {
    supervisor_event_t result = {
        .type = type,
        .handle = {.slot = slot, .generation = state->generation,
                   .epoch = state->epoch},
    };
    return result;
}

void supervisor_init(void) {
    uint32_t flags = supervisor_lock();
    supervisor_descriptor_t empty = {0};
    for (uint32_t slot = 0; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        if (critical_object_init(&slots[slot].protected_descriptor,
                                 SUPERVISOR_DESCRIPTOR_VERSION, &empty,
                                 sizeof(empty)) != 0) {
#ifndef REIST_HOST_TEST
            panic("Unable to initialize supervisor descriptor");
#endif
        }
    }
    next_generation = 1U;
    last_deadline_check_ms = 0;
    next_poll_slot = 0;
    supervisor_unlock(flags);
}

static void check_deadlines_locked(uint64_t now_ms) {
    for (uint32_t slot = 0; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        supervisor_descriptor_t descriptor;
        if (descriptor_read(slot, &descriptor) != 0 || descriptor.active == 0U)
            continue;
        supervisor_state_t state;
        if (state_read(slot, &state) != 0) continue;
        if ((state.state == SUPERVISOR_STARTING ||
             state.state == SUPERVISOR_HEALTHY ||
             state.state == SUPERVISOR_DEGRADED ||
             state.state == SUPERVISOR_RECOVERING) &&
            now_ms >= state.deadline_ms) {
            state.state = SUPERVISOR_ISOLATED;
            (void)state_write(slot, &state);
        }
    }
}

void supervisor_clock_tick(uint64_t now_ms) {
    if (now_ms - last_deadline_check_ms < SUPERVISOR_CHECK_INTERVAL_MS) return;
    uint32_t flags = supervisor_lock();
    if (now_ms - last_deadline_check_ms >= SUPERVISOR_CHECK_INTERVAL_MS) {
        last_deadline_check_ms = now_ms;
        check_deadlines_locked(now_ms);
    }
    supervisor_unlock(flags);
}

int supervisor_register(const char *name, const supervisor_config_t *config,
                        const supervisor_fence_ops_t *fence_ops,
                        uint64_t now_ms, supervisor_handle_t *handle_out) {
    if (name == 0 || name[0] == '\0' || config == 0 || fence_ops == 0 ||
        fence_ops->apply == 0 || fence_ops->verify == 0 || handle_out == 0 ||
        config->heartbeat_timeout_ms == 0 || config->recovery_timeout_ms == 0)
        return -1;
    uint32_t flags = supervisor_lock();
    uint32_t slot = 0;
    for (; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        supervisor_descriptor_t descriptor;
        if (descriptor_read(slot, &descriptor) != 0) {
            supervisor_unlock(flags);
            return -1;
        }
        if (descriptor.active == 0U) break;
    }
    if (slot == SUPERVISOR_MAX_DOMAINS) {
        supervisor_unlock(flags);
        return -1;
    }
    uint32_t generation = next_generation++;
    if (generation == 0) generation = next_generation++;
    supervisor_state_t state = {
        .generation = generation, .state = SUPERVISOR_STARTING, .epoch = 1U,
        .progress_marker = 0, .restart_count = 0,
        .heartbeat_timeout_ms = config->heartbeat_timeout_ms,
        .recovery_timeout_ms = config->recovery_timeout_ms,
        .restart_budget = config->restart_budget,
        .deadline_ms = deadline_after(now_ms, config->recovery_timeout_ms),
    };
    if (critical_object_init(&slots[slot].protected_state,
                             SUPERVISOR_STATE_VERSION, &state,
                             sizeof(state)) != 0) {
        supervisor_unlock(flags);
        return -1;
    }
    supervisor_descriptor_t descriptor = {
        .active = 1U, .generation = generation, .name = {0},
    };
    uint32_t index = 0;
    while (index + 1U < SUPERVISOR_NAME_CAPACITY && name[index] != '\0') {
        descriptor.name[index] = name[index];
        ++index;
    }
    descriptor.name[index] = '\0';
    if (critical_object_init(&slots[slot].protected_fence_ops,
                             SUPERVISOR_FENCE_OPS_VERSION, fence_ops,
                             sizeof(*fence_ops)) != 0) {
        supervisor_unlock(flags);
        return -1;
    }
    if (critical_object_update(&slots[slot].protected_descriptor,
                               SUPERVISOR_DESCRIPTOR_VERSION, &descriptor,
                               sizeof(descriptor), descriptor_valid) != 0) {
        supervisor_unlock(flags);
        return -1;
    }
    handle_out->slot = slot;
    handle_out->generation = generation;
    handle_out->epoch = state.epoch;
    supervisor_unlock(flags);
    return 0;
}

int supervisor_report_progress(supervisor_handle_t handle,
                               uint64_t progress_marker, uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 ||
        (state.state != SUPERVISOR_STARTING && state.state != SUPERVISOR_HEALTHY &&
         state.state != SUPERVISOR_DEGRADED && state.state != SUPERVISOR_IDLE) ||
        progress_marker <= state.progress_marker) {
        supervisor_unlock(flags);
        return -1;
    }
    state.progress_marker = progress_marker;
    state.state = SUPERVISOR_HEALTHY;
    state.deadline_ms = deadline_after(now_ms, state.heartbeat_timeout_ms);
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

int supervisor_report_idle(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_HEALTHY) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = SUPERVISOR_IDLE;
    state.deadline_ms = UINT64_MAX;
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

#ifndef REIST_HOST_TEST
static int supervisor_force_isolate(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state == SUPERVISOR_SAFE_STATE) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = SUPERVISOR_ISOLATED;
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}
#endif

supervisor_event_t supervisor_poll(uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_event_t none = {.type = SUPERVISOR_EVENT_NONE};
    check_deadlines_locked(now_ms);
    for (uint32_t offset = 0; offset < SUPERVISOR_MAX_DOMAINS; ++offset) {
        uint32_t slot = (next_poll_slot + offset) % SUPERVISOR_MAX_DOMAINS;
        supervisor_descriptor_t descriptor;
        if (descriptor_read(slot, &descriptor) != 0) {
            supervisor_event_t result = {
                .type = SUPERVISOR_EVENT_SAFE_STATE_REQUIRED,
                .handle = {.slot = slot, .generation = 0, .epoch = 0},
            };
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (descriptor.active == 0U) continue;
        supervisor_state_t state;
        if (state_read(slot, &state) != 0) {
            supervisor_event_t result = {
                .type = SUPERVISOR_EVENT_SAFE_STATE_REQUIRED,
                .handle = {.slot = slot, .generation = 0, .epoch = 0},
            };
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (state.state == SUPERVISOR_ISOLATED) {
            supervisor_event_t result = event(SUPERVISOR_EVENT_FENCE_REQUIRED,
                                               slot, &state);
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (state.state == SUPERVISOR_RECOVERING) {
            supervisor_event_t result = event(
                SUPERVISOR_EVENT_RESTART_REQUIRED, slot, &state);
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (state.state == SUPERVISOR_SAFE_STATE) {
            supervisor_event_t result = event(
                SUPERVISOR_EVENT_SAFE_STATE_REQUIRED, slot, &state);
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
    }
    supervisor_unlock(flags);
    return none;
}

#ifndef REIST_HOST_TEST
static bool probe_fence_apply(void *context) {
    supervisor_probe_runtime_t *runtime = context;
    if (runtime == NULL || !runtime->active) return false;
    runtime->fenced = true;
    if (runtime->launch_count == 1U) printf("REIST_PROBE CRASH_DETECTED\n");
    else if (runtime->launch_count == 2U) printf("REIST_PROBE HANG_DETECTED\n");
    else if (runtime->launch_count == 3U)
        printf("REIST_PROBE INVALID_REPLY_DETECTED\n");
    if (process_identity_alive(runtime->pid, runtime->process_generation)) {
        (void)process_terminate(runtime->pid);
    }
    return true;
}

static bool probe_fence_verify(void *context) {
    supervisor_probe_runtime_t *runtime = context;
    return runtime != NULL && runtime->fenced &&
        !process_identity_alive(runtime->pid, runtime->process_generation);
}

static bool probe_spawn_next(void) {
    static const char *const modes[] = {"crash", "hang", "invalid", "healthy"};
    uint32_t mode_index = probe_runtime.launch_count;
    if (mode_index >= sizeof(modes) / sizeof(modes[0])) mode_index = 3U;
    const char *arguments[] = {"REIST.PRG", modes[mode_index]};
    int pid = supervisor_spawn_service("/REIST.PRG", 2, arguments,
                                       PROCESS_DOMAIN_PROBE);
    uint32_t generation = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) return false;
    probe_runtime.pid = pid;
    probe_runtime.process_generation = generation;
    ++probe_runtime.launch_count;
    return true;
}

bool supervisor_start_probe(uint64_t now_ms) {
    if (probe_runtime.active) return false;
    probe_runtime = (supervisor_probe_runtime_t){0};
    supervisor_config_t config = {
        .heartbeat_timeout_ms = 2000U,
        .recovery_timeout_ms = 1000U,
        .restart_budget = 4U,
    };
    supervisor_fence_ops_t fence = {
        .apply = probe_fence_apply,
        .verify = probe_fence_verify,
        .context = &probe_runtime,
    };
    if (supervisor_register("ring3-probe", &config, &fence, now_ms,
                            &probe_runtime.handle) != 0) return false;
    probe_runtime.active = true;
    if (!probe_spawn_next()) {
        probe_runtime.active = false;
        return false;
    }
    return true;
}

int supervisor_probe_report(int pid, uint32_t generation,
                            uint32_t report_type, uint32_t value,
                            uint64_t now_ms) {
    if (!probe_runtime.active || pid != probe_runtime.pid ||
        generation != probe_runtime.process_generation ||
        !process_identity_alive(pid, generation)) return -1;
    if (report_type == REIST_REPORT_SELF_TEST) {
        if (value == 0U ||
            (probe_runtime.endpoint_handle != 0U &&
             value == probe_runtime.endpoint_handle)) {
            (void)supervisor_force_isolate(probe_runtime.handle);
            return -1;
        }
        probe_runtime.endpoint_handle = value;
        return supervisor_report_self_test(probe_runtime.handle, true, now_ms);
    }
    if (report_type == REIST_REPORT_PROGRESS) {
        int result = supervisor_report_progress(probe_runtime.handle,
                                                value, now_ms);
        if (result == 0 && probe_runtime.fenced) {
            probe_runtime.fenced = false;
            if (probe_runtime.launch_count == 2U)
                printf("\nREIST_PROBE CRASH_DETECTED\n"
                       "REIST_PROBE CRASH_RECOVERED\n");
            else if (probe_runtime.launch_count == 3U)
                printf("\nREIST_PROBE HANG_DETECTED\n"
                       "REIST_PROBE HANG_RECOVERED\n");
            else if (probe_runtime.launch_count >= 4U) {
                printf("\nREIST_PROBE INVALID_REPLY_DETECTED\n"
                       "REIST_PROBE INVALID_RECOVERED\n");
                printf("REIST_PROBE REINTEGRATED\n");
            }
        }
        return result;
    }
    if (report_type == REIST_REPORT_INVALID) {
        (void)supervisor_force_isolate(probe_runtime.handle);
        return -1;
    }
    return -1;
}

static void supervisor_worker(void) {
    for (;;) {
        if (probe_runtime.active && !probe_runtime.fenced &&
            !process_identity_alive(probe_runtime.pid,
                                    probe_runtime.process_generation)) {
            (void)supervisor_force_isolate(probe_runtime.handle);
        }
        supervisor_event_t result = supervisor_service_one(pit_monotonic_ms());
        if (probe_runtime.active &&
            result.type == SUPERVISOR_EVENT_RESTART_REQUIRED &&
            result.handle.slot == probe_runtime.handle.slot &&
            result.handle.generation == probe_runtime.handle.generation) {
            probe_runtime.handle = result.handle;
            if (!probe_spawn_next()) {
                (void)supervisor_force_isolate(probe_runtime.handle);
            }
        }
        if (result.type == SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) {
            /* Until per-hazard external interlocks are registered, the
             * conservative system response revokes every known output. */
            output_fence_all();
        }
        if (scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS) != 0)
            (void)scheduler_yield();
    }
}

bool supervisor_start_worker(void) {
    uint32_t *stack = scheduler_allocate_kernel_stack();
    if (stack == NULL) return false;
    if (create_task(supervisor_worker, stack, NULL) < 0) {
        scheduler_free_kernel_stack(stack);
        return false;
    }
    return true;
}

int supervisor_spawn_service(const char *path, int argc,
                             const char *const *argv, uint32_t domain_kind) {
    if (domain_kind != PROCESS_DOMAIN_PROBE) return -1;
    return process_spawn_supervised(path, argc, argv,
                                    (process_domain_kind_t)domain_kind);
}
#else
bool supervisor_start_worker(void) {
    return false;
}

int supervisor_spawn_service(const char *path, int argc,
                             const char *const *argv, uint32_t domain_kind) {
    (void)path;
    (void)argc;
    (void)argv;
    (void)domain_kind;
    return -1;
}

bool supervisor_start_probe(uint64_t now_ms) {
    (void)now_ms;
    return false;
}

int supervisor_probe_report(int pid, uint32_t generation,
                            uint32_t report_type, uint32_t value,
                            uint64_t now_ms) {
    (void)pid; (void)generation; (void)report_type; (void)value; (void)now_ms;
    return -1;
}
#endif

supervisor_event_t supervisor_service_one(uint64_t now_ms) {
#ifndef REIST_HOST_TEST
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
#endif
    /* One invocation performs at most one potentially slow fence action.
     * Callers retain explicit control over their execution-time budget. */
    supervisor_event_t pending = supervisor_poll(now_ms);
    if (pending.type != SUPERVISOR_EVENT_FENCE_REQUIRED) return pending;
    return supervisor_apply_fence(pending.handle, now_ms);
}

supervisor_event_t supervisor_apply_fence(supervisor_handle_t handle,
                                          uint64_t now_ms) {
    supervisor_event_t none = {.type = SUPERVISOR_EVENT_NONE};
    supervisor_fence_ops_t fence_ops;
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_ISOLATED) {
        supervisor_unlock(flags);
        return none;
    }
    if (fence_ops_read(handle.slot, &fence_ops) != 0) {
        state.state = SUPERVISOR_SAFE_STATE;
        (void)state_write(handle.slot, &state);
        supervisor_event_t result = event(
            SUPERVISOR_EVENT_SAFE_STATE_REQUIRED, handle.slot, &state);
        supervisor_unlock(flags);
        return result;
    }
    state.state = SUPERVISOR_FENCING;
    if (state_write(handle.slot, &state) != 0) {
        supervisor_unlock(flags);
        return event(SUPERVISOR_EVENT_SAFE_STATE_REQUIRED, handle.slot, &state);
    }
    supervisor_unlock(flags);

    /* Hardware/service fencing may require interrupt-driven I/O. Never run it
     * under the supervisor's IRQ-off state lock. Verification is deliberately
     * separate: a successful write alone is not safety evidence. */
    bool fenced = fence_ops.apply(fence_ops.context) &&
                  fence_ops.verify(fence_ops.context);

    flags = supervisor_lock();
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_FENCING) {
        supervisor_unlock(flags);
        return none;
    }
    if (!fenced) {
        state.state = SUPERVISOR_SAFE_STATE;
        (void)state_write(handle.slot, &state);
        supervisor_event_t result = event(SUPERVISOR_EVENT_SAFE_STATE_REQUIRED,
                                           handle.slot, &state);
        supervisor_unlock(flags);
        return result;
    }
    supervisor_event_type_t type;
    if (state.restart_count >= state.restart_budget) {
        state.state = SUPERVISOR_SAFE_STATE;
        type = SUPERVISOR_EVENT_SAFE_STATE_REQUIRED;
    } else {
        ++state.restart_count;
        ++state.epoch;
        if (state.epoch == 0) state.epoch = 1U;
        state.state = SUPERVISOR_RECOVERING;
        state.deadline_ms = deadline_after(now_ms, state.recovery_timeout_ms);
        type = SUPERVISOR_EVENT_RESTART_REQUIRED;
    }
    if (state_write(handle.slot, &state) != 0)
        type = SUPERVISOR_EVENT_SAFE_STATE_REQUIRED;
    supervisor_event_t result = event(type, handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

int supervisor_report_self_test(supervisor_handle_t handle, bool passed,
                                uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 ||
        (state.state != SUPERVISOR_RECOVERING &&
         state.state != SUPERVISOR_STARTING)) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = passed ? SUPERVISOR_STARTING : SUPERVISOR_ISOLATED;
    state.progress_marker = 0;
    state.deadline_ms = deadline_after(now_ms, state.recovery_timeout_ms);
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

bool supervisor_output_allowed(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    bool allowed = resolve(handle, &state) == 0 &&
                   (state.state == SUPERVISOR_HEALTHY ||
                    state.state == SUPERVISOR_IDLE);
    supervisor_unlock(flags);
    return allowed;
}

#ifdef REIST_HOST_TEST
int supervisor_test_corrupt_fence_ops(supervisor_handle_t handle,
                                      bool corrupt_both_copies) {
    supervisor_state_t state;
    if (resolve(handle, &state) != 0) return -1;
    critical_object_t *object = &slots[handle.slot].protected_fence_ops;
    object->primary.crc32 ^= 1U;
    if (corrupt_both_copies) object->shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_corrupt_descriptor(supervisor_handle_t handle,
                                       bool corrupt_both_copies) {
    supervisor_state_t state;
    if (resolve(handle, &state) != 0) return -1;
    critical_object_t *object = &slots[handle.slot].protected_descriptor;
    object->primary.crc32 ^= 1U;
    if (corrupt_both_copies) object->shadow.crc32 ^= 2U;
    return 0;
}
#endif
