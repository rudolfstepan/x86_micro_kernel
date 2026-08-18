/**
 * @file userspace/sdk/reist_dhcp_state.c
 * @brief Deadlinegebundene DHCP-Client-Zustandsmaschine.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#include "reist_dhcp_state.h"

#include <stddef.h>
#include <stdint.h>

static uint64_t deadline_after(uint64_t now_ms, uint32_t interval_ms) {
    return UINT64_MAX - now_ms < interval_ms
        ? UINT64_MAX : now_ms + interval_ms;
}

void reist_dhcp_state_init(reist_dhcp_state_t *state) {
    if (state == NULL) return;
    *state = (reist_dhcp_state_t){
        .version = REIST_DHCP_STATE_VERSION,
        .state = REIST_DHCP_STATE_IDLE,
    };
}

int reist_dhcp_state_configure(reist_dhcp_state_t *state, uint64_t now_ms,
                               uint32_t ip_address, uint32_t renew_after_ms,
                               uint32_t rebind_after_ms,
                               uint32_t expire_after_ms) {
    if (state == NULL || ip_address == 0U || ip_address == UINT32_MAX ||
        renew_after_ms == 0U || renew_after_ms >= rebind_after_ms ||
        rebind_after_ms >= expire_after_ms) return -22;
    *state = (reist_dhcp_state_t){
        .version = REIST_DHCP_STATE_VERSION,
        .state = REIST_DHCP_STATE_BOUND,
        .ip_address = ip_address,
        .renew_deadline_ms = deadline_after(now_ms, renew_after_ms),
        .rebind_deadline_ms = deadline_after(now_ms, rebind_after_ms),
        .expiry_deadline_ms = deadline_after(now_ms, expire_after_ms),
        .next_attempt_ms = deadline_after(now_ms, renew_after_ms),
    };
    return 0;
}

reist_dhcp_action_t reist_dhcp_state_poll(reist_dhcp_state_t *state,
                                          uint64_t now_ms) {
    if (state == NULL || state->version != REIST_DHCP_STATE_VERSION ||
        state->state == REIST_DHCP_STATE_IDLE ||
        state->state == REIST_DHCP_STATE_EXPIRED)
        return REIST_DHCP_ACTION_NONE;
    if (now_ms >= state->expiry_deadline_ms) {
        state->state = REIST_DHCP_STATE_EXPIRED;
        state->attempts = 0U;
        return REIST_DHCP_ACTION_NONE;
    }
    if (now_ms >= state->rebind_deadline_ms &&
        state->state != REIST_DHCP_STATE_REBINDING) {
        state->state = REIST_DHCP_STATE_REBINDING;
        state->attempts = 0U;
        state->next_attempt_ms = now_ms;
    } else if (now_ms >= state->renew_deadline_ms &&
               state->state == REIST_DHCP_STATE_BOUND) {
        state->state = REIST_DHCP_STATE_RENEWING;
        state->attempts = 0U;
        state->next_attempt_ms = now_ms;
    }
    if ((state->state != REIST_DHCP_STATE_RENEWING &&
         state->state != REIST_DHCP_STATE_REBINDING) ||
        state->attempts >= REIST_DHCP_MAX_ATTEMPTS_PER_PHASE ||
        now_ms < state->next_attempt_ms)
        return REIST_DHCP_ACTION_NONE;

    ++state->attempts;
    state->next_attempt_ms = deadline_after(
        now_ms, REIST_DHCP_RETRY_INTERVAL_MS);
    return state->state == REIST_DHCP_STATE_RENEWING
        ? REIST_DHCP_ACTION_RENEW : REIST_DHCP_ACTION_REBIND;
}
