/**
 * @file test/test_ipc_host.c
 * @brief Hostseitiger Regressionstest für ipc.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
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
static uint64_t monotonic_ms;
static unsigned inherit_count;
static unsigned inheritance_clear_count;

uint64_t pit_monotonic_ms(void) { return monotonic_ms; }

void irq_context_enter(void) {}
void irq_context_exit(void) {}
int irq_in_context(void) { return 0; }
bool scheduler_preempt_is_disabled(void) { return false; }
bool scheduler_can_sleep(void) { return true; }
bool scheduler_set_wait_owner_locked(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return false;
    ++inherit_count;
    return true;
}
void scheduler_clear_wait_owner_locked(void) {
    ++inheritance_clear_count;
}

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

int wait_queue_block_until_locked(wait_queue_t *queue,
                                  task_block_kind_t kind,
                                  uint64_t deadline_ms) {
    (void)deadline_ms;
    return wait_queue_block_locked(queue, kind);
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

static ipc_bulk_message_t bulk_message(uint32_t length, uint8_t seed) {
    ipc_bulk_message_t result;
    memset(&result, 0xA5, sizeof(result));
    result.version = IPC_BULK_MESSAGE_VERSION;
    result.struct_size = sizeof(result);
    result.length = length;
    for (uint32_t index = 0U; index < length; ++index)
        result.payload[index] = (uint8_t)(seed + index);
    return result;
}

static void prepare_bulk_receive(ipc_bulk_message_t *message_out) {
    memset(message_out, 0, sizeof(*message_out));
    message_out->version = IPC_BULK_MESSAGE_VERSION;
    message_out->struct_size = sizeof(*message_out);
}

static int test_rights_fifo_and_bounds(void) {
    Process owner = process(10, 1U);
    Process child = process(11, 3U);
    Process stranger = process(12, 1U);
    ipc_handle_t handle = 0U;

    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0 && handle != 0U);
    CHECK(ipc_delegate(&owner, handle, &child,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);
    CHECK(ipc_close(&child, handle) < 0);
    CHECK(ipc_send(&stranger, handle, &(ipc_message_t){0}) < 0);

    /* Trusted ingress is routed as the active peer: the peer cannot consume
     * the injected message, while the endpoint owner can. */
    ipc_message_t ingress = message(0x4EU);
    CHECK(ipc_send_external_from_peer(owner.pid, owner.generation, handle,
                                      &ingress) == 0);
    ipc_message_t peer_view;
    prepare_receive(&peer_view);
    CHECK(ipc_receive_timeout(&child, handle, &peer_view, 0U) == -11);
    prepare_receive(&peer_view);
    CHECK(ipc_receive_timeout(&owner, handle, &peer_view, 0U) == 0);
    CHECK(peer_view.length == 1U && peer_view.payload[0] == 0x4EU);

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
    ipc_resource_stats_t stats;
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.version == IPC_RESOURCE_STATS_VERSION);
    CHECK(stats.struct_size == sizeof(stats));
    CHECK(stats.active_endpoints == 1U && stats.endpoint_high_water == 1U);
    CHECK(stats.active_capabilities == 2U &&
          stats.capability_high_water == 2U);
    CHECK(stats.queued_messages == IPC_QUEUE_DEPTH &&
          stats.message_high_water == IPC_QUEUE_DEPTH);
    unsigned blocks_before = block_count;
    unsigned inherits_before = inherit_count;
    unsigned clears_before = inheritance_clear_count;
    ipc_message_t excess = message(0xEEU);
    CHECK(ipc_send(&owner, handle, &excess) < 0);
    CHECK(block_count == blocks_before + 1U);
    CHECK(inherit_count == inherits_before + 1U);
    CHECK(inheritance_clear_count == clears_before + 1U);

    blocks_before = block_count;
    CHECK(ipc_send_timeout(&owner, handle, &excess, 0U) == -11);
    CHECK(block_count == blocks_before);
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.capacity_rejections != 0U);

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
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.queued_messages == 0U &&
          stats.message_high_water == IPC_QUEUE_DEPTH);
    blocks_before = block_count;
    inherits_before = inherit_count;
    clears_before = inheritance_clear_count;
    ipc_message_t empty;
    prepare_receive(&empty);
    CHECK(ipc_receive(&child, handle, &empty) < 0);
    CHECK(block_count == blocks_before + 1U);
    CHECK(inherit_count == inherits_before + 1U);
    CHECK(inheritance_clear_count == clears_before + 1U);
    blocks_before = block_count;
    CHECK(ipc_receive_timeout(&child, handle, &empty, 0U) == -11);
    CHECK(block_count == blocks_before);

    /* Bidirectional queues must not let the sender consume its own message. */
    ipc_message_t reply = message(0xA5U);
    CHECK(ipc_send(&child, handle, &reply) == 0);
    prepare_receive(&reply);
    CHECK(ipc_receive(&owner, handle, &reply) == 0);
    CHECK(reply.length == 1U && reply.payload[0] == 0xA5U);
    CHECK(wake_one_count >= IPC_QUEUE_DEPTH * 2U + 2U);
    CHECK(ipc_close(&owner, handle) == 0);
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.active_endpoints == 0U &&
          stats.active_capabilities == 0U && stats.queued_messages == 0U);
    return 0;
}

static int test_generation_quota_and_cleanup(void) {
    Process owner = process(20, 7U);
    Process child = process(21, 9U);
    ipc_handle_t stale = 0U;
    ipc_message_t value = message(1U);

    ipc_init();
    CHECK(ipc_create(&owner, &stale) == 0);
    CHECK(ipc_delegate(&owner, stale, &child,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);
    CHECK(ipc_release(&owner, stale) == -13);
    CHECK(ipc_release(&child, stale) == 0);
    CHECK(ipc_send(&child, stale, &value) == -9);
    CHECK(ipc_delegate(&owner, stale, &child,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);

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
    CHECK(ipc_delegate(&owner, cleanup_handle, &child,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);
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
        CHECK(ipc_delegate(&owner, cleanup_handle, &child,
                           IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);
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
    CHECK(ipc_delegate(&owner, handle, &child,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);
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
    ipc_resource_stats_t stats;
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.active_endpoints == IPC_MAX_ENDPOINTS);
    CHECK(stats.endpoint_high_water == IPC_MAX_ENDPOINTS);
    CHECK(stats.capacity_rejections != 0U);
    for (size_t index = 0; index < IPC_MAX_ENDPOINTS; ++index) {
        size_t owner = index / IPC_MAX_CAPABILITIES_PER_PROCESS;
        CHECK(ipc_close(&owners[owner], handles[index]) == 0);
    }
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.active_endpoints == 0U &&
          stats.active_capabilities == 0U && stats.queued_messages == 0U);
    CHECK(stats.endpoint_high_water == IPC_MAX_ENDPOINTS);
    return 0;
}

static int test_quiescent_owner_validation(void) {
    Process owner = process(35, 2U);
    Process peer = process(36, 4U);
    ipc_handle_t handle = 0U;
    ipc_message_t value = message(0x63U);
    ipc_message_t received;

    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_endpoint_validate_quiescent_owner(
              owner.pid, owner.generation, handle) == 0);
    CHECK(ipc_endpoint_validate_quiescent_owner(
              owner.pid, owner.generation + 1U, handle) == -9);
    CHECK(ipc_delegate(&owner, handle, &peer,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);
    CHECK(ipc_endpoint_validate_quiescent_owner(
              owner.pid, owner.generation, handle) == -16);
    CHECK(ipc_send(&peer, handle, &value) == 0);
    CHECK(ipc_release(&peer, handle) == 0);
    CHECK(ipc_endpoint_validate_quiescent_owner(
              owner.pid, owner.generation, handle) == -16);
    prepare_receive(&received);
    CHECK(ipc_receive(&owner, handle, &received) == 0);
    CHECK(ipc_endpoint_validate_quiescent_owner(
              owner.pid, owner.generation, handle) == 0);
    CHECK(ipc_close(&owner, handle) == 0);
    return 0;
}

static int test_kernel_to_owner_ingress_without_peer(void) {
    Process owner = process(40, 5U);
    ipc_handle_t handle = 0U;
    ipc_message_t ingress = message(0xD4U);
    ipc_message_t received;

    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_send_kernel_to_owner(owner.pid, owner.generation, handle,
                                   &ingress) == 0);
    prepare_receive(&received);
    CHECK(ipc_receive_timeout(&owner, handle, &received, 0U) == 0);
    CHECK(received.length == 1U && received.payload[0] == 0xD4U);
    CHECK(ipc_send_kernel_to_owner(owner.pid, owner.generation + 1U, handle,
                                   &ingress) == -9);
    CHECK(ipc_send_kernel_to_owner(owner.pid + 1, owner.generation, handle,
                                   &ingress) == -9);
    CHECK(ipc_close(&owner, handle) == 0);
    return 0;
}

static int test_bulk_rendezvous_and_compatibility(void) {
    Process owner = process(45, 2U);
    Process peer = process(46, 3U);
    ipc_handle_t handle = 0U;
    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_delegate(&owner, handle, &peer,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);

    ipc_bulk_message_t bulk = bulk_message(1500U, 0x31U);
    CHECK(ipc_send_bulk_timeout(&owner, handle, &bulk, 0U) == 0);
    ipc_message_t small_receive;
    prepare_receive(&small_receive);
    CHECK(ipc_receive_timeout(&peer, handle, &small_receive, 0U) == -11);
    CHECK(ipc_send_bulk_timeout(&owner, handle, &bulk, 0U) == -11);
    ipc_bulk_message_t received;
    prepare_bulk_receive(&received);
    CHECK(ipc_receive_bulk_timeout(&peer, handle, &received, 0U) == 0);
    CHECK(received.version == IPC_BULK_MESSAGE_VERSION &&
          received.struct_size == sizeof(received) &&
          received.length == 1500U);
    for (uint32_t index = 0U; index < received.length; ++index)
        CHECK(received.payload[index] == (uint8_t)(0x31U + index));
    for (uint32_t index = received.length;
         index < IPC_BULK_MAX_MESSAGE_SIZE; ++index)
        CHECK(received.payload[index] == 0U);

    ipc_message_t small = message(0x77U);
    CHECK(ipc_send(&owner, handle, &small) == 0);
    CHECK(ipc_send_bulk_timeout(&owner, handle, &bulk, 0U) == 0);
    prepare_bulk_receive(&received);
    CHECK(ipc_receive_bulk_timeout(&peer, handle, &received, 0U) == 0);
    CHECK(received.version == IPC_MESSAGE_VERSION &&
          received.struct_size == sizeof(ipc_message_t) &&
          received.length == 1U && received.payload[0] == 0x77U);
    prepare_bulk_receive(&received);
    CHECK(ipc_receive_bulk_timeout(&peer, handle, &received, 0U) == 0);
    CHECK(received.version == IPC_BULK_MESSAGE_VERSION &&
          received.length == 1500U);
    ipc_resource_stats_t stats;
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.queued_messages == 0U && stats.message_high_water == 2U);

    CHECK(ipc_send_bulk_timeout(&owner, handle, &bulk, 0U) == 0);
    CHECK(ipc_fault_inject(IPC_FAULT_BULK_PAYLOAD, 0U, 0U, 4U, 1U) == 0);
    prepare_bulk_receive(&received);
    CHECK(ipc_receive_bulk_timeout(&peer, handle, &received, 0U) ==
          IPC_EINTEGRITY);
    CHECK(ipc_resource_stats(&stats) == 0);
    CHECK(stats.active_endpoints == 0U && stats.queued_messages == 0U);
    return 0;
}

static int test_attenuating_delegation(void) {
    Process owner = process(25, 2U);
    Process peer = process(26, 4U);
    Process second_peer = process(27, 1U);
    ipc_handle_t handle = 0U;
    ipc_message_t value = message(0x31U);
    ipc_message_t received;

    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_delegate(&owner, handle, &peer, 0U) < 0);
    CHECK(ipc_delegate(&owner, handle, &peer, IPC_RIGHT_CONTROL) < 0);
    CHECK(ipc_delegate(&owner, handle, &peer, IPC_RIGHT_RECEIVE) == 0);
    CHECK(ipc_send(&peer, handle, &value) < 0);
    CHECK(ipc_delegate(&owner, handle, &second_peer, IPC_RIGHT_SEND) < 0);
    CHECK(ipc_send(&owner, handle, &value) == 0);
    prepare_receive(&received);
    CHECK(ipc_receive(&peer, handle, &received) == 0);
    CHECK(received.payload[0] == 0x31U);
    CHECK(ipc_close(&owner, handle) == 0);

    ipc_handle_t quota[IPC_MAX_CAPABILITIES_PER_PROCESS];
    for (size_t index = 0; index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        CHECK(ipc_create(&peer, &quota[index]) == 0);
    }
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_delegate(&owner, handle, &peer, IPC_RIGHT_SEND) < 0);
    CHECK(ipc_close(&owner, handle) == 0);
    for (size_t index = 0; index < IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        CHECK(ipc_close(&peer, quota[index]) == 0);
    }
    return 0;
}

static int test_integrity_fault_injection(void) {
    Process owner = process(200, 1U);
    Process child = process(201, 1U);
    ipc_handle_t handle = 0U;
    ipc_message_t sent = message(0x5AU);
    ipc_message_t received;

    ipc_init();
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_delegate(&owner, handle, &child,
                       IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) == 0);
    CHECK(ipc_fault_inject(IPC_FAULT_ENDPOINT, 0U, 0U, 4U, 1U) == 0);
    CHECK(ipc_send(&owner, handle, &sent) == 0);
    CHECK(ipc_integrity_correction_count() == 1U);

    CHECK(ipc_fault_inject(IPC_FAULT_MESSAGE, 0U, 0U, 4U, 3U) == 0);
    CHECK(ipc_fault_inject(IPC_FAULT_MESSAGE, 0U, 1U, 4U, 3U) == 0);
    prepare_receive(&received);
    unsigned wakes_before = wake_all_count;
    CHECK(ipc_receive(&child, handle, &received) == IPC_EINTEGRITY);
    CHECK(wake_all_count >= wakes_before + 2U);
    CHECK(received.length == 0U);

    ipc_init();
    owner = process(202, 1U);
    CHECK(ipc_create(&owner, &handle) == 0);
    CHECK(ipc_fault_inject(IPC_FAULT_CAPABILITY, 0U, 0U, 4U, 3U) == 0);
    CHECK(ipc_fault_inject(IPC_FAULT_CAPABILITY, 0U, 1U, 4U, 3U) == 0);
    wakes_before = wake_all_count;
    CHECK(ipc_send(&owner, handle, &sent) == IPC_EINTEGRITY);
    CHECK(wake_all_count >= wakes_before + 2U);
    return 0;
}

int main(void) {
    int result = test_rights_fifo_and_bounds();
    if (result != 0) return result;
    result = test_generation_quota_and_cleanup();
    if (result != 0) return result;
    result = test_attenuating_delegation();
    if (result != 0) return result;
    result = test_global_endpoint_quota();
    if (result != 0) return result;
    result = test_message_stress_without_resource_growth();
    if (result != 0) return result;
    result = test_quiescent_owner_validation();
    if (result != 0) return result;
    result = test_kernel_to_owner_ingress_without_peer();
    if (result != 0) return result;
    result = test_bulk_rendezvous_and_compatibility();
    if (result != 0) return result;
    return test_integrity_fault_injection();
}
