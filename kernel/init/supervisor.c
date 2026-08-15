#include "include/kernel/supervisor.h"

#include "include/kernel/critical_object.h"
#ifndef REIST_HOST_TEST
#include "arch/x86/include/interrupt.h"
#include "include/kernel/panic.h"
#include "include/kernel/output_fence.h"
#include "include/kernel/storage_service.h"
#include "drivers/net/netstack.h"
#include "drivers/net/netdev.h"
#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"
#endif

typedef struct {
    uint32_t generation;
    uint32_t state;
    uint32_t epoch;
    uint64_t progress_marker;
    uint32_t restart_count;
    uint32_t heartbeat_timeout_ms;
    uint32_t recovery_timeout_ms;
    uint32_t restart_budget;
    uint64_t deadline_ms;
} supervisor_state_t;

typedef struct {
    uint32_t active;
    uint32_t generation;
    char name[SUPERVISOR_NAME_CAPACITY];
} supervisor_descriptor_t;

_Static_assert(sizeof(supervisor_probe_authority_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "probe authority exceeds critical object payload");
_Static_assert(sizeof(supervisor_network_probe_context_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "network context exceeds critical object payload");
_Static_assert(sizeof(supervisor_arp_reply_context_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "ARP reply context exceeds critical object payload");
_Static_assert(sizeof(supervisor_arp_resolution_context_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "ARP resolution context exceeds critical object payload");
_Static_assert(sizeof(supervisor_icmp_echo_context_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "ICMP echo context exceeds critical object payload");
_Static_assert(sizeof(supervisor_dhcp_context_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "DHCP context exceeds critical object payload");
_Static_assert(sizeof(supervisor_probe_control_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "probe control exceeds critical object payload");

typedef struct {
    critical_object_t protected_descriptor;
    critical_object_t protected_fence_ops;
    critical_object_t protected_state;
} supervisor_slot_t;

static supervisor_slot_t slots[SUPERVISOR_MAX_DOMAINS];
static uint32_t next_generation = 1U;
static uint64_t last_deadline_check_ms;
static uint32_t next_poll_slot;
static critical_object_t protected_network_degradation_stats;

#define SUPERVISOR_CHECK_INTERVAL_MS 10U
#define SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS 250U
#define SUPERVISOR_DHCP_COMMIT_TIMEOUT_MS 1000U

#ifndef REIST_HOST_TEST
typedef struct {
    supervisor_protected_probe_control_t control;
    supervisor_protected_probe_authority_t network_probe_authority;
    supervisor_protected_network_context_t network_probe_context;
    supervisor_protected_probe_authority_t arp_reply_authority;
    supervisor_protected_arp_reply_context_t arp_reply_context;
    supervisor_protected_probe_authority_t arp_resolution_authority;
    supervisor_protected_arp_resolution_context_t arp_resolution_context;
    supervisor_protected_probe_authority_t icmp_echo_authority;
    supervisor_protected_icmp_echo_context_t icmp_echo_context;
    supervisor_protected_probe_authority_t dhcp_authority;
    supervisor_protected_dhcp_context_t dhcp_context;
} supervisor_probe_runtime_t;

static supervisor_probe_runtime_t probe_runtime;
#endif

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

void supervisor_probe_authority_init(supervisor_probe_authority_t *authority) {
    if (authority == NULL) return;
    *authority = (supervisor_probe_authority_t){.next_id = 1U};
}

int supervisor_probe_authority_begin(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms, uint32_t timeout_ms,
                                     uint32_t *probe_id_out) {
    if (authority == NULL || probe_id_out == NULL || timeout_ms == 0U)
        return -22;
    if (authority->active_id != 0U) return -11;
    if (authority->next_id == 0U || authority->next_id > UINT32_MAX)
        return -75;
    uint32_t probe_id = (uint32_t)authority->next_id++;
    authority->active_id = probe_id;
    authority->deadline_ms = deadline_after(now_ms, timeout_ms);
    *probe_id_out = probe_id;
    return 0;
}

bool supervisor_probe_authority_take(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms,
                                     uint32_t *probe_id_out) {
    if (authority == NULL || probe_id_out == NULL ||
        authority->active_id == 0U) return false;
    if (now_ms >= authority->deadline_ms) {
        authority->active_id = 0U;
        return false;
    }
    *probe_id_out = authority->active_id;
    authority->active_id = 0U;
    return true;
}

bool supervisor_probe_authority_expire(supervisor_probe_authority_t *authority,
                                       uint64_t now_ms) {
    if (authority == NULL || authority->active_id == 0U ||
        now_ms < authority->deadline_ms) return false;
    authority->active_id = 0U;
    return true;
}

void supervisor_probe_authority_cancel(supervisor_probe_authority_t *authority) {
    if (authority != NULL) authority->active_id = 0U;
}

static bool probe_authority_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_probe_authority_t))
        return false;
    const supervisor_probe_authority_t *authority = payload;
    return authority->next_id >= 1U &&
           authority->next_id <= (uint64_t)UINT32_MAX + 1U &&
           (authority->active_id == 0U ||
            (authority->active_id < authority->next_id &&
             authority->transaction_epoch != 0U));
}

static int protected_probe_authority_read(
        supervisor_protected_probe_authority_t *protected_authority,
        supervisor_probe_authority_t *authority) {
    if (protected_authority == NULL || authority == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_authority->object, SUPERVISOR_PROBE_AUTHORITY_VERSION,
        authority, sizeof(*authority), &length, probe_authority_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static int protected_probe_authority_write(
        supervisor_protected_probe_authority_t *protected_authority,
        const supervisor_probe_authority_t *authority) {
    return critical_object_update(
        &protected_authority->object, SUPERVISOR_PROBE_AUTHORITY_VERSION,
        authority, sizeof(*authority), probe_authority_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_probe_authority_init(
        supervisor_protected_probe_authority_t *protected_authority) {
    if (protected_authority == NULL) return -22;
    supervisor_probe_authority_t authority;
    supervisor_probe_authority_init(&authority);
    uint32_t flags = supervisor_lock();
    int result = critical_object_init(
        &protected_authority->object, SUPERVISOR_PROBE_AUTHORITY_VERSION,
        &authority, sizeof(authority)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_begin(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms, uint32_t timeout_ms, uint32_t *probe_id_out) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result != 0) {
        supervisor_unlock(flags);
        return result;
    }
    result = supervisor_probe_authority_begin(&authority, now_ms, timeout_ms,
                                              probe_id_out);
    if (result == 0) authority.transaction_epoch = *probe_id_out;
    if (result == 0)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_begin_epoch(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms, uint32_t timeout_ms, uint32_t transaction_epoch,
        uint32_t *probe_id_out) {
    if (transaction_epoch == 0U) return -22;
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result == 0)
        result = supervisor_probe_authority_begin(&authority, now_ms,
                                                  timeout_ms, probe_id_out);
    if (result == 0) {
        /* Request IDs and service generations are independent monotonic
         * namespaces.  Bind the freshly allocated request to the caller's
         * generation; never require their numeric values to coincide. */
        authority.transaction_epoch = transaction_epoch;
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    }
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_take(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms, uint32_t *probe_id_out) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result == 0 &&
        !supervisor_probe_authority_take(&authority, now_ms, probe_id_out))
        result = -11;
    if (result == 0)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_take_epoch(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms, uint32_t transaction_epoch, uint32_t *probe_id_out) {
    if (transaction_epoch == 0U) return -22;
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result == 0 && authority.transaction_epoch != transaction_epoch)
        result = -13;
    if (result == 0 &&
        !supervisor_probe_authority_take(&authority, now_ms, probe_id_out))
        result = -11;
    if (result == 0)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_authority_expire(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    bool expired = result == 0 &&
        supervisor_probe_authority_expire(&authority, now_ms);
    if (expired)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result == 0 && expired ? 1 : result;
}

int supervisor_protected_probe_authority_expire_epoch(
        supervisor_protected_probe_authority_t *protected_authority,
        uint64_t now_ms, uint32_t transaction_epoch) {
    if (transaction_epoch == 0U) return 0;
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    /* An idle authority has no epoch ownership.  Comparing its cleared epoch
     * against a live process generation would spuriously isolate the service
     * before the first request. */
    if (result == 0 && authority.active_id == 0U) {
        supervisor_unlock(flags);
        return 0;
    }
    if (result == 0 && authority.transaction_epoch != transaction_epoch)
        result = -13;
    bool expired = result == 0 &&
        supervisor_probe_authority_expire(&authority, now_ms);
    if (expired)
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    supervisor_unlock(flags);
    return result == 0 && expired ? 1 : result;
}

int supervisor_protected_probe_authority_cancel(
        supervisor_protected_probe_authority_t *protected_authority) {
    supervisor_probe_authority_t authority;
    uint32_t flags = supervisor_lock();
    int result = protected_probe_authority_read(protected_authority, &authority);
    if (result == 0) {
        supervisor_probe_authority_cancel(&authority);
        authority.transaction_epoch = 0U;
        result = protected_probe_authority_write(protected_authority,
                                                 &authority);
    }
    supervisor_unlock(flags);
    return result;
}

static bool network_context_valid(const void *payload, size_t length) {
    if (payload == NULL ||
        length != sizeof(supervisor_network_probe_context_t)) return false;
    const supervisor_network_probe_context_t *context = payload;
    if (context->reserved[0] != 0U || context->reserved[1] != 0U) return false;
    if (context->candidate_reserved[0] != 0U ||
        context->candidate_reserved[1] != 0U) return false;
    bool empty_mac = true;
    bool empty_candidate_mac = true;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (context->local_mac[index] != 0U) empty_mac = false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (context->candidate_mac[index] != 0U) empty_candidate_mac = false;
    bool empty = context->gateway == 0U && context->local_ip == 0U && empty_mac;
    bool complete = context->gateway != 0U && context->local_ip != 0U &&
                    !empty_mac;
    bool candidate_empty = context->candidate_ip == 0U && empty_candidate_mac;
    bool candidate_complete = context->candidate_ip != 0U &&
                              !empty_candidate_mac &&
                              (context->candidate_mac[0] & 1U) == 0U;
    return (empty && context->delivered_id == 0U && candidate_empty &&
            context->transaction_epoch == 0U) ||
           (complete && context->transaction_epoch != 0U &&
            ((context->delivered_id == 0U && candidate_empty) ||
             (context->delivered_id != 0U && candidate_complete)));
}

static int network_context_read(
        supervisor_protected_network_context_t *protected_context,
        supervisor_network_probe_context_t *context) {
    if (protected_context == NULL || context == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_context->object, SUPERVISOR_NETWORK_CONTEXT_VERSION,
        context, sizeof(*context), &length, network_context_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static int network_context_write(
        supervisor_protected_network_context_t *protected_context,
        const supervisor_network_probe_context_t *context) {
    return critical_object_update(
        &protected_context->object, SUPERVISOR_NETWORK_CONTEXT_VERSION,
        context, sizeof(*context), network_context_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_network_context_init(
        supervisor_protected_network_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_network_probe_context_t context = {0};
    uint32_t flags = supervisor_lock();
    int result = critical_object_init(
        &protected_context->object, SUPERVISOR_NETWORK_CONTEXT_VERSION,
        &context, sizeof(context)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_prepare(
        supervisor_protected_network_context_t *protected_context,
        uint32_t gateway, uint32_t local_ip, const uint8_t local_mac[6]) {
    return supervisor_protected_network_context_prepare_epoch(
        protected_context, 1U, gateway, local_ip, local_mac);
}

int supervisor_protected_network_context_prepare_epoch(
        supervisor_protected_network_context_t *protected_context,
        uint32_t transaction_epoch, uint32_t gateway, uint32_t local_ip,
        const uint8_t local_mac[6]) {
    if (protected_context == NULL || gateway == 0U || local_ip == 0U ||
        local_mac == NULL || transaction_epoch == 0U) return -22;
    supervisor_network_probe_context_t context = {
        .transaction_epoch = transaction_epoch,
        .gateway = gateway, .local_ip = local_ip,
    };
    for (uint32_t index = 0U; index < 6U; ++index)
        context.local_mac[index] = local_mac[index];
    if (!network_context_valid(&context, sizeof(context))) return -22;
    uint32_t flags = supervisor_lock();
    int result = network_context_write(protected_context, &context);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_snapshot(
        supervisor_protected_network_context_t *protected_context,
        supervisor_network_probe_context_t *snapshot_out) {
    uint32_t flags = supervisor_lock();
    int result = network_context_read(protected_context, snapshot_out);
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_publish(
        supervisor_protected_network_context_t *protected_context,
        uint32_t probe_id) {
    supervisor_network_probe_context_t snapshot;
    int result = supervisor_protected_network_context_snapshot(
        protected_context, &snapshot);
    if (result != 0) return result;
    return supervisor_protected_network_context_publish_epoch(
        protected_context, snapshot.transaction_epoch, probe_id);
}

int supervisor_protected_network_context_publish_epoch(
        supervisor_protected_network_context_t *protected_context,
        uint32_t transaction_epoch, uint32_t probe_id) {
    supervisor_network_probe_context_t snapshot;
    int result = supervisor_protected_network_context_snapshot(
        protected_context, &snapshot);
    if (result != 0) return result;
    return supervisor_protected_network_context_publish_binding_epoch(
        protected_context, transaction_epoch, probe_id, snapshot.gateway,
        snapshot.local_mac);
}

int supervisor_protected_network_context_publish_binding_epoch(
        supervisor_protected_network_context_t *protected_context,
        uint32_t transaction_epoch, uint32_t probe_id, uint32_t candidate_ip,
        const uint8_t candidate_mac[6]) {
    if (probe_id == 0U || transaction_epoch == 0U || candidate_ip == 0U ||
        candidate_mac == NULL || (candidate_mac[0] & 1U) != 0U) return -22;
    supervisor_network_probe_context_t context;
    uint32_t flags = supervisor_lock();
    int result = network_context_read(protected_context, &context);
    if (result == 0 && context.transaction_epoch != transaction_epoch)
        result = -13;
    if (result == 0 && (context.gateway == 0U || context.delivered_id != 0U))
        result = -11;
    if (result == 0) {
        context.delivered_id = probe_id;
        context.candidate_ip = candidate_ip;
        for (uint32_t index = 0U; index < 6U; ++index)
            context.candidate_mac[index] = candidate_mac[index];
        result = network_context_write(protected_context, &context);
    }
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_consume(
        supervisor_protected_network_context_t *protected_context,
        uint32_t probe_id) {
    supervisor_network_probe_context_t snapshot;
    int result = supervisor_protected_network_context_snapshot(
        protected_context, &snapshot);
    if (result != 0) return result;
    return supervisor_protected_network_context_consume_epoch(
        protected_context, snapshot.transaction_epoch, probe_id);
}

int supervisor_protected_network_context_consume_epoch(
        supervisor_protected_network_context_t *protected_context,
        uint32_t transaction_epoch, uint32_t probe_id) {
    if (probe_id == 0U || transaction_epoch == 0U) return -22;
    supervisor_network_probe_context_t context;
    uint32_t flags = supervisor_lock();
    int result = network_context_read(protected_context, &context);
    if (result == 0 && context.transaction_epoch != transaction_epoch)
        result = -13;
    if (result == 0 && context.delivered_id != probe_id) result = -13;
    if (result == 0) {
        context.delivered_id = 0U;
        context.candidate_ip = 0U;
        for (uint32_t index = 0U; index < 6U; ++index)
            context.candidate_mac[index] = 0U;
        result = network_context_write(protected_context, &context);
    }
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_network_context_clear(
        supervisor_protected_network_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_network_probe_context_t context = {0};
    uint32_t flags = supervisor_lock();
    int result = network_context_write(protected_context, &context);
    supervisor_unlock(flags);
    return result;
}

static bool arp_reply_context_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_arp_reply_context_t))
        return false;
    const supervisor_arp_reply_context_t *context = payload;
    if (context->reserved[0] != 0U || context->reserved[1] != 0U)
        return false;
    bool empty_mac = true;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (context->target_mac[index] != 0U) empty_mac = false;
    bool empty = context->request_id == 0U &&
                 context->transaction_epoch == 0U &&
                 context->target_ip == 0U && empty_mac;
    bool complete = context->request_id != 0U &&
                    context->transaction_epoch != 0U &&
                    context->target_ip != 0U && !empty_mac &&
                    (context->target_mac[0] & 1U) == 0U;
    return empty || complete;
}

static int arp_reply_context_read(
        supervisor_protected_arp_reply_context_t *protected_context,
        supervisor_arp_reply_context_t *context) {
    if (protected_context == NULL || context == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_context->object, SUPERVISOR_ARP_REPLY_CONTEXT_VERSION,
        context, sizeof(*context), &length, arp_reply_context_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static int arp_reply_context_write(
        supervisor_protected_arp_reply_context_t *protected_context,
        const supervisor_arp_reply_context_t *context) {
    return critical_object_update(
        &protected_context->object, SUPERVISOR_ARP_REPLY_CONTEXT_VERSION,
        context, sizeof(*context), arp_reply_context_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_arp_reply_context_init(
        supervisor_protected_arp_reply_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_arp_reply_context_t context = {0};
    return critical_object_init(
        &protected_context->object, SUPERVISOR_ARP_REPLY_CONTEXT_VERSION,
        &context, sizeof(context)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_arp_reply_context_publish(
        supervisor_protected_arp_reply_context_t *protected_context,
        uint32_t request_id, uint32_t transaction_epoch, uint32_t target_ip,
        const uint8_t target_mac[6]) {
    if (protected_context == NULL || request_id == 0U ||
        transaction_epoch == 0U || target_ip == 0U || target_mac == NULL ||
        (target_mac[0] & 1U) != 0U) return -22;
    supervisor_arp_reply_context_t context = {
        .request_id = request_id,
        .transaction_epoch = transaction_epoch,
        .target_ip = target_ip,
    };
    for (uint32_t index = 0U; index < 6U; ++index)
        context.target_mac[index] = target_mac[index];
    if (!arp_reply_context_valid(&context, sizeof(context))) return -22;
    return arp_reply_context_write(protected_context, &context);
}

int supervisor_protected_arp_reply_context_snapshot(
        supervisor_protected_arp_reply_context_t *protected_context,
        supervisor_arp_reply_context_t *snapshot_out) {
    return arp_reply_context_read(protected_context, snapshot_out);
}

int supervisor_protected_arp_reply_context_clear(
        supervisor_protected_arp_reply_context_t *protected_context) {
    supervisor_arp_reply_context_t context = {0};
    return arp_reply_context_write(protected_context, &context);
}

static bool arp_resolution_context_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_arp_resolution_context_t))
        return false;
    const supervisor_arp_resolution_context_t *context = payload;
    bool empty = context->request_id == 0U &&
                 context->transaction_epoch == 0U &&
                 context->target_ip == 0U && context->reserved == 0U;
    bool complete = context->request_id != 0U &&
                    context->transaction_epoch != 0U &&
                    context->target_ip != 0U && context->reserved == 0U;
    return empty || complete;
}

static int arp_resolution_context_write(
        supervisor_protected_arp_resolution_context_t *protected_context,
        const supervisor_arp_resolution_context_t *context) {
    return critical_object_update(
        &protected_context->object, SUPERVISOR_ARP_RESOLUTION_CONTEXT_VERSION,
        context, sizeof(*context), arp_resolution_context_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_arp_resolution_context_init(
        supervisor_protected_arp_resolution_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_arp_resolution_context_t context = {0};
    return critical_object_init(
        &protected_context->object, SUPERVISOR_ARP_RESOLUTION_CONTEXT_VERSION,
        &context, sizeof(context)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_arp_resolution_context_publish(
        supervisor_protected_arp_resolution_context_t *protected_context,
        uint32_t request_id, uint32_t transaction_epoch, uint32_t target_ip) {
    if (protected_context == NULL || request_id == 0U ||
        transaction_epoch == 0U || target_ip == 0U) return -22;
    supervisor_arp_resolution_context_t context = {
        .request_id = request_id,
        .transaction_epoch = transaction_epoch,
        .target_ip = target_ip,
    };
    return arp_resolution_context_write(protected_context, &context);
}

int supervisor_protected_arp_resolution_context_snapshot(
        supervisor_protected_arp_resolution_context_t *protected_context,
        supervisor_arp_resolution_context_t *snapshot_out) {
    if (protected_context == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_context->object, SUPERVISOR_ARP_RESOLUTION_CONTEXT_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length,
        arp_resolution_context_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_arp_resolution_context_clear(
        supervisor_protected_arp_resolution_context_t *protected_context) {
    supervisor_arp_resolution_context_t context = {0};
    return arp_resolution_context_write(protected_context, &context);
}

static bool icmp_echo_context_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_icmp_echo_context_t))
        return false;
    const supervisor_icmp_echo_context_t *context = payload;
    if (context->reserved != 0U || context->reserved_tail[0] != 0U ||
        context->reserved_tail[1] != 0U ||
        context->data_length > SUPERVISOR_ICMP_ECHO_MAX_DATA) return false;
    bool empty_mac = true;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (context->source_mac[index] != 0U) empty_mac = false;
    bool empty = context->request_id == 0U &&
        context->transaction_epoch == 0U && context->source_ip == 0U &&
        context->identifier == 0U && context->sequence == 0U &&
        context->data_length == 0U && empty_mac;
    if (empty) {
        for (uint32_t index = 0U; index < SUPERVISOR_ICMP_ECHO_MAX_DATA;
             ++index)
            if (context->data[index] != 0U) return false;
        return true;
    }
    if (context->request_id == 0U || context->transaction_epoch == 0U ||
        context->source_ip == 0U || context->source_ip == 0xFFFFFFFFU ||
        empty_mac ||
        (context->source_mac[0] & 1U) != 0U) return false;
    for (uint32_t index = context->data_length;
         index < SUPERVISOR_ICMP_ECHO_MAX_DATA; ++index)
        if (context->data[index] != 0U) return false;
    return true;
}

static int icmp_echo_context_write(
        supervisor_protected_icmp_echo_context_t *protected_context,
        const supervisor_icmp_echo_context_t *context) {
    return critical_object_update(
        &protected_context->object, SUPERVISOR_ICMP_ECHO_CONTEXT_VERSION,
        context, sizeof(*context), icmp_echo_context_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_icmp_echo_context_init(
        supervisor_protected_icmp_echo_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_icmp_echo_context_t context = {0};
    return critical_object_init(
        &protected_context->object, SUPERVISOR_ICMP_ECHO_CONTEXT_VERSION,
        &context, sizeof(context)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_icmp_echo_context_publish(
        supervisor_protected_icmp_echo_context_t *protected_context,
        uint32_t request_id, uint32_t transaction_epoch, uint32_t source_ip,
        const uint8_t source_mac[6], uint16_t identifier, uint16_t sequence,
        const uint8_t *data, uint16_t data_length) {
    if (protected_context == NULL || request_id == 0U ||
        transaction_epoch == 0U || source_ip == 0U ||
        source_ip == 0xFFFFFFFFU || source_mac == NULL ||
        (source_mac[0] & 1U) != 0U ||
        data_length > SUPERVISOR_ICMP_ECHO_MAX_DATA ||
        (data_length != 0U && data == NULL)) return -22;
    supervisor_icmp_echo_context_t context = {
        .request_id = request_id,
        .transaction_epoch = transaction_epoch,
        .source_ip = source_ip,
        .identifier = identifier,
        .sequence = sequence,
        .data_length = data_length,
    };
    for (uint32_t index = 0U; index < 6U; ++index)
        context.source_mac[index] = source_mac[index];
    for (uint32_t index = 0U; index < data_length; ++index)
        context.data[index] = data[index];
    if (!icmp_echo_context_valid(&context, sizeof(context))) return -22;
    return icmp_echo_context_write(protected_context, &context);
}

int supervisor_protected_icmp_echo_context_snapshot(
        supervisor_protected_icmp_echo_context_t *protected_context,
        supervisor_icmp_echo_context_t *snapshot_out) {
    if (protected_context == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_context->object, SUPERVISOR_ICMP_ECHO_CONTEXT_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length,
        icmp_echo_context_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_icmp_echo_context_clear(
        supervisor_protected_icmp_echo_context_t *protected_context) {
    supervisor_icmp_echo_context_t context = {0};
    return icmp_echo_context_write(protected_context, &context);
}

static bool dhcp_config_valid_values(uint32_t ip_address, uint32_t netmask,
                                     uint32_t gateway, uint32_t dns_server) {
    if (ip_address == 0U || ip_address == 0xFFFFFFFFU || netmask == 0U ||
        netmask == 0xFFFFFFFFU || gateway == 0xFFFFFFFFU ||
        dns_server == 0xFFFFFFFFU) return false;
    uint32_t host_mask = ~netmask;
    if ((host_mask & (host_mask + 1U)) != 0U) return false;
    uint32_t host = ip_address & host_mask;
    if (host == 0U || host == host_mask) return false;
    if (gateway != 0U) {
        uint32_t gateway_host = gateway & host_mask;
        if ((gateway & netmask) != (ip_address & netmask) ||
            gateway_host == 0U || gateway_host == host_mask) return false;
    }
    return true;
}

static bool dhcp_context_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_dhcp_context_t))
        return false;
    const supervisor_dhcp_context_t *context = payload;
    if (context->reserved[0] != 0U || context->reserved[1] != 0U)
        return false;
    bool empty = context->request_id == 0U &&
        context->transaction_epoch == 0U && context->ip_address == 0U &&
        context->netmask == 0U && context->gateway == 0U &&
        context->dns_server == 0U;
    if (empty) return true;
    return context->request_id != 0U && context->transaction_epoch != 0U &&
        dhcp_config_valid_values(context->ip_address, context->netmask,
                                 context->gateway, context->dns_server);
}

static int dhcp_context_write(
        supervisor_protected_dhcp_context_t *protected_context,
        const supervisor_dhcp_context_t *context) {
    return critical_object_update(
        &protected_context->object, SUPERVISOR_DHCP_CONTEXT_VERSION,
        context, sizeof(*context), dhcp_context_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_context_init(
        supervisor_protected_dhcp_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_dhcp_context_t context = {0};
    return critical_object_init(
        &protected_context->object, SUPERVISOR_DHCP_CONTEXT_VERSION,
        &context, sizeof(context)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_context_publish(
        supervisor_protected_dhcp_context_t *protected_context,
        uint32_t request_id, uint32_t transaction_epoch, uint32_t ip_address,
        uint32_t netmask, uint32_t gateway, uint32_t dns_server) {
    supervisor_dhcp_context_t context = {
        .request_id = request_id,
        .transaction_epoch = transaction_epoch,
        .ip_address = ip_address,
        .netmask = netmask,
        .gateway = gateway,
        .dns_server = dns_server,
    };
    if (protected_context == NULL || !dhcp_context_valid(&context,
                                                         sizeof(context)))
        return -22;
    return dhcp_context_write(protected_context, &context);
}

int supervisor_protected_dhcp_context_snapshot(
        supervisor_protected_dhcp_context_t *protected_context,
        supervisor_dhcp_context_t *snapshot_out) {
    if (protected_context == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_context->object, SUPERVISOR_DHCP_CONTEXT_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length, dhcp_context_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_dhcp_context_clear(
        supervisor_protected_dhcp_context_t *protected_context) {
    supervisor_dhcp_context_t context = {0};
    return dhcp_context_write(protected_context, &context);
}

static bool probe_control_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_probe_control_t))
        return false;
    const supervisor_probe_control_t *control = payload;
    if (control->active > 1U || control->fenced > 1U ||
        control->healthy > 1U ||
        (control->fenced != 0U && control->healthy != 0U)) return false;
    if (control->active == 0U) {
        const supervisor_probe_control_t empty = {0};
        const uint8_t *actual = (const uint8_t *)control;
        const uint8_t *expected = (const uint8_t *)&empty;
        for (size_t index = 0U; index < sizeof(*control); ++index)
            if (actual[index] != expected[index]) return false;
        return true;
    }
    if (control->handle.slot >= SUPERVISOR_MAX_DOMAINS ||
        control->handle.generation == 0U || control->handle.epoch == 0U ||
        control->pid < 0) return false;
    if (control->pid == 0)
        return control->process_generation == 0U &&
               control->launch_count == 0U && control->healthy == 0U &&
               control->endpoint_handle == 0U;
    return control->process_generation != 0U && control->launch_count != 0U;
}

int supervisor_protected_probe_control_init(
        supervisor_protected_probe_control_t *protected_control) {
    if (protected_control == NULL) return -22;
    supervisor_probe_control_t control = {0};
    uint32_t flags = supervisor_lock();
    int result = critical_object_init(
        &protected_control->object, SUPERVISOR_PROBE_CONTROL_VERSION,
        &control, sizeof(control)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    return result;
}

int supervisor_protected_probe_control_read(
        supervisor_protected_probe_control_t *protected_control,
        supervisor_probe_control_t *snapshot_out) {
    if (protected_control == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    uint32_t flags = supervisor_lock();
    critical_read_result_t read_result = critical_object_read(
        &protected_control->object, SUPERVISOR_PROBE_CONTROL_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length, probe_control_valid);
    supervisor_unlock(flags);
    return read_result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_probe_control_write(
        supervisor_protected_probe_control_t *protected_control,
        const supervisor_probe_control_t *snapshot) {
    if (protected_control == NULL || snapshot == NULL ||
        !probe_control_valid(snapshot, sizeof(*snapshot))) return -22;
    uint32_t flags = supervisor_lock();
    int result = critical_object_update(
        &protected_control->object, SUPERVISOR_PROBE_CONTROL_VERSION,
        snapshot, sizeof(*snapshot), probe_control_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    return result;
}

void supervisor_network_degradation_init(
        supervisor_network_degradation_stats_t *stats) {
    if (stats != NULL) *stats = (supervisor_network_degradation_stats_t){0};
}

static void saturating_increment(uint32_t *value) {
    if (*value != UINT32_MAX) ++*value;
}

void supervisor_network_degradation_record(
        supervisor_network_degradation_stats_t *stats,
        supervisor_network_degradation_reason_t reason) {
    if (stats == NULL) return;
    if (reason == SUPERVISOR_NETWORK_DEGRADED_EXPIRED)
        saturating_increment(&stats->expired);
    else if (reason == SUPERVISOR_NETWORK_DEGRADED_QUEUE)
        saturating_increment(&stats->queue_fallback);
    else if (reason == SUPERVISOR_NETWORK_DEGRADED_SEMANTIC)
        saturating_increment(&stats->semantic_reject);
}

static bool network_degradation_valid(const void *payload, size_t length) {
    return payload != NULL &&
           length == sizeof(supervisor_network_degradation_stats_t);
}

static int network_degradation_read(
        supervisor_network_degradation_stats_t *stats) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_network_degradation_stats,
        SUPERVISOR_NETWORK_DEGRADATION_VERSION, stats, sizeof(*stats),
        &length, network_degradation_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static void network_degradation_record(
        supervisor_network_degradation_reason_t reason) {
    uint32_t flags = supervisor_lock();
    supervisor_network_degradation_stats_t stats;
    if (network_degradation_read(&stats) == 0) {
        supervisor_network_degradation_record(&stats, reason);
        (void)critical_object_update(
            &protected_network_degradation_stats,
            SUPERVISOR_NETWORK_DEGRADATION_VERSION, &stats, sizeof(stats),
            network_degradation_valid);
    }
    supervisor_unlock(flags);
}

int supervisor_network_degradation_snapshot(
        supervisor_network_degradation_stats_t *stats_out) {
    if (stats_out == NULL) return -22;
    uint32_t flags = supervisor_lock();
    int result = network_degradation_read(stats_out);
    supervisor_unlock(flags);
    return result;
}

static bool state_valid(const void *payload, size_t length) {
    if (length != sizeof(supervisor_state_t)) return false;
    const supervisor_state_t *state = (const supervisor_state_t *)payload;
    return state->generation != 0 && state->state >= SUPERVISOR_STARTING &&
           state->state <= SUPERVISOR_IDLE &&
           state->heartbeat_timeout_ms != 0 &&
           state->recovery_timeout_ms != 0 &&
           state->restart_count <= state->restart_budget;
}

static bool fence_ops_valid(const void *payload, size_t length) {
    if (length != sizeof(supervisor_fence_ops_t)) return false;
    const supervisor_fence_ops_t *ops =
        (const supervisor_fence_ops_t *)payload;
    return ops->apply != 0 && ops->verify != 0;
}

static bool descriptor_valid(const void *payload, size_t length) {
    if (length != sizeof(supervisor_descriptor_t)) return false;
    const supervisor_descriptor_t *descriptor =
        (const supervisor_descriptor_t *)payload;
    if (descriptor->active > 1U) return false;
    if (descriptor->active == 0U)
        return descriptor->generation == 0U && descriptor->name[0] == '\0';
    return descriptor->generation != 0U && descriptor->name[0] != '\0' &&
           descriptor->name[SUPERVISOR_NAME_CAPACITY - 1U] == '\0';
}

static int descriptor_read(uint32_t slot, supervisor_descriptor_t *descriptor) {
    size_t length = 0;
    critical_read_result_t result = critical_object_read(
        &slots[slot].protected_descriptor, SUPERVISOR_DESCRIPTOR_VERSION,
        descriptor, sizeof(*descriptor), &length, descriptor_valid);
    return result < 0 ? -1 : 0;
}

static int fence_ops_read(uint32_t slot, supervisor_fence_ops_t *ops) {
    size_t length = 0;
    critical_read_result_t result = critical_object_read(
        &slots[slot].protected_fence_ops, SUPERVISOR_FENCE_OPS_VERSION, ops,
        sizeof(*ops), &length, fence_ops_valid);
    return result < 0 ? -1 : 0;
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
    supervisor_descriptor_t descriptor;
    if (handle.slot >= SUPERVISOR_MAX_DOMAINS ||
        descriptor_read(handle.slot, &descriptor) != 0 ||
        descriptor.active == 0U || descriptor.generation != handle.generation ||
        state_read(handle.slot, state) != 0 ||
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
    supervisor_descriptor_t empty = {0};
    for (uint32_t slot = 0; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        if (critical_object_init(&slots[slot].protected_descriptor,
                                 SUPERVISOR_DESCRIPTOR_VERSION, &empty,
                                 sizeof(empty)) != 0) {
#ifndef REIST_HOST_TEST
            panic("Unable to initialize supervisor descriptor");
#endif
        }
    }
    next_generation = 1U;
    last_deadline_check_ms = 0;
    next_poll_slot = 0;
    supervisor_network_degradation_stats_t network_stats;
    supervisor_network_degradation_init(&network_stats);
    if (critical_object_init(&protected_network_degradation_stats,
                             SUPERVISOR_NETWORK_DEGRADATION_VERSION,
                             &network_stats, sizeof(network_stats)) != 0) {
#ifndef REIST_HOST_TEST
        panic("Unable to initialize supervisor network statistics");
#endif
    }
#ifndef REIST_HOST_TEST
    if (supervisor_protected_probe_control_init(&probe_runtime.control) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.network_probe_authority) != 0 ||
        supervisor_protected_network_context_init(
            &probe_runtime.network_probe_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.arp_reply_authority) != 0 ||
        supervisor_protected_arp_reply_context_init(
            &probe_runtime.arp_reply_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.arp_resolution_authority) != 0 ||
        supervisor_protected_arp_resolution_context_init(
            &probe_runtime.arp_resolution_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.icmp_echo_authority) != 0 ||
        supervisor_protected_icmp_echo_context_init(
            &probe_runtime.icmp_echo_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.dhcp_authority) != 0 ||
        supervisor_protected_dhcp_context_init(
            &probe_runtime.dhcp_context) != 0) {
        panic("Unable to initialize protected probe runtime");
    }
#endif
    supervisor_unlock(flags);
}

static void check_deadlines_locked(uint64_t now_ms) {
    for (uint32_t slot = 0; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        supervisor_descriptor_t descriptor;
        if (descriptor_read(slot, &descriptor) != 0 || descriptor.active == 0U)
            continue;
        supervisor_state_t state;
        if (state_read(slot, &state) != 0) continue;
        if ((state.state == SUPERVISOR_STARTING ||
             state.state == SUPERVISOR_HEALTHY ||
             state.state == SUPERVISOR_DEGRADED ||
             state.state == SUPERVISOR_RECOVERING) &&
            now_ms >= state.deadline_ms) {
            state.state = SUPERVISOR_ISOLATED;
            (void)state_write(slot, &state);
        }
    }
}

void supervisor_clock_tick(uint64_t now_ms) {
    if (now_ms - last_deadline_check_ms < SUPERVISOR_CHECK_INTERVAL_MS) return;
    uint32_t flags = supervisor_lock();
    if (now_ms - last_deadline_check_ms >= SUPERVISOR_CHECK_INTERVAL_MS) {
        last_deadline_check_ms = now_ms;
        check_deadlines_locked(now_ms);
    }
    supervisor_unlock(flags);
}

int supervisor_register(const char *name, const supervisor_config_t *config,
                        const supervisor_fence_ops_t *fence_ops,
                        uint64_t now_ms, supervisor_handle_t *handle_out) {
    if (name == 0 || name[0] == '\0' || config == 0 || fence_ops == 0 ||
        fence_ops->apply == 0 || fence_ops->verify == 0 || handle_out == 0 ||
        config->heartbeat_timeout_ms == 0 || config->recovery_timeout_ms == 0)
        return -1;
    uint32_t flags = supervisor_lock();
    uint32_t slot = 0;
    for (; slot < SUPERVISOR_MAX_DOMAINS; ++slot) {
        supervisor_descriptor_t descriptor;
        if (descriptor_read(slot, &descriptor) != 0) {
            supervisor_unlock(flags);
            return -1;
        }
        if (descriptor.active == 0U) break;
    }
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
    supervisor_descriptor_t descriptor = {
        .active = 1U, .generation = generation, .name = {0},
    };
    uint32_t index = 0;
    while (index + 1U < SUPERVISOR_NAME_CAPACITY && name[index] != '\0') {
        descriptor.name[index] = name[index];
        ++index;
    }
    descriptor.name[index] = '\0';
    if (critical_object_init(&slots[slot].protected_fence_ops,
                             SUPERVISOR_FENCE_OPS_VERSION, fence_ops,
                             sizeof(*fence_ops)) != 0) {
        supervisor_unlock(flags);
        return -1;
    }
    if (critical_object_update(&slots[slot].protected_descriptor,
                               SUPERVISOR_DESCRIPTOR_VERSION, &descriptor,
                               sizeof(descriptor), descriptor_valid) != 0) {
        supervisor_unlock(flags);
        return -1;
    }
    handle_out->slot = slot;
    handle_out->generation = generation;
    handle_out->epoch = state.epoch;
    supervisor_unlock(flags);
    return 0;
}

int supervisor_report_progress(supervisor_handle_t handle,
                               uint64_t progress_marker, uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 ||
        (state.state != SUPERVISOR_STARTING && state.state != SUPERVISOR_HEALTHY &&
         state.state != SUPERVISOR_DEGRADED && state.state != SUPERVISOR_IDLE) ||
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

int supervisor_report_idle(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_HEALTHY) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = SUPERVISOR_IDLE;
    state.deadline_ms = UINT64_MAX;
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

#ifndef REIST_HOST_TEST
static int supervisor_force_isolate(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state == SUPERVISOR_SAFE_STATE) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = SUPERVISOR_ISOLATED;
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}
#endif

supervisor_event_t supervisor_poll(uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_event_t none = {.type = SUPERVISOR_EVENT_NONE};
    check_deadlines_locked(now_ms);
    for (uint32_t offset = 0; offset < SUPERVISOR_MAX_DOMAINS; ++offset) {
        uint32_t slot = (next_poll_slot + offset) % SUPERVISOR_MAX_DOMAINS;
        supervisor_descriptor_t descriptor;
        if (descriptor_read(slot, &descriptor) != 0) {
            supervisor_event_t result = {
                .type = SUPERVISOR_EVENT_SAFE_STATE_REQUIRED,
                .handle = {.slot = slot, .generation = 0, .epoch = 0},
            };
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (descriptor.active == 0U) continue;
        supervisor_state_t state;
        if (state_read(slot, &state) != 0) {
            supervisor_event_t result = {
                .type = SUPERVISOR_EVENT_SAFE_STATE_REQUIRED,
                .handle = {.slot = slot, .generation = 0, .epoch = 0},
            };
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (state.state == SUPERVISOR_ISOLATED) {
            supervisor_event_t result = event(SUPERVISOR_EVENT_FENCE_REQUIRED,
                                               slot, &state);
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (state.state == SUPERVISOR_RECOVERING) {
            supervisor_event_t result = event(
                SUPERVISOR_EVENT_RESTART_REQUIRED, slot, &state);
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
        if (state.state == SUPERVISOR_SAFE_STATE) {
            supervisor_event_t result = event(
                SUPERVISOR_EVENT_SAFE_STATE_REQUIRED, slot, &state);
            next_poll_slot = (slot + 1U) % SUPERVISOR_MAX_DOMAINS;
            supervisor_unlock(flags);
            return result;
        }
    }
    supervisor_unlock(flags);
    return none;
}

#ifndef REIST_HOST_TEST
static bool probe_fence_apply(void *context) {
    supervisor_probe_runtime_t *runtime = context;
    supervisor_probe_control_t control;
    if (runtime == NULL ||
        supervisor_protected_probe_control_read(
            &runtime->control, &control) != 0 || control.active == 0U) {
        output_fence_all();
        return false;
    }
    int revoked = netstack_revoke_arp_bindings(
        control.pid, control.process_generation);
    if (revoked < 0) {
        output_fence_all();
        return false;
    }
    if (revoked > 0)
        printf("REIST_NETWORK ARP_BINDINGS_REVOKED\n");
    control.fenced = 1U;
    control.healthy = 0U;
    control.network_epoch = 0U;
    if (supervisor_protected_probe_control_write(
            &runtime->control, &control) != 0) {
        output_fence_all();
        return false;
    }
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->network_probe_authority);
    (void)supervisor_protected_network_context_clear(
        &runtime->network_probe_context);
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->arp_reply_authority);
    (void)supervisor_protected_arp_reply_context_clear(
        &runtime->arp_reply_context);
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->arp_resolution_authority);
    (void)supervisor_protected_arp_resolution_context_clear(
        &runtime->arp_resolution_context);
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->icmp_echo_authority);
    (void)supervisor_protected_icmp_echo_context_clear(
        &runtime->icmp_echo_context);
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->dhcp_authority);
    (void)supervisor_protected_dhcp_context_clear(&runtime->dhcp_context);
    if (process_identity_alive(control.pid, control.process_generation)) {
        (void)process_terminate(control.pid);
    }
    return true;
}

static void probe_report_recovery_pair(uint32_t launch_count) {
    /* Publish evidence only after the replacement passed self-test. Keep the
     * bounded pair together in the serial record. */
    scheduler_preempt_disable();
    if (launch_count == 2U)
        printf("\nREIST_PROBE CRASH_DETECTED\n"
               "REIST_PROBE CRASH_RECOVERED\n");
    else if (launch_count == 3U)
        printf("\nREIST_PROBE HANG_DETECTED\n"
               "REIST_PROBE HANG_RECOVERED\n");
    else if (launch_count == 4U)
        printf("\nREIST_PROBE INVALID_REPLY_DETECTED\n"
               "REIST_PROBE INVALID_RECOVERED\n"
               "REIST_PROBE REINTEGRATED\n"
               "REIST_PROBE RECOVERY_SEQUENCE_OK\n");
    else if (launch_count >= 5U)
        printf("\nREIST_NETWORK SERVICE_CRASH_RECOVERED\n");
    scheduler_preempt_enable();
}

static bool probe_fence_verify(void *context) {
    supervisor_probe_runtime_t *runtime = context;
    supervisor_probe_control_t control;
    return runtime != NULL &&
        supervisor_protected_probe_control_read(
            &runtime->control, &control) == 0 && control.fenced != 0U &&
        !process_identity_alive(control.pid, control.process_generation);
}

static bool probe_spawn_next(void) {
    static const char *const modes[] = {"crash", "hang", "invalid", "healthy"};
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U)
        return false;
    uint32_t mode_index = control.launch_count;
    if (mode_index >= sizeof(modes) / sizeof(modes[0])) mode_index = 3U;
    const char *arguments[] = {"REIST.PRG", modes[mode_index]};
    int pid = supervisor_spawn_service("/REIST.PRG", 2, arguments,
                                       PROCESS_DOMAIN_PROBE);
    uint32_t generation = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) return false;
    control.pid = pid;
    control.process_generation = generation;
    control.healthy = 0U;
    ++control.launch_count;
    if (supervisor_protected_probe_control_write(
            &probe_runtime.control, &control) != 0) {
        (void)process_terminate(pid);
        return false;
    }
    return true;
}

bool supervisor_start_probe(uint64_t now_ms) {
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active != 0U)
        return false;
    if (supervisor_protected_probe_authority_init(
            &probe_runtime.network_probe_authority) != 0 ||
        supervisor_protected_network_context_init(
            &probe_runtime.network_probe_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.arp_reply_authority) != 0 ||
        supervisor_protected_arp_reply_context_init(
            &probe_runtime.arp_reply_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.arp_resolution_authority) != 0 ||
        supervisor_protected_arp_resolution_context_init(
            &probe_runtime.arp_resolution_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.icmp_echo_authority) != 0 ||
        supervisor_protected_icmp_echo_context_init(
            &probe_runtime.icmp_echo_context) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.dhcp_authority) != 0 ||
        supervisor_protected_dhcp_context_init(
            &probe_runtime.dhcp_context) != 0)
        return false;
    supervisor_config_t config = {
        .heartbeat_timeout_ms = 2000U,
        .recovery_timeout_ms = 1000U,
        .restart_budget = 4U,
    };
    supervisor_fence_ops_t fence = {
        .apply = probe_fence_apply,
        .verify = probe_fence_verify,
        .context = &probe_runtime,
    };
    supervisor_handle_t handle;
    if (supervisor_register("ring3-probe", &config, &fence, now_ms,
                            &handle) != 0) return false;
    control = (supervisor_probe_control_t){
        .active = 1U,
        .handle = handle,
    };
    if (supervisor_protected_probe_control_write(
            &probe_runtime.control, &control) != 0) return false;
    if (!probe_spawn_next()) {
        (void)supervisor_force_isolate(control.handle);
        return false;
    }
    return true;
}

bool supervisor_probe_ready(void) {
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool ready = result == 0 && control.active != 0U &&
        control.fenced == 0U && control.healthy != 0U &&
        control.launch_count >= 4U &&
        control.endpoint_handle != IPC_INVALID_HANDLE &&
        process_identity_alive(control.pid, control.process_generation);
    supervisor_unlock(flags);
    return ready;
}

int supervisor_probe_report(int pid, uint32_t generation,
                            uint32_t report_type, uint32_t value,
                            uint64_t now_ms) {
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U ||
        pid != control.pid || generation != control.process_generation ||
        !process_identity_alive(pid, generation)) return -1;
    if (report_type == REIST_REPORT_SELF_TEST) {
        if (value == 0U ||
            (control.endpoint_handle != 0U &&
             value == control.endpoint_handle)) {
            (void)supervisor_force_isolate(control.handle);
            return -1;
        }
        control.endpoint_handle = value;
        if (supervisor_protected_probe_control_write(
                &probe_runtime.control, &control) != 0) return -1;
        return supervisor_report_self_test(control.handle, true, now_ms);
    }
    if (report_type == REIST_REPORT_PROGRESS) {
        int result = supervisor_report_progress(control.handle, value, now_ms);
        if (result == 0 && control.fenced != 0U) {
            control.fenced = 0U;
            control.healthy = 1U;
            if (supervisor_protected_probe_control_write(
                    &probe_runtime.control, &control) != 0) return -1;
            probe_report_recovery_pair(control.launch_count);
        } else if (result == 0 && control.healthy == 0U) {
            control.healthy = 1U;
            if (supervisor_protected_probe_control_write(
                    &probe_runtime.control, &control) != 0) return -1;
        }
        return result;
    }
    if (report_type == REIST_REPORT_INVALID) {
        (void)supervisor_force_isolate(control.handle);
        return -1;
    }
    if (report_type == REIST_REPORT_NETWORK_HEADER) {
        if (value != 0x0800U && value != 0x0806U) return -1;
        printf(value == 0x0806U ? "REIST_NETWORK RX_HEADER_ARP\n"
                                : "REIST_NETWORK RX_HEADER_IPV4\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_PROBE_ID) {
        uint32_t transaction_flags = supervisor_lock();
        int control_result = supervisor_protected_probe_control_read(
            &probe_runtime.control, &control);
        int consume_result = control_result == 0 && control.active != 0U &&
                pid == control.pid &&
                generation == control.process_generation
            ? supervisor_protected_network_context_consume_epoch(
                  &probe_runtime.network_probe_context, control.network_epoch,
                  value)
            : -1;
        supervisor_unlock(transaction_flags);
        if (consume_result != 0) return -1;
        printf("REIST_NETWORK PROBE_ID_OK\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_DEGRADED) {
        if (value != SUPERVISOR_NETWORK_DEGRADED_SEMANTIC) return -1;
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_SEMANTIC);
        return 0;
    }
    return -1;
}

int supervisor_service_connect(Process *client, uint32_t service_id,
                               uint32_t *handle_out) {
    supervisor_probe_control_t control;
    if (client == NULL || handle_out == NULL ||
        service_id != REIST_SERVICE_DIAGNOSTIC ||
        supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U ||
        control.fenced != 0U || control.healthy == 0U ||
        control.launch_count < 4U ||
        control.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(control.pid, control.process_generation)) {
        return -11;
    }
    int result = process_ipc_delegate_identity(
        control.pid, control.process_generation, control.endpoint_handle, client,
        IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE);
    if (result == 0) *handle_out = control.endpoint_handle;
    return result;
}

bool supervisor_network_submit_header(const uint8_t *frame, uint16_t length) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    if (frame == NULL || length < 42U || frame[12] != 0x08U ||
        frame[13] != 0x06U) return false;
    uint32_t local_ip = 0U;
    uint8_t local_mac[6] = {0};
    bool local_identity = netstack_get_local_identity(&local_ip, local_mac);
    bool arp_request = frame[20] == 0U && frame[21] == 1U;
    uint32_t target_ip = ((uint32_t)frame[38] << 24U) |
                         ((uint32_t)frame[39] << 16U) |
                         ((uint32_t)frame[40] << 8U) | frame[41];
    bool local_request = local_identity && arp_request && target_ip == local_ip;

    uint32_t transaction_flags = supervisor_lock();
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U ||
        control.fenced != 0U || control.healthy == 0U ||
        control.launch_count < 4U ||
        control.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(control.pid, control.process_generation)) {
        supervisor_unlock(transaction_flags);
        /* Once the local identity is configured, ARP replies are exclusively
         * service-owned. An unavailable service therefore drops the request
         * instead of reviving the legacy Ring-0 responder. */
        return local_request;
    }
    if (local_request) {
        uint32_t request_id = 0U;
        uint32_t request_epoch = control.process_generation;
        int begin = supervisor_protected_probe_authority_begin_epoch(
            &probe_runtime.arp_reply_authority, pit_monotonic_ms(),
            SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS, request_epoch, &request_id);
        if (begin == 0)
            begin = supervisor_protected_arp_reply_context_publish(
                &probe_runtime.arp_reply_context, request_id, request_epoch,
                ((uint32_t)frame[28] << 24U) |
                    ((uint32_t)frame[29] << 16U) |
                    ((uint32_t)frame[30] << 8U) | frame[31],
                &frame[22]);
        supervisor_unlock(transaction_flags);
        if (begin != 0) {
            transaction_flags = supervisor_lock();
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.arp_reply_authority);
            (void)supervisor_protected_arp_reply_context_clear(
                &probe_runtime.arp_reply_context);
            supervisor_unlock(transaction_flags);
            if (begin == SUPERVISOR_EINTEGRITY)
                (void)supervisor_force_isolate(control.handle);
            else
                network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
            return true;
        }
        ipc_message_t request = {
            .version = IPC_MESSAGE_VERSION,
            .struct_size = sizeof(ipc_message_t),
            .length = 60U,
            .payload = {'N', 'E', 'T', 'Q'},
        };
        for (uint32_t index = 0U; index < 42U; ++index)
            request.payload[index + 4U] = frame[index];
        for (uint32_t index = 0U; index < 4U; ++index)
            request.payload[46U + index] =
                (uint8_t)(local_ip >> (24U - index * 8U));
        for (uint32_t index = 0U; index < 6U; ++index)
            request.payload[50U + index] = local_mac[index];
        for (uint32_t index = 0U; index < 4U; ++index)
            request.payload[56U + index] =
                (uint8_t)(request_id >> (index * 8U));
        int ingress = ipc_send_external_from_peer(
            control.pid, control.process_generation, control.endpoint_handle,
            &request);
        if (ingress != 0) {
            transaction_flags = supervisor_lock();
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.arp_reply_authority);
            (void)supervisor_protected_arp_reply_context_clear(
                &probe_runtime.arp_reply_context);
            supervisor_unlock(transaction_flags);
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        } else {
            printf("REIST_NETWORK ARP_REQUEST_QUEUED\n");
        }
        /* A local request belongs exclusively to the service.  Queue pressure
         * drops it fail-closed instead of reviving the Ring-0 reply path. */
        return true;
    }
    uint32_t probe_id;
    supervisor_network_probe_context_t network_context;
    int context_result = supervisor_protected_network_context_snapshot(
        &probe_runtime.network_probe_context, &network_context);
    if (context_result != 0 || control.network_epoch == 0U ||
        network_context.transaction_epoch != control.network_epoch) {
        supervisor_unlock(transaction_flags);
        (void)supervisor_force_isolate(control.handle);
        return false;
    }
    if (supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.network_probe_authority, pit_monotonic_ms(),
            control.network_epoch, &probe_id) != 0 ||
        supervisor_protected_network_context_publish_binding_epoch(
            &probe_runtime.network_probe_context, control.network_epoch,
            probe_id,
            ((uint32_t)frame[28] << 24U) |
                ((uint32_t)frame[29] << 16U) |
                ((uint32_t)frame[30] << 8U) | frame[31],
            &frame[22]) != 0) {
        supervisor_unlock(transaction_flags);
        (void)supervisor_force_isolate(control.handle);
        return false;
    }
    supervisor_unlock(transaction_flags);
    ipc_message_t message = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc_message_t),
        .length = 64U,
        .payload = {'N', 'E', 'T', 'R'},
    };
    for (uint32_t index = 0U; index < 42U; ++index)
        message.payload[index + 4U] = frame[index];
    uint32_t gateway = network_context.gateway;
    uint32_t probe_local_ip = network_context.local_ip;
    for (uint32_t index = 0U; index < 4U; ++index) {
        uint32_t shift = 24U - index * 8U;
        message.payload[46U + index] = (uint8_t)(gateway >> shift);
        message.payload[50U + index] = (uint8_t)(probe_local_ip >> shift);
    }
    for (uint32_t index = 0U; index < 6U; ++index)
        message.payload[54U + index] =
            network_context.local_mac[index];
    for (uint32_t index = 0U; index < 4U; ++index)
        message.payload[60U + index] =
            (uint8_t)(probe_id >> (index * 8U));
    int ingress = ipc_send_external_from_peer(
        control.pid, control.process_generation, control.endpoint_handle,
        &message);
    /* A matching reply consumes exactly one probe authorization even when
     * bounded IPC pressure forces the frame back to the kernel path. */
    if (ingress != 0)
        (void)supervisor_protected_network_context_consume_epoch(
            &probe_runtime.network_probe_context, control.network_epoch,
            probe_id);
    if (ingress == -11) {
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        printf("REIST_NETWORK QUEUE_PRESSURE_FALLBACK\n");
    }
    return ingress == 0;
}

int supervisor_network_probe_request(int pid, uint32_t generation,
                                     uint64_t now_ms) {
    uint32_t ignored_probe_id;
    return supervisor_network_probe_request_id(pid, generation, now_ms,
                                               &ignored_probe_id);
}

int supervisor_network_probe_request_id(int pid, uint32_t generation,
                                        uint64_t now_ms,
                                        uint32_t *probe_id_out) {
    if (probe_id_out == NULL) return -22;
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U ||
        control.fenced != 0U || control.healthy == 0U || pid != control.pid ||
        generation != control.process_generation ||
        !process_identity_alive(pid, generation)) return -13;
    if (control.last_network_probe_ms != 0U &&
        now_ms - control.last_network_probe_ms < 250U) return -11;
    uint32_t gateway = netstack_get_gateway();
    uint32_t local_ip = netstack_get_ip_address();
    uint8_t local_mac[6];
    if (gateway == 0U || local_ip == 0U ||
        !netdev_get_mac_address(local_mac)) return -19;
    uint32_t transaction_flags = supervisor_lock();
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U ||
        control.fenced != 0U || control.healthy == 0U || pid != control.pid ||
        generation != control.process_generation ||
        !process_identity_alive(pid, generation)) {
        supervisor_unlock(transaction_flags);
        return -13;
    }
    if (control.last_network_probe_ms != 0U &&
        now_ms - control.last_network_probe_ms < 250U) {
        supervisor_unlock(transaction_flags);
        return -11;
    }
    uint32_t probe_id;
    int authority = supervisor_protected_probe_authority_begin(
        &probe_runtime.network_probe_authority, now_ms,
        SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS, &probe_id);
    if (authority != 0) {
        supervisor_unlock(transaction_flags);
        return authority;
    }
    uint32_t transaction_epoch = probe_id;
    int context_result = supervisor_protected_network_context_prepare_epoch(
        &probe_runtime.network_probe_context, transaction_epoch, gateway,
        local_ip, local_mac);
    if (context_result != 0) {
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.network_probe_authority);
        supervisor_unlock(transaction_flags);
        return context_result;
    }
    control.network_epoch = transaction_epoch;
    control.last_network_probe_ms = now_ms;
    if (supervisor_protected_probe_control_write(
            &probe_runtime.control, &control) != 0) {
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.network_probe_authority);
        (void)supervisor_protected_network_context_clear(
            &probe_runtime.network_probe_context);
        supervisor_unlock(transaction_flags);
        return SUPERVISOR_EINTEGRITY;
    }
    supervisor_unlock(transaction_flags);
    if (netstack_probe_gateway()) {
        *probe_id_out = probe_id;
        return 0;
    }
    (void)supervisor_protected_probe_authority_cancel(
        &probe_runtime.network_probe_authority);
    (void)supervisor_protected_network_context_clear(
        &probe_runtime.network_probe_context);
    control.network_epoch = 0U;
    if (supervisor_protected_probe_control_write(
            &probe_runtime.control, &control) != 0) {
        output_fence_all();
        return SUPERVISOR_EINTEGRITY;
    }
    return -19;
}

int supervisor_network_commit_arp_binding(
        int pid, uint32_t generation,
        const supervisor_arp_binding_t *binding) {
    if (binding == NULL || binding->version != SUPERVISOR_ARP_BINDING_VERSION ||
        binding->struct_size < sizeof(*binding) || binding->probe_id == 0U ||
        binding->ip == 0U || binding->reserved[0] != 0U ||
        binding->reserved[1] != 0U || (binding->mac[0] & 1U) != 0U)
        return -22;
    bool nonzero_mac = false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (binding->mac[index] != 0U) nonzero_mac = true;
    if (!nonzero_mac) return -22;

    uint32_t transaction_flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_network_probe_context_t context;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || pid != control.pid ||
         generation != control.process_generation ||
         !process_identity_alive(pid, generation) ||
         control.network_epoch != binding->probe_id)) result = -13;
    if (result == 0)
        result = supervisor_protected_network_context_snapshot(
            &probe_runtime.network_probe_context, &context);
    if (result == 0 &&
        (context.transaction_epoch != control.network_epoch ||
         context.delivered_id != binding->probe_id ||
         context.candidate_ip != binding->ip)) result = -13;
    if (result == 0) {
        for (uint32_t index = 0U; index < 6U; ++index)
            if (context.candidate_mac[index] != binding->mac[index])
                result = -13;
    }
    if (result == 0)
        result = supervisor_protected_network_context_consume_epoch(
            &probe_runtime.network_probe_context, control.network_epoch,
            binding->probe_id);
    if (result == 0 && !netstack_commit_arp_binding(
            binding->ip, binding->mac, binding->probe_id,
            pid, generation, pit_monotonic_ms()))
        result = -22;
    supervisor_unlock(transaction_flags);
    if (result == 0)
        printf("REIST_NETWORK PROBE_ID_OK\nREIST_NETWORK ARP_BINDING_OK\n");
    return result;
}

bool supervisor_network_request_arp_resolution(uint32_t target_ip) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    uint32_t local_ip = 0U;
    uint8_t local_mac[6] = {0};
    if (target_ip == 0U || target_ip == 0xFFFFFFFFU ||
        !netstack_get_local_identity(&local_ip, local_mac) ||
        target_ip == local_ip) return false;

    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result != 0 || control.active == 0U || control.fenced != 0U ||
        control.healthy == 0U || control.launch_count < 4U ||
        control.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(control.pid, control.process_generation)) {
        supervisor_unlock(flags);
        return false;
    }
    uint32_t request_id = 0U;
    result = supervisor_protected_probe_authority_begin_epoch(
        &probe_runtime.arp_resolution_authority, pit_monotonic_ms(),
        SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS, control.process_generation,
        &request_id);
    if (result == 0)
        result = supervisor_protected_arp_resolution_context_publish(
            &probe_runtime.arp_resolution_context, request_id,
            control.process_generation, target_ip);
    supervisor_unlock(flags);
    if (result != 0) return false;

    ipc_message_t message = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc_message_t),
        .length = 22U,
        .payload = {'N', 'E', 'T', 'A'},
    };
    for (uint32_t index = 0U; index < 4U; ++index) {
        uint32_t shift = 24U - index * 8U;
        message.payload[4U + index] = (uint8_t)(target_ip >> shift);
        message.payload[8U + index] = (uint8_t)(local_ip >> shift);
    }
    for (uint32_t index = 0U; index < 6U; ++index)
        message.payload[12U + index] = local_mac[index];
    for (uint32_t index = 0U; index < 4U; ++index)
        message.payload[18U + index] =
            (uint8_t)(request_id >> (index * 8U));
    result = ipc_send_external_from_peer(
        control.pid, control.process_generation, control.endpoint_handle,
        &message);
    if (result != 0) {
        flags = supervisor_lock();
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.arp_resolution_authority);
        (void)supervisor_protected_arp_resolution_context_clear(
            &probe_runtime.arp_resolution_context);
        supervisor_unlock(flags);
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        return false;
    }
    printf("REIST_NETWORK ARP_RESOLUTION_QUEUED\n");
    return true;
}

int supervisor_network_send_arp_request(
        int pid, uint32_t generation,
        const supervisor_arp_resolution_t *request) {
    if (request == NULL ||
        request->version != SUPERVISOR_ARP_RESOLUTION_VERSION ||
        request->struct_size < sizeof(*request) || request->request_id == 0U ||
        request->target_ip == 0U || request->target_ip == 0xFFFFFFFFU)
        return -22;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_arp_resolution_context_t context;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = supervisor_protected_arp_resolution_context_snapshot(
            &probe_runtime.arp_resolution_context, &context);
    if (result == 0 &&
        (context.request_id != request->request_id ||
         context.transaction_epoch != generation ||
         context.target_ip != request->target_ip)) result = -13;
    uint32_t consumed_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.arp_resolution_authority, pit_monotonic_ms(),
            generation, &consumed_id);
    if (result == 0 && consumed_id != request->request_id) result = -13;
    (void)supervisor_protected_arp_resolution_context_clear(
        &probe_runtime.arp_resolution_context);
    supervisor_unlock(flags);
    if (result != 0) {
        if (caller_is_service)
            printf("REIST_NETWORK ARP_RESOLUTION_REJECTED %d\n", result);
        return result;
    }
    if (!netstack_send_arp_request(request->target_ip)) return -5;
    printf("REIST_NETWORK ARP_RESOLUTION_MEDIATED\n");
    return 0;
}

int supervisor_network_send_arp_reply(
        int pid, uint32_t generation, const supervisor_arp_reply_t *reply) {
    if (reply == NULL || reply->version != SUPERVISOR_ARP_REPLY_VERSION ||
        reply->struct_size < sizeof(*reply) || reply->request_id == 0U ||
        reply->target_ip == 0U || reply->reserved[0] != 0U ||
        reply->reserved[1] != 0U || (reply->target_mac[0] & 1U) != 0U)
        return -22;
    bool nonzero_mac = false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (reply->target_mac[index] != 0U) nonzero_mac = true;
    if (!nonzero_mac) return -22;

    uint32_t transaction_flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_arp_reply_context_t context;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || pid != control.pid ||
         generation != control.process_generation ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = supervisor_protected_arp_reply_context_snapshot(
            &probe_runtime.arp_reply_context, &context);
    if (result == 0 &&
        (context.request_id != reply->request_id ||
         context.transaction_epoch != generation ||
         context.target_ip != reply->target_ip)) result = -13;
    if (result == 0) {
        for (uint32_t index = 0U; index < 6U; ++index)
            if (context.target_mac[index] != reply->target_mac[index])
                result = -13;
    }
    uint32_t consumed_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.arp_reply_authority, pit_monotonic_ms(), generation,
            &consumed_id);
    if (result == 0 && consumed_id != reply->request_id) result = -13;
    if (result != 0)
        (void)supervisor_protected_arp_reply_context_clear(
            &probe_runtime.arp_reply_context);
    if (result == 0)
        result = supervisor_protected_arp_reply_context_clear(
            &probe_runtime.arp_reply_context);
    supervisor_unlock(transaction_flags);
    if (result != 0 && caller_is_service) {
        printf("REIST_NETWORK ARP_REPLY_REJECTED %d\n", result);
        return result;
    }
    if (result != 0) return result;
    if (!netstack_send_arp_reply(reply->target_ip, reply->target_mac)) {
        printf("REIST_NETWORK ARP_REPLY_REJECTED -5\n");
        return -5;
    }
    printf("REIST_NETWORK ARP_REPLY_MEDIATED\n");
    return 0;
}

bool supervisor_network_submit_icmp_echo(
        uint32_t source_ip, const uint8_t source_mac[6], uint16_t identifier,
        uint16_t sequence, const uint8_t *data, uint16_t data_length) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    if (source_ip == 0U || source_ip == 0xFFFFFFFFU || source_mac == NULL ||
        (source_mac[0] & 1U) != 0U ||
        data_length > SUPERVISOR_ICMP_ECHO_MAX_DATA ||
        (data_length != 0U && data == NULL)) return false;
    bool nonzero_mac = false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (source_mac[index] != 0U) nonzero_mac = true;
    if (!nonzero_mac) return false;

    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result != 0 || control.active == 0U || control.fenced != 0U ||
        control.healthy == 0U || control.launch_count < 4U ||
        control.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(control.pid, control.process_generation)) {
        supervisor_unlock(flags);
        return false;
    }
    uint32_t request_id = 0U;
    result = supervisor_protected_probe_authority_begin_epoch(
        &probe_runtime.icmp_echo_authority, pit_monotonic_ms(),
        SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS, control.process_generation,
        &request_id);
    if (result == 0)
        result = supervisor_protected_icmp_echo_context_publish(
            &probe_runtime.icmp_echo_context, request_id,
            control.process_generation, source_ip, source_mac, identifier,
            sequence, data, data_length);
    supervisor_unlock(flags);
    if (result != 0) return false;

    ipc_message_t message = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc_message_t),
        .length = (uint32_t)(24U + data_length),
        .payload = {'N', 'E', 'T', 'I'},
    };
    for (uint32_t index = 0U; index < 4U; ++index) {
        message.payload[4U + index] =
            (uint8_t)(request_id >> (index * 8U));
        message.payload[8U + index] =
            (uint8_t)(source_ip >> (24U - index * 8U));
    }
    for (uint32_t index = 0U; index < 6U; ++index)
        message.payload[12U + index] = source_mac[index];
    message.payload[18] = (uint8_t)(identifier >> 8U);
    message.payload[19] = (uint8_t)identifier;
    message.payload[20] = (uint8_t)(sequence >> 8U);
    message.payload[21] = (uint8_t)sequence;
    message.payload[22] = (uint8_t)data_length;
    message.payload[23] = (uint8_t)(data_length >> 8U);
    for (uint32_t index = 0U; index < data_length; ++index)
        message.payload[24U + index] = data[index];
    result = ipc_send_external_from_peer(
        control.pid, control.process_generation, control.endpoint_handle,
        &message);
    if (result != 0) {
        flags = supervisor_lock();
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.icmp_echo_authority);
        (void)supervisor_protected_icmp_echo_context_clear(
            &probe_runtime.icmp_echo_context);
        supervisor_unlock(flags);
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        return false;
    }
    printf("REIST_NETWORK ICMP_ECHO_QUEUED\n");
    return true;
}

int supervisor_network_send_icmp_echo_reply(
        int pid, uint32_t generation,
        const supervisor_icmp_echo_reply_t *reply) {
    if (reply == NULL ||
        reply->version != SUPERVISOR_ICMP_ECHO_REPLY_VERSION ||
        reply->struct_size < sizeof(*reply) || reply->request_id == 0U ||
        reply->reserved != 0U) return -22;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_icmp_echo_context_t context;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = supervisor_protected_icmp_echo_context_snapshot(
            &probe_runtime.icmp_echo_context, &context);
    if (result == 0 &&
        (context.request_id != reply->request_id ||
         context.transaction_epoch != generation)) result = -13;
    uint32_t consumed_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.icmp_echo_authority, pit_monotonic_ms(), generation,
            &consumed_id);
    if (result == 0 && consumed_id != reply->request_id) result = -13;
    int cleanup_result = supervisor_protected_icmp_echo_context_clear(
        &probe_runtime.icmp_echo_context);
    int cancel_result = supervisor_protected_probe_authority_cancel(
        &probe_runtime.icmp_echo_authority);
    if (result == 0 && cleanup_result != 0) result = cleanup_result;
    if (result == 0 && cancel_result != 0) result = cancel_result;
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (result != 0) {
        if (integrity_failure && caller_is_service)
            (void)supervisor_force_isolate(control.handle);
        if (caller_is_service)
            printf("REIST_NETWORK ICMP_ECHO_REJECTED %d\n", result);
        return result;
    }
    if (!netstack_send_icmp_echo_reply(
            context.source_ip, context.source_mac, context.identifier,
            context.sequence, context.data, context.data_length)) {
        printf("REIST_NETWORK ICMP_ECHO_REJECTED -5\n");
        return -5;
    }
    printf("REIST_NETWORK ICMP_ECHO_MEDIATED\n");
    return 0;
}

bool supervisor_network_submit_dhcp_config(
        uint32_t ip_address, uint32_t netmask, uint32_t gateway,
        uint32_t dns_server) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    if (!dhcp_config_valid_values(ip_address, netmask, gateway, dns_server))
        return false;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result != 0 || control.active == 0U || control.fenced != 0U ||
        control.healthy == 0U || control.launch_count < 4U ||
        control.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(control.pid, control.process_generation)) {
        supervisor_unlock(flags);
        return false;
    }
    uint32_t request_id = 0U;
    result = supervisor_protected_probe_authority_begin_epoch(
        &probe_runtime.dhcp_authority, pit_monotonic_ms(),
        SUPERVISOR_DHCP_COMMIT_TIMEOUT_MS, control.process_generation,
        &request_id);
    if (result == 0)
        result = supervisor_protected_dhcp_context_publish(
            &probe_runtime.dhcp_context, request_id,
            control.process_generation, ip_address, netmask, gateway,
            dns_server);
    supervisor_unlock(flags);
    if (result != 0) return false;

    ipc_message_t message = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc_message_t),
        .length = 24U,
        .payload = {'N', 'E', 'T', 'D'},
    };
    for (uint32_t index = 0U; index < 4U; ++index) {
        message.payload[4U + index] =
            (uint8_t)(request_id >> (index * 8U));
        message.payload[8U + index] =
            (uint8_t)(ip_address >> (24U - index * 8U));
        message.payload[12U + index] =
            (uint8_t)(netmask >> (24U - index * 8U));
        message.payload[16U + index] =
            (uint8_t)(gateway >> (24U - index * 8U));
        message.payload[20U + index] =
            (uint8_t)(dns_server >> (24U - index * 8U));
    }
    result = ipc_send_kernel_to_owner(
        control.pid, control.process_generation, control.endpoint_handle,
        &message);
    if (result != 0) {
        flags = supervisor_lock();
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.dhcp_authority);
        (void)supervisor_protected_dhcp_context_clear(
            &probe_runtime.dhcp_context);
        supervisor_unlock(flags);
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        return false;
    }
    printf("REIST_NETWORK DHCP_CONFIG_QUEUED\n");
    return true;
}

int supervisor_network_commit_dhcp_config(
        int pid, uint32_t generation,
        const supervisor_dhcp_commit_t *commit) {
    if (commit == NULL || commit->version != SUPERVISOR_DHCP_COMMIT_VERSION ||
        commit->struct_size < sizeof(*commit) || commit->request_id == 0U ||
        commit->reserved != 0U) return -22;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_dhcp_context_t context;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = supervisor_protected_dhcp_context_snapshot(
            &probe_runtime.dhcp_context, &context);
    if (result == 0 &&
        (context.request_id != commit->request_id ||
         context.transaction_epoch != generation)) result = -13;
    uint32_t consumed_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.dhcp_authority, pit_monotonic_ms(), generation,
            &consumed_id);
    if (result == 0 && consumed_id != commit->request_id) result = -13;
    int cleanup_result = supervisor_protected_dhcp_context_clear(
        &probe_runtime.dhcp_context);
    int cancel_result = supervisor_protected_probe_authority_cancel(
        &probe_runtime.dhcp_authority);
    if (result == 0 && cleanup_result != 0) result = cleanup_result;
    if (result == 0 && cancel_result != 0) result = cancel_result;
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (result != 0) {
        if (integrity_failure && caller_is_service)
            (void)supervisor_force_isolate(control.handle);
        if (caller_is_service)
            printf("REIST_NETWORK DHCP_CONFIG_REJECTED %d\n", result);
        return result;
    }
    if (!netstack_apply_supervised_dhcp(
            context.ip_address, context.netmask, context.gateway,
            context.dns_server)) {
        printf("REIST_NETWORK DHCP_CONFIG_REJECTED -5\n");
        return -5;
    }
    printf("REIST_NETWORK DHCP_CONFIG_MEDIATED\n");
    return 0;
}

static void supervisor_worker(void) {
    static uint64_t next_arp_scrub_ms;
    for (;;) {
        /* Bounded network bottom half: IRQ handlers only acknowledge and set
         * pending flags, so foreground progress must not depend on a shell
         * command happening to poll the NIC. */
        netdev_poll();
        storage_service_poll(pit_monotonic_ms());
        supervisor_probe_control_t control;
        uint32_t transaction_flags = supervisor_lock();
        int control_result = supervisor_protected_probe_control_read(
            &probe_runtime.control, &control);
        if (control_result != 0) {
            supervisor_unlock(transaction_flags);
            output_fence_all();
            if (scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS) != 0)
                (void)scheduler_yield();
            continue;
        }
        uint64_t now_ms = pit_monotonic_ms();
        int authority_expiry = supervisor_protected_probe_authority_expire_epoch(
            &probe_runtime.network_probe_authority, now_ms,
            control.network_epoch);
        int reply_expiry = supervisor_protected_probe_authority_expire_epoch(
            &probe_runtime.arp_reply_authority, now_ms,
            control.process_generation);
        int resolution_expiry =
            supervisor_protected_probe_authority_expire_epoch(
                &probe_runtime.arp_resolution_authority, now_ms,
                control.process_generation);
        int icmp_expiry = supervisor_protected_probe_authority_expire_epoch(
            &probe_runtime.icmp_echo_authority, now_ms,
            control.process_generation);
        int dhcp_expiry = supervisor_protected_probe_authority_expire_epoch(
            &probe_runtime.dhcp_authority, now_ms,
            control.process_generation);
        if (reply_expiry == 1)
            (void)supervisor_protected_arp_reply_context_clear(
                &probe_runtime.arp_reply_context);
        if (resolution_expiry == 1)
            (void)supervisor_protected_arp_resolution_context_clear(
                &probe_runtime.arp_resolution_context);
        if (icmp_expiry == 1)
            (void)supervisor_protected_icmp_echo_context_clear(
                &probe_runtime.icmp_echo_context);
        if (dhcp_expiry == 1)
            (void)supervisor_protected_dhcp_context_clear(
                &probe_runtime.dhcp_context);
        supervisor_unlock(transaction_flags);
        if (now_ms >= next_arp_scrub_ms) {
            next_arp_scrub_ms = UINT64_MAX - now_ms < 1000U
                                    ? UINT64_MAX : now_ms + 1000U;
            uint32_t expired = 0U;
            uint32_t corrected = 0U;
            if (!netstack_scrub_arp_bindings(now_ms, &expired, &corrected)) {
                if (control.active != 0U)
                    (void)supervisor_force_isolate(control.handle);
                else
                    output_fence_all();
            }
            if (expired != 0U)
                printf("REIST_NETWORK ARP_BINDING_EXPIRED %u\n", expired);
            if (corrected != 0U)
                printf("REIST_NETWORK ARP_BINDING_CORRECTED %u\n", corrected);
        }
        if (authority_expiry == 1) {
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
        } else if (authority_expiry < 0 && control.active != 0U) {
            (void)supervisor_force_isolate(control.handle);
        }
        if (reply_expiry == 1) {
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
        } else if (reply_expiry < 0 && control.active != 0U) {
            (void)supervisor_force_isolate(control.handle);
        }
        if (resolution_expiry == 1) {
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
        } else if (resolution_expiry < 0 && control.active != 0U) {
            (void)supervisor_force_isolate(control.handle);
        }
        if (dhcp_expiry == 1) {
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
        } else if (dhcp_expiry < 0 && control.active != 0U) {
            (void)supervisor_force_isolate(control.handle);
        }
        if (control.active != 0U && control.fenced == 0U &&
            !process_identity_alive(control.pid, control.process_generation)) {
            (void)supervisor_force_isolate(control.handle);
        }
        supervisor_event_t result = supervisor_service_one(pit_monotonic_ms());
        if (control.active != 0U &&
            result.type == SUPERVISOR_EVENT_RESTART_REQUIRED &&
            result.handle.slot == control.handle.slot &&
            result.handle.generation == control.handle.generation) {
            if (supervisor_protected_probe_control_read(
                    &probe_runtime.control, &control) != 0 ||
                control.active == 0U) {
                output_fence_all();
                continue;
            }
            control.handle = result.handle;
            if (supervisor_protected_probe_control_write(
                    &probe_runtime.control, &control) != 0) {
                output_fence_all();
                continue;
            }
            if (!probe_spawn_next()) {
                (void)supervisor_force_isolate(control.handle);
            }
        }
        if (result.type == SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) {
            /* Until per-hazard external interlocks are registered, the
             * conservative system response revokes every known output. */
            output_fence_all();
        }
        if (scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS) != 0)
            (void)scheduler_yield();
    }
}

bool supervisor_start_worker(void) {
    uint32_t *stack = scheduler_allocate_kernel_stack();
    if (stack == NULL) return false;
    if (create_task(supervisor_worker, stack, NULL) < 0) {
        scheduler_free_kernel_stack(stack);
        return false;
    }
    return true;
}

int supervisor_spawn_service(const char *path, int argc,
                             const char *const *argv, uint32_t domain_kind) {
    if (domain_kind != PROCESS_DOMAIN_PROBE &&
        domain_kind != PROCESS_DOMAIN_STORAGE) return -1;
    return process_spawn_supervised(path, argc, argv,
                                    (process_domain_kind_t)domain_kind);
}
#else
bool supervisor_start_worker(void) {
    return false;
}

int supervisor_spawn_service(const char *path, int argc,
                             const char *const *argv, uint32_t domain_kind) {
    (void)path;
    (void)argc;
    (void)argv;
    (void)domain_kind;
    return -1;
}

bool supervisor_start_probe(uint64_t now_ms) {
    (void)now_ms;
    return false;
}

bool supervisor_probe_ready(void) {
    return false;
}

int supervisor_probe_report(int pid, uint32_t generation,
                            uint32_t report_type, uint32_t value,
                            uint64_t now_ms) {
    (void)pid; (void)generation; (void)report_type; (void)value; (void)now_ms;
    return -1;
}

int supervisor_service_connect(struct Process *client, uint32_t service_id,
                               uint32_t *handle_out) {
    (void)client; (void)service_id; (void)handle_out;
    return -1;
}

bool supervisor_network_submit_header(const uint8_t *frame, uint16_t length) {
    (void)frame; (void)length;
    return false;
}
int supervisor_network_probe_request(int pid, uint32_t generation,
                                     uint64_t now_ms) {
    (void)pid; (void)generation; (void)now_ms;
    return -1;
}
int supervisor_network_probe_request_id(int pid, uint32_t generation,
                                        uint64_t now_ms,
                                        uint32_t *probe_id_out) {
    (void)pid; (void)generation; (void)now_ms; (void)probe_id_out;
    return -1;
}
#endif

supervisor_event_t supervisor_service_one(uint64_t now_ms) {
#ifndef REIST_HOST_TEST
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
#endif
    /* One invocation performs at most one potentially slow fence action.
     * Callers retain explicit control over their execution-time budget. */
    supervisor_event_t pending = supervisor_poll(now_ms);
    if (pending.type != SUPERVISOR_EVENT_FENCE_REQUIRED) return pending;
    return supervisor_apply_fence(pending.handle, now_ms);
}

supervisor_event_t supervisor_apply_fence(supervisor_handle_t handle,
                                          uint64_t now_ms) {
    supervisor_event_t none = {.type = SUPERVISOR_EVENT_NONE};
    supervisor_fence_ops_t fence_ops;
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_ISOLATED) {
        supervisor_unlock(flags);
        return none;
    }
    if (fence_ops_read(handle.slot, &fence_ops) != 0) {
        state.state = SUPERVISOR_SAFE_STATE;
        (void)state_write(handle.slot, &state);
        supervisor_event_t result = event(
            SUPERVISOR_EVENT_SAFE_STATE_REQUIRED, handle.slot, &state);
        supervisor_unlock(flags);
        return result;
    }
    state.state = SUPERVISOR_FENCING;
    if (state_write(handle.slot, &state) != 0) {
        supervisor_unlock(flags);
        return event(SUPERVISOR_EVENT_SAFE_STATE_REQUIRED, handle.slot, &state);
    }
    supervisor_unlock(flags);

    /* Hardware/service fencing may require interrupt-driven I/O. Never run it
     * under the supervisor's IRQ-off state lock. Verification is deliberately
     * separate: a successful write alone is not safety evidence. */
    bool fenced = fence_ops.apply(fence_ops.context) &&
                  fence_ops.verify(fence_ops.context);

    flags = supervisor_lock();
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_FENCING) {
        supervisor_unlock(flags);
        return none;
    }
    if (!fenced) {
        state.state = SUPERVISOR_SAFE_STATE;
        (void)state_write(handle.slot, &state);
        supervisor_event_t result = event(SUPERVISOR_EVENT_SAFE_STATE_REQUIRED,
                                           handle.slot, &state);
        supervisor_unlock(flags);
        return result;
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
    if (resolve(handle, &state) != 0 ||
        (state.state != SUPERVISOR_RECOVERING &&
         state.state != SUPERVISOR_STARTING)) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = passed ? SUPERVISOR_STARTING : SUPERVISOR_ISOLATED;
    state.progress_marker = 0;
    state.deadline_ms = deadline_after(now_ms, state.recovery_timeout_ms);
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

bool supervisor_output_allowed(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    bool allowed = resolve(handle, &state) == 0 &&
                   (state.state == SUPERVISOR_HEALTHY ||
                    state.state == SUPERVISOR_IDLE);
    supervisor_unlock(flags);
    return allowed;
}

#ifdef REIST_HOST_TEST
int supervisor_test_corrupt_fence_ops(supervisor_handle_t handle,
                                      bool corrupt_both_copies) {
    supervisor_state_t state;
    if (resolve(handle, &state) != 0) return -1;
    critical_object_t *object = &slots[handle.slot].protected_fence_ops;
    object->primary.crc32 ^= 1U;
    if (corrupt_both_copies) object->shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_corrupt_descriptor(supervisor_handle_t handle,
                                       bool corrupt_both_copies) {
    supervisor_state_t state;
    if (resolve(handle, &state) != 0) return -1;
    critical_object_t *object = &slots[handle.slot].protected_descriptor;
    object->primary.crc32 ^= 1U;
    if (corrupt_both_copies) object->shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_corrupt_network_degradation(bool corrupt_both_copies) {
    protected_network_degradation_stats.primary.crc32 ^= 1U;
    if (corrupt_both_copies)
        protected_network_degradation_stats.shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_record_network_degradation(
        supervisor_network_degradation_reason_t reason) {
    network_degradation_record(reason);
    return 0;
}

int supervisor_test_corrupt_probe_authority(
        supervisor_protected_probe_authority_t *authority,
        bool corrupt_both_copies) {
    if (authority == NULL) return -22;
    authority->object.primary.crc32 ^= 1U;
    if (corrupt_both_copies) authority->object.shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_corrupt_network_context(
        supervisor_protected_network_context_t *context,
        bool corrupt_both_copies) {
    if (context == NULL) return -22;
    context->object.primary.crc32 ^= 1U;
    if (corrupt_both_copies) context->object.shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_corrupt_arp_reply_context(
        supervisor_protected_arp_reply_context_t *context,
        bool corrupt_both_copies) {
    if (context == NULL) return -22;
    context->object.primary.crc32 ^= 1U;
    if (corrupt_both_copies) context->object.shadow.crc32 ^= 2U;
    return 0;
}

int supervisor_test_corrupt_probe_control(
        supervisor_protected_probe_control_t *control,
        bool corrupt_both_copies) {
    if (control == NULL) return -22;
    control->object.primary.crc32 ^= 1U;
    if (corrupt_both_copies) control->object.shadow.crc32 ^= 2U;
    return 0;
}
#endif
