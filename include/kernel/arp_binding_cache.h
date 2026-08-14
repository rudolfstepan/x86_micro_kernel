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

int supervised_arp_cache_init(supervised_arp_cache_t *cache);
int supervised_arp_cache_commit(supervised_arp_cache_t *cache, uint32_t ip,
                                const uint8_t mac[6], uint32_t source_epoch,
                                uint64_t now_ms, uint32_t lease_ms);
supervised_arp_lookup_result_t supervised_arp_cache_lookup(
    supervised_arp_cache_t *cache, uint32_t ip, uint64_t now_ms,
    uint8_t mac_out[6]);

#ifdef REIST_HOST_TEST
int supervised_arp_cache_corrupt(supervised_arp_cache_t *cache, uint32_t slot,
                                 uint32_t copy, uint32_t word,
                                 uint32_t mask);
#endif

#endif
