#include "include/kernel/arp_learning_policy.h"

bool arp_learning_policy_allows_legacy(uint32_t candidate_ip,
                                       uint32_t configured_gateway) {
    if (candidate_ip == 0U) return false;
    return configured_gateway == 0U || candidate_ip != configured_gateway;
}
