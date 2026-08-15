#include <stdint.h>
#include <string.h>

#include "reist_icmp_parser.h"

#define FRAME_CAPACITY 1518U

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

static void set_checksum(uint8_t *bytes, uint16_t length, uint16_t offset) {
    bytes[offset] = 0U;
    bytes[offset + 1U] = 0U;
    uint16_t checksum = finish_checksum(add_bytes(bytes, length, 0U));
    bytes[offset] = (uint8_t)(checksum >> 8U);
    bytes[offset + 1U] = (uint8_t)checksum;
}

static uint32_t make_frame(uint8_t *frame, uint8_t type,
                           uint16_t payload_length) {
    memset(frame, 0, FRAME_CAPACITY);
    frame[12] = 0x08U;
    frame[13] = 0x00U;
    uint8_t *ip = &frame[14];
    uint16_t icmp_length = (uint16_t)(8U + payload_length);
    uint16_t ip_length = (uint16_t)(20U + icmp_length);
    ip[0] = 0x45U;
    ip[2] = (uint8_t)(ip_length >> 8U);
    ip[3] = (uint8_t)ip_length;
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 1U;
    ip[12] = 10U; ip[13] = 0U; ip[14] = 2U; ip[15] = 2U;
    ip[16] = 10U; ip[17] = 0U; ip[18] = 2U; ip[19] = 15U;
    set_checksum(ip, 20U, 10U);

    uint8_t *icmp = &ip[20];
    icmp[0] = type;
    icmp[4] = 0x12U; icmp[5] = 0x34U;
    icmp[6] = 0x56U; icmp[7] = 0x78U;
    for (uint16_t index = 0U; index < payload_length; ++index)
        icmp[8U + index] = (uint8_t)(0xA0U + index);
    set_checksum(icmp, icmp_length, 2U);
    return (uint32_t)14U + ip_length;
}

static int result_is_zero(const reist_icmp_parse_result_t *result) {
    const uint8_t *bytes = (const uint8_t *)result;
    for (uint32_t index = 0U; index < sizeof(*result); ++index)
        if (bytes[index] != 0U) return 0;
    return 1;
}

int main(void) {
    uint8_t frame[FRAME_CAPACITY];
    reist_icmp_parse_result_t result;
    uint32_t length = make_frame(frame, 8U, 5U);
    if (reist_icmp_parse_frame(frame, length, &result) != 0) return 1;
    if (result.version != REIST_ICMP_PARSE_RESULT_VERSION ||
        result.struct_size != sizeof(result) ||
        result.source_ip != 0x0A000202U ||
        result.destination_ip != 0x0A00020FU ||
        result.payload_offset != 42U || result.payload_length != 5U ||
        result.identifier != 0x1234U || result.sequence != 0x5678U ||
        result.type != 8U || result.code != 0U || result.reserved != 0U)
        return 2;

    frame[46] ^= 1U;
    if (reist_icmp_parse_frame(frame, length, &result) !=
        REIST_ICMP_PARSE_EINTEGRITY || !result_is_zero(&result)) return 3;

    length = make_frame(frame, 0U, 4U);
    if (reist_icmp_parse_frame(frame, length, &result) != 0 ||
        result.type != 0U) return 4;

    length = make_frame(frame, 3U, 4U);
    if (reist_icmp_parse_frame(frame, length, &result) !=
        REIST_ICMP_PARSE_EPROTONOSUPPORT || !result_is_zero(&result)) return 5;

    length = make_frame(frame, 8U, 4U);
    frame[23] = 17U;
    set_checksum(&frame[14], 20U, 10U);
    if (reist_icmp_parse_frame(frame, length, &result) !=
        REIST_ICMP_PARSE_EPROTONOSUPPORT) return 6;

    length = make_frame(frame, 8U, 4U);
    frame[16] = 0U; frame[17] = 27U;
    set_checksum(&frame[14], 20U, 10U);
    if (reist_icmp_parse_frame(frame, 41U, &result) !=
        REIST_ICMP_PARSE_EMSGSIZE) return 7;
    if (reist_icmp_parse_frame(NULL, 0U, &result) !=
        REIST_ICMP_PARSE_EINVAL) return 8;
    if (reist_icmp_parse_frame(frame, length, NULL) !=
        REIST_ICMP_PARSE_EINVAL) return 9;
    return 0;
}
