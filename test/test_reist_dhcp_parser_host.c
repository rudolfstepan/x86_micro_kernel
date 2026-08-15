#include <stdint.h>
#include <string.h>

#include "reist_dhcp_parser.h"

#define FRAME_CAPACITY 1518U
#define DHCP_SIZE 300U

static uint32_t add_bytes(const uint8_t *bytes, uint16_t length, uint32_t sum) {
    uint16_t offset = 0U;
    for (; offset + 1U < length; offset += 2U)
        sum += ((uint16_t)bytes[offset] << 8U) | bytes[offset + 1U];
    if (offset < length) sum += (uint16_t)bytes[offset] << 8U;
    return sum;
}

static uint16_t finish_checksum(uint32_t sum) {
    while ((sum >> 16U) != 0U)
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)~sum;
}

static void set_ipv4_checksum(uint8_t *ip) {
    ip[10] = 0U;
    ip[11] = 0U;
    uint16_t checksum = finish_checksum(add_bytes(ip, 20U, 0U));
    ip[10] = (uint8_t)(checksum >> 8U);
    ip[11] = (uint8_t)checksum;
}

static void append_option(uint8_t *dhcp, uint16_t *offset, uint8_t option,
                          const uint8_t *value, uint8_t length) {
    dhcp[(*offset)++] = option;
    dhcp[(*offset)++] = length;
    memcpy(&dhcp[*offset], value, length);
    *offset = (uint16_t)(*offset + length);
}

static uint32_t make_frame(uint8_t *frame, uint8_t message_type,
                           int with_checksum) {
    memset(frame, 0, FRAME_CAPACITY);
    frame[12] = 0x08U;
    frame[13] = 0x00U;
    uint8_t *ip = &frame[14];
    uint16_t udp_length = 8U + DHCP_SIZE;
    uint16_t ip_length = 20U + udp_length;
    ip[0] = 0x45U;
    ip[2] = (uint8_t)(ip_length >> 8U);
    ip[3] = (uint8_t)ip_length;
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 17U;
    ip[12] = 10U; ip[13] = 0U; ip[14] = 2U; ip[15] = 2U;
    ip[16] = 255U; ip[17] = 255U; ip[18] = 255U; ip[19] = 255U;
    set_ipv4_checksum(ip);

    uint8_t *udp = &ip[20];
    udp[0] = 0U; udp[1] = 67U;
    udp[2] = 0U; udp[3] = 68U;
    udp[4] = (uint8_t)(udp_length >> 8U);
    udp[5] = (uint8_t)udp_length;
    uint8_t *dhcp = &udp[8];
    dhcp[0] = 2U;
    dhcp[1] = 1U;
    dhcp[2] = 6U;
    dhcp[4] = 0x12U; dhcp[5] = 0x34U;
    dhcp[6] = 0x56U; dhcp[7] = 0x78U;
    dhcp[16] = 10U; dhcp[17] = 0U; dhcp[18] = 2U; dhcp[19] = 15U;
    dhcp[28] = 0x52U; dhcp[29] = 0x54U; dhcp[30] = 0U;
    dhcp[31] = 0x12U; dhcp[32] = 0x34U; dhcp[33] = 0x56U;
    dhcp[236] = 0x63U; dhcp[237] = 0x82U;
    dhcp[238] = 0x53U; dhcp[239] = 0x63U;
    uint16_t option = 240U;
    uint8_t mask[4] = {255U, 255U, 255U, 0U};
    uint8_t gateway[4] = {10U, 0U, 2U, 2U};
    uint8_t dns[4] = {10U, 0U, 2U, 3U};
    uint8_t lease[4] = {0U, 0U, 0x0EU, 0x10U};
    append_option(dhcp, &option, 53U, &message_type, 1U);
    append_option(dhcp, &option, 54U, gateway, 4U);
    append_option(dhcp, &option, 1U, mask, 4U);
    append_option(dhcp, &option, 3U, gateway, 4U);
    append_option(dhcp, &option, 6U, dns, 4U);
    append_option(dhcp, &option, 51U, lease, 4U);
    dhcp[option] = 255U;

    if (with_checksum) {
        uint32_t sum = add_bytes(&ip[12], 8U, 0U);
        sum += 17U;
        sum += udp_length;
        sum = add_bytes(udp, udp_length, sum);
        uint16_t checksum = finish_checksum(sum);
        if (checksum == 0U) checksum = 0xFFFFU;
        udp[6] = (uint8_t)(checksum >> 8U);
        udp[7] = (uint8_t)checksum;
    }
    return 14U + ip_length;
}

int main(void) {
    uint8_t frame[FRAME_CAPACITY];
    reist_dhcp_parse_result_t result;
    uint32_t length = make_frame(frame, REIST_DHCP_MESSAGE_OFFER, 0);
    if (reist_dhcp_parse_frame(frame, length, &result) != 0) return 1;
    if (result.version != REIST_DHCP_PARSE_RESULT_VERSION ||
        result.struct_size != sizeof(result) ||
        result.transaction_id != 0x12345678U ||
        result.offered_ip != 0x0A00020FU ||
        result.server_id != 0x0A000202U ||
        result.netmask != 0xFFFFFF00U ||
        result.gateway != 0x0A000202U ||
        result.dns_server != 0x0A000203U || result.lease_seconds != 3600U ||
        result.message_type != REIST_DHCP_MESSAGE_OFFER ||
        result.checksum_present != 0U || result.payload_offset != 42U ||
        result.payload_length != DHCP_SIZE || result.client_mac[0] != 0x52U)
        return 2;

    length = make_frame(frame, REIST_DHCP_MESSAGE_ACK, 1);
    if (reist_dhcp_parse_frame(frame, length, &result) != 0 ||
        result.message_type != REIST_DHCP_MESSAGE_ACK ||
        result.checksum_present != 1U) return 3;
    frame[100] ^= 1U;
    if (reist_dhcp_parse_frame(frame, length, &result) !=
        REIST_DHCP_PARSE_EINTEGRITY) return 4;

    length = make_frame(frame, REIST_DHCP_MESSAGE_NAK, 0);
    if (reist_dhcp_parse_frame(frame, length, &result) != 0) return 5;
    frame[34] = 0U; frame[35] = 66U;
    if (reist_dhcp_parse_frame(frame, length, &result) !=
        REIST_DHCP_PARSE_EPROTONOSUPPORT) return 6;

    length = make_frame(frame, REIST_DHCP_MESSAGE_ACK, 0);
    frame[42U + 236U] ^= 1U;
    if (reist_dhcp_parse_frame(frame, length, &result) !=
        REIST_DHCP_PARSE_EINVAL) return 7;

    length = make_frame(frame, REIST_DHCP_MESSAGE_ACK, 0);
    frame[42U + 240U] = 53U;
    frame[42U + 241U] = 1U;
    frame[42U + 243U] = 53U;
    frame[42U + 244U] = 1U;
    frame[42U + 245U] = 5U;
    frame[42U + 246U] = 255U;
    if (reist_dhcp_parse_frame(frame, length, &result) !=
        REIST_DHCP_PARSE_EINVAL) return 8;

    length = make_frame(frame, REIST_DHCP_MESSAGE_ACK, 0);
    memset(&frame[42U + 240U], 0U, DHCP_SIZE - 240U);
    if (reist_dhcp_parse_frame(frame, length, &result) !=
        REIST_DHCP_PARSE_EINVAL) return 9;

    length = make_frame(frame, REIST_DHCP_MESSAGE_ACK, 0);
    frame[42U + 241U] = 0xFFU;
    if (reist_dhcp_parse_frame(frame, length, &result) !=
        REIST_DHCP_PARSE_EMSGSIZE) return 10;

    length = make_frame(frame, REIST_DHCP_MESSAGE_ACK, 0);
    memset(&frame[42U + 28U], 0U, 6U);
    if (reist_dhcp_parse_frame(frame, length, &result) !=
        REIST_DHCP_PARSE_EINVAL) return 11;
    if (reist_dhcp_parse_frame(NULL, 0U, &result) !=
        REIST_DHCP_PARSE_EINVAL) return 12;
    if (reist_dhcp_parse_frame(frame, length, NULL) !=
        REIST_DHCP_PARSE_EINVAL) return 13;
    return 0;
}
