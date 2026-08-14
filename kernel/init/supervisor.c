#include "include/kernel/supervisor.h"

#include "include/kernel/critical_object.h"
#ifndef REIST_HOST_TEST
#include "arch/x86/include/interrupt.h"
#include "include/kernel/panic.h"
#include "include/kernel/output_fence.h"
#include "drivers/net/netstack.h"
#include "drivers/net/netdev.h"
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

_Static_assert(sizeof(supervisor_probe_authority_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "probe authority exceeds critical object payload");
_Static_assert(sizeof(supervisor_network_probe_context_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "network context exceeds critical object payload");

typedef struct {
    critical_object_t protected_descriptor;
    critical_object_t protected_fence_ops;
    critical_object_t protected_state;
} supervisor_slot_t;

static supervisor_slot_t slots[SUPERVISOR_MAX_DOMAINS];
static uint32_t next_generation = 1U;
static uint64_t last_deadline_check_ms;
static uint32_t next_poll_slot;
static critical_object_t protected_network_degradation_stats;

#define SUPERVISOR_CHECK_INTERVAL_MS 10U
#define SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS 250U

#ifndef REIST_HOST_TEST
typedef struct {
    bool active;
    bool fenced;
    bool healthy;
    supervisor_handle_t handle;
    int pid;
    uint32_t process_generation;
    uint32_t launch_count;
    uint32_t endpoint_handle;
    uint64_t last_network_probe_ms;
    supervisor_protected_probe_authority_t network_probe_authority;
    supervisor_protected_network_context_t network_probe_context;
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

void supervisor_probe_authority_init(supervisor_probe_authority_t *authority) {
    if (authority == NULL) return;
    *authority = (supervisor_probe_authority_t){.next_id = 1U};
}

int supervisor_probe_authority_begin(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms, uint32_t timeout_ms,
                                     uint32_t *probe_id_out) {
    if (authority == NULL || probe_id_out == NULL || timeout_ms == 0U)
        return -22;
    if (authority->active_id != 0U) return -11;
    if (authority->next_id == 0U || authority->next_id > UINT32_MAX)
        return -75;
    uint32_t probe_id = (uint32_t)authority->next_id++;
    authority->active_id = probe_id;
    authority->deadline_ms = deadline_after(now_ms, timeout_ms);
    *probe_id_out = probe_id;
    return 0;
}

bool supervisor_probe_authority_take(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms,
                                     uint32_t *probe_id_out) {
    if (authority == NULL || probe_id_out == NULL ||
        authority->active_id == 0U) return false;
    if (now_ms >= authority->deadline_ms) {
        authority->active_id = 0U;
        return false;
    }
    *probe_id_out = authority->active_id;
    authority->active_id = 0U;
    return true;
}

bool supervisor_probe_authority_expire(supervisor_probe_authority_t *authority,
                                       uint64_t now_ms) {
    if (authority == NULL || authority->active_id == 0U ||
        now_ms < authority->deadline_ms) return false;
    authority->active_id = 0U;
    return true;
}

void supervisor_probe_authority_cancel(supervisor_probe_authority_t *authority) {
    if (authority != NULL) authority->active_id = 0U;
}

static bool probe_authority_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_probe_authority_t))
        return false;
    const supervisor_probe_authority_t *authority = payload;
    return authority->next_id >= 1U &&
           authority->next_id <= (uint64_t)UINT32_MAX + 1U &&
           (authority->active_id == 0U ||
            authority->active_id < authority->next_id);
}

static int protected_probe_authority_read(
        supervisor_protected_probe_authority_t *protected_authority,
        supervisor_probe_authority_t *authority) {
    if (protected_authority == NULL || authority == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_authority->object, SUPERVISOR_PROBE_AUTHORITY_VERSION,
        authority, sizeof(*authority), &length, probe_authority_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static int protected_probe_authority_write(
        supervisor_protected_probe_authority_t *protected_authority,
        const supervisor_probe_authority_t *authority) {
    return critical_object_update(
        &protected_authority->object, SUPERVISOR_PROBE_AUTHORITY_VERSION,
        authority, sizeof(*authority), probe_authority_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_probe_authority_init(
        supervisor_protected_probe_authority_t *protected_authority) {
    if (protected_authority == NULL) return -22;
    supervisor_probe_authority_t authority;
    supervisor_probe_authority_init(&authority);
    uint32_t flags = supervisor_lock();
    int result = critical_object_init(
        &protected_authority->object, SUPERVISOR_PROBE_AUTHORITY_VERSION,
        &authority, sizeof(authority)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_begin(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms, uint32_t timeout_ms, uint32_t *probe_id_out) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result != 0) {
        supervisor_unlock(flags);
        return result;
    }
    result = supervisor_probe_authority_begin(&authority, now_ms, timeout_ms,
                                              probe_id_out);
    if (result == 0)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_take(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms, uint32_t *probe_id_out) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result == 0 &&
        !supervisor_probe_authority_take(&authority, now_ms, probe_id_out))
        result = -11;
    if (result == 0)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_expire(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    bool expired = result == 0 &&
        supervisor_probe_authority_expire(&authority, now_ms);
    if (expired)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result == 0 && expired ? 1 : result;
}

int supervisor_protected_probe_authority_cancel(
        supervisor_protected_probe_authority_t *protected_authority) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result == 0) {
        supervisor_probe_authority_cancel(&authority);
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    }
    supervisor_unlock(flags);
    return result;
}

static bool network_context_valid(const void *payload, size_t length) {
    if (payload == NULL ||
        length != sizeof(supervisor_network_probe_context_t)) return false;
    const supervisor_network_probe_context_t *context = payload;
    if (context->reserved[0] != 0U || context->reserved[1] != 0U) return false;
    bool empty_mac = true;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (context->local_mac[index] != 0U) empty_mac = false;
    bool empty = context->gateway == 0U && context->local_ip == 0U && empty_mac;
    bool complete = context->gateway != 0U && context->local_ip != 0U &&
                    !empty_mac;
    return (empty && context->delivered_id == 0U) || complete;
}

static int network_context_read(
        supervisor_protected_network_context_t *protected_context,
        supervisor_network_probe_context_t *context) {
    if (protected_context == NULL || context == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_context->object, SUPERVISOR_NETWORK_CONTEXT_VERSION,
        context, sizeof(*context), &length, network_context_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static int network_context_write(
        supervisor_protected_network_context_t *protected_context,
        const supervisor_network_probe_context_t *context) {
    return critical_object_update(
        &protected_context->object, SUPERVISOR_NETWORK_CONTEXT_VERSION,
        context, sizeof(*context), network_context_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_network_context_init(
        supervisor_protected_network_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_network_probe_context_t context = {0};
    uint32_t flags = supervisor_lock();
    int result = critical_object_init(
        &protected_context->object, SUPERVISOR_NETWORK_CONTEXT_VERSION,
        &context, sizeof(context)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_prepare(
        supervisor_protected_network_context_t *protected_context,
        uint32_t gateway, uint32_t local_ip, const uint8_t local_mac[6]) {
    if (protected_context == NULL || gateway == 0U || local_ip == 0U ||
        local_mac == NULL) return -22;
    supervisor_network_probe_context_t context = {
        .gateway = gateway, .local_ip = local_ip,
    };
    for (uint32_t index = 0U; index < 6U; ++index)
        context.local_mac[index] = local_mac[index];
    if (!network_context_valid(&context, sizeof(context))) return -22;
    uint32_t flags = supervisor_lock();
    int result = network_context_write(protected_context, &context);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_snapshot(
        supervisor_protected_network_context_t *protected_context,
        supervisor_network_probe_context_t *snapshot_out) {
    uint32_t flags = supervisor_lock();
    int result = network_context_read(protected_context, snapshot_out);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_publish(
        supervisor_protected_network_context_t *protected_context,
        uint32_t probe_id) {
    if (probe_id == 0U) return -22;
    supervisor_network_probe_context_t context;
    uint32_t flags = supervisor_lock();
    int result = network_context_read(protected_context, &context);
    if (result == 0 && (context.gateway == 0U || context.delivered_id != 0U))
        result = -11;
    if (result == 0) {
        context.delivered_id = probe_id;
        result = network_context_write(protected_context, &context);
    }
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_consume(
        supervisor_protected_network_context_t *protected_context,
        uint32_t probe_id) {
    if (probe_id == 0U) return -22;
    supervisor_network_probe_context_t context;
    uint32_t flags = supervisor_lock();
    int result = network_context_read(protected_context, &context);
    if (result == 0 && context.delivered_id != probe_id) result = -13;
    if (result == 0) {
        context.delivered_id = 0U;
        result = network_context_write(protected_context, &context);
    }
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_clear(
        supervisor_protected_network_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_network_probe_context_t context = {0};
    uint32_t flags = supervisor_lock();
    int result = network_context_write(protected_context, &context);
    supervisor_unlock(flags);
    return result;
}

void supervisor_network_degradation_init(
        supervisor_network_degradation_stats_t *stats) {
    if (stats != NULL) *stats = (supervisor_network_degradation_stats_t){0};
}

static void saturating_increment(uint32_t *value) {
    if (*value != UINT32_MAX) ++*value;
}

void supervisor_network_degradation_record(
        supervisor_network_degradation_stats_t *stats,
        supervisor_network_degradation_reason_t reason) {
    if (stats == NULL) return;
    if (reason == SUPERVISOR_NETWORK_DEGRADED_EXPIRED)
        saturating_increment(&stats->expired);
    else if (reason == SUPERVISOR_NETWORK_DEGRADED_QUEUE)
        saturating_increment(&stats->queue_fallback);
    else if (reason == SUPERVISOR_NETWORK_DEGRADED_SEMANTIC)
        saturating_increment(&stats->semantic_reject);
}

static bool network_degradation_valid(const void *payload, size_t length) {
    return payload != NULL &&
           length == sizeof(supervisor_network_degradation_stats_t);
}

static int network_degradation_read(
        supervisor_network_degradation_stats_t *stats) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_network_degradation_stats,
        SUPERVISOR_NETWORK_DEGRADATION_VERSION, stats, sizeof(*stats),
        &length, network_degradation_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static void network_degradation_record(
        supervisor_network_degradation_reason_t reason) {
    uint32_t flags = supervisor_lock();
    supervisor_network_degradation_stats_t stats;
    if (network_degradation_read(&stats) == 0) {
        supervisor_network_degradation_record(&stats, reason);
        (void)critical_object_update(
            &protected_network_degradation_stats,
            SUPERVISOR_NETWORK_DEGRADATION_VERSION, &stats, sizeof(stats),
            network_degradation_valid);
    }
    supervisor_unlock(flags);
}

int supervisor_network_degradation_snapshot(
        supervisor_network_degradation_stats_t *stats_out) {
    if (stats_out == NULL) return -22;
    uint32_t flags = supervisor_lock();
    int result = network_degradation_read(stats_out);
    supervisor_unlock(flags);
    return result;
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
    supervisor_network_degradation_stats_t network_stats;
    supervisor_network_degradation_init(&network_stats);
    if (critical_object_init(&protected_network_degradation_stats,
                             SUPERVISOR_NETWORK_DEGRADATION_VERSION,
                             &network_stats, sizeof(network_stats)) != 0) {
#ifndef REIST_HOST_TEST
        panic("Unable to initialize supervisor network statistics");
#endif
    }
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
    runtime->healthy = false;
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->network_probe_authority);
    (void)supervisor_protected_network_context_clear(
        &runtime->network_probe_context);
    if (process_identity_alive(runtime->pid, runtime->process_generation)) {
        (void)process_terminate(runtime->pid);
    }
    return true;
}

static void probe_report_recovery_pair(uint32_t launch_count) {
    /* Publish evidence only after the replacement passed self-test. Keep the
     * bounded pair together in the serial record. */
    scheduler_preempt_disable();
    if (launch_count == 2U)
        printf("\nREIST_PROBE CRASH_DETECTED\n"
               "REIST_PROBE CRASH_RECOVERED\n");
    else if (launch_count == 3U)
        printf("\nREIST_PROBE HANG_DETECTED\n"
               "REIST_PROBE HANG_RECOVERED\n");
    else if (launch_count == 4U)
        printf("\nREIST_PROBE INVALID_REPLY_DETECTED\n"
               "REIST_PROBE INVALID_RECOVERED\n"
               "REIST_PROBE REINTEGRATED\n"
               "REIST_PROBE RECOVERY_SEQUENCE_OK\n");
    else if (launch_count >= 5U)
        printf("\nREIST_NETWORK SERVICE_CRASH_RECOVERED\n");
    scheduler_preempt_enable();
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
    probe_runtime.healthy = false;
    ++probe_runtime.launch_count;
    return true;
}

bool supervisor_start_probe(uint64_t now_ms) {
    if (probe_runtime.active) return false;
    probe_runtime = (supervisor_probe_runtime_t){0};
    if (supervisor_protected_probe_authority_init(
            &probe_runtime.network_probe_authority) != 0) return false;
    if (supervisor_protected_network_context_init(
            &probe_runtime.network_probe_context) != 0) return false;
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
            probe_runtime.healthy = true;
            probe_report_recovery_pair(probe_runtime.launch_count);
        }
        if (result == 0) probe_runtime.healthy = true;
        return result;
    }
    if (report_type == REIST_REPORT_INVALID) {
        (void)supervisor_force_isolate(probe_runtime.handle);
        return -1;
    }
    if (report_type == REIST_REPORT_NETWORK_HEADER) {
        if (value != 0x0800U && value != 0x0806U) return -1;
        printf(value == 0x0806U ? "REIST_NETWORK RX_HEADER_ARP\n"
                                : "REIST_NETWORK RX_HEADER_IPV4\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_PROBE_ID) {
        if (supervisor_protected_network_context_consume(
                &probe_runtime.network_probe_context, value) != 0) return -1;
        printf("REIST_NETWORK PROBE_ID_OK\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_DEGRADED) {
        if (value != SUPERVISOR_NETWORK_DEGRADED_SEMANTIC) return -1;
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_SEMANTIC);
        return 0;
    }
    return -1;
}

int supervisor_service_connect(Process *client, uint32_t service_id,
                               uint32_t *handle_out) {
    if (client == NULL || handle_out == NULL ||
        service_id != REIST_SERVICE_DIAGNOSTIC || !probe_runtime.active ||
        probe_runtime.fenced || !probe_runtime.healthy ||
        probe_runtime.launch_count < 4U ||
        probe_runtime.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(probe_runtime.pid,
                                probe_runtime.process_generation)) {
        return -11;
    }
    int result = process_ipc_delegate_identity(
        probe_runtime.pid, probe_runtime.process_generation,
        probe_runtime.endpoint_handle, client,
        IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE);
    if (result == 0) *handle_out = probe_runtime.endpoint_handle;
    return result;
}

bool supervisor_network_submit_header(const uint8_t *frame, uint16_t length) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    if (frame == NULL || length < 42U || frame[12] != 0x08U ||
        frame[13] != 0x06U ||
        !probe_runtime.active ||
        probe_runtime.fenced || !probe_runtime.healthy ||
        probe_runtime.launch_count < 4U ||
        probe_runtime.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(probe_runtime.pid,
                                probe_runtime.process_generation)) {
        return false;
    }
    uint32_t probe_id;
    supervisor_network_probe_context_t network_context;
    int context_result = supervisor_protected_network_context_snapshot(
        &probe_runtime.network_probe_context, &network_context);
    if (context_result != 0) {
        (void)supervisor_force_isolate(probe_runtime.handle);
        return false;
    }
    if (supervisor_protected_probe_authority_take(
            &probe_runtime.network_probe_authority, pit_monotonic_ms(),
            &probe_id) != 0) return false;
    ipc_message_t message = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc_message_t),
        .length = 64U,
        .payload = {'N', 'E', 'T', 'R'},
    };
    for (uint32_t index = 0U; index < 42U; ++index)
        message.payload[index + 4U] = frame[index];
    uint32_t gateway = network_context.gateway;
    uint32_t local_ip = network_context.local_ip;
    for (uint32_t index = 0U; index < 4U; ++index) {
        uint32_t shift = 24U - index * 8U;
        message.payload[46U + index] = (uint8_t)(gateway >> shift);
        message.payload[50U + index] = (uint8_t)(local_ip >> shift);
    }
    for (uint32_t index = 0U; index < 6U; ++index)
        message.payload[54U + index] =
            network_context.local_mac[index];
    for (uint32_t index = 0U; index < 4U; ++index)
        message.payload[60U + index] =
            (uint8_t)(probe_id >> (index * 8U));
    if (supervisor_protected_network_context_publish(
            &probe_runtime.network_probe_context, probe_id) != 0) {
        (void)supervisor_force_isolate(probe_runtime.handle);
        return false;
    }
    int ingress = ipc_send_external_from_peer(
        probe_runtime.pid, probe_runtime.process_generation,
        probe_runtime.endpoint_handle, &message);
    /* A matching reply consumes exactly one probe authorization even when
     * bounded IPC pressure forces the frame back to the kernel path. */
    if (ingress != 0)
        (void)supervisor_protected_network_context_consume(
            &probe_runtime.network_probe_context, probe_id);
    if (ingress == -11) {
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        printf("REIST_NETWORK QUEUE_PRESSURE_FALLBACK\n");
    }
    return ingress == 0;
}

int supervisor_network_probe_request(int pid, uint32_t generation,
                                     uint64_t now_ms) {
    uint32_t ignored_probe_id;
    return supervisor_network_probe_request_id(pid, generation, now_ms,
                                               &ignored_probe_id);
}

int supervisor_network_probe_request_id(int pid, uint32_t generation,
                                        uint64_t now_ms,
                                        uint32_t *probe_id_out) {
    if (probe_id_out == NULL) return -22;
    if (!probe_runtime.active || probe_runtime.fenced ||
        !probe_runtime.healthy || pid != probe_runtime.pid ||
        generation != probe_runtime.process_generation ||
        !process_identity_alive(pid, generation)) return -13;
    if (probe_runtime.last_network_probe_ms != 0U &&
        now_ms - probe_runtime.last_network_probe_ms < 250U) return -11;
    uint32_t gateway = netstack_get_gateway();
    uint32_t local_ip = netstack_get_ip_address();
    uint8_t local_mac[6];
    if (gateway == 0U || local_ip == 0U ||
        !netdev_get_mac_address(local_mac)) return -19;
    uint32_t probe_id;
    int authority = supervisor_protected_probe_authority_begin(
        &probe_runtime.network_probe_authority, now_ms,
        SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS, &probe_id);
    if (authority != 0) return authority;
    probe_runtime.last_network_probe_ms = now_ms;
    int context_result = supervisor_protected_network_context_prepare(
        &probe_runtime.network_probe_context, gateway, local_ip, local_mac);
    if (context_result != 0) {
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.network_probe_authority);
        return context_result;
    }
    if (netstack_probe_gateway()) {
        *probe_id_out = probe_id;
        return 0;
    }
    (void)supervisor_protected_probe_authority_cancel(
        &probe_runtime.network_probe_authority);
    (void)supervisor_protected_network_context_clear(
        &probe_runtime.network_probe_context);
    return -19;
}

static void supervisor_worker(void) {
    for (;;) {
        /* Bounded network bottom half: IRQ handlers only acknowledge and set
         * pending flags, so foreground progress must not depend on a shell
         * command happening to poll the NIC. */
        netdev_poll();
        int authority_expiry = supervisor_protected_probe_authority_expire(
            &probe_runtime.network_probe_authority, pit_monotonic_ms());
        if (authority_expiry == 1) {
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
        } else if (authority_expiry == SUPERVISOR_EINTEGRITY &&
                   probe_runtime.active) {
            (void)supervisor_force_isolate(probe_runtime.handle);
        }
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

int supervisor_service_connect(struct Process *client, uint32_t service_id,
                               uint32_t *handle_out) {
    (void)client; (void)service_id; (void)handle_out;
    return -1;
}

bool supervisor_network_submit_header(const uint8_t *frame, uint16_t length) {
    (void)frame; (void)length;
    return false;
}
int supervisor_network_probe_request(int pid, uint32_t generation,
                                     uint64_t now_ms) {
    (void)pid; (void)generation; (void)now_ms;
    return -1;
}
int supervisor_network_probe_request_id(int pid, uint32_t generation,
                                        uint64_t now_ms,
                                        uint32_t *probe_id_out) {
    (void)pid; (void)generation; (void)now_ms; (void)probe_id_out;
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

int supervisor_test_corrupt_network_degradation(bool corrupt_both_copies) {
    protected_network_degradation_stats.primary.crc32 ^= 1U;
    if (corrupt_both_copies)
        protected_network_degradation_stats.shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_record_network_degradation(
        supervisor_network_degradation_reason_t reason) {
    network_degradation_record(reason);
    return 0;
}

int supervisor_test_corrupt_probe_authority(
        supervisor_protected_probe_authority_t *authority,
        bool corrupt_both_copies) {
    if (authority == NULL) return -22;
    authority->object.primary.crc32 ^= 1U;
    if (corrupt_both_copies) authority->object.shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_corrupt_network_context(
        supervisor_protected_network_context_t *context,
        bool corrupt_both_copies) {
    if (context == NULL) return -22;
    context->object.primary.crc32 ^= 1U;
    if (corrupt_both_copies) context->object.shadow.crc32 ^= 2U;
    return 0;
}
#endif
