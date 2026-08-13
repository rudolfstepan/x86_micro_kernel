#include "include/kernel/supervisor.h"

#include "include/kernel/critical_object.h"
#ifndef REIST_HOST_TEST
#include "arch/x86/include/interrupt.h"
#endif

typedef struct {
    uint32_t generation;
    uint32_t state;
    uint32_t epoch;
    uint32_t progress_marker;
    uint32_t restart_count;
    uint32_t heartbeat_timeout_ms;
    uint32_t recovery_timeout_ms;
    uint32_t restart_budget;
    uint64_t deadline_ms;
} supervisor_state_t;

typedef struct {
    bool occupied;
    char name[SUPERVISOR_NAME_CAPACITY];
    critical_object_t protected_state;
} supervisor_slot_t;

static supervisor_slot_t slots[SUPERVISOR_MAX_DOMAINS];
static uint32_t next_generation = 1U;

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
           state->state <= SUPERVISOR_SAFE_STATE &&
           state->heartbeat_timeout_ms != 0 &&
           state->recovery_timeout_ms != 0 && state->restart_budget != 0 &&
           state->restart_count <= state->restart_budget;
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
    if (handle.slot >= SUPERVISOR_MAX_DOMAINS ||
        !slots[handle.slot].occupied || state_read(handle.slot, state) != 0 ||
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
    for (uint32_t slot = 0; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        slots[slot].occupied = false;
        slots[slot].name[0] = '\0';
    }
    next_generation = 1U;
    supervisor_unlock(flags);
}

int supervisor_register(const char *name, const supervisor_config_t *config,
                        uint64_t now_ms, supervisor_handle_t *handle_out) {
    if (name == 0 || name[0] == '\0' || config == 0 || handle_out == 0 ||
        config->heartbeat_timeout_ms == 0 || config->recovery_timeout_ms == 0 ||
        config->restart_budget == 0) return -1;
    uint32_t flags = supervisor_lock();
    uint32_t slot = 0;
    while (slot < SUPERVISOR_MAX_DOMAINS && slots[slot].occupied) ++slot;
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
    uint32_t index = 0;
    while (index + 1U < SUPERVISOR_NAME_CAPACITY && name[index] != '\0') {
        slots[slot].name[index] = name[index];
        ++index;
    }
    slots[slot].name[index] = '\0';
    slots[slot].occupied = true;
    handle_out->slot = slot;
    handle_out->generation = generation;
    handle_out->epoch = state.epoch;
    supervisor_unlock(flags);
    return 0;
}

int supervisor_report_progress(supervisor_handle_t handle,
                               uint32_t progress_marker, uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 ||
        (state.state != SUPERVISOR_STARTING && state.state != SUPERVISOR_HEALTHY &&
         state.state != SUPERVISOR_DEGRADED) ||
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

supervisor_event_t supervisor_poll(uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_event_t none = {.type = SUPERVISOR_EVENT_NONE};
    for (uint32_t slot = 0; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        if (!slots[slot].occupied) continue;
        supervisor_state_t state;
        if (state_read(slot, &state) != 0) {
            supervisor_event_t result = {
                .type = SUPERVISOR_EVENT_SAFE_STATE_REQUIRED,
                .handle = {.slot = slot, .generation = 0, .epoch = 0},
            };
            supervisor_unlock(flags);
            return result;
        }
        if ((state.state == SUPERVISOR_STARTING || state.state == SUPERVISOR_HEALTHY ||
             state.state == SUPERVISOR_DEGRADED || state.state == SUPERVISOR_RECOVERING) &&
            now_ms >= state.deadline_ms) {
            state.state = SUPERVISOR_ISOLATED;
            if (state_write(slot, &state) != 0) {
                supervisor_unlock(flags);
                return event(SUPERVISOR_EVENT_SAFE_STATE_REQUIRED, slot, &state);
            }
            supervisor_event_t result = event(SUPERVISOR_EVENT_FENCE_REQUIRED,
                                               slot, &state);
            supervisor_unlock(flags);
            return result;
        }
    }
    supervisor_unlock(flags);
    return none;
}

supervisor_event_t supervisor_ack_fenced(supervisor_handle_t handle,
                                         uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_ISOLATED) {
        supervisor_event_t none = {.type = SUPERVISOR_EVENT_NONE};
        supervisor_unlock(flags);
        return none;
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
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_RECOVERING) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = passed ? SUPERVISOR_STARTING : SUPERVISOR_SAFE_STATE;
    state.progress_marker = 0;
    state.deadline_ms = deadline_after(now_ms, state.recovery_timeout_ms);
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

bool supervisor_output_allowed(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    bool allowed = resolve(handle, &state) == 0 && state.state == SUPERVISOR_HEALTHY;
    supervisor_unlock(flags);
    return allowed;
}
