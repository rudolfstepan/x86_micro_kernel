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
_Static_assert(sizeof(supervisor_dhcp_lease_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "DHCP lease exceeds critical object payload");
_Static_assert(sizeof(supervisor_dhcp_renewal_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "DHCP renewal exceeds critical object payload");
_Static_assert(sizeof(supervisor_dhcp_boot_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "DHCP boot transaction exceeds critical object payload");
_Static_assert(sizeof(supervisor_dhcp_ingress_t) == 52U,
               "DHCP ingress ABI drift");
_Static_assert(sizeof(supervisor_udp_echo_context_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "UDP echo context exceeds critical object payload");
_Static_assert(sizeof(supervisor_udp_binding_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "UDP binding exceeds critical object payload");
_Static_assert(sizeof(supervisor_udp_delivery_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "UDP delivery exceeds critical object payload");
_Static_assert(sizeof(supervisor_probe_control_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "probe control exceeds critical object payload");

typedef struct {
    supervisor_protected_udp_binding_t binding;
    supervisor_protected_probe_authority_t authority;
    supervisor_protected_udp_echo_context_t context;
} supervisor_udp_binding_runtime_t;

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
#define SUPERVISOR_DHCP_RENEW_TIMEOUT_MS 1500U
#define SUPERVISOR_DHCP_BOOT_TIMEOUT_MS 1500U

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
    supervisor_protected_dhcp_lease_t dhcp_lease;
    supervisor_protected_probe_authority_t dhcp_renewal_authority;
    supervisor_protected_dhcp_renewal_t dhcp_renewal;
    supervisor_protected_probe_authority_t dhcp_boot_authority;
    supervisor_protected_dhcp_boot_t dhcp_boot;
    supervisor_udp_binding_runtime_t udp_bindings[SUPERVISOR_UDP_MAX_BINDINGS];
    int32_t frame_delivery_pid;
    uint32_t frame_delivery_generation;
    uint32_t frame_delivery_ethertype;
    uint32_t frame_delivery_pending;
    int32_t frame_handoff_report_pid;
    uint32_t frame_handoff_report_generation;
    int32_t ipv4_delivery_pid;
    uint32_t ipv4_delivery_generation;
    uint32_t ipv4_delivery_pending;
    int32_t ipv4_report_pid;
    uint32_t ipv4_report_generation;
    int32_t icmp_delivery_pid;
    uint32_t icmp_delivery_generation;
    uint32_t icmp_delivery_crc32;
    uint32_t icmp_delivery_pending;
    int32_t icmp_report_pid;
    uint32_t icmp_report_generation;
    int32_t udp_delivery_pid;
    uint32_t udp_delivery_generation;
    uint32_t udp_delivery_pending;
    int32_t udp_report_pid;
    uint32_t udp_report_generation;
    int32_t dhcp_delivery_pid;
    uint32_t dhcp_delivery_generation;
    uint32_t dhcp_delivery_crc32;
    uint32_t dhcp_delivery_pending;
    int32_t dhcp_report_pid;
    uint32_t dhcp_report_generation;
    supervisor_protected_icmp_delivery_t icmp_ingress_delivery;
    supervisor_protected_udp_delivery_t udp_ingress_delivery;
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

#ifndef REIST_HOST_TEST
static uint32_t network_frame_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static bool network_frame_is_dhcp_reply(
        const supervisor_network_frame_t *frame) {
    if (frame == NULL || frame->length < 42U || frame->data[12U] != 0x08U ||
        frame->data[13U] != 0x00U) return false;
    uint8_t version_ihl = frame->data[14U];
    uint32_t header_length = (uint32_t)(version_ihl & 0x0FU) * 4U;
    if ((version_ihl >> 4U) != 4U || header_length < 20U ||
        header_length > 60U || 14U + header_length + 8U > frame->length ||
        frame->data[23U] != 17U) return false;
    const uint8_t *udp = &frame->data[14U + header_length];
    return udp[0U] == 0U && udp[1U] == 67U &&
           udp[2U] == 0U && udp[3U] == 68U;
}
#endif

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
    bool empty = context->request_id == 0U &&
        context->transaction_epoch == 0U && context->ip_address == 0U &&
        context->netmask == 0U && context->gateway == 0U &&
        context->dns_server == 0U && context->lease_seconds == 0U &&
        context->operation == 0U;
    if (empty) return true;
    return context->request_id != 0U && context->transaction_epoch != 0U &&
        context->lease_seconds >= SUPERVISOR_DHCP_LEASE_MIN_SECONDS &&
        context->lease_seconds <= SUPERVISOR_DHCP_LEASE_MAX_SECONDS &&
        context->operation <= SUPERVISOR_DHCP_REBIND &&
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

static int dhcp_context_publish_operation(
        supervisor_protected_dhcp_context_t *protected_context,
        uint32_t request_id, uint32_t transaction_epoch, uint32_t ip_address,
        uint32_t netmask, uint32_t gateway, uint32_t dns_server,
        uint32_t lease_seconds, uint32_t operation) {
    supervisor_dhcp_context_t context = {
        .request_id = request_id,
        .transaction_epoch = transaction_epoch,
        .ip_address = ip_address,
        .netmask = netmask,
        .gateway = gateway,
        .dns_server = dns_server,
        .lease_seconds = lease_seconds,
        .operation = operation,
    };
    if (protected_context == NULL || !dhcp_context_valid(&context,
                                                         sizeof(context)))
        return -22;
    return dhcp_context_write(protected_context, &context);
}

int supervisor_protected_dhcp_context_publish(
        supervisor_protected_dhcp_context_t *protected_context,
        uint32_t request_id, uint32_t transaction_epoch, uint32_t ip_address,
        uint32_t netmask, uint32_t gateway, uint32_t dns_server,
        uint32_t lease_seconds) {
    return dhcp_context_publish_operation(
        protected_context, request_id, transaction_epoch, ip_address, netmask,
        gateway, dns_server, lease_seconds, 0U);
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

static bool dhcp_lease_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_dhcp_lease_t))
        return false;
    const supervisor_dhcp_lease_t *lease = payload;
    if (lease->reserved != 0U) return false;
    bool empty = lease->process_generation == 0U && lease->ip_address == 0U &&
        lease->lease_seconds == 0U && lease->deadline_ms == 0U;
    if (empty) return true;
    return lease->process_generation != 0U && lease->ip_address != 0U &&
        lease->ip_address != 0xFFFFFFFFU &&
        lease->lease_seconds >= SUPERVISOR_DHCP_LEASE_MIN_SECONDS &&
        lease->lease_seconds <= SUPERVISOR_DHCP_LEASE_MAX_SECONDS &&
        lease->deadline_ms != 0U;
}

static int dhcp_lease_write(
        supervisor_protected_dhcp_lease_t *protected_lease,
        const supervisor_dhcp_lease_t *lease) {
    return critical_object_update(
        &protected_lease->object, SUPERVISOR_DHCP_LEASE_VERSION,
        lease, sizeof(*lease), dhcp_lease_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_lease_init(
        supervisor_protected_dhcp_lease_t *protected_lease) {
    if (protected_lease == NULL) return -22;
    supervisor_dhcp_lease_t lease = {0};
    return critical_object_init(
        &protected_lease->object, SUPERVISOR_DHCP_LEASE_VERSION,
        &lease, sizeof(lease)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_lease_publish(
        supervisor_protected_dhcp_lease_t *protected_lease,
        uint32_t process_generation, uint32_t ip_address,
        uint32_t lease_seconds, uint64_t deadline_ms) {
    supervisor_dhcp_lease_t lease = {
        .process_generation = process_generation,
        .ip_address = ip_address,
        .lease_seconds = lease_seconds,
        .deadline_ms = deadline_ms,
    };
    if (protected_lease == NULL || !dhcp_lease_valid(&lease, sizeof(lease)))
        return -22;
    return dhcp_lease_write(protected_lease, &lease);
}

int supervisor_protected_dhcp_lease_snapshot(
        supervisor_protected_dhcp_lease_t *protected_lease,
        supervisor_dhcp_lease_t *snapshot_out) {
    if (protected_lease == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_lease->object, SUPERVISOR_DHCP_LEASE_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length, dhcp_lease_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_dhcp_lease_clear(
        supervisor_protected_dhcp_lease_t *protected_lease) {
    supervisor_dhcp_lease_t lease = {0};
    return dhcp_lease_write(protected_lease, &lease);
}

static bool dhcp_renewal_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_dhcp_renewal_t))
        return false;
    const supervisor_dhcp_renewal_t *renewal = payload;
    if (renewal->reserved != 0U) return false;
    bool empty = renewal->active == 0U && renewal->operation == 0U &&
        renewal->process_generation == 0U && renewal->transaction_id == 0U &&
        renewal->ip_address == 0U && renewal->deadline_ms == 0U;
    if (empty) return true;
    return renewal->active == 1U &&
        (renewal->operation == SUPERVISOR_DHCP_RENEW ||
         renewal->operation == SUPERVISOR_DHCP_REBIND) &&
        renewal->process_generation != 0U && renewal->transaction_id != 0U &&
        renewal->ip_address != 0U && renewal->ip_address != UINT32_MAX &&
        renewal->deadline_ms != 0U;
}

static int dhcp_renewal_write(
        supervisor_protected_dhcp_renewal_t *protected_renewal,
        const supervisor_dhcp_renewal_t *renewal) {
    return critical_object_update(
        &protected_renewal->object, SUPERVISOR_DHCP_RENEWAL_VERSION,
        renewal, sizeof(*renewal), dhcp_renewal_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_renewal_init(
        supervisor_protected_dhcp_renewal_t *protected_renewal) {
    if (protected_renewal == NULL) return -22;
    supervisor_dhcp_renewal_t renewal = {0};
    return critical_object_init(
        &protected_renewal->object, SUPERVISOR_DHCP_RENEWAL_VERSION,
        &renewal, sizeof(renewal)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_renewal_publish(
        supervisor_protected_dhcp_renewal_t *protected_renewal,
        uint32_t operation, uint32_t process_generation,
        uint32_t transaction_id, uint32_t ip_address,
        uint64_t deadline_ms) {
    supervisor_dhcp_renewal_t renewal = {
        .active = 1U,
        .operation = operation,
        .process_generation = process_generation,
        .transaction_id = transaction_id,
        .ip_address = ip_address,
        .deadline_ms = deadline_ms,
    };
    if (protected_renewal == NULL ||
        !dhcp_renewal_valid(&renewal, sizeof(renewal))) return -22;
    return dhcp_renewal_write(protected_renewal, &renewal);
}

int supervisor_protected_dhcp_renewal_snapshot(
        supervisor_protected_dhcp_renewal_t *protected_renewal,
        supervisor_dhcp_renewal_t *snapshot_out) {
    if (protected_renewal == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_renewal->object, SUPERVISOR_DHCP_RENEWAL_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length, dhcp_renewal_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_dhcp_renewal_clear(
        supervisor_protected_dhcp_renewal_t *protected_renewal) {
    supervisor_dhcp_renewal_t renewal = {0};
    return dhcp_renewal_write(protected_renewal, &renewal);
}

static bool dhcp_boot_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_dhcp_boot_t))
        return false;
    const supervisor_dhcp_boot_t *transaction = payload;
    if (transaction->active == 0U) {
        const supervisor_dhcp_boot_t empty = {0};
        const uint8_t *actual = payload;
        const uint8_t *expected = (const uint8_t *)&empty;
        for (size_t index = 0U; index < sizeof(empty); ++index)
            if (actual[index] != expected[index]) return false;
        return true;
    }
    if (transaction->active != 1U || transaction->reserved != 0U ||
        transaction->process_generation == 0U ||
        transaction->transaction_id == 0U ||
        transaction->deadline_ms == 0U ||
        (transaction->phase != SUPERVISOR_DHCP_BOOT_DISCOVER_SENT &&
         transaction->phase != SUPERVISOR_DHCP_BOOT_REQUEST_SENT))
        return false;
    if (transaction->phase == SUPERVISOR_DHCP_BOOT_DISCOVER_SENT)
        return transaction->offered_ip == 0U && transaction->server_id == 0U;
    return transaction->offered_ip != 0U &&
        transaction->offered_ip != UINT32_MAX &&
        transaction->server_id != 0U && transaction->server_id != UINT32_MAX;
}

static int dhcp_boot_write(
        supervisor_protected_dhcp_boot_t *protected_transaction,
        const supervisor_dhcp_boot_t *transaction) {
    return critical_object_update(
        &protected_transaction->object, SUPERVISOR_DHCP_BOOT_VERSION,
        transaction, sizeof(*transaction), dhcp_boot_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_boot_init(
        supervisor_protected_dhcp_boot_t *protected_transaction) {
    if (protected_transaction == NULL) return -22;
    supervisor_dhcp_boot_t transaction = {0};
    return critical_object_init(
        &protected_transaction->object, SUPERVISOR_DHCP_BOOT_VERSION,
        &transaction, sizeof(transaction)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_dhcp_boot_publish(
        supervisor_protected_dhcp_boot_t *protected_transaction,
        uint32_t phase, uint32_t process_generation, uint32_t transaction_id,
        uint32_t offered_ip, uint32_t server_id, uint64_t deadline_ms) {
    if (protected_transaction == NULL) return -22;
    supervisor_dhcp_boot_t transaction = {
        .active = 1U,
        .phase = phase,
        .process_generation = process_generation,
        .transaction_id = transaction_id,
        .offered_ip = offered_ip,
        .server_id = server_id,
        .deadline_ms = deadline_ms,
    };
    if (!dhcp_boot_valid(&transaction, sizeof(transaction))) return -22;
    return dhcp_boot_write(protected_transaction, &transaction);
}

int supervisor_protected_dhcp_boot_snapshot(
        supervisor_protected_dhcp_boot_t *protected_transaction,
        supervisor_dhcp_boot_t *snapshot_out) {
    if (protected_transaction == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_transaction->object, SUPERVISOR_DHCP_BOOT_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length, dhcp_boot_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_dhcp_boot_clear(
        supervisor_protected_dhcp_boot_t *protected_transaction) {
    supervisor_dhcp_boot_t transaction = {0};
    return dhcp_boot_write(protected_transaction, &transaction);
}

static bool udp_echo_context_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_udp_echo_context_t))
        return false;
    const supervisor_udp_echo_context_t *context = payload;
    if (context->reserved != 0U || context->reserved_tail[0] != 0U ||
        context->reserved_tail[1] != 0U ||
        context->data_length > SUPERVISOR_UDP_ECHO_MAX_DATA) return false;
    bool empty_mac = true;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (context->source_mac[index] != 0U) empty_mac = false;
    bool empty = context->request_id == 0U &&
        context->transaction_epoch == 0U && context->source_ip == 0U &&
        context->source_port == 0U && context->destination_port == 0U &&
        context->data_length == 0U && empty_mac;
    if (empty) {
        for (uint32_t index = 0U; index < SUPERVISOR_UDP_ECHO_MAX_DATA;
             ++index)
            if (context->data[index] != 0U) return false;
        return true;
    }
    if (context->request_id == 0U || context->transaction_epoch == 0U ||
        context->source_ip == 0U || context->source_ip == 0xFFFFFFFFU ||
        context->source_port == 0U ||
        context->destination_port < SUPERVISOR_UDP_BINDING_MIN_PORT ||
        empty_mac ||
        (context->source_mac[0] & 1U) != 0U) return false;
    for (uint32_t index = context->data_length;
         index < SUPERVISOR_UDP_ECHO_MAX_DATA; ++index)
        if (context->data[index] != 0U) return false;
    return true;
}

static int udp_echo_context_write(
        supervisor_protected_udp_echo_context_t *protected_context,
        const supervisor_udp_echo_context_t *context) {
    return critical_object_update(
        &protected_context->object, SUPERVISOR_UDP_ECHO_CONTEXT_VERSION,
        context, sizeof(*context), udp_echo_context_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_udp_echo_context_init(
        supervisor_protected_udp_echo_context_t *protected_context) {
    if (protected_context == NULL) return -22;
    supervisor_udp_echo_context_t context = {0};
    return critical_object_init(
        &protected_context->object, SUPERVISOR_UDP_ECHO_CONTEXT_VERSION,
        &context, sizeof(context)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

int supervisor_protected_udp_echo_context_publish(
        supervisor_protected_udp_echo_context_t *protected_context,
        uint32_t request_id, uint32_t transaction_epoch, uint32_t source_ip,
        const uint8_t source_mac[6], uint16_t source_port,
        uint16_t destination_port, const uint8_t *data,
        uint16_t data_length) {
    if (protected_context == NULL || request_id == 0U ||
        transaction_epoch == 0U || source_ip == 0U ||
        source_ip == 0xFFFFFFFFU || source_mac == NULL ||
        (source_mac[0] & 1U) != 0U || source_port == 0U ||
        destination_port < SUPERVISOR_UDP_BINDING_MIN_PORT ||
        data_length > SUPERVISOR_UDP_ECHO_MAX_DATA ||
        (data_length != 0U && data == NULL)) return -22;
    supervisor_udp_echo_context_t context = {
        .request_id = request_id,
        .transaction_epoch = transaction_epoch,
        .source_ip = source_ip,
        .source_port = source_port,
        .destination_port = destination_port,
        .data_length = data_length,
    };
    for (uint32_t index = 0U; index < 6U; ++index)
        context.source_mac[index] = source_mac[index];
    for (uint32_t index = 0U; index < data_length; ++index)
        context.data[index] = data[index];
    if (!udp_echo_context_valid(&context, sizeof(context))) return -22;
    return udp_echo_context_write(protected_context, &context);
}

int supervisor_protected_udp_echo_context_snapshot(
        supervisor_protected_udp_echo_context_t *protected_context,
        supervisor_udp_echo_context_t *snapshot_out) {
    if (protected_context == NULL || snapshot_out == NULL) return -22;
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_context->object, SUPERVISOR_UDP_ECHO_CONTEXT_VERSION,
        snapshot_out, sizeof(*snapshot_out), &length,
        udp_echo_context_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

int supervisor_protected_udp_echo_context_clear(
        supervisor_protected_udp_echo_context_t *protected_context) {
    supervisor_udp_echo_context_t context = {0};
    return udp_echo_context_write(protected_context, &context);
}

#ifndef REIST_HOST_TEST
static bool udp_binding_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_udp_binding_t))
        return false;
    const supervisor_udp_binding_t *binding = payload;
    if (binding->reserved != 0U || binding->generation > 0xFFFFFFU)
        return false;
    if (binding->active == 0U)
        return binding->process_generation == 0U && binding->port == 0U;
    return binding->active == 1U && binding->generation != 0U &&
        binding->process_generation != 0U &&
        binding->port >= SUPERVISOR_UDP_BINDING_MIN_PORT;
}

static int udp_binding_write(
        supervisor_protected_udp_binding_t *protected_binding,
        const supervisor_udp_binding_t *binding) {
    return critical_object_update(
        &protected_binding->object, SUPERVISOR_UDP_BINDING_VERSION,
        binding, sizeof(*binding), udp_binding_valid) == 0
        ? 0 : SUPERVISOR_EINTEGRITY;
}

static int udp_binding_init(
        supervisor_protected_udp_binding_t *protected_binding) {
    supervisor_udp_binding_t binding = {0};
    return critical_object_init(
        &protected_binding->object, SUPERVISOR_UDP_BINDING_VERSION,
        &binding, sizeof(binding)) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

static int udp_binding_read(
        supervisor_protected_udp_binding_t *protected_binding,
        supervisor_udp_binding_t *binding_out) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &protected_binding->object, SUPERVISOR_UDP_BINDING_VERSION,
        binding_out, sizeof(*binding_out), &length, udp_binding_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static bool udp_delivery_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_udp_delivery_t))
        return false;
    const supervisor_udp_delivery_t *delivery = payload;
    if (delivery->active == 0U) {
        const supervisor_udp_delivery_t empty = {0};
        const uint8_t *actual = payload;
        const uint8_t *expected = (const uint8_t *)&empty;
        for (size_t index = 0U; index < sizeof(empty); ++index)
            if (actual[index] != expected[index]) return false;
        return true;
    }
    return delivery->active == 1U && delivery->process_generation != 0U &&
        delivery->frame_length >= 34U &&
        delivery->frame_length <= SUPERVISOR_NETWORK_FRAME_MAX_SIZE &&
        delivery->deadline_ms != 0U;
}

static int udp_delivery_write(const supervisor_udp_delivery_t *delivery) {
    return critical_object_update(
        &probe_runtime.udp_ingress_delivery.object,
        SUPERVISOR_UDP_DELIVERY_VERSION, delivery, sizeof(*delivery),
        udp_delivery_valid) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

static int udp_delivery_read(supervisor_udp_delivery_t *delivery_out) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &probe_runtime.udp_ingress_delivery.object,
        SUPERVISOR_UDP_DELIVERY_VERSION, delivery_out, sizeof(*delivery_out),
        &length, udp_delivery_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static int udp_delivery_clear(void) {
    const supervisor_udp_delivery_t delivery = {0};
    return udp_delivery_write(&delivery);
}

static bool icmp_delivery_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_icmp_delivery_t))
        return false;
    const supervisor_icmp_delivery_t *delivery = payload;
    if (delivery->active == 0U) {
        const supervisor_icmp_delivery_t empty = {0};
        const uint8_t *actual = payload;
        const uint8_t *expected = (const uint8_t *)&empty;
        for (size_t index = 0U; index < sizeof(empty); ++index)
            if (actual[index] != expected[index]) return false;
        return true;
    }
    return delivery->active == 1U && delivery->process_generation != 0U &&
        delivery->frame_length >= 34U &&
        delivery->frame_length <= SUPERVISOR_NETWORK_FRAME_MAX_SIZE &&
        delivery->deadline_ms != 0U;
}

static int icmp_delivery_write(const supervisor_icmp_delivery_t *delivery) {
    return critical_object_update(
        &probe_runtime.icmp_ingress_delivery.object,
        SUPERVISOR_ICMP_DELIVERY_VERSION, delivery, sizeof(*delivery),
        icmp_delivery_valid) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

static int icmp_delivery_read(supervisor_icmp_delivery_t *delivery_out) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        &probe_runtime.icmp_ingress_delivery.object,
        SUPERVISOR_ICMP_DELIVERY_VERSION, delivery_out, sizeof(*delivery_out),
        &length, icmp_delivery_valid);
    return result < 0 ? SUPERVISOR_EINTEGRITY : 0;
}

static int icmp_delivery_clear(void) {
    const supervisor_icmp_delivery_t delivery = {0};
    return icmp_delivery_write(&delivery);
}

static supervisor_udp_binding_handle_t udp_binding_handle(
        uint32_t slot, uint32_t generation) {
    return (generation << 8U) | (slot + 1U);
}

static bool udp_binding_decode(supervisor_udp_binding_handle_t handle,
                               uint32_t *slot_out,
                               uint32_t *generation_out) {
    uint32_t encoded_slot = handle & 0xFFU;
    uint32_t generation = handle >> 8U;
    if (encoded_slot == 0U || encoded_slot > SUPERVISOR_UDP_MAX_BINDINGS ||
        generation == 0U) return false;
    *slot_out = encoded_slot - 1U;
    *generation_out = generation;
    return true;
}
#endif

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
    supervisor_icmp_delivery_t empty_icmp_delivery = {0};
    supervisor_udp_delivery_t empty_udp_delivery = {0};
    if (critical_object_init(&probe_runtime.icmp_ingress_delivery.object,
            SUPERVISOR_ICMP_DELIVERY_VERSION, &empty_icmp_delivery,
            sizeof(empty_icmp_delivery)) != 0 ||
        critical_object_init(&probe_runtime.udp_ingress_delivery.object,
            SUPERVISOR_UDP_DELIVERY_VERSION, &empty_udp_delivery,
            sizeof(empty_udp_delivery)) != 0 ||
        supervisor_protected_probe_control_init(&probe_runtime.control) != 0 ||
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
            &probe_runtime.dhcp_context) != 0 ||
        supervisor_protected_dhcp_lease_init(
            &probe_runtime.dhcp_lease) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.dhcp_renewal_authority) != 0 ||
        supervisor_protected_dhcp_renewal_init(
            &probe_runtime.dhcp_renewal) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.dhcp_boot_authority) != 0 ||
        supervisor_protected_dhcp_boot_init(
            &probe_runtime.dhcp_boot) != 0) {
        panic("Unable to initialize protected probe runtime");
    }
    for (uint32_t slot = 0U; slot < SUPERVISOR_UDP_MAX_BINDINGS; ++slot) {
        if (udp_binding_init(&probe_runtime.udp_bindings[slot].binding) != 0 ||
            supervisor_protected_probe_authority_init(
                &probe_runtime.udp_bindings[slot].authority) != 0 ||
            supervisor_protected_udp_echo_context_init(
                &probe_runtime.udp_bindings[slot].context) != 0)
            panic("Unable to initialize protected UDP binding runtime");
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
    supervisor_dhcp_lease_t lease;
    int lease_result = supervisor_protected_dhcp_lease_snapshot(
        &runtime->dhcp_lease, &lease);
    if (lease_result != 0) {
        (void)netstack_clear_supervised_dhcp(0U);
        output_fence_all();
        return false;
    }
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
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->dhcp_renewal_authority);
    (void)supervisor_protected_dhcp_renewal_clear(&runtime->dhcp_renewal);
    (void)supervisor_protected_probe_authority_cancel(
        &runtime->dhcp_boot_authority);
    (void)supervisor_protected_dhcp_boot_clear(&runtime->dhcp_boot);
    if (icmp_delivery_clear() != 0 || udp_delivery_clear() != 0) {
        output_fence_all();
        return false;
    }
    if (supervisor_protected_dhcp_lease_clear(&runtime->dhcp_lease) != 0) {
        (void)netstack_clear_supervised_dhcp(0U);
        output_fence_all();
        return false;
    }
    if (lease.ip_address != 0U)
        (void)netstack_clear_supervised_dhcp(lease.ip_address);
    for (uint32_t slot = 0U; slot < SUPERVISOR_UDP_MAX_BINDINGS; ++slot) {
        supervisor_udp_binding_t binding = {0};
        if (udp_binding_read(&runtime->udp_bindings[slot].binding,
                             &binding) != 0) {
            output_fence_all();
            return false;
        }
        (void)supervisor_protected_probe_authority_cancel(
            &runtime->udp_bindings[slot].authority);
        (void)supervisor_protected_udp_echo_context_clear(
            &runtime->udp_bindings[slot].context);
        binding.active = 0U;
        binding.process_generation = 0U;
        binding.port = 0U;
        if (udp_binding_write(&runtime->udp_bindings[slot].binding,
                              &binding) != 0) {
            output_fence_all();
            return false;
        }
    }
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
    /* Do not expose frames queued for a previous service generation. */
    netdev_reset_service_frames();
    probe_runtime.frame_delivery_pid = 0;
    probe_runtime.frame_delivery_generation = 0U;
    probe_runtime.frame_delivery_ethertype = 0U;
    probe_runtime.frame_delivery_pending = 0U;
    probe_runtime.ipv4_delivery_pid = 0;
    probe_runtime.ipv4_delivery_generation = 0U;
    probe_runtime.ipv4_delivery_pending = 0U;
    probe_runtime.icmp_delivery_pid = 0;
    probe_runtime.icmp_delivery_generation = 0U;
    probe_runtime.icmp_delivery_crc32 = 0U;
    probe_runtime.icmp_delivery_pending = 0U;
    probe_runtime.udp_delivery_pid = 0;
    probe_runtime.udp_delivery_generation = 0U;
    probe_runtime.udp_delivery_pending = 0U;
    probe_runtime.dhcp_delivery_pid = 0;
    probe_runtime.dhcp_delivery_generation = 0U;
    probe_runtime.dhcp_delivery_crc32 = 0U;
    probe_runtime.dhcp_delivery_pending = 0U;
    if (icmp_delivery_clear() != 0 || udp_delivery_clear() != 0) return false;
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
            &probe_runtime.dhcp_context) != 0 ||
        supervisor_protected_dhcp_lease_init(
            &probe_runtime.dhcp_lease) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.dhcp_renewal_authority) != 0 ||
        supervisor_protected_dhcp_renewal_init(
            &probe_runtime.dhcp_renewal) != 0 ||
        supervisor_protected_probe_authority_init(
            &probe_runtime.dhcp_boot_authority) != 0 ||
        supervisor_protected_dhcp_boot_init(
            &probe_runtime.dhcp_boot) != 0)
        return false;
    for (uint32_t slot = 0U; slot < SUPERVISOR_UDP_MAX_BINDINGS; ++slot) {
        supervisor_udp_binding_t previous = {0};
        if (udp_binding_read(&probe_runtime.udp_bindings[slot].binding,
                             &previous) != 0)
            return false;
        supervisor_udp_binding_t cleared = {.generation = previous.generation};
        if (udp_binding_write(&probe_runtime.udp_bindings[slot].binding,
                              &cleared) != 0 ||
            supervisor_protected_probe_authority_init(
                &probe_runtime.udp_bindings[slot].authority) != 0 ||
            supervisor_protected_udp_echo_context_init(
                &probe_runtime.udp_bindings[slot].context) != 0)
            return false;
    }
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
    if (report_type == REIST_REPORT_NETWORK_FRAME) {
        if (value != 0x0800U && value != 0x0806U) return -1;
        uint32_t flags = supervisor_lock();
        bool delivery_matches = probe_runtime.frame_delivery_pending != 0U &&
            probe_runtime.frame_delivery_pid == pid &&
            probe_runtime.frame_delivery_generation == generation &&
            probe_runtime.frame_delivery_ethertype == value;
        bool first_for_generation =
            probe_runtime.frame_handoff_report_pid != pid ||
            probe_runtime.frame_handoff_report_generation != generation;
        if (delivery_matches) {
            probe_runtime.frame_delivery_pid = 0;
            probe_runtime.frame_delivery_generation = 0U;
            probe_runtime.frame_delivery_ethertype = 0U;
            probe_runtime.frame_delivery_pending = 0U;
        }
        if (delivery_matches && first_for_generation) {
            probe_runtime.frame_handoff_report_pid = pid;
            probe_runtime.frame_handoff_report_generation = generation;
        }
        supervisor_unlock(flags);
        if (!delivery_matches) return -1;
        if (first_for_generation)
            printf("REIST_NETWORK FRAME_HANDOFF\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_IPV4) {
        if (value != 1U && value != 17U) return -1;
        uint32_t flags = supervisor_lock();
        bool delivery_matches = probe_runtime.ipv4_delivery_pending != 0U &&
            probe_runtime.ipv4_delivery_pid == pid &&
            probe_runtime.ipv4_delivery_generation == generation;
        bool first_for_generation = probe_runtime.ipv4_report_pid != pid ||
            probe_runtime.ipv4_report_generation != generation;
        if (delivery_matches) {
            probe_runtime.ipv4_delivery_pid = 0;
            probe_runtime.ipv4_delivery_generation = 0U;
            probe_runtime.ipv4_delivery_pending = 0U;
        }
        if (delivery_matches && first_for_generation) {
            probe_runtime.ipv4_report_pid = pid;
            probe_runtime.ipv4_report_generation = generation;
        }
        supervisor_unlock(flags);
        if (!delivery_matches) return -1;
        if (first_for_generation)
            printf("REIST_NETWORK IPV4_PARSED_RING3\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_ICMP) {
        uint32_t flags = supervisor_lock();
        bool delivery_matches = probe_runtime.icmp_delivery_pending != 0U &&
            probe_runtime.icmp_delivery_pid == pid &&
            probe_runtime.icmp_delivery_generation == generation &&
            probe_runtime.icmp_delivery_crc32 == value;
        bool first_for_generation = probe_runtime.icmp_report_pid != pid ||
            probe_runtime.icmp_report_generation != generation;
        if (delivery_matches) {
            probe_runtime.icmp_delivery_pid = 0;
            probe_runtime.icmp_delivery_generation = 0U;
            probe_runtime.icmp_delivery_crc32 = 0U;
            probe_runtime.icmp_delivery_pending = 0U;
        }
        if (delivery_matches && first_for_generation) {
            probe_runtime.icmp_report_pid = pid;
            probe_runtime.icmp_report_generation = generation;
        }
        supervisor_unlock(flags);
        if (!delivery_matches) return -1;
        if (first_for_generation)
            printf("REIST_NETWORK ICMP_PARSED_RING3\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_UDP) {
        if (value == 0U || value > UINT16_MAX) return -1;
        uint32_t flags = supervisor_lock();
        bool delivery_matches = probe_runtime.udp_delivery_pending != 0U &&
            probe_runtime.udp_delivery_pid == pid &&
            probe_runtime.udp_delivery_generation == generation;
        bool first_for_generation = probe_runtime.udp_report_pid != pid ||
            probe_runtime.udp_report_generation != generation;
        if (delivery_matches) {
            probe_runtime.udp_delivery_pid = 0;
            probe_runtime.udp_delivery_generation = 0U;
            probe_runtime.udp_delivery_pending = 0U;
        }
        if (delivery_matches && first_for_generation) {
            probe_runtime.udp_report_pid = pid;
            probe_runtime.udp_report_generation = generation;
        }
        supervisor_unlock(flags);
        if (!delivery_matches) return -1;
        if (first_for_generation)
            printf("REIST_NETWORK UDP_PARSED_RING3\n");
        return 0;
    }
    if (report_type == REIST_REPORT_NETWORK_DHCP) {
        uint32_t flags = supervisor_lock();
        bool delivery_matches = probe_runtime.dhcp_delivery_pending != 0U &&
            probe_runtime.dhcp_delivery_pid == pid &&
            probe_runtime.dhcp_delivery_generation == generation &&
            probe_runtime.dhcp_delivery_crc32 == value;
        bool first_for_generation = probe_runtime.dhcp_report_pid != pid ||
            probe_runtime.dhcp_report_generation != generation;
        if (delivery_matches) {
            probe_runtime.dhcp_delivery_pid = 0;
            probe_runtime.dhcp_delivery_generation = 0U;
            probe_runtime.dhcp_delivery_crc32 = 0U;
            probe_runtime.dhcp_delivery_pending = 0U;
        }
        if (delivery_matches && first_for_generation) {
            probe_runtime.dhcp_report_pid = pid;
            probe_runtime.dhcp_report_generation = generation;
        }
        supervisor_unlock(flags);
        if (!delivery_matches) return -1;
        if (first_for_generation)
            printf("REIST_NETWORK DHCP_PARSED_RING3\n");
        return 0;
    }
    return -1;
}

int supervisor_network_receive_frame(int pid, uint32_t generation,
                                     supervisor_network_frame_t *frame_out) {
    if (frame_out == NULL) return -22;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || pid != control.pid ||
         generation != control.process_generation ||
         !process_identity_alive(pid, generation))) result = -13;
    uint64_t now_ms = pit_monotonic_ms();
    supervisor_icmp_delivery_t icmp_delivery = {0};
    if (result == 0) result = icmp_delivery_read(&icmp_delivery);
    if (result == 0 && icmp_delivery.active != 0U) {
        if (icmp_delivery.process_generation != generation ||
            now_ms >= icmp_delivery.deadline_ms) {
            result = icmp_delivery_clear();
        } else {
            result = -11;
        }
    }
    supervisor_udp_delivery_t udp_delivery = {0};
    if (result == 0) result = udp_delivery_read(&udp_delivery);
    if (result == 0 && udp_delivery.active != 0U) {
        if (udp_delivery.process_generation != generation ||
            now_ms >= udp_delivery.deadline_ms) {
            result = udp_delivery_clear();
        } else {
            result = -11;
        }
    }
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure) (void)supervisor_force_isolate(control.handle);
    if (result != 0) return result;

    *frame_out = (supervisor_network_frame_t){
        .version = SUPERVISOR_NETWORK_FRAME_VERSION,
        .struct_size = sizeof(*frame_out),
    };
    int length = netdev_receive_service_frame(
        frame_out->data, sizeof(frame_out->data));
    if (length == 0) return -11;
    if (length < 0) return SUPERVISOR_EINTEGRITY;
    frame_out->length = (uint32_t)length;
    return 0;
}

int supervisor_network_confirm_frame_delivery(
        int pid, uint32_t generation,
        const supervisor_network_frame_t *frame) {
    if (frame == NULL || frame->version != SUPERVISOR_NETWORK_FRAME_VERSION ||
        frame->struct_size != sizeof(*frame) || frame->length < 14U ||
        frame->length > SUPERVISOR_NETWORK_FRAME_MAX_SIZE ||
        frame->reserved != 0U || frame->padding[0] != 0U ||
        frame->padding[1] != 0U) return -22;
    uint32_t ethertype = ((uint32_t)frame->data[12U] << 8U) |
                         frame->data[13U];
    if (ethertype != 0x0800U && ethertype != 0x0806U) return 0;
    uint32_t frame_crc32 = network_frame_crc32(frame->data, frame->length);

    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool already_reported =
        probe_runtime.frame_handoff_report_pid == pid &&
        probe_runtime.frame_handoff_report_generation == generation;
    bool ipv4_already_reported =
        probe_runtime.ipv4_report_pid == pid &&
        probe_runtime.ipv4_report_generation == generation;
    bool icmp_already_reported = probe_runtime.icmp_report_pid == pid &&
        probe_runtime.icmp_report_generation == generation;
    bool udp_already_reported = probe_runtime.udp_report_pid == pid &&
        probe_runtime.udp_report_generation == generation;
    bool dhcp_already_reported = probe_runtime.dhcp_report_pid == pid &&
        probe_runtime.dhcp_report_generation == generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || pid != control.pid ||
         generation != control.process_generation ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0 && !already_reported &&
        probe_runtime.frame_delivery_pending != 0U) result = -11;
    if (result == 0 && !already_reported) {
        probe_runtime.frame_delivery_pid = pid;
        probe_runtime.frame_delivery_generation = generation;
        probe_runtime.frame_delivery_ethertype = ethertype;
        probe_runtime.frame_delivery_pending = 1U;
    }
    if (result == 0 && ethertype == 0x0800U && !ipv4_already_reported &&
        probe_runtime.ipv4_delivery_pending == 0U) {
        probe_runtime.ipv4_delivery_pid = pid;
        probe_runtime.ipv4_delivery_generation = generation;
        probe_runtime.ipv4_delivery_pending = 1U;
    }
    if (result == 0 && ethertype == 0x0800U && frame->length >= 34U &&
        frame->data[23U] == 1U && !icmp_already_reported) {
        probe_runtime.icmp_delivery_pid = pid;
        probe_runtime.icmp_delivery_generation = generation;
        probe_runtime.icmp_delivery_crc32 = frame_crc32;
        probe_runtime.icmp_delivery_pending = 1U;
    }
    if (result == 0 && ethertype == 0x0800U && frame->length >= 34U &&
        frame->data[23U] == 1U) {
        supervisor_icmp_delivery_t delivery = {0};
        result = icmp_delivery_read(&delivery);
        if (result == 0 && delivery.active != 0U) result = -11;
        if (result == 0) {
            delivery = (supervisor_icmp_delivery_t){
                .active = 1U,
                .process_generation = generation,
                .frame_crc32 = frame_crc32,
                .frame_length = frame->length,
                .deadline_ms = deadline_after(
                    pit_monotonic_ms(), SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS),
            };
            result = icmp_delivery_write(&delivery);
        }
    }
    if (result == 0 && ethertype == 0x0800U && frame->length >= 34U &&
        frame->data[23U] == 17U && !udp_already_reported &&
        probe_runtime.udp_delivery_pending == 0U) {
        probe_runtime.udp_delivery_pid = pid;
        probe_runtime.udp_delivery_generation = generation;
        probe_runtime.udp_delivery_pending = 1U;
    }
    if (result == 0 && network_frame_is_dhcp_reply(frame) &&
        !dhcp_already_reported) {
        probe_runtime.dhcp_delivery_pid = pid;
        probe_runtime.dhcp_delivery_generation = generation;
        probe_runtime.dhcp_delivery_crc32 = frame_crc32;
        probe_runtime.dhcp_delivery_pending = 1U;
    }
    if (result == 0 && ethertype == 0x0800U && frame->length >= 34U &&
        frame->data[23U] == 17U) {
        supervisor_udp_delivery_t delivery = {0};
        result = udp_delivery_read(&delivery);
        if (result == 0 && delivery.active != 0U) result = -11;
        if (result == 0) {
            delivery = (supervisor_udp_delivery_t){
                .active = 1U,
                .process_generation = generation,
                .frame_crc32 = frame_crc32,
                .frame_length = frame->length,
                .deadline_ms = deadline_after(
                    pit_monotonic_ms(), SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS),
            };
            result = udp_delivery_write(&delivery);
        }
    }
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure) (void)supervisor_force_isolate(control.handle);
    return result;
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

static bool icmp_ingress_drop_is_canonical(
        const supervisor_icmp_ingress_t *ingress) {
    if (ingress->reserved != 0U || ingress->source_ip != 0U ||
        ingress->destination_ip != 0U || ingress->identifier != 0U ||
        ingress->sequence != 0U || ingress->data_length != 0U ||
        ingress->reserved_byte != 0U || ingress->reserved_tail != 0U)
        return false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (ingress->source_mac[index] != 0U) return false;
    return true;
}

int supervisor_network_icmp_ingress(
        int pid, uint32_t generation,
        const supervisor_icmp_ingress_t *ingress, const uint8_t *data) {
    if (ingress == NULL ||
        ingress->version != SUPERVISOR_ICMP_INGRESS_VERSION ||
        ingress->struct_size != sizeof(*ingress) ||
        ingress->operation > SUPERVISOR_ICMP_INGRESS_ECHO_REPLY ||
        ingress->reserved != 0U || ingress->reserved_byte != 0U ||
        ingress->reserved_tail != 0U ||
        ingress->data_length > SUPERVISOR_ICMP_ECHO_MAX_DATA ||
        (ingress->data_length != 0U && data == NULL)) return -22;
    bool drop = ingress->operation == SUPERVISOR_ICMP_INGRESS_DROP;
    if (drop && !icmp_ingress_drop_is_canonical(ingress)) return -22;

    uint32_t local_ip = 0U;
    uint8_t local_mac[6] = {0};
    if (!drop) {
        if (ingress->source_ip == 0U || ingress->source_ip == UINT32_MAX ||
            !netstack_get_local_identity(&local_ip, local_mac) ||
            ingress->destination_ip != local_ip) return -22;
        bool nonzero_mac = false;
        for (uint32_t index = 0U; index < 6U; ++index)
            if (ingress->source_mac[index] != 0U) nonzero_mac = true;
        if (!nonzero_mac || (ingress->source_mac[0] & 1U) != 0U) return -22;
        if (ingress->operation == SUPERVISOR_ICMP_INGRESS_ECHO_REPLY &&
            ingress->data_length != 0U) return -22;
    }

    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    supervisor_icmp_delivery_t delivery = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0) result = icmp_delivery_read(&delivery);
    if (result == 0 &&
        (delivery.active == 0U ||
         delivery.process_generation != generation ||
         delivery.frame_crc32 != ingress->frame_crc32)) result = -13;
    if (result == 0 && pit_monotonic_ms() >= delivery.deadline_ms) {
        (void)icmp_delivery_clear();
        result = -110;
    }
    if (result == 0) result = icmp_delivery_clear();
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure && caller_is_service)
        (void)supervisor_force_isolate(control.handle);
    if (result != 0) return result;
    if (drop) return 0;

    if (ingress->operation == SUPERVISOR_ICMP_INGRESS_ECHO_REPLY) {
        (void)netstack_accept_validated_icmp_echo_reply(
            ingress->source_ip, ingress->identifier, ingress->sequence);
        printf("REIST_NETWORK ICMP_REPLY_INGRESS_RING3\n");
        return 0;
    }
    netstack_record_validated_icmp_echo_request();
    if (!supervisor_network_submit_icmp_echo(
            ingress->source_ip, ingress->source_mac, ingress->identifier,
            ingress->sequence, data, ingress->data_length)) return -11;
    printf("REIST_NETWORK ICMP_INGRESS_RING3\n");
    return 0;
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

static bool supervisor_network_submit_dhcp_config_operation(
        uint32_t ip_address, uint32_t netmask, uint32_t gateway,
        uint32_t dns_server, uint32_t lease_seconds, uint32_t operation) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    if (!dhcp_config_valid_values(ip_address, netmask, gateway, dns_server) ||
        lease_seconds < SUPERVISOR_DHCP_LEASE_MIN_SECONDS ||
        lease_seconds > SUPERVISOR_DHCP_LEASE_MAX_SECONDS ||
        operation > SUPERVISOR_DHCP_REBIND)
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
        result = dhcp_context_publish_operation(
            &probe_runtime.dhcp_context, request_id,
            control.process_generation, ip_address, netmask, gateway,
            dns_server, lease_seconds, operation);
    supervisor_unlock(flags);
    if (result != 0) return false;

    ipc_message_t message = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc_message_t),
        .length = 28U,
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
        message.payload[24U + index] =
            (uint8_t)(lease_seconds >> (24U - index * 8U));
    }
    /* Publish the queue marker before the receiver can run and commit. This
     * keeps the externally verified transaction order deterministic without
     * holding the IRQ-off supervisor lock across IPC or serial output. */
    scheduler_preempt_disable();
    result = ipc_send_kernel_to_owner(
        control.pid, control.process_generation, control.endpoint_handle,
        &message);
    if (result == 0)
        printf("REIST_NETWORK DHCP_CONFIG_QUEUED\n");
    scheduler_preempt_enable();
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
    return true;
}

bool supervisor_network_submit_dhcp_config(
        uint32_t ip_address, uint32_t netmask, uint32_t gateway,
        uint32_t dns_server, uint32_t lease_seconds) {
    return supervisor_network_submit_dhcp_config_operation(
        ip_address, netmask, gateway, dns_server, lease_seconds, 0U);
}

static int publish_dhcp_lease_schedule(
        const supervisor_probe_control_t *control,
        const supervisor_dhcp_context_t *context,
        uint32_t lease_interval_ms) {
    if (control == NULL || context == NULL || lease_interval_ms < 4U)
        return -22;
    uint32_t renew_after_ms = lease_interval_ms / 2U;
    uint32_t rebind_after_ms = (lease_interval_ms / 8U) * 7U;
    if (rebind_after_ms <= renew_after_ms ||
        rebind_after_ms >= lease_interval_ms) return -22;
    ipc_message_t schedule = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(schedule),
        .length = 28U,
        .payload = {'N', 'E', 'T', 'L'},
    };
    uint32_t values[6] = {
        context->ip_address, lease_interval_ms, renew_after_ms,
        rebind_after_ms, context->operation, context->request_id,
    };
    for (uint32_t value = 0U; value < 6U; ++value)
        for (uint32_t byte = 0U; byte < 4U; ++byte)
            schedule.payload[4U + value * 4U + byte] =
                (uint8_t)(values[value] >> (24U - byte * 8U));
    return ipc_send_kernel_to_owner(
        control->pid, control->process_generation, control->endpoint_handle,
        &schedule);
}

int supervisor_network_commit_dhcp_config(
        int pid, uint32_t generation,
        const supervisor_dhcp_commit_t *commit) {
    if (commit == NULL || commit->version != SUPERVISOR_DHCP_COMMIT_VERSION ||
        commit->struct_size < sizeof(*commit) || commit->request_id == 0U ||
        commit->reserved != 0U) return -22;
    uint64_t now_ms = pit_monotonic_ms();
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_dhcp_context_t context = {0};
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
            &probe_runtime.dhcp_authority, now_ms, generation,
            &consumed_id);
    if (result == 0 && consumed_id != commit->request_id) result = -13;
    uint64_t lease_interval_ms = (uint64_t)context.lease_seconds * 1000U;
#ifdef REIST_DHCP_LEASE_TEST_MS
    if (lease_interval_ms > REIST_DHCP_LEASE_TEST_MS)
        lease_interval_ms = REIST_DHCP_LEASE_TEST_MS;
#endif
#ifdef REIST_DHCP_RENEW_TEST_MS
    if (lease_interval_ms > REIST_DHCP_RENEW_TEST_MS)
        lease_interval_ms = REIST_DHCP_RENEW_TEST_MS;
#endif
    uint64_t lease_deadline = UINT64_MAX - now_ms < lease_interval_ms
        ? UINT64_MAX : now_ms + lease_interval_ms;
    if (result == 0)
        result = supervisor_protected_dhcp_lease_publish(
            &probe_runtime.dhcp_lease, generation, context.ip_address,
            context.lease_seconds, lease_deadline);
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
        flags = supervisor_lock();
        (void)supervisor_protected_dhcp_lease_clear(
            &probe_runtime.dhcp_lease);
        supervisor_unlock(flags);
        printf("REIST_NETWORK DHCP_CONFIG_REJECTED -5\n");
        return -5;
    }
#ifndef REIST_DHCP_LEASE_TEST_MS
    result = publish_dhcp_lease_schedule(
        &control, &context, (uint32_t)lease_interval_ms);
    if (result != 0) {
        flags = supervisor_lock();
        (void)supervisor_protected_dhcp_lease_clear(
            &probe_runtime.dhcp_lease);
        supervisor_unlock(flags);
        (void)netstack_clear_supervised_dhcp(context.ip_address);
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        printf("REIST_NETWORK DHCP_CONFIG_REJECTED %d\n", result);
        return result;
    }
#endif
    if (context.operation == SUPERVISOR_DHCP_RENEW)
        printf("REIST_NETWORK DHCP_RENEWED\n");
    else if (context.operation == SUPERVISOR_DHCP_REBIND)
        printf("REIST_NETWORK DHCP_REBOUND\n");
    else
        printf("REIST_NETWORK DHCP_CONFIG_MEDIATED\n");
    return 0;
}

int supervisor_network_request_dhcp_renewal(
        int pid, uint32_t generation,
        const supervisor_dhcp_renew_request_t *request) {
    if (request == NULL ||
        request->version != SUPERVISOR_DHCP_RENEW_REQUEST_VERSION ||
        request->struct_size < sizeof(*request) ||
        (request->operation != SUPERVISOR_DHCP_RENEW &&
         request->operation != SUPERVISOR_DHCP_REBIND) ||
        request->expected_ip == 0U || request->expected_ip == UINT32_MAX)
        return -22;
    uint64_t now_ms = pit_monotonic_ms();
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    supervisor_dhcp_lease_t lease = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = supervisor_protected_dhcp_lease_snapshot(
            &probe_runtime.dhcp_lease, &lease);
    if (result == 0 &&
        (lease.process_generation != generation ||
         lease.ip_address != request->expected_ip ||
         now_ms >= lease.deadline_ms)) result = -13;
    int expiry = 0;
    if (result == 0)
        expiry = supervisor_protected_probe_authority_expire_epoch(
            &probe_runtime.dhcp_renewal_authority, now_ms, generation);
    if (result == 0 && expiry < 0) result = expiry;
    if (result == 0 && expiry == 1)
        result = supervisor_protected_dhcp_renewal_clear(
            &probe_runtime.dhcp_renewal);
    uint32_t transaction_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_begin_epoch(
            &probe_runtime.dhcp_renewal_authority, now_ms,
            SUPERVISOR_DHCP_RENEW_TIMEOUT_MS, generation,
            &transaction_id);
    if (result == 0)
        result = supervisor_protected_dhcp_renewal_publish(
            &probe_runtime.dhcp_renewal, request->operation, generation,
            transaction_id, lease.ip_address,
            deadline_after(now_ms, SUPERVISOR_DHCP_RENEW_TIMEOUT_MS));
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (result != 0) {
        if (integrity_failure && caller_is_service)
            (void)supervisor_force_isolate(control.handle);
        return result;
    }
    if (!netstack_send_supervised_dhcp_request(
            transaction_id, lease.ip_address,
            request->operation == SUPERVISOR_DHCP_REBIND)) {
        flags = supervisor_lock();
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.dhcp_renewal_authority);
        (void)supervisor_protected_dhcp_renewal_clear(
            &probe_runtime.dhcp_renewal);
        supervisor_unlock(flags);
        return -5;
    }
    printf(request->operation == SUPERVISOR_DHCP_REBIND
        ? "REIST_NETWORK DHCP_REBIND_REQUESTED\n"
        : "REIST_NETWORK DHCP_RENEW_REQUESTED\n");
    return 0;
}

bool supervisor_network_accept_dhcp_renewal(
        uint32_t transaction_id, uint32_t ip_address, uint32_t netmask,
        uint32_t gateway, uint32_t dns_server, uint32_t lease_seconds) {
    if (transaction_id == 0U ||
        !dhcp_config_valid_values(ip_address, netmask, gateway, dns_server) ||
        lease_seconds < SUPERVISOR_DHCP_LEASE_MIN_SECONDS ||
        lease_seconds > SUPERVISOR_DHCP_LEASE_MAX_SECONDS) return false;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    supervisor_dhcp_renewal_t renewal = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result == 0)
        result = supervisor_protected_dhcp_renewal_snapshot(
            &probe_runtime.dhcp_renewal, &renewal);
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || renewal.active == 0U ||
         renewal.transaction_id != transaction_id ||
         renewal.process_generation != control.process_generation ||
         renewal.ip_address != ip_address ||
         !process_identity_alive(control.pid, control.process_generation)))
        result = -13;
    uint32_t consumed_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.dhcp_renewal_authority, pit_monotonic_ms(),
            control.process_generation, &consumed_id);
    if (result == 0 && consumed_id != transaction_id) result = -13;
    int clear_result = supervisor_protected_dhcp_renewal_clear(
        &probe_runtime.dhcp_renewal);
    int cancel_result = supervisor_protected_probe_authority_cancel(
        &probe_runtime.dhcp_renewal_authority);
    if (result == 0 && clear_result != 0) result = clear_result;
    if (result == 0 && cancel_result != 0) result = cancel_result;
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (result != 0) {
        if (integrity_failure && control.active != 0U)
            (void)supervisor_force_isolate(control.handle);
        return false;
    }
    return supervisor_network_submit_dhcp_config_operation(
        ip_address, netmask, gateway, dns_server, lease_seconds,
        renewal.operation);
}

bool supervisor_network_reject_dhcp_renewal(uint32_t transaction_id) {
    if (transaction_id == 0U) return false;
    uint32_t flags = supervisor_lock();
    supervisor_dhcp_renewal_t renewal = {0};
    int result = supervisor_protected_dhcp_renewal_snapshot(
        &probe_runtime.dhcp_renewal, &renewal);
    if (result == 0 &&
        (renewal.active == 0U || renewal.transaction_id != transaction_id))
        result = -13;
    if (result == 0)
        result = supervisor_protected_dhcp_renewal_clear(
            &probe_runtime.dhcp_renewal);
    if (result == 0)
        result = supervisor_protected_dhcp_lease_clear(
            &probe_runtime.dhcp_lease);
    (void)supervisor_protected_probe_authority_cancel(
        &probe_runtime.dhcp_renewal_authority);
    supervisor_unlock(flags);
    if (result != 0) return false;
    (void)netstack_clear_supervised_dhcp(renewal.ip_address);
    printf("REIST_NETWORK DHCP_RENEWAL_REJECTED\n");
    return true;
}

static uint32_t dhcp_transaction_wire_value(uint32_t value) {
    return ((value & 0x000000FFU) << 24U) |
           ((value & 0x0000FF00U) << 8U) |
           ((value & 0x00FF0000U) >> 8U) |
           ((value & 0xFF000000U) >> 24U);
}

int supervisor_network_start_dhcp_boot(
        int pid, uint32_t generation,
        const supervisor_dhcp_boot_start_t *request) {
    if (request == NULL ||
        request->version != SUPERVISOR_DHCP_BOOT_START_VERSION ||
        request->struct_size != sizeof(*request)) return -22;
    if (netstack_is_configured()) return -17;
    uint64_t now_ms = pit_monotonic_ms();
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    supervisor_dhcp_boot_t boot = {0};
    supervisor_dhcp_renewal_t renewal = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = supervisor_protected_dhcp_boot_snapshot(
            &probe_runtime.dhcp_boot, &boot);
    if (result == 0 && boot.active != 0U && now_ms < boot.deadline_ms)
        result = -11;
    if (result == 0 && boot.active != 0U) {
        result = supervisor_protected_dhcp_boot_clear(
            &probe_runtime.dhcp_boot);
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.dhcp_boot_authority);
    }
    if (result == 0)
        result = supervisor_protected_dhcp_renewal_snapshot(
            &probe_runtime.dhcp_renewal, &renewal);
    if (result == 0 && renewal.active != 0U) result = -11;
    uint32_t transaction_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_begin_epoch(
            &probe_runtime.dhcp_boot_authority, now_ms,
            SUPERVISOR_DHCP_BOOT_TIMEOUT_MS, generation, &transaction_id);
    if (result == 0)
        result = supervisor_protected_dhcp_boot_publish(
            &probe_runtime.dhcp_boot, SUPERVISOR_DHCP_BOOT_DISCOVER_SENT,
            generation, transaction_id, 0U, 0U,
            deadline_after(now_ms, SUPERVISOR_DHCP_BOOT_TIMEOUT_MS));
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure && caller_is_service)
        (void)supervisor_force_isolate(control.handle);
    if (result != 0) return result;
    if (!netstack_send_supervised_dhcp_discover(transaction_id)) {
        flags = supervisor_lock();
        (void)supervisor_protected_dhcp_boot_clear(&probe_runtime.dhcp_boot);
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.dhcp_boot_authority);
        supervisor_unlock(flags);
        return -5;
    }
    printf("REIST_NETWORK DHCP_BOOT_DISCOVER_RING3\n");
    return 0;
}

int supervisor_network_dhcp_ingress(
        int pid, uint32_t generation,
        const supervisor_dhcp_ingress_t *ingress) {
    if (ingress == NULL ||
        ingress->version != SUPERVISOR_DHCP_INGRESS_VERSION ||
        ingress->struct_size != sizeof(*ingress) ||
        ingress->frame_crc32 == 0U || ingress->transaction_id == 0U ||
        ingress->checksum_present > 1U ||
        (ingress->option_flags & ~0x3FU) != 0U ||
        (ingress->option_flags & SUPERVISOR_DHCP_OPTION_MESSAGE_TYPE) == 0U ||
        (ingress->message_type != SUPERVISOR_DHCP_MESSAGE_OFFER &&
         ingress->message_type != SUPERVISOR_DHCP_MESSAGE_ACK &&
         ingress->message_type != SUPERVISOR_DHCP_MESSAGE_NAK)) return -22;
    const uint32_t required_ack_options =
        SUPERVISOR_DHCP_OPTION_NETMASK |
        SUPERVISOR_DHCP_OPTION_GATEWAY |
        SUPERVISOR_DHCP_OPTION_DNS |
        SUPERVISOR_DHCP_OPTION_LEASE;
    if (ingress->message_type == SUPERVISOR_DHCP_MESSAGE_ACK &&
        (ingress->option_flags & required_ack_options) != required_ack_options)
        return -22;
    uint8_t local_mac[6] = {0};
    if (!netdev_get_mac_address(local_mac)) return -13;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (ingress->client_mac[index] != local_mac[index]) return -13;

    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    supervisor_dhcp_boot_t boot = {0};
    supervisor_dhcp_renewal_t renewal = {0};
    supervisor_udp_delivery_t delivery = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = supervisor_protected_dhcp_boot_snapshot(
            &probe_runtime.dhcp_boot, &boot);
    if (result == 0)
        result = supervisor_protected_dhcp_renewal_snapshot(
            &probe_runtime.dhcp_renewal, &renewal);
    bool boot_active = result == 0 && boot.active != 0U;
    bool renewal_active = result == 0 && renewal.active != 0U;
    if (result == 0 && !boot_active && !renewal_active) result = -11;
    if (result == 0 && boot_active &&
        (boot.process_generation != generation ||
         boot.transaction_id != ingress->transaction_id ||
         pit_monotonic_ms() >= boot.deadline_ms)) result = -13;
    if (result == 0 && renewal_active && !boot_active &&
        (renewal.process_generation != generation ||
         renewal.transaction_id == 0U ||
         dhcp_transaction_wire_value(renewal.transaction_id) !=
             ingress->transaction_id)) result = -13;
    if (result == 0) result = udp_delivery_read(&delivery);
    if (result == 0 &&
        (delivery.active == 0U ||
         delivery.process_generation != generation ||
         delivery.frame_crc32 != ingress->frame_crc32)) result = -13;
    if (result == 0 && pit_monotonic_ms() >= delivery.deadline_ms) {
        (void)udp_delivery_clear();
        result = -110;
    }
    bool boot_offer = result == 0 && boot_active &&
        ingress->message_type == SUPERVISOR_DHCP_MESSAGE_OFFER;
    bool boot_complete = result == 0 && boot_active && !boot_offer;
    if (result == 0 && boot_offer &&
        (boot.phase != SUPERVISOR_DHCP_BOOT_DISCOVER_SENT ||
         ingress->offered_ip == 0U || ingress->offered_ip == UINT32_MAX ||
         ingress->server_id == 0U || ingress->server_id == UINT32_MAX ||
         (ingress->option_flags & SUPERVISOR_DHCP_OPTION_SERVER_ID) == 0U))
        result = -22;
    if (result == 0 && boot_complete &&
        (boot.phase != SUPERVISOR_DHCP_BOOT_REQUEST_SENT ||
         (ingress->message_type == SUPERVISOR_DHCP_MESSAGE_ACK &&
          (ingress->offered_ip != boot.offered_ip ||
           ((ingress->option_flags & SUPERVISOR_DHCP_OPTION_SERVER_ID) != 0U &&
            ingress->server_id != boot.server_id))))) result = -13;
    if (result == 0 && boot_active &&
        ingress->message_type == SUPERVISOR_DHCP_MESSAGE_ACK &&
        (!dhcp_config_valid_values(
             ingress->offered_ip, ingress->netmask, ingress->gateway,
             ingress->dns_server) ||
         ingress->lease_seconds < SUPERVISOR_DHCP_LEASE_MIN_SECONDS ||
         ingress->lease_seconds > SUPERVISOR_DHCP_LEASE_MAX_SECONDS))
        result = -22;
    if (result == 0 && renewal_active && !boot_active &&
        ingress->message_type == SUPERVISOR_DHCP_MESSAGE_OFFER)
        result = -11;
    if (result == 0 && renewal_active && !boot_active &&
        ingress->message_type == SUPERVISOR_DHCP_MESSAGE_ACK &&
        (!dhcp_config_valid_values(
             ingress->offered_ip != 0U ? ingress->offered_ip :
                                         renewal.ip_address,
             ingress->netmask, ingress->gateway, ingress->dns_server) ||
         ingress->lease_seconds < SUPERVISOR_DHCP_LEASE_MIN_SECONDS ||
         ingress->lease_seconds > SUPERVISOR_DHCP_LEASE_MAX_SECONDS))
        result = -22;
    if (result == 0) result = udp_delivery_clear();
    if (result == 0 && boot_offer)
        result = supervisor_protected_dhcp_boot_publish(
            &probe_runtime.dhcp_boot, SUPERVISOR_DHCP_BOOT_REQUEST_SENT,
            generation, boot.transaction_id, ingress->offered_ip,
            ingress->server_id, boot.deadline_ms);
    if (result == 0 && boot_complete) {
        result = supervisor_protected_dhcp_boot_clear(
            &probe_runtime.dhcp_boot);
        if (result == 0)
            result = supervisor_protected_probe_authority_cancel(
                &probe_runtime.dhcp_boot_authority);
    }
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure && caller_is_service)
        (void)supervisor_force_isolate(control.handle);
    if (result != 0) return result;

    if (boot_offer) {
        if (!netstack_send_supervised_dhcp_select(
                boot.transaction_id, ingress->offered_ip,
                ingress->server_id)) {
            flags = supervisor_lock();
            (void)supervisor_protected_dhcp_boot_clear(
                &probe_runtime.dhcp_boot);
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.dhcp_boot_authority);
            supervisor_unlock(flags);
            (void)netstack_finish_supervised_dhcp_request(
                boot.transaction_id);
            return -5;
        }
        printf("REIST_NETWORK DHCP_BOOT_OFFER_RING3\n");
        return 0;
    }
    if (boot_complete) {
        if (!netstack_finish_supervised_dhcp_request(boot.transaction_id))
            return -13;
        if (ingress->message_type == SUPERVISOR_DHCP_MESSAGE_NAK) {
            printf("REIST_NETWORK DHCP_BOOT_REJECTED\n");
            return 0;
        }
        printf("REIST_NETWORK DHCP_BOOT_ACK_RING3\n");
        return supervisor_network_submit_dhcp_config_operation(
            ingress->offered_ip, ingress->netmask, ingress->gateway,
            ingress->dns_server, ingress->lease_seconds, 0U) ? 0 : -13;
    }
    if (!netstack_finish_supervised_dhcp_request(renewal.transaction_id))
        return -13;

    if (ingress->message_type == SUPERVISOR_DHCP_MESSAGE_NAK)
        return supervisor_network_reject_dhcp_renewal(
            renewal.transaction_id) ? 0 : -13;
    uint32_t ip_address = ingress->offered_ip != 0U
        ? ingress->offered_ip : renewal.ip_address;
    if (ip_address != renewal.ip_address ||
        !supervisor_network_accept_dhcp_renewal(
            renewal.transaction_id, ip_address, ingress->netmask,
            ingress->gateway, ingress->dns_server,
            ingress->lease_seconds)) return -13;
    printf("REIST_NETWORK DHCP_RENEW_INGRESS_RING3\n");
    return 0;
}

int supervisor_network_udp_bind(
        int pid, uint32_t generation,
        const supervisor_udp_bind_request_t *request,
        supervisor_udp_binding_handle_t *handle_out) {
    if (request == NULL || handle_out == NULL ||
        request->version != SUPERVISOR_UDP_BIND_REQUEST_VERSION ||
        request->struct_size < sizeof(*request) ||
        request->port < SUPERVISOR_UDP_BINDING_MIN_PORT ||
        request->max_data != SUPERVISOR_UDP_ECHO_MAX_DATA ||
        request->reserved != 0U) return -22;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || control.launch_count < 4U ||
         !caller_is_service || !process_identity_alive(pid, generation)))
        result = -13;
    uint32_t available = SUPERVISOR_UDP_MAX_BINDINGS;
    supervisor_udp_binding_t bindings[SUPERVISOR_UDP_MAX_BINDINGS];
    for (uint32_t slot = 0U;
         result == 0 && slot < SUPERVISOR_UDP_MAX_BINDINGS; ++slot) {
        result = udp_binding_read(&probe_runtime.udp_bindings[slot].binding,
                                  &bindings[slot]);
        if (result != 0) break;
        if (bindings[slot].active != 0U &&
            bindings[slot].port == request->port) {
            result = bindings[slot].process_generation == generation
                ? -17 : -98;
            break;
        }
        if (available == SUPERVISOR_UDP_MAX_BINDINGS &&
            bindings[slot].active == 0U &&
            bindings[slot].generation < 0xFFFFFFU)
            available = slot;
    }
    if (result == 0 && available == SUPERVISOR_UDP_MAX_BINDINGS)
        result = -28;
    if (result == 0) {
        supervisor_udp_binding_t binding = {
            .active = 1U,
            .generation = bindings[available].generation + 1U,
            .process_generation = generation,
            .port = request->port,
        };
        result = supervisor_protected_probe_authority_init(
            &probe_runtime.udp_bindings[available].authority);
        if (result == 0)
            result = supervisor_protected_udp_echo_context_init(
                &probe_runtime.udp_bindings[available].context);
        if (result == 0)
            result = udp_binding_write(
                &probe_runtime.udp_bindings[available].binding, &binding);
        if (result == 0)
            *handle_out = udp_binding_handle(available, binding.generation);
    }
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure && caller_is_service)
        (void)supervisor_force_isolate(control.handle);
    if (result == 0)
        printf("REIST_NETWORK UDP_BOUND %u\n", (uint32_t)request->port);
    return result;
}

int supervisor_network_udp_unbind(
        int pid, uint32_t generation,
        supervisor_udp_binding_handle_t handle) {
    uint32_t slot = 0U;
    uint32_t handle_generation = 0U;
    if (!udp_binding_decode(handle, &slot, &handle_generation)) return -9;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_udp_binding_t binding;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 && (!caller_is_service || control.active == 0U ||
                        !process_identity_alive(pid, generation)))
        result = -13;
    if (result == 0)
        result = udp_binding_read(&probe_runtime.udp_bindings[slot].binding,
                                  &binding);
    if (result == 0 &&
        (binding.active == 0U || binding.generation != handle_generation ||
         binding.process_generation != generation)) result = -9;
    if (result == 0) {
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.udp_bindings[slot].authority);
        (void)supervisor_protected_udp_echo_context_clear(
            &probe_runtime.udp_bindings[slot].context);
        binding.active = 0U;
        binding.process_generation = 0U;
        binding.port = 0U;
        result = udp_binding_write(
            &probe_runtime.udp_bindings[slot].binding, &binding);
    }
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure && caller_is_service)
        (void)supervisor_force_isolate(control.handle);
    return result;
}

static bool udp_ingress_drop_is_canonical(
        const supervisor_udp_ingress_t *ingress) {
    if (ingress->binding != 0U || ingress->request_id != 0U ||
        ingress->source_ip != 0U || ingress->destination_ip != 0U ||
        ingress->source_port != 0U || ingress->destination_port != 0U ||
        ingress->data_length != 0U) return false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (ingress->source_mac[index] != 0U) return false;
    return true;
}

int supervisor_network_udp_ingress(
        int pid, uint32_t generation,
        const supervisor_udp_ingress_t *ingress, const uint8_t *data,
        uint32_t *request_id_out) {
    if (ingress == NULL || request_id_out == NULL ||
        ingress->version != SUPERVISOR_UDP_INGRESS_VERSION ||
        ingress->struct_size != sizeof(*ingress) ||
        ingress->request_id != 0U ||
        ingress->data_length > SUPERVISOR_UDP_ECHO_MAX_DATA ||
        (ingress->data_length != 0U && data == NULL)) return -22;
    bool drop = ingress->binding == 0U;
    if (drop && !udp_ingress_drop_is_canonical(ingress)) return -22;

    uint32_t binding_slot = 0U;
    uint32_t binding_generation = 0U;
    uint32_t local_ip = 0U;
    uint8_t local_mac[6] = {0};
    if (!drop) {
        if (!udp_binding_decode(ingress->binding, &binding_slot,
                                &binding_generation) ||
            ingress->source_ip == 0U ||
            ingress->source_ip == UINT32_MAX ||
            ingress->source_port == 0U ||
            ingress->destination_port < SUPERVISOR_UDP_BINDING_MIN_PORT ||
            !netstack_get_local_identity(&local_ip, local_mac) ||
            (ingress->destination_ip != local_ip &&
             ingress->destination_ip != UINT32_MAX)) return -22;
        bool nonzero_mac = false;
        for (uint32_t index = 0U; index < 6U; ++index)
            if (ingress->source_mac[index] != 0U) nonzero_mac = true;
        if (!nonzero_mac || (ingress->source_mac[0] & 1U) != 0U) return -22;
    }

    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    supervisor_udp_delivery_t delivery = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0) result = udp_delivery_read(&delivery);
    if (result == 0 &&
        (delivery.active == 0U ||
         delivery.process_generation != generation ||
         delivery.frame_crc32 != ingress->frame_crc32)) result = -13;
    if (result == 0 && pit_monotonic_ms() >= delivery.deadline_ms) {
        (void)udp_delivery_clear();
        result = -110;
    }
    if (result == 0 && drop) result = udp_delivery_clear();

    supervisor_udp_binding_t binding = {0};
    uint32_t request_id = 0U;
    if (result == 0 && !drop)
        result = udp_binding_read(
            &probe_runtime.udp_bindings[binding_slot].binding, &binding);
    if (result == 0 && !drop &&
        (binding.active == 0U ||
         binding.generation != binding_generation ||
         binding.process_generation != generation ||
         binding.port != ingress->destination_port)) result = -9;
    if (result == 0 && !drop)
        result = supervisor_protected_probe_authority_begin_epoch(
            &probe_runtime.udp_bindings[binding_slot].authority,
            pit_monotonic_ms(), SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS,
            generation, &request_id);
    if (result == 0 && !drop)
        result = supervisor_protected_udp_echo_context_publish(
            &probe_runtime.udp_bindings[binding_slot].context, request_id,
            generation, ingress->source_ip, ingress->source_mac,
            ingress->source_port, ingress->destination_port, data,
            ingress->data_length);
    if (result == 0 && !drop) result = udp_delivery_clear();
    if (result != 0 && !drop && request_id != 0U) {
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.udp_bindings[binding_slot].authority);
        (void)supervisor_protected_udp_echo_context_clear(
            &probe_runtime.udp_bindings[binding_slot].context);
    }
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure && caller_is_service)
        (void)supervisor_force_isolate(control.handle);
    if (result != 0) return result;
    *request_id_out = request_id;
    if (!drop) {
        printf("REIST_NETWORK UDP_INGRESS_RING3\n");
        printf(ingress->destination_port == SUPERVISOR_UDP_ECHO_PORT
            ? "REIST_NETWORK UDP_ECHO_QUEUED\n"
            : "REIST_NETWORK UDP_DATAGRAM_QUEUED\n");
    }
    return 0;
}

int supervisor_network_cancel_udp_ingress(
        int pid, uint32_t generation,
        supervisor_udp_binding_handle_t binding_handle,
        uint32_t request_id) {
    uint32_t binding_slot = 0U;
    uint32_t binding_generation = 0U;
    if (request_id == 0U ||
        !udp_binding_decode(binding_handle, &binding_slot,
                            &binding_generation)) return -9;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    supervisor_udp_binding_t binding = {0};
    supervisor_udp_echo_context_t context = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result == 0 &&
        (pid != control.pid || generation != control.process_generation))
        result = -13;
    if (result == 0)
        result = udp_binding_read(
            &probe_runtime.udp_bindings[binding_slot].binding, &binding);
    if (result == 0 &&
        (binding.active == 0U || binding.generation != binding_generation ||
         binding.process_generation != generation)) result = -9;
    if (result == 0)
        result = supervisor_protected_udp_echo_context_snapshot(
            &probe_runtime.udp_bindings[binding_slot].context, &context);
    if (result == 0 &&
        (context.request_id != request_id ||
         context.transaction_epoch != generation)) result = -13;
    if (result == 0)
        result = supervisor_protected_probe_authority_cancel(
            &probe_runtime.udp_bindings[binding_slot].authority);
    if (result == 0)
        result = supervisor_protected_udp_echo_context_clear(
            &probe_runtime.udp_bindings[binding_slot].context);
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (integrity_failure)
        (void)supervisor_force_isolate(control.handle);
    return result;
}

int supervisor_network_send_udp_reply(
        int pid, uint32_t generation,
        const supervisor_udp_reply_t *reply) {
    if (reply == NULL ||
        reply->version != SUPERVISOR_UDP_REPLY_VERSION ||
        reply->struct_size < sizeof(*reply) || reply->request_id == 0U)
        return -22;
    uint32_t binding_slot = 0U;
    uint32_t binding_generation = 0U;
    if (!udp_binding_decode(reply->binding, &binding_slot,
                            &binding_generation)) return -9;
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    supervisor_udp_binding_t binding;
    supervisor_udp_echo_context_t context;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool caller_is_service = result == 0 && pid == control.pid &&
                             generation == control.process_generation;
    if (result == 0 &&
        (control.active == 0U || control.fenced != 0U ||
         control.healthy == 0U || !caller_is_service ||
         !process_identity_alive(pid, generation))) result = -13;
    if (result == 0)
        result = udp_binding_read(
            &probe_runtime.udp_bindings[binding_slot].binding, &binding);
    if (result == 0 &&
        (binding.active == 0U || binding.generation != binding_generation ||
         binding.process_generation != generation)) result = -9;
    if (result == 0)
        result = supervisor_protected_udp_echo_context_snapshot(
            &probe_runtime.udp_bindings[binding_slot].context, &context);
    if (result == 0 &&
        (context.request_id != reply->request_id ||
         context.transaction_epoch != generation ||
         context.destination_port != binding.port)) result = -13;
    uint32_t consumed_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.udp_bindings[binding_slot].authority,
            pit_monotonic_ms(), generation,
            &consumed_id);
    if (result == 0 && consumed_id != reply->request_id) result = -13;
    int cleanup_result = supervisor_protected_udp_echo_context_clear(
        &probe_runtime.udp_bindings[binding_slot].context);
    int cancel_result = supervisor_protected_probe_authority_cancel(
        &probe_runtime.udp_bindings[binding_slot].authority);
    if (result == 0 && cleanup_result != 0) result = cleanup_result;
    if (result == 0 && cancel_result != 0) result = cancel_result;
    bool integrity_failure = result == SUPERVISOR_EINTEGRITY;
    supervisor_unlock(flags);
    if (result != 0) {
        if (integrity_failure && caller_is_service)
            (void)supervisor_force_isolate(control.handle);
        if (caller_is_service)
            printf("REIST_NETWORK UDP_REPLY_REJECTED %d\n", result);
        return result;
    }
    if (!netstack_send_supervised_udp_reply(
            context.source_ip, context.source_mac, context.destination_port,
            context.source_port, context.data, context.data_length)) {
        printf("REIST_NETWORK UDP_REPLY_REJECTED -5\n");
        return -5;
    }
    printf(context.destination_port == SUPERVISOR_UDP_ECHO_PORT
        ? "REIST_NETWORK UDP_ECHO_MEDIATED\n"
        : "REIST_NETWORK UDP_DATAGRAM_MEDIATED\n");
    return 0;
}

int supervisor_network_send_udp_echo_reply(
        int pid, uint32_t generation,
        const supervisor_udp_echo_reply_t *reply) {
    if (reply == NULL ||
        reply->version != SUPERVISOR_UDP_ECHO_REPLY_VERSION ||
        reply->struct_size < sizeof(*reply) || reply->request_id == 0U ||
        reply->reserved != 0U) return -22;
    uint32_t flags = supervisor_lock();
    supervisor_udp_binding_handle_t binding_handle = 0U;
    for (uint32_t slot = 0U; slot < SUPERVISOR_UDP_MAX_BINDINGS; ++slot) {
        supervisor_udp_binding_t binding;
        if (udp_binding_read(&probe_runtime.udp_bindings[slot].binding,
                             &binding) != 0) continue;
        if (binding.active != 0U && binding.process_generation == generation &&
            binding.port == SUPERVISOR_UDP_ECHO_PORT) {
            binding_handle = udp_binding_handle(slot, binding.generation);
            break;
        }
    }
    supervisor_unlock(flags);
    if (binding_handle == 0U) return -9;
    supervisor_udp_reply_t generic = {
        .version = SUPERVISOR_UDP_REPLY_VERSION,
        .struct_size = sizeof(generic),
        .binding = binding_handle,
        .request_id = reply->request_id,
    };
    return supervisor_network_send_udp_reply(pid, generation, &generic);
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
        supervisor_dhcp_lease_t lease = {0};
        int lease_result = supervisor_protected_dhcp_lease_snapshot(
            &probe_runtime.dhcp_lease, &lease);
        bool lease_integrity_failure = lease_result != 0;
        bool lease_expired = lease_result == 0 && lease.ip_address != 0U &&
            now_ms >= lease.deadline_ms;
        if (lease_expired) {
            lease_result = supervisor_protected_dhcp_lease_clear(
                &probe_runtime.dhcp_lease);
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.dhcp_renewal_authority);
            (void)supervisor_protected_dhcp_renewal_clear(
                &probe_runtime.dhcp_renewal);
            lease_integrity_failure = lease_result != 0;
        }
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
        int renewal_expiry = supervisor_protected_probe_authority_expire_epoch(
            &probe_runtime.dhcp_renewal_authority, now_ms,
            control.process_generation);
        int boot_expiry = supervisor_protected_probe_authority_expire_epoch(
            &probe_runtime.dhcp_boot_authority, now_ms,
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
        if (renewal_expiry == 1)
            (void)supervisor_protected_dhcp_renewal_clear(
                &probe_runtime.dhcp_renewal);
        if (boot_expiry == 1)
            (void)supervisor_protected_dhcp_boot_clear(
                &probe_runtime.dhcp_boot);
        int udp_expiry_state = 0;
        for (uint32_t slot = 0U; slot < SUPERVISOR_UDP_MAX_BINDINGS; ++slot) {
            int udp_expiry =
                supervisor_protected_probe_authority_expire_epoch(
                    &probe_runtime.udp_bindings[slot].authority, now_ms,
                    control.process_generation);
            if (udp_expiry == 1)
                (void)supervisor_protected_udp_echo_context_clear(
                    &probe_runtime.udp_bindings[slot].context);
            if (udp_expiry < 0)
                udp_expiry_state = udp_expiry;
            else if (udp_expiry == 1 && udp_expiry_state == 0)
                udp_expiry_state = 1;
        }
        supervisor_unlock(transaction_flags);
        if (lease_integrity_failure) {
            (void)netstack_clear_supervised_dhcp(0U);
            if (control.active != 0U)
                (void)supervisor_force_isolate(control.handle);
            else
                output_fence_all();
        } else if (lease_expired) {
            (void)netstack_clear_supervised_dhcp(lease.ip_address);
            network_degradation_record(
                SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
            printf("\nREIST_NETWORK DHCP_LEASE_EXPIRED\n");
        }
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
        if (renewal_expiry < 0 && control.active != 0U)
            (void)supervisor_force_isolate(control.handle);
        if (boot_expiry == 1) {
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
        } else if (boot_expiry < 0 && control.active != 0U) {
            (void)supervisor_force_isolate(control.handle);
        }
        if (udp_expiry_state == 1) {
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
        } else if (udp_expiry_state < 0 && control.active != 0U) {
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
