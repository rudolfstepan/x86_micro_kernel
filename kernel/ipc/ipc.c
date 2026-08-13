#include "include/kernel/ipc.h"

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
#define IPC_EPIPE    (-32)
#define IPC_EMSGSIZE (-90)
#define IPC_ETIMEDOUT (-110)

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
        if (!ipc_capability_records[index].active) return (int)index;
    }
    return -1;
}

static ipc_capability_record_t *capability_record_for(
    const Process *process, ipc_handle_t handle) {
    if (process == NULL) return NULL;
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        ipc_capability_record_t *record = &ipc_capability_records[index];
        if (record->active && record->handle == handle &&
            record->holder_pid == process->pid &&
            record->holder_generation == process->generation) {
            return record;
        }
    }
    return NULL;
}

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
    if (record == NULL || record->endpoint_slot != endpoint_slot ||
        record->holder_pid != process->pid) {
        return IPC_EBADF;
    }
    if ((record->rights & required_rights) != required_rights) {
        return IPC_EACCES;
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
    size_t endpoint_slot = record->endpoint_slot;
    if (endpoint_slot < IPC_MAX_ENDPOINTS) {
        ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
        if ((record->rights & IPC_RIGHT_CONTROL) == 0U &&
            endpoint->active && endpoint->peer_capabilities != 0U) {
            --endpoint->peer_capabilities;
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
}

static void revoke_endpoint_locked(size_t endpoint_slot) {
    ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
    endpoint->active = false;
    endpoint->count = 0U;
    endpoint->head = 0U;
    endpoint->peer_capabilities = 0U;
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        ipc_capability_record_t *record = &ipc_capability_records[index];
        if (record->active && record->endpoint_slot == endpoint_slot &&
            record->handle == make_handle(endpoint_slot,
                                           endpoint->generation)) {
            clear_capability_record_locked(record);
        }
    }
    (void)wait_queue_wake_all_locked(&endpoint->send_waiters);
    (void)wait_queue_wake_all_locked(&endpoint->receive_waiters);
}

static int install_capability_locked(Process *process, ipc_handle_t handle,
                                     size_t endpoint_slot,
                                     uint32_t rights) {
    int process_slot = free_process_capability_slot(process);
    int record_slot = free_capability_record();
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
    return 0;
}

void ipc_init(void) {
    uint32_t flags = ipc_lock();
    memset(ipc_endpoints, 0, sizeof(ipc_endpoints));
    memset(ipc_capability_records, 0, sizeof(ipc_capability_records));
    for (size_t index = 0; index < IPC_MAX_ENDPOINTS; ++index) {
        wait_queue_init(&ipc_endpoints[index].send_waiters);
        wait_queue_init(&ipc_endpoints[index].receive_waiters);
    }
    ipc_unlock(flags);
}

int ipc_create(Process *owner, ipc_handle_t *handle) {
    if (owner == NULL || handle == NULL || !owner->is_running) {
        return IPC_EINVAL;
    }
    uint32_t flags = ipc_lock();
    if (free_process_capability_slot(owner) < 0 ||
        free_capability_record() < 0) {
        ipc_unlock(flags);
        return IPC_ENOSPC;
    }

    size_t endpoint_slot = IPC_MAX_ENDPOINTS;
    for (size_t index = 0; index < IPC_MAX_ENDPOINTS; ++index) {
        ipc_endpoint_t *candidate = &ipc_endpoints[index];
        if (candidate->active || candidate->retired) continue;
        if (candidate->generation >= IPC_HANDLE_GENERATION_MAX) {
            candidate->retired = true;
            continue;
        }
        endpoint_slot = index;
        break;
    }
    if (endpoint_slot == IPC_MAX_ENDPOINTS) {
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

    ipc_handle_t created = make_handle(endpoint_slot, generation);
    int result = install_capability_locked(
        owner, created, endpoint_slot,
        IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE | IPC_RIGHT_CONTROL);
    if (result != 0) {
        endpoint->active = false;
        ipc_unlock(flags);
        return result;
    }
    *handle = created;
    ipc_unlock(flags);
    return 0;
}

static bool message_valid(const ipc_message_t *message) {
    return message != NULL && message->version == IPC_MESSAGE_VERSION &&
           message->struct_size == sizeof(*message) &&
           message->length <= IPC_MAX_MESSAGE_SIZE;
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
        int result = resolve_capability(sender, handle, IPC_RIGHT_SEND,
                                        &endpoint, NULL);
        if (result != 0) {
            ipc_unlock(flags);
            return result;
        }
        if (endpoint->peer_seen && endpoint->peer_capabilities == 0U) {
            ipc_unlock(flags);
            return IPC_EPIPE;
        }
        if (endpoint->count < IPC_QUEUE_DEPTH) {
            uint32_t tail = (endpoint->head + endpoint->count) %
                            IPC_QUEUE_DEPTH;
            memset(&endpoint->messages[tail], 0,
                   sizeof(endpoint->messages[tail]));
            endpoint->messages[tail].version = message->version;
            endpoint->messages[tail].struct_size = message->struct_size;
            endpoint->messages[tail].length = message->length;
            memcpy(endpoint->messages[tail].payload, message->payload,
                   message->length);
            endpoint->sender_pid[tail] = sender->pid;
            endpoint->sender_generation[tail] = sender->generation;
            ++endpoint->count;
            (void)wait_queue_wake_one_locked(&endpoint->receive_waiters);
            ipc_unlock(flags);
            return 0;
        }
        if (timeout_ms == 0U) {
            ipc_unlock(flags);
            return IPC_EAGAIN;
        }
        if (pit_monotonic_ms() >= deadline) {
            ipc_unlock(flags);
            return IPC_ETIMEDOUT;
        }
        result = wait_queue_block_until_locked(&endpoint->send_waiters,
                                               TASK_BLOCK_WAITING, deadline);
        ipc_unlock(flags);
        if (result == IPC_ETIMEDOUT) return IPC_ETIMEDOUT;
        if (result != 0) return IPC_EAGAIN;
    }
}

int ipc_send(Process *sender, ipc_handle_t handle,
             const ipc_message_t *message) {
    return ipc_send_timeout(sender, handle, message, IPC_DEFAULT_TIMEOUT_MS);
}

static int receivable_offset(const ipc_endpoint_t *endpoint,
                             const Process *receiver) {
    for (uint32_t offset = 0; offset < endpoint->count; ++offset) {
        uint32_t index = (endpoint->head + offset) % IPC_QUEUE_DEPTH;
        if (endpoint->sender_pid[index] != receiver->pid ||
            endpoint->sender_generation[index] != receiver->generation) {
            return (int)offset;
        }
    }
    return -1;
}

static void remove_message_locked(ipc_endpoint_t *endpoint, uint32_t offset,
                                  ipc_message_t *message) {
    uint32_t index = (endpoint->head + offset) % IPC_QUEUE_DEPTH;
    *message = endpoint->messages[index];
    while (offset + 1U < endpoint->count) {
        uint32_t next = (index + 1U) % IPC_QUEUE_DEPTH;
        endpoint->messages[index] = endpoint->messages[next];
        endpoint->sender_pid[index] = endpoint->sender_pid[next];
        endpoint->sender_generation[index] =
            endpoint->sender_generation[next];
        index = next;
        ++offset;
    }
    --endpoint->count;
    if (endpoint->count == 0U) endpoint->head = 0U;
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
        int result = resolve_capability(receiver, handle, IPC_RIGHT_RECEIVE,
                                        &endpoint, NULL);
        if (result != 0) {
            ipc_unlock(flags);
            return result;
        }
        int offset = receivable_offset(endpoint, receiver);
        if (offset >= 0) {
            remove_message_locked(endpoint, (uint32_t)offset, message);
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
        result = wait_queue_block_until_locked(&endpoint->receive_waiters,
                                               TASK_BLOCK_WAITING, deadline);
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

int ipc_inherit(const Process *parent, Process *child) {
    if (parent == NULL || child == NULL || !parent->is_running ||
        !child->is_running) return IPC_EINVAL;
    uint32_t flags = ipc_lock();

    size_t needed = 0U;
    for (size_t index = 0;
         index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        const ipc_capability_t *source = &parent->ipc_capabilities[index];
        size_t endpoint_slot;
        uint32_t generation;
        ipc_capability_record_t *record = source->in_use
            ? capability_record_for(parent, source->handle) : NULL;
        if (record != NULL &&
            decode_handle(source->handle, &endpoint_slot, &generation) == 0 &&
            ipc_endpoints[endpoint_slot].active &&
            ipc_endpoints[endpoint_slot].generation == generation &&
            ipc_endpoints[endpoint_slot].peer_capabilities == 0U &&
            (record->rights & (IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE)) != 0U) {
            ++needed;
        }
    }
    size_t free_local = 0U;
    for (size_t index = 0;
         index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        if (!child->ipc_capabilities[index].in_use) ++free_local;
    }
    size_t free_global = 0U;
    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
        if (!ipc_capability_records[index].active) ++free_global;
    }
    if (needed > free_local || needed > free_global) {
        ipc_unlock(flags);
        return IPC_ENOSPC;
    }

    for (size_t index = 0;
         index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        const ipc_capability_t *source = &parent->ipc_capabilities[index];
        if (!source->in_use) continue;
        size_t endpoint_slot;
        uint32_t generation;
        ipc_capability_record_t *source_record =
            capability_record_for(parent, source->handle);
        if (source_record == NULL ||
            decode_handle(source->handle, &endpoint_slot, &generation) != 0 ||
            !ipc_endpoints[endpoint_slot].active ||
            ipc_endpoints[endpoint_slot].generation != generation) continue;

        /* v1 endpoints are deliberately point-to-point.  Reject a second
         * live peer instead of allowing one child to consume another child's
         * response from the same queue. */
        if (ipc_endpoints[endpoint_slot].peer_capabilities != 0U) continue;

        uint32_t peer_rights = source_record->rights &
            (IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE);
        if (peer_rights == 0U) continue;
        int result = install_capability_locked(
            child, source->handle, endpoint_slot, peer_rights);
        if (result != 0) {
            ipc_unlock(flags);
            return result;
        }
        ipc_endpoints[endpoint_slot].peer_seen = true;
        ++ipc_endpoints[endpoint_slot].peer_capabilities;
    }
    ipc_unlock(flags);
    return 0;
}

void ipc_process_cleanup(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return;
    uint32_t flags = ipc_lock();

    for (size_t endpoint_slot = 0;
         endpoint_slot < IPC_MAX_ENDPOINTS; ++endpoint_slot) {
        ipc_endpoint_t *endpoint = &ipc_endpoints[endpoint_slot];
        if (endpoint->active && endpoint->owner_pid == pid &&
            endpoint->owner_generation == generation) {
            revoke_endpoint_locked(endpoint_slot);
        }
    }

    for (size_t index = 0; index < IPC_MAX_CAPABILITY_RECORDS; ++index) {
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
