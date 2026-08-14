#include "include/kernel/arp_binding_cache.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, code) do { if (!(condition)) return (code); } while (0)

static int mac_equal(const uint8_t left[6], const uint8_t right[6]) {
    return memcmp(left, right, 6U) == 0;
}

int main(void) {
    supervised_arp_cache_t cache;
    uint8_t mac_a[6] = {0x02, 1, 2, 3, 4, 5};
    uint8_t mac_b[6] = {0x02, 6, 7, 8, 9, 10};
    uint8_t out[6] = {0};
    CHECK(supervised_arp_cache_init(&cache) == 0, 1);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 0U, out) ==
          SUPERVISED_ARP_MISS, 2);
    CHECK(supervised_arp_cache_commit(&cache, 1U, mac_a, 7U, 100U, 30U) == 0,
          3);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 129U, out) ==
          SUPERVISED_ARP_HIT && mac_equal(out, mac_a), 4);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 130U, out) ==
          SUPERVISED_ARP_BLOCKED, 5);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 999U, out) ==
          SUPERVISED_ARP_BLOCKED, 6);
    CHECK(supervised_arp_cache_commit(&cache, 1U, mac_b, 8U, 200U, 30U) == 0,
          7);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 201U, out) ==
          SUPERVISED_ARP_HIT && mac_equal(out, mac_b), 8);

    CHECK(supervised_arp_cache_corrupt(&cache, 0U, 0U, 5U, 1U) == 0, 9);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 201U, out) ==
          SUPERVISED_ARP_HIT && mac_equal(out, mac_b), 10);
    CHECK(supervised_arp_cache_corrupt(&cache, 0U, 0U, 5U, 3U) == 0, 11);
    CHECK(supervised_arp_cache_corrupt(&cache, 0U, 1U, 5U, 3U) == 0, 12);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 201U, out) ==
          SUPERVISED_ARP_INTEGRITY_FAILURE, 13);

    CHECK(supervised_arp_cache_init(&cache) == 0, 14);
    for (uint32_t i = 0U; i < SUPERVISED_ARP_CACHE_SIZE; ++i) {
        mac_a[5] = (uint8_t)(i + 1U);
        CHECK(supervised_arp_cache_commit(&cache, i + 1U, mac_a, i + 1U,
                                          i, 100U) == 0, 15);
    }
    mac_b[5] = 77U;
    CHECK(supervised_arp_cache_commit(&cache, 100U, mac_b, 100U, 1000U,
                                      100U) != 0, 16);
    CHECK(supervised_arp_cache_lookup(&cache, 1U, 1000U, out) ==
          SUPERVISED_ARP_BLOCKED, 17);

    CHECK(supervised_arp_cache_init(&cache) == 0, 18);
    CHECK(supervised_arp_cache_commit(&cache, 5U, mac_b, 1U,
                                      UINT64_MAX - 2U, 30U) == 0, 19);
    CHECK(supervised_arp_cache_lookup(&cache, 5U, UINT64_MAX - 1U, out) ==
          SUPERVISED_ARP_HIT, 20);
    CHECK(supervised_arp_cache_lookup(&cache, 5U, UINT64_MAX, out) ==
          SUPERVISED_ARP_BLOCKED, 21);

    uint8_t multicast[6] = {1, 2, 3, 4, 5, 6};
    uint8_t zero[6] = {0};
    CHECK(supervised_arp_cache_commit(&cache, 6U, multicast, 1U, 0U, 1U) != 0,
          22);
    CHECK(supervised_arp_cache_commit(&cache, 6U, zero, 1U, 0U, 1U) != 0,
          23);
    CHECK(supervised_arp_cache_commit(&cache, 6U, mac_b, 0U, 0U, 1U) != 0,
          24);
    puts("ARP_BINDING_CACHE_OK");
    return 0;
}
