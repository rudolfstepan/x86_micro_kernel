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

static int message_is(const x86os_ipc_message_t *message, const char *text) {
    uint32_t length = 0U;
    while (text[length] != '\0') ++length;
    if (message->length != length) return 0;
    for (uint32_t index = 0; index < length; ++index) {
        if (message->payload[index] != (uint8_t)text[index]) return 0;
    }
    return 1;
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

static const char *network_classification(
        const x86os_ipc_message_t *message) {
    /* NET1/NETR/NETQ followed by one complete Ethernet+ARP header. This deliberately
     * bounded v1 parser performs no allocation and publishes no output. */
    if (message->length < 18U || message->payload[0] != 'N' ||
        message->payload[1] != 'E' || message->payload[2] != 'T' ||
        (message->payload[3] != '1' && message->payload[3] != 'R' &&
         message->payload[3] != 'Q' && message->payload[3] != 'X'))
        return NULL;
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
