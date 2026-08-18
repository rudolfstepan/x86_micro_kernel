/**
 * @file userspace/sdk/reist_icmp_parser.c
 * @brief Begrenzter ICMP-Nachrichtenparser.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#include "reist_icmp_parser.h"

#include <stddef.h>

#include "reist_ipv4_parser.h"

#define ICMP_HEADER_SIZE 8U
#define IPV4_PROTOCOL_ICMP 1U
#define ICMP_ECHO_REPLY 0U
#define ICMP_ECHO_REQUEST 8U

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static int checksum_valid(const uint8_t *bytes, uint16_t length) {
    uint32_t sum = 0U;
    uint16_t offset = 0U;
    for (; offset + 1U < length; offset += 2U)
        sum += read_be16(&bytes[offset]);
    if (offset < length) sum += (uint16_t)bytes[offset] << 8U;
    while ((sum >> 16U) != 0U)
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)sum == 0xFFFFU;
}

int reist_icmp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                           reist_icmp_parse_result_t *result) {
    if (result == NULL) return REIST_ICMP_PARSE_EINVAL;
    *result = (reist_icmp_parse_result_t){0};
    if (frame == NULL) return REIST_ICMP_PARSE_EINVAL;

    reist_ipv4_parse_result_t ipv4;
    int ipv4_result = reist_ipv4_parse_frame(frame, frame_length, &ipv4);
    if (ipv4_result != 0) return ipv4_result;
    if (ipv4.protocol != IPV4_PROTOCOL_ICMP)
        return REIST_ICMP_PARSE_EPROTONOSUPPORT;
    if (ipv4.payload_length < ICMP_HEADER_SIZE)
        return REIST_ICMP_PARSE_EMSGSIZE;

    const uint8_t *icmp = &frame[ipv4.payload_offset];
    uint8_t type = icmp[0U];
    uint8_t code = icmp[1U];
    if ((type != ICMP_ECHO_REQUEST && type != ICMP_ECHO_REPLY) || code != 0U)
        return REIST_ICMP_PARSE_EPROTONOSUPPORT;
    if (!checksum_valid(icmp, ipv4.payload_length))
        return REIST_ICMP_PARSE_EINTEGRITY;

    *result = (reist_icmp_parse_result_t){
        .version = REIST_ICMP_PARSE_RESULT_VERSION,
        .struct_size = sizeof(*result),
        .source_ip = ipv4.source_ip,
        .destination_ip = ipv4.destination_ip,
        .payload_offset = (uint16_t)(ipv4.payload_offset + ICMP_HEADER_SIZE),
        .payload_length = (uint16_t)(ipv4.payload_length - ICMP_HEADER_SIZE),
        .identifier = read_be16(&icmp[4U]),
        .sequence = read_be16(&icmp[6U]),
        .type = type,
        .code = code,
    };
    return 0;
}
