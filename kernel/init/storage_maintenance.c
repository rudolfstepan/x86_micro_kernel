#include "include/kernel/storage_maintenance.h"

#include "include/kernel/critical_object.h"
#include "lib/libc/string.h"

#define STORAGE_MAINTENANCE_MAX_RESOURCES 32U
#define STORAGE_MAINTENANCE_TOKEN_SLOT_MASK 0x1FU
#define STORAGE_MAINTENANCE_TOKEN_GENERATION_MAX 0x07FFFFFFU
#define STORAGE_EINVAL (-22)
#define STORAGE_EBUSY (-16)
#define STORAGE_EACCES (-13)
#define STORAGE_ETIMEDOUT (-110)
#define STORAGE_EINTEGRITY (-84)

typedef struct {
    uint32_t active;
    int32_t pid;
    uint32_t process_generation;
    uint32_t media_fingerprint;
    uint32_t token_generation;
    uint64_t deadline_ms;
} storage_maintenance_state_t;

static critical_object_t lease_objects[STORAGE_MAINTENANCE_MAX_RESOURCES];
static bool initialized;

_Static_assert(sizeof(storage_maintenance_state_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "maintenance lease exceeds protected object capacity");

static bool state_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(storage_maintenance_state_t))
        return false;
    const storage_maintenance_state_t *state = payload;
    if (state->active > 1U || state->token_generation == 0U ||
        state->token_generation > STORAGE_MAINTENANCE_TOKEN_GENERATION_MAX)
        return false;
    if (state->active == 0U)
        return state->pid == 0 && state->process_generation == 0U &&
               state->media_fingerprint == 0U && state->deadline_ms == 0U;
    return state->pid > 0 && state->process_generation != 0U &&
           state->media_fingerprint != 0U && state->deadline_ms != 0U;
}

static bool deadline_expired(uint64_t deadline, uint64_t now_ms) {
    return deadline == 0U || now_ms >= deadline;
}

static uint64_t lease_deadline(uint64_t now_ms) {
    return UINT64_MAX - now_ms < STORAGE_MAINTENANCE_LEASE_MS
        ? UINT64_MAX : now_ms + STORAGE_MAINTENANCE_LEASE_MS;
}

static storage_maintenance_token_t make_token(uint32_t slot,
                                               uint32_t generation) {
    return (storage_maintenance_token_t)((generation << 5U) |
                                           ((slot + 1U) & 0x1FU));
}

static bool decode_token(storage_maintenance_token_t token, uint32_t *slot,
                         uint32_t *generation) {
    if (token == STORAGE_MAINTENANCE_INVALID_TOKEN || slot == NULL ||
        generation == NULL) return false;
    uint32_t encoded_slot = token & STORAGE_MAINTENANCE_TOKEN_SLOT_MASK;
    uint32_t encoded_generation = token >> 5U;
    if (encoded_slot == 0U || encoded_slot > STORAGE_MAINTENANCE_MAX_RESOURCES ||
        encoded_generation == 0U ||
        encoded_generation > STORAGE_MAINTENANCE_TOKEN_GENERATION_MAX)
        return false;
    *slot = encoded_slot - 1U;
    *generation = encoded_generation;
    return true;
}

bool storage_maintenance_init(void) {
    if (initialized) return true;
    for (uint32_t slot = 0U; slot < STORAGE_MAINTENANCE_MAX_RESOURCES; ++slot) {
        storage_maintenance_state_t state = {0};
        state.token_generation = 1U;
        if (critical_object_init(&lease_objects[slot],
                STORAGE_MAINTENANCE_VERSION, &state, sizeof(state)) != 0)
            return false;
    }
    initialized = true;
    return true;
}

int storage_maintenance_acquire(int pid, uint32_t process_generation,
        uint32_t resource, uint32_t media_fingerprint, uint64_t now_ms,
        storage_maintenance_token_t *token_out) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        resource >= STORAGE_MAINTENANCE_MAX_RESOURCES ||
        media_fingerprint == 0U || token_out == NULL) return STORAGE_EINVAL;
    storage_maintenance_state_t state;
    size_t length = 0U;
    if (critical_object_read(&lease_objects[resource],
            STORAGE_MAINTENANCE_VERSION, &state, sizeof(state), &length,
            state_valid) < 0 || length != sizeof(state))
        return STORAGE_EINTEGRITY;
    if (state.active != 0U && !deadline_expired(state.deadline_ms, now_ms))
        return STORAGE_EBUSY;
    if (state.token_generation == STORAGE_MAINTENANCE_TOKEN_GENERATION_MAX)
        return STORAGE_EINTEGRITY;
    /* Advance on every acquisition, including after an explicit release, so
       an old token can never regain authority on the same resource. */
    ++state.token_generation;
    state.active = 1U;
    state.pid = pid;
    state.process_generation = process_generation;
    state.media_fingerprint = media_fingerprint;
    state.deadline_ms = lease_deadline(now_ms);
    if (critical_object_update(&lease_objects[resource],
            STORAGE_MAINTENANCE_VERSION, &state, sizeof(state),
            state_valid) != 0) return STORAGE_EINTEGRITY;
    *token_out = make_token(resource, state.token_generation);
    return 0;
}

int storage_maintenance_renew(int pid, uint32_t process_generation,
        storage_maintenance_token_t token, uint32_t media_fingerprint,
        uint64_t now_ms) {
    uint32_t slot, generation;
    if (!initialized || pid <= 0 || process_generation == 0U ||
        media_fingerprint == 0U || !decode_token(token, &slot, &generation))
        return STORAGE_EINVAL;
    storage_maintenance_state_t state;
    size_t length = 0U;
    if (critical_object_read(&lease_objects[slot], STORAGE_MAINTENANCE_VERSION,
            &state, sizeof(state), &length, state_valid) < 0 ||
        length != sizeof(state)) return STORAGE_EINTEGRITY;
    if (state.active == 0U || state.token_generation != generation ||
        state.pid != pid || state.process_generation != process_generation ||
        state.media_fingerprint != media_fingerprint)
        return STORAGE_EACCES;
    if (deadline_expired(state.deadline_ms, now_ms)) return STORAGE_ETIMEDOUT;
    state.deadline_ms = lease_deadline(now_ms);
    return critical_object_update(&lease_objects[slot],
        STORAGE_MAINTENANCE_VERSION, &state, sizeof(state), state_valid) == 0
        ? 0 : STORAGE_EINTEGRITY;
}

int storage_maintenance_release(int pid, uint32_t process_generation,
        storage_maintenance_token_t token) {
    uint32_t slot, generation;
    if (!initialized || !decode_token(token, &slot, &generation))
        return STORAGE_EINVAL;
    storage_maintenance_state_t state;
    size_t length = 0U;
    if (critical_object_read(&lease_objects[slot], STORAGE_MAINTENANCE_VERSION,
            &state, sizeof(state), &length, state_valid) < 0 ||
        length != sizeof(state)) return STORAGE_EINTEGRITY;
    if (state.active == 0U) return 0;
    if (state.token_generation != generation || state.pid != pid ||
        state.process_generation != process_generation) return STORAGE_EACCES;
    state.active = 0U;
    state.pid = 0;
    state.process_generation = 0U;
    state.media_fingerprint = 0U;
    state.deadline_ms = 0U;
    return critical_object_update(&lease_objects[slot],
        STORAGE_MAINTENANCE_VERSION, &state, sizeof(state), state_valid) == 0
        ? 0 : STORAGE_EINTEGRITY;
}

bool storage_maintenance_valid(int pid, uint32_t process_generation,
        storage_maintenance_token_t token, uint32_t media_fingerprint,
        uint64_t now_ms) {
    uint32_t slot, generation;
    if (!initialized || !decode_token(token, &slot, &generation)) return false;
    storage_maintenance_state_t state;
    size_t length = 0U;
    if (critical_object_read(&lease_objects[slot], STORAGE_MAINTENANCE_VERSION,
            &state, sizeof(state), &length, state_valid) < 0 ||
        length != sizeof(state)) return false;
    return state.active != 0U && state.token_generation == generation &&
           state.pid == pid && state.process_generation == process_generation &&
           state.media_fingerprint == media_fingerprint &&
           !deadline_expired(state.deadline_ms, now_ms);
}
