/**
 * @file kernel/init/component_control.c
 * @brief Vermittelt administrative Down/Up-Übergänge registrierter Komponenten.
 *
 * Layer: Ring-0 component-control service.
 * Contract: Eine Operation besitzt genau einen Owner und eine monotone Deadline.
 * Safety: Timeout führt in einen expliziten Fehlerzustand statt zu implizitem Up.
 */
#include "include/kernel/component_control.h"

#include <stddef.h>

#include "arch/x86/include/interrupt.h"
#include "drivers/net/netdev.h"
#include "include/kernel/critical_object.h"
#include "include/kernel/storage_service.h"
#include "include/kernel/supervisor.h"
#include "kernel/time/pit.h"
#include "lib/libc/string.h"

#define COMPONENT_STATE_VERSION 1U

typedef bool (*component_action_t)(uint64_t deadline_ms);
typedef bool (*component_self_test_t)(void);

typedef struct {
    const char *name;
    uint32_t flags;
    uint32_t dependency_mask;
    component_action_t down;
    component_action_t up;
    component_self_test_t ready;
} component_descriptor_t;

typedef struct {
    uint32_t component;
    uint32_t state;
    uint32_t generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    int32_t last_error;
} component_state_t;

#define COMPONENT_DEP(id) (1U << (id))

static const component_descriptor_t components[COMPONENT_CONTROL_MAX_COMPONENTS] = {
    [COMPONENT_ID_SCHEDULER] = {
        "scheduler", COMPONENT_FLAG_PROTECTED, 0U, NULL, NULL, NULL,
    },
    [COMPONENT_ID_CLOCK] = {
        "monotonic-clock", COMPONENT_FLAG_PROTECTED,
        COMPONENT_DEP(COMPONENT_ID_SCHEDULER), NULL, NULL, NULL,
    },
    [COMPONENT_ID_INTERRUPTS] = {
        "interrupt-core", COMPONENT_FLAG_PROTECTED,
        COMPONENT_DEP(COMPONENT_ID_SCHEDULER), NULL, NULL, NULL,
    },
    [COMPONENT_ID_ROOT_STORAGE] = {
        "root-storage", COMPONENT_FLAG_PROTECTED | COMPONENT_FLAG_DRIVER,
        COMPONENT_DEP(COMPONENT_ID_CLOCK) |
            COMPONENT_DEP(COMPONENT_ID_INTERRUPTS),
        NULL, NULL, NULL,
    },
    [COMPONENT_ID_NETWORK_DRIVER] = {
        "network-driver", COMPONENT_FLAG_MANAGEABLE | COMPONENT_FLAG_DRIVER,
        COMPONENT_DEP(COMPONENT_ID_CLOCK) |
            COMPONENT_DEP(COMPONENT_ID_INTERRUPTS),
        netdev_component_down, netdev_component_up, netdev_component_ready,
    },
    [COMPONENT_ID_STORAGE_SERVICE] = {
        "storage-service", COMPONENT_FLAG_MANAGEABLE | COMPONENT_FLAG_SERVICE,
        COMPONENT_DEP(COMPONENT_ID_CLOCK) |
            COMPONENT_DEP(COMPONENT_ID_ROOT_STORAGE),
        storage_service_component_down, storage_service_component_up,
        storage_service_component_ready,
    },
    [COMPONENT_ID_NETWORK_SERVICE] = {
        "network-service", COMPONENT_FLAG_MANAGEABLE | COMPONENT_FLAG_SERVICE,
        COMPONENT_DEP(COMPONENT_ID_CLOCK) |
            COMPONENT_DEP(COMPONENT_ID_NETWORK_DRIVER),
        supervisor_probe_component_down, supervisor_probe_component_up,
        supervisor_probe_component_ready,
    },
};

static critical_object_t protected_states[COMPONENT_CONTROL_MAX_COMPONENTS];
static bool initialized;
static volatile uint32_t operation_busy;
static volatile int32_t operation_owner_pid;
static volatile uint32_t operation_owner_generation;
static volatile bool operation_cancel_requested;

_Static_assert(COMPONENT_CONTROL_MAX_COMPONENTS < 32U,
               "component dependency mask exceeds 32 bits");
_Static_assert(sizeof(component_state_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "component state exceeds protected payload");

static bool state_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(component_state_t)) return false;
    const component_state_t *state = payload;
    if (state->component >= COMPONENT_CONTROL_MAX_COMPONENTS ||
        state->generation == 0U ||
        state->state < COMPONENT_STATE_READY ||
        state->state > COMPONENT_STATE_FAILED) return false;
    bool owned = state->owner_pid > 0 && state->owner_generation != 0U;
    bool unowned = state->owner_pid == 0 && state->owner_generation == 0U;
    if (!owned && !unowned) return false;
    if ((state->state == COMPONENT_STATE_READY ||
         state->state == COMPONENT_STATE_DOWN ||
         state->state == COMPONENT_STATE_FAILED) && !unowned) return false;
    return true;
}

static int state_read(uint32_t component, component_state_t *state) {
    if (component >= COMPONENT_CONTROL_MAX_COMPONENTS || state == NULL)
        return -22;
    size_t length = 0U;
    uint32_t flags = irq_save();
    int result = critical_object_read(&protected_states[component],
        COMPONENT_STATE_VERSION, state, sizeof(*state), &length,
        state_valid) < 0 || length != sizeof(*state) ? -84 : 0;
    irq_restore(flags);
    return result;
}

static int state_write(uint32_t component, const component_state_t *state) {
    if (component >= COMPONENT_CONTROL_MAX_COMPONENTS || state == NULL ||
        state->component != component) return -22;
    uint32_t flags = irq_save();
    int result = critical_object_update(&protected_states[component],
        COMPONENT_STATE_VERSION, state, sizeof(*state), state_valid) == 0
            ? 0 : -84;
    irq_restore(flags);
    return result;
}

static uint64_t deadline_after(uint64_t now_ms, uint32_t timeout_ms) {
    return UINT64_MAX - now_ms < timeout_ms
        ? UINT64_MAX : now_ms + timeout_ms;
}

static bool deadline_expired(uint64_t deadline_ms) {
    return pit_monotonic_ms() >= deadline_ms;
}

static bool dependencies_ready(uint32_t component) {
    uint32_t dependencies = components[component].dependency_mask;
    for (uint32_t index = 0U; index < COMPONENT_CONTROL_MAX_COMPONENTS;
         ++index) {
        if ((dependencies & COMPONENT_DEP(index)) == 0U) continue;
        component_state_t state;
        if (state_read(index, &state) != 0 ||
            state.state != COMPONENT_STATE_READY) return false;
    }
    return true;
}

static bool ready_dependent_exists(uint32_t component) {
    uint32_t dependency = COMPONENT_DEP(component);
    for (uint32_t index = 0U; index < COMPONENT_CONTROL_MAX_COMPONENTS;
         ++index) {
        if ((components[index].dependency_mask & dependency) == 0U) continue;
        component_state_t state;
        if (state_read(index, &state) != 0) return true;
        if (state.state == COMPONENT_STATE_READY ||
            state.state == COMPONENT_STATE_STARTING ||
            state.state == COMPONENT_STATE_QUIESCING) return true;
    }
    return false;
}

static void finish_state(component_state_t *state, uint32_t final_state,
                         int error) {
    state->state = final_state;
    state->owner_pid = 0;
    state->owner_generation = 0U;
    state->last_error = error;
}

static int begin_transition(component_state_t *state, uint32_t transition,
                            int pid, uint32_t process_generation) {
    if (state->generation == UINT32_MAX) return COMPONENT_ESTATE;
    ++state->generation;
    state->state = transition;
    state->owner_pid = pid;
    state->owner_generation = process_generation;
    state->last_error = 0;
    return state_write(state->component, state);
}

static int perform_down(uint32_t component, int pid,
                        uint32_t process_generation, uint64_t deadline_ms) {
    const component_descriptor_t *descriptor = &components[component];
    component_state_t state;
    int result = state_read(component, &state);
    if (result != 0) return result;
    if ((descriptor->flags & COMPONENT_FLAG_PROTECTED) != 0U)
        return COMPONENT_EPROTECTED;
    if (state.state == COMPONENT_STATE_DOWN) return 0;
    if (state.state == COMPONENT_STATE_QUIESCING ||
        state.state == COMPONENT_STATE_STARTING) return -16;
    if (ready_dependent_exists(component)) return COMPONENT_EDEPENDENCY;
    result = begin_transition(&state, COMPONENT_STATE_QUIESCING, pid,
                              process_generation);
    if (result != 0) return result;

    bool stopped = descriptor->down != NULL && descriptor->down(deadline_ms);
    if (operation_cancel_requested) {
        stopped = false;
        result = -125;
    } else if (!stopped) {
        result = deadline_expired(deadline_ms) ? -110 : -5;
    }
    finish_state(&state, stopped ? COMPONENT_STATE_DOWN : COMPONENT_STATE_FAILED,
                 stopped ? 0 : result);
    int write_result = state_write(component, &state);
    return write_result != 0 ? write_result : (stopped ? 0 : result);
}

static int perform_up(uint32_t component, int pid,
                      uint32_t process_generation, uint64_t deadline_ms) {
    const component_descriptor_t *descriptor = &components[component];
    component_state_t state;
    int result = state_read(component, &state);
    if (result != 0) return result;
    if ((descriptor->flags & COMPONENT_FLAG_PROTECTED) != 0U)
        return COMPONENT_EPROTECTED;
    if (state.state == COMPONENT_STATE_READY) return 0;
    if (state.state == COMPONENT_STATE_QUIESCING ||
        state.state == COMPONENT_STATE_STARTING) return -16;
    if (!dependencies_ready(component)) return COMPONENT_EDEPENDENCY;
    result = begin_transition(&state, COMPONENT_STATE_STARTING, pid,
                              process_generation);
    if (result != 0) return result;

    bool started = descriptor->up != NULL && descriptor->up(deadline_ms) &&
                   descriptor->ready != NULL && descriptor->ready();
    if (operation_cancel_requested) {
        started = false;
        result = -125;
    } else if (!started) {
        result = deadline_expired(deadline_ms) ? -110 : -5;
    }
    if (!started && descriptor->down != NULL)
        (void)descriptor->down(deadline_ms);
    finish_state(&state, started ? COMPONENT_STATE_READY : COMPONENT_STATE_FAILED,
                 started ? 0 : result);
    int write_result = state_write(component, &state);
    return write_result != 0 ? write_result : (started ? 0 : result);
}

static void publish_result(uint32_t component, const component_state_t *state,
                           component_control_result_t *result) {
    const component_descriptor_t *descriptor = &components[component];
    memset(result, 0, sizeof(*result));
    result->version = COMPONENT_CONTROL_ABI_VERSION;
    result->struct_size = sizeof(*result);
    result->component = component;
    result->state = state->state;
    result->generation = state->generation;
    result->flags = descriptor->flags;
    if (state->state != COMPONENT_STATE_READY)
        result->flags |= COMPONENT_FLAG_FENCED;
    result->dependency_mask = descriptor->dependency_mask;
    result->last_error = state->last_error;
    size_t length = strlen(descriptor->name);
    if (length >= sizeof(result->name)) length = sizeof(result->name) - 1U;
    memcpy(result->name, descriptor->name, length);
    result->name[length] = '\0';
}

bool component_control_init(void) {
    if (initialized) return true;
    for (uint32_t component = 0U;
         component < COMPONENT_CONTROL_MAX_COMPONENTS; ++component) {
        uint32_t state_value = COMPONENT_STATE_READY;
        if ((components[component].flags & COMPONENT_FLAG_MANAGEABLE) != 0U &&
            (components[component].ready == NULL ||
             !components[component].ready())) state_value = COMPONENT_STATE_DOWN;
        component_state_t state = {
            .component = component,
            .state = state_value,
            .generation = 1U,
        };
        if (critical_object_init(&protected_states[component],
                COMPONENT_STATE_VERSION, &state, sizeof(state)) != 0)
            return false;
    }
    operation_busy = 0U;
    operation_owner_pid = 0;
    operation_owner_generation = 0U;
    operation_cancel_requested = false;
    initialized = true;
    return true;
}

void component_control_poll(uint64_t now_ms) {
    (void)now_ms;
    if (!initialized || operation_busy != 0U) return;
    for (uint32_t component = 0U;
         component < COMPONENT_CONTROL_MAX_COMPONENTS; ++component) {
        const component_descriptor_t *descriptor = &components[component];
        if ((descriptor->flags & COMPONENT_FLAG_MANAGEABLE) == 0U ||
            descriptor->ready == NULL || !descriptor->ready()) continue;
        component_state_t state;
        if (state_read(component, &state) != 0 || state.generation != 1U ||
            state.state != COMPONENT_STATE_DOWN) continue;
        finish_state(&state, COMPONENT_STATE_READY, 0);
        (void)state_write(component, &state);
    }
}

int component_control_execute(int pid, uint32_t process_generation,
        const component_control_request_t *request,
        component_control_result_t *result, uint64_t now_ms) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != COMPONENT_CONTROL_ABI_VERSION ||
        request->struct_size != sizeof(*request) ||
        request->component >= COMPONENT_CONTROL_MAX_COMPONENTS ||
        request->command > COMPONENT_COMMAND_RESTART ||
        request->timeout_ms > COMPONENT_CONTROL_TIMEOUT_MAX_MS) return -22;
    component_state_t state;
    int status = state_read(request->component, &state);
    if (status != 0) return status;
    if (request->expected_generation != 0U &&
        request->expected_generation != state.generation) return -116;
    if (request->command == COMPONENT_COMMAND_STATUS) {
        publish_result(request->component, &state, result);
        return 0;
    }
    if ((components[request->component].flags & COMPONENT_FLAG_PROTECTED) != 0U)
        return COMPONENT_EPROTECTED;
    if (__sync_lock_test_and_set(&operation_busy, 1U) != 0U) return -16;
    operation_owner_pid = pid;
    operation_owner_generation = process_generation;
    operation_cancel_requested = false;
    uint32_t timeout_ms = request->timeout_ms == 0U
        ? COMPONENT_CONTROL_TIMEOUT_DEFAULT_MS : request->timeout_ms;
    uint64_t deadline_ms = deadline_after(now_ms, timeout_ms);

    if (request->command == COMPONENT_COMMAND_DOWN) {
        status = perform_down(request->component, pid, process_generation,
                              deadline_ms);
    } else if (request->command == COMPONENT_COMMAND_UP) {
        status = perform_up(request->component, pid, process_generation,
                            deadline_ms);
    } else {
        status = perform_down(request->component, pid, process_generation,
                              deadline_ms);
        if (status == 0 && !deadline_expired(deadline_ms))
            status = perform_up(request->component, pid, process_generation,
                                deadline_ms);
        else if (status == 0)
            status = -110;
    }
    operation_owner_pid = 0;
    operation_owner_generation = 0U;
    operation_cancel_requested = false;
    __sync_lock_release(&operation_busy);

    int read_result = state_read(request->component, &state);
    if (read_result != 0) return read_result;
    publish_result(request->component, &state, result);
    return status;
}

void component_control_process_cleanup(int pid, uint32_t process_generation) {
    if (!initialized || pid <= 0 || process_generation == 0U) return;
    if (operation_busy != 0U && operation_owner_pid == pid &&
        operation_owner_generation == process_generation)
        operation_cancel_requested = true;
}
