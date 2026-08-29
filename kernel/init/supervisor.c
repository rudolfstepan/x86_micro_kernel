/**
 * @file kernel/init/supervisor.c
 * @brief Überwacht isolierte Dienste und vermittelt sicherheitsrelevante Ausgaben.
 *
 * Layer: Ring-0 supervision and recovery control.
 * Contract: Dienstzustand, Autoritäten und Recovery-Kontexte sind an eine
 *           Generation gebunden und werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: Überwachung, Restart-Budgets, Queues und Netzwerktransaktionen sind
 *         fest begrenzt; ungültiger oder korrupter Zustand führt fail-closed
 *         zu Fence, Degradation oder kontrollierter Recovery.
 */
#include "include/kernel/supervisor.h"

#include "include/kernel/critical_object.h"
#ifndef REIST_HOST_TEST
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/smp.h"
#include "include/kernel/component_control.h"
#include "include/kernel/device_domain.h"
#include "include/kernel/panic.h"
#include "include/kernel/output_fence.h"
#include "include/kernel/storage_service.h"
#include "drivers/net/netstack.h"
#include "drivers/net/netdev.h"
#include "drivers/char/serial.h"
#include "drivers/video/display_control.h"
#include "drivers/video/framebuffer.h"
#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#endif

static volatile uint32_t svga2d_ap_execution_reported;
static volatile uint32_t hda_ap_execution_reported;
#ifdef REIST_HDA_SMP_LIFECYCLE_FAULT_INJECTION
static volatile uint32_t hda_fault_epoch;
#endif
#ifdef REIST_SVGA2D_SMP_LIFECYCLE_FAULT_INJECTION
static volatile uint32_t svga2d_fault_epoch;
#endif

typedef struct {
    uint32_t generation;
    uint32_t state;
    uint32_t epoch;
    uint32_t restart_count;
    uint64_t progress_marker;
    uint32_t heartbeat_timeout_ms;
    uint32_t recovery_timeout_ms;
    uint32_t restart_budget;
    uint32_t startup_timeout_ms;
    uint32_t startup_progress_timeout_ms;
    uint64_t startup_deadline_ms;
    uint64_t deadline_ms;
} supervisor_state_t;

_Static_assert(sizeof(supervisor_state_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "supervisor state exceeds protected payload");

typedef struct {
    uint32_t active;
    uint32_t generation;
    char name[SUPERVISOR_NAME_CAPACITY];
} supervisor_descriptor_t;

_Static_assert(sizeof(supervisor_descriptor_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "supervisor descriptor exceeds protected payload");

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
static volatile uint32_t network_service_ap_execution_generation;

#define SUPERVISOR_CHECK_INTERVAL_MS 10U
#define SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS 250U
#define SUPERVISOR_DHCP_COMMIT_TIMEOUT_MS 1000U
#define SUPERVISOR_DHCP_RENEW_TIMEOUT_MS 1500U
#define SUPERVISOR_DHCP_BOOT_TIMEOUT_MS 1500U
#define SUPERVISOR_DRIVER_CONTROL_VERSION 1U
#define SUPERVISOR_AUDIO_SERVICE_CONTROL_VERSION 1U
#define SUPERVISOR_AUDIO_SERVICE_PATH "/libexec/reist/audio.prg"
#define SUPERVISOR_COMPOSITOR_CONTROL_VERSION 2U
#define SUPERVISOR_COMPOSITOR_PATH "/usr/gui/bin/desktop.prg"
#define SUPERVISOR_COMPOSITOR_STOP_DIAGNOSTIC 0x434D5053U

/* Successful packet-by-packet mediation is useful as deterministic QEMU gate
 * evidence but floods the operator console on a live network.  Production
 * profiles retain rejection, integrity, lease-loss and recovery diagnostics;
 * explicit trace builds can opt back into the successful transaction stream. */
#if defined(QEMU_BUILD) || defined(REIST_NETWORK_TRACE)
#define REIST_NETWORK_TRACE_PRINT(...) printf(__VA_ARGS__)
#else
#define REIST_NETWORK_TRACE_PRINT(...) ((void)0)
#endif

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
static bool probe_wcet_baseline_reported;
static volatile bool probe_administratively_enabled = true;

typedef struct {
    uint32_t active;
    uint32_t administratively_enabled;
    uint32_t fenced;
    uint32_t device_index;
    uint32_t mode;
    int32_t pid;
    uint32_t process_generation;
    device_domain_handle_t device;
    ipc_handle_t channel;
    supervisor_handle_t supervisor;
    uint32_t post_ready_cpu_affinity_mask;
} supervisor_driver_control_t;

typedef struct {
    critical_object_t control;
    supervisor_config_t config;
    char name[SUPERVISOR_NAME_CAPACITY];
    char path[SUPERVISOR_DRIVER_PATH_CAPACITY];
    uint32_t reported_safe_generation;
    uint32_t reported_safe_epoch;
} supervisor_driver_runtime_t;

static supervisor_driver_runtime_t
    driver_runtimes[SUPERVISOR_MAX_DEVICE_DRIVERS];

typedef struct {
    uint32_t active;
    uint32_t fenced;
    uint32_t healthy;
    uint32_t ready;
    uint32_t device_index;
    int32_t pid;
    uint32_t process_generation;
    ipc_handle_t endpoint;
    int32_t client_pid;
    uint32_t client_generation;
    supervisor_handle_t supervisor;
    uint32_t post_ready_cpu_affinity_mask;
} supervisor_audio_service_control_t;

typedef struct {
    critical_object_t control;
    supervisor_config_t config;
    uint32_t reported_safe_generation;
    uint32_t reported_safe_epoch;
} supervisor_audio_service_runtime_t;

static supervisor_audio_service_runtime_t audio_service_runtime;
static volatile uint32_t audio_service_ap_execution_epoch;
#ifdef REIST_AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION
static volatile uint32_t audio_service_fault_epoch;
#endif
static int audio_service_report_if_identity(
    int pid, uint32_t generation, uint32_t report_type, uint32_t value,
    uint64_t now_ms, bool *matched);
static bool audio_service_rotate_session(supervisor_handle_t handle);

typedef struct {
    uint32_t active;
    uint32_t administratively_enabled;
    uint32_t fenced;
    uint32_t healthy;
    uint32_t ready;
    uint32_t stop_requested;
    int32_t pid;
    uint32_t process_generation;
    supervisor_handle_t supervisor;
    uint32_t post_ready_cpu_affinity_mask;
} supervisor_compositor_control_t;

typedef struct {
    critical_object_t control;
    supervisor_config_t config;
    uint32_t reported_safe_generation;
    uint32_t reported_safe_epoch;
} supervisor_compositor_runtime_t;

static supervisor_compositor_runtime_t compositor_runtime;
static volatile uint32_t compositor_ap_execution_generation;
#ifdef REIST_COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION
static volatile uint32_t compositor_fault_epoch;
#endif
static int compositor_report_if_identity(
    int pid, uint32_t generation, uint32_t report_type, uint32_t value,
    uint64_t now_ms, bool *matched);
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
static volatile uint32_t driver_fault_markers;
#endif
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

static uint64_t startup_window_deadline(const supervisor_state_t *state,
                                        uint64_t now_ms) {
    uint32_t window = state->startup_progress_timeout_ms != 0U
        ? state->startup_progress_timeout_ms : state->startup_timeout_ms;
    uint64_t deadline = deadline_after(now_ms, window);
    return deadline < state->startup_deadline_ms
        ? deadline : state->startup_deadline_ms;
}

static void supervisor_begin_startup(supervisor_state_t *state,
                                     uint64_t now_ms) {
    state->progress_marker = 0U;
    state->startup_deadline_ms = deadline_after(
        now_ms, state->startup_timeout_ms);
    state->deadline_ms = startup_window_deadline(state, now_ms);
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
        control->healthy > 1U || control->service_ready > 1U ||
        (control->fenced != 0U && control->healthy != 0U) ||
        (control->service_ready != 0U &&
         (control->healthy == 0U || control->fenced != 0U)) ||
        (control->post_ready_cpu_affinity_mask & 0xFFFF0000U) != 0U)
        return false;
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
               control->service_ready == 0U &&
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
           state->startup_timeout_ms != 0 &&
           state->startup_progress_timeout_ms <= state->startup_timeout_ms &&
           state->startup_deadline_ms != 0U && state->deadline_ms != 0U &&
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

#ifndef REIST_HOST_TEST
_Static_assert(sizeof(supervisor_driver_control_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "driver control exceeds protected payload");

static bool driver_control_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(supervisor_driver_control_t))
        return false;
    const supervisor_driver_control_t *control = payload;
    if (control->active > 1U || control->administratively_enabled > 1U ||
        control->fenced > 1U) return false;
    if (control->active == 0U)
        return control->administratively_enabled == 0U &&
            control->fenced == 0U && control->pid == 0 &&
            control->process_generation == 0U && control->device == 0U &&
            control->channel == 0U &&
            control->post_ready_cpu_affinity_mask == 0U;
    if (control->device_index >= DEVICE_DOMAIN_MAX_DEVICES ||
        (control->mode != DEVICE_DOMAIN_MODE_MEDIATED &&
         control->mode != DEVICE_DOMAIN_MODE_IOMMU_DIRECT) ||
        control->supervisor.generation == 0U ||
        control->supervisor.epoch == 0U ||
        (control->post_ready_cpu_affinity_mask &
         ~((1U << X86_CPU_LOCAL_MAX) - 1U)) != 0U) return false;
    bool process_present = control->pid > 0 &&
        control->process_generation != 0U && control->device != 0U;
    bool process_absent = control->pid == 0 &&
        control->process_generation == 0U && control->device == 0U &&
        control->channel == 0U;
    return process_present != process_absent &&
        (process_present || control->fenced != 0U);
}

static int driver_control_read(supervisor_driver_runtime_t *runtime,
                               supervisor_driver_control_t *control) {
    if (runtime == NULL || control == NULL) return -22;
    size_t length = 0U;
    return critical_object_read(
        &runtime->control, SUPERVISOR_DRIVER_CONTROL_VERSION, control,
        sizeof(*control), &length, driver_control_valid) < 0 ||
        length != sizeof(*control) ? SUPERVISOR_EINTEGRITY : 0;
}

static int driver_control_write(supervisor_driver_runtime_t *runtime,
                                const supervisor_driver_control_t *control) {
    if (runtime == NULL || control == NULL) return -22;
    return critical_object_update(
        &runtime->control, SUPERVISOR_DRIVER_CONTROL_VERSION, control,
        sizeof(*control), driver_control_valid) == 0
            ? 0 : SUPERVISOR_EINTEGRITY;
}

_Static_assert(sizeof(supervisor_audio_service_control_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "audio service control exceeds protected payload");

static bool audio_service_control_valid(const void *payload, size_t length) {
    if (payload == NULL ||
        length != sizeof(supervisor_audio_service_control_t)) return false;
    const supervisor_audio_service_control_t *control = payload;
    if (control->active > 1U || control->fenced > 1U ||
        control->healthy > 1U || control->ready > 1U) return false;
    if (control->active == 0U)
        return control->fenced == 0U && control->healthy == 0U &&
            control->ready == 0U && control->device_index == 0U &&
            control->pid == 0 && control->process_generation == 0U &&
            control->endpoint == 0U && control->client_pid == 0 &&
            control->client_generation == 0U &&
            control->supervisor.slot == 0U &&
            control->supervisor.generation == 0U &&
            control->supervisor.epoch == 0U &&
            control->post_ready_cpu_affinity_mask == 0U;
    if (control->device_index >= DEVICE_DOMAIN_MAX_DEVICES ||
        control->supervisor.generation == 0U ||
        control->supervisor.epoch == 0U ||
        (control->post_ready_cpu_affinity_mask &
         ~((1U << X86_CPU_LOCAL_MAX) - 1U)) != 0U ||
        (control->healthy != 0U && control->fenced != 0U) ||
        (control->ready != 0U &&
         (control->healthy == 0U || control->endpoint == 0U))) return false;
    bool present = control->pid > 0 && control->process_generation != 0U;
    bool absent = control->pid == 0 && control->process_generation == 0U &&
        control->endpoint == 0U && control->fenced != 0U &&
        control->healthy == 0U && control->ready == 0U &&
        control->client_pid == 0 && control->client_generation == 0U;
    bool client_present = control->client_pid > 0 &&
        control->client_generation != 0U;
    bool client_absent = control->client_pid == 0 &&
        control->client_generation == 0U;
    return present != absent && client_present != client_absent;
}

static int audio_service_control_read(
        supervisor_audio_service_control_t *control) {
    if (control == NULL) return -22;
    size_t length = 0U;
    return critical_object_read(
        &audio_service_runtime.control,
        SUPERVISOR_AUDIO_SERVICE_CONTROL_VERSION, control, sizeof(*control),
        &length, audio_service_control_valid) < 0 ||
        length != sizeof(*control) ? SUPERVISOR_EINTEGRITY : 0;
}

static int audio_service_control_write(
        const supervisor_audio_service_control_t *control) {
    if (control == NULL) return -22;
    return critical_object_update(
        &audio_service_runtime.control,
        SUPERVISOR_AUDIO_SERVICE_CONTROL_VERSION, control, sizeof(*control),
        audio_service_control_valid) == 0 ? 0 : SUPERVISOR_EINTEGRITY;
}

_Static_assert(sizeof(supervisor_compositor_control_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "compositor control exceeds protected payload");

static bool compositor_control_valid(const void *payload, size_t length) {
    if (payload == NULL ||
        length != sizeof(supervisor_compositor_control_t)) return false;
    const supervisor_compositor_control_t *control = payload;
    if (control->active > 1U || control->administratively_enabled > 1U ||
        control->fenced > 1U || control->healthy > 1U ||
        control->ready > 1U || control->stop_requested > 1U) return false;
    if ((control->post_ready_cpu_affinity_mask & 0xFFFF0001U) != 0U)
        return false;
    if (control->active == 0U) {
        const supervisor_compositor_control_t empty = {0};
        return memcmp(control, &empty, sizeof(empty)) == 0;
    }
    if (control->supervisor.generation == 0U ||
        control->supervisor.epoch == 0U ||
        (control->healthy != 0U && control->fenced != 0U) ||
        (control->ready != 0U && control->healthy == 0U) ||
        (control->stop_requested != 0U && control->ready == 0U)) return false;
    bool present = control->pid > 0 && control->process_generation != 0U;
    bool absent = control->pid == 0 && control->process_generation == 0U &&
        control->fenced != 0U && control->healthy == 0U &&
        control->ready == 0U;
    return present != absent;
}

static int compositor_control_read(
        supervisor_compositor_control_t *control) {
    if (control == NULL) return -22;
    size_t length = 0U;
    return critical_object_read(
        &compositor_runtime.control, SUPERVISOR_COMPOSITOR_CONTROL_VERSION,
        control, sizeof(*control), &length, compositor_control_valid) < 0 ||
        length != sizeof(*control) ? SUPERVISOR_EINTEGRITY : 0;
}

static int compositor_control_write(
        const supervisor_compositor_control_t *control) {
    if (control == NULL) return -22;
    return critical_object_update(
        &compositor_runtime.control, SUPERVISOR_COMPOSITOR_CONTROL_VERSION,
        control, sizeof(*control), compositor_control_valid) == 0
            ? 0 : SUPERVISOR_EINTEGRITY;
}
#endif

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
    supervisor_driver_control_t empty_driver = {0};
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    driver_fault_markers = 0U;
#endif
    for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS; ++slot) {
        memset(&driver_runtimes[slot], 0, sizeof(driver_runtimes[slot]));
        if (critical_object_init(
                &driver_runtimes[slot].control,
                SUPERVISOR_DRIVER_CONTROL_VERSION, &empty_driver,
                sizeof(empty_driver)) != 0)
            panic("Unable to initialize protected driver runtime");
    }
    supervisor_audio_service_control_t empty_audio_service = {0};
    memset(&audio_service_runtime, 0, sizeof(audio_service_runtime));
    if (critical_object_init(
            &audio_service_runtime.control,
            SUPERVISOR_AUDIO_SERVICE_CONTROL_VERSION, &empty_audio_service,
            sizeof(empty_audio_service)) != 0)
        panic("Unable to initialize protected audio service runtime");
    supervisor_compositor_control_t empty_compositor = {0};
    memset(&compositor_runtime, 0, sizeof(compositor_runtime));
    compositor_ap_execution_generation = 0U;
#ifdef REIST_COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION
    compositor_fault_epoch = 0U;
#endif
    if (critical_object_init(
            &compositor_runtime.control,
            SUPERVISOR_COMPOSITOR_CONTROL_VERSION, &empty_compositor,
            sizeof(empty_compositor)) != 0)
        panic("Unable to initialize protected compositor runtime");
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
    const uint32_t startup_timeout_ms = config->startup_timeout_ms != 0U
        ? config->startup_timeout_ms : config->recovery_timeout_ms;
    if (config->startup_progress_timeout_ms > startup_timeout_ms) return -1;
    const uint32_t startup_window_ms =
        config->startup_progress_timeout_ms != 0U
            ? config->startup_progress_timeout_ms : startup_timeout_ms;
    const uint64_t startup_deadline_ms =
        deadline_after(now_ms, startup_timeout_ms);
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
        .startup_timeout_ms = startup_timeout_ms,
        .startup_progress_timeout_ms = config->startup_progress_timeout_ms,
        .startup_deadline_ms = startup_deadline_ms,
        .deadline_ms = deadline_after(now_ms, startup_window_ms),
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

int supervisor_report_startup_progress(supervisor_handle_t handle,
                                       uint64_t progress_marker,
                                       uint64_t now_ms) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 ||
        (state.state != SUPERVISOR_STARTING &&
         state.state != SUPERVISOR_RECOVERING) ||
        state.startup_progress_timeout_ms == 0U ||
        progress_marker <= state.progress_marker ||
        now_ms >= state.deadline_ms || now_ms >= state.startup_deadline_ms) {
        supervisor_unlock(flags);
        return -1;
    }
    uint64_t next = deadline_after(
        now_ms, state.startup_progress_timeout_ms);
    if (next > state.startup_deadline_ms) next = state.startup_deadline_ms;
    state.progress_marker = progress_marker;
    state.deadline_ms = next;
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

int supervisor_report_idle(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0) {
        supervisor_unlock(flags);
        return -1;
    }
    /* Cleanup/recovery paths may converge on IDLE independently.  Treat an
     * already validated idle state as success without relaxing fenced,
     * isolated or safe-state transitions. */
    if (state.state == SUPERVISOR_IDLE) {
        supervisor_unlock(flags);
        return 0;
    }
    if (state.state != SUPERVISOR_HEALTHY) {
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
    if (control.post_ready_cpu_affinity_mask != 0U &&
        process_identity_alive(control.pid, control.process_generation) &&
        (process_set_supervised_affinity(
             control.pid, control.process_generation, TASK_CPU_MASK_BSP) != 0 ||
         scheduler_sleep_ms(1U) != 0)) {
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
    control.service_ready = 0U;
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
    const char *arguments[] = {"reist.prg", modes[mode_index]};
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
    int pid = supervisor_spawn_service("/libexec/reist/reist.prg", 2, arguments,
                                       PROCESS_DOMAIN_PROBE);
    uint32_t generation = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) return false;
    control.pid = pid;
    control.process_generation = generation;
    control.healthy = 0U;
    control.service_ready = 0U;
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
    probe_administratively_enabled = true;
    return true;
}

bool supervisor_probe_ready(void) {
    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control;
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    bool ready = result == 0 && control.active != 0U &&
        control.fenced == 0U && control.healthy != 0U &&
        control.service_ready != 0U &&
        control.launch_count >= 4U &&
        control.endpoint_handle != IPC_INVALID_HANDLE &&
        process_identity_alive(control.pid, control.process_generation);
    supervisor_unlock(flags);
    return ready;
}

int supervisor_set_network_service_current_affinity(
        uint32_t cpu_affinity_mask) {
    if (cpu_affinity_mask == 0U) return -22;
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U ||
        control.fenced != 0U || control.healthy == 0U ||
        control.service_ready == 0U || control.pid <= 0 ||
        !process_identity_alive(control.pid, control.process_generation))
        return -3;
    control.post_ready_cpu_affinity_mask = cpu_affinity_mask;
    if (supervisor_protected_probe_control_write(
            &probe_runtime.control, &control) != 0)
        return SUPERVISOR_EINTEGRITY;
    return process_set_supervised_affinity(
        control.pid, control.process_generation, cpu_affinity_mask);
}

static int supervisor_admin_pause(supervisor_handle_t handle) {
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 ||
        (state.state != SUPERVISOR_HEALTHY &&
         state.state != SUPERVISOR_STARTING &&
         state.state != SUPERVISOR_IDLE)) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = SUPERVISOR_IDLE;
    state.deadline_ms = UINT64_MAX;
    int result = state_write(handle.slot, &state);
    supervisor_unlock(flags);
    return result;
}

static int supervisor_admin_start(supervisor_handle_t handle,
                                  uint64_t now_ms,
                                  supervisor_handle_t *updated_out) {
    if (updated_out == NULL) return -1;
    uint32_t flags = supervisor_lock();
    supervisor_state_t state;
    if (resolve(handle, &state) != 0 || state.state != SUPERVISOR_IDLE) {
        supervisor_unlock(flags);
        return -1;
    }
    if (state.epoch == UINT32_MAX) {
        supervisor_unlock(flags);
        return -1;
    }
    ++state.epoch;
    state.state = SUPERVISOR_STARTING;
    supervisor_begin_startup(&state, now_ms);
    int result = state_write(handle.slot, &state);
    if (result == 0) {
        *updated_out = (supervisor_handle_t){
            .slot = handle.slot,
            .generation = state.generation,
            .epoch = state.epoch,
        };
    }
    supervisor_unlock(flags);
    return result;
}

bool supervisor_probe_component_ready(void) {
    return probe_administratively_enabled && supervisor_probe_ready();
}

bool supervisor_probe_component_down(uint64_t deadline_ms) {
    KASSERT_NOT_IRQ();
    probe_administratively_enabled = false;
    __asm__ volatile("" ::: "memory");
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U)
        return false;
    bool fenced = control.fenced != 0U &&
                  !process_identity_alive(control.pid,
                                          control.process_generation);
    if (!fenced)
        fenced = probe_fence_apply(&probe_runtime) &&
                 probe_fence_verify(&probe_runtime);
    if (!fenced || supervisor_admin_pause(control.handle) != 0)
        return false;
    return pit_monotonic_ms() <= deadline_ms;
}

bool supervisor_probe_component_up(uint64_t deadline_ms) {
    KASSERT_NOT_IRQ();
    if (pit_monotonic_ms() >= deadline_ms) return false;
    if (supervisor_probe_component_ready()) return true;
    supervisor_probe_control_t control;
    if (supervisor_protected_probe_control_read(
            &probe_runtime.control, &control) != 0 || control.active == 0U ||
        control.fenced == 0U || process_identity_alive(
            control.pid, control.process_generation)) return false;
    supervisor_handle_t updated;
    if (supervisor_admin_start(control.handle, pit_monotonic_ms(),
                               &updated) != 0) return false;
    control.handle = updated;
    control.endpoint_handle = 0U;
    if (supervisor_protected_probe_control_write(
            &probe_runtime.control, &control) != 0) return false;
    probe_administratively_enabled = true;
    __asm__ volatile("" ::: "memory");
    if (!probe_spawn_next()) {
        probe_administratively_enabled = false;
        return false;
    }
    while (!supervisor_probe_component_ready() &&
           pit_monotonic_ms() < deadline_ms) {
        if (scheduler_sleep_ms(5U) != 0) (void)scheduler_yield();
    }
    if (supervisor_probe_component_ready()) return true;
    (void)supervisor_probe_component_down(deadline_ms);
    return false;
}

int supervisor_probe_report(int pid, uint32_t generation,
                            uint32_t report_type, uint32_t value,
                            uint64_t now_ms) {
    bool compositor_match = false;
    int compositor_result = compositor_report_if_identity(
        pid, generation, report_type, value, now_ms, &compositor_match);
    if (compositor_match) return compositor_result;
    bool audio_service_match = false;
    int audio_service_result = audio_service_report_if_identity(
        pid, generation, report_type, value, now_ms, &audio_service_match);
    if (audio_service_match) return audio_service_result;
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
        if (result == 0 && control.service_ready != 0U &&
            x86_cpu_current_index() != 0U &&
            network_service_ap_execution_generation !=
                control.process_generation &&
            __sync_bool_compare_and_swap(
                &network_service_ap_execution_generation,
                network_service_ap_execution_generation,
                control.process_generation))
            printf("REIST_NETWORK SERVICE_AP_EXEC cpu=%u generation=%u\n",
                   x86_cpu_current_index(), control.process_generation);
        return result;
    }
    if (report_type == REIST_REPORT_SERVICE_READY) {
        if (value != 1U || control.fenced != 0U || control.healthy == 0U ||
            control.service_ready != 0U || control.launch_count < 4U)
            return -1;
        control.service_ready = 1U;
        if (supervisor_protected_probe_control_write(
                &probe_runtime.control, &control) != 0) return -1;
        printf("REIST_NETWORK SERVICE_READY\n");
        if (control.post_ready_cpu_affinity_mask != 0U &&
            process_set_supervised_affinity(
                control.pid, control.process_generation,
                control.post_ready_cpu_affinity_mask) != 0) {
            (void)supervisor_force_isolate(control.handle);
            return -1;
        }
        return 0;
    }
    if (report_type == REIST_REPORT_WCET_BASELINE) {
        runtime_timing_stats_t stats;
        if (value != RUNTIME_TIMING_STATS_VERSION ||
            control.fenced != 0U || control.healthy == 0U ||
            control.service_ready == 0U || control.launch_count < 4U ||
            scheduler_runtime_timing_stats(&stats) != 0 ||
            stats.scheduler_samples < 64U || stats.syscall_samples < 64U ||
            stats.clock_anomalies != 0U) return -1;
        uint32_t flags = supervisor_lock();
        if (probe_wcet_baseline_reported) {
            supervisor_unlock(flags);
            return 0;
        }
        probe_wcet_baseline_reported = true;
        supervisor_unlock(flags);
        printf("REIST_WCET BASELINE version=%u frequency_hz=%llu "
               "scheduler_samples=%llu scheduler_total_cycles=%llu "
               "scheduler_max_cycles=%llu int80_samples=%llu "
               "int80_total_cycles=%llu int80_max_cycles=%llu "
               "clock_anomalies=%llu\n",
               stats.version, stats.cpu_frequency_hz,
               stats.scheduler_samples, stats.scheduler_total_cycles,
               stats.scheduler_max_cycles, stats.syscall_samples,
               stats.syscall_total_cycles, stats.syscall_max_cycles,
               stats.clock_anomalies);
        return 0;
    }
    if (report_type == REIST_REPORT_WCET_REJECT) {
        if (value == 0U || value > 8U || control.fenced != 0U ||
            control.healthy == 0U || control.service_ready == 0U ||
            control.launch_count < 4U) return -1;
        printf("REIST_WCET REJECT reason=%u\n", value);
        return 0;
    }
    if (report_type == REIST_REPORT_INVALID) {
        (void)supervisor_force_isolate(control.handle);
        return -1;
    }
    if (report_type == REIST_REPORT_NETWORK_HEADER) {
        if (value != 0x0800U && value != 0x0806U) return -1;
        REIST_NETWORK_TRACE_PRINT(
            value == 0x0806U ? "REIST_NETWORK RX_HEADER_ARP\n"
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
        REIST_NETWORK_TRACE_PRINT("REIST_NETWORK PROBE_ID_OK\n");
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
    if (client == NULL || handle_out == NULL) return -22;

    if (service_id == REIST_SERVICE_AUDIO) {
        supervisor_audio_service_control_t audio;
        uint32_t flags = supervisor_lock();
        int control_result = audio_service_control_read(&audio);
        if (control_result != 0 || audio.active == 0U ||
            audio.fenced != 0U || audio.healthy == 0U || audio.ready == 0U ||
            audio.endpoint == IPC_INVALID_HANDLE || audio.pid <= 0 ||
            !process_identity_alive(audio.pid, audio.process_generation)) {
            supervisor_unlock(flags);
            return -11;
        }
        if (audio.client_pid != 0) {
            bool client_alive = process_identity_alive(
                audio.client_pid, audio.client_generation);
            supervisor_handle_t handle = audio.supervisor;
            if (!client_alive) {
                /* Serialize the administrative endpoint rotation.  Other
                 * clients see EAGAIN until a fresh service generation has
                 * completed self-test and published a clean endpoint. */
                audio.ready = 0U;
                control_result = audio_service_control_write(&audio);
            }
            supervisor_unlock(flags);
            if (client_alive) return -16;
            if (control_result != 0) {
                (void)supervisor_force_isolate(handle);
                return SUPERVISOR_EINTEGRITY;
            }
            /* Never delegate an endpoint containing responses from a dead
             * client to a new identity. Revoke the complete service endpoint
             * and create a clean generation as an expected session change,
             * without consuming the service fault-restart budget. */
            (void)audio_service_rotate_session(handle);
            return -11;
        }
        audio.client_pid = client->pid;
        audio.client_generation = client->generation;
        control_result = audio_service_control_write(&audio);
        supervisor_unlock(flags);
        if (control_result != 0) {
            (void)supervisor_force_isolate(audio.supervisor);
            return SUPERVISOR_EINTEGRITY;
        }
        int result = process_ipc_delegate_identity(
            audio.pid, audio.process_generation, audio.endpoint, client,
            IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE);
        if (result == 0) {
            *handle_out = audio.endpoint;
        } else {
            supervisor_audio_service_control_t current;
            bool rollback_integrity_failure = false;
            supervisor_handle_t rollback_handle = audio.supervisor;
            flags = supervisor_lock();
            if (audio_service_control_read(&current) == 0 &&
                current.client_pid == client->pid &&
                current.client_generation == client->generation) {
                current.client_pid = 0;
                current.client_generation = 0U;
                rollback_handle = current.supervisor;
                rollback_integrity_failure =
                    audio_service_control_write(&current) != 0;
            }
            supervisor_unlock(flags);
            if (rollback_integrity_failure)
                (void)supervisor_force_isolate(rollback_handle);
        }
        return result;
    }

    if (service_id == REIST_SERVICE_AUDIO_DRIVER_INTERNAL) {
        supervisor_audio_service_control_t audio;
        if (client->domain_profile.kind != PROCESS_DOMAIN_AUDIO_SERVICE ||
            audio_service_control_read(&audio) != 0 || audio.active == 0U ||
            audio.pid != client->pid ||
            audio.process_generation != client->generation ||
            !process_identity_alive(audio.pid, audio.process_generation))
            return -13;
        for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS;
             ++slot) {
            supervisor_driver_control_t driver;
            if (driver_control_read(&driver_runtimes[slot], &driver) != 0 ||
                driver.active == 0U ||
                driver.device_index != audio.device_index) continue;
            if (driver.fenced != 0U || driver.pid <= 0 ||
                driver.channel == IPC_INVALID_HANDLE ||
                !process_identity_alive(
                    driver.pid, driver.process_generation) ||
                !supervisor_output_allowed(driver.supervisor)) return -11;
            int result = process_ipc_delegate_identity(
                driver.pid, driver.process_generation, driver.channel,
                client, IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE);
            if (result == 0) *handle_out = driver.channel;
            return result;
        }
        return -19;
    }

    if (service_id == REIST_SERVICE_DISPLAY_DRIVER) {
        if ((client->domain_profile.kind != PROCESS_DOMAIN_COMPATIBILITY &&
             client->domain_profile.kind != PROCESS_DOMAIN_COMPOSITOR) ||
            strcmp(client->image_path, "/usr/gui/bin/desktop.prg") != 0)
            return -13;
        for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS;
             ++slot) {
            supervisor_driver_control_t driver;
            if ((strcmp(driver_runtimes[slot].name, "svga2d-ring3") != 0 &&
                 strcmp(driver_runtimes[slot].name,
                        "nvidia-gk208-ring3") != 0) ||
                driver_control_read(&driver_runtimes[slot], &driver) != 0 ||
                driver.active == 0U)
                continue;
            if (driver.fenced != 0U || driver.pid <= 0 ||
                driver.channel == IPC_INVALID_HANDLE ||
                !process_identity_alive(driver.pid,
                                        driver.process_generation) ||
                !supervisor_output_allowed(driver.supervisor))
                return -11;
            int result = process_ipc_delegate_identity(
                driver.pid, driver.process_generation, driver.channel,
                client, IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE);
            if (result == 0) *handle_out = driver.channel;
            return result;
        }
        return -19;
    }

    supervisor_probe_control_t control;
    if (service_id != REIST_SERVICE_DIAGNOSTIC ||
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

static int probe_network_ingress_send(
        const supervisor_probe_control_t *control,
        const ipc_message_t *message) {
    int result = ipc_send_external_from_peer(
        control->pid, control->process_generation, control->endpoint_handle,
        message);
    if (result == IPC_EPIPE)
        result = ipc_send_kernel_to_owner(
            control->pid, control->process_generation,
            control->endpoint_handle, message);
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
    if (frame[14] != 0U || frame[15] != 1U ||
        frame[16] != 0x08U || frame[17] != 0U ||
        frame[18] != 6U || frame[19] != 4U) return false;
    bool arp_request = frame[20] == 0U && frame[21] == 1U;
    bool arp_reply = frame[20] == 0U && frame[21] == 2U;
    if (!arp_request && !arp_reply) return false;
    uint32_t target_ip = ((uint32_t)frame[38] << 24U) |
                         ((uint32_t)frame[39] << 16U) |
                         ((uint32_t)frame[40] << 8U) | frame[41];
    bool local_request = local_identity && arp_request && target_ip == local_ip;
    /* Broadcast discovery and gratuitous ARP are normal before DHCP has
     * established a local identity. They carry no probe authority and must
     * neither be mediated nor isolate the healthy Ring-3 service. */
    if (arp_request && !local_request) return false;

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
        bool reply_transaction_started = begin == 0;
        if (begin == 0)
            begin = supervisor_protected_arp_reply_context_publish(
                &probe_runtime.arp_reply_context, request_id, request_epoch,
                ((uint32_t)frame[28] << 24U) |
                    ((uint32_t)frame[29] << 16U) |
                    ((uint32_t)frame[30] << 8U) | frame[31],
                &frame[22]);
        /* A second ARP request may arrive while Ring 3 is validating the
         * first.  A busy begin belongs to that older transaction and must not
         * revoke it; only roll back state allocated by this attempt. */
        if (begin != 0 && reply_transaction_started) {
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.arp_reply_authority);
            (void)supervisor_protected_arp_reply_context_clear(
                &probe_runtime.arp_reply_context);
        }
        supervisor_unlock(transaction_flags);
        if (begin != 0) {
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
        int ingress = probe_network_ingress_send(&control, &request);
        if (ingress != 0) {
            transaction_flags = supervisor_lock();
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.arp_reply_authority);
            (void)supervisor_protected_arp_reply_context_clear(
                &probe_runtime.arp_reply_context);
            supervisor_unlock(transaction_flags);
            network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        }
        /* A local request belongs exclusively to the service.  Queue pressure
         * drops it fail-closed instead of reviving the Ring-0 reply path. */
        return true;
    }
    uint32_t probe_id;
    supervisor_network_probe_context_t network_context;
    int context_result = supervisor_protected_network_context_snapshot(
        &probe_runtime.network_probe_context, &network_context);
    if (context_result != 0) {
        supervisor_unlock(transaction_flags);
        (void)supervisor_force_isolate(control.handle);
        return false;
    }
    if (control.network_epoch == 0U &&
        network_context.transaction_epoch == 0U) {
        supervisor_unlock(transaction_flags);
        return false;
    }
    if (control.network_epoch == 0U ||
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
    int ingress = probe_network_ingress_send(&control, &message);
    /* The authorization was consumed before delivery. If bounded IPC cannot
     * publish the validated reply, close the matching transaction so a later
     * unrelated ARP frame cannot inherit stale authority. */
    if (ingress != 0) {
        transaction_flags = supervisor_lock();
        supervisor_probe_control_t current;
        int cleanup = supervisor_protected_probe_control_read(
            &probe_runtime.control, &current);
        if (cleanup == 0 &&
            current.network_epoch == control.network_epoch) {
            cleanup = supervisor_protected_network_context_clear(
                &probe_runtime.network_probe_context);
            current.network_epoch = 0U;
            if (cleanup == 0)
                cleanup = supervisor_protected_probe_control_write(
                    &probe_runtime.control, &current);
        }
        supervisor_unlock(transaction_flags);
        if (cleanup != 0) (void)supervisor_force_isolate(control.handle);
    }
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
    if (result == 0)
        result = supervisor_protected_network_context_clear(
            &probe_runtime.network_probe_context);
    if (result == 0) {
        control.network_epoch = 0U;
        result = supervisor_protected_probe_control_write(
            &probe_runtime.control, &control);
    }
    if (result == 0 && !netstack_commit_arp_binding(
            binding->ip, binding->mac, binding->probe_id,
            pid, generation, pit_monotonic_ms())) result = -22;
    supervisor_unlock(transaction_flags);
    if (result == 0)
        REIST_NETWORK_TRACE_PRINT(
            "REIST_NETWORK PROBE_ID_OK\nREIST_NETWORK ARP_BINDING_OK\n");
    return result;
}

bool supervisor_network_request_arp_resolution(uint32_t target_ip) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    uint32_t local_ip = 0U;
    uint8_t local_mac[6] = {0};
    bool local_identity = netstack_get_local_identity(&local_ip, local_mac);
    if (target_ip == 0U || target_ip == 0xFFFFFFFFU ||
        !local_identity ||
        target_ip == local_ip) return false;

    uint32_t flags = supervisor_lock();
    supervisor_probe_control_t control = {0};
    int result = supervisor_protected_probe_control_read(
        &probe_runtime.control, &control);
    if (result != 0 || control.active == 0U || control.fenced != 0U ||
        control.healthy == 0U || control.launch_count < 4U ||
        control.endpoint_handle == IPC_INVALID_HANDLE ||
        !process_identity_alive(control.pid, control.process_generation)) {
        supervisor_unlock(flags);
        return false;
    }
    uint32_t network_probe_id = 0U;
    uint32_t request_id = 0U;
    bool network_transaction_started = false;
    bool resolution_transaction_started = false;
    result = supervisor_protected_probe_authority_begin(
        &probe_runtime.network_probe_authority, pit_monotonic_ms(),
        SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS, &network_probe_id);
    network_transaction_started = result == 0;
    if (result == 0)
        result = supervisor_protected_network_context_prepare_epoch(
            &probe_runtime.network_probe_context, network_probe_id,
            target_ip, local_ip, local_mac);
    if (result == 0)
        result = supervisor_protected_probe_authority_begin_epoch(
            &probe_runtime.arp_resolution_authority, pit_monotonic_ms(),
            SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS, control.process_generation,
            &request_id);
    resolution_transaction_started = result == 0;
    if (result == 0)
        result = supervisor_protected_arp_resolution_context_publish(
            &probe_runtime.arp_resolution_context, request_id,
            control.process_generation, target_ip);
    if (result == 0) {
        control.network_epoch = network_probe_id;
        result = supervisor_protected_probe_control_write(
            &probe_runtime.control, &control);
    }
    if (result != 0) {
        /* A concurrent cache miss can observe the already active transaction.
         * Roll back only state allocated by this attempt; cancelling a busy
         * predecessor would invalidate the NETA message already queued for
         * Ring 3 and turn its valid response into EACCES. */
        if (resolution_transaction_started) {
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.arp_resolution_authority);
            (void)supervisor_protected_arp_resolution_context_clear(
                &probe_runtime.arp_resolution_context);
        }
        if (network_transaction_started) {
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.network_probe_authority);
            (void)supervisor_protected_network_context_clear(
                &probe_runtime.network_probe_context);
        }
    }
    supervisor_unlock(flags);
    if (result != 0) {
        if (result == SUPERVISOR_EINTEGRITY)
            (void)supervisor_force_isolate(control.handle);
        return false;
    }

    ipc_message_t message = {
        .version = IPC_MESSAGE_VERSION,
        .struct_size = sizeof(ipc_message_t),
        .length = 26U,
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
    for (uint32_t index = 0U; index < 4U; ++index)
        message.payload[22U + index] =
            (uint8_t)(network_probe_id >> (index * 8U));
    result = probe_network_ingress_send(&control, &message);
    if (result != 0) {
        flags = supervisor_lock();
        supervisor_probe_control_t current;
        int cleanup = supervisor_protected_probe_control_read(
            &probe_runtime.control, &current);
        (void)supervisor_protected_probe_authority_cancel(
            &probe_runtime.arp_resolution_authority);
        (void)supervisor_protected_arp_resolution_context_clear(
            &probe_runtime.arp_resolution_context);
        if (cleanup == 0 && current.network_epoch == network_probe_id) {
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.network_probe_authority);
            cleanup = supervisor_protected_network_context_clear(
                &probe_runtime.network_probe_context);
            current.network_epoch = 0U;
            if (cleanup == 0)
                cleanup = supervisor_protected_probe_control_write(
                    &probe_runtime.control, &current);
        }
        supervisor_unlock(flags);
        if (cleanup != 0) (void)supervisor_force_isolate(control.handle);
        network_degradation_record(SUPERVISOR_NETWORK_DEGRADED_QUEUE);
        return false;
    }
    REIST_NETWORK_TRACE_PRINT("REIST_NETWORK ARP_RESOLUTION_QUEUED\n");
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
    supervisor_probe_control_t control = {0};
    supervisor_arp_resolution_context_t context;
    supervisor_network_probe_context_t network_context;
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
    bool request_owns_context = result == 0;
    if (result == 0)
        result = supervisor_protected_network_context_snapshot(
            &probe_runtime.network_probe_context, &network_context);
    if (result == 0 &&
        (control.network_epoch == 0U ||
         network_context.transaction_epoch != control.network_epoch ||
         network_context.gateway != request->target_ip)) result = -13;
    uint32_t network_epoch = control.network_epoch;
    uint32_t consumed_id = 0U;
    if (result == 0)
        result = supervisor_protected_probe_authority_take_epoch(
            &probe_runtime.arp_resolution_authority, pit_monotonic_ms(),
            generation, &consumed_id);
    if (result == 0 && consumed_id != request->request_id) result = -13;
    /* A stale response must never erase a newer request's context. */
    if (request_owns_context)
        (void)supervisor_protected_arp_resolution_context_clear(
            &probe_runtime.arp_resolution_context);
    supervisor_unlock(flags);
    if (result != 0) {
        if (caller_is_service)
            printf("REIST_NETWORK ARP_RESOLUTION_REJECTED %d\n", result);
        return result;
    }
    if (!netstack_send_arp_request(request->target_ip)) {
        flags = supervisor_lock();
        if (supervisor_protected_probe_control_read(
                &probe_runtime.control, &control) != 0) {
            result = SUPERVISOR_EINTEGRITY;
        } else if (control.network_epoch == network_epoch) {
            (void)supervisor_protected_probe_authority_cancel(
                &probe_runtime.network_probe_authority);
            result = supervisor_protected_network_context_clear(
                &probe_runtime.network_probe_context);
            control.network_epoch = 0U;
            if (result == 0)
                result = supervisor_protected_probe_control_write(
                    &probe_runtime.control, &control);
        } else {
            result = 0;
        }
        supervisor_unlock(flags);
        if (result != 0) (void)supervisor_force_isolate(control.handle);
        return -5;
    }
    REIST_NETWORK_TRACE_PRINT("REIST_NETWORK ARP_RESOLUTION_MEDIATED\n");
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
    /* A host normally resolves the server before opening TCP. The request
     * source tuple has already been checked by Ring 3 and matched against the
     * generation-scoped reply context above, so publish that bounded neighbor
     * binding before advertising reachability. This avoids losing the first
     * passive TCP response while retaining the supervised cache lease. */
    if (!netstack_commit_arp_binding(
            reply->target_ip, reply->target_mac, reply->request_id,
            pid, generation, pit_monotonic_ms())) {
        printf("REIST_NETWORK ARP_REPLY_REJECTED -5\n");
        return -5;
    }
    if (!netstack_send_arp_reply(reply->target_ip, reply->target_mac)) {
        printf("REIST_NETWORK ARP_REPLY_REJECTED -5\n");
        return -5;
    }
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
    result = probe_network_ingress_send(&control, &message);
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

static supervisor_driver_runtime_t *driver_runtime_for_identity(
        int pid, uint32_t process_generation,
        supervisor_driver_control_t *control_out) {
    for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS; ++slot) {
        supervisor_driver_control_t control;
        if (driver_control_read(&driver_runtimes[slot], &control) != 0 ||
            control.active == 0U || control.pid != pid ||
            control.process_generation != process_generation) continue;
        if (control_out != NULL) *control_out = control;
        return &driver_runtimes[slot];
    }
    return NULL;
}

static supervisor_driver_runtime_t *driver_runtime_for_handle(
        supervisor_handle_t handle, supervisor_driver_control_t *control_out) {
    for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS; ++slot) {
        supervisor_driver_control_t control;
        if (driver_control_read(&driver_runtimes[slot], &control) != 0 ||
            control.active == 0U ||
            control.supervisor.slot != handle.slot ||
            control.supervisor.generation != handle.generation) continue;
        if (control_out != NULL) *control_out = control;
        return &driver_runtimes[slot];
    }
    return NULL;
}

static bool copy_driver_string(char *destination, size_t capacity,
                               const char *source) {
    if (destination == NULL || capacity < 2U || source == NULL) return false;
    size_t length = 0U;
    while (length < capacity && source[length] != '\0') ++length;
    if (length == 0U || length >= capacity) return false;
    memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

static void driver_abort_prepared_spawn(
        supervisor_driver_runtime_t *runtime,
        supervisor_driver_control_t *control, int pid, uint32_t generation,
        device_domain_handle_t device) {
    if (pid > 0 && generation != 0U && device != 0U) {
        (void)device_domain_fence(pid, generation, device);
        (void)process_terminate(pid);
        uint64_t now_ms = pit_monotonic_ms();
        if (runtime != NULL)
            (void)device_domain_recover_owner(
                pid, generation, deadline_after(
                    now_ms, runtime->config.recovery_timeout_ms));
    } else if (pid > 0) {
        (void)process_terminate(pid);
    }
    if (runtime == NULL || control == NULL) return;
    control->pid = 0;
    control->process_generation = 0U;
    control->device = 0U;
    control->channel = 0U;
    control->fenced = 1U;
    (void)driver_control_write(runtime, control);
}

static bool driver_spawn_next(supervisor_driver_runtime_t *runtime,
                              supervisor_handle_t supervisor_handle) {
    supervisor_driver_control_t control;
    if (runtime == NULL || driver_control_read(runtime, &control) != 0 ||
        control.active == 0U || control.administratively_enabled == 0U ||
        control.pid != 0 || control.device != 0U || control.channel != 0U)
        return false;
    const char *arguments[] = {runtime->path};
    uint32_t affinity = runtime->config.cpu_affinity_mask == 0U
        ? TASK_CPU_MASK_BSP : runtime->config.cpu_affinity_mask;
    /* The driver must not bootstrap until its complete generation-scoped
     * control record is visible. HDA and video startup can otherwise expose a
     * publish-after-run race in which bootstrap observes no matching owner.
     * Keep the task PREPARED while affinity, device authority and protected
     * control state are committed, then make it runnable exactly once. */
    int pid = process_spawn_supervised_prepared(
        runtime->path, 1, arguments, PROCESS_DOMAIN_DRIVER);
    uint32_t generation = 0U;
    device_domain_handle_t device = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) {
        driver_abort_prepared_spawn(
            runtime, &control, pid, generation, device);
        return false;
    }
    if (process_set_supervised_affinity(pid, generation, affinity) != 0) {
        driver_abort_prepared_spawn(
            runtime, &control, pid, generation, device);
        return false;
    }
    if (device_domain_claim(pid, generation, control.device_index,
                            control.mode, &device) != 0) {
        driver_abort_prepared_spawn(
            runtime, &control, pid, generation, device);
        return false;
    }
    control.pid = pid;
    control.process_generation = generation;
    control.device = device;
    control.supervisor = supervisor_handle;
    control.fenced = 1U;
    if (driver_control_write(runtime, &control) != 0) {
        driver_abort_prepared_spawn(
            runtime, &control, pid, generation, device);
        return false;
    }
    if (process_start_prepared_supervised(pid, generation) != 0) {
        driver_abort_prepared_spawn(
            runtime, &control, pid, generation, device);
        return false;
    }
    return true;
}

static bool driver_fence_until(supervisor_driver_runtime_t *runtime,
                               uint64_t deadline_ms) {
    supervisor_driver_control_t control;
    if (runtime == NULL || driver_control_read(runtime, &control) != 0 ||
        control.active == 0U) return false;
    if (control.pid == 0)
        return control.fenced != 0U && control.device == 0U;
    if (deadline_ms == 0U || pit_monotonic_ms() >= deadline_ms) return false;
    bool owns_device_scanout = strcmp(runtime->name, "svga2d-ring3") == 0;
    bool passive_vbe_client =
        strcmp(runtime->name, "nvidia-gk208-ring3") == 0;
    /* A supervised driver may currently execute on an AP. Return its
     * generation to the BSP and yield for one scheduler tick before revoking
     * mediated I/O. The BSP-bound supervisor then resumes without the target
     * concurrently entering its userspace exit path on another CPU. */
    if (process_set_supervised_affinity(
            control.pid, control.process_generation, TASK_CPU_MASK_BSP) != 0)
        return false;
    uint64_t now_ms = pit_monotonic_ms();
    if (now_ms >= deadline_ms || scheduler_sleep_ms(1U) != 0 ||
        pit_monotonic_ms() >= deadline_ms)
        return false;
    /* VMware owns the active SVGA mode and must disable it before recovery.
     * The passive GK208 service owns no scanout or GPU command state: its
     * zero-capability endpoint only asks the kernel to retain the sealed VBE
     * mode.  Deactivating that kernel-owned mode here would erase an unrelated
     * software desktop merely because the policy process changed generation. */
    if (owns_device_scanout && display_control_graphics_active() &&
        display_control_deactivate() != 0)
        return false;
    if ((owns_device_scanout || passive_vbe_client) &&
        device_domain_mark_mediated_io_quiesced(
            control.pid, control.process_generation,
            control.device) != 0)
        return false;
    bool fenced = device_domain_fence(
        control.pid, control.process_generation, control.device) == 0;
    bool stopped = !process_identity_alive(
        control.pid, control.process_generation) ||
        process_terminate(control.pid) == 0;
    bool recovered = fenced && stopped &&
        device_domain_recover_owner(control.pid, control.process_generation,
                                    deadline_ms) == 0;
    if (!recovered) return false;
    control.pid = 0;
    control.process_generation = 0U;
    control.device = 0U;
    control.channel = 0U;
    control.fenced = 1U;
    return driver_control_write(runtime, &control) == 0;
}

static bool driver_fence_apply(void *context) {
    supervisor_driver_runtime_t *runtime = context;
    uint64_t now_ms = pit_monotonic_ms();
    return runtime != NULL && driver_fence_until(
        runtime, deadline_after(now_ms, runtime->config.recovery_timeout_ms));
}

static bool driver_fence_verify(void *context) {
    supervisor_driver_runtime_t *runtime = context;
    supervisor_driver_control_t control;
    device_domain_status_t status;
    return runtime != NULL && driver_control_read(runtime, &control) == 0 &&
        control.active != 0U && control.fenced != 0U && control.pid == 0 &&
        control.process_generation == 0U && control.device == 0U &&
        device_domain_status(control.device_index, &status) == 0 &&
        status.state == DEVICE_DOMAIN_AVAILABLE && status.owner_pid == 0 &&
        status.owner_generation == 0U;
}

int supervisor_start_device_driver(
        const char *name, const char *path, uint32_t device_index,
        uint32_t mode, const supervisor_config_t *config, uint64_t now_ms,
        supervisor_handle_t *handle_out) {
    if (name == NULL || path == NULL || config == NULL || handle_out == NULL ||
        device_index >= DEVICE_DOMAIN_MAX_DEVICES ||
        (mode != DEVICE_DOMAIN_MODE_MEDIATED &&
         mode != DEVICE_DOMAIN_MODE_IOMMU_DIRECT) ||
        config->heartbeat_timeout_ms == 0U ||
        config->recovery_timeout_ms == 0U ||
        (config->cpu_affinity_mask &
         ~((1U << X86_CPU_LOCAL_MAX) - 1U)) != 0U) return -22;
    supervisor_driver_runtime_t *runtime = NULL;
    for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS; ++slot) {
        supervisor_driver_control_t control;
        if (driver_control_read(&driver_runtimes[slot], &control) == 0 &&
            control.active == 0U) {
            runtime = &driver_runtimes[slot];
            break;
        }
    }
    if (runtime == NULL) return -28;
    memset(runtime->name, 0, sizeof(runtime->name));
    memset(runtime->path, 0, sizeof(runtime->path));
    if (!copy_driver_string(runtime->name, sizeof(runtime->name), name) ||
        !copy_driver_string(runtime->path, sizeof(runtime->path), path))
        return -36;
    runtime->config = *config;
    supervisor_fence_ops_t fence = {
        .apply = driver_fence_apply,
        .verify = driver_fence_verify,
        .context = runtime,
    };
    supervisor_handle_t handle;
    if (supervisor_register(name, config, &fence, now_ms, &handle) != 0)
        return -5;
    supervisor_driver_control_t control = {
        .active = 1U,
        .administratively_enabled = 1U,
        .fenced = 1U,
        .device_index = device_index,
        .mode = mode,
        .supervisor = handle,
    };
    if (driver_control_write(runtime, &control) != 0 ||
        !driver_spawn_next(runtime, handle)) {
        (void)supervisor_force_isolate(handle);
        return -5;
    }
    *handle_out = handle;
    return 0;
}

int supervisor_set_device_driver_current_affinity(
        supervisor_handle_t handle, uint32_t cpu_affinity_mask) {
    if (cpu_affinity_mask == 0U) return -22;
    supervisor_driver_control_t control;
    supervisor_driver_runtime_t *runtime = driver_runtime_for_handle(
        handle, &control);
    if (runtime == NULL || control.active == 0U || control.pid <= 0 ||
        control.process_generation == 0U || control.fenced != 0U)
        return -3;
    control.post_ready_cpu_affinity_mask = cpu_affinity_mask;
    if (driver_control_write(runtime, &control) != 0)
        return SUPERVISOR_EINTEGRITY;
    return process_set_supervised_affinity(
        control.pid, control.process_generation, cpu_affinity_mask);
}

static void driver_diagnostic_serial(const char *name, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    serial_write_string(SERIAL_COM1, "REIST_DRIVER DIAGNOSTIC name=");
    uint32_t name_length = 0U;
    while (name != NULL && name[name_length] != '\0' &&
           name_length + 1U < SUPERVISOR_NAME_CAPACITY) {
        serial_write_char(SERIAL_COM1, name[name_length]);
        ++name_length;
    }
    serial_write_string(SERIAL_COM1, " value=");
    for (uint32_t nibble = 0U; nibble < 8U; ++nibble) {
        uint32_t shift = 28U - nibble * 4U;
        serial_write_char(SERIAL_COM1, hex[(value >> shift) & 0x0FU]);
    }
    serial_write_char(SERIAL_COM1, '\n');
}

int supervisor_device_driver_bootstrap(
        int pid, uint32_t process_generation,
        device_domain_driver_bootstrap_t *bootstrap) {
    if (pid <= 0 || process_generation == 0U || bootstrap == NULL) return -22;
    supervisor_driver_control_t control;
    if (driver_runtime_for_identity(pid, process_generation, &control) == NULL ||
        !process_identity_alive(pid, process_generation)) return -11;
    *bootstrap = (device_domain_driver_bootstrap_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*bootstrap),
        .device = control.device,
        .mode = control.mode,
        .session_slot = control.supervisor.slot,
        .session_generation = control.supervisor.generation,
        .session_epoch = control.supervisor.epoch,
    };
    return 0;
}

int supervisor_device_driver_report(
        int pid, uint32_t process_generation,
        const device_domain_driver_report_t *report, uint64_t now_ms) {
    if (pid <= 0 || process_generation == 0U || report == NULL ||
        report->version != DEVICE_DOMAIN_ABI_VERSION ||
        report->struct_size != sizeof(*report) || report->flags != 0U ||
        (report->report_type != DEVICE_DOMAIN_DRIVER_REPORT_SELF_TEST &&
         report->report_type != DEVICE_DOMAIN_DRIVER_REPORT_PROGRESS &&
         report->report_type != DEVICE_DOMAIN_DRIVER_REPORT_CHANNEL &&
         report->report_type != DEVICE_DOMAIN_DRIVER_REPORT_DIAGNOSTIC &&
         report->report_type !=
             DEVICE_DOMAIN_DRIVER_REPORT_STARTUP_PROGRESS))
        return -22;
    supervisor_driver_control_t control;
    supervisor_driver_runtime_t *runtime = driver_runtime_for_identity(
        pid, process_generation, &control);
    if (runtime == NULL || report->session_slot != control.supervisor.slot ||
        report->session_generation != control.supervisor.generation ||
        report->session_epoch != control.supervisor.epoch) return -13;
    int result;
    if (report->report_type == DEVICE_DOMAIN_DRIVER_REPORT_DIAGNOSTIC) {
        if (report->value == 0U) result = -22;
        else {
            driver_diagnostic_serial(runtime->name, report->value);
            result = 0;
        }
    } else if (report->report_type ==
               DEVICE_DOMAIN_DRIVER_REPORT_STARTUP_PROGRESS) {
        result = report->value != 0U
            ? supervisor_report_startup_progress(
                control.supervisor, report->value, now_ms)
            : -22;
    } else if (report->report_type == DEVICE_DOMAIN_DRIVER_REPORT_CHANNEL) {
        if (report->value == IPC_INVALID_HANDLE || control.channel != 0U ||
            ipc_capability_validate_owner(
                pid, process_generation, report->value,
                IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE | IPC_RIGHT_CONTROL) != 0)
            result = -13;
        else {
            control.channel = report->value;
            result = driver_control_write(runtime, &control);
        }
    } else if (report->report_type == DEVICE_DOMAIN_DRIVER_REPORT_SELF_TEST) {
        result = (report->value == 1U && control.channel != 0U)
            ? supervisor_report_self_test(control.supervisor, true, now_ms)
            : -5;
    } else {
        bool became_ready = control.fenced != 0U;
#ifdef REIST_HDA_SMP_LIFECYCLE_FAULT_INJECTION
        if (strcmp(runtime->name, "hda-ring3") == 0 &&
            x86_cpu_current_index() != 0U) {
            if (hda_fault_epoch == 0U &&
                __sync_bool_compare_and_swap(
                    &hda_fault_epoch, 0U, control.supervisor.epoch))
                printf("REIST_AUDIO HDA_TIMEOUT_ARMED epoch=%u\n",
                       control.supervisor.epoch);
            if (hda_fault_epoch == control.supervisor.epoch) return 0;
        }
#endif
#ifdef REIST_SVGA2D_SMP_LIFECYCLE_FAULT_INJECTION
        if (strcmp(runtime->name, "svga2d-ring3") == 0 &&
            svga2d_fault_epoch == control.supervisor.epoch) return 0;
#endif
        result = report->value != 0U
            ? supervisor_report_progress(
                control.supervisor, report->value, now_ms) : -22;
        if (result == 0 && control.fenced != 0U) {
            control.fenced = 0U;
            result = driver_control_write(runtime, &control);
        }
        if (result == 0 && became_ready) {
            if (control.post_ready_cpu_affinity_mask != 0U)
                result = process_set_supervised_affinity(
                    control.pid, control.process_generation,
                    control.post_ready_cpu_affinity_mask);
        }
        if (result == 0 && became_ready) {
            printf("REIST_DRIVER READY name=%s\n", runtime->name);
            if (strcmp(runtime->name, "svga2d-ring3") == 0)
                printf("REIST_VIDEO SVGA2D_READY\n");
            else if (strcmp(runtime->name, "nvidia-gk208-ring3") == 0)
                printf("REIST_VIDEO NVIDIA_GK208_READY\n");
        }
    }
    if (result != 0) (void)supervisor_force_isolate(control.supervisor);
    return result;
}

bool supervisor_device_driver_output_allowed(
        int pid, uint32_t process_generation, device_domain_handle_t device) {
    supervisor_driver_control_t control;
    supervisor_driver_runtime_t *runtime = driver_runtime_for_identity(
        pid, process_generation, &control);
    bool allowed = runtime != NULL && control.device == device &&
        control.fenced == 0U && supervisor_output_allowed(control.supervisor);
#ifndef REIST_HOST_TEST
    uint32_t cpu = x86_cpu_current_index();
    if (allowed && cpu != 0U && strcmp(runtime->name, "hda-ring3") == 0) {
        uint32_t prior = __sync_lock_test_and_set(
            &hda_ap_execution_reported, control.supervisor.epoch);
        if (prior != control.supervisor.epoch)
            printf("REIST_AUDIO HDA_AP_EXEC cpu=%u epoch=%u\n",
                   cpu, control.supervisor.epoch);
    }
#endif
    return allowed;
}

bool supervisor_device_driver_command_allowed(
        int pid, uint32_t process_generation, device_domain_handle_t device) {
    supervisor_driver_control_t control;
    supervisor_driver_runtime_t *runtime = driver_runtime_for_identity(
        pid, process_generation, &control);
    bool allowed = runtime != NULL &&
        (strcmp(runtime->name, "svga2d-ring3") == 0 ||
         strcmp(runtime->name, "nvidia-gk208-ring3") == 0) &&
        control.device == device && control.pid == pid &&
        control.process_generation == process_generation &&
        process_identity_alive(pid, process_generation);
#ifndef REIST_HOST_TEST
    uint32_t cpu = x86_cpu_current_index();
    if (allowed && cpu != 0U && strcmp(runtime->name, "svga2d-ring3") == 0) {
        uint32_t prior = __sync_lock_test_and_set(
            &svga2d_ap_execution_reported, control.supervisor.epoch);
        if (prior != control.supervisor.epoch)
            printf("REIST_VIDEO SVGA2D_AP_EXEC cpu=%u epoch=%u\n",
                   cpu, control.supervisor.epoch);
#ifdef REIST_SVGA2D_SMP_LIFECYCLE_FAULT_INJECTION
        if (svga2d_fault_epoch == 0U &&
            __sync_bool_compare_and_swap(
                &svga2d_fault_epoch, 0U, control.supervisor.epoch))
            printf("REIST_VIDEO SVGA2D_TIMEOUT_ARMED epoch=%u\n",
                   control.supervisor.epoch);
#endif
    }
#endif
    return allowed;
}

static supervisor_driver_runtime_t *driver_runtime_for_device(
        uint32_t device_index, supervisor_driver_control_t *control_out) {
    for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS; ++slot) {
        supervisor_driver_control_t control;
        if (driver_control_read(&driver_runtimes[slot], &control) != 0 ||
            control.active == 0U || control.device_index != device_index)
            continue;
        if (control_out != NULL) *control_out = control;
        return &driver_runtimes[slot];
    }
    return NULL;
}

bool supervisor_device_driver_component_ready(uint32_t device_index) {
    supervisor_driver_control_t control;
    return driver_runtime_for_device(device_index, &control) != NULL &&
        control.administratively_enabled != 0U && control.fenced == 0U &&
        control.pid > 0 && process_identity_alive(
            control.pid, control.process_generation) &&
        supervisor_output_allowed(control.supervisor);
}

bool supervisor_device_driver_component_down(uint32_t device_index,
                                             uint64_t deadline_ms) {
    supervisor_driver_control_t control;
    supervisor_driver_runtime_t *runtime = driver_runtime_for_device(
        device_index, &control);
    if (runtime == NULL || deadline_ms == 0U ||
        pit_monotonic_ms() >= deadline_ms) return false;
    if (control.pid == 0 && control.fenced != 0U) {
        control.administratively_enabled = 0U;
        return driver_control_write(runtime, &control) == 0 &&
            supervisor_admin_pause(control.supervisor) == 0;
    }
    control.administratively_enabled = 0U;
    if (driver_control_write(runtime, &control) != 0 ||
        !driver_fence_until(runtime, deadline_ms) ||
        driver_control_read(runtime, &control) != 0 ||
        supervisor_admin_pause(control.supervisor) != 0)
        return false;
    return pit_monotonic_ms() <= deadline_ms;
}

bool supervisor_device_driver_component_up(uint32_t device_index,
                                           uint64_t deadline_ms) {
    if (supervisor_device_driver_component_ready(device_index)) return true;
    supervisor_driver_control_t control;
    supervisor_driver_runtime_t *runtime = driver_runtime_for_device(
        device_index, &control);
    if (runtime == NULL || deadline_ms == 0U ||
        pit_monotonic_ms() >= deadline_ms || control.pid != 0 ||
        control.device != 0U || control.fenced == 0U) return false;
    supervisor_handle_t updated;
    if (supervisor_admin_start(control.supervisor, pit_monotonic_ms(),
                               &updated) != 0) return false;
    control.supervisor = updated;
    control.administratively_enabled = 1U;
    if (driver_control_write(runtime, &control) != 0 ||
        !driver_spawn_next(runtime, updated)) {
        (void)supervisor_force_isolate(updated);
        return false;
    }
    while (!supervisor_device_driver_component_ready(device_index) &&
           pit_monotonic_ms() < deadline_ms) {
        if (scheduler_sleep_ms(5U) != 0) (void)scheduler_yield();
    }
    if (supervisor_device_driver_component_ready(device_index)) return true;
    (void)supervisor_device_driver_component_down(device_index, deadline_ms);
    return false;
}

static void audio_service_abort_prepared_spawn(
        supervisor_audio_service_control_t *control, int pid) {
    if (pid > 0) {
        scheduler_preempt_disable();
        (void)process_terminate(pid);
        scheduler_preempt_enable();
    }
    if (control == NULL) return;
    control->pid = 0;
    control->process_generation = 0U;
    control->endpoint = 0U;
    control->client_pid = 0;
    control->client_generation = 0U;
    control->healthy = 0U;
    control->ready = 0U;
    control->fenced = 1U;
    (void)audio_service_control_write(control);
}

static bool audio_service_spawn_next(supervisor_handle_t handle) {
    supervisor_audio_service_control_t control;
    if (audio_service_control_read(&control) != 0 || control.active == 0U ||
        control.pid != 0 || control.endpoint != 0U || control.fenced == 0U)
        return false;
    const char *arguments[] = {SUPERVISOR_AUDIO_SERVICE_PATH};
    /* Loading may block in VFS.  The task therefore remains PREPARED until
     * its generation-scoped control record has been committed. */
    int pid = process_spawn_supervised_prepared(
        SUPERVISOR_AUDIO_SERVICE_PATH, 1, arguments,
        PROCESS_DOMAIN_AUDIO_SERVICE);
    uint32_t generation = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) {
        audio_service_abort_prepared_spawn(&control, pid);
        return false;
    }
    control.pid = pid;
    control.process_generation = generation;
    control.endpoint = 0U;
    control.client_pid = 0;
    control.client_generation = 0U;
    control.healthy = 0U;
    control.ready = 0U;
    control.fenced = 1U;
    control.supervisor = handle;
    if (audio_service_control_write(&control) != 0) {
        audio_service_abort_prepared_spawn(&control, pid);
        return false;
    }
    if (process_start_prepared_supervised(pid, generation) != 0) {
        audio_service_abort_prepared_spawn(&control, pid);
        return false;
    }
    return true;
}

static bool audio_service_return_to_bsp(
        const supervisor_audio_service_control_t *control) {
    if (control == NULL || control->pid <= 0 ||
        control->post_ready_cpu_affinity_mask == 0U ||
        !process_identity_alive(control->pid, control->process_generation))
        return true;
    return process_set_supervised_affinity(
               control->pid, control->process_generation,
               TASK_CPU_MASK_BSP) == 0 && scheduler_sleep_ms(1U) == 0;
}

static bool audio_service_fence_apply_internal(bool already_bsp) {
    supervisor_audio_service_control_t control;
    if (audio_service_control_read(&control) != 0 || control.active == 0U)
        return false;
    if (!already_bsp && !audio_service_return_to_bsp(&control)) return false;
    if (control.pid > 0 && process_identity_alive(
            control.pid, control.process_generation))
        (void)process_terminate(control.pid);
    control.pid = 0;
    control.process_generation = 0U;
    control.endpoint = 0U;
    control.client_pid = 0;
    control.client_generation = 0U;
    control.healthy = 0U;
    control.ready = 0U;
    control.fenced = 1U;
    return audio_service_control_write(&control) == 0;
}

static bool audio_service_fence_apply(void *context) {
    (void)context;
    return audio_service_fence_apply_internal(false);
}

static bool audio_service_fence_verify(void *context) {
    (void)context;
    supervisor_audio_service_control_t control;
    return audio_service_control_read(&control) == 0 &&
        control.active != 0U && control.fenced != 0U &&
        control.pid == 0 && control.process_generation == 0U &&
        control.endpoint == 0U && control.healthy == 0U &&
        control.ready == 0U;
}

/* Rotate a completed client session without classifying normal application
 * lifetime as an audio-service failure.  The old service process is still
 * fenced and reaped before a new generation is published, so queued replies
 * and delegated endpoint rights can never cross client identities. */
static bool audio_service_rotate_session(supervisor_handle_t handle) {
    supervisor_handle_t isolate_handle = handle;
    supervisor_handle_t updated = {0};
    supervisor_audio_service_control_t control;
    bool rotated = false;
    bool launch_allowed = false;

    if (audio_service_control_read(&control) != 0 ||
        !audio_service_return_to_bsp(&control))
        return false;

    scheduler_preempt_disable();
    do {
        if (!audio_service_fence_apply_internal(true) ||
            !audio_service_fence_verify(&audio_service_runtime) ||
            supervisor_admin_pause(handle) != 0 ||
            supervisor_admin_start(handle, pit_monotonic_ms(), &updated) != 0)
            break;
        isolate_handle = updated;
        if (audio_service_control_read(&control) != 0 ||
            control.active == 0U || control.fenced == 0U || control.pid != 0)
            break;
        control.supervisor = updated;
        if (audio_service_control_write(&control) != 0) break;
        launch_allowed = true;
    } while (0);
    scheduler_preempt_enable();
    if (launch_allowed && audio_service_spawn_next(updated)) rotated = true;
    if (!rotated) {
        (void)supervisor_force_isolate(isolate_handle);
        return false;
    }
    printf("REIST_AUDIO SERVICE_SESSION_ROTATED\n");
    return true;
}

bool supervisor_start_audio_service(uint32_t device_index, uint64_t now_ms) {
    if (device_index >= DEVICE_DOMAIN_MAX_DEVICES) return false;
    supervisor_audio_service_control_t control;
    if (audio_service_control_read(&control) != 0 || control.active != 0U)
        return false;
    const supervisor_config_t config = {
        .heartbeat_timeout_ms = 2000U,
        .recovery_timeout_ms = 1000U,
        .restart_budget = 3U,
    };
    const supervisor_fence_ops_t fence = {
        .apply = audio_service_fence_apply,
        .verify = audio_service_fence_verify,
        .context = &audio_service_runtime,
    };
    supervisor_handle_t handle;
    if (supervisor_register("audio-service", &config, &fence, now_ms,
                            &handle) != 0) return false;
    audio_service_runtime.config = config;
    control = (supervisor_audio_service_control_t){
        .active = 1U,
        .fenced = 1U,
        .device_index = device_index,
        .supervisor = handle,
    };
    if (audio_service_control_write(&control) != 0 ||
        !audio_service_spawn_next(handle)) {
        (void)supervisor_force_isolate(handle);
        return false;
    }
    return true;
}

static int audio_service_report_if_identity(
        int pid, uint32_t generation, uint32_t report_type, uint32_t value,
        uint64_t now_ms, bool *matched) {
    if (matched == NULL) return -22;
    *matched = false;
    supervisor_audio_service_control_t control;
    if (audio_service_control_read(&control) != 0 || control.active == 0U ||
        control.pid != pid || control.process_generation != generation)
        return -9;
    *matched = true;
    if (!process_identity_alive(pid, generation)) return -9;
    int result = -22;
    if (report_type == REIST_REPORT_DIAGNOSTIC) {
        if (value == 0U) result = -22;
        else {
            printf("REIST_AUDIO SERVICE_DIAGNOSTIC value=%08X\n", value);
            result = 0;
        }
    } else if (report_type == REIST_REPORT_SELF_TEST) {
        if (value == IPC_INVALID_HANDLE || control.endpoint != 0U ||
            ipc_capability_validate_owner(
                pid, generation, value,
                IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE | IPC_RIGHT_CONTROL) != 0)
            result = -13;
        else {
            control.endpoint = value;
            result = audio_service_control_write(&control);
            if (result == 0)
                result = supervisor_report_self_test(
                    control.supervisor, true, now_ms);
        }
    } else if (report_type == REIST_REPORT_PROGRESS) {
#ifdef REIST_AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION
        if (control.ready != 0U && x86_cpu_current_index() != 0U) {
            if (audio_service_fault_epoch == 0U &&
                __sync_bool_compare_and_swap(
                    &audio_service_fault_epoch, 0U, control.supervisor.epoch))
                printf("REIST_AUDIO SERVICE_TIMEOUT_ARMED epoch=%u\n",
                       control.supervisor.epoch);
            if (audio_service_fault_epoch == control.supervisor.epoch) return 0;
        }
#endif
        result = value == 0U ? -22 : supervisor_report_progress(
            control.supervisor, value, now_ms);
        if (result == 0 && control.fenced != 0U) {
            control.fenced = 0U;
            control.healthy = 1U;
            result = audio_service_control_write(&control);
        }
#ifndef REIST_HOST_TEST
        if (result == 0 && control.ready != 0U &&
            x86_cpu_current_index() != 0U) {
            uint32_t prior = __sync_lock_test_and_set(
                &audio_service_ap_execution_epoch, control.supervisor.epoch);
            if (prior != control.supervisor.epoch)
                printf("REIST_AUDIO SERVICE_AP_EXEC cpu=%u epoch=%u\n",
                       x86_cpu_current_index(), control.supervisor.epoch);
        }
#endif
    } else if (report_type == REIST_REPORT_SERVICE_READY) {
        if (value != 1U || control.fenced != 0U ||
            control.healthy == 0U || control.ready != 0U ||
            control.endpoint == IPC_INVALID_HANDLE) result = -13;
        else {
            control.ready = 1U;
            result = audio_service_control_write(&control);
            if (result == 0) printf("REIST_AUDIO SERVICE_READY\n");
            if (result == 0 && control.post_ready_cpu_affinity_mask != 0U)
                result = process_set_supervised_affinity(
                    control.pid, control.process_generation,
                    control.post_ready_cpu_affinity_mask);
        }
    } else if (report_type == REIST_REPORT_AUDIO_CLIENT_RELEASED) {
        if (value != 1U || control.fenced != 0U ||
            control.healthy == 0U || control.ready == 0U ||
            control.endpoint == IPC_INVALID_HANDLE) result = -13;
        else if (ipc_endpoint_validate_quiescent_owner(
                     control.pid, control.process_generation,
                     control.endpoint) != 0) result = -13;
        else if (control.client_pid == 0 &&
                 control.client_generation == 0U) result = 0;
        else if (control.client_pid <= 0 ||
                 control.client_generation == 0U) result = -84;
        else {
            control.client_pid = 0;
            control.client_generation = 0U;
            result = audio_service_control_write(&control);
            if (result == 0) printf("REIST_AUDIO CLIENT_RELEASED\n");
        }
    }
    if (result != 0) (void)supervisor_force_isolate(control.supervisor);
    return result;
}

int supervisor_set_audio_service_current_affinity(uint32_t cpu_affinity_mask) {
    if (cpu_affinity_mask == 0U) return -22;
    supervisor_audio_service_control_t control;
    if (audio_service_control_read(&control) != 0 || control.active == 0U ||
        control.pid <= 0 || control.process_generation == 0U ||
        control.fenced != 0U || control.healthy == 0U || control.ready == 0U)
        return -3;
    control.post_ready_cpu_affinity_mask = cpu_affinity_mask;
    if (audio_service_control_write(&control) != 0)
        return SUPERVISOR_EINTEGRITY;
    return process_set_supervised_affinity(
        control.pid, control.process_generation, cpu_affinity_mask);
}

static void audio_service_monitor_process(void) {
    supervisor_audio_service_control_t control;
    if (audio_service_control_read(&control) == 0 && control.active != 0U &&
        control.pid > 0 &&
        !process_identity_alive(control.pid, control.process_generation))
        (void)supervisor_force_isolate(control.supervisor);
}

static bool audio_service_event(supervisor_event_t event) {
    supervisor_audio_service_control_t control;
    if (audio_service_control_read(&control) != 0 || control.active == 0U ||
        event.handle.slot != control.supervisor.slot ||
        event.handle.generation != control.supervisor.generation) return false;
    if (event.type == SUPERVISOR_EVENT_RESTART_REQUIRED) {
        control.supervisor = event.handle;
        bool restarted = audio_service_control_write(&control) == 0 &&
            audio_service_spawn_next(event.handle);
        if (!restarted) (void)supervisor_force_isolate(event.handle);
        else printf("REIST_AUDIO SERVICE_RESTARTED\n");
    } else if (event.type == SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) {
        if (audio_service_runtime.reported_safe_generation !=
                event.handle.generation ||
            audio_service_runtime.reported_safe_epoch != event.handle.epoch) {
            audio_service_runtime.reported_safe_generation =
                event.handle.generation;
            audio_service_runtime.reported_safe_epoch = event.handle.epoch;
            printf("REIST_AUDIO SERVICE_DEGRADED\n");
        }
    }
    return true;
}

static void compositor_abort_prepared_spawn(
        supervisor_compositor_control_t *control, int pid) {
    if (pid > 0) (void)process_terminate(pid);
    if (control == NULL) return;
    control->pid = 0;
    control->process_generation = 0U;
    control->healthy = 0U;
    control->ready = 0U;
    control->stop_requested = 0U;
    control->fenced = 1U;
    (void)compositor_control_write(control);
}

static bool compositor_spawn_next(supervisor_handle_t handle) {
    supervisor_compositor_control_t control;
    if (compositor_control_read(&control) != 0 || control.active == 0U ||
        control.administratively_enabled == 0U || control.pid != 0 ||
        control.fenced == 0U) return false;
#ifdef REIST_SOUNDPLAYER_SURFACE_PROBE
    const char *arguments[] = {"desktop.prg", "--sound-probe"};
    const int argument_count = 2;
#else
    const char *arguments[] = {"desktop.prg"};
    const int argument_count = 1;
#endif
    int pid = process_spawn_supervised_prepared(
        SUPERVISOR_COMPOSITOR_PATH, argument_count, arguments,
        PROCESS_DOMAIN_COMPOSITOR);
    uint32_t generation = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) {
        compositor_abort_prepared_spawn(&control, pid);
        return false;
    }
    control.pid = pid;
    control.process_generation = generation;
    control.healthy = 0U;
    control.ready = 0U;
    control.stop_requested = 0U;
    control.fenced = 1U;
    control.supervisor = handle;
    if (compositor_control_write(&control) != 0 ||
        process_start_prepared_supervised(pid, generation) != 0) {
        compositor_abort_prepared_spawn(&control, pid);
        return false;
    }
    return true;
}

static bool compositor_fence_until(uint64_t deadline_ms) {
    supervisor_compositor_control_t control;
    if (compositor_control_read(&control) != 0 || control.active == 0U)
        return false;
    if (control.pid > 0 && process_identity_alive(
            control.pid, control.process_generation)) {
        if (deadline_ms == 0U || pit_monotonic_ms() >= deadline_ms ||
            process_set_supervised_affinity(
                control.pid, control.process_generation,
                TASK_CPU_MASK_BSP) != 0)
            return false;
        uint64_t now_ms = pit_monotonic_ms();
        if (now_ms >= deadline_ms || scheduler_sleep_ms(1U) != 0 ||
            pit_monotonic_ms() >= deadline_ms)
            return false;
    }
    /* Revoke only publication owned by this compositor generation. The
     * independently supervised display driver retains the live scanout; its
     * device state must not be invalidated by a compositor-only restart. */
    framebuffer_frame_process_cleanup(
        control.pid, control.process_generation);
    if (control.pid > 0 && process_identity_alive(
            control.pid, control.process_generation))
        (void)process_terminate(control.pid);
    control.pid = 0;
    control.process_generation = 0U;
    control.healthy = 0U;
    control.ready = 0U;
    control.stop_requested = 0U;
    control.fenced = 1U;
    return compositor_control_write(&control) == 0;
}

static bool compositor_fence_apply(void *context) {
    supervisor_compositor_runtime_t *runtime = context;
    uint64_t now_ms = pit_monotonic_ms();
    return runtime != NULL && compositor_fence_until(
        deadline_after(now_ms, runtime->config.recovery_timeout_ms));
}

static bool compositor_fence_verify(void *context) {
    (void)context;
    supervisor_compositor_control_t control;
    return compositor_control_read(&control) == 0 && control.active != 0U &&
        control.fenced != 0U && control.pid == 0 &&
        control.process_generation == 0U && control.healthy == 0U &&
        control.ready == 0U;
}

bool supervisor_start_compositor(uint64_t now_ms,
                                 uint32_t post_ready_cpu_affinity_mask,
                                 int *pid_out) {
    if (pid_out == NULL) return false;
    *pid_out = 0;
#ifdef REIST_COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION
    x86_smp_status_t smp_status;
    x86_smp_status(&smp_status);
    if (smp_status.online_cpu_count <= 1U) return false;
    post_ready_cpu_affinity_mask =
        ((1U << smp_status.online_cpu_count) - 1U) & ~TASK_CPU_MASK_BSP;
#endif
    if ((post_ready_cpu_affinity_mask & 0xFFFF0001U) != 0U) return false;
    supervisor_compositor_control_t control;
    if (compositor_control_read(&control) != 0 || control.active != 0U)
        return false;
    const supervisor_config_t config = {
        .heartbeat_timeout_ms = 2000U,
        .recovery_timeout_ms = 1000U,
        .restart_budget = 3U,
        .startup_timeout_ms = 30000U,
    };
    const supervisor_fence_ops_t fence = {
        .apply = compositor_fence_apply,
        .verify = compositor_fence_verify,
        .context = &compositor_runtime,
    };
    supervisor_handle_t handle;
    if (supervisor_register("session-compositor", &config, &fence, now_ms,
                            &handle) != 0) return false;
    compositor_runtime.config = config;
    control = (supervisor_compositor_control_t){
        .active = 1U,
        .administratively_enabled = 1U,
        .fenced = 1U,
        .supervisor = handle,
        .post_ready_cpu_affinity_mask = post_ready_cpu_affinity_mask,
    };
    if (compositor_control_write(&control) != 0 ||
        !compositor_spawn_next(handle)) {
        (void)supervisor_force_isolate(handle);
        return false;
    }
    if (compositor_control_read(&control) != 0 || control.pid <= 0 ||
        control.process_generation == 0U) {
        (void)supervisor_force_isolate(handle);
        return false;
    }
    *pid_out = control.pid;
    return true;
}

bool supervisor_compositor_session_active(void) {
    supervisor_compositor_control_t control;
    return compositor_control_read(&control) == 0 && control.active != 0U &&
        control.administratively_enabled != 0U;
}

static int compositor_report_if_identity(
        int pid, uint32_t generation, uint32_t report_type, uint32_t value,
        uint64_t now_ms, bool *matched) {
    if (matched == NULL) return -22;
    *matched = false;
    supervisor_compositor_control_t control;
    if (compositor_control_read(&control) != 0 || control.active == 0U ||
        control.pid != pid || control.process_generation != generation)
        return -9;
    *matched = true;
    if (!process_identity_alive(pid, generation)) return -9;
    int result = -22;
    if (report_type == REIST_REPORT_SELF_TEST) {
        if (value != 1U || control.healthy != 0U || control.ready != 0U)
            result = -13;
        else result = supervisor_report_self_test(
            control.supervisor, true, now_ms);
    } else if (report_type == REIST_REPORT_PROGRESS) {
        if (value == 0U) result = -22;
        else {
#ifndef REIST_HOST_TEST
            uint32_t cpu = x86_cpu_current_index();
            if (control.ready != 0U && cpu != 0U) {
                uint32_t prior = __sync_lock_test_and_set(
                    &compositor_ap_execution_generation,
                    control.process_generation);
                if (prior != control.process_generation)
                    printf("REIST_GUI COMPOSITOR_AP_EXEC cpu=%u generation=%u\n",
                           cpu, control.process_generation);
#ifdef REIST_COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION
                if (compositor_fault_epoch == 0U &&
                    __sync_bool_compare_and_swap(
                        &compositor_fault_epoch, 0U,
                        control.supervisor.epoch))
                    printf("REIST_GUI COMPOSITOR_TIMEOUT_ARMED epoch=%u\n",
                           control.supervisor.epoch);
                if (compositor_fault_epoch == control.supervisor.epoch)
                    return 0;
#endif
            }
#endif
            result = supervisor_report_progress(
                control.supervisor, value, now_ms);
        }
        if (result == 0 && control.fenced != 0U) {
            control.fenced = 0U;
            control.healthy = 1U;
            result = compositor_control_write(&control);
        }
    } else if (report_type == REIST_REPORT_SERVICE_READY) {
        if (value != 1U || control.fenced != 0U ||
            control.healthy == 0U || control.ready != 0U) result = -13;
        else {
            uint32_t post_ready_cpu_affinity_mask =
                control.post_ready_cpu_affinity_mask;
            control.ready = 1U;
            result = compositor_control_write(&control);
            if (result == 0) printf("REIST_GUI COMPOSITOR_READY generation=%u\n",
                                    control.process_generation);
            if (result == 0 && post_ready_cpu_affinity_mask != 0U)
                result = process_set_supervised_affinity(
                    control.pid, control.process_generation,
                    post_ready_cpu_affinity_mask);
        }
    } else if (report_type == REIST_REPORT_DIAGNOSTIC &&
               value == SUPERVISOR_COMPOSITOR_STOP_DIAGNOSTIC &&
               control.ready != 0U) {
        control.stop_requested = 1U;
        result = compositor_control_write(&control);
    }
    if (result != 0) (void)supervisor_force_isolate(control.supervisor);
    return result;
}

static void compositor_monitor_process(void) {
    supervisor_compositor_control_t control;
    if (compositor_control_read(&control) != 0 || control.active == 0U ||
        control.pid <= 0 || process_identity_alive(
            control.pid, control.process_generation)) return;
    if (control.stop_requested != 0U) {
        if (display_control_graphics_active()) {
            (void)supervisor_force_isolate(control.supervisor);
            return;
        }
        control.pid = 0;
        control.process_generation = 0U;
        control.healthy = 0U;
        control.ready = 0U;
        control.stop_requested = 0U;
        control.fenced = 1U;
        control.administratively_enabled = 0U;
        if (compositor_control_write(&control) != 0) output_fence_all();
        return;
    }
    (void)supervisor_force_isolate(control.supervisor);
}

static bool compositor_event(supervisor_event_t event) {
    supervisor_compositor_control_t control;
    if (compositor_control_read(&control) != 0 || control.active == 0U ||
        event.handle.slot != control.supervisor.slot ||
        event.handle.generation != control.supervisor.generation) return false;
    if (event.type == SUPERVISOR_EVENT_RESTART_REQUIRED) {
        control.supervisor = event.handle;
        bool restarted = control.administratively_enabled != 0U &&
            compositor_control_write(&control) == 0 &&
            compositor_spawn_next(event.handle);
        if (!restarted) (void)supervisor_force_isolate(event.handle);
        else printf("REIST_GUI COMPOSITOR_RESTARTED epoch=%u\n",
                    event.handle.epoch);
    } else if (event.type == SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) {
        control.administratively_enabled = 0U;
        control.post_ready_cpu_affinity_mask = 0U;
        (void)compositor_control_write(&control);
        if (compositor_runtime.reported_safe_generation !=
                event.handle.generation ||
            compositor_runtime.reported_safe_epoch != event.handle.epoch) {
            compositor_runtime.reported_safe_generation =
                event.handle.generation;
            compositor_runtime.reported_safe_epoch = event.handle.epoch;
            printf("REIST_GUI COMPOSITOR_DEGRADED\n");
        }
    }
    return true;
}

static void driver_monitor_processes(void) {
    for (uint32_t slot = 0U; slot < SUPERVISOR_MAX_DEVICE_DRIVERS; ++slot) {
        supervisor_driver_control_t control;
        if (driver_control_read(&driver_runtimes[slot], &control) != 0 ||
            control.active == 0U || control.pid <= 0) continue;
        if (!process_identity_alive(control.pid, control.process_generation)) {
            (void)supervisor_force_isolate(control.supervisor);
        }
    }
}

static bool driver_service_event(supervisor_event_t event) {
    supervisor_driver_control_t control;
    supervisor_driver_runtime_t *runtime = driver_runtime_for_handle(
        event.handle, &control);
    if (runtime == NULL) return false;
    if (event.type == SUPERVISOR_EVENT_RESTART_REQUIRED) {
        control.supervisor = event.handle;
        bool restarted = driver_control_write(runtime, &control) == 0 &&
            driver_spawn_next(runtime, event.handle);
        if (!restarted)
            (void)supervisor_force_isolate(event.handle);
        else if (strcmp(runtime->name, "hda-ring3") == 0)
            printf("REIST_AUDIO DRIVER_RESTARTED\n");
        else if (restarted &&
                 (strcmp(runtime->name, "svga2d-ring3") == 0 ||
                  strcmp(runtime->name, "nvidia-gk208-ring3") == 0))
            printf("REIST_VIDEO DRIVER_RESTARTED\n");
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
        if (restarted && strcmp(runtime->name,
                               "driver-fault-recovery") == 0) {
            uint32_t marker = event.handle.epoch == 2U ? 1U :
                event.handle.epoch == 3U ? 2U :
                event.handle.epoch == 4U ? 4U : 0U;
            if (marker != 0U &&
                (__sync_fetch_and_or(&driver_fault_markers, marker) &
                 marker) == 0U) {
                printf(event.handle.epoch == 2U
                    ? "DRIVER_DOMAIN CRASH_RECOVERED\n"
                    : event.handle.epoch == 3U
                        ? "DRIVER_DOMAIN HANG_RECOVERED\n"
                        : "DRIVER_DOMAIN STALE_GENERATION_REJECTED\n");
            }
        }
#endif
    }
    if (event.type == SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) {
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
        uint32_t marker = strcmp(runtime->name,
            "driver-fault-recovery") == 0 ? 8U :
            strcmp(runtime->name, "driver-fault-reset") == 0 ? 16U : 0U;
        if (marker != 0U &&
            (__sync_fetch_and_or(&driver_fault_markers, marker) & marker) ==
                0U) {
            printf(marker == 8U
                ? "DRIVER_DOMAIN RESTART_BUDGET_EXHAUSTED\n"
                : "DRIVER_DOMAIN RESET_FAILURE_FENCED\n");
        }
#endif
        if (strcmp(runtime->name, "hda-ring3") == 0 &&
            (runtime->reported_safe_generation != event.handle.generation ||
             runtime->reported_safe_epoch != event.handle.epoch)) {
            runtime->reported_safe_generation = event.handle.generation;
            runtime->reported_safe_epoch = event.handle.epoch;
            printf("REIST_AUDIO DRIVER_DEGRADED\n");
        }
    }
    return true;
}

static void supervisor_worker(void) {
    static uint64_t next_arp_scrub_ms;
    for (;;) {
        /* Bounded network bottom half: IRQ handlers only acknowledge and set
         * pending flags, so foreground progress must not depend on a shell
         * command happening to poll the NIC. */
        netdev_poll();
        uint64_t component_now_ms = pit_monotonic_ms();
        device_domain_poll(component_now_ms);
        storage_service_poll(component_now_ms);
        component_control_poll(component_now_ms);
        driver_monitor_processes();
        audio_service_monitor_process();
        compositor_monitor_process();
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
        if (authority_expiry == 1) {
            int cleanup = supervisor_protected_network_context_clear(
                &probe_runtime.network_probe_context);
            control.network_epoch = 0U;
            if (cleanup == 0)
                cleanup = supervisor_protected_probe_control_write(
                    &probe_runtime.control, &control);
            if (cleanup != 0) authority_expiry = cleanup;
        }
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
                REIST_NETWORK_TRACE_PRINT(
                    "REIST_NETWORK ARP_BINDING_EXPIRED %u\n", expired);
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
        if (probe_administratively_enabled && control.active != 0U &&
            control.fenced == 0U &&
            !process_identity_alive(control.pid, control.process_generation)) {
            (void)supervisor_force_isolate(control.handle);
        }
        supervisor_event_t result = supervisor_service_one(pit_monotonic_ms());
        bool driver_event = driver_service_event(result);
        bool audio_event = audio_service_event(result);
        bool compositor_lifecycle_event = compositor_event(result);
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
        if (result.type == SUPERVISOR_EVENT_SAFE_STATE_REQUIRED &&
            !driver_event && !audio_event && !compositor_lifecycle_event) {
            /* Until per-hazard external interlocks are registered, the
             * conservative system response revokes every known output. */
            output_fence_all();
        }
        if (scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS) != 0)
            (void)scheduler_yield();
    }
}

bool supervisor_start_worker(void) {
    if (!component_control_init()) return false;
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
        domain_kind != PROCESS_DOMAIN_STORAGE &&
        domain_kind != PROCESS_DOMAIN_DRIVER &&
        domain_kind != PROCESS_DOMAIN_AUDIO_SERVICE &&
        domain_kind != PROCESS_DOMAIN_COMPOSITOR) return -1;
    return process_spawn_supervised(path, argc, argv,
                                    (process_domain_kind_t)domain_kind);
}
#else
bool supervisor_start_worker(void) {
    return false;
}

bool supervisor_start_compositor(uint64_t now_ms,
                                 uint32_t post_ready_cpu_affinity_mask,
                                 int *pid_out) {
    (void)now_ms;
    (void)post_ready_cpu_affinity_mask;
    if (pid_out != NULL) *pid_out = 0;
    return false;
}

bool supervisor_compositor_session_active(void) {
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

int supervisor_set_device_driver_current_affinity(
        supervisor_handle_t handle, uint32_t cpu_affinity_mask) {
    (void)handle; (void)cpu_affinity_mask;
    return -1;
}

int supervisor_start_device_driver(
        const char *name, const char *path, uint32_t device_index,
        uint32_t mode, const supervisor_config_t *config, uint64_t now_ms,
        supervisor_handle_t *handle_out) {
    (void)name; (void)path; (void)device_index; (void)mode; (void)config;
    (void)now_ms; (void)handle_out;
    return -1;
}

bool supervisor_start_audio_service(uint32_t device_index, uint64_t now_ms) {
    (void)device_index;
    (void)now_ms;
    return false;
}

int supervisor_device_driver_bootstrap(
        int pid, uint32_t process_generation,
        device_domain_driver_bootstrap_t *bootstrap) {
    (void)pid; (void)process_generation; (void)bootstrap;
    return -1;
}

int supervisor_device_driver_report(
        int pid, uint32_t process_generation,
        const device_domain_driver_report_t *report, uint64_t now_ms) {
    (void)pid; (void)process_generation; (void)report; (void)now_ms;
    return -1;
}

bool supervisor_device_driver_output_allowed(
        int pid, uint32_t process_generation, device_domain_handle_t device) {
    (void)pid; (void)process_generation; (void)device;
    return false;
}

bool supervisor_device_driver_command_allowed(
        int pid, uint32_t process_generation, device_domain_handle_t device) {
    (void)pid; (void)process_generation; (void)device;
    return false;
}

bool supervisor_device_driver_component_down(uint32_t device_index,
                                             uint64_t deadline_ms) {
    (void)device_index; (void)deadline_ms;
    return false;
}

bool supervisor_device_driver_component_up(uint32_t device_index,
                                           uint64_t deadline_ms) {
    (void)device_index; (void)deadline_ms;
    return false;
}

bool supervisor_device_driver_component_ready(uint32_t device_index) {
    (void)device_index;
    return false;
}

bool supervisor_start_probe(uint64_t now_ms) {
    (void)now_ms;
    return false;
}

int supervisor_set_network_service_current_affinity(
        uint32_t cpu_affinity_mask) {
    (void)cpu_affinity_mask;
    return -1;
}

bool supervisor_probe_ready(void) {
    return false;
}

bool supervisor_probe_component_down(uint64_t deadline_ms) {
    (void)deadline_ms;
    return false;
}

bool supervisor_probe_component_up(uint64_t deadline_ms) {
    (void)deadline_ms;
    return false;
}

bool supervisor_probe_component_ready(void) {
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
        supervisor_begin_startup(&state, now_ms);
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
    if (resolve(handle, &state) != 0 || now_ms >= state.deadline_ms ||
        now_ms >= state.startup_deadline_ms ||
        (state.state != SUPERVISOR_RECOVERING &&
         state.state != SUPERVISOR_STARTING)) {
        supervisor_unlock(flags);
        return -1;
    }
    state.state = passed ? SUPERVISOR_STARTING : SUPERVISOR_ISOLATED;
    state.progress_marker = 0;
    state.deadline_ms = startup_window_deadline(&state, now_ms);
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
