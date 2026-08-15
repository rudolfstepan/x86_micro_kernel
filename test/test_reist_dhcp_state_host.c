#include "reist_dhcp_state.h"

#include <stdint.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void) {
    reist_dhcp_state_t state;
    reist_dhcp_state_init(&state);
    CHECK(state.version == REIST_DHCP_STATE_VERSION);
    CHECK(state.state == REIST_DHCP_STATE_IDLE);
    CHECK(reist_dhcp_state_configure(
        &state, 100U, 0x0A00020FU, 1000U, 3500U, 5000U) == 0);
    CHECK(reist_dhcp_state_poll(&state, 1099U) == REIST_DHCP_ACTION_NONE);
    CHECK(reist_dhcp_state_poll(&state, 1100U) == REIST_DHCP_ACTION_RENEW);
    CHECK(reist_dhcp_state_poll(&state, 1101U) == REIST_DHCP_ACTION_NONE);
    CHECK(reist_dhcp_state_poll(&state, 2100U) == REIST_DHCP_ACTION_RENEW);
    CHECK(reist_dhcp_state_poll(&state, 3100U) == REIST_DHCP_ACTION_RENEW);
    CHECK(reist_dhcp_state_poll(&state, 3500U) == REIST_DHCP_ACTION_NONE);
    CHECK(reist_dhcp_state_poll(&state, 3600U) == REIST_DHCP_ACTION_REBIND);
    CHECK(reist_dhcp_state_poll(&state, 5100U) == REIST_DHCP_ACTION_NONE);
    CHECK(state.state == REIST_DHCP_STATE_EXPIRED);

    CHECK(reist_dhcp_state_configure(
        &state, UINT64_MAX - 10U, 0x0A00020FU, 5U, 6U, 7U) == 0);
    CHECK(state.renew_deadline_ms == UINT64_MAX - 5U);
    CHECK(state.rebind_deadline_ms == UINT64_MAX - 4U);
    CHECK(state.expiry_deadline_ms == UINT64_MAX - 3U);
    CHECK(reist_dhcp_state_configure(
        &state, 0U, 0x0A00020FU, 3U, 2U, 4U) == -22);
    return 0;
}
