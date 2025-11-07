#ifndef NETSTACK_H
#define NETSTACK_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// ETHERNET LAYER (Layer 2)
// =============================================================================

#define ETH_ADDR_LEN 6
#define ETH_HEADER_LEN 14
#define ETH_MAX_PAYLOAD 1500

// EtherTypes
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV6 0x86DD

typedef struct {
    uint8_t dst_mac[ETH_ADDR_LEN];
    uint8_t src_mac[ETH_ADDR_LEN];
    uint16_t ethertype;
} __attribute__((packed)) eth_header_t;

// =============================================================================
// ARP PROTOCOL (Address Resolution Protocol)
// =============================================================================

#define ARP_REQUEST 1
#define ARP_REPLY   2

#define ARP_HARDWARE_ETHERNET 1
#define ARP_PROTOCOL_IPV4     0x0800

typedef struct {
    uint16_t hardware_type;      // 1 = Ethernet
    uint16_t protocol_type;      // 0x0800 = IPv4
    uint8_t  hardware_addr_len;  // 6 for MAC
    uint8_t  protocol_addr_len;  // 4 for IPv4
    uint16_t operation;          // 1 = Request, 2 = Reply
    uint8_t  sender_mac[ETH_ADDR_LEN];
    uint32_t sender_ip;
    uint8_t  target_mac[ETH_ADDR_LEN];
    uint32_t target_ip;
} __attribute__((packed)) arp_packet_t;

// ARP Cache Entry
#define ARP_CACHE_SIZE 32
typedef struct {
    uint32_t ip;
    uint8_t mac[ETH_ADDR_LEN];
    uint32_t timestamp;
    bool valid;
} arp_cache_entry_t;

// =============================================================================
// IP LAYER (Layer 3)
// =============================================================================

#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP  6
#define IP_PROTOCOL_UDP  17

typedef struct {
    uint8_t  version_ihl;        // Version (4 bits) + IHL (4 bits)
    uint8_t  tos;                // Type of Service
    uint16_t total_length;       // Total length (header + data)
    uint16_t identification;     // Identification
    uint16_t flags_fragment;     // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t  ttl;                // Time to Live
    uint8_t  protocol;           // Protocol (1=ICMP, 6=TCP, 17=UDP)
    uint16_t header_checksum;    // Header checksum
    uint32_t src_ip;             // Source IP address
    uint32_t dst_ip;             // Destination IP address
} __attribute__((packed)) ip_header_t;

// Helper macros for IP header
#define IP_VERSION(iph)    ((iph)->version_ihl >> 4)
#define IP_IHL(iph)        ((iph)->version_ihl & 0x0F)
#define IP_HEADER_LEN(iph) (IP_IHL(iph) * 4)

// =============================================================================
// ICMP PROTOCOL (Internet Control Message Protocol)
// =============================================================================

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

// =============================================================================
// UDP PROTOCOL (User Datagram Protocol)
// =============================================================================

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

// =============================================================================
// TCP PROTOCOL (Transmission Control Protocol)
// =============================================================================

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset_reserved;  // Data offset (4 bits) + Reserved (3 bits) + NS flag
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} __attribute__((packed)) tcp_header_t;

#define TCP_DATA_OFFSET(tcph) ((tcph)->data_offset_reserved >> 4)
#define TCP_HEADER_LEN(tcph)  (TCP_DATA_OFFSET(tcph) * 4)

// TCP Connection State
typedef enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

// =============================================================================
// NETWORK CONFIGURATION
// =============================================================================

typedef struct {
    uint32_t ip_address;      // Our IP address
    uint32_t netmask;         // Network mask
    uint32_t gateway;         // Default gateway
    uint32_t dns_server;      // DNS server
    uint8_t  mac_address[ETH_ADDR_LEN];  // Our MAC address
} network_config_t;

// =============================================================================
// NETWORK STACK API
// =============================================================================

// Initialization
void netstack_init(void);
void netstack_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway);

// Packet Processing
void netstack_process_packet(uint8_t *packet, uint16_t length);

// ARP Functions
void arp_send_request(uint32_t target_ip);
void arp_send_reply(uint32_t target_ip, uint8_t *target_mac);
bool arp_lookup(uint32_t ip, uint8_t *mac_out);
void arp_add_entry(uint32_t ip, uint8_t *mac);

// ICMP Functions
void icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq);
void icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq, uint8_t *data, uint16_t data_len);

// UDP Functions
int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t *data, uint16_t length);
typedef void (*udp_callback_t)(uint32_t src_ip, uint16_t src_port, uint8_t *data, uint16_t length);
void udp_bind(uint16_t port, udp_callback_t callback);

// TCP Functions (simplified for now)
int tcp_connect(uint32_t dst_ip, uint16_t dst_port);
int tcp_send(int socket, uint8_t *data, uint16_t length);
int tcp_recv(int socket, uint8_t *buffer, uint16_t max_length);
void tcp_close(int socket);

// Utility Functions
uint16_t ip_checksum(void *data, uint16_t length);
uint32_t parse_ipv4(const char *ip_string);  // "192.168.1.1" -> uint32_t
void format_ipv4(uint32_t ip, char *buffer);  // uint32_t -> "192.168.1.1"
void format_mac(uint8_t *mac, char *buffer);  // MAC -> "AA:BB:CC:DD:EE:FF"

// Network byte order conversion (big-endian <-> little-endian)
uint16_t htons(uint16_t host_short);
uint32_t htonl(uint32_t host_long);
uint16_t ntohs(uint16_t net_short);
uint32_t ntohl(uint32_t net_long);

#endif // NETSTACK_H
