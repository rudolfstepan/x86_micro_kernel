#include "include/kernel/arp_binding_cache.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef REIST_HOST_TEST
#include "arch/x86/include/interrupt.h"
#endif

#define SUPERVISED_ARP_OBJECT_VERSION 1U

typedef enum {
    ARP_BINDING_EMPTY = 0,
    ARP_BINDING_VALID = 1,
    ARP_BINDING_EXPIRED = 2,
} arp_binding_state_t;

typedef struct {
    uint32_t state;
    uint32_t ip;
    uint8_t mac[6];
    uint8_t reserved[2];
    uint32_t transaction_epoch;
    int32_t source_pid;
    uint32_t source_generation;
    uint64_t deadline_ms;
} arp_binding_payload_t;

_Static_assert(sizeof(arp_binding_payload_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "ARP binding exceeds protected payload");

static uint32_t cache_lock(void) {
#ifdef REIST_HOST_TEST
    return 0U;
#else
    return irq_save();
#endif
}

static void cache_unlock(uint32_t flags) {
#ifdef REIST_HOST_TEST
    (void)flags;
#else
    irq_restore(flags);
#endif
}

static bool mac_valid(const uint8_t mac[6]) {
    if ((mac[0] & 1U) != 0U) return false;
    uint8_t nonzero = 0U;
    for (uint32_t i = 0U; i < 6U; ++i) nonzero |= mac[i];
    return nonzero != 0U;
}

static bool payload_valid(const void *data, size_t length) {
    if (data == NULL || length != sizeof(arp_binding_payload_t)) return false;
    const arp_binding_payload_t *payload = data;
    if (payload->reserved[0] != 0U || payload->reserved[1] != 0U) return false;
    if (payload->state == ARP_BINDING_EMPTY)
        return payload->ip == 0U && payload->transaction_epoch == 0U &&
               payload->source_pid == 0 &&
               payload->source_generation == 0U &&
               payload->deadline_ms == 0U;
    if (payload->state != ARP_BINDING_VALID &&
        payload->state != ARP_BINDING_EXPIRED) return false;
    return payload->ip != 0U && payload->transaction_epoch != 0U &&
           payload->source_pid > 0 && payload->source_generation != 0U &&
           mac_valid(payload->mac);
}

static int read_entry(critical_object_t *object,
                      arp_binding_payload_t *payload) {
    size_t length = 0U;
    critical_read_result_t result = critical_object_read(
        object, SUPERVISED_ARP_OBJECT_VERSION, payload, sizeof(*payload),
        &length, payload_valid);
    return result < 0 || length != sizeof(*payload) ? -1 : (int)result;
}

int supervised_arp_cache_init(supervised_arp_cache_t *cache) {
    if (cache == NULL) return -1;
    arp_binding_payload_t empty = {0};
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i)
        if (critical_object_init(&cache->entries[i],
                                 SUPERVISED_ARP_OBJECT_VERSION, &empty,
                                 sizeof(empty)) != 0) return -1;
    return 0;
}

int supervised_arp_cache_commit(supervised_arp_cache_t *cache, uint32_t ip,
                                const uint8_t mac[6],
                                uint32_t transaction_epoch,
                                int32_t source_pid, uint32_t source_generation,
                                uint64_t now_ms,
                                uint32_t lease_ms) {
    if (cache == NULL || ip == 0U || mac == NULL || transaction_epoch == 0U ||
        source_pid <= 0 || source_generation == 0U || lease_ms == 0U ||
        !mac_valid(mac)) return -1;
    uint32_t flags = cache_lock();
    arp_binding_payload_t entries[SUPERVISED_ARP_CACHE_SIZE];
    int selected = -1;
    int empty = -1;
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i) {
        if (read_entry(&cache->entries[i], &entries[i]) < 0) {
            cache_unlock(flags);
            return -2;
        }
        if (entries[i].state != ARP_BINDING_EMPTY && entries[i].ip == ip)
            selected = (int)i;
        if (entries[i].state == ARP_BINDING_EMPTY && empty < 0) empty = (int)i;
    }
    if (selected < 0) selected = empty;
    if (selected < 0) {
        cache_unlock(flags);
        return -2;
    }
    arp_binding_payload_t value = {0};
    value.state = ARP_BINDING_VALID;
    value.ip = ip;
    memcpy(value.mac, mac, sizeof(value.mac));
    value.transaction_epoch = transaction_epoch;
    value.source_pid = source_pid;
    value.source_generation = source_generation;
    value.deadline_ms = UINT64_MAX - now_ms < lease_ms
                            ? UINT64_MAX
                            : now_ms + lease_ms;
    int result = critical_object_update(&cache->entries[selected],
                                        SUPERVISED_ARP_OBJECT_VERSION, &value,
                                        sizeof(value), payload_valid) == 0
                     ? 0 : -2;
    cache_unlock(flags);
    return result;
}

int supervised_arp_cache_revoke_identity(supervised_arp_cache_t *cache,
                                         int32_t source_pid,
                                         uint32_t source_generation) {
    if (cache == NULL || source_pid <= 0 || source_generation == 0U) return -1;
    uint32_t flags = cache_lock();
    arp_binding_payload_t entries[SUPERVISED_ARP_CACHE_SIZE];
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i) {
        if (read_entry(&cache->entries[i], &entries[i]) < 0) {
            cache_unlock(flags);
            return -2;
        }
    }
    int revoked = 0;
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i) {
        if (entries[i].state != ARP_BINDING_VALID ||
            entries[i].source_pid != source_pid ||
            entries[i].source_generation != source_generation) continue;
        entries[i].state = ARP_BINDING_EXPIRED;
        if (critical_object_update(&cache->entries[i],
                                   SUPERVISED_ARP_OBJECT_VERSION, &entries[i],
                                   sizeof(entries[i]), payload_valid) != 0) {
            cache_unlock(flags);
            return -2;
        }
        ++revoked;
    }
    cache_unlock(flags);
    return revoked;
}

int supervised_arp_cache_scrub(supervised_arp_cache_t *cache, uint64_t now_ms,
                               supervised_arp_scrub_stats_t *stats_out) {
    if (cache == NULL || stats_out == NULL) return -1;
    uint32_t flags = cache_lock();
    arp_binding_payload_t entries[SUPERVISED_ARP_CACHE_SIZE];
    supervised_arp_scrub_stats_t stats = {0};
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i) {
        int result = read_entry(&cache->entries[i], &entries[i]);
        if (result < 0) {
            cache_unlock(flags);
            return -2;
        }
        ++stats.scanned;
        if (result == CRITICAL_READ_CORRECTED ||
            result == CRITICAL_READ_RECOVERED) ++stats.corrected;
    }
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i) {
        if (entries[i].state == ARP_BINDING_VALID &&
            now_ms >= entries[i].deadline_ms) {
            entries[i].state = ARP_BINDING_EXPIRED;
            if (critical_object_update(&cache->entries[i],
                                       SUPERVISED_ARP_OBJECT_VERSION,
                                       &entries[i], sizeof(entries[i]),
                                       payload_valid) != 0) {
                cache_unlock(flags);
                return -2;
            }
            ++stats.newly_expired;
        }
        if (entries[i].state == ARP_BINDING_VALID) ++stats.active;
        else if (entries[i].state == ARP_BINDING_EXPIRED) ++stats.expired;
    }
    *stats_out = stats;
    cache_unlock(flags);
    return 0;
}

supervised_arp_lookup_result_t supervised_arp_cache_lookup(
    supervised_arp_cache_t *cache, uint32_t ip, uint64_t now_ms,
    uint8_t mac_out[6]) {
    if (cache == NULL || ip == 0U || mac_out == NULL)
        return SUPERVISED_ARP_INTEGRITY_FAILURE;
    uint32_t flags = cache_lock();
    arp_binding_payload_t value;
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i) {
        if (read_entry(&cache->entries[i], &value) < 0) {
            cache_unlock(flags);
            return SUPERVISED_ARP_INTEGRITY_FAILURE;
        }
        if (value.state == ARP_BINDING_EMPTY || value.ip != ip) continue;
        if (value.state == ARP_BINDING_EXPIRED) {
            cache_unlock(flags);
            return SUPERVISED_ARP_BLOCKED;
        }
        if (now_ms >= value.deadline_ms) {
            value.state = ARP_BINDING_EXPIRED;
            if (critical_object_update(&cache->entries[i],
                                       SUPERVISED_ARP_OBJECT_VERSION, &value,
                                       sizeof(value), payload_valid) != 0) {
                cache_unlock(flags);
                return SUPERVISED_ARP_INTEGRITY_FAILURE;
            }
            cache_unlock(flags);
            return SUPERVISED_ARP_BLOCKED;
        }
        memcpy(mac_out, value.mac, sizeof(value.mac));
        cache_unlock(flags);
        return SUPERVISED_ARP_HIT;
    }
    cache_unlock(flags);
    return SUPERVISED_ARP_MISS;
}

#ifdef REIST_HOST_TEST
int supervised_arp_cache_corrupt(supervised_arp_cache_t *cache, uint32_t slot,
                                 uint32_t copy, uint32_t word, uint32_t mask) {
    if (cache == NULL || slot >= SUPERVISED_ARP_CACHE_SIZE ||
        copy > 1U || word >= CRITICAL_OBJECT_WORDS || mask == 0U) return -1;
    critical_object_copy_t *target = copy == 0U
        ? &cache->entries[slot].primary : &cache->entries[slot].shadow;
    target->words[word] ^= mask;
    return 0;
}
#endif
