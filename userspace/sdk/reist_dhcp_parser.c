#include "reist_dhcp_parser.h"

#include <stdbool.h>
#include <stddef.h>

#include "reist_ipv4_parser.h"

#define DHCP_FIXED_SIZE 236U
#define DHCP_MIN_MESSAGE_SIZE 241U
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_SERVER_PORT 67U
#define DHCP_CLIENT_PORT 68U
#define IPV4_PROTOCOL_UDP 17U
#define UDP_HEADER_SIZE 8U

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | bytes[3];
}

static uint32_t checksum_add(const uint8_t *bytes, uint16_t length,
                             uint32_t sum) {
    uint16_t offset = 0U;
    for (; offset + 1U < length; offset += 2U)
        sum += read_be16(&bytes[offset]);
    if (offset < length) sum += (uint16_t)bytes[offset] << 8U;
    return sum;
}

static bool udp_checksum_valid(const uint8_t *frame, const uint8_t *udp,
                               uint16_t udp_length) {
    const uint8_t *ip = &frame[14U];
    uint32_t sum = checksum_add(&ip[12U], 8U, 0U);
    sum += IPV4_PROTOCOL_UDP;
    sum += udp_length;
    sum = checksum_add(udp, udp_length, sum);
    while ((sum >> 16U) != 0U)
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)sum == 0xFFFFU;
}

static bool client_mac_valid(const uint8_t mac[6]) {
    bool nonzero = false;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (mac[index] != 0U) nonzero = true;
    return nonzero && (mac[0] & 1U) == 0U;
}

static int set_option(uint32_t *flags, uint32_t flag) {
    if ((*flags & flag) != 0U) return REIST_DHCP_PARSE_EINVAL;
    *flags |= flag;
    return 0;
}

int reist_dhcp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                           reist_dhcp_parse_result_t *result) {
    if (result == NULL) return REIST_DHCP_PARSE_EINVAL;
    *result = (reist_dhcp_parse_result_t){0};
    if (frame == NULL) return REIST_DHCP_PARSE_EINVAL;

    reist_ipv4_parse_result_t ipv4;
    int parse_result = reist_ipv4_parse_frame(frame, frame_length, &ipv4);
    if (parse_result != 0) return parse_result;
    if (ipv4.protocol != IPV4_PROTOCOL_UDP)
        return REIST_DHCP_PARSE_EPROTONOSUPPORT;
    if (ipv4.payload_length < UDP_HEADER_SIZE)
        return REIST_DHCP_PARSE_EMSGSIZE;

    const uint8_t *udp = &frame[ipv4.payload_offset];
    uint16_t udp_length = read_be16(&udp[4U]);
    uint16_t checksum = read_be16(&udp[6U]);
    if (read_be16(&udp[0U]) != DHCP_SERVER_PORT ||
        read_be16(&udp[2U]) != DHCP_CLIENT_PORT)
        return REIST_DHCP_PARSE_EPROTONOSUPPORT;
    if (udp_length < UDP_HEADER_SIZE || udp_length != ipv4.payload_length)
        return REIST_DHCP_PARSE_EMSGSIZE;
    if (checksum != 0U && !udp_checksum_valid(frame, udp, udp_length))
        return REIST_DHCP_PARSE_EINTEGRITY;

    uint16_t payload_length = (uint16_t)(udp_length - UDP_HEADER_SIZE);
    if (payload_length < DHCP_MIN_MESSAGE_SIZE ||
        payload_length > REIST_DHCP_MAX_MESSAGE_SIZE)
        return REIST_DHCP_PARSE_EMSGSIZE;
    const uint8_t *dhcp = &udp[UDP_HEADER_SIZE];
    if (dhcp[0U] != 2U || dhcp[1U] != 1U || dhcp[2U] != 6U ||
        read_be32(&dhcp[4U]) == 0U ||
        read_be32(&dhcp[DHCP_FIXED_SIZE]) != DHCP_MAGIC_COOKIE ||
        !client_mac_valid(&dhcp[28U]))
        return REIST_DHCP_PARSE_EINVAL;

    reist_dhcp_parse_result_t parsed = {
        .version = REIST_DHCP_PARSE_RESULT_VERSION,
        .struct_size = sizeof(parsed),
        .transaction_id = read_be32(&dhcp[4U]),
        .offered_ip = read_be32(&dhcp[16U]),
        .payload_offset = (uint16_t)(ipv4.payload_offset + UDP_HEADER_SIZE),
        .payload_length = payload_length,
        .checksum_present = checksum != 0U ? 1U : 0U,
    };
    for (uint32_t index = 0U; index < 6U; ++index)
        parsed.client_mac[index] = dhcp[28U + index];

    bool end_seen = false;
    for (uint16_t offset = 240U; offset < payload_length;) {
        uint8_t option = dhcp[offset++];
        if (option == 0U) continue;
        if (option == 255U) {
            end_seen = true;
            break;
        }
        if (offset >= payload_length) return REIST_DHCP_PARSE_EMSGSIZE;
        uint8_t length = dhcp[offset++];
        if ((uint32_t)offset + length > payload_length)
            return REIST_DHCP_PARSE_EMSGSIZE;
        const uint8_t *value = &dhcp[offset];
        uint32_t flag = 0U;
        switch (option) {
            case 1U:
                flag = REIST_DHCP_OPTION_NETMASK;
                if (length != 4U) return REIST_DHCP_PARSE_EINVAL;
                parsed.netmask = read_be32(value);
                break;
            case 3U:
                flag = REIST_DHCP_OPTION_GATEWAY;
                if (length < 4U || (length & 3U) != 0U)
                    return REIST_DHCP_PARSE_EINVAL;
                parsed.gateway = read_be32(value);
                break;
            case 6U:
                flag = REIST_DHCP_OPTION_DNS;
                if (length < 4U || (length & 3U) != 0U)
                    return REIST_DHCP_PARSE_EINVAL;
                parsed.dns_server = read_be32(value);
                break;
            case 51U:
                flag = REIST_DHCP_OPTION_LEASE;
                if (length != 4U) return REIST_DHCP_PARSE_EINVAL;
                parsed.lease_seconds = read_be32(value);
                break;
            case 53U:
                flag = REIST_DHCP_OPTION_MESSAGE_TYPE;
                if (length != 1U ||
                    (value[0U] != REIST_DHCP_MESSAGE_OFFER &&
                     value[0U] != REIST_DHCP_MESSAGE_ACK &&
                     value[0U] != REIST_DHCP_MESSAGE_NAK))
                    return REIST_DHCP_PARSE_EINVAL;
                parsed.message_type = value[0U];
                break;
            case 54U:
                flag = REIST_DHCP_OPTION_SERVER_ID;
                if (length != 4U) return REIST_DHCP_PARSE_EINVAL;
                parsed.server_id = read_be32(value);
                break;
            default:
                break;
        }
        if (flag != 0U && set_option(&parsed.option_flags, flag) != 0)
            return REIST_DHCP_PARSE_EINVAL;
        offset = (uint16_t)(offset + length);
    }
    if (!end_seen ||
        (parsed.option_flags & REIST_DHCP_OPTION_MESSAGE_TYPE) == 0U)
        return REIST_DHCP_PARSE_EINVAL;
    *result = parsed;
    return 0;
}
