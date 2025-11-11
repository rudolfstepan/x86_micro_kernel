// netstack.c – NE2000-focused improved stack (header-aligned)
#include "drivers/net/netstack.h"
#include "drivers/net/ne2000.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>


// ==== IPv4 Parser (definition matching prototype in netstack.h) ====
// Robust: prüft exakt 4 Oktette, 0..255, keine leeren Teile.
uint32_t parse_ipv4(const char *ip) {
    if (!ip) return 0;
    uint32_t out = 0;
    int part = -1;    // -1 = noch nicht gestartet, sonst 0..255
    int dots = 0;

    for (const char *p = ip; ; ++p) {
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
    if (dots != 3) return 0; // genau 3 Punkte nötig
    return out; // Host-Order: a.b.c.d -> 0xAABBCCDD
}


// =============================================================================
// GLOBAL STATE
// =============================================================================
static network_config_t net_config;
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static uint16_t ip_identification = 0;

// =============================================================================
// Byte order helpers (declared in header; implementations provided elsewhere)
// =============================================================================
extern uint16_t htons(uint16_t host_short);
extern uint16_t ntohs(uint16_t net_short);
extern uint32_t htonl(uint32_t host_long);
extern uint32_t ntohl(uint32_t net_long);

// =============================================================================
// DHCP minimal definitions
// =============================================================================
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_DISCOVER    1
#define DHCP_OFFER       2
#define DHCP_REQUEST     3
#define DHCP_DECLINE     4
#define DHCP_ACK         5
#define DHCP_NAK         6

#define DHCP_MAGIC_COOKIE 0x63825363u

// DHCP options
#define DHO_MSG_TYPE     53
#define DHO_PARAM_REQ    55
#define DHO_SERVER_ID    54
#define DHO_REQ_IP       50
#define DHO_SUBNET       1
#define DHO_ROUTER       3
#define DHO_DNS          6
#define DHO_LEASE_TIME   51
#define DHO_END          255

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

// =============================================================================
// Public helpers required by header (match signatures)
// =============================================================================
void format_ipv4(uint32_t ip, char *buffer) {
    // ip is host order here
    sprintf(buffer, "%u.%u.%u.%u",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8)  & 0xFF,
            ip & 0xFF);
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

// ---- Checksummen (öffentliche Signatur aus Header) --------------------------
static uint32_t checksum_accumulate(const void *data, size_t len, uint32_t sum) {
    const uint8_t *p = (const uint8_t*)data;
    while (len > 1) {
        uint16_t w = ((uint16_t)p[0] << 8) | p[1];
        sum += w;
        p += 2; len -= 2;
    }
    if (len > 0) {
        // letztes ungerades Byte als High-Byte des 16-bit Wortes
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

// Optional UDP checksum (IPv4 erlaubt 0)
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
    return fold_checksum(sum);
}

// =============================================================================
// NIC dispatch (NE2000 only right now)
// =============================================================================
static inline bool nic_send(uint8_t *p, size_t n) {
    if (ne2000_is_initialized()) { ne2000_send_packet(p, (uint16_t)n); return true; }
    printf("[ETH] No NIC initialized\n");
    return false;
}
static inline int nic_recv(uint8_t *buf, size_t cap) {
    if (ne2000_is_initialized()) return ne2000_receive_packet(buf, (uint16_t)cap);
    return -1;
}

// =============================================================================
// ARP cache & protocol  (öffentliche Signaturen aus Header)
// =============================================================================
void arp_add_entry(uint32_t ip, uint8_t *mac) {
    int slot = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) { slot = i; break; }
        if (!arp_cache[i].valid && slot < 0) slot = i;
    }
    if (slot < 0) slot = 0; // FIFO/simplest
    arp_cache[slot].ip = ip;
    memcpy(arp_cache[slot].mac, mac, ETH_ADDR_LEN);
    arp_cache[slot].valid = true;
    arp_cache[slot].timestamp = 0; // TODO: aging
    char ip_s[16], mac_s[18];
    format_ipv4(ip, ip_s); format_mac(mac, mac_s);
    printf("[ARP] Add %s -> %s\n", ip_s, mac_s);
}

bool arp_lookup(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
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
    arp->hardware_type     = htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type     = htons(ARP_PROTOCOL_IPV4);
    arp->hardware_addr_len = ETH_ADDR_LEN;
    arp->protocol_addr_len = 4;
    arp->operation         = htons(ARP_REQUEST);

    memcpy(arp->sender_mac, net_config.mac_address, ETH_ADDR_LEN);
    uint32_t sip = net_config.ip_address ? net_config.ip_address : 0; // 0.0.0.0 before DHCP
    arp->sender_ip = htonl(sip);
    memset(arp->target_mac, 0, ETH_ADDR_LEN);
    arp->target_ip = htonl(target_ip);

    char ip_s[16]; format_ipv4(target_ip, ip_s);
    printf("[ARP] Request for %s\n", ip_s);
    nic_send(packet, sizeof(packet));
}

void arp_send_reply(uint32_t target_ip, uint8_t *target_mac) {
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

    char ip_s[16]; format_ipv4(target_ip, ip_s);
    printf("[ARP] Reply to %s\n", ip_s);
    nic_send(packet, sizeof(packet));
}

static void handle_arp_packet(uint8_t *packet, uint16_t length) {
    if (length < sizeof(arp_packet_t)) return;
    arp_packet_t *arp = (arp_packet_t *)packet;

    // Basic validity
    if (ntohs(arp->hardware_type) != ARP_HARDWARE_ETHERNET ||
        ntohs(arp->protocol_type) != ARP_PROTOCOL_IPV4 ||
        arp->hardware_addr_len   != ETH_ADDR_LEN ||
        arp->protocol_addr_len   != 4) return;

    uint16_t op   = ntohs(arp->operation);
    uint32_t sip  = ntohl(arp->sender_ip);
    uint32_t tip  = ntohl(arp->target_ip);

    arp_add_entry(sip, arp->sender_mac);

    if (op == ARP_REQUEST && tip == net_config.ip_address) {
        arp_send_reply(sip, arp->sender_mac);
    } else if (op == ARP_REPLY) {
        char ip_s[16]; format_ipv4(sip, ip_s);
        printf("[ARP] Reply from %s\n", ip_s);
    }
}

// =============================================================================
// IPv4/ICMP
// =============================================================================
void icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq, uint8_t *data, uint16_t data_len) {
    uint8_t packet[1514] = {0};
    eth_header_t  *eth  = (eth_header_t *)packet;
    ip_header_t   *ip   = (ip_header_t  *)(packet + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t*)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
    uint8_t       *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t);

    // Next hop (same subnet or gateway)
    uint32_t next_hop = (((dst_ip & net_config.netmask) == (net_config.ip_address & net_config.netmask)) || dst_ip==0xFFFFFFFFu)
                        ? dst_ip : net_config.gateway;

    uint8_t dst_mac[ETH_ADDR_LEN];
    if (dst_ip == 0xFFFFFFFFu) {
        memset(dst_mac, 0xFF, ETH_ADDR_LEN);
    } else if (!arp_lookup(next_hop, dst_mac)) {
        printf("[ICMP] No ARP entry; send ARP first\n");
        arp_send_request(next_hop);
        return;
    }

    // Ethernet
    memcpy(eth->dst_mac, dst_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_config.mac_address, ETH_ADDR_LEN);
    eth->ethertype = htons(ETHERTYPE_IPV4);

    // IP
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
    ip->header_checksum  = ip_checksum(ip, sizeof(ip_header_t));

    // ICMP
    icmp->type       = ICMP_ECHO_REPLY;
    icmp->code       = 0;
    icmp->identifier = htons(id);
    icmp->sequence   = htons(seq);
    icmp->checksum   = 0;

    if (data && data_len) memcpy(payload, data, data_len);
    icmp->checksum = ip_checksum(icmp, (uint16_t)(sizeof(icmp_header_t) + data_len));

    char dip[16]; format_ipv4(dst_ip, dip);
    printf("[ICMP] Echo reply -> %s (id=%u, seq=%u)\n", dip, id, seq);
    size_t total_len = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len;
    nic_send(packet, total_len);
}

static void handle_icmp_packet(uint8_t *packet, uint16_t length, uint32_t src_ip) {
    if (length < sizeof(icmp_header_t)) return;
    icmp_header_t *icmp = (icmp_header_t *)packet;
    uint8_t *data = packet + sizeof(icmp_header_t);
    uint16_t dlen = length - sizeof(icmp_header_t);

    if (icmp->type == ICMP_ECHO_REQUEST) {
        char s[16]; format_ipv4(src_ip, s);
        printf("[ICMP] Echo request from %s (id=%u, seq=%u)\n", s, ntohs(icmp->identifier), ntohs(icmp->sequence));
        icmp_send_echo_reply(src_ip, ntohs(icmp->identifier), ntohs(icmp->sequence), data, dlen);
    }
}

// =============================================================================
// UDP (send/recv low-level) – sufficient for DHCP
// =============================================================================
static int netstack_send_udp_low(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, void *data, size_t len, bool with_checksum) {
    if (sizeof(eth_header_t)+sizeof(ip_header_t)+sizeof(udp_header_t)+len > 1514) return -1;

    uint8_t packet[1514] = {0};
    uint8_t *ptr = packet;

    // choose next hop
    uint32_t next_hop = (((dst_ip & net_config.netmask) == (net_config.ip_address & net_config.netmask)) || dst_ip==0xFFFFFFFFu)
                        ? dst_ip : net_config.gateway;

    // Ethernet header
    uint8_t dst_mac[6];
    if (dst_ip == 0xFFFFFFFFu) {
        memset(dst_mac, 0xFF, 6); // broadcast
    } else if (!arp_lookup(next_hop, dst_mac)) {
        printf("[UDP] No ARP for next-hop; send ARP\n");
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
    ip->header_checksum  = ip_checksum(ip, sizeof(ip_header_t));
    ptr += sizeof(ip_header_t);

    // UDP
    udp_header_t *udp = (udp_header_t*)ptr;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons((uint16_t)(sizeof(udp_header_t) + len));
    udp->checksum = 0; // may remain 0 on IPv4
    ptr += sizeof(udp_header_t);

    memcpy(ptr, data, len);
    if (with_checksum) {
        udp->checksum = udp_checksum(ip, udp, (const uint8_t*)data, len);
    }
    ptr += len;

    size_t total_len = (size_t)(ptr - packet);
    return nic_send(packet, total_len) ? 0 : -1;
}

static int netstack_receive_udp_low(uint16_t port, void *buffer, size_t buflen, uint32_t *src_ip, uint16_t *src_port, int poll_count) {
    uint8_t pkt[1514];
    for (int i = 0; i < poll_count; ++i) {
        int len = nic_recv(pkt, sizeof(pkt));
        if (len <= 0) continue;
        if (len < 42) continue; // min eth+ip+udp

        // Ethernet
        uint16_t ethertype = (uint16_t)(pkt[12] << 8 | pkt[13]);
        if (ethertype != 0x0800) continue; // IPv4 only

        // IP
        ip_header_t *ip = (ip_header_t *)(pkt + 14);
        int ihl_bytes = (IP_IHL(ip)) * 4;
        if (ihl_bytes < (int)sizeof(ip_header_t) || (14 + ihl_bytes) > len) continue;
        if (ip->protocol != IP_PROTOCOL_UDP) continue;

        // verify IP checksum over IHL
        uint16_t saved = ip->header_checksum;
        ip->header_checksum = 0;
        uint16_t calc = ip_checksum(ip, (uint16_t)ihl_bytes);
        ip->header_checksum = saved;
        if (saved != calc) { printf("[IP] checksum mismatch\n"); continue; }

        udp_header_t *udp = (udp_header_t *)(pkt + 14 + ihl_bytes);
        if (ntohs(udp->dst_port) != port) continue;

        int udp_pl = (int)ntohs(udp->length) - (int)sizeof(udp_header_t);
        int avail  = len - (14 + ihl_bytes + (int)sizeof(udp_header_t));
        if (udp_pl < 0 || udp_pl > avail) continue;

        if (src_ip)   *src_ip   = ntohl(ip->src_ip);
        if (src_port) *src_port = ntohs(udp->src_port);

        int copy = udp_pl < (int)buflen ? udp_pl : (int)buflen;
        memcpy(buffer, pkt + 14 + ihl_bytes + sizeof(udp_header_t), (size_t)copy);
        return copy;
    }
    return -1;
}

// =============================================================================
// DHCP minimal client (blocking, simple timeouts via polls)
// =============================================================================
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
    // magic cookie
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

static bool dhcp_discover_request(uint32_t *out_ip, uint32_t *out_subnet, uint32_t *out_router, uint32_t *out_dns) {
    struct dhcp_packet pkt; memset(&pkt, 0, sizeof(pkt));
    pkt.op    = 1; // BOOTREQUEST
    pkt.htype = 1; // Ethernet
    pkt.hlen  = 6;
    pkt.hops  = 0;
    pkt.xid   = rng32();
    pkt.secs  = 0;
    pkt.flags = htons(0x8000); // broadcast reply
    memcpy(pkt.chaddr, net_config.mac_address, 6);

    // options
    uint8_t *opt = pkt.options;
    uint32_t mc = htonl(DHCP_MAGIC_COOKIE);
    memcpy(opt, &mc, 4); opt += 4;
    // Message Type: DISCOVER
    opt = dhcp_opt_put_u8(opt, DHO_MSG_TYPE, DHCP_DISCOVER);
    // Parameter Request List
    const uint8_t req[] = { DHO_SUBNET, DHO_ROUTER, DHO_DNS, DHO_LEASE_TIME, DHO_SERVER_ID };
    opt = dhcp_opt_put_list(opt, DHO_PARAM_REQ, req, sizeof(req));
    *opt++ = DHO_END;

    // Send DISCOVER to 255.255.255.255:67 from 0.0.0.0:68 (broadcast L2)
    printf("[DHCP] DISCOVER xid=0x%08x\n", (unsigned)pkt.xid);
    if (netstack_send_udp_low(0xFFFFFFFFu, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &pkt, sizeof(pkt), false) != 0) {
        printf("[DHCP] send DISCOVER failed\n");
        return false;
    }

    // Wait for OFFER
    struct dhcp_packet offer; uint32_t sip = 0; uint16_t sport = 0;
    int r = netstack_receive_udp_low(DHCP_CLIENT_PORT, &offer, sizeof(offer), &sip, &sport, /*polls*/ 512);
    if (r <= 0) { printf("[DHCP] no OFFER\n"); return false; }
    if (offer.op != 2 || offer.xid != pkt.xid) { printf("[DHCP] OFFER mismatch\n"); return false; }

    uint8_t mtype = 0; uint32_t sid_n=0, mask_n=0, gw_n=0, dns_n=0;
    if (!dhcp_parse_opts(&offer, &sid_n, &mask_n, &gw_n, &dns_n, &mtype) || mtype != DHCP_OFFER) {
        printf("[DHCP] invalid OFFER/options\n"); return false;
    }
    uint32_t yi = offer.yiaddr; // network order
    printf("[DHCP] OFFER yiaddr=%u.%u.%u.%u\n", ((uint8_t*)&yi)[0],((uint8_t*)&yi)[1],((uint8_t*)&yi)[2],((uint8_t*)&yi)[3]);

    // Build REQUEST
    struct dhcp_packet reqpkt; memset(&reqpkt, 0, sizeof(reqpkt));
    reqpkt.op=1; reqpkt.htype=1; reqpkt.hlen=6; reqpkt.xid=pkt.xid; reqpkt.flags=htons(0x8000);
    memcpy(reqpkt.chaddr, net_config.mac_address, 6);
    opt = reqpkt.options;
    memcpy(opt, &mc, 4); opt += 4;
    opt = dhcp_opt_put_u8 (opt, DHO_MSG_TYPE, DHCP_REQUEST);
    // Requested IP / Server ID from offer
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

    // Wait for ACK
    struct dhcp_packet ack; r = netstack_receive_udp_low(DHCP_CLIENT_PORT, &ack, sizeof(ack), &sip, &sport, 512);
    if (r <= 0 || ack.xid != pkt.xid) { printf("[DHCP] no ACK\n"); return false; }
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
// IP dispatch
// =============================================================================
static void handle_ip_packet(uint8_t *packet, uint16_t length) {
    if (length < sizeof(ip_header_t)) return;
    ip_header_t *ip = (ip_header_t *)packet;

    int ihl_bytes = (IP_IHL(ip)) * 4;
    if (ihl_bytes < (int)sizeof(ip_header_t) || ihl_bytes > (int)length) return;

    // verify checksum over IHL
    uint16_t saved = ip->header_checksum;
    ip->header_checksum = 0;
    uint16_t calc = ip_checksum(ip, (uint16_t)ihl_bytes);
    ip->header_checksum = saved;
    if (saved != calc) { printf("[IP] checksum mismatch -> drop\n"); return; }

    uint32_t dst = ntohl(ip->dst_ip);
    if (dst != net_config.ip_address && dst != 0xFFFFFFFFu) return;

    // drop fragments (no reassembly)
    uint16_t ff = ntohs(ip->flags_fragment);
    if (ff & 0x3FFF) { printf("[IP] fragment -> drop\n"); return; }

    uint8_t *payload = (uint8_t*)ip + ihl_bytes;
    uint16_t payload_len = (uint16_t)(ntohs(ip->total_length) - ihl_bytes);

    switch (ip->protocol) {
        case IP_PROTOCOL_ICMP:
            handle_icmp_packet(payload, payload_len, ntohl(ip->src_ip));
            break;
        case IP_PROTOCOL_UDP:
            // UDP wird über netstack_receive_udp_low konsumiert
            break;
        default:
            printf("[IP] proto=%u not handled\n", ip->protocol);
            break;
    }
}

// =============================================================================
// ETH dispatch
// =============================================================================
void netstack_process_packet(uint8_t *packet, uint16_t length) {
    if (length < sizeof(eth_header_t)) return;
    eth_header_t *eth = (eth_header_t *)packet;
    uint16_t type = ntohs(eth->ethertype);
    uint8_t *payload = packet + sizeof(eth_header_t);
    uint16_t plen = length - sizeof(eth_header_t);

    // Optional: filter destination MAC (own/broadcast)
    bool is_bcast = true;
    for (int i=0;i<6;++i) if (eth->dst_mac[i] != 0xFF) { is_bcast=false; break; }
    if (!is_bcast && memcmp(eth->dst_mac, net_config.mac_address, ETH_ADDR_LEN)!=0) {
        return; // not for us
    }

    switch (type) {
        case ETHERTYPE_ARP:  handle_arp_packet(payload, plen); break;
        case ETHERTYPE_IPV4: handle_ip_packet(payload, plen);  break;
        default: break;
    }
}

// =============================================================================
// Public API
// =============================================================================
void netstack_init(void) {
    printf("[NET] init...\n");
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) arp_cache[i].valid = false;

    if (ne2000_is_initialized()) {
        ne2000_get_mac_address(net_config.mac_address);
    } else {
        memset(net_config.mac_address, 0, ETH_ADDR_LEN);
    }

    net_config.ip_address = 0;
    net_config.netmask    = 0;
    net_config.gateway    = 0;
    net_config.dns_server = 0;

    char mac_s[18]; format_mac(net_config.mac_address, mac_s);
    printf("[NET] MAC=%s\n", mac_s);
}

void netstack_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    net_config.ip_address = ip;
    net_config.netmask    = netmask;
    net_config.gateway    = gateway;
    char ip_s[16]; format_ipv4(ip, ip_s);
    printf("[NET] IP configured: %s\n", ip_s);
}

uint32_t netstack_get_ip_address(void) {
    if (net_config.ip_address == 0) {
        uint32_t ip=0, mask=0, gw=0, dns=0;
        if (dhcp_discover_request(&ip, &mask, &gw, &dns)) {
            net_config.ip_address = ip;
            net_config.netmask    = mask;
            net_config.gateway    = gw;
            net_config.dns_server = dns;
            char ip_s[16], m_s[16], gw_s[16], dns_s[16];
            format_ipv4(ip, ip_s); format_ipv4(mask, m_s); format_ipv4(gw, gw_s); format_ipv4(dns, dns_s);
            printf("[DHCP] ACK IP=%s MASK=%s GW=%s DNS=%s\n", ip_s, m_s, gw_s, dns_s);
        } else {
            printf("[DHCP] failed; no IP\n");
        }
    }
    return net_config.ip_address;
}

// ICMP echo request (manual ping)
void icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq) {
    uint8_t data[4] = {'p','i','n','g'};
    uint8_t packet[1514] = {0};

    eth_header_t  *eth  = (eth_header_t *)packet;
    ip_header_t   *ip   = (ip_header_t  *)(packet + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t*)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
    uint8_t       *payload = packet + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t);

    uint32_t next_hop = (((dst_ip & net_config.netmask) == (net_config.ip_address & net_config.netmask)) || dst_ip==0xFFFFFFFFu)
                        ? dst_ip : net_config.gateway;
    uint8_t dst_mac[ETH_ADDR_LEN];
    if (dst_ip == 0xFFFFFFFFu) memset(dst_mac, 0xFF, ETH_ADDR_LEN);
    else if (!arp_lookup(next_hop, dst_mac)) { printf("[ICMP] ARP needed\n"); arp_send_request(next_hop); return; }

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
    ip->header_checksum  = ip_checksum(ip, sizeof(ip_header_t));

    icmp->type       = ICMP_ECHO_REQUEST;
    icmp->code       = 0;
    icmp->identifier = htons(id);
    icmp->sequence   = htons(seq);
    icmp->checksum   = 0;

    memcpy(payload, data, sizeof(data));
    icmp->checksum = ip_checksum(icmp, (uint16_t)(sizeof(icmp_header_t)+sizeof(data)));

    char dip[16]; format_ipv4(dst_ip, dip);
    printf("[ICMP] Echo request -> %s (id=%u, seq=%u)\n", dip, id, seq);

    size_t total_len = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t) + sizeof(data);
    nic_send(packet, total_len);
}

// Simple UDP API (send only, uses ARP/gateway/broadcast handling)
// Match header signature: data is non-const
int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t *data, uint16_t length) {
    return netstack_send_udp_low(dst_ip, src_port, dst_port, data, length, /*with_checksum*/false);
}

void udp_bind(uint16_t port, udp_callback_t cb) {
    (void)port; (void)cb;
    printf("[UDP] bind not implemented\n");
}

// TCP stubs (match header signatures)
int  tcp_connect(uint32_t dst_ip, uint16_t dst_port){ (void)dst_ip;(void)dst_port; printf("[TCP] connect n/i\n"); return -1; }
int  tcp_send(int socket, uint8_t *data, uint16_t length){ (void)socket;(void)data;(void)length; printf("[TCP] send n/i\n"); return -1; }
int  tcp_recv(int socket, uint8_t *buffer, uint16_t max_length){ (void)socket;(void)buffer;(void)max_length; printf("[TCP] recv n/i\n"); return -1; }
void tcp_close(int socket){ (void)socket; printf("[TCP] close n/i\n"); }

