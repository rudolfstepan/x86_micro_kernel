/**
 * @file test/test_reist_udp_parser_host.c
 * @brief Hostseitiger Regressionstest für reist udp parser.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include <stdint.h>
#include <string.h>

#include "reist_udp_parser.h"

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

static void set_ipv4_checksum(uint8_t *ip) {
    ip[10] = 0U;
    ip[11] = 0U;
    uint16_t checksum = finish_checksum(add_bytes(ip, 20U, 0U));
    ip[10] = (uint8_t)(checksum >> 8U);
    ip[11] = (uint8_t)checksum;
}

static uint32_t make_frame(uint8_t *frame, uint16_t payload_length) {
    memset(frame, 0, FRAME_CAPACITY);
    frame[12] = 0x08U;
    frame[13] = 0x00U;
    uint8_t *ip = &frame[14];
    uint16_t udp_length = (uint16_t)(8U + payload_length);
    uint16_t ip_length = (uint16_t)(20U + udp_length);
    ip[0] = 0x45U;
    ip[2] = (uint8_t)(ip_length >> 8U);
    ip[3] = (uint8_t)ip_length;
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 17U;
    ip[12] = 10U; ip[13] = 0U; ip[14] = 2U; ip[15] = 2U;
    ip[16] = 10U; ip[17] = 0U; ip[18] = 2U; ip[19] = 15U;
    set_ipv4_checksum(ip);

    uint8_t *udp = &ip[20];
    udp[0] = 0x23U; udp[1] = 0x28U;
    udp[2] = 0x23U; udp[3] = 0x29U;
    udp[4] = (uint8_t)(udp_length >> 8U);
    udp[5] = (uint8_t)udp_length;
    for (uint16_t index = 0U; index < payload_length; ++index)
        udp[8U + index] = (uint8_t)(0xA0U + index);
    uint32_t sum = add_bytes(&ip[12], 8U, 0U);
    sum += 17U;
    sum += udp_length;
    sum = add_bytes(udp, udp_length, sum);
    uint16_t checksum = finish_checksum(sum);
    if (checksum == 0U) checksum = 0xFFFFU;
    udp[6] = (uint8_t)(checksum >> 8U);
    udp[7] = (uint8_t)checksum;
    return (uint32_t)14U + ip_length;
}

int main(void) {
    uint8_t frame[FRAME_CAPACITY];
    reist_udp_parse_result_t result;
    uint32_t length = make_frame(frame, 5U);
    if (reist_udp_parse_frame(frame, length, &result) != 0) return 1;
    if (result.version != REIST_UDP_PARSE_RESULT_VERSION ||
        result.struct_size != sizeof(result) || result.source_port != 9000U ||
        result.destination_port != 9001U || result.datagram_length != 13U ||
        result.payload_offset != 42U || result.payload_length != 5U ||
        result.checksum == 0U) return 2;

    frame[46] ^= 1U;
    if (reist_udp_parse_frame(frame, length, &result) !=
        REIST_UDP_PARSE_EINTEGRITY) return 3;
    for (uint32_t index = 0U; index < sizeof(result); ++index)
        if (((uint8_t *)&result)[index] != 0U) return 4;

    length = make_frame(frame, 4U);
    frame[40] = 0U; frame[41] = 0U;
    if (reist_udp_parse_frame(frame, length, &result) != 0 ||
        result.checksum != 0U) return 5;

    length = make_frame(frame, 4U);
    frame[38] = 0U; frame[39] = 9U;
    if (reist_udp_parse_frame(frame, length, &result) !=
        REIST_UDP_PARSE_EMSGSIZE) return 6;

    length = make_frame(frame, 4U);
    frame[23] = 1U;
    set_ipv4_checksum(&frame[14]);
    if (reist_udp_parse_frame(frame, length, &result) !=
        REIST_UDP_PARSE_EPROTONOSUPPORT) return 7;
    length = make_frame(frame, 4U);
    frame[34] = 0U; frame[35] = 0U;
    if (reist_udp_parse_frame(frame, length, &result) !=
        REIST_UDP_PARSE_EINVAL) return 8;
    if (reist_udp_parse_frame(NULL, 0U, &result) !=
        REIST_UDP_PARSE_EINVAL) return 9;
    if (reist_udp_parse_frame(frame, length, NULL) !=
        REIST_UDP_PARSE_EINVAL) return 10;
    return 0;
}
