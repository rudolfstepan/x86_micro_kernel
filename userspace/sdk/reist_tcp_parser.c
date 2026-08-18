/** @file userspace/sdk/reist_tcp_parser.c @brief Validates IPv4/TCP segments. */
#include "reist_tcp_parser.h"

#include <stddef.h>

#include "reist_ipv4_parser.h"

#define ETHERNET_HEADER_SIZE 14U
#define TCP_MIN_HEADER_SIZE 20U
#define IPV4_PROTOCOL_TCP 6U
#define TCP_ALLOWED_FLAGS 0x3fU

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}
static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | bytes[3];
}
static uint32_t checksum_add(const uint8_t *bytes, uint16_t length,
                             uint32_t sum) {
    uint16_t offset = 0U;
    for (; offset + 1U < length; offset += 2U) sum += read_be16(bytes + offset);
    if (offset < length) sum += (uint16_t)bytes[offset] << 8U;
    return sum;
}
static int checksum_valid(const uint8_t *frame, const uint8_t *tcp,
                          uint16_t tcp_length) {
    const uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    uint32_t sum = checksum_add(ip + 12U, 8U, 0U);
    sum += IPV4_PROTOCOL_TCP; sum += tcp_length;
    sum = checksum_add(tcp, tcp_length, sum);
    while ((sum >> 16U) != 0U) sum = (sum & 0xffffU) + (sum >> 16U);
    return (uint16_t)sum == 0xffffU;
}

int reist_tcp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                          reist_tcp_parse_result_t *result) {
    if (result == NULL) return REIST_TCP_PARSE_EINVAL;
    *result = (reist_tcp_parse_result_t){0};
    if (frame == NULL) return REIST_TCP_PARSE_EINVAL;
    reist_ipv4_parse_result_t ipv4;
    int rc = reist_ipv4_parse_frame(frame, frame_length, &ipv4);
    if (rc != 0) return rc;
    if (ipv4.protocol != IPV4_PROTOCOL_TCP)
        return REIST_TCP_PARSE_EPROTONOSUPPORT;
    if (ipv4.payload_length < TCP_MIN_HEADER_SIZE)
        return REIST_TCP_PARSE_EMSGSIZE;
    const uint8_t *tcp = frame + ipv4.payload_offset;
    uint8_t header_length = (uint8_t)((tcp[12U] >> 4U) * 4U);
    if (header_length < TCP_MIN_HEADER_SIZE ||
        header_length > ipv4.payload_length || (tcp[12U] & 0x0fU) != 0U)
        return REIST_TCP_PARSE_EMSGSIZE;
    uint16_t source_port = read_be16(tcp);
    uint16_t destination_port = read_be16(tcp + 2U);
    uint8_t flags = tcp[13U];
    if (source_port == 0U || destination_port == 0U ||
        (flags & (uint8_t)~TCP_ALLOWED_FLAGS) != 0U ||
        (flags & 0x03U) == 0x03U || !checksum_valid(
            frame, tcp, ipv4.payload_length)) return REIST_TCP_PARSE_EINTEGRITY;
    *result = (reist_tcp_parse_result_t){
        .version = REIST_TCP_PARSE_RESULT_VERSION,
        .struct_size = sizeof(*result), .sequence = read_be32(tcp + 4U),
        .acknowledgement = read_be32(tcp + 8U), .source_port = source_port,
        .destination_port = destination_port, .window = read_be16(tcp + 14U),
        .payload_offset = (uint16_t)(ipv4.payload_offset + header_length),
        .payload_length = (uint16_t)(ipv4.payload_length - header_length),
        .flags = flags, .header_length = header_length,
    };
    return 0;
}
