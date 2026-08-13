#include "include/kernel/ipc.h"
#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static unsigned block_count;
static unsigned wake_one_count;
static unsigned wake_all_count;

void irq_context_enter(void) {}
void irq_context_exit(void) {}
int irq_in_context(void) { return 0; }
bool scheduler_preempt_is_disabled(void) { return false; }
bool scheduler_can_sleep(void) { return true; }

void __attribute__((noreturn)) kassert_fail(const char *expression,
                                            const char *file, int line,
                                            const char *function) {
    (void)expression;
    (void)file;
    (void)line;
    (void)function;
    __builtin_trap();
    __builtin_unreachable();
}

/* IPC's host build retains the production wait-queue calls.  A failed block
 * models interruption/closure and makes full/empty paths finite in this
 * single-threaded harness. */
int wait_queue_block_locked(wait_queue_t *queue, task_block_kind_t kind) {
    (void)queue;
    (void)kind;
    ++block_count;
    return -1;
}

bool wait_queue_wake_one_locked(wait_queue_t *queue) {
    (void)queue;
    ++wake_one_count;
    return true;
}

size_t wait_queue_wake_all_locked(wait_queue_t *queue) {
    (void)queue;
    ++wake_all_count;
    return 1U;
}

static Process process(int pid, uint32_t generation) {
    Process result = {0};
    result.pid = pid;
    result.generation = generation;
    result.is_running = true;
    return result;
}

static ipc_message_t message(uint8_t tag) {
    ipc_message_t result;
    memset(&result, 0xA5, sizeof(result));
    result.version = IPC_MESSAGE_VERSION;
    result.struct_size = sizeof(result);
    result.length = 1U;
    result.payload[0] = tag;
    return result;
}

static void prepare_receive(ipc_message_t *message_out) {
    *message_out = (ipc_message_t){0};
    message_out->version = IPC_MESSAGE_VERSION;
    message_out->struct_size = sizeof(*message_out);
}

static int test_rights_fifo_and_bounds(void) {
    Process owner = process(10, 1U);
    Process child = process(11, 3U);
    Process stranger = process(12, 1U);
    ipc_handle_t handle = 0U;

    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0 && handle != 0U);
    CHECK(ipc_inherit(&owner, &child) == 0);
    CHECK(ipc_close(&child, handle) < 0);
    CHECK(ipc_send(&stranger, handle, &(ipc_message_t){0}) < 0);

    ipc_message_t invalid = message(0U);
    invalid.version = IPC_MESSAGE_VERSION + 1U;
    CHECK(ipc_send(&owner, handle, &invalid) < 0);
    invalid = message(0U);
    invalid.struct_size--;
    CHECK(ipc_send(&owner, handle, &invalid) < 0);
    invalid = message(0U);
    invalid.length = IPC_MAX_MESSAGE_SIZE + 1U;
    CHECK(ipc_send(&owner, handle, &invalid) < 0);

    for (uint32_t index = 0; index < IPC_QUEUE_DEPTH; ++index) {
        ipc_message_t queued = message((uint8_t)(index + 1U));
        CHECK(ipc_send(&owner, handle, &queued) == 0);
    }
    unsigned blocks_before = block_count;
    ipc_message_t excess = message(0xEEU);
    CHECK(ipc_send(&owner, handle, &excess) < 0);
    CHECK(block_count == blocks_before + 1U);

    for (uint32_t index = 0; index < IPC_QUEUE_DEPTH; ++index) {
        ipc_message_t received;
        prepare_receive(&received);
        CHECK(ipc_receive(&child, handle, &received) == 0);
        CHECK(received.length == 1U &&
              received.payload[0] == (uint8_t)(index + 1U));
        for (size_t tail = 1U; tail < IPC_MAX_MESSAGE_SIZE; ++tail) {
            CHECK(received.payload[tail] == 0U);
        }
    }
    blocks_before = block_count;
    ipc_message_t empty;
    prepare_receive(&empty);
    CHECK(ipc_receive(&child, handle, &empty) < 0);
    CHECK(block_count == blocks_before + 1U);

    /* Bidirectional queues must not let the sender consume its own message. */
    ipc_message_t reply = message(0xA5U);
    CHECK(ipc_send(&child, handle, &reply) == 0);
    prepare_receive(&reply);
    CHECK(ipc_receive(&owner, handle, &reply) == 0);
    CHECK(reply.length == 1U && reply.payload[0] == 0xA5U);
    CHECK(wake_one_count >= IPC_QUEUE_DEPTH * 2U + 2U);
    CHECK(ipc_close(&owner, handle) == 0);
    return 0;
}

static int test_generation_quota_and_cleanup(void) {
    Process owner = process(20, 7U);
    Process child = process(21, 9U);
    ipc_handle_t stale = 0U;
    ipc_message_t value = message(1U);

    ipc_init();
    CHECK(ipc_create(&owner, &stale) == 0);
    CHECK(ipc_inherit(&owner, &child) == 0);

    Process reused_identity = owner;
    ++reused_identity.generation;
    CHECK(ipc_send(&reused_identity, stale, &value) < 0);

    CHECK(ipc_close(&owner, stale) == 0);
    ipc_handle_t replacement = 0U;
    CHECK(ipc_create(&owner, &replacement) == 0);
    CHECK(replacement != stale);
    CHECK(ipc_send(&owner, stale, &value) < 0);
    CHECK(ipc_close(&owner, replacement) == 0);

    ipc_handle_t quota[IPC_MAX_CAPABILITIES_PER_PROCESS];
    for (size_t index = 0; index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        CHECK(ipc_create(&owner, &quota[index]) == 0);
    }
    ipc_handle_t excess = 0U;
    CHECK(ipc_create(&owner, &excess) < 0);
    for (size_t index = 0; index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        CHECK(ipc_close(&owner, quota[index]) == 0);
    }

    ipc_handle_t cleanup_handle = 0U;
    CHECK(ipc_create(&owner, &cleanup_handle) == 0);
    CHECK(ipc_inherit(&owner, &child) == 0);
    unsigned wakes_before = wake_all_count;
    ipc_process_cleanup(child.pid, child.generation);
    CHECK(wake_all_count > wakes_before);
    CHECK(ipc_send(&child, cleanup_handle, &value) < 0);
    CHECK(ipc_close(&owner, cleanup_handle) == 0);

    /* A stale cleanup request must not revoke a reused process identity. */
    CHECK(ipc_create(&owner, &cleanup_handle) == 0);
    ipc_process_cleanup(owner.pid, owner.generation - 1U);
    CHECK(ipc_close(&owner, cleanup_handle) == 0);

    ipc_handle_t previous = cleanup_handle;
    for (unsigned cycle = 0; cycle < 100U; ++cycle) {
        CHECK(ipc_create(&owner, &cleanup_handle) == 0);
        CHECK(cleanup_handle != previous);
        CHECK(ipc_send(&owner, previous, &value) < 0);
        previous = cleanup_handle;
        CHECK(ipc_close(&owner, cleanup_handle) == 0);
    }

    /* Owner revocation must reclaim the peer's local capability slots. */
    for (size_t cycle = 0; cycle < IPC_MAX_CAPABILITIES_PER_PROCESS; ++cycle) {
        CHECK(ipc_create(&owner, &cleanup_handle) == 0);
        CHECK(ipc_inherit(&owner, &child) == 0);
        CHECK(ipc_close(&owner, cleanup_handle) == 0);
    }
    for (size_t index = 0; index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        CHECK(ipc_create(&child, &quota[index]) == 0);
    }
    for (size_t index = 0; index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        CHECK(ipc_close(&child, quota[index]) == 0);
    }
    return 0;
}

static int test_message_stress_without_resource_growth(void) {
    Process owner = process(30, 1U);
    Process child = process(31, 1U);
    ipc_handle_t handle = 0U;
    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_inherit(&owner, &child) == 0);
    for (uint32_t sequence = 0; sequence < 10000U; ++sequence) {
        ipc_message_t sent = message((uint8_t)sequence);
        ipc_message_t received;
        CHECK(ipc_send(&owner, handle, &sent) == 0);
        prepare_receive(&received);
        CHECK(ipc_receive(&child, handle, &received) == 0);
        CHECK(received.length == 1U &&
              received.payload[0] == (uint8_t)sequence);
    }
    CHECK(ipc_close(&owner, handle) == 0);
    return 0;
}

static int test_global_endpoint_quota(void) {
    enum {
        HOST_OWNER_COUNT =
            (IPC_MAX_ENDPOINTS + IPC_MAX_CAPABILITIES_PER_PROCESS - 1U) /
            IPC_MAX_CAPABILITIES_PER_PROCESS + 1U
    };
    Process owners[HOST_OWNER_COUNT];
    ipc_handle_t handles[IPC_MAX_ENDPOINTS];
    ipc_init();
    for (size_t index = 0; index < HOST_OWNER_COUNT; ++index) {
        owners[index] = process(100 + (int)index, (uint32_t)index + 1U);
    }
    for (size_t index = 0; index < IPC_MAX_ENDPOINTS; ++index) {
        size_t owner = index / IPC_MAX_CAPABILITIES_PER_PROCESS;
        CHECK(ipc_create(&owners[owner], &handles[index]) == 0);
    }
    ipc_handle_t excess = 0U;
    CHECK(ipc_create(&owners[HOST_OWNER_COUNT - 1U], &excess) < 0);
    for (size_t index = 0; index < IPC_MAX_ENDPOINTS; ++index) {
        size_t owner = index / IPC_MAX_CAPABILITIES_PER_PROCESS;
        CHECK(ipc_close(&owners[owner], handles[index]) == 0);
    }
    return 0;
}

int main(void) {
    int result = test_rights_fifo_and_bounds();
    if (result != 0) return result;
    result = test_generation_quota_and_cleanup();
    if (result != 0) return result;
    result = test_global_endpoint_quota();
    if (result != 0) return result;
    return test_message_stress_without_resource_growth();
}
