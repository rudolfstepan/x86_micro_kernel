/** @file test/test_reist_tcp_parser_host.c @brief TCP parser host test. */
#include <stdint.h>
#include <string.h>
#include "reist_tcp_parser.h"

#define CAPACITY 1518U
static uint32_t add(const uint8_t *bytes, uint16_t length, uint32_t sum) {
    uint16_t index = 0U;
    for (; index + 1U < length; index += 2U)
        sum += ((uint16_t)bytes[index] << 8U) | bytes[index + 1U];
    if (index < length) sum += (uint16_t)bytes[index] << 8U;
    return sum;
}
static uint16_t finish(uint32_t sum) {
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    return (uint16_t)~sum;
}
static uint32_t frame(uint8_t *bytes) {
    memset(bytes, 0, CAPACITY); bytes[12] = 8U;
    uint8_t *ip = bytes + 14U; ip[0] = 0x45U; ip[2] = 0U; ip[3] = 43U;
    ip[6] = 0x40U; ip[8] = 64U; ip[9] = 6U;
    ip[12] = 10U; ip[15] = 2U; ip[16] = 10U; ip[19] = 1U;
    uint16_t ip_sum = finish(add(ip, 20U, 0U));
    ip[10] = (uint8_t)(ip_sum >> 8U); ip[11] = (uint8_t)ip_sum;
    uint8_t *tcp = ip + 20U;
    tcp[0] = 0x01U; tcp[1] = 0xbbU; tcp[2] = 0xc0U; tcp[3] = 0x01U;
    tcp[7] = 10U; tcp[11] = 20U; tcp[12] = 0x50U; tcp[13] = 0x18U;
    tcp[14] = 0x10U; tcp[15] = 0U; tcp[20] = 'a'; tcp[21] = 'b'; tcp[22] = 'c';
    uint32_t sum = add(ip + 12U, 8U, 0U) + 6U + 23U;
    uint16_t tcp_sum = finish(add(tcp, 23U, sum));
    tcp[16] = (uint8_t)(tcp_sum >> 8U); tcp[17] = (uint8_t)tcp_sum;
    return 57U;
}
int main(void) {
    uint8_t bytes[CAPACITY]; reist_tcp_parse_result_t result;
    uint32_t length = frame(bytes);
    if (reist_tcp_parse_frame(bytes, length, &result) != 0) return 1;
    if (result.source_port != 443U || result.destination_port != 49153U ||
        result.sequence != 10U || result.acknowledgement != 20U ||
        result.flags != 0x18U || result.payload_offset != 54U ||
        result.payload_length != 3U) return 2;
    bytes[56] ^= 1U;
    if (reist_tcp_parse_frame(bytes, length, &result) !=
        REIST_TCP_PARSE_EINTEGRITY) return 3;
    length = frame(bytes); bytes[46] = 0x40U;
    if (reist_tcp_parse_frame(bytes, length, &result) !=
        REIST_TCP_PARSE_EMSGSIZE) return 4;
    return 0;
}
