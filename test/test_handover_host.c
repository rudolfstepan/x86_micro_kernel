#include "include/kernel/handover.h"

#include <stdint.h>

int main(void) {
    handover_status_t status;
    if (handover_init(1U, 2U, 100U, 1000U) != 0) return 1;
    if (handover_snapshot(&status) != 0 || status.active_node != 1U ||
        status.standby_node != 2U || status.epoch != 1U ||
        status.lease_deadline_ms != 1100U) return 2;

    /* The standby cannot take authority before expiry or without fencing. */
    if (handover_takeover(2U, 1U, 1099U) >= 0) return 3;
    if (handover_confirm_fenced(1U, 1099U) >= 0) return 4;
    if (handover_takeover(2U, 1U, 1100U) >= 0) return 5;
    if (handover_confirm_fenced(1U, 1100U) != 0) return 6;
    if (handover_confirm_fenced(1U, 1100U) != 0) return 16;
    if (handover_takeover(2U, 1U, 1100U) != 0) return 7;
    if (handover_snapshot(&status) != 0 || status.active_node != 2U ||
        status.standby_node != 1U || status.epoch != 2U ||
        status.fenced_epoch != 0U || status.lease_deadline_ms != 1200U)
        return 8;

    /* Stale epochs and the old active can never renew or retake authority. */
    if (handover_renew(1U, 1U, 1110U) >= 0) return 9;
    if (handover_renew(2U, 1U, 1110U) >= 0) return 10;
    if (handover_renew(2U, 2U, 1110U) != 0) return 11;
    if (handover_snapshot(&status) != 0 ||
        status.lease_deadline_ms != 1210U ||
        status.transition_sequence != 4U) return 12;
    if (handover_confirm_fenced(1U, 1210U) >= 0) return 13;

    /* One damaged copy self-recovers; two damaged copies fail closed. */
    if (handover_test_corrupt(false) != 0 ||
        handover_snapshot(&status) != 0) return 14;
    if (handover_test_corrupt(true) != 0 ||
        handover_snapshot(&status) >= 0) return 15;
    return 0;
}
