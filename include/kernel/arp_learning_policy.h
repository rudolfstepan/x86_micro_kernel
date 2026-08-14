#ifndef KERNEL_ARP_LEARNING_POLICY_H
#define KERNEL_ARP_LEARNING_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Gateway identity is safety-relevant and may only enter the protected cache
 * through the supervised Ring-3 ARP mediator.  Legacy learning remains a
 * compatibility mechanism for non-gateway peers until their path is moved. */
bool arp_learning_policy_allows_legacy(uint32_t candidate_ip,
                                       uint32_t configured_gateway);

#endif
