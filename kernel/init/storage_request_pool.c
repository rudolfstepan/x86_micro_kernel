/**
 * @file kernel/init/storage_request_pool.c
 * @brief Verwaltet feste Slots für generationsgebundene Storage-Requests.
 *
 * Layer: Ring-0 storage request allocator.
 * Contract: Zustandswechsel akzeptieren nur passende Slotgenerationen.
 * Safety: Erschöpfung und stale Completion verändern keine fremden Requests.
 */
#include "include/kernel/storage_request_pool.h"

#include <stdbool.h>

#include "include/kernel/critical_object.h"
#include "lib/libc/string.h"

#ifdef REIST_HOST_TEST
static uint32_t storage_pool_lock(void) { return 0U; }
static void storage_pool_unlock(uint32_t flags) { (void)flags; }
#else
#include "arch/x86/include/interrupt.h"
static uint32_t storage_pool_lock(void) { return irq_save(); }
static void storage_pool_unlock(uint32_t flags) { irq_restore(flags); }
#endif

#define STORAGE_POOL_METADATA_VERSION 1U
#define STORAGE_HANDLE_SLOT_MASK 0xFFU
#define STORAGE_HANDLE_GENERATION_MAX 0x00FFFFFFU

#define STORAGE_EAGAIN (-11)
#define STORAGE_EACCES (-13)
#define STORAGE_EINVAL (-22)
#define STORAGE_ENOSPC (-28)
#define STORAGE_EINTEGRITY (-84)
#define STORAGE_EMSGSIZE (-90)

typedef enum {
    STORAGE_SLOT_FREE = 0,
    STORAGE_SLOT_QUEUED = 1,
    STORAGE_SLOT_CLAIMED = 2,
    STORAGE_SLOT_COMPLETE = 3,
    STORAGE_SLOT_RETIRED = 4,
} storage_slot_state_t;

typedef struct {
    uint32_t state;
    uint32_t generation;
    uint32_t operation;
    int32_t client_pid;
    uint32_t client_generation;
    int32_t service_pid;
    uint32_t service_generation;
    uint32_t resource;
    uint32_t offset;
    uint32_t length;
    int32_t result;
    uint64_t deadline_ms;
} storage_slot_metadata_t;

typedef struct {
    int32_t pid;
    uint32_t generation;
} storage_service_identity_t;

typedef struct {
    uint8_t bytes[STORAGE_REQUEST_BLOCK_SIZE];
    uint32_t length;
    uint32_t crc32;
} storage_data_copy_t;

typedef struct {
    critical_object_t metadata;
    storage_data_copy_t primary;
    storage_data_copy_t shadow;
} storage_slot_t;

static storage_slot_t slots[STORAGE_REQUEST_POOL_CAPACITY];
static critical_object_t protected_service_identity;
static storage_request_stats_t request_stats;

static void increment_saturating(uint32_t *value) {
    if (*value != UINT32_MAX) ++*value;
}

static void request_added(void) {
    increment_saturating(&request_stats.active_requests);
    if (request_stats.active_requests > request_stats.request_high_water)
        request_stats.request_high_water = request_stats.active_requests;
}

static void request_removed(void) {
    if (request_stats.active_requests != 0U)
        --request_stats.active_requests;
}

_Static_assert(sizeof(storage_slot_metadata_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "storage request metadata exceeds protected payload");
_Static_assert(sizeof(storage_service_identity_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "storage service identity exceeds protected payload");
_Static_assert(sizeof(storage_request_stats_t) == 6U * sizeof(uint32_t),
               "storage request stats ABI drift");

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFU;
}

static bool metadata_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(storage_slot_metadata_t))
        return false;
    const storage_slot_metadata_t *value = payload;
    if (value->state > STORAGE_SLOT_RETIRED ||
        value->generation > STORAGE_HANDLE_GENERATION_MAX ||
        value->length > STORAGE_REQUEST_BLOCK_SIZE) return false;
    if (value->state == STORAGE_SLOT_FREE ||
        value->state == STORAGE_SLOT_RETIRED)
        return value->operation == 0U && value->client_pid == 0 &&
               value->client_generation == 0U && value->service_pid == 0 &&
               value->service_generation == 0U && value->resource == 0U &&
               value->offset == 0U &&
               value->length == 0U && value->result == 0 &&
               value->deadline_ms == 0U;
    if (value->generation == 0U || value->client_pid <= 0 ||
        value->client_generation == 0U ||
        value->deadline_ms == 0U ||
        value->operation < STORAGE_REQUEST_READ ||
        value->operation > STORAGE_REQUEST_FORMAT_FAT32_PREPARE) return false;
    if (value->operation == STORAGE_REQUEST_BLOCK_FLUSH ||
        value->operation == STORAGE_REQUEST_VFS_SYNC ||
        value->operation == STORAGE_REQUEST_FORMAT_FAT12 ||
        value->operation == STORAGE_REQUEST_FORMAT_FAT32 ||
        value->operation == STORAGE_REQUEST_FORMAT_FAT32_SCAN ||
        value->operation == STORAGE_REQUEST_FORMAT_FAT32_PREPARE)
        return value->length == 0U;
    if (value->operation == STORAGE_REQUEST_BLOCK_READ ||
        value->operation == STORAGE_REQUEST_BLOCK_WRITE)
        return value->length == STORAGE_REQUEST_BLOCK_SIZE;
    return value->length != 0U && value->length <= STORAGE_REQUEST_BLOCK_SIZE;
}

static uint64_t deadline_after(uint64_t now_ms, uint32_t timeout_ms) {
    return UINT64_MAX - now_ms < timeout_ms
        ? UINT64_MAX : now_ms + timeout_ms;
}

static bool operation_is_write(uint32_t operation) {
    return operation == STORAGE_REQUEST_BLOCK_WRITE ||
           operation == STORAGE_REQUEST_VFS_WRITE;
}

static bool operation_is_read(uint32_t operation) {
    return operation == STORAGE_REQUEST_BLOCK_READ ||
           operation == STORAGE_REQUEST_VFS_READ;
}

static bool identity_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(storage_service_identity_t))
        return false;
    const storage_service_identity_t *identity = payload;
    return (identity->pid == 0 && identity->generation == 0U) ||
           (identity->pid > 0 && identity->generation != 0U);
}

static int load_metadata(size_t slot, storage_slot_metadata_t *value) {
    size_t length = 0U;
    return critical_object_read(&slots[slot].metadata,
        STORAGE_POOL_METADATA_VERSION, value, sizeof(*value), &length,
        metadata_valid) < 0 || length != sizeof(*value)
        ? STORAGE_EINTEGRITY : 0;
}

static int store_metadata(size_t slot, const storage_slot_metadata_t *value) {
    return critical_object_update(&slots[slot].metadata,
        STORAGE_POOL_METADATA_VERSION, value, sizeof(*value), metadata_valid)
        == 0 ? 0 : STORAGE_EINTEGRITY;
}

static int load_identity(storage_service_identity_t *identity) {
    size_t length = 0U;
    return critical_object_read(&protected_service_identity,
        STORAGE_POOL_METADATA_VERSION, identity, sizeof(*identity), &length,
        identity_valid) < 0 || length != sizeof(*identity)
        ? STORAGE_EINTEGRITY : 0;
}

static int store_identity(const storage_service_identity_t *identity) {
    return critical_object_update(&protected_service_identity,
        STORAGE_POOL_METADATA_VERSION, identity, sizeof(*identity),
        identity_valid) == 0 ? 0 : STORAGE_EINTEGRITY;
}

static void clear_data(size_t slot) {
    memset(&slots[slot].primary, 0, sizeof(slots[slot].primary));
    memset(&slots[slot].shadow, 0, sizeof(slots[slot].shadow));
}

static void store_data(size_t slot, const uint8_t *data, uint32_t length) {
    clear_data(slot);
    if (length != 0U) memcpy(slots[slot].primary.bytes, data, length);
    slots[slot].primary.length = length;
    slots[slot].primary.crc32 = crc32_bytes(slots[slot].primary.bytes, length);
    slots[slot].shadow = slots[slot].primary;
}

static int load_data(size_t slot, uint8_t *data, uint32_t expected_length) {
    storage_data_copy_t *primary = &slots[slot].primary;
    storage_data_copy_t *shadow = &slots[slot].shadow;
    bool primary_valid = primary->length == expected_length &&
        primary->crc32 == crc32_bytes(primary->bytes, primary->length);
    bool shadow_valid = shadow->length == expected_length &&
        shadow->crc32 == crc32_bytes(shadow->bytes, shadow->length);
    if (!primary_valid && !shadow_valid) return STORAGE_EINTEGRITY;
    storage_data_copy_t *source = primary_valid ? primary : shadow;
    if (!primary_valid) *primary = *shadow;
    if (!shadow_valid) *shadow = *primary;
    if (expected_length != 0U && data != NULL)
        memcpy(data, source->bytes, expected_length);
    return 0;
}

static storage_request_handle_t make_handle(size_t slot, uint32_t generation) {
    return (generation << 8U) | (uint32_t)(slot + 1U);
}

static int resolve_handle(storage_request_handle_t handle, size_t *slot_out,
                          storage_slot_metadata_t *metadata) {
    uint32_t encoded_slot = handle & STORAGE_HANDLE_SLOT_MASK;
    uint32_t generation = handle >> 8U;
    if (encoded_slot == 0U || encoded_slot > STORAGE_REQUEST_POOL_CAPACITY ||
        generation == 0U) return STORAGE_EINVAL;
    size_t slot = encoded_slot - 1U;
    int result = load_metadata(slot, metadata);
    if (result != 0) return result;
    if (metadata->generation != generation ||
        metadata->state == STORAGE_SLOT_FREE ||
        metadata->state == STORAGE_SLOT_RETIRED) return STORAGE_EINVAL;
    *slot_out = slot;
    return 0;
}

int storage_request_pool_init(void) {
    request_stats = (storage_request_stats_t){0};
    storage_service_identity_t identity = {0};
    if (critical_object_init(&protected_service_identity,
            STORAGE_POOL_METADATA_VERSION, &identity, sizeof(identity)) != 0)
        return STORAGE_EINTEGRITY;
    for (size_t slot = 0U; slot < STORAGE_REQUEST_POOL_CAPACITY; ++slot) {
        storage_slot_metadata_t metadata = {0};
        if (critical_object_init(&slots[slot].metadata,
                STORAGE_POOL_METADATA_VERSION, &metadata,
                sizeof(metadata)) != 0) return STORAGE_EINTEGRITY;
        clear_data(slot);
    }
    request_stats = (storage_request_stats_t){
        .version = STORAGE_REQUEST_STATS_VERSION,
        .struct_size = sizeof(storage_request_stats_t),
    };
    return 0;
}

static int bind_service_locked(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return STORAGE_EINVAL;
    storage_service_identity_t identity;
    int result = load_identity(&identity);
    if (result != 0) return result;
    if (identity.pid != 0 &&
        (identity.pid != pid || identity.generation != generation))
        return STORAGE_EACCES;
    identity.pid = pid;
    identity.generation = generation;
    return store_identity(&identity);
}

static void cancel_process_locked(int pid, uint32_t generation);

static void unbind_service_locked(int pid, uint32_t generation) {
    storage_service_identity_t identity;
    if (load_identity(&identity) == 0 && identity.pid == pid &&
        identity.generation == generation) {
        identity = (storage_service_identity_t){0};
        (void)store_identity(&identity);
    }
    cancel_process_locked(pid, generation);
}

static int submit_locked(int client_pid, uint32_t client_generation,
        const storage_request_submit_t *request, const uint8_t *block_data,
        uint64_t now_ms, storage_request_handle_t *handle_out) {
    if (client_pid <= 0 || client_generation == 0U || request == NULL ||
        handle_out == NULL || request->version != STORAGE_REQUEST_VERSION ||
        request->struct_size < sizeof(*request) ||
        request->operation < STORAGE_REQUEST_READ ||
        request->operation > STORAGE_REQUEST_FORMAT_FAT32_PREPARE ||
        request->timeout_ms == 0U ||
        request->timeout_ms > STORAGE_REQUEST_MAX_TIMEOUT_MS)
        return STORAGE_EINVAL;
    uint32_t expected = request->length;
    if (request->operation == STORAGE_REQUEST_BLOCK_FLUSH ||
        request->operation == STORAGE_REQUEST_VFS_SYNC ||
        request->operation == STORAGE_REQUEST_FORMAT_FAT12 ||
        request->operation == STORAGE_REQUEST_FORMAT_FAT32 ||
        request->operation == STORAGE_REQUEST_FORMAT_FAT32_SCAN ||
        request->operation == STORAGE_REQUEST_FORMAT_FAT32_PREPARE) expected = 0U;
    if ((request->operation == STORAGE_REQUEST_BLOCK_READ ||
         request->operation == STORAGE_REQUEST_BLOCK_WRITE) &&
        request->length != STORAGE_REQUEST_BLOCK_SIZE) return STORAGE_EMSGSIZE;
    if ((request->operation == STORAGE_REQUEST_VFS_READ ||
         request->operation == STORAGE_REQUEST_VFS_WRITE) &&
        (request->length == 0U ||
         request->length > STORAGE_REQUEST_BLOCK_SIZE)) return STORAGE_EMSGSIZE;
    if (request->length != expected) return STORAGE_EMSGSIZE;
    if (operation_is_write(request->operation) && block_data == NULL)
        return STORAGE_EINVAL;
    uint32_t client_requests = 0U;
    for (size_t slot = 0U; slot < STORAGE_REQUEST_POOL_CAPACITY; ++slot) {
        storage_slot_metadata_t metadata;
        int result = load_metadata(slot, &metadata);
        if (result != 0) return result;
        if (metadata.state != STORAGE_SLOT_FREE &&
            metadata.state != STORAGE_SLOT_RETIRED &&
            metadata.client_pid == client_pid &&
            metadata.client_generation == client_generation)
            ++client_requests;
    }
    if (client_requests >= STORAGE_REQUEST_MAX_PER_CLIENT) {
        increment_saturating(&request_stats.client_capacity_rejections);
        return STORAGE_ENOSPC;
    }
    for (size_t slot = 0U; slot < STORAGE_REQUEST_POOL_CAPACITY; ++slot) {
        storage_slot_metadata_t metadata;
        int result = load_metadata(slot, &metadata);
        if (result != 0) return result;
        if (metadata.state != STORAGE_SLOT_FREE) continue;
        if (metadata.generation == STORAGE_HANDLE_GENERATION_MAX) {
            metadata = (storage_slot_metadata_t){
                .state = STORAGE_SLOT_RETIRED,
                .generation = STORAGE_HANDLE_GENERATION_MAX,
            };
            if (store_metadata(slot, &metadata) != 0)
                return STORAGE_EINTEGRITY;
            continue;
        }
        uint32_t generation = metadata.generation + 1U;
        metadata = (storage_slot_metadata_t){
            .state = STORAGE_SLOT_QUEUED,
            .generation = generation,
            .operation = request->operation,
            .client_pid = client_pid,
            .client_generation = client_generation,
            .resource = request->resource,
            .offset = request->offset,
            .length = expected,
            .deadline_ms = deadline_after(now_ms, request->timeout_ms),
        };
        store_data(slot, operation_is_write(request->operation)
                                  ? block_data : NULL,
                   operation_is_write(request->operation) ? expected : 0U);
        result = store_metadata(slot, &metadata);
        if (result != 0) {
            clear_data(slot);
            return result;
        }
        request_added();
        *handle_out = make_handle(slot, generation);
        return 0;
    }
    increment_saturating(&request_stats.pool_capacity_rejections);
    return STORAGE_ENOSPC;
}

static int claim_locked(int service_pid, uint32_t service_generation,
        uint64_t now_ms,
        storage_request_descriptor_t *request_out, uint8_t *block_data_out) {
    if (request_out == NULL) return STORAGE_EINVAL;
    storage_service_identity_t identity;
    int result = load_identity(&identity);
    if (result != 0) return result;
    if (identity.pid != service_pid || identity.generation != service_generation)
        return STORAGE_EACCES;
    for (size_t slot = 0U; slot < STORAGE_REQUEST_POOL_CAPACITY; ++slot) {
        storage_slot_metadata_t metadata;
        result = load_metadata(slot, &metadata);
        if (result != 0) return result;
        if (metadata.state != STORAGE_SLOT_QUEUED) continue;
        if (now_ms >= metadata.deadline_ms) {
            metadata.state = STORAGE_SLOT_COMPLETE;
            metadata.result = -110;
            result = store_metadata(slot, &metadata);
            if (result != 0) return result;
            continue;
        }
        if (operation_is_write(metadata.operation) &&
            load_data(slot, block_data_out, metadata.length) != 0)
            return STORAGE_EINTEGRITY;
        metadata.state = STORAGE_SLOT_CLAIMED;
        metadata.service_pid = service_pid;
        metadata.service_generation = service_generation;
        result = store_metadata(slot, &metadata);
        if (result != 0) return result;
        *request_out = (storage_request_descriptor_t){
            .version = STORAGE_REQUEST_VERSION,
            .struct_size = sizeof(*request_out),
            .handle = make_handle(slot, metadata.generation),
            .operation = metadata.operation,
            .resource = metadata.resource,
            .offset = metadata.offset,
            .length = metadata.length,
        };
        return 0;
    }
    return STORAGE_EAGAIN;
}

static int complete_locked(int service_pid, uint32_t service_generation,
        storage_request_handle_t handle, int32_t result_code,
        const uint8_t *block_data) {
    size_t slot;
    storage_slot_metadata_t metadata;
    int result = resolve_handle(handle, &slot, &metadata);
    if (result != 0) return result;
    if (metadata.state != STORAGE_SLOT_CLAIMED ||
        metadata.service_pid != service_pid ||
        metadata.service_generation != service_generation)
        return STORAGE_EACCES;
    if (result_code == 0 && operation_is_read(metadata.operation)) {
        if (block_data == NULL) return STORAGE_EINVAL;
        store_data(slot, block_data, metadata.length);
    }
    metadata.state = STORAGE_SLOT_COMPLETE;
    metadata.result = result_code;
    return store_metadata(slot, &metadata);
}

static int collect_locked(int client_pid, uint32_t client_generation,
        storage_request_handle_t handle, int32_t *result_out,
        uint8_t *block_data_out, uint32_t *data_length_out) {
    if (result_out == NULL) return STORAGE_EINVAL;
    size_t slot;
    storage_slot_metadata_t metadata;
    int result = resolve_handle(handle, &slot, &metadata);
    if (result != 0) return result;
    if (metadata.client_pid != client_pid ||
        metadata.client_generation != client_generation)
        return STORAGE_EACCES;
    if (metadata.state != STORAGE_SLOT_COMPLETE) return STORAGE_EAGAIN;
    uint32_t data_length = metadata.result == 0 &&
        operation_is_read(metadata.operation) ? metadata.length : 0U;
    if (metadata.result == 0 && operation_is_read(metadata.operation)) {
        if (block_data_out == NULL) return STORAGE_EINVAL;
        result = load_data(slot, block_data_out, metadata.length);
        if (result != 0) return result;
    }
    *result_out = metadata.result;
    if (data_length_out != NULL) *data_length_out = data_length;
    uint32_t generation = metadata.generation;
    metadata = (storage_slot_metadata_t){.generation = generation};
    clear_data(slot);
    result = store_metadata(slot, &metadata);
    if (result == 0) request_removed();
    return result;
}

static void cancel_process_locked(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return;
    for (size_t slot = 0U; slot < STORAGE_REQUEST_POOL_CAPACITY; ++slot) {
        storage_slot_metadata_t metadata;
        if (load_metadata(slot, &metadata) != 0) continue;
        if ((metadata.client_pid == pid &&
             metadata.client_generation == generation) ||
            (metadata.service_pid == pid &&
             metadata.service_generation == generation)) {
            uint32_t saved_generation = metadata.generation;
            metadata = (storage_slot_metadata_t){
                .generation = saved_generation,
            };
            clear_data(slot);
            if (store_metadata(slot, &metadata) == 0) request_removed();
        }
    }
}

int storage_request_bind_service(int pid, uint32_t generation) {
    uint32_t flags = storage_pool_lock();
    int result = bind_service_locked(pid, generation);
    storage_pool_unlock(flags);
    return result;
}

void storage_request_unbind_service(int pid, uint32_t generation) {
    uint32_t flags = storage_pool_lock();
    unbind_service_locked(pid, generation);
    storage_pool_unlock(flags);
}

int storage_request_submit(int client_pid, uint32_t client_generation,
        const storage_request_submit_t *request, const uint8_t *block_data,
        uint64_t now_ms, storage_request_handle_t *handle_out) {
    uint32_t flags = storage_pool_lock();
    int result = submit_locked(client_pid, client_generation, request,
                               block_data, now_ms, handle_out);
    storage_pool_unlock(flags);
    return result;
}

int storage_request_claim(int service_pid, uint32_t service_generation,
        uint64_t now_ms, storage_request_descriptor_t *request_out,
        uint8_t *block_data_out) {
    uint32_t flags = storage_pool_lock();
    int result = claim_locked(service_pid, service_generation, now_ms,
                              request_out, block_data_out);
    storage_pool_unlock(flags);
    return result;
}

int storage_request_complete(int service_pid, uint32_t service_generation,
        storage_request_handle_t handle, int32_t result_code,
        const uint8_t *block_data) {
    uint32_t flags = storage_pool_lock();
    int result = complete_locked(service_pid, service_generation, handle,
                                 result_code, block_data);
    storage_pool_unlock(flags);
    return result;
}

int storage_request_collect(int client_pid, uint32_t client_generation,
        storage_request_handle_t handle, int32_t *result_out,
        uint8_t *block_data_out) {
    return storage_request_collect_ex(client_pid, client_generation, handle,
                                      result_out, block_data_out, NULL);
}

int storage_request_collect_ex(int client_pid, uint32_t client_generation,
        storage_request_handle_t handle, int32_t *result_out,
        uint8_t *block_data_out, uint32_t *data_length_out) {
    uint32_t flags = storage_pool_lock();
    int result = collect_locked(client_pid, client_generation, handle,
                                result_out, block_data_out, data_length_out);
    storage_pool_unlock(flags);
    return result;
}

void storage_request_cancel_process(int pid, uint32_t generation) {
    uint32_t flags = storage_pool_lock();
    cancel_process_locked(pid, generation);
    storage_pool_unlock(flags);
}

int storage_request_stats(storage_request_stats_t *stats_out) {
    if (stats_out == NULL) return STORAGE_EINVAL;
    uint32_t flags = storage_pool_lock();
    *stats_out = request_stats;
    storage_pool_unlock(flags);
    return 0;
}

#ifdef REIST_HOST_TEST
int storage_request_test_corrupt_data(storage_request_handle_t handle,
                                      bool corrupt_both_copies) {
    uint32_t encoded_slot = handle & STORAGE_HANDLE_SLOT_MASK;
    if (encoded_slot == 0U || encoded_slot > STORAGE_REQUEST_POOL_CAPACITY)
        return STORAGE_EINVAL;
    size_t slot = encoded_slot - 1U;
    slots[slot].primary.bytes[0] ^= 1U;
    if (corrupt_both_copies) slots[slot].shadow.bytes[1] ^= 2U;
    return 0;
}

int storage_request_test_corrupt_metadata(storage_request_handle_t handle,
                                          bool corrupt_both_copies) {
    uint32_t encoded_slot = handle & STORAGE_HANDLE_SLOT_MASK;
    if (encoded_slot == 0U || encoded_slot > STORAGE_REQUEST_POOL_CAPACITY)
        return STORAGE_EINVAL;
    size_t slot = encoded_slot - 1U;
    slots[slot].metadata.primary.crc32 ^= 1U;
    if (corrupt_both_copies) slots[slot].metadata.shadow.crc32 ^= 2U;
    return 0;
}
#endif
