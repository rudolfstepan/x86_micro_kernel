/**
 * @file test/test_supervisor_host.c
 * @brief Hostseitiger Regressionstest für supervisor.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "include/kernel/supervisor.h"

static bool fence_applied;
static bool fence_verified = true;
static uint32_t fence_apply_count;

static bool apply_fence(void *context) {
    (void)context;
    fence_applied = true;
    ++fence_apply_count;
    return true;
}

static bool verify_fence(void *context) {
    (void)context;
    return fence_applied && fence_verified;
}

int main(void) {
    supervisor_config_t config = {
        .heartbeat_timeout_ms = 100U,
        .recovery_timeout_ms = 50U,
        .restart_budget = 1U,
    };
    supervisor_handle_t handle;
    supervisor_fence_ops_t fence_ops = {
        .apply = apply_fence, .verify = verify_fence, .context = 0,
    };
    supervisor_init();
    if (supervisor_register("nvidia-gk208-ring3", &config, &fence_ops,
                            1000U, &handle) != 0) return 1;
    if (supervisor_output_allowed(handle)) return 2;
    if (supervisor_report_progress(handle, 1U, 1010U) != 0) return 3;
    if (!supervisor_output_allowed(handle)) return 4;
    if (supervisor_report_progress(handle, 1U, 1020U) == 0) return 5;
    if (supervisor_poll(1109U).type != SUPERVISOR_EVENT_NONE) return 6;

    supervisor_clock_tick(1110U);
    supervisor_event_t fence = supervisor_poll(1110U);
    if (fence.type != SUPERVISOR_EVENT_FENCE_REQUIRED) return 7;
    if (supervisor_poll(1111U).type != SUPERVISOR_EVENT_FENCE_REQUIRED) return 25;
    if (supervisor_output_allowed(handle)) return 8;
    supervisor_event_t restart = supervisor_apply_fence(fence.handle, 1120U);
    if (restart.type != SUPERVISOR_EVENT_RESTART_REQUIRED) return 9;
    if (supervisor_poll(1120U).type != SUPERVISOR_EVENT_RESTART_REQUIRED)
        return 26;
    if (fence_apply_count != 1U) return 23;
    if (supervisor_apply_fence(fence.handle, 1120U).type !=
        SUPERVISOR_EVENT_NONE || fence_apply_count != 1U) return 24;
    if (restart.handle.epoch == handle.epoch) return 10;
    if (supervisor_report_progress(handle, 2U, 1121U) == 0) return 11;
    if (supervisor_report_self_test(restart.handle, true, 1130U) != 0) return 12;
    if (supervisor_report_progress(restart.handle, 1U, 1131U) != 0) return 13;
    if (!supervisor_output_allowed(restart.handle)) return 14;

    fence = supervisor_poll(1231U);
    if (fence.type != SUPERVISOR_EVENT_FENCE_REQUIRED) return 15;
    supervisor_event_t safe = supervisor_apply_fence(fence.handle, 1240U);
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) return 16;
    if (supervisor_poll(1240U).type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED)
        return 27;
    if (supervisor_output_allowed(safe.handle)) return 17;

    supervisor_handle_t failed_handle;
    fence_applied = false;
    fence_verified = false;
    if (supervisor_register("actuator", &config, &fence_ops, 2000U,
                            &failed_handle) != 0) return 18;
    if (supervisor_report_progress(failed_handle, 1U, 2001U) != 0) return 19;
    safe = supervisor_service_one(2101U);
    if (!fence_applied) return 20;
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) return 21;
    if (supervisor_output_allowed(failed_handle)) return 22;

    supervisor_init();
    config.restart_budget = 0U;
    fence_applied = false;
    fence_verified = true;
    if (supervisor_register("idle-domain", &config, &fence_ops, 3000U,
                            &handle) != 0) return 28;
    if (supervisor_report_progress(handle, 1U, 3001U) != 0 ||
        supervisor_report_idle(handle) != 0 ||
        supervisor_report_idle(handle) != 0 ||
        !supervisor_output_allowed(handle)) return 29;
    supervisor_clock_tick(UINT64_MAX);
    if (supervisor_poll(UINT64_MAX).type != SUPERVISOR_EVENT_NONE) return 30;
    if (supervisor_report_progress(handle, 2U, 4000U) != 0) return 31;
    supervisor_clock_tick(4100U);
    safe = supervisor_service_one(4100U);
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED) return 32;

    supervisor_init();
    config.restart_budget = 1U;
    fence_applied = false;
    if (supervisor_register("corrected-ops", &config, &fence_ops, 5000U,
                            &handle) != 0 ||
        supervisor_report_progress(handle, 1U, 5001U) != 0 ||
        supervisor_test_corrupt_fence_ops(handle, false) != 0) return 33;
    supervisor_clock_tick(5101U);
    restart = supervisor_service_one(5101U);
    if (restart.type != SUPERVISOR_EVENT_RESTART_REQUIRED || !fence_applied)
        return 34;

    supervisor_init();
    fence_applied = false;
    if (supervisor_register("broken-ops", &config, &fence_ops, 6000U,
                            &handle) != 0 ||
        supervisor_report_progress(handle, 1U, 6001U) != 0 ||
        supervisor_test_corrupt_fence_ops(handle, true) != 0) return 35;
    supervisor_clock_tick(6101U);
    safe = supervisor_service_one(6101U);
    if (safe.type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED || fence_applied)
        return 36;

    supervisor_init();
    if (supervisor_register("corrected-slot", &config, &fence_ops, 7000U,
                            &handle) != 0 ||
        supervisor_test_corrupt_descriptor(handle, false) != 0 ||
        supervisor_report_progress(handle, 1U, 7001U) != 0) return 37;
    if (!supervisor_output_allowed(handle)) return 38;

    supervisor_init();
    if (supervisor_register("broken-slot", &config, &fence_ops, 8000U,
                            &handle) != 0 ||
        supervisor_test_corrupt_descriptor(handle, true) != 0) return 39;
    if (supervisor_poll(8001U).type != SUPERVISOR_EVENT_SAFE_STATE_REQUIRED)
        return 40;

    supervisor_probe_authority_t authority;
    uint32_t probe_id = 0U;
    supervisor_probe_authority_init(&authority);
    if (supervisor_probe_authority_begin(&authority, 100U, 10U,
                                         &probe_id) != 0 || probe_id != 1U)
        return 41;
    if (supervisor_probe_authority_begin(&authority, 101U, 10U,
                                         &probe_id) != -11) return 42;
    if (!supervisor_probe_authority_take(&authority, 109U, &probe_id) ||
        probe_id != 1U ||
        supervisor_probe_authority_take(&authority, 109U, &probe_id)) return 43;
    if (supervisor_probe_authority_begin(&authority, 110U, 10U,
                                         &probe_id) != 0 || probe_id != 2U ||
        supervisor_probe_authority_expire(&authority, 119U) ||
        !supervisor_probe_authority_expire(&authority, 120U) ||
        supervisor_probe_authority_take(&authority, 120U, &probe_id)) return 44;
    authority.next_id = UINT32_MAX;
    if (supervisor_probe_authority_begin(&authority, UINT64_MAX - 5U, 10U,
                                         &probe_id) != 0 ||
        probe_id != UINT32_MAX || authority.deadline_ms != UINT64_MAX ||
        !supervisor_probe_authority_take(&authority, UINT64_MAX - 1U,
                                         &probe_id) ||
        supervisor_probe_authority_begin(&authority, 0U, 1U,
                                         &probe_id) != -75) return 45;

    supervisor_protected_probe_authority_t protected_authority;
    if (supervisor_protected_probe_authority_init(&protected_authority) != 0 ||
        supervisor_protected_probe_authority_begin(
            &protected_authority, 200U, 10U, &probe_id) != 0 ||
        supervisor_test_corrupt_probe_authority(
            &protected_authority, false) != 0 ||
        supervisor_protected_probe_authority_take(
            &protected_authority, 205U, &probe_id) != 0 || probe_id != 1U)
        return 50;
    if (supervisor_protected_probe_authority_init(&protected_authority) != 0 ||
        supervisor_test_corrupt_probe_authority(
            &protected_authority, true) != 0 ||
        supervisor_protected_probe_authority_begin(
            &protected_authority, 200U, 10U, &probe_id) !=
            SUPERVISOR_EINTEGRITY) return 51;
    if (supervisor_protected_probe_authority_init(&protected_authority) != 0 ||
        supervisor_protected_probe_authority_expire_epoch(
            &protected_authority, 300U, 99U) != 0 ||
        supervisor_protected_probe_authority_begin_epoch(
            &protected_authority, 300U, 10U, 7U, &probe_id) != 0 ||
        probe_id != 1U ||
        supervisor_protected_probe_authority_take_epoch(
            &protected_authority, 301U, 2U, &probe_id) != -13 ||
        supervisor_protected_probe_authority_take_epoch(
            &protected_authority, 301U, 7U, &probe_id) != 0) return 56;

    supervisor_protected_network_context_t protected_context;
    supervisor_network_probe_context_t context_snapshot;
    const uint8_t local_mac[6] = {2U, 1U, 2U, 3U, 4U, 5U};
    if (supervisor_protected_network_context_init(&protected_context) != 0 ||
        supervisor_protected_network_context_prepare(
            &protected_context, 0x0A000202U, 0x0A00020FU, local_mac) != 0 ||
        supervisor_test_corrupt_network_context(
            &protected_context, false) != 0 ||
        supervisor_protected_network_context_snapshot(
            &protected_context, &context_snapshot) != 0 ||
        context_snapshot.gateway != 0x0A000202U ||
        context_snapshot.local_ip != 0x0A00020FU ||
        supervisor_protected_network_context_publish(
            &protected_context, 7U) != 0 ||
        supervisor_protected_network_context_consume(
            &protected_context, 8U) != -13 ||
        supervisor_protected_network_context_consume(
            &protected_context, 7U) != 0) return 52;
    if (supervisor_protected_network_context_prepare_epoch(
            &protected_context, 7U, 0x0A000202U, 0x0A00020FU,
            local_mac) != 0 ||
        supervisor_protected_network_context_publish_epoch(
            &protected_context, 8U, 10U) != -13 ||
        supervisor_protected_network_context_publish_epoch(
            &protected_context, 7U, 10U) != 0 ||
        supervisor_protected_network_context_consume_epoch(
            &protected_context, 8U, 10U) != -13 ||
        supervisor_protected_network_context_consume_epoch(
            &protected_context, 7U, 10U) != 0) return 57;
    if (supervisor_protected_network_context_init(&protected_context) != 0 ||
        supervisor_test_corrupt_network_context(
            &protected_context, true) != 0 ||
        supervisor_protected_network_context_snapshot(
            &protected_context, &context_snapshot) !=
            SUPERVISOR_EINTEGRITY ||
        supervisor_protected_network_context_publish(
            &protected_context, 9U) != SUPERVISOR_EINTEGRITY) return 53;

    supervisor_protected_arp_reply_context_t protected_reply;
    supervisor_arp_reply_context_t reply_snapshot;
    const uint8_t peer_mac[6] = {2U, 9U, 8U, 7U, 6U, 5U};
    if (supervisor_protected_arp_reply_context_init(&protected_reply) != 0 ||
        supervisor_protected_arp_reply_context_publish(
            &protected_reply, 11U, 7U, 0x0A000203U, peer_mac) != 0 ||
        supervisor_test_corrupt_arp_reply_context(
            &protected_reply, false) != 0 ||
        supervisor_protected_arp_reply_context_snapshot(
            &protected_reply, &reply_snapshot) != 0 ||
        reply_snapshot.request_id != 11U ||
        reply_snapshot.transaction_epoch != 7U ||
        reply_snapshot.target_ip != 0x0A000203U ||
        supervisor_protected_arp_reply_context_clear(
            &protected_reply) != 0) return 58;
    if (supervisor_protected_arp_reply_context_publish(
            &protected_reply, 12U, 7U, 0x0A000203U, peer_mac) != 0 ||
        supervisor_test_corrupt_arp_reply_context(
            &protected_reply, true) != 0 ||
        supervisor_protected_arp_reply_context_snapshot(
            &protected_reply, &reply_snapshot) !=
            SUPERVISOR_EINTEGRITY) return 59;

    supervisor_protected_icmp_echo_context_t protected_icmp;
    supervisor_icmp_echo_context_t icmp_snapshot;
    const uint8_t echo_data[4] = {0x52U, 0x45U, 0x49U, 0x53U};
    if (supervisor_protected_icmp_echo_context_init(&protected_icmp) != 0 ||
        supervisor_protected_icmp_echo_context_publish(
            &protected_icmp, 13U, 7U, 0x0A000204U, peer_mac,
            0x1234U, 0x5678U, echo_data, sizeof(echo_data)) != 0 ||
        supervisor_protected_icmp_echo_context_snapshot(
            &protected_icmp, &icmp_snapshot) != 0 ||
        icmp_snapshot.request_id != 13U ||
        icmp_snapshot.transaction_epoch != 7U ||
        icmp_snapshot.source_ip != 0x0A000204U ||
        icmp_snapshot.identifier != 0x1234U ||
        icmp_snapshot.sequence != 0x5678U ||
        icmp_snapshot.data_length != sizeof(echo_data) ||
        icmp_snapshot.data[3] != 0x53U ||
        supervisor_protected_icmp_echo_context_publish(
            &protected_icmp, 14U, 7U, 0x0A000204U, peer_mac,
            0U, 0U, echo_data, SUPERVISOR_ICMP_ECHO_MAX_DATA + 1U) != -22 ||
        supervisor_protected_icmp_echo_context_clear(&protected_icmp) != 0 ||
        supervisor_protected_icmp_echo_context_snapshot(
            &protected_icmp, &icmp_snapshot) != 0 ||
        icmp_snapshot.request_id != 0U) return 60;

    supervisor_protected_dhcp_context_t protected_dhcp;
    supervisor_dhcp_context_t dhcp_snapshot;
    if (supervisor_protected_dhcp_context_init(&protected_dhcp) != 0 ||
        supervisor_protected_dhcp_context_publish(
            &protected_dhcp, 17U, 7U, 0x0A00020FU, 0xFFFFFF00U,
            0x0A000202U, 0x0A000203U, 3600U) != 0 ||
        supervisor_protected_dhcp_context_snapshot(
            &protected_dhcp, &dhcp_snapshot) != 0 ||
        dhcp_snapshot.request_id != 17U ||
        dhcp_snapshot.transaction_epoch != 7U ||
        dhcp_snapshot.ip_address != 0x0A00020FU ||
        dhcp_snapshot.netmask != 0xFFFFFF00U ||
        dhcp_snapshot.gateway != 0x0A000202U ||
        dhcp_snapshot.lease_seconds != 3600U ||
        supervisor_protected_dhcp_context_publish(
            &protected_dhcp, 18U, 7U, 0x0A00020FU, 0xFF00FF00U,
            0x0A000202U, 0U, 3600U) != -22 ||
        supervisor_protected_dhcp_context_clear(&protected_dhcp) != 0 ||
        supervisor_protected_dhcp_context_snapshot(
            &protected_dhcp, &dhcp_snapshot) != 0 ||
        dhcp_snapshot.request_id != 0U) return 61;

    supervisor_protected_dhcp_lease_t protected_lease;
    supervisor_dhcp_lease_t lease_snapshot;
    if (supervisor_protected_dhcp_lease_init(&protected_lease) != 0 ||
        supervisor_protected_dhcp_lease_publish(
            &protected_lease, 7U, 0x0A00020FU, 3600U, 5000U) != 0 ||
        supervisor_protected_dhcp_lease_snapshot(
            &protected_lease, &lease_snapshot) != 0 ||
        lease_snapshot.process_generation != 7U ||
        lease_snapshot.ip_address != 0x0A00020FU ||
        lease_snapshot.lease_seconds != 3600U ||
        lease_snapshot.deadline_ms != 5000U ||
        supervisor_protected_dhcp_lease_publish(
            &protected_lease, 7U, 0x0A00020FU, 59U, 5000U) != -22 ||
        supervisor_protected_dhcp_lease_publish(
            &protected_lease, 7U, 0x0A00020FU, 3600U, 0U) != -22 ||
        supervisor_protected_dhcp_lease_clear(&protected_lease) != 0 ||
        supervisor_protected_dhcp_lease_snapshot(
            &protected_lease, &lease_snapshot) != 0 ||
        lease_snapshot.ip_address != 0U) return 63;

    supervisor_protected_dhcp_renewal_t protected_renewal;
    supervisor_dhcp_renewal_t renewal_snapshot;
    if (supervisor_protected_dhcp_renewal_init(&protected_renewal) != 0 ||
        supervisor_protected_dhcp_renewal_publish(
            &protected_renewal, SUPERVISOR_DHCP_RENEW, 7U, 23U,
            0x0A00020FU, 6000U) != 0 ||
        supervisor_protected_dhcp_renewal_snapshot(
            &protected_renewal, &renewal_snapshot) != 0 ||
        renewal_snapshot.active != 1U ||
        renewal_snapshot.operation != SUPERVISOR_DHCP_RENEW ||
        renewal_snapshot.process_generation != 7U ||
        renewal_snapshot.transaction_id != 23U ||
        renewal_snapshot.ip_address != 0x0A00020FU ||
        renewal_snapshot.deadline_ms != 6000U ||
        supervisor_protected_dhcp_renewal_publish(
            &protected_renewal, 3U, 7U, 23U, 0x0A00020FU, 6000U) != -22 ||
        supervisor_protected_dhcp_renewal_clear(&protected_renewal) != 0 ||
        supervisor_protected_dhcp_renewal_snapshot(
            &protected_renewal, &renewal_snapshot) != 0 ||
        renewal_snapshot.active != 0U) return 64;

    supervisor_protected_dhcp_boot_t protected_boot;
    supervisor_dhcp_boot_t boot_snapshot;
    if (supervisor_protected_dhcp_boot_init(&protected_boot) != 0 ||
        supervisor_protected_dhcp_boot_publish(
            &protected_boot, SUPERVISOR_DHCP_BOOT_DISCOVER_SENT, 7U, 31U,
            0U, 0U, 7000U) != 0 ||
        supervisor_protected_dhcp_boot_snapshot(
            &protected_boot, &boot_snapshot) != 0 ||
        boot_snapshot.active != 1U ||
        boot_snapshot.phase != SUPERVISOR_DHCP_BOOT_DISCOVER_SENT ||
        boot_snapshot.process_generation != 7U ||
        boot_snapshot.transaction_id != 31U ||
        boot_snapshot.offered_ip != 0U || boot_snapshot.server_id != 0U ||
        supervisor_protected_dhcp_boot_publish(
            &protected_boot, SUPERVISOR_DHCP_BOOT_REQUEST_SENT, 7U, 31U,
            0x0A00020FU, 0x0A000202U, 7000U) != 0 ||
        supervisor_protected_dhcp_boot_snapshot(
            &protected_boot, &boot_snapshot) != 0 ||
        boot_snapshot.phase != SUPERVISOR_DHCP_BOOT_REQUEST_SENT ||
        boot_snapshot.offered_ip != 0x0A00020FU ||
        boot_snapshot.server_id != 0x0A000202U ||
        supervisor_protected_dhcp_boot_publish(
            &protected_boot, SUPERVISOR_DHCP_BOOT_REQUEST_SENT, 7U, 31U,
            0U, 0x0A000202U, 7000U) != -22 ||
        supervisor_protected_dhcp_boot_clear(&protected_boot) != 0 ||
        supervisor_protected_dhcp_boot_snapshot(
            &protected_boot, &boot_snapshot) != 0 ||
        boot_snapshot.active != 0U) return 65;

    supervisor_protected_udp_echo_context_t protected_udp;
    supervisor_udp_echo_context_t udp_snapshot;
    if (supervisor_protected_udp_echo_context_init(&protected_udp) != 0 ||
        supervisor_protected_udp_echo_context_publish(
            &protected_udp, 19U, 7U, 0x0A000204U, peer_mac, 40000U,
            SUPERVISOR_UDP_ECHO_PORT, echo_data, sizeof(echo_data)) != 0 ||
        supervisor_protected_udp_echo_context_snapshot(
            &protected_udp, &udp_snapshot) != 0 ||
        udp_snapshot.request_id != 19U ||
        udp_snapshot.transaction_epoch != 7U ||
        udp_snapshot.source_port != 40000U ||
        udp_snapshot.destination_port != SUPERVISOR_UDP_ECHO_PORT ||
        udp_snapshot.data_length != sizeof(echo_data) ||
        udp_snapshot.data[3] != 0x53U ||
        supervisor_protected_udp_echo_context_publish(
            &protected_udp, 20U, 7U, 0x0A000204U, peer_mac, 0U,
            SUPERVISOR_UDP_ECHO_PORT, echo_data, sizeof(echo_data)) != -22 ||
        supervisor_protected_udp_echo_context_clear(&protected_udp) != 0 ||
        supervisor_protected_udp_echo_context_snapshot(
            &protected_udp, &udp_snapshot) != 0 ||
        udp_snapshot.request_id != 0U) return 62;

    supervisor_protected_probe_control_t protected_control;
    supervisor_probe_control_t control = {
        .active = 1U,
        .handle = {.slot = 1U, .generation = 2U, .epoch = 3U},
        .pid = 42,
        .process_generation = 7U,
        .launch_count = 1U,
        .endpoint_handle = 9U,
        .network_epoch = 7U,
        .last_network_probe_ms = 123U,
    };
    supervisor_probe_control_t control_snapshot;
    if (supervisor_protected_probe_control_init(&protected_control) != 0 ||
        supervisor_protected_probe_control_write(
            &protected_control, &control) != 0 ||
        supervisor_test_corrupt_probe_control(
            &protected_control, false) != 0 ||
        supervisor_protected_probe_control_read(
            &protected_control, &control_snapshot) != 0 ||
        control_snapshot.pid != 42 || control_snapshot.endpoint_handle != 9U)
        return 54;
    if (supervisor_protected_probe_control_init(&protected_control) != 0 ||
        supervisor_test_corrupt_probe_control(
            &protected_control, true) != 0 ||
        supervisor_protected_probe_control_read(
            &protected_control, &control_snapshot) !=
            SUPERVISOR_EINTEGRITY ||
        supervisor_protected_probe_control_write(
            &protected_control, &control) != SUPERVISOR_EINTEGRITY) return 55;

    supervisor_network_degradation_stats_t stats;
    supervisor_network_degradation_init(&stats);
    supervisor_network_degradation_record(
        &stats, SUPERVISOR_NETWORK_DEGRADED_EXPIRED);
    supervisor_network_degradation_record(
        &stats, SUPERVISOR_NETWORK_DEGRADED_QUEUE);
    supervisor_network_degradation_record(
        &stats, SUPERVISOR_NETWORK_DEGRADED_SEMANTIC);
    if (stats.expired != 1U || stats.queue_fallback != 1U ||
        stats.semantic_reject != 1U) return 46;
    stats.semantic_reject = UINT32_MAX;
    supervisor_network_degradation_record(
        &stats, SUPERVISOR_NETWORK_DEGRADED_SEMANTIC);
    if (stats.semantic_reject != UINT32_MAX) return 47;

    supervisor_init();
    if (supervisor_test_record_network_degradation(
            SUPERVISOR_NETWORK_DEGRADED_QUEUE) != 0 ||
        supervisor_test_corrupt_network_degradation(false) != 0 ||
        supervisor_network_degradation_snapshot(&stats) != 0 ||
        stats.queue_fallback != 1U) return 48;
    supervisor_init();
    if (supervisor_test_record_network_degradation(
            SUPERVISOR_NETWORK_DEGRADED_EXPIRED) != 0 ||
        supervisor_test_corrupt_network_degradation(true) != 0 ||
        supervisor_network_degradation_snapshot(&stats) !=
            SUPERVISOR_EINTEGRITY) return 49;
    return 0;
}
