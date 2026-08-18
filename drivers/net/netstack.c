/**
 * @file drivers/net/netstack.c
 * @brief Minimaler Ethernet-, ARP- und IPv4-Sendepfad.
 *
 * Layer: Ring-0 network driver and mediation.
 * Contract: Frames und Gerätezustand werden vor Veröffentlichung vollständig validiert.
 * Safety: Längen, Checksummen und Nachbarautorität werden vor NIC-Ausgabe geprüft.
 */
// drivers/net/netstack.c
// NE2000-focused improved stack (header-aligned)

#include "drivers/net/netstack.h"
#include "include/kernel/supervisor.h"
#include "drivers/net/netdev.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "include/kernel/arp_binding_cache.h"
#include "include/kernel/panic.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

// =============================================================================
// GLOBAL STATE
// =============================================================================
static network_config_t net_config;
static supervised_arp_cache_t supervised_arp_cache;
static bool supervised_arp_cache_initialized;
static uint16_t ip_identification = 0;
static uint32_t dhcp_runtime_transaction_id;

static uint64_t netstack_deadline_after(uint64_t now_ms,
                                        uint32_t timeout_ms) {
    return UINT64_MAX - now_ms < timeout_ms
        ? UINT64_MAX : now_ms + timeout_ms;
}

static void netstack_wait_one_ms(void) {
    if (scheduler_sleep_ms(1U) != 0) (void)scheduler_yield();
}

typedef struct {
    uint32_t icmp_echo_requests;
    uint32_t icmp_echo_replies;
    uint32_t icmp_echo_replies_sent;
} netstack_stats_t;

static netstack_stats_t netstack_stats;
static bool ping_waiting;
static bool ping_reply_received;
static uint32_t ping_expected_ip;
static uint16_t ping_expected_id;
static uint16_t ping_expected_seq;

// =============================================================================
// Byte order: nur Deklarationen verwenden (Implementierung z.B. in ethernet.c)
// =============================================================================
extern uint16_t htons(uint16_t host_short);
extern uint16_t ntohs(uint16_t net_short);
extern uint32_t htonl(uint32_t host_long);
extern uint32_t ntohl(uint32_t net_long);

// =============================================================================
// IPv4-Parser (von command.c genutzt)
// =============================================================================
uint32_t parse_ipv4(const char *ip) {
    if (!ip) return 0;
    uint32_t out = 0;
    int part = -1;  // -1 = noch nicht gestartet, sonst 0..255
    int dots = 0;

    for (const char *p = ip;; ++p) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            int d = c - '0';
            if (part < 0) part = d;
            else {
                part = part * 10 + d;
                if (part > 255) return 0; // invalid
            }
        } else if (c == '.' || c == '\0') {
            if (part < 0) return 0; // leeres Segment
            out = (out << 8) | (uint32_t)part;
            part = -1;
            if (c == '.') {
                if (++dots > 3) return 0; // zu viele Punkte
            } else { // '\0'
                break;
            }
        } else {
            return 0; // ungültiges Zeichen
        }
    }
    if (dots != 3) return 0;
    return out; // Host-Order: a.b.c.d -> 0xAABBCCDD
}

// =============================================================================
/* Hilfsfunktionen zur Darstellung */
// =============================================================================
#include <stdint.h>

// Hilfsfunktion: 0..255 als Dezimal ohne sprintf ausgeben
static inline char* u8_to_dec(uint8_t v, char* out) {
    if (v >= 100) {
        *out++ = (char)('0' + v / 100);
        v %= 100;
        *out++ = (char)('0' + v / 10);
        *out++ = (char)('0' + v % 10);
    } else if (v >= 10) {
        *out++ = (char)('0' + v / 10);
        *out++ = (char)('0' + v % 10);
    } else {
        *out++ = (char)('0' + v);
    }
    return out;
}

void format_ipv4(uint32_t ip, char *buffer) {
    // ip ist Host-Order: A.B.C.D = (ip>>24).(ip>>16&0xFF).(ip>>8&0xFF).(ip&0xFF)
    uint8_t a = (uint8_t)((ip >> 24) & 0xFF);
    uint8_t b = (uint8_t)((ip >> 16) & 0xFF);
    uint8_t c = (uint8_t)((ip >>  8) & 0xFF);
    uint8_t d = (uint8_t)( ip        & 0xFF);

    char *p = buffer;
    p = u8_to_dec(a, p); *p++ = '.';
    p = u8_to_dec(b, p); *p++ = '.';
    p = u8_to_dec(c, p); *p++ = '.';
    p = u8_to_dec(d, p);
    *p = '\0';
}


void format_mac(uint8_t *mac, char *buffer) {
    static const char hex[] = "0123456789ABCDEF";
    int pos = 0;
    for (int i = 0; i < 6; ++i) {
        buffer[pos++] = hex[(mac[i] >> 4) & 0xF];
        buffer[pos++] = hex[(mac[i]     ) & 0xF];
        if (i < 5) buffer[pos++] = ':';
    }
    buffer[pos] = 0;
}

// =============================================================================
// Checksummen
// =============================================================================
static uint32_t checksum_accumulate(const void *data, size_t len, uint32_t sum) {
    const uint8_t *p = (const uint8_t*)data;
    while (len > 1) {
        uint16_t w = ((uint16_t)p[0] << 8) | p[1];
        sum += w;
        p += 2; len -= 2;
    }
    if (len > 0) {
        // letztes Byte als High-Byte addieren
        sum += ((uint16_t)p[0]) << 8;
    }
    return sum;
}
static uint16_t fold_checksum(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}
uint16_t ip_checksum(void *data, uint16_t length) {
    return fold_checksum(checksum_accumulate(data, length, 0));
}

static uint16_t udp_checksum(const ip_header_t* ip, const udp_header_t* udp, const uint8_t* payload, size_t len) {
    struct pseudo {
        uint32_t src, dst;
        uint8_t  zero;
        uint8_t  proto;
        uint16_t udp_len;
    } __attribute__((packed)) pseudo = {
        ip->src_ip, ip->dst_ip, 0, IP_PROTOCOL_UDP, htons((uint16_t)(sizeof(*udp)+len))
    };
    uint32_t sum = 0;
    sum = checksum_accumulate(&pseudo, sizeof(pseudo), sum);
    sum = checksum_accumulate(udp, sizeof(*udp), sum);
    sum = checksum_accumulate(payload, len, sum);
    uint16_t checksum = fold_checksum(sum);
    /* In UDP/IPv4 a computed zero is encoded as all ones; zero means that no
     * checksum was supplied. */
    return checksum ? checksum : 0xFFFFu;
}

// =============================================================================
// NIC wrapper (selected initialized backend)
// =============================================================================
static inline bool nic_send(uint8_t *p, size_t n) {
    return netdev_send(p, n);
}

static uint32_t netstack_next_hop(uint32_t dst_ip) {
    if (dst_ip == 0xFFFFFFFFu) return dst_ip;
    if ((dst_ip & net_config.netmask) ==
        (net_config.ip_address & net_config.netmask)) {
        return dst_ip;
    }
    return net_config.gateway;
}

bool arp_lookup(uint32_t ip, uint8_t *mac_out) {
    if (!mac_out) return false;
    supervised_arp_lookup_result_t protected_result =
        supervised_arp_cache_lookup(&supervised_arp_cache, ip,
                                    pit_monotonic_ms(), mac_out);
    return protected_result == SUPERVISED_ARP_HIT;
}

static bool arp_revoke_route_bindings(uint32_t old_gateway,
                                      uint32_t new_gateway) {
    if (old_gateway != 0U &&
        supervised_arp_cache_revoke_ip(&supervised_arp_cache,
                                       old_gateway) < 0) return false;
    return new_gateway == 0U || new_gateway == old_gateway ||
           supervised_arp_cache_revoke_ip(&supervised_arp_cache,
                                          new_gateway) >= 0;
}

static bool arp_send_request_now(uint32_t target_ip) {
    uint8_t packet[sizeof(eth_header_t) + sizeof(arp_packet_t)];
    eth_header_t *eth = (eth_header_t *)packet;
    arp_packet_t *arp = (arp_packet_t *)(packet + sizeof(eth_header_t));

    // Ethernet header (Broadcast)
    memset(eth->dst_mac, 0xFF, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_ARP);

    // ARP packet
    arp->hardware_type     = htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type     = htons(ARP_PROTOCOL_IPV4);
    arp->hardware_addr_len = ETH_ADDR_LEN;
    arp->protocol_addr_len = 4;
    arp->operation         = htons(ARP_REQUEST);

    memcpy(arp->sender_mac, net_config.mac_address, ETH_ADDR_LEN);
    uint32_t sip = net_config.ip_address ? net_config.ip_address : 0;
    arp->sender_ip = htonl(sip);
    memset(arp->target_mac, 0, ETH_ADDR_LEN);
    arp->target_ip = htonl(target_ip);

    return nic_send(packet, sizeof(packet));
}

bool netstack_commit_arp_binding(uint32_t ip, const uint8_t mac[6],
                                 uint32_t transaction_epoch,
                                 int32_t source_pid,
                                 uint32_t source_generation, uint64_t now_ms) {
    if (ip == 0U || mac == NULL || (mac[0] & 1U) != 0U) return false;
    bool nonzero = false;
    for (uint32_t index = 0U; index < ETH_ADDR_LEN; ++index)
        if (mac[index] != 0U) nonzero = true;
    if (!nonzero) return false;
    if (supervised_arp_cache_commit(&supervised_arp_cache, ip, mac,
                                    transaction_epoch, source_pid,
                                    source_generation,
                                    now_ms,
                                    SUPERVISED_ARP_LEASE_MS) != 0) return false;
    return true;
}

int netstack_revoke_arp_bindings(int32_t source_pid,
                                 uint32_t source_generation) {
    return supervised_arp_cache_revoke_identity(
        &supervised_arp_cache, source_pid, source_generation);
}

bool netstack_scrub_arp_bindings(uint64_t now_ms,
                                 uint32_t *newly_expired_out,
                                 uint32_t *corrected_out) {
    if (newly_expired_out == NULL || corrected_out == NULL) return false;
    supervised_arp_scrub_stats_t stats;
    if (supervised_arp_cache_scrub(&supervised_arp_cache, now_ms,
                                   &stats) != 0) return false;
    *newly_expired_out = stats.newly_expired;
    *corrected_out = stats.corrected;
    return true;
}

void arp_send_request(uint32_t target_ip) {
    (void)supervisor_network_request_arp_resolution(target_ip);
}

bool netstack_send_arp_request(uint32_t target_ip) {
    return arp_send_request_now(target_ip);
}

bool netstack_probe_gateway(void) {
    return net_config.gateway != 0U &&
           arp_send_request_now(net_config.gateway);
}

bool netstack_send_arp_reply(uint32_t target_ip,
                             const uint8_t target_mac[6]) {
    if (!target_mac || target_ip == 0U || net_config.ip_address == 0U)
        return false;
    uint8_t packet[sizeof(eth_header_t) + sizeof(arp_packet_t)];
    eth_header_t *eth = (eth_header_t *)packet;
    arp_packet_t *arp = (arp_packet_t *)(packet + sizeof(eth_header_t));

    memcpy(eth->dst_mac, target_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_ARP);

    arp->hardware_type     = htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type     = htons(ARP_PROTOCOL_IPV4);
    arp->hardware_addr_len = ETH_ADDR_LEN;
    arp->protocol_addr_len = 4;
    arp->operation         = htons(ARP_REPLY);

    memcpy(arp->sender_mac, net_config.mac_address, ETH_ADDR_LEN);
    arp->sender_ip = htonl(net_config.ip_address);
    memcpy(arp->target_mac, target_mac, ETH_ADDR_LEN);
    arp->target_ip = htonl(target_ip);

    return nic_send(packet, sizeof(packet));
}

// =============================================================================
// IPv4/ICMP
// =============================================================================
bool netstack_send_icmp_echo_reply(uint32_t dst_ip,
                                  const uint8_t dst_mac[6], uint16_t id,
                                  uint16_t seq, const uint8_t *data,
                                  uint16_t data_len) {
    if (dst_ip == 0U || dst_ip == 0xFFFFFFFFU || dst_mac == NULL ||
        data_len > SUPERVISOR_ICMP_ECHO_MAX_DATA ||
        (data_len != 0U && data == NULL) || (dst_mac[0] & 1U) != 0U)
        return false;
    uint8_t packet[1514] = {0};
    eth_header_t  *eth  = (eth_header_t *)packet;
    ip_header_t   *ip   = (ip_header_t  *)(packet + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t *)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
    uint8_t       *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t);

    memcpy(eth->dst_mac, dst_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_IPV4);

    ip->version_ihl      = 0x45;
    ip->tos              = 0;
    ip->total_length     = htons((uint16_t)(sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len));
    ip->identification   = htons(ip_identification++);
    ip->flags_fragment   = 0;
    ip->ttl              = 64;
    ip->protocol         = IP_PROTOCOL_ICMP;
    ip->src_ip           = htonl(net_config.ip_address);
    ip->dst_ip           = htonl(dst_ip);
    ip->header_checksum  = 0;
    ip->header_checksum  = htons(ip_checksum(ip, sizeof(ip_header_t)));

    icmp->type       = ICMP_ECHO_REPLY;
    icmp->code       = 0;
    icmp->identifier = htons(id);
    icmp->sequence   = htons(seq);
    icmp->checksum   = 0;

    if (data && data_len) memcpy(payload, data, data_len);
    icmp->checksum = htons(ip_checksum(icmp, (uint16_t)(sizeof(icmp_header_t) + data_len)));

    size_t total_len = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len;
    if (!nic_send(packet, total_len)) return false;
    ++netstack_stats.icmp_echo_replies_sent;
    return true;
}

bool netstack_send_supervised_udp_reply(uint32_t dst_ip,
                                        const uint8_t dst_mac[6],
                                        uint16_t source_port,
                                        uint16_t destination_port,
                                        const uint8_t *data,
                                        uint16_t data_len) {
    if (dst_ip == 0U || dst_ip == 0xFFFFFFFFU || dst_mac == NULL ||
        source_port < SUPERVISOR_UDP_BINDING_MIN_PORT ||
        destination_port == 0U ||
        data_len > SUPERVISOR_UDP_ECHO_MAX_DATA ||
        (data_len != 0U && data == NULL) || (dst_mac[0] & 1U) != 0U)
        return false;
    uint8_t packet[1514] = {0};
    eth_header_t *eth = (eth_header_t *)packet;
    ip_header_t *ip = (ip_header_t *)(packet + sizeof(eth_header_t));
    udp_header_t *udp = (udp_header_t *)(packet + sizeof(eth_header_t) +
                                         sizeof(ip_header_t));
    uint8_t *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) +
                       sizeof(udp_header_t);
    memcpy(eth->dst_mac, dst_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_IPV4);
    ip->version_ihl = 0x45;
    ip->total_length = htons((uint16_t)(sizeof(ip_header_t) +
                                        sizeof(udp_header_t) + data_len));
    ip->identification = htons(ip_identification++);
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_UDP;
    ip->src_ip = htonl(net_config.ip_address);
    ip->dst_ip = htonl(dst_ip);
    ip->header_checksum = htons(ip_checksum(ip, sizeof(ip_header_t)));
    udp->src_port = htons(source_port);
    udp->dst_port = htons(destination_port);
    udp->length = htons((uint16_t)(sizeof(udp_header_t) + data_len));
    if (data_len != 0U) memcpy(payload, data, data_len);
    udp->checksum = htons(udp_checksum(ip, udp, payload, data_len));
    size_t total_len = sizeof(eth_header_t) + sizeof(ip_header_t) +
                       sizeof(udp_header_t) + data_len;
    return nic_send(packet, total_len);
}

// =============================================================================
// UDP low-level (für DHCP ausreichend)
// =============================================================================
static int netstack_send_udp_low(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, void *data, size_t len, bool with_checksum) {
    const size_t max_udp_payload = ETH_MAX_PAYLOAD - sizeof(ip_header_t) - sizeof(udp_header_t);
    if ((len && !data) || len > max_udp_payload) return -1;

    uint8_t packet[1514] = {0};
    uint8_t *ptr = packet;

    uint32_t next_hop = netstack_next_hop(dst_ip);

    uint8_t dst_mac[6];
    if (dst_ip == 0xFFFFFFFFu) {
        memset(dst_mac, 0xFF, 6); // Broadcast
    } else if (!arp_lookup(next_hop, dst_mac)) {
        arp_send_request(next_hop);
        return -1;
    }

    // Ethernet
    memcpy(ptr, dst_mac, 6); ptr += 6;
    memcpy(ptr, net_config.mac_address, 6); ptr += 6;
    *(uint16_t*)ptr = htons(0x0800); ptr += 2; // IPv4

    // IPv4
    ip_header_t *ip = (ip_header_t*)ptr;
    ip->version_ihl      = 0x45;
    ip->tos              = 0;
    ip->total_length     = htons((uint16_t)(sizeof(ip_header_t) + sizeof(udp_header_t) + len));
    ip->identification   = htons(ip_identification++);
    ip->flags_fragment   = 0;
    ip->ttl              = 64;
    ip->protocol         = IP_PROTOCOL_UDP;
    ip->src_ip           = htonl(net_config.ip_address);
    ip->dst_ip           = htonl(dst_ip);
    ip->header_checksum  = 0;
    ip->header_checksum  = htons(ip_checksum(ip, sizeof(ip_header_t)));
    ptr += sizeof(ip_header_t);

    // UDP
    udp_header_t *udp = (udp_header_t*)ptr;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons((uint16_t)(sizeof(udp_header_t) + len));
    udp->checksum = 0; // IPv4: optional
    ptr += sizeof(udp_header_t);

    memcpy(ptr, data, len);
    if (with_checksum) {
        udp->checksum = htons(udp_checksum(ip, udp, (const uint8_t*)data, len));
    }
    ptr += len;

    size_t total_len = (size_t)(ptr - packet);
    return nic_send(packet, total_len) ? 0 : -1;
}

// =============================================================================
// Supervised DHCP transport (protocol decisions live in Ring 3)
// =============================================================================
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_DISCOVER    1
#define DHCP_REQUEST     3
#define DHCP_MAGIC_COOKIE 0x63825363u

#define DHO_MSG_TYPE   53
#define DHO_PARAM_REQ  55
#define DHO_SERVER_ID  54
#define DHO_REQ_IP     50
#define DHO_SUBNET     1
#define DHO_ROUTER     3
#define DHO_DNS        6
#define DHO_LEASE_TIME 51
#define DHO_END        255

struct dhcp_packet {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint8_t  options[312];
} __attribute__((packed));

static uint8_t* dhcp_opt_put_u8(uint8_t *opt, uint8_t code, uint8_t v) {
    *opt++ = code; *opt++ = 1; *opt++ = v; return opt;
}
static uint8_t* dhcp_opt_put_u32(uint8_t *opt, uint8_t code, uint32_t v_host) {
    *opt++ = code; *opt++ = 4;
    uint32_t v = htonl(v_host);
    memcpy(opt, &v, 4); opt += 4;
    return opt;
}
static uint8_t* dhcp_opt_put_list(uint8_t *opt, uint8_t code, const uint8_t *lst, uint8_t n) {
    *opt++ = code; *opt++ = n; memcpy(opt, lst, n); opt += n; return opt;
}
bool netstack_send_supervised_dhcp_discover(uint32_t transaction_id) {
    if (transaction_id == 0U || net_config.ip_address != 0U) return false;
    struct dhcp_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.op = 1U;
    packet.htype = 1U;
    packet.hlen = 6U;
    packet.xid = htonl(transaction_id);
    packet.flags = htons(0x8000U);
    memcpy(packet.chaddr, net_config.mac_address, 6U);

    uint8_t *option = packet.options;
    uint32_t cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(option, &cookie, sizeof(cookie));
    option += sizeof(cookie);
    option = dhcp_opt_put_u8(option, DHO_MSG_TYPE, DHCP_DISCOVER);
    const uint8_t requested[] = {
        DHO_SUBNET, DHO_ROUTER, DHO_DNS, DHO_LEASE_TIME, DHO_SERVER_ID
    };
    option = dhcp_opt_put_list(option, DHO_PARAM_REQ, requested,
                               sizeof(requested));
    *option = DHO_END;
    if (netstack_send_udp_low(
            UINT32_MAX, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
            &packet, sizeof(packet), false) != 0) return false;
    dhcp_runtime_transaction_id = transaction_id;
    return true;
}

bool netstack_send_supervised_dhcp_select(uint32_t transaction_id,
                                          uint32_t offered_ip,
                                          uint32_t server_id) {
    if (transaction_id == 0U || offered_ip == 0U ||
        offered_ip == UINT32_MAX || server_id == 0U ||
        server_id == UINT32_MAX || net_config.ip_address != 0U ||
        dhcp_runtime_transaction_id != transaction_id) return false;
    struct dhcp_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.op = 1U;
    packet.htype = 1U;
    packet.hlen = 6U;
    packet.xid = htonl(transaction_id);
    packet.flags = htons(0x8000U);
    memcpy(packet.chaddr, net_config.mac_address, 6U);

    uint8_t *option = packet.options;
    uint32_t cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(option, &cookie, sizeof(cookie));
    option += sizeof(cookie);
    option = dhcp_opt_put_u8(option, DHO_MSG_TYPE, DHCP_REQUEST);
    option = dhcp_opt_put_u32(option, DHO_REQ_IP, offered_ip);
    option = dhcp_opt_put_u32(option, DHO_SERVER_ID, server_id);
    *option = DHO_END;
    return netstack_send_udp_low(
        UINT32_MAX, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
        &packet, sizeof(packet), false) == 0;
}

bool netstack_send_supervised_dhcp_request(uint32_t transaction_id,
                                           uint32_t ip_address,
                                           bool rebind) {
    if (transaction_id == 0U || ip_address == 0U ||
        ip_address == UINT32_MAX || net_config.ip_address != ip_address)
        return false;
    struct dhcp_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.op = 1U;
    packet.htype = 1U;
    packet.hlen = 6U;
    packet.xid = transaction_id;
    packet.flags = rebind ? htons(0x8000U) : 0U;
    packet.ciaddr = htonl(ip_address);
    memcpy(packet.chaddr, net_config.mac_address, 6U);

    uint8_t *option = packet.options;
    uint32_t cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(option, &cookie, sizeof(cookie));
    option += sizeof(cookie);
    option = dhcp_opt_put_u8(option, DHO_MSG_TYPE, DHCP_REQUEST);
    const uint8_t requested[] = {
        DHO_SUBNET, DHO_ROUTER, DHO_DNS, DHO_LEASE_TIME, DHO_SERVER_ID
    };
    option = dhcp_opt_put_list(option, DHO_PARAM_REQ, requested,
                               sizeof(requested));
    *option = DHO_END;

    /* Both bounded phases use a broadcast transport in v1. This avoids an
     * unprotected server-address cache and lets policy still distinguish T1
     * renewal from T2 rebinding. One call emits exactly one frame. */
    if (netstack_send_udp_low(
            UINT32_MAX, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
            &packet, sizeof(packet), false) != 0) return false;
    dhcp_runtime_transaction_id = transaction_id;
    return true;
}

bool netstack_finish_supervised_dhcp_request(uint32_t transaction_id) {
    if (transaction_id == 0U ||
        dhcp_runtime_transaction_id != transaction_id) return false;
    dhcp_runtime_transaction_id = 0U;
    return true;
}

// =============================================================================
// Öffentliche API
// =============================================================================
bool netstack_safety_init(void) {
    if (supervised_arp_cache_initialized) return true;
    if (supervised_arp_cache_init(&supervised_arp_cache) != 0) return false;
    supervised_arp_cache_initialized = true;
    return true;
}

void netstack_record_validated_icmp_echo_request(void) {
    ++netstack_stats.icmp_echo_requests;
}

bool netstack_accept_validated_icmp_echo_reply(uint32_t source_ip,
                                               uint16_t identifier,
                                               uint16_t sequence) {
    ++netstack_stats.icmp_echo_replies;
    if (!ping_waiting || source_ip != ping_expected_ip ||
        identifier != ping_expected_id || sequence != ping_expected_seq)
        return false;
    ping_reply_received = true;
    return true;
}

void netstack_init(void) {
    printf("[NET] init...\n");
    memset(&netstack_stats, 0, sizeof(netstack_stats));
    ping_waiting = false;
    ping_reply_received = false;
    dhcp_runtime_transaction_id = 0U;
    if (!netstack_safety_init())
        panic("Unable to initialize protected ARP binding cache");

    if (!netdev_get_mac_address(net_config.mac_address)) {
        memset(net_config.mac_address, 0, ETH_ADDR_LEN);
    }

    net_config.ip_address = 0;
    net_config.netmask    = 0;
    net_config.gateway    = 0;
    net_config.dns_server = 0;

    char mac_s[18]; format_mac(net_config.mac_address, mac_s);
    printf("[NET] backend=%s MAC=%s\n", netdev_backend_name(), mac_s);
}

bool netstack_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    if (!arp_revoke_route_bindings(net_config.gateway, gateway)) return false;
    net_config.ip_address = ip;
    net_config.netmask    = netmask;
    net_config.gateway    = gateway;
    net_config.dns_server = 0;
    char ip_s[16]; format_ipv4(ip, ip_s);
    printf("[NET] IP configured: %s\n", ip_s);
    return true;
}

bool netstack_apply_supervised_dhcp(uint32_t ip, uint32_t netmask,
                                    uint32_t gateway, uint32_t dns_server) {
    if (ip == 0U || ip == 0xFFFFFFFFU || netmask == 0U ||
        netmask == 0xFFFFFFFFU || gateway == 0xFFFFFFFFU ||
        dns_server == 0xFFFFFFFFU) return false;
    uint32_t host_mask = ~netmask;
    uint32_t host = ip & host_mask;
    if ((host_mask & (host_mask + 1U)) != 0U || host == 0U ||
        host == host_mask) return false;
    if (gateway != 0U) {
        uint32_t gateway_host = gateway & host_mask;
        if ((gateway & netmask) != (ip & netmask) || gateway_host == 0U ||
            gateway_host == host_mask) return false;
    }
    if (!arp_revoke_route_bindings(net_config.gateway, gateway)) return false;
    net_config.ip_address = ip;
    net_config.netmask = netmask;
    net_config.gateway = gateway;
    net_config.dns_server = dns_server;
    char ip_s[16], m_s[16], gw_s[16], dns_s[16];
    format_ipv4(ip, ip_s);
    format_ipv4(netmask, m_s);
    format_ipv4(gateway, gw_s);
    format_ipv4(dns_server, dns_s);
    printf("[DHCP] MEDIATED IP=%s MASK=%s GW=%s DNS=%s\n",
           ip_s, m_s, gw_s, dns_s);
    return true;
}

bool netstack_clear_supervised_dhcp(uint32_t expected_ip) {
    if (expected_ip != 0U && net_config.ip_address != expected_ip)
        return false;
    uint32_t gateway = net_config.gateway;
    bool protected_cache_intact = gateway == 0U ||
        supervised_arp_cache_revoke_ip(&supervised_arp_cache, gateway) >= 0;
    net_config.ip_address = 0U;
    net_config.netmask = 0U;
    net_config.gateway = 0U;
    net_config.dns_server = 0U;
    dhcp_runtime_transaction_id = 0U;
    return protected_cache_intact;
}

uint32_t netstack_get_ip_address(void) {
    return net_config.ip_address;
}

uint32_t netstack_get_netmask(void) {
    return net_config.netmask;
}

bool netstack_is_configured(void) {
    return net_config.ip_address != 0U;
}

uint32_t netstack_get_gateway(void) {
    return net_config.gateway;
}

bool netstack_get_local_identity(uint32_t *ip_out, uint8_t mac_out[6]) {
    if (ip_out == NULL || mac_out == NULL || net_config.ip_address == 0U)
        return false;
    bool nonzero_mac = false;
    for (uint32_t index = 0U; index < ETH_ADDR_LEN; ++index) {
        mac_out[index] = net_config.mac_address[index];
        if (mac_out[index] != 0U) nonzero_mac = true;
    }
    *ip_out = net_config.ip_address;
    return nonzero_mac;
}

void netstack_debug_stats(void) {
    char ip_s[16], mask_s[16], gateway_s[16], dns_s[16];
    format_ipv4(net_config.ip_address, ip_s);
    format_ipv4(net_config.netmask, mask_s);
    format_ipv4(net_config.gateway, gateway_s);
    format_ipv4(net_config.dns_server, dns_s);
    printf("Network configuration:\n");
    printf("  IP=%s MASK=%s GW=%s DNS=%s\n",
           ip_s, mask_s, gateway_s, dns_s);
    printf("Validated network statistics:\n");
    printf("  ICMP requests=%u replies=%u replies-sent=%u\n",
           netstack_stats.icmp_echo_requests,
           netstack_stats.icmp_echo_replies,
           netstack_stats.icmp_echo_replies_sent);
}

static bool icmp_send_echo_request_now(uint32_t dst_ip, uint16_t id,
                                       uint16_t seq) {
    uint8_t data[4] = {'p','i','n','g'};
    uint8_t packet[1514] = {0};

    eth_header_t  *eth  = (eth_header_t *)packet;
    ip_header_t   *ip   = (ip_header_t  *)(packet + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t *)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
    uint8_t       *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t);

    uint32_t next_hop = netstack_next_hop(dst_ip);
    if (next_hop == 0) return false;
    uint8_t dst_mac[ETH_ADDR_LEN];
    if (dst_ip == 0xFFFFFFFFu) memset(dst_mac, 0xFF, ETH_ADDR_LEN);
    else if (!arp_lookup(next_hop, dst_mac)) return false;

    memcpy(eth->dst_mac, dst_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_IPV4);

    ip->version_ihl      = 0x45;
    ip->tos              = 0;
    ip->total_length     = htons((uint16_t)(sizeof(ip_header_t) + sizeof(icmp_header_t) + sizeof(data)));
    ip->identification   = htons(ip_identification++);
    ip->flags_fragment   = 0;
    ip->ttl              = 64;
    ip->protocol         = IP_PROTOCOL_ICMP;
    ip->src_ip           = htonl(net_config.ip_address);
    ip->dst_ip           = htonl(dst_ip);
    ip->header_checksum  = 0;
    ip->header_checksum  = htons(ip_checksum(ip, sizeof(ip_header_t)));

    icmp->type       = ICMP_ECHO_REQUEST;
    icmp->code       = 0;
    icmp->identifier = htons(id);
    icmp->sequence   = htons(seq);
    icmp->checksum   = 0;

    memcpy(payload, data, sizeof(data));
    icmp->checksum = htons(ip_checksum(icmp, (uint16_t)(sizeof(icmp_header_t)+sizeof(data))));

    size_t total_len = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t) + sizeof(data);
    return nic_send(packet, total_len);
}

void icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq) {
    (void)icmp_send_echo_request_now(dst_ip, id, seq);
}

static bool arp_resolve_with_timeout(uint32_t ip, uint8_t mac[ETH_ADDR_LEN],
                                     uint32_t timeout_ms) {
    if (ip == 0) return false;
    if (arp_lookup(ip, mac)) return true;

    uint64_t now_ms = pit_monotonic_ms();
    uint64_t deadline_ms = netstack_deadline_after(now_ms, timeout_ms);
    uint64_t next_request_ms = now_ms;
    while ((now_ms = pit_monotonic_ms()) < deadline_ms) {
        if (now_ms >= next_request_ms) {
            arp_send_request(ip);
            next_request_ms = netstack_deadline_after(now_ms, 500U);
        }
        netdev_poll();
        if (arp_lookup(ip, mac)) return true;
        netstack_wait_one_ms();
    }
    return false;
}

bool netstack_ping(uint32_t dst_ip, uint16_t id, uint16_t seq,
                   uint32_t timeout_ms) {
    uint32_t local_ip = netstack_get_ip_address();
    if (dst_ip == 0 || timeout_ms == 0 || local_ip == 0) return false;

    /* A host's own address is local by definition. Physical NICs and
     * switches are not required to reflect a frame back to its sender, so a
     * self-ping must not depend on ARP or external loopback behavior. */
    if (dst_ip == local_ip) return true;

    uint32_t next_hop = netstack_next_hop(dst_ip);
    uint8_t mac[ETH_ADDR_LEN];
    if (dst_ip != 0xFFFFFFFFu &&
        !arp_resolve_with_timeout(next_hop, mac, timeout_ms)) {
        return false;
    }

    ping_expected_ip = dst_ip;
    ping_expected_id = id;
    ping_expected_seq = seq;
    ping_reply_received = false;
    ping_waiting = true;

    if (!icmp_send_echo_request_now(dst_ip, id, seq)) {
        ping_waiting = false;
        return false;
    }

    uint64_t deadline_ms = netstack_deadline_after(
        pit_monotonic_ms(), timeout_ms);
    while (pit_monotonic_ms() < deadline_ms) {
        netdev_poll();
        if (ping_reply_received) {
            ping_waiting = false;
            return true;
        }
        netstack_wait_one_ms();
    }

    ping_waiting = false;
    return false;
}

// UDP-Send API (Header-Signatur: data non-const)
int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t *data, uint16_t length) {
    return netstack_send_udp_low(dst_ip, src_port, dst_port, data, length, /*with_checksum*/false);
}

void udp_bind(uint16_t port, udp_callback_t cb) {
    (void)port; (void)cb;
    printf("[UDP] bind not implemented\n");
}

// TCP Stubs
int  tcp_connect(uint32_t dst_ip, uint16_t dst_port){ (void)dst_ip;(void)dst_port; printf("[TCP] connect n/i\n"); return -1; }
int  tcp_send(int socket, uint8_t *data, uint16_t length){ (void)socket;(void)data;(void)length; printf("[TCP] send n/i\n"); return -1; }
int  tcp_recv(int socket, uint8_t *buffer, uint16_t max_length){ (void)socket;(void)buffer;(void)max_length; printf("[TCP] recv n/i\n"); return -1; }
void tcp_close(int socket){ (void)socket; printf("[TCP] close n/i\n"); }
