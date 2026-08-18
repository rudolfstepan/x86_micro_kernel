/**
 * @file kernel/ipc/ipc.c
 * @brief Implementiert capability-geschützte, begrenzte IPC-Endpunkte.
 *
 * Layer: Ring-0 IPC service.
 * Contract: Endpunkt-Handles codieren Slot und Generation; Nachrichten werden
 *           vollständig validiert und kopiert, bevor sie veröffentlicht werden.
 * Safety: Endpunkte, Capabilities, Queue-Tiefe, Nachrichtengröße und Wartezeit
 *         sind fest begrenzt. Stale oder unberechtigte Handles bleiben wirkungslos.
 */
#include "include/kernel/ipc.h"
#include "include/kernel/critical_object.h"

#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/wait_queue.h"
#include "kernel/time/pit.h"

#ifdef REIST_HOST_TEST
#include <string.h>
static uint32_t ipc_lock(void) { return 0U; }
static void ipc_unlock(uint32_t flags) { (void)flags; }
#else
#include "arch/x86/include/interrupt.h"
#include "lib/libc/string.h"
static uint32_t ipc_lock(void) { return irq_save(); }
static void ipc_unlock(uint32_t flags) { irq_restore(flags); }
#endif

#define IPC_MAX_CAPABILITY_RECORDS 64U
#define IPC_HANDLE_SLOT_MASK 0xFFU
#define IPC_HANDLE_GENERATION_MAX 0x00FFFFFFU

#define IPC_EBADF    (-9)
#define IPC_EAGAIN   (-11)
#define IPC_EACCES   (-13)
#define IPC_EINVAL   (-22)
#define IPC_ENOSPC   (-28)
#define IPC_EMSGSIZE (-90)
#define IPC_ETIMEDOUT (-110)
#define IPC_METADATA_VERSION 1U
#define IPC_MESSAGE_CHUNKS 3U

typedef struct {
    bool active;
    bool retired;
    bool peer_seen;
    uint32_t generation;
    int owner_pid;
    uint32_t owner_generation;
    uint32_t peer_capabilities;
    uint32_t head;
    uint32_t count;
    ipc_message_t messages[IPC_QUEUE_DEPTH];
    int sender_pid[IPC_QUEUE_DEPTH];
    uint32_t sender_generation[IPC_QUEUE_DEPTH];
    wait_queue_t send_waiters;
    wait_queue_t receive_waiters;
} ipc_endpoint_t;

typedef struct {
    bool active;
    ipc_handle_t handle;
    Process *holder;
    int holder_pid;
    uint32_t holder_generation;
    uint32_t rights;
    uint8_t endpoint_slot;
} ipc_capability_record_t;

static ipc_endpoint_t ipc_endpoints[IPC_MAX_ENDPOINTS];
static ipc_capability_record_t
    ipc_capability_records[IPC_MAX_CAPABILITY_RECORDS];

typedef struct {
    uint32_t active;
    uint32_t retired;
    uint32_t peer_seen;
    uint32_t generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t peer_capabilities;
    uint32_t head;
    uint32_t count;
} ipc_endpoint_metadata_t;

typedef struct {
    uint32_t active;
    ipc_handle_t handle;
    uintptr_t holder;
    int32_t holder_pid;
    uint32_t holder_generation;
    uint32_t rights;
    uint32_t endpoint_slot;
} ipc_capability_metadata_t;

typedef struct {
    ipc_message_t message;
    int32_t sender_pid;
    uint32_t sender_generation;
} ipc_protected_message_t;

static critical_object_t ipc_endpoint_integrity[IPC_MAX_ENDPOINTS];
static critical_object_t ipc_capability_integrity[IPC_MAX_CAPABILITY_RECORDS];
static critical_object_t
    ipc_message_integrity[IPC_MAX_ENDPOINTS][IPC_QUEUE_DEPTH]
                         [IPC_MESSAGE_CHUNKS];
static uint32_t ipc_integrity_corrections;
static bool ipc_capability_scan_corrupt;
static ipc_resource_stats_t ipc_stats;

static void increment_saturating(uint32_t *value) {
    if (*value != UINT32_MAX) ++*value;
}

static void resource_added(uint32_t *active, uint32_t *high_water) {
    increment_saturating(active);
    if (*active > *high_water) *high_water = *active;
}

static void resource_removed(uint32_t *active) {
    if (*active != 0U) --*active;
}

static void messages_removed(uint32_t count) {
    if (ipc_stats.queued_messages >= count)
        ipc_stats.queued_messages -= count;
    else
        ipc_stats.queued_messages = 0U;
}

_Static_assert(sizeof(ipc_endpoint_metadata_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "IPC endpoint metadata exceeds critical-object capacity");
_Static_assert(sizeof(ipc_capability_metadata_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "IPC capability metadata exceeds critical-object capacity");
_Static_assert(sizeof(ipc_protected_message_t) <=
                   IPC_MESSAGE_CHUNKS * CRITICAL_OBJECT_MAX_PAYLOAD,
               "IPC message split is too small");
_Static_assert(sizeof(ipc_resource_stats_t) == 9U * sizeof(uint32_t),
               "IPC resource stats ABI drift");

static bool endpoint_metadata_valid(const void *payload, size_t length) {
    const ipc_endpoint_metadata_t *value = payload;
    if (length != sizeof(*value) || value->active > 1U ||
        value->retired > 1U || value->peer_seen > 1U ||
        value->generation > IPC_HANDLE_GENERATION_MAX ||
        value->peer_capabilities > IPC_MAX_CAPABILITY_RECORDS ||
        value->head >= IPC_QUEUE_DEPTH || value->count > IPC_QUEUE_DEPTH) {
        return false;
    }
    return value->active == 0U ||
        (value->generation != 0U && value->owner_pid > 0 &&
         value->owner_generation != 0U);
}

static bool capability_metadata_valid(const void *payload, size_t length) {
    const ipc_capability_metadata_t *value = payload;
    const uint32_t all_rights =
        IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE | IPC_RIGHT_CONTROL;
    if (length != sizeof(*value) || value->active > 1U) return false;
    return value->active == 0U ||
        (value->handle != IPC_INVALID_HANDLE && value->holder != 0U &&
         value->holder_pid > 0 && value->holder_generation != 0U &&
         value->rights != 0U && (value->rights & ~all_rights) == 0U &&
         value->endpoint_slot < IPC_MAX_ENDPOINTS);
}

static bool message_chunk_valid(const void *payload, size_t length) {
    (void)payload;
    return length != 0U && length <= CRITICAL_OBJECT_MAX_PAYLOAD;
}

static void note_integrity_result(critical_read_result_t result) {
    if ((result == CRITICAL_READ_CORRECTED ||
         result == CRITICAL_READ_RECOVERED) &&
        ipc_integrity_corrections != UINT32_MAX) {
        ++ipc_integrity_corrections;
    }
}

static int seal_endpoint(size_t slot) {
    ipc_endpoint_t *endpoint = &ipc_endpoints[slot];
    ipc_endpoint_metadata_t value = {
        endpoint->active ? 1U : 0U,
        endpoint->retired ? 1U : 0U,
        endpoint->peer_seen ? 1U : 0U,
        endpoint->generation,
        endpoint->owner_pid,
        endpoint->owner_generation,
        endpoint->peer_capabilities,
        endpoint->head,
        endpoint->count,
    };
    if (!endpoint_metadata_valid(&value, sizeof(value))) return IPC_EINTEGRITY;
    return critical_object_update(&ipc_endpoint_integrity[slot],
        IPC_METADATA_VERSION, &value, sizeof(value), endpoint_metadata_valid);
}

static int load_endpoint(size_t slot) {
    ipc_endpoint_metadata_t value;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &ipc_endpoint_integrity[slot], IPC_METADATA_VERSION, &value,
        sizeof(value), &length, endpoint_metadata_valid);
    if (result < 0 || length != sizeof(value)) return IPC_EINTEGRITY;
    note_integrity_result(result);
    ipc_endpoint_t *endpoint = &ipc_endpoints[slot];
    endpoint->active = value.active != 0U;
    endpoint->retired = value.retired != 0U;
    endpoint->peer_seen = value.peer_seen != 0U;
    endpoint->generation = value.generation;
    endpoint->owner_pid = value.owner_pid;
    endpoint->owner_generation = value.owner_generation;
    endpoint->peer_capabilities = value.peer_capabilities;
    endpoint->head = value.head;
    endpoint->count = value.count;
    return 0;
}

static int seal_capability(size_t slot) {
    ipc_capability_record_t *record = &ipc_capability_records[slot];
    ipc_capability_metadata_t value = {
        record->active ? 1U : 0U,
        record->handle,
        (uintptr_t)record->holder,
        record->holder_pid,
        record->holder_generation,
        record->rights,
        record->endpoint_slot,
    };
    if (!capability_metadata_valid(&value, sizeof(value))) {
        return IPC_EINTEGRITY;
    }
    return critical_object_update(&ipc_capability_integrity[slot],
        IPC_METADATA_VERSION, &value, sizeof(value), capability_metadata_valid);
}

static int load_capability(size_t slot) {
    ipc_capability_metadata_t value;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &ipc_capability_integrity[slot], IPC_METADATA_VERSION, &value,
        sizeof(value), &length, capability_metadata_valid);
    if (result < 0 || length != sizeof(value)) return IPC_EINTEGRITY;
    note_integrity_result(result);
    ipc_capability_record_t *record = &ipc_capability_records[slot];
    record->active = value.active != 0U;
    record->handle = value.handle;
    record->holder = (Process *)value.holder;
    record->holder_pid = value.holder_pid;
    record->holder_generation = value.holder_generation;
    record->rights = value.rights;
    record->endpoint_slot = (uint8_t)value.endpoint_slot;
    return 0;
}

static ipc_handle_t make_handle(size_t slot, uint32_t generation) {
    return (generation << 8U) | (ipc_handle_t)(slot + 1U);
}

static int decode_handle(ipc_handle_t handle, size_t *slot,
                         uint32_t *generation) {
    uint32_t encoded_slot = handle & IPC_HANDLE_SLOT_MASK;
    uint32_t encoded_generation = handle >> 8U;
    if (encoded_slot == 0U || encoded_slot > IPC_MAX_ENDPOINTS ||
        encoded_generation == 0U || slot == NULL || generation == NULL) {
        return IPC_EBADF;
    }
    *slot = (size_t)(encoded_slot - 1U);
    *generation = encoded_generation;
    return 0;
}

static int process_capability_slot(const Process *process,
                                   ipc_handle_t handle) {
    if (process == NULL) return -1;
    for (size_t index = 0;
         index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        if (process->ipc_capabilities[index].in_use &&
            process->ipc_capabilities[index].handle == handle) {
            return (int)index;
        }
    }
    return -1;
}

static int free_process_capability_slot(const Process *process) {
    if (process == NULL) return -1;
    for (size_t index = 0;
         index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        if (!process->ipc_capabilities[index].in_use) return (int)index;
    }
    return -1;
}

static int free_capability_record(void) {
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (load_capability(index) != 0) return IPC_EINTEGRITY;
        if (!ipc_capability_records[index].active) return (int)index;
    }
    return -1;
}

static ipc_capability_record_t *capability_record_for(
    const Process *process, ipc_handle_t handle) {
    if (process == NULL) return NULL;
    ipc_capability_scan_corrupt = false;
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (load_capability(index) != 0) {
            ipc_capability_scan_corrupt = true;
            return NULL;
        }
        ipc_capability_record_t *record = &ipc_capability_records[index];
        if (record->active && record->handle == handle &&
            record->holder_pid == process->pid &&
            record->holder_generation == process->generation) {
            return record;
        }
    }
    return NULL;
}

static void quarantine_endpoint_locked(size_t endpoint_slot);

static int resolve_capability(Process *process, ipc_handle_t handle,
                              uint32_t required_rights,
                              ipc_endpoint_t **endpoint_out,
                              size_t *endpoint_slot_out) {
    size_t endpoint_slot;
    uint32_t generation;
    if (process == NULL || !process->is_running ||
        process_capability_slot(process, handle) < 0 ||
        decode_handle(handle, &endpoint_slot, &generation) != 0) {
        return IPC_EBADF;
    }

    ipc_capability_record_t *record = capability_record_for(process, handle);
    if (ipc_capability_scan_corrupt) {
        quarantine_endpoint_locked(endpoint_slot);
        return IPC_EINTEGRITY;
    }
    if (record == NULL || record->endpoint_slot != endpoint_slot ||
        record->holder_pid != process->pid) {
        return IPC_EBADF;
    }
    if ((record->rights & required_rights) != required_rights) {
        return IPC_EACCES;
    }

    if (load_endpoint(endpoint_slot) != 0) {
        quarantine_endpoint_locked(endpoint_slot);
        return IPC_EINTEGRITY;
    }
    ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
    if (!endpoint->active || endpoint->generation != generation) {
        return IPC_EBADF;
    }
    if (endpoint->owner_pid <= 0 || endpoint->owner_generation == 0U) {
        return IPC_EBADF;
    }
    if (endpoint_out != NULL) *endpoint_out = endpoint;
    if (endpoint_slot_out != NULL) *endpoint_slot_out = endpoint_slot;
    return 0;
}

static void clear_capability_record_locked(
    ipc_capability_record_t *record) {
    if (record == NULL || !record->active) return;
    size_t record_slot = (size_t)(record - ipc_capability_records);
    size_t endpoint_slot = record->endpoint_slot;
    if (endpoint_slot < IPC_MAX_ENDPOINTS) {
        ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
        if ((record->rights & IPC_RIGHT_CONTROL) == 0U &&
            endpoint->active && endpoint->peer_capabilities != 0U) {
            --endpoint->peer_capabilities;
            (void)seal_endpoint(endpoint_slot);
        }
    }
    Process *holder = record->holder;
    if (holder != NULL && holder->pid == record->holder_pid &&
        holder->generation == record->holder_generation) {
        int local_slot = process_capability_slot(holder, record->handle);
        if (local_slot >= 0) {
            memset(&holder->ipc_capabilities[local_slot], 0,
                   sizeof(holder->ipc_capabilities[local_slot]));
        }
    }
    memset(record, 0, sizeof(*record));
    (void)seal_capability(record_slot);
    resource_removed(&ipc_stats.active_capabilities);
}

static void quarantine_endpoint_locked(size_t endpoint_slot) {
    if (endpoint_slot >= IPC_MAX_ENDPOINTS) return;
    ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
    if (endpoint->active) {
        resource_removed(&ipc_stats.active_endpoints);
        messages_removed(endpoint->count);
    }
    endpoint->active = false;
    endpoint->count = 0U;
    endpoint->head = 0U;
    endpoint->peer_capabilities = 0U;
    (void)seal_endpoint(endpoint_slot);
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (load_capability(index) != 0) continue;
        ipc_capability_record_t *record = &ipc_capability_records[index];
        if (record->active && record->endpoint_slot == endpoint_slot) {
            clear_capability_record_locked(record);
        }
    }
    (void)wait_queue_wake_all_locked(&endpoint->send_waiters);
    (void)wait_queue_wake_all_locked(&endpoint->receive_waiters);
}

static void revoke_endpoint_locked(size_t endpoint_slot) {
    ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
    if (endpoint->active) {
        resource_removed(&ipc_stats.active_endpoints);
        messages_removed(endpoint->count);
    }
    endpoint->active = false;
    endpoint->count = 0U;
    endpoint->head = 0U;
    endpoint->peer_capabilities = 0U;
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (load_capability(index) != 0) continue;
        ipc_capability_record_t *record = &ipc_capability_records[index];
        if (record->active && record->endpoint_slot == endpoint_slot &&
            record->handle == make_handle(endpoint_slot,
                                           endpoint->generation)) {
            clear_capability_record_locked(record);
        }
    }
    (void)wait_queue_wake_all_locked(&endpoint->send_waiters);
    (void)wait_queue_wake_all_locked(&endpoint->receive_waiters);
    (void)seal_endpoint(endpoint_slot);
}

static int install_capability_locked(Process *process, ipc_handle_t handle,
                                     size_t endpoint_slot,
                                     uint32_t rights) {
    int process_slot = free_process_capability_slot(process);
    int record_slot = free_capability_record();
    if (record_slot == IPC_EINTEGRITY) return IPC_EINTEGRITY;
    if (process_slot < 0 || record_slot < 0) return IPC_ENOSPC;

    ipc_capability_t *capability =
        &process->ipc_capabilities[process_slot];
    capability->handle = handle;
    capability->rights = rights;
    capability->in_use = true;

    ipc_capability_record_t *record =
        &ipc_capability_records[record_slot];
    record->active = true;
    record->handle = handle;
    record->holder = process;
    record->holder_pid = process->pid;
    record->holder_generation = process->generation;
    record->rights = rights;
    record->endpoint_slot = (uint8_t)endpoint_slot;
    if (seal_capability((size_t)record_slot) != 0) {
        memset(capability, 0, sizeof(*capability));
        memset(record, 0, sizeof(*record));
        return IPC_EINTEGRITY;
    }
    resource_added(&ipc_stats.active_capabilities,
                   &ipc_stats.capability_high_water);
    return 0;
}

static int counterpart_identity_locked(size_t endpoint_slot,
                                       const Process *process,
                                       uint32_t required_right,
                                       int *pid_out,
                                       uint32_t *generation_out) {
    if (pid_out != NULL) *pid_out = 0;
    if (generation_out != NULL) *generation_out = 0U;
    bool found = false;
    int pid = 0;
    uint32_t generation = 0U;
    for (size_t index = 0U; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (load_capability(index) != 0) return IPC_EINTEGRITY;
        const ipc_capability_record_t *record =
            &ipc_capability_records[index];
        if (!record->active || record->endpoint_slot != endpoint_slot ||
            (record->rights & required_right) == 0U ||
            (record->holder_pid == process->pid &&
             record->holder_generation == process->generation)) continue;
        if (found && (pid != record->holder_pid ||
                      generation != record->holder_generation)) {
            return IPC_EINTEGRITY;
        }
        found = true;
        pid = record->holder_pid;
        generation = record->holder_generation;
    }
    if (found) {
        if (pid_out != NULL) *pid_out = pid;
        if (generation_out != NULL) *generation_out = generation;
        return 1;
    }
    return 0;
}

void ipc_init(void) {
    uint32_t flags = ipc_lock();
    memset(ipc_endpoints, 0, sizeof(ipc_endpoints));
    memset(ipc_capability_records, 0, sizeof(ipc_capability_records));
    memset(ipc_endpoint_integrity, 0, sizeof(ipc_endpoint_integrity));
    memset(ipc_capability_integrity, 0, sizeof(ipc_capability_integrity));
    memset(ipc_message_integrity, 0, sizeof(ipc_message_integrity));
    ipc_integrity_corrections = 0U;
    ipc_capability_scan_corrupt = false;
    ipc_stats = (ipc_resource_stats_t){
        .version = IPC_RESOURCE_STATS_VERSION,
        .struct_size = sizeof(ipc_resource_stats_t),
    };
    for (size_t index = 0; index < IPC_MAX_ENDPOINTS; ++index) {
        wait_queue_init(&ipc_endpoints[index].send_waiters);
        wait_queue_init(&ipc_endpoints[index].receive_waiters);
        ipc_endpoint_metadata_t empty = {0};
        (void)critical_object_init(&ipc_endpoint_integrity[index],
            IPC_METADATA_VERSION, &empty, sizeof(empty));
    }
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        ipc_capability_metadata_t empty = {0};
        (void)critical_object_init(&ipc_capability_integrity[index],
            IPC_METADATA_VERSION, &empty, sizeof(empty));
    }
    ipc_unlock(flags);
}

int ipc_create(Process *owner, ipc_handle_t *handle) {
    if (owner == NULL || handle == NULL || !owner->is_running) {
        return IPC_EINVAL;
    }
    uint32_t flags = ipc_lock();
    int available_record = free_capability_record();
    if (available_record == IPC_EINTEGRITY) {
        ipc_unlock(flags);
        return IPC_EINTEGRITY;
    }
    if (free_process_capability_slot(owner) < 0 || available_record < 0) {
        increment_saturating(&ipc_stats.capacity_rejections);
        ipc_unlock(flags);
        return IPC_ENOSPC;
    }

    size_t endpoint_slot = IPC_MAX_ENDPOINTS;
    for (size_t index = 0; index < IPC_MAX_ENDPOINTS; ++index) {
        if (load_endpoint(index) != 0) {
            quarantine_endpoint_locked(index);
            continue;
        }
        ipc_endpoint_t *candidate = &ipc_endpoints[index];
        if (candidate->active || candidate->retired) continue;
        if (candidate->generation >= IPC_HANDLE_GENERATION_MAX) {
            candidate->retired = true;
            (void)seal_endpoint(index);
            continue;
        }
        endpoint_slot = index;
        break;
    }
    if (endpoint_slot == IPC_MAX_ENDPOINTS) {
        increment_saturating(&ipc_stats.capacity_rejections);
        ipc_unlock(flags);
        return IPC_ENOSPC;
    }

    ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
    uint32_t generation = endpoint->generation + 1U;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->active = true;
    endpoint->generation = generation;
    endpoint->owner_pid = owner->pid;
    endpoint->owner_generation = owner->generation;
    wait_queue_init(&endpoint->send_waiters);
    wait_queue_init(&endpoint->receive_waiters);
    if (seal_endpoint(endpoint_slot) != 0) {
        endpoint->active = false;
        ipc_unlock(flags);
        return IPC_EINTEGRITY;
    }

    ipc_handle_t created = make_handle(endpoint_slot, generation);
    int result = install_capability_locked(
        owner, created, endpoint_slot,
        IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE | IPC_RIGHT_CONTROL);
    if (result != 0) {
        endpoint->active = false;
        (void)seal_endpoint(endpoint_slot);
        ipc_unlock(flags);
        return result;
    }
    resource_added(&ipc_stats.active_endpoints,
                   &ipc_stats.endpoint_high_water);
    *handle = created;
    ipc_unlock(flags);
    return 0;
}

static bool message_valid(const ipc_message_t *message) {
    return message != NULL && message->version == IPC_MESSAGE_VERSION &&
           message->struct_size == sizeof(*message) &&
           message->length <= IPC_MAX_MESSAGE_SIZE;
}

static int seal_message(size_t endpoint_slot, uint32_t queue_slot,
                        const ipc_protected_message_t *value) {
    const uint8_t *bytes = (const uint8_t *)value;
    size_t remaining = sizeof(*value);
    for (size_t chunk = 0; chunk < IPC_MESSAGE_CHUNKS; ++chunk) {
        size_t length = remaining > CRITICAL_OBJECT_MAX_PAYLOAD
            ? CRITICAL_OBJECT_MAX_PAYLOAD : remaining;
        critical_object_t *object =
            &ipc_message_integrity[endpoint_slot][queue_slot][chunk];
        int result = object->primary.words[0] == 0U
            ? critical_object_init(object, IPC_METADATA_VERSION, bytes, length)
            : critical_object_update(object, IPC_METADATA_VERSION, bytes,
                                     length, message_chunk_valid);
        if (result != 0) return IPC_EINTEGRITY;
        bytes += length;
        remaining -= length;
    }
    return remaining == 0U ? 0 : IPC_EINTEGRITY;
}

static int load_message(size_t endpoint_slot, uint32_t queue_slot,
                        ipc_protected_message_t *value) {
    uint8_t *bytes = (uint8_t *)value;
    size_t remaining = sizeof(*value);
    for (size_t chunk = 0; chunk < IPC_MESSAGE_CHUNKS; ++chunk) {
        size_t expected = remaining > CRITICAL_OBJECT_MAX_PAYLOAD
            ? CRITICAL_OBJECT_MAX_PAYLOAD : remaining;
        size_t length = 0U;
        critical_read_result_t result = critical_object_read(
            &ipc_message_integrity[endpoint_slot][queue_slot][chunk],
            IPC_METADATA_VERSION, bytes, expected, &length,
            message_chunk_valid);
        if (result < 0 || length != expected) return IPC_EINTEGRITY;
        note_integrity_result(result);
        bytes += length;
        remaining -= length;
    }
    bool kernel_sender = value->sender_pid == 0 &&
                         value->sender_generation == 0U;
    bool process_sender = value->sender_pid > 0 &&
                          value->sender_generation != 0U;
    if (remaining != 0U || !message_valid(&value->message) ||
        (!kernel_sender && !process_sender)) {
        return IPC_EINTEGRITY;
    }
    return 0;
}

static int enqueue_message_locked(size_t endpoint_slot,
                                  ipc_endpoint_t *endpoint,
                                  const ipc_message_t *message,
                                  int sender_pid,
                                  uint32_t sender_generation) {
    if (endpoint->count >= IPC_QUEUE_DEPTH) {
        increment_saturating(&ipc_stats.capacity_rejections);
        return IPC_EAGAIN;
    }
    uint32_t tail = (endpoint->head + endpoint->count) % IPC_QUEUE_DEPTH;
    memset(&endpoint->messages[tail], 0, sizeof(endpoint->messages[tail]));
    endpoint->messages[tail].version = message->version;
    endpoint->messages[tail].struct_size = message->struct_size;
    endpoint->messages[tail].length = message->length;
    memcpy(endpoint->messages[tail].payload, message->payload,
           message->length);
    endpoint->sender_pid[tail] = sender_pid;
    endpoint->sender_generation[tail] = sender_generation;
    ipc_protected_message_t protected_message = {
        endpoint->messages[tail], sender_pid, sender_generation
    };
    if (seal_message(endpoint_slot, tail, &protected_message) != 0) {
        return IPC_EINTEGRITY;
    }
    ++endpoint->count;
    if (seal_endpoint(endpoint_slot) != 0) {
        --endpoint->count;
        return IPC_EINTEGRITY;
    }
    resource_added(&ipc_stats.queued_messages,
                   &ipc_stats.message_high_water);
    (void)wait_queue_wake_one_locked(&endpoint->receive_waiters);
    return 0;
}

int ipc_send_timeout(Process *sender, ipc_handle_t handle,
                     const ipc_message_t *message, uint32_t timeout_ms) {
    if (!message_valid(message)) {
        return message != NULL && message->length > IPC_MAX_MESSAGE_SIZE
            ? IPC_EMSGSIZE : IPC_EINVAL;
    }
    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now + (uint64_t)timeout_ms;
    if (deadline < now) deadline = UINT64_MAX;
    for (;;) {
        uint32_t flags = ipc_lock();
        ipc_endpoint_t *endpoint = NULL;
        size_t endpoint_slot = 0U;
        int result = resolve_capability(sender, handle, IPC_RIGHT_SEND,
                                        &endpoint, &endpoint_slot);
        if (result != 0) {
            ipc_unlock(flags);
            return result;
        }
        if (endpoint->peer_seen && endpoint->peer_capabilities == 0U) {
            ipc_unlock(flags);
            return IPC_EPIPE;
        }
        if (endpoint->count < IPC_QUEUE_DEPTH) {
            result = enqueue_message_locked(endpoint_slot, endpoint, message,
                                            sender->pid,
                                            sender->generation);
            if (result == IPC_EINTEGRITY) {
                quarantine_endpoint_locked(endpoint_slot);
            }
            ipc_unlock(flags);
            return result;
        }
        if (timeout_ms == 0U) {
            increment_saturating(&ipc_stats.capacity_rejections);
            ipc_unlock(flags);
            return IPC_EAGAIN;
        }
        if (pit_monotonic_ms() >= deadline) {
            ipc_unlock(flags);
            return IPC_ETIMEDOUT;
        }
        int owner_pid = 0;
        uint32_t owner_generation = 0U;
        int counterpart = counterpart_identity_locked(
            endpoint_slot, sender, IPC_RIGHT_RECEIVE, &owner_pid,
            &owner_generation);
        if (counterpart == IPC_EINTEGRITY) {
            quarantine_endpoint_locked(endpoint_slot);
            ipc_unlock(flags);
            return IPC_EINTEGRITY;
        }
        if (counterpart > 0)
            (void)scheduler_set_wait_owner_locked(owner_pid,
                                                  owner_generation);
        result = wait_queue_block_until_locked(&endpoint->send_waiters,
                                               TASK_BLOCK_WAITING, deadline);
        scheduler_clear_wait_owner_locked();
        ipc_unlock(flags);
        if (result == IPC_ETIMEDOUT) return IPC_ETIMEDOUT;
        if (result != 0) return IPC_EAGAIN;
    }
}

int ipc_send(Process *sender, ipc_handle_t handle,
             const ipc_message_t *message) {
    return ipc_send_timeout(sender, handle, message, IPC_DEFAULT_TIMEOUT_MS);
}

static int receivable_offset(size_t endpoint_slot,
                             const ipc_endpoint_t *endpoint,
                             const Process *receiver) {
    for (uint32_t offset = 0; offset < endpoint->count; ++offset) {
        uint32_t index = (endpoint->head + offset) % IPC_QUEUE_DEPTH;
        ipc_protected_message_t value;
        if (load_message(endpoint_slot, index, &value) != 0) {
            return IPC_EINTEGRITY;
        }
        if (value.sender_pid != receiver->pid ||
            value.sender_generation != receiver->generation) {
            return (int)offset;
        }
    }
    return -1;
}

static int remove_message_locked(size_t endpoint_slot,
                                 ipc_endpoint_t *endpoint, uint32_t offset,
                                 ipc_message_t *message) {
    uint32_t index = (endpoint->head + offset) % IPC_QUEUE_DEPTH;
    ipc_protected_message_t protected_message;
    if (load_message(endpoint_slot, index, &protected_message) != 0) {
        return IPC_EINTEGRITY;
    }
    *message = protected_message.message;
    while (offset + 1U < endpoint->count) {
        uint32_t next = (index + 1U) % IPC_QUEUE_DEPTH;
        ipc_protected_message_t shifted;
        if (load_message(endpoint_slot, next, &shifted) != 0) {
            return IPC_EINTEGRITY;
        }
        endpoint->messages[index] = endpoint->messages[next];
        endpoint->sender_pid[index] = endpoint->sender_pid[next];
        endpoint->sender_generation[index] =
            endpoint->sender_generation[next];
        if (seal_message(endpoint_slot, index, &shifted) != 0) {
            return IPC_EINTEGRITY;
        }
        index = next;
        ++offset;
    }
    --endpoint->count;
    resource_removed(&ipc_stats.queued_messages);
    if (endpoint->count == 0U) endpoint->head = 0U;
    return seal_endpoint(endpoint_slot) == 0 ? 0 : IPC_EINTEGRITY;
}

int ipc_send_external_from_peer(int owner_pid, uint32_t owner_generation,
                                ipc_handle_t handle,
                                const ipc_message_t *message) {
    if (owner_pid <= 0 || owner_generation == 0U || !message_valid(message))
        return IPC_EINVAL;
    size_t endpoint_slot;
    uint32_t generation;
    if (decode_handle(handle, &endpoint_slot, &generation) != 0)
        return IPC_EBADF;

    uint32_t flags = ipc_lock();
    if (load_endpoint(endpoint_slot) != 0) {
        quarantine_endpoint_locked(endpoint_slot);
        ipc_unlock(flags);
        return IPC_EINTEGRITY;
    }
    ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
    if (!endpoint->active || endpoint->generation != generation ||
        endpoint->owner_pid != owner_pid ||
        endpoint->owner_generation != owner_generation) {
        ipc_unlock(flags);
        return IPC_EBADF;
    }

    ipc_capability_record_t *peer = NULL;
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (load_capability(index) != 0) {
            quarantine_endpoint_locked(endpoint_slot);
            ipc_unlock(flags);
            return IPC_EINTEGRITY;
        }
        ipc_capability_record_t *candidate = &ipc_capability_records[index];
        if (candidate->active && candidate->endpoint_slot == endpoint_slot &&
            (candidate->rights & IPC_RIGHT_CONTROL) == 0U) {
            if (peer != NULL) {
                quarantine_endpoint_locked(endpoint_slot);
                ipc_unlock(flags);
                return IPC_EINTEGRITY;
            }
            peer = candidate;
        }
    }
    if (peer == NULL || peer->holder == NULL || !peer->holder->is_running ||
        peer->holder->pid != peer->holder_pid ||
        peer->holder->generation != peer->holder_generation) {
        ipc_unlock(flags);
        return IPC_EPIPE;
    }
    int result = enqueue_message_locked(endpoint_slot, endpoint, message,
                                        peer->holder_pid,
                                        peer->holder_generation);
    if (result == IPC_EINTEGRITY) quarantine_endpoint_locked(endpoint_slot);
    ipc_unlock(flags);
    return result;
}

int ipc_send_kernel_to_owner(int owner_pid, uint32_t owner_generation,
                             ipc_handle_t handle,
                             const ipc_message_t *message) {
    if (owner_pid <= 0 || owner_generation == 0U || !message_valid(message))
        return IPC_EINVAL;
    size_t endpoint_slot;
    uint32_t generation;
    if (decode_handle(handle, &endpoint_slot, &generation) != 0)
        return IPC_EBADF;
    uint32_t flags = ipc_lock();
    if (load_endpoint(endpoint_slot) != 0) {
        quarantine_endpoint_locked(endpoint_slot);
        ipc_unlock(flags);
        return IPC_EINTEGRITY;
    }
    ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
    if (!endpoint->active || endpoint->generation != generation ||
        endpoint->owner_pid != owner_pid ||
        endpoint->owner_generation != owner_generation) {
        ipc_unlock(flags);
        return IPC_EBADF;
    }
    int result = enqueue_message_locked(endpoint_slot, endpoint, message,
                                        0, 0U);
    if (result == IPC_EINTEGRITY) quarantine_endpoint_locked(endpoint_slot);
    ipc_unlock(flags);
    return result;
}

int ipc_receive_timeout(Process *receiver, ipc_handle_t handle,
                        ipc_message_t *message, uint32_t timeout_ms) {
    if (!message_valid(message)) return IPC_EINVAL;
    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now + (uint64_t)timeout_ms;
    if (deadline < now) deadline = UINT64_MAX;
    for (;;) {
        uint32_t flags = ipc_lock();
        ipc_endpoint_t *endpoint = NULL;
        size_t endpoint_slot = 0U;
        int result = resolve_capability(receiver, handle, IPC_RIGHT_RECEIVE,
                                        &endpoint, &endpoint_slot);
        if (result != 0) {
            ipc_unlock(flags);
            return result;
        }
        int offset = receivable_offset(endpoint_slot, endpoint, receiver);
        if (offset == IPC_EINTEGRITY) {
            quarantine_endpoint_locked(endpoint_slot);
            ipc_unlock(flags);
            return IPC_EINTEGRITY;
        }
        if (offset >= 0) {
            result = remove_message_locked(endpoint_slot, endpoint,
                                           (uint32_t)offset, message);
            if (result != 0) {
                quarantine_endpoint_locked(endpoint_slot);
                ipc_unlock(flags);
                return IPC_EINTEGRITY;
            }
            (void)wait_queue_wake_one_locked(&endpoint->send_waiters);
            ipc_unlock(flags);
            return 0;
        }
        if (endpoint->peer_seen && endpoint->peer_capabilities == 0U) {
            ipc_unlock(flags);
            return IPC_EPIPE;
        }
        if (timeout_ms == 0U) {
            ipc_unlock(flags);
            return IPC_EAGAIN;
        }
        if (pit_monotonic_ms() >= deadline) {
            ipc_unlock(flags);
            return IPC_ETIMEDOUT;
        }
        int owner_pid = 0;
        uint32_t owner_generation = 0U;
        int counterpart = counterpart_identity_locked(
            endpoint_slot, receiver, IPC_RIGHT_SEND, &owner_pid,
            &owner_generation);
        if (counterpart == IPC_EINTEGRITY) {
            quarantine_endpoint_locked(endpoint_slot);
            ipc_unlock(flags);
            return IPC_EINTEGRITY;
        }
        if (counterpart > 0)
            (void)scheduler_set_wait_owner_locked(owner_pid,
                                                  owner_generation);
        result = wait_queue_block_until_locked(&endpoint->receive_waiters,
                                               TASK_BLOCK_WAITING, deadline);
        scheduler_clear_wait_owner_locked();
        ipc_unlock(flags);
        if (result == IPC_ETIMEDOUT) return IPC_ETIMEDOUT;
        if (result != 0) return IPC_EAGAIN;
    }
}

int ipc_receive(Process *receiver, ipc_handle_t handle,
                ipc_message_t *message) {
    return ipc_receive_timeout(receiver, handle, message,
                               IPC_DEFAULT_TIMEOUT_MS);
}

int ipc_close(Process *process, ipc_handle_t handle) {
    uint32_t flags = ipc_lock();
    ipc_endpoint_t *endpoint = NULL;
    size_t endpoint_slot = 0U;
    int result = resolve_capability(process, handle, IPC_RIGHT_CONTROL,
                                    &endpoint, &endpoint_slot);
    if (result != 0) {
        ipc_unlock(flags);
        return result;
    }
    if (endpoint->owner_pid != process->pid ||
        endpoint->owner_generation != process->generation) {
        ipc_unlock(flags);
        return IPC_EACCES;
    }

    int local_slot = process_capability_slot(process, handle);
    revoke_endpoint_locked(endpoint_slot);
    if (local_slot >= 0) {
        memset(&process->ipc_capabilities[local_slot], 0,
               sizeof(process->ipc_capabilities[local_slot]));
    }
    ipc_unlock(flags);
    return 0;
}

int ipc_delegate(Process *source, ipc_handle_t handle, Process *target,
                 uint32_t rights) {
    const uint32_t delegable = IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE;
    if (source == NULL || target == NULL || source == target ||
        !source->is_running || !target->is_running || rights == 0U ||
        source->generation == 0U || target->generation == 0U ||
        (rights & IPC_RIGHT_CONTROL) != 0U ||
        (rights & ~delegable) != 0U) return IPC_EINVAL;

    uint32_t flags = ipc_lock();
    ipc_endpoint_t *endpoint = NULL;
    size_t endpoint_slot = 0U;
    int result = resolve_capability(source, handle, rights, &endpoint,
                                    &endpoint_slot);
    if (result != 0) {
        ipc_unlock(flags);
        return result;
    }
    ipc_capability_record_t *source_record =
        capability_record_for(source, handle);
    if (ipc_capability_scan_corrupt) {
        quarantine_endpoint_locked(endpoint_slot);
        ipc_unlock(flags);
        return IPC_EINTEGRITY;
    }
    if (source_record == NULL ||
        (rights & source_record->rights) != rights) {
        ipc_unlock(flags);
        return IPC_EACCES;
    }
    if (endpoint->peer_capabilities != 0U ||
        process_capability_slot(target, handle) >= 0) {
        ipc_unlock(flags);
        return IPC_EACCES;
    }
    if (free_process_capability_slot(target) < 0) {
        increment_saturating(&ipc_stats.capacity_rejections);
        ipc_unlock(flags);
        return IPC_ENOSPC;
    }
    int record_slot = free_capability_record();
    if (record_slot == IPC_EINTEGRITY) {
        quarantine_endpoint_locked(endpoint_slot);
        ipc_unlock(flags);
        return IPC_EINTEGRITY;
    }
    if (record_slot < 0) {
        increment_saturating(&ipc_stats.capacity_rejections);
        ipc_unlock(flags);
        return IPC_ENOSPC;
    }

    result = install_capability_locked(target, handle, endpoint_slot, rights);
    if (result == 0) {
        endpoint->peer_seen = true;
        endpoint->peer_capabilities = 1U;
        if (seal_endpoint(endpoint_slot) != 0) {
            quarantine_endpoint_locked(endpoint_slot);
            result = IPC_EINTEGRITY;
        }
    }
    ipc_unlock(flags);
    return result;
}

int ipc_release(Process *process, ipc_handle_t handle) {
    if (process == NULL || handle == IPC_INVALID_HANDLE) return IPC_EINVAL;
    uint32_t flags = ipc_lock();
    ipc_endpoint_t *endpoint = NULL;
    size_t endpoint_slot = 0U;
    int result = resolve_capability(process, handle, 0U, &endpoint,
                                    &endpoint_slot);
    if (result != 0) {
        ipc_unlock(flags);
        return result;
    }
    if (endpoint->owner_pid == process->pid &&
        endpoint->owner_generation == process->generation) {
        ipc_unlock(flags);
        return IPC_EACCES;
    }
    ipc_capability_record_t *record = capability_record_for(process, handle);
    if (ipc_capability_scan_corrupt || record == NULL) {
        quarantine_endpoint_locked(endpoint_slot);
        ipc_unlock(flags);
        return ipc_capability_scan_corrupt ? IPC_EINTEGRITY : IPC_EBADF;
    }
    clear_capability_record_locked(record);
    (void)wait_queue_wake_all_locked(&endpoint->send_waiters);
    (void)wait_queue_wake_all_locked(&endpoint->receive_waiters);
    ipc_unlock(flags);
    return 0;
}

void ipc_process_cleanup(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return;
    uint32_t flags = ipc_lock();

    for (size_t endpoint_slot = 0;
         endpoint_slot < IPC_MAX_ENDPOINTS; ++endpoint_slot) {
        if (load_endpoint(endpoint_slot) != 0) {
            quarantine_endpoint_locked(endpoint_slot);
            continue;
        }
        ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
        if (endpoint->active && endpoint->owner_pid == pid &&
            endpoint->owner_generation == generation) {
            revoke_endpoint_locked(endpoint_slot);
        }
    }

    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (load_capability(index) != 0) continue;
        ipc_capability_record_t *record = &ipc_capability_records[index];
        if (!record->active || record->holder_pid != pid ||
            record->holder_generation != generation) continue;
        size_t endpoint_slot = record->endpoint_slot;
        clear_capability_record_locked(record);
        if (endpoint_slot < IPC_MAX_ENDPOINTS) {
            ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
            (void)wait_queue_wake_all_locked(&endpoint->send_waiters);
            (void)wait_queue_wake_all_locked(&endpoint->receive_waiters);
        }
    }

    ipc_unlock(flags);
}

uint32_t ipc_integrity_correction_count(void) {
    uint32_t flags = ipc_lock();
    uint32_t result = ipc_integrity_corrections;
    ipc_unlock(flags);
    return result;
}

int ipc_resource_stats(ipc_resource_stats_t *stats_out) {
    if (stats_out == NULL) return IPC_EINVAL;
    uint32_t flags = ipc_lock();
    *stats_out = ipc_stats;
    ipc_unlock(flags);
    return 0;
}

int ipc_fault_inject(ipc_fault_target_t target, size_t object_index,
                     size_t copy_index, size_t word_index, uint32_t bit_mask) {
    if (copy_index > 1U || word_index >= CRITICAL_OBJECT_WORDS ||
        bit_mask == 0U) {
        return IPC_EINVAL;
    }
    uint32_t flags = ipc_lock();
    critical_object_t *object = NULL;
    if (target == IPC_FAULT_ENDPOINT && object_index < IPC_MAX_ENDPOINTS) {
        object = &ipc_endpoint_integrity[object_index];
    } else if (target == IPC_FAULT_CAPABILITY &&
               object_index < IPC_MAX_CAPABILITY_RECORDS) {
        object = &ipc_capability_integrity[object_index];
    } else if (target == IPC_FAULT_MESSAGE &&
               object_index < IPC_MAX_ENDPOINTS * IPC_QUEUE_DEPTH *
                                  IPC_MESSAGE_CHUNKS) {
        object = &ipc_message_integrity[0][0][0] + object_index;
    }
    if (object == NULL || object->primary.words[0] == 0U) {
        ipc_unlock(flags);
        return IPC_EINVAL;
    }
    critical_object_copy_t *copy = copy_index == 0U
        ? &object->primary : &object->shadow;
    copy->words[word_index] ^= bit_mask;
    ipc_unlock(flags);
    return 0;
}
