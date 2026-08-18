/**
 * @file userspace/sdk/include/reist_udp_parser.h
 * @brief UDP-Parservertrag für überwachte Netzwerkdienste.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#ifndef REIST_UDP_PARSER_H
#define REIST_UDP_PARSER_H

#include <stdint.h>

#define REIST_UDP_PARSE_RESULT_VERSION 1U
#define REIST_UDP_PARSE_RESULT_SIZE 20U

#define REIST_UDP_PARSE_EINVAL (-22)
#define REIST_UDP_PARSE_EINTEGRITY (-84)
#define REIST_UDP_PARSE_EMSGSIZE (-90)
#define REIST_UDP_PARSE_EPROTONOSUPPORT (-93)

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t datagram_length;
    uint16_t payload_offset;
    uint16_t payload_length;
    uint16_t checksum;
} reist_udp_parse_result_t;

_Static_assert(sizeof(reist_udp_parse_result_t) == REIST_UDP_PARSE_RESULT_SIZE,
               "reist_udp_parse_result_t ABI drift");

int reist_udp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                          reist_udp_parse_result_t *result);

#endif
