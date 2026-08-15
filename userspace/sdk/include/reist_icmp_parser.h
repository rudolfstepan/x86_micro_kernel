#ifndef REIST_ICMP_PARSER_H
#define REIST_ICMP_PARSER_H

#include <stdint.h>

#define REIST_ICMP_PARSE_RESULT_VERSION 1U
#define REIST_ICMP_PARSE_RESULT_SIZE 28U

#define REIST_ICMP_PARSE_EINVAL (-22)
#define REIST_ICMP_PARSE_EINTEGRITY (-84)
#define REIST_ICMP_PARSE_EMSGSIZE (-90)
#define REIST_ICMP_PARSE_EPROTONOSUPPORT (-93)

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint16_t payload_offset;
    uint16_t payload_length;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t type;
    uint8_t code;
    uint16_t reserved;
} reist_icmp_parse_result_t;

_Static_assert(sizeof(reist_icmp_parse_result_t) ==
               REIST_ICMP_PARSE_RESULT_SIZE,
               "reist_icmp_parse_result_t ABI drift");

int reist_icmp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                           reist_icmp_parse_result_t *result);

#endif
