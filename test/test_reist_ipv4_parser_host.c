#include <stdint.h>
#include <string.h>

#include "reist_ipv4_parser.h"

static void set_checksum(uint8_t *header, uint16_t length) {
    header[10] = 0U;
    header[11] = 0U;
    uint32_t sum = 0U;
    for (uint16_t offset = 0U; offset < length; offset += 2U)
        sum += ((uint16_t)header[offset] << 8U) | header[offset + 1U];
    while ((sum >> 16U) != 0U)
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    uint16_t checksum = (uint16_t)~sum;
    header[10] = (uint8_t)(checksum >> 8U);
    header[11] = (uint8_t)checksum;
}

static void make_frame(uint8_t *frame, uint8_t ihl_words,
                       uint16_t payload_length) {
    uint16_t header_length = (uint16_t)ihl_words * 4U;
    memset(frame, 0, REIST_IPV4_MAX_FRAME_SIZE);
    frame[12] = 0x08U;
    frame[13] = 0x00U;
    uint8_t *ip = &frame[14];
    ip[0] = (uint8_t)(0x40U | ihl_words);
    uint16_t total = (uint16_t)(header_length + payload_length);
    ip[2] = (uint8_t)(total >> 8U);
    ip[3] = (uint8_t)total;
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 17U;
    ip[12] = 10U; ip[13] = 0U; ip[14] = 2U; ip[15] = 2U;
    ip[16] = 10U; ip[17] = 0U; ip[18] = 2U; ip[19] = 15U;
    for (uint16_t index = 20U; index < header_length; ++index)
        ip[index] = (uint8_t)index;
    set_checksum(ip, header_length);
}

int main(void) {
    uint8_t frame[REIST_IPV4_MAX_FRAME_SIZE];
    reist_ipv4_parse_result_t result;
    make_frame(frame, 5U, 8U);
    if (reist_ipv4_parse_frame(frame, 42U, &result) != 0) return 1;
    if (result.version != REIST_IPV4_PARSE_RESULT_VERSION ||
        result.struct_size != sizeof(result) || result.source_ip != 0x0A000202U ||
        result.destination_ip != 0x0A00020FU || result.header_length != 20U ||
        result.total_length != 28U || result.payload_offset != 34U ||
        result.payload_length != 8U || result.protocol != 17U ||
        result.ttl != 64U || result.reserved != 0U) return 2;

    frame[24] ^= 1U;
    if (reist_ipv4_parse_frame(frame, 42U, &result) !=
        REIST_IPV4_PARSE_EINTEGRITY) return 3;
    for (uint32_t index = 0U; index < sizeof(result); ++index)
        if (((uint8_t *)&result)[index] != 0U) return 4;

    make_frame(frame, 5U, 8U);
    frame[20] = 0x20U;
    set_checksum(&frame[14], 20U);
    if (reist_ipv4_parse_frame(frame, 42U, &result) !=
        REIST_IPV4_PARSE_EPROTONOSUPPORT) return 5;

    make_frame(frame, 6U, 0U);
    if (reist_ipv4_parse_frame(frame, 38U, &result) != 0 ||
        result.header_length != 24U || result.payload_offset != 38U ||
        result.payload_length != 0U) return 6;

    frame[12] = 0x08U; frame[13] = 0x06U;
    if (reist_ipv4_parse_frame(frame, 38U, &result) !=
        REIST_IPV4_PARSE_EPROTONOSUPPORT) return 7;
    if (reist_ipv4_parse_frame(frame, 33U, &result) !=
        REIST_IPV4_PARSE_EMSGSIZE) return 8;
    if (reist_ipv4_parse_frame(frame, REIST_IPV4_MAX_FRAME_SIZE + 1U,
                               &result) != REIST_IPV4_PARSE_EMSGSIZE) return 9;
    if (reist_ipv4_parse_frame(NULL, 0U, &result) !=
        REIST_IPV4_PARSE_EINVAL) return 10;
    if (reist_ipv4_parse_frame(frame, 38U, NULL) !=
        REIST_IPV4_PARSE_EINVAL) return 11;
    return 0;
}
