#include "include/kernel/handover.h"

#include <stddef.h>

#include "include/kernel/critical_object.h"

#ifndef REIST_HOST_TEST
#include "arch/x86/include/interrupt.h"
#endif

#define HANDOVER_EINVAL (-22)
#define HANDOVER_EACCES (-13)
#define HANDOVER_EAGAIN (-11)
#define HANDOVER_EINTEGRITY (-117)
#define HANDOVER_EOVERFLOW (-75)
#define HANDOVER_ENODEV (-19)

static critical_object_t protected_status;
static bool initialized;
static bool backend_attached;
static handover_fence_backend_t fence_backend;

_Static_assert(sizeof(handover_status_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "handover status exceeds protected payload");

static uint32_t handover_lock(void) {
#ifdef REIST_HOST_TEST
    return 0U;
#else
    return irq_save();
#endif
}

static void handover_unlock(uint32_t flags) {
#ifdef REIST_HOST_TEST
    (void)flags;
#else
    irq_restore(flags);
#endif
}

static uint64_t deadline_after(uint64_t now_ms, uint32_t lease_ms) {
    return UINT64_MAX - now_ms < lease_ms
        ? UINT64_MAX : now_ms + lease_ms;
}

static bool status_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(handover_status_t)) return false;
    const handover_status_t *status = payload;
    return status->version == HANDOVER_PROTOCOL_VERSION &&
           status->struct_size == sizeof(*status) &&
           status->active_node != 0U && status->standby_node != 0U &&
           status->active_node != status->standby_node &&
           status->lease_ms >= HANDOVER_MIN_LEASE_MS &&
           status->lease_ms <= HANDOVER_MAX_LEASE_MS &&
           status->reserved == 0U && status->epoch != 0U &&
           status->lease_deadline_ms != 0U &&
           (status->fenced_epoch == 0U ||
            status->fenced_epoch == status->epoch) &&
           status->transition_sequence != 0U;
}

static int load_status(handover_status_t *status) {
    size_t length = 0U;
    return critical_object_read(&protected_status, HANDOVER_PROTOCOL_VERSION,
        status, sizeof(*status), &length, status_valid) < 0 ||
        length != sizeof(*status) ? HANDOVER_EINTEGRITY : 0;
}

static int store_status(const handover_status_t *status) {
    return critical_object_update(&protected_status,
        HANDOVER_PROTOCOL_VERSION, status, sizeof(*status),
        status_valid) == 0 ? 0 : HANDOVER_EINTEGRITY;
}

int handover_attach_fence_backend(const handover_fence_backend_t *backend) {
    if (backend == NULL || backend_attached || initialized ||
        backend->request_fence == NULL || backend->fence_confirmed == NULL)
        return HANDOVER_EINVAL;
    fence_backend = *backend;
    backend_attached = true;
    return 0;
}

int handover_init(uint32_t active_node, uint32_t standby_node,
                  uint32_t lease_ms, uint64_t now_ms) {
    if (!backend_attached) return HANDOVER_ENODEV;
    if (initialized || active_node == 0U || standby_node == 0U ||
        active_node == standby_node || lease_ms < HANDOVER_MIN_LEASE_MS ||
        lease_ms > HANDOVER_MAX_LEASE_MS) return HANDOVER_EINVAL;
    handover_status_t status = {
        .version = HANDOVER_PROTOCOL_VERSION,
        .struct_size = sizeof(status),
        .active_node = active_node,
        .standby_node = standby_node,
        .lease_ms = lease_ms,
        .epoch = 1U,
        .lease_deadline_ms = deadline_after(now_ms, lease_ms),
        .transition_sequence = 1U,
    };
    uint32_t flags = handover_lock();
    int result = critical_object_init(&protected_status,
        HANDOVER_PROTOCOL_VERSION, &status, sizeof(status)) == 0
        ? 0 : HANDOVER_EINTEGRITY;
    if (result == 0) initialized = true;
    handover_unlock(flags);
    return result;
}

int handover_snapshot(handover_status_t *status_out) {
    if (!initialized || status_out == NULL) return HANDOVER_EINVAL;
    uint32_t flags = handover_lock();
    int result = load_status(status_out);
    handover_unlock(flags);
    return result;
}

int handover_renew(uint32_t node, uint64_t expected_epoch, uint64_t now_ms) {
    if (!initialized || node == 0U || expected_epoch == 0U)
        return HANDOVER_EINVAL;
    uint32_t flags = handover_lock();
    handover_status_t status = {0};
    int result = load_status(&status);
    if (result == 0 && (node != status.active_node ||
                       expected_epoch != status.epoch)) result = HANDOVER_EACCES;
    if (result == 0 && now_ms >= status.lease_deadline_ms)
        result = HANDOVER_EAGAIN;
    if (result == 0 && status.transition_sequence == UINT64_MAX)
        result = HANDOVER_EOVERFLOW;
    if (result == 0) {
        status.lease_deadline_ms = deadline_after(now_ms, status.lease_ms);
        ++status.transition_sequence;
        result = store_status(&status);
    }
    handover_unlock(flags);
    return result;
}

int handover_confirm_fenced(uint64_t expected_epoch, uint64_t now_ms) {
    if (!initialized || !backend_attached || expected_epoch == 0U)
        return HANDOVER_EINVAL;
    uint32_t flags = handover_lock();
    handover_status_t status = {0};
    int result = load_status(&status);
    if (result == 0 && expected_epoch != status.epoch)
        result = HANDOVER_EACCES;
    if (result == 0 && now_ms < status.lease_deadline_ms)
        result = HANDOVER_EAGAIN;
    uint32_t active_node = status.active_node;
    uint64_t sequence = status.transition_sequence;
    handover_unlock(flags);
    if (result != 0) return result;
    if (!fence_backend.fence_confirmed(fence_backend.context, active_node,
                                       expected_epoch))
        return HANDOVER_EAGAIN;

    flags = handover_lock();
    result = load_status(&status);
    if (result == 0 && (status.epoch != expected_epoch ||
                       status.active_node != active_node ||
                       status.transition_sequence != sequence ||
                       now_ms < status.lease_deadline_ms))
        result = HANDOVER_EAGAIN;
    if (result == 0 && status.fenced_epoch == status.epoch) {
        handover_unlock(flags);
        return 0;
    }
    if (result == 0 && status.transition_sequence == UINT64_MAX)
        result = HANDOVER_EOVERFLOW;
    if (result == 0) {
        status.fenced_epoch = status.epoch;
        ++status.transition_sequence;
        result = store_status(&status);
    }
    handover_unlock(flags);
    return result;
}

int handover_request_fence(uint64_t expected_epoch, uint64_t now_ms) {
    if (!initialized || !backend_attached || expected_epoch == 0U)
        return HANDOVER_EINVAL;
    uint32_t flags = handover_lock();
    handover_status_t status = {0};
    int result = load_status(&status);
    if (result == 0 && expected_epoch != status.epoch)
        result = HANDOVER_EACCES;
    if (result == 0 && now_ms < status.lease_deadline_ms)
        result = HANDOVER_EAGAIN;
    uint32_t active_node = status.active_node;
    handover_unlock(flags);
    if (result != 0) return result;
    return fence_backend.request_fence(fence_backend.context, active_node,
                                       expected_epoch)
        ? 0 : HANDOVER_EAGAIN;
}

int handover_takeover(uint32_t candidate_node, uint64_t expected_epoch,
                      uint64_t now_ms) {
    if (!initialized || candidate_node == 0U || expected_epoch == 0U)
        return HANDOVER_EINVAL;
    uint32_t flags = handover_lock();
    handover_status_t status;
    int result = load_status(&status);
    if (result == 0 && (candidate_node != status.standby_node ||
                       expected_epoch != status.epoch)) result = HANDOVER_EACCES;
    if (result == 0 && (now_ms < status.lease_deadline_ms ||
                       status.fenced_epoch != status.epoch))
        result = HANDOVER_EAGAIN;
    if (result == 0 && (status.epoch == UINT64_MAX ||
                       status.transition_sequence == UINT64_MAX))
        result = HANDOVER_EOVERFLOW;
    if (result == 0) {
        uint32_t previous_active = status.active_node;
        status.active_node = status.standby_node;
        status.standby_node = previous_active;
        ++status.epoch;
        ++status.transition_sequence;
        status.fenced_epoch = 0U;
        status.lease_deadline_ms = deadline_after(now_ms, status.lease_ms);
        result = store_status(&status);
    }
    handover_unlock(flags);
    return result;
}

#ifdef REIST_HOST_TEST
int handover_test_corrupt(bool both_copies) {
    if (!initialized) return HANDOVER_EINVAL;
    protected_status.primary.crc32 ^= 1U;
    if (both_copies) protected_status.shadow.crc32 ^= 2U;
    return 0;
}
#endif
