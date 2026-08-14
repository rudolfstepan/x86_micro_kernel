// drivers/net/netstack.c
// NE2000-focused improved stack (header-aligned)

#include "drivers/net/netstack.h"
#include "drivers/net/netdev.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "kernel/time/pit.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

// =============================================================================
// GLOBAL STATE
// =============================================================================
static network_config_t net_config;
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static uint16_t ip_identification = 0;

typedef struct {
    uint32_t rx_arp;
    uint32_t rx_ipv4;
    uint32_t rx_dropped;
    uint32_t arp_cache_updates;
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

static bool udp_checksum_valid(const ip_header_t* ip, const udp_header_t* udp,
                               const uint8_t* payload, size_t len) {
    if (udp->checksum == 0) return true;
    struct pseudo {
        uint32_t src, dst;
        uint8_t zero;
        uint8_t proto;
        uint16_t udp_len;
    } __attribute__((packed)) pseudo = {
        ip->src_ip, ip->dst_ip, 0, IP_PROTOCOL_UDP,
        htons((uint16_t)(sizeof(*udp) + len))
    };
    uint32_t sum = checksum_accumulate(&pseudo, sizeof(pseudo), 0);
    sum = checksum_accumulate(udp, sizeof(*udp), sum);
    sum = checksum_accumulate(payload, len, sum);
    return fold_checksum(sum) == 0;
}

// =============================================================================
// NIC wrapper (selected initialized backend)
// =============================================================================
static inline bool nic_send(uint8_t *p, size_t n) {
    return netdev_send(p, n);
}
static inline int nic_recv(uint8_t *buf, size_t cap) {
    return netdev_receive(buf, cap);
}

static uint32_t netstack_next_hop(uint32_t dst_ip) {
    if (dst_ip == 0xFFFFFFFFu) return dst_ip;
    if ((dst_ip & net_config.netmask) ==
        (net_config.ip_address & net_config.netmask)) {
        return dst_ip;
    }
    return net_config.gateway;
}

// =============================================================================
// ARP (öffentliche Signaturen aus Header)
// =============================================================================
void arp_add_entry(uint32_t ip, const uint8_t *mac) {
    if (!mac) return;
    int slot = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) { slot = i; break; }
        if (!arp_cache[i].valid && slot < 0) slot = i;
    }
    if (slot < 0) slot = 0;
    arp_cache[slot].ip = ip;
    memcpy(arp_cache[slot].mac, mac, ETH_ADDR_LEN);
    arp_cache[slot].valid = true;
    arp_cache[slot].timestamp = 0;
    ++netstack_stats.arp_cache_updates;
}

bool arp_lookup(uint32_t ip, uint8_t *mac_out) {
    if (!mac_out) return false;
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac_out, arp_cache[i].mac, ETH_ADDR_LEN);
            return true;
        }
    }
    return false;
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

void arp_send_request(uint32_t target_ip) {
    (void)arp_send_request_now(target_ip);
}

bool netstack_probe_gateway(void) {
    return net_config.gateway != 0U &&
           arp_send_request_now(net_config.gateway);
}

void arp_send_reply(uint32_t target_ip, uint8_t *target_mac) {
    if (!target_mac) return;
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

    nic_send(packet, sizeof(packet));
}

static void handle_arp_packet(uint8_t *packet, uint16_t length) {
    if (length < sizeof(arp_packet_t)) {
        ++netstack_stats.rx_dropped;
        return;
    }
    arp_packet_t *arp = (arp_packet_t *)packet;

    if (ntohs(arp->hardware_type) != ARP_HARDWARE_ETHERNET ||
        ntohs(arp->protocol_type) != ARP_PROTOCOL_IPV4 ||
        arp->hardware_addr_len   != ETH_ADDR_LEN ||
        arp->protocol_addr_len   != 4) {
        ++netstack_stats.rx_dropped;
        return;
    }

    ++netstack_stats.rx_arp;

    uint16_t op   = ntohs(arp->operation);
    uint32_t sip  = ntohl(arp->sender_ip);
    uint32_t tip  = ntohl(arp->target_ip);

    arp_add_entry(sip, arp->sender_mac);

    if (op == ARP_REQUEST && tip == net_config.ip_address) {
        arp_send_reply(sip, arp->sender_mac);
    }
}

// =============================================================================
// IPv4/ICMP
// =============================================================================
void icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq, uint8_t *data, uint16_t data_len) {
    const uint16_t max_data = (uint16_t)(ETH_MAX_PAYLOAD - sizeof(ip_header_t) - sizeof(icmp_header_t));
    if (data_len > max_data || (data_len && !data)) {
        ++netstack_stats.rx_dropped;
        return;
    }
    uint8_t packet[1514] = {0};
    eth_header_t  *eth  = (eth_header_t *)packet;
    ip_header_t   *ip   = (ip_header_t  *)(packet + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t *)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
    uint8_t       *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t);

    uint32_t next_hop = netstack_next_hop(dst_ip);

    uint8_t dst_mac[ETH_ADDR_LEN];
    if (dst_ip == 0xFFFFFFFFu) {
        memset(dst_mac, 0xFF, ETH_ADDR_LEN);
    } else if (!arp_lookup(next_hop, dst_mac)) {
        arp_send_request(next_hop);
        return;
    }

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
    if (nic_send(packet, total_len)) ++netstack_stats.icmp_echo_replies_sent;
}

static void handle_icmp_packet(uint8_t *packet, uint16_t length, uint32_t src_ip) {
    if (length < sizeof(icmp_header_t)) {
        ++netstack_stats.rx_dropped;
        return;
    }
    if (ip_checksum(packet, length) != 0) {
        ++netstack_stats.rx_dropped;
        return;
    }
    icmp_header_t *icmp = (icmp_header_t *)packet;
    uint8_t *data = packet + sizeof(icmp_header_t);
    uint16_t dlen = length - sizeof(icmp_header_t);

    if (icmp->type == ICMP_ECHO_REQUEST) {
        ++netstack_stats.icmp_echo_requests;
        icmp_send_echo_reply(src_ip, ntohs(icmp->identifier), ntohs(icmp->sequence), data, dlen);
    } else if (icmp->type == ICMP_ECHO_REPLY && icmp->code == 0) {
        ++netstack_stats.icmp_echo_replies;
        if (ping_waiting && src_ip == ping_expected_ip &&
            ntohs(icmp->identifier) == ping_expected_id &&
            ntohs(icmp->sequence) == ping_expected_seq) {
            ping_reply_received = true;
        }
    }
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

static int netstack_receive_udp_low(uint16_t port, uint16_t expected_src_port,
                                    void *buffer, size_t buflen,
                                    uint32_t *src_ip, uint16_t *src_port,
                                    int poll_count) {
    uint8_t pkt[1514];
    for (int i = 0; i < poll_count; ++i) {
        netdev_poll();
        int len = nic_recv(pkt, sizeof(pkt));
        if (len <= 0) {
            pit_delay(1);
            continue;
        }
        if (len < 42) continue; // min eth+ip+udp

        uint16_t ethertype = (uint16_t)(pkt[12] << 8 | pkt[13]);
        if (ethertype != 0x0800) continue; // IPv4 only

        ip_header_t *ip = (ip_header_t *)(pkt + 14);
        int ihl_bytes = (IP_IHL(ip)) * 4;
        if (ihl_bytes < (int)sizeof(ip_header_t) || (14 + ihl_bytes) > len) continue;
        if (ip->protocol != IP_PROTOCOL_UDP) continue;

        if (IP_VERSION(ip) != 4 || ip_checksum(ip, (uint16_t)ihl_bytes) != 0) {
            continue;
        }

        uint16_t ip_total = ntohs(ip->total_length);
        if (ip_total < (uint16_t)ihl_bytes || ip_total > (uint16_t)(len - 14) ||
            ip_total < (uint16_t)(ihl_bytes + sizeof(udp_header_t))) continue;
        if ((ntohs(ip->flags_fragment) & 0x3FFFu) != 0) continue;

        udp_header_t *udp = (udp_header_t *)(pkt + 14 + ihl_bytes);
        if (ntohs(udp->dst_port) != port) continue;
        uint16_t actual_src_port = ntohs(udp->src_port);
        if (expected_src_port != 0 && actual_src_port != expected_src_port) continue;

        uint16_t udp_total = ntohs(udp->length);
        if (udp_total < sizeof(udp_header_t)) continue;
        int udp_pl = (int)udp_total - (int)sizeof(udp_header_t);
        int avail  = (int)ip_total - (ihl_bytes + (int)sizeof(udp_header_t));
        if (udp_pl < 0 || udp_pl > avail) continue;

        uint8_t *payload = pkt + 14 + ihl_bytes + sizeof(udp_header_t);
        if (!udp_checksum_valid(ip, udp, payload, (size_t)udp_pl)) continue;

        if (src_ip)   *src_ip   = ntohl(ip->src_ip);
        if (src_port) *src_port = actual_src_port;

        int copy = udp_pl < (int)buflen ? udp_pl : (int)buflen;
        memcpy(buffer, payload, (size_t)copy);
        return copy;
    }
    return -1;
}

// =============================================================================
// Minimaler DHCP-Client (DISCOVER->OFFER->REQUEST->ACK)
// =============================================================================
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_DISCOVER    1
#define DHCP_OFFER       2
#define DHCP_REQUEST     3
#define DHCP_ACK         5
#define DHCP_MAGIC_COOKIE 0x63825363u
#define DHCP_ATTEMPTS 3
#define DHCP_REPLY_TIMEOUT_MS 1500

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

static uint32_t rng32(void) {
    static uint32_t seed = 0x12345678u;
    seed = seed * 1664525u + 1013904223u;
    return seed;
}
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
static bool dhcp_parse_opts(const struct dhcp_packet *pkt, uint32_t *server_id_n, uint32_t *subnet_n, uint32_t *router_n, uint32_t *dns_n, uint8_t *msgtype) {
    const uint8_t *opt = pkt->options;
    uint32_t mc; memcpy(&mc, opt, 4); opt += 4;
    if (ntohl(mc) != DHCP_MAGIC_COOKIE) return false;
    while (opt < pkt->options + sizeof(pkt->options)) {
        uint8_t code = *opt++;
        if (code == DHO_END) break;
        if (code == 0) continue;
        if (opt >= pkt->options + sizeof(pkt->options)) break;
        uint8_t len = *opt++;
        if (opt + len > pkt->options + sizeof(pkt->options)) break;
        switch (code) {
            case DHO_MSG_TYPE: if (len>=1 && msgtype) *msgtype = opt[0]; break;
            case DHO_SERVER_ID: if (len==4 && server_id_n) memcpy(server_id_n, opt, 4); break;
            case DHO_SUBNET:    if (len==4 && subnet_n)    memcpy(subnet_n,    opt, 4); break;
            case DHO_ROUTER:    if (len>=4 && router_n)    memcpy(router_n,    opt, 4); break;
            case DHO_DNS:       if (len>=4 && dns_n)       memcpy(dns_n,       opt, 4); break;
            default: break;
        }
        opt += len;
    }
    return true;
}

static bool dhcp_receive_message(uint32_t xid, uint8_t expected_type,
                                 struct dhcp_packet* packet,
                                 int poll_count) {
    for (int attempt = 0; attempt < poll_count; ++attempt) {
        memset(packet, 0, sizeof(*packet));
        int received = netstack_receive_udp_low(
            DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, sizeof(*packet),
            NULL, NULL, 1);
        if (received < (int)(offsetof(struct dhcp_packet, options) + 4u) ||
            packet->op != 2 || packet->htype != 1 || packet->hlen != 6 ||
            packet->xid != xid ||
            memcmp(packet->chaddr, net_config.mac_address, 6) != 0) {
            continue;
        }

        uint8_t message_type = 0;
        if (dhcp_parse_opts(packet, NULL, NULL, NULL, NULL, &message_type) &&
            message_type == expected_type) {
            return true;
        }
    }
    return false;
}

static bool dhcp_discover_request(uint32_t *out_ip, uint32_t *out_subnet, uint32_t *out_router, uint32_t *out_dns) {
    netdev_reset_rx();
    struct dhcp_packet pkt; memset(&pkt, 0, sizeof(pkt));
    pkt.op    = 1; pkt.htype = 1; pkt.hlen = 6; pkt.hops = 0;
    pkt.xid   = rng32();
    pkt.secs  = 0;
    pkt.flags = htons(0x8000); // Broadcast-Antwort erwünscht
    memcpy(pkt.chaddr, net_config.mac_address, 6);

    uint8_t *opt = pkt.options;
    uint32_t mc = htonl(DHCP_MAGIC_COOKIE);
    memcpy(opt, &mc, 4); opt += 4;
    opt = dhcp_opt_put_u8(opt, DHO_MSG_TYPE, DHCP_DISCOVER);
    const uint8_t req[] = { DHO_SUBNET, DHO_ROUTER, DHO_DNS, DHO_LEASE_TIME, DHO_SERVER_ID };
    opt = dhcp_opt_put_list(opt, DHO_PARAM_REQ, req, sizeof(req));
    *opt++ = DHO_END;

    printf("[DHCP] DISCOVER xid=0x%08x\n", (unsigned)pkt.xid);
    if (netstack_send_udp_low(0xFFFFFFFFu, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &pkt, sizeof(pkt), false) != 0) {
        printf("[DHCP] send DISCOVER failed\n");
        return false;
    }

    struct dhcp_packet offer;
    if (!dhcp_receive_message(pkt.xid, DHCP_OFFER, &offer,
                              DHCP_REPLY_TIMEOUT_MS)) {
        printf("[DHCP] no valid OFFER\n"); return false;
    }

    uint8_t mtype = 0; uint32_t sid_n=0, mask_n=0, gw_n=0, dns_n=0;
    if (!dhcp_parse_opts(&offer, &sid_n, &mask_n, &gw_n, &dns_n, &mtype) || mtype != DHCP_OFFER) {
        printf("[DHCP] invalid OFFER/options\n"); return false;
    }
    uint32_t yi = offer.yiaddr; // network order
    printf("[DHCP] OFFER yiaddr=%u.%u.%u.%u\n", ((uint8_t*)&yi)[0],((uint8_t*)&yi)[1],((uint8_t*)&yi)[2],((uint8_t*)&yi)[3]);

    struct dhcp_packet reqpkt; memset(&reqpkt, 0, sizeof(reqpkt));
    reqpkt.op=1; reqpkt.htype=1; reqpkt.hlen=6; reqpkt.xid=pkt.xid; reqpkt.flags=htons(0x8000);
    memcpy(reqpkt.chaddr, net_config.mac_address, 6);
    opt = reqpkt.options;
    memcpy(opt, &mc, 4); opt += 4;
    opt = dhcp_opt_put_u8 (opt, DHO_MSG_TYPE, DHCP_REQUEST);
    {
        uint32_t yi_h = ntohl(yi);
        uint32_t sid_h= ntohl(sid_n);
        opt = dhcp_opt_put_u32(opt, DHO_REQ_IP, yi_h);
        opt = dhcp_opt_put_u32(opt, DHO_SERVER_ID, sid_h);
    }
    *opt++ = DHO_END;

    printf("[DHCP] REQUEST for offered IP\n");
    if (netstack_send_udp_low(0xFFFFFFFFu, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &reqpkt, sizeof(reqpkt), false) != 0) {
        printf("[DHCP] send REQUEST failed\n");
        return false;
    }

    struct dhcp_packet ack;
    if (!dhcp_receive_message(pkt.xid, DHCP_ACK, &ack,
                              DHCP_REPLY_TIMEOUT_MS)) {
        printf("[DHCP] no valid ACK\n"); return false;
    }
    mtype = 0; sid_n=mask_n=gw_n=dns_n=0;
    if (!dhcp_parse_opts(&ack, &sid_n, &mask_n, &gw_n, &dns_n, &mtype) || mtype != DHCP_ACK) {
        printf("[DHCP] not ACK\n"); return false;
    }

    *out_ip     = ntohl(ack.yiaddr);
    *out_subnet = ntohl(mask_n);
    *out_router = ntohl(gw_n);
    *out_dns    = ntohl(dns_n);
    return true;
}

// =============================================================================
// IP/ETH Demux
// =============================================================================
static void handle_ip_packet(uint8_t *packet, uint16_t length,
                             const uint8_t source_mac[ETH_ADDR_LEN]) {
    if (length < sizeof(ip_header_t)) {
        ++netstack_stats.rx_dropped;
        return;
    }
    ip_header_t *ip = (ip_header_t *)packet;

    if (IP_VERSION(ip) != 4) {
        ++netstack_stats.rx_dropped;
        return;
    }
    int ihl_bytes = (IP_IHL(ip)) * 4;
    if (ihl_bytes < (int)sizeof(ip_header_t) || ihl_bytes > (int)length) {
        ++netstack_stats.rx_dropped;
        return;
    }

    if (ip_checksum(ip, (uint16_t)ihl_bytes) != 0) {
        ++netstack_stats.rx_dropped;
        return;
    }

    uint16_t total_length = ntohs(ip->total_length);
    if (total_length < (uint16_t)ihl_bytes || total_length > length) {
        ++netstack_stats.rx_dropped;
        return;
    }

    uint32_t dst = ntohl(ip->dst_ip);
    if (dst != net_config.ip_address && dst != 0xFFFFFFFFu) return;

    uint16_t ff = ntohs(ip->flags_fragment);
    if (ff & 0x3FFF) {
        ++netstack_stats.rx_dropped;
        return;
    }

    ++netstack_stats.rx_ipv4;

    /* Learn the directly reachable sender. For off-subnet packets the frame
     * came from the configured gateway, so cache that next-hop MAC instead. */
    uint32_t source_ip = ntohl(ip->src_ip);
    uint32_t source_next_hop = netstack_next_hop(source_ip);
    if (source_next_hop != 0 && source_mac) {
        arp_add_entry(source_next_hop, source_mac);
    }

    uint8_t *payload = (uint8_t*)ip + ihl_bytes;
    uint16_t payload_len = (uint16_t)(total_length - ihl_bytes);

    switch (ip->protocol) {
        case IP_PROTOCOL_ICMP:
            handle_icmp_packet(payload, payload_len, source_ip);
            break;
        case IP_PROTOCOL_UDP:
            // UDP wird über netstack_receive_udp_low konsumiert
            break;
        default:
            break;
    }
}

void netstack_process_packet(uint8_t *packet, uint16_t length) {
    if (!packet || length < sizeof(eth_header_t)) {
        ++netstack_stats.rx_dropped;
        return;
    }
    eth_header_t *eth = (eth_header_t *)packet;
    uint16_t type = ntohs(eth->ethertype);
    uint8_t *payload = packet + sizeof(eth_header_t);
    uint16_t plen = length - sizeof(eth_header_t);

    // nur für uns / Broadcast
    bool is_bcast = true;
    for (int i=0;i<6;++i) if (eth->dst_mac[i] != 0xFF) { is_bcast=false; break; }
    if (!is_bcast && memcmp(eth->dst_mac, net_config.mac_address, ETH_ADDR_LEN)!=0) {
        return;
    }

    switch (type) {
        case ETHERTYPE_ARP:  handle_arp_packet(payload, plen); break;
        case ETHERTYPE_IPV4:
            handle_ip_packet(payload, plen, eth->src_mac);
            break;
        default: break;
    }
}

// =============================================================================
// Öffentliche API
// =============================================================================
void netstack_init(void) {
    printf("[NET] init...\n");
    memset(&netstack_stats, 0, sizeof(netstack_stats));
    ping_waiting = false;
    ping_reply_received = false;
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) arp_cache[i].valid = false;

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

void netstack_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    net_config.ip_address = ip;
    net_config.netmask    = netmask;
    net_config.gateway    = gateway;
    net_config.dns_server = 0;
    char ip_s[16]; format_ipv4(ip, ip_s);
    printf("[NET] IP configured: %s\n", ip_s);
}

bool netstack_configure_dhcp(void) {
    net_config.ip_address = 0;
    net_config.netmask = 0;
    net_config.gateway = 0;
    net_config.dns_server = 0;

    for (unsigned int attempt = 1; attempt <= DHCP_ATTEMPTS; ++attempt) {
        printf("[DHCP] attempt %u/%u\n", attempt, DHCP_ATTEMPTS);
        uint32_t ip=0, mask=0, gw=0, dns=0;
        if (dhcp_discover_request(&ip, &mask, &gw, &dns)) {
            net_config.ip_address = ip;
            net_config.netmask    = mask;
            net_config.gateway    = gw;
            net_config.dns_server = dns;
            char ip_s[16], m_s[16], gw_s[16], dns_s[16];
            format_ipv4(ip, ip_s); format_ipv4(mask, m_s); format_ipv4(gw, gw_s); format_ipv4(dns, dns_s);
            printf("[DHCP] ACK IP=%s MASK=%s GW=%s DNS=%s\n", ip_s, m_s, gw_s, dns_s);
            return true;
        }
    }

    printf("[DHCP] failed after %u attempts; no IP\n", DHCP_ATTEMPTS);
    return false;
}

uint32_t netstack_get_ip_address(void) {
    if (net_config.ip_address == 0) {
        (void)netstack_configure_dhcp();
    }
    return net_config.ip_address;
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
    printf("Network receive statistics:\n");
    printf("  ARP=%u IPv4=%u dropped=%u ARP-cache-updates=%u\n",
           netstack_stats.rx_arp, netstack_stats.rx_ipv4,
           netstack_stats.rx_dropped, netstack_stats.arp_cache_updates);
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

    for (uint32_t elapsed = 0; elapsed < timeout_ms; ++elapsed) {
        if ((elapsed % 500u) == 0) arp_send_request(ip);
        netdev_poll();
        if (arp_lookup(ip, mac)) return true;
        pit_delay(1);
    }
    return false;
}

bool netstack_ping(uint32_t dst_ip, uint16_t id, uint16_t seq,
                   uint32_t timeout_ms) {
    if (dst_ip == 0 || timeout_ms == 0 ||
        netstack_get_ip_address() == 0) return false;

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

    for (uint32_t elapsed = 0; elapsed < timeout_ms; ++elapsed) {
        netdev_poll();
        if (ping_reply_received) {
            ping_waiting = false;
            return true;
        }
        pit_delay(1);
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
