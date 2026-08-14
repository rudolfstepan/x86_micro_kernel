#include "include/kernel/arp_learning_policy.h"

#include <stdio.h>

#define CHECK(condition, code) do { if (!(condition)) return (code); } while (0)

int main(void) {
    const uint32_t gateway = 0xC0A80101U;
    CHECK(!arp_learning_policy_allows_legacy(0U, 0U), 1);
    CHECK(arp_learning_policy_allows_legacy(gateway, 0U), 2);
    CHECK(!arp_learning_policy_allows_legacy(gateway, gateway), 3);
    CHECK(arp_learning_policy_allows_legacy(0xC0A80102U, gateway), 4);
    puts("ARP_LEARNING_POLICY_OK");
    return 0;
}
