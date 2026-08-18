/**
 * @file userspace/sdk/include/reist_dhcp_parser.h
 * @brief DHCP-Parser- und Optionsvertrag.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#ifndef REIST_DHCP_PARSER_H
#define REIST_DHCP_PARSER_H

#include <stdint.h>

#define REIST_DHCP_PARSE_RESULT_VERSION 1U
#define REIST_DHCP_PARSE_RESULT_SIZE 52U
#define REIST_DHCP_MAX_MESSAGE_SIZE 548U

#define REIST_DHCP_MESSAGE_OFFER 2U
#define REIST_DHCP_MESSAGE_ACK 5U
#define REIST_DHCP_MESSAGE_NAK 6U

#define REIST_DHCP_OPTION_NETMASK 0x01U
#define REIST_DHCP_OPTION_GATEWAY 0x02U
#define REIST_DHCP_OPTION_DNS 0x04U
#define REIST_DHCP_OPTION_LEASE 0x08U
#define REIST_DHCP_OPTION_MESSAGE_TYPE 0x10U
#define REIST_DHCP_OPTION_SERVER_ID 0x20U

#define REIST_DHCP_PARSE_EINVAL (-22)
#define REIST_DHCP_PARSE_EINTEGRITY (-84)
#define REIST_DHCP_PARSE_EMSGSIZE (-90)
#define REIST_DHCP_PARSE_EPROTONOSUPPORT (-93)

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t transaction_id;
    uint32_t offered_ip;
    uint32_t server_id;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t lease_seconds;
    uint32_t option_flags;
    uint16_t payload_offset;
    uint16_t payload_length;
    uint8_t client_mac[6];
    uint8_t message_type;
    uint8_t checksum_present;
} reist_dhcp_parse_result_t;

_Static_assert(sizeof(reist_dhcp_parse_result_t) ==
               REIST_DHCP_PARSE_RESULT_SIZE,
               "reist_dhcp_parse_result_t ABI drift");

int reist_dhcp_parse_frame(const uint8_t *frame, uint32_t frame_length,
                           reist_dhcp_parse_result_t *result);

#endif
