#include "reist_udp_parser.h"

#include <stddef.h>

#include "reist_ipv4_parser.h"

#define ETHERNET_HEADER_SIZE 14U
#define UDP_HEADER_SIZE 8U
#define IPV4_PROTOCOL_UDP 17U

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static uint32_t checksum_add_bytes(const uint8_t *bytes, uint16_t length,
                                   uint32_t sum) {
    uint16_t offset = 0U;
    for (; offset + 1U < length; offset += 2U)
        sum += read_be16(&bytes[offset]);
    if (offset < length) sum += (uint16_t)bytes[offset] << 8U;
    return sum;
}

static int checksum_valid(const uint8_t *frame, const uint8_t *udp,
                          uint16_t udp_length) {
    uint32_t sum = 0U;
    const uint8_t *ip = &frame[ETHERNET_HEADER_SIZE];
    sum = checksum_add_bytes(&ip[12U], 8U, sum);
    sum += IPV4_PROTOCOL_UDP;
    sum += udp_length;
    sum = checksum_add_bytes(udp, udp_length, sum);
    while ((sum >> 16U) != 0U)
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)sum == 0xFFFFU;
}

int reist_udp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                          reist_udp_parse_result_t *result) {
    if (result == NULL) return REIST_UDP_PARSE_EINVAL;
    *result = (reist_udp_parse_result_t){0};
    if (frame == NULL) return REIST_UDP_PARSE_EINVAL;

    reist_ipv4_parse_result_t ipv4;
    int ipv4_result = reist_ipv4_parse_frame(frame, frame_length, &ipv4);
    if (ipv4_result != 0) return ipv4_result;
    if (ipv4.protocol != IPV4_PROTOCOL_UDP)
        return REIST_UDP_PARSE_EPROTONOSUPPORT;
    if (ipv4.payload_length < UDP_HEADER_SIZE)
        return REIST_UDP_PARSE_EMSGSIZE;

    const uint8_t *udp = &frame[ipv4.payload_offset];
    uint16_t udp_length = read_be16(&udp[4U]);
    uint16_t checksum = read_be16(&udp[6U]);
    if (udp_length < UDP_HEADER_SIZE || udp_length != ipv4.payload_length)
        return REIST_UDP_PARSE_EMSGSIZE;
    uint16_t source_port = read_be16(&udp[0U]);
    uint16_t destination_port = read_be16(&udp[2U]);
    if (source_port == 0U || destination_port == 0U)
        return REIST_UDP_PARSE_EINVAL;
    if (checksum == 0U || !checksum_valid(frame, udp, udp_length))
        return REIST_UDP_PARSE_EINTEGRITY;

    *result = (reist_udp_parse_result_t){
        .version = REIST_UDP_PARSE_RESULT_VERSION,
        .struct_size = sizeof(*result),
        .source_port = source_port,
        .destination_port = destination_port,
        .datagram_length = udp_length,
        .payload_offset = (uint16_t)(ipv4.payload_offset + UDP_HEADER_SIZE),
        .payload_length = (uint16_t)(udp_length - UDP_HEADER_SIZE),
        .checksum = checksum,
    };
    return 0;
}
