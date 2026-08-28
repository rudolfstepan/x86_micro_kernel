/**
 * @file kernel/init/resilient_page.c
 * @brief Fixed-capacity transactional resilient-page research primitive.
 *
 * Domain identifiers are software fault-injection labels only.  This module
 * neither changes PTEs nor claims physical DIMM, rank or channel separation.
 */
#include "include/kernel/resilient_page.h"

#include "include/kernel/critical_object.h"

#include <stdbool.h>

#ifdef REIST_RESILIENT_PAGE_BOOT_PROOF
#include "lib/libc/stdio.h"
#endif

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
#include "arch/x86/include/interrupt.h"
#endif

#define RESILIENT_PAGE_METADATA_VERSION 1U
#define RESILIENT_PAGE_LOCK_RETRY_LIMIT (1U << 20U)
#define RESILIENT_PAGE_REPLICA_COUNT 2U

typedef struct {
    uint32_t handle_generation;
    uint32_t data_generation;
    uint32_t state;
    uint32_t replica_count;
    uint32_t domain[RESILIENT_PAGE_REPLICA_COUNT];
    uint32_t active_bank[RESILIENT_PAGE_REPLICA_COUNT];
    uint32_t replica_crc32[RESILIENT_PAGE_REPLICA_COUNT];
    uint32_t reserved[2];
} resilient_page_metadata_t;

typedef struct {
    uint32_t active;
    uint32_t generation;
    critical_object_t metadata;
} resilient_page_slot_t;

_Static_assert(sizeof(resilient_page_metadata_t) <=
               CRITICAL_OBJECT_MAX_PAYLOAD,
               "resilient page metadata exceeds critical object payload");

static _Alignas(RESILIENT_PAGE_SIZE) uint8_t page_storage
    [RESILIENT_PAGE_DOMAIN_COUNT]
    [RESILIENT_PAGE_CAPACITY]
    [RESILIENT_PAGE_BANK_COUNT]
    [RESILIENT_PAGE_SIZE];
static resilient_page_slot_t slots[RESILIENT_PAGE_CAPACITY];
static uint32_t failed_domains[RESILIENT_PAGE_DOMAIN_COUNT];
static volatile uint32_t pool_lock;
static uint32_t pool_initialized;
static uint32_t next_handle_generation;

#ifdef REIST_HOST_TEST
static resilient_page_test_fault_stage_t test_fault_stage;
static resilient_page_domain_t test_fault_domain;
#endif

static void bytes_zero(void *destination, size_t length) {
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    for (size_t index = 0U; index < length; ++index) to[index] = from[index];
}

static uint32_t bytes_equal(const void *left, const void *right,
                            size_t length) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (size_t index = 0U; index < length; ++index)
        if (a[index] != b[index]) return 0U;
    return 1U;
}

static uint32_t page_crc32(const uint8_t *bytes) {
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < RESILIENT_PAGE_SIZE; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^
                (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static uint8_t *bank_bytes(uint32_t domain, uint32_t slot, uint32_t bank) {
    return page_storage[domain][slot][bank];
}

static uint32_t resilient_page_lock(uint32_t *irq_flags_out) {
    if (irq_flags_out == 0) return 0U;
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    *irq_flags_out = irq_save();
#else
    *irq_flags_out = 0U;
#endif
    for (uint32_t retry = 0U; retry < RESILIENT_PAGE_LOCK_RETRY_LIMIT;
         ++retry) {
        if (__sync_bool_compare_and_swap(&pool_lock, 0U, 1U)) {
            __sync_synchronize();
            return 1U;
        }
        __asm__ __volatile__("pause");
    }
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    irq_restore(*irq_flags_out);
#endif
    return 0U;
}

static void resilient_page_unlock(uint32_t irq_flags) {
    __sync_synchronize();
    __sync_lock_release(&pool_lock);
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    irq_restore(irq_flags);
#else
    (void)irq_flags;
#endif
}

static bool metadata_valid(const void *payload, size_t length) {
    if (payload == 0 || length != sizeof(resilient_page_metadata_t))
        return false;
    const resilient_page_metadata_t *metadata =
        (const resilient_page_metadata_t *)payload;
    if (metadata->handle_generation == 0U ||
        metadata->data_generation == 0U ||
        metadata->state > RESILIENT_PAGE_FAILED ||
        metadata->replica_count > RESILIENT_PAGE_REPLICA_COUNT ||
        metadata->reserved[0] != 0U || metadata->reserved[1] != 0U)
        return false;
    if (metadata->state == RESILIENT_PAGE_HEALTHY &&
        metadata->replica_count != RESILIENT_PAGE_REPLICA_COUNT)
        return false;
    if ((metadata->state == RESILIENT_PAGE_DEGRADED ||
         metadata->state == RESILIENT_PAGE_REBUILDING) &&
        metadata->replica_count != 1U)
        return false;
    for (uint32_t index = 0U; index < metadata->replica_count; ++index) {
        if (metadata->domain[index] >= RESILIENT_PAGE_DOMAIN_COUNT ||
            metadata->active_bank[index] >= RESILIENT_PAGE_BANK_COUNT)
            return false;
    }
    if (metadata->replica_count == RESILIENT_PAGE_REPLICA_COUNT &&
        metadata->domain[0] == metadata->domain[1]) return false;
    return true;
}

static resilient_page_result_t metadata_read_locked(
        uint32_t slot_index, resilient_page_metadata_t *metadata) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &slots[slot_index].metadata, RESILIENT_PAGE_METADATA_VERSION,
        metadata, sizeof(*metadata), &length, metadata_valid);
    if (result < 0 || length != sizeof(*metadata))
        return RESILIENT_PAGE_ERROR_CORRUPT;
    return RESILIENT_PAGE_OK;
}

static resilient_page_result_t metadata_update_locked(
        uint32_t slot_index, const resilient_page_metadata_t *metadata) {
    if (!metadata_valid(metadata, sizeof(*metadata)) ||
        critical_object_update(
            &slots[slot_index].metadata, RESILIENT_PAGE_METADATA_VERSION,
            metadata, sizeof(*metadata), metadata_valid) != 0)
        return RESILIENT_PAGE_ERROR_CORRUPT;
    return RESILIENT_PAGE_OK;
}

static resilient_page_result_t resolve_locked(
        resilient_page_handle_t handle, uint32_t *slot_index_out,
        resilient_page_metadata_t *metadata) {
    if (slot_index_out == 0 || metadata == 0 ||
        handle.slot >= RESILIENT_PAGE_CAPACITY || handle.generation == 0U)
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    resilient_page_slot_t *slot = &slots[handle.slot];
    if (slot->active == 0U || slot->generation != handle.generation)
        return RESILIENT_PAGE_ERROR_STALE;
    resilient_page_result_t result = metadata_read_locked(handle.slot, metadata);
    if (result != RESILIENT_PAGE_OK) return result;
    if (metadata->handle_generation != handle.generation)
        return RESILIENT_PAGE_ERROR_STALE;
    *slot_index_out = handle.slot;
    return RESILIENT_PAGE_OK;
}

static resilient_page_result_t mark_failed_locked(
        uint32_t slot_index, resilient_page_metadata_t *metadata,
        resilient_page_result_t result) {
    metadata->state = RESILIENT_PAGE_FAILED;
    if (metadata_update_locked(slot_index, metadata) != RESILIENT_PAGE_OK)
        return RESILIENT_PAGE_ERROR_CORRUPT;
    return result;
}

static uint32_t replica_valid(uint32_t slot_index,
                              const resilient_page_metadata_t *metadata,
                              uint32_t replica_index) {
    uint32_t domain = metadata->domain[replica_index];
    uint32_t bank = metadata->active_bank[replica_index];
    return failed_domains[domain] == 0U &&
        page_crc32(bank_bytes(domain, slot_index, bank)) ==
            metadata->replica_crc32[replica_index];
}

/* Validate all complete replicas, reject equal-generation conflicts, and
 * publish a one-replica degraded metadata set when exactly one copy remains. */
static resilient_page_result_t assess_locked(
        uint32_t slot_index, resilient_page_metadata_t *metadata,
        uint32_t *valid_index_out) {
    if (metadata->state == RESILIENT_PAGE_FAILED) {
        if (valid_index_out != 0) *valid_index_out = 0U;
        return RESILIENT_PAGE_ERROR_FAILED;
    }
    uint32_t valid[RESILIENT_PAGE_REPLICA_COUNT] = {0U, 0U};
    uint32_t valid_count = 0U;
    for (uint32_t index = 0U; index < metadata->replica_count; ++index) {
        if (replica_valid(slot_index, metadata, index))
            valid[valid_count++] = index;
    }
    if (valid_count == 0U)
        return mark_failed_locked(slot_index, metadata,
                                  RESILIENT_PAGE_ERROR_FAILED);
    if (valid_count == RESILIENT_PAGE_REPLICA_COUNT) {
        uint8_t *first = bank_bytes(metadata->domain[valid[0]], slot_index,
                                    metadata->active_bank[valid[0]]);
        uint8_t *second = bank_bytes(metadata->domain[valid[1]], slot_index,
                                     metadata->active_bank[valid[1]]);
        if (!bytes_equal(first, second, RESILIENT_PAGE_SIZE))
            return mark_failed_locked(slot_index, metadata,
                                      RESILIENT_PAGE_ERROR_CORRUPT);
        if (metadata->state != RESILIENT_PAGE_HEALTHY) {
            metadata->state = RESILIENT_PAGE_HEALTHY;
            resilient_page_result_t updated =
                metadata_update_locked(slot_index, metadata);
            if (updated != RESILIENT_PAGE_OK) return updated;
        }
        if (valid_index_out != 0) *valid_index_out = valid[0];
        return RESILIENT_PAGE_OK;
    }

    uint32_t survivor = valid[0];
    if (survivor != 0U) {
        metadata->domain[0] = metadata->domain[survivor];
        metadata->active_bank[0] = metadata->active_bank[survivor];
        metadata->replica_crc32[0] = metadata->replica_crc32[survivor];
    }
    metadata->domain[1] = 0U;
    metadata->active_bank[1] = 0U;
    metadata->replica_crc32[1] = 0U;
    metadata->replica_count = 1U;
    metadata->state = RESILIENT_PAGE_DEGRADED;
    resilient_page_result_t updated = metadata_update_locked(slot_index, metadata);
    if (updated != RESILIENT_PAGE_OK) return updated;
    if (valid_index_out != 0) *valid_index_out = 0U;
    return RESILIENT_PAGE_RESULT_DEGRADED;
}

static resilient_page_result_t fail_domain_locked(
        resilient_page_domain_t domain) {
    if ((uint32_t)domain >= RESILIENT_PAGE_DOMAIN_COUNT)
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    if (failed_domains[domain] != 0U) return RESILIENT_PAGE_OK;
    failed_domains[domain] = 1U;
    resilient_page_result_t aggregate = RESILIENT_PAGE_OK;
    for (uint32_t slot_index = 0U; slot_index < RESILIENT_PAGE_CAPACITY;
         ++slot_index) {
        if (slots[slot_index].active == 0U) continue;
        resilient_page_metadata_t metadata;
        if (metadata_read_locked(slot_index, &metadata) != RESILIENT_PAGE_OK) {
            aggregate = RESILIENT_PAGE_ERROR_CORRUPT;
            continue;
        }
        resilient_page_result_t assessed =
            assess_locked(slot_index, &metadata, 0);
        if (assessed == RESILIENT_PAGE_RESULT_DEGRADED || assessed < 0)
            aggregate = RESILIENT_PAGE_RESULT_DEGRADED;
    }
    return aggregate;
}

#ifdef REIST_HOST_TEST
static uint32_t trigger_test_fault_locked(
        resilient_page_test_fault_stage_t stage) {
    if (test_fault_stage != stage) return 0U;
    resilient_page_domain_t domain = test_fault_domain;
    test_fault_stage = RESILIENT_PAGE_TEST_FAULT_NONE;
    (void)fail_domain_locked(domain);
    return 1U;
}
#else
#define trigger_test_fault_locked(stage) (0U)
#endif

void resilient_page_initialize(void) {
    pool_lock = 0U;
    bytes_zero(slots, sizeof(slots));
    bytes_zero(page_storage, sizeof(page_storage));
    bytes_zero(failed_domains, sizeof(failed_domains));
    next_handle_generation = 1U;
    pool_initialized = 1U;
#ifdef REIST_HOST_TEST
    test_fault_stage = RESILIENT_PAGE_TEST_FAULT_NONE;
    test_fault_domain = RESILIENT_PAGE_DOMAIN_A;
#endif
    __sync_synchronize();
}

resilient_page_result_t resilient_page_create(
        const void *initial_bytes, size_t length,
        resilient_page_handle_t *handle_out) {
    if (pool_initialized == 0U || initial_bytes == 0 || handle_out == 0 ||
        length != RESILIENT_PAGE_SIZE)
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    uint32_t slot_index = 0U;
    while (slot_index < RESILIENT_PAGE_CAPACITY &&
           slots[slot_index].active != 0U) ++slot_index;
    if (slot_index == RESILIENT_PAGE_CAPACITY) {
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_CAPACITY;
    }
    if (next_handle_generation == 0U ||
        next_handle_generation == UINT32_MAX) {
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_FAILED;
    }

    resilient_page_metadata_t metadata;
    bytes_zero(&metadata, sizeof(metadata));
    metadata.handle_generation = next_handle_generation++;
    metadata.data_generation = 1U;
    for (uint32_t domain = 0U;
         domain < RESILIENT_PAGE_DOMAIN_COUNT &&
             metadata.replica_count < RESILIENT_PAGE_REPLICA_COUNT;
         ++domain) {
        if (failed_domains[domain] != 0U) continue;
        uint32_t replica = metadata.replica_count++;
        metadata.domain[replica] = domain;
        metadata.active_bank[replica] = 0U;
        bytes_copy(bank_bytes(domain, slot_index, 0U), initial_bytes,
                   RESILIENT_PAGE_SIZE);
        metadata.replica_crc32[replica] =
            page_crc32(bank_bytes(domain, slot_index, 0U));
    }
    if (metadata.replica_count != RESILIENT_PAGE_REPLICA_COUNT) {
        for (uint32_t domain = 0U; domain < RESILIENT_PAGE_DOMAIN_COUNT;
             ++domain)
            bytes_zero(&page_storage[domain][slot_index],
                       sizeof(page_storage[domain][slot_index]));
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_FAILED;
    }
    metadata.state = RESILIENT_PAGE_HEALTHY;
    slots[slot_index].generation = metadata.handle_generation;
    if (critical_object_init(
            &slots[slot_index].metadata, RESILIENT_PAGE_METADATA_VERSION,
            &metadata, sizeof(metadata)) != 0) {
        slots[slot_index].generation = 0U;
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_CORRUPT;
    }
    slots[slot_index].active = 1U;
    handle_out->slot = slot_index;
    handle_out->generation = metadata.handle_generation;
    resilient_page_unlock(irq_flags);
    return RESILIENT_PAGE_OK;
}

resilient_page_result_t resilient_page_destroy(resilient_page_handle_t handle) {
    if (pool_initialized == 0U) return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    if (result == RESILIENT_PAGE_OK) {
        slots[slot_index].active = 0U;
        __sync_synchronize();
        for (uint32_t domain = 0U; domain < RESILIENT_PAGE_DOMAIN_COUNT;
             ++domain)
            bytes_zero(&page_storage[domain][slot_index],
                       sizeof(page_storage[domain][slot_index]));
        bytes_zero(&slots[slot_index].metadata,
                   sizeof(slots[slot_index].metadata));
        slots[slot_index].generation = 0U;
    }
    resilient_page_unlock(irq_flags);
    return result;
}

resilient_page_result_t resilient_page_read(
        resilient_page_handle_t handle, void *bytes_out, size_t capacity,
        resilient_page_state_t *state_out) {
    if (pool_initialized == 0U || bytes_out == 0 || state_out == 0 ||
        capacity < RESILIENT_PAGE_SIZE)
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    uint32_t source_index = 0U;
    if (result == RESILIENT_PAGE_OK)
        result = assess_locked(slot_index, &metadata, &source_index);
    if (result >= RESILIENT_PAGE_OK) {
        bytes_copy(bytes_out,
                   bank_bytes(metadata.domain[source_index], slot_index,
                              metadata.active_bank[source_index]),
                   RESILIENT_PAGE_SIZE);
        *state_out = (resilient_page_state_t)metadata.state;
    } else if (result == RESILIENT_PAGE_ERROR_FAILED) {
        *state_out = RESILIENT_PAGE_FAILED;
    }
    resilient_page_unlock(irq_flags);
    return result;
}

resilient_page_result_t resilient_page_write(
        resilient_page_handle_t handle, size_t offset,
        const void *bytes, size_t length) {
    if (pool_initialized == 0U || bytes == 0 || length == 0U ||
        offset > RESILIENT_PAGE_SIZE || length > RESILIENT_PAGE_SIZE - offset)
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    uint32_t source_index = 0U;
    if (result == RESILIENT_PAGE_OK)
        result = assess_locked(slot_index, &metadata, &source_index);
    if (result < 0) {
        resilient_page_unlock(irq_flags);
        return result;
    }
    if (metadata.data_generation == UINT32_MAX) {
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_FAILED;
    }
    if (trigger_test_fault_locked(
            RESILIENT_PAGE_TEST_FAULT_WRITE_BEFORE_PREPARE)) {
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_INJECTED;
    }

    uint8_t *source = bank_bytes(metadata.domain[source_index], slot_index,
                                 metadata.active_bank[source_index]);
    resilient_page_metadata_t candidate = metadata;
    candidate.data_generation++;
    for (uint32_t replica = 0U; replica < metadata.replica_count; ++replica) {
        uint32_t inactive_bank = metadata.active_bank[replica] ^ 1U;
        uint8_t *target = bank_bytes(metadata.domain[replica], slot_index,
                                     inactive_bank);
        bytes_copy(target, source, RESILIENT_PAGE_SIZE);
        bytes_copy(target + offset, bytes, length);
        candidate.active_bank[replica] = inactive_bank;
        candidate.replica_crc32[replica] = page_crc32(target);
        if (candidate.replica_crc32[replica] != page_crc32(target)) {
            resilient_page_unlock(irq_flags);
            return RESILIENT_PAGE_ERROR_CORRUPT;
        }
        if (replica == 0U && trigger_test_fault_locked(
                RESILIENT_PAGE_TEST_FAULT_WRITE_AFTER_FIRST_PREPARE)) {
            resilient_page_unlock(irq_flags);
            return RESILIENT_PAGE_ERROR_INJECTED;
        }
    }
    if (trigger_test_fault_locked(
            RESILIENT_PAGE_TEST_FAULT_WRITE_BEFORE_COMMIT)) {
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_INJECTED;
    }
    result = metadata_update_locked(slot_index, &candidate);
    if (result != RESILIENT_PAGE_OK) {
        resilient_page_unlock(irq_flags);
        return result;
    }
    metadata = candidate;
    if (trigger_test_fault_locked(
            RESILIENT_PAGE_TEST_FAULT_WRITE_AFTER_COMMIT)) {
        result = metadata_read_locked(slot_index, &metadata);
        if (result == RESILIENT_PAGE_OK &&
            metadata.state == RESILIENT_PAGE_DEGRADED)
            result = RESILIENT_PAGE_RESULT_DEGRADED;
        resilient_page_unlock(irq_flags);
        return result;
    }
    result = metadata.state == RESILIENT_PAGE_DEGRADED
        ? RESILIENT_PAGE_RESULT_DEGRADED : RESILIENT_PAGE_OK;
    resilient_page_unlock(irq_flags);
    return result;
}

resilient_page_result_t resilient_page_scrub(
        resilient_page_handle_t handle, resilient_page_state_t *state_out) {
    if (pool_initialized == 0U || state_out == 0)
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    (void)trigger_test_fault_locked(RESILIENT_PAGE_TEST_FAULT_SCRUB);
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    if (result == RESILIENT_PAGE_OK)
        result = assess_locked(slot_index, &metadata, 0);
    *state_out = result < 0 ? RESILIENT_PAGE_FAILED
                            : (resilient_page_state_t)metadata.state;
    resilient_page_unlock(irq_flags);
    return result;
}

resilient_page_result_t resilient_page_fail_domain(
        resilient_page_domain_t domain) {
    if (pool_initialized == 0U) return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    resilient_page_result_t result = fail_domain_locked(domain);
    resilient_page_unlock(irq_flags);
    return result;
}

resilient_page_result_t resilient_page_rebuild(
        resilient_page_handle_t handle) {
    if (pool_initialized == 0U) return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    uint32_t source_index = 0U;
    if (result == RESILIENT_PAGE_OK)
        result = assess_locked(slot_index, &metadata, &source_index);
    if (result < 0) {
        resilient_page_unlock(irq_flags);
        return result;
    }
    if (metadata.state != RESILIENT_PAGE_DEGRADED ||
        metadata.replica_count != 1U) {
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    }
    uint32_t replacement = RESILIENT_PAGE_DOMAIN_COUNT;
    for (uint32_t domain = 0U; domain < RESILIENT_PAGE_DOMAIN_COUNT; ++domain) {
        if (failed_domains[domain] == 0U && domain != metadata.domain[0]) {
            replacement = domain;
            break;
        }
    }
    if (replacement == RESILIENT_PAGE_DOMAIN_COUNT) {
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_FAILED;
    }

    metadata.state = RESILIENT_PAGE_REBUILDING;
    result = metadata_update_locked(slot_index, &metadata);
    if (result != RESILIENT_PAGE_OK) {
        resilient_page_unlock(irq_flags);
        return result;
    }
    uint8_t *source = bank_bytes(metadata.domain[source_index], slot_index,
                                 metadata.active_bank[source_index]);
    uint8_t *target = bank_bytes(replacement, slot_index, 0U);
    bytes_copy(target, source, RESILIENT_PAGE_SIZE);
    uint32_t replacement_crc = page_crc32(target);
    if (trigger_test_fault_locked(
            RESILIENT_PAGE_TEST_FAULT_REBUILD_AFTER_COPY)) {
        resilient_page_metadata_t failed;
        if (metadata_read_locked(slot_index, &failed) != RESILIENT_PAGE_OK ||
            failed.state == RESILIENT_PAGE_FAILED) {
            resilient_page_unlock(irq_flags);
            return RESILIENT_PAGE_ERROR_FAILED;
        }
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_INJECTED;
    }
    if (replacement_crc != page_crc32(target)) {
        metadata.state = RESILIENT_PAGE_DEGRADED;
        (void)metadata_update_locked(slot_index, &metadata);
        resilient_page_unlock(irq_flags);
        return RESILIENT_PAGE_ERROR_CORRUPT;
    }
    metadata.replica_count = RESILIENT_PAGE_REPLICA_COUNT;
    metadata.domain[1] = replacement;
    metadata.active_bank[1] = 0U;
    metadata.replica_crc32[1] = replacement_crc;
    metadata.state = RESILIENT_PAGE_HEALTHY;
    result = metadata_update_locked(slot_index, &metadata);
    resilient_page_unlock(irq_flags);
    return result == RESILIENT_PAGE_OK
        ? RESILIENT_PAGE_RESULT_REBUILT : result;
}

resilient_page_result_t resilient_page_get_state(
        resilient_page_handle_t handle, resilient_page_state_t *state_out) {
    if (pool_initialized == 0U || state_out == 0)
        return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    if (result == RESILIENT_PAGE_OK) {
        *state_out = (resilient_page_state_t)metadata.state;
        if (metadata.state == RESILIENT_PAGE_FAILED)
            result = RESILIENT_PAGE_ERROR_FAILED;
        else if (metadata.state == RESILIENT_PAGE_DEGRADED ||
                 metadata.state == RESILIENT_PAGE_REBUILDING)
            result = RESILIENT_PAGE_RESULT_DEGRADED;
    }
    resilient_page_unlock(irq_flags);
    return result;
}

#ifdef REIST_RESILIENT_PAGE_BOOT_PROOF
static uint8_t boot_initial[RESILIENT_PAGE_SIZE];
static uint8_t boot_committed[RESILIENT_PAGE_SIZE];
static uint8_t boot_unrelated[RESILIENT_PAGE_SIZE];
static uint8_t boot_observed[RESILIENT_PAGE_SIZE];
static uint32_t boot_proof_executed;

static void boot_fill(uint8_t *bytes, uint8_t seed) {
    for (size_t index = 0U; index < RESILIENT_PAGE_SIZE; ++index)
        bytes[index] = (uint8_t)(seed + (uint8_t)(index * 13U));
}

static bool boot_metadata_matches(resilient_page_handle_t handle,
                                  uint32_t data_generation,
                                  resilient_page_state_t state,
                                  uint32_t replica_count,
                                  uint32_t require_domain_c) {
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return false;
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    bool matches = result == RESILIENT_PAGE_OK &&
        metadata.data_generation == data_generation &&
        metadata.state == (uint32_t)state &&
        metadata.replica_count == replica_count;
    if (matches && require_domain_c != 0U) {
        matches = false;
        for (uint32_t replica = 0U; replica < metadata.replica_count;
             ++replica) {
            if (metadata.domain[replica] == RESILIENT_PAGE_DOMAIN_C) {
                matches = true;
                break;
            }
        }
    }
    resilient_page_unlock(irq_flags);
    return matches;
}

static bool boot_read_matches(resilient_page_handle_t handle,
                              const uint8_t *expected,
                              resilient_page_state_t expected_state) {
    resilient_page_state_t state = RESILIENT_PAGE_FAILED;
    resilient_page_result_t result = resilient_page_read(
        handle, boot_observed, sizeof(boot_observed), &state);
    return result >= RESILIENT_PAGE_OK && state == expected_state &&
        bytes_equal(boot_observed, expected, RESILIENT_PAGE_SIZE);
}

static bool boot_proof_fail(uint32_t stage, resilient_page_result_t result) {
    printf("REIST_RESILIENT_PAGE BOOT_PROOF_FAIL stage=%u result=%d\n",
           stage, result);
    return false;
}

bool resilient_page_boot_proof(void) {
    if (boot_proof_executed != 0U)
        return boot_proof_fail(1U, RESILIENT_PAGE_ERROR_FAILED);
    boot_proof_executed = 1U;

    boot_fill(boot_initial, 7U);
    boot_fill(boot_committed, 41U);
    boot_fill(boot_unrelated, 113U);
    resilient_page_initialize();

    resilient_page_handle_t primary;
    resilient_page_handle_t unrelated;
    resilient_page_result_t result = resilient_page_create(
        boot_initial, sizeof(boot_initial), &primary);
    if (result != RESILIENT_PAGE_OK) return boot_proof_fail(2U, result);
    result = resilient_page_create(
        boot_unrelated, sizeof(boot_unrelated), &unrelated);
    if (result != RESILIENT_PAGE_OK) return boot_proof_fail(3U, result);

    result = resilient_page_write(primary, 0U, boot_committed,
                                  sizeof(boot_committed));
    if (result != RESILIENT_PAGE_OK ||
        !boot_read_matches(primary, boot_committed, RESILIENT_PAGE_HEALTHY) ||
        !boot_metadata_matches(primary, 2U, RESILIENT_PAGE_HEALTHY, 2U, 0U))
        return boot_proof_fail(4U, result);
    printf("REIST_RESILIENT_PAGE COMMIT_OK generation=2\n");

    result = resilient_page_fail_domain(RESILIENT_PAGE_DOMAIN_A);
    if (result != RESILIENT_PAGE_RESULT_DEGRADED ||
        !boot_read_matches(primary, boot_committed,
                           RESILIENT_PAGE_DEGRADED) ||
        !boot_metadata_matches(primary, 2U, RESILIENT_PAGE_DEGRADED, 1U, 0U))
        return boot_proof_fail(5U, result);
    printf("REIST_RESILIENT_PAGE DEGRADED_DATA_OK generation=2\n");

    if (!boot_read_matches(unrelated, boot_unrelated,
                           RESILIENT_PAGE_DEGRADED) ||
        !boot_metadata_matches(unrelated, 1U, RESILIENT_PAGE_DEGRADED,
                               1U, 0U))
        return boot_proof_fail(6U, RESILIENT_PAGE_ERROR_CORRUPT);
    printf("REIST_RESILIENT_PAGE UNRELATED_OK generation=1\n");

    result = resilient_page_rebuild(primary);
    if (result != RESILIENT_PAGE_RESULT_REBUILT ||
        !boot_read_matches(primary, boot_committed, RESILIENT_PAGE_HEALTHY) ||
        !boot_metadata_matches(primary, 2U, RESILIENT_PAGE_HEALTHY, 2U, 1U))
        return boot_proof_fail(7U, result);
    printf("REIST_RESILIENT_PAGE REBUILD_OK generation=2 domain=C\n");

    result = resilient_page_destroy(primary);
    if (result != RESILIENT_PAGE_OK) return boot_proof_fail(8U, result);
    result = resilient_page_destroy(unrelated);
    if (result != RESILIENT_PAGE_OK) return boot_proof_fail(9U, result);
    resilient_page_initialize();
    printf("REIST_RESILIENT_PAGE BOOT_PROOF_OK objects=2\n");
    return true;
}
#endif

#ifdef REIST_HOST_TEST
void resilient_page_test_arm_domain_failure(
        resilient_page_test_fault_stage_t stage,
        resilient_page_domain_t domain) {
    if (stage > RESILIENT_PAGE_TEST_FAULT_REBUILD_AFTER_COPY ||
        (uint32_t)domain >= RESILIENT_PAGE_DOMAIN_COUNT) return;
    test_fault_domain = domain;
    test_fault_stage = stage;
}

resilient_page_result_t resilient_page_test_corrupt_replica(
        resilient_page_handle_t handle, uint32_t replica_index,
        size_t offset, uint8_t xor_mask, uint32_t reseal_crc) {
    if (pool_initialized == 0U || offset >= RESILIENT_PAGE_SIZE ||
        xor_mask == 0U) return RESILIENT_PAGE_ERROR_ARGUMENT;
    uint32_t irq_flags;
    if (!resilient_page_lock(&irq_flags)) return RESILIENT_PAGE_ERROR_BUSY;
    resilient_page_metadata_t metadata;
    uint32_t slot_index;
    resilient_page_result_t result =
        resolve_locked(handle, &slot_index, &metadata);
    if (result == RESILIENT_PAGE_OK &&
        replica_index >= metadata.replica_count)
        result = RESILIENT_PAGE_ERROR_ARGUMENT;
    if (result == RESILIENT_PAGE_OK) {
        uint8_t *active = bank_bytes(metadata.domain[replica_index], slot_index,
                                     metadata.active_bank[replica_index]);
        active[offset] ^= xor_mask;
        if (reseal_crc != 0U) {
            metadata.replica_crc32[replica_index] = page_crc32(active);
            result = metadata_update_locked(slot_index, &metadata);
        }
    }
    resilient_page_unlock(irq_flags);
    return result;
}
#endif
