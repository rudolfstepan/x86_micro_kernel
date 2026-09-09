/* R3.38: fixed-capacity file pin and namespace-admission mechanism.
 * Neither the key nor a token confers access authority. The trusted mediator
 * validates live service/client identities and normalizes the media binding.
 * All compound state transitions use one SMP try-lock, never a spin loop. */
#include "include/kernel/file_object_guard.h"
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
#include <string.h>
#else
#include "arch/x86/include/interrupt.h"
#include "lib/libc/string.h"
#endif

typedef struct {
    reist_file_object_key_t key;
    file_object_owner_t service, client;
    uint32_t generation, media_generation, active;
} guard_pin_t;

typedef struct {
    uint64_t epoch, deadline_ms;
    file_object_owner_t owner;
    uint32_t generation, resource, active, fenced;
    uint32_t mode, attempted, pending;
} guard_control_t;

typedef struct { uint32_t generation; } guard_media_t;

#define GUARD_INITIALIZED 0x4f424a47U

_Static_assert(sizeof(reist_file_object_key_t) == 32, "file key ABI");
_Static_assert(sizeof(reist_file_object_guard_request_t) == 112, "file guard ABI");
_Static_assert(sizeof(guard_pin_t) <= CRITICAL_OBJECT_MAX_PAYLOAD, "pin bound");
_Static_assert(sizeof(guard_control_t) <= CRITICAL_OBJECT_MAX_PAYLOAD, "control bound");
_Static_assert(sizeof(guard_media_t) <= CRITICAL_OBJECT_MAX_PAYLOAD, "media bound");

static bool guard_owner_valid(file_object_owner_t owner) {
    return owner.pid > 0 && owner.generation != 0;
}

static bool guard_same_owner(file_object_owner_t a, file_object_owner_t b) {
    return a.pid == b.pid && a.generation == b.generation;
}

bool file_object_guard_key_valid(const reist_file_object_key_t *key) {
    if (!key || key->resource >= FILE_OBJECT_GUARD_RESOURCES || key->reserved)
        return false;
    uint8_t aliases = 0;
    for (unsigned i = 0; i < sizeof(key->alias); ++i) aliases |= key->alias[i];
    switch (key->kind) {
    case REIST_FILE_OBJECT_FAT12:
        return key->object_a != 0 && key->object_b <= 480 &&
            (key->object_b & 31) == 0 && aliases == 0;
    case REIST_FILE_OBJECT_FAT32:
        for (unsigned i = 0; i < 11; ++i)
            if (!key->alias[i]) return false;
        return key->object_a >= 2 && key->object_a < 0x0ffffff0U &&
            key->object_b == 0 && key->alias[0] != 0 &&
            key->alias[0] != 0xe5 && key->alias[11] == 0;
    case REIST_FILE_OBJECT_EXT2:
        return key->object_a != 0 && key->object_b == 0 && aliases == 0;
    default: return false;
    }
}

bool file_object_guard_request_valid(const reist_file_object_guard_request_t *request) {
    if (!request || request->version != REIST_FILE_OBJECT_VERSION ||
        request->struct_size != sizeof(*request) || request->reserved) return false;
    reist_file_object_key_t zero = {0};
    bool first_empty = !memcmp(&request->keys[0], &zero, sizeof(zero));
    bool second_empty = !memcmp(&request->keys[1], &zero, sizeof(zero));
    bool client_empty = !request->client_pid && !request->client_generation;
    bool client_valid = request->client_pid > 0 && request->client_generation;
    switch (request->operation) {
    case REIST_FILE_OBJECT_SNAPSHOT:
        return first_empty && second_empty && client_empty && !request->flags &&
            !request->epoch && !request->deadline_ms && !request->token;
    case REIST_FILE_OBJECT_PIN:
        return file_object_guard_key_valid(&request->keys[0]) && second_empty &&
            client_valid && !request->flags && request->epoch &&
            !request->deadline_ms && !request->token;
    case REIST_FILE_OBJECT_RELEASE:
    case REIST_FILE_OBJECT_VERIFY:
        return first_empty && second_empty && client_valid && !request->flags &&
            !request->epoch && !request->deadline_ms && (request->token >> 8) &&
            (request->token & 255U) >= 1 &&
            (request->token & 255U) <= FILE_OBJECT_GUARD_CAPACITY;
    case REIST_FILE_OBJECT_MUTATION_BEGIN:
        return file_object_guard_key_valid(&request->keys[0]) &&
            (second_empty || (file_object_guard_key_valid(&request->keys[1]) &&
                request->keys[1].kind == request->keys[0].kind &&
                request->keys[1].resource == request->keys[0].resource)) &&
            client_empty && !(request->flags & ~(REIST_FILE_OBJECT_EXCLUSIVE |
                REIST_FILE_OBJECT_EXTERNAL_JOURNAL)) &&
            (!(request->flags & REIST_FILE_OBJECT_EXTERNAL_JOURNAL) ||
                ((request->flags & REIST_FILE_OBJECT_EXCLUSIVE) && second_empty &&
                 request->keys[0].kind == REIST_FILE_OBJECT_FAT32)) &&
            request->epoch && request->deadline_ms && !request->token;
    case REIST_FILE_OBJECT_MUTATION_END:
        return first_empty && second_empty && client_empty &&
            request->flags >= REIST_FILE_OBJECT_NO_EFFECT &&
            request->flags <= REIST_FILE_OBJECT_UNKNOWN && !request->epoch &&
            !request->deadline_ms && request->token;
    default: return false;
    }
}

static bool guard_pin_valid(const void *data, size_t size) {
    if (!data || size != sizeof(guard_pin_t)) return false;
    const guard_pin_t *pin = data;
    if (pin->active > 1 || pin->generation > FILE_OBJECT_GUARD_GENERATION_MAX)
        return false;
    if (pin->active)
        return pin->generation && pin->media_generation &&
            guard_owner_valid(pin->service) && guard_owner_valid(pin->client) &&
            file_object_guard_key_valid(&pin->key);
    reist_file_object_key_t empty = {0};
    if (memcmp(&pin->key, &empty, sizeof(empty)) || pin->media_generation)
        return false;
    if (!pin->generation)
        return !pin->service.pid && !pin->service.generation &&
            !pin->client.pid && !pin->client.generation;
    return guard_owner_valid(pin->service) && guard_owner_valid(pin->client);
}

static bool guard_control_valid(const void *data, size_t size) {
    if (!data || size != sizeof(guard_control_t)) return false;
    const guard_control_t *control = data;
    if (!control->epoch || control->active > 1 || control->attempted > 1 ||
        control->pending > control->attempted ||
        (control->mode != 0 && control->mode != REIST_FILE_OBJECT_EXTERNAL_JOURNAL) ||
        (!control->mode && (control->attempted || control->pending))) return false;
    if (!control->active)
        return !control->deadline_ms && !control->owner.pid &&
            !control->owner.generation && !control->resource && !control->mode &&
            !control->attempted && !control->pending;
    return guard_owner_valid(control->owner) && control->generation &&
        control->deadline_ms && control->resource < FILE_OBJECT_GUARD_RESOURCES;
}

static bool guard_media_valid(const void *data, size_t size) {
    return data && size == sizeof(guard_media_t) &&
        ((const guard_media_t *)data)->generation != 0;
}

static int guard_poison(file_object_guard_t *guard) {
    __atomic_store_n(&guard->poisoned, UINT32_MAX, __ATOMIC_RELEASE);
    return -REIST_EIO;
}

static int guard_lock(file_object_guard_t *guard, uint32_t *irq_flags) {
    if (!guard || !irq_flags) return -REIST_EINVAL;
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
    *irq_flags = 0;
#else
    *irq_flags = irq_save();
#endif
    if (__sync_bool_compare_and_swap(&guard->lock, 0, 1)) return 0;
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    irq_restore(*irq_flags);
#endif
    return -REIST_EBUSY;
}

static int guard_unlock(file_object_guard_t *guard, uint32_t flags, int result) {
    __sync_lock_release(&guard->lock);
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    irq_restore(flags);
#else
    (void)flags;
#endif
    return result;
}

/* The outer guard lock exclusively owns these private critical_objects. A
 * nonzero inner lock here is corruption, not legitimate contention. Do not
 * enter critical_object's bounded retry loop on an already stuck lock. */
static int guard_read(file_object_guard_t *guard, critical_object_t *object,
    void *value, size_t size, critical_object_validator_t valid) {
    size_t length = 0;
    if (object->publication_lock || critical_object_read(object,
            REIST_FILE_OBJECT_VERSION, value, size, &length, valid) < 0 || length != size)
        return guard_poison(guard);
    return 0;
}

static int guard_write(file_object_guard_t *guard, critical_object_t *object,
    const void *value, size_t size, critical_object_validator_t valid) {
    if (object->publication_lock || critical_object_update(object,
            REIST_FILE_OBJECT_VERSION, value, size, valid)) return guard_poison(guard);
    return 0;
}

static int guard_control_read(file_object_guard_t *guard, guard_control_t *control) {
    if (guard->initialized != GUARD_INITIALIZED ||
        __atomic_load_n(&guard->poisoned, __ATOMIC_ACQUIRE))
        return guard_poison(guard);
    return guard_read(guard, &guard->control, control, sizeof(*control), guard_control_valid);
}

static int guard_control_write(file_object_guard_t *guard, guard_control_t *control) {
    return guard_write(guard, &guard->control, control, sizeof(*control), guard_control_valid);
}

static int guard_epoch_advance(file_object_guard_t *guard, guard_control_t *control) {
    if (control->epoch == UINT64_MAX) return guard_poison(guard);
    ++control->epoch;
    return 0;
}

static int guard_finish(file_object_guard_t *guard, guard_control_t *control,
                         bool uncertain) {
    /* Publish the sticky fence in the same protected record as retirement.
     * There is no state in which the owner is gone but admission is open. */
    if (uncertain) control->fenced |= 1U << control->resource;
    control->active = 0;
    control->deadline_ms = 0;
    control->owner = (file_object_owner_t){0};
    control->resource = 0;
    control->mode = control->attempted = control->pending = 0;
    int result = guard_epoch_advance(guard, control);
    return result ? result : guard_control_write(guard, control);
}

static int guard_expire(file_object_guard_t *guard, guard_control_t *control,
                         uint64_t now_ms) {
    if (control->active && now_ms >= control->deadline_ms)
        return guard_finish(guard, control, true);
    return 0;
}

int file_object_guard_init(file_object_guard_t *guard) {
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    if (__atomic_load_n(&guard->poisoned, __ATOMIC_ACQUIRE))
        return guard_unlock(guard, flags, -REIST_EIO);
    if (guard->initialized) {
        guard_control_t control;
        result = guard_control_read(guard, &control);
        return guard_unlock(guard, flags, result);
    }
    /* A damaged initialized marker must never reset token/media generations.
     * Only pristine BSS objects may take the first-initialization path. */
    const uint8_t *bytes = (const uint8_t *)guard + offsetof(file_object_guard_t, control);
    size_t payload_size = sizeof(*guard) - offsetof(file_object_guard_t, control);
    for (size_t i = 0; i < payload_size; ++i)
        if (bytes[i]) return guard_unlock(guard, flags, guard_poison(guard));
    guard_control_t control = {0};
    control.epoch = 1;
    if (critical_object_init(&guard->control, REIST_FILE_OBJECT_VERSION,
                            &control, sizeof(control))) result = guard_poison(guard);
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        guard_pin_t pin = {0};
        if (critical_object_init(&guard->pins[i], REIST_FILE_OBJECT_VERSION,
                                 &pin, sizeof(pin))) result = guard_poison(guard);
    }
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_RESOURCES; ++i) {
        guard_media_t media = {1};
        if (critical_object_init(&guard->media[i], REIST_FILE_OBJECT_VERSION,
                                 &media, sizeof(media))) result = guard_poison(guard);
    }
    if (!result) guard->initialized = GUARD_INITIALIZED;
    return guard_unlock(guard, flags, result);
}

int file_object_guard_snapshot(file_object_guard_t *guard, uint64_t *epoch,
                                uint64_t now_ms) {
    if (!epoch) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && control.active) result = -REIST_EBUSY;
    if (!result) *epoch = control.epoch;
    return guard_unlock(guard, flags, result);
}

static int guard_admit(file_object_guard_t *guard, guard_control_t *control,
                        uint32_t resource, uint64_t epoch, uint64_t now_ms) {
    int result = guard_control_read(guard, control);
    if (!result) result = guard_expire(guard, control, now_ms);
    if (result) return result;
    if (control->fenced & (1U << resource)) return -REIST_EIO;
    if (control->active) return -REIST_EBUSY;
    return control->epoch == epoch ? 0 : -REIST_EAGAIN;
}

int file_object_guard_pin(file_object_guard_t *guard,
    const reist_file_object_key_t *key, file_object_owner_t service,
    file_object_owner_t client, uint64_t epoch, uint64_t now_ms, uint32_t *token) {
    if (!file_object_guard_key_valid(key) || !guard_owner_valid(service) ||
        !guard_owner_valid(client) || !epoch || !token) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_admit(guard, &control, key->resource, epoch, now_ms);
    unsigned owned = 0, free_slot = FILE_OBJECT_GUARD_CAPACITY;
    guard_pin_t chosen = {0};
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        guard_pin_t pin;
        result = guard_read(guard, &guard->pins[i], &pin, sizeof(pin), guard_pin_valid);
        if (result) break;
        if (pin.active && guard_same_owner(pin.client, client)) ++owned;
        if (!pin.active && pin.generation < FILE_OBJECT_GUARD_GENERATION_MAX &&
            free_slot == FILE_OBJECT_GUARD_CAPACITY) { free_slot = i; chosen = pin; }
    }
    if (!result && owned >= FILE_OBJECT_GUARD_PER_CLIENT) result = -REIST_EMFILE;
    if (!result && free_slot == FILE_OBJECT_GUARD_CAPACITY) result = -REIST_ENOSPC;
    guard_media_t media;
    if (!result) result = guard_read(guard, &guard->media[key->resource],
                                    &media, sizeof(media), guard_media_valid);
    if (!result) {
        chosen.key = *key;
        chosen.service = service;
        chosen.client = client;
        chosen.media_generation = media.generation;
        chosen.active = 1;
        ++chosen.generation;
        result = guard_write(guard, &guard->pins[free_slot], &chosen,
                              sizeof(chosen), guard_pin_valid);
        if (!result) *token = (chosen.generation << 8) | (free_slot + 1);
    }
    return guard_unlock(guard, flags, result);
}

static int guard_token_read(file_object_guard_t *guard, uint32_t token,
    file_object_owner_t service, file_object_owner_t client, guard_pin_t *pin,
    uint32_t *slot) {
    uint32_t selected = token & 0xffU;
    if (!selected || selected > FILE_OBJECT_GUARD_CAPACITY || !(token >> 8))
        return -REIST_EINVAL;
    *slot = selected - 1;
    int result = guard_read(guard, &guard->pins[*slot], pin, sizeof(*pin), guard_pin_valid);
    if (result) return result;
    if (pin->generation != (token >> 8)) return -REIST_ESTALE;
    if (!guard_same_owner(pin->service, service) || !guard_same_owner(pin->client, client))
        return -REIST_EACCES;
    return 0;
}

int file_object_guard_verify(file_object_guard_t *guard, uint32_t token,
    file_object_owner_t service, file_object_owner_t client, uint64_t now_ms) {
    return file_object_guard_lookup(guard, token, service, client, now_ms, NULL);
}

int file_object_guard_lookup(file_object_guard_t *guard, uint32_t token,
    file_object_owner_t service, file_object_owner_t client, uint64_t now_ms,
    reist_file_object_key_t *key) {
    uint32_t flags, slot;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    guard_pin_t pin;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result) result = guard_token_read(guard, token, service, client, &pin, &slot);
    if (!result && !pin.active) result = -REIST_ESTALE;
    if (!result && (control.fenced & (1U << pin.key.resource))) result = -REIST_EIO;
    if (!result) {
        guard_media_t media;
        result = guard_read(guard, &guard->media[pin.key.resource],
                             &media, sizeof(media), guard_media_valid);
        if (!result && media.generation != pin.media_generation) result = -REIST_ESTALE;
    }
    if (!result && key) *key = pin.key;
    return guard_unlock(guard, flags, result);
}

int file_object_guard_legacy_enter(file_object_guard_t *guard, uint64_t now_ms) {
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && control.fenced) result = -REIST_EIO;
    if (!result && control.active) result = -REIST_EBUSY;
    if (!result) result = guard_epoch_advance(guard, &control);
    if (!result) result = guard_control_write(guard, &control);
    return guard_unlock(guard, flags, result);
}

int file_object_guard_can_open(file_object_guard_t *guard, uint32_t resource,
                               uint64_t now_ms) {
    if (resource >= FILE_OBJECT_GUARD_RESOURCES) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && (control.fenced & (1U << resource))) result = -REIST_EIO;
    if (!result && control.active && control.resource == resource) result = -REIST_EBUSY;
    return guard_unlock(guard, flags, result);
}

int file_object_guard_key_busy(file_object_guard_t *guard,
    const reist_file_object_key_t *key, uint64_t now_ms) {
    if (!file_object_guard_key_valid(key)) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && (control.fenced & (1U << key->resource))) result = -REIST_EIO;
    if (!result && control.active) result = -REIST_EBUSY;
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        guard_pin_t pin;
        result = guard_read(guard, &guard->pins[i], &pin, sizeof(pin), guard_pin_valid);
        if (!result && pin.active && !memcmp(&pin.key, key, sizeof(*key)))
            result = -REIST_EBUSY;
    }
    return guard_unlock(guard, flags, result);
}

static int guard_pin_clear(file_object_guard_t *guard, unsigned slot, guard_pin_t *pin) {
    memset(&pin->key, 0, sizeof(pin->key));
    pin->active = 0;
    pin->media_generation = 0;
    /* Preserve the last token's owners for exact, idempotent Close. A new
     * acquisition advances generation before the slot can be published. */
    return guard_write(guard, &guard->pins[slot], pin, sizeof(*pin), guard_pin_valid);
}

int file_object_guard_release(file_object_guard_t *guard, uint32_t token,
    file_object_owner_t service, file_object_owner_t client) {
    uint32_t flags, slot;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    guard_pin_t pin;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_token_read(guard, token, service, client, &pin, &slot);
    if (!result && pin.active) result = guard_pin_clear(guard, slot, &pin);
    return guard_unlock(guard, flags, result);
}

int file_object_guard_begin(file_object_guard_t *guard,
    const reist_file_object_key_t *keys, uint32_t count, bool exclusive,
    file_object_owner_t owner, uint64_t epoch, uint64_t now_ms,
    uint64_t deadline_ms, uint32_t *token) {
    return file_object_guard_begin_mode(guard, keys, count,
        exclusive ? REIST_FILE_OBJECT_EXCLUSIVE : 0, owner, epoch, now_ms,
        deadline_ms, token);
}

int file_object_guard_begin_mode(file_object_guard_t *guard,
    const reist_file_object_key_t *keys, uint32_t count, uint32_t mode,
    file_object_owner_t owner, uint64_t epoch, uint64_t now_ms,
    uint64_t deadline_ms, uint32_t *token) {
    if (!keys || !count || count > 2 || !guard_owner_valid(owner) || !epoch ||
        !token || deadline_ms <= now_ms || deadline_ms - now_ms > FILE_OBJECT_GUARD_MAX_MS)
        return -REIST_EINVAL;
    if ((mode & ~(REIST_FILE_OBJECT_EXCLUSIVE | REIST_FILE_OBJECT_EXTERNAL_JOURNAL)) ||
        ((mode & REIST_FILE_OBJECT_EXTERNAL_JOURNAL) &&
         (!(mode & REIST_FILE_OBJECT_EXCLUSIVE) || count != 1 ||
          keys[0].kind != REIST_FILE_OBJECT_FAT32))) return -REIST_EINVAL;
    for (unsigned i = 0; i < count; ++i)
        if (!file_object_guard_key_valid(&keys[i]) ||
            keys[i].kind != keys[0].kind || keys[i].resource != keys[0].resource)
            return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_admit(guard, &control, keys[0].resource, epoch, now_ms);
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        guard_pin_t pin;
        result = guard_read(guard, &guard->pins[i], &pin, sizeof(pin), guard_pin_valid);
        if (result || !pin.active) continue;
        if ((mode & REIST_FILE_OBJECT_EXCLUSIVE) && pin.key.resource == keys[0].resource)
            result = -REIST_EBUSY;
        for (unsigned j = 0; !result && j < count; ++j)
            if (!memcmp(&pin.key, &keys[j], sizeof(pin.key))) result = -REIST_EBUSY;
    }
    if (!result && control.generation == UINT32_MAX) result = -REIST_EOVERFLOW;
    if (!result) result = guard_epoch_advance(guard, &control);
    if (!result) {
        control.owner = owner;
        control.resource = keys[0].resource;
        control.deadline_ms = deadline_ms;
        control.active = 1;
        control.mode = mode & REIST_FILE_OBJECT_EXTERNAL_JOURNAL;
        ++control.generation;
        result = guard_control_write(guard, &control);
        if (!result) *token = control.generation;
    }
    return guard_unlock(guard, flags, result);
}

int file_object_guard_end(file_object_guard_t *guard, uint32_t token,
    file_object_owner_t owner, uint32_t outcome, uint64_t now_ms) {
    if (!token || !guard_owner_valid(owner) || outcome < REIST_FILE_OBJECT_NO_EFFECT ||
        outcome > REIST_FILE_OBJECT_UNKNOWN) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && (!control.active || control.generation != token)) result = -REIST_ESTALE;
    if (!result && !guard_same_owner(control.owner, owner)) result = -REIST_EACCES;
    if (!result && control.mode &&
        ((outcome == REIST_FILE_OBJECT_NO_EFFECT && control.attempted) ||
         (outcome == REIST_FILE_OBJECT_DURABLE_COMMIT && control.pending)))
        result = -REIST_EINVAL;
    if (!result) result = guard_finish(guard, &control, outcome == REIST_FILE_OBJECT_UNKNOWN);
    return guard_unlock(guard, flags, result);
}

int file_object_guard_mutation_authorized(file_object_guard_t *guard,
    file_object_owner_t owner, uint32_t resource, uint64_t now_ms) {
    if (!guard_owner_valid(owner) || resource >= FILE_OBJECT_GUARD_RESOURCES)
        return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && (control.fenced & (1U << resource))) result = -REIST_EIO;
    if (!result && !control.active) result = -REIST_EACCES;
    if (!result && (control.resource != resource || !guard_same_owner(control.owner, owner)))
        result = -REIST_EBUSY;
    if (!result && control.mode) result = -REIST_EBUSY;
    return guard_unlock(guard, flags, result);
}

bool file_object_guard_journal_request_valid(const reist_storage_journal_request_t *request) {
    if (!request || request->version != REIST_STORAGE_JOURNAL_VERSION ||
        request->struct_size != sizeof(*request) || !request->token ||
        request->resource >= FILE_OBJECT_GUARD_RESOURCES || request->reserved)
        return false;
    if (request->operation == REIST_STORAGE_JOURNAL_FLUSH)
        return !request->sector && !request->count;
    return (request->operation == REIST_STORAGE_JOURNAL_READ ||
            request->operation == REIST_STORAGE_JOURNAL_WRITE_DEFERRED) &&
        request->count && request->count <= REIST_STORAGE_JOURNAL_MAX_SECTORS &&
        request->sector <= UINT32_MAX - (request->count - 1U);
}

int file_object_guard_journal_io_deadline(file_object_guard_t *guard,
    file_object_owner_t owner, uint32_t token, uint32_t resource,
    uint32_t event, uint64_t now_ms, bool *was_pending, uint64_t *deadline_ms) {
    if (was_pending) *was_pending = false;
    if (deadline_ms) *deadline_ms = 0;
    if (!guard_owner_valid(owner) || !token || resource >= FILE_OBJECT_GUARD_RESOURCES ||
        event > FILE_OBJECT_JOURNAL_FLUSHED) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && (control.fenced & (1U << resource))) result = -REIST_EIO;
    if (!result && (!control.active || control.generation != token)) result = -REIST_ESTALE;
    if (!result && (!guard_same_owner(control.owner, owner) ||
        control.resource != resource || control.mode != REIST_FILE_OBJECT_EXTERNAL_JOURNAL))
        result = -REIST_EACCES;
    bool pending = !result && control.pending != 0;
    if (!result && event) {
        if (event == FILE_OBJECT_JOURNAL_WRITE) control.attempted = control.pending = 1;
        else control.pending = 0;
        result = guard_control_write(guard, &control);
    }
    result = guard_unlock(guard, flags, result);
    if (!result) {
        if (was_pending) *was_pending = pending;
        if (deadline_ms) *deadline_ms = control.deadline_ms;
    }
    return result;
}

int file_object_guard_journal_io(file_object_guard_t *guard,
    file_object_owner_t owner, uint32_t token, uint32_t resource,
    uint32_t event, uint64_t now_ms, bool *was_pending) {
    return file_object_guard_journal_io_deadline(guard, owner, token, resource,
        event, now_ms, was_pending, NULL);
}

int file_object_guard_cancel_undelivered(file_object_guard_t *guard,
    uint32_t operation, uint32_t token, file_object_owner_t service,
    file_object_owner_t client) {
    if (operation == REIST_FILE_OBJECT_PIN)
        return file_object_guard_release(guard, token, service, client);
    if (operation != REIST_FILE_OBJECT_MUTATION_BEGIN || !token ||
        !guard_owner_valid(service)) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result && (!control.active || control.generation != token)) result = -REIST_ESTALE;
    if (!result && !guard_same_owner(control.owner, service)) result = -REIST_EACCES;
    /* Do not turn a known-undelivered reservation into an uncertain write just
     * because the copyout fault was descheduled past its deadline. Existing
     * fences from another observer are never removed. */
    if (!result && control.attempted) result = -REIST_EINVAL;
    if (!result) result = guard_finish(guard, &control, false);
    return guard_unlock(guard, flags, result);
}

int file_object_guard_cleanup(file_object_guard_t *guard, file_object_owner_t owner) {
    if (!guard_owner_valid(owner)) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result && control.active && guard_same_owner(control.owner, owner))
        result = guard_finish(guard, &control, true);
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        guard_pin_t pin;
        result = guard_read(guard, &guard->pins[i], &pin, sizeof(pin), guard_pin_valid);
        if (!result && pin.active && (guard_same_owner(pin.service, owner) ||
            guard_same_owner(pin.client, owner))) result = guard_pin_clear(guard, i, &pin);
    }
    return guard_unlock(guard, flags, result);
}

int file_object_guard_owner_at(file_object_guard_t *guard, uint32_t slot,
    file_object_owner_t *service, file_object_owner_t *client) {
    if (slot > FILE_OBJECT_GUARD_CAPACITY || !service || !client) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    file_object_owner_t owner = {0}, consumer = {0};
    if (!result && slot == FILE_OBJECT_GUARD_CAPACITY) {
        if (control.active) owner = control.owner;
    } else if (!result) {
        guard_pin_t pin;
        result = guard_read(guard, &guard->pins[slot], &pin, sizeof(pin), guard_pin_valid);
        if (!result && pin.active) { owner = pin.service; consumer = pin.client; }
    }
    if (!result) { *service = owner; *client = consumer; }
    return guard_unlock(guard, flags, result);
}

int file_object_guard_revoke_media(file_object_guard_t *guard, uint32_t resource) {
    if (resource >= FILE_OBJECT_GUARD_RESOURCES) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    guard_media_t media;
    result = guard_control_read(guard, &control);
    if (!result && control.active && control.resource == resource)
        result = guard_finish(guard, &control, true);
    if (!result) result = guard_epoch_advance(guard, &control);
    if (!result) result = guard_control_write(guard, &control);
    if (!result) result = guard_read(guard, &guard->media[resource],
                                     &media, sizeof(media), guard_media_valid);
    if (!result && media.generation == UINT32_MAX) result = guard_poison(guard);
    if (!result) {
        ++media.generation;
        result = guard_write(guard, &guard->media[resource], &media,
                              sizeof(media), guard_media_valid);
    }
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        guard_pin_t pin;
        result = guard_read(guard, &guard->pins[i], &pin, sizeof(pin), guard_pin_valid);
        if (!result && pin.active && pin.key.resource == resource)
            result = guard_pin_clear(guard, i, &pin);
    }
    return guard_unlock(guard, flags, result);
}

int file_object_guard_poll(file_object_guard_t *guard, uint64_t now_ms) {
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    return guard_unlock(guard, flags, result);
}

int file_object_guard_fenced(file_object_guard_t *guard, uint32_t *mask) {
    if (!mask) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    *mask = result ? UINT32_MAX : control.fenced;
    return guard_unlock(guard, flags, result);
}

int file_object_guard_count(file_object_guard_t *guard, uint32_t resource,
                            uint32_t *count, uint64_t now_ms) {
    if (!count || resource >= FILE_OBJECT_GUARD_RESOURCES) return -REIST_EINVAL;
    uint32_t flags;
    int result = guard_lock(guard, &flags);
    if (result) return result;
    guard_control_t control;
    result = guard_control_read(guard, &control);
    if (!result) result = guard_expire(guard, &control, now_ms);
    if (!result && (control.fenced & (1U << resource))) result = -REIST_EIO;
    if (!result && control.active && control.resource == resource) result = -REIST_EBUSY;
    uint32_t found = 0;
    for (unsigned i = 0; !result && i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        guard_pin_t pin;
        result = guard_read(guard, &guard->pins[i], &pin, sizeof(pin), guard_pin_valid);
        if (!result && pin.active && pin.key.resource == resource) ++found;
    }
    if (!result) *count = found;
    return guard_unlock(guard, flags, result);
}
