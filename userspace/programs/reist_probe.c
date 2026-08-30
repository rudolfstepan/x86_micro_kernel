/**
 * @file userspace/programs/reist_probe.c
 * @brief Implementiert den überwachten Ring-3-Netzwerkdienst.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include <stdint.h>
#include <stdbool.h>
#include "x86os.h"
#include "reist_dhcp_parser.h"
#include "reist_dhcp_state.h"
#include "reist_icmp_parser.h"
#include "reist_ipv4_parser.h"
#include "reist_udp_parser.h"
#include "reist_tcp_parser.h"

#define REIST_DHCP_BOOT_MAX_ATTEMPTS 3U
#define REIST_DHCP_BOOT_RETRY_MS 1600U
#define REIST_DHCP_BOOT_CYCLE_DELAY_MS 5000U
#define REIST_NETWORK_RX_BATCH 8U
#define REIST_WCET_MINIMUM_SAMPLES 64U
#define REIST_WCET_MAX_ATTEMPTS 768U
#define REIST_WCET_SAMPLE_INTERVAL_MS 20U
#define REIST_WCET_SAMPLE_DEADLINE_MS 15000U
#define REIST_TCP_FLAG_FIN 0x01U
#define REIST_TCP_FLAG_SYN 0x02U

static uint64_t probe_deadline_after(uint64_t now_ms, uint32_t interval_ms) {
    return UINT64_MAX - now_ms < interval_ms
        ? UINT64_MAX : now_ms + interval_ms;
}

static x86os_reist_network_frame_t network_frame;

static void reject_wcet_baseline(uint32_t reason) {
    (void)x86os_reist_report(X86OS_REIST_REPORT_WCET_REJECT, reason);
}

static int text_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int report_startup(x86os_ipc_handle_t *endpoint) {
    if (x86os_ipc_create(endpoint) != 0 || *endpoint == 0U) return -1;
    if (x86os_reist_report(X86OS_REIST_REPORT_SELF_TEST, *endpoint) != 0)
        return -1;
    return x86os_reist_report(X86OS_REIST_REPORT_PROGRESS, 1U);
}

static void message_init(x86os_ipc_message_t *message, const char *text) {
    message->version = X86OS_IPC_MESSAGE_VERSION;
    message->struct_size = sizeof(*message);
    message->length = 0U;
    while (text[message->length] != '\0' &&
           message->length < X86OS_IPC_MAX_MESSAGE_SIZE) {
        message->payload[message->length] = (uint8_t)text[message->length];
        ++message->length;
    }
}

#define SERVICE_PROTOCOL_HEADER_SIZE 8U

static uint32_t message_request_id(const x86os_ipc_message_t *message) {
    if (message->length < SERVICE_PROTOCOL_HEADER_SIZE ||
        message->payload[0] != 'R' || message->payload[1] != 'Q' ||
        message->payload[2] != '1' || message->payload[3] != 0U)
        return 0U;
    return (uint32_t)message->payload[4] |
           ((uint32_t)message->payload[5] << 8U) |
           ((uint32_t)message->payload[6] << 16U) |
           ((uint32_t)message->payload[7] << 24U);
}

static int message_request_is(const x86os_ipc_message_t *message,
                              const char *text) {
    uint32_t request_id = message_request_id(message);
    if (request_id == 0U) return 0;
    uint32_t length = 0U;
    while (text[length] != '\0') ++length;
    if (message->length != SERVICE_PROTOCOL_HEADER_SIZE + length) return 0;
    for (uint32_t index = 0U; index < length; ++index) {
        if (message->payload[SERVICE_PROTOCOL_HEADER_SIZE + index] !=
            (uint8_t)text[index]) return 0;
    }
    return 1;
}

static void response_init(x86os_ipc_message_t *message, uint32_t request_id,
                          const char *text) {
    message_init(message, "");
    message->payload[0] = 'R';
    message->payload[1] = 'S';
    message->payload[2] = '1';
    message->payload[3] = 0U;
    message->payload[4] = (uint8_t)request_id;
    message->payload[5] = (uint8_t)(request_id >> 8U);
    message->payload[6] = (uint8_t)(request_id >> 16U);
    message->payload[7] = (uint8_t)(request_id >> 24U);
    message->length = SERVICE_PROTOCOL_HEADER_SIZE;
    while (*text != '\0' && message->length < X86OS_IPC_MAX_MESSAGE_SIZE)
        message->payload[message->length++] = (uint8_t)*text++;
}

static uint32_t payload_be32(const x86os_ipc_message_t *message,
                             uint32_t offset) {
    return ((uint32_t)message->payload[offset] << 24U) |
           ((uint32_t)message->payload[offset + 1U] << 16U) |
           ((uint32_t)message->payload[offset + 2U] << 8U) |
           message->payload[offset + 3U];
}

static bool dhcp_proposal_valid(const x86os_ipc_message_t *message) {
    if (message->length != 28U) return false;
    uint32_t request_id = (uint32_t)message->payload[4] |
        ((uint32_t)message->payload[5] << 8U) |
        ((uint32_t)message->payload[6] << 16U) |
        ((uint32_t)message->payload[7] << 24U);
    uint32_t ip = payload_be32(message, 8U);
    uint32_t mask = payload_be32(message, 12U);
    uint32_t gateway = payload_be32(message, 16U);
    uint32_t dns = payload_be32(message, 20U);
    uint32_t lease_seconds = payload_be32(message, 24U);
    if (request_id == 0U || ip == 0U || ip == 0xFFFFFFFFU || mask == 0U ||
        mask == 0xFFFFFFFFU || gateway == 0xFFFFFFFFU ||
        dns == 0xFFFFFFFFU || lease_seconds < 60U ||
        lease_seconds > 604800U) return false;
    uint32_t host_mask = ~mask;
    uint32_t host = ip & host_mask;
    if ((host_mask & (host_mask + 1U)) != 0U || host == 0U ||
        host == host_mask) return false;
    if (gateway != 0U) {
        uint32_t gateway_host = gateway & host_mask;
        if ((gateway & mask) != (ip & mask) || gateway_host == 0U ||
            gateway_host == host_mask) return false;
    }
    return true;
}

static bool dhcp_schedule_valid(const x86os_ipc_message_t *message) {
    if (message->length != 28U) return false;
    uint32_t ip_address = payload_be32(message, 4U);
    uint32_t lease_ms = payload_be32(message, 8U);
    uint32_t renew_ms = payload_be32(message, 12U);
    uint32_t rebind_ms = payload_be32(message, 16U);
    uint32_t operation = payload_be32(message, 20U);
    uint32_t request_id = payload_be32(message, 24U);
    return ip_address != 0U && ip_address != UINT32_MAX &&
        renew_ms != 0U && renew_ms < rebind_ms && rebind_ms < lease_ms &&
        operation <= X86OS_REIST_DHCP_REBIND && request_id != 0U;
}

static bool udp_proposal_valid(const x86os_ipc_message_t *message) {
    if (message->length < 28U || message->length > 60U) return false;
    uint32_t binding = (uint32_t)message->payload[4] |
        ((uint32_t)message->payload[5] << 8U) |
        ((uint32_t)message->payload[6] << 16U) |
        ((uint32_t)message->payload[7] << 24U);
    uint32_t request_id = (uint32_t)message->payload[8] |
        ((uint32_t)message->payload[9] << 8U) |
        ((uint32_t)message->payload[10] << 16U) |
        ((uint32_t)message->payload[11] << 24U);
    uint32_t source_ip = payload_be32(message, 12U);
    uint16_t source_port = ((uint16_t)message->payload[22] << 8U) |
                           message->payload[23];
    uint16_t destination_port = ((uint16_t)message->payload[24] << 8U) |
                                message->payload[25];
    uint16_t data_length = (uint16_t)message->payload[26] |
                           ((uint16_t)message->payload[27] << 8U);
    bool nonzero_mac = false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (message->payload[16U + index] != 0U) nonzero_mac = true;
    return binding != 0U && request_id != 0U && source_ip != 0U &&
        source_ip != 0xFFFFFFFFU && source_port != 0U &&
        destination_port >= 1024U && nonzero_mac &&
        (message->payload[16] & 1U) == 0U && data_length <= 32U &&
        message->length == 28U + data_length;
}

static x86os_reist_udp_binding_t udp_binding_for_port(
        const x86os_reist_udp_binding_t bindings[4],
        const uint16_t ports[4], uint16_t destination_port) {
    for (uint32_t index = 0U; index < 4U; ++index) {
        if (bindings[index] != 0U && ports[index] == destination_port)
            return bindings[index];
    }
    return 0U;
}

static const char *network_classification(
        const x86os_ipc_message_t *message) {
    /* NET1/NETR/NETQ followed by one complete Ethernet+ARP header. This deliberately
     * bounded v1 parser performs no allocation and publishes no output. */
    if (message->length < 18U || message->payload[0] != 'N' ||
        message->payload[1] != 'E' || message->payload[2] != 'T' ||
        (message->payload[3] != '1' && message->payload[3] != 'R' &&
         message->payload[3] != 'A' &&
         message->payload[3] != 'Q' && message->payload[3] != 'I' &&
         message->payload[3] != 'D' &&
         message->payload[3] != 'L' &&
         message->payload[3] != 'U' && message->payload[3] != 'V' &&
         message->payload[3] != 'X'))
        return NULL;
    if (message->payload[3] == 'A')
        return message->length == 26U ? "REIST_ARP_RESOLUTION" : NULL;
    if (message->payload[3] == 'I') {
        if (message->length < 24U || message->length > 56U) return NULL;
        uint32_t request_id = (uint32_t)message->payload[4] |
            ((uint32_t)message->payload[5] << 8U) |
            ((uint32_t)message->payload[6] << 16U) |
            ((uint32_t)message->payload[7] << 24U);
        uint32_t source_ip = ((uint32_t)message->payload[8] << 24U) |
            ((uint32_t)message->payload[9] << 16U) |
            ((uint32_t)message->payload[10] << 8U) |
            message->payload[11];
        uint16_t data_length = (uint16_t)message->payload[22] |
            ((uint16_t)message->payload[23] << 8U);
        bool nonzero_mac = false;
        for (uint32_t index = 0U; index < 6U; ++index)
            if (message->payload[12U + index] != 0U) nonzero_mac = true;
        if (request_id == 0U || source_ip == 0U ||
            source_ip == 0xFFFFFFFFU || !nonzero_mac ||
            (message->payload[12] & 1U) != 0U || data_length > 32U ||
            message->length != 24U + data_length) return NULL;
        return "REIST_ICMP_ECHO";
    }
    if (message->payload[3] == 'D')
        return dhcp_proposal_valid(message) ? "REIST_DHCP_CONFIG" : NULL;
    if (message->payload[3] == 'L')
        return dhcp_schedule_valid(message) ? "REIST_DHCP_LEASE" : NULL;
    if (message->payload[3] == 'U')
        return NULL;
    if (message->payload[3] == 'V')
        return udp_proposal_valid(message) ? "REIST_UDP_DATAGRAM" : NULL;
    uint16_t ethertype = ((uint16_t)message->payload[16] << 8) |
                         message->payload[17];
    if (ethertype == 0x0806U) {
        if (message->length < 46U || message->payload[18] != 0U ||
            message->payload[19] != 1U || message->payload[20] != 0x08U ||
            message->payload[21] != 0U || message->payload[22] != 6U ||
            message->payload[23] != 4U || message->payload[24] != 0U ||
            (message->payload[25] != 1U && message->payload[25] != 2U))
            return NULL;
        if (message->payload[3] == 'Q') {
            if (message->length < 60U || message->payload[25] != 1U)
                return NULL;
            uint32_t request_id = (uint32_t)message->payload[56] |
                ((uint32_t)message->payload[57] << 8U) |
                ((uint32_t)message->payload[58] << 16U) |
                ((uint32_t)message->payload[59] << 24U);
            if (request_id == 0U) return NULL;
            bool source_nonzero = false;
            bool destination_is_broadcast = true;
            bool target_mac_zero = true;
            bool target_mac_local = true;
            for (uint32_t index = 0U; index < 6U; ++index) {
                if (message->payload[26U + index] != 0U)
                    source_nonzero = true;
                if (message->payload[4U + index] != 0xFFU)
                    destination_is_broadcast = false;
                if (message->payload[36U + index] != 0U)
                    target_mac_zero = false;
                if (message->payload[36U + index] !=
                    message->payload[50U + index])
                    target_mac_local = false;
                if (message->payload[26U + index] !=
                    message->payload[10U + index]) return NULL;
            }
            if (!source_nonzero || (message->payload[26] & 1U) != 0U ||
                (!target_mac_zero && !target_mac_local)) return NULL;
            for (uint32_t index = 0U; index < 4U; ++index)
                if (message->payload[42U + index] !=
                    message->payload[46U + index]) return NULL;
            if (!destination_is_broadcast) {
                for (uint32_t index = 0U; index < 6U; ++index)
                    if (message->payload[4U + index] !=
                        message->payload[50U + index]) return NULL;
            }
        } else if (message->payload[3] != '1') {
            if (message->length < 60U || message->payload[25] != 2U)
                return NULL;
            for (uint32_t index = 0U; index < 4U; ++index) {
                if (message->payload[32U + index] !=
                        message->payload[46U + index] ||
                    message->payload[42U + index] !=
                        message->payload[50U + index]) return NULL;
            }
            for (uint32_t index = 0U; index < 6U; ++index) {
                if (message->payload[4U + index] !=
                        message->payload[54U + index] ||
                    message->payload[36U + index] !=
                        message->payload[54U + index] ||
                    message->payload[10U + index] !=
                        message->payload[26U + index]) return NULL;
            }
        }
        return "REIST_NET_ARP";
    }
    if (ethertype == 0x0800U) return "REIST_NET_IPV4";
    return "REIST_NET_OTHER";
}

static uint32_t message_probe_id(const x86os_ipc_message_t *message) {
    if (message->length < 64U || message->payload[3] != 'R') return 0U;
    return (uint32_t)message->payload[60] |
           ((uint32_t)message->payload[61] << 8U) |
           ((uint32_t)message->payload[62] << 16U) |
           ((uint32_t)message->payload[63] << 24U);
}

static int message_is_network_ingress(const x86os_ipc_message_t *message) {
    return message->length >= 4U && message->payload[0] == 'N' &&
        message->payload[1] == 'E' && message->payload[2] == 'T' &&
        message->payload[3] == 'R';
}

/* The supervisor is the only authority that can launch this binary with the
 * DRIVER profile.  This fixed fault client exercises the generic lifecycle in
 * a QEMU fault build; normal PROBE launches are denied DEVICE_CONTROL and
 * continue into the network service below. */
static int run_driver_domain_fault_client(
        const x86os_device_driver_bootstrap_t *bootstrap) {
    if (bootstrap == NULL || bootstrap->version != X86OS_DEVICE_ABI_VERSION ||
        bootstrap->struct_size != sizeof(*bootstrap) ||
        bootstrap->device == 0U || bootstrap->session_epoch == 0U) return 50;
    uint32_t fixture = bootstrap->device & 0xFFU;
    if (fixture != 1U && fixture != 2U) return 51;

    if (fixture == 1U && bootstrap->session_epoch == 4U) {
        if (x86os_device_driver_report(
                bootstrap, X86OS_DEVICE_DRIVER_REPORT_SELF_TEST, 0U) != -5)
            return 52;
        for (;;) (void)x86os_sleep_ms(1000U);
    }
    if (x86os_device_driver_report(
            bootstrap, X86OS_DEVICE_DRIVER_REPORT_SELF_TEST, 1U) != 0 ||
        x86os_device_driver_report(
            bootstrap, X86OS_DEVICE_DRIVER_REPORT_PROGRESS, 1U) != 0)
        return 53;
    (void)x86os_sleep_ms(30U);

    if (fixture == 2U || bootstrap->session_epoch == 1U) {
        __asm__ volatile("ud2");
        return 54;
    }
    if (bootstrap->session_epoch == 2U) {
        for (;;) (void)x86os_sleep_ms(1000U);
    }
    if (bootstrap->session_epoch == 3U) {
        x86os_device_driver_bootstrap_t stale = *bootstrap;
        --stale.session_epoch;
        return x86os_device_driver_report(
            &stale, X86OS_DEVICE_DRIVER_REPORT_PROGRESS, 2U) == -13 ? 55 : 56;
    }
    return 57;
}

int main(int argc, char **argv) {
    x86os_device_driver_bootstrap_t driver_bootstrap = {0};
    if (x86os_device_driver_bootstrap(&driver_bootstrap) == 0)
        return run_driver_domain_fault_client(&driver_bootstrap);
    if (argc != 2) return 2;
    x86os_ipc_handle_t endpoint = 0U;
    if (report_startup(&endpoint) != 0) return 3;

    if (text_equal(argv[1], "crash")) {
        (void)x86os_sleep_ms(30U);
        __asm__ volatile("ud2");
        return 4;
    }
    if (text_equal(argv[1], "hang")) {
        for (;;) (void)x86os_sleep_ms(1000U);
    }
    if (text_equal(argv[1], "invalid")) {
        (void)x86os_sleep_ms(30U);
        (void)x86os_reist_report(X86OS_REIST_REPORT_INVALID, 0U);
        for (;;) (void)x86os_sleep_ms(1000U);
    }
    if (!text_equal(argv[1], "healthy")) return 5;

    x86os_reist_udp_binding_t udp_bindings[4] = {0U, 0U, 0U, 0U};
    reist_dhcp_state_t dhcp_state;
    reist_dhcp_state_init(&dhcp_state);
    uint32_t dhcp_boot_attempts = 0U;
    uint64_t dhcp_boot_cycle_after_ms = 0U;
    uint64_t dhcp_boot_next_attempt_ms = 0U;
    const uint16_t udp_ports[4] = {9000U, 9001U, 9002U, 9003U};
    for (uint32_t index = 0U; index < 4U; ++index) {
        x86os_reist_udp_bind_request_t bind = {
            .version = X86OS_REIST_UDP_BIND_REQUEST_VERSION,
            .struct_size = sizeof(bind),
            .port = udp_ports[index],
            .max_data = X86OS_REIST_UDP_MAX_DATA,
        };
        if (x86os_reist_udp_bind(&bind, &udp_bindings[index]) != 0 ||
            udp_bindings[index] == 0U) return 21;
    }
    x86os_reist_udp_bind_request_t quota_probe = {
        .version = X86OS_REIST_UDP_BIND_REQUEST_VERSION,
        .struct_size = sizeof(quota_probe),
        .port = 9004U,
        .max_data = X86OS_REIST_UDP_MAX_DATA,
    };
    x86os_reist_udp_binding_t rejected_binding = 0U;
    if (x86os_reist_udp_bind(&quota_probe, &rejected_binding) != -28 ||
        rejected_binding != 0U) return 22;
    quota_probe.port = 9000U;
    if (x86os_reist_udp_bind(&quota_probe, &rejected_binding) != -17)
        return 23;
    x86os_reist_udp_binding_t stale_binding = udp_bindings[3];
    if (x86os_reist_udp_unbind(stale_binding) != 0) return 24;
    quota_probe.port = 9003U;
    if (x86os_reist_udp_bind(&quota_probe, &udp_bindings[3]) != 0 ||
        udp_bindings[3] == stale_binding) return 25;
    if (x86os_reist_udp_unbind(stale_binding) != -9 ||
        x86os_reist_udp_unbind(udp_bindings[3]) != 0 ||
        x86os_reist_udp_unbind(udp_bindings[2]) != 0) return 26;
    udp_bindings[3] = 0U;
    udp_bindings[2] = 0U;
    if (x86os_reist_report(X86OS_REIST_REPORT_SERVICE_READY, 1U) != 0)
        return 43;

    uint32_t sequence = 2U;
    uint32_t pending_network_request = 0U;
    uint32_t pending_network_probe_id = 0U;
    bool network_frame_reported = false;
    bool network_ipv4_reported = false;
    bool network_icmp_reported = false;
    bool network_udp_reported = false;
    bool network_dhcp_reported = false;
    bool wcet_reported = false;
    bool wcet_failed = false;
    uint32_t wcet_attempts = 0U;
    uint64_t wcet_now_ms = 0U;
    uint64_t wcet_next_sample_ms = 0U;
    uint64_t wcet_deadline_ms = 0U;
    if (x86os_monotonic_ms(&wcet_now_ms) != 0) {
        wcet_failed = true;
        reject_wcet_baseline(1U);
    } else {
        wcet_next_sample_ms = wcet_now_ms;
        wcet_deadline_ms = probe_deadline_after(
            wcet_now_ms, REIST_WCET_SAMPLE_DEADLINE_MS);
    }
    for (;;) {
        if (!wcet_reported && !wcet_failed &&
            x86os_monotonic_ms(&wcet_now_ms) != 0) {
            wcet_failed = true;
            reject_wcet_baseline(2U);
        }
        if (!wcet_reported && !wcet_failed &&
            (wcet_now_ms >= wcet_deadline_ms ||
             wcet_attempts >= REIST_WCET_MAX_ATTEMPTS)) {
            wcet_failed = true;
            reject_wcet_baseline(
                wcet_now_ms >= wcet_deadline_ms ? 3U : 4U);
        }
        if (!wcet_reported && !wcet_failed &&
            wcet_now_ms >= wcet_next_sample_ms) {
            x86os_runtime_timing_t timing;
            if (x86os_runtime_timing(&timing) != 0) {
                wcet_failed = true;
                reject_wcet_baseline(5U);
                continue;
            }
            ++wcet_attempts;
            wcet_next_sample_ms = probe_deadline_after(
                wcet_now_ms, REIST_WCET_SAMPLE_INTERVAL_MS);
            if (timing.version != X86OS_RUNTIME_TIMING_VERSION ||
                timing.struct_size != sizeof(timing) ||
                timing.cpu_frequency_hz == 0U ||
                timing.clock_anomalies != 0U) {
                wcet_failed = true;
                reject_wcet_baseline(
                    timing.clock_anomalies != 0U ? 7U : 6U);
                continue;
            }
            if (timing.scheduler_samples >= REIST_WCET_MINIMUM_SAMPLES &&
                timing.syscall_samples >= REIST_WCET_MINIMUM_SAMPLES) {
                if (x86os_reist_report(
                        X86OS_REIST_REPORT_WCET_BASELINE,
                        X86OS_RUNTIME_TIMING_VERSION) != 0) {
                    wcet_failed = true;
                    reject_wcet_baseline(8U);
                } else {
                    wcet_reported = true;
                }
            }
        }
        /* Drain a fixed batch before waiting for control IPC. Real LANs can
         * fill the bounded RX queue with broadcasts much faster than QEMU's
         * quiet user network; one frame per 40 ms could otherwise starve a
         * DHCP OFFER behind unrelated traffic. */
        for (uint32_t rx_index = 0U; rx_index < REIST_NETWORK_RX_BATCH;
             ++rx_index) {
            int frame_result =
                x86os_reist_receive_network_frame(&network_frame);
            if (frame_result == 0) {
            if (network_frame.version != X86OS_REIST_NETWORK_FRAME_VERSION ||
                network_frame.struct_size != sizeof(network_frame) ||
                network_frame.length < 14U ||
                network_frame.length > X86OS_REIST_NETWORK_FRAME_MAX_SIZE ||
                network_frame.reserved != 0U || network_frame.padding[0] != 0U ||
                network_frame.padding[1] != 0U) return 30;
            uint32_t ethertype =
                ((uint32_t)network_frame.data[12U] << 8U) |
                network_frame.data[13U];
            reist_ipv4_parse_result_t ipv4_result;
            int ipv4_parse = reist_ipv4_parse_frame(
                network_frame.data, network_frame.length, &ipv4_result);
            reist_icmp_parse_result_t icmp_result;
            int icmp_parse = reist_icmp_parse_frame(
                network_frame.data, network_frame.length, &icmp_result);
            reist_udp_parse_result_t udp_result;
            int udp_parse = reist_udp_parse_frame(
                network_frame.data, network_frame.length, &udp_result);
            reist_tcp_parse_result_t tcp_result;
            int tcp_parse = reist_tcp_parse_frame(
                network_frame.data, network_frame.length, &tcp_result);
            reist_dhcp_parse_result_t dhcp_result;
            int dhcp_parse = reist_dhcp_parse_frame(
                network_frame.data, network_frame.length, &dhcp_result);
            uint32_t frame_crc32 = reist_frame_crc32(
                network_frame.data, network_frame.length);
            if (!network_frame_reported &&
                (ethertype == 0x0800U || ethertype == 0x0806U)) {
                if (x86os_reist_report(
                        X86OS_REIST_REPORT_NETWORK_FRAME, ethertype) != 0)
                    return 31;
                network_frame_reported = true;
            }
            if (!network_ipv4_reported && ipv4_parse == 0 &&
                (ipv4_result.protocol == 1U || ipv4_result.protocol == 17U)) {
                if (x86os_reist_report(X86OS_REIST_REPORT_NETWORK_IPV4,
                                       ipv4_result.protocol) != 0)
                    return 33;
                network_ipv4_reported = true;
            }
            if (!network_icmp_reported && icmp_parse == 0) {
                if (x86os_reist_report(X86OS_REIST_REPORT_NETWORK_ICMP,
                                       frame_crc32) != 0)
                    return 41;
                network_icmp_reported = true;
            }
            bool raw_icmp_delivery = ethertype == 0x0800U &&
                network_frame.length >= 34U &&
                network_frame.data[23U] == 1U;
            if (raw_icmp_delivery) {
                x86os_reist_icmp_ingress_t ingress = {
                    .version = X86OS_REIST_ICMP_INGRESS_VERSION,
                    .struct_size = sizeof(ingress),
                    .frame_crc32 = frame_crc32,
                };
                const uint8_t *payload = NULL;
                /* Before a validated lease (or for traffic addressed to a
                 * different host), submit only the canonical drop decision.
                 * Treating such normal LAN traffic as an invalid non-drop
                 * request would unnecessarily restart the service. */
                if (icmp_parse == 0 && dhcp_state.ip_address != 0U &&
                    icmp_result.destination_ip == dhcp_state.ip_address) {
                    if (icmp_result.type == 8U &&
                        icmp_result.payload_length <=
                            X86OS_REIST_ICMP_ECHO_MAX_DATA) {
                        ingress.operation =
                            X86OS_REIST_ICMP_INGRESS_ECHO_REQUEST;
                        ingress.data_length = icmp_result.payload_length;
                        payload = &network_frame.data[
                            icmp_result.payload_offset];
                    } else if (icmp_result.type == 0U) {
                        ingress.operation =
                            X86OS_REIST_ICMP_INGRESS_ECHO_REPLY;
                    }
                    if (ingress.operation !=
                            X86OS_REIST_ICMP_INGRESS_DROP) {
                        ingress.source_ip = icmp_result.source_ip;
                        ingress.destination_ip = icmp_result.destination_ip;
                        ingress.identifier = icmp_result.identifier;
                        ingress.sequence = icmp_result.sequence;
                        for (uint32_t index = 0U; index < 6U; ++index)
                            ingress.source_mac[index] =
                                network_frame.data[6U + index];
                    }
                }
                int ingress_result = x86os_reist_icmp_ingress(
                    &ingress, payload);
                if (ingress_result != 0 && ingress_result != -11 &&
                    ingress_result != -13 && ingress_result != -110)
                    return 42;
            }
            if (!network_udp_reported && udp_parse == 0) {
                if (x86os_reist_report(X86OS_REIST_REPORT_NETWORK_UDP,
                                       udp_result.destination_port) != 0)
                    return 34;
                network_udp_reported = true;
            }
            if (!network_dhcp_reported && dhcp_parse == 0) {
                if (x86os_reist_report(X86OS_REIST_REPORT_NETWORK_DHCP,
                                       frame_crc32) != 0)
                    return 39;
                network_dhcp_reported = true;
            }
            bool dhcp_ingress_consumed = false;
            if (dhcp_parse == 0) {
                x86os_reist_dhcp_ingress_t ingress = {
                    .version = X86OS_REIST_DHCP_INGRESS_VERSION,
                    .struct_size = sizeof(ingress),
                    .frame_crc32 = frame_crc32,
                    .transaction_id = dhcp_result.transaction_id,
                    .offered_ip = dhcp_result.offered_ip,
                    .server_id = dhcp_result.server_id,
                    .netmask = dhcp_result.netmask,
                    .gateway = dhcp_result.gateway,
                    .dns_server = dhcp_result.dns_server,
                    .lease_seconds = dhcp_result.lease_seconds,
                    .option_flags = dhcp_result.option_flags,
                    .message_type = dhcp_result.message_type,
                    .checksum_present = dhcp_result.checksum_present,
                };
                for (uint32_t index = 0U; index < 6U; ++index)
                    ingress.client_mac[index] = dhcp_result.client_mac[index];
                int ingress_result = x86os_reist_dhcp_ingress(&ingress);
                if (ingress_result == 0) {
                    dhcp_ingress_consumed = true;
                } else if (ingress_result != -11 && ingress_result != -13 &&
                           ingress_result != -22 && ingress_result != -110) {
                    return 40;
                }
            }
            bool raw_udp_delivery = ethertype == 0x0800U &&
                network_frame.length >= 34U &&
                network_frame.data[23U] == 17U;
            if (raw_udp_delivery && !dhcp_ingress_consumed) {
                x86os_reist_udp_ingress_t ingress = {
                    .version = X86OS_REIST_UDP_INGRESS_VERSION,
                    .struct_size = sizeof(ingress),
                    .frame_crc32 = frame_crc32,
                };
                const uint8_t *payload = NULL;
                if (ipv4_parse == 0 && udp_parse == 0 &&
                    udp_result.payload_length <= X86OS_REIST_UDP_MAX_DATA) {
                    ingress.binding = udp_binding_for_port(
                        udp_bindings, udp_ports,
                        udp_result.destination_port);
                    if (ingress.binding != 0U) {
                        ingress.source_ip = ipv4_result.source_ip;
                        ingress.destination_ip = ipv4_result.destination_ip;
                        ingress.source_port = udp_result.source_port;
                        ingress.destination_port =
                            udp_result.destination_port;
                        ingress.data_length = udp_result.payload_length;
                        for (uint32_t index = 0U; index < 6U; ++index)
                            ingress.source_mac[index] =
                                network_frame.data[6U + index];
                        payload = &network_frame.data[
                            udp_result.payload_offset];
                    }
                }
                if (x86os_reist_udp_ingress(&ingress, payload) != 0)
                    return 35;
                if (ingress.binding != 0U) {
                    if (ingress.request_id == 0U) return 36;
                    x86os_reist_udp_reply_t reply = {
                        .version = X86OS_REIST_UDP_REPLY_VERSION,
                        .struct_size = sizeof(reply),
                        .binding = ingress.binding,
                        .request_id = ingress.request_id,
                    };
                    if (x86os_reist_udp_reply(&reply) != 0) return 37;
                } else if (ingress.request_id != 0U) {
                    return 38;
                }
                if (ipv4_parse == 0 && udp_parse == 0 &&
                    udp_result.payload_length <= X86OS_UDP_MAX_DATAGRAM) {
                    x86os_udp_datagram_t datagram = {
                        .version = X86OS_UDP_SOCKET_VERSION,
                        .struct_size = sizeof(datagram), .socket = 0U,
                        .ip = ipv4_result.source_ip,
                        .source_port = udp_result.source_port,
                        .destination_port = udp_result.destination_port,
                        .length = udp_result.payload_length, .timeout_ms = 0U,
                    };
                    int socket_result = x86os_udp_socket_ingress(
                        &datagram,
                        &network_frame.data[udp_result.payload_offset]);
                    if (socket_result != 0 && socket_result != -2 &&
                        socket_result != -105) return 41;
                }
            }
            if (ipv4_parse == 0 && tcp_parse == 0) {
                if (tcp_result.payload_length > X86OS_TCP_MAX_SEGMENT &&
                    (tcp_result.flags & REIST_TCP_FLAG_SYN) != 0U)
                    return 43;
                uint32_t delivered = 0U;
                do {
                    uint32_t amount = tcp_result.payload_length - delivered;
                    if (amount > X86OS_TCP_MAX_SEGMENT)
                        amount = X86OS_TCP_MAX_SEGMENT;
                    uint8_t flags = tcp_result.flags;
                    if (delivered + amount < tcp_result.payload_length)
                        flags &= (uint8_t)~REIST_TCP_FLAG_FIN;
                    x86os_tcp_segment_t segment = {
                        .version = X86OS_TCP_SOCKET_VERSION,
                        .struct_size = sizeof(segment),
                        .source_ip = ipv4_result.source_ip,
                        .destination_ip = ipv4_result.destination_ip,
                        .sequence = tcp_result.sequence + delivered,
                        .acknowledgement = tcp_result.acknowledgement,
                        .source_port = tcp_result.source_port,
                        .destination_port = tcp_result.destination_port,
                        .window = tcp_result.window,
                        .length = amount,
                        .flags = flags,
                    };
                    int tcp_ingress = x86os_tcp_socket_ingress(
                        &segment,
                        &network_frame.data[tcp_result.payload_offset +
                                            delivered]);
                    if (tcp_ingress != 0 && tcp_ingress != -2 &&
                        tcp_ingress != -11 && tcp_ingress != -84)
                        return 43;
                    delivered += amount;
                } while (delivered < tcp_result.payload_length);
            }
            } else if (frame_result == -11) {
                break;
            } else {
                return 32;
            }
        }
        uint64_t now_ms = 0U;
        if (x86os_monotonic_ms(&now_ms) != 0) return 27;
        if (dhcp_state.state == REIST_DHCP_STATE_IDLE &&
            dhcp_boot_attempts >= REIST_DHCP_BOOT_MAX_ATTEMPTS &&
            now_ms >= dhcp_boot_cycle_after_ms) {
            /* A missing link or DHCP server must not leave the machine
             * permanently unconfigured. Restart a new finite transaction
             * cycle after a bounded quiet interval. */
            dhcp_boot_attempts = 0U;
            dhcp_boot_next_attempt_ms = now_ms;
        }
        if (dhcp_state.state == REIST_DHCP_STATE_IDLE &&
            dhcp_boot_attempts < REIST_DHCP_BOOT_MAX_ATTEMPTS &&
            now_ms >= dhcp_boot_next_attempt_ms) {
            x86os_reist_dhcp_boot_start_t start = {
                .version = X86OS_REIST_DHCP_BOOT_START_VERSION,
                .struct_size = sizeof(start),
            };
            int start_result = x86os_reist_start_dhcp_boot(&start);
            if (start_result == 0) {
                ++dhcp_boot_attempts;
                if (dhcp_boot_attempts >= REIST_DHCP_BOOT_MAX_ATTEMPTS)
                    dhcp_boot_cycle_after_ms = probe_deadline_after(
                        now_ms, REIST_DHCP_BOOT_CYCLE_DELAY_MS);
                dhcp_boot_next_attempt_ms = probe_deadline_after(
                    now_ms, REIST_DHCP_BOOT_RETRY_MS);
            } else if (start_result == -11) {
                dhcp_boot_next_attempt_ms = probe_deadline_after(now_ms, 50U);
            } else if (start_result == -17) {
                dhcp_boot_attempts = REIST_DHCP_BOOT_MAX_ATTEMPTS;
            } else if (start_result != -5 && start_result != -13) {
                return 41;
            } else {
                ++dhcp_boot_attempts;
                dhcp_boot_next_attempt_ms = probe_deadline_after(
                    now_ms, REIST_DHCP_BOOT_RETRY_MS);
            }
        }
        reist_dhcp_action_t dhcp_action =
            reist_dhcp_state_poll(&dhcp_state, now_ms);
        if (dhcp_action != REIST_DHCP_ACTION_NONE) {
            x86os_reist_dhcp_renew_request_t renewal = {
                .version = X86OS_REIST_DHCP_RENEW_REQUEST_VERSION,
                .struct_size = sizeof(renewal),
                .operation = dhcp_action == REIST_DHCP_ACTION_RENEW
                    ? X86OS_REIST_DHCP_RENEW : X86OS_REIST_DHCP_REBIND,
                .expected_ip = dhcp_state.ip_address,
            };
            int renew_result = x86os_reist_renew_dhcp(&renewal);
            if (renew_result != 0 && renew_result != -11 &&
                renew_result != -13 && renew_result != -5) return 28;
        }
        x86os_ipc_message_t request;
        message_init(&request, "");
        int receive = x86os_ipc_receive_timeout(endpoint, &request, 40U);
        if (receive == 0) {
            x86os_ipc_message_t response;
            uint32_t request_id = message_request_id(&request);
            if (message_request_is(&request, "BADID")) {
                if (request_id == UINT32_MAX) return 12;
                response_init(&response, request_id + 1U, "REIST_DIAG_OK");
                if (x86os_ipc_send_timeout(endpoint, &response, 100U) != 0)
                    return 7;
                continue;
            }
            if (message_request_is(&request, "NETPROBE")) {
                pending_network_request = request_id;
                if (x86os_network_probe_id(&pending_network_probe_id) == 0)
                    continue;
                pending_network_request = 0U;
                pending_network_probe_id = 0U;
                response_init(&response, request_id, "REIST_NET_UNAVAILABLE");
                if (x86os_ipc_send_timeout(endpoint, &response, 100U) != 0)
                    return 7;
                continue;
            }
            if (message_request_is(&request, "NETCRASH")) {
                if (x86os_network_probe_id(&pending_network_probe_id) != 0) {
                    response_init(&response, request_id,
                                  "REIST_NET_UNAVAILABLE");
                    if (x86os_ipc_send_timeout(endpoint, &response, 100U) != 0)
                        return 7;
                    continue;
                }
                __asm__ volatile("ud2");
                return 10;
            }
            if (message_request_is(&request, "NETPRESSURE")) {
                response_init(&response, request_id, "REIST_PRESSURE_READY");
                if (x86os_ipc_send_timeout(endpoint, &response, 100U) != 0)
                    return 7;
                (void)x86os_sleep_ms(100U);
                if (x86os_network_probe_id(&pending_network_probe_id) != 0)
                    return 11;
                continue;
            }
            const char *network = network_classification(&request);
            if (network == NULL && pending_network_probe_id != 0U &&
                message_is_network_ingress(&request)) {
                if (x86os_reist_report(X86OS_REIST_REPORT_NETWORK_DEGRADED,
                        X86OS_REIST_NETWORK_DEGRADED_SEMANTIC) != 0) return 14;
                continue;
            }
            if (network != NULL && request.payload[3] == 'R' &&
                (pending_network_probe_id == 0U ||
                 message_probe_id(&request) != pending_network_probe_id)) {
                if (x86os_reist_report(X86OS_REIST_REPORT_NETWORK_DEGRADED,
                        X86OS_REIST_NETWORK_DEGRADED_SEMANTIC) != 0) return 14;
                continue;
            }
            if (network != NULL && request.payload[3] == 'R') {
                bool direct_resolution = pending_network_request == 0U;
                uint32_t ethertype = ((uint32_t)request.payload[16] << 8) |
                                     request.payload[17];
                x86os_reist_arp_binding_t binding = {
                    .version = X86OS_REIST_ARP_BINDING_VERSION,
                    .struct_size = sizeof(binding),
                    .probe_id = pending_network_probe_id,
                    .ip = ((uint32_t)request.payload[32] << 24U) |
                          ((uint32_t)request.payload[33] << 16U) |
                          ((uint32_t)request.payload[34] << 8U) |
                          request.payload[35],
                };
                for (uint32_t index = 0U; index < 6U; ++index)
                    binding.mac[index] = request.payload[26U + index];
                if (x86os_reist_commit_arp_binding(&binding) != 0) return 13;
                if (x86os_reist_report(X86OS_REIST_REPORT_NETWORK_HEADER,
                                       ethertype) != 0) return 9;
                if (direct_resolution) {
                    pending_network_probe_id = 0U;
                    continue;
                }
            }
            if (network != NULL && request.payload[3] == 'Q') {
                x86os_reist_arp_reply_t reply = {
                    .version = X86OS_REIST_ARP_REPLY_VERSION,
                    .struct_size = sizeof(reply),
                    .request_id = (uint32_t)request.payload[56] |
                        ((uint32_t)request.payload[57] << 8U) |
                        ((uint32_t)request.payload[58] << 16U) |
                        ((uint32_t)request.payload[59] << 24U),
                    .target_ip = ((uint32_t)request.payload[32] << 24U) |
                        ((uint32_t)request.payload[33] << 16U) |
                        ((uint32_t)request.payload[34] << 8U) |
                        request.payload[35],
                };
                for (uint32_t index = 0U; index < 6U; ++index)
                    reply.target_mac[index] = request.payload[26U + index];
                if (x86os_reist_send_arp_reply(&reply) != 0) {
                    if (x86os_reist_report(
                            X86OS_REIST_REPORT_NETWORK_DEGRADED,
                            X86OS_REIST_NETWORK_DEGRADED_SEMANTIC) != 0)
                        return 15;
                }
                continue;
            }
            if (network != NULL && request.payload[3] == 'A') {
                if (request.length != 26U ||
                    pending_network_probe_id != 0U) return 16;
                uint32_t target_ip = ((uint32_t)request.payload[4] << 24U) |
                    ((uint32_t)request.payload[5] << 16U) |
                    ((uint32_t)request.payload[6] << 8U) |
                    request.payload[7];
                uint32_t local_ip = ((uint32_t)request.payload[8] << 24U) |
                    ((uint32_t)request.payload[9] << 16U) |
                    ((uint32_t)request.payload[10] << 8U) |
                    request.payload[11];
                uint32_t request_id = (uint32_t)request.payload[18] |
                    ((uint32_t)request.payload[19] << 8U) |
                    ((uint32_t)request.payload[20] << 16U) |
                    ((uint32_t)request.payload[21] << 24U);
                uint32_t network_probe_id =
                    (uint32_t)request.payload[22] |
                    ((uint32_t)request.payload[23] << 8U) |
                    ((uint32_t)request.payload[24] << 16U) |
                    ((uint32_t)request.payload[25] << 24U);
                bool nonzero_mac = false;
                for (uint32_t index = 0U; index < 6U; ++index)
                    if (request.payload[12U + index] != 0U) nonzero_mac = true;
                if (target_ip == 0U || target_ip == local_ip ||
                    request_id == 0U || network_probe_id == 0U ||
                    !nonzero_mac ||
                    (request.payload[12] & 1U) != 0U) return 16;
                x86os_reist_arp_resolution_t resolution = {
                    .version = X86OS_REIST_ARP_RESOLUTION_VERSION,
                    .struct_size = sizeof(resolution),
                    .request_id = request_id,
                    .target_ip = target_ip,
                };
                if (x86os_reist_send_arp_request(&resolution) != 0) return 17;
                pending_network_probe_id = network_probe_id;
                continue;
            }
            if (network != NULL && request.payload[3] == 'I') {
                x86os_reist_icmp_echo_reply_t reply = {
                    .version = X86OS_REIST_ICMP_ECHO_REPLY_VERSION,
                    .struct_size = sizeof(reply),
                    .request_id = (uint32_t)request.payload[4] |
                        ((uint32_t)request.payload[5] << 8U) |
                        ((uint32_t)request.payload[6] << 16U) |
                        ((uint32_t)request.payload[7] << 24U),
                };
                if (x86os_reist_send_icmp_echo_reply(&reply) != 0) {
                    if (x86os_reist_report(
                            X86OS_REIST_REPORT_NETWORK_DEGRADED,
                            X86OS_REIST_NETWORK_DEGRADED_SEMANTIC) != 0)
                        return 18;
                }
                continue;
            }
            if (network != NULL && request.payload[3] == 'D') {
                x86os_reist_dhcp_commit_t commit = {
                    .version = X86OS_REIST_DHCP_COMMIT_VERSION,
                    .struct_size = sizeof(commit),
                    .request_id = (uint32_t)request.payload[4] |
                        ((uint32_t)request.payload[5] << 8U) |
                        ((uint32_t)request.payload[6] << 16U) |
                        ((uint32_t)request.payload[7] << 24U),
                };
                if (x86os_reist_commit_dhcp(&commit) != 0) {
                    if (x86os_reist_report(
                            X86OS_REIST_REPORT_NETWORK_DEGRADED,
                            X86OS_REIST_NETWORK_DEGRADED_SEMANTIC) != 0)
                        return 19;
                }
                continue;
            }
            if (network != NULL && request.payload[3] == 'L') {
                if (x86os_monotonic_ms(&now_ms) != 0 ||
                    reist_dhcp_state_configure(
                        &dhcp_state, now_ms, payload_be32(&request, 4U),
                        payload_be32(&request, 12U),
                        payload_be32(&request, 16U),
                        payload_be32(&request, 8U)) != 0) {
                    if (x86os_reist_report(
                            X86OS_REIST_REPORT_NETWORK_DEGRADED,
                            X86OS_REIST_NETWORK_DEGRADED_SEMANTIC) != 0)
                        return 29;
                }
                continue;
            }
            if (network != NULL && request.payload[3] == 'V') {
                x86os_reist_udp_reply_t reply = {
                    .version = X86OS_REIST_UDP_REPLY_VERSION,
                    .struct_size = sizeof(reply),
                    .binding = (uint32_t)request.payload[4] |
                        ((uint32_t)request.payload[5] << 8U) |
                        ((uint32_t)request.payload[6] << 16U) |
                        ((uint32_t)request.payload[7] << 24U),
                    .request_id = (uint32_t)request.payload[8] |
                        ((uint32_t)request.payload[9] << 8U) |
                        ((uint32_t)request.payload[10] << 16U) |
                        ((uint32_t)request.payload[11] << 24U),
                };
                if (x86os_reist_udp_reply(&reply) != 0) {
                    if (x86os_reist_report(
                            X86OS_REIST_REPORT_NETWORK_DEGRADED,
                            X86OS_REIST_NETWORK_DEGRADED_SEMANTIC) != 0)
                        return 20;
                }
                continue;
            }
            if (network != NULL && request.payload[3] == 'R' &&
                pending_network_request != 0U) {
                response_init(&response, pending_network_request, network);
                pending_network_request = 0U;
                pending_network_probe_id = 0U;
            } else if (network != NULL) {
                message_init(&response, network);
            } else if (request_id != 0U) {
                response_init(&response, request_id,
                              message_request_is(&request, "DIAG")
                                  ? "REIST_DIAG_OK"
                                  : "REIST_DIAG_INVALID");
            } else {
                continue;
            }
            if (x86os_ipc_send_timeout(endpoint, &response, 100U) != 0)
                return 7;
        } else if (receive == -32) {
            (void)x86os_sleep_ms(40U);
        } else if (receive != -110 && receive != -11) {
            return 8;
        }
        if (x86os_reist_report(X86OS_REIST_REPORT_PROGRESS, sequence++) != 0)
            return 6;
    }
}
