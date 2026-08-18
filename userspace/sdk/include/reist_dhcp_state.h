/**
 * @file userspace/sdk/include/reist_dhcp_state.h
 * @brief DHCP-Zustands- und Leasevertrag.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#ifndef REIST_DHCP_STATE_H
#define REIST_DHCP_STATE_H

#include <stdint.h>

#define REIST_DHCP_STATE_VERSION 1U
#define REIST_DHCP_MAX_ATTEMPTS_PER_PHASE 3U
#define REIST_DHCP_RETRY_INTERVAL_MS 1000U

typedef enum {
    REIST_DHCP_STATE_IDLE = 0,
    REIST_DHCP_STATE_BOUND = 1,
    REIST_DHCP_STATE_RENEWING = 2,
    REIST_DHCP_STATE_REBINDING = 3,
    REIST_DHCP_STATE_EXPIRED = 4,
} reist_dhcp_state_kind_t;

typedef enum {
    REIST_DHCP_ACTION_NONE = 0,
    REIST_DHCP_ACTION_RENEW = 1,
    REIST_DHCP_ACTION_REBIND = 2,
} reist_dhcp_action_t;

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t ip_address;
    uint32_t attempts;
    uint64_t renew_deadline_ms;
    uint64_t rebind_deadline_ms;
    uint64_t expiry_deadline_ms;
    uint64_t next_attempt_ms;
} reist_dhcp_state_t;

void reist_dhcp_state_init(reist_dhcp_state_t *state);
int reist_dhcp_state_configure(reist_dhcp_state_t *state, uint64_t now_ms,
                               uint32_t ip_address, uint32_t renew_after_ms,
                               uint32_t rebind_after_ms,
                               uint32_t expire_after_ms);
reist_dhcp_action_t reist_dhcp_state_poll(reist_dhcp_state_t *state,
                                          uint64_t now_ms);

#endif
