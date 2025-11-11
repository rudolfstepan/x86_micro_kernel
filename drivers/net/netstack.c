#include "drivers/net/netstack.h"
#include "drivers/net/e1000.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"

// =============================================================================
// GLOBAL STATE
// =============================================================================

static network_config_t net_config;
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static uint16_t ip_identification = 0;

// Declare external byte order functions from ethernet.c
extern uint16_t htons(uint16_t host_short);

// Create the other byte order functions
uint32_t htonl(uint32_t host_long) {
    return ((host_long & 0xFF000000) >> 24) |
           ((host_long & 0x00FF0000) >> 8) |
           ((host_long & 0x0000FF00) << 8) |
           ((host_long & 0x000000FF) << 24);
}

uint16_t ntohs(uint16_t net_short) {
    return htons(net_short);  // Same operation, use existing function
}

uint32_t ntohl(uint32_t net_long) {
    return htonl(net_long);  // Same operation
}

// =============================================================================
// CHECKSUM CALCULATION
// =============================================================================

uint16_t ip_checksum(void *data, uint16_t length) {
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;
    
    // Sum all 16-bit words
    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }
    
    // Add leftover byte if odd length
    if (length > 0) {
        sum += *(uint8_t *)ptr;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

uint32_t parse_ipv4(const char *ip_string) {
    uint32_t ip = 0;
    int octets[4];
    int count = 0;
    int current = 0;
    
    for (int i = 0; ip_string[i] != '\0'; i++) {
        if (ip_string[i] == '.') {
            octets[count++] = current;
            current = 0;
        } else if (ip_string[i] >= '0' && ip_string[i] <= '9') {
            current = current * 10 + (ip_string[i] - '0');
        }
    }
    octets[count++] = current;
    
    if (count == 4) {
        ip = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    }
    
    return ip;
}

void format_ipv4(uint32_t ip, char *buffer) {
    sprintf(buffer, "%d.%d.%d.%d",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF,
            ip & 0xFF);
}

void format_mac(uint8_t *mac, char *buffer) {
    // Manually format MAC address since sprintf doesn't handle %02X properly
    const char hex[] = "0123456789ABCDEF";
    int pos = 0;
    for (int i = 0; i < 6; i++) {
        buffer[pos++] = hex[(mac[i] >> 4) & 0x0F];  // High nibble
        buffer[pos++] = hex[mac[i] & 0x0F];         // Low nibble
        if (i < 5) {
            buffer[pos++] = ':';
        }
    }
    buffer[pos] = '\0';
}

// Getter for current IP address
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

// Minimal DHCP definitions
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5

#define DHCP_MAGIC_COOKIE 0x63825363

struct dhcp_packet {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint8_t options[312];
} __attribute__((packed));

#include "drivers/net/rtl8139.h"
#include "drivers/net/ne2000.h"
#include "drivers/net/e1000.h"

int netstack_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void *data, size_t len) {
    // Build Ethernet + IPv4 + UDP headers
    uint8_t packet[1514] = {0};
    uint8_t *ptr = packet;

    // Ethernet header
    uint8_t dst_mac[6];
    if (!arp_lookup(dst_ip, dst_mac)) {
        printf("[UDP] No ARP entry for destination, sending ARP request\n");
        arp_send_request(dst_ip);
        return -1;
    }
    memcpy(ptr, dst_mac, 6); ptr += 6;
    memcpy(ptr, net_config.mac_address, 6); ptr += 6;
    *(uint16_t*)ptr = htons(0x0800); ptr += 2; // EtherType IPv4

    // IPv4 header
    ip_header_t *ip = (ip_header_t*)ptr;
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_length = htons(sizeof(ip_header_t) + sizeof(udp_header_t) + len);
    ip->identification = htons(ip_identification++);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_UDP;
    ip->src_ip = htonl(net_config.ip_address);
    ip->dst_ip = htonl(dst_ip);
    ip->header_checksum = 0;
    ip->header_checksum = ip_checksum(ip, sizeof(ip_header_t));
    ptr += sizeof(ip_header_t);

    // UDP header
    udp_header_t *udp = (udp_header_t*)ptr;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(sizeof(udp_header_t) + len);
    udp->checksum = 0; // Optional for IPv4
    ptr += sizeof(udp_header_t);

    // Payload
    memcpy(ptr, data, len);
    ptr += len;

    size_t total_len = ptr - packet;

    // Send using available NIC
    if (ne2000_is_initialized()) {
        ne2000_send_packet(packet, total_len);
        return 0;
    } else if (e1000_is_initialized()) {
        e1000_send_packet(packet, total_len);
        return 0;
    } else if (rtl8139_is_initialized()) {
        rtl8139_send_packet(packet, total_len);
        return 0;
    } else {
        printf("[UDP] No supported NIC initialized\n");
        return -1;
    }
}
int netstack_receive_udp(uint16_t port, void *buffer, size_t buflen, uint32_t *src_ip, uint16_t *src_port) {
    uint8_t pkt[1514];
    int len = -1;

    // Try each NIC for a received packet
    if (ne2000_is_initialized()) {
        len = ne2000_receive_packet(pkt, sizeof(pkt));
    } else if (e1000_is_initialized()) {
        len = e1000_receive_packet(pkt, sizeof(pkt));
    } else if (rtl8139_is_initialized()) {
        // No direct receive function, skip for now
        return -1;
    } else {
        return -1;
    }

    if (len < 42) return -1; // Minimum Ethernet+IP+UDP

    // Parse Ethernet header
    uint16_t ethertype = (pkt[12] << 8) | pkt[13];
    if (ethertype != 0x0800) return -1; // Not IPv4

    // Parse IP header
    ip_header_t *ip = (ip_header_t *)(pkt + 14);
    if (ip->protocol != IP_PROTOCOL_UDP) return -1;

    int ip_header_len = (ip->version_ihl & 0x0F) * 4;
    udp_header_t *udp = (udp_header_t *)(pkt + 14 + ip_header_len);

    if (ntohs(udp->dst_port) != port) return -1;

    int udp_len = ntohs(udp->length) - sizeof(udp_header_t);
    if (udp_len > 0 && udp_len <= (len - 14 - ip_header_len - sizeof(udp_header_t))) {
        if (src_ip) *src_ip = ntohl(ip->src_ip);
        if (src_port) *src_port = ntohs(udp->src_port);
        memcpy(buffer, pkt + 14 + ip_header_len + sizeof(udp_header_t), udp_len < buflen ? udp_len : buflen);
        return udp_len < buflen ? udp_len : buflen;
    }
    return -1;
}
// Forward declarations for sending/receiving raw UDP packets
extern int netstack_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void *data, size_t len);
extern int netstack_receive_udp(uint16_t port, void *buffer, size_t buflen, uint32_t *src_ip, uint16_t *src_port);

// Minimal random function for xid
static uint32_t dhcp_random_xid(void) {
    static uint32_t seed = 0x12345678;
    seed = seed * 1664525 + 1013904223;
    return seed;
}

// Send DHCPDISCOVER and wait for DHCPOFFER
static uint32_t dhcp_request_ip(uint8_t *mac) {
    struct dhcp_packet discover;
    memset(&discover, 0, sizeof(discover));
    discover.op = 1; // BOOTREQUEST
    discover.htype = 1; // Ethernet
    discover.hlen = 6;
    discover.xid = dhcp_random_xid();
    memcpy(discover.chaddr, mac, 6);
    uint8_t *opt = discover.options;
    *(uint32_t*)opt = __builtin_bswap32(DHCP_MAGIC_COOKIE); opt += 4;
    *opt++ = 53; *opt++ = 1; *opt++ = DHCP_DISCOVER; // DHCP Message Type
    *opt++ = 55; *opt++ = 1; *opt++ = 1; // Parameter Request List: Subnet Mask
    *opt++ = 255; // End

    netstack_send_udp(0xFFFFFFFF, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &discover, sizeof(discover));

    // Wait for DHCPOFFER (very simple, not robust)
    struct dhcp_packet offer;
    uint32_t src_ip;
    uint16_t src_port;
    int r = netstack_receive_udp(DHCP_CLIENT_PORT, &offer, sizeof(offer), &src_ip, &src_port);
    if (r > 0 && offer.op == 2 && offer.xid == discover.xid) {
        // Find offered IP
        return offer.yiaddr;
    }
    return 0;
}

uint32_t netstack_get_ip_address(void) {
    if (net_config.ip_address == 0) {
        // Try to get IP from DHCP
        uint32_t ip = dhcp_request_ip(net_config.mac_address);
        if (ip != 0) {
            net_config.ip_address = ip;
        }
    }
    return net_config.ip_address;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void netstack_init(void) {
    printf("Initializing network stack...\n");
    
    // Clear ARP cache
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = false;
    }
    
    // Get MAC address from E1000
    e1000_get_mac_address(net_config.mac_address);
    
    // Default configuration (will be set by user)
    net_config.ip_address = 0;
    net_config.netmask = 0;
    net_config.gateway = 0;
    net_config.dns_server = 0;
    
    //printf("Network stack initialized.\n");    net debug
    // printf("MAC: ");
    // char mac_str[18];
    // format_mac(net_config.mac_address, mac_str);
    // printf("%s\n", mac_str);
}

void netstack_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    net_config.ip_address = ip;
    net_config.netmask = netmask;
    net_config.gateway = gateway;
    
    char ip_str[16];
    format_ipv4(ip, ip_str);
    printf("IP configured: %s\n", ip_str);
}

// =============================================================================
// ARP PROTOCOL
// =============================================================================

void arp_add_entry(uint32_t ip, uint8_t *mac) {
    // Find free slot or replace oldest
    int slot = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        slot = 0;  // Replace first entry (simple strategy)
    }
    
    arp_cache[slot].ip = ip;
    memcpy(arp_cache[slot].mac, mac, ETH_ADDR_LEN);
    arp_cache[slot].valid = true;
    arp_cache[slot].timestamp = 0;  // TODO: Add actual timestamp
    
    char ip_str[16], mac_str[18];
    format_ipv4(ip, ip_str);
    format_mac(mac, mac_str);
    printf("[ARP] Added: %s -> %s\n", ip_str, mac_str);
}

bool arp_lookup(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac_out, arp_cache[i].mac, ETH_ADDR_LEN);
            return true;
        }
    }
    return false;
}

void arp_send_request(uint32_t target_ip) {
    uint8_t packet[sizeof(eth_header_t) + sizeof(arp_packet_t)];
    eth_header_t *eth = (eth_header_t *)packet;
    arp_packet_t *arp = (arp_packet_t *)(packet + sizeof(eth_header_t));
    
    // Ethernet header (broadcast)
    memset(eth->dst_mac, 0xFF, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_ARP);
    
    // ARP packet
    arp->hardware_type = htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type = htons(ARP_PROTOCOL_IPV4);
    arp->hardware_addr_len = ETH_ADDR_LEN;
    arp->protocol_addr_len = 4;
    arp->operation = htons(ARP_REQUEST);
    
    memcpy(arp->sender_mac, net_config.mac_address, ETH_ADDR_LEN);
    arp->sender_ip = htonl(net_config.ip_address);
    memset(arp->target_mac, 0, ETH_ADDR_LEN);
    arp->target_ip = htonl(target_ip);
    
    char ip_str[16];
    format_ipv4(target_ip, ip_str);
    printf("[ARP] Sending request for %s\n", ip_str);
    
    e1000_send_packet(packet, sizeof(packet));
}

void arp_send_reply(uint32_t target_ip, uint8_t *target_mac) {
    uint8_t packet[sizeof(eth_header_t) + sizeof(arp_packet_t)];
    eth_header_t *eth = (eth_header_t *)packet;
    arp_packet_t *arp = (arp_packet_t *)(packet + sizeof(eth_header_t));
    
    // Ethernet header
    memcpy(eth->dst_mac, target_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_ARP);
    
    // ARP packet
    arp->hardware_type = htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type = htons(ARP_PROTOCOL_IPV4);
    arp->hardware_addr_len = ETH_ADDR_LEN;
    arp->protocol_addr_len = 4;
    arp->operation = htons(ARP_REPLY);
    
    memcpy(arp->sender_mac, net_config.mac_address, ETH_ADDR_LEN);
    arp->sender_ip = htonl(net_config.ip_address);
    memcpy(arp->target_mac, target_mac, ETH_ADDR_LEN);
    arp->target_ip = htonl(target_ip);
    
    char ip_str[16];
    format_ipv4(target_ip, ip_str);
    printf("[ARP] Sending reply to %s\n", ip_str);
    
    e1000_send_packet(packet, sizeof(packet));
}

static void handle_arp_packet(uint8_t *packet, uint16_t length) {
    if (length < sizeof(arp_packet_t)) {
        return;
    }
    
    arp_packet_t *arp = (arp_packet_t *)packet;
    uint16_t operation = ntohs(arp->operation);
    uint32_t sender_ip = ntohl(arp->sender_ip);
    uint32_t target_ip = ntohl(arp->target_ip);
    
    // Add sender to ARP cache
    arp_add_entry(sender_ip, arp->sender_mac);
    
    if (operation == ARP_REQUEST && target_ip == net_config.ip_address) {
        // Someone is asking for our MAC address
        char ip_str[16];
        format_ipv4(sender_ip, ip_str);
        printf("[ARP] Request from %s - sending reply\n", ip_str);
        arp_send_reply(sender_ip, arp->sender_mac);
    } else if (operation == ARP_REPLY) {
        char ip_str[16];
        format_ipv4(sender_ip, ip_str);
        printf("[ARP] Reply received from %s\n", ip_str);
    }
}

// =============================================================================
// ICMP PROTOCOL
// =============================================================================

void icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq, uint8_t *data, uint16_t data_len) {
    uint16_t total_len = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len;
    uint8_t packet[total_len];
    
    eth_header_t *eth = (eth_header_t *)packet;
    ip_header_t *ip = (ip_header_t *)(packet + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t *)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
    uint8_t *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t);
    
    // Lookup destination MAC
    uint8_t dst_mac[ETH_ADDR_LEN];
    if (!arp_lookup(dst_ip, dst_mac)) {
        printf("[ICMP] No ARP entry for destination, sending ARP request\n");
        arp_send_request(dst_ip);
        return;
    }
    
    // Ethernet header
    memcpy(eth->dst_mac, dst_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_IPV4);
    
    // IP header
    ip->version_ihl = 0x45;  // Version 4, IHL 5 (20 bytes)
    ip->tos = 0;
    ip->total_length = htons(sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len);
    ip->identification = htons(ip_identification++);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_ICMP;
    ip->src_ip = htonl(net_config.ip_address);
    ip->dst_ip = htonl(dst_ip);
    ip->header_checksum = 0;
    ip->header_checksum = ip_checksum(ip, sizeof(ip_header_t));
    
    // ICMP header
    icmp->type = ICMP_ECHO_REPLY;
    icmp->code = 0;
    icmp->identifier = id;
    icmp->sequence = seq;
    icmp->checksum = 0;
    
    // Copy payload
    if (data && data_len > 0) {
        memcpy(payload, data, data_len);
    }
    
    // Calculate ICMP checksum (header + data)
    icmp->checksum = ip_checksum(icmp, sizeof(icmp_header_t) + data_len);
    
    printf("[ICMP] Sending echo reply (id=%d, seq=%d)\n", ntohs(id), ntohs(seq));
    e1000_send_packet(packet, total_len);
}

static void handle_icmp_packet(uint8_t *packet, uint16_t length, uint32_t src_ip) {
    if (length < sizeof(icmp_header_t)) {
        return;
    }
    
    icmp_header_t *icmp = (icmp_header_t *)packet;
    uint8_t *data = packet + sizeof(icmp_header_t);
    uint16_t data_len = length - sizeof(icmp_header_t);
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        char ip_str[16];
        format_ipv4(src_ip, ip_str);
        printf("[ICMP] Echo request from %s (id=%d, seq=%d)\n", 
               ip_str, ntohs(icmp->identifier), ntohs(icmp->sequence));
        
        // Send echo reply
        icmp_send_echo_reply(src_ip, icmp->identifier, icmp->sequence, data, data_len);
    }
}

// =============================================================================
// IP PACKET PROCESSING
// =============================================================================

static void handle_ip_packet(uint8_t *packet, uint16_t length) {
    if (length < sizeof(ip_header_t)) {
        return;
    }
    
    ip_header_t *ip = (ip_header_t *)packet;
    
    // Verify checksum
    uint16_t received_checksum = ip->header_checksum;
    ip->header_checksum = 0;
    uint16_t calculated_checksum = ip_checksum(ip, sizeof(ip_header_t));
    
    if (received_checksum != calculated_checksum) {
        printf("[IP] Checksum mismatch, dropping packet\n");
        return;
    }
    
    ip->header_checksum = received_checksum;
    
    uint32_t src_ip = ntohl(ip->src_ip);
    uint32_t dst_ip = ntohl(ip->dst_ip);
    
    // Check if packet is for us
    if (dst_ip != net_config.ip_address && dst_ip != 0xFFFFFFFF) {
        return;  // Not for us
    }
    
    uint8_t *payload = packet + IP_HEADER_LEN(ip);
    uint16_t payload_len = ntohs(ip->total_length) - IP_HEADER_LEN(ip);
    
    switch (ip->protocol) {
        case IP_PROTOCOL_ICMP:
            handle_icmp_packet(payload, payload_len, src_ip);
            break;
            
        case IP_PROTOCOL_UDP:
            printf("[IP] UDP packet received (not yet implemented)\n");
            break;
            
        case IP_PROTOCOL_TCP:
            printf("[IP] TCP packet received (not yet implemented)\n");
            break;
            
        default:
            printf("[IP] Unknown protocol: %d\n", ip->protocol);
            break;
    }
}

// =============================================================================
// MAIN PACKET PROCESSOR
// =============================================================================

void netstack_process_packet(uint8_t *packet, uint16_t length) {
    if (length < sizeof(eth_header_t)) {
        return;
    }
    
    eth_header_t *eth = (eth_header_t *)packet;
    uint16_t ethertype = ntohs(eth->ethertype);
    uint8_t *payload = packet + sizeof(eth_header_t);
    uint16_t payload_len = length - sizeof(eth_header_t);
    
    switch (ethertype) {
        case ETHERTYPE_ARP:
            handle_arp_packet(payload, payload_len);
            break;
            
        case ETHERTYPE_IPV4:
            handle_ip_packet(payload, payload_len);
            break;
            
        default:
            printf("[ETH] Unknown EtherType: 0x%04X\n", ethertype);
            break;
    }
}

// =============================================================================
// STUB FUNCTIONS (TO BE IMPLEMENTED)
// =============================================================================

#include "drivers/net/ne2000.h"
#include "drivers/net/e1000.h"

void icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq) {
    uint8_t data[32] = "ping"; // Simple payload
    uint16_t data_len = 4;

    uint16_t total_len = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len;
    uint8_t packet[1514] = {0};

    eth_header_t *eth = (eth_header_t *)packet;
    ip_header_t *ip = (ip_header_t *)(packet + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t *)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
    uint8_t *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t);

    // Lookup destination MAC
    uint8_t dst_mac[ETH_ADDR_LEN];
    if (!arp_lookup(dst_ip, dst_mac)) {
        printf("[ICMP] No ARP entry for destination, sending ARP request\n");
        arp_send_request(dst_ip);
        return;
    }

    // Ethernet header
    memcpy(eth->dst_mac, dst_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_IPV4);

    // IP header
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_length = htons(sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len);
    ip->identification = htons(ip_identification++);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_ICMP;
    ip->src_ip = htonl(net_config.ip_address);
    ip->dst_ip = htonl(dst_ip);
    ip->header_checksum = 0;
    ip->header_checksum = ip_checksum(ip, sizeof(ip_header_t));

    // ICMP header
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->identifier = id;
    icmp->sequence = seq;
    icmp->checksum = 0;

    memcpy(payload, data, data_len);

    icmp->checksum = ip_checksum(icmp, sizeof(icmp_header_t) + data_len);

    printf("[ICMP] Sending echo request (id=%d, seq=%d)\n", ntohs(id), ntohs(seq));

    // Use correct NIC send function
    if (ne2000_is_initialized()) {
        ne2000_send_packet(packet, total_len);
    } else if (e1000_is_initialized()) {
        e1000_send_packet(packet, total_len);
    } else {
        printf("[ICMP] No network card initialized for sending\n");
    }
}

int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t *data, uint16_t length) {
    printf("[UDP] Send - not yet implemented\n");
    return -1;
}

void udp_bind(uint16_t port, udp_callback_t callback) {
    printf("[UDP] Bind - not yet implemented\n");
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    printf("[TCP] Connect - not yet implemented\n");
    return -1;
}

int tcp_send(int socket, uint8_t *data, uint16_t length) {
    printf("[TCP] Send - not yet implemented\n");
    return -1;
}

int tcp_recv(int socket, uint8_t *buffer, uint16_t max_length) {
    printf("[TCP] Recv - not yet implemented\n");
    return -1;
}

void tcp_close(int socket) {
    printf("[TCP] Close - not yet implemented\n");
}
