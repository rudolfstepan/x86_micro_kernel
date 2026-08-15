#include <stdint.h>
#include <stdbool.h>
#include "x86os.h"

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
    if (message->length != 24U) return false;
    uint32_t request_id = (uint32_t)message->payload[4] |
        ((uint32_t)message->payload[5] << 8U) |
        ((uint32_t)message->payload[6] << 16U) |
        ((uint32_t)message->payload[7] << 24U);
    uint32_t ip = payload_be32(message, 8U);
    uint32_t mask = payload_be32(message, 12U);
    uint32_t gateway = payload_be32(message, 16U);
    uint32_t dns = payload_be32(message, 20U);
    if (request_id == 0U || ip == 0U || ip == 0xFFFFFFFFU || mask == 0U ||
        mask == 0xFFFFFFFFU || gateway == 0xFFFFFFFFU ||
        dns == 0xFFFFFFFFU) return false;
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
         message->payload[3] != 'X'))
        return NULL;
    if (message->payload[3] == 'A')
        return message->length == 22U ? "REIST_ARP_RESOLUTION" : NULL;
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

int main(int argc, char **argv) {
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

    uint32_t sequence = 2U;
    uint32_t pending_network_request = 0U;
    uint32_t pending_network_probe_id = 0U;
    for (;;) {
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
                if (request.length != 22U) return 16;
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
                bool nonzero_mac = false;
                for (uint32_t index = 0U; index < 6U; ++index)
                    if (request.payload[12U + index] != 0U) nonzero_mac = true;
                if (target_ip == 0U || target_ip == local_ip ||
                    request_id == 0U || !nonzero_mac ||
                    (request.payload[12] & 1U) != 0U) return 16;
                x86os_reist_arp_resolution_t resolution = {
                    .version = X86OS_REIST_ARP_RESOLUTION_VERSION,
                    .struct_size = sizeof(resolution),
                    .request_id = request_id,
                    .target_ip = target_ip,
                };
                if (x86os_reist_send_arp_request(&resolution) != 0) return 17;
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
