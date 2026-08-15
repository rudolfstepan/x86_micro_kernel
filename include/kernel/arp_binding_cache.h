#ifndef KERNEL_ARP_BINDING_CACHE_H
#define KERNEL_ARP_BINDING_CACHE_H

#include "include/kernel/critical_object.h"

#include <stdint.h>

#define SUPERVISED_ARP_CACHE_SIZE 32U
#define SUPERVISED_ARP_LEASE_MS 30000U

typedef enum {
    SUPERVISED_ARP_MISS = 0,
    SUPERVISED_ARP_HIT = 1,
    SUPERVISED_ARP_BLOCKED = 2,
    SUPERVISED_ARP_INTEGRITY_FAILURE = -1,
} supervised_arp_lookup_result_t;

typedef struct {
    critical_object_t entries[SUPERVISED_ARP_CACHE_SIZE];
} supervised_arp_cache_t;

typedef struct {
    uint32_t scanned;
    uint32_t active;
    uint32_t expired;
    uint32_t newly_expired;
    uint32_t corrected;
} supervised_arp_scrub_stats_t;

int supervised_arp_cache_init(supervised_arp_cache_t *cache);
int supervised_arp_cache_commit(supervised_arp_cache_t *cache, uint32_t ip,
                                const uint8_t mac[6],
                                uint32_t transaction_epoch,
                                int32_t source_pid, uint32_t source_generation,
                                uint64_t now_ms,
                                uint32_t lease_ms);
int supervised_arp_cache_revoke_identity(supervised_arp_cache_t *cache,
                                         int32_t source_pid,
                                         uint32_t source_generation);
int supervised_arp_cache_revoke_ip(supervised_arp_cache_t *cache,
                                   uint32_t ip);
int supervised_arp_cache_scrub(supervised_arp_cache_t *cache, uint64_t now_ms,
                               supervised_arp_scrub_stats_t *stats_out);
supervised_arp_lookup_result_t supervised_arp_cache_lookup(
    supervised_arp_cache_t *cache, uint32_t ip, uint64_t now_ms,
    uint8_t mac_out[6]);

#ifdef REIST_HOST_TEST
int supervised_arp_cache_corrupt(supervised_arp_cache_t *cache, uint32_t slot,
                                 uint32_t copy, uint32_t word,
                                 uint32_t mask);
#endif

#endif
