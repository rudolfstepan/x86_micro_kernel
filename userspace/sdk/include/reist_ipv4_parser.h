#ifndef REIST_IPV4_PARSER_H
#define REIST_IPV4_PARSER_H

#include <stdint.h>

#define REIST_IPV4_PARSE_RESULT_VERSION 1U
#define REIST_IPV4_PARSE_RESULT_SIZE 28U
#define REIST_IPV4_MAX_FRAME_SIZE 1518U

#define REIST_IPV4_PARSE_EINVAL (-22)
#define REIST_IPV4_PARSE_EINTEGRITY (-84)
#define REIST_IPV4_PARSE_EMSGSIZE (-90)
#define REIST_IPV4_PARSE_EPROTONOSUPPORT (-93)

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint16_t header_length;
    uint16_t total_length;
    uint16_t payload_offset;
    uint16_t payload_length;
    uint8_t protocol;
    uint8_t ttl;
    uint16_t reserved;
} reist_ipv4_parse_result_t;

_Static_assert(sizeof(reist_ipv4_parse_result_t) ==
               REIST_IPV4_PARSE_RESULT_SIZE,
               "reist_ipv4_parse_result_t ABI drift");

int reist_ipv4_parse_frame(const uint8_t *frame, uint32_t frame_length,
                           reist_ipv4_parse_result_t *result);

#endif
