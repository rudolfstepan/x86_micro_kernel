#include "reist_ipv4_parser.h"

#include <stdbool.h>
#include <stddef.h>

#define ETHERNET_HEADER_SIZE 14U
#define IPV4_MIN_HEADER_SIZE 20U
#define IPV4_MAX_HEADER_SIZE 60U
#define IPV4_ETHERTYPE 0x0800U

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | bytes[3];
}

static bool checksum_valid(const uint8_t *header, uint16_t length) {
    uint32_t sum = 0U;
    for (uint16_t offset = 0U; offset < length; offset += 2U)
        sum += read_be16(&header[offset]);
    while ((sum >> 16U) != 0U)
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)sum == 0xFFFFU;
}

uint32_t reist_frame_crc32(const uint8_t *frame, uint32_t frame_length) {
    if (frame == NULL || frame_length > REIST_IPV4_MAX_FRAME_SIZE) return 0U;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < frame_length; ++index) {
        crc ^= frame[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

int reist_ipv4_parse_frame(const uint8_t *frame, uint32_t frame_length,
                           reist_ipv4_parse_result_t *result) {
    if (result == NULL) return REIST_IPV4_PARSE_EINVAL;
    *result = (reist_ipv4_parse_result_t){0};
    if (frame == NULL) return REIST_IPV4_PARSE_EINVAL;
    if (frame_length < ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE ||
        frame_length > REIST_IPV4_MAX_FRAME_SIZE)
        return REIST_IPV4_PARSE_EMSGSIZE;
    if (read_be16(&frame[12U]) != IPV4_ETHERTYPE)
        return REIST_IPV4_PARSE_EPROTONOSUPPORT;

    const uint8_t *header = &frame[ETHERNET_HEADER_SIZE];
    uint8_t version = header[0U] >> 4U;
    uint16_t header_length = (uint16_t)(header[0U] & 0x0FU) * 4U;
    if (version != 4U || header_length < IPV4_MIN_HEADER_SIZE ||
        header_length > IPV4_MAX_HEADER_SIZE ||
        ETHERNET_HEADER_SIZE + header_length > frame_length)
        return REIST_IPV4_PARSE_EINVAL;
    uint16_t total_length = read_be16(&header[2U]);
    if (total_length < header_length ||
        (uint32_t)ETHERNET_HEADER_SIZE + total_length > frame_length)
        return REIST_IPV4_PARSE_EMSGSIZE;
    if ((read_be16(&header[6U]) & 0x3FFFU) != 0U)
        return REIST_IPV4_PARSE_EPROTONOSUPPORT;
    if (header[8U] == 0U || !checksum_valid(header, header_length))
        return REIST_IPV4_PARSE_EINTEGRITY;

    *result = (reist_ipv4_parse_result_t){
        .version = REIST_IPV4_PARSE_RESULT_VERSION,
        .struct_size = sizeof(*result),
        .source_ip = read_be32(&header[12U]),
        .destination_ip = read_be32(&header[16U]),
        .header_length = header_length,
        .total_length = total_length,
        .payload_offset = (uint16_t)(ETHERNET_HEADER_SIZE + header_length),
        .payload_length = (uint16_t)(total_length - header_length),
        .protocol = header[9U],
        .ttl = header[8U],
    };
    return 0;
}
