/**
 * @file userspace/sdk/include/reist_tcp_parser.h
 * @brief Bounded TCP segment parser for the supervised Ring-3 service.
 */
#ifndef REIST_TCP_PARSER_H
#define REIST_TCP_PARSER_H

#include <stdint.h>

#define REIST_TCP_PARSE_RESULT_VERSION 1U
#define REIST_TCP_PARSE_EINVAL (-22)
#define REIST_TCP_PARSE_EINTEGRITY (-84)
#define REIST_TCP_PARSE_EMSGSIZE (-90)
#define REIST_TCP_PARSE_EPROTONOSUPPORT (-93)

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t window;
    uint16_t payload_offset;
    uint16_t payload_length;
    uint8_t flags;
    uint8_t header_length;
} reist_tcp_parse_result_t;

_Static_assert(sizeof(reist_tcp_parse_result_t) == 28U,
               "TCP parser result ABI drift");

int reist_tcp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                          reist_tcp_parse_result_t *result);

#endif
